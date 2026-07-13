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
