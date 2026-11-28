#include "TransformPreview.hpp"

#include "CadCommands.hpp"
#include <GoSurvey/Geometry/geom2d.hpp>
#include <GoSurvey/Renderer/GizmoOverlay.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace {

// Component comp (0-2 = x, y, z) of endpoint (0 = start, 1 = end) of a line segment.
double LineEndComp(const CadLineRecord& l, int endpoint, int comp) {
  const double v[6] = {l.x0, l.y0, l.z0, l.x1, l.y1, l.z1};
  return v[endpoint * 3 + comp];
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

/// One ellipse into the preview line buffer, in the plane it lies in and over the span it draws.
///
/// The same story as \ref appendPreviewCircle above, for the other curve that gained a plane. A flat
/// whole ellipse goes through the 2D helper exactly as before, so every existing preview and
/// highlight is unchanged. A tilted or part-drawn one is walked through `EllipseWorldPointAt`, the
/// same points the renderer strokes.
///
/// Highlighting is not cosmetic here: the selection highlight is how you confirm you picked the
/// thing you meant. Drawn flat and whole it contradicted the curve on screen — the section would
/// select, then light up as a full oval lying on the datum.
void appendEllipsePolylineStrip(std::vector<float>* out, float z, const CadEllipse& el, int fallbackN) {
  int n = fallbackN;
  if (g_previewOrthoHalfH > 0.f && g_previewFbHeightPx > 0) {
    // Ellipses scale with the major semi-axis — the worst-case chord — as the renderer does.
    const double majLen = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    n = std::max(16, CircleTessellationSegmentCount(majLen, static_cast<double>(g_previewOrthoHalfH),
                                                    g_previewFbHeightPx, g_previewSmoothnessCap));
  }
  if (EllipseIsFlat(el) && EllipseIsFullTurn(el)) {
    AppendEllipseLineSegments(*out, static_cast<double>(el.cx), static_cast<double>(el.cy),
                              static_cast<double>(el.majVx), static_cast<double>(el.majVy),
                              static_cast<double>(el.ratio), n, z);
    return;
  }
  ray3d::Vec3 prev = EllipseWorldPointAt(el, EllipseSpanAngleAt(el, 0.0));
  for (int i = 1; i <= n; ++i) {
    const ray3d::Vec3 p =
        EllipseWorldPointAt(el, EllipseSpanAngleAt(el, static_cast<double>(i) / static_cast<double>(n)));
    out->push_back(static_cast<float>(prev.x));
    out->push_back(static_cast<float>(prev.y));
    out->push_back(static_cast<float>(prev.z));
    out->push_back(static_cast<float>(p.x));
    out->push_back(static_cast<float>(p.y));
    out->push_back(static_cast<float>(p.z));
    prev = p;
  }
}

// REQ-443: a ghost of spline \p index with every point mapped through \p f - the same map the command
// will apply on commit (`spline::MapPoints`), so the ghost is the spline that will be left behind. Each
// chord of the tessellation is a segment with its own Z, as a committed polyline's is.
template <class F>
void appendSplinePreview(std::vector<float>* out, const DrawingContent& d, int index, const F& f) {
  if (index < 0 || static_cast<size_t>(index) >= d.cadSplines.size())
    return;
  CadSpline s = d.cadSplines[static_cast<size_t>(index)];
  spline::MapPoints(&s, f);
  std::vector<ray3d::Vec3> pts;
  spline::Tessellate(s, kSplineSampleTolerance, &pts);
  for (size_t i = 1; i < pts.size(); ++i) {
    out->push_back(static_cast<float>(pts[i - 1].x));
    out->push_back(static_cast<float>(pts[i - 1].y));
    out->push_back(static_cast<float>(pts[i - 1].z));
    out->push_back(static_cast<float>(pts[i].x));
    out->push_back(static_cast<float>(pts[i].y));
    out->push_back(static_cast<float>(pts[i].z));
  }
}

// Draws a COMMITTED polyline, so it uses each vertex's own Z rather than a caller-supplied flat
// depth (REQ-057/058). A highlight or hover stroke drawn at a fixed Z would sit on the datum while
// the polyline itself sat at elevation, showing the object twice in an orbited view.
void appendCommittedPolylineStrip(std::vector<float>* out, const DrawingContent& d, int pi) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= d.userPolylineOffsets.size())
    return;
  const int v0 = d.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = d.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const bool closed =
      static_cast<size_t>(pi) < d.userPolylineClosed.size() && d.userPolylineClosed[static_cast<size_t>(pi)];
  auto emit = [&](int a, int b) {
    const size_t A = static_cast<size_t>(a), B = static_cast<size_t>(b);
    const float za = d.vertex(A).z;
    // REQ-316 / ADR-047: a curved segment is traced as an arc, not a chord, so the selection /
    // hover highlight follows the shape the renderer drew.
    const float bulge =
        static_cast<size_t>(a) < d.userPolylineVertsBulge.size() ? d.userPolylineVertsBulge[static_cast<size_t>(a)] : 0.f;
    if (bulge == 0.f) {
      out->push_back(d.vertex(A).x);
      out->push_back(d.vertex(A).y);
      out->push_back(za);
      out->push_back(d.vertex(B).x);
      out->push_back(d.vertex(B).y);
      out->push_back(d.vertex(B).z);
      return;
    }
    // REQ-325 / ADR-053: this segment's own plane, when it is not flat +Z — the same construction
    // the renderer's own AppendChainEdgesVc (ViewportRenderer.cpp) and PickClosestCadEntity's
    // polySegD2 already use, so a tilted curve's highlight traces the curve actually drawn instead
    // of a flat approximation sitting somewhere else entirely (a real report: a joined polyline's
    // tilted arc segment never lit up yellow when the polyline was selected).
    float nx = 0.f, ny = 0.f, nz = 1.f;
    // Three floats per vertex, parallel to the vertices (cadblock.hpp).
    if (const size_t nk = A * 3; nk + 2 < d.userPolylineVertsNormal.size()) {
      nx = d.userPolylineVertsNormal[nk];
      ny = d.userPolylineVertsNormal[nk + 1];
      nz = d.userPolylineVertsNormal[nk + 2];
    }
    const bool flat = IsFlatNormal(nx, ny, nz);
    ucs::Ucs plane{};
    BulgeArcSpan arc{};
    if (flat) {
      arc = BulgeArc(d.vertex(A).x, d.vertex(A).y, d.vertex(B).x,
                     d.vertex(B).y, static_cast<double>(bulge));
    } else if (ucs::FromNormal(ray3d::Vec3{d.vertex(A).x, d.vertex(A).y, za},
                               ray3d::Vec3{static_cast<double>(nx), static_cast<double>(ny),
                                           static_cast<double>(nz)},
                               &plane)) {
      const ucs::Point2D p1Local = ucs::WorldToPlane(
          plane, ray3d::Vec3{d.vertex(B).x, d.vertex(B).y, d.vertex(B).z});
      arc = BulgeArc(0.0, 0.0, p1Local.x, p1Local.y, static_cast<double>(bulge));
    }
    if (!arc.valid) {
      out->push_back(d.vertex(A).x);
      out->push_back(d.vertex(A).y);
      out->push_back(za);
      out->push_back(d.vertex(B).x);
      out->push_back(d.vertex(B).y);
      out->push_back(d.vertex(B).z);
      return;
    }
    constexpr double kPi = 3.14159265358979323846;
    const int n = std::clamp(static_cast<int>(std::ceil(std::fabs(arc.sweep) / (kPi / 24.0))), 2, 96);
    auto sampleWorld = [&](double u, float* ox, float* oy, float* oz) {
      const double lx = arc.cx + arc.radius * std::cos(u);
      const double ly = arc.cy + arc.radius * std::sin(u);
      if (flat) {
        *ox = static_cast<float>(lx);
        *oy = static_cast<float>(ly);
        *oz = za;
        return;
      }
      const ray3d::Vec3 wp = ucs::PlaneToWorld(plane, ucs::Point2D{lx, ly});
      *ox = static_cast<float>(wp.x);
      *oy = static_cast<float>(wp.y);
      *oz = static_cast<float>(wp.z);
    };
    float px = 0.f, py = 0.f, pz = 0.f;
    sampleWorld(arc.startAngle, &px, &py, &pz);
    for (int s = 1; s <= n; ++s) {
      const double u = arc.startAngle + arc.sweep * (static_cast<double>(s) / n);
      float qx = 0.f, qy = 0.f, qz = 0.f;
      sampleWorld(u, &qx, &qy, &qz);
      out->push_back(px);
      out->push_back(py);
      out->push_back(pz);
      out->push_back(qx);
      out->push_back(qy);
      out->push_back(qz);
      px = qx;
      py = qy;
      pz = qz;
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
void appendFeatureLineStrip(std::vector<float>* out, const DrawingContent& d, int fi, Xf&& xf) {
  if (fi < 0 || static_cast<size_t>(fi + 1) >= d.featureLineOffsets.size())
    return;
  const int v0 = d.featureLineOffsets[static_cast<size_t>(fi)];
  const int v1 = d.featureLineOffsets[static_cast<size_t>(fi + 1)];
  if (v1 - v0 < 2 || static_cast<size_t>(v1) * 3 > d.featureLineVerts.size())
    return;
  const bool closed = static_cast<size_t>(fi) < d.featureLineClosed.size() &&
                      d.featureLineClosed[static_cast<size_t>(fi)];

  auto emit = [&](int a, int b) {
    const int idx[2] = {a, b};
    for (int k = 0; k < 2; ++k) {
      const size_t base = static_cast<size_t>(idx[k]) * 3;
      float x = d.featureLineVerts[base];
      float y = d.featureLineVerts[base + 1];
      xf(&x, &y);
      out->push_back(x);
      out->push_back(y);
      out->push_back(d.featureLineVerts[base + 2]);
    }
  };
  for (int vi = v0; vi + 1 < v1; ++vi)
    emit(vi, vi + 1);
  if (closed)
    emit(v1 - 1, v0);
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
    const size_t li = static_cast<size_t>(e.index);
    if (li >= cmd.drawing.lineCount())
      return;
    const CadLineRecord seg = cmd.drawing.line(li);
    // Both points already lie on the segment, so the removed material is simply the span between
    // them — no ordering needed.
    appendPreviewSegment(out, p1.x, p1.y, p2.x, p2.y, seg.z0);
    return;
  }
  case SelectedEntity::Type::Circle: {
    const size_t ci = static_cast<size_t>(e.index);
    if (ci >= cmd.drawing.circleCount())
      return;
    CadArc removed{};
    removed.cx = cmd.drawing.circle(ci).cx;
    removed.cy = cmd.drawing.circle(ci).cy;
    removed.z = cmd.drawing.circle(ci).cz;
    removed.r = cmd.drawing.circle(ci).r;
    removed.startRad = p1.theta;
    float sweep = std::fmod(p2.theta - p1.theta, kTwoPi);
    if (sweep < 0.f)
      sweep += kTwoPi;  // a repeated pick leaves sweep 0 — "break at point" removes nothing
    removed.sweepRad = sweep;
    appendArcPolylineStrip(out, removed.z, removed, 64);
    return;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.drawing.userArcs.size())
      return;
    const CadArc& src = cmd.drawing.userArcs[static_cast<size_t>(e.index)];
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
      const double r = std::max(src.r, 1e-9);
      removed.startRad = src.startRad + sgn * (nearP / r);
      removed.sweepRad = sgn * ((farP - nearP) / r);
    }
    appendArcPolylineStrip(out, removed.z, removed, 64);
    return;
  }
  case SelectedEntity::Type::Polyline: {
    const int pi = e.index;
    if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.drawing.userPolylineOffsets.size())
      return;
    const int v0 = cmd.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = cmd.drawing.userPolylineOffsets[static_cast<size_t>(pi + 1)];
    const int n = v1 - v0;
    if (n < 2 || static_cast<size_t>(v1 - 1) * 3 + 1 >= cmd.drawing.userPolylineVerts.size())
      return;
    auto vx = [&](int i) { return cmd.drawing.vertex(static_cast<size_t>(v0 + i)).x; };
    auto vy = [&](int i) { return cmd.drawing.vertex(static_cast<size_t>(v0 + i)).y; };
    const bool closed = static_cast<size_t>(pi) < cmd.drawing.userPolylineClosed.size() &&
                        cmd.drawing.userPolylineClosed[static_cast<size_t>(pi)] != 0u;
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
  const auto* brk = cmd.sessionAs<stepengine::BreakSession>();
  if (!brk || brk->phase != stepengine::BreakPhase::SelectSecondPoint)
    return;
  BreakPoint p2{};
  if (!ClosestPointOnEntity(cmd, brk->entity, curX, curY, &p2))
    return;
  g_previewOrthoHalfH = orthoHalfHeightWorld;
  g_previewSmoothnessCap = std::clamp(cmd.displayArcCircleSmoothness, 8, 20000);
  std::vector<float> localSpan;
  std::vector<float>* span = outSpan ? outSpan : &localSpan;
  appendBreakRemovedSpan(span, cmd, brk->entity, brk->p1, p2);
  if (outMarkers) {
    const float mz = span->size() >= 3 ? (*span)[2] : 0.f;
    appendBreakPointMarker(outMarkers, brk->p1.x, brk->p1.y, mz, orthoHalfHeightWorld);
    appendBreakPointMarker(outMarkers, p2.x, p2.y, mz, orthoHalfHeightWorld);
  }
}

