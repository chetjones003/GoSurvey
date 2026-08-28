#include "surfacestats.hpp"

#include "surfaceanalysis.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

SurfaceStats ComputeSurfaceStats(const std::vector<float>& vertsXyz,
                                 const std::vector<std::uint32_t>& indices) {
  SurfaceStats s;
  const int nTri = static_cast<int>(indices.size() / 3);
  if (nTri <= 0 || vertsXyz.size() < 9)
    return s;

  s.built = true;
  s.points = static_cast<int>(vertsXyz.size() / 3);
  s.triangles = nTri;
  s.minX = s.minY = s.minZ = std::numeric_limits<double>::infinity();
  s.maxX = s.maxY = s.maxZ = -std::numeric_limits<double>::infinity();
  s.minSlopePct = std::numeric_limits<double>::infinity();
  s.maxSlopePct = 0.0;
  double slopeWeight = 0.0;
  double slopeAccum = 0.0;

  for (int vi = 0; vi < s.points; ++vi) {
    const double x = vertsXyz[static_cast<size_t>(vi) * 3 + 0];
    const double y = vertsXyz[static_cast<size_t>(vi) * 3 + 1];
    const double z = vertsXyz[static_cast<size_t>(vi) * 3 + 2];
    s.minX = std::min(s.minX, x);
    s.maxX = std::max(s.maxX, x);
    s.minY = std::min(s.minY, y);
    s.maxY = std::max(s.maxY, y);
    s.minZ = std::min(s.minZ, z);
    s.maxZ = std::max(s.maxZ, z);
  }

  for (int t = 0; t < nTri; ++t) {
    const std::uint32_t ia = indices[static_cast<size_t>(t) * 3 + 0];
    const std::uint32_t ib = indices[static_cast<size_t>(t) * 3 + 1];
    const std::uint32_t ic = indices[static_cast<size_t>(t) * 3 + 2];
    const size_t na = vertsXyz.size() / 3;
    if (ia >= na || ib >= na || ic >= na)
      continue;
    AnalysisTriangle tri;
    tri.x0 = vertsXyz[static_cast<size_t>(ia) * 3 + 0];
    tri.y0 = vertsXyz[static_cast<size_t>(ia) * 3 + 1];
    tri.z0 = vertsXyz[static_cast<size_t>(ia) * 3 + 2];
    tri.x1 = vertsXyz[static_cast<size_t>(ib) * 3 + 0];
    tri.y1 = vertsXyz[static_cast<size_t>(ib) * 3 + 1];
    tri.z1 = vertsXyz[static_cast<size_t>(ib) * 3 + 2];
    tri.x2 = vertsXyz[static_cast<size_t>(ic) * 3 + 0];
    tri.y2 = vertsXyz[static_cast<size_t>(ic) * 3 + 1];
    tri.z2 = vertsXyz[static_cast<size_t>(ic) * 3 + 2];

    const double ux = tri.x1 - tri.x0, uy = tri.y1 - tri.y0;
    const double vx = tri.x2 - tri.x0, vy = tri.y2 - tri.y0;
    const double area2 = 0.5 * std::abs(ux * vy - uy * vx);
    s.area2d += area2;

    const double uz = tri.z1 - tri.z0, vz = tri.z2 - tri.z0;
    const double nx = uy * vz - uz * vy;
    const double ny = uz * vx - ux * vz;
    const double nz = ux * vy - uy * vx;
    s.area3d += 0.5 * std::sqrt(nx * nx + ny * ny + nz * nz);

    const double grade = TrianglePlaneSlopePct(tri);
    if (std::isfinite(grade) && area2 > 0.0) {
      s.minSlopePct = std::min(s.minSlopePct, grade);
      s.maxSlopePct = std::max(s.maxSlopePct, grade);
      slopeAccum += grade * area2;
      slopeWeight += area2;
    }
  }

  if (!(s.minSlopePct < std::numeric_limits<double>::infinity()))
    s.minSlopePct = 0.0;
  if (slopeWeight > 0.0)
    s.meanSlopePct = slopeAccum / slopeWeight;
  return s;
}
