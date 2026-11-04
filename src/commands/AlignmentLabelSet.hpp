#pragma once

#include <string>
#include <vector>

#include "AlignmentStyle.hpp"
#include "CadEntities.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentLabels.hpp"

#include <algorithm>

// REQ-492.5 alignment label styles: named, drawing-owned, with a built-in "Standard" (the REQ-478 pattern).
namespace AlignmentLabelStyles {

inline constexpr const char* kStandardName = "Standard";

inline const AlignmentLabelStyle* Find(const std::vector<AlignmentLabelStyle>& styles, const std::string& name) {
  for (const AlignmentLabelStyle& s : styles)
    if (s.name == name) return &s;
  return nullptr;
}
inline AlignmentLabelStyle* Find(std::vector<AlignmentLabelStyle>& styles, const std::string& name) {
  for (AlignmentLabelStyle& s : styles)
    if (s.name == name) return &s;
  return nullptr;
}

inline AlignmentLabelStyle StandardLabelStyle() {
  AlignmentLabelStyle s;
  s.name = kStandardName;
  return s;
}

inline AlignmentLabelStyle& EnsureStandard(std::vector<AlignmentLabelStyle>& styles) {
  if (AlignmentLabelStyle* s = Find(styles, kStandardName)) return *s;
  styles.insert(styles.begin(), StandardLabelStyle());
  return styles.front();
}

inline std::vector<AlignmentLabelStyle> DefaultAlignmentLabelStyles() {
  std::vector<AlignmentLabelStyle> styles;
  EnsureStandard(styles);
  return styles;
}

/// The style a reference names, else Standard (a missing reference is logged and cleared at load).
inline const AlignmentLabelStyle& Resolve(const std::vector<AlignmentLabelStyle>& styles, const std::string& name) {
  static const AlignmentLabelStyle kFallback = StandardLabelStyle();
  if (const AlignmentLabelStyle* s = Find(styles, name)) return *s;
  if (const AlignmentLabelStyle* s = Find(styles, kStandardName)) return *s;
  return kFallback;
}

/// Drawing units of a label's text height at the drawing's plot scale.
inline double TextHeight(const AlignmentLabelStyle& style, double modelUnitsPerPlottedInch) {
  return std::max(style.heightInches, 0.0) * std::max(modelUnitsPerPlottedInch, 1.e-6);
}

/// Every style reference a label set holds, so delete and rename can check them in one place.
inline std::vector<std::string*> References(AlignmentLabelSet& set) {
  return {&set.majorStyle, &set.geometryStyle, &set.speedStyle, &set.equationStyle, &set.segmentStyle, &set.offsetStyle};
}
inline std::vector<const std::string*> References(const AlignmentLabelSet& set) {
  return {&set.majorStyle, &set.geometryStyle, &set.speedStyle, &set.equationStyle, &set.segmentStyle, &set.offsetStyle};
}
inline int CountLabelSetsUsing(const std::vector<AlignmentLabelSet>& sets, const std::string& styleName) {
  int n = 0;
  for (const AlignmentLabelSet& s : sets) {
    const std::vector<const std::string*> refs = References(s);
    if (std::any_of(refs.begin(), refs.end(), [&](const std::string* r) { return *r == styleName; })) ++n;
  }
  return n;
}

}  // namespace AlignmentLabelStyles

namespace AlignmentLabelSets {

inline constexpr const char* kStandardName = "Standard";

inline const AlignmentLabelSet* Find(const std::vector<AlignmentLabelSet>& sets, const std::string& name) {
  for (const AlignmentLabelSet& s : sets)
    if (s.name == name) return &s;
  return nullptr;
}
inline AlignmentLabelSet* Find(std::vector<AlignmentLabelSet>& sets, const std::string& name) {
  for (AlignmentLabelSet& s : sets)
    if (s.name == name) return &s;
  return nullptr;
}

inline AlignmentLabelSet StandardLabelSet() {
  AlignmentLabelSet s;
  s.name = kStandardName;
  return s;
}

inline AlignmentLabelSet& EnsureStandard(std::vector<AlignmentLabelSet>& sets) {
  if (AlignmentLabelSet* s = Find(sets, kStandardName)) return *s;
  sets.insert(sets.begin(), StandardLabelSet());
  return sets.front();
}

inline std::vector<AlignmentLabelSet> DefaultAlignmentLabelSets() {
  std::vector<AlignmentLabelSet> sets;
  EnsureStandard(sets);
  return sets;
}

inline const AlignmentLabelSet* Resolve(const std::vector<AlignmentLabelSet>& sets, const std::string& name) {
  if (!name.empty())
    if (const AlignmentLabelSet* s = Find(sets, name)) return s;
  if (const AlignmentLabelSet* s = Find(sets, kStandardName)) return s;
  return sets.empty() ? nullptr : &sets.front();
}

/// What a drawing needs to place an alignment's labels: its style tables and plot scale.
struct LabelContext {
  const std::vector<AlignmentLabelSet>& sets;
  const std::vector<AlignmentStyle>& styles;
  const std::vector<AlignmentLabelStyle>& labelStyles;
  int drawingInsUnits = 2;
  double modelUnitsPerPlottedInch = 1.0;
};

/// The plan box of an alignment and the labels its label set and style draw, so ZOOM EXTENTS frames them as Civil 3D
/// does. A metric drawing ($INSUNITS 4 or 6) labels speeds in km/h.
inline bool LabelledBounds(const alignment::Alignment& al, const LabelContext& ctx, alignment::Point2* mn,
                           alignment::Point2* mx) {
  const AlignmentLabelSet* ls = Resolve(ctx.sets, al.labelSet);
  const AlignmentStyle* sty = AlignmentStyles::Resolve(ctx.styles, al.style);
  alignment::AlignmentLabelItems items;
  items.metric = ctx.drawingInsUnits == 4 || ctx.drawingInsUnits == 6;
  if (ls != nullptr && (sty == nullptr || sty->labelsVisible)) {
    const auto height = [&](const std::string& styleName) {
      return AlignmentLabelStyles::TextHeight(AlignmentLabelStyles::Resolve(ctx.labelStyles, styleName),
                                              ctx.modelUnitsPerPlottedInch);
    };
    items.besideHeight = std::max({height(ls->majorStyle), height(ls->equationStyle), height(ls->segmentStyle)});
    if (ls->geometryPointsOn) items.geometryHeight = height(ls->geometryStyle);
    if (ls->designSpeedsOn) items.speedHeight = height(ls->speedStyle);
  }
  return alignment::LabelledBounds(al, items, mn, mx);
}

inline int CountAlignmentsResolvingTo(const std::vector<alignment::Alignment>& alignments,
                                      const std::vector<AlignmentLabelSet>& sets, const std::string& setName) {
  int n = 0;
  for (const alignment::Alignment& a : alignments) {
    const AlignmentLabelSet* r = Resolve(sets, a.labelSet);
    if (r && r->name == setName) ++n;
  }
  return n;
}

}  // namespace AlignmentLabelSets
