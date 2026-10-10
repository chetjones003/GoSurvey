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

// REQ-047 direct-distance entry: typing a length during LINE/POLYLINE must build the segment along the
// axis the crosshair indicates, in BOTH directions on BOTH axes. All four quadrants are covered because the
// reported failure was that every typed distance drew to the right (+X) regardless of the crosshair.
TEST_CASE("ORTHO direct-distance direction follows the crosshair on all four axes (REQ-047)", "[ortho]") {
  const float ax = 10.f, ay = 10.f;
  float ux = 0.f, uy = 0.f;

  SECTION("crosshair right -> +X") {
    REQUIRE(OrthoUnitTowardPoint(ax, ay, 40.f, 12.f, &ux, &uy));
    REQUIRE(ux == Approx(1.f));
    REQUIRE(uy == Approx(0.f));
  }

  SECTION("crosshair left -> -X") {
    REQUIRE(OrthoUnitTowardPoint(ax, ay, -20.f, 12.f, &ux, &uy));
    REQUIRE(ux == Approx(-1.f));
    REQUIRE(uy == Approx(0.f));
  }

  SECTION("crosshair up -> +Y") {
    REQUIRE(OrthoUnitTowardPoint(ax, ay, 12.f, 40.f, &ux, &uy));
    REQUIRE(ux == Approx(0.f));
    REQUIRE(uy == Approx(1.f));
  }

  SECTION("crosshair down -> -Y") {
    REQUIRE(OrthoUnitTowardPoint(ax, ay, 12.f, -20.f, &ux, &uy));
    REQUIRE(ux == Approx(0.f));
    REQUIRE(uy == Approx(-1.f));
  }

  SECTION("crosshair on the anchor has no direction") {
    REQUIRE_FALSE(OrthoUnitTowardPoint(ax, ay, ax, ay, &ux, &uy));
  }
}

// REQ-047 / local-storage invariant: the axis choice must be made with anchor and crosshair in the SAME
// frame. This is the shape of the bug that was fixed — a state-plane document origin leaked into dx only,
// so |dx| always dominated and the direction collapsed to +X. Passing local-frame values keeps the
// leftward/downward answers that the mixed-frame call could never produce.
TEST_CASE("ORTHO direction is frame-sensitive: local crosshair keeps -X (REQ-047)", "[ortho]") {
  constexpr float kDocumentOriginX = 2000000.f;  // state-plane easting, as in a Civil 3D import
  const float anchorLocalX = 10.f, anchorLocalY = 10.f;
  const float cursorLocalX = -20.f, cursorLocalY = 12.f;  // crosshair is LEFT of the anchor
  float ux = 0.f, uy = 0.f;

  // Correct: both points local.
  REQUIRE(OrthoUnitTowardPoint(anchorLocalX, anchorLocalY, cursorLocalX, cursorLocalY, &ux, &uy));
  REQUIRE(ux == Approx(-1.f));

  // The defect: the crosshair still carrying the document origin swamps dy and forces +X.
  REQUIRE(OrthoUnitTowardPoint(anchorLocalX, anchorLocalY, cursorLocalX + kDocumentOriginX, cursorLocalY, &ux, &uy));
  REQUIRE(ux == Approx(1.f));
}
