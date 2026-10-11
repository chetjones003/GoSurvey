// REQ-103 FILLET (step 6a, TASK-102) — the genuinely new geometry this step adds: a tangent-arc
// solve between two curves (offset both by the fillet radius, intersect every combination
// analytically, pick the candidate nearest both picks), tangent-point extraction, the arc
// endpoint-to-newLength conversion FILLET's trim/extend step feeds into the reused
// ApplyLengthenToArc, and the parallel-lines semicircle special case. Header-only/inline
// (CadCommands.hpp), so this is testable without linking the whole command layer — the same
// reasoning tests/StretchGeomTests.cpp already follows for STRETCH's own new geometry.
//
// Every expected value below was hand-derived (see the comment above each case) before being
// written here — this file is the automated pin of that derivation, not a second independent
// check of it.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CadCommands.hpp"

using Catch::Approx;

TEST_CASE("SolveFilletCenter: radius too large for the picked segments - picks the geometrically "
         "correct (kept-side) candidate, not a nearer-but-wrong one",
         "[fillet][req103]") {
  // Real geometry from a second user bug report (2026-08-25): a peak at (0,50) with two short legs
  // (length ~31) down to (+-8,20), radius 20 -- far larger than the tangent length a small radius
  // would need. The bug: SolveFilletCenter's plain nearest-to-BOTH-picks tie-break chose a
  // candidate off to one side (not between the two legs at all) because the mathematically-correct
  // "inside the corner" candidate is pushed far from the peak by the large radius (tangent length
  // t = r/tan(halfAngle) ~= 75, versus each leg's own ~31-unit length), while the wrong candidate
  // happened to sit closer to picks placed near the visible peak. Fixed by filtering candidates to
  // the "kept side" of both lines (FilletPointOnKeptSide) before the nearest-to-pick tie-break.
  // This test pins that the CORRECT candidate is chosen: hand-derived via the tangent-length
  // formula, r/tan(halfAngle) ~= 20/tan(14.96deg) ~= 74.96, straight down from the peak along the
  // symmetric bisector (this V is symmetric about x=0), landing the center near (0, 50-74.96*... )
  // -- verified here by the weaker, robust invariant that matters operationally: the tangent points
  // must land in the KEPT direction of each leg (away from the peak, past the original far
  // endpoint -- this specific radius genuinely does need to overshoot each 31-unit leg, which the
  // separate FilletRadiusFitsCurve check is what actually refuses "too large" for the command; this
  // test only pins that the SOLVE itself picks the correct side, not an arbitrary one).
  FilletCurve c1{};
  c1.isLine = true;
  c1.ax = 0.f; c1.ay = 50.f; c1.bx = -8.f; c1.by = 20.f;
  FilletCurve c2{};
  c2.isLine = true;
  c2.ax = 0.f; c2.ay = 50.f; c2.bx = 8.f; c2.by = 20.f;

  float cx = 0.f, cy = 0.f;
  const bool ok = SolveFilletCenter(c1, c2, 20.f, -4.f, 35.f, 4.f, 35.f, &cx, &cy);
  REQUIRE(ok);
  // The correct candidate is on the kept side of BOTH legs (i.e. FilletPointOnKeptSide holds for
  // both) -- the wrong candidate this test was written to catch (found via the actual bug report,
  // center near (20.7, 50.0)) fails curve1's own kept-side test, which is exactly why it must not
  // be chosen.
  CHECK(FilletPointOnKeptSide(c1, -4.f, 35.f, cx, cy));
  CHECK(FilletPointOnKeptSide(c2, 4.f, 35.f, cx, cy));
  // And it must be well below the peak (the symmetric bisector direction, -Y) rather than off to
  // either side near y=50 the way the wrong candidate was.
  CHECK(cy < 30.0);
}

