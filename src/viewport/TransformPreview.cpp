#include "TransformPreview.hpp"

#include "CadCommands.hpp"
#include "geom2d.hpp"
#include "gizmooverlay.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

void rotatePreviewPt(float baseX, float baseY, float angleRad, float* inOutX, float* inOutY) {
  const float c = std::cos(angleRad);
  const float s = std::sin(angleRad);
  const float dx = *inOutX - baseX;
  const float dy = *inOutY - baseY;
  *inOutX = baseX + c * dx - s * dy;
  *inOutY = baseY + s * dx + c * dy;
}

void scalePreviewPt(float baseX, float baseY, float scale, float* inOutX, float* inOutY) {
  *inOutX = baseX + scale * (*inOutX - baseX);
  *inOutY = baseY + scale * (*inOutY - baseY);
}

/// REQ-103 MIRROR. Reflects (*inOutX,*inOutY) across the line through (x0,y0)-(x1,y1). Kept local
/// to this translation unit rather than shared with CadCommands.cpp's identical
/// \c ReflectPtAcrossLine — the same reason \c rotatePreviewPt above duplicates
/// \c RotateAroundBase instead of linking it: this file previews, it does not commit, and the two
/// must never accidentally share mutable state across a TU boundary.
void mirrorPreviewPt(float x0, float y0, float x1, float y1, float* inOutX, float* inOutY) {
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float len2 = dx * dx + dy * dy;
  if (len2 < 1e-12f)
    return;
  const float t = ((*inOutX - x0) * dx + (*inOutY - y0) * dy) / len2;
  const float px = x0 + t * dx;
  const float py = y0 + t * dy;
  *inOutX = 2.f * px - *inOutX;
  *inOutY = 2.f * py - *inOutY;
}

/// See \c ReflectAngleAcrossLine in CadCommands.cpp — same formula, same reason for the duplicate.
float mirrorPreviewAngle(float x0, float y0, float x1, float y1, float angle) {
  const float phi = std::atan2(y1 - y0, x1 - x0);
  return 2.f * phi - angle;
}

// Preview curves must be tessellated the way the renderer tessellates the committed ones, or the preview
// is a visibly different size from the object it previews. These mirror ViewportRenderer's segment counts
// (and its 8 / 16 floors); when the view is unknown they fall back to the old fixed counts.
float g_previewOrthoHalfH = -1.f;
int g_previewFbHeightPx = 0;
int g_previewSmoothnessCap = 20000;

/// Segment count for a curve of radius \p r at the current preview zoom.
[[nodiscard]] int previewCurveSegments(float r, int fallbackN) {
  if (g_previewOrthoHalfH > 0.f && g_previewFbHeightPx > 0)
    return std::max(8, CircleTessellationSegmentCount(static_cast<double>(r),
                                                      static_cast<double>(g_previewOrthoHalfH),
                                                      g_previewFbHeightPx, g_previewSmoothnessCap));
  return fallbackN;
}

void appendArcPolylineStrip(std::vector<float>* out, float z, const CadArc& a, int fallbackN) {
  const int n = previewCurveSegments(a.r, fallbackN);
  // A tilted arc (REQ-312) is walked in its own plane; every sample then carries its own Z, which
  // the flat AppendArcLineSegments cannot express -- it takes one elevation for the whole strip.
  if (!IsFlatNormal(a.nx, a.ny, a.nz)) {
    AppendCurveWorldSegs(*out, CurvePlane(a), static_cast<double>(a.r), static_cast<double>(a.startRad),
                         static_cast<double>(a.sweepRad), n);
    return;
  }
  AppendArcLineSegments(*out, static_cast<double>(a.cx), static_cast<double>(a.cy), static_cast<double>(a.r),
                        static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), n, z);
}

/// One circle into the preview buffers, in the plane it actually lies in (REQ-312).
///
/// A flat circle goes into the circle buffer exactly as before, so every existing preview is
/// unchanged. A tilted one is emitted as a tessellated strip into the LINE buffer instead: the
/// preview circle buffer is a (cx, cy, z, r) quad with nowhere to put a normal, and this is the
/// choice arcs have always made -- they have no circle buffer either and have always previewed as
/// strips. Adding a fifth to seventh float to the quad would touch every producer and consumer of
/// four separate preview buffers to carry a value that is +Z in every drawing that exists.
void appendPreviewCircle(std::vector<float>* prevLines, std::vector<float>* prevCircles, float cx, float cy,
                         float z, float r, float nx, float ny, float nz) {
  if (IsFlatNormal(nx, ny, nz)) {
    prevCircles->push_back(cx);
    prevCircles->push_back(cy);
    prevCircles->push_back(z);
    prevCircles->push_back(r);
    return;
  }
  constexpr double kTwoPiD = 6.283185307179586;
  AppendCurveWorldSegs(*prevLines,
                       CurvePlane(static_cast<double>(cx), static_cast<double>(cy), static_cast<double>(z),
                                  static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)),
                       static_cast<double>(r), 0.0, kTwoPiD, previewCurveSegments(r, 64));
}

void appendEllipsePolylineStrip(std::vector<float>* out, float z, const CadEllipse& el, int fallbackN) {
  int n = fallbackN;
  if (g_previewOrthoHalfH > 0.f && g_previewFbHeightPx > 0) {
    // Ellipses scale with the major semi-axis — the worst-case chord — as the renderer does.
    const double majLen = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    n = std::max(16, CircleTessellationSegmentCount(majLen, static_cast<double>(g_previewOrthoHalfH),
                                                    g_previewFbHeightPx, g_previewSmoothnessCap));
  }
  AppendEllipseLineSegments(*out, static_cast<double>(el.cx), static_cast<double>(el.cy),
                            static_cast<double>(el.majVx), static_cast<double>(el.majVy),
                            static_cast<double>(el.ratio), n, z);
}

// Draws a COMMITTED polyline, so it uses each vertex's own Z rather than a caller-supplied flat
// depth (REQ-057/058). A highlight or hover stroke drawn at a fixed Z would sit on the datum while
// the polyline itself sat at elevation, showing the object twice in an orbited view.
void appendCommittedPolylineStrip(std::vector<float>* out, const AppCommandState& cmd, int pi) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
    return;
  const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const bool closed =
      static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
  auto emit = [&](int a, int b) {
    const size_t A = static_cast<size_t>(a) * 3, B = static_cast<size_t>(b) * 3;
    const float za = cmd.userPolylineVerts[A + 2];
    // REQ-316 / ADR-047: a curved segment is traced as an arc, not a chord, so the selection /
    // hover highlight follows the shape the renderer drew.
    const float bulge =
        static_cast<size_t>(a) < cmd.userPolylineVertsBulge.size() ? cmd.userPolylineVertsBulge[static_cast<size_t>(a)] : 0.f;
    const BulgeArcSpan arc = (bulge != 0.f)
                                 ? BulgeArc(cmd.userPolylineVerts[A], cmd.userPolylineVerts[A + 1],
                                            cmd.userPolylineVerts[B], cmd.userPolylineVerts[B + 1],
                                            static_cast<double>(bulge))
                                 : BulgeArcSpan{};
    if (!arc.valid) {
      out->push_back(cmd.userPolylineVerts[A]);
      out->push_back(cmd.userPolylineVerts[A + 1]);
      out->push_back(za);
      out->push_back(cmd.userPolylineVerts[B]);
      out->push_back(cmd.userPolylineVerts[B + 1]);
      out->push_back(cmd.userPolylineVerts[B + 2]);
      return;
    }
    constexpr double kPi = 3.14159265358979323846;
    const int n = std::clamp(static_cast<int>(std::ceil(std::fabs(arc.sweep) / (kPi / 24.0))), 2, 96);
    double px = cmd.userPolylineVerts[A], py = cmd.userPolylineVerts[A + 1];
    for (int s = 1; s <= n; ++s) {
      const double u = arc.startAngle + arc.sweep * (static_cast<double>(s) / n);
      const double qx = arc.cx + arc.radius * std::cos(u);
      const double qy = arc.cy + arc.radius * std::sin(u);
      out->push_back(static_cast<float>(px));
      out->push_back(static_cast<float>(py));
      out->push_back(za);
      out->push_back(static_cast<float>(qx));
      out->push_back(static_cast<float>(qy));
      out->push_back(za);
      px = qx;
      py = qy;
    }
  };
  for (int vi = v0; vi + 1 < v1; ++vi)
    emit(vi, vi + 1);
  if (closed && v1 - v0 >= 2)
    emit(v1 - 1, v0);
}

/// Append every selected feature line as a preview strip, with \p xf applied to each vertex's plan
/// position and its own Z kept (REQ-057/058 — a preview drawn at a flat depth sits on the datum
/// while the object sits at elevation, showing it twice in an orbited view).
///
/// The three transform previews below — move/copy, rotate, scale — differ ONLY in \p xf. Sharing one
/// walk is the same reasoning as ForEachSelectedFeatureLine in CadCommands.cpp: a preview that
/// disagrees with what the command actually commits is worse than no preview, because it looks
/// right. REQ-087.
template <class Xf>
void appendFeatureLineStrip(std::vector<float>* out, const AppCommandState& cmd, int fi, Xf&& xf) {
  if (fi < 0 || static_cast<size_t>(fi + 1) >= cmd.featureLineOffsets.size())
    return;
  const int v0 = cmd.featureLineOffsets[static_cast<size_t>(fi)];
  const int v1 = cmd.featureLineOffsets[static_cast<size_t>(fi + 1)];
  if (v1 - v0 < 2 || static_cast<size_t>(v1) * 3 > cmd.featureLineVerts.size())
    return;
  const bool closed = static_cast<size_t>(fi) < cmd.featureLineClosed.size() &&
                      cmd.featureLineClosed[static_cast<size_t>(fi)];

  auto emit = [&](int a, int b) {
    const int idx[2] = {a, b};
    for (int k = 0; k < 2; ++k) {
      const size_t base = static_cast<size_t>(idx[k]) * 3;
      float x = cmd.featureLineVerts[base];
      float y = cmd.featureLineVerts[base + 1];
      xf(&x, &y);
      out->push_back(x);
      out->push_back(y);
      out->push_back(cmd.featureLineVerts[base + 2]);
    }
  };
  for (int vi = v0; vi + 1 < v1; ++vi)
    emit(vi, vi + 1);
  if (closed)
    emit(v1 - 1, v0);
}

template <class Xf>
void appendSelectedFeatureLinePreview(std::vector<float>* out, const AppCommandState& cmd, Xf&& xf) {
  for (const auto& e : cmd.selection)
    if (e.type == SelectedEntity::Type::FeatureLine)
      appendFeatureLineStrip(out, cmd, e.index, xf);
}

} // namespace

/// A small X drawn at a break point, sized from the current view so it stays the same apparent size
/// at any zoom. Falls back to a fixed world size when the view is unknown (headless, first frame).
void appendBreakPointMarker(std::vector<float>* out, float x, float y, float z, float orthoHalfH) {
  const float h = (orthoHalfH > 0.f) ? orthoHalfH * 0.012f : 1.f;
  out->push_back(x - h); out->push_back(y - h); out->push_back(z);
  out->push_back(x + h); out->push_back(y + h); out->push_back(z);
  out->push_back(x - h); out->push_back(y + h); out->push_back(z);
  out->push_back(x + h); out->push_back(y - h); out->push_back(z);
}

