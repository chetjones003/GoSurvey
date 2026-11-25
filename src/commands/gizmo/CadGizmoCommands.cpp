#include "CadCommandsInternal.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include "GoSurvey/Document/cadpiperun.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


// --- The translate gizmo (REQ-060, GitHub issue #148 Phase 5 slice 4b) --------------------------
//
// Everything the gizmo MEANS lives here, in the command layer, and not in `CadUi.cpp`. Two things
// stay in the UI because the command layer has no access to them: the cursor RAY, and the pixel
// aperture it is tested against. That split is the same one REQ-318's sub-object pick uses, and it
// is what lets a headless transcript drive the gizmo at all — which REQ-060's "agrees with the
// equivalent typed command" has to be asserted by, since asserting it by inspection is exactly the
// kind of claim that stops being true.

namespace {

/// Accumulate one WCS point into a running box.
void GizmoGrowBounds(double x, double y, double z, ray3d::Vec3* mn, ray3d::Vec3* mx, bool* any) {
  if (!*any) {
    *mn = ray3d::Vec3{x, y, z};
    *mx = *mn;
    *any = true;
    return;
  }
  mn->x = std::min(mn->x, x);
  mn->y = std::min(mn->y, y);
  mn->z = std::min(mn->z, z);
  mx->x = std::max(mx->x, x);
  mx->y = std::max(mx->y, y);
  mx->z = std::max(mx->z, z);
}

}  // namespace

bool IsGizmoCommandKind(AppCommandState::Kind k) {
  return k == AppCommandState::Kind::Move3d || k == AppCommandState::Kind::Rotate3d ||
         k == AppCommandState::Kind::Scale3d;
}

bool CadGizmoSummoned(const AppCommandState& st) {
  if (const auto* sess = st.sessionAs<stepengine::GizmoCmdSession>())
    return sess->phase == stepengine::GizmoCmdPhase::Handles;
  return st.gizmoPersistent;
}

CadGizmoMode CadGizmoModeFor(const AppCommandState& st) {
  // A selection alone never draws the gizmo (GitHub issue #564 section 3, D-2026-09-28-a): the
  // handles covered the model the user had only meant to pick. It is summoned — by 3DMOVE /
  // 3DROTATE / 3DSCALE for their duration, or by the persistent `GIZMO` setting. Tested first, so
  // the hover, the click, the overlay and every transcript assertion inherit it from this one place.
  if (!CadGizmoSummoned(st))
    return CadGizmoMode::None;
  if (st.activeSpaceIndex != kModelSpaceIndex)
    return CadGizmoMode::None;  // a paper sheet is 2D (ADR-025 (g)); there is no third handle to draw
  // The sub-object selection is tested FIRST, but the order is a belt rather than the only brace:
  // the two stores are mutually exclusive by decision (D-2026-09-04-a), so they are never both
  // non-empty. Testing this one first means that if that invariant ever breaks, the gizmo shows the
  // narrower, more specific edit rather than silently translating a solid whose face was picked.
  // Exactly one sub-object, of any kind. More than one has no single thing to move, and a mixed
  // selection is one whose meaning is not settled — both refused rather than guessed at.
  if (st.subObjectSelection.size() == 1) {
    const SelectedSubObject& sub = st.subObjectSelection.front();
    // A sub-object can be MOVED and nothing else: no kernel operation rotates or scales one. So
    // under Rotate or Scale it gets no gizmo at all rather than a handle that would refuse on drop
    // (TASK-232).
    if (st.gizmoOp != CadGizmoOp::Translate)
      return CadGizmoMode::None;
    // In every case the GRIP decides, not this function. Each grip helper returns false wherever its
    // kernel operation could not run — a non-planar face, a pyramid's apex, a cylinder's rim — so
    // the gizmo appears exactly where a drag can be applied rather than appearing and then
    // declining on release.
    ray3d::Vec3 a{};
    ray3d::Vec3 n{};
    ray3d::Vec3 n2{};
    switch (sub.kind) {
    case solidpick::Kind::Face:
      return CadSubObjectFaceGrip(st, sub, &a, &n) ? CadGizmoMode::SubObjectFace
                                                   : CadGizmoMode::None;
    // REQ-333 replaced the note that stood here. An edge and a vertex got no gizmo because
    // `brep::PushPullFace` was the only solid edit there was; `brep::MoveEdge` and
    // `brep::MoveVertex` are now the second and third.
    case solidpick::Kind::Edge:
      return CadSubObjectEdgeGrip(st, sub, &a, &n, &n2) ? CadGizmoMode::SubObjectEdge
                                                        : CadGizmoMode::None;
    case solidpick::Kind::Vertex:
      return CadSubObjectVertexGrip(st, sub, &a) ? CadGizmoMode::SubObjectVertex
                                                 : CadGizmoMode::None;
    default:
      return CadGizmoMode::None;
    }
  }
  if (!st.subObjectSelection.empty())
    return CadGizmoMode::None;  // several sub-objects: no single thing to move
  return st.selection.empty() ? CadGizmoMode::None : CadGizmoMode::Entity;
}

int CadGizmoAxisCountFor(const AppCommandState& st) {
  switch (CadGizmoModeFor(st)) {
  case CadGizmoMode::Entity:
    // THREE to move and THREE to turn — one ring per active-UCS axis (D-2026-09-28-b, the user's
    // AutoCAD-3DROTATE reference; it replaces TASK-232's single UCS-Z ring). The Z ring commits
    // through typed ROTATE's own function, so REQ-060's agreement holds there unchanged; the X and Y
    // rings are 3DROTATE's arbitrary-axis turn (issue #564 §4), typed as "grab a ring, type an angle".
    // Scale has three too (D-2026-09-28-c, AutoCAD's 3DSCALE widget) but EVERY one scales uniformly,
    // as AutoCAD's does for a solid: typed SCALE and `brep::Scale` are uniform, and a one-direction
    // stretch is issue #564's own open question Q2.
    return kGizmoAxisCount;
  case CadGizmoMode::SubObjectFace:
    // ONE, because `brep::PushPullFace` takes a distance along the face normal and nothing else. A
    // second handle would name a direction the kernel cannot move the face in.
    return 1;
  case CadGizmoMode::SubObjectEdge:
    // TWO: two planes meet along an edge, each with one degree of freedom that keeps it planar, and
    // together they span exactly the plane perpendicular to the edge. No third, because the
    // along-the-edge direction is not a motion — an edge slid along its own line is the same edge.
    return 2;
  case CadGizmoMode::SubObjectVertex:
    // THREE: three planes meet, which is three degrees of freedom, so every direction is reachable
    // and there is nothing to leave out.
    return kGizmoAxisCount;
  case CadGizmoMode::None:
    break;
  }
  return 0;
}

int CadGizmoPlaneHandleCountFor(const AppCommandState& st) {
  // Whole entities under Translate only (D-2026-09-28-c): a sub-object's handles are the directions
  // its kernel edit can take, and a plane drag would name ones it cannot.
  return CadGizmoModeFor(st) == CadGizmoMode::Entity && st.gizmoOp == CadGizmoOp::Translate ? 3 : 0;
}

