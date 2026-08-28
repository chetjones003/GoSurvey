#include "surfacevolume.hpp"

#include "tinbuild.hpp"

#include <algorithm>
#include <cmath>

namespace {

/// The 2D (plan) bounding box of a vertex array, X/Y only. `hasAny` is false for an empty array —
/// distinct from a degenerate single-point box, which is a real (zero-area) answer.
struct PlanBounds {
  double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
  bool hasAny = false;
};

PlanBounds ComputePlanBounds(const std::vector<float>& vertsXyz) {
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

/// Triangles-per-cell the index aims for. Not "cells per surface": the index scales with the
/// triangulation it is built for, so a sparse and a dense surface both get roughly the same lookup
/// cost per query rather than the sparse one getting an oversized grid for no benefit.
constexpr double kTargetTrianglesPerCell = 4.0;

/// A hard ceiling on total cells, independent of triangle count. Guards a degenerate input — a huge
/// bounding box with very few triangles (e.g. two duplicate points far apart plus everything else
/// collinear) — from computing a cell size so small the grid would try to allocate millions of empty
/// buckets for no triangles to put in them.
constexpr long long kMaxIndexCells = 4'000'000;

} // namespace

TinSpatialIndex BuildTinSpatialIndex(const std::vector<float>& vertsXyz,
                                     const std::vector<std::uint32_t>& indices) {
  TinSpatialIndex idx;
  const size_t triCount = indices.size() / 3;
  if (triCount == 0 || vertsXyz.size() < 9)
    return idx;  // empty() reports true — no cells, no crash

  const PlanBounds b = ComputePlanBounds(vertsXyz);
  if (!b.hasAny)
    return idx;

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

  const auto colOf = [&](double x) {
    return std::clamp(static_cast<int>((x - idx.minX) / idx.cellSize), 0, idx.cols - 1);
  };
  const auto rowOf = [&](double y) {
    return std::clamp(static_cast<int>((y - idx.minY) / idx.cellSize), 0, idx.rows - 1);
  };

  const std::uint32_t vertexCount = static_cast<std::uint32_t>(vertsXyz.size() / 3);
  for (size_t t = 0; t + 2 < indices.size(); t += 3) {
    const std::uint32_t ia = indices[t], ib = indices[t + 1], ic = indices[t + 2];
    if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount)
      continue;  // corrupt index (e.g. a hand-edited .gs) — skip rather than read past the array

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
        idx.cells[static_cast<size_t>(r) * static_cast<size_t>(idx.cols) + static_cast<size_t>(c)]
            .push_back(triOrdinal);
  }
  return idx;
}

bool TinElevationAtIndexed(const std::vector<float>& vertsXyz, const std::vector<std::uint32_t>& indices,
                           const TinSpatialIndex& index, double x, double y, double* outZ) {
  if (!outZ || index.empty())
    return false;
  const int c = std::clamp(static_cast<int>((x - index.minX) / index.cellSize), 0, index.cols - 1);
  const int r = std::clamp(static_cast<int>((y - index.minY) / index.cellSize), 0, index.rows - 1);
  const auto& bucket = index.cells[static_cast<size_t>(r) * static_cast<size_t>(index.cols) + static_cast<size_t>(c)];
  for (std::uint32_t triOrdinal : bucket) {
    const size_t t = static_cast<size_t>(triOrdinal) * 3;
    if (t + 2 >= indices.size())
      continue;
    if (TinTriangleElevationAt(vertsXyz, indices[t], indices[t + 1], indices[t + 2], x, y, outZ))
      return true;
  }
  return false;
}

namespace {

/// The sample grid's target cell count (ASSUMPTION-2, TASK-095): a FIXED budget independent of
/// either surface's triangle count, so a live recompute's cost stays bounded no matter how dense the
/// two TINs are. Named so it can be tuned without touching the integration logic.
constexpr double kTargetVolumeSamples = 250000.0;

} // namespace

