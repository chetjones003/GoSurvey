#include "daylight.hpp"

#include <cmath>

namespace {

/// Tight enough that the bracketing step, not this, decides the answer; loose enough to stop.
constexpr double kOnGradeEps = 1e-9;
constexpr double kBisectTolFt = 1e-7;  // 20,000x inside REQ-101, so the tolerance is the REQ's
constexpr int kBisectMaxIters = 80;

}  // namespace

double SlopePercentFromRun(double run) {
  if (!(run > 0.0) || !std::isfinite(run))
    return 0.0;
  return 100.0 / run;  // run:rise -> rise/run as a percentage
}

DaylightPoint SolveDaylightPoint(const ISurfaceQuery& ground, double bx, double by, double bz,
                                 double dirX, double dirY, const SideSlopes& slopes,
                                 const DaylightSearch& search) {
  DaylightPoint r;

  double g0 = 0.0;
  if (!ground.elevationAt(bx, by, &g0)) {
    r.why = DaylightFailure::BaselineOutsideSurface;
    return r;
  }

  const double delta = bz - g0;
  if (std::fabs(delta) <= kOnGradeEps) {
    // The design already sits on the ground here, so it daylights where it stands. Marching would
    // find a crossing further out and report a side slope that does not exist.
    r.daylighted = true;
    r.offset = 0.0;
    r.x = bx;
    r.y = by;
    r.z = g0;
    return r;
  }

  r.inCut = delta < 0.0;
  const double run = r.inCut ? slopes.cutRun : slopes.fillRun;
  if (!(run > 0.0) || !std::isfinite(run)) {
    r.why = DaylightFailure::DegenerateSlope;
    return r;
  }
  if (!(search.stepFt > 0.0) || !(search.maxOffsetFt > 0.0)) {
    r.why = DaylightFailure::DegenerateSlope;
    return r;
  }

  // The slope always leaves the baseline heading back TOWARD the ground: down when the design sits
  // high (fill), up when it sits low (cut). One signed rate covers both cases, which is what keeps
  // the two from drifting apart as separate code paths.
  const double rate = r.inCut ? (1.0 / run) : (-1.0 / run);

  // f(t) = slope elevation - ground elevation along the outward ray. f(0) has the sign of delta,
  // and daylight is f(t) == 0.
  const auto f = [&](double t, double* out) -> bool {
    double g = 0.0;
    if (!ground.elevationAt(bx + dirX * t, by + dirY * t, &g))
      return false;
    *out = (bz + rate * t) - g;
    return true;
  };

  double tPrev = 0.0;
  double fPrev = delta;
  for (double t = search.stepFt; t <= search.maxOffsetFt + 0.5 * search.stepFt; t += search.stepFt) {
    double fc = 0.0;
    if (!f(t, &fc)) {
      // Off the edge of the surface before meeting it. REQ-074: say so; do not extrapolate the
      // ground outward to manufacture an intersection.
      r.why = DaylightFailure::SlopeLeftSurface;
      return r;
    }
    if ((fPrev > 0.0) != (fc > 0.0) || std::fabs(fc) <= kOnGradeEps) {
      double lo = tPrev;
      double hi = t;
      double flo = fPrev;
      for (int i = 0; i < kBisectMaxIters && (hi - lo) > kBisectTolFt; ++i) {
        const double mid = 0.5 * (lo + hi);
        double fm = 0.0;
        if (!f(mid, &fm)) {
          r.why = DaylightFailure::SlopeLeftSurface;
          return r;
        }
        if ((flo > 0.0) == (fm > 0.0)) {
          lo = mid;
          flo = fm;
        } else {
          hi = mid;
        }
      }
      const double t0 = 0.5 * (lo + hi);
      const double px = bx + dirX * t0;
      const double py = by + dirY * t0;
      double g = 0.0;
      if (!ground.elevationAt(px, py, &g)) {
        r.why = DaylightFailure::SlopeLeftSurface;
        return r;
      }
      // The reported elevation is the SURFACE's, not the slope's: the daylight point is a point on
      // existing ground, and quoting the ray's own elevation would hide any residual bracket error.
      r.daylighted = true;
      r.offset = t0;
      r.x = px;
      r.y = py;
      r.z = g;
      return r;
    }
    tPrev = t;
    fPrev = fc;
  }

  r.why = DaylightFailure::NeverMeets;
  return r;
}