bool CadGizmoAnchorWorld(const AppCommandState& st, ray3d::Vec3* out) {
  if (!out)
    return false;
  {
    const CadGizmoMode mode = CadGizmoModeFor(st);
    if (mode != CadGizmoMode::Entity && mode != CadGizmoMode::None &&
        st.subObjectSelection.size() == 1) {
      const SelectedSubObject& sub = st.subObjectSelection.front();
      ray3d::Vec3 axis{};
      ray3d::Vec3 axisB{};
      switch (mode) {
      case CadGizmoMode::SubObjectFace:
        return CadSubObjectFaceGrip(st, sub, out, &axis);
      case CadGizmoMode::SubObjectEdge:
        return CadSubObjectEdgeGrip(st, sub, out, &axis, &axisB);
      case CadGizmoMode::SubObjectVertex:
        return CadSubObjectVertexGrip(st, sub, out);
      default:
        break;
      }
      return false;
    }
  }
  if (st.selection.empty())
    return false;
  // A 3D gizmo command's own base point (D-2026-09-28-c), when one was given: the user said where
  // the gizmo goes, and for ROTATE / SCALE that is the pivot and the centre.
  if (const auto* gizmoSess = st.sessionAs<stepengine::GizmoCmdSession>();
      gizmoSess && gizmoSess->baseValid) {
    *out = gizmoSess->base;
    return true;
  }
  ray3d::Vec3 mn{};
  ray3d::Vec3 mx{};
  bool any = false;
  const auto grow = [&](double x, double y, double z) { GizmoGrowBounds(x, y, z, &mn, &mx, &any); };
  using T = SelectedEntity::Type;
  for (const auto& e : st.selection) {
    const size_t k = static_cast<size_t>(std::max(e.index, 0));
    switch (e.type) {
    case T::LineSeg: {
      const size_t b = k;
      if (b < st.drawing.lineCount()) {
        grow(st.drawing.line(b).x0, st.drawing.line(b).y0, st.drawing.line(b).z0);
        grow(st.drawing.line(b).x1, st.drawing.line(b).y1, st.drawing.line(b).z1);
      }
      break;
    }
    case T::Circle: {
      if (k < st.drawing.circleCount()) {
        const CadCircleRecord c = st.drawing.circle(k);
        const double cx = c.cx, cy = c.cy, cz = c.cz, r = c.r;
        grow(cx - r, cy - r, cz);
        grow(cx + r, cy + r, cz);
      }
      break;
    }
    case T::Spline:  // REQ-443: its control points bound it (convex-hull property); a handle position only
      if (k < st.drawing.cadSplines.size())
        for (const ray3d::Vec3& v : st.drawing.cadSplines[k].ctrl)
          grow(v.x, v.y, v.z);
      break;
    case T::Point:
      if (k < st.drawing.cadPoints.size())
        grow(st.drawing.cadPoints[k].x, st.drawing.cadPoints[k].y, st.drawing.cadPoints[k].z);
      break;
    case T::Arc:
      // The full circle the arc lies on, not the arc's own extent. Conservative by up to a radius,
      // and the anchor is a handle position rather than an input to any move — see the header note.
      if (k < st.drawing.userArcs.size()) {
        const auto& a = st.drawing.userArcs[k];
        grow(a.cx - a.r, a.cy - a.r, a.z);
        grow(a.cx + a.r, a.cy + a.r, a.z);
      }
      break;
    case T::Ellipse:
      if (k < st.drawing.userEllipses.size()) {
        const auto& el = st.drawing.userEllipses[k];
        // The major-axis vector bounds it either way round; the minor is `ratio` times it and smaller.
        const double rr = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
        grow(el.cx - rr, el.cy - rr, el.z);
        grow(el.cx + rr, el.cy + rr, el.z);
      }
      break;
    case T::Polyline: {
      if (k + 1 < st.drawing.userPolylineOffsets.size()) {
        const int v0 = st.drawing.userPolylineOffsets[k];
        const int v1 = st.drawing.userPolylineOffsets[k + 1];
        for (int vi = v0; vi < v1; ++vi) {
          const size_t b = static_cast<size_t>(vi);
          if (b < st.drawing.vertexCount())
            grow(st.drawing.vertex(b).x, st.drawing.vertex(b).y, st.drawing.vertex(b).z);
        }
      }
      break;
    }
    case T::FeatureLine: {
      if (k + 1 < st.drawing.featureLineOffsets.size()) {
        const int v0 = st.drawing.featureLineOffsets[k];
        const int v1 = st.drawing.featureLineOffsets[k + 1];
        for (int vi = v0; vi < v1; ++vi) {
          const size_t b = static_cast<size_t>(vi) * 3;
          if (b + 2 < st.drawing.featureLineVerts.size())
            grow(st.drawing.featureLineVerts[b], st.drawing.featureLineVerts[b + 1], st.drawing.featureLineVerts[b + 2]);
        }
      }
      break;
    }
    case T::Annotation:
      if (k < st.drawing.cadAnnotations.size()) {
        const CadAnnotation& a = st.drawing.cadAnnotations[k];
        float mnx = 0.f, mny = 0.f, mxx = 0.f, mxy = 0.f;
        CadAnnotationRoughBounds(a, st.drawing.modelUnitsPerPlottedInch, &mnx, &mny, &mxx, &mxy);
        grow(mnx, mny, a.insZ);
        grow(mxx, mxy, a.insZ);
      }
      break;
    case T::Table:
      if (k < st.drawing.cadTables.size())
        grow(st.drawing.cadTables[k].insX, st.drawing.cadTables[k].insY, st.drawing.cadTables[k].insZ);
      break;
    case T::PdfUnderlay:
      if (k < st.drawing.pdfAttachments.size())
        grow(st.drawing.pdfAttachments[k].insertX, st.drawing.pdfAttachments[k].insertY, 0.0);
      break;
    case T::FilledRegion:
      if (k < st.drawing.cadFilledRegions.size()) {
        const CadFilledRegion& fr = st.drawing.cadFilledRegions[k];
        for (size_t vi = 0; vi + 2 < fr.vertsXyz.size(); vi += 3)
          grow(fr.vertsXyz[vi], fr.vertsXyz[vi + 1], fr.vertsXyz[vi + 2]);
      }
      break;
    case T::BlockRef:
      if (k < st.drawing.cadBlockRefs.size()) {
        const CadBlockRef& br = st.drawing.cadBlockRefs[k];
        float mnx = 0.f, mny = 0.f, mxx = 0.f, mxy = 0.f;
        CadBlockWorldAabb(st.drawing.blockDefs, br, &mnx, &mny, &mxx, &mxy);
        grow(mnx, mny, br.xf.z);
        grow(mxx, mxy, br.xf.z);
      }
      break;
    case T::Solid:
      // The one type Phase 5 exists for, and the only one whose bounds are exact here: the kernel
      // already owns the walk (`brep::ComputeBounds`), so there is nothing to re-derive.
      if (k < st.drawing.cadSolids.size() && st.drawing.cadSolids[k]) {
        const brep::Bounds b = brep::ComputeBounds(*st.drawing.cadSolids[k]);
        if (b.valid) {
          grow(b.mn.x, b.mn.y, b.mn.z);
          grow(b.mx.x, b.mx.y, b.mx.z);
        }
      }
      break;
    case T::Mesh:
    case T::Surface:
    case T::PipeRun:
    case T::PointCloud:
      // Display-only (REQ-063, REQ-068 / ADR-036 (b); PipeRun issue #486; PointCloud REQ-171 —
      // the same stated boundary Solid has, on the enum's own doc comment): every transform
      // refuses them by name, so a gizmo anchored partly on one would advertise a move that will
      // not happen to it.
      break;
    case T::SectionPlane:
      // REQ-343 amended / ADR-059 (i), issue #479. Unreachable in practice: the plane is a type TAG
      // only, never an entry in `st.selection` (its own dedicated `sectionPlaneSelected` bool tracks
      // that instead — see the enumerator's doc comment). Stated explicitly so this isn't a silent
      // -Wswitch fallthrough; if it ever were reachable, the plane's own grip drag (slide/flip/resize)
      // is its only way to move, so it should not contribute to this transform gizmo's bounds anyway.
      break;
    }
  }
  if (!any)
    return false;
  *out = ray3d::Vec3{0.5 * (mn.x + mx.x), 0.5 * (mn.y + mx.y), 0.5 * (mn.z + mx.z)};
  return true;
}

