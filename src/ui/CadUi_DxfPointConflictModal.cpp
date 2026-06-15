#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"

#include <imgui.h>

#include <algorithm>
#include <vector>

void DrawDxfPointConflictModal(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.dxfPointConflictModalOpen)
    return;

  if (cmd.dxfPointConflictModalOpenRequested) {
    ImGui::OpenPopup("GoSurveyDxfPointConflict");
    cmd.dxfPointConflictModalOpenRequested = false;
  }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  auto finish = [&]() {
    cmd.pendingDxfConflictPoints.clear();
    cmd.dxfPointConflictModalOpen = false;
    BumpCadGpuCache(cmd);
    ImGui::CloseCurrentPopup();
  };

  if (ImGui::BeginPopupModal("GoSurveyDxfPointConflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(
        "%d survey point(s) from the DXF have ID numbers that already exist in this drawing. "
        "Choose how to bring them in.",
        static_cast<int>(cmd.pendingDxfConflictPoints.size()));
    ImGui::Separator();

    ImGui::SetNextItemWidth(160.f);
    ImGui::InputInt("ID offset", &cmd.dxfPointConflictOffset);
    cmd.dxfPointConflictOffset = std::max(cmd.dxfPointConflictOffset, 0);
    ImGui::TextDisabled("Offset is added to each conflicting imported ID (e.g. ID 5 + offset becomes 5+offset).");

    ImGui::Separator();
    if (ImGui::Button("Add with offset", ImVec2(150.f, 0.f))) {
      ResolveConflictingWorldSurveyPoints(cmd, cmd.pendingDxfConflictPoints, /*overwrite=*/false,
                                          cmd.dxfPointConflictOffset, &log);
      log.push_back("DXF import — added conflicting points with ID offset " +
                    std::to_string(cmd.dxfPointConflictOffset) + ".");
      finish();
    }
    ImGui::SameLine();
    if (ImGui::Button("Overwrite existing", ImVec2(150.f, 0.f))) {
      ResolveConflictingWorldSurveyPoints(cmd, cmd.pendingDxfConflictPoints, /*overwrite=*/true, 0, &log);
      log.push_back("DXF import — overwrote existing points with the imported ones.");
      finish();
    }
    ImGui::SameLine();
    if (ImGui::Button("Skip", ImVec2(90.f, 0.f))) {
      log.push_back("DXF import — skipped " + std::to_string(cmd.pendingDxfConflictPoints.size()) +
                    " conflicting point(s).");
      finish();
    }
    ImGui::EndPopup();
  }
}
