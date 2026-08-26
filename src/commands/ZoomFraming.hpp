#pragma once

#include <algorithm>
#include <cmath>

// Zoom framing (REQ-122): turn a world rectangle into the camera that shows it.
//
// This is the ONE implementation behind every "fit this rect on screen" path — `ZOOMEXTENTS`, the
// REQ-120 middle double-click gesture, `ZOOM WINDOW`, and the post-import fit. GitHub issue #88's
// Architecture section requires exactly that: the command and the gesture must not be able to
// disagree, so there is one function and no second copy of the math.
//
// Pure + header-only for the same reason `OrthoConstrain.hpp` and `ViewportPickPolicy.hpp` are: it
// puts the math where the Catch2 target can reach it. `ProcessPendingViewportZoom` cannot be tested
// — it early-returns on `fbW <= 0` and the headless driver models no framebuffer (TASK-113 DEBT-1)
// — but every guarantee issue #88 asks for (margin, aspect, no clipping, degenerate extents, no
// NaN) lives in the framing math, not in the deferred consumer, so hoisting it makes those
// guarantees testable without giving the harness a synthetic viewport.
namespace zoomframing {

/// The camera's ortho half-height at `zoom == 1`. `halfH = kOrthoHalfHRef / zoom` — the same
/// relationship `Camera::orthoHalfH` documents, and the only place zoom is given a meaning.
inline constexpr float kOrthoHalfHRef = 50.f;

/// Slack left around the framed rectangle, as a fraction of the viewport: the rect spans at most
/// `1 - kMarginFraction` of the binding axis, so HALF of this is free on each of its two sides.
/// Geometry therefore never touches an edge (issue #88: "not positioned directly against the edge").
inline constexpr float kMarginFraction = 0.08f;

/// The smallest world span that will ever be framed, on either axis.
///
/// A degeneracy pad (the previous 1e-5) keeps the arithmetic well defined but NOT the result usable:
/// a single point padded to 2e-5 frames at zoom ~4.6e6, a view a fifth of a thousandth of a foot
/// tall. Issue #88 asks for a minimum extent precisely so that "a single point", "a very short line"
/// and "objects with identical coordinates" land at a scale a user can work at. One world unit is
/// 1% of the default view height (`zoom == 1` shows 100 units), so a degenerate drawing frames near
/// — but still tighter than — the view the application opens with.
inline constexpr double kMinFrameSpan = 1.0;

/// Frame the world rect (\p mnX..\p mxX, \p mnY..\p mxY) in a viewport of aspect \p viewportAspect.
///
/// On success writes the camera centre to \p panX / \p panY and the zoom to \p zoom, and returns
/// true. On a rect that is not a finite rectangle — a NaN coordinate, an infinite one, or a span
/// that overflows to infinity — it returns false and writes NOTHING, so the caller's current camera
/// survives untouched rather than becoming NaN. That is issue #88's "invalid/infinite camera values
/// are never produced": the only way to produce one is not to write at all.
inline bool FrameWorldRect(double mnX, double mxX, double mnY, double mxY, float viewportAspect, double* panX,
                          double* panY, float* zoom) {
  if (!panX || !panY || !zoom)
    return false;
  if (!std::isfinite(mnX) || !std::isfinite(mxX) || !std::isfinite(mnY) || !std::isfinite(mxY))
    return false;
  if (!std::isfinite(viewportAspect) || viewportAspect <= 0.f)
    return false;

  double dmnX = std::min(mnX, mxX);
  double dmxX = std::max(mnX, mxX);
  double dmnY = std::min(mnY, mxY);
  double dmxY = std::max(mnY, mxY);
  double rw = dmxX - dmnX;
  double rh = dmxY - dmnY;
  // Both coordinates can be finite while their difference overflows (-1e308 .. 1e308). Refuse that
  // rather than divide by an infinity and clamp the result into a silently wrong camera.
  if (!std::isfinite(rw) || !std::isfinite(rh))
    return false;

  // Expand about the centre, so the floor never moves what is being framed.
  if (rw < kMinFrameSpan) {
    const double grow = 0.5 * (kMinFrameSpan - rw);
    dmnX -= grow;
    dmxX += grow;
    rw = dmxX - dmnX;
  }
  if (rh < kMinFrameSpan) {
    const double grow = 0.5 * (kMinFrameSpan - rh);
    dmnY -= grow;
    dmxY += grow;
    rh = dmxY - dmnY;
  }

  const double cx = 0.5 * (dmnX + dmxX);
  const double cy = 0.5 * (dmnY + dmxY);
  const double aspect = static_cast<double>(std::max(viewportAspect, 1e-6f));
  const double denom = 2.0 * (1.0 - static_cast<double>(kMarginFraction));
  // The BINDING axis decides: height needs rh/denom, width needs rw/(aspect*denom) once converted to
  // a half-height. Taking the max is what keeps the other axis un-clipped and what makes the result
  // depend on the viewport's aspect rather than assuming a square one.
  const double needHalfH = std::max(rh / denom, rw / (aspect * denom));
  if (!std::isfinite(needHalfH) || needHalfH <= 0.)
    return false;

  *panX = cx;
  *panY = cy;
  *zoom = std::clamp(static_cast<float>(static_cast<double>(kOrthoHalfHRef) / needHalfH), 1.e-9f, 1.e9f);
  return true;
}

}  // namespace zoomframing