ray3d::Vec3 CadGizmoAxisWorld(const AppCommandState& st, int axis) {
  // FACE MODE: the face's own outward normal, which is the only direction `brep::PushPullFace` can
  // move it in. There is one handle, so `axis` is ignored - the caller's loop is bounded by
  // `CadGizmoAxisCountFor`, which returns 1 here.
  {
    const CadGizmoMode mode = CadGizmoModeFor(st);
    if (mode != CadGizmoMode::Entity && mode != CadGizmoMode::None &&
        st.subObjectSelection.size() == 1) {
      const SelectedSubObject& sub = st.subObjectSelection.front();
      ray3d::Vec3 anchor{};
      ray3d::Vec3 nA{};
      ray3d::Vec3 nB{};
      if (mode == CadGizmoMode::SubObjectFace && CadSubObjectFaceGrip(st, sub, &anchor, &nA))
        return nA;
      // An EDGE's two handles are the two adjacent faces' own outward normals — what the kernel
      // actually offsets, and together exactly the plane perpendicular to the edge.
      if (mode == CadGizmoMode::SubObjectEdge && CadSubObjectEdgeGrip(st, sub, &anchor, &nA, &nB))
        return axis == 0 ? nA : nB;
      // A VERTEX deliberately falls through to the UCS axes below: three planes meet there, so every
      // direction is reachable, and the natural basis is the one the grid, ORTHO and the entity
      // gizmo already use (REQ-154).
    }
  }
  // ENTITY MODE: the ACTIVE UCS, not the world frame. The grid, ORTHO and coordinate entry all take
  // their directions from it (REQ-154), and a gizmo that disagreed with the grid it is drawn over
  // would be the only thing in the viewport pointing somewhere else. In the World UCS — the default,
  // and what every existing drawing has — the two are identical.
  const ucs::Ucs& u = st.activeUcs;
  // ROTATE: ring `axis` turns about UCS axis `axis` — the ring's own normal (D-2026-09-28-b).
  // SCALE: handle `axis` lies along UCS axis `axis`, but it is only a DIRECTION TO DRAG ALONG — a
  // uniform scale has no axis, so all three give the same result (D-2026-09-28-c).
  const int pick = std::clamp(axis, 0, 2);
  const ray3d::Vec3 v = pick == 0 ? u.xAxis : pick == 1 ? u.yAxis : u.zAxis;
  const double len = ray3d::Length(v);
  if (len > 1.e-12)
    return ray3d::Scale(v, 1.0 / len);
  return ray3d::Vec3{pick == 0 ? 1.0 : 0.0, pick == 1 ? 1.0 : 0.0, pick == 2 ? 1.0 : 0.0};
}

/// The ortho half-height \ref CadViewCamera builds its projection from, repeated rather than
/// reached for: this function has no viewport size to construct a Camera with, and the expression

bool CadGizmoVisible(const AppCommandState& st) {
  if (CadGizmoModeFor(st) == CadGizmoMode::None)
    return false;
  ray3d::Vec3 a{};
  return CadGizmoAnchorWorld(st, &a);
}

bool CadAxisDragParam(const ray3d::Vec3& anchor, const ray3d::Vec3& axisDir, const ray3d::Ray& ray,
                      double* outParam, double parallelTol) {
  if (!outParam)
    return false;
  const double axisLen = ray3d::Length(axisDir);
  const double dirLen = ray3d::Length(ray.dir);
  if (axisLen < 1.e-12 || dirLen < 1.e-12)
    return false;
  const ray3d::Vec3 u = ray3d::Scale(axisDir, 1.0 / axisLen);
  const ray3d::Vec3 d = ray3d::Scale(ray.dir, 1.0 / dirLen);
  // Closest points of two skew lines: P = anchor + s*u, Q = ray.origin + t*d, with (P - Q)
  // perpendicular to both. With u and d unit that is  s - t(u.d) = -(w.u)  and  s(u.d) - t = -(w.d),
  // where w = anchor - ray.origin.
  const ray3d::Vec3 w = ray3d::Sub(anchor, ray.origin);
  const double b = ray3d::Dot(u, d);
  const double denom = b * b - 1.0;
  // |b| -> 1 is the ray looking straight down the handle: every point of it projects to the same
  // pixel, so no distance is being expressed. Refusing is the honest answer — a near-singular
  // divide returns an enormous number, which reads on screen as the selection flying away.
  if (std::fabs(denom) < parallelTol)
    return false;
  const double t = (b * ray3d::Dot(w, u) - ray3d::Dot(w, d)) / denom;
  *outParam = t * b - ray3d::Dot(w, u);
  return true;
}

