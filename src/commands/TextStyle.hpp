#pragma once

#include <string>
#include <vector>

#include "CadEntities.hpp"

// Pure text-style helpers (REQ-044 / ADR-020). Dependency-free (no AppCommandState / UI / GL) so they are
// unit-tested in TextStyleTests. The drawing owns the std::vector<TextStyle>; each CadAnnotation references
// a style by name with per-property override flags. Resolution is "bake-on-write": a CadAnnotation's own
// fields (fontFamily/plottedHeightInches/obliqueDeg/bold/italic) always hold the *effective* values, so the
// renderer/measure/export paths read them directly with no change. Creating text bakes the active style into
// the new annotation; editing a style re-bakes its referencing, non-overridden annotations.

namespace TextStyles {

inline constexpr const char* kStandardName = "Standard";

/// Find a style by exact name, or nullptr.
inline const TextStyle* Find(const std::vector<TextStyle>& styles, const std::string& name) {
  for (const auto& s : styles)
    if (s.name == name) return &s;
  return nullptr;
}
inline TextStyle* Find(std::vector<TextStyle>& styles, const std::string& name) {
  for (auto& s : styles)
    if (s.name == name) return &s;
  return nullptr;
}

/// Guarantee a "Standard" style exists (inserted at the front if missing). Returns it.
inline TextStyle& EnsureStandard(std::vector<TextStyle>& styles) {
  if (TextStyle* s = Find(styles, kStandardName)) return *s;
  TextStyle standard;
  standard.name = kStandardName;
  styles.insert(styles.begin(), standard);
  return styles.front();
}

/// True when this annotation is a kind that participates in text styles (TEXT / MTEXT, not dimensions).
inline bool IsStyleableText(const CadAnnotation& a) {
  return a.kind == CadAnnotation::Kind::Text || a.kind == CadAnnotation::Kind::Mtext;
}

/// Copy the style's non-overridden properties into the annotation's effective fields (bake-on-write).
inline void RebakeAnnotation(CadAnnotation& a, const TextStyle& s) {
  if (!a.ovFont)    a.fontFamily = s.fontFamily;
  if (!a.ovHeight)  a.plottedHeightInches = s.heightInches;
  if (!a.ovOblique) a.obliqueDeg = s.obliqueDeg;
  if (!a.ovBold)    a.bold = s.bold;
  if (!a.ovItalic)  a.italic = s.italic;
}

/// Assign a style to a fresh annotation: reference it, clear all overrides, and bake every property.
inline void Assign(CadAnnotation& a, const TextStyle& s) {
  a.styleName = s.name;
  a.ovFont = a.ovHeight = a.ovOblique = a.ovBold = a.ovItalic = false;
  RebakeAnnotation(a, s);
}

/// Re-bake every annotation referencing \p s.name (used after a style is edited in the STYLE dialog).
inline void RebakeAllForStyle(std::vector<CadAnnotation>& anns, const TextStyle& s) {
  for (auto& a : anns)
    if (IsStyleableText(a) && a.styleName == s.name)
      RebakeAnnotation(a, s);
}

}  // namespace TextStyles
