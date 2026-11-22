#include "CadCommandsInternal.hpp"
#include "modify/CadOffsetInternal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static float ArcLengthOf(float r, float sweepRad) { return std::fabs(r * sweepRad); }

/// Sum of chord lengths across polyline `pi`'s segments. LENGTHEN never calls this on a closed
/// polyline (refused before this runs), so this always walks an open path — same shape as
/// QSELECT's ad hoc length filter (CadUi.cpp), written here as a reusable function instead.
static float PolylineOpenLengthOf(const AppCommandState& st, int pi) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
    return 0.f;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  float total = 0.f;
  for (int vi = v0; vi + 1 < v1; ++vi) {
    const size_t a = static_cast<size_t>(vi);
    const size_t b = (static_cast<size_t>(vi)+ 1);
    if (b >= st.drawing.vertexCount())
      break;
    // REQ-316 / ADR-047: a curved segment contributes its arc length, not its chord length.
    const float bulge = static_cast<size_t>(vi) < st.drawing.userPolylineVertsBulge.size()
                            ? st.drawing.userPolylineVertsBulge[static_cast<size_t>(vi)] : 0.f;
    total += static_cast<float>(BulgeSegmentLength(
        st.drawing.vertex(a).x, st.drawing.vertex(a).y, st.drawing.vertex(b).x,
        st.drawing.vertex(b).y, static_cast<double>(bulge)));
  }
  return total;
}

/// True when (px,py) is nearer (x0,y0) than (x1,y1) — the same squared-distance tie-break TRIM
/// uses to decide which end of a Line a pick is closest to (TrimSegmentIntersectPickSide's
/// `dA <= dB`), generalized: every two-endpoint entity LENGTHEN touches uses this one comparison,
/// with its two points computed inline per type (this codebase has no shared "get the endpoints of
/// this entity" helper — confirmed by research — so this follows the established idiom rather than
/// inventing one).
static bool NearerToFirstPoint(float px, float py, float x0, float y0, float x1, float y1) {
  const float d0 = (px - x0) * (px - x0) + (py - y0) * (py - y0);
  const float d1 = (px - x1) * (px - x1) + (py - y1) * (py - y1);
  return d0 <= d1;
}

/// Applies `newLength` to a Line, moving the end nearest the original pick (`nearFirst` selects
/// (x0,y0) vs (x1,y1)) and holding the other end fixed. Rejects — does not clamp — a result that
/// would collapse the line or an already-degenerate source (REQ-201: LENGTHEN must say why it did
/// nothing, the same convention TRIM's degenerate-length delete and OFFSET's refusals follow).
static bool ApplyLengthenToLine(AppCommandState& st, int index, bool nearFirst, float newLength,
                                std::vector<std::string>& log, bool pushUndo = true) {
  const size_t k = static_cast<size_t>(index);
  if (k >= st.drawing.lineCount())
    return false;
  const float x0 = static_cast<float>(st.drawing.line(k).x0), y0 = static_cast<float>(st.drawing.line(k).y0);
  const float x1 = static_cast<float>(st.drawing.line(k).x1), y1 = static_cast<float>(st.drawing.line(k).y1);
  const float fixedX = nearFirst ? x1 : x0, fixedY = nearFirst ? y1 : y0;
  const float movingX = nearFirst ? x0 : x1, movingY = nearFirst ? y0 : y1;
  const float dx = movingX - fixedX, dy = movingY - fixedY;
  const float curLen = std::hypot(dx, dy);
  if (curLen < 1e-9f) {
    log.push_back("LENGTHEN — the line is degenerate; there is no direction to extend along.");
    return false;
  }
  if (!(newLength > 1e-6f) || !std::isfinite(newLength)) {
    log.push_back("LENGTHEN — that change would collapse the line to zero length; refused.");
    return false;
  }
  const float ux = dx / curLen, uy = dy / curLen;
  const float newX = fixedX + ux * newLength, newY = fixedY + uy * newLength;
  // FILLET (REQ-103 step 6a) reuses this function twice (plus an Arc creation) as ONE atomic
  // fillet, and pushes its own single "Fillet" snapshot up front — pushUndo=false there suppresses
  // this function's own push so a fillet is one undo step, not three (the bug this parameter fixes,
  // caught by fillet-lines-basic.txt's own UNDO/REDO round-trip, TASK-102 task log).
  if (pushUndo)
    PushUndoSnapshot(st, "Lengthen");
  if (nearFirst) {
    st.drawing.lineComponent(k, CadLineComponent::X0) = newX;
    st.drawing.lineComponent(k, CadLineComponent::Y0) = newY;
  } else {
    st.drawing.lineComponent(k, CadLineComponent::X1) = newX;
    st.drawing.lineComponent(k, CadLineComponent::Y1) = newY;
  }
  return true;
}