void appendPreviewSegment(std::vector<float>* out, float x0, float y0, float x1, float y1, float z) {
  out->push_back(x0); out->push_back(y0); out->push_back(z);
  out->push_back(x1); out->push_back(y1); out->push_back(z);
}

/// REQ-103 BREAK, live preview (TASK-101). Appends the material a break at (p1,p2) would REMOVE.
///
/// Every branch mirrors the ordering decision its committing counterpart in CadCommands.cpp makes,
/// because a preview that picks the other side of the entity is worse than no preview at all — it
/// tells the user confidently that the wrong half is about to disappear:
///   - open entities (Line, non-full Arc, open Polyline): the two break points are ordered by
///     position along the entity and the material BETWEEN them goes (ApplyBreakToLine/Arc/
///     OpenPolyline), so click order does not matter here either;
///   - closed entities (Circle, full-sweep Arc, closed Polyline): click order DOES matter. The
///     commit keeps the span running forward from point 2 to point 1 (CircleBreakStartSweep's
///     counterclockwise convention; stored vertex order for a closed polyline), so what is removed
///     is the complementary span running forward from point 1 to point 2.
void appendBreakRemovedSpan(std::vector<float>* out, const AppCommandState& cmd, const SelectedEntity& e,
                            const BreakPoint& p1, const BreakPoint& p2) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 5 >= cmd.userLinesFlat.size())
      return;
    // Both points already lie on the segment, so the removed material is simply the span between
    // them — no ordering needed.
    appendPreviewSegment(out, p1.x, p1.y, p2.x, p2.y, cmd.userLinesFlat[k + 2]);
    return;
  }
  case SelectedEntity::Type::Circle: {
    const size_t k = static_cast<size_t>(e.index) * 4;
    if (k + 3 >= cmd.userCirclesCxCyZR.size())
      return;
    CadArc removed{};
    removed.cx = cmd.userCirclesCxCyZR[k];
    removed.cy = cmd.userCirclesCxCyZR[k + 1];
    removed.z = cmd.userCirclesCxCyZR[k + 2];
    removed.r = cmd.userCirclesCxCyZR[k + 3];
    removed.startRad = p1.theta;
    float sweep = std::fmod(p2.theta - p1.theta, kTwoPi);
    if (sweep < 0.f)
      sweep += kTwoPi;  // a repeated pick leaves sweep 0 — "break at point" removes nothing
    removed.sweepRad = sweep;
    appendArcPolylineStrip(out, removed.z, removed, 64);
    return;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size())
      return;
    const CadArc& src = cmd.userArcs[static_cast<size_t>(e.index)];
    CadArc removed = src;
    if (std::fabs(std::fabs(src.sweepRad) - kTwoPi) < 1e-4f) {
      // A full sweep is a closed loop: it follows the Circle rule, not the arc-parameter one.
      removed.startRad = p1.theta;
      float sweep = std::fmod(p2.theta - p1.theta, kTwoPi);
      if (sweep < 0.f)
        sweep += kTwoPi;
      removed.sweepRad = sweep;
    } else {
      const float sgn = src.sweepRad >= 0.f ? 1.f : -1.f;
      const float nearP = std::min(p1.param, p2.param), farP = std::max(p1.param, p2.param);
      const float r = std::max(src.r, 1e-9f);
      removed.startRad = src.startRad + sgn * (nearP / r);
      removed.sweepRad = sgn * ((farP - nearP) / r);
    }
    appendArcPolylineStrip(out, removed.z, removed, 64);
    return;
  }
  case SelectedEntity::Type::Polyline: {
    const int pi = e.index;
    if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
      return;
    const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const int n = v1 - v0;
    if (n < 2 || static_cast<size_t>(v1 - 1) * 3 + 1 >= cmd.userPolylineVerts.size())
      return;
    auto vx = [&](int i) { return cmd.userPolylineVerts[static_cast<size_t>(v0 + i) * 3]; };
    auto vy = [&](int i) { return cmd.userPolylineVerts[static_cast<size_t>(v0 + i) * 3 + 1]; };
    const bool closed = static_cast<size_t>(pi) < cmd.userPolylineClosed.size() &&
                        cmd.userPolylineClosed[static_cast<size_t>(pi)] != 0u;
    constexpr float kZ = 0.f;  // ReplacePolylineVerts writes z 0; the preview matches it

    std::vector<std::pair<float, float>> span;
    if (!closed) {
      const bool p1First = p1.param <= p2.param;
      const BreakPoint& nearBp = p1First ? p1 : p2;
      const BreakPoint& farBp = p1First ? p2 : p1;
      span.push_back({nearBp.x, nearBp.y});
      for (int i = nearBp.segIndex + 1; i <= farBp.segIndex && i < n; ++i)
        span.push_back({vx(i), vy(i)});
      span.push_back({farBp.x, farBp.y});
    } else {
      // Ring parameters, exactly as ApplyBreakToClosedPolyline builds them.
      std::vector<float> vparam(static_cast<size_t>(n));
      float ringLen = 0.f;
      for (int i = 0; i < n; ++i) {
        vparam[static_cast<size_t>(i)] = ringLen;
        const int j = (i + 1) % n;
        ringLen += std::hypot(vx(j) - vx(i), vy(j) - vy(i));
      }
      if (ringLen < 1e-9f)
        return;
      // Forward FROM p1 — the complement of the span the commit keeps.
      auto rot = [&](float param) {
        float d = std::fmod(param - p1.param, ringLen);
        if (d < 0.f)
          d += ringLen;
        return d;
      };
      const bool samePoint = std::hypot(p1.x - p2.x, p1.y - p2.y) < 1e-4f;
      const float p2Rot = samePoint ? 0.f : rot(p2.param);
      span.push_back({p1.x, p1.y});
      for (int i = 0; i < n; ++i) {
        const float r = rot(vparam[static_cast<size_t>(i)]);
        if (r > 1e-6f && r < p2Rot - 1e-6f)
          span.push_back({vx(i), vy(i)});
      }
      span.push_back({p2.x, p2.y});
    }
    for (size_t i = 0; i + 1 < span.size(); ++i)
      appendPreviewSegment(out, span[i].first, span[i].second, span[i + 1].first, span[i + 1].second, kZ);
    return;
  }
  default:
    return;  // every other kind is refused by BREAK itself, with a stated reason
  }
}

void BuildBreakRemovalPreview(const AppCommandState& cmd, float curX, float curY, float orthoHalfHeightWorld,
                              std::vector<float>* outSpan, std::vector<float>* outMarkers) {
  if (outSpan)
    outSpan->clear();
  if (outMarkers)
    outMarkers->clear();
  if (cmd.active != AppCommandState::Kind::Break ||
      cmd.breakPhase != AppCommandState::BreakPhase::SelectSecondPoint)
    return;
  BreakPoint p2{};
  if (!ClosestPointOnEntity(cmd, cmd.breakEntity, curX, curY, &p2))
    return;
  // Arc spans tessellate against the view, exactly as the renderer tessellates committed arcs
  // (REQ-058) — set here rather than inherited from a previous BuildTransformPreview call, since
  // this function is now reached on its own.
  g_previewOrthoHalfH = orthoHalfHeightWorld;
  g_previewSmoothnessCap = std::clamp(cmd.displayArcCircleSmoothness, 8, 20000);
  std::vector<float> localSpan;
  std::vector<float>* span = outSpan ? outSpan : &localSpan;
  appendBreakRemovedSpan(span, cmd, cmd.breakEntity, cmd.breakP1, p2);
  if (outMarkers) {
    // Markers carry the span's own Z so they sit on the object, not on the datum — the same rule
    // AppendEntityHighlight follows, and for the same reason: at a tilted view a fixed Z draws the
    // marker somewhere the object is not.
    const float mz = span->size() >= 3 ? (*span)[2] : 0.f;
    appendBreakPointMarker(outMarkers, cmd.breakP1.x, cmd.breakP1.y, mz, orthoHalfHeightWorld);
    appendBreakPointMarker(outMarkers, p2.x, p2.y, mz, orthoHalfHeightWorld);
  }
}

