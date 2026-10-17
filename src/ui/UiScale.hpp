#pragma once

// REQ-850: a single DPI/resolution scale factor, set once at startup (main.cpp, from
// ComputeMonitorDpiScale) and read everywhere a screen hand-draws pixel-exact layout instead of
// going through ImGuiStyle (which ImGui::GetStyle().ScaleAllSizes already scales on its own).
// Native ImGui widgets (buttons, combos, frame padding, ...) do NOT need this — they already scale
// via ImGuiStyle and io.FontGlobalScale. This is for custom ImDrawList positioning/sizing only
// (Start screen cards, hero band, badges, etc.) where literal pixel constants would otherwise stay
// fixed size while the text drawn into them grows, causing overlap on high-DPI displays and
// disproportionately large chrome on low-DPI ones.

namespace ui {

// Current global UI scale (1.0 = no scaling / the original hardcoded-pixel baseline).
float UiScale();

// Called once at startup (main.cpp) after the monitor DPI is known.
void SetUiScale(float scale);

}  // namespace ui
