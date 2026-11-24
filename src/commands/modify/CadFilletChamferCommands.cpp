#include "CadCommandsInternal.hpp"
#include "modify/CadCurveEditInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Geometry/FilletChamfer.hpp>
#include <GoSurvey/Geometry/curveintersect.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
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

// ================================================================================================
// FILLET (REQ-103 step 6a, TASK-102). Two picked curves (Line, non-full-circle Arc, or a Polyline
// segment) get a tangent arc between them (SolveFilletCenter et al., GoSurvey/Geometry/FilletChamfer.hpp — inline/pure
// so it's unit-tested independently of this command layer, tests/FilletGeomTests.cpp), then each
// curve is trimmed/extended to its own tangent point. Two different curves (Case B) reuses
// LENGTHEN's ApplyLengthenToLine/ToArc/ToPolylineEnd UNCHANGED, converting the known tangent point
// into the `newLength` those functions already accept — REQ-103's own stated reuse chain. Two
// adjacent segments of the SAME polyline (Case A) is new: it splices the shared vertex into the two
// tangent points (or one, at radius 0) via ReplacePolylineVerts — BREAK's own CSR-shift technique —
// and inserts a fresh Arc entity for the corner.
// ================================================================================================

/// Is `e` (picked at pickX,pickY) an eligible FILLET curve, and if it's a Polyline, which edge
/// (0-based) did the pick land nearest? Eligible: Line, non-full-circle Arc, any Polyline segment
/// (open or closed). Circle and full-circle-sweep Arc are refused — no single tangent-side
/// construction distinguishes a closed loop's "which side did you mean" (D-2026-08-24-g), the same
/// reasoning LENGTHEN/EXTEND already exclude Circle for. Every other kind refused with a stated
/// reason (REQ-201).
static bool FilletEligibility(const AppCommandState& st, const SelectedEntity& e, float pickX, float pickY,
                              int* outPolySeg, std::vector<std::string>& log) {
  using T = SelectedEntity::Type;
  constexpr float kTwoPi = 6.28318530717958647692f;
  *outPolySeg = -1;
  switch (e.type) {
  case T::LineSeg:
    return true;
  case T::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
      return false;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
    if (std::fabs(std::fabs(a.sweepRad) - kTwoPi) < 1e-4f) {
      log.push_back("FILLET — 1 full-circle arc ignored: fillet needs a curve with a definite tangent "
                    "side. Pick a line, non-full arc, or polyline segment.");
      return false;
    }
    return true;
  }
  case T::Polyline: {
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
      return false;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const int numVerts = v1 - v0;
    if (numVerts < 2)
      return false;
    const bool closed =
        static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
    const int numEdges = closed ? numVerts : (numVerts - 1);
    int bestSeg = 0;
    float bestD = -1.f;
    for (int e2 = 0; e2 < numEdges; ++e2) {
      const int viA = v0 + e2, viB = v0 + ((e2 + 1) % numVerts);
      const size_t a3 = static_cast<size_t>(viA), b3 = static_cast<size_t>(viB);
      float qx = 0.f, qy = 0.f;
      ClosestPointOnSegment(static_cast<float>(st.drawing.vertex(a3).x), static_cast<float>(st.drawing.vertex(a3).y), static_cast<float>(st.drawing.vertex(b3).x),
                            static_cast<float>(st.drawing.vertex(b3).y), pickX, pickY, &qx, &qy);
      const float d = (qx - pickX) * (qx - pickX) + (qy - pickY) * (qy - pickY);
      if (bestD < 0.f || d < bestD) {
        bestD = d;
        bestSeg = e2;
      }
    }
    *outPolySeg = bestSeg;
    return true;
  }
  case T::Circle:
    log.push_back("FILLET — 1 circle ignored: fillet needs a curve with a definite tangent side. Pick a "
                  "line, non-full arc, or polyline segment.");
    return false;
  case T::Spline:
    log.push_back("FILLET — spline entities are not supported; nothing changed.");
    return false;
  default:
    log.push_back("FILLET — 1 object ignored: only a line, arc, or polyline segment can be filleted.");
    return false;
  }
}

/// Builds the pure geometry \ref FilletCurve for a picked Line/Arc/Polyline-segment (\p polySeg
/// ignored for Line/Arc).
static bool BuildFilletCurveFromEntity(const AppCommandState& st, const SelectedEntity& e, int polySeg,
                                       FilletCurve* out) {
  using T = SelectedEntity::Type;
  if (e.type == T::LineSeg) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return false;
    out->isLine = true;
    out->ax = static_cast<float>(st.drawing.line(k).x0);
    out->ay = static_cast<float>(st.drawing.line(k).y0);
    out->az0 = static_cast<float>(st.drawing.line(k).z0);
    out->bx = static_cast<float>(st.drawing.line(k).x1);
    out->by = static_cast<float>(st.drawing.line(k).y1);
    out->az1 = static_cast<float>(st.drawing.line(k).z1);
    return true;
  }
  if (e.type == T::Arc) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
      return false;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
    out->isLine = false;
    out->cx = static_cast<float>(a.cx);
    out->cy = static_cast<float>(a.cy);
    out->r = static_cast<float>(a.r);
    return true;
  }
  if (e.type == T::Polyline) {
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size() || polySeg < 0)
      return false;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const int numVerts = v1 - v0;
    const int viA = v0 + polySeg, viB = v0 + ((polySeg + 1) % numVerts);
    const size_t a3 = static_cast<size_t>(viA), b3 = static_cast<size_t>(viB);
    if (b3 >= st.drawing.vertexCount())
      return false;
    out->isLine = true;
    out->ax = static_cast<float>(st.drawing.vertex(a3).x);
    out->ay = static_cast<float>(st.drawing.vertex(a3).y);
    out->bx = static_cast<float>(st.drawing.vertex(b3).x);
    out->by = static_cast<float>(st.drawing.vertex(b3).y);
    return true;
  }
  return false;
}

/// "Radius is too large" detection (D-2026-08-25-b, a real user-reported bug). Pick-INDEPENDENT by
/// design — an earlier version of this check compared the tangent-point-based near/far endpoint
/// against the PICK-based one, which sounded plausible but was a false-positive machine: any pick
/// past a line's own midpoint (completely ordinary FILLET usage — you often click nearer the far,
/// kept end) disagreed with a perfectly valid small-radius tangent point, which naturally sits near
/// the corner. The correct, pick-independent signature: `e`'s two ORIGINAL endpoints are compared
/// against `p0` (the plain radius-0 intersection of the two curves — the true corner), and whichever
/// is FARTHER from `p0` is that curve's own "far" end. A valid fillet always moves the NEAR end
/// (the one already close to the corner) to the tangent point; when the requested radius is too
/// large for the curve's own length, the tangent point ends up closer to the FAR end instead, so
/// FILLET would silently move the wrong end (or, for a short segment, barely touch the far end
/// while leaving the actually-picked corner completely untrimmed) — exactly what a real report
/// described ("radius 20... drew to the bottom endpoints instead of just not drawing"). Arc uses
/// its own start/end points the same way LENGTHEN/EXTEND already do. Polyline is not checked here —
/// Case B already restricts it to a segment's own free end (no meaningful "swap" to detect the same
/// way), and Case A's shared-vertex splice has no near/far selection to swap in the first place.
static bool FilletRadiusFitsCurve(const AppCommandState& st, const SelectedEntity& e, float p0x, float p0y,
                                  float tangentX, float tangentY) {
  using T = SelectedEntity::Type;
  if (e.type == T::LineSeg) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return true;  // let the mutation itself refuse for real on a bad index
    const float x0 = static_cast<float>(st.drawing.line(k).x0), y0 = static_cast<float>(st.drawing.line(k).y0);
    const float x1 = static_cast<float>(st.drawing.line(k).x1), y1 = static_cast<float>(st.drawing.line(k).y1);
    const bool nearIsFirst = NearerToFirstPoint(p0x, p0y, x0, y0, x1, y1);
    const float nearX = nearIsFirst ? x0 : x1, nearY = nearIsFirst ? y0 : y1;
    const float farX = nearIsFirst ? x1 : x0, farY = nearIsFirst ? y1 : y0;
    return FilletPointWithinSpan(nearX, nearY, farX, farY, tangentX, tangentY);
  }
  // Arc: not checked (returns true unconditionally) — the reported bug and its fix are both
  // Line-specific; an angular equivalent of FilletPointWithinSpan is a real gap, not silently
  // assumed safe (see TASK-102's own follow-up log for why it was deferred rather than rushed).
  return true;
}

/// Case B: the two picked curves are different entities (or a Polyline's own end segment against a
/// different entity). Trims/extends `e` (at `polySeg` if Polyline) to `tangentX,tangentY` by
/// converting it into LENGTHEN's own `newLength` currency and calling ApplyLengthenToLine/ToArc/
/// ToPolylineEnd UNCHANGED — REQ-103's own stated reuse chain, never re-derived.
static bool ApplyFilletTrimSingle(AppCommandState& st, const SelectedEntity& e, int polySeg, float tangentX,
                                  float tangentY, std::vector<std::string>& log) {
  using T = SelectedEntity::Type;
  if (e.type == T::LineSeg) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return false;
    const float x0 = static_cast<float>(st.drawing.line(k).x0), y0 = static_cast<float>(st.drawing.line(k).y0);
    const float x1 = static_cast<float>(st.drawing.line(k).x1), y1 = static_cast<float>(st.drawing.line(k).y1);
    // Which end moves is decided by nearness to the TANGENT POINT, not the pick — unlike LENGTHEN,
    // where the pick directly names the end to change, FILLET's pick only disambiguates which
    // corner/candidate solution to build (SolveFilletCenter); a pick anywhere along the KEPT
    // portion of the line (which for a typical corner is most of it) must still move the
    // corner-adjacent end, not whichever end the pick happens to be nearer to — a real bug this
    // fixed (a pick at a line's own midpoint tied under the pick-based rule and moved the wrong,
    // far end, refusing the trim outright; fillet-lines-basic.txt's Part B caught it).
    const bool nearFirst = NearerToFirstPoint(tangentX, tangentY, x0, y0, x1, y1);
    const float fixedX = nearFirst ? x1 : x0, fixedY = nearFirst ? y1 : y0;
    const float newLength = std::hypot(tangentX - fixedX, tangentY - fixedY);
    // pushUndo=false: HandleFilletViewportPick already pushed ONE "Fillet" snapshot covering both
    // curves' trims plus the arc — REQ-103's "one undo step per fillet" (a real bug this fixed,
    // caught by fillet-lines-basic.txt's UNDO/REDO round-trip: 3 pushes made 1 UNDO only partially
    // revert, TASK-102 task log).
    return ApplyLengthenToLine(st, e.index, nearFirst, newLength, log, false);
  }
  if (e.type == T::Arc) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
      return false;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
    const float sx = static_cast<float>(a.cx + a.r * std::cos(a.startRad));
    const float sy = static_cast<float>(a.cy + a.r * std::sin(a.startRad));
    const float ex = static_cast<float>(a.cx + a.r * std::cos(a.startRad + a.sweepRad));
    const float ey = static_cast<float>(a.cy + a.r * std::sin(a.startRad + a.sweepRad));
    const bool nearFirst = NearerToFirstPoint(tangentX, tangentY, sx, sy, ex, ey);  // see the Line branch's comment
    const float newLength =
        FilletArcTangentPointToNewLength(static_cast<float>(a.cx), static_cast<float>(a.cy), static_cast<float>(a.r), a.startRad, a.sweepRad, nearFirst, tangentX, tangentY);
    return ApplyLengthenToArc(st, e.index, nearFirst, newLength, log, false);
  }
  if (e.type == T::Polyline) {
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size() || polySeg < 0)
      return false;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const int numVerts = v1 - v0;
    const int numEdges = numVerts - 1;  // Case B never reaches a closed polyline — no free end
    const bool nearFirst = (polySeg == 0);
    if (!nearFirst && polySeg != numEdges - 1)
      return false;  // not an end segment — caller already refuses this before reaching here
    const int movingVi = nearFirst ? v0 : (v1 - 1);
    const int fixedVi = nearFirst ? (v0 + 1) : (v1 - 2);
    const size_t mIdx = static_cast<size_t>(movingVi), fIdx = static_cast<size_t>(fixedVi);
    if (mIdx >= st.drawing.vertexCount() || fIdx >= st.drawing.vertexCount())
      return false;
    const float fx = static_cast<float>(st.drawing.vertex(fIdx).x), fy = static_cast<float>(st.drawing.vertex(fIdx).y);
    const float mx = static_cast<float>(st.drawing.vertex(mIdx).x), my = static_cast<float>(st.drawing.vertex(mIdx).y);
    const float oldSegLen = std::hypot(mx - fx, my - fy);
    const float newSegLen = std::hypot(tangentX - fx, tangentY - fy);
    const float curTotal = PolylineOpenLengthOf(st, pi);
    const float newLength = curTotal - oldSegLen + newSegLen;
    return ApplyLengthenToPolylineEnd(st, pi, nearFirst, newLength, log, false);  // see the Line branch's comment
  }
  return false;
}

