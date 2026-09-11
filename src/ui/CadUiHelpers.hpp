#pragma once
// Internal helpers shared across CadUi translation units.

#include "WikiHelp.hpp"

#include <imgui.h>

inline void ItemHelpTooltip(const char* text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    WikiHelpNotifyUiHover(text);
    if (ImGui::BeginTooltip()) {
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
      ImGui::TextUnformatted(text);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
  }
}
