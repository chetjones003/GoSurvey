#include "UiScale.hpp"

namespace ui {

namespace {
float g_uiScale = 1.0f;
}

float UiScale() { return g_uiScale; }

void SetUiScale(float scale) { g_uiScale = scale > 0.f ? scale : 1.0f; }

}  // namespace ui
