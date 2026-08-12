// Camera matrix and screen-ray tests (REQ-058 / ADR-025 (c)).
//
// The single most important test here is PLAN-VIEW PARITY: the 3D camera must reproduce the
// pre-3D orthographic pipeline exactly when the view is unrotated, because REQ-058's acceptance
// says plan view renders pixel-comparable to the previous build. Everything else in the 3D work
// sits on top of that guarantee.
//
// The composition test encodes FINDING-3 from the REQ-057 review: the pan/anchor subtraction is a
// WORLD-space translation and must be applied BEFORE the view rotation. Reversed, geometry swims
// during orbit at state-plane coordinates — a failure that looks like a camera bug but is a
// cache-ordering bug, and which is invisible at small coordinates.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstring>

#include "render/Camera.hpp"

using Catch::Approx;

namespace {

// Column-major 4x4 multiply — the same convention as ViewportRenderer's MulMat4, duplicated here
// so the test does not need to link the GL renderer.
void Mul(const float* a, const float* b, float* out) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                       a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
}

void Translate(float tx, float ty, float tz, float* m) {
  std::memset(m, 0, sizeof(float) * 16);
  m[0] = m[5] = m[10] = m[15] = 1.f;
  m[12] = tx;
  m[13] = ty;
  m[14] = tz;
}

// Apply a column-major 4x4 to a point.
void Apply(const float* m, double x, double y, double z, double* ox, double* oy, double* oz) {
  *ox = m[0] * x + m[4] * y + m[8] * z + m[12];
  *oy = m[1] * x + m[5] * y + m[9] * z + m[13];
  *oz = m[2] * x + m[6] * y + m[10] * z + m[14];
}

// The renderer's pre-3D projection, reproduced exactly (ViewportRenderer::Ortho).
void LegacyOrtho(float l, float r, float b, float t, float n, float f, float* m) {
  std::memset(m, 0, sizeof(float) * 16);
  m[0] = 2.f / (r - l);
  m[5] = 2.f / (t - b);
  m[10] = -2.f / (f - n);
  m[12] = -(r + l) / (r - l);
  m[13] = -(t + b) / (t - b);
  m[14] = -(f + n) / (f - n);
  m[15] = 1.f;
}

}  // namespace

// ---------------------------------------------------------------------------
// Plan-view parity — the guarantee the whole 3D change rests on.
// ---------------------------------------------------------------------------

TEST_CASE("Plan view produces an identity view rotation", "[camera]") {
  const Camera cam = Camera::Plan(1000.0, 2000.0, 50.f);
  REQUIRE(cam.isPlanView());

  float r[16];
  cam.ViewRotation(r);
  for (int i = 0; i < 16; ++i) {
    const float expected = (i % 5 == 0) ? 1.f : 0.f;  // diagonal of a column-major 4x4
    REQUIRE(r[i] == Approx(expected).margin(1e-6));
  }
}

TEST_CASE("Camera ortho projection matches the pre-3D renderer matrix", "[camera]") {
  // Same inputs the renderer used: halfH = (1/zoom)*50, halfW = halfH*aspect, near/far +/-1000.
  constexpr float kHalfH = 50.f;
  constexpr float kAspect = 1.6f;
  const float halfW = kHalfH * kAspect;

  float legacy[16];
  LegacyOrtho(-halfW, halfW, -kHalfH, kHalfH, -1000.f, 1000.f, legacy);

  Camera cam = Camera::Plan(0.0, 0.0, kHalfH);
  cam.nearZ = -1000.f;
  cam.farZ = 1000.f;
  float mine[16];
  cam.OrthoProjection(kAspect, mine);

  for (int i = 0; i < 16; ++i)
    REQUIRE(mine[i] == Approx(legacy[i]).margin(1e-6));
}

// ---------------------------------------------------------------------------
// FINDING-3 — anchor translation happens in WORLD space, before the rotation.
// ---------------------------------------------------------------------------