/// Case A: the two picked curves are adjacent segments (sharing exactly one vertex) of the SAME
/// polyline — the classic "round this corner" case. Splices the shared vertex into the two tangent
/// points (radius > ~0) or moves it to the single intersection point (radius ~0, vertex count
/// unchanged), via \ref ReplacePolylineVerts, and inserts a fresh Arc entity for the corner
/// (radius > ~0 only).
static bool ApplyFilletPolylineCorner(AppCommandState& st, int pi, int edgeA, int edgeB, float radius,
                                      float pick1X, float pick1Y, float pick2X, float pick2Y,
                                      std::vector<std::string>& log) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
    return false;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  const int numVerts = v1 - v0;
  if (numVerts < 2)
    return false;
  const bool closed =
      static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
  const int numEdges = closed ? numVerts : (numVerts - 1);
  if (edgeA < 0 || edgeA >= numEdges || edgeB < 0 || edgeB >= numEdges || edgeA == edgeB)
    return false;

  auto edgeVerts = [&](int e, int* viA, int* viB) {
    *viA = v0 + e;
    *viB = v0 + ((e + 1) % numVerts);
  };
  int aVi0 = 0, aVi1 = 0, bVi0 = 0, bVi1 = 0;
  edgeVerts(edgeA, &aVi0, &aVi1);
  edgeVerts(edgeB, &bVi0, &bVi1);
  int sharedVi = -1, otherAVi = -1;
  if (aVi0 == bVi0 || aVi0 == bVi1) {
    sharedVi = aVi0;
    otherAVi = aVi1;
  } else if (aVi1 == bVi0 || aVi1 == bVi1) {
    sharedVi = aVi1;
    otherAVi = aVi0;
  }
  if (sharedVi < 0) {
    log.push_back("FILLET — those two polyline segments are not adjacent; refused.");
    return false;
  }
  const int otherBVi = (bVi0 == sharedVi) ? bVi1 : bVi0;

  auto readVert = [&](int vi, float* x, float* y) {
    *x = static_cast<float>(st.drawing.vertex(vi).x);
    *y = static_cast<float>(st.drawing.vertex(vi).y);
  };
  auto vertZ = [&](int vi) { return st.drawing.vertex(vi).z; };
  // A tangent point lies ON one of the two segments meeting at the corner, so its elevation is the
  // interpolation between that segment's ends (issue 01). Flat polylines - the ordinary case - get
  // the shared elevation from this either way; a sloped one now gets the right answer instead of 0.
  auto zOnSegTo = [&](int fromVi, int toVi, float px, float py) {
    float fx = 0.f, fy = 0.f, tx = 0.f, ty = 0.f;
    readVert(fromVi, &fx, &fy);
    readVert(toVi, &tx, &ty);
    const float len = std::hypot(tx - fx, ty - fy);
    const float t = (len > 1e-9f) ? (std::hypot(px - fx, py - fy) / len) : 0.f;
    return vertZ(fromVi) + (vertZ(toVi) - vertZ(fromVi)) * t;
  };
  float sharedX = 0.f, sharedY = 0.f, otherAX = 0.f, otherAY = 0.f, otherBX = 0.f, otherBY = 0.f;
  readVert(sharedVi, &sharedX, &sharedY);
  readVert(otherAVi, &otherAX, &otherAY);
  readVert(otherBVi, &otherBX, &otherBY);

  FilletCurve curveA{};
  curveA.isLine = true;
  curveA.ax = otherAX;
  curveA.ay = otherAY;
  curveA.bx = sharedX;
  curveA.by = sharedY;
  FilletCurve curveB{};
  curveB.isLine = true;
  curveB.ax = sharedX;
  curveB.ay = sharedY;
  curveB.bx = otherBX;
  curveB.by = otherBY;

  float cx = 0.f, cy = 0.f;
  if (!SolveFilletCenter(curveA, curveB, radius, pick1X, pick1Y, pick2X, pick2Y, &cx, &cy)) {
    log.push_back("FILLET — no valid tangent arc exists for that radius at that corner; refused.");
    return false;
  }
  float tAx = 0.f, tAy = 0.f, tBx = 0.f, tBy = 0.f;
  FilletTangentPointOnLine(curveA, cx, cy, &tAx, &tAy);
  FilletTangentPointOnLine(curveB, cx, cy, &tBx, &tBy);

  const int sharedLocal = sharedVi - v0;
  const int otherALocal = otherAVi - v0;
  const bool aIsIncoming = (otherALocal == (sharedLocal - 1 + numVerts) % numVerts);
  const float inTx = aIsIncoming ? tAx : tBx, inTy = aIsIncoming ? tAy : tBy;
  const float outTx = aIsIncoming ? tBx : tAx, outTy = aIsIncoming ? tBy : tAy;
  const bool radiusIsZero = radius < 1e-4f;

  const int inNeighbourVi = aIsIncoming ? otherAVi : otherBVi;
  const int outNeighbourVi = aIsIncoming ? otherBVi : otherAVi;
  const float inTz = static_cast<float>(zOnSegTo(sharedVi, inNeighbourVi, inTx, inTy));
  const float outTz = static_cast<float>(zOnSegTo(sharedVi, outNeighbourVi, outTx, outTy));
  std::vector<PolyVert> newXY;
  newXY.reserve(static_cast<size_t>(numVerts) + 1);
  for (int vi = v0; vi < v1; ++vi) {
    if (vi - v0 == sharedLocal) {
      if (radiusIsZero) {
        newXY.push_back({cx, cy, static_cast<float>(vertZ(sharedVi))});
      } else {
        newXY.push_back({inTx, inTy, inTz});
        newXY.push_back({outTx, outTy, outTz});
      }
    } else {
      float x = 0.f, y = 0.f;
      readVert(vi, &x, &y);
      newXY.push_back({x, y, static_cast<float>(vertZ(vi))});
    }
  }

  PushUndoSnapshot(st, "Fillet");
  ReplacePolylineVerts(st, pi, newXY);
  if (!radiusIsZero) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float thetaIn = std::atan2(inTy - cy, inTx - cx);
    const float thetaOut = std::atan2(outTy - cy, outTx - cx);
    float sweep = FilletCcwAngleDelta(thetaIn, thetaOut);
    if (sweep > kPi)
      sweep -= kTwoPi;
    CadArc arc{};
    arc.cx = cx;
    arc.cy = cy;
    arc.r = radius;
    arc.startRad = thetaIn;
    arc.sweepRad = sweep;
    // CadArc holds ONE elevation - it stays parallel to XY, and a tilted arc would need a plane
    // normal no accepted requirement asks for (REQ-057 / ADR-025). The two tangent points are equal
    // on a level polyline, which is the ordinary case and is then exact; where they differ the
    // midpoint is the closest a single-elevation arc can come. Before this it was 0 regardless.
    arc.z = 0.5f * (inTz + outTz);
    st.drawing.userArcs.push_back(arc);
    st.drawing.userArcAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);
  log.push_back(radiusIsZero ? "FILLET — polyline corner trimmed to a point (radius 0)."
                            : "FILLET — polyline corner filleted.");
  return true;
}

/// issue #373: the 3D fillet solve's own "straight curve" — a plain Line, or a Polyline's own free
/// end segment (Case B already restricts a Polyline to that, same as the 2D path) — reduced to its
/// two WORLD-space 3D endpoints, plus enough to trim it back afterward. Kept separate from
/// \ref FilletCurve, which stays the plane-local 2D shape both this and the legacy 2D solve share.
struct Fillet3DCurve {
  ray3d::Vec3 p0, p1;
  bool isPolyline = false;
  int viA = -1, viB = -1;  // Polyline only: p0's and p1's own vertex index (LOCAL store, not *3)
};

/// Reads a Line or Polyline-end-segment's own two endpoints in world XYZ. False on a bad index or an
/// entity type the 3D solve does not handle (Arc/Circle — unchanged, still 2D-only via the legacy
/// path, matching REQ-201's "refuse rather than guess" for what is not explicitly supported).
static bool ReadFillet3DCurve(const AppCommandState& st, const SelectedEntity& e, int polySeg, Fillet3DCurve* out) {
  using T = SelectedEntity::Type;
  if (e.type == T::LineSeg) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return false;
    out->p0 = {st.drawing.line(k).x0, st.drawing.line(k).y0, st.drawing.line(k).z0};
    out->p1 = {st.drawing.line(k).x1, st.drawing.line(k).y1, st.drawing.line(k).z1};
    out->isPolyline = false;
    return true;
  }
  if (e.type == T::Polyline) {
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size() || polySeg < 0)
      return false;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const int numVerts = v1 - v0;
    const int viA = v0 + polySeg, viB = v0 + ((polySeg + 1) % numVerts);
    const size_t a3 = static_cast<size_t>(viA), b3 = static_cast<size_t>(viB);
    if (b3 >= st.drawing.vertexCount())
      return false;
    out->p0 = {st.drawing.vertex(a3).x, st.drawing.vertex(a3).y, st.drawing.vertex(a3).z};
    out->p1 = {st.drawing.vertex(b3).x, st.drawing.vertex(b3).y, st.drawing.vertex(b3).z};
    out->isPolyline = true;
    out->viA = viA;
    out->viB = viB;
    return true;
  }
  return false;
}

