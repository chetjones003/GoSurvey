// 3D crosshair cursor projection (REQ-310, GitHub #144 / Phase 1 of #120).
//
// The crosshair's axis arms are the one part of this feature that is pure geometry, and they are
// the part that can be silently wrong: an arm drawn along the wrong screen direction still LOOKS
// like a 3D cursor, so a visual check does not catch a sign error. These tests pin the directions
// against hand-reasoned expectations rather than against the implementation.
//
// The load-bearing case is PLAN VIEW: with a world UCS looking straight down, X must project to
// screen-right and Y to screen-UP (screen Y is negative upward), which is the orientation every
// existing 2D drawing has always been read in. If that is wrong, the cursor lies about the drawing
// plane in the most common view in the application.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "viewport/Crosshair3d.hpp"

using Catch::Approx;

namespace {

// A world-aligned UCS: the default frame every drawing starts in.
ucs::Ucs WorldFrame() {
  ucs::Ucs u;
  u.xAxis = ray3d::Vec3{1, 0, 0};
  u.yAxis = ray3d::Vec3{0, 1, 0};
  u.zAxis = ray3d::Vec3{0, 0, 1};
  return u;
}

constexpr float kArm = 100.f;

}  // namespace

TEST_CASE("REQ-310 plan view maps X to screen right and Y to screen up", "[crosshair3d][req310]") {
  Camera cam = Camera::Plan(0.0, 0.0, 50.f);  // elevation 90, azimuth 0 — straight down
  const crosshair3d::Triad t = crosshair3d::Compute(cam, WorldFrame(), kArm);

  REQUIRE(t.x.visible);
  REQUIRE(t.y.visible);

  // X runs fully to the right: full arm length in +dx, nothing vertical.
  CHECK(t.x.dx == Approx(kArm).margin(1e-3));
  CHECK(t.x.dy == Approx(0.f).margin(1e-3));

  // Y runs fully UP the screen. Screen Y grows downward, so up is NEGATIVE dy — this sign is the
  // single easiest thing to get backwards, and getting it backwards mirrors the cursor.
  CHECK(t.y.dx == Approx(0.f).margin(1e-3));
  CHECK(t.y.dy == Approx(-kArm).margin(1e-3));

  // Z points straight at the viewer in plan view, so it collapses and is not drawn. Its absence is
  // the information: the axis is coming out of the screen.
  CHECK_FALSE(t.z.visible);
  CHECK(std::sqrt(t.z.dx * t.z.dx + t.z.dy * t.z.dy) < crosshair3d::kMinArmPx);
}

TEST_CASE("REQ-310 a tilted view makes the Z axis appear", "[crosshair3d][req310]") {
  // Tilting off plan is exactly when the 3D crosshair earns its place: Z becomes visible and X/Y
  // foreshorten, which is what tells the user the drawing plane is no longer facing them.
  Camera cam = Camera::Plan(0.0, 0.0, 50.f);
  cam.elevationDeg = 35.f;
  const crosshair3d::Triad t = crosshair3d::Compute(cam, WorldFrame(), kArm);

  REQUIRE(t.z.visible);
  // Z is world up, so on screen it must point UP (negative dy) — never down.
  CHECK(t.z.dy < 0.f);

  // X is perpendicular to the tilt axis at azimuth 0, so it stays full length and horizontal.
  CHECK(t.x.dx == Approx(kArm).margin(1e-3));
  CHECK(t.x.dy == Approx(0.f).margin(1e-3));

  // Y foreshortens: it is tipping away from the viewer, so its projection is SHORTER than the arm.
  const float yLen = std::sqrt(t.y.dx * t.y.dx + t.y.dy * t.y.dy);
  CHECK(yLen < kArm);
  CHECK(yLen > 0.f);
}

