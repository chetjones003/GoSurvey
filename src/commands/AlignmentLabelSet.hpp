#pragma once

#include <string>
#include <vector>

#include "AlignmentStyle.hpp"
#include "CadEntities.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentLabels.hpp"

#include <algorithm>

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

/// Drawing units of an alignment label's text height at the drawing's plot scale (REQ-492.5, D-2026-11-04-c).
inline double LabelTextHeight(double modelUnitsPerPlottedInch) {
  return alignment::kLabelPlottedHeightInches * std::max(modelUnitsPerPlottedInch, 1.e-6);
}

/// The plan box of an alignment and the labels its label set and style draw, so ZOOM EXTENTS frames them as Civil 3D
/// does. A metric drawing ($INSUNITS 4 or 6) labels speeds in km/h.
inline bool LabelledBounds(const alignment::Alignment& al, const std::vector<AlignmentLabelSet>& sets,
                           const std::vector<AlignmentStyle>& styles, int drawingInsUnits, double modelUnitsPerPlottedInch,
                           alignment::Point2* mn, alignment::Point2* mx) {
  const AlignmentLabelSet* ls = Resolve(sets, al.labelSet);
  const AlignmentStyle* sty = AlignmentStyles::Resolve(styles, al.style);
  const bool labelsDrawn = ls != nullptr && (sty == nullptr || sty->labelsVisible);
  alignment::AlignmentLabelItems items;
  items.geometryPoints = labelsDrawn && ls->geometryPointsOn;
  items.designSpeeds = labelsDrawn && ls->designSpeedsOn;
  items.metric = drawingInsUnits == 4 || drawingInsUnits == 6;
  return alignment::LabelledBounds(al, items, labelsDrawn ? LabelTextHeight(modelUnitsPerPlottedInch) : 0.0, mn, mx);
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