bool CadAxisDragAngle(const ray3d::Vec3& anchor, const ray3d::Vec3& axisDir, const ray3d::Ray& ray,
                      double* outAngle, double parallelTol, double minRadius) {
  if (!outAngle)
    return false;
  const double axisLen = ray3d::Length(axisDir);
  const double dirLen = ray3d::Length(ray.dir);
  if (axisLen < 1.e-12 || dirLen < 1.e-12)
    return false;
  const ray3d::Vec3 n = ray3d::Scale(axisDir, 1.0 / axisLen);
  const ray3d::Vec3 d = ray3d::Scale(ray.dir, 1.0 / dirLen);
  // Where the ray meets the ROTATION PLANE — the plane through the anchor whose normal is the axis.
  // A ray parallel to that plane never reaches it, so the gesture names no point at all; that is the
  // rotation counterpart of `CadAxisDragParam`'s end-on refusal, and it is refused for the same
  // reason rather than answered from a near-singular divide.
  const double denom = ray3d::Dot(n, d);
  if (std::fabs(denom) < parallelTol)
    return false;
  const double t = ray3d::Dot(n, ray3d::Sub(anchor, ray.origin)) / denom;
  const ray3d::Vec3 hit = ray3d::Add(ray.origin, ray3d::Scale(d, t));
  const ray3d::Vec3 r = ray3d::Sub(hit, anchor);
  // Dead centre there is no direction to take an angle OF. A hit within `minRadius` of the anchor is
  // refused rather than given whatever `atan2(0, 0)` happens to return.
  if (ray3d::Length(r) < minRadius)
    return false;
  // The measuring frame is built from the AXIS alone, never from the camera, so the angle a drag
  // reports does not change when the view orbits underneath it — the same choice the arrowhead
  // geometry makes a few hundred lines below, and for the same reason.
  ray3d::Vec3 seed{0.0, 0.0, 1.0};
  if (std::fabs(ray3d::Dot(n, seed)) > 0.9)
    seed = ray3d::Vec3{1.0, 0.0, 0.0};
  const ray3d::Vec3 e0 = ray3d::Normalize(ray3d::Cross(seed, n));
  const ray3d::Vec3 e1 = ray3d::Cross(n, e0);  // right-handed: e0 x e1 = n, so CCW is positive
  *outAngle = std::atan2(ray3d::Dot(r, e1), ray3d::Dot(r, e0));
  return true;
}

int PickGizmoAxis(const AppCommandState& st, const ray3d::Ray& ray, double tolWorld) {
  ray3d::Vec3 anchor{};
  if (!CadGizmoVisible(st) || !CadGizmoAnchorWorld(st, &anchor))
    return -1;
  const double len = static_cast<double>(CadGizmoHandleLenWorld(st));
  const double dirLen = ray3d::Length(ray.dir);
  if (dirLen < 1.e-12)
    return -1;
  const ray3d::Vec3 d = ray3d::Scale(ray.dir, 1.0 / dirLen);
  int best = -1;
  double bestDist = tolWorld;
  const int axisCount = CadGizmoAxisCountFor(st);

  // ROTATE's handle is a RING, not a segment, so it needs its own hit test: meet the rotation plane
  // and ask how far the hit is from the ring's radius. Running the segment test below on it would
  // grab along the UCS Z AXIS — the one line the ring never occupies — so the widget would be
  // ungrabbable everywhere it is drawn and grabbable where it is not.
  //
  // With three rings (D-2026-09-28-b) the test is the distance from the RAY to each ring, measured
  // at sample points round it, so an obliquely-seen ring is judged by how close it looks rather than
  // by where the ray happens to pierce its plane. The nearest ring within the aperture wins.
  if (st.gizmoOp == CadGizmoOp::Rotate) {
    constexpr int kRingSamples = 96;
    constexpr double kTwoPi = 6.28318530717958647692;
    for (int axis = 0; axis < axisCount; ++axis) {
      const ray3d::Vec3 n = CadGizmoAxisWorld(st, axis);
      // Seen (nearly) edge-on, a ring is a line on screen and a drag round it names no angle —
      // `CadAxisDragAngle` would refuse or jitter — so it is not offered as a target at all.
      if (std::fabs(ray3d::Dot(n, d)) < kGizmoRingEdgeOnCos)
        continue;
      ray3d::Vec3 seed{0.0, 0.0, 1.0};
      if (std::fabs(ray3d::Dot(n, seed)) > 0.9)
        seed = ray3d::Vec3{1.0, 0.0, 0.0};
      const ray3d::Vec3 e0 = ray3d::Normalize(ray3d::Cross(seed, n));
      const ray3d::Vec3 e1 = ray3d::Cross(n, e0);
      for (int i = 0; i < kRingSamples; ++i) {
        const double th = kTwoPi * static_cast<double>(i) / static_cast<double>(kRingSamples);
        const ray3d::Vec3 p = ray3d::Add(
            anchor, ray3d::Add(ray3d::Scale(e0, std::cos(th) * len), ray3d::Scale(e1, std::sin(th) * len)));
        const ray3d::Vec3 rel = ray3d::Sub(p, ray.origin);
        const double along = ray3d::Dot(rel, d);
        const double dist = ray3d::Length(ray3d::Sub(rel, ray3d::Scale(d, along)));
        if (dist < bestDist) {
          bestDist = dist;
          best = axis;
        }
      }
    }
    return best;
  }

  for (int axis = 0; axis < axisCount; ++axis) {
    const ray3d::Vec3 u = CadGizmoAxisWorld(st, axis);
    double s = 0.0;
    if (!CadAxisDragParam(anchor, u, ray, &s))
      continue;  // sighting down this handle: not grabbable, and not a near miss either
    // The handle is a SEGMENT from the anchor to its tip, so a point past either end is measured
    // from that end. Without the clamp the whole infinite axis line grabs, and a click anywhere
    // along a world axis — far outside the widget — would start a drag.
    s = std::clamp(s, 0.0, len);
    const ray3d::Vec3 onAxis = ray3d::Add(anchor, ray3d::Scale(u, s));
    const ray3d::Vec3 rel = ray3d::Sub(onAxis, ray.origin);
    const double along = ray3d::Dot(rel, d);
    const double dist = ray3d::Length(ray3d::Sub(rel, ray3d::Scale(d, along)));
    if (dist < bestDist) {
      bestDist = dist;
      best = axis;
    }
  }
  if (best >= 0)
    return best;
  // The PLANE handles (D-2026-09-28-c): a square in each UCS plane, from the anchor out to
  // `kGizmoPlaneHandleFrac` of the handle along both of its axes. An arrow near the cursor wins
  // (above); failing that, the square the ray passes through, nearest the eye first. A plane seen
  // (nearly) edge-on is a line on screen and names no in-plane offset, so it is not offered.
  double bestT = std::numeric_limits<double>::infinity();
  const double side = len * kGizmoPlaneHandleFrac;
  for (int p = 0; p < CadGizmoPlaneHandleCountFor(st); ++p) {
    int ia = 0;
    int ib = 0;
    CadGizmoPlaneAxes(p, &ia, &ib);
    const ray3d::Vec3 ua = CadGizmoAxisWorld(st, ia);
    const ray3d::Vec3 ub = CadGizmoAxisWorld(st, ib);
    const ray3d::Vec3 n = ray3d::Normalize(ray3d::Cross(ua, ub));
    const double denom = ray3d::Dot(n, d);
    if (std::fabs(denom) < kGizmoRingEdgeOnCos)
      continue;
    const double t = ray3d::Dot(n, ray3d::Sub(anchor, ray.origin)) / denom;
    const ray3d::Vec3 rel = ray3d::Sub(ray3d::Add(ray.origin, ray3d::Scale(d, t)), anchor);
    const double ca = ray3d::Dot(rel, ua);
    const double cb = ray3d::Dot(rel, ub);
    if (ca >= 0.0 && ca <= side && cb >= 0.0 && cb <= side && t < bestT) {
      bestT = t;
      best = kGizmoPlaneHandleFirst + p;
    }
  }
  return best;
}

