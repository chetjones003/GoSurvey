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

/// Frame the world rect into a PAPER-SPACE VIEWPORT rather than onto the screen camera (REQ-123).
///
/// A floating viewport is not framed by FrameWorldRect's output: its view is `modelCenterX/Y`
/// plus `scaleModelPerPaperIn` (model units per paper inch), and its aspect is the aspect of its
/// own RECTANGLE ON THE SHEET — not of the application window. That is the whole of GitHub issue
/// #100: the screen camera and the viewport's framing are different quantities, and zoom-extents
/// was writing the first while the user was looking through the second.
///
/// This is a conversion, not a second implementation. FrameWorldRect decides everything — the
/// centre, the margin, which axis binds, the minimum span, the refusal on a non-finite rect — and
/// the arithmetic below only restates its answer in the viewport's own units. Issue #88's
/// Architecture section requires exactly that: one framing implementation, no duplicate.
///
/// \p paperWIn / \p paperHIn are the viewport rect's size in paper inches. On success writes the
/// model point to show at the viewport's centre and the scale that fits the rect; returns false and
/// writes nothing on a degenerate rectangle or a non-finite input, so the viewport's current framing
/// survives.
inline bool FrameWorldRectInViewport(double mnX, double mxX, double mnY, double mxY, float paperWIn,
                                     float paperHIn, double* modelCenterX, double* modelCenterY,
                                     float* scaleModelPerPaperIn) {
  if (!modelCenterX || !modelCenterY || !scaleModelPerPaperIn)
    return false;
  if (!std::isfinite(paperWIn) || !std::isfinite(paperHIn) || paperWIn <= 0.f || paperHIn <= 0.f)
    return false;

  double panX = 0.;
  double panY = 0.;
  float zoom = 1.f;
  // The viewport's own aspect — its rect on the sheet. Passing the window's aspect here is the bug
  // issue #100 reports, in one argument.
  if (!FrameWorldRect(mnX, mxX, mnY, mxY, paperWIn / paperHIn, &panX, &panY, &zoom))
    return false;

  // FrameWorldRect speaks in the camera's units: it shows a half-height of kOrthoHalfHRef / zoom
  // model units. A viewport shows paperHIn inches at `scale` model units per inch, so the same
  // visible height is (paperHIn * scale). Equate them and solve for the scale.
  const double halfH = static_cast<double>(kOrthoHalfHRef) / static_cast<double>(zoom);
  const double scale = (2.0 * halfH) / static_cast<double>(paperHIn);
  if (!std::isfinite(scale) || scale <= 0.)
    return false;

  *modelCenterX = panX;
  *modelCenterY = panY;
  // The same clamp `Viewport::safeScale` applies, so a framing this function accepts is one the
  // viewport can actually hold.
  *scaleModelPerPaperIn = std::clamp(static_cast<float>(scale), 1.e-6f, 1.e9f);
  return true;
}

}  // namespace zoomframing
