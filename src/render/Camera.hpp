#pragma once

#include <cmath>
#include <cstring>

#include "util/ray3d.hpp"

/// The model viewport's camera (REQ-058 / ADR-025 (c)).
///
/// A **value type**, not an abstraction: it has three present-day concrete uses — the model
/// viewport, each paper-space `Viewport` (REQ-061), and the PDF plot — which is what satisfies
/// §11.4. There is no camera interface and no scene graph.
///
/// Header-only and GL-free so the matrix math is unit-testable without a window (ADR-002).
///
/// **Orientation is stored as azimuth + elevation about a target**, not as a free eye/up pair,
/// because that is what a ViewCube drives (REQ-059) and it keeps the basis continuous through
/// plan view. A free eye/up formulation needs an up-vector fallback when the view direction is
/// parallel to world Z — exactly at plan view, the most common orientation in this application —
/// and that fallback shows up as a visible flip when orbiting through the pole.
///
/// **The load-bearing property: plan view produces an IDENTITY rotation.** The renderer composes
/// `MVP = Proj · R · Translate(cachedAnchor − target)`, so an identity `R` reproduces the previous
/// 2D pipeline exactly — which is what REQ-058's "plan view renders pixel-comparable" depends on.
struct Camera {
  /// World point the camera looks at and orbits around. In the model viewport this IS the existing
  /// pan position, which is why the pan/anchor subtraction already in the renderer becomes the
  /// view translation for free (see ADR-025 and the TASK-035 plan).
  double targetX = 0.0;
  double targetY = 0.0;
  double targetZ = 0.0;

  /// Rotation about world +Z, in degrees. 0 = looking from the south toward +Y (north up on screen).
  float azimuthDeg = 0.f;
  /// Angle above the horizon, in degrees. **+90 = straight down = plan view** (the startup default).
  /// Clamped to [-90, +90] by \ref Orbit so the camera cannot roll over the pole.
  float elevationDeg = 90.f;

  /// Half-height of the orthographic view volume in world units. Mirrors the existing
  /// `halfH = (1/zoom) * 50` relationship, so `zoom` and this are two views of one quantity.
  float orthoHalfH = 50.f;

  enum class Projection { Orthographic = 0, Perspective = 1 };
  Projection projection = Projection::Orthographic;
  float fovDeg = 45.f;  ///< Perspective vertical field of view.

  /// Depth range. Generous because survey drawings span large coordinates and the view volume is
  /// centred on the target rather than fitted to the geometry.
  float nearZ = -100000.f;
  float farZ = 100000.f;

  [[nodiscard]] bool isPlanView() const {
    return std::fabs(elevationDeg - 90.f) < 1e-4f && std::fabs(azimuthDeg) < 1e-4f;
  }

  /// A plan view (the startup default) centred on \p tx, \p ty at the given ortho half-height.
  static Camera Plan(double tx, double ty, float halfH) {
    Camera c;
    c.targetX = tx;
    c.targetY = ty;
    c.orthoHalfH = halfH;
    c.azimuthDeg = 0.f;
    c.elevationDeg = 90.f;
    return c;
  }

  /// Apply an orbit delta, clamping elevation to the poles and wrapping azimuth.
  void Orbit(float dAzimuthDeg, float dElevationDeg) {
    azimuthDeg += dAzimuthDeg;
    while (azimuthDeg >= 360.f)
      azimuthDeg -= 360.f;
    while (azimuthDeg < 0.f)
      azimuthDeg += 360.f;
    elevationDeg += dElevationDeg;
    // Clamp rather than wrap: passing through the pole would invert the horizon and is
    // disorienting. AutoCAD's constrained orbit does the same.
    if (elevationDeg > 90.f)
      elevationDeg = 90.f;
    if (elevationDeg < -90.f)
      elevationDeg = -90.f;
  }

