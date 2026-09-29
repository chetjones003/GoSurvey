// Drawing Settings window (REQ-357, GitHub issue #582 increment 1): the drawing's units, scale and
// settings. The window edits a staged copy; only OK and Apply write it to the drawing, through
// ApplyDrawingSettings (one undo step). The Transformation and Object Layers tabs, and the Zone
// group, arrive with REQ-358..REQ-361 and are shown greyed until then (REQ-084).

#include "CadUi.hpp"
#include "CadUiHelpers.hpp"
#include "util/PlotScales.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr const char* kPopupId = "###GoSurveyDrawingSettings";
constexpr const char* kNotImplemented = "Not implemented yet.";

struct StagedDrawingSettings {
  int             insUnits = 2;
  float           modelUnitsPerPlottedInch = 50.f;
  bool            customScale = false;
  char            customScaleText[32] = {};
  DrawingSettings settings;
};

void SeedFromDrawing(const AppCommandState& cmd, StagedDrawingSettings& s) {
  s.insUnits = cmd.drawingInsUnits;
  s.modelUnitsPerPlottedInch = cmd.modelUnitsPerPlottedInch;
  s.customScale = PlotScaleChoiceIndex(PlotScaleChoicesFor(s.insUnits), s.modelUnitsPerPlottedInch) < 0;
  std::snprintf(s.customScaleText, sizeof(s.customScaleText), "%g",
                static_cast<double>(s.modelUnitsPerPlottedInch));
  s.settings = cmd.drawingSettings;
}

/// The staged scale, or false when Custom holds something that is not a positive number.
bool StagedScale(const StagedDrawingSettings& s, float* out) {
  if (!s.customScale) {
    *out = s.modelUnitsPerPlottedInch;
    return true;
  }
  char* end = nullptr;
  const double v = std::strtod(s.customScaleText, &end);
  while (end != nullptr && *end == ' ')
    ++end;
  if (end == s.customScaleText || end == nullptr || *end != '\0' || !std::isfinite(v) || v <= 0.0)
    return false;
  *out = static_cast<float>(v);
  return true;
}

void GreyedWithReason(const char* reason) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", reason);
}

void DrawUnitsAndZoneTab(StagedDrawingSettings& s) {
  const float labelW = 230.f;
  const float fieldW = 260.f;
  auto row = [&](const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(fieldW);
  };

  int unitSel = 0;
  for (int i = 0; i < kDrawingUnitCount; ++i)
    if (s.insUnits == kDrawingUnitCodes[i])
      unitSel = i;
  row("Drawing units:");
  if (ImGui::Combo("##ds_units", &unitSel, kDrawingUnitNames, kDrawingUnitCount)) {
    s.insUnits = kDrawingUnitCodes[std::clamp(unitSel, 0, kDrawingUnitCount - 1)];
    // A relabel: the scale value is kept, and reads as Custom if the new list does not hold it.
    if (!s.customScale &&
        PlotScaleChoiceIndex(PlotScaleChoicesFor(s.insUnits), s.modelUnitsPerPlottedInch) < 0) {
      s.customScale = true;
      std::snprintf(s.customScaleText, sizeof(s.customScaleText), "%g",
                    static_cast<double>(s.modelUnitsPerPlottedInch));
    }
  }
  ItemHelpTooltip("The drawing's unit (INSUNITS). A relabel only: no coordinate changes. "
                  "The same value the UNITS dialog shows.");

  const char* kAngular[] = {"Degrees", "Radians", "Grads"};
  int ang = static_cast<int>(s.settings.angularUnits);
  row("Angular units:");
  if (ImGui::Combo("##ds_ang", &ang, kAngular, IM_ARRAYSIZE(kAngular)))
    s.settings.angularUnits = static_cast<DrawingSettings::AngularUnits>(std::clamp(ang, 0, 2));
  ItemHelpTooltip("The drawing's angular unit (AUNITS). The app-wide angle display format is set in UNITS.");

  const char* kFoot[] = {"US Survey Foot (1 m = 39.37 in)", "International Foot (1 ft = 0.3048 m)"};
  int foot = static_cast<int>(s.settings.footDefinition);
  row("Imperial to Metric conversion:");
  if (ImGui::Combo("##ds_foot", &foot, kFoot, IM_ARRAYSIZE(kFoot)))
    s.settings.footDefinition = static_cast<DrawingSettings::FootDefinition>(std::clamp(foot, 0, 1));
  ItemHelpTooltip("The foot every feet/meters conversion in this drawing uses. The two differ by "
                  "2 parts per million: about 1 ft at 500,000 ft.");

  const std::vector<PlotScaleChoice> choices = PlotScaleChoicesFor(s.insUnits);
  const int listIx = s.customScale ? -1 : PlotScaleChoiceIndex(choices, s.modelUnitsPerPlottedInch);
  const std::string preview = listIx >= 0 ? choices[static_cast<size_t>(listIx)].label : std::string("Custom");
  row("Scale:");
  if (ImGui::BeginCombo("##ds_scale", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
    for (size_t i = 0; i < choices.size(); ++i) {
      if (ImGui::Selectable(choices[i].label.c_str(), static_cast<int>(i) == listIx)) {
        s.customScale = false;
        s.modelUnitsPerPlottedInch = choices[i].modelUnitsPerPlottedInch;
      }
    }
    if (ImGui::Selectable("Custom", listIx < 0))
      s.customScale = true;
    ImGui::EndCombo();
  }
  ItemHelpTooltip("The drawing's plot scale: the same value as the status-bar scale. It sizes survey-point "
                  "markers, labels and plotted text.");

  ImGui::BeginDisabled(!s.customScale);
  row("Custom scale:");
  ImGui::InputText("##ds_custom", s.customScaleText, sizeof(s.customScaleText));
  ImGui::EndDisabled();
  ItemHelpTooltip("Drawing units per plotted inch (e.g. 50 for 1\" = 50' in a feet drawing).");
  float scale = 0.f;
  if (s.customScale && !StagedScale(s, &scale))
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.f), "The custom scale must be a positive number.");

  ImGui::Spacing();
  ImGui::Checkbox("Scale objects inserted from other drawings", &s.settings.scaleInsertedObjects);
  ItemHelpTooltip("On: INSERT converts a block drawn in another unit to this drawing's unit. "
                  "Off: blocks are inserted as drawn.");
  ImGui::Checkbox("Set drawing variables to match", &s.settings.setDrawingVariables);
  ItemHelpTooltip("On: saving writes LUNITS and AUNITS from these settings beside INSUNITS.");

  ImGui::Spacing();
  ImGui::SeparatorText("Zone");
  ImGui::BeginDisabled();
  row("Categories:");
  const char* kNoZone[] = {"No Datum, No Projection"};
  int zone = 0;
  ImGui::Combo("##ds_zone", &zone, kNoZone, IM_ARRAYSIZE(kNoZone));
  ImGui::EndDisabled();
  GreyedWithReason("Coordinate-system zones: not implemented yet (issue #582).");
}

}  // namespace

