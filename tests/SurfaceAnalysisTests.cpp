// REQ-072 / ADR-036 (g) — surface analysis: banding and slope arrows.
//
// Banding and arrows are read against a legend, which is what makes a wrong answer here quiet and
// expensive: a triangle painted one band off is not a crash, it is a grading plan that says the wrong
// thing about the ground. The three things that actually go wrong:
//
//   * the breakpoint rule — a value sitting exactly ON a band boundary, which REQ-072 requires to be
//     "defined and tested rather than left to float comparison" (Q2's half-open `[lo, hi)` rule),
//   * the flat triangle — which must produce NO direction rather than an arrow that happens to point
//     east, and whose flatness threshold is a grade a surveyor can argue with (ASSUMPTION-2),
//   * winding — the fall direction is a property of the ground, not of the order the corners were
//     listed in, and a normal taken the other way round silently reverses every arrow.
//
// Every expectation is HAND-COMPUTED from the geometry in its comment. Where a boundary is asserted
// exactly, the numbers are binary-exact fractions on purpose: a boundary test that passes because a
// division happened to round its way is not a boundary test.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include "util/surfaceanalysis.hpp"

using Catch::Approx;

namespace {

/// Comfortably inside REQ-101's +/-0.01 ft; these are unit vectors and exact arithmetic besides.
constexpr double kEps = 1.0e-9;

/// A triangle on the plane z = x/2 — a constant 50% grade falling due west, whatever three corners
/// are picked off it. Used to prove every triangle of one plane gives the SAME answer.
AnalysisTriangle OnHalfSlope(double ax, double ay, double bx, double by, double cx, double cy) {
  return AnalysisTriangle{ax, ay, ax / 2.0, bx, by, bx / 2.0, cx, cy, cx / 2.0};
}

} // namespace

TEST_CASE("A triangle's representative elevation is its centroid", "[surface][req072][analysis]") {
  // ASSUMPTION-1: one value, one colour, no subdivision. (0+5+10)/3 = 5.
  const AnalysisTriangle t{0.0, 0.0, 0.0, 10.0, 0.0, 5.0, 0.0, 10.0, 10.0};
  CHECK(TriangleCentroidZ(t) == Approx(5.0));
}

TEST_CASE("Grade is reported as a percent, not a ratio or an angle", "[surface][req072][analysis]") {
  SECTION("1 in 10 is 10%") {
    // (0,0,0) (10,0,1) (0,10,0): one foot of rise over ten of run.
    const AnalysisTriangle t{0.0, 0.0, 0.0, 10.0, 0.0, 1.0, 0.0, 10.0, 0.0};
    CHECK(TrianglePlaneSlopePct(t) == Approx(10.0));
  }
  SECTION("45 degrees is 100%, which is the number a grading plan carries") {
    const AnalysisTriangle t{0.0, 0.0, 0.0, 10.0, 0.0, 10.0, 0.0, 10.0, 0.0};
    CHECK(TrianglePlaneSlopePct(t) == Approx(100.0));
  }
  SECTION("a level triangle is 0%") {
    const AnalysisTriangle t{0.0, 0.0, 5.0, 10.0, 0.0, 5.0, 0.0, 10.0, 5.0};
    CHECK(TrianglePlaneSlopePct(t) == Approx(0.0).margin(kEps));
  }
  SECTION("the steepest direction counts, not the axis-aligned one") {
    // The plane z = x + y. Its fall line runs diagonally, so the grade is sqrt(2) x 100%, NOT the
    // 100% that reading off either axis alone would give.
    const AnalysisTriangle t{0.0, 0.0, 0.0, 10.0, 0.0, 10.0, 0.0, 10.0, 10.0};
    CHECK(TrianglePlaneSlopePct(t) == Approx(141.4213562373));
  }
}

TEST_CASE("Every triangle of one tilted plane gives the same arrow", "[surface][req072][analysis]") {
  // REQ-072's acceptance verbatim: "on a planar tilted surface every arrow points the same direction,
  // and that direction matches the hand-computed downhill vector within REQ-101".
  //
  // A 2x1 grid on the plane z = x/2, cut into four triangles two different ways. The plane rises due
  // east at a constant 50%, so the fall line is due WEST — (-1, 0) — for every one of them, and the
  // grade is 50% for every one of them. A triangulation detail must not show up in the arrows.
  const AnalysisTriangle tris[4] = {
      OnHalfSlope(0.0, 0.0, 10.0, 0.0, 10.0, 10.0),   // lower-left quad, split one way
      OnHalfSlope(0.0, 0.0, 10.0, 10.0, 0.0, 10.0),
      OnHalfSlope(10.0, 0.0, 20.0, 0.0, 20.0, 10.0),  // upper-right quad, split the other way
      OnHalfSlope(10.0, 0.0, 20.0, 10.0, 10.0, 10.0),
  };

  for (int i = 0; i < 4; ++i) {
    INFO("triangle " << i);
    double dx = 0.0, dy = 0.0;
    REQUIRE(TriangleDownhillDirection(tris[i], kFlatGradePctDefault, &dx, &dy));
    CHECK(dx == Approx(-1.0).margin(kEps));
    CHECK(dy == Approx(0.0).margin(kEps));
    CHECK(TrianglePlaneSlopePct(tris[i]) == Approx(50.0));
  }
}