static void AppendTransformPreview(const AppCommandState& cmd, const CadTransformPreviewPlan& plan,
                                   std::vector<float>* prevLines, std::vector<float>* prevCircles);
static void AppendScratchCopies(const AppCommandState& cmd, const CadTransformPreviewScratch& scratch,
                                std::vector<float>* prevLines, std::vector<float>* prevCircles);

void BuildTransformPreview(const AppCommandState& cmd, float curX, float curY, std::vector<float>* prevLines,
                           std::vector<float>* prevCircles, float orthoHalfHeightWorld, int framebufferHeightPx) {
  prevLines->clear();
  prevCircles->clear();
  g_previewOrthoHalfH = orthoHalfHeightWorld;
  g_previewFbHeightPx = framebufferHeightPx;
  g_previewSmoothnessCap = std::clamp(cmd.displayArcCircleSmoothness, 8, 20000);
  using MP = stepengine::ModifyPhase;
  using OP = stepengine::OffsetPhase;
  using MirP = stepengine::MirrorPhase;

  if (const auto* sps = cmd.sessionAs<stepengine::SplineSession>(); sps && !sps->draftPoints.empty()) {
    std::vector<spline::Vec3> points = sps->draftPoints;
    // The command commits a picked point at the active/snap-resolved work-plane elevation. Keep
    // the ghost on that same plane instead of inheriting the previous pick's potentially different Z.
    const double z = CadCommitElevation(cmd);
    if (std::hypot(points.back().x - static_cast<double>(curX),
                   points.back().y - static_cast<double>(curY)) > 1e-9)
      points.push_back({static_cast<double>(curX), static_cast<double>(curY), z});
    CadSpline draft;
    const bool made = sps->method == stepengine::SplineMethod::Fit
                          ? spline::MakeFit(points, false, &draft, nullptr)
                          : spline::MakeControl(points, false, &draft, nullptr);
    if (made) {
      std::vector<ray3d::Vec3> samples;
      spline::Tessellate(draft, kSplineSampleTolerance, &samples);
      for (std::size_t i = 1; i < samples.size(); ++i) {
        prevLines->insert(prevLines->end(), {
            static_cast<float>(samples[i - 1].x), static_cast<float>(samples[i - 1].y),
            static_cast<float>(samples[i - 1].z), static_cast<float>(samples[i].x),
            static_cast<float>(samples[i].y), static_cast<float>(samples[i].z)});
      }
    }
    return;
  }

  if (const auto* ofs = cmd.sessionAs<stepengine::OffsetSession>();
      ofs && ofs->entityValid &&
      (ofs->phase == OP::WaitThroughPick || ofs->phase == OP::WaitSidePick)) {
    CadOffsetAppendLivePreview(cmd, curX, curY, prevLines, prevCircles);
    return;
  }

  // REQ-103 BREAK's removal preview is deliberately NOT in this batch — see
  // BuildBreakRemovalPreview. This one is drawn translucent at ordinary line width, which is right
  // for a ghost of geometry somewhere it is not yet, and useless for material highlighted in place.

  // MOVE, COPY, PASTE, ARRAY, SCALE, MIRROR and ROTATE (ADR-0011 / issue #1700): the commit's own
  // transform applied to a scratch copy of what it would transform, drawn as a wireframe. SCALE's
  // reference rubber and MIRROR's line belong to the step rather than to the objects, so they are
  // drawn first.
  if (const auto* ss = cmd.sessionAs<stepengine::ScaleSession>();
      ss && ss->phase == MP::NeedDestination) {
    using SP = stepengine::ScalePhase;
    if (ss->scalePhase == SP::Ref_WaitP2) {
      // Reference/measure rubber: drawn on the work plane, since it marks distances the user is
      // picking rather than geometry belonging to an object (REQ-058).
      appendPreviewSegment(prevLines, ss->refP1X, ss->refP1Y, curX, curY, CadCommitElevation(cmd));
      return;
    }
    if (ss->scalePhase == SP::NewLength_WaitP2)
      appendPreviewSegment(prevLines, ss->newLenP1X, ss->newLenP1Y, curX, curY, CadCommitElevation(cmd));
  }
  if (const auto* ms = cmd.sessionAs<stepengine::MirrorSession>();
      ms && (ms->phase == MirP::NeedP2 || ms->phase == MirP::NeedEraseAnswer)) {
    float x1 = ms->p2X, y1 = ms->p2Y, z1 = ms->p2Z;
    if (ms->phase == MirP::NeedP2) {
      // The endpoint CadActiveTransformPreviewPlan reflects across (MirrorSession::onViewportPick).
      x1 = curX;
      y1 = curY;
      z1 = CadCommitElevation(cmd);
      ApplyOrthoConstrainFromAnchor(cmd, ms->p1X, ms->p1Y, &x1, &y1, cmd.orthoMode, ms->p1Z, z1, &z1);
    }
    // The mirror line itself, so the user sees what they are reflecting across.
    prevLines->insert(prevLines->end(), {ms->p1X, ms->p1Y, ms->p1Z, x1, y1, z1});
  }
  if (CadTransformPreviewPlan plan; CadActiveTransformPreviewPlan(cmd, curX, curY, &plan)) {
    AppendTransformPreview(cmd, plan, prevLines, prevCircles);
    return;
  }

  // STRETCH: the commit's own window and per-kind STRETCH on a scratch copy (issue #1700 phase 4).
  if (CadTransformPreviewScratch scratch; BuildCadStretchPreviewScratch(cmd, curX, curY, &scratch)) {
    AppendScratchCopies(cmd, scratch, prevLines, prevCircles);
    return;
  }

  if (const auto* lenSess = cmd.sessionAs<stepengine::LengthenSession>()) {
    if (lenSess->phase != stepengine::LengthenPhase::WaitDynamicTarget)
      return;
    const SelectedEntity& e = lenSess->pendingEntity;
    const bool nearFirst = lenSess->pendingNearFirst;
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t li = static_cast<size_t>(e.index);
      if (li >= cmd.drawing.lineCount())
        return;
      const CadLineRecord seg = cmd.drawing.line(li);
      const float x0 = static_cast<float>(seg.x0), y0 = static_cast<float>(seg.y0), z0 = static_cast<float>(seg.z0);
      const float x1 = static_cast<float>(seg.x1), y1 = static_cast<float>(seg.y1), z1 = static_cast<float>(seg.z1);
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
      if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.drawing.userPolylineOffsets.size())
        return;
      const int v0 = cmd.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = cmd.drawing.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      if (v1 - v0 < 2)
        return;
      const int movingVi = nearFirst ? v0 : (v1 - 1);
      const int fixedVi = nearFirst ? (v0 + 1) : (v1 - 2);
      const size_t mIdx = static_cast<size_t>(movingVi), fIdx = static_cast<size_t>(fixedVi);
      if (mIdx >= cmd.drawing.vertexCount() || fIdx >= cmd.drawing.vertexCount())
        return;
      const float fx = cmd.drawing.vertex(fIdx).x, fy = cmd.drawing.vertex(fIdx).y;
      const float mz = cmd.drawing.vertex(mIdx).z;
      const float mx = cmd.drawing.vertex(mIdx).x, my = cmd.drawing.vertex(mIdx).y;
      const float segLen = std::hypot(mx - fx, my - fy);
      if (segLen < 1e-9f)
        return;
      const float ux = (mx - fx) / segLen, uy = (my - fy) / segLen;
      const float proj = std::max((curX - fx) * ux + (curY - fy) * uy, 1e-6f);
      prevLines->push_back(fx);
      prevLines->push_back(fy);
      prevLines->push_back(cmd.drawing.vertex(fIdx).z);
      prevLines->push_back(fx + ux * proj);
      prevLines->push_back(fy + uy * proj);
      prevLines->push_back(mz);
      return;
    }
    if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.drawing.userArcs.size())
        return;
      CadArc a = cmd.drawing.userArcs[static_cast<size_t>(e.index)];
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

}