void UpdateGizmoHover(AppCommandState& st, const ray3d::Ray& ray, double tolWorld) {
  if (st.gizmoDragActive)
    return;  // the grabbed handle stays lit; pre-highlighting one the click cannot reach is a lie
  st.gizmoHoverAxis = PickGizmoAxis(st, ray, tolWorld);
}

void UpdateGizmoDrag(AppCommandState& st, const ray3d::Ray& ray) {
  if (!st.gizmoDragActive)
    return;
  // Each operation reads the cursor through its own solve, and each holds the last good value rather
  // than jumping when the gesture stops meaning anything (see both solves' refusal notes).
  //
  // A PLANE handle (D-2026-09-28-c): where the ray meets the grabbed plane, less where the grab met
  // it — an in-plane offset by construction. A ray (nearly) parallel to the plane meets it nowhere
  // useful, so the last good offset is held.
  if (st.gizmoDragAxis >= kGizmoPlaneHandleFirst) {
    const ray3d::Vec3 n = st.gizmoAxisDir;
    const double dirLen = ray3d::Length(ray.dir);
    if (dirLen < 1.e-12)
      return;
    const ray3d::Vec3 dir = ray3d::Scale(ray.dir, 1.0 / dirLen);
    const double denom = ray3d::Dot(n, dir);
    if (std::fabs(denom) < 1.e-6)
      return;
    const double t = ray3d::Dot(n, ray3d::Sub(st.gizmoAnchor, ray.origin)) / denom;
    st.gizmoDragVec = ray3d::Sub(ray3d::Add(ray.origin, ray3d::Scale(dir, t)), st.gizmoGrabPoint);
    st.gizmoDragDistance = ray3d::Length(st.gizmoDragVec);
    return;
  }
  if (st.gizmoOp == CadGizmoOp::Rotate) {
    double a = 0.0;
    if (!CadAxisDragAngle(st.gizmoAnchor, st.gizmoAxisDir, ray, &a))
      return;  // ray parallel to the rotation plane, or dead centre: no angle is being expressed
    // Wrapped into (-pi, pi] so a drag past the far side reads as a turn the short way rather than
    // as a jump of nearly a full revolution.
    constexpr double kPi = 3.14159265358979323846;
    double delta = a - st.gizmoGrabParam;
    while (delta > kPi)
      delta -= 2.0 * kPi;
    while (delta <= -kPi)
      delta += 2.0 * kPi;
    st.gizmoDragDistance = delta;
    return;
  }
  double s = 0.0;
  if (!CadAxisDragParam(st.gizmoAnchor, st.gizmoAxisDir, ray, &s))
    return;  // sighting down the axis: hold the last distance rather than jump
  if (st.gizmoOp == CadGizmoOp::Scale) {
    // The RATIO of where the handle is now to where it was grabbed. A grab parameter at the anchor
    // gives no baseline to divide by, and a non-positive ratio means the cursor has crossed the
    // anchor and come out the far side — which is a MIRROR, its own operation (REQ-332 item 7) and
    // emphatically not a scale. Both hold the last good factor instead of inventing one.
    if (std::fabs(st.gizmoGrabParam) < 1.e-9)
      return;
    const double f = s / st.gizmoGrabParam;
    if (!(f > 0.0) || !std::isfinite(f))
      return;
    st.gizmoDragDistance = f;
    return;
  }
  st.gizmoDragDistance = s - st.gizmoGrabParam;
  st.gizmoDragVec = ray3d::Scale(st.gizmoAxisDir, st.gizmoDragDistance);
}

void CancelGizmoDrag(AppCommandState& st) {
  st.gizmoDragActive = false;
  st.gizmoDragAxis = -1;
  st.gizmoDragVec = ray3d::Vec3{0.0, 0.0, 0.0};
  // The neutral value is the operation's own: a scale of ZERO is a collapse, not "no drag" (see
  // `gizmoDragDistance`'s table).
  st.gizmoDragDistance = st.gizmoOp == CadGizmoOp::Scale ? 1.0 : 0.0;
  st.gizmoDragIsSubObject = false;
}

static bool CommitGizmoDragImpl(AppCommandState& st, std::vector<std::string>& log);

bool CommitGizmoDrag(AppCommandState& st, std::vector<std::string>& log) {
  const bool committed = CommitGizmoDragImpl(st, log);
  // 3DMOVE / 3DROTATE / 3DSCALE are ONE operation each (GitHub issue #564 section 3): the commit
  // that just pushed its undo step is also the end of the command. Here rather than at each caller
  // so the click, the headless DROP and the typed value all end it the same way. A drag that
  // changed nothing, or one the kernel refused, leaves the command running to try again.
  if (committed && IsGizmoCommandKind(st.active))
    EndGizmoCommand(st);
  return committed;
}

