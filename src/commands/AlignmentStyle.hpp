#pragma once

#include <string>
#include <vector>

#include "CadEntities.hpp"
#include "alignment/Alignment.hpp"

// Pure alignment-style helpers (REQ-491 / REQ-478 pattern). Dependency-free aside from
// Alignment forward use in Count — same layering as SurfaceStyle.hpp.

namespace AlignmentStyles {

inline constexpr const char* kStandardName = "Standard";

inline const AlignmentStyle* Find(const std::vector<AlignmentStyle>& styles, const std::string& name) {
  for (const AlignmentStyle& s : styles)
    if (s.name == name) return &s;
  return nullptr;
}
inline AlignmentStyle* Find(std::vector<AlignmentStyle>& styles, const std::string& name) {
  for (AlignmentStyle& s : styles)
    if (s.name == name) return &s;
  return nullptr;
}

inline AlignmentStyle StandardAlignmentStyle() {
  AlignmentStyle s;
  s.name = kStandardName;
  s.arrows.visible = false;  // direction arrows are opt-in (REQ-491)
  return s;
}

inline AlignmentStyle& EnsureStandard(std::vector<AlignmentStyle>& styles) {
  if (AlignmentStyle* s = Find(styles, kStandardName)) return *s;
  styles.insert(styles.begin(), StandardAlignmentStyle());
  return styles.front();
}

inline std::vector<AlignmentStyle> DefaultAlignmentStyles() {
  std::vector<AlignmentStyle> styles;
  EnsureStandard(styles);
  return styles;
}

inline const AlignmentStyle* Resolve(const std::vector<AlignmentStyle>& styles, const std::string& name) {
  if (!name.empty())
    if (const AlignmentStyle* s = Find(styles, name)) return s;
  if (const AlignmentStyle* s = Find(styles, kStandardName)) return s;
  return styles.empty() ? nullptr : &styles.front();
}

// REQ-502 / D-2026-11-04-b: the check set an alignment is checked against: its own, else its style's, else none.
inline std::string EffectiveCheckSetName(const std::vector<AlignmentStyle>& styles, const alignment::Alignment& a) {
  if (!a.checkSetName.empty()) return a.checkSetName;
  if (const AlignmentStyle* s = Resolve(styles, a.style)) return s->checkSetName;
  return {};
}

inline int CountAlignmentsResolvingTo(const std::vector<alignment::Alignment>& alignments,
                                      const std::vector<AlignmentStyle>& styles, const std::string& styleName) {
  int n = 0;
  for (const alignment::Alignment& a : alignments) {
    const AlignmentStyle* r = Resolve(styles, a.style);
    if (r && r->name == styleName) ++n;
  }
  return n;
}

inline const SurfaceComponentStyle* ComponentForEntity(const AlignmentStyle& style, alignment::EntityKind kind) {
  switch (kind) {
  case alignment::EntityKind::Line:
    return &style.tangent;
  case alignment::EntityKind::Arc:
    return &style.curve;
  case alignment::EntityKind::Spiral:
    return &style.spiral;
  }
  return &style.tangent;
}

}  // namespace AlignmentStyles
