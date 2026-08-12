// Ray/plane/segment math backing 3D picking, snapping and drawing (REQ-058 / ADR-025 (d)).
//
// These are the tests that let the 3D input path be trusted without a window: every one of them
// checks a value a hand calculation can confirm. The failure-mode cases matter as much as the
// happy path — a click that misses the work plane must produce NO coordinate rather than a
// plausible-looking wrong one (REQ-201).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "util/ray3d.hpp"

using namespace ray3d;
using Catch::Approx;

namespace {
// A ray straight down at (x,y) — a plan-view cursor cast at the world XY plane.
Ray DownAt(double x, double y, double z = 100.0) {
  return Ray{{x, y, z}, {0.0, 0.0, -1.0}};
}
}  // namespace

// ---------------------------------------------------------------------------
// RayPlaneIntersect — the function every orbited click goes through.
// ---------------------------------------------------------------------------

TEST_CASE("Ray straight down hits the world XY plane under the cursor", "[ray3d]") {
  Vec3 hit;
  REQUIRE(RayPlaneIntersect(DownAt(12.0, -7.0), Plane{}, &hit));
  REQUIRE(hit.x == Approx(12.0));
  REQUIRE(hit.y == Approx(-7.0));
  REQUIRE(hit.z == Approx(0.0));
}

TEST_CASE("Ray hits an elevated work plane at that elevation", "[ray3d]") {
  // A UCS raised to Z = 25: a click must land ON the plane, not on the datum (REQ-058).
  Plane ucs;
  ucs.point = {0.0, 0.0, 25.0};
  Vec3 hit;
  REQUIRE(RayPlaneIntersect(DownAt(3.0, 4.0), ucs, &hit));
  REQUIRE(hit.z == Approx(25.0));
  REQUIRE(hit.x == Approx(3.0));
  REQUIRE(hit.y == Approx(4.0));
}

TEST_CASE("Oblique ray hits the plane where the geometry says it should", "[ray3d]") {
  // From (0,0,10) aimed 45 degrees down the +X axis: it must cross Z=0 at exactly x=10.
  const Ray r{{0.0, 0.0, 10.0}, Normalize(Vec3{1.0, 0.0, -1.0})};
  Vec3 hit;
  double t = 0.0;
  REQUIRE(RayPlaneIntersect(r, Plane{}, &hit, &t));
  REQUIRE(hit.x == Approx(10.0));
  REQUIRE(hit.y == Approx(0.0));
  REQUIRE(hit.z == Approx(0.0).margin(1e-9));
  REQUIRE(t == Approx(std::sqrt(200.0)));  // the ray is normalized, so t is a true distance
}

TEST_CASE("A tilted work plane is intersected correctly", "[ray3d]") {
  // Plane through the origin tilted 45 degrees about the Y axis (normal (1,0,1)/sqrt2).
  // A ray straight down at x=4 meets z = -x, i.e. z = -4.
  Plane tilted;
  tilted.point = {0.0, 0.0, 0.0};
  tilted.normal = {1.0, 0.0, 1.0};
  Vec3 hit;
  REQUIRE(RayPlaneIntersect(DownAt(4.0, 0.0), tilted, &hit));
  REQUIRE(hit.x == Approx(4.0));
  REQUIRE(hit.z == Approx(-4.0));
}

TEST_CASE("A ray parallel to the plane reports no hit and writes nothing", "[ray3d]") {
  // The failure mode that matters: an edge-on work plane. The caller must get `false` and the
  // out-parameter must be untouched, so a missed click cannot silently become a (0,0,0) click.
  const Ray horizontal{{0.0, 0.0, 5.0}, {1.0, 0.0, 0.0}};
  Vec3 hit{-999.0, -999.0, -999.0};
  REQUIRE_FALSE(RayPlaneIntersect(horizontal, Plane{}, &hit));
  REQUIRE(hit.x == -999.0);  // untouched
  REQUIRE(hit.y == -999.0);
  REQUIRE(hit.z == -999.0);
}

TEST_CASE("A plane behind the ray origin is not hit", "[ray3d]") {
  // Looking UP, away from a plane below: there is no intersection in front of the camera.
  const Ray up{{0.0, 0.0, 5.0}, {0.0, 0.0, 1.0}};
  Vec3 hit;
  REQUIRE_FALSE(RayPlaneIntersect(up, Plane{}, &hit));
}

TEST_CASE("Degenerate ray and degenerate plane are rejected, not NaN", "[ray3d]") {
  Vec3 hit;
  const Ray zeroDir{{0.0, 0.0, 5.0}, {0.0, 0.0, 0.0}};
  REQUIRE_FALSE(zeroDir.valid());
  REQUIRE_FALSE(RayPlaneIntersect(zeroDir, Plane{}, &hit));

  Plane degenerate;
  degenerate.normal = {0.0, 0.0, 0.0};
  REQUIRE_FALSE(RayPlaneIntersect(DownAt(1.0, 1.0), degenerate, &hit));

  REQUIRE_FALSE(RayPlaneIntersect(DownAt(1.0, 1.0), Plane{}, nullptr));  // null out-param
}

