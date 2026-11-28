#pragma once

/// Document-owned per-kind entity operations (ADR-0011 / GitHub issue #1577 phase 1).
///
/// Plain free functions per entity kind taking a `SimilarityTransform` in `double`, plus exact 3D
/// bounding boxes, duplication (for COPY = duplicate-then-transform), and store-wide kind/attribute
/// iteration. Computations run in `double`; float storage fields (normals, arc angles, bulges) are
/// rounded once on write.
///
/// Covers every kind Document owns. Kinds owned by other systems (feature lines, surfaces, point
/// clouds, plus survey points, alignments and PDF underlays) are added by Commands through those
/// systems' own functions (ADR-0011).

#include <GoSurvey/Document/AngleNormalize.hpp>
#include <GoSurvey/Document/EntityRef.hpp>
#include <GoSurvey/Document/cadblock.hpp>
#include <GoSurvey/Document/cadpiperun.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
#include <GoSurvey/Geometry/spline.hpp>
#include <GoSurvey/Math/ray3d.hpp>
#include <GoSurvey/Math/ucs.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace GoSurvey::Document {

namespace entityops_detail {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

/// Snaps a normal that is already within `1e-12` of world `+Z` to exact `(0, 0, 1)` when the input
/// was flat, so flat entities stay bit-identical through plan transforms and save/reload (REQ-312).
inline void StoreUnitNormal(const ray3d::Vec3& n, bool wasFlat, float* outNx, float* outNy,
                            float* outNz) {
  if (wasFlat && std::fabs(n.x) <= 1e-12 && std::fabs(n.y) <= 1e-12 &&
      std::fabs(n.z - 1.0) <= 1e-12) {
    *outNx = kFlatNormalX;
    *outNy = kFlatNormalY;
    *outNz = kFlatNormalZ;
    return;
  }
  *outNx = static_cast<float>(n.x);
  *outNy = static_cast<float>(n.y);
  *outNz = static_cast<float>(n.z);
}

inline void SyncPolylineNormalVec(std::vector<float>& normals, std::size_t vertexCount) {
  const std::size_t want = vertexCount * 3;
  if (normals.size() > want) {
    normals.resize(want);
    return;
  }
  const std::size_t oldVerts = normals.size() / 3;
  normals.resize(want);
  for (std::size_t i = oldVerts; i < vertexCount; ++i) {
    normals[i * 3 + 0] = kFlatNormalX;
    normals[i * 3 + 1] = kFlatNormalY;
    normals[i * 3 + 2] = kFlatNormalZ;
  }
}

/// True when angle \p theta lies within the directed arc sweep `[start, start + sweep]` modulo `2*pi`.
[[nodiscard]] inline bool AngleInSweep(double theta, double start, double sweep) {
  const double absSweep = std::fabs(sweep);
  if (absSweep >= kTwoPi - 1e-12)
    return true;
  if (absSweep <= 1e-12)
    return false;
  const double lo = sweep >= 0.0 ? start : (start + sweep);
  double delta = std::fmod(theta - lo, kTwoPi);
  if (delta < 0.0)
    delta += kTwoPi;
  return delta <= absSweep + 1e-12;
}

/// Exact 3D AABB of a circular arc in \p plane (origin = arc centre, zAxis = plane normal),
/// with \p radius > 0, \p startRad and signed \p sweepRad.
///
/// For each axis k in {x, y, z}, `P_k(t) = C_k + r * (X_k cos t + Y_k sin t)` attains its extrema
/// at the two endpoints and at `t* = atan2(Y_k, X_k)` and `t* + pi` whenever those lie inside the
/// swept interval. Zero discretization error in full 3D.
inline void ExpandBoundsWithArc3d(EntityBounds3d& b, const ucs::Ucs& plane, double radius,
                                  double startRad, double sweepRad) {
  if (!(radius > 1e-12) || !std::isfinite(radius) || !std::isfinite(startRad) ||
      !std::isfinite(sweepRad))
    return;
  b.expand(ucs::PointOnPlaneCircle(plane, radius, startRad));
  b.expand(ucs::PointOnPlaneCircle(plane, radius, startRad + sweepRad));

  const double ax[3] = {plane.xAxis.x, plane.xAxis.y, plane.xAxis.z};
  const double ay[3] = {plane.yAxis.x, plane.yAxis.y, plane.yAxis.z};
  for (int axis = 0; axis < 3; ++axis) {
    if (std::hypot(ax[axis], ay[axis]) <= 1e-15)
      continue;
    const double t0 = std::atan2(ay[axis], ax[axis]);
    if (AngleInSweep(t0, startRad, sweepRad))
      b.expand(ucs::PointOnPlaneCircle(plane, radius, t0));
    const double t1 = t0 + kPi;
    if (AngleInSweep(t1, startRad, sweepRad))
      b.expand(ucs::PointOnPlaneCircle(plane, radius, t1));
  }
}

/// Exact 3D AABB of a single polyline segment `a -> b` carrying DXF \p bulge in plane normal `(nx,ny,nz)`.
inline void ExpandBoundsWithPolylineSegment(EntityBounds3d& out, const ray3d::Vec3& a,
                                            const ray3d::Vec3& b, float bulge, float nx, float ny,
                                            float nz) {
  out.expand(a);
  out.expand(b);
  if (!(std::fabs(bulge) > 1e-12f) || !std::isfinite(bulge))
    return;
  if (IsFlatNormal(nx, ny, nz)) {
    const BulgeArcSpan span = BulgeArc(a.x, a.y, b.x, b.y, static_cast<double>(bulge));
    if (!span.valid)
      return;
    ucs::Ucs plane{};
    plane.origin = {span.cx, span.cy, a.z};
    ExpandBoundsWithArc3d(out, plane, span.radius, span.startAngle, span.sweep);
    if (b.z != a.z) {
      plane.origin.z = b.z;
      ExpandBoundsWithArc3d(out, plane, span.radius, span.startAngle, span.sweep);
    }
    return;
  }
  ucs::Ucs plane{};
  if (!ucs::FromNormal(a, {static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)},
                       &plane))
    return;
  const ucs::Point2D p1Local = ucs::WorldToPlane(plane, b);
  const BulgeArcSpan span = BulgeArc(0.0, 0.0, p1Local.x, p1Local.y, static_cast<double>(bulge));
  if (!span.valid)
    return;
  ucs::Ucs arcPlane = plane;
  arcPlane.origin = ucs::PlaneToWorld(plane, {span.cx, span.cy}, 0.0);
  ExpandBoundsWithArc3d(out, arcPlane, span.radius, span.startAngle, span.sweep);
}

inline void PadAttrs(std::vector<EntityAttributes>& attrs, std::size_t count) {
  if (attrs.size() < count)
    attrs.resize(count);
}

[[nodiscard]] inline bool ValidPolylineRange(const CadEntityStore& store, std::size_t index,
                                             int* outV0, int* outV1) {
  if (store.userPolylineOffsets.size() < 2 || index >= store.userPolylineOffsets.size() - 1)
    return false;
  const int v0 = store.userPolylineOffsets[index];
  const int v1 = store.userPolylineOffsets[index + 1];
  if (v0 < 0 || v1 <= v0 || static_cast<std::size_t>(v1) > store.vertexCount())
    return false;
  if (outV0)
    *outV0 = v0;
  if (outV1)
    *outV1 = v1;
  return true;
}

}  // namespace entityops_detail

// -----------------------------------------------------------------------------------------------
// Store-wide attribute & kind iteration
// -----------------------------------------------------------------------------------------------

/// Duplicate an entity's attributes for a newly created copy: preserves layer, colour, linetype,
/// lineweight, plot style and material, while clearing `id` to `0` so the next id sweep issues a
/// fresh identity (REQ-076).
[[nodiscard]] inline EntityAttributes DuplicateEntityAttrs(EntityAttributes a) {
  a.id = 0;
  return a;
}

/// Mutable attribute array for a Document-owned \ref EntityKind in \p store, or `nullptr` for
/// kinds owned by other systems (`FeatureLine`, `Surface`, `PointCloud`).
[[nodiscard]] inline std::vector<EntityAttributes>* EntityAttrsForKind(CadDrawingEntityStore& store,
                                                                       EntityKind kind) {
  switch (kind) {
  case EntityKind::Line:           return &store.userLineAttrs;
  case EntityKind::Circle:         return &store.userCircleAttrs;
  case EntityKind::Arc:            return &store.userArcAttrs;
  case EntityKind::Ellipse:        return &store.userEllAttrs;
  case EntityKind::Polyline:       return &store.userPolylineAttrs;
  case EntityKind::Annotation:     return &store.cadAnnotationAttrs;
  case EntityKind::FilledRegion:   return &store.cadFilledRegionAttrs;
  case EntityKind::Mesh:           return &store.cadMeshAttrs;
  case EntityKind::Table:          return &store.cadTableAttrs;
  case EntityKind::BlockRef:       return &store.cadBlockRefAttrs;
  case EntityKind::Solid:          return &store.cadSolidAttrs;
  case EntityKind::PositionMarker: return &store.cadPositionMarkerAttrs;
  case EntityKind::Multileader:    return &store.cadMultileaderAttrs;
  case EntityKind::Spline:         return &store.cadSplineAttrs;
  case EntityKind::Point:          return &store.cadPointAttrs;
  case EntityKind::PipeRun:        return &store.cadPipeRunAttrs;
  case EntityKind::FeatureLine:
  case EntityKind::Surface:
  case EntityKind::PointCloud:     return nullptr;
  }
  return nullptr;
}

[[nodiscard]] inline const std::vector<EntityAttributes>* EntityAttrsForKind(
    const CadDrawingEntityStore& store, EntityKind kind) {
  return EntityAttrsForKind(const_cast<CadDrawingEntityStore&>(store), kind);
}

/// Number of entities of \p kind currently held in \p store (0 for non-Document kinds).
[[nodiscard]] inline std::size_t EntityCountForKind(const CadDrawingEntityStore& store,
                                                    EntityKind kind) {
  switch (kind) {
  case EntityKind::Line:           return store.lineCount();
  case EntityKind::Circle:         return store.circleCount();
  case EntityKind::Arc:            return store.userArcs.size();
  case EntityKind::Ellipse:        return store.userEllipses.size();
  case EntityKind::Polyline:
    return store.userPolylineOffsets.size() > 1 ? store.userPolylineOffsets.size() - 1 : 0;
  case EntityKind::Annotation:     return store.cadAnnotations.size();
  case EntityKind::FilledRegion:   return store.cadFilledRegions.size();
  case EntityKind::Mesh:           return store.cadMeshes.size();
  case EntityKind::Table:          return store.cadTables.size();
  case EntityKind::BlockRef:       return store.cadBlockRefs.size();
  case EntityKind::Solid:          return store.cadSolids.size();
  case EntityKind::PositionMarker: return store.cadPositionMarkers.size();
  case EntityKind::Multileader:    return store.cadMultileaders.size();
  case EntityKind::Spline:         return store.cadSplines.size();
  case EntityKind::Point:          return store.cadPoints.size();
  case EntityKind::PipeRun:        return store.cadPipeRuns.size();
  case EntityKind::FeatureLine:
  case EntityKind::Surface:
  case EntityKind::PointCloud:     return 0;
  }
  return 0;
}

