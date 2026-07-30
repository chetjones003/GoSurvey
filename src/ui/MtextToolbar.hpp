#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Pure geometry/string helpers behind the MTEXT "Text Formatting" panel (REQ-051). Kept header-only and
// free of ImGui so the fiddly parts — keeping a dragged panel on screen, composing the rich-text run tags,
// spacing the column ruler's ticks, and naming an attachment point — are unit-testable (the PlotFont.hpp /
// OrthoConstrain / ColorContrast precedent). Everything that needs a draw list stays in CadUi.cpp.
namespace mtexttoolbar {

/// Paired rich-text wire tags wrapping a selection. Both empty = nothing to apply (caller no-ops).
struct RunTags {
  std::string open;
  std::string close;
};

/// Keep a panel of \p panelW × \p panelH fully inside the rect [minX,minY]–[maxX,maxY], moving it the
/// least amount needed. A panel larger than the rect on an axis is pinned to that axis's minimum, so it
/// stays reachable (its far edge overflows rather than its title bar going off-screen).
inline void ClampPanelAnchor(float ax, float ay, float panelW, float panelH, float minX, float minY,
                             float maxX, float maxY, float* outX, float* outY) {
  const float limX = maxX - panelW;
  const float limY = maxY - panelH;
  *outX = (limX <= minX) ? minX : (ax < minX ? minX : (ax > limX ? limX : ax));
  *outY = (limY <= minY) ? minY : (ay < minY ? minY : (ay > limY ? limY : ay));
}

/// Tags that set the typeface of a run: [[font:Arial]] … [[/font]]. An empty family yields empty tags —
/// "(default font)" is the absence of an override, and [[font:]] would be a malformed tag the parser
/// would read as a run in the empty-named font.
inline RunTags FontRunTags(const std::string& family) {
  if (family.empty())
    return RunTags{};
  return RunTags{"[[font:" + family + "]]", "[[/font]]"};
}

/// Tags that colour a run: [[color:RRGGBB]] … [[/color]], matching MtextRichFormat's %06X emission.
/// Only the low 24 bits are used; any alpha in the high byte is dropped (runs carry no alpha).
inline RunTags ColorRunTags(uint32_t rgb) {
  char buf[20];
  std::snprintf(buf, sizeof(buf), "[[color:%06X]]", rgb & 0xFFFFFFu);
  return RunTags{buf, "[[/color]]"};
}

/// One mark on the column ruler. \c isMajor gets the taller tick (the reference draws minor ticks in
/// groups separated by a longer one).
struct RulerTick {
  float offsetPx = 0.f;
  bool isMajor = false;
};

/// Tick offsets across a ruler \p widthPx wide, one every \p minorSpacingPx, with every \p majorEvery-th
/// (counting the 0 mark as major) drawn tall. A non-positive width or spacing yields no ticks, so a
/// collapsed or not-yet-laid-out panel simply draws nothing instead of dividing by zero or spinning.
/// The count is capped so a pathological spacing cannot allocate without bound.
inline std::vector<RulerTick> RulerTicks(float widthPx, float minorSpacingPx, int majorEvery) {
  std::vector<RulerTick> out;
  if (!(widthPx > 0.f) || !(minorSpacingPx > 0.f))
    return out;
  if (majorEvery < 1)
    majorEvery = 1;
  constexpr int kMaxTicks = 4096;
  for (int n = 0; n <= kMaxTicks; ++n) {
    const float x = static_cast<float>(n) * minorSpacingPx;
    if (x > widthPx)
      break;
    out.push_back(RulerTick{x, (n % majorEvery) == 0});
  }
  return out;
}

/// Human name for an MTEXT attachment point (DXF group 71: 1 = top-left … 9 = bottom-right). Anything
/// out of range reads as the default top-left rather than indexing past the table.
inline const char* AttachLabel(int attach) {
  switch (attach) {
  case 1:  return "Top Left";
  case 2:  return "Top Center";
  case 3:  return "Top Right";
  case 4:  return "Middle Left";
  case 5:  return "Middle Center";
  case 6:  return "Middle Right";
  case 7:  return "Bottom Left";
  case 8:  return "Bottom Center";
  case 9:  return "Bottom Right";
  default: return "Top Left";
  }
}

}  // namespace mtexttoolbar