static bool CommitGizmoDragImpl(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.gizmoDragActive)
    return false;
  const double dist = st.gizmoDragDistance;
  const int axis = st.gizmoDragAxis;
  const ray3d::Vec3 u = st.gizmoAxisDir;
  const ray3d::Vec3 moveVec = st.gizmoDragVec;  // TRANSLATE's displacement, axis or plane handle
  const bool onFace = st.gizmoDragIsSubObject;
  const SelectedSubObject face = st.gizmoDragSubObject;
  const solidpick::Kind subKind = face.kind;
  const CadGizmoOp op = st.gizmoOp;
  const ray3d::Vec3 anchor = st.gizmoAnchor;
  CancelGizmoDrag(st);
  // "Nothing happened" is measured against the OPERATION's neutral value, because a scale of zero is
  // a collapse rather than a no-op (see `gizmoDragDistance`'s table).
  const double neutral = op == CadGizmoOp::Scale ? 1.0 : 0.0;
  if (std::fabs(dist - neutral) < 1.e-12)
    return false;  // a click that changed nothing is a cancel, not an empty undo step

  // ROTATE / SCALE act on the ENTITY selection only — `CadGizmoModeFor` already returns None for a
  // sub-object selection under either, so a face drag can only be a translate.
  if (!onFace && op == CadGizmoOp::Rotate) {
    // ONE undo snapshot for the whole drag, and then the SAME function typed ROTATE calls — dispatch
    // included, so the agreement holds under a tilted UCS as well as in plan (REQ-060 acceptance 2).
    PushUndoSnapshot(st, "Rotate");
    // The Z ring is typed ROTATE exactly (REQ-060's agreement, byte-identical in plan); the X and Y
    // rings turn about their own UCS axis through the same anchor (D-2026-09-28-b).
    if (axis == 2)
      ApplyRotationAboutUcsZ(st, static_cast<float>(anchor.x), static_cast<float>(anchor.y),
                             static_cast<float>(anchor.z), static_cast<float>(dist), log);
    else
      ApplyRotationAboutAxis(st, anchor, u, static_cast<float>(dist), log);
    char rbuf[128];
    std::snprintf(rbuf, sizeof(rbuf), "Gizmo rotate: %.4f degrees about the UCS %s axis.",
                  dist * 180.0 / 3.14159265358979323846, axis == 0 ? "X" : axis == 1 ? "Y" : "Z");
    log.push_back(rbuf);
    return true;
  }
  if (!onFace && op == CadGizmoOp::Scale) {
    PushUndoSnapshot(st, "Scale");
    ApplyUniformScaleAboutBase(st, static_cast<float>(anchor.x), static_cast<float>(anchor.y),
                               static_cast<float>(anchor.z), static_cast<float>(dist), log);
    char sbuf[128];
    std::snprintf(sbuf, sizeof(sbuf), "Gizmo scale: %.4f about the selection centre.", dist);
    log.push_back(sbuf);
    return true;
  }

  // FACE MODE: the SAME function typed `PRESSPULL` calls, which is what makes issue #148's fourth
  // criterion ("the gizmo ... matches the equivalent typed command within REQ-101") true by
  // construction. `CadApplyPushPull` owns the undo snapshot, the re-pointing of every sub-object
  // reference that named the replaced solid, and the kernel's own sentence on a refusal — none of
  // which should be said twice.
  //
  // The distance passes through unchanged because the gizmo's axis IS the face normal
  // (`CadSubObjectFaceGrip` supplies both), so positive is outward in both.
  if (onFace) {
    // Which of the three sub-object edits this is, captured at the grab like everything else about
    // the drag. Each goes through the one function that owns it, so the undo step, the re-pointed
    // selection and the kernel's refusal sentence are said once rather than three times.
    if (subKind == solidpick::Kind::Vertex) {
      if (!CadApplyMoveVertex(st, face, ray3d::Scale(u, dist), log))
        return false;
      char vBuf[128];
      std::snprintf(vBuf, sizeof(vBuf), "Gizmo move vertex: %.4f along %s.", dist,
                    axis == 0 ? "X" : axis == 1 ? "Y" : "Z");
      log.push_back(vBuf);
      return true;
    }
    if (subKind == solidpick::Kind::Edge) {
      if (!CadApplyMoveEdge(st, face, ray3d::Scale(u, dist), log))
        return false;
      char eBuf[128];
      std::snprintf(eBuf, sizeof(eBuf), "Gizmo move edge: %.4f along face %d's normal.", dist,
                    axis);
      log.push_back(eBuf);
      return true;
    }
    if (!CadApplyPushPull(st, face, dist, log))
      return false;  // refused by the kernel and already reported; the document is untouched
    char faceBuf[128];
    std::snprintf(faceBuf, sizeof(faceBuf), "Gizmo push/pull: %.4f along the face normal.", dist);
    log.push_back(faceBuf);
    return true;
  }

  // ENTITY MODE: ONE undo snapshot for the whole drag (REQ-060 acceptance 1: "one Ctrl+Z restores
  // the prior state in a single step"), and then the SAME function typed MOVE calls. That is the
  // whole design of this feature: "a gizmo drag and the equivalent typed command produce
  // coordinates agreeing within REQ-101" holds because there is one implementation, not because two
  // agree today.
  PushUndoSnapshot(st, "Move");
  ApplyTranslationToSelection(st, static_cast<float>(moveVec.x), static_cast<float>(moveVec.y),
                              static_cast<float>(moveVec.z), log);
  char buf[128];
  if (axis >= kGizmoPlaneHandleFirst) {
    const int p = axis - kGizmoPlaneHandleFirst;
    std::snprintf(buf, sizeof(buf), "Gizmo move: %.4f in the UCS %s plane.", dist,
                  p == 0 ? "XY" : p == 1 ? "YZ" : "ZX");
  } else {
    std::snprintf(buf, sizeof(buf), "Gizmo move: %.4f along %s.", dist,
                  axis == 0 ? "X" : axis == 1 ? "Y" : "Z");
  }
  log.push_back(buf);
  return true;
}

/// Arms a drag on handle \p axis — the state a grab click leaves, and what a typed exact value in a
/// 3D gizmo command starts from. False when a sub-object gizmo has lost its one sub-object.
static bool ArmGizmoDrag(AppCommandState& st, int axis, const ray3d::Vec3& anchor,
                         const ray3d::Vec3& u, double grabParam) {
  // WHICH face, captured now rather than read at the commit: the selection can be cleared or
  // re-picked between the two clicks, and the drag belongs to the face the user actually grabbed.
  // A sub-object drag of ANY kind — face, edge or vertex — captures its target now rather than
  // reading it at the commit, for the reason the face case already gave: the selection can be
  // cleared or re-picked between the two clicks, and the drag belongs to what the user grabbed.
  const CadGizmoMode grabMode = CadGizmoModeFor(st);
  const bool onFace = grabMode == CadGizmoMode::SubObjectFace ||
                      grabMode == CadGizmoMode::SubObjectEdge ||
                      grabMode == CadGizmoMode::SubObjectVertex;
  SelectedSubObject face;
  if (onFace) {
    if (st.subObjectSelection.size() != 1)
      return false;
    face = st.subObjectSelection.front();
  }
  st.gizmoDragActive = true;
  st.gizmoDragAxis = axis;
  st.gizmoAnchor = anchor;
  st.gizmoAxisDir = u;
  st.gizmoGrabParam = grabParam;
  st.gizmoDragDistance = st.gizmoOp == CadGizmoOp::Scale ? 1.0 : 0.0;  // the operation's neutral
  st.gizmoDragVec = ray3d::Vec3{0.0, 0.0, 0.0};
  st.gizmoGrabPoint = anchor;
  st.gizmoHoverAxis = axis;
  st.gizmoDragIsSubObject = onFace;
  st.gizmoDragSubObject = face;
  return true;
}

