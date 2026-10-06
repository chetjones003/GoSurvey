#include "DrawingConversion.hpp"

#include "CoordinateSystems.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace geo {

namespace {

constexpr double kUsSurveyFootMeters = 1200.0 / 3937.0;
constexpr double kInternationalFootMeters = 0.3048;

struct Pt {
  double x = 0.0;
  double y = 0.0;
};

}  // namespace

double MetersPerInsUnit(int insUnits, bool usSurveyFoot) {
  switch (insUnits) {
    case 1: return 0.0254;
    case 2: return usSurveyFoot ? kUsSurveyFootMeters : kInternationalFootMeters;
    case 4: return 0.001;
    case 6: return 1.0;
    default: return 0.0;
  }
}

void Similarity::Apply(double x, double y, double* ox, double* oy) const {
  const double c = std::cos(rotationRad);
  const double s = std::sin(rotationRad);
  *ox = shiftX + scale * (c * x - s * y);
  *oy = shiftY + scale * (s * x + c * y);
}

ConversionPlan CompareSettings(const ConversionInput& in) {
  ConversionPlan p;
  p.unitsDiffer = in.fromMetersPerUnit > 0.0 && in.toMetersPerUnit > 0.0 &&
                  std::fabs(in.fromMetersPerUnit - in.toMetersPerUnit) > 1e-12;
  p.zoneDiffers = !in.fromZone.empty() && !in.toZone.empty() && in.fromZone != in.toZone;
  return p;
}

ConversionPlan PlanConversion(const ConversionInput& in) {
  ConversionPlan plan = CompareSettings(in);
  if (!plan.Needed()) {
    plan.ok = true;
    return plan;
  }
  if (plan.unitsDiffer)
    plan.transform.scaleZ = in.fromMetersPerUnit / in.toMetersPerUnit;

  if (!plan.zoneDiffers) {  // units only: a pure scale about the world origin
    plan.transform.scale = in.fromMetersPerUnit / in.toMetersPerUnit;
    plan.ok = true;
    return plan;
  }

  if (!DictionariesLoaded()) {
    plan.error = "The coordinate-system dictionary is not loaded, so the drawing cannot be converted.";
    return plan;
  }
  const auto from = FindCoordinateSystem(in.fromZone);
  const auto to = FindCoordinateSystem(in.toZone);
  if (!from || !to) {
    plan.error = "The coordinate system " + (from ? in.toZone : in.fromZone) +
                 " is not in the installed dictionary, so the drawing cannot be converted.";
    return plan;
  }
  if (from->geographic || to->geographic) {
    plan.error = "Conversion to or from a latitude/longitude system is not supported.";
    return plan;
  }
  // The drawing's coordinates are in the drawing's unit; the zones' grids are in their own units.
  // A unitless drawing (or a project fixing no unit) is taken to be in the zone's own unit.
  const double pre = in.fromMetersPerUnit > 0.0 && from->metersPerUnit > 0.0
                         ? in.fromMetersPerUnit / from->metersPerUnit
                         : 1.0;
  const double post = in.toMetersPerUnit > 0.0 && to->metersPerUnit > 0.0 ? to->metersPerUnit / in.toMetersPerUnit
                                                                           : 1.0;
  std::string why;
  auto exact = [&](double x, double y, Pt* out) {
    const GeoResult ll = GridToLatLong(in.fromZone, x * pre, y * pre);
    if (!ll.ok) {
      why = ll.error;
      return false;
    }
    const GeoResult d = ConvertLatLong(in.fromZone, in.toZone, ll.x, ll.y);
    if (!d.ok) {
      why = d.error;
      return false;
    }
    const GeoResult g = LatLongToGrid(in.toZone, d.x, d.y);
    if (!g.ok) {
      why = g.error;
      return false;
    }
    out->x = g.x * post;
    out->y = g.y * post;
    return true;
  };

  const double cx = 0.5 * (in.minX + in.maxX);
  const double cy = 0.5 * (in.minY + in.maxY);
  const double step = in.fromMetersPerUnit > 0.0 ? 1000.0 / in.fromMetersPerUnit : 1000.0;  // about a kilometre
  Pt p0, px;
  if (!exact(cx, cy, &p0) || !exact(cx + step, cy, &px)) {
    plan.error = "The conversion could not be computed (" + why + ").";
    return plan;
  }
  const double vx = px.x - p0.x;
  const double vy = px.y - p0.y;
  Similarity t;
  t.scaleZ = plan.transform.scaleZ;
  t.scale = std::hypot(vx, vy) / step;
  t.rotationRad = std::atan2(vy, vx);
  if (!(t.scale > 0.0) || !std::isfinite(t.scale) || !std::isfinite(t.rotationRad)) {
    plan.error = "The conversion could not be computed (degenerate transform).";
    return plan;
  }
  {
    double ox = 0.0, oy = 0.0;
    t.shiftX = 0.0;
    t.shiftY = 0.0;
    t.Apply(cx, cy, &ox, &oy);
    t.shiftX = p0.x - ox;
    t.shiftY = p0.y - oy;
  }

  // The worst leftover over the extents' corners and edge midpoints, in meters.
  const double xs[3] = {in.minX, cx, in.maxX};
  const double ys[3] = {in.minY, cy, in.maxY};
  const double outUnitMeters = in.toMetersPerUnit > 0.0 ? in.toMetersPerUnit : to->metersPerUnit;
  double worst = 0.0;
  for (const double x : xs)
    for (const double y : ys) {
      Pt e;
      if (!exact(x, y, &e)) {
        plan.error = "The conversion could not be checked across the drawing (" + why + ").";
        return plan;
      }
      double tx = 0.0, ty = 0.0;
      t.Apply(x, y, &tx, &ty);
      worst = std::max(worst, std::hypot(tx - e.x, ty - e.y) * outUnitMeters);
    }
  plan.transform = t;
  plan.residualMeters = worst;
  if (worst > kMaxResidualMeters) {
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "A single shift, turn and scale would be off by up to %.3f m somewhere in this drawing (the "
                  "limit is %.2f m), so it cannot be converted.",
                  worst, kMaxResidualMeters);
    plan.error = buf;
    return plan;
  }
  plan.ok = true;
  return plan;
}

}  // namespace geo