void BuildTransformPreview(const AppCommandState& cmd, float curX, float curY, std::vector<float>* prevLines,
                           std::vector<float>* prevCircles, float orthoHalfHeightWorld, int framebufferHeightPx) {
  prevLines->clear();
  prevCircles->clear();
  g_previewOrthoHalfH = orthoHalfHeightWorld;
  g_previewFbHeightPx = framebufferHeightPx;
  g_previewSmoothnessCap = std::clamp(cmd.displayArcCircleSmoothness, 8, 20000);
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using OP = AppCommandState::OffsetPhase;
  using MirP = AppCommandState::MirrorPhase;

  if (cmd.active == K::Offset && cmd.offsetEntityValid &&
      (cmd.offsetPhase == OP::WaitDistanceOrThrough || cmd.offsetPhase == OP::WaitSidePick)) {
    CadOffsetAppendLivePreview(cmd, curX, curY, prevLines, prevCircles);
    return;
  }

  // REQ-103 BREAK's removal preview is deliberately NOT in this batch — see
  // BuildBreakRemovalPreview. This one is drawn translucent at ordinary line width, which is right
  // for a ghost of geometry somewhere it is not yet, and useless for material highlighted in place.

  if (cmd.active == K::Move || cmd.active == K::Copy) {
    if (cmd.modifyPhase != MP::NeedDestination)
      return;
    const float dx = curX - cmd.modifyBaseX;
    const float dy = curY - cmd.modifyBaseY;
    for (const auto& e : cmd.selection) {
      if (e.type == SelectedEntity::Type::LineSeg) {
        const size_t k = static_cast<size_t>(e.index) * 6;
        if (k + 5 >= cmd.userLinesFlat.size())
          continue;
        for (int i = 0; i < 2; ++i) {
          prevLines->push_back(cmd.userLinesFlat[k + i * 3] + dx);
          prevLines->push_back(cmd.userLinesFlat[k + i * 3 + 1] + dy);
          prevLines->push_back(cmd.userLinesFlat[k + i * 3 + 2]);  // keep the endpoint's elevation
        }
      } else if (e.type == SelectedEntity::Type::Circle) {
        const size_t k = static_cast<size_t>(e.index) * 4;  // cx,cy,z,r
        if (k + 3 >= cmd.userCirclesCxCyZR.size())
          continue;
        float cnx = kFlatNormalX;
        float cny = kFlatNormalY;
        float cnz = kFlatNormalZ;
        CircleNormalAt(cmd.userCircleNormals, k / 4, &cnx, &cny, &cnz);  // a translation cannot tilt a plane
        appendPreviewCircle(prevLines, prevCircles, cmd.userCirclesCxCyZR[k] + dx,
                            cmd.userCirclesCxCyZR[k + 1] + dy,
                            cmd.userCirclesCxCyZR[k + 2],  // z rides along unchanged
                            cmd.userCirclesCxCyZR[k + 3], cnx, cny, cnz);
      } else if (e.type == SelectedEntity::Type::Arc) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userArcs.size())
          continue;
        CadArc a = cmd.userArcs[k];
        a.cx += dx;
        a.cy += dy;
        appendArcPolylineStrip(prevLines, a.z, a, 48);
      } else if (e.type == SelectedEntity::Type::Ellipse) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userEllipses.size())
          continue;
        CadEllipse el = cmd.userEllipses[k];
        el.cx += dx;
        el.cy += dy;
        appendEllipsePolylineStrip(prevLines, el.z, el, 56);
      } else if (e.type == SelectedEntity::Type::Polyline) {
        const int pi = e.index;
        if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
          continue;
        const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
        const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
        const bool closed =
            static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
        for (int vi = v0; vi + 1 < v1; ++vi) {
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3)] + dx);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)] + dy);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)]);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)] + dx);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)] + dy);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 2)]);
        }
        if (closed && v1 - v0 >= 2) {
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)] + dx);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)] + dy);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 2)]);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3)] + dx);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)] + dy);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 2)]);
        }
      }
    }
    appendSelectedFeatureLinePreview(prevLines, cmd, [&](float* x, float* y) {  // REQ-087
      *x += dx;
      *y += dy;
    });
    return;
  }

  // REQ-305 ARRAY. Two local, per-instance append lambdas (translate / rotate-about-a-point) reuse
  // the exact per-type walks the Move/Copy block above and the Rotate block below already do —
  // looped once per grid cell / polar item instead of once, so this is the same coverage
  // (LineSeg/Circle/Arc/Ellipse/Polyline/FeatureLine) as every other command's own preview, not a
  // new abstraction.
  if (cmd.active == K::Array) {
    using APh = AppCommandState::ArrayPhase;

    auto appendTranslatedInstance = [&](float dx, float dy) {
      for (const auto& e : cmd.selection) {
        if (e.type == SelectedEntity::Type::LineSeg) {
          const size_t k = static_cast<size_t>(e.index) * 6;
          if (k + 5 >= cmd.userLinesFlat.size())
            continue;
          for (int i = 0; i < 2; ++i) {
            prevLines->push_back(cmd.userLinesFlat[k + i * 3] + dx);
            prevLines->push_back(cmd.userLinesFlat[k + i * 3 + 1] + dy);
            prevLines->push_back(cmd.userLinesFlat[k + i * 3 + 2]);
          }
        } else if (e.type == SelectedEntity::Type::Circle) {
          const size_t k = static_cast<size_t>(e.index) * 4;
          if (k + 3 >= cmd.userCirclesCxCyZR.size())
            continue;
          float cnx = kFlatNormalX;
          float cny = kFlatNormalY;
          float cnz = kFlatNormalZ;
          CircleNormalAt(cmd.userCircleNormals, k / 4, &cnx, &cny, &cnz);
          appendPreviewCircle(prevLines, prevCircles, cmd.userCirclesCxCyZR[k] + dx,
                              cmd.userCirclesCxCyZR[k + 1] + dy, cmd.userCirclesCxCyZR[k + 2],
                              cmd.userCirclesCxCyZR[k + 3], cnx, cny, cnz);
        } else if (e.type == SelectedEntity::Type::Arc) {
          const size_t k = static_cast<size_t>(e.index);
          if (k >= cmd.userArcs.size())
            continue;
          CadArc a = cmd.userArcs[k];
          a.cx += dx;
          a.cy += dy;
          appendArcPolylineStrip(prevLines, a.z, a, 48);
        } else if (e.type == SelectedEntity::Type::Ellipse) {
          const size_t k = static_cast<size_t>(e.index);
          if (k >= cmd.userEllipses.size())
            continue;
          CadEllipse el = cmd.userEllipses[k];
          el.cx += dx;
          el.cy += dy;
          appendEllipsePolylineStrip(prevLines, el.z, el, 56);
        } else if (e.type == SelectedEntity::Type::Polyline) {
          const int pi = e.index;
          if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
            continue;
          const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
          const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
          const bool closed = static_cast<size_t>(pi) < cmd.userPolylineClosed.size() &&
                              cmd.userPolylineClosed[static_cast<size_t>(pi)];
          for (int vi = v0; vi + 1 < v1; ++vi) {
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3)] + dx);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)] + dy);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)]);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)] + dx);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)] + dy);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 2)]);
          }
          if (closed && v1 - v0 >= 2) {
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)] + dx);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)] + dy);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 2)]);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3)] + dx);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)] + dy);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 2)]);
          }
        }
      }
      appendSelectedFeatureLinePreview(prevLines, cmd, [&](float* x, float* y) {
        *x += dx;
        *y += dy;
      });
    };

    auto appendRotatedInstance = [&](float bx, float by, float theta) {
      for (const auto& e : cmd.selection) {
        if (e.type == SelectedEntity::Type::LineSeg) {
          const size_t k = static_cast<size_t>(e.index) * 6;
          if (k + 5 >= cmd.userLinesFlat.size())
            continue;
          for (int i = 0; i < 2; ++i) {
            float x = cmd.userLinesFlat[k + i * 3];
            float y = cmd.userLinesFlat[k + i * 3 + 1];
            rotatePreviewPt(bx, by, theta, &x, &y);
            prevLines->push_back(x);
            prevLines->push_back(y);
            prevLines->push_back(cmd.userLinesFlat[k + i * 3 + 2]);
          }
        } else if (e.type == SelectedEntity::Type::Circle) {
          const size_t k = static_cast<size_t>(e.index) * 4;
          if (k + 3 >= cmd.userCirclesCxCyZR.size())
            continue;
          float x = cmd.userCirclesCxCyZR[k];
          float y = cmd.userCirclesCxCyZR[k + 1];
          rotatePreviewPt(bx, by, theta, &x, &y);
          float cnx = kFlatNormalX;
          float cny = kFlatNormalY;
          float cnz = kFlatNormalZ;
          CircleNormalAt(cmd.userCircleNormals, k / 4, &cnx, &cny, &cnz);
          RotateNormalAboutZ(theta, &cnx, &cny);  // the plane turns with the circle (REQ-312)
          appendPreviewCircle(prevLines, prevCircles, x, y, cmd.userCirclesCxCyZR[k + 2],
                              cmd.userCirclesCxCyZR[k + 3], cnx, cny, cnz);
        } else if (e.type == SelectedEntity::Type::Arc) {
          const size_t k = static_cast<size_t>(e.index);
          if (k >= cmd.userArcs.size())
            continue;
          CadArc a = cmd.userArcs[k];
          // The same three steps the commit takes (REQ-312): move the arc, turn its plane, then
          // re-anchor the sweep onto where the start point actually went. The ghost has to be the
          // shape the commit will produce, and on a tilted arc `startRad += theta` is not it.
          ray3d::Vec3 startPt = CurveWorldPointOnArc(a, static_cast<double>(a.startRad));
          float spx = static_cast<float>(startPt.x);
          float spy = static_cast<float>(startPt.y);
          rotatePreviewPt(bx, by, theta, &spx, &spy);
          startPt.x = static_cast<double>(spx);
          startPt.y = static_cast<double>(spy);
          rotatePreviewPt(bx, by, theta, &a.cx, &a.cy);
          a.startRad += theta;
          RotateNormalAboutZ(theta, &a.nx, &a.ny);
          CadReanchorArcStart(&a, startPt);
          appendArcPolylineStrip(prevLines, a.z, a, 48);
        } else if (e.type == SelectedEntity::Type::Ellipse) {
          const size_t k = static_cast<size_t>(e.index);
          if (k >= cmd.userEllipses.size())
            continue;
          CadEllipse el = cmd.userEllipses[k];
          float mx = el.cx + el.majVx;
          float my = el.cy + el.majVy;
          rotatePreviewPt(bx, by, theta, &el.cx, &el.cy);
          rotatePreviewPt(bx, by, theta, &mx, &my);
          el.majVx = mx - el.cx;
          el.majVy = my - el.cy;
          appendEllipsePolylineStrip(prevLines, el.z, el, 56);
        } else if (e.type == SelectedEntity::Type::Polyline) {
          const int pi = e.index;
          if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
            continue;
          const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
          const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
          const bool closed = static_cast<size_t>(pi) < cmd.userPolylineClosed.size() &&
                              cmd.userPolylineClosed[static_cast<size_t>(pi)];
          for (int vi = v0; vi + 1 < v1; ++vi) {
            float x0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3)];
            float y0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
            float x1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
            float y1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
            rotatePreviewPt(bx, by, theta, &x0, &y0);
            rotatePreviewPt(bx, by, theta, &x1, &y1);
            prevLines->push_back(x0);
            prevLines->push_back(y0);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)]);
            prevLines->push_back(x1);
            prevLines->push_back(y1);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 2)]);
          }
          if (closed && v1 - v0 >= 2) {
            float x0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
            float y0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
            float x1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3)];
            float y1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
            rotatePreviewPt(bx, by, theta, &x0, &y0);
            rotatePreviewPt(bx, by, theta, &x1, &y1);
            prevLines->push_back(x0);
            prevLines->push_back(y0);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 2)]);
            prevLines->push_back(x1);
            prevLines->push_back(y1);
            prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 2)]);
          }
        }
      }
      appendSelectedFeatureLinePreview(prevLines, cmd,
                                       [&](float* x, float* y) { rotatePreviewPt(bx, by, theta, x, y); });
    };

    if (cmd.arrayType == AppCommandState::ArrayType::Rectangular) {
      int cols = std::max(cmd.arrayCols, 1);
      float colSpacing = cmd.arrayColSpacing;
      int rows = 1;
      float rowSpacing = 0.f;
      if (cmd.arrayPhase == APh::Rect_WaitColumnSpacing) {
        colSpacing = curX - cmd.arrayAnchorX;
      } else if (cmd.arrayPhase == APh::Rect_WaitRows) {
        // cols/colSpacing already fixed; rows not chosen yet — preview the single fixed row.
      } else if (cmd.arrayPhase == APh::Rect_WaitRowSpacing) {
        rows = std::max(cmd.arrayRows, 1);
        rowSpacing = curY - cmd.arrayAnchorY;
      } else {
        return;  // PickSelection / WaitType / Rect_WaitColumns — not enough entered yet to preview
      }
      for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
          if (r == 0 && c == 0)
            continue;
          appendTranslatedInstance(static_cast<float>(c) * colSpacing, static_cast<float>(r) * rowSpacing);
        }
      return;
    }

    // Polar.
    if (cmd.arrayPhase != APh::Polar_WaitAngle && cmd.arrayPhase != APh::Polar_WaitRotateAnswer)
      return;
    constexpr float kPi = 3.14159265358979323846f;
    const int n = std::max(cmd.arrayItemCount, 1);
    float fillDeg = cmd.arrayFillAngleDeg;
    if (cmd.arrayPhase == APh::Polar_WaitAngle) {
      fillDeg = std::atan2(curY - cmd.arrayCenterY, curX - cmd.arrayCenterX) * (180.f / kPi);
      if (fillDeg < 0.f)
        fillDeg += 360.f;
    }
    const bool fullTurn = std::fabs(std::fmod(fillDeg, 360.f)) < 1e-3f;
    const float fillRad = fillDeg * (kPi / 180.f);
    const float step =
        (n <= 1) ? 0.f : (fullTurn ? fillRad / static_cast<float>(n) : fillRad / static_cast<float>(n - 1));
    for (int i = 1; i < n; ++i) {
      const float ang = step * static_cast<float>(i);
      if (cmd.arrayRotateItems) {
        appendRotatedInstance(cmd.arrayCenterX, cmd.arrayCenterY, ang);
      } else {
        float ax = cmd.arrayAnchorX, ay = cmd.arrayAnchorY;
        rotatePreviewPt(cmd.arrayCenterX, cmd.arrayCenterY, ang, &ax, &ay);
        appendTranslatedInstance(ax - cmd.arrayAnchorX, ay - cmd.arrayAnchorY);
      }
    }
    return;
  }

  if (cmd.active == K::Stretch && cmd.modifyPhase == MP::NeedDestination) {
    // Mirrors the Move/Copy block above, but each definition point is gated against the captured
    // box before moving — the live preview of the genuine stretch effect (REQ-103 step 5).
    const float dx = curX - cmd.modifyBaseX;
    const float dy = curY - cmd.modifyBaseY;
    const float mnX = cmd.stretchRectMnX, mxX = cmd.stretchRectMxX;
    const float mnY = cmd.stretchRectMnY, mxY = cmd.stretchRectMxY;
    auto inBox = [&](float x, float y) { return PointInsideClosedRect(x, y, mnX, mxX, mnY, mxY); };
    for (const auto& e : cmd.selection) {
      if (e.type == SelectedEntity::Type::LineSeg) {
        const size_t k = static_cast<size_t>(e.index) * 6;
        if (k + 5 >= cmd.userLinesFlat.size())
          continue;
        for (int i = 0; i < 2; ++i) {
          float x = cmd.userLinesFlat[k + i * 3], y = cmd.userLinesFlat[k + i * 3 + 1];
          if (inBox(x, y)) { x += dx; y += dy; }
          prevLines->push_back(x);
          prevLines->push_back(y);
          prevLines->push_back(cmd.userLinesFlat[k + i * 3 + 2]);
        }
      } else if (e.type == SelectedEntity::Type::Circle) {
        const size_t k = static_cast<size_t>(e.index) * 4;  // cx,cy,z,r
        if (k + 3 >= cmd.userCirclesCxCyZR.size())
          continue;
        float cx = cmd.userCirclesCxCyZR[k], cy = cmd.userCirclesCxCyZR[k + 1];
        if (inBox(cx, cy)) { cx += dx; cy += dy; }
        float cnx = kFlatNormalX;
        float cny = kFlatNormalY;
        float cnz = kFlatNormalZ;
        CircleNormalAt(cmd.userCircleNormals, k / 4, &cnx, &cny, &cnz);  // STRETCH moves, so the plane holds
        appendPreviewCircle(prevLines, prevCircles, cx, cy, cmd.userCirclesCxCyZR[k + 2],
                            cmd.userCirclesCxCyZR[k + 3], cnx, cny, cnz);
      } else if (e.type == SelectedEntity::Type::Arc) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userArcs.size())
          continue;
        CadArc a = cmd.userArcs[k];
        std::vector<std::string> scratch;  // preview never surfaces refusal reasons to the log
        StretchOneArc(a, mnX, mxX, mnY, mxY, dx, dy, scratch);
        appendArcPolylineStrip(prevLines, a.z, a, 48);
      } else if (e.type == SelectedEntity::Type::Ellipse) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userEllipses.size())
          continue;
        CadEllipse el = cmd.userEllipses[k];
        if (inBox(el.cx, el.cy)) { el.cx += dx; el.cy += dy; }
        appendEllipsePolylineStrip(prevLines, el.z, el, 56);
      } else if (e.type == SelectedEntity::Type::Polyline) {
        const int pi = e.index;
        if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
          continue;
        const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
        const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
        const bool closed =
            static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
        auto vert = [&](int vi, float* ox, float* oy, float* oz) {
          *ox = cmd.userPolylineVerts[static_cast<size_t>(vi * 3)];
          *oy = cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
          *oz = cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)];
          if (inBox(*ox, *oy)) { *ox += dx; *oy += dy; }
        };
        for (int vi = v0; vi + 1 < v1; ++vi) {
          float x, y, z;
          vert(vi, &x, &y, &z);
          prevLines->push_back(x); prevLines->push_back(y); prevLines->push_back(z);
          vert(vi + 1, &x, &y, &z);
          prevLines->push_back(x); prevLines->push_back(y); prevLines->push_back(z);
        }
        if (closed && v1 - v0 >= 2) {
          float x, y, z;
          vert(v1 - 1, &x, &y, &z);
          prevLines->push_back(x); prevLines->push_back(y); prevLines->push_back(z);
          vert(v0, &x, &y, &z);
          prevLines->push_back(x); prevLines->push_back(y); prevLines->push_back(z);
        }
      }
    }
    appendSelectedFeatureLinePreview(prevLines, cmd, [&](float* x, float* y) {  // REQ-087
      if (inBox(*x, *y)) {
        *x += dx;
        *y += dy;
      }
    });
    return;
  }

  if (cmd.active == K::Paste && cmd.modifyPhase == MP::NeedDestination) {
    const float dx = curX - cmd.modifyBaseX;
    const float dy = curY - cmd.modifyBaseY;
    const CadClipboard& cb = cmd.clipboard;
    // Lines
    for (size_t i = 0; i + 5 < cb.lines.size() + 1; i += 6) {
      prevLines->push_back(cb.lines[i + 0] + dx);
      prevLines->push_back(cb.lines[i + 1] + dy);
      prevLines->push_back(cb.lines[i + 2]);
      prevLines->push_back(cb.lines[i + 3] + dx);
      prevLines->push_back(cb.lines[i + 4] + dy);
      prevLines->push_back(cb.lines[i + 5]);
    }
    // Circles
    for (size_t i = 0; i + 3 < cb.circlesCxCyZR.size() + 1; i += 4) {  // cx,cy,z,r
      float cnx = kFlatNormalX;
      float cny = kFlatNormalY;
      float cnz = kFlatNormalZ;
      CircleNormalAt(cb.circleNormals, i / 4, &cnx, &cny, &cnz);
      appendPreviewCircle(prevLines, prevCircles, cb.circlesCxCyZR[i + 0] + dx, cb.circlesCxCyZR[i + 1] + dy,
                          cb.circlesCxCyZR[i + 2], cb.circlesCxCyZR[i + 3], cnx, cny, cnz);
    }
    // Arcs
    for (const auto& a : cb.arcs) {
      CadArc pa = a;
      pa.cx += dx;
      pa.cy += dy;
      appendArcPolylineStrip(prevLines, pa.z, pa, 48);
    }
    // Ellipses
    for (const auto& el : cb.ellipses) {
      CadEllipse pe = el;
      pe.cx += dx;
      pe.cy += dy;
      appendEllipsePolylineStrip(prevLines, pe.z, pe, 56);
    }
    // Polylines
    const int nPoly = static_cast<int>(cb.polyOffsets.size()) - 1;
    for (int pi = 0; pi < nPoly; ++pi) {
      const int v0 = cb.polyOffsets[static_cast<size_t>(pi)];
      const int v1 = cb.polyOffsets[static_cast<size_t>(pi + 1)];
      const bool closed =
          static_cast<size_t>(pi) < cb.polyClosed.size() && cb.polyClosed[static_cast<size_t>(pi)];
      for (int vi = v0; vi + 1 < v1; ++vi) {
        prevLines->push_back(cb.polyVerts[static_cast<size_t>(vi * 3 + 0)] + dx);
        prevLines->push_back(cb.polyVerts[static_cast<size_t>(vi * 3 + 1)] + dy);
        prevLines->push_back(cb.polyVerts[static_cast<size_t>(vi * 3 + 2)]);
        prevLines->push_back(cb.polyVerts[static_cast<size_t>((vi + 1) * 3 + 0)] + dx);
        prevLines->push_back(cb.polyVerts[static_cast<size_t>((vi + 1) * 3 + 1)] + dy);
        prevLines->push_back(cb.polyVerts[static_cast<size_t>((vi + 1) * 3 + 2)]);
      }
      if (closed && v1 - v0 >= 2) {
        prevLines->push_back(cb.polyVerts[static_cast<size_t>((v1 - 1) * 3 + 0)] + dx);
        prevLines->push_back(cb.polyVerts[static_cast<size_t>((v1 - 1) * 3 + 1)] + dy);
        prevLines->push_back(cb.polyVerts[static_cast<size_t>((v1 - 1) * 3 + 2)]);
        prevLines->push_back(cb.polyVerts[static_cast<size_t>(v0 * 3 + 0)] + dx);
        prevLines->push_back(cb.polyVerts[static_cast<size_t>(v0 * 3 + 1)] + dy);
        prevLines->push_back(cb.polyVerts[static_cast<size_t>(v0 * 3 + 2)]);
      }
    }
    return;
  }

  if (cmd.active == K::Scale && cmd.modifyPhase == MP::NeedDestination) {
    using SP = AppCommandState::ScalePhase;
    if (cmd.scalePhase == SP::Ref_WaitP2) {
      prevLines->push_back(cmd.scaleRefP1X);
      prevLines->push_back(cmd.scaleRefP1Y);
      // Reference/measure rubber: drawn on the work plane, since it marks distances the user is
      // picking rather than geometry belonging to an object (REQ-058).
      prevLines->push_back(CadCommitElevation(cmd));
      prevLines->push_back(curX);
      prevLines->push_back(curY);
      prevLines->push_back(CadCommitElevation(cmd));
      return;
    }
    if (cmd.scalePhase == SP::NewLength_WaitP2) {
      prevLines->push_back(cmd.scaleNewLenP1X);
      prevLines->push_back(cmd.scaleNewLenP1Y);
      prevLines->push_back(CadCommitElevation(cmd));
      prevLines->push_back(curX);
      prevLines->push_back(curY);
      prevLines->push_back(CadCommitElevation(cmd));
    }
    float sc = 1.f;
    if (!CadScalePreviewFactor(cmd, curX, curY, &sc))
      return;
    const float bx = cmd.modifyBaseX;
    const float by = cmd.modifyBaseY;
    for (const auto& e : cmd.selection) {
      if (e.type == SelectedEntity::Type::LineSeg) {
        const size_t k = static_cast<size_t>(e.index) * 6;
        if (k + 5 >= cmd.userLinesFlat.size())
          continue;
        for (int i = 0; i < 2; ++i) {
          float x = cmd.userLinesFlat[k + i * 3];
          float y = cmd.userLinesFlat[k + i * 3 + 1];
          scalePreviewPt(bx, by, sc, &x, &y);
          prevLines->push_back(x);
          prevLines->push_back(y);
          prevLines->push_back(cmd.userLinesFlat[k + i * 3 + 2]);
        }
      } else if (e.type == SelectedEntity::Type::Circle) {
        const size_t k = static_cast<size_t>(e.index) * 4;  // cx,cy,z,r
        if (k + 3 >= cmd.userCirclesCxCyZR.size())
          continue;
        float x = cmd.userCirclesCxCyZR[k];
        float y = cmd.userCirclesCxCyZR[k + 1];
        float r = cmd.userCirclesCxCyZR[k + 3];
        scalePreviewPt(bx, by, sc, &x, &y);
        r *= sc;
        float cnx = kFlatNormalX;
        float cny = kFlatNormalY;
        float cnz = kFlatNormalZ;
        CircleNormalAt(cmd.userCircleNormals, k / 4, &cnx, &cny, &cnz);  // a uniform scale keeps the normal
        appendPreviewCircle(prevLines, prevCircles, x, y,
                            cmd.userCirclesCxCyZR[k + 2],  // SCALE is planar here - z unscaled
                            r, cnx, cny, cnz);
      } else if (e.type == SelectedEntity::Type::Arc) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userArcs.size())
          continue;
        CadArc a = cmd.userArcs[k];
        scalePreviewPt(bx, by, sc, &a.cx, &a.cy);
        a.r *= sc;
        appendArcPolylineStrip(prevLines, a.z, a, 48);
      } else if (e.type == SelectedEntity::Type::Ellipse) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userEllipses.size())
          continue;
        CadEllipse el = cmd.userEllipses[k];
        float mx = el.cx + el.majVx;
        float my = el.cy + el.majVy;
        scalePreviewPt(bx, by, sc, &el.cx, &el.cy);
        scalePreviewPt(bx, by, sc, &mx, &my);
        el.majVx = mx - el.cx;
        el.majVy = my - el.cy;
        appendEllipsePolylineStrip(prevLines, el.z, el, 56);
      } else if (e.type == SelectedEntity::Type::Polyline) {
        const int pi = e.index;
        if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
          continue;
        const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
        const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
        const bool closed =
            static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
        for (int vi = v0; vi + 1 < v1; ++vi) {
          float x0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3)];
          float y0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
          float x1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
          float y1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
          scalePreviewPt(bx, by, sc, &x0, &y0);
          scalePreviewPt(bx, by, sc, &x1, &y1);
          prevLines->push_back(x0);
          prevLines->push_back(y0);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)]);
          prevLines->push_back(x1);
          prevLines->push_back(y1);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 2)]);
        }
        if (closed && v1 - v0 >= 2) {
          float x0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
          float y0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
          float x1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3)];
          float y1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
          scalePreviewPt(bx, by, sc, &x0, &y0);
          scalePreviewPt(bx, by, sc, &x1, &y1);
          prevLines->push_back(x0);
          prevLines->push_back(y0);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 2)]);
          prevLines->push_back(x1);
          prevLines->push_back(y1);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 2)]);
        }
      }
    }
    appendSelectedFeatureLinePreview(  // REQ-087
        prevLines, cmd, [&](float* x, float* y) { scalePreviewPt(bx, by, sc, x, y); });
    return;
  }

  if (cmd.active == K::Mirror) {
    if (cmd.mirrorPhase != MirP::NeedP2 && cmd.mirrorPhase != MirP::NeedEraseAnswer)
      return;
    const float x0 = cmd.mirrorP1X, y0 = cmd.mirrorP1Y;
    const float x1 = (cmd.mirrorPhase == MirP::NeedP2) ? curX : cmd.mirrorP2X;
    const float y1 = (cmd.mirrorPhase == MirP::NeedP2) ? curY : cmd.mirrorP2Y;
    // The mirror line itself, so the user sees what they are reflecting across — same rubber-line
    // convention SCALE uses for its reference/new-length segments above.
    prevLines->push_back(x0);
    prevLines->push_back(y0);
    prevLines->push_back(CadCommitElevation(cmd));
    prevLines->push_back(x1);
    prevLines->push_back(y1);
    prevLines->push_back(CadCommitElevation(cmd));

    for (const auto& e : cmd.selection) {
      if (e.type == SelectedEntity::Type::LineSeg) {
        const size_t k = static_cast<size_t>(e.index) * 6;
        if (k + 5 >= cmd.userLinesFlat.size())
          continue;
        for (int i = 0; i < 2; ++i) {
          float x = cmd.userLinesFlat[k + i * 3];
          float y = cmd.userLinesFlat[k + i * 3 + 1];
          mirrorPreviewPt(x0, y0, x1, y1, &x, &y);
          prevLines->push_back(x);
          prevLines->push_back(y);
          prevLines->push_back(cmd.userLinesFlat[k + i * 3 + 2]);
        }
      } else if (e.type == SelectedEntity::Type::Circle) {
        const size_t k = static_cast<size_t>(e.index) * 4;  // cx,cy,z,r
        if (k + 3 >= cmd.userCirclesCxCyZR.size())
          continue;
        float x = cmd.userCirclesCxCyZR[k];
        float y = cmd.userCirclesCxCyZR[k + 1];
        mirrorPreviewPt(x0, y0, x1, y1, &x, &y);
        float cnx = kFlatNormalX;
        float cny = kFlatNormalY;
        float cnz = kFlatNormalZ;
        CircleNormalAt(cmd.userCircleNormals, k / 4, &cnx, &cny, &cnz);
        ReflectNormalAcrossLine(x0, y0, x1, y1, &cnx, &cny);  // the plane is mirrored too (REQ-312)
        appendPreviewCircle(prevLines, prevCircles, x, y, cmd.userCirclesCxCyZR[k + 2],
                            cmd.userCirclesCxCyZR[k + 3],  // radius preserved (isometry)
                            cnx, cny, cnz);
      } else if (e.type == SelectedEntity::Type::Arc) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userArcs.size())
          continue;
        CadArc a = cmd.userArcs[k];
        // Same reflect-the-old-end-angle-into-the-new-start rule as the committed path
        // (CadCommands.cpp's DuplicateCadSelectionReflected) — a reflection reverses handedness.
        ray3d::Vec3 farEnd = CurveWorldPointOnArc(a, static_cast<double>(a.startRad) +
                                                         static_cast<double>(a.sweepRad));
        float fex = static_cast<float>(farEnd.x);
        float fey = static_cast<float>(farEnd.y);
        mirrorPreviewPt(x0, y0, x1, y1, &fex, &fey);
        farEnd.x = static_cast<double>(fex);
        farEnd.y = static_cast<double>(fey);
        const float newStart = mirrorPreviewAngle(x0, y0, x1, y1, a.startRad + a.sweepRad);
        mirrorPreviewPt(x0, y0, x1, y1, &a.cx, &a.cy);
        a.startRad = newStart;
        ReflectNormalAcrossLine(x0, y0, x1, y1, &a.nx, &a.ny);  // REQ-312, as the commit does
        CadReanchorArcStart(&a, farEnd);
        appendArcPolylineStrip(prevLines, a.z, a, 48);
      } else if (e.type == SelectedEntity::Type::Ellipse) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userEllipses.size())
          continue;
        CadEllipse el = cmd.userEllipses[k];
        float mx = el.cx + el.majVx;
        float my = el.cy + el.majVy;
        mirrorPreviewPt(x0, y0, x1, y1, &el.cx, &el.cy);
        mirrorPreviewPt(x0, y0, x1, y1, &mx, &my);
        el.majVx = mx - el.cx;
        el.majVy = my - el.cy;
        appendEllipsePolylineStrip(prevLines, el.z, el, 56);
      } else if (e.type == SelectedEntity::Type::Polyline) {
        const int pi = e.index;
        if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
          continue;
        const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
        const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
        const bool closed =
            static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
        for (int vi = v0; vi + 1 < v1; ++vi) {
          float px0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3)];
          float py0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
          float px1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
          float py1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
          mirrorPreviewPt(x0, y0, x1, y1, &px0, &py0);
          mirrorPreviewPt(x0, y0, x1, y1, &px1, &py1);
          prevLines->push_back(px0);
          prevLines->push_back(py0);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)]);
          prevLines->push_back(px1);
          prevLines->push_back(py1);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 2)]);
        }
        if (closed && v1 - v0 >= 2) {
          float px0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
          float py0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
          float px1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3)];
          float py1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
          mirrorPreviewPt(x0, y0, x1, y1, &px0, &py0);
          mirrorPreviewPt(x0, y0, x1, y1, &px1, &py1);
          prevLines->push_back(px0);
          prevLines->push_back(py0);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 2)]);
          prevLines->push_back(px1);
          prevLines->push_back(py1);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 2)]);
        }
      }
    }
    appendSelectedFeatureLinePreview(  // REQ-087
        prevLines, cmd, [&](float* x, float* y) { mirrorPreviewPt(x0, y0, x1, y1, x, y); });
    return;
  }

  if (cmd.active == K::Lengthen) {
    using LenP = AppCommandState::LengthenPhase;
    if (cmd.lengthenPhase != LenP::WaitDynamicTarget)
      return;
    const SelectedEntity& e = cmd.lengthenPendingEntity;
    const bool nearFirst = cmd.lengthenPendingNearFirst;
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size())
        return;
      const float x0 = cmd.userLinesFlat[k], y0 = cmd.userLinesFlat[k + 1], z0 = cmd.userLinesFlat[k + 2];
      const float x1 = cmd.userLinesFlat[k + 3], y1 = cmd.userLinesFlat[k + 4], z1 = cmd.userLinesFlat[k + 5];
      const float fixedX = nearFirst ? x1 : x0, fixedY = nearFirst ? y1 : y0, fixedZ = nearFirst ? z1 : z0;
      const float movingZ = nearFirst ? z0 : z1;
      const float dx = (nearFirst ? x0 : x1) - fixedX, dy = (nearFirst ? y0 : y1) - fixedY;
      const float curLen = std::hypot(dx, dy);
      if (curLen < 1e-9f)
        return;
      const float ux = dx / curLen, uy = dy / curLen;
      const float proj = std::max((curX - fixedX) * ux + (curY - fixedY) * uy, 1e-6f);
      prevLines->push_back(fixedX);
      prevLines->push_back(fixedY);
      prevLines->push_back(fixedZ);
      prevLines->push_back(fixedX + ux * proj);
      prevLines->push_back(fixedY + uy * proj);
      prevLines->push_back(movingZ);
      return;
    }
    if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
        return;
      const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      if (v1 - v0 < 2)
        return;
      const int movingVi = nearFirst ? v0 : (v1 - 1);
      const int fixedVi = nearFirst ? (v0 + 1) : (v1 - 2);
      const size_t mIdx = static_cast<size_t>(movingVi) * 3, fIdx = static_cast<size_t>(fixedVi) * 3;
      if (mIdx + 2 >= cmd.userPolylineVerts.size() || fIdx + 2 >= cmd.userPolylineVerts.size())
        return;
      const float fx = cmd.userPolylineVerts[fIdx], fy = cmd.userPolylineVerts[fIdx + 1];
      const float mz = cmd.userPolylineVerts[mIdx + 2];
      const float mx = cmd.userPolylineVerts[mIdx], my = cmd.userPolylineVerts[mIdx + 1];
      const float segLen = std::hypot(mx - fx, my - fy);
      if (segLen < 1e-9f)
        return;
      const float ux = (mx - fx) / segLen, uy = (my - fy) / segLen;
      const float proj = std::max((curX - fx) * ux + (curY - fy) * uy, 1e-6f);
      prevLines->push_back(fx);
      prevLines->push_back(fy);
      prevLines->push_back(cmd.userPolylineVerts[fIdx + 2]);
      prevLines->push_back(fx + ux * proj);
      prevLines->push_back(fy + uy * proj);
      prevLines->push_back(mz);
      return;
    }
    if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size())
        return;
      CadArc a = cmd.userArcs[static_cast<size_t>(e.index)];
      if (a.r < 1e-6f)
        return;
      const float pickAngle = std::atan2(curY - a.cy, curX - a.cx);
      const float fixedAngle = nearFirst ? (a.startRad + a.sweepRad) : a.startRad;
      float delta = pickAngle - fixedAngle;
      constexpr float kPi = 3.14159265358979323846f;
      while (delta > kPi) delta -= 2.f * kPi;
      while (delta < -kPi) delta += 2.f * kPi;
      const float newAbsSweep = std::fabs(delta);
      const float deltaTheta = std::copysign(newAbsSweep - std::fabs(a.sweepRad), a.sweepRad);
      if (nearFirst) {
        a.startRad -= deltaTheta;
        a.sweepRad += deltaTheta;
      } else {
        a.sweepRad += deltaTheta;
      }
      appendArcPolylineStrip(prevLines, a.z, a, 48);
      return;
    }
    return;
  }

  if (cmd.active != K::Rotate)
    return;

  float theta = 0.f;
  if (!CadRotatePreviewTheta(cmd, curX, curY, &theta))
    return;

  const float bx = cmd.rotateBaseX;
  const float by = cmd.rotateBaseY;
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size())
        continue;
      for (int i = 0; i < 2; ++i) {
        float x = cmd.userLinesFlat[k + i * 3];
        float y = cmd.userLinesFlat[k + i * 3 + 1];
        rotatePreviewPt(bx, by, theta, &x, &y);
        prevLines->push_back(x);
        prevLines->push_back(y);
        prevLines->push_back(cmd.userLinesFlat[k + i * 3 + 2]);
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 4;  // cx,cy,z,r
      if (k + 3 >= cmd.userCirclesCxCyZR.size())
        continue;
      float x = cmd.userCirclesCxCyZR[k];
      float y = cmd.userCirclesCxCyZR[k + 1];
      rotatePreviewPt(bx, by, theta, &x, &y);
      float cnx = kFlatNormalX;
      float cny = kFlatNormalY;
      float cnz = kFlatNormalZ;
      CircleNormalAt(cmd.userCircleNormals, k / 4, &cnx, &cny, &cnz);
      RotateNormalAboutZ(theta, &cnx, &cny);  // rotation is about the Z axis, and so is the normal's
      appendPreviewCircle(prevLines, prevCircles, x, y,
                          cmd.userCirclesCxCyZR[k + 2],  // rotation is about the Z axis
                          cmd.userCirclesCxCyZR[k + 3], cnx, cny, cnz);
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userArcs.size())
        continue;
      CadArc a = cmd.userArcs[k];
      rotatePreviewPt(bx, by, theta, &a.cx, &a.cy);
      a.startRad += theta;
      appendArcPolylineStrip(prevLines, a.z, a, 48);
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userEllipses.size())
        continue;
      CadEllipse el = cmd.userEllipses[k];
      float mx = el.cx + el.majVx;
      float my = el.cy + el.majVy;
      rotatePreviewPt(bx, by, theta, &el.cx, &el.cy);
      rotatePreviewPt(bx, by, theta, &mx, &my);
      el.majVx = mx - el.cx;
      el.majVy = my - el.cy;
      appendEllipsePolylineStrip(prevLines, el.z, el, 56);
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
        continue;
      const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const bool closed =
          static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
      for (int vi = v0; vi + 1 < v1; ++vi) {
        float x0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3)];
        float y0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
        float x1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
        float y1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
        rotatePreviewPt(bx, by, theta, &x0, &y0);
        rotatePreviewPt(bx, by, theta, &x1, &y1);
        prevLines->push_back(x0);
        prevLines->push_back(y0);
        prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 2)]);
        prevLines->push_back(x1);
        prevLines->push_back(y1);
        prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 2)]);
      }
      if (closed && v1 - v0 >= 2) {
        float x0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
        float y0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
        float x1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3)];
        float y1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
        rotatePreviewPt(bx, by, theta, &x0, &y0);
        rotatePreviewPt(bx, by, theta, &x1, &y1);
        prevLines->push_back(x0);
        prevLines->push_back(y0);
        prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 2)]);
        prevLines->push_back(x1);
        prevLines->push_back(y1);
        prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 2)]);
      }
    }
  }
  appendSelectedFeatureLinePreview(  // REQ-087
      prevLines, cmd, [&](float* x, float* y) { rotatePreviewPt(bx, by, theta, x, y); });
}