bool SubmitGizmoClick(AppCommandState& st, const ray3d::Ray& ray, double tolWorld,
                      std::vector<std::string>& log) {
  if (st.gizmoDragActive) {
    UpdateGizmoDrag(st, ray);
    CommitGizmoDrag(st, log);
    return true;  // the click was the gizmo's whether or not the distance came out zero
  }
  const int axis = PickGizmoAxis(st, ray, tolWorld);
  if (axis < 0)
    return false;
  ray3d::Vec3 anchor{};
  if (!CadGizmoAnchorWorld(st, &anchor))
    return false;
  // A PLANE handle (D-2026-09-28-c): the grab is where the ray meets the plane, and the drag is
  // measured from there, so the selection does not jump to the cursor on the first move.
  if (axis >= kGizmoPlaneHandleFirst) {
    const int p = axis - kGizmoPlaneHandleFirst;
    int ia = 0;
    int ib = 0;
    CadGizmoPlaneAxes(p, &ia, &ib);
    const ray3d::Vec3 n =
        ray3d::Normalize(ray3d::Cross(CadGizmoAxisWorld(st, ia), CadGizmoAxisWorld(st, ib)));
    const double dirLen = ray3d::Length(ray.dir);
    const double denom = dirLen > 1.e-12 ? ray3d::Dot(n, ray3d::Scale(ray.dir, 1.0 / dirLen)) : 0.0;
    if (std::fabs(denom) < 1.e-6)
      return false;
    const ray3d::Vec3 dir = ray3d::Scale(ray.dir, 1.0 / dirLen);
    const double t = ray3d::Dot(n, ray3d::Sub(anchor, ray.origin)) / denom;
    if (!ArmGizmoDrag(st, axis, anchor, n, 0.0))
      return false;
    st.gizmoGrabPoint = ray3d::Add(ray.origin, ray3d::Scale(dir, t));
    log.push_back(std::string("Gizmo: dragging in the UCS ") + (p == 0 ? "XY" : p == 1 ? "YZ" : "ZX") +
                  " plane — click to place, or type dx,dy and Enter. ESC cancels.");
    return true;
  }
  const ray3d::Vec3 u = CadGizmoAxisWorld(st, axis);
  double s = 0.0;
  // The grab is captured through the SAME solve the drag will use, so the two are measured in one
  // set of units and the difference (or ratio) between them is meaningful.
  if (st.gizmoOp == CadGizmoOp::Rotate) {
    if (!CadAxisDragAngle(anchor, u, ray, &s))
      return false;  // grabbed while looking along the rotation plane: no angle to measure from
  } else if (!CadAxisDragParam(anchor, u, ray, &s)) {
    return false;  // grabbed while sighting down the handle: nothing to measure from
  } else if (st.gizmoOp == CadGizmoOp::Scale && std::fabs(s) < 1.e-9) {
    return false;  // grabbed at the anchor: a scale is a RATIO, and there is no baseline here
  }
  if (!ArmGizmoDrag(st, axis, anchor, u, s))
    return false;
  if (st.gizmoDragIsSubObject) {
    log.push_back("Gizmo: pushing the face along its normal — click to place, ESC to cancel.");
  } else {
    log.push_back(std::string("Gizmo: dragging along ") + (axis == 0 ? "X" : axis == 1 ? "Y" : "Z") +
                  " — click to place, ESC to cancel.");
  }
  return true;
}

// ---------------------------------------------------------------------------------------------
// 3DMOVE / 3DROTATE / 3DSCALE (GitHub issue #564 section 3, D-2026-09-28-a).
//
// The REQ-060 gizmo as a command's UI rather than a side effect of selecting. The commands add no
// transform of their own: they set `gizmoOp` for their duration, show the existing widget, and let
// `CommitGizmoDrag` — the one function every gizmo commit already goes through — do the work and
// the one undo step. That function also ends the command, so a click, a typed value and a headless
// DROP all finish it the same way.
// ---------------------------------------------------------------------------------------------

static CadGizmoOp GizmoOpForCommandKind(AppCommandState::Kind k) {
  if (k == AppCommandState::Kind::Rotate3d)
    return CadGizmoOp::Rotate;
  if (k == AppCommandState::Kind::Scale3d)
    return CadGizmoOp::Scale;
  return CadGizmoOp::Translate;
}

void EndGizmoCommand(AppCommandState& st) {
  CancelGizmoDrag(st);  // with the command's op still set: the cancel's neutral value is per-op
  if (const auto* sess = st.sessionAs<stepengine::GizmoCmdSession>())
    st.gizmoOp = sess->opBeforeCmd;
  st.gizmoHoverAxis = -1;
  stepengine::EndSession(st);
}

std::string CadGizmoCommandPromptText(const AppCommandState& st) {
  const auto* sess = st.sessionAs<stepengine::GizmoCmdSession>();
  const auto phase = sess ? sess->phase : stepengine::GizmoCmdPhase::SelectObjects;
  const std::string verb = AppCommandState::KindName(st.active);
  if (phase == stepengine::GizmoCmdPhase::SelectObjects) {
    std::string s = verb + " — select objects";
    if (st.active == AppCommandState::Kind::Move3d)
      s += " (Ctrl+click a solid face, edge or vertex)";
    const size_t have = st.selection.size() + st.subObjectSelection.size();
    if (have > 0)
      s += " (" + std::to_string(have) + " selected)";
    return s + ", Enter when done. ESC cancels.";
  }
  if (phase == stepengine::GizmoCmdPhase::BasePoint)
    return verb + " — specify base point (click, snap or type X,Y[,Z]) <Enter = centre of the "
                  "selection>. ESC cancels.";
  const char* what = st.gizmoOp == CadGizmoOp::Rotate  ? "an angle in degrees"
                     : st.gizmoOp == CadGizmoOp::Scale ? "a scale factor"
                                                       : "a distance";
  if (st.gizmoDragActive)
    return verb + " — click to place, or type " + what + " and Enter. ESC cancels.";
  if (CadGizmoAxisCountFor(st) == 1)
    return verb + " — drag the handle, or type " + what + " and Enter. ESC cancels.";
  if (st.gizmoOp == CadGizmoOp::Rotate)
    return verb + " — click a ring (X red, Y green, Z blue) to turn about that UCS axis, then "
                  "click to place or type " + what + ". ESC cancels.";
  if (st.gizmoOp == CadGizmoOp::Scale)
    return verb + " — drag any axis handle (all scale evenly), or type " + what +
           " and Enter. ESC cancels.";
  return verb + " — drag an arrow, or a corner square to slide in that plane; then click to place "
                "or type " + what + " (dx,dy for a plane). ESC cancels.";
}

void SubmitGizmoBasePoint(AppCommandState& st, double x, double y, double z,
                          std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::GizmoCmdSession>();
  if (!sess || sess->phase != stepengine::GizmoCmdPhase::BasePoint)
    return;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
    log.push_back(std::string(AppCommandState::KindName(st.active)) +
                  " — that base point is not a finite coordinate.");  // REQ-201
    return;
  }
  sess->base = ray3d::Vec3{x, y, z};
  sess->baseValid = true;
  sess->phase = stepengine::GizmoCmdPhase::Handles;
  log.push_back(CadGizmoCommandPromptText(st));
}

/// Move from "select objects" to the base point (whole entities) or straight to the handles (a
/// solid face / edge / vertex, whose handle sits where its kernel edit acts) — or say why there is
/// no gizmo and end the command (REQ-201): a gizmo command with nothing to drag would look hung.
static void EnterGizmoHandlesPhase(AppCommandState& st, std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::GizmoCmdSession>();
  if (!sess)
    return;
  const std::string verb = AppCommandState::KindName(st.active);
  sess->phase = stepengine::GizmoCmdPhase::Handles;
  sess->baseValid = false;
  if (CadGizmoVisible(st)) {
    if (st.subObjectSelection.empty())
      sess->phase = stepengine::GizmoCmdPhase::BasePoint;  // D-2026-09-28-c
    log.push_back(CadGizmoCommandPromptText(st));
    return;
  }
  const std::string opVerb = st.gizmoOp == CadGizmoOp::Rotate  ? "rotated"
                             : st.gizmoOp == CadGizmoOp::Scale ? "scaled"
                                                               : "moved";
  std::string why = "nothing selected can be " + opVerb + ".";
  if (st.subObjectSelection.size() > 1)
    why = "several faces, edges or vertices have no single handle; select one.";
  else if (st.subObjectSelection.size() == 1 && st.gizmoOp != CadGizmoOp::Translate)
    why = "a solid's face, edge or vertex can only be moved, not " + opVerb + ".";
  else if (st.subObjectSelection.size() == 1)
    why = "the kernel cannot move that face, edge or vertex (a curved face, a cylinder's rim or a "
          "pyramid's apex, for example).";
  log.push_back(verb + " — " + why + " Nothing changed.");
  EndGizmoCommand(st);
}

