#pragma once

#include <string>
#include <vector>

#include "CadEntities.hpp"
#include "alignment/Alignment.hpp"

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