/// Highlight strokes for one entity, each at the entity's OWN elevation (REQ-058). This used to
/// take a flat `lineZ` draw depth; with the filled region converted, no type needs one — depth
/// testing is off, so draw ORDER decides occlusion and the Z is free to mean elevation.
template<typename LineVert>
static void AppendFloatLineBatch(const std::vector<float>& batch, std::vector<LineVert>* out) {
  for (float v : batch)
    out->push_back(static_cast<LineVert>(v));
}

/// The wireframe of entity \p e of drawing \p d: what the selection highlight traces, and what the
/// transform preview draws for each scratch copy (issue #1700). \p cmd supplies the display caches
/// (solid tessellation, surface borders, pipe-run solids), which belong to the live drawing only;
/// \p blockDefs the block definitions \p d's inserts name.
template<typename LineVert>
static void AppendEntityWireframe(const AppCommandState& cmd, const DrawingContent& d,
                                  const std::vector<CadBlockDefinition>& blockDefs, const SelectedEntity& e,
                                  std::vector<LineVert>* hlLines, std::vector<float>* hlCircles) {
  if (e.type == SelectedEntity::Type::LineSeg) {
    const size_t li = static_cast<size_t>(e.index);
    if (li >= d.lineCount())
      return;
    const CadLineRecord seg = d.line(li);
    for (int i = 0; i < 2; ++i) {
      hlLines->push_back(static_cast<LineVert>(LineEndComp(seg, i, 0)));
      hlLines->push_back(static_cast<LineVert>(LineEndComp(seg, i, 1)));
      // The entity's OWN Z, not a flat overlay depth (REQ-057/058). Drawing the highlight at a
      // fixed Z put it on the datum while the line sat at its elevation, so an orbited view showed
      // the object twice — once real, once highlighted in the wrong place.
      hlLines->push_back(static_cast<LineVert>(LineEndComp(seg, i, 2)));
    }
  } else if (e.type == SelectedEntity::Type::Circle) {
    const size_t ci = static_cast<size_t>(e.index);  // cx,cy,z,r
    if (ci >= d.circleCount())
      return;
    // Through the same emitter the previews use, so a selected tilted circle is highlighted on the
    // plane it is drawn on rather than ringed flat beside itself (REQ-312).
    float cnx = kFlatNormalX;
    float cny = kFlatNormalY;
    float cnz = kFlatNormalZ;
    CircleNormalAt(d.userCircleNormals, ci, &cnx, &cny, &cnz);
    std::vector<float> strip;
    appendPreviewCircle(&strip, hlCircles, d.circle(ci).cx, d.circle(ci).cy,
                        d.circle(ci).cz, d.circle(ci).r, cnx, cny, cnz);
    AppendFloatLineBatch(strip, hlLines);
  } else if (e.type == SelectedEntity::Type::Alignment) {  // REQ-480: highlight the exact geometry, sampled for display
    const size_t k = static_cast<size_t>(e.index);
    if (k >= d.alignments.size() || d.alignments[k].Empty())
      return;
    const std::vector<alignment::Point2> pts = d.alignments[k].SampleForDisplay(0.01);
    for (size_t i = 1; i < pts.size(); ++i) {
      hlLines->push_back(static_cast<float>(pts[i - 1].x));
      hlLines->push_back(static_cast<float>(pts[i - 1].y));
      hlLines->push_back(0.f);
      hlLines->push_back(static_cast<float>(pts[i].x));
      hlLines->push_back(static_cast<float>(pts[i].y));
      hlLines->push_back(0.f);
    }
  } else if (e.type == SelectedEntity::Type::Arc) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= d.userArcs.size())
      return;
    std::vector<float> strip;
    appendArcPolylineStrip(&strip, d.userArcs[k].z, d.userArcs[k], 48);  // arc plane, not a flat depth
    AppendFloatLineBatch(strip, hlLines);
  } else if (e.type == SelectedEntity::Type::Spline) {  // REQ-443
    std::vector<float> strip;
    appendSplinePreview(&strip, d, e.index, [&](const spline::Vec3& v) { return v; });
    AppendFloatLineBatch(strip, hlLines);
  } else if (e.type == SelectedEntity::Type::Ellipse) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= d.userEllipses.size())
      return;
    std::vector<float> strip;
    appendEllipsePolylineStrip(&strip, d.userEllipses[k].z, d.userEllipses[k], 56);
    AppendFloatLineBatch(strip, hlLines);
  } else if (e.type == SelectedEntity::Type::Polyline) {
    std::vector<float> strip;
    appendCommittedPolylineStrip(&strip, d, e.index);
    AppendFloatLineBatch(strip, hlLines);
  } else if (e.type == SelectedEntity::Type::FeatureLine) {
    // REQ-087. Without this a selected feature line drew no highlight at all: it would pick, box-
    // select and move correctly while giving the user nothing on screen to confirm what was
    // selected. Identity transform — the highlight traces the committed geometry where it is.
    std::vector<float> strip;
    appendFeatureLineStrip(&strip, d, e.index, [](float*, float*) {});
    AppendFloatLineBatch(strip, hlLines);
  } else if (e.type == SelectedEntity::Type::Surface) {
    // REQ-068 / ADR-036 (b). The highlight traces the surface's BORDER, not its 600k triangle edges.
    // Two reasons, and the second is the one that decides it: at REQ-100's profile the whole
    // triangulation re-emitted here every frame is ~14 MB per frame, and even if it were free, a
    // 200k-triangle mesh drawn in highlight yellow is a solid block rather than a shape you can read.
    // The border says "this surface, this extent, including the voids its hide-boundaries left."
    //
    // Read from the cache (ADR-036 (e)); computing it here would put the O(n log n) walk back on the
    // per-frame path this function is called from.
    if (&d != &cmd.drawing)
      return;
    if (const std::vector<float>* border = SurfaceBorderEdges(cmd, static_cast<size_t>(e.index)))
      AppendFloatLineBatch(*border, hlLines);
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
    if (k >= d.cadSolids.size() || !d.cadSolids[k])
      return;
    const CadSolidPtr& sp = d.cadSolids[k];
    for (const CadSolidTessellation& t : cmd.solidDisplayCache) {
      if (t.key.lock() != sp)
        continue;
      AppendFloatLineBatch(t.edgeVerts, hlLines);
      break;
    }
  } else if (e.type == SelectedEntity::Type::PipeRun) {
    // Issue #486. Same omission as the Solid branch above, for the same reason: without this a
    // selected pipe run drew no highlight at all. `e.index` is into `cadPipeRuns`, but the
    // rendered geometry lives in the DERIVED `pipeRunWorldSolids` array — walk that array for the
    // (at most one) solid this run actually built, matching `PickClosestSolidEntity`'s own mapping.
    if (&d != &cmd.drawing)
      return;
    for (size_t si = 0; si < cmd.pipeRunWorldSolids.size(); ++si) {
      if (si >= cmd.pipeRunWorldSolidOwnerIndex.size() ||
          cmd.pipeRunWorldSolidOwnerIndex[si] != e.index || !cmd.pipeRunWorldSolids[si])
        continue;
      const CadSolidPtr& sp = cmd.pipeRunWorldSolids[si];
      for (const CadSolidTessellation& t : cmd.solidDisplayCache) {
        if (t.key.lock() != sp)
          continue;
        AppendFloatLineBatch(t.edgeVerts, hlLines);
        break;
      }
      break;
    }
  } else if (e.type == SelectedEntity::Type::BlockRef) {
    // Issue #496 follow-up, same omission as the Solid case above and the one PickClosestCadEntity
    // itself had: a piping fitting block whose content is a 3D solid (no lines/circles) drew NO
    // highlight at all, hover or selected — only the gizmo. Trace BOTH the block's 2D content (the
    // ordinary case) and its solids' own edges (from the tessellation cache, same as the Solid
    // branch), so every block ref gets a highlight regardless of what its content is made of.
    const size_t k = static_cast<size_t>(e.index);
    if (k >= d.cadBlockRefs.size())
      return;
    const CadBlockRef& ref = d.cadBlockRefs[k];
    EntityAttributes dummy{};
    if (k < d.cadBlockRefAttrs.size())
      dummy = d.cadBlockRefAttrs[k];
    std::vector<CadBlockWorldSeg> segs;
    CadBlockCollectWorldLines(blockDefs, ref, dummy, &segs);
    for (const CadBlockWorldSeg& s : segs) {
      hlLines->push_back(static_cast<LineVert>(s.x0));
      hlLines->push_back(static_cast<LineVert>(s.y0));
      hlLines->push_back(static_cast<LineVert>(s.z0));
      hlLines->push_back(static_cast<LineVert>(s.x1));
      hlLines->push_back(static_cast<LineVert>(s.y1));
      hlLines->push_back(static_cast<LineVert>(s.z1));
    }
    // NOT the tessellation-cache lookup the Solid branch above uses: CadBlockCollectWorldSolids
    // allocates a brand-new transformed brep::Solid (and so a brand-new shared_ptr identity) on
    // every call, which would never match anything already keyed into cmd.solidDisplayCache by
    // RebuildBlockRefWorldSolids. Walk the edges directly instead — the same chord-count-24-per-arc
    // approach PickClosestCadEntity's own BlockRef case already uses for exactly this reason.
    std::vector<CadBlockWorldSolid> blockSolids;
    CadBlockCollectWorldSolids(blockDefs, ref, dummy, &blockSolids);
    for (const CadBlockWorldSolid& ws : blockSolids) {
      if (!ws.solid)
        continue;
      for (const brep::Edge& ed : ws.solid->edges) {
        const int steps = ed.kind == brep::CurveKind::Arc ? 24 : 1;
        ray3d::Vec3 prev = brep::EdgePointAt(*ws.solid, ed, 0.0);
        for (int i = 1; i <= steps; ++i) {
          const ray3d::Vec3 next = brep::EdgePointAt(*ws.solid, ed, static_cast<double>(i) / steps);
          hlLines->push_back(static_cast<LineVert>(prev.x));
          hlLines->push_back(static_cast<LineVert>(prev.y));
          hlLines->push_back(static_cast<LineVert>(prev.z));
          hlLines->push_back(static_cast<LineVert>(next.x));
          hlLines->push_back(static_cast<LineVert>(next.y));
          hlLines->push_back(static_cast<LineVert>(next.z));
          prev = next;
        }
      }
    }
  } else if (e.type == SelectedEntity::Type::FilledRegion) {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= d.cadFilledRegions.size())
      return;
    const CadFilledRegion& fr = d.cadFilledRegions[k];
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
        hlLines->push_back(static_cast<LineVert>(fr.vertsXyz[static_cast<size_t>(a) * 3]));
        hlLines->push_back(static_cast<LineVert>(fr.vertsXyz[static_cast<size_t>(a) * 3 + 1]));
        hlLines->push_back(static_cast<LineVert>(fr.vertsXyz[static_cast<size_t>(a) * 3 + 2]));
        hlLines->push_back(static_cast<LineVert>(fr.vertsXyz[static_cast<size_t>(b) * 3]));
        hlLines->push_back(static_cast<LineVert>(fr.vertsXyz[static_cast<size_t>(b) * 3 + 1]));
        hlLines->push_back(static_cast<LineVert>(fr.vertsXyz[static_cast<size_t>(b) * 3 + 2]));
      }
    }
  }
}