/// Applies `newLength` to an Arc, holding its RADIUS fixed and changing only `startRad`/
/// `sweepRad` — deliberately NOT the model the existing Arc grip-drag (`ApplyEntityGripPoint`)
/// uses, which re-fits the radius on every drag; that is right for a free grip drag and wrong
/// here, where only the length along the arc may change. `nearFirst` selects the start endpoint
/// (true) or the end endpoint (false) as the one that moves; the other stays exactly fixed.
static bool ApplyLengthenToArc(AppCommandState& st, int index, bool nearFirst, float newLength,
                               std::vector<std::string>& log, bool pushUndo = true) {
  if (index < 0 || static_cast<size_t>(index) >= st.drawing.userArcs.size())
    return false;
  CadArc& a = st.drawing.userArcs[static_cast<size_t>(index)];
  if (a.r < 1e-6f) {
    log.push_back("LENGTHEN — the arc's radius is degenerate; there is no length to change.");
    return false;
  }
  if (!(newLength > 1e-6f) || !std::isfinite(newLength)) {
    log.push_back("LENGTHEN — that change would collapse the arc to zero length; refused.");
    return false;
  }
  constexpr float kTwoPi = 6.28318530717958647692f;
  const float newAbsSweep = static_cast<float>(newLength / a.r);
  if (newAbsSweep >= kTwoPi - 1e-4f) {
    log.push_back("LENGTHEN — that length would take the arc past a full circle; refused.");
    return false;
  }
  // Grows/shrinks in the sweep's OWN rotational sense (CW arcs have a negative sweepRad), so the
  // sign of the change always matches the sign of the existing sweep rather than assuming CCW.
  const float deltaTheta = std::copysign(newAbsSweep - std::fabs(a.sweepRad), a.sweepRad);
  if (pushUndo)  // see ApplyLengthenToLine's own comment on this parameter (FILLET, REQ-103 step 6a)
    PushUndoSnapshot(st, "Lengthen");
  if (nearFirst) {
    // Extending from the start: the END (start+sweep) must stay put, so the start absorbs the
    // whole angular change in the opposite sign while sweep grows by the same amount.
    a.startRad -= deltaTheta;
    a.sweepRad += deltaTheta;
  } else {
    // Extending from the end: the start stays put; the sweep alone grows toward the new end.
    a.sweepRad += deltaTheta;
  }
  return true;
}