  /// The 3×3 view rotation, written into a column-major 4×4 (the convention `MulMat4`/`Ortho` use).
  ///
  /// `R = Rx(elevation − 90°) · Rz(azimuth)`: azimuth spins the world about Z, then the tilt drops
  /// the horizon into place. At elevation 90 / azimuth 0 both factors are identity, so plan view is
  /// exactly the previous pipeline — asserted by a parity test, not assumed.
  void ViewRotation(float* out16) const {
    const double kDeg = 3.14159265358979323846 / 180.0;
    const double az = static_cast<double>(azimuthDeg) * kDeg;
    const double tilt = (static_cast<double>(elevationDeg) - 90.0) * kDeg;
    const double ca = std::cos(az), sa = std::sin(az);
    const double ct = std::cos(tilt), st = std::sin(tilt);

    // Row-major basis rows (camera right / up / backward in world axes), then written transposed
    // into the column-major output.
    const double r0[3] = {ca, -sa, 0.0};
    const double r1[3] = {ct * sa, ct * ca, -st};
    const double r2[3] = {st * sa, st * ca, ct};

    std::memset(out16, 0, sizeof(float) * 16);
    out16[0] = static_cast<float>(r0[0]);
    out16[4] = static_cast<float>(r0[1]);
    out16[8] = static_cast<float>(r0[2]);
    out16[1] = static_cast<float>(r1[0]);
    out16[5] = static_cast<float>(r1[1]);
    out16[9] = static_cast<float>(r1[2]);
    out16[2] = static_cast<float>(r2[0]);
    out16[6] = static_cast<float>(r2[1]);
    out16[10] = static_cast<float>(r2[2]);
    out16[15] = 1.f;
  }

  /// Orthographic projection for the given aspect (width / height), column-major.
  /// Identical in form to the renderer's existing `Ortho`, so plan view is unchanged.
  void OrthoProjection(float aspect, float* out16) const {
    const float halfH = orthoHalfH;
    const float halfW = halfH * aspect;
    std::memset(out16, 0, sizeof(float) * 16);
    out16[0] = 1.f / halfW;   // == 2/(right-left) with right=-left=halfW
    out16[5] = 1.f / halfH;
    out16[10] = -2.f / (farZ - nearZ);
    out16[14] = -(farZ + nearZ) / (farZ - nearZ);
    out16[15] = 1.f;
  }

  /// World-space direction the camera looks ALONG (from eye toward target).
  ///
  /// **Derived from `ViewRotation`, never re-derived from the angles.** Row 2 of the view rotation
  /// is the camera's backward axis in world coordinates, so forward is its negation. Computing it
  /// independently from azimuth/elevation is how the two silently disagreed on the sign of the
  /// azimuth term — which made `ScreenRay` and `WorldToScreen` inconsistent at any non-zero
  /// azimuth (picking would miss what was drawn). One derivation, one convention.
  [[nodiscard]] ray3d::Vec3 ForwardWorld() const {
    float r[16];
    ViewRotation(r);
    return ray3d::Normalize(ray3d::Vec3{-r[2], -r[6], -r[10]});
  }

  /// World-space camera RIGHT (screen +X) — row 0 of the view rotation.
  ///
  /// Derived from `ViewRotation` for the same reason `ForwardWorld` is: one derivation, one
  /// convention. Used to build screen-facing (billboarded) UI markers about a world point —
  /// `p + RightWorld()*u + UpWorld()*v` — so a glyph reads the same at any orientation instead of
  /// foreshortening with the work plane (REQ-058).
  [[nodiscard]] ray3d::Vec3 RightWorld() const {
    float r[16];
    ViewRotation(r);
    return ray3d::Normalize(ray3d::Vec3{r[0], r[4], r[8]});
  }

  /// World-space camera UP (screen +Y) — row 1 of the view rotation. See \ref RightWorld.
  [[nodiscard]] ray3d::Vec3 UpWorld() const {
    float r[16];
    ViewRotation(r);
    return ray3d::Normalize(ray3d::Vec3{r[1], r[5], r[9]});
  }