template<typename LineVert>
static void AppendEntityHighlight(const AppCommandState& cmd, const SelectedEntity& e,
                                  std::vector<LineVert>* hlLines, std::vector<float>* hlCircles) {
  AppendEntityWireframe(cmd, cmd.drawing, cmd.drawing.blockDefs, e, hlLines, hlCircles);
}

namespace {

/// Above this many triangles a mesh's preview is its box rather than every triangle edge: the
/// preview is redrawn on every cursor move.
constexpr std::size_t kMaxPreviewMeshTriangles = 20000;

void AppendMappedSeg(std::vector<float>* out, const GoSurvey::Document::SimilarityTransform& xf,
                     const ray3d::Vec3& a, const ray3d::Vec3& b) {
  const ray3d::Vec3 p = xf.applyPoint(a);
  const ray3d::Vec3 q = xf.applyPoint(b);
  out->insert(out->end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z),
                           static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z)});
}

/// A mesh's triangle edges (or its box, for a large mesh) mapped by \p xf.
void AppendMeshPreview(std::vector<float>* out, const CadMesh& m, const GoSurvey::Document::SimilarityTransform& xf) {
  const std::vector<float>& v = m.vertsXyz;
  const auto vert = [&](std::size_t i) { return ray3d::Vec3{v[i * 3], v[i * 3 + 1], v[i * 3 + 2]}; };
  const std::size_t nv = v.size() / 3;
  const std::size_t nt = m.indices.size() / 3;
  if (nt <= kMaxPreviewMeshTriangles) {
    for (std::size_t t = 0; t < nt; ++t) {
      const std::size_t i[3] = {m.indices[t * 3], m.indices[t * 3 + 1], m.indices[t * 3 + 2]};
      if (i[0] >= nv || i[1] >= nv || i[2] >= nv)
        continue;
      for (int k = 0; k < 3; ++k)
        AppendMappedSeg(out, xf, vert(i[k]), vert(i[(k + 1) % 3]));
    }
    return;
  }
  GoSurvey::Document::EntityBounds3d b;
  for (std::size_t i = 0; i < nv; ++i)
    b.expand(vert(i));
  if (!b.valid)
    return;
  const ray3d::Vec3 c[2] = {b.min, b.max};
  const auto corner = [&](int k) { return ray3d::Vec3{c[k & 1].x, c[(k >> 1) & 1].y, c[(k >> 2) & 1].z}; };
  for (int k = 0; k < 8; ++k)
    for (int bit = 1; bit < 8; bit <<= 1)
      if ((k & bit) == 0)
        AppendMappedSeg(out, xf, corner(k), corner(k | bit));
}