bool BaselineOutwardNormals(const std::vector<double>& xy, bool closed, bool leftSide,
                            std::vector<double>* outNxy) {
  if (!outNxy)
    return false;
  outNxy->clear();

  const std::size_t n = xy.size() / 2;
  if (n < 2)
    return false;

  // Everything below is expressed against the LEFT-hand normal of a directed edge, so that
  // `leftSide` means what it says. One sign flips it to the right-hand side.
  //
  // A closed ring decides its own outward from its signed area, so a ring drawn clockwise and the
  // same ring drawn counter-clockwise both grade away from the interior. Vertex order is an
  // accident of how it was drawn; the enclosed side is not.
  double sideSign = leftSide ? 1.0 : -1.0;
  if (closed) {
    double twiceArea = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t j = (i + 1) % n;
      twiceArea += xy[2 * i] * xy[2 * j + 1] - xy[2 * j] * xy[2 * i + 1];
    }
    // Counter-clockwise (positive area) keeps its interior on the LEFT of each directed edge, so
    // outward is the right-hand normal — the left normal flipped.
    sideSign = (twiceArea >= 0.0) ? -1.0 : 1.0;
  }

  // Left-hand normal of a directed edge: rotate the edge +90 degrees. Scaled by `sideSign`.
  const auto edgeNormal = [&](std::size_t i, std::size_t j, double* nx, double* ny) -> bool {
    const double ex = xy[2 * j] - xy[2 * i];
    const double ey = xy[2 * j + 1] - xy[2 * i + 1];
    const double len = std::hypot(ex, ey);
    if (!(len > 0.0) || !std::isfinite(len))
      return false;
    *nx = sideSign * (-ey / len);
    *ny = sideSign * (ex / len);
    return true;
  };

  outNxy->assign(2 * n, 0.0);
  bool any = false;
  for (std::size_t i = 0; i < n; ++i) {
    // Average the normals of the edges meeting at this vertex, so the slope leaves a corner along
    // its bisector rather than jumping between two directions.
    double ax = 0.0, ay = 0.0;
    int contributions = 0;
    double nx = 0.0, ny = 0.0;

    const bool hasBack = closed ? true : (i > 0);
    const bool hasFwd = closed ? true : (i + 1 < n);
    if (hasBack) {
      const std::size_t p = (i == 0) ? (n - 1) : (i - 1);
      if (edgeNormal(p, i, &nx, &ny)) { ax += nx; ay += ny; ++contributions; }
    }
    if (hasFwd) {
      const std::size_t q = (i + 1) % n;
      if (edgeNormal(i, q, &nx, &ny)) { ax += nx; ay += ny; ++contributions; }
    }
    if (contributions == 0)
      continue;

    double len = std::hypot(ax, ay);
    if (!(len > 1e-12)) {
      // A 180-degree reversal cancels the two normals. Fall back to one edge's normal rather than
      // emitting a zero direction that would send the projection nowhere.
      const std::size_t q = (i + 1) % n;
      if (!edgeNormal(i, q, &ax, &ay)) {
        const std::size_t p = (i == 0) ? (n - 1) : (i - 1);
        if (!edgeNormal(p, i, &ax, &ay))
          continue;
      }
      len = std::hypot(ax, ay);
      if (!(len > 1e-12))
        continue;
    }
    (*outNxy)[2 * i] = ax / len;
    (*outNxy)[2 * i + 1] = ay / len;
    any = true;
  }
  return any;
}