TEST_CASE("REQ-310 the triad follows a rotated UCS, not the world", "[crosshair3d][req310]") {
  // The whole point of the feature: under a rotated UCS the cursor must show the UCS's axes. A
  // crosshair that stayed world-aligned would actively mislead — it would say the drawing plane
  // runs one way while typed coordinates go another.
  Camera cam = Camera::Plan(0.0, 0.0, 50.f);

  ucs::Ucs rot;  // 90 degrees about Z: UCS X points along world +Y.
  rot.xAxis = ray3d::Vec3{0, 1, 0};
  rot.yAxis = ray3d::Vec3{-1, 0, 0};
  rot.zAxis = ray3d::Vec3{0, 0, 1};

  const crosshair3d::Triad t = crosshair3d::Compute(cam, rot, kArm);

  // In plan view, world +Y is screen up — so the UCS X arm now points UP, not right.
  CHECK(t.x.dx == Approx(0.f).margin(1e-3));
  CHECK(t.x.dy == Approx(-kArm).margin(1e-3));
  // and UCS Y points screen LEFT.
  CHECK(t.y.dx == Approx(-kArm).margin(1e-3));
  CHECK(t.y.dy == Approx(0.f).margin(1e-3));
}

TEST_CASE("REQ-310 azimuth rotates the triad with the camera", "[crosshair3d][req310]") {
  // Orbiting must turn the cursor with the view. At azimuth 90 the world X axis has swung to lie
  // along the screen's vertical, so the X arm must no longer be horizontal.
  Camera cam = Camera::Plan(0.0, 0.0, 50.f);
  cam.azimuthDeg = 90.f;
  const crosshair3d::Triad t = crosshair3d::Compute(cam, WorldFrame(), kArm);

  REQUIRE(t.x.visible);
  REQUIRE(t.y.visible);
  // Still full length (plan view, no foreshortening) but rotated a quarter turn.
  const float xLen = std::sqrt(t.x.dx * t.x.dx + t.x.dy * t.x.dy);
  CHECK(xLen == Approx(kArm).margin(1e-3));
  CHECK(std::fabs(t.x.dx) < 1e-3f);       // no longer horizontal
  CHECK(std::fabs(t.x.dy) == Approx(kArm).margin(1e-3));

  // X and Y stay perpendicular on screen through any orbit — the triad must not shear.
  const float dot = t.x.dx * t.y.dx + t.x.dy * t.y.dy;
  CHECK(dot == Approx(0.f).margin(1e-2));
}

TEST_CASE("REQ-310 arms stay perpendicular and equal in plan at any azimuth", "[crosshair3d][req310]") {
  // A sweep, because a sign error that survives one angle usually dies at another.
  for (float az = 0.f; az < 360.f; az += 17.f) {
    Camera cam = Camera::Plan(0.0, 0.0, 50.f);
    cam.azimuthDeg = az;
    const crosshair3d::Triad t = crosshair3d::Compute(cam, WorldFrame(), kArm);
    const float xLen = std::sqrt(t.x.dx * t.x.dx + t.x.dy * t.x.dy);
    const float yLen = std::sqrt(t.y.dx * t.y.dx + t.y.dy * t.y.dy);
    REQUIRE(xLen == Approx(kArm).margin(1e-2));
    REQUIRE(yLen == Approx(kArm).margin(1e-2));
    REQUIRE((t.x.dx * t.y.dx + t.x.dy * t.y.dy) == Approx(0.f).margin(1e-1));
  }
}

TEST_CASE("REQ-310 a zero arm length yields nothing drawable", "[crosshair3d][req310]") {
  // Guard: the caller derives the arm from a viewport dimension, which is zero on the first frame
  // of a collapsed panel. That must not produce a visible one-pixel smear.
  Camera cam = Camera::Plan(0.0, 0.0, 50.f);
  const crosshair3d::Triad t = crosshair3d::Compute(cam, WorldFrame(), 0.f);
  CHECK_FALSE(t.x.visible);
  CHECK_FALSE(t.y.visible);
  CHECK_FALSE(t.z.visible);
  CHECK(crosshair3d::Degenerate(t));
}

TEST_CASE("REQ-310 plan view is not degenerate despite the collapsed Z", "[crosshair3d][req310]") {
  // Plan view is the startup default and has only two visible arms. If `Degenerate` rejected that,
  // the feature would silently fall back to the 2D crosshair in the most common view of all.
  Camera cam = Camera::Plan(0.0, 0.0, 50.f);
  const crosshair3d::Triad t = crosshair3d::Compute(cam, WorldFrame(), kArm);
  CHECK_FALSE(crosshair3d::Degenerate(t));
}