void AppendSeg(std::vector<float>* out, double ax, double ay, double az, double bx, double by, double bz) {
  out->insert(out->end(), {static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(az),
                           static_cast<float>(bx), static_cast<float>(by), static_cast<float>(bz)});
}

/// An XYZ path as segments. Spline-fit leaders are drawn through their path points.
void AppendPathSegs(std::vector<float>* out, const std::vector<float>& xyz) {
  for (std::size_t i = 0; i + 5 < xyz.size(); i += 3)
    AppendSeg(out, xyz[i], xyz[i + 1], xyz[i + 2], xyz[i + 3], xyz[i + 4], xyz[i + 5]);
}

/// A plus sign of half-size \p h at (x, y, z).
void AppendCross(std::vector<float>* out, double x, double y, double z, double h) {
  AppendSeg(out, x - h, y, z, x + h, y, z);
  AppendSeg(out, x, y - h, z, x, y + h, z);
}

/// The ghost of the kinds the selection highlight leaves to the annotation overlay (QtAnnotations
/// highlights them itself): a table's outline, a multileader's leader paths, a CAD point's marker
/// and a Position Marker's circle and cross, read from the scratch copy \p d. Their text is not
/// drawn here.
void AppendOverlayKindPreview(const AppCommandState& cmd, const DrawingContent& d, const SelectedEntity& e,
                              std::vector<float>* lines, std::vector<float>* circles) {
  const std::size_t k = static_cast<std::size_t>(e.index);
  const double plotted = std::max(static_cast<double>(cmd.drawing.modelUnitsPerPlottedInch), 1e-6);
  if (e.type == SelectedEntity::Type::Table) {
    if (k >= d.cadTables.size())
      return;
    const CadTable& t = d.cadTables[k];
    float x[4], y[4];
    for (int c = 0; c < 4; ++c)
      CadTableWorldCorner(t, c, &x[c], &y[c]);
    for (int c = 0; c < 4; ++c)
      AppendSeg(lines, x[c], y[c], t.insZ, x[(c + 1) % 4], y[(c + 1) % 4], t.insZ);
  } else if (e.type == SelectedEntity::Type::Multileader) {
    if (k >= d.cadMultileaders.size())
      return;
    const CadMultileader& ml = d.cadMultileaders[k];
    AppendPathSegs(lines, ml.pathXyz);
    for (const std::vector<float>& path : ml.extraLeaderPaths)
      AppendPathSegs(lines, path);
  } else if (e.type == SelectedEntity::Type::Point) {
    if (k >= d.cadPoints.size())
      return;
    const CadPoint& p = d.cadPoints[k];
    const double h = cmd.drawing.pdSize > 0.0 ? cmd.drawing.pdSize * 0.5 : kPositionMarkerPlottedRadiusIn * plotted;
    AppendCross(lines, p.x, p.y, p.z, h);
  } else if (e.type == SelectedEntity::Type::PositionMarker) {
    if (k >= d.cadPositionMarkers.size())
      return;
    const CadPositionMarker& m = d.cadPositionMarkers[k];
    const double r = kPositionMarkerPlottedRadiusIn * plotted;
    std::vector<float> strip;
    appendPreviewCircle(&strip, circles, static_cast<float>(m.x), static_cast<float>(m.y), m.z,
                        static_cast<float>(r), kFlatNormalX, kFlatNormalY, kFlatNormalZ);
    lines->insert(lines->end(), strip.begin(), strip.end());
    AppendCross(lines, m.x, m.y, m.z, r);
  }
}

}  // namespace

