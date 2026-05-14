#include "CadUi.hpp"

#include "CadCommands.hpp"

#include <imgui.h>

#include <vector>

void DrawCopySurveyDuplicateModal(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.copySurveyDupModalOpen)
    return;

  if (cmd.copySurveyDupModalOpenRequested) {
    ImGui::OpenPopup("GoSurveyCopySurveyDup");
    cmd.copySurveyDupModalOpenRequested = false;
  }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal("GoSurveyCopySurveyDup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(
        "Lines and circles are already copied. Choose how to add duplicate survey points when another point "
        "already uses the same ID.");
    ImGui::Separator();
    int pol = static_cast<int>(cmd.copySurveyDuplicatePolicy);
    if (ImGui::Combo(
            "If point numbers exist",
            &pol,
            "Notify (skip duplicate)\0Renumber (next free ID)\0Merge (update coords/desc)\0Overwrite (replace "
            "row)\0\0"))
      cmd.copySurveyDuplicatePolicy = static_cast<SurveyDuplicatePolicy>(pol);

    if (ImGui::Button("OK##copy_survey_dup", ImVec2(120.f, 0.f))) {
      ApplyCopySurveyDuplicateModalResult(cmd, true, log);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##copy_survey_dup", ImVec2(120.f, 0.f))) {
      ApplyCopySurveyDuplicateModalResult(cmd, false, log);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
