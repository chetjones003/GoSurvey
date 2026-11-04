#pragma once

#include "alignment/Alignment.hpp"
#include "util/AngleFormat.hpp"

#include <string>
#include <vector>

namespace alignment {

/// The cell text of one alignment table (REQ-493.1). The first row is the header; `cols` cells make one row.
struct AlignmentTableText {
  int cols = 0;
  std::vector<std::string> cells;
};

/// Builds the table text for `kind` (REQ-493.1). Line, curve and segment tables need the segments labelled first
/// (REQ-493.3): without that, the function refuses and `whyNot` names the missing labels. Equation tables need no labels.
[[nodiscard]] bool BuildAlignmentTableText(const Alignment& a, AlignmentTableKind kind, int linearPrecision,
                                           const AngleDisplaySettings& angles, AlignmentTableText* out, std::string* whyNot);

/// The label text of a circular curve in the layout the spec gives (REQ-492.3): `R = 500.00' Δ = 15°00'00" L = 130.90' T = 65.83'`.
/// Precision and angle format follow the drawing's settings; the curve's true arc length is used, not the chord.
[[nodiscard]] std::string CurveLabelText(const CurveData& curve, int linearPrecision, const AngleDisplaySettings& angles);

}  // namespace alignment