/// issue #373: FILLET between two Line/Polyline-end-segment curves whose four endpoints are not all
/// flat at world Z=0. Two curves that share SOME plane (even a tilted/vertical one) get a real
/// tangent arc, solved by reusing the exact same 2D machinery (\ref SolveFilletCenter, \ref
/// FilletTangentPointOnLine, \ref FilletParallelSemicircle) every OTHER fillet already goes through
/// — just fed that plane's own projected 2D coordinates instead of world XY — then mapped back to
/// world XYZ. Two curves with no common plane at all (true skew lines) are refused by name (REQ-201):
/// there is no single correct arc for them, and guessing one silently would be worse than refusing.
static bool HandleFillet3DLineLine(AppCommandState& st, const SelectedEntity& e1, int polySeg1,
                                   const SelectedEntity& e2, int polySeg2, float pick1X, float pick1Y,
                                   const ray3d::Ray& pick1Ray, float pick2X, float pick2Y,
                                   const ray3d::Ray* pick2Ray, std::vector<std::string>& log) {
  Fillet3DCurve curve1, curve2;
  if (!ReadFillet3DCurve(st, e1, polySeg1, &curve1) || !ReadFillet3DCurve(st, e2, polySeg2, &curve2))
    return false;
  const ray3d::Vec3 a0 = curve1.p0, a1 = curve1.p1, b0 = curve2.p0, b1 = curve2.p1;

  ucs::Ucs frame{};
  ray3d::Vec3 offPlanePt = b1;
  bool haveFrame = ucs::FromThreePoints(a0, a1, b0, &frame);
  if (!haveFrame) {
    haveFrame = ucs::FromThreePoints(a0, a1, b1, &frame);
    offPlanePt = b0;
  }
  if (!haveFrame) {
    log.push_back("FILLET — the two lines are degenerate; refused.");
    return true;
  }

  double tol = 1e-3;
  {
    double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
    if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
      tol = std::max(1e-5, 1e-4 * std::max(mxX - mnX, mxY - mnY));
  }
  if (std::fabs(ucs::SignedDistanceToPlane(frame, offPlanePt)) > tol) {
    log.push_back("FILLET — the two lines do not share a plane (non-coplanar 3D lines are not "
                  "supported); refused.");
    return true;
  }

  const ucs::Point2D pa0 = ucs::WorldToPlane(frame, a0);
  const ucs::Point2D pa1 = ucs::WorldToPlane(frame, a1);
  const ucs::Point2D pb0 = ucs::WorldToPlane(frame, b0);
  const ucs::Point2D pb1 = ucs::WorldToPlane(frame, b1);
  FilletCurve c1{}, c2{};
  c1.isLine = true;
  c1.ax = static_cast<float>(pa0.x);
  c1.ay = static_cast<float>(pa0.y);
  c1.bx = static_cast<float>(pa1.x);
  c1.by = static_cast<float>(pa1.y);
  c2.isLine = true;
  c2.ax = static_cast<float>(pb0.x);
  c2.ay = static_cast<float>(pb0.y);
  c2.bx = static_cast<float>(pb1.x);
  c2.by = static_cast<float>(pb1.y);

  // The 2D pick points only disambiguate which candidate solution to use; the nearest point on
  // each line (in its own WORLD XY projection) is guaranteed to lie on that line, hence exactly on
  // the shared plane — project that instead of the raw 2D pick.
  auto nearestOnLine = [](const ray3d::Vec3& p0, const ray3d::Vec3& p1, float px, float py) -> ray3d::Vec3 {
    const double vx = p1.x - p0.x, vy = p1.y - p0.y;
    const double len2 = vx * vx + vy * vy;
    if (len2 < 1e-18)
      return p0;
    const double t = ((px - p0.x) * vx + (py - p0.y) * vy) / len2;
    return {p0.x + t * vx, p0.y + t * vy, p0.z + t * (p1.z - p0.z)};
  };
  // issue #373: a pick projected exactly ONTO its own line (nearestOnLine) throws away the one
  // thing that tells two mathematically valid tangent arcs apart — which side of the line the user
  // actually clicked on. A point exactly on the line is equidistant from both perpendicular offset
  // candidates, so SolveFilletCenter's nearest-to-pick tie-break degenerates to iteration order (a
  // real report: a small-radius fillet rounding the OUTSIDE of a corner instead of the inside).
  // Whenever the click's own camera ray is available (any orbited/non-plan view), intersecting it
  // with THIS fillet's own plane gives the exact point the user pointed at, off-line component
  // included — nearestOnLine stays the fallback for a flat plan-view pick (where wx,wy already IS
  // the true in-plane point) or the rare case a ray misses the plane edge-on.
  const ray3d::Plane solvePlane{frame.origin, frame.zAxis};
  ray3d::Vec3 pickPt1{};
  const bool haveRayPick1 = pick1Ray.valid() && ray3d::RayPlaneIntersect(pick1Ray, solvePlane, &pickPt1);
  ray3d::Vec3 pickPt2{};
  const bool haveRayPick2 =
      pick2Ray && pick2Ray->valid() && ray3d::RayPlaneIntersect(*pick2Ray, solvePlane, &pickPt2);
  const ucs::Point2D pp1 =
      ucs::WorldToPlane(frame, haveRayPick1 ? pickPt1 : nearestOnLine(a0, a1, pick1X, pick1Y));
  const ucs::Point2D pp2 =
      ucs::WorldToPlane(frame, haveRayPick2 ? pickPt2 : nearestOnLine(b0, b1, pick2X, pick2Y));

  // issue #373: trims either curve kind back to a plane-local 2D tangent point. A Line moves
  // whichever of its OWN two endpoints sits nearer the tangent point (same rule the 2D path uses);
  // a Polyline moves its already-known free-end vertex (\ref ReadFillet3DCurve) — there is no
  // "nearer of two ends" choice there, since the other end is a shared interior vertex Case B never
  // lets this touch.
  auto trimCurveTo3D = [&](const SelectedEntity& e, const Fillet3DCurve& curve, float tx2d, float ty2d) -> bool {
    const ray3d::Vec3 t3d = ucs::PlaneToWorld(frame, ucs::Point2D{tx2d, ty2d});
    if (curve.isPolyline) {
      if (curve.viA < 0 || curve.viB < 0)
        return false;
      // Which vertex moves is decided by nearness to the TANGENT POINT, the same rule the Line
      // branch below uses (and for the same reason: a single-segment open polyline's one edge is
      // simultaneously its own "first" and "last", so polySeg alone cannot say which end is free —
      // only the corner this fillet is actually rounding can).
      const ucs::Point2D pp0 = ucs::WorldToPlane(frame, curve.p0);
      const ucs::Point2D pp1b = ucs::WorldToPlane(frame, curve.p1);
      const float d0 = (tx2d - static_cast<float>(pp0.x)) * (tx2d - static_cast<float>(pp0.x)) +
                       (ty2d - static_cast<float>(pp0.y)) * (ty2d - static_cast<float>(pp0.y));
      const float d1 = (tx2d - static_cast<float>(pp1b.x)) * (tx2d - static_cast<float>(pp1b.x)) +
                       (ty2d - static_cast<float>(pp1b.y)) * (ty2d - static_cast<float>(pp1b.y));
      const bool nearFirst = d0 <= d1;
      const int movingVi = nearFirst ? curve.viA : curve.viB;
      const size_t k = static_cast<size_t>(movingVi);
      if (k >= st.drawing.vertexCount())
        return false;
      const ray3d::Vec3 fixed = nearFirst ? curve.p1 : curve.p0;
      const float newLen = static_cast<float>(ray3d::Length(ray3d::Sub(t3d, fixed)));
      if (!(newLen > 1e-6f)) {
        log.push_back("FILLET — that would collapse a polyline segment to zero length; refused.");
        return false;
      }
      st.drawing.vertexComponent(k, CadVertexComponent::X) = static_cast<float>(t3d.x);
      st.drawing.vertexComponent(k, CadVertexComponent::Y) = static_cast<float>(t3d.y);
      st.drawing.vertexComponent(k, CadVertexComponent::Z) = static_cast<float>(t3d.z);
      return true;
    }
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return false;
    const ucs::Point2D pp0 = ucs::WorldToPlane(frame, curve.p0);
    const ucs::Point2D pp1b = ucs::WorldToPlane(frame, curve.p1);
    const float d0 = (tx2d - static_cast<float>(pp0.x)) * (tx2d - static_cast<float>(pp0.x)) +
                     (ty2d - static_cast<float>(pp0.y)) * (ty2d - static_cast<float>(pp0.y));
    const float d1 = (tx2d - static_cast<float>(pp1b.x)) * (tx2d - static_cast<float>(pp1b.x)) +
                     (ty2d - static_cast<float>(pp1b.y)) * (ty2d - static_cast<float>(pp1b.y));
    const bool nearFirst = d0 <= d1;
    const float newLen =
        static_cast<float>(ray3d::Length(ray3d::Sub(t3d, nearFirst ? curve.p1 : curve.p0)));
    if (!(newLen > 1e-6f)) {
      log.push_back("FILLET — that would collapse a line to zero length; refused.");
      return false;
    }
    if (nearFirst) {
      st.drawing.lineComponent(k, CadLineComponent::X0) = static_cast<float>(t3d.x);
      st.drawing.lineComponent(k, CadLineComponent::Y0) = static_cast<float>(t3d.y);
      st.drawing.lineComponent(k, CadLineComponent::Z0) = static_cast<float>(t3d.z);
    } else {
      st.drawing.lineComponent(k, CadLineComponent::X1) = static_cast<float>(t3d.x);
      st.drawing.lineComponent(k, CadLineComponent::Y1) = static_cast<float>(t3d.y);
      st.drawing.lineComponent(k, CadLineComponent::Z1) = static_cast<float>(t3d.z);
    }
    return true;
  };

  // Builds the ARC — but its startRad/sweepRad must be measured in the arc's own CANONICAL frame,
  // `ucs::FromNormal(centre, normal)` (REQ-312, CadEntities.hpp's own CurvePlane), not in whatever
  // ad-hoc frame this function's own 2D solve happened to use. FromThreePoints' X axis runs along
  // curve1's own direction — a choice made for the solve's convenience — while FromNormal's X axis
  // is the deterministic Arbitrary Axis Algorithm from the normal alone; the two agree only by
  // accident (a real bug this fixed, caught by a non-90-degree corner: the semicircle special case's
  // exact 180-degree sweep hid it, since a reflection through the centre lands on the same point in
  // ANY consistently-handed frame sharing that centre and normal, but a partial sweep does not).
  auto addArc3D = [&](const ray3d::Vec3& c3d, const ray3d::Vec3& startWorld, const ray3d::Vec3& endWorld,
                      float r) -> bool {
    ucs::Ucs canon{};
    if (!ucs::FromNormal(c3d, frame.zAxis, &canon)) {
      log.push_back("FILLET — could not orient the fillet arc's plane; refused.");
      return false;
    }
    const ucs::Point2D sLocal = ucs::WorldToPlane(canon, startWorld);
    const ucs::Point2D eLocal = ucs::WorldToPlane(canon, endWorld);
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float thetaA = std::atan2(static_cast<float>(sLocal.y), static_cast<float>(sLocal.x));
    const float thetaB = std::atan2(static_cast<float>(eLocal.y), static_cast<float>(eLocal.x));
    float sweep = FilletCcwAngleDelta(thetaA, thetaB);
    if (sweep > kPi)
      sweep -= kTwoPi;
    CadArc arc{};
    arc.cx = static_cast<float>(c3d.x);
    arc.cy = static_cast<float>(c3d.y);
    arc.z = static_cast<float>(c3d.z);
    arc.r = r;
    arc.startRad = thetaA;
    arc.sweepRad = sweep;
    arc.nx = static_cast<float>(frame.zAxis.x);
    arc.ny = static_cast<float>(frame.zAxis.y);
    arc.nz = static_cast<float>(frame.zAxis.z);
    st.drawing.userArcs.push_back(arc);
    st.drawing.userArcAttrs.push_back(MakeNewEntityAttrs(st));
    return true;
  };

  if (FilletLinesAreParallel(c1.ax, c1.ay, c1.bx, c1.by, c2.ax, c2.ay, c2.bx, c2.by)) {
    float anchorX = 0.f, anchorY = 0.f, projX = 0.f, projY = 0.f;
    FilletParallelSemicircle(c1.ax, c1.ay, c1.bx, c1.by, c2.ax, c2.ay, c2.bx, c2.by, static_cast<float>(pp1.x), static_cast<float>(pp1.y), &anchorX,
                             &anchorY, &projX, &projY);
    const float semiR = 0.5f * std::hypot(projX - anchorX, projY - anchorY);
    if (semiR < 1e-4f) {
      log.push_back("FILLET — the two lines coincide; refused.");
      return true;
    }
    PushUndoSnapshot(st, "Fillet");
    const bool ok = !st.cornerTrimMode || trimCurveTo3D(e2, curve2, projX, projY);
    if (ok) {
      const float cx = 0.5f * (anchorX + projX), cy = 0.5f * (anchorY + projY);
      const ray3d::Vec3 c3d = ucs::PlaneToWorld(frame, ucs::Point2D{cx, cy});
      const ray3d::Vec3 startWorld = ucs::PlaneToWorld(frame, ucs::Point2D{anchorX, anchorY});
      const ray3d::Vec3 endWorld = ucs::PlaneToWorld(frame, ucs::Point2D{projX, projY});
      if (addArc3D(c3d, startWorld, endWorld, semiR)) {
        BumpCadGpuCache(st);
        log.push_back("FILLET — parallel 3D lines connected by a semicircle (radius set by the gap "
                      "between them, not the current FILLET radius).");
      }
    }
    return true;
  }

  float cx = 0.f, cy = 0.f;
  if (!SolveFilletCenter(c1, c2, st.filletRadius, static_cast<float>(pp1.x), static_cast<float>(pp1.y), static_cast<float>(pp2.x), static_cast<float>(pp2.y), &cx, &cy)) {
    log.push_back("FILLET — no valid tangent arc exists for that radius; refused.");
    return true;
  }
  float t1x = 0.f, t1y = 0.f, t2x = 0.f, t2y = 0.f;
  FilletTangentPointOnLine(c1, cx, cy, &t1x, &t1y);
  FilletTangentPointOnLine(c2, cx, cy, &t2x, &t2y);

  // "Radius too large" check (D-2026-08-25-b), the 3D counterpart of the 2D path's own
  // FilletRadiusFitsCurve: the plain radius-0 intersection (p0) is the TRUE corner, and a valid
  // tangent point must stay within each line's own span measured from whichever original endpoint
  // sits nearer p0 out to the far one — going past the far endpoint means the requested radius needs
  // more line than either curve actually has. Checked in the same plane-local 2D coordinates
  // everything else in this function already uses, so the existing (curve-agnostic) helpers apply
  // unchanged.
  float p0x = 0.f, p0y = 0.f;
  const bool haveP0 = SolveFilletCenter(c1, c2, 0.f, static_cast<float>(pp1.x), static_cast<float>(pp1.y), static_cast<float>(pp2.x), static_cast<float>(pp2.y), &p0x, &p0y);
  if (haveP0) {
    const bool near1First = NearerToFirstPoint(p0x, p0y, c1.ax, c1.ay, c1.bx, c1.by);
    const bool near2First = NearerToFirstPoint(p0x, p0y, c2.ax, c2.ay, c2.bx, c2.by);
    const bool fits1 = FilletPointWithinSpan(near1First ? c1.ax : c1.bx, near1First ? c1.ay : c1.by,
                                             near1First ? c1.bx : c1.ax, near1First ? c1.by : c1.ay, t1x, t1y);
    const bool fits2 = FilletPointWithinSpan(near2First ? c2.ax : c2.bx, near2First ? c2.ay : c2.by,
                                             near2First ? c2.bx : c2.ax, near2First ? c2.by : c2.ay, t2x, t2y);
    if (!fits1 || !fits2) {
      log.push_back("FILLET — radius " + std::to_string(st.filletRadius) +
                    " is too large for the selected objects; refused.");
      return true;
    }
  }

  PushUndoSnapshot(st, "Fillet");
  bool ok1 = true, ok2 = true;
  if (st.cornerTrimMode) {
    ok1 = trimCurveTo3D(e1, curve1, t1x, t1y);
    ok2 = trimCurveTo3D(e2, curve2, t2x, t2y);
  }
  if (ok1 && ok2) {
    const bool radiusIsZero = st.filletRadius < 1e-4f;
    bool arcOk = true;
    if (!radiusIsZero) {
      const ray3d::Vec3 c3d = ucs::PlaneToWorld(frame, ucs::Point2D{cx, cy});
      const ray3d::Vec3 startWorld = ucs::PlaneToWorld(frame, ucs::Point2D{t1x, t1y});
      const ray3d::Vec3 endWorld = ucs::PlaneToWorld(frame, ucs::Point2D{t2x, t2y});
      arcOk = addArc3D(c3d, startWorld, endWorld, st.filletRadius);
    }
    if (arcOk) {
      BumpCadGpuCache(st);
      log.push_back(radiusIsZero ? "FILLET — corner trimmed to a point (radius 0)."
                                 : "FILLET — 3D corner filleted.");
    }
  }
  return true;
}

static std::string FilletPromptSuffix(const AppCommandState& st) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "<R=%.3f, %s>", static_cast<double>(st.filletRadius),
               st.cornerTrimMode ? "Trim" : "No trim");
  return buf;
}

void StartFilletCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetPaperModifyDrafts(st);
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {
    stepengine::EndSession(st);
    st.paperFilletPhase = 1;
    st.paperFilletFirstEntity = PaperEntityRef{};
    st.paperFilletFirstPolySeg = -1;
    log.push_back("FILLET — select first object " + FilletPromptSuffix(st) + ". ESC cancels.");
    return;
  }
  ClearPendingViewportZoom(st);
  stepengine::BeginSession(st, std::make_unique<stepengine::FilletSession>(false), true);
  log.push_back("FILLET — select first object or [Radius/Trim] " + FilletPromptSuffix(st) + ". ESC cancels.");
}

void HandleFilletViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log,
                              const ray3d::Ray* pickRay) {
  auto* sess = st.sessionAs<stepengine::FilletSession>();
  if (!sess)
    return;
  using FP = stepengine::FilletPhase;
  if (sess->solidAwaitingRadius) {
    log.push_back("FILLET — type a fillet radius for the selected edge(s), or ESC to cancel.");
    return;
  }
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2, pickRay)) {
    log.push_back("FILLET — no object at pick.");
    return;
  }
  int polySeg = -1;
  if (!FilletEligibility(st, hit, wx, wy, &polySeg, log))
    return;

  if (sess->phase == FP::WaitFirstEntity) {
    sess->firstEntity = hit;
    sess->firstPolySeg = polySeg;
    sess->firstPickX = wx;
    sess->firstPickY = wy;
    sess->firstPickRay = pickRay ? *pickRay : ray3d::Ray{};
    sess->phase = FP::WaitSecondEntity;
    log.push_back("FILLET — select second object:");
    return;
  }

  const bool sameEntity = (hit.type == sess->firstEntity.type && hit.index == sess->firstEntity.index);
  if (sameEntity && hit.type != SelectedEntity::Type::Polyline) {
    log.push_back("FILLET — select two different objects.");
    return;
  }
  if (sameEntity && polySeg == sess->firstPolySeg) {
    log.push_back("FILLET — select two different segments.");
    return;
  }

  if (sameEntity) {
    ApplyFilletPolylineCorner(st, hit.index, sess->firstPolySeg, polySeg, st.filletRadius,
                              sess->firstPickX, sess->firstPickY, wx, wy, log);
  } else {
    auto polylineIsEndSegmentOnly = [&](const SelectedEntity& e, int seg) -> bool {
      if (e.type != SelectedEntity::Type::Polyline)
        return true;
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        return false;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      const bool closed =
          static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
      if (closed) {
        log.push_back("FILLET — a closed polyline has no free end; only an adjacent segment of the SAME "
                      "polyline can be filleted against it.");
        return false;
      }
      const int numEdges = (v1 - v0) - 1;
      if (seg != 0 && seg != numEdges - 1) {
        log.push_back("FILLET — only a polyline's own first or last segment can be filleted against a "
                      "different object; an interior segment shares its vertex with a neighbor that "
                      "would be silently disturbed.");
        return false;
      }
      return true;
    };
    if (!polylineIsEndSegmentOnly(sess->firstEntity, sess->firstPolySeg) ||
        !polylineIsEndSegmentOnly(hit, polySeg)) {
      return;
    }

    auto isFilletStraightType = [](SelectedEntity::Type t) {
      return t == SelectedEntity::Type::LineSeg || t == SelectedEntity::Type::Polyline;
    };
    if (isFilletStraightType(sess->firstEntity.type) && isFilletStraightType(hit.type)) {
      Fillet3DCurve probe1, probe2;
      const bool haveCurves = ReadFillet3DCurve(st, sess->firstEntity, sess->firstPolySeg, &probe1) &&
                              ReadFillet3DCurve(st, hit, polySeg, &probe2);
      auto flatZero = [](const ray3d::Vec3& p0, const ray3d::Vec3& p1) {
        return p0.z == 0.0 && p1.z == 0.0;
      };
      if (haveCurves &&
          (!flatZero(probe1.p0, probe1.p1) || !flatZero(probe2.p0, probe2.p1))) {
        HandleFillet3DLineLine(st, sess->firstEntity, sess->firstPolySeg, hit, polySeg,
                               sess->firstPickX, sess->firstPickY, sess->firstPickRay, wx, wy,
                               pickRay, log);
        sess->phase = FP::WaitFirstEntity;
        sess->firstEntity = SelectedEntity{};
        sess->firstPolySeg = -1;
        log.push_back("FILLET — select first object or [Radius/Trim] " + FilletPromptSuffix(st) + ". ESC cancels.");
        return;
      }
    }

    FilletCurve c1{}, c2{};
    if (!BuildFilletCurveFromEntity(st, sess->firstEntity, sess->firstPolySeg, &c1) ||
        !BuildFilletCurveFromEntity(st, hit, polySeg, &c2)) {
      log.push_back("FILLET — could not resolve the picked geometry; refused.");
      return;
    }

    if (c1.isLine && c2.isLine &&
        FilletLinesAreParallel(c1.ax, c1.ay, c1.bx, c1.by, c2.ax, c2.ay, c2.bx, c2.by)) {
      float anchorX = 0.f, anchorY = 0.f, projX = 0.f, projY = 0.f;
      FilletParallelSemicircle(c1.ax, c1.ay, c1.bx, c1.by, c2.ax, c2.ay, c2.bx, c2.by, sess->firstPickX,
                               sess->firstPickY, &anchorX, &anchorY, &projX, &projY);
      const float semiR = 0.5f * std::hypot(projX - anchorX, projY - anchorY);
      if (semiR < 1e-4f) {
        log.push_back("FILLET — the two lines coincide; refused.");
      } else {
        PushUndoSnapshot(st, "Fillet");
        const bool ok = !st.cornerTrimMode || ApplyFilletTrimSingle(st, hit, polySeg, projX, projY, log);
        if (ok) {
          const float cx = 0.5f * (anchorX + projX), cy = 0.5f * (anchorY + projY);
          CadArc arc{};
          arc.cx = cx;
          arc.cy = cy;
          arc.r = semiR;
          arc.startRad = std::atan2(anchorY - cy, anchorX - cx);
          arc.sweepRad = 3.14159265358979323846f;
          {
            BreakPoint ba{}, bb{};
            const float za = ClosestPointOnEntity(st, sess->firstEntity, anchorX, anchorY, &ba) ? ba.z : 0.f;
            const float zb = ClosestPointOnEntity(st, hit, projX, projY, &bb) ? bb.z : 0.f;
            arc.z = 0.5f * (za + zb);
          }
          st.drawing.userArcs.push_back(arc);
          st.drawing.userArcAttrs.push_back(MakeNewEntityAttrs(st));
          BumpCadGpuCache(st);
          log.push_back("FILLET — parallel lines connected by a semicircle (radius set by the gap "
                        "between them, not the current FILLET radius).");
        }
      }
    } else {
      float cx = 0.f, cy = 0.f;
      if (!SolveFilletCenter(c1, c2, st.filletRadius, sess->firstPickX, sess->firstPickY, wx, wy, &cx,
                             &cy)) {
        log.push_back("FILLET — no valid tangent arc exists for that radius; refused.");
      } else {
        float t1x = 0.f, t1y = 0.f, t2x = 0.f, t2y = 0.f;
        if (c1.isLine)
          FilletTangentPointOnLine(c1, cx, cy, &t1x, &t1y);
        else
          FilletTangentPointOnCircle(c1, cx, cy, &t1x, &t1y);
        if (c2.isLine)
          FilletTangentPointOnLine(c2, cx, cy, &t2x, &t2y);
        else
          FilletTangentPointOnCircle(c2, cx, cy, &t2x, &t2y);

        float p0x = 0.f, p0y = 0.f;
        const bool haveP0 = SolveFilletCenter(c1, c2, 0.f, sess->firstPickX, sess->firstPickY, wx, wy, &p0x,
                                              &p0y);
        if (haveP0 &&
            (!FilletRadiusFitsCurve(st, sess->firstEntity, p0x, p0y, t1x, t1y) ||
             !FilletRadiusFitsCurve(st, hit, p0x, p0y, t2x, t2y))) {
          log.push_back("FILLET — radius " + std::to_string(st.filletRadius) +
                        " is too large for the selected objects; refused.");
        } else {
          PushUndoSnapshot(st, "Fillet");
          bool ok1 = true, ok2 = true;
          if (st.cornerTrimMode) {
            ok1 = ApplyFilletTrimSingle(st, sess->firstEntity, sess->firstPolySeg, t1x, t1y, log);
            ok2 = ApplyFilletTrimSingle(st, hit, polySeg, t2x, t2y, log);
          }
          if (ok1 && ok2) {
            const bool radiusIsZero = st.filletRadius < 1e-4f;
            if (!radiusIsZero) {
              constexpr float kPi = 3.14159265358979323846f;
              constexpr float kTwoPi = 6.28318530717958647692f;
              const float thetaA = std::atan2(t1y - cy, t1x - cx);
              const float thetaB = std::atan2(t2y - cy, t2x - cx);
              float sweep = FilletCcwAngleDelta(thetaA, thetaB);
              if (sweep > kPi)
                sweep -= kTwoPi;
              CadArc arc{};
              arc.cx = cx;
              arc.cy = cy;
              arc.r = st.filletRadius;
              arc.startRad = thetaA;
              arc.sweepRad = sweep;
              {
                BreakPoint bt1{}, bt2{};
                const float za = ClosestPointOnEntity(st, sess->firstEntity, t1x, t1y, &bt1) ? bt1.z : 0.f;
                const float zb = ClosestPointOnEntity(st, hit, t2x, t2y, &bt2) ? bt2.z : 0.f;
                arc.z = 0.5f * (za + zb);
              }
              st.drawing.userArcs.push_back(arc);
              st.drawing.userArcAttrs.push_back(MakeNewEntityAttrs(st));
            }
            BumpCadGpuCache(st);
            log.push_back(radiusIsZero ? "FILLET — corner trimmed to a point (radius 0)."
                                       : "FILLET — corner filleted.");
          }
        }
      }
    }
  }

  sess->phase = FP::WaitFirstEntity;
  sess->firstEntity = SelectedEntity{};
  sess->firstPolySeg = -1;
  log.push_back("FILLET — select first object or [Radius/Trim] " + FilletPromptSuffix(st) + ". ESC cancels.");
}

bool HandleFilletText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::FilletSession>();
  const bool paperMode = !sess && st.paperFilletPhase != 0;
  if (!sess && !paperMode)
    return false;
  using FP = stepengine::FilletPhase;
  const std::string line = StringUtil::trimCopy(lineIn);
  const std::string low = StringUtil::toLowerAsciiCopy(line);
  if (sess && sess->phase != FP::WaitFirstEntity) {
    log.push_back("FILLET — pick the second object in the viewport, or ESC to cancel.");
    return false;
  }
  bool& awaitingRadius = sess ? sess->textAwaitingRadius : st.paperFilletAwaitingRadius;
  bool& awaitingTrim = sess ? sess->textAwaitingTrim : st.paperFilletAwaitingTrim;
  if (sess && sess->solidAwaitingRadius) {
    double want = static_cast<double>(st.filletRadius);
    if (!line.empty()) {
      float v = 0.f;
      if (!ParseOneFloat(line, &v) || !std::isfinite(v)) {
        log.push_back("FILLET - radius must be a number. Type one, or ESC to cancel:");
        return false;
      }
      want = static_cast<double>(v);
    }
    if (!CadApplyFilletToSelectedEdges(st, want, log)) {
      log.push_back("FILLET - specify a different radius, or ESC to cancel:");
      return true;
    }
    st.filletRadius = static_cast<float>(want);
    stepengine::EndSession(st);
    return true;
  }
  if (awaitingRadius) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !(v >= 0.f) || !std::isfinite(v)) {
      log.push_back("FILLET — radius must be a non-negative finite number.");
      return false;
    }
    st.filletRadius = v;
    awaitingRadius = false;
    log.push_back("FILLET — radius set to " + std::to_string(v) + ". Select first object or [Radius/Trim]:");
    return true;
  }
  if (awaitingTrim) {
    if (low == "t" || low == "trim") {
      st.cornerTrimMode = 1;
    } else if (low == "n" || low == "notrim" || low == "no trim") {
      st.cornerTrimMode = 0;
    } else {
      log.push_back("FILLET — type T (Trim) or N (No trim).");
      return false;
    }
    awaitingTrim = false;
    log.push_back(std::string("FILLET — trim mode set to ") + (st.cornerTrimMode ? "Trim" : "No trim") +
                  ". Select first object or [Radius/Trim]:");
    return true;
  }
  if (sess && CadSubObjectSelectionIsAllEdges(st)) {
    double want = static_cast<double>(st.filletRadius);
    bool answered = line.empty();
    if (!line.empty()) {
      float v = 0.f;
      if (ParseOneFloat(line, &v) && std::isfinite(v)) {
        want = static_cast<double>(v);
        answered = true;
      }
    }
    if (answered) {
      if (!CadApplyFilletToSelectedEdges(st, want, log)) {
        log.push_back("FILLET - specify a different radius, or ESC to cancel:");
        return true;
      }
      st.filletRadius = static_cast<float>(want);
      stepengine::EndSession(st);
      return true;
    }
    if (!line.empty() && low != "r" && low != "radius" && low != "t" && low != "trim") {
      log.push_back("FILLET - radius must be a number. Type one, or ESC to cancel:");
      return false;
    }
  }
  if (low == "r" || low == "radius") {
    awaitingRadius = true;
    log.push_back("FILLET — specify fillet radius <" + std::to_string(st.filletRadius) + ">:");
    return true;
  }
  if (low == "t" || low == "trim") {
    awaitingTrim = true;
    log.push_back(std::string("FILLET — Enter Trim mode option [Trim/No trim] <") +
                  (st.cornerTrimMode ? "Trim" : "No trim") + ">:");
    return true;
  }
  log.push_back("FILLET — type R (Radius) or T (Trim), or pick an object in the viewport.");
  return false;
}

// ------------------------------------------------------------------------------------------------
// FILLET, pure-paper-space path (step 6a) — full parity with model space (Q3, D-2026-08-24-g).
// Mirrors the model-space functions above one-for-one against PaperLayout's stores instead of
// AppCommandState's — the same unavoidable duplication MIRROR/EXTEND/BREAK's own paper paths
// already accept (this codebase's two-store convention, no shared entity representation).
// ------------------------------------------------------------------------------------------------