/// The preview's whole budget, in floats across both buffers (about a million segments). It is
/// rebuilt on every cursor move, so an ARRAY of a large selection stops adding copies here rather
/// than stalling the viewport; the copies it does draw are still exactly the commit's.
constexpr std::size_t kMaxPreviewFloats = 6000000;

/// The wireframe of every copy in \p scratch (not its cached solids, pipe runs and meshes).
static void AppendScratchCopies(const AppCommandState& cmd, const CadTransformPreviewScratch& scratch,
                                std::vector<float>* prevLines, std::vector<float>* prevCircles) {
  const std::vector<CadBlockDefinition>& blockDefs = scratch.source ? scratch.source->blockDefs : cmd.drawing.blockDefs;
  for (const SelectedEntity& e : scratch.entities) {
    AppendEntityWireframe(cmd, scratch.drawing, blockDefs, e, prevLines, prevCircles);
    AppendOverlayKindPreview(cmd, scratch.drawing, e, prevLines, prevCircles);
  }
}

static void AppendTransformPreview(const AppCommandState& cmd, const CadTransformPreviewPlan& plan,
                                   std::vector<float>* prevLines, std::vector<float>* prevCircles) {
  // One instance at a time, so the budget can stop an ARRAY part way.
  CadTransformPreviewPlan one = plan;
  CadTransformPreviewScratch scratch;
  // A solid's or pipe run's display wireframe, read once and moved by each instance's transform.
  std::map<std::pair<int, int>, std::vector<float>> displayLines;
  for (const GoSurvey::Document::SimilarityTransform& xf : plan.instances) {
    if (prevLines->size() + prevCircles->size() > kMaxPreviewFloats)
      return;
    one.instances.assign(1, xf);
    BuildCadTransformPreviewScratch(cmd, one, &scratch);
    if (!scratch.source)
      return;
    AppendScratchCopies(cmd, scratch, prevLines, prevCircles);
    // Solids, pipe runs and meshes: their existing display geometry, moved by the same transform.
    for (const CadTransformPreviewScratch::CachedDisplay& c : scratch.cached) {
      if (c.entity.type == SelectedEntity::Type::Mesh) {
        const auto& meshes = scratch.source->cadMeshes;
        const std::size_t k = static_cast<std::size_t>(c.entity.index);
        if (k < meshes.size() && meshes[k])
          AppendMeshPreview(prevLines, *meshes[k], c.xf);
        continue;
      }
      auto [it, added] = displayLines.try_emplace({static_cast<int>(c.entity.type), c.entity.index});
      if (added) {
        std::vector<float> circles;
        AppendEntityWireframe(cmd, *scratch.source, scratch.source->blockDefs, c.entity, &it->second, &circles);
      }
      const std::vector<float>& lines = it->second;
      for (std::size_t i = 0; i + 5 < lines.size(); i += 6)
        AppendMappedSeg(prevLines, c.xf, {lines[i], lines[i + 1], lines[i + 2]},
                        {lines[i + 3], lines[i + 4], lines[i + 5]});
    }
  }
}

