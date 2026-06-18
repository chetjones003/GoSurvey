#pragma once

#include <imgui.h>

#include <string>
#include <vector>

// SHX stroke-font rendering (ADR-012 follow-up): AutoCAD .shx fonts (romans, txt, simplex, …) are
// compiled vector "shape" fonts with no TrueType equivalent. To match AutoCAD exactly we parse the real
// .shx files and draw each glyph as line strokes — the same way AutoCAD renders them. Files are located
// in the installed Autodesk font folders (or a folder configured by the app).
namespace Shx {

/// A parsed glyph: stroke polylines in font units (+x right, +y up, baseline at y=0) plus advance width.
struct Glyph {
  std::vector<std::vector<ImVec2>> strokes;
  float advance = 0.f;
};

class Font {
 public:
  bool LoadFromFile(const std::string& path);
  bool valid() const { return loaded_; }
  /// Cap height in font units (the height a capital letter occupies); used to scale to a text height.
  float capHeight() const { return capHeight_ > 0.f ? capHeight_ : 1.f; }
  /// Get (lazily building) the glyph for a character code, or nullptr if undefined.
  const Glyph* glyph(unsigned code);

 private:
  void buildGlyph(unsigned code, Glyph* out);

  bool loaded_ = false;
  float capHeight_ = 21.f;
  // code → raw shape bytecode (name already stripped).
  std::vector<std::pair<unsigned, std::vector<unsigned char>>> defs_;
  std::vector<std::pair<unsigned, Glyph>> cache_;
};

/// Resolve a DXF font name (e.g. "romans.shx", "romans", "txt") to a parsed SHX font, searching the
/// Autodesk font folders. Returns nullptr if the file isn't found (caller falls back to a TTF). Cached.
Font* Resolve(const std::string& fontName);

/// Width of \p text in pixels at the given pixel cap-height, using \p font's advances.
float MeasureWidthPx(Font& font, const std::string& text, float capPx);

/// Draw \p text with \p font as strokes. \p baseline is the screen-space baseline-left point; the glyph
/// cap height maps to \p capPx; \p rotRad rotates CCW about \p baseline (screen y grows downward).
void DrawText(ImDrawList* dl, Font& font, ImVec2 baseline, float capPx, float rotRad, ImU32 col,
              const std::string& text, float thicknessPx);

}  // namespace Shx
