#pragma once

#include "alignment/Alignment.hpp"

#include <vector>

namespace alignment {

/// The stations a feature line made from an alignment samples at (REQ-498.1), as distances from the start.
/// Every entity boundary and geometry point is included (so PC and PT are vertices), plus a station every
/// `interval` along the alignment and enough stations on each curve that the chord between neighbours lies within
/// `chordTolerance` of the true arc. Sorted, duplicates removed. An empty result means no geometry or a bad input.
[[nodiscard]] std::vector<double> FeatureLineStations(const Alignment& a, double interval, double chordTolerance);

}  // namespace alignment
