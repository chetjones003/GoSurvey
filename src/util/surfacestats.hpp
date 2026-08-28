#pragma once

/// Surface statistics from a TIN (REQ-125 / ADR-039 (g)).
///
/// Pure — only the triangulation arrays — so Catch2 links it without GL, beside tinbuild.

#include <cstdint>
#include <vector>

struct SurfaceStats {
  bool built = false;  ///< False when there is no triangle; every other field is then 0.
  int points = 0;
  int triangles = 0;
  double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0, minZ = 0.0, maxZ = 0.0;
  double area2d = 0.0;  ///< Sum of triangle plan (XY) areas.
  double area3d = 0.0;  ///< Sum of triangle face areas.
  double minSlopePct = 0.0;
  double maxSlopePct = 0.0;
  double meanSlopePct = 0.0;  ///< Plan-area-weighted mean of per-triangle grade; 0 if area2d is 0.
};

/// Returns zeros and \c built false when \p indices is empty or \p vertsXyz is too short.
[[nodiscard]] SurfaceStats ComputeSurfaceStats(const std::vector<float>& vertsXyz,
                                               const std::vector<std::uint32_t>& indices);