/// Invoke `fn(EntityKind)` once for every Document-owned kind in `CadDrawingEntityStore`.
template <class Fn>
inline void ForEachDocumentKind(Fn&& fn) {
  for (EntityKind kind : kDocumentEntityKinds)
    fn(kind);
}

/// Invoke `fn(EntityKind, std::vector<EntityAttributes>&)` for every Document-owned entity kind.
/// Enables store-wide attribute operations (such as layer rename, #1555) without hand-written
/// per-kind loops that can drift out of sync.
template <class Fn>
inline void ForEachEntityAttrs(CadDrawingEntityStore& store, Fn&& fn) {
  for (EntityKind kind : kDocumentEntityKinds) {
    if (std::vector<EntityAttributes>* attrs = EntityAttrsForKind(store, kind))
      fn(kind, *attrs);
  }
}

template <class Fn>
inline void ForEachEntityAttrs(const CadDrawingEntityStore& store, Fn&& fn) {
  for (EntityKind kind : kDocumentEntityKinds) {
    if (const std::vector<EntityAttributes>* attrs = EntityAttrsForKind(store, kind))
      fn(kind, *attrs);
  }
}

// -----------------------------------------------------------------------------------------------
// Per-kind transforms (Phase 1: Line, Circle, Arc, Polyline, Spline, Point)
// -----------------------------------------------------------------------------------------------

inline bool TransformLine(CadEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!xf.valid() || index >= store.lineCount())
    return false;
  const CadLineRecord ln = store.line(index);
  const ray3d::Vec3 p0 = xf.applyPoint({ln.x0, ln.y0, ln.z0});
  const ray3d::Vec3 p1 = xf.applyPoint({ln.x1, ln.y1, ln.z1});
  store.setLine(index, CadLineRecord{p0.x, p0.y, p0.z, p1.x, p1.y, p1.z});
  return true;
}

inline bool TransformCircle(CadEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!xf.valid() || index >= store.circleCount())
    return false;
  const CadCircleRecord c = store.circle(index);
  const ray3d::Vec3 nc = xf.applyPoint({c.cx, c.cy, c.cz});
  store.setCircle(index, CadCircleRecord{nc.x, nc.y, nc.z, c.r * xf.scale()});

  if (!xf.isIdentityRotation()) {
    float nx = kFlatNormalX, ny = kFlatNormalY, nz = kFlatNormalZ;
    CircleNormalAt(store.userCircleNormals, index, &nx, &ny, &nz);
    const bool wasFlat = IsFlatNormal(nx, ny, nz);
    const ray3d::Vec3 nn =
        xf.applyUnitDirection({static_cast<double>(nx), static_cast<double>(ny),
                               static_cast<double>(nz)});
    if (ray3d::Length(nn) > 0.5) {
      float outNx = kFlatNormalX, outNy = kFlatNormalY, outNz = kFlatNormalZ;
      entityops_detail::StoreUnitNormal(nn, wasFlat, &outNx, &outNy, &outNz);
      if (index >= store.userCircleNormals.size() / 3 && !IsFlatNormal(outNx, outNy, outNz))
        EnsureCircleNormals(store.userCircleNormals, store.circleCount());
      if (index < store.userCircleNormals.size() / 3) {
        const std::size_t nk = index * 3;
        store.userCircleNormals[nk + 0] = outNx;
        store.userCircleNormals[nk + 1] = outNy;
        store.userCircleNormals[nk + 2] = outNz;
      }
    }
  }
  return true;
}

inline bool TransformArc(CadEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!xf.valid() || index >= store.userArcs.size())
    return false;
  CadArc& a = store.userArcs[index];

  // For a reflection, handedness reverses (CCW becomes CW), so the old end point becomes the new
  // start point while sweepRad keeps its signed magnitude — exact in full 3D for any mirror plane.
  const bool needReanchor = !xf.isIdentityRotation();
  ray3d::Vec3 anchorWorld{};
  if (needReanchor) {
    const double anchorAngle = xf.isReflection()
                                   ? (static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad))
                                   : static_cast<double>(a.startRad);
    anchorWorld = CurveWorldPointOnArc(a, anchorAngle);
  }

  const ray3d::Vec3 nc = xf.applyPoint({a.cx, a.cy, a.z});
  a.cx = nc.x;
  a.cy = nc.y;
  a.z = nc.z;
  a.r *= xf.scale();

  if (needReanchor) {
    const bool wasFlat = IsFlatNormal(a.nx, a.ny, a.nz);
    const ray3d::Vec3 nn = xf.applyUnitDirection(
        {static_cast<double>(a.nx), static_cast<double>(a.ny), static_cast<double>(a.nz)});
    if (ray3d::Length(nn) > 0.5)
      entityops_detail::StoreUnitNormal(nn, wasFlat, &a.nx, &a.ny, &a.nz);
    const ray3d::Vec3 anchorMapped = xf.applyPoint(anchorWorld);
    const ucs::Point2D local = ucs::WorldToPlane(CurvePlane(a), anchorMapped);
    if (std::hypot(local.x, local.y) > 1e-12)
      a.startRad = static_cast<float>(std::atan2(local.y, local.x));
  }
  return true;
}

/// Transform a polyline stored in the block-capable core `CadEntityStore` (no vertex normal sidecar).
///
/// Straight polylines accept any 3D similarity transform. A polyline carrying any nonzero bulge
/// requires a planar-Z transform (`xf.isPlanarZ()`), because `CadEntityStore` has no per-vertex
/// normal sidecar to store a tilted or inverted bulge plane; out-of-plane transforms on bulged
/// block-store polylines return false without mutating the store (use `CadDrawingEntityStore` for
/// full 3D bulged polyline transforms).
inline bool TransformPolyline(CadEntityStore& store, std::size_t index,
                              const SimilarityTransform& xf) {
  if (!xf.valid())
    return false;
  int v0 = 0, v1 = 0;
  if (!entityops_detail::ValidPolylineRange(store, index, &v0, &v1))
    return false;
  if (!xf.isPlanarZ()) {
    for (int vi = v0; vi < v1; ++vi) {
      const std::size_t k = static_cast<std::size_t>(vi);
      if (k < store.userPolylineVertsBulge.size() && store.userPolylineVertsBulge[k] != 0.f)
        return false;
    }
  }
  for (int vi = v0; vi < v1; ++vi) {
    const std::size_t k = static_cast<std::size_t>(vi);
    const CadPolyVertex v = store.vertex(k);
    const ray3d::Vec3 p = xf.applyPoint({v.x, v.y, v.z});
    store.setVertex(k, CadPolyVertex{p.x, p.y, p.z});
    if (xf.isReflection() && k < store.userPolylineVertsBulge.size() &&
        store.userPolylineVertsBulge[k] != 0.f) {
      store.userPolylineVertsBulge[k] = -store.userPolylineVertsBulge[k];
    }
  }
  return true;
}

/// Transform a polyline stored in `CadDrawingEntityStore`, including per-vertex bulge reflection
/// and curve-plane normal transformation for bulged or tilted segments.
inline bool TransformPolyline(CadDrawingEntityStore& store, std::size_t index,
                              const SimilarityTransform& xf) {
  if (!xf.valid())
    return false;
  int v0 = 0, v1 = 0;
  if (!entityops_detail::ValidPolylineRange(store, index, &v0, &v1))
    return false;
  const bool updateNormals = !xf.isIdentityRotation();
  for (int vi = v0; vi < v1; ++vi) {
    const std::size_t k = static_cast<std::size_t>(vi);
    const CadPolyVertex v = store.vertex(k);
    const ray3d::Vec3 p = xf.applyPoint({v.x, v.y, v.z});
    store.setVertex(k, CadPolyVertex{p.x, p.y, p.z});

    const float bulge =
        k < store.userPolylineVertsBulge.size() ? store.userPolylineVertsBulge[k] : 0.f;
    if (xf.isReflection() && k < store.userPolylineVertsBulge.size() && bulge != 0.f)
      store.userPolylineVertsBulge[k] = -bulge;

    if (updateNormals) {
      const bool haveNormal = k < store.userPolylineVertsNormal.size() / 3;
      const std::size_t nk = k * 3;
      const float nx = haveNormal ? store.userPolylineVertsNormal[nk + 0] : kFlatNormalX;
      const float ny = haveNormal ? store.userPolylineVertsNormal[nk + 1] : kFlatNormalY;
      const float nz = haveNormal ? store.userPolylineVertsNormal[nk + 2] : kFlatNormalZ;
      const bool wasFlat = IsFlatNormal(nx, ny, nz);
      // Straight segments never consult their normal (cadblock.hpp), so only transform normals
      // that carry a bulge or are already tilted.
      if (bulge != 0.f || !wasFlat) {
        const ray3d::Vec3 nn = xf.applyUnitDirection(
            {static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)});
        if (ray3d::Length(nn) > 0.5) {
          float outNx = kFlatNormalX, outNy = kFlatNormalY, outNz = kFlatNormalZ;
          entityops_detail::StoreUnitNormal(nn, wasFlat, &outNx, &outNy, &outNz);
          if (!haveNormal && !IsFlatNormal(outNx, outNy, outNz))
            entityops_detail::SyncPolylineNormalVec(store.userPolylineVertsNormal,
                                                    store.vertexCount());
          if (k < store.userPolylineVertsNormal.size() / 3) {
            store.userPolylineVertsNormal[nk + 0] = outNx;
            store.userPolylineVertsNormal[nk + 1] = outNy;
            store.userPolylineVertsNormal[nk + 2] = outNz;
          }
        }
      }
    }
  }
  return true;
}

/// Transform a 3D NURBS spline in place via `spline::MapPoints` (building on `MapSelectedSplines`).
inline bool TransformSpline(CadDrawingEntityStore& store, std::size_t index,
                            const SimilarityTransform& xf) {
  if (!xf.valid() || index >= store.cadSplines.size())
    return false;
  spline::MapPoints(&store.cadSplines[index],
                    [&](const spline::Vec3& v) { return xf.applyPoint(v); });
  return true;
}

