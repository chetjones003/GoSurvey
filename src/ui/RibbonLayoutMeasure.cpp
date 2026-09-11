#include "RibbonLayoutMeasure.hpp"

#include <cassert>
#include <algorithm>

namespace ribbonlayout {

namespace {

// Matches bundled resources/icons/*.png (32×32) and UiChrome::ribbonIconSideMin default.
constexpr float kDefaultRibbonIconSideMin = 32.f;

float g_ribbonMeasureIconSide = 0.f;

} // namespace

void SetRibbonMeasureIconSide(const float side) {
  assert(side > 0.f);
  g_ribbonMeasureIconSide = side;
}

float RibbonMeasureIconSide() {
  if (g_ribbonMeasureIconSide > 0.f)
    return g_ribbonMeasureIconSide;
  return std::max(kDefaultRibbonIconSideMin, ImGui::GetFrameHeight());
}

} // namespace ribbonlayout
