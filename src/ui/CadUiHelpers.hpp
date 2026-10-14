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

// Native-dialog form row: a label in a fixed-width left column, vertically centered on
// the next widget's frame, followed by ImGui::SameLine() so the caller's own
// SetNextItemWidth + widget (an "##id"-only label, so nothing is drawn twice) lands in
// the right column. See spec/coding-standards.md "Settings/options window layout" —
// every settings-style window (a popup or panel of labeled fields a person edits, not a
// toolbar or a report view) uses this for its rows instead of relying on a widget's own
// trailing label, so columns of different windows all line up the same way.
inline void UiFormLabel(const char* label, float labelColumnWidth) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(labelColumnWidth);
}
