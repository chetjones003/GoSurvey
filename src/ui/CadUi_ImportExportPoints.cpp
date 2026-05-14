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
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.88f, 0.95f, 1.f));
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

  if (ImGui::Button("Import")) {
    if (SurveyCsvImportFile(cmd, log))
      cmd.surveyImportPreviewDirty = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh preview"))
    cmd.surveyImportPreviewDirty = true;

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