TEST_CASE("SolveFilletCenter: oblique (non-axis-aligned) lines land the tangent points EXACTLY on "
         "the fillet circle, not ~1-4% off",
         "[fillet][req103]") {
  // Real geometry from a user bug report (2026-08-25): two lines meeting at a shallow, oblique
  // angle (neither horizontal nor vertical), radius 0.5. The original bug: FilletCandidateCenters'
  // Line-Line branch built the offset lines' "infinite extension" via a huge (1e6-unit) float32
  // translation before intersecting them — float32 has ~7 significant digits, so a coordinate near
  // 1e6 in magnitude carries only ~0.03-0.05 units of precision, which propagated into the returned
  // center being ~0.01-0.02 units off from the true tangent distance. That is exactly what a user
  // reported as "none of those arc endpoints actually connect to the lines" (visible gap, not a
  // rendering artifact). Fixed by computing the offset-line intersection in double precision
  // (FilletLineLineIntersectInf, exact Cramer's-rule solve, no huge-segment approximation at all
  // for the Line-Line case) instead of the float32 curveisect::IntersectSegSeg-on-huge-segments path.
  FilletCurve c1{};
  c1.isLine = true;
  c1.ax = -55.22260284423828f; c1.ay = -7.619863510131836f;
  c1.bx = -33.75779724121094f; c1.by = 38.023681640625f;
  FilletCurve c2{};
  c2.isLine = true;
  c2.ax = -32.806365966796875f; c2.ay = 37.98614501953125f;
  c2.bx = -17.294519424438477f; c2.by = -4.195207595825195f;

  float cx = 0.f, cy = 0.f;
  const bool ok = SolveFilletCenter(c1, c2, 0.5f, -44.f, 15.f, -25.f, 17.f, &cx, &cy);
  REQUIRE(ok);

  float t1x = 0.f, t1y = 0.f, t2x = 0.f, t2y = 0.f;
  FilletTangentPointOnLine(c1, cx, cy, &t1x, &t1y);
  FilletTangentPointOnLine(c2, cx, cy, &t2x, &t2y);

  // The whole point of a TANGENT point is that it sits exactly on the fillet circle — verify that
  // directly (tight tolerance: this was off by ~0.01-0.02, roughly 100-200x looser than this bound,
  // under the original bug) rather than trusting a hand-picked expected coordinate.
  const double d1 = std::hypot(static_cast<double>(t1x) - cx, static_cast<double>(t1y) - cy);
  const double d2 = std::hypot(static_cast<double>(t2x) - cx, static_cast<double>(t2y) - cy);
  CHECK(d1 == Approx(0.5).margin(1e-4));
  CHECK(d2 == Approx(0.5).margin(1e-4));

  // And the tangent point must actually lie ON each original (infinite) line, not merely near it.
  auto perpDist = [](const FilletCurve& c, float px, float py) {
    const double vx = c.bx - c.ax, vy = c.by - c.ay;
    const double len = std::hypot(vx, vy);
    const double cross = (px - c.ax) * vy - (py - c.ay) * vx;
    return std::fabs(cross) / len;
  };
  CHECK(perpDist(c1, t1x, t1y) == Approx(0.0).margin(1e-4));
  CHECK(perpDist(c2, t2x, t2y) == Approx(0.0).margin(1e-4));
}

TEST_CASE("SolveFilletCenter: perpendicular lines, radius 3, picks disambiguate the correct quadrant",
         "[fillet][req103]") {
  // Line1 = x-axis through (-10,0)-(0,0); Line2 = y-axis through (0,0)-(0,-10). Fillet radius 3,
  // picks near (-5,0) and (0,-5) select the corner opening into the (x<0, y<0) quadrant. Offsetting
  // each line +-3 gives 4 candidate centers: (3,3), (-3,3), (3,-3), (-3,-3); summed squared
  // distance to both picks is smallest for (-3,-3) (hand-computed: 26, vs 86, 86, 146 for the
  // others) — verifying SolveFilletCenter's tie-break, not just that A solution exists.
  FilletCurve line1{};
  line1.isLine = true;
  line1.ax = -10.f; line1.ay = 0.f; line1.bx = 0.f; line1.by = 0.f;
  FilletCurve line2{};
  line2.isLine = true;
  line2.ax = 0.f; line2.ay = 0.f; line2.bx = 0.f; line2.by = -10.f;

  float cx = 0.f, cy = 0.f;
  const bool ok = SolveFilletCenter(line1, line2, 3.f, -5.f, 0.f, 0.f, -5.f, &cx, &cy);
  REQUIRE(ok);
  CHECK(cx == Approx(-3.0).margin(1e-3));
  CHECK(cy == Approx(-3.0).margin(1e-3));

  float t1x = 0.f, t1y = 0.f, t2x = 0.f, t2y = 0.f;
  FilletTangentPointOnLine(line1, cx, cy, &t1x, &t1y);
  FilletTangentPointOnLine(line2, cx, cy, &t2x, &t2y);
  CHECK(t1x == Approx(-3.0).margin(1e-3));
  CHECK(t1y == Approx(0.0).margin(1e-3));
  CHECK(t2x == Approx(0.0).margin(1e-3));
  CHECK(t2y == Approx(-3.0).margin(1e-3));
}