/// Highlight strokes for one entity, each at the entity's OWN elevation (REQ-058). This used to
/// take a flat `lineZ` draw depth; with the filled region converted, no type needs one — depth
/// testing is off, so draw ORDER decides occlusion and the Z is free to mean elevation.
static void AppendEntityHighlight(const AppCommandState& cmd, const SelectedEntity& e,
                                  std::vector<float>* hlLines, std::vector<float>* hlCircles) {
  if (e.type == SelectedEntity::Type::LineSeg) {
    const size_t k = static_cast<size_t>(e.index) * 6;
    if (k + 5 >= cmd.userLinesFlat.size())
      return;
    for (int i = 0; i < 2; ++i) {
      hlLines->push_back(cmd.userLinesFlat[k + i * 3]);
      hlLines->push_back(cmd.userLinesFlat[k + i * 3 + 1]);
      // The entity's OWN Z, not a flat overlay depth (REQ-057/058). Drawing the highlight at a
      // fixed Z put it on the datum while the line sat at its elevation, so an orbited view showed
      // the object twice — once real, once highlighted in the wrong place.
      hlLines->push_back(cmd.userLinesFlat[k + i * 3 + 2]);
    }
  } else if (e.type == SelectedEntity::Type::Circle) {
    const size_t k = static_cast<size_t>(e.index) * 4;  // cx,cy,z,r
    if (k + 3 >= cmd.userCirclesCxCyZR.size())
      return;
    // Through the same emitter the previews use, so a selected tilted circle is highlighted on the
    // plane it is drawn on rather than ringed flat beside itself (REQ-312).
    float cnx = kFlatNormalX;
    float cny = kFlatNormalY;
    float cnz = kFlatNormalZ;
    CircleNormalAt(cmd.userCircleNormals, k / 4, &cnx, &cny, &cnz);
    appendPreviewCircle(hlLines, hlCircles, cmd.userCirclesCxCyZR[k], cmd.userCirclesCxCyZR[k + 1],
                        cmd.userCirclesCxCyZR[k + 2], cmd.userCirclesCxCyZR[k + 3], cnx, cny, cnz);
  } else if (e.type == SelectedEntity::Type::Arc) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= cmd.userArcs.size())
      return;
    appendArcPolylineStrip(hlLines, cmd.userArcs[k].z, cmd.userArcs[k], 48);  // arc plane, not a flat depth
  } else if (e.type == SelectedEntity::Type::Ellipse) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= cmd.userEllipses.size())
      return;
    appendEllipsePolylineStrip(hlLines, cmd.userEllipses[k].z, cmd.userEllipses[k], 56);
  } else if (e.type == SelectedEntity::Type::Polyline) {
    appendCommittedPolylineStrip(hlLines, cmd, e.index);
  } else if (e.type == SelectedEntity::Type::FeatureLine) {
    // REQ-087. Without this a selected feature line drew no highlight at all: it would pick, box-
    // select and move correctly while giving the user nothing on screen to confirm what was
    // selected. Identity transform — the highlight traces the committed geometry where it is.
    appendFeatureLineStrip(hlLines, cmd, e.index, [](float*, float*) {});
  } else if (e.type == SelectedEntity::Type::Surface) {
    // REQ-068 / ADR-036 (b). The highlight traces the surface's BORDER, not its 600k triangle edges.
    // Two reasons, and the second is the one that decides it: at REQ-100's profile the whole
    // triangulation re-emitted here every frame is ~14 MB per frame, and even if it were free, a
    // 200k-triangle mesh drawn in highlight yellow is a solid block rather than a shape you can read.
    // The border says "this surface, this extent, including the voids its hide-boundaries left."
    //
    // Read from the cache (ADR-036 (e)); computing it here would put the O(n log n) walk back on the
    // per-frame path this function is called from.
    if (const std::vector<float>* border = SurfaceBorderEdges(cmd, static_cast<size_t>(e.index)))
      hlLines->insert(hlLines->end(), border->begin(), border->end());
  } else if (e.type == SelectedEntity::Type::Solid) {
    // REQ-318 item 12. Before this, a selected solid drew NO highlight at all — it picked, box-
    // selected and erased correctly while giving the user nothing on screen to confirm what was
    // selected. Exactly the omission REQ-087's feature line had, three branches up.
    //
    // The solid's own EDGES, which is what the entity pick tests against (`PickClosestCadEntity`
    // picks solid edges and deliberately not triangles, because in 2D Wireframe the edges are the
    // only thing on screen). So the highlight traces the thing that selects, in every visual style.
    //
    // Read from the tessellation cache rather than rebuilt from the topology: an arc edge is walked
    // into chords there already, and this function is on the per-frame path (the Surface branch
    // above records the same reasoning for the same reason).
    const size_t k = static_cast<size_t>(e.index);
    if (k >= cmd.cadSolids.size() || !cmd.cadSolids[k])
      return;
    const CadSolidPtr& sp = cmd.cadSolids[k];
    for (const CadSolidTessellation& t : cmd.solidDisplayCache) {
      if (t.key.lock() != sp)
        continue;
      hlLines->insert(hlLines->end(), t.edgeVerts.begin(), t.edgeVerts.end());
      break;
    }
  } else if (e.type == SelectedEntity::Type::FilledRegion) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= cmd.cadFilledRegions.size())
      return;
    const CadFilledRegion& fr = cmd.cadFilledRegions[k];
    // Outline every loop (outer + holes) as closed line-segment pairs (GL_LINES) so the selection/hover
    // highlight traces the hatch boundary on top of the fill (REQ-042).
    for (size_t loop = 0; loop < fr.loopStart.size(); ++loop) {
      const int begin = fr.loopStart[loop];
      const int cnt = fr.loopCount(loop);
      if (cnt < 2)
        continue;
      for (int i = 0; i < cnt; ++i) {
        const int a = begin + i;
        const int b = begin + (i + 1) % cnt;
        // The vertex's OWN Z, not the overlay's fixed draw depth (REQ-058) — the last entity type
        // in this file still using lineZ. Depth testing is off, so lineZ only ever ordered the draw;
        // once the view can tilt it is simply a wrong elevation, and the highlight detaches from
        // the hatch it is meant to trace.
        hlLines->push_back(fr.vertsXyz[static_cast<size_t>(a) * 3]);
        hlLines->push_back(fr.vertsXyz[static_cast<size_t>(a) * 3 + 1]);
        hlLines->push_back(fr.vertsXyz[static_cast<size_t>(a) * 3 + 2]);
        hlLines->push_back(fr.vertsXyz[static_cast<size_t>(b) * 3]);
        hlLines->push_back(fr.vertsXyz[static_cast<size_t>(b) * 3 + 1]);
        hlLines->push_back(fr.vertsXyz[static_cast<size_t>(b) * 3 + 2]);
      }
    }
  }
}

