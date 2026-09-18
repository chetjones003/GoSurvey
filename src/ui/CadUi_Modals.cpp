// CadUi_Modals.cpp — small leaf dialogs split out of CadUi.cpp
// (TASK-150 Phase 2, GitHub issue #142): the DWG lossy-export warning, the
// close-confirm modal, the ALIGN results window, and the View Points panel.
//
// Each entry point is declared in CadUi.hpp and called from the main frame loop;
// they share only header-declared helpers and CadUiInternal.hpp.

#include "CadUi.hpp"
#include "CadUiInternal.hpp"
#include "CadUiChrome.hpp"
#include "CadCoordinateFrame.hpp"
#include "NumFormat.hpp"
#include "SurveyPoints.hpp"
#include "DwgIo.hpp"
#include "WinFileDialogs.hpp"
#include "StringUtil.hpp"

#include "imgui.h"
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

/// "~2m 30s remaining" / "~45s remaining" — never a negative or absurd figure: `seconds` is
/// clamped to a sane cap so an early, noisy fraction estimate (e.g. the first tick of phase 2,
/// before enough points have finalized to trust the extrapolation) does not print "~11 hours."
std::string FormatEtaRemaining(double seconds) {
  if (!(seconds > 0.0)) return "estimating...";
  seconds = std::min(seconds, 24.0 * 3600.0);
  const int totalSec = static_cast<int>(seconds);
  if (totalSec < 60) return "~" + std::to_string(totalSec) + "s remaining";
  const int mins = totalSec / 60;
  const int secs = totalSec % 60;
  return "~" + std::to_string(mins) + "m " + std::to_string(secs) + "s remaining";
}

}  // namespace

void DrawPointCloudImportProgress(AppCommandState& cmd) {
  if (!cmd.pointCloudImportAsync) return;
  // Once the worker is done, TickPointCloudImport (called earlier this frame, main.cpp) reaps and
  // clears this pointer before this draw call runs — so reaching here means an import is still
  // genuinely in flight. Never drawn for a job that already finished.

  const char* kTitle = "Importing Point Cloud";
  if (!ImGui::IsPopupOpen(kTitle))
    ImGui::OpenPopup(kTitle);

  // Centered every frame, same reasoning as DrawUpdateDialog: this dialog's only exit is its own
  // Cancel button, so it must never be able to drift somewhere the button is unreachable.
  ImGui::SetNextWindowSize(ImVec2(420.f, 0.f), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

  if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoResize))
    return;

  auto& job = *cmd.pointCloudImportAsync;
  const std::int64_t total = job.totalPointsEstimate;
  const std::int64_t streamed = job.pointsStreamed.load(std::memory_order_relaxed);
  const std::int64_t finalized = job.pointsFinalized.load(std::memory_order_relaxed);
  const std::int64_t leavesRead = job.previewLeavesRead.load(std::memory_order_relaxed);
  const std::int64_t totalLeaves = job.previewTotalLeaves.load(std::memory_order_relaxed);

  // Three real phases, in strict sequence (see pointcloudcache::BuildFromE57 and
  // RunPointCloudImportWorker): streaming completes fully before the recursive octree split
  // starts, which completes fully before the preview sample is read back. Phase 3 was invisible
  // before this fix — it re-reads the WHOLE cache off disk to subsample it, a real full pass with
  // no progress signal at all, which is exactly what read as "stuck at 100%" (TASK-270 log): the
  // bar itself was reporting phase 1+2 as 100% while the worker kept working on phase 3.
  const bool phase1Done = total > 0 && streamed >= total;
  const bool phase2Done = phase1Done && finalized >= total;
  const char* phaseLabel = "Reading scan...";
  if (phase2Done) phaseLabel = "Sampling preview...";
  else if (phase1Done) phaseLabel = "Building spatial index...";
  ImGui::TextUnformatted(phaseLabel);

  // Weighted by rough relative cost, not by point count: phase 2 re-reads/re-writes the dataset
  // across several octree levels (the slowest part), phase 3 is one more full read-back.
  float fraction = -1.f;  // negative = ImGui draws an indeterminate marquee
  if (total > 0) {
    const float p1 = std::min(1.f, static_cast<float>(streamed) / static_cast<float>(total));
    const float p2 = std::min(1.f, static_cast<float>(finalized) / static_cast<float>(total));
    const float p3 = totalLeaves > 0
                          ? std::min(1.f, static_cast<float>(leavesRead) / static_cast<float>(totalLeaves))
                          : 0.f;
    fraction = 0.35f * p1 + 0.45f * p2 + 0.20f * p3;
  }
  ImGui::ProgressBar(fraction, ImVec2(-1.f, 0.f));

  // Recomputed at most once a second — every-frame recomputation made the countdown jitter rather
  // than tick like a clock, which is what a reader expects from a "time remaining" line.
  const auto now = std::chrono::steady_clock::now();
  if (total > 0 &&
      std::chrono::duration<double>(now - job.lastEtaUpdate).count() >= 1.0) {
    const double elapsed = std::chrono::duration<double>(now - job.startTime).count();
    // Only trust the extrapolation once there is real progress to extrapolate FROM — an ETA
    // computed from the first half-percent of a multi-hour build is noise, not information.
    job.cachedEtaText =
        fraction > 0.005f ? FormatEtaRemaining(elapsed * (1.0 - fraction) / fraction) : "estimating...";
    job.lastEtaUpdate = now;
  }
  if (total > 0) ImGui::TextDisabled("%s", job.cachedEtaText.c_str());

  if (ImGui::Button("Cancel", ImVec2(120.f, 0.f)))
    job.cancelRequested.store(true, std::memory_order_release);

  ImGui::EndPopup();
}