TEST_CASE("SolveFilletCenter: radius 0 collapses to the lines' own intersection, no special case",
         "[fillet][req103]") {
  FilletCurve line1{};
  line1.isLine = true;
  line1.ax = -10.f; line1.ay = 0.f; line1.bx = 0.f; line1.by = 0.f;
  FilletCurve line2{};
  line2.isLine = true;
  line2.ax = 0.f; line2.ay = 0.f; line2.bx = 0.f; line2.by = -10.f;

  float cx = 0.f, cy = 0.f;
  const bool ok = SolveFilletCenter(line1, line2, 0.f, -5.f, 0.f, 0.f, -5.f, &cx, &cy);
  REQUIRE(ok);
  CHECK(cx == Approx(0.0).margin(1e-3));
  CHECK(cy == Approx(0.0).margin(1e-3));

  float t1x = 0.f, t1y = 0.f, t2x = 0.f, t2y = 0.f;
  FilletTangentPointOnLine(line1, cx, cy, &t1x, &t1y);
  FilletTangentPointOnLine(line2, cx, cy, &t2x, &t2y);
  CHECK(t1x == Approx(0.0).margin(1e-3));
  CHECK(t1y == Approx(0.0).margin(1e-3));
  CHECK(t2x == Approx(0.0).margin(1e-3));
  CHECK(t2y == Approx(0.0).margin(1e-3));
}

TEST_CASE("SolveFilletCenter: line above a circle, external tangency in the gap between them",
         "[fillet][req103]") {
  // Line y=10 (infinite, through (-20,10)-(20,10)); circle center (0,0) r=4. Fillet radius 3 sits
  // in the 6-unit gap (y in [4,10]) tangent to the underside of the line and the outside of the
  // circle. Hand-derived: center_y = 10-3 = 7 (offset line) must equal r_circle+fillet_r = 4+3 = 7
  // (offset circle, external tangency) — chosen exactly so both constraints agree at x=0.
  FilletCurve line{};
  line.isLine = true;
  line.ax = -20.f; line.ay = 10.f; line.bx = 20.f; line.by = 10.f;
  FilletCurve circle{};
  circle.isLine = false;
  circle.cx = 0.f; circle.cy = 0.f; circle.r = 4.f;

  float cx = 0.f, cy = 0.f;
  const bool ok = SolveFilletCenter(line, circle, 3.f, 2.f, 10.f, 1.f, 4.f, &cx, &cy);
  REQUIRE(ok);
  CHECK(cx == Approx(0.0).margin(1e-2));
  CHECK(cy == Approx(7.0).margin(1e-2));

  float tLx = 0.f, tLy = 0.f, tCx = 0.f, tCy = 0.f;
  FilletTangentPointOnLine(line, cx, cy, &tLx, &tLy);
  FilletTangentPointOnCircle(circle, cx, cy, &tCx, &tCy);
  CHECK(tLx == Approx(0.0).margin(1e-2));
  CHECK(tLy == Approx(10.0).margin(1e-2));
  CHECK(tCx == Approx(0.0).margin(1e-2));
  CHECK(tCy == Approx(4.0).margin(1e-2));
}

TEST_CASE("SolveFilletCenter: two circles, external tangency in the gap between them",
         "[fillet][req103]") {
  // Circle1 center (0,10) r=3; circle2 center (0,0) r=3 — a 4-unit gap (y in [3,7]) between them.
  // Fillet radius 2: center equidistant (r+fillet_r=5) from both centers; by symmetry that is
  // (0,5) (distance to (0,10) and to (0,0) both exactly 5).
  FilletCurve c1{};
  c1.isLine = false;
  c1.cx = 0.f; c1.cy = 10.f; c1.r = 3.f;
  FilletCurve c2{};
  c2.isLine = false;
  c2.cx = 0.f; c2.cy = 0.f; c2.r = 3.f;

  float cx = 0.f, cy = 0.f;
  const bool ok = SolveFilletCenter(c1, c2, 2.f, 1.f, 7.f, 1.f, 3.f, &cx, &cy);
  REQUIRE(ok);
  CHECK(cx == Approx(0.0).margin(1e-2));
  CHECK(cy == Approx(5.0).margin(1e-2));

  float t1x = 0.f, t1y = 0.f, t2x = 0.f, t2y = 0.f;
  FilletTangentPointOnCircle(c1, cx, cy, &t1x, &t1y);
  FilletTangentPointOnCircle(c2, cx, cy, &t2x, &t2y);
  CHECK(t1x == Approx(0.0).margin(1e-2));
  CHECK(t1y == Approx(7.0).margin(1e-2));
  CHECK(t2x == Approx(0.0).margin(1e-2));
  CHECK(t2y == Approx(3.0).margin(1e-2));
}

