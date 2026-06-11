#include "CadUi.hpp"

#include "CadCommands.hpp"

#include <imgui.h>

void DrawSurveyReportsPanel(AppCommandState& cmd) {
  ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Reports", nullptr)) {
    ImGui::End();
    return;
  }

  if (cmd.surveyReportTabs.empty()) {
    ImGui::TextDisabled("Exported CSV files and other generated reports will appear here as tabs.");
  } else {
    const bool focusLatest = cmd.surveyReportSelectLatestPending;
    const int lastIx = static_cast<int>(cmd.surveyReportTabs.size()) - 1;
    if (ImGui::BeginTabBar("SurveyReportsTabBar", ImGuiTabBarFlags_Reorderable)) {
      for (int i = 0; i <= lastIx; ++i) {
        ImGuiTabItemFlags tabFlags = 0;
        if (focusLatest && i == lastIx)
          tabFlags |= ImGuiTabItemFlags_SetSelected;
        if (ImGui::BeginTabItem(cmd.surveyReportTabs[static_cast<size_t>(i)].first.c_str(), nullptr, tabFlags)) {
          cmd.surveyReportSelectedTab = i;
          ImGui::BeginChild("SurveyReportBody", ImVec2(0, 0), false,
                            ImGuiWindowFlags_HorizontalScrollbar);
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.898f, 0.906f, 0.922f, 1.f));  // #E5E7EB light on dark panel
          ImGui::TextUnformatted(cmd.surveyReportTabs[static_cast<size_t>(i)].second.c_str());
          ImGui::PopStyleColor();
          ImGui::EndChild();
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
    if (focusLatest)
      cmd.surveyReportSelectLatestPending = false;
  }

  ImGui::End();
}
