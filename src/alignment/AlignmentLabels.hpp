#pragma once

#include "Alignment.hpp"

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

}  // namespace alignment
