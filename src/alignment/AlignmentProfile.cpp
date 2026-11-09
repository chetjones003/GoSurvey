#include "alignment/AlignmentProfile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace alignment {
namespace {

constexpr double kStationEps = 1e-6;  // matches the other station tolerances in the alignment code
constexpr double kOverlapEps = 1e-9;  // curves may meet exactly; a real overlap is refused (REQ-520.3)

// Grade of the tangent from points[i] to points[i + 1]. Points must be strictly increasing in station (validated first).
double SegmentGrade(const std::vector<VerticalPoint>& pts, size_t i) {
  return (pts[i + 1].elevation - pts[i].elevation) / (pts[i + 1].station - pts[i].station);
}

// The symmetric parabola of `pt` (curve length L > 0) between the incoming grade g1 and the outgoing grade g2 (REQ-520.2).
// It starts on the incoming tangent at L/2 before the PVI, so Eb = Ep - g1 L/2, and its elevation and grade follow the
// standard parabola: E(x) = Eb + g1 x + (g2 - g1) x^2 / (2L), G(x) = g1 + (g2 - g1) x / L, with x measured from the start.
ProfileSample CurveSample(const VerticalPoint& pt, double g1, double g2, double station) {
  const double L = pt.curveLength;
  const double x = station - (pt.station - 0.5 * L);
  const double eb = pt.elevation - g1 * 0.5 * L;
  ProfileSample out;
  out.hasValue = true;
  out.station = station;
  out.elevation = eb + g1 * x + (g2 - g1) * x * x / (2.0 * L);
  out.grade = g1 + (g2 - g1) * x / L;
  return out;
}

std::string Fmt(const char* format, double a) {
  // Two stations near 1e308 print to about 320 characters each, so a message needs room for both.
  char buf[1024];
  std::snprintf(buf, sizeof(buf), format, a);
  return buf;
}

// A query answer must be a finite number; a profile that cannot give one here says so instead (REQ-520.3).
ProfileSample Finish(ProfileSample s) {
  if (!std::isfinite(s.elevation) || !std::isfinite(s.grade)) {
    s.hasValue = false;
    s.reason = "the profile does not give a finite elevation here";
  }
  return s;
}

std::string Fmt2(const char* format, double a, double b) {
  // Two stations near 1e308 print to about 320 characters each, so a message needs room for both.
  char buf[1024];
  std::snprintf(buf, sizeof(buf), format, a, b);
  return buf;
}

}  // namespace

std::string ProfileGeometryRefusal(const VerticalProfile& p) {
  const std::vector<VerticalPoint>& pts = p.points;
  for (size_t i = 0; i < pts.size(); ++i) {
    const VerticalPoint& q = pts[i];
    if (!std::isfinite(q.station) || !std::isfinite(q.elevation) || !std::isfinite(q.curveLength))
      return Fmt("a PVI at station %.6f has a value that is not a number", q.station);
    if (q.curveLength < 0.0) return Fmt("the PVI at station %.6f has a negative curve length", q.station);
    if (i > 0 && q.station <= pts[i - 1].station)
      return Fmt("the PVI at station %.6f is not after the PVI before it", q.station);
  }
  if (pts.empty()) return std::string();
  // REQ-520.3: a curve needs a tangent on both sides, so the first and last PVIs carry no curve.
  if (pts.front().curveLength > 0.0)
    return Fmt("the first PVI (station %.6f) cannot have a curve; add a PVI before it", pts.front().station);
  if (pts.back().curveLength > 0.0)
    return Fmt("the last PVI (station %.6f) cannot have a curve; add a PVI after it", pts.back().station);
  // REQ-520.3: the two curves bounding a tangent must not overlap. Touching is allowed.
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const double spacing = pts[i + 1].station - pts[i].station;
    const double used = 0.5 * pts[i].curveLength + 0.5 * pts[i + 1].curveLength;
    if (!std::isfinite(pts[i + 1].station - pts[i].station) || !std::isfinite(SegmentGrade(pts, i)))
      return Fmt2("the tangent between the PVIs at stations %.6f and %.6f is too steep to represent",
                  pts[i].station, pts[i + 1].station);
    if (used > spacing + kOverlapEps)
      return Fmt2("the curves at the PVIs at stations %.6f and %.6f overlap; the tangent between them is too short",
                  pts[i].station, pts[i + 1].station);
  }
  return std::string();
}

ProfileSample ProfileAtStation(const VerticalProfile& p, double station) {
  ProfileSample out;
  out.station = station;
  const std::vector<VerticalPoint>& pts = p.points;
  if (pts.size() < 2) {  // REQ-520.3: no geometry is reported before anything else
    out.reason = "no geometry: the profile needs at least two PVIs";
    return out;
  }
  if (!std::isfinite(station)) {
    out.reason = "the station is not a number";
    return out;
  }
  if (const std::string why = ProfileGeometryRefusal(p); !why.empty()) {
    out.reason = "the profile geometry is not valid: " + why;
    return out;
  }
  if (station < pts.front().station || station > pts.back().station) {  // exact: REQ-520.4
    out.reason = "outside the profile";
    return out;
  }

  // The segment i with pts[i].station <= station < pts[i + 1].station; the last segment takes the final station.
  const size_t n = pts.size();
  const auto after = std::upper_bound(pts.begin(), pts.end(), station,
                                      [](double s, const VerticalPoint& q) { return s < q.station; });
  size_t i = static_cast<size_t>(after - pts.begin());
  i = (i == 0) ? 0 : i - 1;
  if (i > n - 2) i = n - 2;

  const double g = SegmentGrade(pts, i);

  // Inside the curve at the PVI that ends this segment (REQ-520.2): its first half.
  if (i + 1 < n - 1 && pts[i + 1].curveLength > 0.0) {
    const VerticalPoint& end = pts[i + 1];
    if (station >= end.station - 0.5 * end.curveLength - kStationEps)
      return Finish(CurveSample(end, g, SegmentGrade(pts, i + 1), station));
  }
  // Inside the curve at the PVI that starts this segment: its second half.
  if (i > 0 && pts[i].curveLength > 0.0) {
    const VerticalPoint& start = pts[i];
    if (station <= start.station + 0.5 * start.curveLength + kStationEps)
      return Finish(CurveSample(start, SegmentGrade(pts, i - 1), g, station));
  }

  // A straight tangent: the grade line through the two PVIs. At the far PVI the answer is that PVI's own elevation, exactly.
  out.hasValue = true;
  out.elevation = station == pts[i + 1].station ? pts[i + 1].elevation
                                               : pts[i].elevation + g * (station - pts[i].station);
  out.grade = g;
  return Finish(out);
}

bool DesignPathPoint(const Alignment& a, const VerticalProfile& p, double station, DesignPoint* out, std::string* why) {
  if (p.alignmentId != a.id) {
    if (why) *why = "the profile belongs to a different alignment";
    return false;
  }
  if (a.Empty()) {
    if (why) *why = "the alignment has no geometry";
    return false;
  }
  if (station < a.StartStation() - kStationEps || station > a.EndStation() + kStationEps) {
    if (why) *why = "outside the alignment";
    return false;
  }
  const ProfileSample s = ProfileAtStation(p, station);
  if (!s.hasValue) {
    if (why) *why = s.reason;
    return false;
  }
  const Point2 xy = a.GetPointAtStation(station);
  if (out) *out = DesignPoint{xy.x, xy.y, s.elevation};
  return true;
}

}  // namespace alignment
