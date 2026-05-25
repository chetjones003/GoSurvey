#pragma once

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

/// Circle/arc chord count from on-screen size (~8 px per segment, clamped).
[[nodiscard]] int CircleTessellationSegmentCount(double radiusWorld, double orthoHalfHeightWorld,
                                                 int framebufferHeightPx);

/// Point on a circle in world space (double precision: center + radius * trig).
void CirclePointWorld(double cx, double cy, double r, double angleRad, double* outX, double* outY);

void AppendLineSeg3(std::vector<float>& out, double x0, double y0, double z, double x1, double y1);

/// Tessellated arc/ellipse as GL_LINES triplets in world coordinates (double math).
void AppendArcLineSegments(std::vector<float>& out, double cx, double cy, double r, double startRad, double sweepRad,
                           int segments, float z);
void AppendEllipseLineSegments(std::vector<float>& out, double cx, double cy, double majVx, double majVy, double ratio,
                               int segments, float z);

/// Expand an axis-aligned bbox (double); \p any set true on first point.
void ExpandExtents(double x, double y, double* mnX, double* mxX, double* mnY, double* mxY, bool* any);
