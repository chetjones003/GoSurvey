#pragma once

#include <string>
#include <vector>

// SHX stroke-font geometry (ADR-012 follow-up; relocated to a shared lower layer per ADR-022 so both UI
// and IO/plot can use it). AutoCAD .shx fonts (romans, txt, simplex, …) are compiled vector "shape" fonts
// with no TrueType equivalent; we parse the real .shx files and produce each glyph as line strokes, the
// way AutoCAD renders them. This header is PURE geometry — no imgui, no rendering; the UI draw adapter
// lives in ui/ShxDraw. Files are located in the installed Autodesk font folders.
namespace Shx {

/// Plain 2-D point in font units — no imgui dependency (this module sits below UI).
struct Vec2 {
  float x = 0.f;
  float y = 0.f;
};

/// A parsed glyph: stroke polylines in font units (+x right, +y up, baseline at y=0) plus advance width.
struct Glyph {
  std::vector<std::vector<Vec2>> strokes;
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

}  // namespace Shx
