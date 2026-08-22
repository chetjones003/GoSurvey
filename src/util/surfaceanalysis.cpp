#include "surfaceanalysis.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

/// The triangle's plane normal, as the cross product of two edge vectors. Its length is twice the
/// triangle's area, so a zero-length normal is exactly the degenerate case — collinear or coincident
/// corners — and is tested for by length rather than by comparing the corners to each other.
void PlaneNormal(const AnalysisTriangle& t, double* nx, double* ny, double* nz) {
  const double ux = t.x1 - t.x0, uy = t.y1 - t.y0, uz = t.z1 - t.z0;
  const double vx = t.x2 - t.x0, vy = t.y2 - t.y0, vz = t.z2 - t.z0;
  *nx = uy * vz - uz * vy;
  *ny = uz * vx - ux * vz;
  *nz = ux * vy - uy * vx;
}

} // namespace

double TriangleCentroidZ(const AnalysisTriangle& t) {
  return (t.z0 + t.z1 + t.z2) / 3.0;
}

double TrianglePlaneSlopePct(const AnalysisTriangle& t) {
  double nx = 0.0, ny = 0.0, nz = 0.0;
  PlaneNormal(t, &nx, &ny, &nz);

  const double horiz = std::sqrt(nx * nx + ny * ny);
  if (horiz == 0.0 && nz == 0.0)
    return 0.0;  // Degenerate: no plane, so no grade. Drawn flat, never given an arbitrary steepness.
  if (nz == 0.0)
    return std::numeric_limits<double>::infinity();  // Vertical face — see the header.

  // The plane's gradient is (-nx/nz, -ny/nz); its magnitude is rise over run, which is grade.
  return 100.0 * horiz / std::abs(nz);
}

bool TriangleDownhillDirection(const AnalysisTriangle& t, double flatGradePct, double* outDx,
                               double* outDy) {
  if (!outDx || !outDy)
    return false;

  double nx = 0.0, ny = 0.0, nz = 0.0;
  PlaneNormal(t, &nx, &ny, &nz);
  if (nz == 0.0)
    return false;  // Degenerate or vertical: no plane to fall down, or no XY direction to fall in.

  // Downhill is the negated gradient, (nx/nz, ny/nz). Dividing by the SIGNED nz is what makes the
  // answer independent of winding: reversing the corner order negates the whole normal, and the two
  // sign flips cancel.
  const double dx = nx / nz;
  const double dy = ny / nz;

  // The magnitude of that vector IS the grade (rise over run), so the flatness test costs nothing
  // extra and is expressed in the units ASSUMPTION-2 chose — percent, not vector length.
  const double len = std::sqrt(dx * dx + dy * dy);
  const double gradePct = 100.0 * len;
  // At or below the threshold is flat: the constant names the grade a drawing stops meaning to show
  // a direction for, so the boundary value is on the flat side of it. Written as a negated `>` so a
  // NaN grade is rejected too, which the comparison spelled the other way round would admit.
  if (!(gradePct > flatGradePct))
    return false;

  *outDx = dx / len;
  *outDy = dy / len;
  return true;
}

int AssignBand(double value, const std::vector<double>& upperBounds) {
  if (upperBounds.empty())
    return -1;

  // upper_bound returns the first bound strictly GREATER than the value, which is precisely the
  // half-open rule: a value sitting exactly on a breakpoint is not less than it, so it lands in the
  // band above. The rule is the search, rather than a chain of comparisons that has to re-state it.
  const auto it = std::upper_bound(upperBounds.begin(), upperBounds.end(), value);
  if (it != upperBounds.end())
    return static_cast<int>(it - upperBounds.begin());

  // Past the end: only the table's own maximum still has a band, because the topmost band is closed
  // at its top. Anything above it is outside the table and says so.
  if (value == upperBounds.back())
    return static_cast<int>(upperBounds.size()) - 1;
  return -1;
}
