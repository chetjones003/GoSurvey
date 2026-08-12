// Intersection math for the INT / APPINT object snaps (REQ-062).
//
// This is the module the snaps stand on, and it is exactly the kind of code that is wrong in ways
// no screenshot shows — a chord approximation looks perfect on screen and misses REQ-101 by 86×.
// So the coordinates below are hand-computed, and the tolerance asserted against is REQ-101's
// ±0.01 ft rather than "close enough to look right".

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "util/curveintersect.hpp"

using Catch::Approx;
using namespace curveisect;

namespace {

constexpr double kReq101 = 0.01;  ///< ±0.01 ft — the project tolerance (REQ-101).

Seg S(double x0, double y0, double x1, double y1) { return Seg{{x0, y0}, {x1, y1}}; }

/// True when some hit lands on (x, y) within REQ-101.
bool HasPoint(const std::vector<Hit2>& v, double x, double y, double tol = kReq101) {
  return std::any_of(v.begin(), v.end(), [&](const Hit2& h) {
    return std::fabs(h.p.x - x) <= tol && std::fabs(h.p.y - y) <= tol;
  });
}

} // namespace

// --- segment × segment -------------------------------------------------------------------------

TEST_CASE("Two crossing segments intersect at the expected point", "[isect][seg]") {
  std::vector<Hit2> out;
  IntersectSegSeg(S(0, 0, 10, 10), S(0, 10, 10, 0), &out);
  REQUIRE(out.size() == 1);
  CHECK(out[0].p.x == Approx(5.0).margin(kReq101));
  CHECK(out[0].p.y == Approx(5.0).margin(kReq101));
  // The parameters must come back too — the snap interpolates elevation with them.
  CHECK(out[0].tA == Approx(0.5).margin(1e-9));
  CHECK(out[0].tB == Approx(0.5).margin(1e-9));
}

TEST_CASE("Segments that would cross only if extended do not intersect", "[isect][seg]") {
  // The whole point of INT rather than "extended intersection": the crossing is off both segments.
  std::vector<Hit2> out;
  IntersectSegSeg(S(0, 0, 1, 1), S(9, 10, 10, 9), &out);
  CHECK(out.empty());
}

TEST_CASE("Parallel and collinear segments report nothing", "[isect][seg]") {
  std::vector<Hit2> out;
  IntersectSegSeg(S(0, 0, 10, 0), S(0, 5, 10, 5), &out);  // parallel
  CHECK(out.empty());
  out.clear();
  // Collinear overlap: a shared interval, not a point. Reporting an endpoint would snap the user to
  // a place where nothing visibly crosses.
  IntersectSegSeg(S(0, 0, 10, 0), S(5, 0, 15, 0), &out);
  CHECK(out.empty());
}

TEST_CASE("A T-junction touching an endpoint still intersects", "[isect][seg]") {
  std::vector<Hit2> out;
  IntersectSegSeg(S(0, 0, 10, 0), S(5, 0, 5, 8), &out);
  REQUIRE(out.size() == 1);
  CHECK(out[0].p.x == Approx(5.0).margin(kReq101));
  CHECK(out[0].p.y == Approx(0.0).margin(kReq101));
}

// --- segment × circle / arc --------------------------------------------------------------------

TEST_CASE("A chord through a circle gives both exact crossings", "[isect][circle]") {
  // Circle r=100 at the origin, horizontal line y=60 → x = ±sqrt(100² − 60²) = ±80 exactly.
  // A 24-chord tessellation would be off by ~0.86 here; the margin is REQ-101, so it would fail.
  std::vector<Hit2> out;
  IntersectSegConic(S(-200, 60, 200, 60), MakeCircle(0, 0, 100), &out);
  REQUIRE(out.size() == 2);
  CHECK(HasPoint(out, 80.0, 60.0));
  CHECK(HasPoint(out, -80.0, 60.0));
}

TEST_CASE("A tangent line touches a circle once, not twice", "[isect][circle]") {
  std::vector<Hit2> out;
  IntersectSegConic(S(-50, 100, 50, 100), MakeCircle(0, 0, 100), &out);
  REQUIRE(out.size() == 1);
  CHECK(HasPoint(out, 0.0, 100.0));
}

