#include "surfacequery.hpp"

#include "gridsurface.hpp"
#include "surfaceanalysis.hpp"
#include "surfacevolume.hpp"
#include "tinbuild.hpp"

#include <cassert>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] double GradeToAngleDeg(double pct) {
  if (!std::isfinite(pct))
    return 90.0;
  return std::atan(pct / 100.0) * 180.0 / kPi;
}

}  // namespace

TinSurfaceQuery::TinSurfaceQuery(const std::vector<float>& vertsXyz, const std::vector<std::uint32_t>& indices)
    : verts_(&vertsXyz), indices_(&indices), index_(BuildTinSpatialIndex(vertsXyz, indices)) {}

bool TinSurfaceQuery::triangleAt(double x, double y, double* outZ, size_t* outTri) const {
  if (!verts_ || !indices_ || !outZ)
    return false;
  if (!index_.empty())
    return TinElevationAtIndexed(*verts_, *indices_, index_, x, y, outZ, outTri);
  for (size_t t = 0; t + 2 < indices_->size(); t += 3) {
    if (TinTriangleElevationAt(*verts_, (*indices_)[t], (*indices_)[t + 1], (*indices_)[t + 2], x, y, outZ)) {
      if (outTri)
        *outTri = t / 3;
      return true;
    }
  }
  return false;
}

bool TinSurfaceQuery::elevationAt(double x, double y, double* outZ) const {
  return triangleAt(x, y, outZ, nullptr);
}

bool TinSurfaceQuery::slopePercentAt(double x, double y, double* outPct) const {
  if (!outPct || !verts_ || !indices_)
    return false;
  double z = 0.0;
  size_t tri = 0;
  if (!triangleAt(x, y, &z, &tri))
    return false;
  const size_t t = tri * 3;
  const std::uint32_t ia = (*indices_)[t], ib = (*indices_)[t + 1], ic = (*indices_)[t + 2];
  AnalysisTriangle a;
  a.x0 = (*verts_)[ia * 3 + 0];
  a.y0 = (*verts_)[ia * 3 + 1];
  a.z0 = (*verts_)[ia * 3 + 2];
  a.x1 = (*verts_)[ib * 3 + 0];
  a.y1 = (*verts_)[ib * 3 + 1];
  a.z1 = (*verts_)[ib * 3 + 2];
  a.x2 = (*verts_)[ic * 3 + 0];
  a.y2 = (*verts_)[ic * 3 + 1];
  a.z2 = (*verts_)[ic * 3 + 2];
  *outPct = TrianglePlaneSlopePct(a);
  return std::isfinite(*outPct);
}

bool TinSurfaceQuery::slopeAngleDegAt(double x, double y, double* outDeg) const {
  double pct = 0.0;
  if (!slopePercentAt(x, y, &pct) || !outDeg)
    return false;
  *outDeg = GradeToAngleDeg(pct);
  return true;
}

bool TinSurfaceQuery::aspectDegAt(double x, double y, double* outDeg) const {
  if (!outDeg || !verts_ || !indices_)
    return false;
  double z = 0.0;
  size_t tri = 0;
  if (!triangleAt(x, y, &z, &tri))
    return false;
  const size_t t = tri * 3;
  const std::uint32_t ia = (*indices_)[t], ib = (*indices_)[t + 1], ic = (*indices_)[t + 2];
  AnalysisTriangle a;
  a.x0 = (*verts_)[ia * 3 + 0];
  a.y0 = (*verts_)[ia * 3 + 1];
  a.z0 = (*verts_)[ia * 3 + 2];
  a.x1 = (*verts_)[ib * 3 + 0];
  a.y1 = (*verts_)[ib * 3 + 1];
  a.z1 = (*verts_)[ib * 3 + 2];
  a.x2 = (*verts_)[ic * 3 + 0];
  a.y2 = (*verts_)[ic * 3 + 1];
  a.z2 = (*verts_)[ic * 3 + 2];
  return TriangleDownhillAspectDeg(a, kFlatGradePctDefault, outDeg);
}

GridSurfaceQuery::GridSurfaceQuery(double originX, double originY, double spacingX, double spacingY, int cols,
                                   int rows, const std::vector<float>& z)
    : originX_(originX), originY_(originY), spacingX_(spacingX), spacingY_(spacingY), cols_(cols), rows_(rows),
      z_(&z) {}

