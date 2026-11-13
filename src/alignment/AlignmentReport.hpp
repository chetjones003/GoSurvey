#pragma once

#include "alignment/Alignment.hpp"  // StationFormat
#include <GoSurvey/Core/AngleFormat.hpp>
#include "util/SheetGrid.hpp"

#include <string>
#include <vector>

namespace alignment {

class Alignment;
struct AlignmentCheckSet;

/// Display settings for alignment text/CSV reports (REQ-494). Pure values — no session state.
struct AlignmentReportFormat {
  int linearPrecision = 2;
  AngleDisplaySettings angles{};
  /// REQ-502: the check set the alignment is designed against, or null for none. Owned by the caller.
  const AlignmentCheckSet* checkSet = nullptr;
  StationFormat station{};  ///< REQ-483.4: the drawing's station format.
};

/// Builds the multi-line ALIGNMENTREPORT text (on-screen and `.txt` export).
void BuildAlignmentReportLines(const Alignment& alignment, const AlignmentReportFormat& format,
                               std::vector<std::string>* lines);

/// One CSV row per entity; header row first (UTF-8, quoted fields, `.` decimals via FormatLinear).
SheetGrid::Grid BuildAlignmentReportCsv(const Alignment& alignment, const AlignmentReportFormat& format);

}  // namespace alignment
