#pragma once

/// Axis-aligned box tests and segment intersection (world/plot units, no CAD session state).

bool SegIntersectsAABB(float x0, float y0, float x1, float y1, float mnX, float mxX, float mnY, float mxY);

bool CircleIntersectsAABB(float cx, float cy, float r, float mnX, float mxX, float mnY, float mxY);

bool CircleFullyInsideRect(float cx, float cy, float r, float mnX, float mxX, float mnY, float mxY);

bool PointInsideClosedRect(float x, float y, float mnX, float mxX, float mnY, float mxY);
