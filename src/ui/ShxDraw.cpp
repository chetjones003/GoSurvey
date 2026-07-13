#include "ShxDraw.hpp"

#include <cmath>

namespace Shx {

void DrawText(ImDrawList* dl, Font& font, ImVec2 baseline, float capPx, float rotRad, ImU32 col,
              const std::string& text, float thicknessPx) {
  if (!dl)
    return;
  const float s = capPx / font.capHeight();
  const float cr = std::cos(rotRad), sr = std::sin(rotRad);
  float penX = 0.f;
  for (unsigned char ch : text) {
    if (ch == '\n')
      continue;
    const Glyph* g = font.glyph(ch);
    if (!g)
      continue;
    for (const auto& stroke : g->strokes) {
      if (stroke.size() < 2)
        continue;
      for (size_t k = 0; k + 1 < stroke.size(); ++k) {
        auto map = [&](const Vec2& p) {
          // font units → text-local px (+y up), then rotate, then offset from baseline (screen y down).
          const float lx = (penX + p.x) * s;
          const float ly = p.y * s;
          return ImVec2(baseline.x + lx * cr + ly * sr, baseline.y + lx * sr - ly * cr);
        };
        dl->AddLine(map(stroke[k]), map(stroke[k + 1]), col, thicknessPx);
      }
    }
    penX += g->advance;
  }
}

}  // namespace Shx