/// Transform a CAD POINT in place (building on `MapSelectedCadPoints`).
inline bool TransformPoint(CadDrawingEntityStore& store, std::size_t index,
                           const SimilarityTransform& xf) {
  if (!xf.valid() || index >= store.cadPoints.size())
    return false;
  CadPoint& pt = store.cadPoints[index];
  const ray3d::Vec3 mapped = xf.applyPoint({pt.x, pt.y, pt.z});
  pt.x = mapped.x;
  pt.y = mapped.y;
  pt.z = mapped.z;
  return true;
}

// -----------------------------------------------------------------------------------------------
// Per-kind 3D bounding boxes (Phase 1: Line, Circle, Arc, Polyline, Spline, Point)
// -----------------------------------------------------------------------------------------------

[[nodiscard]] inline EntityBounds3d LineBounds(const CadEntityStore& store, std::size_t index) {
  EntityBounds3d b;
  if (index >= store.lineCount())
    return b;
  const CadLineRecord ln = store.line(index);
  b.expand({ln.x0, ln.y0, ln.z0});
  b.expand({ln.x1, ln.y1, ln.z1});
  return b;
}

[[nodiscard]] inline EntityBounds3d CircleBounds(const CadEntityStore& store, std::size_t index) {
  EntityBounds3d b;
  if (index >= store.circleCount())
    return b;
  const CadCircleRecord c = store.circle(index);
  const double r = std::fabs(c.r);
  if (!(r > 1e-12) || !std::isfinite(r))
    return b;
  float nx = kFlatNormalX, ny = kFlatNormalY, nz = kFlatNormalZ;
  CircleNormalAt(store.userCircleNormals, index, &nx, &ny, &nz);
  const ray3d::Vec3 n =
      ray3d::Normalize({static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)});
  if (ray3d::Length(n) < 0.5) {
    // Degenerate stored normal falls back to flat world XY, matching CurvePlane.
    b.expand({c.cx - r, c.cy - r, c.cz});
    b.expand({c.cx + r, c.cy + r, c.cz});
    return b;
  }
  // Use hypot of the other two unit-normal components rather than sqrt(1 - n_k^2) to avoid
  // catastrophic cancellation when the normal is nearly axis-aligned (e.g. (1e-8, 0, 1)).
  const double dx = r * std::hypot(n.y, n.z);
  const double dy = r * std::hypot(n.x, n.z);
  const double dz = r * std::hypot(n.x, n.y);
  b.expand({c.cx - dx, c.cy - dy, c.cz - dz});
  b.expand({c.cx + dx, c.cy + dy, c.cz + dz});
  return b;
}

[[nodiscard]] inline EntityBounds3d ArcBounds(const CadEntityStore& store, std::size_t index) {
  EntityBounds3d b;
  if (index >= store.userArcs.size())
    return b;
  const CadArc& a = store.userArcs[index];
  const double r = std::fabs(a.r);
  if (!(r > 1e-12) || !std::isfinite(r))
    return b;
  entityops_detail::ExpandBoundsWithArc3d(b, CurvePlane(a), r, static_cast<double>(a.startRad),
                                          static_cast<double>(a.sweepRad));
  return b;
}

[[nodiscard]] inline EntityBounds3d PolylineBounds(const CadEntityStore& store, std::size_t index) {
  EntityBounds3d b;
  int v0 = 0, v1 = 0;
  if (!entityops_detail::ValidPolylineRange(store, index, &v0, &v1))
    return b;
  const bool closed = index < store.userPolylineClosed.size() && store.userPolylineClosed[index] != 0;
  const int nv = v1 - v0;
  if (nv == 1) {
    const CadPolyVertex v = store.vertex(static_cast<std::size_t>(v0));
    b.expand({v.x, v.y, v.z});
    return b;
  }
  const int segCount = closed ? nv : (nv - 1);
  for (int s = 0; s < segCount; ++s) {
    const std::size_t ia = static_cast<std::size_t>(v0 + s);
    const std::size_t ib = static_cast<std::size_t>(v0 + ((s + 1) % nv));
    const CadPolyVertex va = store.vertex(ia);
    const CadPolyVertex vb = store.vertex(ib);
    const float bulge =
        ia < store.userPolylineVertsBulge.size() ? store.userPolylineVertsBulge[ia] : 0.f;
    entityops_detail::ExpandBoundsWithPolylineSegment(
        b, {va.x, va.y, va.z}, {vb.x, vb.y, vb.z}, bulge, kFlatNormalX, kFlatNormalY, kFlatNormalZ);
  }
  return b;
}

[[nodiscard]] inline EntityBounds3d PolylineBounds(const CadDrawingEntityStore& store,
                                                   std::size_t index) {
  EntityBounds3d b;
  int v0 = 0, v1 = 0;
  if (!entityops_detail::ValidPolylineRange(store, index, &v0, &v1))
    return b;
  const bool closed = index < store.userPolylineClosed.size() && store.userPolylineClosed[index] != 0;
  const int nv = v1 - v0;
  if (nv == 1) {
    const CadPolyVertex v = store.vertex(static_cast<std::size_t>(v0));
    b.expand({v.x, v.y, v.z});
    return b;
  }
  const int segCount = closed ? nv : (nv - 1);
  for (int s = 0; s < segCount; ++s) {
    const std::size_t ia = static_cast<std::size_t>(v0 + s);
    const std::size_t ib = static_cast<std::size_t>(v0 + ((s + 1) % nv));
    const CadPolyVertex va = store.vertex(ia);
    const CadPolyVertex vb = store.vertex(ib);
    const float bulge =
        ia < store.userPolylineVertsBulge.size() ? store.userPolylineVertsBulge[ia] : 0.f;
    float nx = kFlatNormalX, ny = kFlatNormalY, nz = kFlatNormalZ;
    if (ia < store.userPolylineVertsNormal.size() / 3) {
      nx = store.userPolylineVertsNormal[ia * 3 + 0];
      ny = store.userPolylineVertsNormal[ia * 3 + 1];
      nz = store.userPolylineVertsNormal[ia * 3 + 2];
    }
    entityops_detail::ExpandBoundsWithPolylineSegment(b, {va.x, va.y, va.z}, {vb.x, vb.y, vb.z},
                                                      bulge, nx, ny, nz);
  }
  return b;
}

/// Conservative 3D bounding box of a NURBS spline.
///
/// Tessellates the curve within `kSplineSampleTolerance`. For degree > 1 (curved splines), pads
/// the sampled vertex box by `kSplineSampleTolerance` so an off-sample extremum can never protrude
/// outside the returned box, then clamps to the control polygon's convex-hull box (when all weights
/// are positive) so flat or linear spans are never inflated out of their plane.
[[nodiscard]] inline EntityBounds3d SplineBounds(const CadDrawingEntityStore& store,
                                                 std::size_t index) {
  EntityBounds3d b;
  if (index >= store.cadSplines.size())
    return b;
  const CadSpline& sp = store.cadSplines[index];
  std::vector<ray3d::Vec3> pts;
  spline::Tessellate(sp, kSplineSampleTolerance, &pts);
  for (const ray3d::Vec3& p : pts)
    b.expand(p);
  if (!b.valid || sp.degree <= 1)
    return b;

  // Pad by chord tolerance to guarantee the returned box encloses every point on the curve.
  b.min.x -= kSplineSampleTolerance;
  b.min.y -= kSplineSampleTolerance;
  b.min.z -= kSplineSampleTolerance;
  b.max.x += kSplineSampleTolerance;
  b.max.y += kSplineSampleTolerance;
  b.max.z += kSplineSampleTolerance;

  // A B-spline with positive weights lies strictly within the convex hull of its control points;
  // clamping keeps planar splines exact in their off-plane coordinate.
  bool positiveWeights = true;
  for (double w : sp.weights) {
    if (!(w > 0.0) || !std::isfinite(w)) {
      positiveWeights = false;
      break;
    }
  }
  if (positiveWeights && !sp.ctrl.empty()) {
    EntityBounds3d hull;
    for (const ray3d::Vec3& c : sp.ctrl)
      hull.expand(c);
    if (hull.valid) {
      b.min.x = std::max(b.min.x, hull.min.x);
      b.min.y = std::max(b.min.y, hull.min.y);
      b.min.z = std::max(b.min.z, hull.min.z);
      b.max.x = std::min(b.max.x, hull.max.x);
      b.max.y = std::min(b.max.y, hull.max.y);
      b.max.z = std::min(b.max.z, hull.max.z);
    }
  }
  return b;
}

[[nodiscard]] inline EntityBounds3d PointBounds(const CadDrawingEntityStore& store,
                                                std::size_t index) {
  EntityBounds3d b;
  if (index >= store.cadPoints.size())
    return b;
  const CadPoint& pt = store.cadPoints[index];
  b.expand({pt.x, pt.y, pt.z});
  return b;
}

// -----------------------------------------------------------------------------------------------
// Per-kind duplication (for COPY / ARRAY = duplicate, then transform)
// -----------------------------------------------------------------------------------------------

[[nodiscard]] inline EntityRef DuplicateLine(CadEntityStore& store, std::size_t index) {
  const std::size_t n0 = store.lineCount();
  if (index >= n0)
    return {};
  const CadLineRecord ln = store.line(index);
  entityops_detail::PadAttrs(store.userLineAttrs, n0);
  const EntityAttributes attrs = DuplicateEntityAttrs(store.userLineAttrs[index]);
  store.pushLine(ln);
  store.userLineAttrs.push_back(attrs);
  return EntityRef{EntityKind::Line, static_cast<int>(n0)};
}

[[nodiscard]] inline EntityRef DuplicateCircle(CadEntityStore& store, std::size_t index) {
  const std::size_t n0 = store.circleCount();
  if (index >= n0)
    return {};
  const CadCircleRecord c = store.circle(index);
  float nx = kFlatNormalX, ny = kFlatNormalY, nz = kFlatNormalZ;
  CircleNormalAt(store.userCircleNormals, index, &nx, &ny, &nz);
  entityops_detail::PadAttrs(store.userCircleAttrs, n0);
  EnsureCircleNormals(store.userCircleNormals, n0);
  const EntityAttributes attrs = DuplicateEntityAttrs(store.userCircleAttrs[index]);
  store.pushCircle(c);
  store.userCircleAttrs.push_back(attrs);
  PushCircleNormal(store.userCircleNormals, nx, ny, nz);
  return EntityRef{EntityKind::Circle, static_cast<int>(n0)};
}

[[nodiscard]] inline EntityRef DuplicateArc(CadEntityStore& store, std::size_t index) {
  const std::size_t n0 = store.userArcs.size();
  if (index >= n0)
    return {};
  const CadArc arc = store.userArcs[index];
  entityops_detail::PadAttrs(store.userArcAttrs, n0);
  const EntityAttributes attrs = DuplicateEntityAttrs(store.userArcAttrs[index]);
  store.userArcs.push_back(arc);
  store.userArcAttrs.push_back(attrs);
  return EntityRef{EntityKind::Arc, static_cast<int>(n0)};
}

