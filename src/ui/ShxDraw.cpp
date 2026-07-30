#include "ShxDraw.hpp"

#include <cmath>
#include <vector>

namespace Shx {

void DrawText(ImDrawList* dl, Font& font, ImVec2 baseline, float capPx, float rotRad, ImU32 col,
              const std::string& text, float thicknessPx) {
  if (!dl)
    return;
  const float s = capPx / font.capHeight();
  const float cr = std::cos(rotRad), sr = std::sin(rotRad);
  float penX = 0.f;
  std::vector<ImVec2> pts;  // reused across strokes/glyphs to keep this allocation-free after the first
  for (unsigned char ch : text) {
    if (ch == '\n')
      continue;
    const Glyph* g = font.glyph(ch);
    if (!g)
      continue;
    auto map = [&](const Vec2& p) {
      // font units → text-local px (+y up), then rotate, then offset from baseline (screen y down).
      const float lx = (penX + p.x) * s;
      const float ly = p.y * s;
      return ImVec2(baseline.x + lx * cr + ly * sr, baseline.y + lx * sr - ly * cr);
    };
    for (const auto& stroke : g->strokes) {
      if (stroke.size() < 2)
        continue;
      // One polyline per stroke rather than an AddLine per segment: ImGui then joins the segments,
      // instead of laying down an independently anti-aliased quad for each. Per-segment lines left a
      // notch on the outside of every bend and a double-blended seam on the inside — clearly visible
      // as nicks along curved glyphs. It also emits far fewer vertices.
      pts.clear();
      pts.reserve(stroke.size());
      for (const Vec2& p : stroke)
        pts.push_back(map(p));
      dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, ImDrawFlags_None, thicknessPx);
    }
    penX += g->advance;
  }
}

}  // namespace Shx
