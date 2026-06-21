#pragma once

#include <cmath>
#include <cstddef>

#include "CadEntities.hpp"

// Pure geometry helpers for filled regions (solid hatches), dependency-free so the command layer
// (CadCommands.cpp) and the unit tests can both use them without linking the GUI (REQ-042, ADR-016).
// They operate only on CadFilledRegion's local-coordinate loop data — loop 0 is the outer boundary,
// any further loops are holes (islands), matching ADR-011's storage.

namespace hatchgeom {

/// Ray-cast point-in-polygon over a single loop \p loop of \p fr (local coordinates).
inline bool PointInLoop(const CadFilledRegion& fr, std::size_t loop, double x, double y) {
  if (loop >= fr.loopStart.size())
    return false;
  const int begin = fr.loopStart[loop];
  const int cnt = fr.loopCount(loop);
  if (cnt < 3)
    return false;
  bool in = false;
  for (int i = 0, j = cnt - 1; i < cnt; j = i++) {
    const double xi = fr.verts[static_cast<std::size_t>(begin + i) * 2];
    const double yi = fr.verts[static_cast<std::size_t>(begin + i) * 2 + 1];
    const double xj = fr.verts[static_cast<std::size_t>(begin + j) * 2];
    const double yj = fr.verts[static_cast<std::size_t>(begin + j) * 2 + 1];
    if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
      in = !in;
  }
  return in;
}

/// True when (x,y) is inside the outer loop (0) and outside every hole loop.
inline bool ContainsPoint(const CadFilledRegion& fr, double x, double y) {
  if (fr.loopStart.empty())
    return false;
  if (!PointInLoop(fr, 0, x, y))
    return false;
  for (std::size_t k = 1; k < fr.loopStart.size(); ++k)
    if (PointInLoop(fr, k, x, y))
      return false;  // inside a hole
  return true;
}

/// Absolute area of the outer loop — used to prefer the smallest enclosing fill when fills overlap.
inline double OuterAreaAbs(const CadFilledRegion& fr) {
  if (fr.loopStart.empty())
    return 0.0;
  const int begin = fr.loopStart[0];
  const int cnt = fr.loopCount(0);
  if (cnt < 3)
    return 0.0;
  double a = 0.0;
  for (int i = 0, j = cnt - 1; i < cnt; j = i++) {
    const double xi = fr.verts[static_cast<std::size_t>(begin + i) * 2];
    const double yi = fr.verts[static_cast<std::size_t>(begin + i) * 2 + 1];
    const double xj = fr.verts[static_cast<std::size_t>(begin + j) * 2];
    const double yj = fr.verts[static_cast<std::size_t>(begin + j) * 2 + 1];
    a += xj * yi - xi * yj;
  }
  return std::fabs(a) * 0.5;
}

/// Outer-loop axis-aligned bounds. Returns false (and leaves outputs untouched) for a degenerate region.
inline bool OuterBounds(const CadFilledRegion& fr, float* mnX, float* mnY, float* mxX, float* mxY) {
  if (fr.loopStart.empty())
    return false;
  const int begin = fr.loopStart[0];
  const int cnt = fr.loopCount(0);
  if (cnt < 3)
    return false;
  float lmnX = 1e30f, lmxX = -1e30f, lmnY = 1e30f, lmxY = -1e30f;
  for (int v = 0; v < cnt; ++v) {
    const float x = fr.verts[static_cast<std::size_t>(begin + v) * 2];
    const float y = fr.verts[static_cast<std::size_t>(begin + v) * 2 + 1];
    lmnX = std::fmin(lmnX, x); lmxX = std::fmax(lmxX, x);
    lmnY = std::fmin(lmnY, y); lmxY = std::fmax(lmxY, y);
  }
  *mnX = lmnX; *mnY = lmnY; *mxX = lmxX; *mxY = lmxY;
  return true;
}

/// Translate every loop vertex by (dx,dy).
inline void Translate(CadFilledRegion& fr, float dx, float dy) {
  for (std::size_t v = 0; v + 1 < fr.verts.size(); v += 2) {
    fr.verts[v] += dx;
    fr.verts[v + 1] += dy;
  }
}

}  // namespace hatchgeom