  /// Signed shortest turn from \p fromDeg to \p toDeg, in (-180, 180].
  ///
  /// Animating an orientation change has to take the short way around: interpolating raw degrees
  /// from 350° to 45° would spin the model 305° backwards instead of 55° forwards. Pure and
  /// static so the wrap-around cases are unit-testable (REQ-059).
  static float ShortestAzimuthDelta(float fromDeg, float toDeg) {
    float d = toDeg - fromDeg;
    while (d > 180.f)
      d -= 360.f;
    while (d <= -180.f)
      d += 360.f;
    return d;
  }

  /// Recover azimuth/elevation from a view rotation produced elsewhere (REQ-059).
  ///
  /// The view gizmo owns its own camera representation and hands back a mutated 4×4, so the angles
  /// have to be read out of it. Inverting `ViewRotation`'s construction: row 2 is
  /// `(sin(t)·sin(az), sin(t)·cos(az), cos(t))` with `t = elevation − 90`, so `cos(t) = sin(el)`
  /// gives the elevation and the remaining pair gives the azimuth. `sin(t) = −cos(el) ≤ 0` over the
  /// clamped elevation range, which is why the azimuth terms are negated.
  ///
  /// Near the poles (`|elevation| → 90`) the azimuth is degenerate — every azimuth names the same
  /// view — so it is left unchanged rather than snapped to an arbitrary value, which would make the
  /// drawing spin when the user clicks the top handle.
  void SetFromViewRotation(const float* m16) {
    if (!m16)
      return;
    const double kRad = 180.0 / 3.14159265358979323846;
    double ct = static_cast<double>(m16[10]);
    if (ct > 1.0)
      ct = 1.0;
    if (ct < -1.0)
      ct = -1.0;
    elevationDeg = static_cast<float>(std::asin(ct) * kRad);
    const double st = -std::cos(std::asin(ct));
    if (std::fabs(st) > 1e-6) {
      double az = std::atan2(-static_cast<double>(m16[2]), -static_cast<double>(m16[6])) * kRad;
      while (az < 0.0)
        az += 360.0;
      while (az >= 360.0)
        az -= 360.0;
      azimuthDeg = static_cast<float>(az);
    }
  }

  /// Project a world point to a pixel inside the viewport (origin at its TOP-LEFT, ImGui
  /// convention). The inverse of \ref ScreenRay, and the function every overlay-drawn thing —
  /// text, dimensions, survey labels, grips, snap glyphs — must go through once the camera can
  /// rotate. Those are drawn by ImGui, not GL, so they do not inherit the renderer's MVP: without
  /// this they would stay in plan positions while the linework tilts.
  ///
  /// In plan view this reduces exactly to the pre-3D linear mapping
  /// `u = (wx − worldLeft)/(worldRight − worldLeft)`, which a parity test asserts.
  ///
  /// \param outDepth optional camera-space depth (larger = farther from the eye), for painter
  ///        ordering if a caller ever needs it.
  void WorldToScreen(double wx, double wy, double wz, float widthPx, float heightPx, float* outPxX,
                     float* outPxY, double* outDepth = nullptr) const {
    using namespace ray3d;
    if (!outPxX || !outPxY || !(widthPx > 0.f) || !(heightPx > 0.f))
      return;
    float r[16];
    ViewRotation(r);
    // Offset from the camera target, expressed in world axes, then rotated into camera axes.
    const double dx = wx - targetX, dy = wy - targetY, dz = wz - targetZ;
    const double cxr = r[0] * dx + r[4] * dy + r[8] * dz;
    const double cyr = r[1] * dx + r[5] * dy + r[9] * dz;
    const double czr = r[2] * dx + r[6] * dy + r[10] * dz;

    const double aspect = static_cast<double>(widthPx) / static_cast<double>(heightPx);
    const double halfH = static_cast<double>(orthoHalfH);
    const double halfW = halfH * aspect;

    double ndcX = 0.0, ndcY = 0.0;
    if (projection == Projection::Perspective) {
      const double kDeg = 3.14159265358979323846 / 180.0;
      const double dist = halfH / std::tan(0.5 * static_cast<double>(fovDeg) * kDeg);
      // Camera looks down -Z in camera space; distance in front of the eye is dist − czr.
      const double inFront = dist - czr;
      if (!(inFront > 1e-9)) {  // at or behind the eye — no meaningful projection
        *outPxX = *outPxY = 0.f;
        if (outDepth)
          *outDepth = 1e30;
        return;
      }
      const double scale = dist / inFront;
      ndcX = (cxr * scale) / halfW;
      ndcY = (cyr * scale) / halfH;
    } else {
      ndcX = cxr / halfW;
      ndcY = cyr / halfH;
    }
    *outPxX = static_cast<float>((ndcX * 0.5 + 0.5) * static_cast<double>(widthPx));
    *outPxY = static_cast<float>((0.5 - ndcY * 0.5) * static_cast<double>(heightPx));
    if (outDepth)
      *outDepth = -czr;  // camera looks down -Z, so farther points have more negative czr
  }