void BuildSelectionHighlight(const AppCommandState& cmd, std::vector<double>* hlLines,
                             std::vector<float>* hlCircles) {
  hlLines->clear();
  hlCircles->clear();
  for (const auto& e : cmd.selection)
    AppendEntityHighlight(cmd, e, hlLines, hlCircles);
  if (const auto* ofs = cmd.sessionAs<stepengine::OffsetSession>();
      ofs && ofs->phase == stepengine::OffsetPhase::WaitSelectEntity &&
      ofs->hoverHighlightValid)
    AppendEntityHighlight(cmd, ofs->hoverEntity, hlLines, hlCircles);
  if (const auto* trim = cmd.sessionAs<stepengine::TrimSession>()) {
    for (const auto& c : trim->cutters)
      AppendEntityHighlight(cmd, c, hlLines, hlCircles);
  }
  if (const auto* ext = cmd.sessionAs<stepengine::ExtendSession>()) {
    for (const auto& c : ext->boundaries)
      AppendEntityHighlight(cmd, c, hlLines, hlCircles);
  }
  if (const auto* brk = cmd.sessionAs<stepengine::BreakSession>();
      brk && brk->phase == stepengine::BreakPhase::SelectSecondPoint)
    AppendEntityHighlight(cmd, brk->entity, hlLines, hlCircles);
  if (const auto* len = cmd.sessionAs<stepengine::LengthenSession>();
      len && len->phase == stepengine::LengthenPhase::WaitDynamicTarget)
    AppendEntityHighlight(cmd, len->pendingEntity, hlLines, hlCircles);
  if (const auto* fil = cmd.sessionAs<stepengine::FilletSession>();
      fil && fil->phase == stepengine::FilletPhase::WaitSecondEntity)
    AppendEntityHighlight(cmd, fil->firstEntity, hlLines, hlCircles);
  if (const auto* chm = cmd.sessionAs<stepengine::ChamferSession>();
      chm && chm->phase == stepengine::ChamferPhase::WaitSecondEntity)
    AppendEntityHighlight(cmd, chm->firstEntity, hlLines, hlCircles);
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
    if (s.solidIndex < 0 || static_cast<size_t>(s.solidIndex) >= cmd.drawing.cadSolids.size())
      return;
    const CadSolidPtr& sp = cmd.drawing.cadSolids[static_cast<size_t>(s.solidIndex)];
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

void BuildSubObjectFaceGhost(const AppCommandState& cmd, std::vector<float>* preview) {
  if (!preview)
    return;
  preview->clear();
  // Only while a gizmo drag is armed ON A FACE. The square handle this function used to draw is
  // gone: the gizmo's arrow is the handle now (issue #148 acceptance 4), and two handles for one
  // operation is worse than either.
  if (!cmd.gizmoDragActive || !cmd.gizmoDragIsSubObject)
    return;
  const SelectedSubObject& ref = cmd.gizmoDragSubObject;
  const ray3d::Vec3 anchor = cmd.gizmoAnchor;
  const ray3d::Vec3 delta = ray3d::Scale(cmd.gizmoAxisDir, cmd.gizmoDragDistance);
  const CadSolidPtr sp = ref.owner.lock();
  if (!sp || ref.index < 0 || static_cast<size_t>(ref.index) >= sp->faces.size())
    return;
  // The face's boundary where it would land. Translated, not rebuilt: `brep::PushPullFace` copies
  // the whole solid and validates it, which is the right cost once on commit and the wrong cost
  // every frame of a drag.
  //
  // It is a PICTURE of the distance, not a promise about the result: a real push also re-solves
  // every neighbouring face, and the corners of this outline are where the face's own vertices
  // would go if nothing else moved. That is the honest thing to show for the same reason the ghost
  // is not the rebuilt solid — the rebuild can still be refused, and it happens on commit where a
  // refusal can be reported (ADR-046 (d)).
  for (const brep::Loop& loop : sp->faces[static_cast<size_t>(ref.index)].loops) {
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
  // A leader from the face's centroid to where it is going, so the drag distance is readable even
  // when the moved boundary happens to sit behind other geometry.
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
  if (const auto* trim = cmd.sessionAs<stepengine::TrimSession>()) {
    for (const auto& c : trim->cutters) {
      if (c.type == e.type && c.index == e.index)
        return;
    }
  }
  if (const auto* ext = cmd.sessionAs<stepengine::ExtendSession>()) {
    for (const auto& c : ext->boundaries) {
      if (c.type == e.type && c.index == e.index)
        return;
    }
  }
  if (const auto* brk = cmd.sessionAs<stepengine::BreakSession>();
      brk && brk->phase == stepengine::BreakPhase::SelectSecondPoint &&
      brk->entity.type == e.type && brk->entity.index == e.index)
    return;
  if (const auto* len = cmd.sessionAs<stepengine::LengthenSession>();
      len && len->phase == stepengine::LengthenPhase::WaitDynamicTarget &&
      len->pendingEntity.type == e.type && len->pendingEntity.index == e.index)
    return;
  if (const auto* fil = cmd.sessionAs<stepengine::FilletSession>();
      fil && fil->phase == stepengine::FilletPhase::WaitSecondEntity &&
      fil->firstEntity.type == e.type && fil->firstEntity.index == e.index)
    return;
  if (const auto* chm = cmd.sessionAs<stepengine::ChamferSession>();
      chm && chm->phase == stepengine::ChamferPhase::WaitSecondEntity &&
      chm->firstEntity.type == e.type && chm->firstEntity.index == e.index)
    return;
  AppendEntityHighlight(cmd, e, hoverLines, hoverCircles);
}

void BuildBoxPreviewHighlight(const AppCommandState& cmd, std::vector<float>* previewLines,
                              std::vector<float>* previewCircles) {
  previewLines->clear();
  previewCircles->clear();
  if (!cmd.selBoxWaitingSecond)
    return;
  for (const auto& e : cmd.selBoxPreview) {
    // Already selected: the selection stroke is drawn over this anyway, so skip the double draw.
    const bool selected = std::any_of(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& s) {
      return s.type == e.type && s.index == e.index;
    });
    if (!selected)
      AppendEntityHighlight(cmd, e, previewLines, previewCircles);
  }
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
    out->plane[i].clear();
    out->planeHot[i] = false;
  }
  out->guide.clear();
  out->center.clear();
  out->faceMode = false;
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
  const int axisCount = CadGizmoAxisCountFor(cmd);
  out->faceMode = CadGizmoModeFor(cmd) == CadGizmoMode::SubObjectFace;
  out->soloOp = cmd.gizmoOp == CadGizmoOp::Rotate ? 1 : cmd.gizmoOp == CadGizmoOp::Scale ? 2 : 0;

  // The base point: a small circle round the anchor in the active UCS's XY plane, as AutoCAD marks
  // it (D-2026-09-28-c). Whole entities only — a sub-object's handle sits where its edit acts.
  if (CadGizmoModeFor(cmd) == CadGizmoMode::Entity) {
    const ray3d::Vec3 n = ray3d::Normalize(cmd.activeUcs.zAxis);
    ray3d::Vec3 seed{0.0, 0.0, 1.0};
    if (std::fabs(ray3d::Dot(n, seed)) > 0.9)
      seed = ray3d::Vec3{1.0, 0.0, 0.0};
    const ray3d::Vec3 e0 = ray3d::Normalize(ray3d::Cross(seed, n));
    const ray3d::Vec3 e1 = ray3d::Cross(n, e0);
    const double r = len * 0.1;
    constexpr int kSegs = 24;
    ray3d::Vec3 prev{};
    for (int i = 0; i <= kSegs; ++i) {
      const double th = 6.28318530717958647692 * static_cast<double>(i) / static_cast<double>(kSegs);
      const ray3d::Vec3 pt =
          ray3d::Add(anchor, ray3d::Add(ray3d::Scale(e0, std::cos(th) * r), ray3d::Scale(e1, std::sin(th) * r)));
      if (i > 0)
        GizmoSeg(&out->center, prev, pt);
      prev = pt;
    }
  }

  // ROTATE: a RING in the plane the rotation happens in, rather than an arrow. An arrow says "drag
  // along me" and this gesture is "drag around me" — the widget has to state which, because the two
  // read identically once the view is orbited.
  //
  // THREE rings, one per active-UCS axis, each in the plane perpendicular to it and in that axis's
  // colour (D-2026-09-28-b, the AutoCAD 3DROTATE widget the user asked for).
  if (cmd.gizmoOp == CadGizmoOp::Rotate) {
    constexpr int kRingSegs = 64;
    constexpr double kTwoPi = 6.28318530717958647692;
    for (int a = 0; a < axisCount; ++a) {
      const ray3d::Vec3 n = (cmd.gizmoDragActive && a == cmd.gizmoDragAxis) ? cmd.gizmoAxisDir
                                                                           : CadGizmoAxisWorld(cmd, a);
      // The ring's own frame, built from the AXIS and not the camera, so it does not swim when the
      // view orbits — the same choice the arrowheads below make.
      ray3d::Vec3 seed{0.0, 0.0, 1.0};
      if (std::fabs(ray3d::Dot(n, seed)) > 0.9)
        seed = ray3d::Vec3{1.0, 0.0, 0.0};
      const ray3d::Vec3 e0 = ray3d::Normalize(ray3d::Cross(seed, n));
      const ray3d::Vec3 e1 = ray3d::Cross(n, e0);
      out->hot[a] = (a == lit);
      ray3d::Vec3 prev{};
      for (int i = 0; i <= kRingSegs; ++i) {
        const double th = kTwoPi * static_cast<double>(i) / static_cast<double>(kRingSegs);
        const ray3d::Vec3 p = ray3d::Add(
            anchor, ray3d::Add(ray3d::Scale(e0, std::cos(th) * len), ray3d::Scale(e1, std::sin(th) * len)));
        if (i > 0)
          GizmoSeg(&out->axis[a], prev, p);
        prev = p;
      }
    }
    return;  // no arrow shafts, no drag guide: a rotation has no track to slide along
  }

  for (int a = 0; a < axisCount; ++a) {
    // Mid-drag the direction is the one CAPTURED at the grab, not one re-derived from the live
    // selection: in face mode that direction comes from the face's own normal, and re-deriving it
    // would read it off geometry the drag is about to change.
    const ray3d::Vec3 u = (cmd.gizmoDragActive && a == cmd.gizmoDragAxis) ? cmd.gizmoAxisDir
                                                                         : CadGizmoAxisWorld(cmd, a);
    const ray3d::Vec3 tip = ray3d::Add(anchor, ray3d::Scale(u, len));
    out->hot[a] = (a == lit);
    // The head is built in the AXIS's own frame rather than the camera's, so it is part of the
    // widget's geometry and does not swim when the view orbits — the opposite choice from a survey
    // point's marker cross, which is a screen-space annotation and should billboard.
    ray3d::Vec3 seed{0.0, 0.0, 1.0};
    if (std::fabs(ray3d::Dot(u, seed)) > 0.9)
      seed = ray3d::Vec3{1.0, 0.0, 0.0};
    const ray3d::Vec3 p = ray3d::Normalize(ray3d::Cross(u, seed));
    const ray3d::Vec3 q = ray3d::Cross(u, p);
    if (cmd.gizmoOp == CadGizmoOp::Scale) {
      // SCALE: a small BOX on the tip, the AutoCAD 3DSCALE look (D-2026-09-28-c) — a box says
      // "resize", an arrowhead says "slide".
      const double h = len * 0.06;
      const ray3d::Vec3 c = ray3d::Sub(tip, ray3d::Scale(u, h));
      GizmoSeg(&out->axis[a], anchor, ray3d::Sub(c, ray3d::Scale(u, h)));
      ray3d::Vec3 corner[8];
      for (int k = 0; k < 8; ++k)
        corner[k] = ray3d::Add(
            c, ray3d::Add(ray3d::Scale(u, (k & 1) ? h : -h),
                          ray3d::Add(ray3d::Scale(p, (k & 2) ? h : -h), ray3d::Scale(q, (k & 4) ? h : -h))));
      for (int k = 0; k < 8; ++k)
        for (int bit = 1; bit < 8; bit <<= 1)
          if (!(k & bit))
            GizmoSeg(&out->axis[a], corner[k], corner[k | bit]);
      continue;
    }
    // MOVE (and a sub-object's handles): a shaft and a CONE — twelve spokes and a base ring, the
    // closest a line-only overlay gets to AutoCAD's solid 3DMOVE arrowhead (D-2026-09-28-c).
    const double back = len * 0.78;
    const double flare = len * 0.07;
    const ray3d::Vec3 base = ray3d::Add(anchor, ray3d::Scale(u, back));
    GizmoSeg(&out->axis[a], anchor, base);
    constexpr int kConeSpokes = 12;
    constexpr double kTwoPiCone = 6.28318530717958647692;
    ray3d::Vec3 prevRim{};
    for (int i = 0; i <= kConeSpokes; ++i) {
      const double th = kTwoPiCone * static_cast<double>(i) / static_cast<double>(kConeSpokes);
      const ray3d::Vec3 rim = ray3d::Add(
          base, ray3d::Add(ray3d::Scale(p, std::cos(th) * flare), ray3d::Scale(q, std::sin(th) * flare)));
      if (i < kConeSpokes)
        GizmoSeg(&out->axis[a], tip, rim);
      if (i > 0)
        GizmoSeg(&out->axis[a], prevRim, rim);
      prevRim = rim;
    }
  }
  // The corner marks between each pair of axes (D-2026-09-28-c). Under TRANSLATE they are the plane
  // handles — the square `PickGizmoAxis` tests, drawn as the two far sides of it, each in the colour
  // of the axis it runs along. Under SCALE they are AutoCAD's triangle between two axes: a line from
  // half-way up one to half-way up the other, split at its middle into the two axes' colours.
  if (CadGizmoModeFor(cmd) == CadGizmoMode::Entity && cmd.gizmoOp != CadGizmoOp::Rotate) {
    for (int pl = 0; pl < 3; ++pl) {
      int ia = 0;
      int ib = 0;
      CadGizmoPlaneAxes(pl, &ia, &ib);
      const ray3d::Vec3 ua = CadGizmoAxisWorld(cmd, ia);
      const ray3d::Vec3 ub = CadGizmoAxisWorld(cmd, ib);
      if (cmd.gizmoOp == CadGizmoOp::Translate) {
        const double s = len * kGizmoPlaneHandleFrac;
        const ray3d::Vec3 corner = ray3d::Add(anchor, ray3d::Add(ray3d::Scale(ua, s), ray3d::Scale(ub, s)));
        GizmoSeg(&out->plane[pl], ray3d::Add(anchor, ray3d::Scale(ub, s)), corner);  // along A
        GizmoSeg(&out->plane[pl], ray3d::Add(anchor, ray3d::Scale(ua, s)), corner);  // along B
        out->planeHot[pl] = (lit == kGizmoPlaneHandleFirst + pl);
      } else {
        const ray3d::Vec3 pa = ray3d::Add(anchor, ray3d::Scale(ua, len * 0.5));
        const ray3d::Vec3 pb = ray3d::Add(anchor, ray3d::Scale(ub, len * 0.5));
        const ray3d::Vec3 mid = ray3d::Scale(ray3d::Add(pa, pb), 0.5);
        GizmoSeg(&out->plane[pl], pa, mid);
        GizmoSeg(&out->plane[pl], pb, mid);
      }
    }
  }
  if (cmd.gizmoDragActive && cmd.gizmoDragAxis < kGizmoPlaneHandleFirst) {
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
  // The transform the drag would commit (CommitGizmoDrag), applied to a scratch copy of the selection
  // exactly as the typed MOVE, ROTATE and SCALE previews are (issue #1700). The ghost is then, by
  // construction, the result the release produces, and it leaves out what the commit refuses.
  CadTransformPreviewPlan plan;
  if (CadGizmoDragPreviewPlan(cmd, &plan))
    AppendTransformPreview(cmd, plan, outLines, outCircles);
}