TEST_CASE("The fall direction does not depend on winding", "[surface][req072][analysis]") {
  // The same ground, corners listed both ways round. Taking the normal without accounting for this
  // reverses every arrow on half the triangulation — and a surface where half the arrows point
  // uphill still looks plausible at a glance, which is what makes it worth pinning.
  const AnalysisTriangle ccw{0.0, 0.0, 0.0, 10.0, 0.0, 1.0, 0.0, 10.0, 0.0};
  const AnalysisTriangle cw{0.0, 0.0, 0.0, 0.0, 10.0, 0.0, 10.0, 0.0, 1.0};

  double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
  REQUIRE(TriangleDownhillDirection(ccw, kFlatGradePctDefault, &ax, &ay));
  REQUIRE(TriangleDownhillDirection(cw, kFlatGradePctDefault, &bx, &by));

  CHECK(ax == Approx(-1.0).margin(kEps));  // rises east, so it falls west
  CHECK(ay == Approx(0.0).margin(kEps));
  CHECK(bx == Approx(ax).margin(kEps));
  CHECK(by == Approx(ay).margin(kEps));
  CHECK(TrianglePlaneSlopePct(cw) == Approx(TrianglePlaneSlopePct(ccw)));
}

TEST_CASE("A diagonal fall line is reported diagonally", "[surface][req072][analysis]") {
  // The plane z = x + y rises to the north-east, so it falls to the south-west: (-1,-1) normalised.
  const AnalysisTriangle t{0.0, 0.0, 0.0, 10.0, 0.0, 10.0, 0.0, 10.0, 10.0};
  double dx = 0.0, dy = 0.0;
  REQUIRE(TriangleDownhillDirection(t, kFlatGradePctDefault, &dx, &dy));
  CHECK(dx == Approx(-0.7071067811865).margin(1.0e-9));
  CHECK(dy == Approx(-0.7071067811865).margin(1.0e-9));
  CHECK(Approx(dx * dx + dy * dy) == 1.0);  // a UNIT vector, so the renderer scales arrows itself
}

TEST_CASE("A flat triangle has no direction, and says so", "[surface][req072][analysis]") {
  // REQ-072 verbatim: "a perfectly flat triangle produces no arrow direction and is drawn as flat
  // rather than as an arbitrary direction". Asserted on the RETURN, not on a zero-length vector — a
  // caller that reads (0,0) as a direction draws an arrow pointing due east across flat ground.
  const AnalysisTriangle flat{0.0, 0.0, 5.0, 10.0, 0.0, 5.0, 0.0, 10.0, 5.0};
  double dx = -99.0, dy = -99.0;
  CHECK_FALSE(TriangleDownhillDirection(flat, kFlatGradePctDefault, &dx, &dy));
  CHECK(dx == -99.0);  // outputs left untouched, so a caller cannot read a stale direction as new
  CHECK(dy == -99.0);
}

TEST_CASE("The flat threshold is a grade, and its boundary is defined",
          "[surface][req072][analysis]") {
  // ASSUMPTION-2. The threshold is expressed as a grade so it can be argued about in the units a
  // survey uses. The numbers here are binary-exact on purpose: a triangle of run 2 and rise 1 is
  // 100 x 1/2 = exactly 50%, and 1/2 and 50 are both exact in binary, so "exactly at the threshold"
  // means exactly at it — not within a rounding of it.
  const AnalysisTriangle fiftyPct{0.0, 0.0, 0.0, 2.0, 0.0, 1.0, 0.0, 2.0, 0.0};
  REQUIRE(TrianglePlaneSlopePct(fiftyPct) == 50.0);  // exact equality, deliberately

  double dx = 0.0, dy = 0.0;
  SECTION("exactly at the threshold is flat") {
    // The constant names the grade a drawing stops meaning to show a direction for, so the boundary
    // value falls on the flat side of it.
    CHECK_FALSE(TriangleDownhillDirection(fiftyPct, 50.0, &dx, &dy));
  }
  SECTION("just above the threshold gets an arrow") {
    CHECK(TriangleDownhillDirection(fiftyPct, 25.0, &dx, &dy));
    CHECK(dx == Approx(-1.0).margin(kEps));
  }
  SECTION("well below the threshold is flat") {
    CHECK_FALSE(TriangleDownhillDirection(fiftyPct, 75.0, &dx, &dy));
  }
  SECTION("a shallow grade survives the default threshold") {
    // 1 in 100 is 1%, ten times the 0.1% default: a real surveyed grade, and it must keep its arrow.
    const AnalysisTriangle onePct{0.0, 0.0, 0.0, 100.0, 0.0, 1.0, 0.0, 100.0, 0.0};
    CHECK(TriangleDownhillDirection(onePct, kFlatGradePctDefault, &dx, &dy));
  }
}

