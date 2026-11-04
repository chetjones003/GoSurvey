#pragma once

#include "Alignment.hpp"

#include <string>
#include <vector>

namespace alignment {

/// One major or minor station tick label along an alignment (REQ-492.2).
struct StationTickLabel {
  double rawStation = 0.0;
  double displayedStation = 0.0;
  bool major = false;
};

/// Collects major and minor station labels using displayed-station spacing. Major ticks sit at
/// `majorInterval` from the alignment start (excluding the start/end themselves). Minor ticks sit
/// every `minorInterval` but skip majors and skip ticks closer than one minor interval to the end.
[[nodiscard]] std::vector<StationTickLabel> CollectMajorMinorStationTicks(const Alignment& al,
                                                                          double majorInterval,
                                                                          double minorInterval,
                                                                          bool wantMajor, bool wantMinor);

/// One equation label (REQ-492.2 / REQ-492 AC2): where a station equation sits and the displayed stations either side of it.
struct EquationLabel {
  double rawStation = 0.0;       ///< Internal station of the equation point.
  double backDisplayed = 0.0;    ///< Displayed station just before it (the back value).
  double aheadDisplayed = 0.0;   ///< Displayed station just after it (the ahead value).
  bool increasing = true;
};

/// Equation labels in travel order. Their displayed values follow every equation before them (REQ-483).
[[nodiscard]] std::vector<EquationLabel> CollectEquationLabels(const Alignment& al);

/// The text and plan point of one station/offset label now (REQ-492.4). A fixed label reports the station and offset of its
/// stored point on the current geometry; a relative label reports its stored station and offset, placed on the current geometry.
struct StationOffsetLabelPlacement {
  Point2 point;
  double internalStation = 0.0;
  double offset = 0.0;
  bool onAlignment = true;  ///< False when a fixed point projects beyond an end of the alignment, or a relative station is off it.
  std::string text;         ///< "12+45.32  OFF -15.42" in the drawing's precision.
};
[[nodiscard]] StationOffsetLabelPlacement PlaceStationOffsetLabel(const Alignment& al, const StationOffsetLabel& label,
                                                                  int linearPrecision);

/// Moves a label to a plan point (REQ-492.5, drag). A fixed label takes the point as it is; a relative label takes the
/// station and offset of the nearest point on the alignment. False when the point has no meaning (empty alignment).
[[nodiscard]] bool MoveStationOffsetLabelTo(const Alignment& al, StationOffsetLabel* label, Point2 point);

}  // namespace alignment