/// Applies `newLength` (the polyline's new TOTAL length) by moving only the vertex adjacent to the
/// picked end along that terminal segment's own direction — the rest of the polyline is untouched.
/// This is AutoCAD's actual LENGTHEN behavior on a polyline (it never uniformly rescales the whole
/// thing), and it is also the only definition that keeps every OTHER vertex's position meaningful.
static bool ApplyLengthenToPolylineEnd(AppCommandState& st, int pi, bool nearFirst, float newLength,
                                       std::vector<std::string>& log, bool pushUndo = true) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
    return false;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  if (v1 - v0 < 2)
    return false;
  if (!(newLength > 1e-6f) || !std::isfinite(newLength)) {
    log.push_back("LENGTHEN — that change would collapse the polyline to zero length; refused.");
    return false;
  }
  const float curTotal = PolylineOpenLengthOf(st, pi);
  const float deltaLen = newLength - curTotal;
  const int movingVi = nearFirst ? v0 : (v1 - 1);
  const int fixedVi = nearFirst ? (v0 + 1) : (v1 - 2);
  const size_t mIdx = static_cast<size_t>(movingVi), fIdx = static_cast<size_t>(fixedVi);
  if (mIdx >= st.drawing.vertexCount() || fIdx >= st.drawing.vertexCount())
    return false;
  const float fx = static_cast<float>(st.drawing.vertex(fIdx).x), fy = static_cast<float>(st.drawing.vertex(fIdx).y);
  const float mx = static_cast<float>(st.drawing.vertex(mIdx).x), my = static_cast<float>(st.drawing.vertex(mIdx).y);
  const float segLen = std::hypot(mx - fx, my - fy);
  if (segLen < 1e-9f) {
    log.push_back("LENGTHEN — the polyline's end segment is degenerate; there is no direction to extend along.");
    return false;
  }
  const float newSegLen = segLen + deltaLen;
  if (!(newSegLen > 1e-6f)) {
    log.push_back("LENGTHEN — that change would collapse the polyline's end segment; refused.");
    return false;
  }
  const float ux = (mx - fx) / segLen, uy = (my - fy) / segLen;
  if (pushUndo)  // see ApplyLengthenToLine's own comment on this parameter (FILLET, REQ-103 step 6a)
    PushUndoSnapshot(st, "Lengthen");
  st.drawing.vertexComponent(mIdx, CadVertexComponent::X) = fx + ux * newSegLen;
  st.drawing.vertexComponent(mIdx, CadVertexComponent::Y) = fy + uy * newSegLen;
  return true;
}

/// Single choke point: dispatches by entity type, refusing anything not Line/Arc/open-Polyline

/// exists exactly once for the paper store (as it already does for the model store).
static void ApplyLengthToPaperEntityMutation(PaperLayout* L, const PaperEntityRef& ref, bool nearFirst, float curLen,
                                             float newLen) {
  switch (ref.type) {
  case PaperEntityRef::Type::Line: {
    const size_t k = static_cast<size_t>(ref.index) * 6;
    const float x0 = L->paperLines[k], y0 = L->paperLines[k + 1];
    const float x1 = L->paperLines[k + 3], y1 = L->paperLines[k + 4];
    const float fixedX = nearFirst ? x1 : x0, fixedY = nearFirst ? y1 : y0;
    const float movingX = nearFirst ? x0 : x1, movingY = nearFirst ? y0 : y1;
    const float dx = movingX - fixedX, dy = movingY - fixedY;
    const float ux = dx / curLen, uy = dy / curLen;
    const float nx = fixedX + ux * newLen, ny = fixedY + uy * newLen;
    if (nearFirst) { L->paperLines[k] = nx; L->paperLines[k + 1] = ny; }
    else { L->paperLines[k + 3] = nx; L->paperLines[k + 4] = ny; }
    break;
  }
  case PaperEntityRef::Type::Arc: {
    CadArc& a = L->paperArcs[static_cast<size_t>(ref.index)];
    const float newAbsSweep = static_cast<float>(newLen / a.r);
    const float deltaTheta = std::copysign(newAbsSweep - std::fabs(a.sweepRad), a.sweepRad);
    if (nearFirst) { a.startRad -= deltaTheta; a.sweepRad += deltaTheta; }
    else { a.sweepRad += deltaTheta; }
    break;
  }
  case PaperEntityRef::Type::Polyline: {
    const int pi = ref.index;
    const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
    const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
    const int movingVi = nearFirst ? v0 : (v1 - 1);
    const int fixedVi = nearFirst ? (v0 + 1) : (v1 - 2);
    const size_t mIdx = static_cast<size_t>(movingVi) * 3, fIdx = static_cast<size_t>(fixedVi) * 3;
    const float fx = L->paperPolyVerts[fIdx], fy = L->paperPolyVerts[fIdx + 1];
    const float mx = L->paperPolyVerts[mIdx], my = L->paperPolyVerts[mIdx + 1];
    const float segLen = std::hypot(mx - fx, my - fy);
    const float newSegLen = std::max(segLen + (newLen - curLen), 1e-6f);
    const float ux = (mx - fx) / std::max(segLen, 1e-9f), uy = (my - fy) / std::max(segLen, 1e-9f);
    L->paperPolyVerts[mIdx] = fx + ux * newSegLen;
    L->paperPolyVerts[mIdx + 1] = fy + uy * newSegLen;
    break;
  }
  default:
    break;
  }
}