[[nodiscard]] inline EntityRef DuplicatePolyline(CadEntityStore& store, std::size_t index) {
  int v0 = 0, v1 = 0;
  if (!entityops_detail::ValidPolylineRange(store, index, &v0, &v1))
    return {};
  const std::size_t n0 = store.userPolylineOffsets.size() - 1;
  const std::size_t vertsBefore = store.vertexCount();
  const int nv = v1 - v0;
  if (vertsBefore > static_cast<std::size_t>(std::numeric_limits<int>::max()) - static_cast<std::size_t>(nv) ||
      store.userPolylineOffsets.back() != static_cast<int>(vertsBefore))
    return {};

  std::vector<CadPolyVertex> verts;
  std::vector<float> bulges;
  verts.reserve(static_cast<std::size_t>(nv));
  const bool keepBulge = !store.userPolylineVertsBulge.empty();
  if (keepBulge) {
    store.userPolylineVertsBulge.resize(vertsBefore, 0.f);
    bulges.reserve(static_cast<std::size_t>(nv));
  }
  for (int vi = v0; vi < v1; ++vi) {
    const std::size_t k = static_cast<std::size_t>(vi);
    verts.push_back(store.vertex(k));
    if (keepBulge)
      bulges.push_back(store.userPolylineVertsBulge[k]);
  }
  const std::uint8_t closed =
      index < store.userPolylineClosed.size() ? store.userPolylineClosed[index] : std::uint8_t{0};
  entityops_detail::PadAttrs(store.userPolylineAttrs, n0);
  if (store.userPolylineClosed.size() < n0)
    store.userPolylineClosed.resize(n0, 0);
  const EntityAttributes attrs = DuplicateEntityAttrs(store.userPolylineAttrs[index]);

  const int baseVert = store.userPolylineOffsets.back();
  for (const CadPolyVertex& v : verts)
    store.pushVertex(v);
  if (keepBulge)
    store.userPolylineVertsBulge.insert(store.userPolylineVertsBulge.end(), bulges.begin(),
                                        bulges.end());
  store.userPolylineOffsets.push_back(baseVert + nv);
  store.userPolylineClosed.push_back(closed);
  store.userPolylineAttrs.push_back(attrs);
  return EntityRef{EntityKind::Polyline, static_cast<int>(n0)};
}

[[nodiscard]] inline EntityRef DuplicatePolyline(CadDrawingEntityStore& store, std::size_t index) {
  int v0 = 0, v1 = 0;
  if (!entityops_detail::ValidPolylineRange(store, index, &v0, &v1))
    return {};
  const std::size_t vertsBefore = store.vertexCount();
  const bool keepNormal = !store.userPolylineVertsNormal.empty();
  std::vector<float> copiedNormals;
  if (keepNormal) {
    entityops_detail::SyncPolylineNormalVec(store.userPolylineVertsNormal, vertsBefore);
    copiedNormals.reserve(static_cast<std::size_t>(v1 - v0) * 3);
    for (int vi = v0; vi < v1; ++vi) {
      const std::size_t nk = static_cast<std::size_t>(vi) * 3;
      copiedNormals.push_back(store.userPolylineVertsNormal[nk + 0]);
      copiedNormals.push_back(store.userPolylineVertsNormal[nk + 1]);
      copiedNormals.push_back(store.userPolylineVertsNormal[nk + 2]);
    }
  }
  const EntityRef ref = DuplicatePolyline(static_cast<CadEntityStore&>(store), index);
  if (!ref.valid())
    return {};
  if (keepNormal)
    store.userPolylineVertsNormal.insert(store.userPolylineVertsNormal.end(),
                                         copiedNormals.begin(), copiedNormals.end());
  return ref;
}

[[nodiscard]] inline EntityRef DuplicateSpline(CadDrawingEntityStore& store, std::size_t index) {
  const std::size_t n0 = store.cadSplines.size();
  if (index >= n0)
    return {};
  CadSpline copy = store.cadSplines[index];
  entityops_detail::PadAttrs(store.cadSplineAttrs, n0);
  const EntityAttributes attrs = DuplicateEntityAttrs(store.cadSplineAttrs[index]);
  store.cadSplines.push_back(std::move(copy));
  store.cadSplineAttrs.push_back(attrs);
  return EntityRef{EntityKind::Spline, static_cast<int>(n0)};
}

[[nodiscard]] inline EntityRef DuplicatePoint(CadDrawingEntityStore& store, std::size_t index) {
  const std::size_t n0 = store.cadPoints.size();
  if (index >= n0)
    return {};
  const CadPoint copy = store.cadPoints[index];
  entityops_detail::PadAttrs(store.cadPointAttrs, n0);
  const EntityAttributes attrs = DuplicateEntityAttrs(store.cadPointAttrs[index]);
  store.cadPoints.push_back(copy);
  store.cadPointAttrs.push_back(attrs);
  return EntityRef{EntityKind::Point, static_cast<int>(n0)};
}

// -----------------------------------------------------------------------------------------------
// Remaining Document kinds (#1577): ellipses, annotations, tables, block references, hatch fills,
// meshes, solids, pipe runs, multileaders and Position Markers. Each follows AutoCAD 2025, checked
// with accoreconsole.exe (see tools/autocad-transform-oracle/): text stays readable under MIRROR
// (MIRRTEXT 0), a hatch pattern keeps its angle under MIRROR (MIRRHATCH 0), and SCALE is uniform in
// X, Y and Z about the base point.
// -----------------------------------------------------------------------------------------------

/// What an entity-kind operation needs to know beyond the transform itself.
struct EntityTransformContext {
  /// Model units per plotted inch for annotation text. MIRROR re-justifies single-line TEXT around its
  /// width so it stays readable, the way AutoCAD does with MIRRTEXT 0.
  double textUnitsPerPlottedInch = 1.0;
  /// The drawn width of a TEXT in model units, when the caller can measure it (Commands measures
  /// with the renderer's font). Empty: the width is estimated from \ref textUnitsPerPlottedInch.
  std::function<double(const CadAnnotation&)> textWidth;
};

