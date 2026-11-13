#include "AlignmentLabels.hpp"

#include <GoSurvey/Core/NumFormat.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
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
                                                    int linearPrecision, const StationFormat& fmt) {
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
  r.text = FormatStation(al.DisplayedStation(r.internalStation, true), fmt) + "  OFF " + FormatLinear(r.offset, linearPrecision);
  return r;
}

}  // namespace alignment

namespace alignment {

const char* GeometryPointAbbreviation(GeometryPointCode code) {
  switch (code) {
    case GeometryPointCode::Begin: return "BP";
    case GeometryPointCode::End: return "EP";
    case GeometryPointCode::TanTan: return "PI";
    case GeometryPointCode::TanCurve: return "PC";
    case GeometryPointCode::CurveTan: return "PT";
    case GeometryPointCode::CompCurveCurve: return "PCC";
    case GeometryPointCode::RevCurveCurve: return "PRC";
    case GeometryPointCode::TanSpiral: return "TS";
    case GeometryPointCode::SpiralTan: return "ST";
    case GeometryPointCode::CurveSpiral: return "CS";
    case GeometryPointCode::SpiralCurve: return "SC";
    case GeometryPointCode::SpiralSpiral: return "SS";
    case GeometryPointCode::RevSpiral: return "RS";
    case GeometryPointCode::SpiralPI: return "";
    case GeometryPointCode::CurveMidPt: return "Mid";
    case GeometryPointCode::CurvePI:
    case GeometryPointCode::CurveCenter:
    case GeometryPointCode::LineMidPt: return "";
  }
  return "";
}

std::vector<GeometryPointLabel> CollectGeometryPointLabels(const Alignment& al, const StationFormat& fmt) {
  std::vector<GeometryPointLabel> out;
  if (al.Empty()) return out;
  for (const GeometryPoint& gp : al.GeometryPoints()) {
    const std::string abbreviation = GeometryPointAbbreviation(gp.code);
    if (abbreviation.empty()) continue;
    // The ahead value at an equation point, like the station labels (REQ-492.2).
    const std::string station = FormatStation(al.DisplayedStation(gp.internalStation, true), fmt);
    out.push_back(GeometryPointLabel{gp.point, gp.internalStation, gp.code, abbreviation + ": " + station});
  }
  return out;
}

std::vector<DesignSpeedLabel> CollectDesignSpeedLabels(const Alignment& al, bool metric, const StationFormat& fmt) {
  std::vector<DesignSpeedLabel> out;
  if (al.Empty()) return out;
  for (const DesignSpeed& d : al.designSpeeds) {
    if (!(d.speed > 0.0)) continue;
    const bool whole = std::fabs(d.speed - std::round(d.speed)) < 1e-9;
    const std::string speed = FormatLinear(d.speed, whole ? 0 : 2);
    out.push_back(DesignSpeedLabel{d.internalStation, "STA " + FormatStation(al.DisplayedStation(d.internalStation, true), fmt) +
                                                          " DESIGN SPEED = " + speed + (metric ? " km/h" : " mi/h")});
  }
  return out;
}

std::vector<std::string> LineLabelLines(const Entity& line, int linearPrecision, const AngleDisplaySettings& angles) {
  return {FormatBearing(line.startAzimuth * 180.0 / kPi, angles), FormatLinear(line.length, linearPrecision) + "'"};
}

std::vector<std::string> SpiralLabelLines(const SpiralData& spiral, int linearPrecision) {
  // \xE2\x88\x9E is the infinity sign in UTF-8: a zero radius is a tangent end (Alignment.hpp).
  const auto radius = [&](double r) { return r > 0.0 ? FormatLinear(r, linearPrecision) + "'" : std::string("\xE2\x88\x9E"); };
  return {"A = " + FormatLinear(spiral.parameterA, linearPrecision), "Ls = " + FormatLinear(spiral.length, linearPrecision) + "'",
          "R1 = " + radius(spiral.startRadius), "R2 = " + radius(spiral.endRadius)};
}

double LeaderLabelReach(const std::string& text, double textHeight) {
  std::size_t codePoints = 0;
  for (const char c : text)
    if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) ++codePoints;  // count UTF-8 lead bytes, not continuations
  return textHeight * (kLeaderTextStartHeights + kLabelCharWidthHeights * static_cast<double>(codePoints) +
                       kLeaderOvershootHeights);
}

bool LabelledBounds(const Alignment& al, const AlignmentLabelItems& items, Point2* mn, Point2* mx) {
  if (mn == nullptr || mx == nullptr || !al.Bounds(mn, mx)) return false;
  const auto usable = [](double h) { return std::isfinite(h) && h > 0.0 ? h : 0.0; };
  const double beside = 9.0 * usable(items.besideHeight);  // a two-line stack moved up to four rows out from the line
  mn->x -= beside;
  mn->y -= beside;
  mx->x += beside;
  mx->y += beside;
  // A leader label runs across the alignment: to the left of travel for a geometry point, to the right for a speed.
  const auto grow = [&](double station, const std::string& text, double side, double h) {
    const Point2 p = al.GetPointAtStation(station);
    const double az = al.GetBearingAtStation(station);
    const double reach = side * LeaderLabelReach(text, h);
    const Point2 end{p.x - reach * std::cos(az), p.y + reach * std::sin(az)};
    mn->x = std::min(mn->x, end.x);
    mn->y = std::min(mn->y, end.y);
    mx->x = std::max(mx->x, end.x);
    mx->y = std::max(mx->y, end.y);
  };
  if (const double h = usable(items.geometryHeight); h > 0.0)
    for (const GeometryPointLabel& g : CollectGeometryPointLabels(al)) grow(g.internalStation, g.text, 1.0, h);
  if (const double h = usable(items.speedHeight); h > 0.0)
    for (const DesignSpeedLabel& d : CollectDesignSpeedLabels(al, items.metric)) grow(d.internalStation, d.text, -1.0, h);
  return true;
}

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
