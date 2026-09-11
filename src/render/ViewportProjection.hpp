#pragma once

#include "commands/PaperSpace.hpp"
#include "render/Camera.hpp"

/// REQ-061: project model-space geometry onto a paper-space sheet through a `Viewport`'s own camera.
///
/// A paper-space `Viewport` used to be a purely 2D window — a model centre and a scale
/// (`ModelToPaperIn`). REQ-061 lets each viewport carry a camera direction/projection so a plan
/// view and an isometric of the same model can share one sheet. This header is the single place
/// that turns a viewport's stored orientation into a projection; the on-screen overlay
/// (`CadUi.cpp`) and the PDF plot (`PdfPlot.cpp`) both route their model geometry through it.
///
/// Header-only and GL-free (it only uses the `Camera` value type), so the projection — including
/// the load-bearing plan-view parity — is unit-testable without a window.

/// The `Camera` a viewport views model space through. In plan orientation this composes to the
/// identity rotation, so `ModelToPaperInThroughCamera` below reproduces `ModelToPaperIn` exactly.
[[nodiscard]] inline Camera CameraForViewport(const Viewport& vp) {
  Camera c;
  c.targetX = vp.modelCenterX;
  c.targetY = vp.modelCenterY;
  c.targetZ = 0.0;
  c.azimuthDeg = vp.camAzimuthDeg;
  c.elevationDeg = vp.camElevationDeg;
  c.rollDeg = vp.camRollDeg;
  c.projection = vp.camPerspective ? Camera::Projection::Perspective : Camera::Projection::Orthographic;
  c.fovDeg = vp.camFovDeg;
  // Half-height of the ortho view volume, in model units: half the sheet height the viewport rect
  // occupies, times model-units-per-paper-inch. This is the exact relationship `ModelToPaperIn`
  // encodes, which is why plan view stays bit-for-bit identical.
  c.orthoHalfH = 0.5f * vp.paperHIn * vp.safeScale();
  return c;
}

/// Project a model point (world coordinates, absolute Z per ADR-025) to PAPER INCHES within \p vp,
/// through \p vp's camera. The sheet origin is (0,0) at the lower-left; the viewport rect is
/// `[paperXIn, paperXIn+paperWIn] x [paperYIn, paperYIn+paperHIn]`.
///
/// **Plan view is delegated to `ModelToPaperIn` unchanged** — the parity a legacy `.gs` depends on.
/// For a rotated camera the viewport rect's aspect (`paperWIn/paperHIn`) is the camera aspect, so a
/// square model feature stays square on the sheet.
inline void ModelToPaperInThroughCamera(const Viewport& vp, double mx, double my, double mz,
                                        float* outPaperX, float* outPaperY) {
  if (!outPaperX || !outPaperY)
    return;
  if (vp.cameraIsPlan()) {
    ModelToPaperIn(vp, mx, my, outPaperX, outPaperY);
    return;
  }
  const Camera c = CameraForViewport(vp);
  // WorldToScreen maps into a pixel box with the origin at the TOP-LEFT and Y pointing DOWN. Feed
  // it paper inches as the box size, then flip Y back to the sheet's Y-up convention.
  float sx = 0.f, sy = 0.f;
  c.WorldToScreen(mx, my, mz, vp.paperWIn, vp.paperHIn, &sx, &sy);
  *outPaperX = vp.paperXIn + sx;
  *outPaperY = vp.paperYIn + (vp.paperHIn - sy);
}
