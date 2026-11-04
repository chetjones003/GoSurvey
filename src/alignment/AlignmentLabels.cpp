#include "AlignmentLabels.hpp"

#include "util/NumFormat.hpp"

#include <cmath>
#include <string>

namespace alignment {

namespace {

[[nodiscard]] bool IsMajorDisplayedStation(double displayed, double dStart, double majorInterval) {
  if (!(majorInterval > 0.0)) return false;
  const double n = (displayed - dStart) / majorInterval;
  const double k = std::round(n);
  if (k < 1.0 - 1e-9) return false;
  return std::fabs(n - k) <= 1e-6;
}

}  // namespace

std::vector<StationTickLabel> CollectMajorMinorStationTicks(const Alignment& al, double majorInterval,
                                                            double minorInterval, bool wantMajor,
                                                            bool wantMinor) {
  std::vector<StationTickLabel> out;
  if (al.Empty() || !(minorInterval > 0.0) || !(majorInterval > 0.0)) return out;
  const double dStart = al.DisplayedStation(al.StartStation(), true);
  const double dEnd = al.EndStationWithEquations();
  if (!(dEnd > dStart + 1e-9)) return out;

  for (int k = 1;; ++k) {
    const double displayed = dStart + static_cast<double>(k) * minorInterval;
    if (displayed > dEnd - minorInterval + 1e-9) break;
    const bool isMajor = IsMajorDisplayedStation(displayed, dStart, majorInterval);
    if (isMajor && !wantMajor) continue;
    if (!isMajor && !wantMinor) continue;
    const std::vector<double> raws = al.RawStationsForDisplayed(displayed);
    if (raws.empty()) continue;
    out.push_back(StationTickLabel{raws.front(), displayed, isMajor});
  }
  return out;
}

}  // namespace alignment

namespace alignment {

std::vector<EquationLabel> CollectEquationLabels(const Alignment& al) {
  std::vector<EquationLabel> out;
  for (const StationEquation& q : al.equations) {
    EquationLabel e;
    e.rawStation = q.rawBack;
    e.backDisplayed = al.DisplayedStation(q.rawBack, false);
    e.aheadDisplayed = al.DisplayedStation(q.rawBack, true);
    e.increasing = q.increasing;
    out.push_back(e);
  }
  return out;
}

StationOffsetLabelPlacement PlaceStationOffsetLabel(const Alignment& al, const StationOffsetLabel& label,
                                                    int linearPrecision) {
  StationOffsetLabelPlacement r;
  if (al.Empty()) {
    r.onAlignment = false;
    return r;
  }
  if (label.fixed) {
    // The stored point stays put; what it reads as on the alignment now is what changes (REQ-492.4).
    const StationOffset so = al.GetStationOffset(label.fixedPoint.x, label.fixedPoint.y);
    r.point = label.fixedPoint;
    r.internalStation = so.internalStation;
    r.offset = so.offset;
    r.onAlignment = !so.outOfRange;
  } else {
    // The stored station and offset stay; the plan point follows the geometry (REQ-492.4).
    r.internalStation = label.internalStation;
    r.offset = label.offset;
    r.onAlignment = label.internalStation >= al.StartStation() - 1e-9 && label.internalStation <= al.EndStation() + 1e-9;
    r.point = al.GetPointFromStationOffset(label.internalStation, label.offset);
  }
  r.text = FormatStation(al.DisplayedStation(r.internalStation, true)) + "  OFF " + FormatLinear(r.offset, linearPrecision);
  return r;
}

}  // namespace alignment

namespace alignment {

bool MoveStationOffsetLabelTo(const Alignment& al, StationOffsetLabel* label, Point2 point) {
  if (label == nullptr || al.Empty()) return false;
  if (label->fixed) {
    label->fixedPoint = point;
    return true;
  }
  const StationOffset so = al.GetStationOffset(point.x, point.y);
  label->internalStation = so.internalStation;
  label->offset = so.offset;
  return true;
}

}  // namespace alignment