void DrawDrawingSettingsWindow(AppCommandState& cmd, std::vector<std::string>& log) {
  static StagedDrawingSettings staged;
  static bool wasOpen = false;

  if (!cmd.showDrawingSettingsWindow) {
    wasOpen = false;
    return;
  }
  if (cmd.activeDrawingIdx == 0) {  // REQ-308: the Start tab owns no drawing
    cmd.showDrawingSettingsWindow = false;
    wasOpen = false;
    log.push_back("DRAWINGSETTINGS — open or create a drawing first.");
    return;
  }
  if (!wasOpen) {
    SeedFromDrawing(cmd, staged);
    wasOpen = true;
  }

  const std::string name = cmd.activeDrawingIdx < static_cast<int>(cmd.drawingTabs.size())
                               ? cmd.drawingTabs[static_cast<size_t>(cmd.activeDrawingIdx)].name
                               : std::string("Drawing");
  const std::string title = "Drawing Settings - " + name + kPopupId;
  if (!ImGui::IsPopupOpen(kPopupId))  // "###" makes the id independent of the drawing name
    ImGui::OpenPopup(title.c_str());

  ImGui::SetNextWindowSize(ImVec2(620.f, 470.f), ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  PushProductDialogAccent();
  bool open = true;
  if (!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
    PopProductDialogAccent();
    cmd.showDrawingSettingsWindow = false;  // closed by the title-bar [X]: Cancel
    wasOpen = false;
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();

  bool close = !open || ImGui::IsKeyPressed(ImGuiKey_Escape);  // [X] and Esc are Cancel

  const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
  if (ImGui::BeginChild("##ds_body", ImVec2(0.f, -footerH))) {
    if (ImGui::BeginTabBar("##ds_tabs")) {
      if (ImGui::BeginTabItem("Units and Zone")) {
        ImGui::Spacing();
        DrawUnitsAndZoneTab(staged);
        ImGui::EndTabItem();
      }
      for (const char* later : {"Transformation", "Object Layers"}) {
        ImGui::BeginDisabled();
        if (ImGui::BeginTabItem(later))
          ImGui::EndTabItem();
        ImGui::EndDisabled();
        GreyedWithReason(kNotImplemented);
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::EndChild();

  float scale = 0.f;
  const bool valid = StagedScale(staged, &scale);
  const float btnW = 90.f;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  ImGui::Separator();
  ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - (btnW * 3.f + spacing * 2.f));
  ImGui::BeginDisabled(!valid);
  const bool ok = ImGui::Button("OK", ImVec2(btnW, 0.f));
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(btnW, 0.f)))
    close = true;
  ImGui::SameLine();
  ImGui::BeginDisabled(!valid);
  const bool apply = ImGui::Button("Apply", ImVec2(btnW, 0.f));
  ImGui::EndDisabled();

  if ((ok || apply) && ApplyDrawingSettings(cmd, staged.insUnits, scale, staged.settings, log)) {
    SeedFromDrawing(cmd, staged);
    if (ok)
      close = true;
  }

  if (close) {
    cmd.showDrawingSettingsWindow = false;
    wasOpen = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  PopProductDialogAccent();
}