TEST_CASE("A line clear of a circle misses it", "[isect][circle]") {
  std::vector<Hit2> out;
  IntersectSegConic(S(-50, 140, 50, 140), MakeCircle(0, 0, 100), &out);
  CHECK(out.empty());
}

TEST_CASE("A segment ending inside a circle crosses it once", "[isect][circle]") {
  std::vector<Hit2> out;
  IntersectSegConic(S(-200, 60, 0, 60), MakeCircle(0, 0, 100), &out);
  REQUIRE(out.size() == 1);
  CHECK(HasPoint(out, -80.0, 60.0));
}

TEST_CASE("An arc reports only crossings within its sweep", "[isect][arc]") {
  // Upper half only (0 → π). The y=60 chord crosses the full circle at x = ±80, both of which are
  // in the upper half, so both survive...
  std::vector<Hit2> upper;
  IntersectSegConic(S(-200, 60, 200, 60), MakeArc(0, 0, 100, 0.0, 3.14159265358979), &upper);
  CHECK(upper.size() == 2);

  // ...but the SAME chord mirrored below the axis touches no part of the upper arc.
  std::vector<Hit2> below;
  IntersectSegConic(S(-200, -60, 200, -60), MakeArc(0, 0, 100, 0.0, 3.14159265358979), &below);
  CHECK(below.empty());
}

TEST_CASE("A negative arc sweep is range-tested correctly", "[isect][arc]") {
  // Sweeping clockwise from π to 0 covers the same upper half as 0 → π.
  std::vector<Hit2> out;
  IntersectSegConic(S(-200, 60, 200, 60), MakeArc(0, 0, 100, 3.14159265358979, -3.14159265358979), &out);
  CHECK(out.size() == 2);
}

// --- segment × ellipse -------------------------------------------------------------------------

TEST_CASE("A segment crosses an axis-aligned ellipse at exact roots", "[isect][ellipse]") {
  // Semi-major 100 along +X, ratio 0.5 → semi-minor 50. At y = 25 (half the semi-minor):
  // x = ±100·sqrt(1 − (25/50)²) = ±100·sqrt(0.75) = ±86.6025...
  const double expected = 100.0 * std::sqrt(0.75);
  std::vector<Hit2> out;
  IntersectSegConic(S(-300, 25, 300, 25), MakeEllipse(0, 0, 100, 0, 0.5), &out);
  REQUIRE(out.size() == 2);
  CHECK(HasPoint(out, expected, 25.0));
  CHECK(HasPoint(out, -expected, 25.0));
}

TEST_CASE("A rotated ellipse is handled with no special case", "[isect][ellipse]") {
  // Major axis along +Y (the 90°-rotated version of the case above), so the roles of x and y swap.
  const double expected = 100.0 * std::sqrt(0.75);
  std::vector<Hit2> out;
  IntersectSegConic(S(25, -300, 25, 300), MakeEllipse(0, 0, 0, 100, 0.5), &out);
  REQUIRE(out.size() == 2);
  CHECK(HasPoint(out, 25.0, expected));
  CHECK(HasPoint(out, 25.0, -expected));
}

// --- circle × circle ---------------------------------------------------------------------------

TEST_CASE("Two overlapping circles meet at two points", "[isect][circle]") {
  // r=5 at (0,0) and r=5 at (6,0) → x = 3, y = ±4 (the 3-4-5 triangle).
  std::vector<Hit2> out;
  IntersectConicConic(MakeCircle(0, 0, 5), MakeCircle(6, 0, 5), &out);
  REQUIRE(out.size() == 2);
  CHECK(HasPoint(out, 3.0, 4.0));
  CHECK(HasPoint(out, 3.0, -4.0));
}

TEST_CASE("Externally tangent circles meet exactly once", "[isect][circle]") {
  std::vector<Hit2> out;
  IntersectConicConic(MakeCircle(0, 0, 5), MakeCircle(10, 0, 5), &out);
  REQUIRE(out.size() == 1);
  CHECK(HasPoint(out, 5.0, 0.0));
}