bool PaperFilletEligibility(const PaperLayout& L, const PaperEntityRef& ref, float pickX, float pickY,
                            int* outPolySeg, std::vector<std::string>& log) {
  using T = PaperEntityRef::Type;
  constexpr float kTwoPi = 6.28318530717958647692f;
  *outPolySeg = -1;
  switch (ref.type) {
  case T::Line:
    return true;
  case T::Arc: {
    if (ref.index < 0 || static_cast<size_t>(ref.index) >= L.paperArcs.size())
      return false;
    const CadArc& a = L.paperArcs[static_cast<size_t>(ref.index)];
    if (std::fabs(std::fabs(a.sweepRad) - kTwoPi) < 1e-4f) {
      log.push_back("FILLET — 1 full-circle arc ignored: fillet needs a curve with a definite tangent "
                    "side. Pick a line, non-full arc, or polyline segment.");
      return false;
    }
    return true;
  }
  case T::Polyline: {
    const int pi = ref.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L.paperPolyOffsets.size())
      return false;
    const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L.paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    const int numVerts = v1 - v0;
    if (numVerts < 2)
      return false;
    const bool closed =
        static_cast<size_t>(pi) < L.paperPolyClosed.size() && L.paperPolyClosed[static_cast<size_t>(pi)];
    const int numEdges = closed ? numVerts : (numVerts - 1);
    int bestSeg = 0;
    float bestD = -1.f;
    for (int e2 = 0; e2 < numEdges; ++e2) {
      const int viA = v0 + e2, viB = v0 + ((e2 + 1) % numVerts);
      const size_t a3 = static_cast<size_t>(viA) * 3, b3 = static_cast<size_t>(viB) * 3;
      float qx = 0.f, qy = 0.f;
      ClosestPointOnSegment(L.paperPolyVerts[a3], L.paperPolyVerts[a3 + 1], L.paperPolyVerts[b3],
                            L.paperPolyVerts[b3 + 1], pickX, pickY, &qx, &qy);
      const float d = (qx - pickX) * (qx - pickX) + (qy - pickY) * (qy - pickY);
      if (bestD < 0.f || d < bestD) {
        bestD = d;
        bestSeg = e2;
      }
    }
    *outPolySeg = bestSeg;
    return true;
  }
  case T::Circle:
    log.push_back("FILLET — 1 circle ignored: fillet needs a curve with a definite tangent side. Pick a "
                  "line, non-full arc, or polyline segment.");
    return false;
  default:
    log.push_back("FILLET — 1 object ignored: only a line, arc, or polyline segment can be filleted.");
    return false;
  }
}

static bool BuildFilletCurveFromPaperEntity(const PaperLayout& L, const PaperEntityRef& ref, int polySeg,
                                            FilletCurve* out) {
  using T = PaperEntityRef::Type;
  if (ref.type == T::Line) {
    const size_t k = static_cast<size_t>(ref.index) * 6;
    if (k + 5 >= L.paperLines.size())
      return false;
    out->isLine = true;
    out->ax = L.paperLines[k];
    out->ay = L.paperLines[k + 1];
    out->bx = L.paperLines[k + 3];
    out->by = L.paperLines[k + 4];
    return true;
  }
  if (ref.type == T::Arc) {
    if (ref.index < 0 || static_cast<size_t>(ref.index) >= L.paperArcs.size())
      return false;
    const CadArc& a = L.paperArcs[static_cast<size_t>(ref.index)];
    out->isLine = false;
    out->cx = static_cast<float>(a.cx);
    out->cy = static_cast<float>(a.cy);
    out->r = static_cast<float>(a.r);
    return true;
  }
  if (ref.type == T::Polyline) {
    const int pi = ref.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L.paperPolyOffsets.size() || polySeg < 0)
      return false;
    const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L.paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    const int numVerts = v1 - v0;
    const int viA = v0 + polySeg, viB = v0 + ((polySeg + 1) % numVerts);
    const size_t a3 = static_cast<size_t>(viA) * 3, b3 = static_cast<size_t>(viB) * 3;
    if (b3 + 1 >= L.paperPolyVerts.size())
      return false;
    out->isLine = true;
    out->ax = L.paperPolyVerts[a3];
    out->ay = L.paperPolyVerts[a3 + 1];
    out->bx = L.paperPolyVerts[b3];
    out->by = L.paperPolyVerts[b3 + 1];
    return true;
  }
  return false;
}

/// Paper-space equivalent of the model-space `FilletRadiusFitsCurve` — see its own comment
/// (D-2026-08-25-b) for the full "radius is too large" reasoning.
static bool PaperFilletRadiusFitsCurve(const PaperLayout& L, const PaperEntityRef& ref, float p0x, float p0y,
                                       float tangentX, float tangentY) {
  using T = PaperEntityRef::Type;
  if (ref.type == T::Line) {
    const size_t k = static_cast<size_t>(ref.index) * 6;
    if (k + 5 >= L.paperLines.size())
      return true;
    const float x0 = L.paperLines[k], y0 = L.paperLines[k + 1];
    const float x1 = L.paperLines[k + 3], y1 = L.paperLines[k + 4];
    const bool nearIsFirst = NearerToFirstPoint(p0x, p0y, x0, y0, x1, y1);
    const float nearX = nearIsFirst ? x0 : x1, nearY = nearIsFirst ? y0 : y1;
    const float farX = nearIsFirst ? x1 : x0, farY = nearIsFirst ? y1 : y0;
    return FilletPointWithinSpan(nearX, nearY, farX, farY, tangentX, tangentY);
  }
  // Arc: see the model-space FilletRadiusFitsCurve's own comment — not checked here either.
  return true;
}

/// Case B, paper store: reuses `ApplyLengthToPaperEntityMutation` — the shared paper mutation
/// LENGTHEN/EXTEND already established — by converting the known tangent point into its `newLen`
/// currency. That function pushes no undo snapshot itself (unlike its model-space counterparts), so
/// there is no double-push to guard against here, unlike the model-space `ApplyFilletTrimSingle`.
static bool ApplyFilletTrimSingleToPaperEntity(PaperLayout* L, const PaperEntityRef& ref, int polySeg,
                                               float tangentX, float tangentY) {
  using T = PaperEntityRef::Type;
  if (ref.type == T::Line) {
    const size_t k = static_cast<size_t>(ref.index) * 6;
    if (k + 5 >= L->paperLines.size())
      return false;
    const float x0 = L->paperLines[k], y0 = L->paperLines[k + 1];
    const float x1 = L->paperLines[k + 3], y1 = L->paperLines[k + 4];
    const bool nearFirst = NearerToFirstPoint(tangentX, tangentY, x0, y0, x1, y1);  // nearest-to-TANGENT, not pick
    const float fixedX = nearFirst ? x1 : x0, fixedY = nearFirst ? y1 : y0;
    const float newLen = std::hypot(tangentX - fixedX, tangentY - fixedY);
    ApplyLengthToPaperEntityMutation(L, ref, nearFirst, 0.f, newLen);
    return true;
  }
  if (ref.type == T::Arc) {
    if (ref.index < 0 || static_cast<size_t>(ref.index) >= L->paperArcs.size())
      return false;
    const CadArc& a = L->paperArcs[static_cast<size_t>(ref.index)];
    const float sx = static_cast<float>(a.cx + a.r * std::cos(a.startRad)), sy = static_cast<float>(a.cy + a.r * std::sin(a.startRad));
    const float ex = static_cast<float>(a.cx + a.r * std::cos(a.startRad + a.sweepRad));
    const float ey = static_cast<float>(a.cy + a.r * std::sin(a.startRad + a.sweepRad));
    const bool nearFirst = NearerToFirstPoint(tangentX, tangentY, sx, sy, ex, ey);
    const float newLen =
        FilletArcTangentPointToNewLength(static_cast<float>(a.cx), static_cast<float>(a.cy), static_cast<float>(a.r), a.startRad, a.sweepRad, nearFirst, tangentX, tangentY);
    ApplyLengthToPaperEntityMutation(L, ref, nearFirst, 0.f, newLen);
    return true;
  }
  if (ref.type == T::Polyline) {
    const int pi = ref.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L->paperPolyOffsets.size() || polySeg < 0)
      return false;
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    const int numVerts = v1 - v0;
    const int numEdges = numVerts - 1;  // Case B never reaches a closed polyline — no free end
    const bool nearFirst = (polySeg == 0);
    if (!nearFirst && polySeg != numEdges - 1)
      return false;
    const int movingVi = nearFirst ? v0 : (v1 - 1);
    const int fixedVi = nearFirst ? (v0 + 1) : (v1 - 2);
    const size_t mIdx = static_cast<size_t>(movingVi) * 3, fIdx = static_cast<size_t>(fixedVi) * 3;
    if (mIdx + 1 >= L->paperPolyVerts.size() || fIdx + 1 >= L->paperPolyVerts.size())
      return false;
    const float fx = L->paperPolyVerts[fIdx], fy = L->paperPolyVerts[fIdx + 1];
    const float mx = L->paperPolyVerts[mIdx], my = L->paperPolyVerts[mIdx + 1];
    const float oldSegLen = std::hypot(mx - fx, my - fy);
    const float newSegLen = std::hypot(tangentX - fx, tangentY - fy);
    float curTotal = 0.f;
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const size_t a = static_cast<size_t>(vi) * 3, b = (static_cast<size_t>(vi)+ 1) * 3;
      curTotal += std::hypot(L->paperPolyVerts[b] - L->paperPolyVerts[a], L->paperPolyVerts[b + 1] - L->paperPolyVerts[a + 1]);
    }
    const float newLen = curTotal - oldSegLen + newSegLen;
    ApplyLengthToPaperEntityMutation(L, ref, nearFirst, curTotal, newLen);
    return true;
  }
  return false;
}

/// Case A, paper store: same-polyline adjacent segments — the paper equivalent of
/// `ApplyFilletPolylineCorner`, using `ReplacePaperPolylineVerts` (BREAK's own paper CSR-shift
/// helper) in place of the model store's `ReplacePolylineVerts`.
static bool ApplyFilletPolylineCornerPaper(AppCommandState& st, PaperLayout* L, int pi, int edgeA, int edgeB,
                                           float radius, float pick1X, float pick1Y, float pick2X, float pick2Y,
                                           std::vector<std::string>& log) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L->paperPolyOffsets.size())
    return false;
  const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
  const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
  const int numVerts = v1 - v0;
  if (numVerts < 2)
    return false;
  const bool closed =
      static_cast<size_t>(pi) < L->paperPolyClosed.size() && L->paperPolyClosed[static_cast<size_t>(pi)];
  const int numEdges = closed ? numVerts : (numVerts - 1);
  if (edgeA < 0 || edgeA >= numEdges || edgeB < 0 || edgeB >= numEdges || edgeA == edgeB)
    return false;

  auto edgeVerts = [&](int e, int* viA, int* viB) {
    *viA = v0 + e;
    *viB = v0 + ((e + 1) % numVerts);
  };
  int aVi0 = 0, aVi1 = 0, bVi0 = 0, bVi1 = 0;
  edgeVerts(edgeA, &aVi0, &aVi1);
  edgeVerts(edgeB, &bVi0, &bVi1);
  int sharedVi = -1, otherAVi = -1;
  if (aVi0 == bVi0 || aVi0 == bVi1) {
    sharedVi = aVi0;
    otherAVi = aVi1;
  } else if (aVi1 == bVi0 || aVi1 == bVi1) {
    sharedVi = aVi1;
    otherAVi = aVi0;
  }
  if (sharedVi < 0) {
    log.push_back("FILLET — those two polyline segments are not adjacent; refused.");
    return false;
  }
  const int otherBVi = (bVi0 == sharedVi) ? bVi1 : bVi0;

  auto readVert = [&](int vi, float* x, float* y) {
    *x = L->paperPolyVerts[static_cast<size_t>(vi) * 3];
    *y = L->paperPolyVerts[static_cast<size_t>(vi) * 3 + 1];
  };
  float sharedX = 0.f, sharedY = 0.f, otherAX = 0.f, otherAY = 0.f, otherBX = 0.f, otherBY = 0.f;
  readVert(sharedVi, &sharedX, &sharedY);
  readVert(otherAVi, &otherAX, &otherAY);
  readVert(otherBVi, &otherBX, &otherBY);

  FilletCurve curveA{};
  curveA.isLine = true;
  curveA.ax = otherAX;
  curveA.ay = otherAY;
  curveA.bx = sharedX;
  curveA.by = sharedY;
  FilletCurve curveB{};
  curveB.isLine = true;
  curveB.ax = sharedX;
  curveB.ay = sharedY;
  curveB.bx = otherBX;
  curveB.by = otherBY;

  float cx = 0.f, cy = 0.f;
  if (!SolveFilletCenter(curveA, curveB, radius, pick1X, pick1Y, pick2X, pick2Y, &cx, &cy)) {
    log.push_back("FILLET — no valid tangent arc exists for that radius at that corner; refused.");
    return false;
  }
  float tAx = 0.f, tAy = 0.f, tBx = 0.f, tBy = 0.f;
  FilletTangentPointOnLine(curveA, cx, cy, &tAx, &tAy);
  FilletTangentPointOnLine(curveB, cx, cy, &tBx, &tBy);

  const int sharedLocal = sharedVi - v0;
  const int otherALocal = otherAVi - v0;
  const bool aIsIncoming = (otherALocal == (sharedLocal - 1 + numVerts) % numVerts);
  const float inTx = aIsIncoming ? tAx : tBx, inTy = aIsIncoming ? tAy : tBy;
  const float outTx = aIsIncoming ? tBx : tAx, outTy = aIsIncoming ? tBy : tAy;
  const bool radiusIsZero = radius < 1e-4f;

  std::vector<std::pair<float, float>> newXY;
  newXY.reserve(static_cast<size_t>(numVerts) + 1);
  for (int vi = v0; vi < v1; ++vi) {
    if (vi - v0 == sharedLocal) {
      if (radiusIsZero) {
        newXY.push_back({cx, cy});
      } else {
        newXY.push_back({inTx, inTy});
        newXY.push_back({outTx, outTy});
      }
    } else {
      float x = 0.f, y = 0.f;
      readVert(vi, &x, &y);
      newXY.push_back({x, y});
    }
  }

  PushUndoSnapshot(st, "Fillet paper geometry");
  ReplacePaperPolylineVerts(L, pi, newXY);
  if (!radiusIsZero) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float thetaIn = std::atan2(inTy - cy, inTx - cx);
    const float thetaOut = std::atan2(outTy - cy, outTx - cx);
    float sweep = FilletCcwAngleDelta(thetaIn, thetaOut);
    if (sweep > kPi)
      sweep -= kTwoPi;
    CadArc arc{};
    arc.cx = cx;
    arc.cy = cy;
    arc.r = radius;
    arc.startRad = thetaIn;
    arc.sweepRad = sweep;
    L->paperArcs.push_back(arc);
    L->paperArcAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);
  log.push_back(radiusIsZero ? "FILLET — polyline corner trimmed to a point (radius 0)."
                            : "FILLET — polyline corner filleted.");
  return true;
}

