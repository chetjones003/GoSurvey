#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "PaperSpace.hpp"
#include "PdfPlot.hpp"
#include "WinFileDialogs.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

namespace {

void PaperSizeCombo(const char* id, int* presetIdx, float* portraitW, float* portraitH) {
  const char* label = (*presetIdx >= 0 && *presetIdx < kPaperSizePresetCount)
                          ? kPaperSizePresets[*presetIdx].name
                          : "Custom";
  if (ImGui::BeginCombo(id, label)) {
    for (int s = 0; s < kPaperSizePresetCount; ++s) {
      if (ImGui::Selectable(kPaperSizePresets[s].name, *presetIdx == s)) {
        *presetIdx = s;
        *portraitW = kPaperSizePresets[s].widthIn;
        *portraitH = kPaperSizePresets[s].heightIn;
      }
    }
    ImGui::EndCombo();
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Viewports manager (moved off the status bar into its own window).
// ---------------------------------------------------------------------------
void DrawViewportsWindow(AppCommandState& cmd, std::vector<std::string>& log) {
  (void)log;
  if (!cmd.showViewportsWindow)
    return;
  ImGui::SetNextWindowSize(ImVec2(360, 380), ImGuiCond_FirstUseEver);
  bool open = cmd.showViewportsWindow;
  if (!ImGui::Begin("Viewports", &open)) {
    cmd.showViewportsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showViewportsWindow = open;
  if (cmd.activeSpaceIndex < 0 || cmd.activeSpaceIndex >= static_cast<int>(cmd.paperLayouts.size())) {
    ImGui::TextDisabled("Switch to a paper layout to manage its viewports.");
    ImGui::End();
    return;
  }
  const int layoutIdx = cmd.activeSpaceIndex;
  PaperLayout& PL = cmd.paperLayouts[static_cast<size_t>(layoutIdx)];
  ImGui::TextDisabled("Layout \"%s\"", PL.name.c_str());
  if (ImGui::Button("+ Add viewport"))
    AddViewport(cmd, layoutIdx);
  ImGui::Separator();
  for (int vi = 0; vi < static_cast<int>(PL.viewports.size()); ++vi) {
    ImGui::PushID(vi);
    char lbl[32];
    std::snprintf(lbl, sizeof(lbl), "Viewport %d", vi + 1);
    if (ImGui::Selectable(lbl, IsViewportSelected(cmd, vi)))
      SelectViewport(cmd, vi, /*additive=*/false);
    ImGui::PopID();
  }
  if (cmd.selectedViewportLayout == layoutIdx && cmd.selectedViewportIndex >= 0 &&
      cmd.selectedViewportIndex < static_cast<int>(PL.viewports.size())) {
    Viewport& V = PL.viewports[static_cast<size_t>(cmd.selectedViewportIndex)];
    ImGui::Separator();
    ImGui::TextDisabled("Selected viewport (paper inches / model units)");
    bool ch = false;
    ImGui::SetNextItemWidth(90.f);
    ch |= ImGui::InputFloat("X##vpx", &V.paperXIn, 0.f, 0.f, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.f);
    ch |= ImGui::InputFloat("Y##vpy", &V.paperYIn, 0.f, 0.f, "%.3f");
    ImGui::SetNextItemWidth(90.f);
    ch |= ImGui::InputFloat("W##vpw", &V.paperWIn, 0.f, 0.f, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.f);
    ch |= ImGui::InputFloat("H##vph", &V.paperHIn, 0.f, 0.f, "%.3f");
    ImGui::SetNextItemWidth(140.f);
    ch |= ImGui::InputFloat("Scale (model/in)##vps", &V.scaleModelPerPaperIn, 0.f, 0.f, "%.4f");
    ImGui::SetNextItemWidth(120.f);
    ch |= ImGui::InputDouble("Center X##vpcx", &V.modelCenterX, 0., 0., "%.4f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    ch |= ImGui::InputDouble("Center Y##vpcy", &V.modelCenterY, 0., 0., "%.4f");
    ImGui::SetNextItemWidth(150.f);
    if (ImGui::BeginCombo("Layer##vplayer", V.layer.c_str())) {
      for (const CadLayerRow& lr : cmd.drawingLayerTable)
        if (ImGui::Selectable(lr.name.c_str(), V.layer == lr.name)) {
          V.layer = lr.name;
          ch = true;
        }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(border omitted from plots if layer not plottable)");
    if (V.paperWIn < 0.1f) V.paperWIn = 0.1f;
    if (V.paperHIn < 0.1f) V.paperHIn = 0.1f;
    if (ch)
      BumpCadGpuCache(cmd);
    if (ImGui::Button("Delete viewport"))
      DeleteViewport(cmd, layoutIdx, cmd.selectedViewportIndex);
  }
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Plotting (REQ-029/030).
// ---------------------------------------------------------------------------
void PlotActiveLayout(AppCommandState& cmd, std::vector<std::string>& log) {
  if (cmd.activeSpaceIndex < 0 || cmd.activeSpaceIndex >= static_cast<int>(cmd.paperLayouts.size())) {
    log.push_back("PLOT — switch to a paper layout first.");
    return;
  }
  const std::string def = cmd.paperLayouts[static_cast<size_t>(cmd.activeSpaceIndex)].name + ".pdf";
  char path[1024] = {0};
  if (!BrowseSaveFilePdfUtf8(path, sizeof(path), def.c_str()))
    return;
  PlotLayoutsToPdf(cmd, {cmd.activeSpaceIndex}, path, log);
}

void DrawBatchPlotDialog(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showBatchPlotDialog)
    return;
  ImGui::OpenPopup("Batch Plot");
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_Appearing);
  bool open = true;
  if (ImGui::BeginPopupModal("Batch Plot", &open, ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::TextUnformatted("Select layouts to plot (one page each, in order):");
    ImGui::BeginChild("bp_list", ImVec2(0, 240), true);
    for (int i = 0; i < static_cast<int>(cmd.paperLayouts.size()); ++i) {
      bool sel = std::find(cmd.batchPlotSelected.begin(), cmd.batchPlotSelected.end(), i) !=
                 cmd.batchPlotSelected.end();
      if (ImGui::Checkbox(cmd.paperLayouts[static_cast<size_t>(i)].name.c_str(), &sel)) {
        if (sel)
          cmd.batchPlotSelected.push_back(i);
        else
          cmd.batchPlotSelected.erase(
              std::remove(cmd.batchPlotSelected.begin(), cmd.batchPlotSelected.end(), i),
              cmd.batchPlotSelected.end());
      }
    }
    ImGui::EndChild();
    ImGui::Spacing();
    const bool any = !cmd.batchPlotSelected.empty();
    if (!any) ImGui::BeginDisabled();
    if (ImGui::Button("Plot to PDF…", ImVec2(140, 0))) {
      char path[1024] = {0};
      if (BrowseSaveFilePdfUtf8(path, sizeof(path), "plot.pdf")) {
        std::sort(cmd.batchPlotSelected.begin(), cmd.batchPlotSelected.end());
        PlotLayoutsToPdf(cmd, cmd.batchPlotSelected, path, log);
        cmd.showBatchPlotDialog = false;
        ImGui::CloseCurrentPopup();
      }
    }
    if (!any) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) {
      cmd.showBatchPlotDialog = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (!open)
    cmd.showBatchPlotDialog = false;
}

// ---------------------------------------------------------------------------
// Move or Copy (reorder / duplicate a layout).
// ---------------------------------------------------------------------------
void DrawMoveCopyLayoutDialog(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showMoveCopyLayout)
    return;
  ImGui::OpenPopup("Move or Copy");
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(380, 360), ImGuiCond_Appearing);
  bool open = true;
  if (ImGui::BeginPopupModal("Move or Copy", &open, ImGuiWindowFlags_NoSavedSettings)) {
    const int n = static_cast<int>(cmd.paperLayouts.size());
    ImGui::TextUnformatted("Move or copy selected layout");
    ImGui::Spacing();
    ImGui::TextUnformatted("Before layout:");
    ImGui::BeginChild("mc_list", ImVec2(0, 200), true);
    for (int i = 0; i < n; ++i) {
      if (i == cmd.pageSetupLayoutIdx && !cmd.moveCopyCreateCopy)
        continue;  // can't move before itself (when moving)
      if (ImGui::Selectable(cmd.paperLayouts[static_cast<size_t>(i)].name.c_str(), cmd.moveCopyBeforeSel == i))
        cmd.moveCopyBeforeSel = i;
    }
    if (ImGui::Selectable("(move to end)", cmd.moveCopyBeforeSel == n))
      cmd.moveCopyBeforeSel = n;
    ImGui::EndChild();
    ImGui::Checkbox("Create a copy", &cmd.moveCopyCreateCopy);

    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(110, 0))) {
      MoveOrCopyLayout(cmd, cmd.pageSetupLayoutIdx, cmd.moveCopyBeforeSel, cmd.moveCopyCreateCopy, log);
      cmd.showMoveCopyLayout = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) {
      cmd.showMoveCopyLayout = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (!open)
    cmd.showMoveCopyLayout = false;
}

// ---------------------------------------------------------------------------
// Page Setup Manager.
// ---------------------------------------------------------------------------
void DrawPageSetupManager(AppCommandState& cmd, std::vector<std::string>& log) {
  (void)log;
  if (!cmd.showPageSetupManager)
    return;
  EnsureStandardPageSetup(cmd);
  ImGui::OpenPopup("Page Setup Manager");
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 460), ImGuiCond_Appearing);
  bool open = true;
  const int layoutIdx = cmd.pageSetupLayoutIdx;
  const bool validLayout = layoutIdx >= 0 && layoutIdx < static_cast<int>(cmd.paperLayouts.size());
  if (ImGui::BeginPopupModal("Page Setup Manager", &open, ImGuiWindowFlags_NoSavedSettings)) {
    if (!validLayout) {
      ImGui::TextDisabled("No layout selected.");
    } else {
      PaperLayout& L = cmd.paperLayouts[static_cast<size_t>(layoutIdx)];
      ImGui::Text("Current layout:   %s", L.name.c_str());
      ImGui::Separator();
      ImGui::TextUnformatted("Page setups");
      ImGui::Text("Current page setup:   %s", L.pageSetupName.empty() ? "<None>" : L.pageSetupName.c_str());

      // -1 row = the layout's own setup (shown as *name*); >=0 rows = saved named setups.
      ImGui::BeginChild("ps_list", ImVec2(360, 240), true);
      {
        char cur[80];
        std::snprintf(cur, sizeof(cur), "*%s*", L.name.c_str());
        if (ImGui::Selectable(cur, cmd.pageSetupManagerSel == -1))
          cmd.pageSetupManagerSel = -1;
        for (int i = 0; i < static_cast<int>(cmd.savedPageSetups.size()); ++i)
          if (ImGui::Selectable(cmd.savedPageSetups[static_cast<size_t>(i)].name.c_str(),
                                cmd.pageSetupManagerSel == i))
            cmd.pageSetupManagerSel = i;
      }
      ImGui::EndChild();
      ImGui::SameLine();
      ImGui::BeginGroup();
      const bool selSaved = cmd.pageSetupManagerSel >= 0 &&
                            cmd.pageSetupManagerSel < static_cast<int>(cmd.savedPageSetups.size());
      if (ImGui::Button("Set Current", ImVec2(110, 0)) && selSaved) {
        ApplyPageSetupToLayout(L, cmd.savedPageSetups[static_cast<size_t>(cmd.pageSetupManagerSel)]);
        BumpCadGpuCache(cmd);
      }
      if (ImGui::Button("New...", ImVec2(110, 0))) {
        std::snprintf(cmd.newPageSetupName, sizeof(cmd.newPageSetupName), "Setup%d",
                      static_cast<int>(cmd.savedPageSetups.size()) + 1);
        cmd.newPageSetupStartWith = 3;  // *layout* by default
        cmd.showNewPageSetup = true;
        cmd.showPageSetupManager = false;  // close first (modals don't nest cleanly); reopened on close
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::Button("Modify...", ImVec2(110, 0))) {
        cmd.pageSetupEditorTarget = cmd.pageSetupManagerSel;  // -1 layout current, else saved index
        cmd.pageSetupEditorDraft =
            selSaved ? cmd.savedPageSetups[static_cast<size_t>(cmd.pageSetupManagerSel)]
                     : PageSetupFromLayout(L, L.pageSetupName.empty() ? std::string("*") + L.name + "*"
                                                                      : L.pageSetupName);
        cmd.showPageSetupEditor = true;
        cmd.showPageSetupManager = false;  // close first; reopened when the editor closes
        ImGui::CloseCurrentPopup();
      }
      ImGui::BeginDisabled();
      ImGui::Button("Import...", ImVec2(110, 0));
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Import page setups from another drawing — coming in a later update.");
      ImGui::EndGroup();

      ImGui::Separator();
      ImGui::TextUnformatted("Selected page setup details");
      const PageSetup detail =
          selSaved ? cmd.savedPageSetups[static_cast<size_t>(cmd.pageSetupManagerSel)] : PageSetupFromLayout(L, "");
      const char* paperName = (detail.presetIdx >= 0 && detail.presetIdx < kPaperSizePresetCount)
                                  ? kPaperSizePresets[detail.presetIdx].name
                                  : "Custom";
      const float w = detail.landscape ? std::max(detail.portraitWidthIn, detail.portraitHeightIn)
                                       : std::min(detail.portraitWidthIn, detail.portraitHeightIn);
      const float h = detail.landscape ? std::min(detail.portraitWidthIn, detail.portraitHeightIn)
                                       : std::max(detail.portraitWidthIn, detail.portraitHeightIn);
      ImGui::Text("Device name:   GoSurvey PDF (DWG To PDF)");
      ImGui::Text("Plotter:       PDF (vector) — built in");
      ImGui::Text("Plot size:     %.2f x %.2f inches (%s)", w, h, detail.landscape ? "Landscape" : "Portrait");
      ImGui::Text("Paper:         %s", paperName);
      ImGui::Text("Where:         File");
    }

    ImGui::Separator();
    ImGui::Checkbox("Display when creating a new layout", &cmd.pageSetupDisplayOnNew);
    ImGui::SameLine(ImGui::GetWindowWidth() - 130.f);
    if (ImGui::Button("Close", ImVec2(110, 0))) {
      cmd.showPageSetupManager = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (!open)
    cmd.showPageSetupManager = false;
}

// ---------------------------------------------------------------------------
// New Page Setup (name + "start with").
// ---------------------------------------------------------------------------
void DrawNewPageSetupDialog(AppCommandState& cmd, std::vector<std::string>& log) {
  (void)log;
  if (!cmd.showNewPageSetup)
    return;
  ImGui::OpenPopup("New Page Setup");
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_Appearing);
  bool open = true;
  const bool validLayout =
      cmd.pageSetupLayoutIdx >= 0 && cmd.pageSetupLayoutIdx < static_cast<int>(cmd.paperLayouts.size());
  if (ImGui::BeginPopupModal("New Page Setup", &open, ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::TextUnformatted("New page setup name:");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##newpsname", cmd.newPageSetupName, sizeof(cmd.newPageSetupName));
    ImGui::Spacing();
    ImGui::TextUnformatted("Start with:");
    // Built-in starting points, then *layout* and each saved setup.
    std::vector<std::string> items = {"<None>", "<Default output device>", "<Previous plot>"};
    int layoutRow = -1;
    if (validLayout) {
      layoutRow = static_cast<int>(items.size());
      items.push_back("*" + cmd.paperLayouts[static_cast<size_t>(cmd.pageSetupLayoutIdx)].name + "*");
    }
    const int savedBase = static_cast<int>(items.size());
    for (const PageSetup& p : cmd.savedPageSetups)
      items.push_back(p.name);
    cmd.newPageSetupStartWith = std::clamp(cmd.newPageSetupStartWith, 0, static_cast<int>(items.size()) - 1);
    ImGui::BeginChild("nps_list", ImVec2(0, 200), true);
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
      if (ImGui::Selectable(items[static_cast<size_t>(i)].c_str(), cmd.newPageSetupStartWith == i))
        cmd.newPageSetupStartWith = i;
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(110, 0))) {
      PageSetup ps;
      ps.name = cmd.newPageSetupName[0] ? cmd.newPageSetupName : "Setup";
      const int sw = cmd.newPageSetupStartWith;
      if (sw == layoutRow && validLayout)
        ps = PageSetupFromLayout(cmd.paperLayouts[static_cast<size_t>(cmd.pageSetupLayoutIdx)], ps.name);
      else if (sw >= savedBase && (sw - savedBase) < static_cast<int>(cmd.savedPageSetups.size())) {
        ps = cmd.savedPageSetups[static_cast<size_t>(sw - savedBase)];
        ps.name = cmd.newPageSetupName[0] ? cmd.newPageSetupName : ps.name;
      }
      // (<None>/<Default>/<Previous> → plain defaults)
      cmd.savedPageSetups.push_back(ps);
      cmd.pageSetupManagerSel = static_cast<int>(cmd.savedPageSetups.size()) - 1;
      cmd.showNewPageSetup = false;
      cmd.showPageSetupManager = true;  // back to the manager
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) {
      cmd.showNewPageSetup = false;
      cmd.showPageSetupManager = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (!open) {
    cmd.showNewPageSetup = false;
    cmd.showPageSetupManager = true;
  }
}

// ---------------------------------------------------------------------------
// Page Setup editor (Modify) — AutoCAD-style layout; functional fields wired, the
// rest shown disabled (real plotting device/styles arrive with PDF plotting).
// ---------------------------------------------------------------------------
void DrawPageSetupEditor(AppCommandState& cmd, std::vector<std::string>& log) {
  (void)log;
  if (!cmd.showPageSetupEditor)
    return;
  ImGui::OpenPopup("Page Setup");
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_Appearing);
  bool open = true;
  if (ImGui::BeginPopupModal("Page Setup", &open, ImGuiWindowFlags_NoSavedSettings)) {
    PageSetup& ps = cmd.pageSetupEditorDraft;
    ImGui::Text("Page setup:   %s", ps.name.c_str());
    ImGui::Separator();

    // Printer/plotter (fixed PDF device for now).
    ImGui::TextUnformatted("Printer / plotter");
    ImGui::BeginDisabled();
    char dev[64] = "GoSurvey PDF (DWG To PDF)";
    ImGui::SetNextItemWidth(360.f);
    ImGui::InputText("Name##device", dev, sizeof(dev), ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Plotter selection / PDF options arrive with PDF plotting (a later update).");
    ImGui::TextDisabled("Plotter:  PDF (vector) — built in        Where:  File");
    ImGui::Separator();

    // Paper size.
    ImGui::TextUnformatted("Paper size");
    ImGui::SetNextItemWidth(360.f);
    PaperSizeCombo("##editpaper", &ps.presetIdx, &ps.portraitWidthIn, &ps.portraitHeightIn);
    ImGui::Separator();

    // Plot area + offset (left column) and plot scale (right column).
    ImGui::Columns(2, "pseditcols", false);
    ImGui::TextUnformatted("Plot area");
    ImGui::SetNextItemWidth(140.f);
    const char* areaItems[] = {"Layout"};
    ImGui::Combo("What to plot", &ps.plotArea, areaItems, 1);
    ImGui::Spacing();
    ImGui::TextUnformatted("Plot offset (inches)");
    ImGui::SetNextItemWidth(90.f);
    ImGui::InputFloat("X##psox", &ps.offsetXIn, 0.f, 0.f, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.f);
    ImGui::InputFloat("Y##psoy", &ps.offsetYIn, 0.f, 0.f, "%.3f");
    ImGui::Checkbox("Center the plot", &ps.centerPlot);

    ImGui::NextColumn();
    ImGui::TextUnformatted("Plot scale");
    ImGui::Checkbox("Fit to paper", &ps.fitToPaper);
    ImGui::BeginDisabled(ps.fitToPaper);
    ImGui::SetNextItemWidth(150.f);
    ImGui::InputFloat("model units / inch", &ps.scaleModelPerPaperIn, 0.f, 0.f, "%.4f");
    if (ps.scaleModelPerPaperIn < 1e-4f) ps.scaleModelPerPaperIn = 1e-4f;
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextUnformatted("Drawing orientation");
    int orient = ps.landscape ? 1 : 0;
    ImGui::RadioButton("Portrait", &orient, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Landscape", &orient, 1);
    ps.landscape = (orient == 1);
    ImGui::Columns(1);

    ImGui::Separator();
    // Plot options / styles — placeholders for the plotting increment.
    ImGui::BeginDisabled();
    static bool plOptLW = true, plOptTr = true, plOptPsLast = true;
    ImGui::Checkbox("Plot object lineweights", &plOptLW);
    ImGui::SameLine();
    ImGui::Checkbox("Plot transparency", &plOptTr);
    ImGui::SameLine();
    ImGui::Checkbox("Plot paperspace last", &plOptPsLast);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Plot styles / options apply once PDF plotting lands (a later update).");

    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(110, 0))) {
      if (cmd.pageSetupEditorTarget >= 0 &&
          cmd.pageSetupEditorTarget < static_cast<int>(cmd.savedPageSetups.size())) {
        cmd.savedPageSetups[static_cast<size_t>(cmd.pageSetupEditorTarget)] = ps;  // edited a saved setup
      } else if (cmd.pageSetupLayoutIdx >= 0 &&
                 cmd.pageSetupLayoutIdx < static_cast<int>(cmd.paperLayouts.size())) {
        ApplyPageSetupToLayout(cmd.paperLayouts[static_cast<size_t>(cmd.pageSetupLayoutIdx)], ps);  // layout's own
      }
      BumpCadGpuCache(cmd);
      cmd.showPageSetupEditor = false;
      cmd.showPageSetupManager = true;  // back to the manager
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) {
      cmd.showPageSetupEditor = false;
      cmd.showPageSetupManager = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (!open) {
    cmd.showPageSetupEditor = false;
    cmd.showPageSetupManager = true;
  }
}