void BuildSelectionHighlight(const AppCommandState& cmd, std::vector<float>* hlLines,
                             std::vector<float>* hlCircles) {
  hlLines->clear();
  hlCircles->clear();
  for (const auto& e : cmd.selection)
    AppendEntityHighlight(cmd, e, hlLines, hlCircles);
  if (cmd.active == AppCommandState::Kind::Offset && cmd.offsetPhase == AppCommandState::OffsetPhase::WaitSelectEntity &&
      cmd.offsetHoverHighlightValid)
    AppendEntityHighlight(cmd, cmd.offsetHoverEntity, hlLines, hlCircles);
  // TRIM cutting edges read as a selection while they are being picked (REQ-056): they are chosen the way
  // a selection is chosen, so they get the selection's highlight rather than an appearance of their own.
  if (cmd.active == AppCommandState::Kind::Trim) {
    for (const auto& c : cmd.trimCutters)
      AppendEntityHighlight(cmd, c, hlLines, hlCircles);
  }
  // EXTEND boundary edges get the same selection-highlight treatment as TRIM cutting edges
  // (REQ-056) — they are picked the way a selection is picked.
  if (cmd.active == AppCommandState::Kind::Extend) {
    for (const auto& c : cmd.extendBoundaries)
      AppendEntityHighlight(cmd, c, hlLines, hlCircles);
  }
  // BREAK's pending entity (between the first and second point picks) reads as a selection too —
  // same REQ-056 precedent as TRIM/EXTEND.
  if (cmd.active == AppCommandState::Kind::Break &&
      cmd.breakPhase == AppCommandState::BreakPhase::SelectSecondPoint)
    AppendEntityHighlight(cmd, cmd.breakEntity, hlLines, hlCircles);
  // LENGTHEN DYnamic latches its target between the object pick and the new-length pick, exactly
  // as BREAK latches breakEntity — so it reads as a selection for the same REQ-056 reason. It was
  // the one entity-latching REQ-103 command left out of this list (TASK-099 F3).
  if (cmd.active == AppCommandState::Kind::Lengthen &&
      cmd.lengthenPhase == AppCommandState::LengthenPhase::WaitDynamicTarget)
    AppendEntityHighlight(cmd, cmd.lengthenPendingEntity, hlLines, hlCircles);
  // FILLET's first curve is latched between the two picks, exactly as BREAK latches breakEntity —
  // same REQ-056 reason (REQ-103 step 6a).
  if (cmd.active == AppCommandState::Kind::Fillet &&
      cmd.filletPhase == AppCommandState::FilletPhase::WaitSecondEntity)
    AppendEntityHighlight(cmd, cmd.filletFirstEntity, hlLines, hlCircles);
  // CHAMFER's first curve, same shape as FILLET's above (REQ-103 step 6b).
  if (cmd.active == AppCommandState::Kind::Chamfer &&
      cmd.chamferPhase == AppCommandState::ChamferPhase::WaitSecondEntity)
    AppendEntityHighlight(cmd, cmd.chamferFirstEntity, hlLines, hlCircles);
}

