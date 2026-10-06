#pragma once

// The look shared by the PDF viewer's panels (its tool bars, the thumbnail strip, the comparison bar and the change list): a
// raised, rounded, bordered panel with steel-blue buttons and dark input boxes. Push before BeginChild, pop after EndChild.

#include <imgui.h>

namespace pdfview {

inline void PushPanelStyle() {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 5.f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.f, 7.f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.17f, 0.21f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.30f, 0.36f, 0.46f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.27f, 0.35f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.42f, 0.58f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.48f, 0.80f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.09f, 0.10f, 0.13f, 1.f));
}

inline void PopPanelStyle() {
  ImGui::PopStyleColor(6);
  ImGui::PopStyleVar(6);
}

} // namespace pdfview