bool ApplyFilletToPaperEntities(AppCommandState& st, const PaperEntityRef& first, int firstPolySeg,
                                float firstPickX, float firstPickY, const PaperEntityRef& second,
                                int secondPolySeg, float pickX, float pickY, std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L)
    return false;

  const bool sameEntity = (second.type == first.type && second.index == first.index);
  if (sameEntity && second.type != PaperEntityRef::Type::Polyline) {
    log.push_back("FILLET — select two different objects.");
    return false;
  }
  if (sameEntity && secondPolySeg == firstPolySeg) {
    log.push_back("FILLET — select two different segments.");
    return false;
  }

  if (sameEntity)
    return ApplyFilletPolylineCornerPaper(st, L, second.index, firstPolySeg, secondPolySeg, st.filletRadius,
                                          firstPickX, firstPickY, pickX, pickY, log);

  auto polylineIsEndSegmentOnly = [&](const PaperEntityRef& ref, int seg) -> bool {
    if (ref.type != PaperEntityRef::Type::Polyline)
      return true;
    const int pi = ref.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L->paperPolyOffsets.size())
      return false;
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    const bool closed =
        static_cast<size_t>(pi) < L->paperPolyClosed.size() && L->paperPolyClosed[static_cast<size_t>(pi)];
    if (closed) {
      log.push_back("FILLET — a closed polyline has no free end; only an adjacent segment of the SAME "
                    "polyline can be filleted against it.");
      return false;
    }
    const int numEdges = (v1 - v0) - 1;
    if (seg != 0 && seg != numEdges - 1) {
      log.push_back("FILLET — only a polyline's own first or last segment can be filleted against a "
                    "different object; an interior segment shares its vertex with a neighbor that would "
                    "be silently disturbed.");
      return false;
    }
    return true;
  };
  if (!polylineIsEndSegmentOnly(first, firstPolySeg) || !polylineIsEndSegmentOnly(second, secondPolySeg))
    return false;

  FilletCurve c1{}, c2{};
  if (!BuildFilletCurveFromPaperEntity(*L, first, firstPolySeg, &c1) ||
      !BuildFilletCurveFromPaperEntity(*L, second, secondPolySeg, &c2)) {
    log.push_back("FILLET — could not resolve the picked geometry; refused.");
    return false;
  }

  if (c1.isLine && c2.isLine && FilletLinesAreParallel(c1.ax, c1.ay, c1.bx, c1.by, c2.ax, c2.ay, c2.bx, c2.by)) {
    float anchorX = 0.f, anchorY = 0.f, projX = 0.f, projY = 0.f;
    FilletParallelSemicircle(c1.ax, c1.ay, c1.bx, c1.by, c2.ax, c2.ay, c2.bx, c2.by, firstPickX, firstPickY,
                             &anchorX, &anchorY, &projX, &projY);
    const float semiR = 0.5f * std::hypot(projX - anchorX, projY - anchorY);
    if (semiR < 1e-4f) {
      log.push_back("FILLET — the two lines coincide; refused.");
      return false;
    }
    PushUndoSnapshot(st, "Fillet paper geometry");
    if (st.cornerTrimMode)
      ApplyFilletTrimSingleToPaperEntity(L, second, secondPolySeg, projX, projY);
    const float cx = 0.5f * (anchorX + projX), cy = 0.5f * (anchorY + projY);
    CadArc arc{};
    arc.cx = cx;
    arc.cy = cy;
    arc.r = semiR;
    arc.startRad = std::atan2(anchorY - cy, anchorX - cx);
    arc.sweepRad = 3.14159265358979323846f;
    L->paperArcs.push_back(arc);
    L->paperArcAttrs.push_back(MakeNewEntityAttrs(st));
    BumpCadGpuCache(st);
    log.push_back("FILLET — parallel lines connected by a semicircle (radius set by the gap between "
                  "them, not the current FILLET radius).");
    return true;
  }

  float cx = 0.f, cy = 0.f;
  if (!SolveFilletCenter(c1, c2, st.filletRadius, firstPickX, firstPickY, pickX, pickY, &cx, &cy)) {
    log.push_back("FILLET — no valid tangent arc exists for that radius; refused.");
    return false;
  }
  float t1x = 0.f, t1y = 0.f, t2x = 0.f, t2y = 0.f;
  if (c1.isLine)
    FilletTangentPointOnLine(c1, cx, cy, &t1x, &t1y);
  else
    FilletTangentPointOnCircle(c1, cx, cy, &t1x, &t1y);
  if (c2.isLine)
    FilletTangentPointOnLine(c2, cx, cy, &t2x, &t2y);
  else
    FilletTangentPointOnCircle(c2, cx, cy, &t2x, &t2y);

  // See the model-space call site's own comment for why this needs a fresh radius-0 solve rather
  // than the offset candidate `cx,cy` above.
  float p0x = 0.f, p0y = 0.f;
  const bool haveP0 = SolveFilletCenter(c1, c2, 0.f, firstPickX, firstPickY, pickX, pickY, &p0x, &p0y);
  if (haveP0 && (!PaperFilletRadiusFitsCurve(*L, first, p0x, p0y, t1x, t1y) ||
                !PaperFilletRadiusFitsCurve(*L, second, p0x, p0y, t2x, t2y))) {
    log.push_back("FILLET — radius " + std::to_string(st.filletRadius) +
                  " is too large for the selected objects; refused.");
    return false;
  }

  PushUndoSnapshot(st, "Fillet paper geometry");
  if (st.cornerTrimMode) {
    ApplyFilletTrimSingleToPaperEntity(L, first, firstPolySeg, t1x, t1y);
    ApplyFilletTrimSingleToPaperEntity(L, second, secondPolySeg, t2x, t2y);
  }
  const bool radiusIsZero = st.filletRadius < 1e-4f;
  if (!radiusIsZero) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    const float thetaA = std::atan2(t1y - cy, t1x - cx);
    const float thetaB = std::atan2(t2y - cy, t2x - cx);
    float sweep = FilletCcwAngleDelta(thetaA, thetaB);
    if (sweep > kPi)
      sweep -= kTwoPi;
    CadArc arc{};
    arc.cx = cx;
    arc.cy = cy;
    arc.r = st.filletRadius;
    arc.startRad = thetaA;
    arc.sweepRad = sweep;
    L->paperArcs.push_back(arc);
    L->paperArcAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);
  log.push_back(radiusIsZero ? "FILLET — corner trimmed to a point (radius 0)." : "FILLET — corner filleted.");
  return true;
}

// ================================================================================================
// CHAMFER (REQ-103 step 6b, TASK-103). Reuses almost everything FILLET (step 6a, above) already
// built: `BuildFilletCurveFromEntity`/`ApplyFilletTrimSingle` unchanged (both dispatch by entity
// type and simply never see an Arc, since CHAMFER refuses it upstream), the same Case A/B split,
// the same `cornerTrimMode` toggle, `SolveFilletCenter(c1,c2,0.f,...)` for the two curves'
// intersection point (FILLET's own radius-0 path). Only the corner-point construction is new
// (`ChamferPointAtDistance`/`ChamferRayIntersect`, GoSurvey/Geometry/FilletChamfer.hpp) and the connector is an
// ordinary Line, not an Arc.
// ================================================================================================

constexpr float kChamferDegToRad = 0.01745329251994329577f;

/// Is `e` (picked at pickX,pickY) an eligible CHAMFER curve, and if it's a Polyline, which edge?
/// Eligible: Line, any Polyline segment (open or closed). Arc/Circle refused — chamfer connects two
/// straight curves only, measured by distance/angle from their intersection; no standard
/// chamfer-to-arc geometry exists (matching AutoCAD's own restriction). Every other kind refused
/// with a stated reason (REQ-201).
static bool ChamferEligibility(const AppCommandState& st, const SelectedEntity& e, float pickX, float pickY,
                               int* outPolySeg, std::vector<std::string>& log) {
  using T = SelectedEntity::Type;
  *outPolySeg = -1;
  switch (e.type) {
  case T::LineSeg:
    return true;
  case T::Polyline: {
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
      return false;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const int numVerts = v1 - v0;
    if (numVerts < 2)
      return false;
    const bool closed =
        static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
    const int numEdges = closed ? numVerts : (numVerts - 1);
    int bestSeg = 0;
    float bestD = -1.f;
    for (int e2 = 0; e2 < numEdges; ++e2) {
      const int viA = v0 + e2, viB = v0 + ((e2 + 1) % numVerts);
      const size_t a3 = static_cast<size_t>(viA), b3 = static_cast<size_t>(viB);
      float qx = 0.f, qy = 0.f;
      ClosestPointOnSegment(static_cast<float>(st.drawing.vertex(a3).x), static_cast<float>(st.drawing.vertex(a3).y), static_cast<float>(st.drawing.vertex(b3).x),
                            static_cast<float>(st.drawing.vertex(b3).y), pickX, pickY, &qx, &qy);
      const float d = (qx - pickX) * (qx - pickX) + (qy - pickY) * (qy - pickY);
      if (bestD < 0.f || d < bestD) {
        bestD = d;
        bestSeg = e2;
      }
    }
    *outPolySeg = bestSeg;
    return true;
  }
  case T::Arc:
    log.push_back("CHAMFER — 1 arc ignored: chamfer connects two straight curves only, measured by "
                  "distance/angle from their intersection — no standard chamfer-to-arc geometry "
                  "exists. Pick a line or polyline segment.");
    return false;
  case T::Circle:
    log.push_back("CHAMFER — 1 circle ignored: chamfer connects two straight curves only. Pick a "
                  "line or polyline segment.");
    return false;
  default:
    log.push_back("CHAMFER — 1 object ignored: only a line or polyline segment can be chamfered.");
    return false;
  }
}

/// Resolves the current mode+values into the two chamfer points for curves `c1`/`c2`, whose
/// intersection is `(px,py)`. Distance/Distance: each point is its own curve's persisted distance
/// from the intersection. Distance/Angle: curve 1's point is `chamferDist1` from the intersection;
/// curve 2's point is where curve 1's kept direction, rotated by `chamferAngle`, meets curve 2.
/// False (REQ-201, caller states the reason) only in Distance/Angle mode when neither rotation
/// sense reaches curve 2 at all.
static bool ChamferResolvePoints(const AppCommandState& st, const FilletCurve& c1, const FilletCurve& c2, float px,
                                 float py, float pick1X, float pick1Y, float pick2X, float pick2Y, float* out1X,
                                 float* out1Y, float* out2X, float* out2Y) {
  ChamferPointAtDistance(c1, px, py, st.chamferDist1, pick1X, pick1Y, out1X, out1Y);
  if (st.chamferMode == 0) {
    ChamferPointAtDistance(c2, px, py, st.chamferDist2, pick2X, pick2Y, out2X, out2Y);
    return true;
  }
  return ChamferRayIntersect(c1, *out1X, *out1Y, pick1X, pick1Y, st.chamferAngle * kChamferDegToRad, c2, pick2X,
                             pick2Y, out2X, out2Y);
}