namespace {

/// Chords to draw one solid edge with.
///
/// A DISPLAY budget, deliberately not `solidpick.cpp`'s `EdgeSearchChords`, which is a SEARCH
/// budget: that one only has to be fine enough that the right edge wins a nearest-approach contest
/// (the winner is then placed exactly by `ClosestPointOnEdge`), while this one is the line the user
/// looks at. pi/24 per chord is the step `ChainHitsRect` and the curve fences already use, so a
/// highlighted arc bends the same way everywhere in the program.
int SubObjectEdgeChords(const brep::Edge& e) {
  if (e.kind == brep::CurveKind::Line)
    return 1;
  constexpr double kPi = 3.14159265358979323846;
  const double sweep = std::fabs(e.sweep);
  // `sweep` is documented as meaningful for Arc and Ellipse only — an Intersection edge (the
  // procedural surface-crossing curve REQ-314's booleans produce) leaves it zero, and keying on it
  // would draw that edge as a single straight chord across a curve. Key on the KIND, as the pick
  // does and for the same reason.
  if (e.kind == brep::CurveKind::Intersection || !(sweep > 0.0) || !std::isfinite(sweep))
    return 64;
  return std::clamp(static_cast<int>(std::ceil(sweep / (kPi / 24.0))), 8, 96);
}

void AppendSeg(std::vector<float>* out, const ray3d::Vec3& a, const ray3d::Vec3& b) {
  out->push_back(static_cast<float>(a.x));
  out->push_back(static_cast<float>(a.y));
  out->push_back(static_cast<float>(a.z));
  out->push_back(static_cast<float>(b.x));
  out->push_back(static_cast<float>(b.y));
  out->push_back(static_cast<float>(b.z));
}

}  // namespace