bool GridSurfaceQuery::sampleCell(double x, double y, double* z00, double* z10, double* z01, double* z11,
                                  double* fx, double* fy) const {
  if (!z_ || !GridIndexValid(cols_, rows_, *z_) || spacingX_ <= 0.0 || spacingY_ <= 0.0)
    return false;
  const double u = (x - originX_) / spacingX_;
  const double v = (y - originY_) / spacingY_;
  if (u < 0.0 || v < 0.0 || u > static_cast<double>(cols_ - 1) || v > static_cast<double>(rows_ - 1))
    return false;
  int i0 = static_cast<int>(std::floor(u));
  int j0 = static_cast<int>(std::floor(v));
  if (i0 >= cols_ - 1)
    i0 = cols_ - 2;
  if (j0 >= rows_ - 1)
    j0 = rows_ - 2;
  if (i0 < 0 || j0 < 0)
    return false;
  *fx = u - static_cast<double>(i0);
  *fy = v - static_cast<double>(j0);
  const auto at = [&](int i, int j) {
    return static_cast<double>((*z_)[static_cast<size_t>(j) * static_cast<size_t>(cols_) + static_cast<size_t>(i)]);
  };
  *z00 = at(i0, j0);
  *z10 = at(i0 + 1, j0);
  *z01 = at(i0, j0 + 1);
  *z11 = at(i0 + 1, j0 + 1);
  return std::isfinite(*z00) && std::isfinite(*z10) && std::isfinite(*z01) && std::isfinite(*z11);
}

bool GridSurfaceQuery::elevationAt(double x, double y, double* outZ) const {
  double z00 = 0, z10 = 0, z01 = 0, z11 = 0, fx = 0, fy = 0;
  if (!outZ || !sampleCell(x, y, &z00, &z10, &z01, &z11, &fx, &fy))
    return false;
  const double z0 = z00 * (1.0 - fx) + z10 * fx;
  const double z1 = z01 * (1.0 - fx) + z11 * fx;
  *outZ = z0 * (1.0 - fy) + z1 * fy;
  return true;
}

bool GridSurfaceQuery::slopePercentAt(double x, double y, double* outPct) const {
  double z00 = 0, z10 = 0, z01 = 0, z11 = 0, fx = 0, fy = 0;
  if (!outPct || !sampleCell(x, y, &z00, &z10, &z01, &z11, &fx, &fy))
    return false;
  const double dzdx = ((z10 - z00) * (1.0 - fy) + (z11 - z01) * fy) / spacingX_;
  const double dzdy = ((z01 - z00) * (1.0 - fx) + (z11 - z10) * fx) / spacingY_;
  *outPct = 100.0 * std::hypot(dzdx, dzdy);
  return std::isfinite(*outPct);
}

bool GridSurfaceQuery::slopeAngleDegAt(double x, double y, double* outDeg) const {
  double pct = 0.0;
  if (!slopePercentAt(x, y, &pct) || !outDeg)
    return false;
  *outDeg = GradeToAngleDeg(pct);
  return true;
}

bool GridSurfaceQuery::aspectDegAt(double x, double y, double* outDeg) const {
  double z00 = 0, z10 = 0, z01 = 0, z11 = 0, fx = 0, fy = 0;
  if (!outDeg || !sampleCell(x, y, &z00, &z10, &z01, &z11, &fx, &fy))
    return false;
  const double dzdx = ((z10 - z00) * (1.0 - fy) + (z11 - z01) * fy) / spacingX_;
  const double dzdy = ((z01 - z00) * (1.0 - fx) + (z11 - z10) * fx) / spacingY_;
  // Downhill is opposite the gradient. Azimuth 0 = +Y, increasing toward +X.
  const double gx = -dzdx;
  const double gy = -dzdy;
  if (std::hypot(gx, gy) < 1.0e-12)
    return false;
  double deg = std::atan2(gx, gy) * 180.0 / kPi;
  if (deg < 0.0)
    deg += 360.0;
  *outDeg = deg;
  return true;
}

bool SampleSurfaceProfileLine(const ISurfaceQuery& q, double x0, double y0, double x1, double y1, double stepFt,
                              int maxSamples, std::vector<SurfaceProfileSample>* out) {
  assert(out != nullptr);
  assert(maxSamples >= 2);
  if (out == nullptr)
    return false;
  out->clear();
  if (maxSamples < 2)
    return false;
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double len = std::hypot(dx, dy);
  if (!std::isfinite(len) || len < 1.0e-9)
    return false;
  double step = stepFt;
  if (!(step > 0.0) || !std::isfinite(step))
    step = len;
  int n = static_cast<int>(std::floor(len / step)) + 1;
  if (n < 2)
    n = 2;
  if (n > maxSamples)
    n = maxSamples;
  out->reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(n - 1);
    SurfaceProfileSample s;
    s.station = t * len;
    s.x = x0 + t * dx;
    s.y = y0 + t * dy;
    s.onSurface = q.elevationAt(s.x, s.y, &s.z);
    if (!s.onSurface)
      s.z = 0.0;
    out->push_back(s);
  }
  return true;
}
