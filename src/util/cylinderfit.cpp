#include "util/cylinderfit.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace cylinderfit {

namespace {

/// Symmetric 3x3 eigen-decomposition via the cyclic Jacobi method. `m` is read/written in place as
/// the rotation sweeps converge it toward diagonal; `eigenvectors` accumulates the rotations
/// (columns are the eigenvectors, matching `m`'s final diagonal order). Small, fixed problem size —
/// hand-written rather than a dependency, per REQ-300.
void JacobiEigenSymmetric3x3(std::array<std::array<double, 3>, 3> &m,
                              std::array<std::array<double, 3>, 3> &eigenvectors) {
  eigenvectors = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  for (int sweep = 0; sweep < 50; ++sweep) {
    double offDiagSum = std::fabs(m[0][1]) + std::fabs(m[0][2]) + std::fabs(m[1][2]);
    if (offDiagSum < 1e-14) break;
    for (int p = 0; p < 2; ++p) {
      for (int q = p + 1; q < 3; ++q) {
        if (std::fabs(m[p][q]) < 1e-15) continue;
        const double theta = (m[q][q] - m[p][p]) / (2.0 * m[p][q]);
        const double t = (theta >= 0 ? 1.0 : -1.0) /
                         (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
        const double c = 1.0 / std::sqrt(t * t + 1.0);
        const double s = t * c;
        const double mpp = m[p][p], mqq = m[q][q], mpq = m[p][q];
        m[p][p] = mpp - t * mpq;
        m[q][q] = mqq + t * mpq;
        m[p][q] = m[q][p] = 0.0;
        for (int i = 0; i < 3; ++i) {
          if (i != p && i != q) {
            const double mip = m[i][p], miq = m[i][q];
            m[i][p] = m[p][i] = c * mip - s * miq;
            m[i][q] = m[q][i] = s * mip + c * miq;
          }
          const double vip = eigenvectors[i][p], viq = eigenvectors[i][q];
          eigenvectors[i][p] = c * vip - s * viq;
          eigenvectors[i][q] = s * vip + c * viq;
        }
      }
    }
  }
}

}  // namespace

FitResult FitCylinderAxisLeastSquares(const std::vector<double> &pointsXyz,
                                       const FitParams &params) {
  FitResult result;
  const int n = static_cast<int>(pointsXyz.size() / 3);
  if (n < params.minPoints) return result;

  double cx = 0, cy = 0, cz = 0;
  for (int i = 0; i < n; ++i) {
    cx += pointsXyz[i * 3 + 0];
    cy += pointsXyz[i * 3 + 1];
    cz += pointsXyz[i * 3 + 2];
  }
  cx /= n;
  cy /= n;
  cz /= n;

  std::array<std::array<double, 3>, 3> cov{};
  for (int i = 0; i < n; ++i) {
    const double dx = pointsXyz[i * 3 + 0] - cx;
    const double dy = pointsXyz[i * 3 + 1] - cy;
    const double dz = pointsXyz[i * 3 + 2] - cz;
    cov[0][0] += dx * dx;
    cov[0][1] += dx * dy;
    cov[0][2] += dx * dz;
    cov[1][1] += dy * dy;
    cov[1][2] += dy * dz;
    cov[2][2] += dz * dz;
  }
  cov[1][0] = cov[0][1];
  cov[2][0] = cov[0][2];
  cov[2][1] = cov[1][2];
  for (auto &row : cov)
    for (double &v : row) v /= n;

  std::array<std::array<double, 3>, 3> eigenvectors{};
  JacobiEigenSymmetric3x3(cov, eigenvectors);

  int dominant = 0;
  for (int i = 1; i < 3; ++i)
    if (cov[i][i] > cov[dominant][dominant]) dominant = i;

  double ax = eigenvectors[0][dominant];
  double ay = eigenvectors[1][dominant];
  double az = eigenvectors[2][dominant];
  const double axisLen = std::sqrt(ax * ax + ay * ay + az * az);
  if (axisLen < 1e-12) return result;  // degenerate: no dominant direction (points near-coincident)
  ax /= axisLen;
  ay /= axisLen;
  az /= axisLen;

  // Radial distance of each point from the axis line through the centroid, and its projection
  // (signed distance along the axis from the centroid) — the least-squares radius is the mean
  // radial distance; the inlier axial extent uses the projections.
  std::vector<double> radial(n), axial(n);
  double meanRadius = 0;
  for (int i = 0; i < n; ++i) {
    const double dx = pointsXyz[i * 3 + 0] - cx;
    const double dy = pointsXyz[i * 3 + 1] - cy;
    const double dz = pointsXyz[i * 3 + 2] - cz;
    const double t = dx * ax + dy * ay + dz * az;
    const double rx = dx - t * ax, ry = dy - t * ay, rz = dz - t * az;
    const double r = std::sqrt(rx * rx + ry * ry + rz * rz);
    radial[i] = r;
    axial[i] = t;
    meanRadius += r;
  }
  meanRadius /= n;
  if (meanRadius < 1e-9) return result;  // degenerate: points collapse onto the axis (a flat/line cluster)

  double sumSqResidual = 0;
  double axialMin = std::numeric_limits<double>::max();
  double axialMax = std::numeric_limits<double>::lowest();
  for (int i = 0; i < n; ++i) {
    const double d = radial[i] - meanRadius;
    sumSqResidual += d * d;
    axialMin = std::min(axialMin, axial[i]);
    axialMax = std::max(axialMax, axial[i]);
  }
  const double rms = std::sqrt(sumSqResidual / n);
  if (rms > params.maxResidualToRadiusRatio * meanRadius) return result;

  result.ok = true;
  result.axisX = ax;
  result.axisY = ay;
  result.axisZ = az;
  result.centerX = cx;
  result.centerY = cy;
  result.centerZ = cz;
  result.radius = meanRadius;
  result.rmsResidual = rms;
  result.p0X = cx + ax * axialMin;
  result.p0Y = cy + ay * axialMin;
  result.p0Z = cz + az * axialMin;
  result.p1X = cx + ax * axialMax;
  result.p1Y = cy + ay * axialMax;
  result.p1Z = cz + az * axialMax;
  return result;
}

}  // namespace cylinderfit
