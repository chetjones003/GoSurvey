#pragma once

#include <cstdint>
#include <vector>

namespace geometry {

struct PlanBounds {
  double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
  bool hasAny = false;
};

// XY bounds of packed XYZ vertices; empty input has hasAny == false.
[[nodiscard]] PlanBounds ComputePlanBounds(const std::vector<double>& vertsXyz);

/// Triangle ordinals bucketed by plan bounding box into a uniform grid.
/// The index keeps its cell layout so existing Terrain callers can adapt without a copy.
struct TriangleGridIndex {
  double minX = 0.0, minY = 0.0;
  double cellSize = 1.0;
  int cols = 0, rows = 0;
  /// One bucket per cell (`cells[row * cols + col]`), containing triangle ordinals.
  std::vector<std::vector<std::uint32_t>> cells;

  [[nodiscard]] bool empty() const { return cols == 0 || rows == 0; }
};

/// Builds the uniform triangle grid index for a packed XYZ vertex array and triangle indices.
[[nodiscard]] TriangleGridIndex BuildTriangleGridIndex(const std::vector<double>& vertsXyz,
                                                       const std::vector<std::uint32_t>& indices);

}  // namespace geometry