// ---------------------------------------------------------------------------
// RaySegmentDistance — how picking works once the camera tilts.
// ---------------------------------------------------------------------------

TEST_CASE("Distance to a segment the ray passes directly through is zero", "[ray3d]") {
  // Segment along X at the origin; ray straight down through its midpoint.
  const Vec3 a{-10.0, 0.0, 0.0}, b{10.0, 0.0, 0.0};
  double s = 0.0;
  REQUIRE(RaySegmentDistance(DownAt(0.0, 0.0), a, b, nullptr, &s) == Approx(0.0).margin(1e-9));
  REQUIRE(s == Approx(0.5));  // midpoint of the segment
}

TEST_CASE("Distance to an offset segment is the perpendicular offset", "[ray3d]") {
  // Ray down at y = 3 against a segment along the X axis: the answer is exactly 3.
  const Vec3 a{-10.0, 0.0, 0.0}, b{10.0, 0.0, 0.0};
  REQUIRE(RaySegmentDistance(DownAt(0.0, 3.0), a, b, nullptr, nullptr) == Approx(3.0));
}

TEST_CASE("Past the end of a segment the distance is measured to the endpoint", "[ray3d]") {
  // This is what the clamp is for: a ray beyond the end must NOT report the (shorter) distance to
  // the infinite line. Ray down at x = 15 against a segment ending at x = 10, offset y = 0
  // → distance is 5 (to the endpoint), not 0 (to the extended line).
  const Vec3 a{-10.0, 0.0, 0.0}, b{10.0, 0.0, 0.0};
  double s = 0.0;
  REQUIRE(RaySegmentDistance(DownAt(15.0, 0.0), a, b, nullptr, &s) == Approx(5.0));
  REQUIRE(s == Approx(1.0));  // clamped to the far end
}

TEST_CASE("A vertical segment is measured correctly from a horizontal ray", "[ray3d]") {
  // The case a 2D pick cannot express at all: a segment rising in Z, viewed side-on. A ray along
  // +X at height z=5 passes 2 units from a vertical segment standing at x=10, y=2.
  const Vec3 a{10.0, 2.0, 0.0}, b{10.0, 2.0, 10.0};
  const Ray alongX{{0.0, 2.0, 5.0}, {1.0, 0.0, 0.0}};
  REQUIRE(RaySegmentDistance(alongX, a, b, nullptr, nullptr) == Approx(0.0).margin(1e-9));

  const Ray offsetY{{0.0, 0.0, 5.0}, {1.0, 0.0, 0.0}};
  REQUIRE(RaySegmentDistance(offsetY, a, b, nullptr, nullptr) == Approx(2.0));
}

TEST_CASE("A zero-length segment behaves as a point", "[ray3d]") {
  const Vec3 p{0.0, 4.0, 0.0};
  REQUIRE(RaySegmentDistance(DownAt(0.0, 0.0), p, p, nullptr, nullptr) == Approx(4.0));
}

TEST_CASE("A degenerate ray returns a large finite distance, never NaN", "[ray3d]") {
  // Callers compare against a pick tolerance; NaN would compare false against everything and a
  // bad ray would silently "pick" nothing OR everything depending on the comparison's direction.
  const Ray bad{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
  const double d = RaySegmentDistance(bad, Vec3{0, 0, 0}, Vec3{1, 0, 0}, nullptr, nullptr);
  REQUIRE(std::isfinite(d));
  REQUIRE(d > 1e20);
}

// ---------------------------------------------------------------------------
// RayPointDistance — survey points and snap candidates.
// ---------------------------------------------------------------------------

TEST_CASE("Distance to a point off the ray axis is the perpendicular offset", "[ray3d]") {
  REQUIRE(RayPointDistance(DownAt(0.0, 0.0), Vec3{3.0, 4.0, 0.0}) == Approx(5.0));
  REQUIRE(RayPointDistance(DownAt(3.0, 4.0), Vec3{3.0, 4.0, 0.0}) == Approx(0.0).margin(1e-9));
}

TEST_CASE("A point behind the ray origin is measured from the origin", "[ray3d]") {
  // Clamping t at 0 means a point behind the camera reports its true distance from the eye rather
  // than a bogus small value from the backward extension of the ray.
  const Ray down = DownAt(0.0, 0.0, 0.0);          // origin at z=0 looking down
  REQUIRE(RayPointDistance(down, Vec3{0.0, 0.0, 10.0}) == Approx(10.0));
}

TEST_CASE("Elevation separates points that are coincident in plan", "[ray3d]") {
  // The consequence the user accepted when survey elevation became the drawn Z: two points at the
  // same easting/northing are distinct in 3D, and a side-on ray can tell them apart.
  const Ray alongX{{-100.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};
  REQUIRE(RayPointDistance(alongX, Vec3{0.0, 0.0, 0.0}) == Approx(0.0).margin(1e-9));
  REQUIRE(RayPointDistance(alongX, Vec3{0.0, 0.0, 15.0}) == Approx(15.0));
}
