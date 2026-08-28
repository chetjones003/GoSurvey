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
  double minTriArea2d = 0.0;
  double maxTriArea2d = 0.0;
  int uniqueEdges = 0;
  int breaklineEdges = 0;  ///< Caller-supplied count of constrained TIN edges (REQ-140).
  double minSlopeDeg = 0.0;
  double maxSlopeDeg = 0.0;
  double meanSlopeDeg = 0.0;
  double volumeCutFt3 = 0.0;   ///< Positive cut volume where difference Z is below 0 (REQ-140).
  double volumeFillFt3 = 0.0;  ///< Positive fill volume where difference Z is above 0.
};

/// Returns zeros and \c built false when \p indices is empty or \p vertsXyz is too short.
/// \p breaklineEdgeCount is reported unchanged. When \p zIsDifference, each triangle is clipped at
/// Z = 0 so mixed-sign faces contribute cut and fill separately (same idea as REQ-147 sample volumes).
[[nodiscard]] SurfaceStats ComputeSurfaceStats(const std::vector<float>& vertsXyz,
                                               const std::vector<std::uint32_t>& indices,
                                               int breaklineEdgeCount = 0, bool zIsDifference = false);