SurfaceVolumeResult ComputeSurfaceVolume(const std::vector<float>& baseVertsXyz,
                                         const std::vector<std::uint32_t>& baseIndices,
                                         const std::vector<float>& compVertsXyz,
                                         const std::vector<std::uint32_t>& compIndices,
                                         std::vector<float>* outCutTrianglesXyz,
                                         std::vector<float>* outFillTrianglesXyz,
                                         const std::vector<std::pair<double, double>>* clipRingXy) {
  SurfaceVolumeResult out;
  if (baseIndices.size() < 3 || compIndices.size() < 3 || baseVertsXyz.size() < 9 || compVertsXyz.size() < 9)
    return out;  // no triangulation on one side: nothing to compare, reported as no overlap

  const PlanBounds a = ComputePlanBounds(baseVertsXyz);
  const PlanBounds b = ComputePlanBounds(compVertsXyz);
  if (!a.hasAny || !b.hasAny)
    return out;

  // Fast disjoint check: two surfaces whose extents do not even overlap cost nothing beyond this —
  // no index built, no sample taken (REQ-073: "report zero volume and say so").
  const double ixMin0 = std::max(a.minX, b.minX);
  const double ixMax0 = std::min(a.maxX, b.maxX);
  const double iyMin0 = std::max(a.minY, b.minY);
  const double iyMax0 = std::min(a.maxY, b.maxY);
  if (ixMin0 >= ixMax0 || iyMin0 >= iyMax0)
    return out;  // overlapped stays false

  const bool haveClip = clipRingXy && clipRingXy->size() >= 3;
  double ixMin = ixMin0, ixMax = ixMax0, iyMin = iyMin0, iyMax = iyMax0;
  if (haveClip) {
    double cMinX = (*clipRingXy)[0].first, cMaxX = cMinX;
    double cMinY = (*clipRingXy)[0].second, cMaxY = cMinY;
    for (const auto& p : *clipRingXy) {
      cMinX = std::min(cMinX, p.first);
      cMaxX = std::max(cMaxX, p.first);
      cMinY = std::min(cMinY, p.second);
      cMaxY = std::max(cMaxY, p.second);
    }
    ixMin = std::max(ixMin, cMinX);
    ixMax = std::min(ixMax, cMaxX);
    iyMin = std::max(iyMin, cMinY);
    iyMax = std::min(iyMax, cMaxY);
    if (ixMin >= ixMax || iyMin >= iyMax)
      return out;  // clip misses the common footprint
  }

  const TinSpatialIndex baseIdx = BuildTinSpatialIndex(baseVertsXyz, baseIndices);
  const TinSpatialIndex compIdx = BuildTinSpatialIndex(compVertsXyz, compIndices);
  if (baseIdx.empty() || compIdx.empty())
    return out;

  // The sample grid tiles the bounding-box INTERSECTION exactly (cell size derived from ITS own
  // dimensions, not a global constant), so cells never spill outside it and the reported common area
  // is the sum of cells that landed on real surface, not an approximation of the box itself.
  const double width = ixMax - ixMin;
  const double height = iyMax - iyMin;
  const double cellSize = std::max(std::sqrt((width * height) / kTargetVolumeSamples), 1e-9);
  const int cols = std::max(1, static_cast<int>(std::round(width / cellSize)));
  const int rows = std::max(1, static_cast<int>(std::round(height / cellSize)));
  const double cellW = width / static_cast<double>(cols);
  const double cellH = height / static_cast<double>(rows);
  const double cellArea = cellW * cellH;

  // A cell's map quad, at the BASE surface's own elevation so it reads as draped on the existing
  // ground rather than floating at an arbitrary datum. Two triangles, sharing the cell's diagonal.
  const auto emitCellQuad = [](std::vector<float>* out, double x0, double y0, double x1, double y1,
                               double z) {
    const float fx0 = static_cast<float>(x0), fy0 = static_cast<float>(y0);
    const float fx1 = static_cast<float>(x1), fy1 = static_cast<float>(y1);
    const float fz = static_cast<float>(z);
    out->insert(out->end(), {fx0, fy0, fz, fx1, fy0, fz, fx1, fy1, fz,
                             fx0, fy0, fz, fx1, fy1, fz, fx0, fy1, fz});
  };

  double cutFt3 = 0.0, fillFt3 = 0.0, commonArea = 0.0;
  for (int j = 0; j < rows; ++j) {
    const double sy = iyMin + (static_cast<double>(j) + 0.5) * cellH;
    for (int i = 0; i < cols; ++i) {
      const double sx = ixMin + (static_cast<double>(i) + 0.5) * cellW;
      if (haveClip && !TinPointInPolygon(sx, sy, *clipRingXy))
        continue;
      double zBase = 0.0, zComp = 0.0;
      if (!TinElevationAtIndexed(baseVertsXyz, baseIndices, baseIdx, sx, sy, &zBase))
        continue;
      if (!TinElevationAtIndexed(compVertsXyz, compIndices, compIdx, sx, sy, &zComp))
        continue;
      commonArea += cellArea;
      const double diff = zBase - zComp;  // ASSUMPTION-3: Base above Comparison is cut
      const double x0 = ixMin + static_cast<double>(i) * cellW, x1 = x0 + cellW;
      const double y0 = iyMin + static_cast<double>(j) * cellH, y1 = y0 + cellH;
      if (diff > 0.0) {
        cutFt3 += diff * cellArea;
        if (outCutTrianglesXyz)
          emitCellQuad(outCutTrianglesXyz, x0, y0, x1, y1, zBase);
      } else if (diff < 0.0) {
        fillFt3 += (-diff) * cellArea;
        if (outFillTrianglesXyz)
          emitCellQuad(outFillTrianglesXyz, x0, y0, x1, y1, zBase);
      }
    }
  }

  out.overlapped = commonArea > 0.0;
  out.commonAreaFt2 = commonArea;
  out.cutFt3 = cutFt3;
  out.fillFt3 = fillFt3;
  out.netFt3 = fillFt3 - cutFt3;
  return out;
}
