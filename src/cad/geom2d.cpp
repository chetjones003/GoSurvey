#include "geom2d.hpp"

#include <algorithm>
#include <cmath>

bool SegIntersectsAABB(float x0, float y0, float x1, float y1, float mnX, float mxX, float mnY, float mxY) {
  auto ins = [&](float x, float y) { return x >= mnX && x <= mxX && y >= mnY && y <= mxY; };
  if (ins(x0, y0) || ins(x1, y1))
    return true;
  auto edgeHit = [&](float ex0, float ey0, float ex1, float ey1) {
    float dxs = x1 - x0;
    float dys = y1 - y0;
    float dxe = ex1 - ex0;
    float dye = ey1 - ey0;
    float denom = dxs * dye - dys * dxe;
    if (std::fabs(denom) < 1e-12f)
      return false;
    float t = ((ex0 - x0) * dye - (ey0 - y0) * dxe) / denom;
    float u = ((ex0 - x0) * dys - (ey0 - y0) * dxs) / denom;
    return t >= 0.f && t <= 1.f && u >= 0.f && u <= 1.f;
  };
  if (edgeHit(mnX, mnY, mxX, mnY))
    return true;
  if (edgeHit(mxX, mnY, mxX, mxY))
    return true;
  if (edgeHit(mxX, mxY, mnX, mxY))
    return true;
  if (edgeHit(mnX, mxY, mnX, mnY))
    return true;
  return false;
}

bool CircleIntersectsAABB(float cx, float cy, float r, float mnX, float mxX, float mnY, float mxY) {
  if (r <= 0.f)
    return false;
  // Crossing fence: use the circle *curve* (circumference), not the filled disk. The old disk test
  // selected circles whenever the box lay inside the disk (closest point in rect to center < r),
  // even when the box never touched the visible circle.
  const float qx = std::clamp(cx, mnX, mxX);
  const float qy = std::clamp(cy, mnY, mxY);
  const float dx0 = cx - qx;
  const float dy0 = cy - qy;
  const float dMinSq = dx0 * dx0 + dy0 * dy0;
  auto cornerDsq = [&](float x, float y) {
    const float dx = cx - x;
    const float dy = cy - y;
    return dx * dx + dy * dy;
  };
  float dMaxSq = cornerDsq(mnX, mnY);
  dMaxSq = std::max(dMaxSq, cornerDsq(mxX, mnY));
  dMaxSq = std::max(dMaxSq, cornerDsq(mxX, mxY));
  dMaxSq = std::max(dMaxSq, cornerDsq(mnX, mxY));
  const float r2 = r * r;
  const float tol = 1e-5f * (std::fabs(r2) + 1.f);
  return dMinSq <= r2 + tol && r2 <= dMaxSq + tol;
}

bool CircleFullyInsideRect(float cx, float cy, float r, float mnX, float mxX, float mnY, float mxY) {
  if (r <= 0.f)
    return false;
  return cx - r >= mnX && cx + r <= mxX && cy - r >= mnY && cy + r <= mxY;
}

bool PointInsideClosedRect(float x, float y, float mnX, float mxX, float mnY, float mxY) {
  return x >= mnX && x <= mxX && y >= mnY && y <= mxY;
}