namespace {

/// One sub-object's drawable geometry, appended. Shared by the selection highlight and the hover
/// pre-highlight so the two cannot draw a picked face differently from a hovered one.
/// Walk one solid edge into \p out as `GL_LINES`. Shared by the edge highlight and the face
/// boundary, so a face's outline bends exactly as that same edge does when picked on its own.
void AppendSolidEdge(const brep::Solid& sp, const brep::Edge& e, std::vector<float>* out) {
  const int n = SubObjectEdgeChords(e);
  ray3d::Vec3 prev = brep::EdgePointAt(sp, e, 0.0);
  for (int i = 1; i <= n; ++i) {
    const ray3d::Vec3 cur = brep::EdgePointAt(sp, e, static_cast<double>(i) / n);
    AppendSeg(out, prev, cur);
    prev = cur;
  }
}

void AppendSubObjectGeometry(const AppCommandState& cmd, const SelectedSubObject& s, double armWorld,
                             std::vector<float>* faceTris, std::vector<float>* faceEdges,
                             std::vector<float>* lines) {
  {
    if (s.solidIndex < 0 || static_cast<size_t>(s.solidIndex) >= cmd.cadSolids.size())
      return;
    const CadSolidPtr& sp = cmd.cadSolids[static_cast<size_t>(s.solidIndex)];
    // The reference expires rather than re-binds (ADR-049): if the solid it names is not the solid
    // it came from, an edit has replaced it and this index no longer means what it meant. Draw
    // nothing rather than highlight a face the user never picked. `ExpireSubObjectSelection` sweeps
    // these once a frame; this guard is what makes the order of the two not matter.
    if (!sp || s.owner.lock() != sp)
      return;
    if (s.kind == solidpick::Kind::Face) {
      if (s.index < 0 || static_cast<size_t>(s.index) >= sp->faces.size())
        return;
      // The face's BOUNDARY first, because it is what the user actually sees. Every loop — the
      // outer one and any holes — walked as its own edges, so a curved face outlines as a curve
      // and a face with a hole shows the hole.
      if (faceEdges) {
        for (const brep::Loop& loop : sp->faces[static_cast<size_t>(s.index)].loops) {
          for (const brep::EdgeUse& use : loop.uses) {
            if (use.edge < 0 || static_cast<size_t>(use.edge) >= sp->edges.size())
              continue;
            AppendSolidEdge(*sp, sp->edges[static_cast<size_t>(use.edge)], faceEdges);
          }
        }
      }
      if (!faceTris)
        return;
      // The face's OWN triangles, from the per-solid cache. Not from `solidDisplayGeometry`, which
      // merges solids into shared buffers and keeps no face channel (REQ-318 item 13).
      for (const CadSolidTessellation& t : cmd.solidDisplayCache) {
        if (t.key.lock() != sp)
          continue;
        if (t.triVerts.size() != t.triFaceIds.size() * 9)
          break;  // inconsistent buffers: draw nothing rather than read past the end (REQ-201)
        for (size_t tri = 0; tri < t.triFaceIds.size(); ++tri) {
          if (t.triFaceIds[tri] != s.index)
            continue;
          faceTris->insert(faceTris->end(), t.triVerts.begin() + static_cast<std::ptrdiff_t>(tri * 9),
                           t.triVerts.begin() + static_cast<std::ptrdiff_t>(tri * 9 + 9));
        }
        break;
      }
    } else if (s.kind == solidpick::Kind::Edge) {
      if (!lines || s.index < 0 || static_cast<size_t>(s.index) >= sp->edges.size())
        return;
      AppendSolidEdge(*sp, sp->edges[static_cast<size_t>(s.index)], lines);
    } else if (s.kind == solidpick::Kind::Vertex) {
      if (!lines || s.index < 0 || static_cast<size_t>(s.index) >= sp->vertices.size())
        return;
      const ray3d::Vec3 v = sp->vertices[static_cast<size_t>(s.index)].p;
      // A three-axis cross, not a dot: a dot is one pixel of a colour the drawing may already use,
      // while a cross reads as a marker at any zoom and from any camera angle.
      AppendSeg(lines, ray3d::Vec3{v.x - armWorld, v.y, v.z}, ray3d::Vec3{v.x + armWorld, v.y, v.z});
      AppendSeg(lines, ray3d::Vec3{v.x, v.y - armWorld, v.z}, ray3d::Vec3{v.x, v.y + armWorld, v.z});
      AppendSeg(lines, ray3d::Vec3{v.x, v.y, v.z - armWorld}, ray3d::Vec3{v.x, v.y, v.z + armWorld});
    }
  }
}

/// A vertex marker's arm, as a fixed fraction of the VIEW rather than of the model: a marker sized
/// in world units is a speck when zoomed out and fills the screen when zoomed in, which is the same
/// reason the snap glyphs are screen-sized (REQ-058).
double SubObjectMarkerArm(const AppCommandState& cmd) {
  return std::max(1.e-9, static_cast<double>(cmd.viewportLastSurveyLayoutOrthoHalfH) * 0.012);
}

}  // namespace

void BuildSubObjectHighlight(const AppCommandState& cmd, std::vector<float>* faceTris,
                             std::vector<float>* faceEdges, std::vector<float>* lines) {
  if (faceTris)
    faceTris->clear();
  if (faceEdges)
    faceEdges->clear();
  if (lines)
    lines->clear();
  const double arm = SubObjectMarkerArm(cmd);
  for (const SelectedSubObject& s : cmd.subObjectSelection)
    AppendSubObjectGeometry(cmd, s, arm, faceTris, faceEdges, lines);
}

void BuildSubObjectHoverHighlight(const AppCommandState& cmd, std::vector<float>* faceTris,
                                  std::vector<float>* faceEdges, std::vector<float>* lines) {
  if (faceTris)
    faceTris->clear();
  if (faceEdges)
    faceEdges->clear();
  if (lines)
    lines->clear();
  if (!cmd.subObjectHoverValid)
    return;
  // Already selected? Say nothing. The selection highlight is the stronger statement, and drawing a
  // quieter one over it only muddies the colour — the rule BuildHoverHighlight already applies to
  // entities ("skip if already selected — selection highlight takes visual precedence").
  for (const SelectedSubObject& s : cmd.subObjectSelection)
    if (s.sameTarget(cmd.subObjectHover))
      return;
  AppendSubObjectGeometry(cmd, cmd.subObjectHover, SubObjectMarkerArm(cmd), faceTris, faceEdges, lines);
}