TEST_CASE("Separate, nested and concentric circles report nothing", "[isect][circle]") {
  std::vector<Hit2> out;
  IntersectConicConic(MakeCircle(0, 0, 5), MakeCircle(100, 0, 5), &out);  // far apart
  CHECK(out.empty());
  out.clear();
  IntersectConicConic(MakeCircle(0, 0, 50), MakeCircle(1, 0, 5), &out);  // one inside the other
  CHECK(out.empty());
  out.clear();
  IntersectConicConic(MakeCircle(0, 0, 5), MakeCircle(0, 0, 5), &out);  // identical: infinitely many
  CHECK(out.empty());
}

TEST_CASE("Arc sweeps filter circle-circle intersections", "[isect][arc]") {
  // The two circles meet at (3, ±4); restricting the first to its upper half keeps only (3, 4).
  std::vector<Hit2> out;
  IntersectConicConic(MakeArc(0, 0, 5, 0.0, 3.14159265358979), MakeCircle(6, 0, 5), &out);
  REQUIRE(out.size() == 1);
  CHECK(HasPoint(out, 3.0, 4.0));
}

// --- conic × conic, numerically refined ---------------------------------------------------------

TEST_CASE("An ellipse and a circle are refined to within REQ-101", "[isect][ellipse]") {
  // Ellipse semi-axes 100 × 50 and circle r = 70, both at the origin. Solving
  //   x²/100² + y²/50² = 1  with  x² + y² = 70²
  // gives 3y² = 5100 → y = ±sqrt(1700), x = ±sqrt(3200). Four transversal roots, so this is a real
  // test of the refinement rather than one a tangency could satisfy by finding nothing.
  const double ex = std::sqrt(3200.0);
  const double ey = std::sqrt(1700.0);
  std::vector<Hit2> out;
  IntersectConicConic(MakeEllipse(0, 0, 100, 0, 0.5), MakeCircle(0, 0, 70), &out);
  REQUIRE(out.size() == 4);
  CHECK(HasPoint(out, ex, ey));
  CHECK(HasPoint(out, -ex, ey));
  CHECK(HasPoint(out, ex, -ey));
  CHECK(HasPoint(out, -ex, -ey));
  for (const Hit2& h : out) {
    // Each root must genuinely lie on BOTH curves, not merely near the bracket that found it.
    CHECK(std::fabs(h.p.x * h.p.x / 10000.0 + h.p.y * h.p.y / 2500.0 - 1.0) < 1e-6);
    CHECK(std::hypot(h.p.x, h.p.y) == Approx(70.0).margin(kReq101));
  }
}

TEST_CASE("A tangency never reports a point that is not on both curves", "[isect][ellipse]") {
  // Ellipse 100 × 50 against circle r = 50: they touch at (0, ±50) and nowhere else. Newton's
  // Jacobian is singular at a tangency, so the refinement may legitimately find nothing — what it
  // must never do is report a point that is not actually on both curves.
  std::vector<Hit2> out;
  IntersectConicConic(MakeEllipse(0, 0, 100, 0, 0.5), MakeCircle(0, 0, 50), &out);
  for (const Hit2& h : out) {
    CHECK(std::fabs(h.p.x * h.p.x / 10000.0 + h.p.y * h.p.y / 2500.0 - 1.0) < 1e-6);
    CHECK(std::hypot(h.p.x, h.p.y) == Approx(50.0).margin(kReq101));
  }
}

TEST_CASE("Two ellipses crossing at four points find all four", "[isect][ellipse]") {
  // Identical ellipses, one rotated 90°: 100×50 and 50×100. By symmetry they cross where
  // x² /100² + y²/50² = 1 and x²/50² + y²/100² = 1 → x² = y² = 100²·50²/(100²+50²) → |x| = |y| =
  // 100·50/sqrt(100²+50²) = 44.7213595...
  const double e = 100.0 * 50.0 / std::sqrt(100.0 * 100.0 + 50.0 * 50.0);
  std::vector<Hit2> out;
  IntersectConicConic(MakeEllipse(0, 0, 100, 0, 0.5), MakeEllipse(0, 0, 0, 100, 0.5), &out);
  REQUIRE(out.size() == 4);
  CHECK(HasPoint(out, e, e));
  CHECK(HasPoint(out, -e, e));
  CHECK(HasPoint(out, e, -e));
  CHECK(HasPoint(out, -e, -e));
}