void DrawDwgLossyExportModal(AppCommandState& cmd, std::vector<std::string>& log) {
  if (cmd.dwgLossyExportModal) {
    ImGui::OpenPopup("Export DWG##dwglossy");
    cmd.dwgLossyExportModal = false;
  }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Export DWG##dwglossy", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  const std::filesystem::path dst(cmd.dwgPendingExportPath);
  const bool overwriting = std::filesystem::exists(dst);

  ImGui::TextUnformatted("GoSurvey writes DWG with LibreDWG as AutoCAD 2000 (AC1015). This export drops:");
  ImGui::Spacing();
  ImGui::BulletText("hatches, ellipses, meshes, TIN surfaces, and dimensions");
  ImGui::BulletText("block definitions (inserts are exploded on import; not rebuilt on save)");
  ImGui::BulletText("paper-space layouts beyond model space");
  ImGui::BulletText("Civil 3D objects, proxies and anything else GoSurvey does not model");
  ImGui::Spacing();

  if (overwriting) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.45f, 0.20f, 1.0f));
    ImGui::TextWrapped("%s already exists. Overwriting it will permanently discard the data listed above.",
                       dst.filename().string().c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();
  }

  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button(overwriting ? "Overwrite" : "Export", ImVec2(120, 0))) {
    ExportDwgFile(cmd, cmd.dwgPendingExportPath.c_str(), log);
    cmd.dwgPendingExportPath.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) {
    cmd.dwgPendingExportPath.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void DrawCloseConfirmModal(AppCommandState& cmd, std::vector<std::string>& log) {
  if (cmd.confirmCloseModal) {
    ImGui::OpenPopup("Unsaved Changes##closeconf");
    cmd.confirmCloseModal = false;
  }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  PushProductDialogAccent();
  if (!ImGui::BeginPopupModal("Unsaved Changes##closeconf", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    PopProductDialogAccent();
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();

  // Collect dirty drawings: active doc first, then inactive snapshots.
  struct DirtyEntry { int idx; std::string name; };
  std::vector<DirtyEntry> dirty;
  if (cmd.cadGpuRevision != cmd.activeDocSavedRevision &&
      cmd.activeDrawingIdx < static_cast<int>(cmd.drawingTabs.size()))
    dirty.push_back({cmd.activeDrawingIdx, cmd.drawingTabs[cmd.activeDrawingIdx].name});
  for (int i = 0; i < static_cast<int>(cmd.documents.size()); ++i) {
    if (i == cmd.activeDrawingIdx) continue;
    if (cmd.documents[i].cadGpuRevision != cmd.documents[i].savedRevision &&
        i < static_cast<int>(cmd.drawingTabs.size()))
      dirty.push_back({i, cmd.drawingTabs[i].name});
  }

  if (dirty.empty()) {
    cmd.closeConfirmed = true;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    PopProductDialogAccent();
    return;
  }

  ImGui::TextUnformatted("The following drawings have unsaved changes:");
  ImGui::Spacing();
  for (const auto& e : dirty)
    ImGui::BulletText("%s", e.name.c_str());
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  static char s_savePath[4096]{};

  if (ImGui::Button("Save All & Close", ImVec2(0, 0))) {
    bool allOk = true;
    for (const auto& e : dirty) {
      const bool isActive = (e.idx == cmd.activeDrawingIdx);
      if (!isActive) {
        // Temporarily bring this doc's data into cmd.
        SaveDocumentToSnapshot(cmd, cmd.activeDrawingIdx);
        RestoreDocumentFromSnapshot(cmd, e.idx);
      }
      std::string path = cmd.activeDocFilePath;
      if (path.empty()) {
        const std::string def = e.name + ".dwg";
        if (BrowseSaveFileDwgUtf8(s_savePath, sizeof(s_savePath), def.c_str()))
          path = s_savePath;
      }
      if (!path.empty() && SaveDrawingDocument(cmd, path.c_str(), log)) {
        cmd.activeDocSavedRevision = cmd.cadGpuRevision;
        cmd.activeDocFilePath      = path;
        if (!isActive) {
          // Commit updated saved-revision back into the snapshot.
          SaveDocumentToSnapshot(cmd, e.idx);
        }
      } else {
        allOk = false;
      }
      if (!isActive)
        RestoreDocumentFromSnapshot(cmd, cmd.activeDrawingIdx);
    }
    if (allOk) {
      cmd.closeConfirmed = true;
      ImGui::CloseCurrentPopup();
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Close Without Saving", ImVec2(0, 0))) {
    cmd.closeConfirmed = true;
    ImGui::CloseCurrentPopup();
  }

  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(0, 0)))
    ImGui::CloseCurrentPopup();

  ImGui::EndPopup();
  PopProductDialogAccent();
}

void DrawAlignResultsWindow(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showAlignResultsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(600, 480), ImGuiCond_FirstUseEver);
  bool open = cmd.showAlignResultsWindow;
  if (!ImGui::Begin("ALIGN — Helmert transformation", &open, ImGuiWindowFlags_NoCollapse)) {
    cmd.showAlignResultsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showAlignResultsWindow = open;

  const auto& res = cmd.alignLastResult;

  // Solution summary
  if (res.valid) {
    ImGui::Text("Pairs: %d", res.nPairs);
    ImGui::SameLine(110.f);
    ImGui::Text("Scale: %.8f", static_cast<double>(res.scale));
    ImGui::Text("Rotation:     %s", FormatBearing(static_cast<double>(res.rotationCwNorthDeg), CadAngleDisplaySettings(cmd)).c_str());
    ImGui::Text("Translation:  X = %s   Y = %s",
                FormatLinear(static_cast<double>(res.tx), cmd.displayLinearPrecision).c_str(),
                FormatLinear(static_cast<double>(res.ty), cmd.displayLinearPrecision).c_str());
    ImGui::Text("Point error:  %s (avg. distance each source maps from its destination)",
                FormatLinear(static_cast<double>(res.rms), cmd.displayLinearPrecision).c_str());
  } else if (cmd.alignControlPts.empty()) {
    ImGui::TextColored(ImVec4(1.f, 0.5f, 0.5f, 1.f), "No pairs — add control pairs and solve again.");
  } else {
    ImGui::TextColored(ImVec4(1.f, 0.5f, 0.5f, 1.f), "Degenerate — pairs are coincident or collinear.");
  }

  ImGui::Separator();

  // Pairs table with per-row Remove button
  const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.f;
  const float tableH  = std::max(60.f, ImGui::GetContentRegionAvail().y - footerH);
  const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
  int removeIdx = -1;
  if (ImGui::BeginTable("##align_pairs", 7, tf, ImVec2(0.f, tableH))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##rm",  ImGuiTableColumnFlags_WidthFixed,   26.f);
    ImGui::TableSetupColumn("Pair",  ImGuiTableColumnFlags_WidthFixed,   36.f);
    ImGui::TableSetupColumn("Src X", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Src Y", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Dst X", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Dst Y", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Resid", ImGuiTableColumnFlags_WidthFixed,   72.f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor();

    for (int i = 0; i < static_cast<int>(cmd.alignControlPts.size()); ++i) {
      const auto& cp    = cmd.alignControlPts[static_cast<size_t>(i)];
      const float resid = (res.valid && i < static_cast<int>(res.pairResiduals.size()))
                              ? res.pairResiduals[static_cast<size_t>(i)] : 0.f;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(i);
      if (ImGui::SmallButton("-"))
        removeIdx = i;
      ImGui::PopID();
      const int pp = cmd.displayLinearPrecision;
      ImGui::TableNextColumn(); ImGui::Text("%d", i + 1);
      ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatLinear(static_cast<double>(cp.srcX), pp).c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatLinear(static_cast<double>(cp.srcY), pp).c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatLinear(static_cast<double>(cp.dstX), pp).c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatLinear(static_cast<double>(cp.dstY), pp).c_str());
      ImGui::TableNextColumn();
      if (res.valid) ImGui::TextUnformatted(FormatLinear(static_cast<double>(resid), pp).c_str());
      else ImGui::TextUnformatted("—");
    }
    ImGui::EndTable();
  }

  if (removeIdx >= 0) {
    cmd.alignControlPts.erase(cmd.alignControlPts.begin() + removeIdx);
    RecalcAlignResult(cmd);
  }

  ImGui::Separator();
  static bool s_applyScale = true;
  ImGui::Checkbox("Apply scale", &s_applyScale);
  ImGui::SameLine();
  const bool canApply = res.valid;
  if (!canApply)
    ImGui::BeginDisabled();
  if (ImGui::Button("Apply", ImVec2(90.f, 0.f)))
    ApplyAlignCommand(cmd, log, s_applyScale);
  if (!canApply)
    ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Close", ImVec2(70.f, 0.f))) {
    cmd.showAlignResultsWindow = false;
    cmd.alignControlPts.clear();
    cmd.alignPhase = AppCommandState::AlignPhase::PickSrc;
  }

  ImGui::End();
}

void DrawViewPointsPanel(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showViewPointsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(960, 480), ImGuiCond_FirstUseEver);
  bool open = cmd.showViewPointsWindow;
  PushProductDialogAccent();
  if (!ImGui::Begin("Viewpoints — survey database", &open)) {
    cmd.showViewPointsWindow = open;
    ImGui::End();
    PopProductDialogAccent();
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();
  cmd.showViewPointsWindow = open;

  cmd.surveyPointIdBuffers.resize(cmd.surveyPoints.size());
  for (size_t i = 0; i < cmd.surveyPoints.size(); ++i) {
    if (cmd.surveyPointIdBuffers[i].empty())
      cmd.surveyPointIdBuffers[i] = std::to_string(cmd.surveyPoints[i].id);
  }

  ImGui::Text("%zu point(s)", cmd.surveyPoints.size());
  static char pathBuf[512] = "gosurvey_points.json";
  ImGui::InputText("File##vp_path", pathBuf, sizeof(pathBuf));
  if (StyledButton("Save##vp", ImVec2(0, 0), /*primary=*/true))
    SaveSurveyPointsToJsonFile(cmd, pathBuf, log);
  ImGui::SameLine();
  if (StyledButton("Load##vp", ImVec2(0, 0), /*primary=*/true))
    LoadSurveyPointsFromJsonFile(cmd, pathBuf, log);

  ImGui::Separator();

  // Second line of defence for BUG-023. The resize at the top of this function
  // runs BEFORE the Load button, so any control above that mutates the point list
  // leaves the buffers short for the rest of the same frame. Re-checking here,
  // immediately before the rows are read, makes that impossible by construction
  // rather than by every such control remembering to do it.
  if (cmd.surveyPointIdBuffers.size() != cmd.surveyPoints.size()) {
    const size_t was = cmd.surveyPointIdBuffers.size();
    cmd.surveyPointIdBuffers.resize(cmd.surveyPoints.size());
    for (size_t i = was; i < cmd.surveyPoints.size(); ++i)
      cmd.surveyPointIdBuffers[i] = std::to_string(cmd.surveyPoints[i].id);
  }

  int pendingDelete = -1;
  if (ImGui::BeginTable("survey_pts", 7, kGridTableFlags, ImVec2(0.f, -ImGui::GetFrameHeightWithSpacing()))) {
    ImGui::TableSetupScrollFreeze(0, 1);  // header stays put while the rows scroll
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 64.f);
    ImGui::TableSetupColumn("Easting", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Northing", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Elev", ImGuiTableColumnFlags_WidthFixed, 84.f);
    ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 56.f);
    ImGui::TableHeadersRow();

    // Sorting reorders a VIEW, never `cmd.surveyPoints` itself. The vector's order
    // is the identity of a point everywhere else — `surveyPointIdBuffers` is a
    // parallel array indexed by it, labels are looked up by it, and delete takes
    // an index — so sorting the storage to sort the display would silently
    // rewire all of that. `order` is the only thing that moves.
    static std::vector<size_t> order;
    order.resize(cmd.surveyPoints.size());
    for (size_t k = 0; k < order.size(); ++k)
      order[k] = k;
    if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
      if (ss->SpecsCount > 0) {
        const auto& pts = cmd.surveyPoints;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
          for (int s = 0; s < ss->SpecsCount; ++s) {
            const ImGuiTableColumnSortSpecs& sp = ss->Specs[s];
            int c = 0;
            switch (sp.ColumnIndex) {
              case 0: c = (pts[a].id < pts[b].id) ? -1 : (pts[a].id > pts[b].id) ? 1 : 0; break;
              case 1: c = (pts[a].easting < pts[b].easting) ? -1 : (pts[a].easting > pts[b].easting) ? 1 : 0; break;
              case 2: c = (pts[a].northing < pts[b].northing) ? -1 : (pts[a].northing > pts[b].northing) ? 1 : 0; break;
              case 3: c = (pts[a].elevation < pts[b].elevation) ? -1 : (pts[a].elevation > pts[b].elevation) ? 1 : 0; break;
              case 4: c = pts[a].layer.compare(pts[b].layer); break;
              case 5: c = pts[a].description.compare(pts[b].description); break;
              default: break;
            }
            if (c != 0)
              return sp.SortDirection == ImGuiSortDirection_Ascending ? c < 0 : c > 0;
          }
          return a < b;  // stable tie-break, so equal rows never shuffle between frames
        });
      }
    }

    PushGridCellStyle();
    for (size_t k = 0; k < order.size(); ++k) {
      const size_t i = order[k];
      SurveyPoint& p = cmd.surveyPoints[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(i));
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputText("##id", &cmd.surveyPointIdBuffers[i]);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string t = StringUtil::trimCopy(cmd.surveyPointIdBuffers[i]);
        char* end = nullptr;
        const long v = std::strtol(t.c_str(), &end, 10);
        const bool parsed =
            end == t.c_str() + static_cast<std::ptrdiff_t>(t.size()) && end != t.c_str();
        if (!parsed) {
          log.push_back("VIEWPOINTS — ID must be a whole number (no spaces or extra text).");
          cmd.surveyPointIdBuffers[i] = std::to_string(p.id);
        } else {
          const int nid = static_cast<int>(v);
          bool dup = false;
          for (size_t j = 0; j < cmd.surveyPoints.size(); ++j) {
            if (j != i && cmd.surveyPoints[j].id == nid)
              dup = true;
          }
          if (dup) {
            log.push_back("VIEWPOINTS — duplicate ID " + std::to_string(nid) + ".");
            cmd.surveyPointIdBuffers[i] = std::to_string(p.id);
          } else {
            p.id = nid;
            cmd.surveyPointIdBuffers[i] = std::to_string(nid);
          }
        }
        EnsureSurveyPointLabelMtext(cmd, i, &log);
      }
      ImGui::TableNextColumn();
      double de = static_cast<double>(CadCoord::WorldXFromLocal(cmd, p.easting));
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputDouble("##e", &de, 0., 0., DisplayFloatFmt(cmd.surveyPointDisplayPrecision).c_str());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const double wy = static_cast<double>(CadCoord::WorldYFromLocal(cmd, p.northing));
        CadCoord::LocalFromWorld(cmd, de, wy, &p.easting, &p.northing);
        EnsureSurveyPointLabelMtext(cmd, i, &log);
      }
      ImGui::TableNextColumn();
      double dn = static_cast<double>(CadCoord::WorldYFromLocal(cmd, p.northing));
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputDouble("##n", &dn, 0., 0., DisplayFloatFmt(cmd.surveyPointDisplayPrecision).c_str());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        const double wx = static_cast<double>(CadCoord::WorldXFromLocal(cmd, p.easting));
        CadCoord::LocalFromWorld(cmd, wx, dn, &p.easting, &p.northing);
        EnsureSurveyPointLabelMtext(cmd, i, &log);
      }
      ImGui::TableNextColumn();
      double dz = static_cast<double>(p.elevation);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputDouble("##z", &dz, 0., 0., DisplayFloatFmt(cmd.surveyPointDisplayPrecision).c_str());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        p.elevation = dz;
        EnsureSurveyPointLabelMtext(cmd, i, &log);
      }
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputText("##layer", &p.layer);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        RepositionSurveyLabelMtextForPoint(cmd, i);
        BumpCadGpuCache(cmd);
      }
      ImGui::TableNextColumn();
      // Single line, not multiline: a 52px-tall description cell made every row
      // three times the height of its own text, which is the main reason this
      // read as a form rather than a sheet. A description is one line of text.
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputText("##desc", &p.description);
      if (ImGui::IsItemDeactivatedAfterEdit())
        EnsureSurveyPointLabelMtext(cmd, i, &log);
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("X"))
        pendingDelete = static_cast<int>(i);
      ImGui::PopID();
    }
    PopGridCellStyle();
    ImGui::EndTable();
  }

  if (pendingDelete >= 0)
    RemoveSurveyPointAt(cmd, static_cast<size_t>(pendingDelete));

  ImGui::End();
  PopProductDialogAccent();
}