void StartGizmoCommand(AppCommandState& st, AppCommandState::Kind kind,
                       std::vector<std::string>& log) {
  const std::string verb = AppCommandState::KindName(kind);
  if (IsGizmoCommandKind(st.active))
    EndGizmoCommand(st);  // restarting: put the previous command's op back before saving it again
  if (st.activeSpaceIndex != kModelSpaceIndex) {
    // Paper space is a 2D sheet (ADR-025 (g)); the gizmo has no third handle to draw there.
    log.push_back(verb + " — works in model space only.");
    return;
  }
  ClearPendingViewportZoom(st);
  const CadGizmoOp savedOp = st.gizmoOp;
  stepengine::BeginSession(st, std::make_unique<stepengine::GizmoCmdSession>(kind, savedOp),
                           /*recordAsLastCommand=*/true);
  CancelGizmoDrag(st);  // a persistent-gizmo drag armed under the old op cannot carry over
  st.gizmoOp = GizmoOpForCommandKind(kind);
  st.gizmoHoverAxis = -1;
  if (!st.selection.empty() || !st.subObjectSelection.empty()) {
    EnterGizmoHandlesPhase(st, log);  // a pre-selection goes straight to the handles
    return;
  }
  log.push_back(CadGizmoCommandPromptText(st));
}

bool HandleGizmoCommandTextInput(const std::string& lineIn, AppCommandState& st,
                                 std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::GizmoCmdSession>();
  if (!sess)
    return false;
  const std::string verb = AppCommandState::KindName(st.active);
  const std::string line = StringUtil::trimCopy(lineIn);
  if (sess->phase == stepengine::GizmoCmdPhase::SelectObjects) {
    if (!line.empty()) {
      log.push_back(CadGizmoCommandPromptText(st));
      return true;
    }
    if (st.selection.empty() && st.subObjectSelection.empty()) {
      log.push_back(verb + " — select at least one object, then Enter. ESC cancels.");
      return true;
    }
    EnterGizmoHandlesPhase(st, log);
    return true;
  }

  // BASE POINT (D-2026-09-28-c). Enter keeps the default — the centre of the selection's box — and
  // a typed point is read the way MOVE reads its base point.
  if (sess->phase == stepengine::GizmoCmdPhase::BasePoint) {
    if (line.empty()) {
      sess->baseValid = false;
      sess->phase = stepengine::GizmoCmdPhase::Handles;
      log.push_back(CadGizmoCommandPromptText(st));
      return true;
    }
    float bx = 0.f;
    float by = 0.f;
    float bz = 0.f;
    bool consumed = false;
    if (!ResolveTypedModifyPoint(st, line, false, 0.f, 0.f, 0.f, verb.c_str(), &bx, &by, &bz,
                                 &consumed, log)) {
      if (!consumed)
        log.push_back(verb + " — could not read '" + line + "' as a point. " +
                      CadGizmoCommandPromptText(st));
      return true;
    }
    SubmitGizmoBasePoint(st, bx, by, bz, log);
    return true;
  }

  // HANDLES. A blank Enter commits what an armed drag shows — or, with nothing armed, ends the
  // command having changed nothing.
  if (line.empty()) {
    if (st.gizmoDragActive) {
      if (!CommitGizmoDrag(st, log))
        log.push_back(CadGizmoCommandPromptText(st));  // a zero drag, or refused and reported
      return true;
    }
    log.push_back(verb + " — nothing changed.");
    EndGizmoCommand(st);
    return true;
  }

  // A PLANE handle grabbed (D-2026-09-28-c): "dx,dy" along the plane's own two UCS axes.
  if (st.gizmoDragActive && st.gizmoDragAxis >= kGizmoPlaneHandleFirst) {
    double da = 0.0;
    double db = 0.0;
    if (!ParseTwoDoubles(line, &da, &db) || !std::isfinite(da) || !std::isfinite(db)) {
      log.push_back(verb + " — type the offset in the plane as dx,dy.");
      return true;
    }
    int ia = 0;
    int ib = 0;
    CadGizmoPlaneAxes(st.gizmoDragAxis - kGizmoPlaneHandleFirst, &ia, &ib);
    st.gizmoDragVec = ray3d::Add(ray3d::Scale(CadGizmoAxisWorld(st, ia), da),
                                 ray3d::Scale(CadGizmoAxisWorld(st, ib), db));
    st.gizmoDragDistance = ray3d::Length(st.gizmoDragVec);
    if (!CommitGizmoDrag(st, log))
      log.push_back(CadGizmoCommandPromptText(st));
    return true;
  }

  // An exact value, in the operation's own units.
  const char* begin = line.c_str();
  char* end = nullptr;
  const double v = std::strtod(begin, &end);
  if (end == begin || *end != '\0' || !std::isfinite(v)) {
    log.push_back(verb + " — '" + line + "' is not a number. " + CadGizmoCommandPromptText(st));
    return true;
  }
  if (st.gizmoOp == CadGizmoOp::Scale && !(v > 0.0)) {
    // Zero collapses the selection and a negative factor is a mirror — its own operation
    // (REQ-332 item 7), not a scale.
    log.push_back(verb + " — the scale factor must be greater than zero.");
    return true;
  }
  if (!st.gizmoDragActive) {
    // With several handles, a bare number does not say which way; with one, it does. A SCALE says
    // nothing about a direction at all — every handle scales evenly (D-2026-09-28-c).
    if (CadGizmoAxisCountFor(st) != 1 && st.gizmoOp != CadGizmoOp::Scale) {
      log.push_back(st.gizmoOp == CadGizmoOp::Rotate
                        ? verb + " — click the ring to turn about first (X red, Y green, Z blue), "
                                 "then type the angle."
                        : verb + " — click the axis handle to move along first, then type the "
                                 "distance.");
      return true;
    }
    ray3d::Vec3 anchor{};
    if (!CadGizmoAnchorWorld(st, &anchor) ||
        !ArmGizmoDrag(st, 0, anchor, CadGizmoAxisWorld(st, 0), 0.0)) {
      log.push_back(verb + " — the selection no longer has a gizmo. Nothing changed.");
      EndGizmoCommand(st);
      return true;
    }
  }
  st.gizmoDragDistance =
      st.gizmoOp == CadGizmoOp::Rotate ? v * 3.14159265358979323846 / 180.0 : v;
  st.gizmoDragVec = ray3d::Scale(st.gizmoAxisDir, st.gizmoDragDistance);  // read by a TRANSLATE
  if (!CommitGizmoDrag(st, log))
    log.push_back(CadGizmoCommandPromptText(st));  // zero, or refused by the kernel and reported
  return true;
}
