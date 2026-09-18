#pragma once

#include <vector>

/// Least-squares cylinder-axis fit over a local point-cloud neighborhood (REQ-347).
///
/// Pure Domain code: takes an interleaved x,y,z point buffer (architecture §11.8, local storage
/// coordinates) and estimates the axis of the cylinder those points sample from, if they plausibly
/// lie on one. Knows nothing about the octree, the cache, or the command that gathers the
/// neighborhood — those stay in `pointcloudoctree`/`pointcloudcache`/`CadCommands`, keeping this a
/// single, easily-unit-tested concrete function (architecture §11.4).
namespace cylinderfit {

/// Result of \ref FitCylinderAxisLeastSquares. `ok` is false (all other fields zero) when the
/// neighborhood has too few points or does not fit a cylindrical shell within tolerance — callers
/// must not draw or commit anything in that case (REQ-347's "no misleading line" requirement).
struct FitResult {
  bool ok = false;

  /// Unit vector along the fitted axis. Sign is arbitrary (fit from an unordered point set).
  double axisX = 0, axisY = 0, axisZ = 0;
  /// A point on the axis: the neighborhood centroid, projected axially to the fit's center of mass.
  double centerX = 0, centerY = 0, centerZ = 0;
  /// Least-squares radius: the mean radial distance of inlier points from the axis.
  double radius = 0;
  /// RMS of (radial distance - radius) across inlier points — the fit-quality gate REQ-347 requires.
  double rmsResidual = 0;

  /// The two axis endpoints, at the extent of inlier points projected onto the axis (REQ-347: "not
  /// an infinite cylinder"). `p0` + `p1` are what a caller draws/commits as a LINE.
  double p0X = 0, p0Y = 0, p0Z = 0;
  double p1X = 0, p1Y = 0, p1Z = 0;
};

/// Fit parameters. Defaults are conservative enough to reject a flat/planar or sparse-noise
/// neighborhood rather than draw a spurious line, per REQ-347.
struct FitParams {
  /// Below this point count, the fit is not attempted (a covariance-based axis estimate is unstable
  /// with too few samples).
  int minPoints = 12;
  /// Reject the fit when `rmsResidual / radius` exceeds this fraction — a real cylindrical shell's
  /// points sit close to a common radius; a flat or noisy cluster does not. 0.2 (loosened from an
  /// initial 0.15, field-tested on a real terrestrial scan — REQ-347 GUI note) leaves room for real
  /// scan noise, insulation wrap, and weld seams without accepting a neighborhood that spans two
  /// different surfaces.
  double maxResidualToRadiusRatio = 0.2;
};

/// Fits a cylinder axis to `pointsXyz` (interleaved x,y,z, stride 3) by:
///  1. PCA of the centered neighborhood (3x3 covariance, Jacobi eigen-decomposition) — the
///     dominant eigenvector is the least-squares best-fit LINE through the points (minimizes the
///     sum of squared perpendicular distances), which is the axis estimate for a neighborhood
///     elongated along a pipe segment (see TASK-271 ASSUMPTION-1).
///  2. Projecting every point onto the plane perpendicular to that axis and computing its radial
///     distance from the axis; the least-squares radius is the mean of those distances.
///  3. The RMS of (radial distance - radius) gauges how cylindrical the neighborhood actually is;
///     `params.maxResidualToRadiusRatio` gates acceptance.
/// Returns `FitResult{ok=false}` if there are fewer than `params.minPoints` points, the points are
/// degenerate (near-zero spread in every direction), or the residual gate fails.
[[nodiscard]] FitResult FitCylinderAxisLeastSquares(const std::vector<double> &pointsXyz,
                                                      const FitParams &params = {});

}  // namespace cylinderfit