// inside `namespace OffsetCmd`, so a using-declaration is the least-code way to name it here).
using OffsetCmd::ClosestPointOnSegment;


/// visible rather than implied.
struct PolyVert {
  float x = 0.f, y = 0.f, z = 0.f;
};

/// Rewrites polyline `pi`'s vertex range to `newVerts`, elevation included, shifting every later
/// polyline's CSR offsets by the length delta. Same technique OVERKILL's cleanup pass already uses
/// for the identical "this polyline's vertex count changed" problem (this file, the
/// LWPOLYLINE-cleanup block).
///
/// This used to write `0.f` into every Z. That was documented as matching LENGTHEN/EXTEND's 2D-only
/// treatment of endpoints — but those two never *rewrite* a vertex list, they move an endpoint in
/// place and leave its Z untouched, so the analogy did not hold and the effect was that BREAK,
/// FILLET and CHAMFER silently flattened any polyline they edited (issue 01).
static void ReplacePolylineVerts(AppCommandState& st, int pi, const std::vector<PolyVert>& newVerts) {
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  const int nNew = static_cast<int>(newVerts.size());
  const int delta = nNew - (v1 - v0);
  st.drawing.eraseVertexRange(static_cast<size_t>(v0), static_cast<size_t>(v1));
  std::vector<float> flat;
  flat.reserve(static_cast<size_t>(nNew) * 3);
  for (const auto& p : newVerts) {
    flat.push_back(p.x);
    flat.push_back(p.y);
    flat.push_back(p.z);
  }
  st.drawing.userPolylineVerts.insert(st.drawing.userPolylineVerts.begin() + static_cast<std::ptrdiff_t>(v0) * 3, flat.begin(),
                              flat.end());
  for (size_t oi = (static_cast<size_t>(pi)+ 1); oi < st.drawing.userPolylineOffsets.size(); ++oi)
    st.drawing.userPolylineOffsets[oi] += delta;
  SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());  // REQ-316 / ADR-047
  SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());  // REQ-325 / ADR-053
}

/// Reads polyline \p pi's vertices back out, elevation included — the collector every caller of
/// \ref ReplacePolylineVerts needs, so none of them re-derives the `* 3 + c` indexing.
static std::vector<PolyVert> PolylineVertsOf(const AppCommandState& st, int pi) {
  std::vector<PolyVert> out;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  out.reserve((static_cast<size_t>(v1)- v0));
  for (int vi = v0; vi < v1; ++vi) {
    const size_t o = static_cast<size_t>(vi);
    out.push_back({static_cast<float>(st.drawing.vertex(o).x),
                   static_cast<float>(st.drawing.vertex(o).y),
                   static_cast<float>(st.drawing.vertex(o).z)});
  }
  return out;
}


/// Paper equivalent of `ReplacePolylineVerts`.
static void ReplacePaperPolylineVerts(PaperLayout* L, int pi, const std::vector<std::pair<float, float>>& newXY) {
  const int v0 = L->paperPolyOffsets[static_cast<size_t>(pi)];
  const int v1 = L->paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
  const int nNew = static_cast<int>(newXY.size());
  const int delta = nNew - (v1 - v0);
  L->paperPolyVerts.erase(L->paperPolyVerts.begin() + static_cast<std::ptrdiff_t>(v0) * 3,
                          L->paperPolyVerts.begin() + static_cast<std::ptrdiff_t>(v1) * 3);
  std::vector<float> flat;
  flat.reserve(static_cast<size_t>(nNew) * 3);
  for (const auto& p : newXY) {
    flat.push_back(p.first);
    flat.push_back(p.second);
    flat.push_back(0.f);
  }
  L->paperPolyVerts.insert(L->paperPolyVerts.begin() + static_cast<std::ptrdiff_t>(v0) * 3, flat.begin(), flat.end());
  SyncPolylineBulge(L->paperPolyVertsBulge, L->paperPolyVerts.size());
  for (size_t oi = (static_cast<size_t>(pi)+ 1); oi < L->paperPolyOffsets.size(); ++oi)
    L->paperPolyOffsets[oi] += delta;
}