/// Case A: adjacent segments of the SAME polyline — the CHAMFER equivalent of
/// `ApplyFilletPolylineCorner`. Splices the shared vertex into the two chamfer points (or one, if
/// both distances/the single Distance/Angle distance are ~0) and inserts a fresh Line connector
/// (radius-0-equivalent case: none).
static bool ApplyChamferPolylineCorner(AppCommandState& st, int pi, int edgeA, int edgeB, float pick1X,
                                       float pick1Y, float pick2X, float pick2Y, std::vector<std::string>& log) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
    return false;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  const int numVerts = v1 - v0;
  if (numVerts < 2)
    return false;
  const bool closed =
      static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
  const int numEdges = closed ? numVerts : (numVerts - 1);
  if (edgeA < 0 || edgeA >= numEdges || edgeB < 0 || edgeB >= numEdges || edgeA == edgeB)
    return false;

  auto edgeVerts = [&](int e, int* viA, int* viB) {
    *viA = v0 + e;
    *viB = v0 + ((e + 1) % numVerts);
  };
  int aVi0 = 0, aVi1 = 0, bVi0 = 0, bVi1 = 0;
  edgeVerts(edgeA, &aVi0, &aVi1);
  edgeVerts(edgeB, &bVi0, &bVi1);
  int sharedVi = -1, otherAVi = -1;
  if (aVi0 == bVi0 || aVi0 == bVi1) {
    sharedVi = aVi0;
    otherAVi = aVi1;
  } else if (aVi1 == bVi0 || aVi1 == bVi1) {
    sharedVi = aVi1;
    otherAVi = aVi0;
  }
  if (sharedVi < 0) {
    log.push_back("CHAMFER — those two polyline segments are not adjacent; refused.");
    return false;
  }
  const int otherBVi = (bVi0 == sharedVi) ? bVi1 : bVi0;

  auto readVert = [&](int vi, float* x, float* y) {
    *x = static_cast<float>(st.drawing.vertex(vi).x);
    *y = static_cast<float>(st.drawing.vertex(vi).y);
  };
  auto vertZ = [&](int vi) { return st.drawing.vertex(vi).z; };
  // Same rule as FILLET's corner (issue 01): a chamfer endpoint sits on one of the two segments
  // meeting at the corner, so its elevation interpolates along that segment.
  auto zOnSegTo = [&](int fromVi, int toVi, float qx, float qy) {
    float fx = 0.f, fy = 0.f, tx = 0.f, ty = 0.f;
    readVert(fromVi, &fx, &fy);
    readVert(toVi, &tx, &ty);
    const float len = std::hypot(tx - fx, ty - fy);
    const float t = (len > 1e-9f) ? (std::hypot(qx - fx, qy - fy) / len) : 0.f;
    return vertZ(fromVi) + (vertZ(toVi) - vertZ(fromVi)) * t;
  };
  float sharedX = 0.f, sharedY = 0.f, otherAX = 0.f, otherAY = 0.f, otherBX = 0.f, otherBY = 0.f;
  readVert(sharedVi, &sharedX, &sharedY);
  readVert(otherAVi, &otherAX, &otherAY);
  readVert(otherBVi, &otherBX, &otherBY);

  FilletCurve curveA{};
  curveA.isLine = true;
  curveA.ax = otherAX;
  curveA.ay = otherAY;
  curveA.bx = sharedX;
  curveA.by = sharedY;
  FilletCurve curveB{};
  curveB.isLine = true;
  curveB.ax = sharedX;
  curveB.ay = sharedY;
  curveB.bx = otherBX;
  curveB.by = otherBY;

  float px = 0.f, py = 0.f;
  if (!SolveFilletCenter(curveA, curveB, 0.f, pick1X, pick1Y, pick2X, pick2Y, &px, &py)) {
    log.push_back("CHAMFER — those two segments are parallel; no corner exists; refused.");
    return false;
  }
  float dAx = 0.f, dAy = 0.f, dBx = 0.f, dBy = 0.f;
  if (!ChamferResolvePoints(st, curveA, curveB, px, py, pick1X, pick1Y, pick2X, pick2Y, &dAx, &dAy, &dBx, &dBy)) {
    log.push_back("CHAMFER — that angle does not reach the second segment; refused.");
    return false;
  }

  const int sharedLocal = sharedVi - v0;
  const int otherALocal = otherAVi - v0;
  const bool aIsIncoming = (otherALocal == (sharedLocal - 1 + numVerts) % numVerts);
  const float inX = aIsIncoming ? dAx : dBx, inY = aIsIncoming ? dAy : dBy;
  const float outX = aIsIncoming ? dBx : dAx, outY = aIsIncoming ? dBy : dAy;
  const bool bothZero = (st.chamferMode == 0) ? (st.chamferDist1 < 1e-4f && st.chamferDist2 < 1e-4f)
                                              : (st.chamferDist1 < 1e-4f);

  const int inNeighbourVi = aIsIncoming ? otherAVi : otherBVi;
  const int outNeighbourVi = aIsIncoming ? otherBVi : otherAVi;
  const float inZ = static_cast<float>(zOnSegTo(sharedVi, inNeighbourVi, inX, inY));
  const float outZ = static_cast<float>(zOnSegTo(sharedVi, outNeighbourVi, outX, outY));
  std::vector<PolyVert> newXY;
  newXY.reserve(static_cast<size_t>(numVerts) + 1);
  for (int vi = v0; vi < v1; ++vi) {
    if (vi - v0 == sharedLocal) {
      if (bothZero) {
        newXY.push_back({px, py, static_cast<float>(vertZ(sharedVi))});
      } else {
        newXY.push_back({inX, inY, inZ});
        newXY.push_back({outX, outY, outZ});
      }
    } else {
      float x = 0.f, y = 0.f;
      readVert(vi, &x, &y);
      newXY.push_back({x, y, static_cast<float>(vertZ(vi))});
    }
  }

  PushUndoSnapshot(st, "Chamfer");
  ReplacePolylineVerts(st, pi, newXY);
  if (!bothZero) {
    const size_t li = st.drawing.lineCount();
    // The chamfer segment joins two points that have real elevations, so it is built at them.
    // It used to be born at datum while the polyline it connects kept its height - a corner whose
    // two ends were at grade and whose connector was hundreds of feet below (issue 01).
    st.drawing.pushLine(CadLineRecord{inX, inY, inZ, outX, outY, outZ});
    st.drawing.userLineAttrs.push_back(MakeNewEntityAttrs(st));
    (void)li;
  }
  BumpCadGpuCache(st);
  log.push_back(bothZero ? "CHAMFER — polyline corner trimmed to a point (distance 0)."
                        : "CHAMFER — polyline corner chamfered.");
  return true;
}

static std::string ChamferPromptSuffix(const AppCommandState& st) {
  char buf[128];
  if (st.chamferMode == 0)
    std::snprintf(buf, sizeof(buf), "<D1=%.3f, D2=%.3f, %s>", static_cast<double>(st.chamferDist1),
                 static_cast<double>(st.chamferDist2), st.cornerTrimMode ? "Trim" : "No trim");
  else
    std::snprintf(buf, sizeof(buf), "<D=%.3f, A=%.1f, %s>", static_cast<double>(st.chamferDist1),
                 static_cast<double>(st.chamferAngle), st.cornerTrimMode ? "Trim" : "No trim");
  return buf;
}

void StartChamferCommand(AppCommandState& st, std::vector<std::string>& log) {
  ResetPaperModifyDrafts(st);
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {
    stepengine::EndSession(st);
    st.paperChamferPhase = 1;
    st.paperChamferFirstEntity = PaperEntityRef{};
    st.paperChamferFirstPolySeg = -1;
    log.push_back("CHAMFER — select first object " + ChamferPromptSuffix(st) + ". ESC cancels.");
    return;
  }
  ClearPendingViewportZoom(st);
  stepengine::BeginSession(st, std::make_unique<stepengine::ChamferSession>(false), true);
  log.push_back("CHAMFER — select first object or [Distance/Angle/Trim] " + ChamferPromptSuffix(st) +
               ". ESC cancels.");
}

void HandleChamferViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::ChamferSession>();
  if (!sess)
    return;
  using CP = stepengine::ChamferPhase;
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
    log.push_back("CHAMFER — no object at pick.");
    return;
  }
  int polySeg = -1;
  if (!ChamferEligibility(st, hit, wx, wy, &polySeg, log))
    return;

  if (sess->phase == CP::WaitFirstEntity) {
    sess->firstEntity = hit;
    sess->firstPolySeg = polySeg;
    sess->firstPickX = wx;
    sess->firstPickY = wy;
    sess->phase = CP::WaitSecondEntity;
    log.push_back("CHAMFER — select second object:");
    return;
  }

  const bool sameEntity = (hit.type == sess->firstEntity.type && hit.index == sess->firstEntity.index);
  if (sameEntity && hit.type != SelectedEntity::Type::Polyline) {
    log.push_back("CHAMFER — select two different objects.");
    return;
  }
  if (sameEntity && polySeg == sess->firstPolySeg) {
    log.push_back("CHAMFER — select two different segments.");
    return;
  }

  if (sameEntity) {
    ApplyChamferPolylineCorner(st, hit.index, sess->firstPolySeg, polySeg, sess->firstPickX,
                               sess->firstPickY, wx, wy, log);
  } else {
    auto polylineIsEndSegmentOnly = [&](const SelectedEntity& e, int seg) -> bool {
      if (e.type != SelectedEntity::Type::Polyline)
        return true;
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        return false;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      const bool closed =
          static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
      if (closed) {
        log.push_back("CHAMFER — a closed polyline has no free end; only an adjacent segment of the "
                      "SAME polyline can be chamfered against it.");
        return false;
      }
      const int numEdges = (v1 - v0) - 1;
      if (seg != 0 && seg != numEdges - 1) {
        log.push_back("CHAMFER — only a polyline's own first or last segment can be chamfered against "
                      "a different object; an interior segment shares its vertex with a neighbor that "
                      "would be silently disturbed.");
        return false;
      }
      return true;
    };
    if (!polylineIsEndSegmentOnly(sess->firstEntity, sess->firstPolySeg) ||
        !polylineIsEndSegmentOnly(hit, polySeg)) {
      return;
    }

    FilletCurve c1{}, c2{};
    if (!BuildFilletCurveFromEntity(st, sess->firstEntity, sess->firstPolySeg, &c1) ||
        !BuildFilletCurveFromEntity(st, hit, polySeg, &c2)) {
      log.push_back("CHAMFER — could not resolve the picked geometry; refused.");
      return;
    }

    float px = 0.f, py = 0.f;
    if (!SolveFilletCenter(c1, c2, 0.f, sess->firstPickX, sess->firstPickY, wx, wy, &px, &py)) {
      log.push_back("CHAMFER — those two curves are parallel; no corner exists; refused.");
    } else {
      float d1x = 0.f, d1y = 0.f, d2x = 0.f, d2y = 0.f;
      if (!ChamferResolvePoints(st, c1, c2, px, py, sess->firstPickX, sess->firstPickY, wx, wy, &d1x, &d1y,
                                &d2x, &d2y)) {
        log.push_back("CHAMFER — that angle does not reach the second curve; refused.");
      } else {
        PushUndoSnapshot(st, "Chamfer");
        bool ok1 = true, ok2 = true;
        if (st.cornerTrimMode) {
          ok1 = ApplyFilletTrimSingle(st, sess->firstEntity, sess->firstPolySeg, d1x, d1y, log);
          ok2 = ApplyFilletTrimSingle(st, hit, polySeg, d2x, d2y, log);
        }
        if (ok1 && ok2) {
          const bool bothZero = (st.chamferMode == 0) ? (st.chamferDist1 < 1e-4f && st.chamferDist2 < 1e-4f)
                                                       : (st.chamferDist1 < 1e-4f);
          if (!bothZero) {
            BreakPoint bz1{}, bz2{};
            const float z1 = ClosestPointOnEntity(st, sess->firstEntity, d1x, d1y, &bz1) ? bz1.z : 0.f;
            const float z2 = ClosestPointOnEntity(st, hit, d2x, d2y, &bz2) ? bz2.z : 0.f;
            st.drawing.pushLine(CadLineRecord{d1x, d1y, z1, d2x, d2y, z2});
            st.drawing.userLineAttrs.push_back(MakeNewEntityAttrs(st));
          }
          BumpCadGpuCache(st);
          log.push_back(bothZero ? "CHAMFER — corner trimmed to a point (distance 0)."
                                 : "CHAMFER — corner chamfered.");
        }
      }
    }
  }

  sess->phase = CP::WaitFirstEntity;
  sess->firstEntity = SelectedEntity{};
  sess->firstPolySeg = -1;
  log.push_back("CHAMFER — select first object or [Distance/Angle/Trim] " + ChamferPromptSuffix(st) +
               ". ESC cancels.");
}

bool HandleChamferText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::ChamferSession>();
  const bool paperMode = !sess && st.paperChamferPhase != 0;
  if (!sess && !paperMode)
    return false;
  using CP = stepengine::ChamferPhase;
  const std::string line = StringUtil::trimCopy(lineIn);
  const std::string low = StringUtil::toLowerAsciiCopy(line);
  if (sess && sess->phase != CP::WaitFirstEntity) {
    log.push_back("CHAMFER — pick the second object in the viewport, or ESC to cancel.");
    return false;
  }
  bool& awaitingFirst = sess ? sess->textAwaitingFirstValue : st.paperChamferAwaitingFirstValue;
  bool& awaitingSecond = sess ? sess->textAwaitingSecondDist : st.paperChamferAwaitingSecondDist;
  bool& awaitingAngle = sess ? sess->textAwaitingAngle : st.paperChamferAwaitingAngle;
  bool& awaitingTrim = sess ? sess->textAwaitingTrim : st.paperChamferAwaitingTrim;
  if (sess && sess->solidAwaitingDistance) {
    double want = static_cast<double>(st.chamferDist1);
    if (!line.empty()) {
      float v = 0.f;
      if (!ParseOneFloat(line, &v) || !std::isfinite(v)) {
        log.push_back("CHAMFER - distance must be a number. Type one, or ESC to cancel:");
        return false;
      }
      want = static_cast<double>(v);
    }
    if (!CadApplyChamferToSelectedEdges(st, want, log)) {
      log.push_back("CHAMFER - specify a different distance, or ESC to cancel:");
      return true;
    }
    st.chamferDist1 = static_cast<float>(want);
    stepengine::EndSession(st);
    return true;
  }
  if (awaitingFirst) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !(v >= 0.f) || !std::isfinite(v)) {
      log.push_back("CHAMFER — must be a non-negative finite number.");
      return false;
    }
    st.chamferDist1 = v;
    awaitingFirst = false;
    if (st.chamferMode == 0) {
      awaitingSecond = true;
      log.push_back("CHAMFER — specify second distance <" + std::to_string(st.chamferDist2) + ">:");
    } else {
      awaitingAngle = true;
      log.push_back("CHAMFER — specify angle in degrees <" + std::to_string(st.chamferAngle) + ">:");
    }
    return true;
  }
  if (awaitingSecond) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !(v >= 0.f) || !std::isfinite(v)) {
      log.push_back("CHAMFER — must be a non-negative finite number.");
      return false;
    }
    st.chamferDist2 = v;
    awaitingSecond = false;
    log.push_back("CHAMFER — distances set. Select first object or [Distance/Angle/Trim]:");
    return true;
  }
  if (awaitingAngle) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !std::isfinite(v) || v <= 0.f || v >= 180.f) {
      log.push_back("CHAMFER — angle must be a finite number strictly between 0 and 180 degrees.");
      return false;
    }
    st.chamferAngle = v;
    awaitingAngle = false;
    log.push_back("CHAMFER — distance/angle set. Select first object or [Distance/Angle/Trim]:");
    return true;
  }
  if (awaitingTrim) {
    if (low == "t" || low == "trim") {
      st.cornerTrimMode = 1;
    } else if (low == "n" || low == "notrim" || low == "no trim") {
      st.cornerTrimMode = 0;
    } else {
      log.push_back("CHAMFER — type T (Trim) or N (No trim).");
      return false;
    }
    awaitingTrim = false;
    log.push_back(std::string("CHAMFER — trim mode set to ") + (st.cornerTrimMode ? "Trim" : "No trim") +
                 ". Select first object or [Distance/Angle/Trim]:");
    return true;
  }
  if (sess && CadSubObjectSelectionIsAllEdges(st)) {
    double want = static_cast<double>(st.chamferDist1);
    bool answered = line.empty();
    if (!line.empty()) {
      float v = 0.f;
      if (ParseOneFloat(line, &v) && std::isfinite(v)) {
        want = static_cast<double>(v);
        answered = true;
      }
    }
    if (answered) {
      if (!CadApplyChamferToSelectedEdges(st, want, log)) {
        log.push_back("CHAMFER - specify a different distance, or ESC to cancel:");
        return true;
      }
      st.chamferDist1 = static_cast<float>(want);
      stepengine::EndSession(st);
      return true;
    }
  }
  if (low == "d" || low == "distance") {
    st.chamferMode = 0;
    awaitingFirst = true;
    log.push_back("CHAMFER Distance — specify first distance <" + std::to_string(st.chamferDist1) + ">:");
    return true;
  }
  if (low == "a" || low == "angle") {
    st.chamferMode = 1;
    awaitingFirst = true;
    log.push_back("CHAMFER Angle — specify distance <" + std::to_string(st.chamferDist1) + ">:");
    return true;
  }
  if (low == "t" || low == "trim") {
    awaitingTrim = true;
    log.push_back(std::string("CHAMFER — Enter Trim mode option [Trim/No trim] <") +
                 (st.cornerTrimMode ? "Trim" : "No trim") + ">:");
    return true;
  }
  log.push_back("CHAMFER — type D (Distance), A (Angle), or T (Trim), or pick an object in the viewport.");
  return false;
}