namespace entityops_detail {

/// The plan part of a transform that keeps world +Z (`SimilarityTransform::isPlanarZ`).
///
/// `angle` is the rotation for a proper motion. For a reflection it is `2*phi`, where `phi` is the
/// direction of the mirror line, so a direction at angle `a` maps to `angle - a`.
struct PlanarParts {
  bool reflected = false;
  double angle = 0.0;
  double scale = 1.0;
};

[[nodiscard]] inline PlanarParts DecomposePlanar(const SimilarityTransform& xf) {
  return {xf.isReflection(), std::atan2(xf.orthogonal(1, 0), xf.orthogonal(0, 0)), xf.scale()};
}

/// Where a plan direction at angle \p a points after \p p.
[[nodiscard]] inline double MapPlanAngle(const PlanarParts& p, double a) {
  return p.reflected ? p.angle - a : a + p.angle;
}

/// The angle text reads at after a reflection with MIRRTEXT 0: the mirrored direction turned half a
/// turn, so the text runs forwards in its mirrored box.
[[nodiscard]] inline double MirroredReadableAngle(const PlanarParts& p, double a) {
  return CadAngNormalizeMinusPiToPi(p.angle - a + kPi);
}

template <class T>
inline void MapPlanPoint(const SimilarityTransform& xf, T* x, T* y) {
  const ray3d::Vec3 q = xf.applyPoint({static_cast<double>(*x), static_cast<double>(*y), 0.0});
  *x = static_cast<T>(q.x);
  *y = static_cast<T>(q.y);
}

/// Maps an elevation through a planar-Z transform (Z does not depend on X or Y there).
template <class T>
inline void MapPlanZ(const SimilarityTransform& xf, T* z) {
  *z = static_cast<T>(xf.applyPoint({0.0, 0.0, static_cast<double>(*z)}).z);
}

template <class T>
inline void MapPoint3(const SimilarityTransform& xf, T* x, T* y, T* z) {
  const ray3d::Vec3 q =
      xf.applyPoint({static_cast<double>(*x), static_cast<double>(*y), static_cast<double>(*z)});
  *x = static_cast<T>(q.x);
  *y = static_cast<T>(q.y);
  *z = static_cast<T>(q.z);
}

template <class Vec>
inline void MapXyzTriplets(const SimilarityTransform& xf, Vec& xyz) {
  for (std::size_t k = 0; k + 2 < xyz.size(); k += 3)
    MapPoint3(xf, &xyz[k], &xyz[k + 1], &xyz[k + 2]);
}

/// MTEXT attachment (1..9) with its left and right columns swapped — the attachment a mirrored
/// MTEXT takes so its frame lands on the mirror image of the old one (AutoCAD: TL becomes TR).
[[nodiscard]] inline int MirroredMtextAttachment(int attach) {
  const int k = std::clamp(attach, 1, 9) - 1;
  const int column = k % 3;
  return (k - column) + (2 - column) + 1;
}

/// MTEXT (or an MTEXT label) under a planar transform. The insertion maps, the frame scales, and the
/// text turns with a rotation. Under a reflection it stays readable: the angle is mirrored and turned
/// half a turn, and the attachment swaps sides, so the frame lands on the mirror image of the old one.
inline void TransformMtextFrame(CadAnnotation& a, const SimilarityTransform& xf, const PlanarParts& p) {
  const float s = static_cast<float>(p.scale);
  if (!p.reflected) {
    CadMtextTransform(a, [&](float* x, float* y) { MapPlanPoint(xf, x, y); },
                      static_cast<float>(p.angle), s);
    return;
  }
  const float w = CadMtextFrameWidth(a) * s;
  const float h = CadMtextFrameHeight(a) * s;
  CadMtextSyncInsertion(a);
  MapPlanPoint(xf, &a.insX, &a.insY);
  a.rotationRad = static_cast<float>(MirroredReadableAngle(p, static_cast<double>(a.rotationRad)));
  a.mtextAttach = MirroredMtextAttachment(a.mtextAttach);
  CadMtextPlaceFrame(a, w, h);
}

inline void ScalePlottedHeight(float* plottedHeightInches, double s) {
  if (s != 1.0)
    *plottedHeightInches = std::max(static_cast<float>(*plottedHeightInches * s), 1.e-6f);
}

/// A linear dimension's measuring direction after \p xf: AutoCAD turns it with the dimension, so a
/// quarter turn makes a horizontal dimension vertical. GoSurvey stores only horizontal or vertical,
/// so the result snaps to whichever the turned direction is closer to.
[[nodiscard]] inline bool MappedDimLinearVertical(const SimilarityTransform& xf, bool vertical) {
  const ray3d::Vec3 d = xf.applyUnitDirection(vertical ? ray3d::Vec3{0.0, 1.0, 0.0} : ray3d::Vec3{1.0, 0.0, 0.0});
  return std::fabs(d.y) > std::fabs(d.x);
}

inline void TransformDimLinear(CadAnnotation& a, const SimilarityTransform& xf) {
  const double cmx = 0.5 * (a.dimExt1X + a.dimExt2X);
  const double cmy = 0.5 * (a.dimExt1Y + a.dimExt2Y);
  double dmx = cmx, dmy = cmy;
  if (a.dimLinearVertical)
    dmx += a.dimSignedOffset;
  else
    dmy += a.dimSignedOffset;
  MapPlanPoint(xf, &a.dimExt1X, &a.dimExt1Y);
  MapPlanPoint(xf, &a.dimExt2X, &a.dimExt2Y);
  MapPlanPoint(xf, &dmx, &dmy);
  MapPlanPoint(xf, &a.insX, &a.insY);
  a.dimLinearVertical = MappedDimLinearVertical(xf, a.dimLinearVertical);
  const double ncmx = 0.5 * (a.dimExt1X + a.dimExt2X);
  const double ncmy = 0.5 * (a.dimExt1Y + a.dimExt2Y);
  a.dimSignedOffset = a.dimLinearVertical ? dmx - ncmx : dmy - ncmy;
  // A linear dimension's text runs along its measuring direction, read upright.
  a.rotationRad = a.dimLinearVertical ? static_cast<float>(kPi / 2.0) : 0.f;
}

inline void TransformDimAligned(CadAnnotation& a, const SimilarityTransform& xf, const PlanarParts& p) {
  MapPlanPoint(xf, &a.dimExt1X, &a.dimExt1Y);
  MapPlanPoint(xf, &a.dimExt2X, &a.dimExt2Y);
  MapPlanPoint(xf, &a.insX, &a.insY);
  // The dimension line sits at offset * (-t.y, t.x) from the chord. A reflection reverses that
  // normal relative to the mirrored chord, so the offset changes sign to keep the line where the
  // mirror puts it.
  a.dimSignedOffset *= p.reflected ? -p.scale : p.scale;
  const double vx = a.dimExt2X - a.dimExt1X, vy = a.dimExt2Y - a.dimExt1Y;
  if (std::hypot(vx, vy) >= 1.e-8)
    a.rotationRad = CadDimReadableTextRotation(static_cast<float>(std::atan2(vy, vx)));
}

inline void TransformDimAngular(CadAnnotation& a, const SimilarityTransform& xf, const PlanarParts& p) {
  MapPlanPoint(xf, &a.dimAngVertexX, &a.dimAngVertexY);
  MapPlanPoint(xf, &a.dimExt1X, &a.dimExt1Y);
  MapPlanPoint(xf, &a.dimExt2X, &a.dimExt2Y);
  MapPlanPoint(xf, &a.insX, &a.insY);
  a.dimSignedOffset *= p.scale;  // the arc radius
}

/// Any other annotation kind: the insertion and the four box corners map, and the box is refitted.
inline void TransformAnnotationBox(CadAnnotation& a, const SimilarityTransform& xf) {
  float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
  float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
  for (int i = 0; i < 4; ++i)
    MapPlanPoint(xf, &xs[i], &ys[i]);
  a.boxMinX = *std::min_element(xs, xs + 4);
  a.boxMaxX = *std::max_element(xs, xs + 4);
  a.boxMinY = *std::min_element(ys, ys + 4);
  a.boxMaxY = *std::max_element(ys, ys + 4);
  a.insX = a.boxMinX;
  a.insY = a.boxMinY;
}

/// Rotation matrix `Rx(rotX) * Ry(rotY) * Rz(rotZ)` of an insert — the order \ref CadBlockXformPoint
/// applies them in (Z first).
inline void BlockRotationMatrix(const CadBlockXform& x, double m[3][3]) {
  const double ca = std::cos(static_cast<double>(x.rotX)), sa = std::sin(static_cast<double>(x.rotX));
  const double cb = std::cos(static_cast<double>(x.rotY)), sb = std::sin(static_cast<double>(x.rotY));
  const double cc = std::cos(x.rotZ), sc = std::sin(x.rotZ);
  m[0][0] = cb * cc;                 m[0][1] = -cb * sc;                m[0][2] = sb;
  m[1][0] = ca * sc + sa * sb * cc;  m[1][1] = ca * cc - sa * sb * sc;  m[1][2] = -sa * cb;
  m[2][0] = sa * sc - ca * sb * cc;  m[2][1] = sa * cc + ca * sb * sc;  m[2][2] = ca * cb;
}

/// Euler angles of a proper rotation \p m in the \ref BlockRotationMatrix convention.
inline void BlockEulerFromMatrix(const double m[3][3], CadBlockXform* x) {
  const double sb = std::clamp(m[0][2], -1.0, 1.0);
  const double b = std::asin(sb);
  double a = 0.0, c = 0.0;
  if (std::fabs(sb) < 1.0 - 1e-12) {
    a = std::atan2(-m[1][2], m[2][2]);
    c = std::atan2(-m[0][1], m[0][0]);
  } else {
    a = std::atan2(m[2][1], m[1][1]);  // gimbal lock: fold the Z turn into X
  }
  x->rotX = static_cast<float>(a);
  x->rotY = static_cast<float>(b);
  x->rotZ = c;
}

/// Exact 3D AABB of the elliptical span `C + U cos t + V sin t` for t in [start, start + sweep].
inline void ExpandBoundsWithEllipse3d(EntityBounds3d& b, const ray3d::Vec3& c, const ray3d::Vec3& u,
                                      const ray3d::Vec3& v, double start, double sweep) {
  auto at = [&](double t) {
    return ray3d::Add(c, ray3d::Add(ray3d::Scale(u, std::cos(t)), ray3d::Scale(v, std::sin(t))));
  };
  b.expand(at(start));
  b.expand(at(start + sweep));
  const double us[3] = {u.x, u.y, u.z};
  const double vs[3] = {v.x, v.y, v.z};
  for (int axis = 0; axis < 3; ++axis) {
    if (std::hypot(us[axis], vs[axis]) <= 1e-15)
      continue;
    const double t0 = std::atan2(vs[axis], us[axis]);
    if (AngleInSweep(t0, start, sweep))
      b.expand(at(t0));
    if (AngleInSweep(t0 + kPi, start, sweep))
      b.expand(at(t0 + kPi));
  }
}

/// World major (`U`) and minor (`V`) semi-axis vectors of \p e: `EllipseWorldPointAt(e, t) = C + U cos t + V sin t`.
inline void EllipseWorldAxes(const CadEllipse& e, ray3d::Vec3* u, ray3d::Vec3* v) {
  const ucs::Ucs plane = CurvePlane(e);
  const double mx = static_cast<double>(e.majVx), my = static_cast<double>(e.majVy);
  const double r = static_cast<double>(e.ratio);
  *u = ray3d::Add(ray3d::Scale(plane.xAxis, mx), ray3d::Scale(plane.yAxis, my));
  *v = ray3d::Add(ray3d::Scale(plane.xAxis, -my * r), ray3d::Scale(plane.yAxis, mx * r));
}

/// A proper rotation as axis and angle, for kernels that take that form (brep::Rotate).
inline bool AxisAngleFromRotation(const double m[3][3], ray3d::Vec3* axis, double* angle) {
  // Shepperd's quaternion extraction: stable at every angle, including half a turn.
  const double tr = m[0][0] + m[1][1] + m[2][2];
  double w = 0.0, x = 0.0, y = 0.0, z = 0.0;
  if (tr > m[0][0] && tr > m[1][1] && tr > m[2][2]) {
    const double s = 2.0 * std::sqrt(std::max(0.0, 1.0 + tr));
    w = 0.25 * s;
    x = (m[2][1] - m[1][2]) / s;
    y = (m[0][2] - m[2][0]) / s;
    z = (m[1][0] - m[0][1]) / s;
  } else if (m[0][0] >= m[1][1] && m[0][0] >= m[2][2]) {
    const double s = 2.0 * std::sqrt(std::max(0.0, 1.0 + m[0][0] - m[1][1] - m[2][2]));
    w = (m[2][1] - m[1][2]) / s;
    x = 0.25 * s;
    y = (m[0][1] + m[1][0]) / s;
    z = (m[0][2] + m[2][0]) / s;
  } else if (m[1][1] >= m[2][2]) {
    const double s = 2.0 * std::sqrt(std::max(0.0, 1.0 + m[1][1] - m[0][0] - m[2][2]));
    w = (m[0][2] - m[2][0]) / s;
    x = (m[0][1] + m[1][0]) / s;
    y = 0.25 * s;
    z = (m[1][2] + m[2][1]) / s;
  } else {
    const double s = 2.0 * std::sqrt(std::max(0.0, 1.0 + m[2][2] - m[0][0] - m[1][1]));
    w = (m[1][0] - m[0][1]) / s;
    x = (m[0][2] + m[2][0]) / s;
    y = (m[1][2] + m[2][1]) / s;
    z = 0.25 * s;
  }
  const double vlen = std::sqrt(x * x + y * y + z * z);
  if (!(vlen > 1e-15)) {
    *axis = {0.0, 0.0, 1.0};
    *angle = 0.0;
    return true;
  }
  *axis = {x / vlen, y / vlen, z / vlen};
  *angle = 2.0 * std::atan2(vlen, w);
  return std::isfinite(*angle);
}

template <class T>
[[nodiscard]] inline EntityRef DuplicateInVector(std::vector<T>& items, std::vector<EntityAttributes>& attrs,
                                                 std::size_t index, EntityKind kind) {
  const std::size_t n0 = items.size();
  if (index >= n0)
    return {};
  T copy = items[index];
  PadAttrs(attrs, n0);
  const EntityAttributes a = DuplicateEntityAttrs(attrs[index]);
  items.push_back(std::move(copy));
  attrs.push_back(a);
  return EntityRef{kind, static_cast<int>(n0)};
}

}  // namespace entityops_detail