TEST_CASE("Anchor offset composes before the view rotation (state-plane safe)", "[camera]") {
  // A realistic state-plane setup: the cached vertex buffer is anchored near the drawing, and the
  // camera has since panned slightly. Cached vertices are stored as (world - cachedAnchor).
  const double cachedAnchorX = 2043500.0, cachedAnchorY = 731200.0;
  const double panX = 2043510.0, panY = 731190.0;

  Camera cam = Camera::Plan(panX, panY, 50.f);
  cam.Orbit(30.f, -35.f);  // an arbitrary orbited view

  float R[16], T[16], mvpRot[16];
  cam.ViewRotation(R);
  Translate(static_cast<float>(cachedAnchorX - panX), static_cast<float>(cachedAnchorY - panY), 0.f, T);
  Mul(R, T, mvpRot);  // CORRECT: R * T  (translate first, then rotate)

  // A world point 12 east / 5 north of the pan target, expressed in the cache's anchored frame.
  const double worldX = panX + 12.0, worldY = panY + 5.0, worldZ = 0.0;
  const double cachedX = worldX - cachedAnchorX, cachedY = worldY - cachedAnchorY;

  double gx = 0, gy = 0, gz = 0;
  Apply(mvpRot, cachedX, cachedY, worldZ, &gx, &gy, &gz);

  // Ground truth: rotate the target-relative offset directly.
  double tx = 0, ty = 0, tz = 0;
  Apply(R, 12.0, 5.0, 0.0, &tx, &ty, &tz);

  REQUIRE(gx == Approx(tx).margin(1e-3));
  REQUIRE(gy == Approx(ty).margin(1e-3));
  REQUIRE(gz == Approx(tz).margin(1e-3));

  // And the reversed order is genuinely different — proving this test would catch the mistake
  // rather than passing either way.
  float wrong[16];
  Mul(T, R, wrong);  // WRONG: rotate first, then translate by a world-space offset
  double wx = 0, wy = 0, wz = 0;
  Apply(wrong, cachedX, cachedY, worldZ, &wx, &wy, &wz);
  REQUIRE(std::fabs(wx - tx) + std::fabs(wy - ty) + std::fabs(wz - tz) > 1.0);
}

// ---------------------------------------------------------------------------
// Orbit behaviour.
// ---------------------------------------------------------------------------

TEST_CASE("Orbit clamps elevation at the poles and wraps azimuth", "[camera]") {
  Camera cam = Camera::Plan(0.0, 0.0, 50.f);

  cam.Orbit(0.f, 45.f);  // already at +90 (plan) — must not roll over
  REQUIRE(cam.elevationDeg == Approx(90.f));

  cam.Orbit(0.f, -200.f);
  REQUIRE(cam.elevationDeg == Approx(-90.f));

  cam.elevationDeg = 0.f;
  cam.Orbit(370.f, 0.f);
  REQUIRE(cam.azimuthDeg == Approx(10.f));
  cam.Orbit(-20.f, 0.f);
  REQUIRE(cam.azimuthDeg == Approx(350.f));
}

TEST_CASE("Plan view looks straight down; a front view looks north", "[camera]") {
  const Camera plan = Camera::Plan(0.0, 0.0, 50.f);
  const ray3d::Vec3 f = plan.ForwardWorld();
  REQUIRE(f.x == Approx(0.0).margin(1e-6));
  REQUIRE(f.y == Approx(0.0).margin(1e-6));
  REQUIRE(f.z == Approx(-1.0).margin(1e-6));  // straight down

  Camera front = Camera::Plan(0.0, 0.0, 50.f);
  front.elevationDeg = 0.f;
  front.azimuthDeg = 0.f;
  const ray3d::Vec3 ff = front.ForwardWorld();
  REQUIRE(ff.z == Approx(0.0).margin(1e-6));  // horizontal
  REQUIRE(ff.y == Approx(1.0).margin(1e-6));  // looking toward +Y (north)
}

TEST_CASE("A front view puts world Z up on screen", "[camera]") {
  // Elevation 0 = horizontal view. World +Z must map to screen up (+Y in camera space), which is
  // what makes an elevation view legible.
  Camera front = Camera::Plan(0.0, 0.0, 50.f);
  front.elevationDeg = 0.f;
  float r[16];
  front.ViewRotation(r);
  double cx = 0, cy = 0, cz = 0;
  Apply(r, 0.0, 0.0, 1.0, &cx, &cy, &cz);  // world up
  REQUIRE(cy == Approx(1.0).margin(1e-6));
  REQUIRE(cx == Approx(0.0).margin(1e-6));
}

