#pragma once

/// REQ-073: surface-to-surface cut/fill/net volume, and the spatial index it is built on
/// (TASK-095, D-2026-08-23-k).
///
/// Pure and GL/GUI-free — only `tinbuild.hpp` — so `GoSurveyTests` and `gosurvey_headless` link it
/// with no window and no GPU, beside `contourgen`/`tinbuild`/`surfaceanalysis` and for the same
/// reason (ADR-028 (c)).
///
/// **The algorithm is regular-grid sampling over the two surfaces' common footprint, not an exact
/// TIN-TIN boolean overlay** (ASSUMPTION-1, TASK-095 §5). REQ-073 specifies no algorithm, and its own
/// acceptance text — "within a stated tolerance" — anticipates a numerical method. Each surface is
/// queried through its OWN \ref TinSpatialIndex rather than a full linear scan per sample, because a
/// live recompute over two REQ-100-density surfaces (~200,000 triangles each) cannot afford
/// `TinElevationAt`'s O(triangles) scan once per sample point.

#include <cstdint>
#include <vector>

/// A per-TIN spatial index: triangles bucketed by their 2D (plan) bounding box into a uniform grid,
/// so a point query tests only the triangles whose box could plausibly contain it. Built fresh for
/// one volume computation — unlike the display-geometry cache, it is not kept across frames, because
/// it exists only for the lifetime of one (usually off-UI-thread) recompute.
struct TinSpatialIndex {
  double minX = 0.0, minY = 0.0;
  double cellSize = 1.0;
  int cols = 0, rows = 0;
  /// One bucket per cell (`cells[row * cols + col]`), holding TRIANGLE ORDINALS — `indices[ord*3]` is
  /// that triangle's first corner — not raw vertex indices, so a bucket entry names one triangle.
  std::vector<std::vector<std::uint32_t>> cells;

  [[nodiscard]] bool empty() const { return cols == 0 || rows == 0; }
};

/// Builds a spatial index over \p vertsXyz / \p indices, cell-sized so each cell holds a handful of
/// triangles on average (a fixed triangles-per-cell target, not a fixed cell count — the index scales
/// with the surface it is built for). An empty or too-small triangulation yields an empty index
/// (`empty()` true), never a crash.
[[nodiscard]] TinSpatialIndex BuildTinSpatialIndex(const std::vector<float>& vertsXyz,
                                                   const std::vector<std::uint32_t>& indices);

/// Elevation at (\p x, \p y), narrowing the candidate triangles through \p index instead of scanning
/// every triangle — the same containment/plane-eval rule `TinElevationAt` uses
/// (`TinTriangleElevationAt`, `tinbuild.hpp`), so an indexed and a full-scan query can never disagree
/// about what "inside" or "the elevation there" means.
///
/// \param outZ receives the elevation; untouched when no triangle in the point's cell covers it.
/// \returns true when a triangle covers the point.
[[nodiscard]] bool TinElevationAtIndexed(const std::vector<float>& vertsXyz,
                                         const std::vector<std::uint32_t>& indices,
                                         const TinSpatialIndex& index, double x, double y, double* outZ);

/// The result of a REQ-073 volume comparison between a **Base** and a **Comparison** surface
/// (ASSUMPTION-3, TASK-095: Civil 3D's own terms).
struct SurfaceVolumeResult {
  double cutFt3 = 0.0;   ///< Base above Comparison — material that would be REMOVED reaching it.
  double fillFt3 = 0.0;  ///< Comparison above Base — material that would be ADDED reaching it.
  double netFt3 = 0.0;   ///< `fillFt3 - cutFt3`. Positive: net material must be brought in.
  double commonAreaFt2 = 0.0;
  /// False when the two surfaces have no common footprint at all — sampling is not attempted, and
  /// every other field is 0, rather than a number derived from no common area (REQ-073).
  bool overlapped = false;
};

/// Computes \ref SurfaceVolumeResult for a Base/Comparison pair by regular-grid sampling over their
/// common footprint (ASSUMPTION-1/2, TASK-095): each sample point is queried against BOTH surfaces
/// through their own \ref TinSpatialIndex, and a point covered by only one, or neither, contributes
/// to neither the volume nor the reported common area.
///
/// A fast bounding-box-disjoint check runs first — two surfaces whose 2D extents do not even overlap
/// cost nothing beyond that check, with no spatial index built and no sample taken.
///
/// \param outCutTrianglesXyz  when non-null, APPENDED to (not cleared first — the caller's choice,
///        matching `AppendTriangleEdges`'s convention) with two triangles (nine floats each,
///        `GL_TRIANGLES` layout, `x0,y0,z0,x1,y1,z1,x2,y2,z2`) per sample cell where Base sits above
///        Comparison — REQ-073's cut/fill MAP, drawn at the Base surface's own elevation so it reads
///        as draped on the existing ground. Nothing is written, and no extra work done, when null —
///        a caller that does not want the map pays nothing for it.
/// \param outFillTrianglesXyz same, for cells where Comparison sits above Base.
///
/// Both outputs, when requested, contain geometry ONLY for cells covered by BOTH surfaces — "the
/// cut/fill map... shows nothing outside the common area" (REQ-073) is then a property of what is
/// generated, not of what the renderer remembers to clip.
[[nodiscard]] SurfaceVolumeResult ComputeSurfaceVolume(const std::vector<float>& baseVertsXyz,
                                                       const std::vector<std::uint32_t>& baseIndices,
                                                       const std::vector<float>& compVertsXyz,
                                                       const std::vector<std::uint32_t>& compIndices,
                                                       std::vector<float>* outCutTrianglesXyz = nullptr,
                                                       std::vector<float>* outFillTrianglesXyz = nullptr);
