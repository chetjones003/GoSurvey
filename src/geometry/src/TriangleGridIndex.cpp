#include <GoSurvey/Geometry/TriangleGridIndex.hpp>

#include <algorithm>
#include <cmath>

namespace geometry {

PlanBounds ComputePlanBounds(const std::vector<double>& vertsXyz) {
  PlanBounds b;
  for (size_t v = 0; v + 1 < vertsXyz.size(); v += 3) {
    const double x = vertsXyz[v];
    const double y = vertsXyz[v + 1];
    if (!b.hasAny) {
      b.minX = b.maxX = x;
      b.minY = b.maxY = y;
      b.hasAny = true;
    } else {
      b.minX = std::min(b.minX, x);
      b.maxX = std::max(b.maxX, x);
      b.minY = std::min(b.minY, y);
      b.maxY = std::max(b.maxY, y);
    }
  }
  return b;
}

namespace {

constexpr double kTargetTrianglesPerCell = 4.0;
constexpr long long kMaxIndexCells = 4'000'000;

}  // namespace

TriangleGridIndex BuildTriangleGridIndex(const std::vector<double>& vertsXyz,
                                         const std::vector<std::uint32_t>& indices) {
  TriangleGridIndex idx;
  const size_t triCount = indices.size() / 3;
  if (triCount == 0 || vertsXyz.size() < 9) return idx;

  const PlanBounds b = ComputePlanBounds(vertsXyz);
  if (!b.hasAny) return idx;

  const double width = std::max(b.maxX - b.minX, 1e-6);
  const double height = std::max(b.maxY - b.minY, 1e-6);
  double cellSize = std::sqrt((width * height) / (static_cast<double>(triCount) / kTargetTrianglesPerCell));
  cellSize = std::max(cellSize, 1e-6);

  long long cols = static_cast<long long>(std::ceil(width / cellSize)) + 1;
  long long rows = static_cast<long long>(std::ceil(height / cellSize)) + 1;
  if (cols * rows > kMaxIndexCells) {
    const double scale =
        std::sqrt(static_cast<double>(cols) * static_cast<double>(rows) / static_cast<double>(kMaxIndexCells));
    cellSize *= scale;
    cols = static_cast<long long>(std::ceil(width / cellSize)) + 1;
    rows = static_cast<long long>(std::ceil(height / cellSize)) + 1;
  }

  idx.minX = b.minX;
  idx.minY = b.minY;
  idx.cellSize = cellSize;
  idx.cols = static_cast<int>(std::max<long long>(cols, 1));
  idx.rows = static_cast<int>(std::max<long long>(rows, 1));
  idx.cells.resize(static_cast<size_t>(idx.cols) * static_cast<size_t>(idx.rows));

  const auto colOf = [&idx](double x) {
    return std::clamp(static_cast<int>((x - idx.minX) / idx.cellSize), 0, idx.cols - 1);
  };
  const auto rowOf = [&idx](double y) {
    return std::clamp(static_cast<int>((y - idx.minY) / idx.cellSize), 0, idx.rows - 1);
  };

  const std::uint32_t vertexCount = static_cast<std::uint32_t>(vertsXyz.size() / 3);
  for (size_t t = 0; t + 2 < indices.size(); t += 3) {
    const std::uint32_t ia = indices[t], ib = indices[t + 1], ic = indices[t + 2];
    if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) continue;

    const double x0 = vertsXyz[ia * 3], y0 = vertsXyz[ia * 3 + 1];
    const double x1 = vertsXyz[ib * 3], y1 = vertsXyz[ib * 3 + 1];
    const double x2 = vertsXyz[ic * 3], y2 = vertsXyz[ic * 3 + 1];
    const int c0 = colOf(std::min({x0, x1, x2}));
    const int c1 = colOf(std::max({x0, x1, x2}));
    const int r0 = rowOf(std::min({y0, y1, y2}));
    const int r1 = rowOf(std::max({y0, y1, y2}));
    const std::uint32_t triOrdinal = static_cast<std::uint32_t>(t / 3);
    for (int r = r0; r <= r1; ++r)
      for (int c = c0; c <= c1; ++c)
        idx.cells[static_cast<size_t>(r) * static_cast<size_t>(idx.cols) + static_cast<size_t>(c)].push_back(triOrdinal);
  }
  return idx;
}

}  // namespace geometry