// ---------------------------------------------------------------------------
// Screen rays — the input path for picking and drawing.
// ---------------------------------------------------------------------------

TEST_CASE("In plan view the centre pixel casts a ray straight down through the target", "[camera]") {
  const Camera cam = Camera::Plan(100.0, 200.0, 50.f);
  const ray3d::Ray r = cam.ScreenRay(400.f, 300.f, 800.f, 600.f);
  REQUIRE(r.valid());
  REQUIRE(r.dir.z == Approx(-1.0).margin(1e-6));

  ray3d::Vec3 hit;
  REQUIRE(ray3d::RayPlaneIntersect(r, ray3d::Plane{}, &hit));
  REQUIRE(hit.x == Approx(100.0).margin(1e-3));
  REQUIRE(hit.y == Approx(200.0).margin(1e-3));
  REQUIRE(hit.z == Approx(0.0).margin(1e-3));
}

TEST_CASE("In plan view a screen ray reproduces the legacy linear mapping", "[camera]") {
  // This is the parity that lets the input seam keep its existing arithmetic in plan view: the
  // ray-based path must agree with worldLeft + u*(worldRight-worldLeft) to within tolerance.
  constexpr float kW = 800.f, kH = 600.f;
  constexpr float kHalfH = 50.f;
  const float aspect = kW / kH;
  const float halfW = kHalfH * aspect;
  const double panX = 1234.5, panY = -678.25;

  const Camera cam = Camera::Plan(panX, panY, kHalfH);

  const float px = 610.f, py = 145.f;
  const double u = static_cast<double>(px) / kW;
  const double v = static_cast<double>(py) / kH;
  const double legacyX = (panX - halfW) + u * (2.0 * halfW);
  const double legacyY = (panY + kHalfH) - v * (2.0 * kHalfH);

  ray3d::Vec3 hit;
  REQUIRE(ray3d::RayPlaneIntersect(cam.ScreenRay(px, py, kW, kH), ray3d::Plane{}, &hit));
  REQUIRE(hit.x == Approx(legacyX).margin(1e-3));
  REQUIRE(hit.y == Approx(legacyY).margin(1e-3));
}

TEST_CASE("An orbited camera still casts a usable ray at the work plane", "[camera]") {
  Camera cam = Camera::Plan(0.0, 0.0, 50.f);
  cam.Orbit(45.f, -45.f);  // a typical isometric-ish view
  const ray3d::Ray r = cam.ScreenRay(400.f, 300.f, 800.f, 600.f);
  REQUIRE(r.valid());

  ray3d::Vec3 hit;
  REQUIRE(ray3d::RayPlaneIntersect(r, ray3d::Plane{}, &hit));
  // The centre pixel always points at the target, whatever the orientation.
  REQUIRE(hit.x == Approx(0.0).margin(1e-3));
  REQUIRE(hit.y == Approx(0.0).margin(1e-3));
  REQUIRE(hit.z == Approx(0.0).margin(1e-3));
}

TEST_CASE("A degenerate viewport size yields an invalid ray, not a NaN one", "[camera]") {
  const Camera cam = Camera::Plan(0.0, 0.0, 50.f);
  REQUIRE_FALSE(cam.ScreenRay(0.f, 0.f, 0.f, 600.f).valid());
  REQUIRE_FALSE(cam.ScreenRay(0.f, 0.f, 800.f, 0.f).valid());
}

// ---------------------------------------------------------------------------
// WorldToScreen — the projection every OVERLAY-drawn thing must use once the camera can rotate.
//
// Text, dimensions, survey labels, grips and snap glyphs are drawn by ImGui rather than GL, so
// they do not inherit the renderer's MVP. Before this existed they used an axis-aligned plan
// mapping, which meant an orbit would tilt the linework and leave every annotation behind.
// ---------------------------------------------------------------------------