  /// The ray a screen pixel casts into the world (REQ-058 picking and drawing).
  ///
  /// \param px,py      pixel position inside the viewport, origin at its TOP-LEFT (ImGui convention).
  /// \param widthPx,heightPx  viewport size in pixels.
  ///
  /// Under an orthographic projection every ray shares the view direction and differs only in
  /// origin; the origin is pushed back along the view direction so geometry behind the target
  /// still lies in front of the ray (the `t > 0` requirement in `RayPlaneIntersect`).
  [[nodiscard]] ray3d::Ray ScreenRay(float px, float py, float widthPx, float heightPx) const {
    using namespace ray3d;
    if (!(widthPx > 0.f) || !(heightPx > 0.f))
      return Ray{};
    const float aspect = widthPx / heightPx;
    // Normalised device coords: x right, y UP (so flip ImGui's downward y).
    const double ndcX = (2.0 * static_cast<double>(px) / static_cast<double>(widthPx)) - 1.0;
    const double ndcY = 1.0 - (2.0 * static_cast<double>(py) / static_cast<double>(heightPx));

    float r[16];
    ViewRotation(r);
    // Camera basis in world axes = the ROWS of the rotation (its transpose, since R is orthonormal).
    const Vec3 right{r[0], r[4], r[8]};
    const Vec3 up{r[1], r[5], r[9]};
    const Vec3 fwd = ForwardWorld();

    const Vec3 target{targetX, targetY, targetZ};
    const double halfH = static_cast<double>(orthoHalfH);
    const double halfW = halfH * static_cast<double>(aspect);

    if (projection == Projection::Perspective) {
      const double kDeg = 3.14159265358979323846 / 180.0;
      const double dist = halfH / std::tan(0.5 * static_cast<double>(fovDeg) * kDeg);
      const Vec3 eye = Sub(target, Scale(fwd, dist));
      const double sx = ndcX * halfW;
      const double sy = ndcY * halfH;
      const Vec3 through = Add(Add(target, Scale(right, sx)), Scale(up, sy));
      return Ray{eye, Normalize(Sub(through, eye))};
    }

    // Orthographic: the ray starts on the view plane through the target, offset by the pixel's
    // position, then pulled back so nothing in the scene is behind it.
    const Vec3 onPlane = Add(Add(target, Scale(right, ndcX * halfW)), Scale(up, ndcY * halfH));
    const double pullBack = static_cast<double>(farZ > 0.f ? farZ : 100000.f);
    return Ray{Sub(onPlane, Scale(fwd, pullBack)), fwd};
  }
};
