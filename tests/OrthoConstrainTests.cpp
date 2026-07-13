#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "commands/OrthoConstrain.hpp"

using Catch::Approx;

// REQ-047: with ORTHO OFF the constraint is a no-op — the point commits at its true angle. This is the
// invariant that was broken (ORTHO forced on), so it is the regression guard.
TEST_CASE("ORTHO off leaves the point unchanged at any angle (REQ-047)", "[ortho]") {
  float x = 13.f, y = 7.f;  // arbitrary off-axis point relative to the anchor
  OrthoConstrainPoint(10.f, 10.f, &x, &y, /*ortho=*/false);
  REQUIRE(x == Approx(13.f));
  REQUIRE(y == Approx(7.f));
}

// REQ-047: with ORTHO ON the point snaps onto the axis it is farther along, through the anchor.
TEST_CASE("ORTHO on snaps to the nearer H/V axis through the anchor (REQ-047)", "[ortho]") {
  const float ax = 10.f, ay = 10.f;

  SECTION("farther along X -> locks Y to the horizontal") {
    float x = 30.f, y = 13.f;  // |dx|=20 > |dy|=3
    OrthoConstrainPoint(ax, ay, &x, &y, true);
    REQUIRE(x == Approx(30.f));  // X free
    REQUIRE(y == Approx(ay));    // Y snapped to the anchor row
  }

  SECTION("farther along Y -> locks X to the vertical") {
    float x = 12.f, y = 40.f;  // |dy|=30 > |dx|=2
    OrthoConstrainPoint(ax, ay, &x, &y, true);
    REQUIRE(x == Approx(ax));    // X snapped to the anchor column
    REQUIRE(y == Approx(40.f));  // Y free
  }

  SECTION("exact diagonal (|dx| == |dy|) resolves horizontal") {
    float x = 25.f, y = 25.f;  // dx == dy
    OrthoConstrainPoint(ax, ay, &x, &y, true);
    REQUIRE(x == Approx(25.f));
    REQUIRE(y == Approx(ay));
  }
}
