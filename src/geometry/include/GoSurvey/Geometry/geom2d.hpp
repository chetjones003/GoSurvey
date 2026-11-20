#pragma once

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

/// Axis-aligned box tests and segment intersection (world/plot units, no CAD session state).

bool SegIntersectsAABB(float x0, float y0, float x1, float y1, float mnX, float mxX, float mnY, float mxY);

bool CircleIntersectsAABB(float cx, float cy, float r, float mnX, float mxX, float mnY, float mxY);

bool CircleFullyInsideRect(float cx, float cy, float r, float mnX, float mxX, float mnY, float mxY);

bool PointInsideClosedRect(float x, float y, float mnX, float mxX, float mnY, float mxY);

/// World XY minus a view anchor, cast to float (keeps sub-unit detail near large state-plane values).
void WorldToViewRelativeFloat(double worldX, double worldY, double anchorX, double anchorY, float* relX,
                              float* relY);

/// Circle point in view-relative coords: (local center − anchor) + r·(cos, sin). Stable at high zoom.
void CirclePointViewRel(double localCx, double localCy, double anchorX, double anchorY, double r, double angleRad,
                        float* relX, float* relY);

/// Circle/arc chord count from on-screen size (~4 px per segment, clamped).
/// \param maxSegmentCap upper clamp on returned segment count (AutoCAD-style "Arc and circle smoothness" / VIEWRES).
[[nodiscard]] int CircleTessellationSegmentCount(double radiusWorld, double orthoHalfHeightWorld,
                                                 int framebufferHeightPx, int maxSegmentCap = 512);

/// Point on a circle in world space (double precision: center + radius * trig).
void CirclePointWorld(double cx, double cy, double r, double angleRad, double* outX, double* outY);

/// Circular arc that a DXF-style bulge (`tan(theta/4)`) turns a chord p0->p1 into (ADR-047).
/// `valid` is false for a straight segment (|bulge| ~ 0), a degenerate chord, or a zero sweep —
/// callers then treat p0->p1 as a line. Angles are radians; `sweep` is signed (CCW positive) and
/// its magnitude is the arc's included angle. All math is double precision.
struct BulgeArcSpan {
  bool valid = false;
  double cx = 0.0, cy = 0.0;  // arc centre
  double radius = 0.0;
  double startAngle = 0.0;  // atan2(p0 - centre)
  double sweep = 0.0;       // signed; endAngle = startAngle + sweep
};
[[nodiscard]] BulgeArcSpan BulgeArc(double x0, double y0, double x1, double y1, double bulge);

/// Walks the chord p0→p1 as a line, or as \p segs equal arc steps when the bulge is a real arc.
/// \p addSeg is called as addSeg(xA, yA, xB, yB) for each sample chord (paper overlay and PDF plot).
template <class SegFn>
void ForEachBulgeChord(double x0, double y0, double x1, double y1, double bulge, int segs, SegFn&& addSeg) {
  assert(segs > 0);
  assert(std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1) && std::isfinite(y1));
  const BulgeArcSpan span = BulgeArc(x0, y0, x1, y1, bulge);
  if (!span.valid) {
    addSeg(x0, y0, x1, y1);
    return;
  }
  double px = x0;
  double py = y0;
  for (int i = 1; i <= segs; ++i) {
    const double ang = span.startAngle + span.sweep * static_cast<double>(i) / static_cast<double>(segs);
    const double qx = span.cx + span.radius * std::cos(ang);
    const double qy = span.cy + span.radius * std::sin(ang);
    addSeg(px, py, qx, qy);
    px = qx;
    py = qy;
  }
}

/// Arc length of the segment p0->p1 carrying `bulge`; the plain chord length when it is straight.
[[nodiscard]] double BulgeSegmentLength(double x0, double y0, double x1, double y1, double bulge);

/// The DXF bulge for an arc from (ax,ay) to (bx,by) that also passes through (px,py) — used when a
/// grip drags an arc segment's midpoint. 0 when the three points are collinear or degenerate.
[[nodiscard]] double ArcBulgeThrough(double ax, double ay, double px, double py, double bx, double by);

/// Exact squared distance from (px,py) to the circular arc `span` (centre, radius, start, sweep).
/// Analytic — no sampling — so a hover/pick aperture means the same thing on a curve as on a line.
[[nodiscard]] double PointArcDistanceSq(double px, double py, const BulgeArcSpan& span);

void AppendLineSeg3(std::vector<float>& out, double x0, double y0, double z, double x1, double y1);

/// Tessellated arc/ellipse as GL_LINES triplets in world coordinates (double math).
void AppendArcLineSegments(std::vector<float>& out, double cx, double cy, double r, double startRad, double sweepRad,
                           int segments, float z);
void AppendEllipseLineSegments(std::vector<float>& out, double cx, double cy, double majVx, double majVy, double ratio,
                               int segments, float z);

/// Expand an axis-aligned bbox (double); \p any set true on first point.
void ExpandExtents(double x, double y, double* mnX, double* mxX, double* mnY, double* mxY, bool* any);

/// REQ-444 REVCLOUD. The closed outline of scalloped arcs round the corner list \p xy (x,y pairs, at
/// least three). Each edge is cut into the fewest equal scallops whose ARC length is no more than
/// \p arcLength; the arcs bulge away from the enclosed area, or into it when \p reverse. \p outXy and
/// \p outBulge receive one entry per vertex of the closed polyline (the bulge of the segment that
/// LEAVES that vertex, REQ-316), so they drop straight into the polyline store.
/// False, with the reason in \p why, for fewer than three distinct corners, a non-positive or
/// non-finite arc length, a path that encloses no area, or an arc length so small the cloud would
/// need more than kRevisionCloudMaxScallops arcs.
inline constexpr int kRevisionCloudMaxScallops = 20000;
/// The bulge (`tan(theta/4)`) every scallop carries: about a 106 degree arc, the familiar cloud look.
inline constexpr double kRevisionCloudBulge = 0.5;
/// How much longer a scallop's ARC is than the chord it spans, for \ref kRevisionCloudBulge.
[[nodiscard]] double RevisionCloudArcPerChord();
[[nodiscard]] bool BuildRevisionCloud(const std::vector<double>& xy, double arcLength, bool reverse,
                                      std::vector<double>* outXy, std::vector<float>* outBulge,
                                      std::string* why);
