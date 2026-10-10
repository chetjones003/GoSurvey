// REQ-103 CHAMFER (step 6b, TASK-103) — the two genuinely new geometry functions this step adds:
// ChamferPointAtDistance (Distance/Distance mode) and ChamferRayIntersect (Distance/Angle mode).
// Header-only/inline (CadCommands.hpp), so this is testable without linking the whole command
// layer — the same reasoning tests/FilletGeomTests.cpp already follows for FILLET's own geometry
// (whose radius-0 SolveFilletCenter path CHAMFER reuses directly for the intersection point, not
// re-tested here).
//
// Every expected value below was hand-derived before being written here — this file is the
// automated pin of that derivation, not a second independent check of it.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CadCommands.hpp"

using Catch::Approx;

TEST_CASE("ChamferPointAtDistance: signs toward whichever side the pick is on", "[chamfer][req103]") {
  FilletCurve c{};
  c.isLine = true;
  c.ax = 0.f; c.ay = 0.f; c.bx = 10.f; c.by = 0.f;

  float x = 0.f, y = 0.f;
  ChamferPointAtDistance(c, 5.f, 0.f, 3.f, 8.f, 0.f, &x, &y);  // pick toward b (+x)
  CHECK(x == Approx(8.0).margin(1e-4));
  CHECK(y == Approx(0.0).margin(1e-4));

  ChamferPointAtDistance(c, 5.f, 0.f, 3.f, 2.f, 0.f, &x, &y);  // pick toward a (-x)
  CHECK(x == Approx(2.0).margin(1e-4));
  CHECK(y == Approx(0.0).margin(1e-4));
}

TEST_CASE("ChamferRayIntersect: 45-degree ray picks the rotation nearer the other curve's pick",
         "[chamfer][req103]") {
  // c = horizontal line through (0,0)-(10,0), kept direction toward (8,0) i.e. +X. other = vertical
  // line x=20. A ray from (5,0) at 45deg from +X hits x=20 at y=+15 (rotating one way) or y=-15
  // (rotating the other) — hand-derived: dx=15, at 45deg tan(45)=1 so dy=15 too.
  FilletCurve c{};
  c.isLine = true;
  c.ax = 0.f; c.ay = 0.f; c.bx = 10.f; c.by = 0.f;
  FilletCurve other{};
  other.isLine = true;
  other.ax = 20.f; other.ay = -10.f; other.bx = 20.f; other.by = 10.f;

  constexpr float kPi = 3.14159265358979323846f;
  float x = 0.f, y = 0.f;
  const bool ok = ChamferRayIntersect(c, 5.f, 0.f, 8.f, 0.f, kPi * 0.25f, other, 20.f, 5.f, &x, &y);
  REQUIRE(ok);
  CHECK(x == Approx(20.0).margin(1e-2));
  CHECK(y == Approx(15.0).margin(1e-2));  // nearer otherPick (20,5) than the (20,-15) alternative

  // Flip which side otherPick favors: now the (20,-15) solution should win instead.
  const bool ok2 = ChamferRayIntersect(c, 5.f, 0.f, 8.f, 0.f, kPi * 0.25f, other, 20.f, -5.f, &x, &y);
  REQUIRE(ok2);
  CHECK(x == Approx(20.0).margin(1e-2));
  CHECK(y == Approx(-15.0).margin(1e-2));
}

TEST_CASE("ChamferRayIntersect: no intersection when the ray direction is parallel to other",
         "[chamfer][req103]") {
  FilletCurve c{};
  c.isLine = true;
  c.ax = 0.f; c.ay = 0.f; c.bx = 10.f; c.by = 0.f;
  FilletCurve other{};
  other.isLine = true;
  other.ax = 0.f; other.ay = 5.f; other.bx = 10.f; other.by = 5.f;  // parallel to c

  float x = 0.f, y = 0.f;
  // angle 0: ray stays along c's own direction (horizontal), never reaches the parallel `other`.
  const bool ok = ChamferRayIntersect(c, 5.f, 0.f, 8.f, 0.f, 0.f, other, 5.f, 5.f, &x, &y);
  CHECK_FALSE(ok);
}