TEST_CASE("FilletArcTangentPointToNewLength: CCW arc, extending from the end and from the start",
         "[fillet][req103]") {
  constexpr float kPi = 3.14159265358979323846f;
  // Arc center (0,0) r=10, start at angle 0 (10,0), sweep +90deg -> end at angle 90 (0,10). CCW.
  // Extending from the end (nearFirst=false, start fixed at angle 0): target at 45deg -> (7.0711,
  // 7.0711). Expected newAbsSweep = 45deg = pi/4 -> newLength = 10*pi/4.
  const float tx1 = 10.f * std::cos(kPi * 0.25f), ty1 = 10.f * std::sin(kPi * 0.25f);
  const float len1 = FilletArcTangentPointToNewLength(0.f, 0.f, 10.f, 0.f, kPi * 0.5f, false, tx1, ty1);
  CHECK(len1 == Approx(10.f * kPi * 0.25f).margin(1e-3));

  // Extending from the start (nearFirst=true, end fixed at angle 90deg): target at -30deg.
  // Expected newAbsSweep = CCW distance from -30 to 90 = 120deg = 2pi/3 -> newLength = 10*2pi/3.
  const float tx2 = 10.f * std::cos(-kPi / 6.f), ty2 = 10.f * std::sin(-kPi / 6.f);
  const float len2 = FilletArcTangentPointToNewLength(0.f, 0.f, 10.f, 0.f, kPi * 0.5f, true, tx2, ty2);
  CHECK(len2 == Approx(10.f * kPi * (2.f / 3.f)).margin(1e-2));
}

TEST_CASE("FilletArcTangentPointToNewLength: CW arc (negative sweepRad)", "[fillet][req103]") {
  constexpr float kPi = 3.14159265358979323846f;
  // Arc center (0,0) r=10, start at angle 0 (10,0), sweep -90deg (CW) -> end at angle -90 (0,-10).
  // Extending from the end (nearFirst=false, start fixed at angle 0): target at -45deg, halfway.
  // Expected newAbsSweep = 45deg (CW distance from 0 to -45) -> newLength = 10*pi/4.
  const float tx = 10.f * std::cos(-kPi * 0.25f), ty = 10.f * std::sin(-kPi * 0.25f);
  const float len = FilletArcTangentPointToNewLength(0.f, 0.f, 10.f, 0.f, -kPi * 0.5f, false, tx, ty);
  CHECK(len == Approx(10.f * kPi * 0.25f).margin(1e-3));
}

TEST_CASE("FilletLinesAreParallel: detects parallel and rejects non-parallel", "[fillet][req103]") {
  CHECK(FilletLinesAreParallel(0.f, 0.f, 10.f, 0.f, 0.f, 5.f, 10.f, 5.f));   // both along +X
  CHECK(FilletLinesAreParallel(0.f, 0.f, 10.f, 0.f, 10.f, 5.f, 0.f, 5.f));  // opposite direction, still parallel
  CHECK_FALSE(FilletLinesAreParallel(0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 0.f, 10.f));  // perpendicular
}

TEST_CASE("FilletParallelSemicircle: anchors on the near-pick endpoint, projects perpendicular",
         "[fillet][req103]") {
  // Line1 along y=0 from (0,0) to (10,0); Line2 along y=6 from (0,6) to (10,6). Pick near (2,0) on
  // line1 -> anchor should be whichever endpoint of line1 is nearer, (0,0). Perpendicular
  // projection of (0,0) onto line2 (y=6) is (0,6).
  float ax = 0.f, ay = 0.f, px = 0.f, py = 0.f;
  FilletParallelSemicircle(0.f, 0.f, 10.f, 0.f, 0.f, 6.f, 10.f, 6.f, 2.f, 0.f, &ax, &ay, &px, &py);
  CHECK(ax == Approx(0.0).margin(1e-4));
  CHECK(ay == Approx(0.0).margin(1e-4));
  CHECK(px == Approx(0.0).margin(1e-4));
  CHECK(py == Approx(6.0).margin(1e-4));
}
