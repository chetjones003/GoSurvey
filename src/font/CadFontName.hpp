#pragma once

#include <string>

// Pure font-name classification shared by overlay, plot, and MTEXT layout. Header-only so the
// SHX-vs-TTF decision is unit-testable without ImGui or the SHX file loader (PlotFont.hpp precedent).
namespace cadfont {

inline std::string LowerAscii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return s;
}

/// True when \p name refers to an AutoCAD stroke font (".shx" suffix, any case).
inline bool IsShxFontName(const std::string& name) {
  if (name.size() < 4)
    return false;
  const std::string ext = LowerAscii(name.substr(name.size() - 4));
  return ext == ".shx";
}

/// UTF-8 degree sign U+00B0. SHX stroke fonts in this tree typically have no glyph for it, so
/// dimension text that contains the sign must use a TrueType fallback.
inline bool ContainsUtf8Degree(const std::string& text) {
  return text.find("\xc2\xb0") != std::string::npos;
}

/// Stroke-render an SHX family unless the string needs a glyph the stroke font cannot supply.
inline bool PreferShxStrokes(const std::string& fontFamily, const std::string& text) {
  return IsShxFontName(fontFamily) && !ContainsUtf8Degree(text);
}

}  // namespace cadfont