/// True when \p kind can represent the result of \p xf. Check before duplicating, so a COPY never
/// leaves an untransformed duplicate behind. One case it cannot rule out: the B-rep kernel may still
/// refuse a solid (\ref TransformSolid returns false), so a duplicate-then-transform caller must
/// remove a solid copy whose transform failed.
///
/// A hatch takes any transform because its boundary is bare points. It stores no plane normal, so
/// after a tilted rotation its pattern is still laid out in plan, which is the existing ROTATE3D
/// behaviour and a known gap against AutoCAD.
///
/// Annotations, tables and multileaders have no plane normal, so they take only transforms that keep
/// world +Z (move, plan rotate, scale, mirror across a vertical plane). A Position Marker is a
/// surveyed position (REQ-359): it only moves. Kinds that other systems own (`FeatureLine`,
/// `Surface`, `PointCloud`) are not transformed here.
[[nodiscard]] inline bool SupportsEntityTransform(EntityKind kind, const SimilarityTransform& xf) {
  if (!xf.valid())
    return false;
  switch (kind) {
  case EntityKind::Annotation:
  case EntityKind::Table:
  case EntityKind::Multileader:
    return xf.isPlanarZ();
  case EntityKind::PositionMarker:
    return xf.isTranslationOnly();
  case EntityKind::FeatureLine:
  case EntityKind::Surface:
  case EntityKind::PointCloud:
    return false;
  default:
    return true;
  }
}

/// Ellipse or elliptical arc in any plane. The centre and both axes map; under a reflection the
/// parametrisation runs backwards, so the span becomes `[-(start + sweep), -start]`.
inline bool TransformEllipse(CadEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!xf.valid() || index >= store.userEllipses.size())
    return false;
  CadEllipse& e = store.userEllipses[index];
  ray3d::Vec3 u{}, v{};
  entityops_detail::EllipseWorldAxes(e, &u, &v);
  const ray3d::Vec3 c = xf.applyPoint({e.cx, e.cy, e.z});
  const ray3d::Vec3 u2 = xf.applyVector(u);
  const bool wasFlat = IsFlatNormal(e.nx, e.ny, e.nz);
  const ray3d::Vec3 n2 = xf.applyUnitDirection(
      {static_cast<double>(e.nx), static_cast<double>(e.ny), static_cast<double>(e.nz)});
  e.cx = c.x;
  e.cy = c.y;
  e.z = c.z;
  if (ray3d::Length(n2) > 0.5)
    entityops_detail::StoreUnitNormal(n2, wasFlat, &e.nx, &e.ny, &e.nz);
  const ucs::Ucs plane = CurvePlane(e);
  e.majVx = static_cast<float>(ray3d::Dot(u2, plane.xAxis));
  e.majVy = static_cast<float>(ray3d::Dot(u2, plane.yAxis));
  if (xf.isReflection() && !EllipseIsFullTurn(e))
    e.startRad = -(e.startRad + e.sweepRad);
  return true;
}

/// TEXT, MTEXT and dimensions under a transform that keeps world +Z. Returns false otherwise (no
/// plane normal to tip). The dimension measurement text is not refreshed here: it depends on the
/// dimension style, which the caller resolves.
inline bool TransformAnnotation(CadDrawingEntityStore& store, std::size_t index, const SimilarityTransform& xf,
                                const EntityTransformContext& ctx = {}) {
  if (!SupportsEntityTransform(EntityKind::Annotation, xf) || index >= store.cadAnnotations.size())
    return false;
  using entityops_detail::MapPlanPoint;
  CadAnnotation& a = store.cadAnnotations[index];
  const entityops_detail::PlanarParts p = entityops_detail::DecomposePlanar(xf);
  entityops_detail::MapPlanZ(xf, &a.insZ);
  switch (a.kind) {
  case CadAnnotation::Kind::Text:
    if (p.reflected) {
      // MIRRTEXT 0: mirror the far end of the baseline and run the text forwards from there, so it
      // reads normally in its mirrored box (AutoCAD moves the insertion the same way).
      const double h = static_cast<double>(a.plottedHeightInches) * ctx.textUnitsPerPlottedInch;
      const double w = ctx.textWidth ? ctx.textWidth(a) : CadTextEstimatedWidth(a.text, static_cast<float>(h));
      const double rot = static_cast<double>(a.rotationRad);
      double fx = a.insX + w * std::cos(rot), fy = a.insY + w * std::sin(rot);
      MapPlanPoint(xf, &fx, &fy);
      a.insX = static_cast<float>(fx);
      a.insY = static_cast<float>(fy);
      a.rotationRad = static_cast<float>(entityops_detail::MirroredReadableAngle(p, rot));
    } else {
      MapPlanPoint(xf, &a.insX, &a.insY);
      a.rotationRad = static_cast<float>(static_cast<double>(a.rotationRad) + p.angle);
    }
    break;
  case CadAnnotation::Kind::Mtext:
    entityops_detail::TransformMtextFrame(a, xf, p);
    break;
  case CadAnnotation::Kind::DimLinear:
    entityops_detail::TransformDimLinear(a, xf);
    break;
  case CadAnnotation::Kind::DimAligned:
    entityops_detail::TransformDimAligned(a, xf, p);
    break;
  case CadAnnotation::Kind::DimAngular:
    entityops_detail::TransformDimAngular(a, xf, p);
    break;
  default:
    entityops_detail::TransformAnnotationBox(a, xf);
    break;
  }
  entityops_detail::ScalePlottedHeight(&a.plottedHeightInches, p.scale);
  return true;
}

/// TABLE under a transform that keeps world +Z. A reflection keeps the table readable, as AutoCAD
/// does: the insertion (top-left) moves to the mirror image of the top-right corner and the table
/// turns to the mirrored, reversed direction, so its box lands on the mirror image of the old box.
inline bool TransformTable(CadDrawingEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!SupportsEntityTransform(EntityKind::Table, xf) || index >= store.cadTables.size())
    return false;
  CadTable& t = store.cadTables[index];
  const entityops_detail::PlanarParts p = entityops_detail::DecomposePlanar(xf);
  entityops_detail::MapPlanZ(xf, &t.insZ);
  if (p.reflected) {
    float trx = 0.f, try_ = 0.f;
    CadTableLocalToWorld(t, std::max(t.width, 1.e-3f), 0.f, &trx, &try_);
    entityops_detail::MapPlanPoint(xf, &trx, &try_);
    t.insX = trx;
    t.insY = try_;
    t.rotationRad =
        static_cast<float>(entityops_detail::MirroredReadableAngle(p, static_cast<double>(t.rotationRad)));
  } else {
    entityops_detail::MapPlanPoint(xf, &t.insX, &t.insY);
    t.rotationRad = static_cast<float>(static_cast<double>(t.rotationRad) + p.angle);
  }
  if (p.scale != 1.0) {
    const float s = static_cast<float>(p.scale);
    t.width = std::max(t.width * s, 1.e-3f);
    t.height = std::max(t.height * s, 1.e-3f);
    t.plottedHeightInches = std::max(t.plottedHeightInches * s, 1.e-6f);
  }
  return true;
}

/// Block reference (INSERT) under any similarity transform. A reflection becomes a negative X scale,
/// as AutoCAD stores a mirrored insert. A transform that keeps world +Z on an upright insert only
/// changes the Z rotation, so plan edits never introduce X or Y tilt.
inline bool TransformBlockRef(CadDrawingEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!xf.valid() || index >= store.cadBlockRefs.size())
    return false;
  CadBlockXform& x = store.cadBlockRefs[index].xf;
  entityops_detail::MapPoint3(xf, &x.x, &x.y, &x.z);
  if (xf.isPlanarZ() && x.rotX == 0.f && x.rotY == 0.f) {
    const entityops_detail::PlanarParts p = entityops_detail::DecomposePlanar(xf);
    if (p.reflected) {
      // M(phi) * Rz(r) = Rz(2*phi + pi - r) * diag(-1, 1).
      x.sx = -x.sx;
      x.rotZ = CadAngNormalizeMinusPiToPi(p.angle + entityops_detail::kPi - x.rotZ);
    } else {
      x.rotZ += p.angle;
    }
  } else {
    double r[3][3];
    entityops_detail::BlockRotationMatrix(x, r);
    double m[3][3];
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
        m[i][j] = xf.orthogonal(i, 0) * r[0][j] + xf.orthogonal(i, 1) * r[1][j] + xf.orthogonal(i, 2) * r[2][j];
    if (xf.isReflection()) {
      // Q R = (Q R diag(-1,1,1)) diag(-1,1,1): fold the reflection into the local X scale.
      for (int i = 0; i < 3; ++i)
        m[i][0] = -m[i][0];
      x.sx = -x.sx;
    }
    entityops_detail::BlockEulerFromMatrix(m, &x);
  }
  if (xf.scale() != 1.0) {
    const double s = xf.scale();
    x.sx = static_cast<float>(static_cast<double>(x.sx) * s);
    x.sy = static_cast<float>(static_cast<double>(x.sy) * s);
    x.sz = static_cast<float>(static_cast<double>(x.sz) * s);
  }
  return true;
}

/// HATCH fill. Every boundary vertex maps. In plan, the pattern turns with a rotation, keeps its
/// angle under a mirror (MIRRHATCH 0), and its spacing scales with SCALE.
inline bool TransformFilledRegion(CadDrawingEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!xf.valid() || index >= store.cadFilledRegions.size())
    return false;
  CadFilledRegion& fr = store.cadFilledRegions[index];
  entityops_detail::MapXyzTriplets(xf, fr.vertsXyz);
  if (xf.isPlanarZ()) {
    const entityops_detail::PlanarParts p = entityops_detail::DecomposePlanar(xf);
    if (!p.reflected && p.angle != 0.0)
      fr.patternAngleDeg = static_cast<float>(static_cast<double>(fr.patternAngleDeg) + p.angle * 180.0 / entityops_detail::kPi);
  }
  if (xf.scale() != 1.0)
    fr.patternScale = static_cast<float>(static_cast<double>(fr.patternScale) * xf.scale());
  return true;
}

/// Imported mesh. Replaces the shared payload (meshes are never written through): vertices map,
/// normals turn, and a reflection reverses each triangle's winding so the faces still point outwards.
inline bool TransformMesh(CadEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!xf.valid() || index >= store.cadMeshes.size() || !store.cadMeshes[index])
    return false;
  auto mesh = std::make_shared<CadMesh>(*store.cadMeshes[index]);
  entityops_detail::MapXyzTriplets(xf, mesh->vertsXyz);
  for (std::size_t k = 0; k + 2 < mesh->normalsXyz.size(); k += 3) {
    const ray3d::Vec3 n = xf.applyUnitDirection({mesh->normalsXyz[k], mesh->normalsXyz[k + 1], mesh->normalsXyz[k + 2]});
    mesh->normalsXyz[k] = static_cast<float>(n.x);
    mesh->normalsXyz[k + 1] = static_cast<float>(n.y);
    mesh->normalsXyz[k + 2] = static_cast<float>(n.z);
  }
  if (xf.isReflection()) {
    for (std::size_t k = 0; k + 2 < mesh->indices.size(); k += 3)
      std::swap(mesh->indices[k + 1], mesh->indices[k + 2]);
  }
  store.cadMeshes[index] = std::move(mesh);
  return true;
}