TEST_CASE("WorldToScreen reproduces the legacy plan mapping exactly", "[camera]") {
  constexpr float kW = 800.f, kH = 600.f, kHalfH = 50.f;
  const float aspect = kW / kH;
  const float halfW = kHalfH * aspect;
  const double panX = -4321.5, panY = 987.25;
  const Camera cam = Camera::Plan(panX, panY, kHalfH);

  // The exact arithmetic the overlay lambdas used before REQ-058.
  const double worldLeft = panX - halfW, worldRight = panX + halfW;
  const double worldTop = panY + kHalfH, worldBottom = panY - kHalfH;
  auto legacy = [&](double wx, double wy, float* sx, float* sy) {
    *sx = static_cast<float>((wx - worldLeft) / (worldRight - worldLeft) * kW);
    *sy = static_cast<float>((worldTop - wy) / (worldTop - worldBottom) * kH);
  };

  const double pts[][2] = {{panX, panY}, {panX + 30.0, panY - 12.0}, {panX - 55.0, panY + 40.0}};
  for (const auto& p : pts) {
    float ex = 0.f, ey = 0.f, gx = 0.f, gy = 0.f;
    legacy(p[0], p[1], &ex, &ey);
    cam.WorldToScreen(p[0], p[1], 0.0, kW, kH, &gx, &gy);
    REQUIRE(gx == Approx(ex).margin(1e-3));
    REQUIRE(gy == Approx(ey).margin(1e-3));
  }
}

TEST_CASE("WorldToScreen round-trips against ScreenRay", "[camera]") {
  // Projecting a point and casting a ray back through that pixel must return the same point.
  // This is the consistency that keeps picking aligned with what is drawn, at any orientation.
  Camera cam = Camera::Plan(120.0, -60.0, 40.f);
  cam.Orbit(37.f, -52.f);
  constexpr float kW = 1024.f, kH = 768.f;

  const ray3d::Vec3 world{135.0, -48.0, 17.0};
  float sx = 0.f, sy = 0.f;
  cam.WorldToScreen(world.x, world.y, world.z, kW, kH, &sx, &sy);

  // The ray through that pixel must pass through the original point.
  const ray3d::Ray r = cam.ScreenRay(sx, sy, kW, kH);
  REQUIRE(ray3d::RayPointDistance(r, world) == Approx(0.0).margin(1e-3));
}

TEST_CASE("Elevation moves a point on screen once the camera is tilted", "[camera]") {
  // In plan view, raising a point changes nothing on screen (you are looking straight down it).
  // Tilt the camera and the same rise must become visible vertical movement — the whole point of
  // REQ-058 for a survey drawing.
  constexpr float kW = 800.f, kH = 600.f;
  const Camera plan = Camera::Plan(0.0, 0.0, 50.f);
  float flatX = 0.f, flatY = 0.f, highX = 0.f, highY = 0.f;
  plan.WorldToScreen(10.0, 10.0, 0.0, kW, kH, &flatX, &flatY);
  plan.WorldToScreen(10.0, 10.0, 25.0, kW, kH, &highX, &highY);
  REQUIRE(highX == Approx(flatX).margin(1e-4));
  REQUIRE(highY == Approx(flatY).margin(1e-4));

  Camera tilted = Camera::Plan(0.0, 0.0, 50.f);
  tilted.Orbit(0.f, -60.f);  // elevation 30 degrees
  tilted.WorldToScreen(10.0, 10.0, 0.0, kW, kH, &flatX, &flatY);
  tilted.WorldToScreen(10.0, 10.0, 25.0, kW, kH, &highX, &highY);
  REQUIRE(std::fabs(highY - flatY) > 10.f);  // the rise is clearly visible
  REQUIRE(highY < flatY);                    // and it goes UP the screen (smaller y)
}

// ---------------------------------------------------------------------------
// SetFromViewRotation — reading angles back out of a matrix the view gizmo mutated (REQ-059).
//
// The gizmo owns its own camera representation and hands back a 4x4, so this decomposition is the
// seam between the two. A convention mismatch here shows up as the view jumping to a mirrored or
// upside-down orientation when a handle is clicked, so it is pinned by a round trip.
// ---------------------------------------------------------------------------

