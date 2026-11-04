#include "alignment/AlignmentGround.hpp"

#include "util/surfacequery.hpp"

#include <algorithm>
#include <cmath>

namespace alignment {
namespace {

/// Points the alignment passes through. Mid-tangent, mid-curve, centre and PI are construction or display points, not
/// places the alignment runs over, so they are not sampled (REQ-497.3 asks for the geometry points themselves).
bool LiesOnAlignment(GeometryPointCode code) {
  switch (code) {
    case GeometryPointCode::LineMidPt:
    case GeometryPointCode::CurveMidPt:
    case GeometryPointCode::CurveCenter:
    case GeometryPointCode::CurvePI:
      return false;
    default:
      return true;
  }
}

constexpr double kSameStation = 1e-9;

}  // namespace

GroundPoint GroundAt(const Alignment& a, const ISurfaceQuery& surface, double internalStation, double offset) {
  GroundPoint g;
  g.internalStation = internalStation;
  g.offset = offset;
  if (a.Empty()) return g;
  // A station past either end is not on the alignment, so it has no ground (the clamped point would be a lie).
  if (internalStation < a.StartStation() - kSameStation || internalStation > a.EndStation() + kSameStation) return g;
  g.point = a.GetPointFromStationOffset(internalStation, offset);
  double z = 0.0;
  if (surface.elevationAt(g.point.x, g.point.y, &z)) {
    g.hasGround = true;
    g.ground = z;
  }
  return g;
}

bool SampleGround(const Alignment& a, const ISurfaceQuery& surface, double interval, int maxSamples,
                  std::vector<GroundPoint>* out, std::string* err) {
  if (out == nullptr) return false;
  out->clear();
  if (a.Empty()) {
    if (err != nullptr) *err = "the alignment has no geometry";
    return false;
  }
  if (!(interval > 0.0) || !std::isfinite(interval)) {
    if (err != nullptr) *err = "the sampling interval must be a positive number";
    return false;
  }
  const double length = a.Length();
  // Stations 0, interval, 2*interval, ... up to the end (inclusive), then the end itself if the step missed it.
  const double steps = std::floor(length / interval + kSameStation);
  if (steps + 2.0 > static_cast<double>(maxSamples)) {
    if (err != nullptr) *err = "the interval gives more than " + std::to_string(maxSamples) + " samples; use a larger interval";
    return false;
  }
  std::vector<double> stations;
  stations.reserve(static_cast<size_t>(steps) + 2);
  for (double k = 0.0; k <= steps; k += 1.0) stations.push_back(std::min(k * interval, length));
  stations.push_back(length);
  for (const GeometryPoint& gp : a.GeometryPoints()) {
    if (LiesOnAlignment(gp.code)) stations.push_back(gp.internalStation - a.StartStation());
  }
  for (double& s : stations) s = std::clamp(s, 0.0, length);
  std::sort(stations.begin(), stations.end());
  stations.erase(std::unique(stations.begin(), stations.end(),
                             [](double x, double y) { return std::fabs(x - y) <= kSameStation; }),
                 stations.end());
  if (static_cast<int>(stations.size()) > maxSamples) {
    if (err != nullptr) *err = "too many samples; use a larger interval";
    return false;
  }
  out->reserve(stations.size());
  for (const double s : stations) out->push_back(GroundAt(a, surface, a.StartStation() + s, 0.0));
  return true;
}

}  // namespace alignment
