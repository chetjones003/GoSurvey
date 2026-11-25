#pragma once

/// Document-owned per-kind entity operations (ADR-0011 / GitHub issue #1577 phase 1).
///
/// Plain free functions per entity kind taking a `SimilarityTransform` in `double`, plus exact 3D
/// bounding boxes, duplication (for COPY = duplicate-then-transform), and store-wide kind/attribute
/// iteration. Computations run in `double`; float storage fields (normals, arc angles, bulges) are
/// rounded once on write.
///
/// Phase 1 implements lines, circles, arcs, polylines, splines and CAD points. Remaining Document
/// kinds follow in Phase 2a.

#include <GoSurvey/Document/EntityRef.hpp>
#include <GoSurvey/Document/cadblock.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
#include <GoSurvey/Geometry/spline.hpp>
#include <GoSurvey/Math/ray3d.hpp>
#include <GoSurvey/Math/ucs.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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
// EntityRef dispatch (uniform entry points over handles)
// -----------------------------------------------------------------------------------------------

/// True when \p kind has transform, duplicate and bounds implementations in this header.
[[nodiscard]] constexpr bool HasEntityKindOps(EntityKind kind) {
  switch (kind) {
  case EntityKind::Line:
  case EntityKind::Circle:
  case EntityKind::Arc:
  case EntityKind::Polyline:
  case EntityKind::Spline:
  case EntityKind::Point:
    return true;
  default:
    return false;
  }
}

/// Apply \p xf to a single entity referenced by \p ref. Returns false if \p xf is invalid, \p ref
/// is out of bounds, or \p ref.kind is not yet implemented in this header.
inline bool TransformEntity(CadDrawingEntityStore& store, const EntityRef& ref,
                            const SimilarityTransform& xf) {
  if (!ref.valid() || !xf.valid())
    return false;
  const std::size_t ix = static_cast<std::size_t>(ref.index);
  switch (ref.kind) {
  case EntityKind::Line:     return TransformLine(store, ix, xf);
  case EntityKind::Circle:   return TransformCircle(store, ix, xf);
  case EntityKind::Arc:      return TransformArc(store, ix, xf);
  case EntityKind::Polyline: return TransformPolyline(store, ix, xf);
  case EntityKind::Spline:   return TransformSpline(store, ix, xf);
  case EntityKind::Point:    return TransformPoint(store, ix, xf);
  default:                   return false;
  }
}

/// Apply \p xf to every unique valid entity in \p refs. Duplicate `(kind, index)` pairs are
/// ignored so an entity is never transformed twice. Returns the number of transformed entities.
inline std::size_t TransformEntities(CadDrawingEntityStore& store,
                                     const std::vector<EntityRef>& refs,
                                     const SimilarityTransform& xf) {
  if (!xf.valid())
    return 0;
  std::set<std::pair<std::uint8_t, int>> seen;
  std::size_t transformed = 0;
  for (const EntityRef& ref : refs) {
    if (!ref.valid())
      continue;
    if (!seen.insert({static_cast<std::uint8_t>(ref.kind), ref.index}).second)
      continue;
    if (TransformEntity(store, ref, xf))
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
  case EntityKind::Line:     return DuplicateLine(store, ix);
  case EntityKind::Circle:   return DuplicateCircle(store, ix);
  case EntityKind::Arc:      return DuplicateArc(store, ix);
  case EntityKind::Polyline: return DuplicatePolyline(store, ix);
  case EntityKind::Spline:   return DuplicateSpline(store, ix);
  case EntityKind::Point:    return DuplicatePoint(store, ix);
  default:                   return {};
  }
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

/// 3D bounding box of the entity referenced by \p ref (invalid if out of range or unsupported).
/// Analytic and exact for lines, circles, arcs, polylines and points; conservative within
/// `kSplineSampleTolerance` (clamped to the control polygon) for curved splines.
[[nodiscard]] inline EntityBounds3d EntityBounds(const CadDrawingEntityStore& store,
                                                 const EntityRef& ref) {
  if (!ref.valid())
    return {};
  const std::size_t ix = static_cast<std::size_t>(ref.index);
  switch (ref.kind) {
  case EntityKind::Line:     return LineBounds(store, ix);
  case EntityKind::Circle:   return CircleBounds(store, ix);
  case EntityKind::Arc:      return ArcBounds(store, ix);
  case EntityKind::Polyline: return PolylineBounds(store, ix);
  case EntityKind::Spline:   return SplineBounds(store, ix);
  case EntityKind::Point:    return PointBounds(store, ix);
  default:                   return {};
  }
}

}  // namespace GoSurvey::Document