TEST_CASE("View rotation round-trips through SetFromViewRotation", "[camera]") {
  const float azs[] = {0.f, 37.f, 90.f, 180.f, 271.5f, 359.f};
  const float els[] = {89.f, 60.f, 12.f, 0.f, -30.f, -89.f};
  for (float az : azs) {
    for (float el : els) {
      Camera src = Camera::Plan(0.0, 0.0, 50.f);
      src.azimuthDeg = az;
      src.elevationDeg = el;
      float m[16];
      src.ViewRotation(m);

      Camera dst = Camera::Plan(0.0, 0.0, 50.f);
      dst.SetFromViewRotation(m);

      INFO("az=" << az << " el=" << el << " -> az'=" << dst.azimuthDeg << " el'=" << dst.elevationDeg);
      REQUIRE(dst.elevationDeg == Approx(el).margin(1e-3));
      REQUIRE(dst.azimuthDeg == Approx(az).margin(1e-3));

      // And the reconstructed matrix must match the original, which is the property that actually
      // matters — the angles are only a parametrisation of it.
      float m2[16];
      dst.ViewRotation(m2);
      for (int i = 0; i < 16; ++i)
        REQUIRE(m2[i] == Approx(m[i]).margin(1e-4));
    }
  }
}

TEST_CASE("Azimuth is left alone at the poles rather than snapped", "[camera]") {
  // At elevation +/-90 every azimuth names the same view, so the decomposition must not invent
  // one — snapping it would spin the drawing when the user clicks the top handle.
  Camera pole = Camera::Plan(0.0, 0.0, 50.f);
  pole.azimuthDeg = 123.f;
  pole.elevationDeg = 90.f;
  float m[16];
  pole.ViewRotation(m);

  Camera dst = Camera::Plan(0.0, 0.0, 50.f);
  dst.azimuthDeg = 45.f;  // a different prior azimuth
  dst.SetFromViewRotation(m);
  REQUIRE(dst.elevationDeg == Approx(90.f).margin(1e-3));
  REQUIRE(dst.azimuthDeg == Approx(45.f));  // preserved, not overwritten
}

// ---------------------------------------------------------------------------
// ShortestAzimuthDelta — the wrap logic behind ViewCube animation (REQ-059).
//
// A view change eases between orientations, and interpolating raw degrees would send the model
// the long way round the compass (350 -> 45 as -305 instead of +55). Only the wrap cases are
// interesting, so those are what is pinned here.
// ---------------------------------------------------------------------------

TEST_CASE("ShortestAzimuthDelta always takes the short way round", "[camera]") {
  REQUIRE(Camera::ShortestAzimuthDelta(0.f, 90.f) == Approx(90.f));
  REQUIRE(Camera::ShortestAzimuthDelta(90.f, 0.f) == Approx(-90.f));

  // The wrap cases: crossing 0/360 must be a short forward or backward hop, not a full sweep.
  REQUIRE(Camera::ShortestAzimuthDelta(350.f, 45.f) == Approx(55.f));
  REQUIRE(Camera::ShortestAzimuthDelta(45.f, 350.f) == Approx(-55.f));
  REQUIRE(Camera::ShortestAzimuthDelta(10.f, 350.f) == Approx(-20.f));
  REQUIRE(Camera::ShortestAzimuthDelta(350.f, 10.f) == Approx(20.f));

  // Exactly opposite: either direction is equally short; the result must stay in range and be
  // half a turn, not something that leaves the model spinning past it.
  REQUIRE(std::fabs(Camera::ShortestAzimuthDelta(0.f, 180.f)) == Approx(180.f));
  REQUIRE(std::fabs(Camera::ShortestAzimuthDelta(270.f, 90.f)) == Approx(180.f));

  // No movement means no movement.
  REQUIRE(Camera::ShortestAzimuthDelta(137.f, 137.f) == Approx(0.f));

  // Result is always within a half turn, whatever the inputs.
  for (float a = 0.f; a < 360.f; a += 17.f)
    for (float b = 0.f; b < 360.f; b += 23.f)
      REQUIRE(std::fabs(Camera::ShortestAzimuthDelta(a, b)) <= 180.f + 1e-4f);
}
