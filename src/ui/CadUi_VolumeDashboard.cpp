// Volume Dashboard (REQ-073's 2026-08-23 amendment, D-2026-08-23-k, TASK-095).
//
// A standing panel, unlike the one-shot report REQ-073's ORIGINAL (2026-08-12) text asked for: pick
// a Base and a Comparison surface, and the reported cut/fill/net/common-area stays live, recomputing
// automatically whenever either picked surface rebuilds — `TickVolumeDashboard` (`CadCommands.cpp`)
// reuses architecture §8's one-shot-worker contract, the same one REQ-069 already established for a
// surface's own dynamic rebuild, rather than a second staleness mechanism for the same problem shape.
//
// **ASSUMPTION-3 (TASK-095):** "Base" and "Comparison" are Civil 3D's own terms for this exact
// comparison — cut is Base sitting above Comparison (material to remove reaching it), fill is the
// reverse. Shown to the user on real data before being treated as settled, like TASK-086's own
// ASSUMPTION-1.
//
// **UI/session-only, never `.gs`** (REQ-073's own rule, matching REQ-075's Surface Manager): the
// panel's open state, its two picks, and its last result are never in `DrawingGeometrySnapshot`, in
// `DrawingDocument`, or written to a file.

#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "CadUiHelpers.hpp"
#include "NumFormat.hpp"

#include <imgui.h>

namespace {

/// A surface picker bound to a STABLE ENTITY ID (REQ-076), never an index or a name — the same
/// reason `SurfaceRebuildAsync` keys on id: a rename or an erase-then-recreate under the same name
/// must not silently repoint the dashboard at the wrong surface.
bool SurfacePicker(const char* comboId, const AppCommandState& cmd, std::uint64_t* pickedId) {
  const char* preview = "(none)";
  for (size_t si = 0; si < cmd.cadSurfaces.size(); ++si) {
    const std::uint64_t id = si < cmd.cadSurfaceAttrs.size() ? cmd.cadSurfaceAttrs[si].id : 0;
    if (id != 0 && id == *pickedId) {
      preview = cmd.cadSurfaces[si].name.c_str();
      break;
    }
  }
  bool changed = false;
  ImGui::SetNextItemWidth(-1);
  if (ImGui::BeginCombo(comboId, preview)) {
    for (size_t si = 0; si < cmd.cadSurfaces.size(); ++si) {
      const std::uint64_t id = si < cmd.cadSurfaceAttrs.size() ? cmd.cadSurfaceAttrs[si].id : 0;
      if (id == 0)
        continue;  // not yet swept an id this frame — transient, skip rather than let it be picked
      const bool sel = (id == *pickedId);
      if (ImGui::Selectable(cmd.cadSurfaces[si].name.c_str(), sel)) {
        *pickedId = id;
        changed = true;
      }
      if (sel)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return changed;
}

const ImVec4 kWaitingColor(0.95f, 0.75f, 0.35f, 1.f);
const ImVec4 kComputingColor(0.45f, 0.72f, 0.95f, 1.f);
const ImVec4 kErrorColor(1.f, 0.5f, 0.4f, 1.f);

} // namespace

void DrawVolumeDashboardWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  if (!cmd.volumeDashboard.open)
    return;

  AppCommandState::VolumeDashboardState& dash = cmd.volumeDashboard;

  ImGui::SetNextWindowSize(ImVec2(360.f, 320.f), ImGuiCond_FirstUseEver);
  bool open = dash.open;
  if (!ImGui::Begin("Volume Dashboard", &open)) {
    dash.open = open;
    ImGui::End();
    return;
  }
  dash.open = open;

  ImGui::TextUnformatted("Base");
  SurfacePicker("##vdbase", cmd, &dash.baseSurfaceId);
  ImGui::TextUnformatted("Comparison");
  SurfacePicker("##vdcomp", cmd, &dash.comparisonSurfaceId);

  ImGui::Spacing();
  ImGui::Checkbox("Show cut/fill map", &dash.showMap);
  ItemHelpTooltip("Colours the common area orange where Base sits above Comparison (cut) and blue "
                  "where Comparison sits above Base (fill). Nothing is drawn outside the common "
                  "area.");

  ImGui::Separator();
  ImGui::Spacing();

  if (dash.baseSurfaceId == 0 || dash.comparisonSurfaceId == 0) {
    ImGui::TextDisabled("Pick a Base and a Comparison surface.");
    ImGui::End();
    return;
  }

  const int baseIx = FindSurfaceIndexById(cmd, dash.baseSurfaceId);
  const int compIx = FindSurfaceIndexById(cmd, dash.comparisonSurfaceId);
  if (baseIx < 0 || compIx < 0) {
    ImGui::TextColored(kErrorColor, "A picked surface no longer exists.");
    ImGui::End();
    return;
  }

  // REQ-073: "picking a surface that is itself out of date (mid-rebuild) is reflected as such rather
  // than computing a volume against a stale triangulation" — this is TickVolumeDashboard's own gate,
  // read back here rather than re-decided, so the panel can never show a state the dispatcher
  // disagrees with.
  const SurfaceState baseState = SurfaceRebuildStateOf(cmd, static_cast<size_t>(baseIx));
  const SurfaceState compState = SurfaceRebuildStateOf(cmd, static_cast<size_t>(compIx));
  if (baseState != SurfaceState::Current || compState != SurfaceState::Current) {
    const bool baseWaiting = baseState != SurfaceState::Current;
    ImGui::TextColored(kWaitingColor, "Waiting for \"%s\" to finish rebuilding...",
                       (baseWaiting ? cmd.cadSurfaces[static_cast<size_t>(baseIx)]
                                    : cmd.cadSurfaces[static_cast<size_t>(compIx)])
                           .name.c_str());
    ImGui::End();
    return;
  }

  if (dash.job) {
    ImGui::TextColored(kComputingColor, "Computing...");
    ImGui::End();
    return;
  }

  if (!dash.hasResult) {
    ImGui::TextDisabled("No result yet.");
    ImGui::End();
    return;
  }

  // Current for the LANDED result's own pick/revision, which can differ from the panel's pick RIGHT
  // now for exactly one frame — the tick that changed it, before TickVolumeDashboard has run again.
  const bool stale = dash.resultForRevision != cmd.cadGpuRevision ||
                     dash.resultForBaseSurfaceId != dash.baseSurfaceId ||
                     dash.resultForComparisonSurfaceId != dash.comparisonSurfaceId;
  if (stale)
    ImGui::TextColored(kWaitingColor, "Out of date - recomputing...");

  const int p = cmd.displayLinearPrecision;
  const SurfaceVolumeResult& r = dash.lastResult;
  if (!r.overlapped) {
    ImGui::TextUnformatted("No common area between these surfaces.");
  } else if (ImGui::BeginTable("##vdresult", 2, ImGuiTableFlags_SizingStretchProp)) {
    const auto row = [](const char* label, const std::string& value) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(value.c_str());
    };
    row("Cut", FormatLinear(r.cutFt3, p) + " ft3");
    row("Fill", FormatLinear(r.fillFt3, p) + " ft3");
    row("Net", FormatLinear(r.netFt3, p) + " ft3");
    row("Common area", FormatLinear(r.commonAreaFt2, p) + " ft2");
    ImGui::EndTable();
  }

  ImGui::End();
}
