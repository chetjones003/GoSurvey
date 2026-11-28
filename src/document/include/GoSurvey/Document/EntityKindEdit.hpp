#pragma once

/// Document-owned ERASE and STRETCH for every entity kind Document stores (ADR-0011, issue #1700
/// phase 4). The transform, bounds and duplicate operations are in EntityKindOps.hpp. Kinds owned
/// by other systems (feature lines, surfaces, point clouds, survey points, alignments and PDF
/// underlays) are erased and stretched by Commands through those systems' own functions.

#include <GoSurvey/Document/EntityKindOps.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GoSurvey::Document {

// -----------------------------------------------------------------------------------------------
// ERASE
// -----------------------------------------------------------------------------------------------

namespace entityedit_detail {

template <class T>
bool EraseInVector(std::vector<T>& items, std::vector<EntityAttributes>& attrs, std::size_t index) {
  if (index >= items.size())
    return false;
  items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
  if (index < attrs.size())
    attrs.erase(attrs.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

}  // namespace entityedit_detail

/// Removes polyline \p index with its vertices, its per-vertex bulges and normals, its closed flag
/// and its attributes. Later polylines keep their vertices; their offsets shift down.
inline bool ErasePolyline(CadDrawingEntityStore& store, std::size_t index) {
  if (index + 1 >= store.userPolylineOffsets.size())
    return false;
  const int a = store.userPolylineOffsets[index];
  const int b = store.userPolylineOffsets[index + 1];
  if (a < 0 || b < a)
    return false;
  store.eraseVertexRange(static_cast<std::size_t>(a), static_cast<std::size_t>(b));
  // REQ-316 / ADR-047: the bulge side-car is per VERTEX, so its cut span is [a, b).
  if (!store.userPolylineVertsBulge.empty() && static_cast<std::size_t>(b) <= store.userPolylineVertsBulge.size())
    store.userPolylineVertsBulge.erase(store.userPolylineVertsBulge.begin() + a,
                                       store.userPolylineVertsBulge.begin() + b);
  // REQ-325 / ADR-053: the same per-vertex span, three floats wide.
  if (!store.userPolylineVertsNormal.empty() && static_cast<std::size_t>(b) * 3 <= store.userPolylineVertsNormal.size())
    store.userPolylineVertsNormal.erase(store.userPolylineVertsNormal.begin() + static_cast<std::ptrdiff_t>(a) * 3,
                                        store.userPolylineVertsNormal.begin() + static_cast<std::ptrdiff_t>(b) * 3);
  const int removed = b - a;
  std::vector<int>& off = store.userPolylineOffsets;
  off.erase(off.begin() + static_cast<std::ptrdiff_t>(index) + 1);
  for (std::size_t i = index + 1; i < off.size(); ++i)
    off[i] -= removed;
  // Erasing the only polyline leaves `{0}`, which is not how the store spells "no polylines": an
  // empty table is, and the file reader rejects a single entry (issue #60, REQ-079).
  if (off.size() == 1)
    off.clear();
  if (index < store.userPolylineClosed.size())
    store.userPolylineClosed.erase(store.userPolylineClosed.begin() + static_cast<std::ptrdiff_t>(index));
  if (index < store.userPolylineAttrs.size())
    store.userPolylineAttrs.erase(store.userPolylineAttrs.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

/// Removes pipe run \p index and its place in every piping network; the networks' later run indices
/// shift down with the store (issue #486 / REQ-345).
inline bool ErasePipeRun(CadDrawingEntityStore& store, std::size_t index) {
  if (!entityedit_detail::EraseInVector(store.cadPipeRuns, store.cadPipeRunAttrs, index))
    return false;
  const int idx = static_cast<int>(index);
  for (CadPipingSystem& sys : store.cadPipingSystems) {
    std::vector<int>& runs = sys.pipeRunIndices;
    runs.erase(std::remove(runs.begin(), runs.end(), idx), runs.end());
    for (int& ri : runs)
      if (ri > idx)
        --ri;
  }
  return true;
}

/// True when \ref EraseEntity can remove \p kind: every kind Document stores.
[[nodiscard]] constexpr bool HasEntityErase(EntityKind kind) { return HasEntityKindOps(kind); }

/// Removes the entity \p ref names, with its attributes and side-car data. Later entities of the
/// same kind shift down by one, so erase several of one kind highest index first, as
/// \ref EraseEntities does. Returns false if \p ref is out of range or not a Document kind.
inline bool EraseEntity(CadDrawingEntityStore& store, const EntityRef& ref) {
  if (!ref.valid())
    return false;
  const std::size_t ix = static_cast<std::size_t>(ref.index);
  using entityedit_detail::EraseInVector;
  switch (ref.kind) {
  case EntityKind::Line:
    if (ix >= store.lineCount())
      return false;
    store.eraseLine(ix);
    if (ix < store.userLineAttrs.size())
      store.userLineAttrs.erase(store.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(ix));
    return true;
  case EntityKind::Circle:
    if (ix >= store.circleCount())
      return false;
    // One whole record (stride 4) comes out (issue #62), with its plane normal.
    store.eraseCircle(ix);
    if (ix < store.userCircleAttrs.size())
      store.userCircleAttrs.erase(store.userCircleAttrs.begin() + static_cast<std::ptrdiff_t>(ix));
    EraseCircleNormal(store.userCircleNormals, ix);
    return true;
  case EntityKind::Arc:            return EraseInVector(store.userArcs, store.userArcAttrs, ix);
  case EntityKind::Ellipse:        return EraseInVector(store.userEllipses, store.userEllAttrs, ix);
  case EntityKind::Polyline:       return ErasePolyline(store, ix);
  case EntityKind::Annotation:     return EraseInVector(store.cadAnnotations, store.cadAnnotationAttrs, ix);
  case EntityKind::FilledRegion:   return EraseInVector(store.cadFilledRegions, store.cadFilledRegionAttrs, ix);
  // A mesh or solid is a shared pointer: removing it is the one undo step. Display caches key on a
  // weak pointer, so the erased entry expires on its own.
  case EntityKind::Mesh:           return EraseInVector(store.cadMeshes, store.cadMeshAttrs, ix);
  case EntityKind::Solid:          return EraseInVector(store.cadSolids, store.cadSolidAttrs, ix);
  case EntityKind::Table:          return EraseInVector(store.cadTables, store.cadTableAttrs, ix);
  case EntityKind::BlockRef:       return EraseInVector(store.cadBlockRefs, store.cadBlockRefAttrs, ix);
  case EntityKind::PositionMarker: return EraseInVector(store.cadPositionMarkers, store.cadPositionMarkerAttrs, ix);
  case EntityKind::Multileader:    return EraseInVector(store.cadMultileaders, store.cadMultileaderAttrs, ix);
  case EntityKind::Spline:         return EraseInVector(store.cadSplines, store.cadSplineAttrs, ix);
  case EntityKind::Point:          return EraseInVector(store.cadPoints, store.cadPointAttrs, ix);
  case EntityKind::PipeRun:        return ErasePipeRun(store, ix);
  case EntityKind::FeatureLine:
  case EntityKind::Surface:
  case EntityKind::PointCloud:
    return false;
  }
  return false;
}

/// Erases every entity in \p refs, ignoring duplicates and kinds Document does not store. Each kind
/// is erased highest index first, so no removal shifts an index not yet used. Returns how many were
/// erased.
inline std::size_t EraseEntities(CadDrawingEntityStore& store, const std::vector<EntityRef>& refs) {
  std::map<EntityKind, std::set<int>> byKind;
  for (const EntityRef& r : refs)
    if (r.valid() && HasEntityErase(r.kind))
      byKind[r.kind].insert(r.index);
  std::size_t n = 0;
  for (const auto& [kind, indices] : byKind)
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
      n += EraseEntity(store, EntityRef{kind, *it}) ? 1 : 0;
  return n;
}

// -----------------------------------------------------------------------------------------------
// STRETCH
// -----------------------------------------------------------------------------------------------

/// The crossing window and displacement of one STRETCH (REQ-103). \ref contains says whether a
/// definition point lies inside the window; the caller decides the window's frame (plan, or a
/// tilted work plane).
struct StretchWindow {
  std::function<bool(const ray3d::Vec3&)> contains;
  ray3d::Vec3 delta{0.0, 0.0, 0.0};
  /// True when the window is in plan. An arc then stretches its endpoints in plan; under a tilted
  /// window, where that maths does not hold, it moves whole when its centre is inside.
  bool planWindow = true;
};

/// Re-centres \p arc on its moved endpoints, keeping its sweep (D-2026-08-24-d).
///
/// C' = M' - (1/2)·cot(s/2)·Rot90CW(V'), where M' is the midpoint of the new endpoints, V' the new
/// chord and s the sweep. Returns false, leaving the arc unchanged and adding the reason to \p log,
/// if the sweep is degenerate, the endpoints would coincide, or the radius would collapse. Not for a
/// full-circle sweep, whose endpoints coincide (see \ref StretchArc).
inline bool RecomputeArcFromEndpoints(CadArc& arc, float newStartX, float newStartY, float newEndX, float newEndY,
                                      std::vector<std::string>* log) {
  const float s = arc.sweepRad;
  const float halfS = 0.5f * s;
  const float sinHalf = std::sin(halfS);
  if (std::fabs(sinHalf) < 1e-6f) {
    if (log)
      log->push_back("STRETCH — arc endpoint stretch skipped (degenerate sweep); object unchanged.");
    return false;
  }
  const float vx = newEndX - newStartX, vy = newEndY - newStartY;
  const float chordLen = std::hypot(vx, vy);
  if (chordLen < 1e-4f) {
    if (log)
      log->push_back("STRETCH — arc endpoints would coincide; object left unchanged.");
    return false;
  }
  const float mx = 0.5f * (newStartX + newEndX);
  const float my = 0.5f * (newStartY + newEndY);
  const float rvx = vy, rvy = -vx;  // Rot90CW(V')
  const float cotHalf = std::cos(halfS) / sinHalf;
  const float cx = mx - 0.5f * cotHalf * rvx;
  const float cy = my - 0.5f * cotHalf * rvy;
  const float r = std::hypot(newStartX - cx, newStartY - cy);
  if (r < 1e-4f) {
    if (log)
      log->push_back("STRETCH — arc stretch would collapse its radius; object left unchanged.");
    return false;
  }
  arc.cx = cx;
  arc.cy = cy;
  arc.r = r;
  arc.startRad = std::atan2(newStartY - cy, newStartX - cx);
  return true;  // the sweep is unchanged by design
}

/// One arc under STRETCH, in plan: each endpoint inside the window moves by the plan displacement and
/// the arc is refit through them (\ref RecomputeArcFromEndpoints). A full-circle sweep has no distinct
/// endpoints and moves whole when its centre is inside, as a circle does. Elevation is not changed.
inline void StretchArc(CadArc& arc, const std::function<bool(const ray3d::Vec3&)>& contains, float dx, float dy,
                       std::vector<std::string>* log) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  const double z = arc.z;
  if (std::fabs(std::fabs(arc.sweepRad) - kTwoPi) < 1e-4f) {
    if (contains({static_cast<double>(static_cast<float>(arc.cx)), static_cast<double>(static_cast<float>(arc.cy)), z})) {
      arc.cx += dx;
      arc.cy += dy;
    }
    return;
  }
  const float sx = static_cast<float>(arc.cx + arc.r * std::cos(arc.startRad));
  const float sy = static_cast<float>(arc.cy + arc.r * std::sin(arc.startRad));
  const float endAng = arc.startRad + arc.sweepRad;
  const float ex = static_cast<float>(arc.cx + arc.r * std::cos(endAng));
  const float ey = static_cast<float>(arc.cy + arc.r * std::sin(endAng));
  const bool startIn = contains({sx, sy, z});
  const bool endIn = contains({ex, ey, z});
  if (!startIn && !endIn)
    return;  // nothing to move, as in AutoCAD
  RecomputeArcFromEndpoints(arc, startIn ? sx + dx : sx, startIn ? sy + dy : sy, endIn ? ex + dx : ex,
                            endIn ? ey + dy : ey, log);
}

namespace entityedit_detail {

template <class T>
void AddTo(T* v, double d) {
  *v = static_cast<T>(static_cast<double>(*v) + d);
}

/// Every XYZ triplet inside the window moves. Returns whether any did.
inline bool StretchXyzTriplets(std::vector<float>& xyz, const StretchWindow& w) {
  bool moved = false;
  for (std::size_t i = 0; i + 2 < xyz.size(); i += 3) {
    if (!w.contains({xyz[i], xyz[i + 1], xyz[i + 2]}))
      continue;
    AddTo(&xyz[i], w.delta.x);
    AddTo(&xyz[i + 1], w.delta.y);
    AddTo(&xyz[i + 2], w.delta.z);
    moved = true;
  }
  return moved;
}

/// A spline under STRETCH, as AutoCAD's: a fit spline moves its fit points inside the window and is
/// refit through them; a CV spline moves its control points inside the window.
inline bool StretchSpline(CadSpline& sp, const StretchWindow& w, std::vector<std::string>* log) {
  if (sp.isFit()) {
    const CadSpline before = sp;
    bool moved = false;
    for (ray3d::Vec3& p : sp.fit)
      if (w.contains(p)) {
        p = ray3d::Add(p, w.delta);
        moved = true;
      }
    if (!moved)
      return false;
    std::string why;
    if (!spline::Rebuild(&sp, &why)) {
      sp = before;
      if (log)
        log->push_back("STRETCH — spline left unchanged: " + why);
      return false;
    }
    return true;
  }
  bool moved = false;
  const std::size_t n = spline::UniqueControlCount(sp);
  for (std::size_t i = 0; i < n && i < sp.ctrl.size(); ++i)
    if (w.contains(sp.ctrl[i])) {
      spline::SetControlPoint(&sp, i, ray3d::Add(sp.ctrl[i], w.delta));
      moved = true;
    }
  return moved;
}

/// A multileader under STRETCH, as AutoCAD's: each leader point inside the window moves, and the
/// label moves with the landing when the landing is inside.
inline bool StretchMultileader(CadMultileader& ml, const StretchWindow& w) {
  const std::size_t n = ml.pathXyz.size();
  const bool landingIn = n >= 3 && w.contains({ml.pathXyz[n - 3], ml.pathXyz[n - 2], ml.pathXyz[n - 1]});
  bool moved = StretchXyzTriplets(ml.pathXyz, w);
  for (std::vector<float>& branch : ml.extraLeaderPaths)
    moved = StretchXyzTriplets(branch, w) || moved;
  if (landingIn) {
    CadAnnotation& a = ml.label;
    AddTo(&a.insX, w.delta.x);
    AddTo(&a.insY, w.delta.y);
    AddTo(&a.insZ, w.delta.z);
    AddTo(&a.boxMinX, w.delta.x);
    AddTo(&a.boxMaxX, w.delta.x);
    AddTo(&a.boxMinY, w.delta.y);
    AddTo(&a.boxMaxY, w.delta.y);
  }
  return moved;
}

}  // namespace entityedit_detail

/// True when \ref StretchEntity implements \p kind: every Document kind its declared capability
/// lets STRETCH take (\ref CheckOpSupport).
[[nodiscard]] constexpr bool HasEntityStretch(EntityKind kind) {
  return HasEntityKindOps(kind) && CheckOpSupport(kind, EntityOp::Stretch).supported;
}

/// STRETCH one entity (REQ-103), as AutoCAD 2025 does (`tools/autocad-transform-oracle/`):
/// - lines, polylines, splines and multileaders move each point inside the window;
/// - an arc moves its endpoints inside the window (see \ref StretchWindow::planWindow);
/// - circles, ellipses, text, tables, inserts and CAD points move whole when their centre or
///   insertion point is inside;
/// - a hatch moves whole when its first boundary vertex is inside (a recorded simplification).
/// Returns whether the entity changed. Kinds that cannot be stretched (\ref HasEntityStretch) and
/// out-of-range references return false.
inline bool StretchEntity(CadDrawingEntityStore& store, const EntityRef& ref, const StretchWindow& w,
                          std::vector<std::string>* log = nullptr) {
  if (!ref.valid() || !w.contains || !HasEntityStretch(ref.kind))
    return false;
  const std::size_t ix = static_cast<std::size_t>(ref.index);
  const ray3d::Vec3 d = w.delta;
  const SimilarityTransform move = SimilarityTransform::Translation(d);
  using namespace entityedit_detail;
  switch (ref.kind) {
  case EntityKind::Line: {
    if (ix >= store.lineCount())
      return false;
    const CadLineRecord ln = store.line(ix);
    bool moved = false;
    if (w.contains({ln.x0, ln.y0, ln.z0})) {
      store.lineComponent(ix, CadLineComponent::X0) += d.x;
      store.lineComponent(ix, CadLineComponent::Y0) += d.y;
      store.lineComponent(ix, CadLineComponent::Z0) += d.z;
      moved = true;
    }
    if (w.contains({ln.x1, ln.y1, ln.z1})) {
      store.lineComponent(ix, CadLineComponent::X1) += d.x;
      store.lineComponent(ix, CadLineComponent::Y1) += d.y;
      store.lineComponent(ix, CadLineComponent::Z1) += d.z;
      moved = true;
    }
    return moved;
  }
  case EntityKind::Circle: {
    if (ix >= store.circleCount())
      return false;
    const CadCircleRecord c = store.circle(ix);
    return w.contains({c.cx, c.cy, c.cz}) && TransformEntity(store, ref, move);
  }
  case EntityKind::Arc: {
    if (ix >= store.userArcs.size())
      return false;
    CadArc& a = store.userArcs[ix];
    // The endpoint maths is in plan, so a tilted window or a tilted arc (REQ-312) moves the arc
    // whole when its centre is inside instead.
    if (!w.planWindow || !IsFlatNormal(a.nx, a.ny, a.nz))
      return w.contains({a.cx, a.cy, a.z}) && TransformEntity(store, ref, move);
    const CadArc before = a;
    StretchArc(a, w.contains, static_cast<float>(d.x), static_cast<float>(d.y), log);
    const double sx = before.cx + before.r * std::cos(before.startRad);
    const double sy = before.cy + before.r * std::sin(before.startRad);
    const double ex = before.cx + before.r * std::cos(before.startRad + before.sweepRad);
    const double ey = before.cy + before.r * std::sin(before.startRad + before.sweepRad);
    // An arc has one elevation: it takes the Z change only when it moves whole.
    const bool whole = std::fabs(std::fabs(before.sweepRad) - 6.28318530717958647692f) < 1e-4f
                           ? a.cx != before.cx || a.cy != before.cy
                           : w.contains({static_cast<float>(sx), static_cast<float>(sy), before.z}) &&
                                 w.contains({static_cast<float>(ex), static_cast<float>(ey), before.z});
    if (whole)
      a.z += d.z;
    return a.cx != before.cx || a.cy != before.cy || a.r != before.r || a.startRad != before.startRad;
  }
  case EntityKind::Ellipse: {
    if (ix >= store.userEllipses.size())
      return false;
    const CadEllipse& e = store.userEllipses[ix];
    return w.contains({e.cx, e.cy, e.z}) && TransformEntity(store, ref, move);
  }
  case EntityKind::Polyline: {
    if (ix + 1 >= store.userPolylineOffsets.size())
      return false;
    bool moved = false;
    for (int vi = store.userPolylineOffsets[ix]; vi < store.userPolylineOffsets[ix + 1]; ++vi) {
      const std::size_t b = static_cast<std::size_t>(vi);
      const CadPolyVertex v = store.vertex(b);
      if (!w.contains({v.x, v.y, v.z}))
        continue;
      store.vertexComponent(b, CadVertexComponent::X) += d.x;
      store.vertexComponent(b, CadVertexComponent::Y) += d.y;
      store.vertexComponent(b, CadVertexComponent::Z) += d.z;
      moved = true;
    }
    return moved;
  }
  case EntityKind::Annotation: {
    // Moves whole when its insertion point is inside. An MTEXT's insertion is its attachment point
    // (issue #1667), so it is refreshed from the box first.
    if (ix >= store.cadAnnotations.size())
      return false;
    CadAnnotation& a = store.cadAnnotations[ix];
    CadMtextSyncInsertion(a);
    return w.contains({a.insX, a.insY, a.insZ}) && TransformEntity(store, ref, move);
  }
  case EntityKind::Table: {
    if (ix >= store.cadTables.size())
      return false;
    CadTable& t = store.cadTables[ix];
    if (!w.contains({t.insX, t.insY, t.insZ}))
      return false;
    CadTableTranslate(&t, static_cast<float>(d.x), static_cast<float>(d.y));
    t.insZ += static_cast<float>(d.z);
    return true;
  }
  case EntityKind::FilledRegion: {
    if (ix >= store.cadFilledRegions.size())
      return false;
    const std::vector<double>& v = store.cadFilledRegions[ix].vertsXyz;
    return v.size() >= 3 && w.contains({v[0], v[1], v[2]}) && TransformEntity(store, ref, move);
  }
  case EntityKind::Point: {
    if (ix >= store.cadPoints.size())
      return false;
    const CadPoint& p = store.cadPoints[ix];
    return w.contains({p.x, p.y, p.z}) && TransformEntity(store, ref, move);
  }
  case EntityKind::BlockRef: {
    if (ix >= store.cadBlockRefs.size())
      return false;
    const CadBlockXform& x = store.cadBlockRefs[ix].xf;
    return w.contains({x.x, x.y, x.z}) && TransformEntity(store, ref, move);
  }
  case EntityKind::Spline:
    return ix < store.cadSplines.size() && StretchSpline(store.cadSplines[ix], w, log);
  case EntityKind::Multileader:
    return ix < store.cadMultileaders.size() && StretchMultileader(store.cadMultileaders[ix], w);
  default:
    return false;
  }
}

}  // namespace GoSurvey::Document
