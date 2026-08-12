#pragma once

#include <cmath>

// ORTHO constraint (REQ-047). When \p ortho is true, snap the point (*x,*y) onto the horizontal or
// vertical line through the anchor — whichever axis the cursor is farther along — matching AutoCAD's
// ORTHO. When \p ortho is FALSE this is a NO-OP: the point keeps its true position so draw commands
// commit at any angle. That no-op-when-off guarantee is what makes ORTHO disable-able (REQ-047).
// Pure + header-only so the commit and rubber-band paths share one tested implementation.
inline void OrthoConstrainPoint(float anchorX, float anchorY, float* x, float* y, bool ortho) {
  if (!ortho || !x || !y)
    return;
  const float dx = *x - anchorX;
  const float dy = *y - anchorY;
  if (std::fabs(dx) >= std::fabs(dy))
    *y = anchorY;  // farther along X → lock to the horizontal through the anchor
  else
    *x = anchorX;  // farther along Y → lock to the vertical through the anchor
}

// Direct-distance entry (REQ-047): the axis unit vector from \p anchor toward \p target, picking the axis
// the target is farther along — the same axis choice \ref OrthoConstrainPoint makes. Returns false when the
// target coincides with the anchor (no direction to take a distance along).
//
// BOTH points must be in the SAME coordinate frame. Storage is local (world = local + worldDocumentOrigin),
// so a world-space crosshair must be converted to local before it is passed here: mixing the frames adds the
// document origin to dx only, which pins the result to +X on any drawing with a non-zero origin.
inline bool OrthoUnitTowardPoint(float anchorX, float anchorY, float targetX, float targetY, float* ux, float* uy) {
  if (!ux || !uy)
    return false;
  const float dx = targetX - anchorX;
  const float dy = targetY - anchorY;
  if (std::fabs(dx) < 1.e-12f && std::fabs(dy) < 1.e-12f)
    return false;
  if (std::fabs(dx) >= std::fabs(dy)) {
    *ux = dx >= 0.f ? 1.f : -1.f;
    *uy = 0.f;
  } else {
    *ux = 0.f;
    *uy = dy >= 0.f ? 1.f : -1.f;
  }
  return true;
}