void BuildSubObjectGripGeometry(const AppCommandState& cmd, std::vector<float>* handle,
                                std::vector<float>* preview) {
  if (handle)
    handle->clear();
  if (preview)
    preview->clear();

  // Exactly one selected face gets a handle. Two would need two handles and a rule for which one a
  // drag grabs, and PRESSPULL already refuses to move two faces at once — offering a gesture the
  // commit would decline is worse than offering none.
  const SelectedSubObject* ref = nullptr;
  if (cmd.subObjectGripActive) {
    ref = &cmd.subObjectGripRef;  // mid-drag the handle belongs to the face being dragged
  } else {
    int faces = 0;
    for (const SelectedSubObject& s : cmd.subObjectSelection)
      if (s.kind == solidpick::Kind::Face) {
        ++faces;
        ref = &s;
      }
    if (faces != 1)
      return;
  }

  ray3d::Vec3 anchor;
  ray3d::Vec3 axis;
  if (!CadSubObjectFaceGrip(cmd, *ref, &anchor, &axis))
    return;

  const double arm = SubObjectMarkerArm(cmd);
  // Two directions IN the face's plane, so the handle lies flat on the face rather than floating in
  // front of it — a square that cuts through the surface reads as a bug at a glancing camera angle.
  ray3d::Vec3 u = std::fabs(axis.z) < 0.9 ? ray3d::Vec3{0, 0, 1} : ray3d::Vec3{1, 0, 0};
  u = ray3d::Normalize(ray3d::Cross(axis, u));
  const ray3d::Vec3 v = ray3d::Normalize(ray3d::Cross(axis, u));

  if (handle) {
    const ray3d::Vec3 c[4] = {
        ray3d::Add(anchor, ray3d::Add(ray3d::Scale(u, arm), ray3d::Scale(v, arm))),
        ray3d::Add(anchor, ray3d::Add(ray3d::Scale(u, -arm), ray3d::Scale(v, arm))),
        ray3d::Add(anchor, ray3d::Add(ray3d::Scale(u, -arm), ray3d::Scale(v, -arm))),
        ray3d::Add(anchor, ray3d::Add(ray3d::Scale(u, arm), ray3d::Scale(v, -arm))),
    };
    for (int i = 0; i < 4; ++i)
      AppendSeg(handle, c[i], c[(i + 1) % 4]);
  }

  if (!preview || !cmd.subObjectGripActive)
    return;
  const ray3d::Vec3 delta = ray3d::Scale(axis, cmd.subObjectGripDistance);
  const CadSolidPtr sp = ref->owner.lock();
  if (!sp || ref->index < 0 || static_cast<size_t>(ref->index) >= sp->faces.size())
    return;
  // The face's boundary where it would land. Translated, not rebuilt: `brep::PushPullFace` copies
  // the whole solid and validates it, which is the right cost once on commit and the wrong cost
  // every frame of a drag.
  for (const brep::Loop& loop : sp->faces[static_cast<size_t>(ref->index)].loops) {
    for (const brep::EdgeUse& use : loop.uses) {
      if (use.edge < 0 || static_cast<size_t>(use.edge) >= sp->edges.size())
        continue;
      std::vector<float> seg;
      AppendSolidEdge(*sp, sp->edges[static_cast<size_t>(use.edge)], &seg);
      for (size_t i = 0; i + 2 < seg.size(); i += 3) {
        preview->push_back(seg[i] + static_cast<float>(delta.x));
        preview->push_back(seg[i + 1] + static_cast<float>(delta.y));
        preview->push_back(seg[i + 2] + static_cast<float>(delta.z));
      }
    }
  }
  // A leader from the handle to where it is going, so the drag distance is readable even when the
  // moved boundary happens to sit behind other geometry.
  AppendSeg(preview, anchor, ray3d::Add(anchor, delta));
}


void BuildHoverHighlight(const AppCommandState& cmd, std::vector<float>* hoverLines,
                         std::vector<float>* hoverCircles) {
  hoverLines->clear();
  hoverCircles->clear();
  if (!cmd.viewportHoverEntityValid)
    return;
  // Skip if already selected — selection highlight takes visual precedence. An already-picked TRIM
  // cutting edge counts as selected here for the same reason (REQ-056).
  const SelectedEntity& e = cmd.viewportHoverEntity;
  for (const auto& sel : cmd.selection) {
    if (sel.type == e.type && sel.index == e.index)
      return;
  }
  if (cmd.active == AppCommandState::Kind::Trim) {
    for (const auto& c : cmd.trimCutters) {
      if (c.type == e.type && c.index == e.index)
        return;
    }
  }
  if (cmd.active == AppCommandState::Kind::Extend) {
    for (const auto& c : cmd.extendBoundaries) {
      if (c.type == e.type && c.index == e.index)
        return;
    }
  }
  if (cmd.active == AppCommandState::Kind::Break &&
      cmd.breakPhase == AppCommandState::BreakPhase::SelectSecondPoint &&
      cmd.breakEntity.type == e.type && cmd.breakEntity.index == e.index)
    return;
  if (cmd.active == AppCommandState::Kind::Lengthen &&
      cmd.lengthenPhase == AppCommandState::LengthenPhase::WaitDynamicTarget &&
      cmd.lengthenPendingEntity.type == e.type && cmd.lengthenPendingEntity.index == e.index)
    return;
  if (cmd.active == AppCommandState::Kind::Fillet &&
      cmd.filletPhase == AppCommandState::FilletPhase::WaitSecondEntity &&
      cmd.filletFirstEntity.type == e.type && cmd.filletFirstEntity.index == e.index)
    return;
  if (cmd.active == AppCommandState::Kind::Chamfer &&
      cmd.chamferPhase == AppCommandState::ChamferPhase::WaitSecondEntity &&
      cmd.chamferFirstEntity.type == e.type && cmd.chamferFirstEntity.index == e.index)
    return;
  AppendEntityHighlight(cmd, e, hoverLines, hoverCircles);
}

// --- The translate gizmo (REQ-060, GitHub issue #148 Phase 5 slice 4b) --------------------------

namespace {

void GizmoSeg(std::vector<float>* out, const ray3d::Vec3& a, const ray3d::Vec3& b) {
  out->push_back(static_cast<float>(a.x));
  out->push_back(static_cast<float>(a.y));
  out->push_back(static_cast<float>(a.z));
  out->push_back(static_cast<float>(b.x));
  out->push_back(static_cast<float>(b.y));
  out->push_back(static_cast<float>(b.z));
}

}  // namespace

void BuildGizmoOverlay(const AppCommandState& cmd, CadGizmoOverlay* out) {
  if (!out)
    return;
  for (int i = 0; i < 3; ++i) {
    out->axis[i].clear();
    out->hot[i] = false;
  }
  out->guide.clear();
  // REQ-060 acceptance 3, and the whole of it: an empty selection has no anchor, so nothing is
  // emitted and nothing is drawn. Stated by the anchor's own return rather than by a separate
  // "is the selection empty" test, which is a second thing that could disagree with the first.
  ray3d::Vec3 anchor{};
  if (!CadGizmoVisible(cmd) || !CadGizmoAnchorWorld(cmd, &anchor))
    return;
  // The gizmo does NOT follow the ghost during a drag: the handles stay where they were grabbed,
  // which is what the drag distance is measured from. A widget that slid along with the preview
  // would be measuring from a moving origin, and the number under the cursor would be nonsense.
  if (cmd.gizmoDragActive)
    anchor = cmd.gizmoAnchor;
  const double len = static_cast<double>(CadGizmoHandleLenWorld(cmd));
  const int lit = cmd.gizmoDragActive ? cmd.gizmoDragAxis : cmd.gizmoHoverAxis;
  for (int a = 0; a < kGizmoAxisCount; ++a) {
    const ray3d::Vec3 u = CadGizmoAxisWorld(cmd, a);
    const ray3d::Vec3 tip = ray3d::Add(anchor, ray3d::Scale(u, len));
    out->hot[a] = (a == lit);
    GizmoSeg(&out->axis[a], anchor, tip);
    // Four barbs back from the tip, on a small ring around the axis. Built in the AXIS's own frame
    // rather than the camera's, so the arrowhead is part of the widget's geometry and does not swim
    // when the view orbits — the opposite choice from a survey point's marker cross, which is a
    // screen-space annotation and should billboard.
    ray3d::Vec3 seed{0.0, 0.0, 1.0};
    if (std::fabs(ray3d::Dot(u, seed)) > 0.9)
      seed = ray3d::Vec3{1.0, 0.0, 0.0};
    const ray3d::Vec3 p = ray3d::Normalize(ray3d::Cross(u, seed));
    const ray3d::Vec3 q = ray3d::Cross(u, p);
    const double back = len * 0.82;
    const double flare = len * 0.07;
    const ray3d::Vec3 base = ray3d::Add(anchor, ray3d::Scale(u, back));
    for (int i = 0; i < 4; ++i) {
      const ray3d::Vec3 side = i == 0   ? ray3d::Scale(p, flare)
                               : i == 1 ? ray3d::Scale(p, -flare)
                               : i == 2 ? ray3d::Scale(q, flare)
                                        : ray3d::Scale(q, -flare);
      GizmoSeg(&out->axis[a], tip, ray3d::Add(base, side));
    }
  }
  if (cmd.gizmoDragActive) {
    // The track, extended well past the handle in both directions: the drag is not limited to the
    // handle's length, and a guide that stopped at the tip would say it was.
    const ray3d::Vec3 u = cmd.gizmoAxisDir;
    const double reach = len * 12.0;
    GizmoSeg(&out->guide, ray3d::Sub(anchor, ray3d::Scale(u, reach)),
             ray3d::Add(anchor, ray3d::Scale(u, reach)));
  }
}

void BuildGizmoDragGhost(const AppCommandState& cmd, std::vector<float>* outLines,
                         std::vector<float>* outCircles) {
  if (!outLines || !outCircles)
    return;
  outLines->clear();
  outCircles->clear();
  if (!cmd.gizmoDragActive || std::fabs(cmd.gizmoDragDistance) < 1.e-12)
    return;
  // The selection's own highlight linework, translated. Built from `AppendEntityHighlight` rather
  // than from a second per-type walk: the ghost is then, by construction, a picture of exactly what
  // the drag is about to move — every type it covers and no type it does not.
  for (const auto& e : cmd.selection)
    AppendEntityHighlight(cmd, e, outLines, outCircles);
  const double d = cmd.gizmoDragDistance;
  const ray3d::Vec3 u = cmd.gizmoAxisDir;
  const float dx = static_cast<float>(d * u.x);
  const float dy = static_cast<float>(d * u.y);
  const float dz = static_cast<float>(d * u.z);
  for (size_t i = 0; i + 2 < outLines->size(); i += 3) {
    (*outLines)[i] += dx;
    (*outLines)[i + 1] += dy;
    (*outLines)[i + 2] += dz;
  }
  // Circles are carried as (cx, cy, z, r) rather than as chords, so only the centre moves.
  for (size_t i = 0; i + 3 < outCircles->size(); i += 4) {
    (*outCircles)[i] += dx;
    (*outCircles)[i + 1] += dy;
    (*outCircles)[i + 2] += dz;
  }
}