// ------------------------------------------------------------------------------------------------
// CHAMFER, pure-paper-space path (step 6b) — full parity with model space. Mirrors paper FILLET's
// own shape exactly, minus the Arc branch (CHAMFER refuses it) and the parallel-lines special case
// (CHAMFER has none — a real, stated asymmetry, not an oversight).
// ------------------------------------------------------------------------------------------------

bool PaperChamferEligibility(const PaperLayout& L, const PaperEntityRef& ref, float pickX, float pickY,
                             int* outPolySeg, std::vector<std::string>& log) {
  using T = PaperEntityRef::Type;
  *outPolySeg = -1;
  switch (ref.type) {
  case T::Line:
    return true;
  case T::Polyline: {
    const int pi = ref.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L.paperPolyOffsets.size())
      return false;
    const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L.paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    const int numVerts = v1 - v0;
    if (numVerts < 2)
      return false;
    const bool closed =
        static_cast<size_t>(pi) < L.paperPolyClosed.size() && L.paperPolyClosed[static_cast<size_t>(pi)];
    const int numEdges = closed ? numVerts : (numVerts - 1);
    int bestSeg = 0;
    float bestD = -1.f;
    for (int e2 = 0; e2 < numEdges; ++e2) {
      const int viA = v0 + e2, viB = v0 + ((e2 + 1) % numVerts);
      const size_t a3 = static_cast<size_t>(viA) * 3, b3 = static_cast<size_t>(viB) * 3;
      float qx = 0.f, qy = 0.f;
      ClosestPointOnSegment(L.paperPolyVerts[a3], L.paperPolyVerts[a3 + 1], L.paperPolyVerts[b3],
                            L.paperPolyVerts[b3 + 1], pickX, pickY, &qx, &qy);
      const float d = (qx - pickX) * (qx - pickX) + (qy - pickY) * (qy - pickY);
      if (bestD < 0.f || d < bestD) {
        bestD = d;
        bestSeg = e2;
      }
    }
    *outPolySeg = bestSeg;
    return true;
  }
  case T::Arc:
    log.push_back("CHAMFER — 1 arc ignored: chamfer connects two straight curves only. Pick a line "
                  "or polyline segment.");
    return false;
  case T::Circle:
    log.push_back("CHAMFER — 1 circle ignored: chamfer connects two straight curves only. Pick a "
                  "line or polyline segment.");
    return false;
  default:
    log.push_back("CHAMFER — 1 object ignored: only a line or polyline segment can be chamfered.");
    return false;
  }
}

/// Case A, paper store: same-polyline adjacent segments — the paper equivalent of
/// `ApplyChamferPolylineCorner`.
static bool ApplyChamferPolylineCornerPaper(AppCommandState& st, PaperLayout* L, int pi, int edgeA, int edgeB,
                                            float pick1X, float pick1Y, float pick2X, float pick2Y,
                                            std::vector<std::string>& log) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L->paperPolyOffsets.size())
    return false;
  const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
  const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
  const int numVerts = v1 - v0;
  if (numVerts < 2)
    return false;
  const bool closed =
      static_cast<size_t>(pi) < L->paperPolyClosed.size() && L->paperPolyClosed[static_cast<size_t>(pi)];
  const int numEdges = closed ? numVerts : (numVerts - 1);
  if (edgeA < 0 || edgeA >= numEdges || edgeB < 0 || edgeB >= numEdges || edgeA == edgeB)
    return false;

  auto edgeVerts = [&](int e, int* viA, int* viB) {
    *viA = v0 + e;
    *viB = v0 + ((e + 1) % numVerts);
  };
  int aVi0 = 0, aVi1 = 0, bVi0 = 0, bVi1 = 0;
  edgeVerts(edgeA, &aVi0, &aVi1);
  edgeVerts(edgeB, &bVi0, &bVi1);
  int sharedVi = -1, otherAVi = -1;
  if (aVi0 == bVi0 || aVi0 == bVi1) {
    sharedVi = aVi0;
    otherAVi = aVi1;
  } else if (aVi1 == bVi0 || aVi1 == bVi1) {
    sharedVi = aVi1;
    otherAVi = aVi0;
  }
  if (sharedVi < 0) {
    log.push_back("CHAMFER — those two polyline segments are not adjacent; refused.");
    return false;
  }
  const int otherBVi = (bVi0 == sharedVi) ? bVi1 : bVi0;

  auto readVert = [&](int vi, float* x, float* y) {
    *x = L->paperPolyVerts[static_cast<size_t>(vi) * 3];
    *y = L->paperPolyVerts[static_cast<size_t>(vi) * 3 + 1];
  };
  float sharedX = 0.f, sharedY = 0.f, otherAX = 0.f, otherAY = 0.f, otherBX = 0.f, otherBY = 0.f;
  readVert(sharedVi, &sharedX, &sharedY);
  readVert(otherAVi, &otherAX, &otherAY);
  readVert(otherBVi, &otherBX, &otherBY);

  FilletCurve curveA{};
  curveA.isLine = true;
  curveA.ax = otherAX;
  curveA.ay = otherAY;
  curveA.bx = sharedX;
  curveA.by = sharedY;
  FilletCurve curveB{};
  curveB.isLine = true;
  curveB.ax = sharedX;
  curveB.ay = sharedY;
  curveB.bx = otherBX;
  curveB.by = otherBY;

  float px = 0.f, py = 0.f;
  if (!SolveFilletCenter(curveA, curveB, 0.f, pick1X, pick1Y, pick2X, pick2Y, &px, &py)) {
    log.push_back("CHAMFER — those two segments are parallel; no corner exists; refused.");
    return false;
  }
  float dAx = 0.f, dAy = 0.f, dBx = 0.f, dBy = 0.f;
  if (!ChamferResolvePoints(st, curveA, curveB, px, py, pick1X, pick1Y, pick2X, pick2Y, &dAx, &dAy, &dBx, &dBy)) {
    log.push_back("CHAMFER — that angle does not reach the second segment; refused.");
    return false;
  }

  const int sharedLocal = sharedVi - v0;
  const int otherALocal = otherAVi - v0;
  const bool aIsIncoming = (otherALocal == (sharedLocal - 1 + numVerts) % numVerts);
  const float inX = aIsIncoming ? dAx : dBx, inY = aIsIncoming ? dAy : dBy;
  const float outX = aIsIncoming ? dBx : dAx, outY = aIsIncoming ? dBy : dAy;
  const bool bothZero = (st.chamferMode == 0) ? (st.chamferDist1 < 1e-4f && st.chamferDist2 < 1e-4f)
                                              : (st.chamferDist1 < 1e-4f);

  std::vector<std::pair<float, float>> newXY;
  newXY.reserve(static_cast<size_t>(numVerts) + 1);
  for (int vi = v0; vi < v1; ++vi) {
    if (vi - v0 == sharedLocal) {
      if (bothZero) {
        newXY.push_back({px, py});
      } else {
        newXY.push_back({inX, inY});
        newXY.push_back({outX, outY});
      }
    } else {
      float x = 0.f, y = 0.f;
      readVert(vi, &x, &y);
      newXY.push_back({x, y});
    }
  }

  PushUndoSnapshot(st, "Chamfer paper geometry");
  ReplacePaperPolylineVerts(L, pi, newXY);
  if (!bothZero) {
    const size_t k = L->paperLines.size();
    L->paperLines.push_back(inX);
    L->paperLines.push_back(inY);
    L->paperLines.push_back(0.f);
    L->paperLines.push_back(outX);
    L->paperLines.push_back(outY);
    L->paperLines.push_back(0.f);
    L->paperLineAttrs.push_back(MakeNewEntityAttrs(st));
    (void)k;
  }
  BumpCadGpuCache(st);
  log.push_back(bothZero ? "CHAMFER — polyline corner trimmed to a point (distance 0)."
                        : "CHAMFER — polyline corner chamfered.");
  return true;
}

bool ApplyChamferToPaperEntities(AppCommandState& st, const PaperEntityRef& first, int firstPolySeg,
                                 float firstPickX, float firstPickY, const PaperEntityRef& second,
                                 int secondPolySeg, float pickX, float pickY, std::vector<std::string>& log) {
  PaperLayout* L = ActivePaperGeometryTarget(st);
  if (!L)
    return false;

  const bool sameEntity = (second.type == first.type && second.index == first.index);
  if (sameEntity && second.type != PaperEntityRef::Type::Polyline) {
    log.push_back("CHAMFER — select two different objects.");
    return false;
  }
  if (sameEntity && secondPolySeg == firstPolySeg) {
    log.push_back("CHAMFER — select two different segments.");
    return false;
  }

  if (sameEntity)
    return ApplyChamferPolylineCornerPaper(st, L, second.index, firstPolySeg, secondPolySeg, firstPickX,
                                           firstPickY, pickX, pickY, log);

  auto polylineIsEndSegmentOnly = [&](const PaperEntityRef& ref, int seg) -> bool {
    if (ref.type != PaperEntityRef::Type::Polyline)
      return true;
    const int pi = ref.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L->paperPolyOffsets.size())
      return false;
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    const bool closed =
        static_cast<size_t>(pi) < L->paperPolyClosed.size() && L->paperPolyClosed[static_cast<size_t>(pi)];
    if (closed) {
      log.push_back("CHAMFER — a closed polyline has no free end; only an adjacent segment of the SAME "
                    "polyline can be chamfered against it.");
      return false;
    }
    const int numEdges = (v1 - v0) - 1;
    if (seg != 0 && seg != numEdges - 1) {
      log.push_back("CHAMFER — only a polyline's own first or last segment can be chamfered against a "
                    "different object; an interior segment shares its vertex with a neighbor that would "
                    "be silently disturbed.");
      return false;
    }
    return true;
  };
  if (!polylineIsEndSegmentOnly(first, firstPolySeg) || !polylineIsEndSegmentOnly(second, secondPolySeg))
    return false;

  FilletCurve c1{}, c2{};
  if (!BuildFilletCurveFromPaperEntity(*L, first, firstPolySeg, &c1) ||
      !BuildFilletCurveFromPaperEntity(*L, second, secondPolySeg, &c2)) {
    log.push_back("CHAMFER — could not resolve the picked geometry; refused.");
    return false;
  }

  float px = 0.f, py = 0.f;
  if (!SolveFilletCenter(c1, c2, 0.f, firstPickX, firstPickY, pickX, pickY, &px, &py)) {
    log.push_back("CHAMFER — those two curves are parallel; no corner exists; refused.");
    return false;
  }
  float d1x = 0.f, d1y = 0.f, d2x = 0.f, d2y = 0.f;
  if (!ChamferResolvePoints(st, c1, c2, px, py, firstPickX, firstPickY, pickX, pickY, &d1x, &d1y, &d2x, &d2y)) {
    log.push_back("CHAMFER — that angle does not reach the second curve; refused.");
    return false;
  }

  PushUndoSnapshot(st, "Chamfer paper geometry");
  if (st.cornerTrimMode) {
    ApplyFilletTrimSingleToPaperEntity(L, first, firstPolySeg, d1x, d1y);
    ApplyFilletTrimSingleToPaperEntity(L, second, secondPolySeg, d2x, d2y);
  }
  const bool bothZero =
      (st.chamferMode == 0) ? (st.chamferDist1 < 1e-4f && st.chamferDist2 < 1e-4f) : (st.chamferDist1 < 1e-4f);
  if (!bothZero) {
    L->paperLines.push_back(d1x);
    L->paperLines.push_back(d1y);
    L->paperLines.push_back(0.f);
    L->paperLines.push_back(d2x);
    L->paperLines.push_back(d2y);
    L->paperLines.push_back(0.f);
    L->paperLineAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);
  log.push_back(bothZero ? "CHAMFER — corner trimmed to a point (distance 0)." : "CHAMFER — corner chamfered.");
  return true;
}
