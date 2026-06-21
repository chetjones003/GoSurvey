#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "SurveyCsv.hpp"
#include "WinFileDialogs.hpp"

#include <imgui.h>

#include <cfloat>
#include <vector>

namespace {

const char* kSurveyCsvLayoutComboItems =
    "P,N,E,Z,D (point ID, northing, easting, Z, description)\0"
    "P,E,N,Z,D (point ID, easting, northing, Z, description)\0"
    "N,E,Z (northing, easting, Z — IDs assigned on import)\0"
    "E,N,Z (easting, northing, Z — IDs assigned on import)\0\0";

} // namespace

void DrawImportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showImportPointsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
  bool open = cmd.showImportPointsWindow;
  if (!ImGui::Begin("Import points", &open)) {
    cmd.showImportPointsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showImportPointsWindow = open;

  if (ImGui::Button("Browse…")) {
    if (BrowseOpenFileCsvUtf8(cmd.surveyImportCsvPath, sizeof(cmd.surveyImportCsvPath)))
      cmd.surveyImportPreviewDirty = true;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::InputText("##imp_csv_path", cmd.surveyImportCsvPath, IM_ARRAYSIZE(cmd.surveyImportCsvPath)))
    cmd.surveyImportPreviewDirty = true;

  if (ImGui::Combo("Column order##imp", &cmd.surveyImportCsvLayoutIdx, kSurveyCsvLayoutComboItems))
    cmd.surveyImportPreviewDirty = true;
  if (ImGui::Checkbox("First row is header (skip)", &cmd.surveyImportCsvSkipFirstRow))
    cmd.surveyImportPreviewDirty = true;

  if (cmd.surveyImportPreviewDirty)
    SurveyCsvRefreshImportPreview(cmd);

  ImGui::Separator();
  ImGui::TextUnformatted("Validation summary");
  ImGui::BeginChild("imp_val", ImVec2(0, 110), true, ImGuiWindowFlags_HorizontalScrollbar);
  // REQ-041: green when the file is fully valid and importable; red when there are any
  // validation errors (a file-level block or skippable bad rows); neutral before a pick.
  ImVec4 valColor;
  if (!cmd.surveyImportCsvPath[0])
    valColor = ImVec4(0.82f, 0.88f, 0.95f, 1.f); // neutral: nothing selected yet
  else if (!cmd.surveyImportFileBlocked && cmd.surveyImportBadRowCount == 0)
    valColor = ImVec4(0.40f, 0.85f, 0.45f, 1.f); // green: clean and ready to import
  else
    valColor = ImVec4(0.95f, 0.45f, 0.45f, 1.f); // red: validation errors present
  ImGui::PushStyleColor(ImGuiCol_Text, valColor);
  ImGui::TextUnformatted(cmd.surveyImportPreviewValidation.c_str());
  ImGui::PopStyleColor();
  ImGui::EndChild();

  ImGui::TextUnformatted("File preview");
  ImGui::BeginChild("imp_prev", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.f), true,
                    ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.92f, 0.86f, 1.f));
  ImGui::TextUnformatted(cmd.surveyImportPreviewText.empty() ? "(no preview)" : cmd.surveyImportPreviewText.c_str());
  ImGui::PopStyleColor();
  ImGui::EndChild();

  // REQ-041: a file-level problem (missing/empty/locked/no valid rows) disables Import.
  const bool blocked = cmd.surveyImportFileBlocked;
  if (blocked)
    ImGui::BeginDisabled();
  if (ImGui::Button("Import")) {
    if (cmd.surveyImportBadRowCount > 0)
      ImGui::OpenPopup("Confirm import##imp"); // row-level problems: confirm skipping them first
    else if (SurveyCsvImportFile(cmd, log))
      cmd.surveyImportPreviewDirty = true;
  }
  if (blocked)
    ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Refresh preview"))
    cmd.surveyImportPreviewDirty = true;

  // REQ-041: row-level problems don't block, but the user confirms importing the valid
  // rows and skipping the bad ones before any change is made.
  if (ImGui::BeginPopupModal("Confirm import##imp", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Import %d valid row(s) and skip %d bad row(s)?", cmd.surveyImportValidRowCount,
                cmd.surveyImportBadRowCount);
    ImGui::Spacing();
    if (ImGui::Button("Import", ImVec2(120, 0))) {
      if (SurveyCsvImportFile(cmd, log))
        cmd.surveyImportPreviewDirty = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::End();
}

void DrawExportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showExportPointsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(520, 340), ImGuiCond_FirstUseEver);
  bool open = cmd.showExportPointsWindow;
  if (!ImGui::Begin("Export points", &open)) {
    cmd.showExportPointsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showExportPointsWindow = open;

  if (ImGui::Button("Browse…")) {
    BrowseSaveFileCsvUtf8(cmd.surveyExportCsvPath, sizeof(cmd.surveyExportCsvPath), "points.csv");
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputText("##exp_csv_path", cmd.surveyExportCsvPath, IM_ARRAYSIZE(cmd.surveyExportCsvPath));

  ImGui::Combo("Column order##exp", &cmd.surveyExportCsvLayoutIdx, kSurveyCsvLayoutComboItems);
  ImGui::Checkbox("Write header row", &cmd.surveyExportCsvWriteHeader);

  if (ImGui::Button("Export"))
    SurveyCsvExportFile(cmd, log);

  ImGui::End();
}