/// B-rep solid. Replaces the solid with the kernel's own scale, mirror, rotate and translate, applied
/// about the solid's bounds centre so state-plane coordinates keep their precision. Returns false,
/// leaving the solid unchanged, if the kernel refuses (\p outWhy says why).
inline bool TransformSolid(CadEntityStore& store, std::size_t index, const SimilarityTransform& xf,
                           brep::Problem* outWhy = nullptr) {
  if (!xf.valid() || index >= store.cadSolids.size() || !store.cadSolids[index])
    return false;
  brep::Problem why = brep::Problem::Ok;
  const brep::Solid& src = *store.cadSolids[index];
  const brep::Bounds bb = brep::ComputeBounds(src);
  const ray3d::Vec3 c = bb.valid ? ray3d::Vec3{0.5 * (bb.mn.x + bb.mx.x), 0.5 * (bb.mn.y + bb.mx.y), 0.5 * (bb.mn.z + bb.mx.z)}
                                 : ray3d::Vec3{0.0, 0.0, 0.0};
  brep::Solid cur = src;
  brep::Solid next;
  const auto fail = [&]() {
    if (outWhy)
      *outWhy = why;
    return false;
  };
  if (xf.scale() != 1.0) {
    if (!brep::Scale(cur, c, xf.scale(), &next, &why))
      return fail();
    cur = std::move(next);
  }
  double q[3][3];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      q[i][j] = xf.orthogonal(i, j);
  if (xf.isReflection()) {
    // Q = R * H(z): mirror across the horizontal plane through c first, then turn by R = Q * H(z).
    if (!brep::Mirror(cur, c, {0.0, 0.0, 1.0}, &next, &why))
      return fail();
    cur = std::move(next);
    for (int i = 0; i < 3; ++i)
      q[i][2] = -q[i][2];
  }
  ray3d::Vec3 axis{};
  double angle = 0.0;
  if (!entityops_detail::AxisAngleFromRotation(q, &axis, &angle))
    return fail();
  if (std::fabs(angle) > 1e-15) {
    if (!brep::Rotate(cur, c, axis, angle, &next, &why))
      return fail();
    cur = std::move(next);
  }
  const ray3d::Vec3 delta = ray3d::Sub(xf.applyPoint(c), c);
  if (ray3d::Length(delta) > 0.0)
    cur = brep::Translate(cur, delta);
  store.cadSolids[index] = std::make_shared<const brep::Solid>(std::move(cur));
  return true;
}

/// Pipe run: the path maps. The pipe size is a catalog part and never scales (D-2026-09-28-f).
inline bool TransformPipeRun(CadDrawingEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!xf.valid() || index >= store.cadPipeRuns.size())
    return false;
  entityops_detail::MapXyzTriplets(xf, store.cadPipeRuns[index].vertsXyz);
  return true;
}

/// Multileader under a transform that keeps world +Z: every leader path maps, the arrowhead and
/// label scale, and the MTEXT label follows the MTEXT rules (readable under a mirror).
inline bool TransformMultileader(CadDrawingEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!SupportsEntityTransform(EntityKind::Multileader, xf) || index >= store.cadMultileaders.size())
    return false;
  CadMultileader& ml = store.cadMultileaders[index];
  const entityops_detail::PlanarParts p = entityops_detail::DecomposePlanar(xf);
  entityops_detail::MapXyzTriplets(xf, ml.pathXyz);
  for (std::vector<float>& branch : ml.extraLeaderPaths)
    entityops_detail::MapXyzTriplets(xf, branch);
  entityops_detail::MapPlanZ(xf, &ml.label.insZ);
  entityops_detail::TransformMtextFrame(ml.label, xf, p);
  entityops_detail::ScalePlottedHeight(&ml.label.plottedHeightInches, p.scale);
  if (p.scale != 1.0) {
    const float s = static_cast<float>(p.scale);
    ml.arrowSizeWorld *= s;
    ml.labelWidthWorld *= s;
    ml.labelHeightWorld *= s;
  }
  return true;
}

/// Position Marker: moves with its label (REQ-359). Any other transform returns false.
inline bool TransformPositionMarker(CadDrawingEntityStore& store, std::size_t index, const SimilarityTransform& xf) {
  if (!SupportsEntityTransform(EntityKind::PositionMarker, xf) || index >= store.cadPositionMarkers.size())
    return false;
  CadPositionMarker& m = store.cadPositionMarkers[index];
  const ray3d::Vec3 p0{m.x, m.y, static_cast<double>(m.z)};
  const ray3d::Vec3 d = ray3d::Sub(xf.applyPoint(p0), p0);
  // Add the double displacement to each stored value and round once, so a label at state-plane
  // coordinates stays with its marker even when the move brings it near the origin.
  const auto add = [](float* v, double delta) { *v = static_cast<float>(static_cast<double>(*v) + delta); };
  m.x += d.x;
  m.y += d.y;
  add(&m.z, d.z);
  add(&m.label.insX, d.x);
  add(&m.label.insY, d.y);
  add(&m.label.insZ, d.z);
  add(&m.label.boxMinX, d.x);
  add(&m.label.boxMaxX, d.x);
  add(&m.label.boxMinY, d.y);
  add(&m.label.boxMaxY, d.y);
  return true;
}

[[nodiscard]] inline EntityBounds3d EllipseBounds(const CadEntityStore& store, std::size_t index) {
  EntityBounds3d b;
  if (index >= store.userEllipses.size())
    return b;
  const CadEllipse& e = store.userEllipses[index];
  ray3d::Vec3 u{}, v{};
  entityops_detail::EllipseWorldAxes(e, &u, &v);
  entityops_detail::ExpandBoundsWithEllipse3d(b, {e.cx, e.cy, e.z}, u, v, static_cast<double>(e.startRad),
                                              static_cast<double>(e.sweepRad));
  return b;
}

[[nodiscard]] inline EntityBounds3d FilledRegionBounds(const CadDrawingEntityStore& store, std::size_t index) {
  EntityBounds3d b;
  if (index >= store.cadFilledRegions.size())
    return b;
  const std::vector<double>& v = store.cadFilledRegions[index].vertsXyz;
  for (std::size_t k = 0; k + 2 < v.size(); k += 3)
    b.expand({v[k], v[k + 1], v[k + 2]});
  return b;
}

[[nodiscard]] inline EntityBounds3d MeshBounds(const CadEntityStore& store, std::size_t index) {
  EntityBounds3d b;
  if (index >= store.cadMeshes.size() || !store.cadMeshes[index])
    return b;
  const std::vector<float>& v = store.cadMeshes[index]->vertsXyz;
  for (std::size_t k = 0; k + 2 < v.size(); k += 3)
    b.expand({v[k], v[k + 1], v[k + 2]});
  return b;
}

/// Conservative, like brep::ComputeBounds: never smaller than the solid.
[[nodiscard]] inline EntityBounds3d SolidBounds(const CadEntityStore& store, std::size_t index) {
  EntityBounds3d b;
  if (index >= store.cadSolids.size() || !store.cadSolids[index])
    return b;
  const brep::Bounds bb = brep::ComputeBounds(*store.cadSolids[index]);
  if (bb.valid) {
    b.expand(bb.mn);
    b.expand(bb.mx);
  }
  return b;
}

/// Path box grown by the pipe's outside radius on every axis — conservative at bends.
[[nodiscard]] inline EntityBounds3d PipeRunBounds(const CadDrawingEntityStore& store, std::size_t index) {
  EntityBounds3d b;
  if (index >= store.cadPipeRuns.size())
    return b;
  const CadPipeRun& run = store.cadPipeRuns[index];
  for (std::size_t k = 0; k + 2 < run.vertsXyz.size(); k += 3)
    b.expand({run.vertsXyz[k], run.vertsXyz[k + 1], run.vertsXyz[k + 2]});
  double od = 0.0;
  if (b.valid && CadPipeNominalOdFeet(run.nominalSize, &od) && od > 0.0) {
    const double r = 0.5 * od;
    b.min = ray3d::Sub(b.min, {r, r, r});
    b.max = ray3d::Add(b.max, {r, r, r});
  }
  return b;
}

/// The table's four corners at its insertion elevation.
[[nodiscard]] inline EntityBounds3d TableBounds(const CadDrawingEntityStore& store, std::size_t index) {
  EntityBounds3d b;
  if (index >= store.cadTables.size())
    return b;
  const CadTable& t = store.cadTables[index];
  const float w = std::max(t.width, 1.e-3f), h = std::max(t.height, 1.e-3f);
  const float lx[4] = {0.f, w, w, 0.f};
  const float ly[4] = {0.f, 0.f, h, h};
  for (int i = 0; i < 4; ++i) {
    float wx = 0.f, wy = 0.f;
    CadTableLocalToWorld(t, lx[i], ly[i], &wx, &wy);
    b.expand({wx, wy, static_cast<double>(t.insZ)});
  }
  return b;
}

[[nodiscard]] inline EntityRef DuplicateEllipse(CadEntityStore& store, std::size_t index) {
  return entityops_detail::DuplicateInVector(store.userEllipses, store.userEllAttrs, index, EntityKind::Ellipse);
}

/// A copied annotation is not any survey point's label (the point keeps its own).
[[nodiscard]] inline EntityRef DuplicateAnnotation(CadEntityStore& store, std::size_t index) {
  const EntityRef ref =
      entityops_detail::DuplicateInVector(store.cadAnnotations, store.cadAnnotationAttrs, index, EntityKind::Annotation);
  if (ref.valid())
    store.cadAnnotations[static_cast<std::size_t>(ref.index)].surveyPointLabelForId = -1;
  return ref;
}

[[nodiscard]] inline EntityRef DuplicateMesh(CadEntityStore& store, std::size_t index) {
  return entityops_detail::DuplicateInVector(store.cadMeshes, store.cadMeshAttrs, index, EntityKind::Mesh);
}

[[nodiscard]] inline EntityRef DuplicateSolid(CadEntityStore& store, std::size_t index) {
  return entityops_detail::DuplicateInVector(store.cadSolids, store.cadSolidAttrs, index, EntityKind::Solid);
}

[[nodiscard]] inline EntityRef DuplicateTable(CadDrawingEntityStore& store, std::size_t index) {
  return entityops_detail::DuplicateInVector(store.cadTables, store.cadTableAttrs, index, EntityKind::Table);
}

[[nodiscard]] inline EntityRef DuplicateBlockRef(CadDrawingEntityStore& store, std::size_t index) {
  return entityops_detail::DuplicateInVector(store.cadBlockRefs, store.cadBlockRefAttrs, index, EntityKind::BlockRef);
}

