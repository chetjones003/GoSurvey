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
                           const TinSpatialIndex& index, double x, double y, double* outZ, size_t* outTri) {
  if (!outZ || index.empty())
    return false;
  const int c = std::clamp(static_cast<int>((x - index.minX) / index.cellSize), 0, index.cols - 1);
  const int r = std::clamp(static_cast<int>((y - index.minY) / index.cellSize), 0, index.rows - 1);
  const auto& bucket = index.cells[static_cast<size_t>(r) * static_cast<size_t>(index.cols) + static_cast<size_t>(c)];
  for (std::uint32_t triOrdinal : bucket) {
    const size_t t = static_cast<size_t>(triOrdinal) * 3;
    if (t + 2 >= indices.size())
      continue;
    if (TinTriangleElevationAt(vertsXyz, indices[t], indices[t + 1], indices[t + 2], x, y, outZ)) {
      if (outTri)
        *outTri = static_cast<size_t>(triOrdinal);
      return true;
    }
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

  const auto emitTri = [](std::vector<float>* dest, double x0, double y0, double z0, double x1, double y1,
                          double z1, double x2, double y2, double z2) {
    if (!dest)
      return;
    dest->insert(dest->end(), {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(z0),
                               static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(z1),
                               static_cast<float>(x2), static_cast<float>(y2), static_cast<float>(z2)});
  };

  struct Corner {
    double x = 0.0, y = 0.0, d = 0.0, zb = 0.0;
  };

  const auto lerpCorner = [](const Corner& a, const Corner& b) {
    Corner c;
    const double den = a.d - b.d;
    const double t = (std::fabs(den) < 1.0e-18) ? 0.5 : (a.d / den);
    const double u = std::clamp(t, 0.0, 1.0);
    c.x = a.x + u * (b.x - a.x);
    c.y = a.y + u * (b.y - a.y);
    c.zb = a.zb + u * (b.zb - a.zb);
    c.d = 0.0;
    return c;
  };

  const auto addSameSign = [&](const Corner& a, const Corner& b, const Corner& c, bool cut) {
    const double ux = b.x - a.x, uy = b.y - a.y;
    const double vx = c.x - a.x, vy = c.y - a.y;
    const double area = 0.5 * std::abs(ux * vy - uy * vx);
    if (area < 1.0e-18)
      return;
    const double meanD = (a.d + b.d + c.d) / 3.0;
    if (cut) {
      out.cutFt3 += meanD * area;
      out.cutAreaFt2 += area;
      emitTri(outCutTrianglesXyz, a.x, a.y, a.zb, b.x, b.y, b.zb, c.x, c.y, c.zb);
    } else {
      out.fillFt3 += (-meanD) * area;
      out.fillAreaFt2 += area;
      emitTri(outFillTrianglesXyz, a.x, a.y, a.zb, b.x, b.y, b.zb, c.x, c.y, c.zb);
    }
  };

  const auto addPrismTri = [&](Corner p0, Corner p1, Corner p2) {
    const int nPos = (p0.d > 0.0 ? 1 : 0) + (p1.d > 0.0 ? 1 : 0) + (p2.d > 0.0 ? 1 : 0);
    const int nNeg = (p0.d < 0.0 ? 1 : 0) + (p1.d < 0.0 ? 1 : 0) + (p2.d < 0.0 ? 1 : 0);
    if (nPos == 0 && nNeg == 0)
      return;
    if (nNeg == 0) {
      addSameSign(p0, p1, p2, true);
      return;
    }
    if (nPos == 0) {
      addSameSign(p0, p1, p2, false);
      return;
    }
    Corner v[3] = {p0, p1, p2};
    int odd = 0;
    const bool oddIsCut = nPos == 1;
    for (int k = 0; k < 3; ++k) {
      const bool isCut = v[k].d > 0.0;
      if (oddIsCut == isCut) {
        odd = k;
        break;
      }
    }
    const Corner& o = v[odd];
    const Corner& a = v[(odd + 1) % 3];
    const Corner& b = v[(odd + 2) % 3];
    const Corner ca = lerpCorner(o, a);
    const Corner cb = lerpCorner(o, b);
    if (oddIsCut) {
      addSameSign(o, ca, cb, true);
      addSameSign(ca, a, b, false);
      addSameSign(ca, b, cb, false);
    } else {
      addSameSign(o, ca, cb, false);
      addSameSign(ca, a, b, true);
      addSameSign(ca, b, cb, true);
    }
  };

  const auto addCentreCell = [&](double x0, double y0, double x1, double y1, double zBase, double zComp) {
    const double diff = zBase - zComp;
    out.commonAreaFt2 += cellArea;
    if (diff > 0.0) {
      out.cutFt3 += diff * cellArea;
      out.cutAreaFt2 += cellArea;
      emitTri(outCutTrianglesXyz, x0, y0, zBase, x1, y0, zBase, x1, y1, zBase);
      emitTri(outCutTrianglesXyz, x0, y0, zBase, x1, y1, zBase, x0, y1, zBase);
    } else if (diff < 0.0) {
      out.fillFt3 += (-diff) * cellArea;
      out.fillAreaFt2 += cellArea;
      emitTri(outFillTrianglesXyz, x0, y0, zBase, x1, y0, zBase, x1, y1, zBase);
      emitTri(outFillTrianglesXyz, x0, y0, zBase, x1, y1, zBase, x0, y1, zBase);
    }
  };

  for (int j = 0; j < rows; ++j) {
    const double y0 = iyMin + static_cast<double>(j) * cellH;
    const double y1 = y0 + cellH;
    const double sy = y0 + 0.5 * cellH;
    for (int i = 0; i < cols; ++i) {
      const double x0 = ixMin + static_cast<double>(i) * cellW;
      const double x1 = x0 + cellW;
      const double sx = x0 + 0.5 * cellW;
      if (haveClip && !TinPointInPolygon(sx, sy, *clipRingXy))
        continue;

      Corner c[4];
      const double xs[4] = {x0, x1, x1, x0};
      const double ys[4] = {y0, y0, y1, y1};
      bool allOk = true;
      for (int k = 0; k < 4; ++k) {
        double zb = 0.0, zc = 0.0;
        if (!TinElevationAtIndexed(baseVertsXyz, baseIndices, baseIdx, xs[k], ys[k], &zb) ||
            !TinElevationAtIndexed(compVertsXyz, compIndices, compIdx, xs[k], ys[k], &zc)) {
          allOk = false;
          break;
        }
        c[k].x = xs[k];
        c[k].y = ys[k];
        c[k].zb = zb;
        c[k].d = zb - zc;
      }
      if (allOk) {
        out.commonAreaFt2 += cellArea;
        addPrismTri(c[0], c[1], c[2]);
        addPrismTri(c[0], c[2], c[3]);
        continue;
      }
      double zBase = 0.0, zComp = 0.0;
      if (!TinElevationAtIndexed(baseVertsXyz, baseIndices, baseIdx, sx, sy, &zBase))
        continue;
      if (!TinElevationAtIndexed(compVertsXyz, compIndices, compIdx, sx, sy, &zComp))
        continue;
      addCentreCell(x0, y0, x1, y1, zBase, zComp);
    }
  }

  out.overlapped = out.commonAreaFt2 > 0.0;
  out.netFt3 = out.fillFt3 - out.cutFt3;
  return out;
}