TEST_CASE("A triangle with no plane is refused rather than guessed at",
          "[surface][req072][analysis]") {
  double dx = 0.0, dy = 0.0;

  SECTION("three collinear corners") {
    // Zero area, so there is no plane and no grade. Drawn flat, never given a steepness.
    const AnalysisTriangle degenerate{0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 20.0, 0.0, 0.0};
    CHECK(TrianglePlaneSlopePct(degenerate) == Approx(0.0).margin(kEps));
    CHECK_FALSE(TriangleDownhillDirection(degenerate, kFlatGradePctDefault, &dx, &dy));
  }
  SECTION("a vertical face") {
    // Defensive: a Delaunay TIN of distinct XY points cannot make one. Its grade is infinite, and it
    // has no XY direction to fall in, so there is no arrow to draw.
    const AnalysisTriangle vertical{0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 0.0, 0.0, 10.0};
    CHECK(TrianglePlaneSlopePct(vertical) > 1.0e12);
    CHECK_FALSE(TriangleDownhillDirection(vertical, kFlatGradePctDefault, &dx, &dy));
  }
  SECTION("a null out is refused, not written through") {
    const AnalysisTriangle t{0.0, 0.0, 0.0, 10.0, 0.0, 5.0, 0.0, 10.0, 0.0};
    CHECK_FALSE(TriangleDownhillDirection(t, kFlatGradePctDefault, nullptr, &dy));
    CHECK_FALSE(TriangleDownhillDirection(t, kFlatGradePctDefault, &dx, nullptr));
  }
}

TEST_CASE("A value on a breakpoint falls in the band above it", "[surface][req072][analysis]") {
  // REQ-072 verbatim: "including at an exact breakpoint, where the band a value falls into is defined
  // and tested rather than left to float comparison". Q2's rule, stated once and asserted directly.
  //
  //   band 0 = (-inf, 100)   band 1 = [100, 110)   band 2 = [110, 120]
  //
  // Every bound here is exact in binary, so an assertion about the boundary is about the RULE.
  const std::vector<double> bounds{100.0, 110.0, 120.0};

  CHECK(AssignBand(95.0, bounds) == 0);
  CHECK(AssignBand(100.0, bounds) == 1);  // ON the breakpoint: the band ABOVE, not below
  CHECK(AssignBand(105.0, bounds) == 1);
  CHECK(AssignBand(110.0, bounds) == 2);  // and again at the next one
  CHECK(AssignBand(115.0, bounds) == 2);
}

TEST_CASE("The table's extremes both have a band", "[surface][req072][analysis]") {
  const std::vector<double> bounds{100.0, 110.0, 120.0};

  SECTION("the maximum has one, because the topmost band is closed at its top") {
    // A range table is built to SPAN the surface, so the surface's highest point sits exactly on the
    // last bound. Half-open all the way up would leave that one triangle unpainted, on every surface.
    CHECK(AssignBand(120.0, bounds) == 2);
  }
  SECTION("the lowest band is open at the bottom") {
    CHECK(AssignBand(-9999.0, bounds) == 0);
  }
  SECTION("above the top of the table is no band at all") {
    // Not clamped into band 2: a triangle above the table is not IN the table, and the caller draws
    // it unbanded rather than being told a colour that would misread against the legend.
    CHECK(AssignBand(120.001, bounds) == -1);
  }
}

TEST_CASE("A degenerate range table does not crash", "[surface][req072][analysis]") {
  SECTION("no bands at all") {
    CHECK(AssignBand(50.0, {}) == -1);
  }
  SECTION("a single band, which is therefore closed at its top") {
    const std::vector<double> one{100.0};
    CHECK(AssignBand(50.0, one) == 0);
    CHECK(AssignBand(100.0, one) == 0);
    CHECK(AssignBand(100.001, one) == -1);
  }
}

TEST_CASE("Banding by elevation and by slope use the same rule", "[surface][req072][analysis]") {
  // The two analysis modes differ only in which number they hand to AssignBand — the band rule
  // itself is one function in one place, which is what keeps the legend honest for both.
  const AnalysisTriangle t{0.0, 0.0, 0.0, 10.0, 0.0, 1.0, 0.0, 10.0, 0.0};  // centroid 1/3, grade 10%

  const std::vector<double> elevationBounds{0.25, 0.5, 1.0};
  CHECK(AssignBand(TriangleCentroidZ(t), elevationBounds) == 1);  // 0.3333 is in [0.25, 0.5)

  const std::vector<double> slopeBounds{5.0, 10.0, 15.0};
  CHECK(AssignBand(TrianglePlaneSlopePct(t), slopeBounds) == 2);  // exactly 10% -> the band above
}
