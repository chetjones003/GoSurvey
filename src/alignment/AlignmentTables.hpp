#pragma once

#include "alignment/Alignment.hpp"
#include <GoSurvey/Core/AngleFormat.hpp>

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
                                           const AngleDisplaySettings& angles, AlignmentTableText* out, std::string* whyNot,
                                           const StationFormat& station = {});

/// REQ-492.3: true when entity `entity` carries a segment label: every segment does under Multiple Segment labels,
/// otherwise only the segments picked as Single Segment labels.
[[nodiscard]] bool SegmentIsLabelled(const Alignment& a, int entity);
/// True when any segment of the alignment is labelled.
[[nodiscard]] bool HasSegmentLabels(const Alignment& a);

/// The table tag of every entity in travel order (REQ-493.1): L1, L2, … for lines, C1, … for curves and S1, … for
/// spirals, as Civil 3D numbers tag labels. A line, curve, spiral or segment table names its rows by these tags.
[[nodiscard]] std::vector<std::string> SegmentTags(const Alignment& a);
/// True when the alignment's segment labels are drawn as tags: they are on and a line, curve, spiral or segment table
/// lists them. Civil 3D converts existing segment labels to tag labels when such a table is made.
[[nodiscard]] bool ShowsTagLabels(const Alignment& a);
/// One part of a split table (REQ-493.1 Split Table): the header and data rows [firstRow, firstRow + rowCount);
/// rowCount 0 takes every row from firstRow on.
[[nodiscard]] AlignmentTableText SliceTableRows(const AlignmentTableText& full, int firstRow, int rowCount);

/// The label text of a circular curve in the layout the spec gives (REQ-492.3): `R = 500.00' Δ = 15°00'00" L = 130.90' T = 65.83'`.
/// Precision and angle format follow the drawing's settings; the curve's true arc length is used, not the chord.
[[nodiscard]] std::string CurveLabelText(const CurveData& curve, int linearPrecision, const AngleDisplaySettings& angles);
/// The same label as separate lines (R, Δ, L, T), for a stacked label in the drawing (REQ-492.5).
[[nodiscard]] std::vector<std::string> CurveLabelLines(const CurveData& curve, int linearPrecision,
                                                       const AngleDisplaySettings& angles);

}  // namespace alignment