TEST_CASE("Ellipses that do not overlap report nothing", "[isect][ellipse]") {
  std::vector<Hit2> out;
  IntersectConicConic(MakeEllipse(0, 0, 10, 0, 0.5), MakeEllipse(1000, 0, 10, 0, 0.5), &out);
  CHECK(out.empty());
}

// --- projection into a view basis (apparent intersection) ---------------------------------------

TEST_CASE("A plan-view basis projects world XY unchanged", "[isect][project]") {
  // Plan view is right = +X, up = +Y, so apparent intersection must reduce to the XY case exactly.
  const double right[3] = {1, 0, 0};
  const double up[3] = {0, 1, 0};
  const Seg s = ProjectSeg(3, 4, 999, 7, 8, -999, right, up);
  CHECK(s.a.x == Approx(3.0));
  CHECK(s.a.y == Approx(4.0));
  CHECK(s.b.x == Approx(7.0));
  CHECK(s.b.y == Approx(8.0));

  const Conic k = ProjectConic(MakeCircle(10, 20, 5), 500.0, right, up);
  CHECK(k.c.x == Approx(10.0));
  CHECK(k.c.y == Approx(20.0));
  CHECK(k.u.x == Approx(5.0));
  CHECK(k.v.y == Approx(5.0));
}

TEST_CASE("Elevation only reaches the projection through the view basis", "[isect][project]") {
  // A front view (right = +X, up = +Z) turns elevation into screen height — which is precisely how
  // two objects at different elevations stop appearing to cross once the camera leaves plan view.
  const double right[3] = {1, 0, 0};
  const double up[3] = {0, 0, 1};
  const Seg lowSeg = ProjectSeg(0, 0, 0, 10, 0, 0, right, up);
  const Seg highSeg = ProjectSeg(0, 0, 25, 10, 0, 25, right, up);
  CHECK(lowSeg.a.y == Approx(0.0));
  CHECK(highSeg.a.y == Approx(25.0));

  // In plan view these two would lie on top of each other and "cross" everywhere; from the front
  // they are 25 apart and cross nowhere.
  std::vector<Hit2> out;
  IntersectSegSeg(lowSeg, highSeg, &out);
  CHECK(out.empty());
}

TEST_CASE("A circle projects to a squashed conic, not a circle", "[isect][project]") {
  // Looking from 30° above the horizon, a horizontal circle foreshortens: its screen extent across
  // is the full radius and its extent up the screen is r·sin(30°) = r/2. If ProjectConic wrongly
  // treated u and v as points it would also shear the result off-centre, which this pins down.
  const double s = std::sin(30.0 * 3.14159265358979 / 180.0);
  const double right[3] = {1, 0, 0};
  const double up[3] = {0, s, std::cos(30.0 * 3.14159265358979 / 180.0)};
  const Conic k = ProjectConic(MakeCircle(0, 0, 100), 0.0, right, up);
  CHECK(k.c.x == Approx(0.0).margin(1e-9));
  CHECK(k.c.y == Approx(0.0).margin(1e-9));
  CHECK(std::hypot(k.u.x, k.u.y) == Approx(100.0).margin(1e-6));   // across: unforeshortened
  CHECK(std::hypot(k.v.x, k.v.y) == Approx(100.0 * s).margin(1e-6));  // up: squashed by sin(30°)

  // And a chord across the middle still meets it at ±100 horizontally.
  std::vector<Hit2> out;
  IntersectSegConic(S(-300, 0, 300, 0), k, &out);
  REQUIRE(out.size() == 2);
  CHECK(HasPoint(out, 100.0, 0.0));
  CHECK(HasPoint(out, -100.0, 0.0));
}