[[nodiscard]] inline EntityRef DuplicateFilledRegion(CadDrawingEntityStore& store, std::size_t index) {
  return entityops_detail::DuplicateInVector(store.cadFilledRegions, store.cadFilledRegionAttrs, index,
                                             EntityKind::FilledRegion);
}

/// A copied run joins no piping network: a network is a set the user built (REQ-351).
[[nodiscard]] inline EntityRef DuplicatePipeRun(CadDrawingEntityStore& store, std::size_t index) {
  return entityops_detail::DuplicateInVector(store.cadPipeRuns, store.cadPipeRunAttrs, index, EntityKind::PipeRun);
}

[[nodiscard]] inline EntityRef DuplicateMultileader(CadDrawingEntityStore& store, std::size_t index) {
  return entityops_detail::DuplicateInVector(store.cadMultileaders, store.cadMultileaderAttrs, index,
                                             EntityKind::Multileader);
}

[[nodiscard]] inline EntityRef DuplicatePositionMarker(CadDrawingEntityStore& store, std::size_t index) {
  return entityops_detail::DuplicateInVector(store.cadPositionMarkers, store.cadPositionMarkerAttrs, index,
                                             EntityKind::PositionMarker);
}

// -----------------------------------------------------------------------------------------------
// EntityRef dispatch (uniform entry points over handles)
// -----------------------------------------------------------------------------------------------

/// True when \p kind has transform and duplicate implementations in this header: every kind
/// Document owns. `FeatureLine`, `Surface` and `PointCloud` belong to other systems.
[[nodiscard]] constexpr bool HasEntityKindOps(EntityKind kind) {
  switch (kind) {
  case EntityKind::Line:
  case EntityKind::Circle:
  case EntityKind::Arc:
  case EntityKind::Ellipse:
  case EntityKind::Polyline:
  case EntityKind::Annotation:
  case EntityKind::FilledRegion:
  case EntityKind::Mesh:
  case EntityKind::Table:
  case EntityKind::BlockRef:
  case EntityKind::Solid:
  case EntityKind::PositionMarker:
  case EntityKind::Multileader:
  case EntityKind::Spline:
  case EntityKind::Point:
  case EntityKind::PipeRun:
    return true;
  case EntityKind::FeatureLine:
  case EntityKind::Surface:
  case EntityKind::PointCloud:
    return false;
  }
  return false;
}

/// True when \ref EntityBounds has an implementation for \p kind. Annotations, block references,
/// multileaders and Position Markers size themselves from drawing state (text height, the block
/// definition), so their boxes stay with the extents code until issue #1577 phase 4.
[[nodiscard]] constexpr bool HasEntityBounds(EntityKind kind) {
  switch (kind) {
  case EntityKind::Annotation:
  case EntityKind::BlockRef:
  case EntityKind::Multileader:
  case EntityKind::PositionMarker:
    return false;
  default:
    return HasEntityKindOps(kind);
  }
}

/// Apply \p xf to a single entity referenced by \p ref. Returns false, leaving the entity unchanged,
/// if \p xf is invalid, \p ref is out of bounds, or the kind cannot represent the result
/// (\ref SupportsEntityTransform).
inline bool TransformEntity(CadDrawingEntityStore& store, const EntityRef& ref, const SimilarityTransform& xf,
                            const EntityTransformContext& ctx = {}) {
  if (!ref.valid() || !xf.valid())
    return false;
  const std::size_t ix = static_cast<std::size_t>(ref.index);
  switch (ref.kind) {
  case EntityKind::Line:           return TransformLine(store, ix, xf);
  case EntityKind::Circle:         return TransformCircle(store, ix, xf);
  case EntityKind::Arc:            return TransformArc(store, ix, xf);
  case EntityKind::Ellipse:        return TransformEllipse(store, ix, xf);
  case EntityKind::Polyline:       return TransformPolyline(store, ix, xf);
  case EntityKind::Annotation:     return TransformAnnotation(store, ix, xf, ctx);
  case EntityKind::FilledRegion:   return TransformFilledRegion(store, ix, xf);
  case EntityKind::Mesh:           return TransformMesh(store, ix, xf);
  case EntityKind::Table:          return TransformTable(store, ix, xf);
  case EntityKind::BlockRef:       return TransformBlockRef(store, ix, xf);
  case EntityKind::Solid:          return TransformSolid(store, ix, xf);
  case EntityKind::PositionMarker: return TransformPositionMarker(store, ix, xf);
  case EntityKind::Multileader:    return TransformMultileader(store, ix, xf);
  case EntityKind::Spline:         return TransformSpline(store, ix, xf);
  case EntityKind::Point:          return TransformPoint(store, ix, xf);
  case EntityKind::PipeRun:        return TransformPipeRun(store, ix, xf);
  case EntityKind::FeatureLine:
  case EntityKind::Surface:
  case EntityKind::PointCloud:     return false;
  }
  return false;
}

/// Apply \p xf to every unique valid entity in \p refs. Duplicate `(kind, index)` pairs are
/// ignored so an entity is never transformed twice. Returns the number of transformed entities.
inline std::size_t TransformEntities(CadDrawingEntityStore& store,
                                     const std::vector<EntityRef>& refs,
                                     const SimilarityTransform& xf,
                                     const EntityTransformContext& ctx = {}) {
  if (!xf.valid())
    return 0;
  std::set<std::pair<std::uint8_t, int>> seen;
  std::size_t transformed = 0;
  for (const EntityRef& ref : refs) {
    if (!ref.valid())
      continue;
    if (!seen.insert({static_cast<std::uint8_t>(ref.kind), ref.index}).second)
      continue;
    if (TransformEntity(store, ref, xf, ctx))
      ++transformed;
  }
  return transformed;
}

/// Append a copy of the single entity referenced by \p ref, returning the new handle (or invalid).
[[nodiscard]] inline EntityRef DuplicateEntity(CadDrawingEntityStore& store, const EntityRef& ref) {
  if (!ref.valid())
    return {};
  const std::size_t ix = static_cast<std::size_t>(ref.index);
  switch (ref.kind) {
  case EntityKind::Line:           return DuplicateLine(store, ix);
  case EntityKind::Circle:         return DuplicateCircle(store, ix);
  case EntityKind::Arc:            return DuplicateArc(store, ix);
  case EntityKind::Ellipse:        return DuplicateEllipse(store, ix);
  case EntityKind::Polyline:       return DuplicatePolyline(store, ix);
  case EntityKind::Annotation:     return DuplicateAnnotation(store, ix);
  case EntityKind::FilledRegion:   return DuplicateFilledRegion(store, ix);
  case EntityKind::Mesh:           return DuplicateMesh(store, ix);
  case EntityKind::Table:          return DuplicateTable(store, ix);
  case EntityKind::BlockRef:       return DuplicateBlockRef(store, ix);
  case EntityKind::Solid:          return DuplicateSolid(store, ix);
  case EntityKind::PositionMarker: return DuplicatePositionMarker(store, ix);
  case EntityKind::Multileader:    return DuplicateMultileader(store, ix);
  case EntityKind::Spline:         return DuplicateSpline(store, ix);
  case EntityKind::Point:          return DuplicatePoint(store, ix);
  case EntityKind::PipeRun:        return DuplicatePipeRun(store, ix);
  case EntityKind::FeatureLine:
  case EntityKind::Surface:
  case EntityKind::PointCloud:     return {};
  }
  return {};
}

/// Append copies of all unique valid entities in \p refs and return their new handles (in input
/// order). Source handles are validated against the original store counts before appending so an
/// initially out-of-range handle can never match a copy appended earlier in the same call.
/// Callers implement COPY / ARRAY as `DuplicateEntities` followed by `TransformEntities`.
[[nodiscard]] inline std::vector<EntityRef> DuplicateEntities(CadDrawingEntityStore& store,
                                                              const std::vector<EntityRef>& refs) {
  std::array<std::size_t, kAllEntityKinds.size()> originalCounts{};
  for (std::size_t i = 0; i < kAllEntityKinds.size(); ++i)
    originalCounts[i] = EntityCountForKind(store, kAllEntityKinds[i]);

  std::set<std::pair<std::uint8_t, int>> seen;
  std::vector<EntityRef> uniqueRefs;
  uniqueRefs.reserve(refs.size());
  for (const EntityRef& ref : refs) {
    if (!ref.valid() || !HasEntityKindOps(ref.kind))
      continue;
    const std::size_t kindOrd = static_cast<std::size_t>(ref.kind);
    if (kindOrd >= originalCounts.size() ||
        static_cast<std::size_t>(ref.index) >= originalCounts[kindOrd])
      continue;
    if (seen.insert({static_cast<std::uint8_t>(ref.kind), ref.index}).second)
      uniqueRefs.push_back(ref);
  }
  std::vector<EntityRef> copies;
  copies.reserve(uniqueRefs.size());
  for (const EntityRef& ref : uniqueRefs) {
    const EntityRef copy = DuplicateEntity(store, ref);
    if (copy.valid())
      copies.push_back(copy);
  }
  return copies;
}

/// 3D bounding box of the entity referenced by \p ref (invalid if out of range or not covered by
/// \ref HasEntityBounds). Analytic and exact for lines, circles, arcs, ellipses, polylines, points,
/// hatch fills, meshes and tables; conservative within `kSplineSampleTolerance` (clamped to the
/// control polygon) for curved splines, and conservative for solids and pipe runs.
[[nodiscard]] inline EntityBounds3d EntityBounds(const CadDrawingEntityStore& store,
                                                 const EntityRef& ref) {
  if (!ref.valid())
    return {};
  const std::size_t ix = static_cast<std::size_t>(ref.index);
  switch (ref.kind) {
  case EntityKind::Line:         return LineBounds(store, ix);
  case EntityKind::Circle:       return CircleBounds(store, ix);
  case EntityKind::Arc:          return ArcBounds(store, ix);
  case EntityKind::Ellipse:      return EllipseBounds(store, ix);
  case EntityKind::Polyline:     return PolylineBounds(store, ix);
  case EntityKind::FilledRegion: return FilledRegionBounds(store, ix);
  case EntityKind::Mesh:         return MeshBounds(store, ix);
  case EntityKind::Table:        return TableBounds(store, ix);
  case EntityKind::Solid:        return SolidBounds(store, ix);
  case EntityKind::Spline:       return SplineBounds(store, ix);
  case EntityKind::Point:        return PointBounds(store, ix);
  case EntityKind::PipeRun:      return PipeRunBounds(store, ix);
  default:                       return {};
  }
}

}  // namespace GoSurvey::Document
