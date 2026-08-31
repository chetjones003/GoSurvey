// The UCS transform core (REQ-154, GitHub #126).
//
// This is where the coordinate-system rules are actually proven. Every case below checks a value a
// hand calculation can confirm, because "the UCS looked right on screen" is exactly the kind of
// evidence that hides a mirrored or skewed frame until someone plots a drawing.
//
// The invariant that matters most and is asserted on EVERY constructed frame: the basis stays
// right-handed and orthonormal. A left-handed UCS does not crash, it silently mirrors everything
// the user draws.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "util/ucs.hpp"

#include <cmath>

using Catch::Approx;
using ucs::Ucs;
using ucs::Vec3;

namespace {

void RequireVec(const Vec3& got, double x, double y, double z) {
  REQUIRE(got.x == Approx(x).margin(1e-9));
  REQUIRE(got.y == Approx(y).margin(1e-9));
  REQUIRE(got.z == Approx(z).margin(1e-9));
}

/// A UCS rotated 90 degrees about world Z with its origin moved - the common survey case, and the
/// one the round-trip and ORTHO tests lean on.
Ucs RotatedAt(double ox, double oy, double deg) {
  Ucs u;
  u.origin = {ox, oy, 0.0};
  return ucs::RotatedAboutZ(u, deg);
}

}  // namespace

// ---------------------------------------------------------------------------
// The default frame, and what "World" means.
// ---------------------------------------------------------------------------

TEST_CASE("The default UCS is the WCS", "[ucs]") {
  const Ucs u;
  REQUIRE(ucs::IsWorld(u));
  REQUIRE(ucs::IsRightHandedOrthonormal(u));
  // A point is its own image under the identity frame.
  RequireVec(ucs::WorldToUcs(u, {3.0, -4.0, 5.0}), 3.0, -4.0, 5.0);
  RequireVec(ucs::UcsToWorld(u, {3.0, -4.0, 5.0}), 3.0, -4.0, 5.0);
}

TEST_CASE("A UCS rotated in place is not World", "[ucs]") {
  // The origin is untouched, so an origin-only test would wrongly call this World.
  REQUIRE_FALSE(ucs::IsWorld(ucs::RotatedAboutZ(Ucs{}, 45.0)));
}

// ---------------------------------------------------------------------------
// WCS <-> UCS: the four transforms every command codes against.
// ---------------------------------------------------------------------------

TEST_CASE("A translated UCS shifts points by its origin", "[ucs]") {
  Ucs u;
  u.origin = {100.0, 200.0, 5.0};
  RequireVec(ucs::WorldToUcs(u, {110.0, 220.0, 8.0}), 10.0, 20.0, 3.0);
  RequireVec(ucs::UcsToWorld(u, {10.0, 20.0, 3.0}), 110.0, 220.0, 8.0);
}

TEST_CASE("A UCS rotated 90 degrees about Z maps X to Y", "[ucs]") {
  const Ucs u = ucs::RotatedAboutZ(Ucs{}, 90.0);
  REQUIRE(ucs::IsRightHandedOrthonormal(u));
  // The UCS X axis now points along world +Y, so UCS (10,0,0) is world (0,10,0).
  RequireVec(ucs::UcsToWorld(u, {10.0, 0.0, 0.0}), 0.0, 10.0, 0.0);
  // ...and world +X reads back as UCS -Y.
  RequireVec(ucs::WorldToUcs(u, {10.0, 0.0, 0.0}), 0.0, -10.0, 0.0);
}

TEST_CASE("WorldToUcs and UcsToWorld invert each other", "[ucs]") {
  // Deliberately awkward frame: translated, rotated about all three axes.
  Ucs u;
  u.origin = {1234.5, -6789.25, 42.0};
  u = ucs::RotatedAboutZ(u, 37.0);
  u = ucs::RotatedAboutX(u, -22.5);
  u = ucs::RotatedAboutY(u, 13.75);
  REQUIRE(ucs::IsRightHandedOrthonormal(u));

  const Vec3 world{987.125, -321.5, 77.25};
  const Vec3 back = ucs::UcsToWorld(u, ucs::WorldToUcs(u, world));
  RequireVec(back, world.x, world.y, world.z);
}

TEST_CASE("A vector transform ignores the origin while a point transform does not", "[ucs]") {
  Ucs u;
  u.origin = {1000.0, 1000.0, 1000.0};
  // The same numbers read as a point and as a direction must NOT agree - that difference is the
  // whole reason the vector entry points exist.
  RequireVec(ucs::WorldVectorToUcs(u, {1.0, 0.0, 0.0}), 1.0, 0.0, 0.0);
  RequireVec(ucs::WorldToUcs(u, {1.0, 0.0, 0.0}), -999.0, -1000.0, -1000.0);
}

TEST_CASE("A vector round trips through a rotated UCS", "[ucs]") {
  const Ucs u = ucs::RotatedAboutZ(Ucs{}, 30.0);
  const Vec3 v{3.0, 4.0, 5.0};
  const Vec3 back = ucs::UcsVectorToWorld(u, ucs::WorldVectorToUcs(u, v));
  RequireVec(back, 3.0, 4.0, 5.0);
  // A rotation preserves length; that is the cheapest guard against a scaled basis.
  REQUIRE(ray3d::Length(ucs::WorldVectorToUcs(u, v)) == Approx(ray3d::Length(v)));
}

// ---------------------------------------------------------------------------
// Three-point construction.
// ---------------------------------------------------------------------------

TEST_CASE("Three points build the frame they describe", "[ucs]") {
  Ucs u;
  // Origin at (10,10,0), X toward +X, and a third point in the +Y half.
  REQUIRE(ucs::FromThreePoints({10.0, 10.0, 0.0}, {20.0, 10.0, 0.0}, {10.0, 20.0, 0.0}, &u));
  REQUIRE(ucs::IsRightHandedOrthonormal(u));
  RequireVec(u.origin, 10.0, 10.0, 0.0);
  RequireVec(u.xAxis, 1.0, 0.0, 0.0);
  RequireVec(u.yAxis, 0.0, 1.0, 0.0);
  RequireVec(u.zAxis, 0.0, 0.0, 1.0);
}

TEST_CASE("The third point need not be perpendicular to the X axis", "[ucs]") {
  // (20,5,0) is not at 90 degrees from the X axis, but its perpendicular component still defines
  // +Y. The resulting frame must be exactly orthonormal regardless.
  Ucs u;
  REQUIRE(ucs::FromThreePoints({0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {20.0, 5.0, 0.0}, &u));
  REQUIRE(ucs::IsRightHandedOrthonormal(u));
  RequireVec(u.xAxis, 1.0, 0.0, 0.0);
  RequireVec(u.yAxis, 0.0, 1.0, 0.0);
}

TEST_CASE("A three-point UCS can define a vertical plane", "[ucs]") {
  // Origin, +X east, third point straight up: the XZ plane of the world, as a UCS.
  Ucs u;
  REQUIRE(ucs::FromThreePoints({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, &u));
  REQUIRE(ucs::IsRightHandedOrthonormal(u));
  RequireVec(u.yAxis, 0.0, 0.0, 1.0);
  RequireVec(u.zAxis, 0.0, -1.0, 0.0);
  // Drawing at UCS (5, 3) on this plane lands 5 east and 3 UP, at Y = 0.
  RequireVec(ucs::UcsToWorld(u, {5.0, 3.0, 0.0}), 5.0, 0.0, 3.0);
}

TEST_CASE("Three collinear points are refused rather than made into a frame", "[ucs]") {
  Ucs u;
  REQUIRE_FALSE(ucs::FromThreePoints({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {5.0, 0.0, 0.0}, &u));
}

TEST_CASE("A second point coincident with the origin is refused", "[ucs]") {
  Ucs u;
  REQUIRE_FALSE(ucs::FromThreePoints({7.0, 7.0, 0.0}, {7.0, 7.0, 0.0}, {9.0, 9.0, 0.0}, &u));
}

// ---------------------------------------------------------------------------
// ZAxis / normal construction, and the Arbitrary Axis Algorithm.
// ---------------------------------------------------------------------------

TEST_CASE("ZAxis points the UCS Z at the second pick", "[ucs]") {
  Ucs u;
  REQUIRE(ucs::FromZAxis({0.0, 0.0, 0.0}, {0.0, 0.0, 10.0}, &u));
  REQUIRE(ucs::IsRightHandedOrthonormal(u));
  RequireVec(u.zAxis, 0.0, 0.0, 1.0);
}

TEST_CASE("ZAxis along world X still yields a valid perpendicular XY plane", "[ucs]") {
  Ucs u;
  REQUIRE(ucs::FromZAxis({2.0, 3.0, 4.0}, {12.0, 3.0, 4.0}, &u));
  REQUIRE(ucs::IsRightHandedOrthonormal(u));
  RequireVec(u.zAxis, 1.0, 0.0, 0.0);
  // The XY plane must be perpendicular to that Z - i.e. contain no X component.
  REQUIRE(u.xAxis.x == Approx(0.0).margin(1e-9));
  REQUIRE(u.yAxis.x == Approx(0.0).margin(1e-9));
}

TEST_CASE("The arbitrary axis algorithm stays stable near the pole", "[ucs]") {
  // Just inside the 1/64 band the algorithm switches its reference axis. Both sides must still
  // produce an orthonormal right-handed frame - the switch exists to avoid a degenerate cross
  // product, and a bug there shows up as a frame that flips as the normal creeps past 1/64.
  Ucs a;
  Ucs b;
  REQUIRE(ucs::FromNormal({0.0, 0.0, 0.0}, {0.001, 0.0, 1.0}, &a));
  REQUIRE(ucs::FromNormal({0.0, 0.0, 0.0}, {0.1, 0.0, 1.0}, &b));
  REQUIRE(ucs::IsRightHandedOrthonormal(a));
  REQUIRE(ucs::IsRightHandedOrthonormal(b));
}

TEST_CASE("A zero-length normal is refused", "[ucs]") {
  Ucs u;
  REQUIRE_FALSE(ucs::FromNormal({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, &u));
  REQUIRE_FALSE(ucs::FromZAxis({5.0, 5.0, 5.0}, {5.0, 5.0, 5.0}, &u));
}

// ---------------------------------------------------------------------------
// Axis rotations.
// ---------------------------------------------------------------------------

TEST_CASE("Rotation about Z follows the right-hand rule", "[ucs]") {
  const Ucs u = ucs::RotatedAboutZ(Ucs{}, 90.0);
  RequireVec(u.xAxis, 0.0, 1.0, 0.0);   // +X swept toward +Y
  RequireVec(u.yAxis, -1.0, 0.0, 0.0);  // +Y swept toward -X
  RequireVec(u.zAxis, 0.0, 0.0, 1.0);   // the axis itself is fixed
}

TEST_CASE("Rotation about X follows the right-hand rule", "[ucs]") {
  const Ucs u = ucs::RotatedAboutX(Ucs{}, 90.0);
  RequireVec(u.xAxis, 1.0, 0.0, 0.0);
  RequireVec(u.yAxis, 0.0, 0.0, 1.0);
  RequireVec(u.zAxis, 0.0, -1.0, 0.0);
  REQUIRE(ucs::IsRightHandedOrthonormal(u));
}

TEST_CASE("Rotation about Y follows the right-hand rule", "[ucs]") {
  const Ucs u = ucs::RotatedAboutY(Ucs{}, 90.0);
  RequireVec(u.zAxis, 1.0, 0.0, 0.0);
  RequireVec(u.xAxis, 0.0, 0.0, -1.0);
  RequireVec(u.yAxis, 0.0, 1.0, 0.0);
  REQUIRE(ucs::IsRightHandedOrthonormal(u));
}

TEST_CASE("A negative rotation is the inverse of the positive one", "[ucs]") {
  const Ucs u = ucs::RotatedAboutZ(ucs::RotatedAboutZ(Ucs{}, 37.0), -37.0);
  REQUIRE(ucs::IsWorld(u, 1e-12));
}

TEST_CASE("Rotations are about the UCS own axes, so they compose", "[ucs]") {
  // Rotate 90 about Z, then 90 about the NEW X. The second rotation must use the rotated X axis
  // (world +Y), not world +X - that difference is the whole point of the option.
  Ucs u = ucs::RotatedAboutZ(Ucs{}, 90.0);
  u = ucs::RotatedAboutX(u, 90.0);
  REQUIRE(ucs::IsRightHandedOrthonormal(u));
  // Rotating about world +Y took the UCS Z (world +Z) onto world +X.
  RequireVec(u.zAxis, 1.0, 0.0, 0.0);
}

TEST_CASE("A full turn about any axis returns the original frame", "[ucs]") {
  Ucs u;
  u.origin = {5.0, 6.0, 7.0};
  u = ucs::RotatedAboutZ(u, 22.0);
  const Ucs turned = ucs::RotatedAboutX(u, 360.0);
  RequireVec(turned.xAxis, u.xAxis.x, u.xAxis.y, u.xAxis.z);
  RequireVec(turned.yAxis, u.yAxis.x, u.yAxis.y, u.yAxis.z);
  RequireVec(turned.zAxis, u.zAxis.x, u.zAxis.y, u.zAxis.z);
  // A rotation never moves the origin.
  RequireVec(turned.origin, 5.0, 6.0, 7.0);
}

// ---------------------------------------------------------------------------
// Origin moves preserve orientation (the single-point UCS option).
// ---------------------------------------------------------------------------

TEST_CASE("Moving the origin preserves the axis orientation", "[ucs]") {
  const Ucs rot = ucs::RotatedAboutZ(Ucs{}, 45.0);
  const Ucs moved = ucs::WithOrigin(rot, {50.0, -50.0, 10.0});
  RequireVec(moved.xAxis, rot.xAxis.x, rot.xAxis.y, rot.xAxis.z);
  RequireVec(moved.yAxis, rot.yAxis.x, rot.yAxis.y, rot.yAxis.z);
  RequireVec(moved.origin, 50.0, -50.0, 10.0);
}

// ---------------------------------------------------------------------------
// The work plane a click resolves against.
// ---------------------------------------------------------------------------

TEST_CASE("The work plane is the UCS XY plane", "[ucs]") {
  Ucs u;
  u.origin = {1.0, 2.0, 25.0};
  const ray3d::Plane p = ucs::WorkPlane(u);
  RequireVec(p.point, 1.0, 2.0, 25.0);
  RequireVec(p.normal, 0.0, 0.0, 1.0);

  // A plan-view click at (10, 10) lands on the raised plane, not on the datum.
  ray3d::Vec3 hit;
  REQUIRE(ray3d::RayPlaneIntersect(ray3d::Ray{{10.0, 10.0, 500.0}, {0.0, 0.0, -1.0}}, p, &hit));
  RequireVec(hit, 10.0, 10.0, 25.0);
}

TEST_CASE("A tilted work plane gives a click a varying elevation", "[ucs]") {
  // This is the case that a constant work-plane elevation gets wrong: on a 45-degree plane the
  // committed Z has to come from the click, not from the UCS origin.
  Ucs u;
  REQUIRE(ucs::FromThreePoints({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 1.0}, &u));
  const ray3d::Plane p = ucs::WorkPlane(u);
  ray3d::Vec3 hit;
  REQUIRE(ray3d::RayPlaneIntersect(ray3d::Ray{{0.0, 10.0, 500.0}, {0.0, 0.0, -1.0}}, p, &hit));
  REQUIRE(hit.z == Approx(10.0));  // 45 degrees: Z rises with Y
}

// ---------------------------------------------------------------------------
// POLAR tracking (issue #154, REQ-154): snap a pick onto the nearest polar ray,
// measured in the active UCS's XY plane from +X.
// ---------------------------------------------------------------------------

TEST_CASE("POLAR snaps to the nearest increment ray under the WCS", "[ucs][polar]") {
  const Vec3 anchor{10.0, 10.0, 0.0};
  // A pick roughly north-east but closer to due east: 90 deg increment -> due east.
  const Vec3 target{40.0, 15.0, 0.0};
  const Vec3 got = ucs::SnapToPolarRay(Ucs{}, anchor, target, 90.0);
  // Snapped to due east: Y collapses to the anchor row, the pick distance is preserved (AutoCAD).
  RequireVec(got, 10.0 + std::hypot(30.0, 5.0), 10.0, 0.0);
}

TEST_CASE("POLAR 45-degree increment keeps a diagonal pick on the diagonal", "[ucs][polar]") {
  const Vec3 anchor{0.0, 0.0, 0.0};
  const Vec3 got = ucs::SnapToPolarRay(Ucs{}, anchor, {10.0, 9.0, 0.0}, 45.0);
  REQUIRE(got.x == Approx(got.y));  // landed on the 45 deg ray
}

TEST_CASE("POLAR additional angles win when nearer than any increment", "[ucs][polar]") {
  const Vec3 anchor{0.0, 0.0, 0.0};
  const double extra[] = {30.0};
  // Pick at ~28 deg: nearest 90 deg multiple is 0, but the 30 deg extra angle is closer.
  const Vec3 got = ucs::SnapToPolarRay(Ucs{}, anchor, {10.0, 5.32, 0.0}, 90.0, extra, 1);
  double deg = 0.0;
  REQUIRE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Z', got, &deg));
  REQUIRE(deg == Approx(30.0).margin(1e-6));
}

// AC-6: the headless regression. Under a UCS rotated 45 deg about Z, a 90 deg polar pull must land
// along the UCS axis, not the world axis.
TEST_CASE("POLAR follows a rotated UCS: a 90-degree pull lands on the UCS axis", "[ucs][polar]") {
  const Ucs u = ucs::RotatedAboutZ(Ucs{}, 45.0);
  const Vec3 anchor{0.0, 0.0, 0.0};
  // A pick near the UCS +X direction (world 45 deg) but pulled off it.
  const Vec3 target{7.0, 8.0, 0.0};
  const Vec3 got = ucs::SnapToPolarRay(u, anchor, target, 90.0);

  // The committed point lies on the UCS +X ray: its UCS-local Y is zero, X is the planar distance.
  const Vec3 local = ucs::WorldToUcs(u, got);
  REQUIRE(local.y == Approx(0.0).margin(1e-9));
  REQUIRE(local.x == Approx(std::hypot(7.0, 8.0)).margin(1e-9));
  // And it is emphatically NOT on a world axis (that would be y == 0 in world).
  REQUIRE(got.y > 1.0);
}

// ---------------------------------------------------------------------------
// PLAN camera derivation.
// ---------------------------------------------------------------------------

TEST_CASE("PLAN of the World UCS is the startup plan view", "[ucs][plan]") {
  float az = 123.f;
  float el = 45.f;
  ucs::PlanViewAngles(Ucs{}, &az, &el);
  REQUIRE(el == Approx(90.0f));
  REQUIRE(az == Approx(0.0f));
  REQUIRE(ucs::PlanViewIsExact(Ucs{}));
}

TEST_CASE("PLAN of a UCS rotated about Z turns the camera by that angle", "[ucs][plan]") {
  // A UCS squared to a road bearing: still a plan view, but the camera spins so the UCS +Y is up.
  const Ucs u = ucs::RotatedAboutZ(Ucs{}, 30.0);
  float az = 0.f;
  float el = 0.f;
  ucs::PlanViewAngles(u, &az, &el);
  REQUIRE(el == Approx(90.0f));  // still looking straight down
  // A POSITIVE camera azimuth turns screen-up clockwise from north (measured against Camera), so a
  // UCS turned 30 degrees counter-clockwise needs -30 for its +Y to come out up the screen.
  REQUIRE(az == Approx(330.0f).margin(1e-3));
  REQUIRE(ucs::PlanViewIsExact(u));
}

TEST_CASE("PLAN of a translated UCS does not change the view direction", "[ucs][plan]") {
  // Translation moves what is centred, never where the camera looks from.
  Ucs u;
  u.origin = {5000.0, -2000.0, 30.0};
  float az = 0.f;
  float el = 0.f;
  ucs::PlanViewAngles(u, &az, &el);
  REQUIRE(el == Approx(90.0f));
  REQUIRE(az == Approx(0.0f));
}

TEST_CASE("PLAN of a vertical UCS looks horizontally along its normal", "[ucs][plan]") {
  // UCS Z along world -Y: the camera drops to the horizon. To look DOWN the UCS +Z it must sit on
  // the +Z side - the south - and look north, which is camera azimuth 0.
  Ucs u;
  REQUIRE(ucs::FromThreePoints({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, &u));
  RequireVec(u.zAxis, 0.0, -1.0, 0.0);
  float az = 123.f;
  float el = 123.f;
  ucs::PlanViewAngles(u, &az, &el);
  REQUIRE(el == Approx(0.0f).margin(1e-4));  // on the horizon
  REQUIRE(az == Approx(0.0f).margin(1e-4));  // eye to the south, looking north
  // Now that Camera has a roll axis (#153) even this frame is exact: the caller adds the roll that
  // places the UCS +Y (world +Z here) up the screen. Here the azimuth/elevation up is already +Z,
  // so the roll is zero.
  REQUIRE(ucs::PlanViewIsExact(u));
}

TEST_CASE("PlanViewIsExact holds for every valid frame (#153)", "[ucs][plan]") {
  REQUIRE(ucs::PlanViewIsExact(ucs::RotatedAboutZ(Ucs{}, 217.0)));
  REQUIRE(ucs::PlanViewIsExact(ucs::WithOrigin(Ucs{}, {1.0, 2.0, 3.0})));
  REQUIRE(ucs::PlanViewIsExact(ucs::RotatedAboutX(Ucs{}, 10.0)));
  REQUIRE(ucs::PlanViewIsExact(ucs::RotatedAboutX(Ucs{}, 180.0)));  // upside down is still a valid frame
  Ucs tilted;
  REQUIRE(ucs::FromThreePoints({0, 0, 0}, {1, 0, 0}, {0, 1, 1}, &tilted));
  REQUIRE(ucs::PlanViewIsExact(tilted));
  // Only a degenerate basis is rejected.
  Ucs bad;
  bad.yAxis = bad.xAxis;  // X and Y collinear: not an orthonormal frame
  REQUIRE_FALSE(ucs::PlanViewIsExact(bad));
}

// ---------------------------------------------------------------------------
// The ViewCube's compass angle.
// ---------------------------------------------------------------------------

TEST_CASE("The world UCS reports a zero compass rotation", "[ucs]") {
  REQUIRE(ucs::AzimuthAboutWorldZDeg(Ucs{}) == Approx(0.0f));
}

TEST_CASE("A UCS rotated about Z reports that rotation to the compass", "[ucs]") {
  REQUIRE(ucs::AzimuthAboutWorldZDeg(ucs::RotatedAboutZ(Ucs{}, 45.0)) == Approx(45.0f));
  // Wrapped into [0,360) rather than reported negative, which is what the widget expects.
  REQUIRE(ucs::AzimuthAboutWorldZDeg(ucs::RotatedAboutZ(Ucs{}, -90.0)) == Approx(270.0f));
}

// ---------------------------------------------------------------------------
// The rule the whole feature rests on: a UCS never moves geometry.
// ---------------------------------------------------------------------------

TEST_CASE("Changing the UCS leaves world coordinates untouched", "[ucs]") {
  // The same stored world point, read through three different frames, is still the same point -
  // and converting back through each frame returns it exactly. If a UCS change ever rewrote
  // geometry, this is the test that would catch it.
  const Vec3 stored{1234.5, 678.25, 42.0};
  const Ucs frames[] = {Ucs{}, RotatedAt(100.0, 100.0, 45.0), ucs::RotatedAboutX(RotatedAt(-50.0, 20.0, 10.0), 30.0)};
  for (const Ucs& f : frames) {
    const Vec3 back = ucs::UcsToWorld(f, ucs::WorldToUcs(f, stored));
    RequireVec(back, stored.x, stored.y, stored.z);
  }
}

// ---------------------------------------------------------------------------
// UCS <axis> 2P — the angle taken from two picked points.
// ---------------------------------------------------------------------------

TEST_CASE("Two points give the angle of their direction in the Z rotation plane", "[ucs]") {
  double deg = 0.0;
  // Due east from the WCS origin is the reference direction for a rotation about Z, so zero.
  REQUIRE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Z', Vec3{10.0, 0.0, 0.0}, &deg));
  REQUIRE(deg == Approx(0.0));
  // Due north is +90: Z spins X toward Y, so positive is toward +Y.
  REQUIRE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Z', Vec3{0.0, 10.0, 0.0}, &deg));
  REQUIRE(deg == Approx(90.0));
  REQUIRE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Z', Vec3{10.0, 10.0, 0.0}, &deg));
  REQUIRE(deg == Approx(45.0));
  // Signed, not absolute — a line running south-east is negative, not 45.
  REQUIRE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Z', Vec3{10.0, -10.0, 0.0}, &deg));
  REQUIRE(deg == Approx(-45.0));
}

TEST_CASE("The measured angle is what makes the rotated X axis point along the picks", "[ucs]") {
  // This is the property the feature actually promises: pick two points along a lot line, and the
  // UCS X axis ends up running down that line. Feeding the measured angle back into the rotation
  // is what has to produce that, so the two are tested together rather than separately.
  const Vec3 dir{3.0, 4.0, 0.0};  // 3-4-5, so the axis lands on exact values
  double deg = 0.0;
  REQUIRE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Z', dir, &deg));
  const Ucs turned = ucs::RotatedAboutZ(Ucs{}, deg);
  RequireVec(turned.xAxis, 0.6, 0.8, 0.0);
  REQUIRE(ucs::IsRightHandedOrthonormal(turned));
}

TEST_CASE("Two points measured in an already-rotated frame are relative to that frame", "[ucs]") {
  // The angle is measured in the CURRENT UCS, not the world, so the rotations compose the way
  // applying them one after another does. A frame already at 30 degrees, given a due-east pick,
  // measures -30 - which turns it back to world-aligned rather than leaving it where it was.
  const Ucs at30 = ucs::RotatedAboutZ(Ucs{}, 30.0);
  double deg = 0.0;
  REQUIRE(ucs::AngleInRotationPlaneDeg(at30, 'Z', Vec3{10.0, 0.0, 0.0}, &deg));
  REQUIRE(deg == Approx(-30.0));
  RequireVec(ucs::RotatedAboutZ(at30, deg).xAxis, 1.0, 0.0, 0.0);
}

TEST_CASE("Each axis measures in the plane its own rotation spins", "[ucs]") {
  double deg = 0.0;
  // X spins Y toward Z: measured from +Y, positive toward +Z.
  REQUIRE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'X', Vec3{0.0, 0.0, 5.0}, &deg));
  REQUIRE(deg == Approx(90.0));
  // Y spins Z toward X: measured from +Z, positive toward +X.
  REQUIRE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Y', Vec3{5.0, 0.0, 0.0}, &deg));
  REQUIRE(deg == Approx(90.0));
}

TEST_CASE("A direction with no component in the rotation plane is refused", "[ucs]") {
  double deg = 123.0;
  // Straight up defines no rotation ABOUT Z - there is no angle to measure, and any answer would
  // be invented. The refusal leaves the caller's value alone (REQ-201).
  REQUIRE_FALSE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Z', Vec3{0.0, 0.0, 10.0}, &deg));
  REQUIRE(deg == Approx(123.0));
  // Two coincident picks: no direction at all.
  REQUIRE_FALSE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Z', Vec3{0.0, 0.0, 0.0}, &deg));
  // The out-of-plane test scales with the picks rather than using a fixed distance, so a mile-long
  // pick pair that is barely off-plane is still refused for the same reason a short one is.
  REQUIRE_FALSE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Z', Vec3{1e-9, 0.0, 5280.0}, &deg));
  // An unknown axis letter is refused rather than silently treated as Z.
  REQUIRE_FALSE(ucs::AngleInRotationPlaneDeg(Ucs{}, 'Q', Vec3{1.0, 0.0, 0.0}, &deg));
}

// ---------------------------------------------------------------------------
// The plane contract (REQ-311).
//
// A `Ucs` is the plane abstraction: origin, normal, and an in-plane axis pair. These cases prove
// the 2D <-> world conversion is a true inverse and that the off-plane component is REPORTED rather
// than dropped, which is the failure the explicit offset output exists to prevent.
// ---------------------------------------------------------------------------

TEST_CASE("On the world frame, plane coordinates are just X and Y", "[ucs][req311]") {
  const Ucs w;
  double off = 99.0;
  const ucs::Point2D p = ucs::WorldToPlane(w, Vec3{3.0, -4.0, 7.5}, &off);
  REQUIRE(p.x == Approx(3.0));
  REQUIRE(p.y == Approx(-4.0));
  // The Z is the OFFSET, not a third plane coordinate, and not silently discarded.
  REQUIRE(off == Approx(7.5));
  RequireVec(ucs::PlaneToWorld(w, p, off), 3.0, -4.0, 7.5);
  // Without the offset the point lands on the plane itself.
  RequireVec(ucs::PlaneToWorld(w, p), 3.0, -4.0, 0.0);
}

TEST_CASE("A tilted plane round-trips a survey-magnitude point well inside REQ-101", "[ucs][req311]") {
  Ucs tilted;
  // A 3-4-5 normal, so nothing here is axis-aligned and a dropped or swapped axis cannot pass.
  REQUIRE(ucs::FromNormal(Vec3{1200.0, -800.0, 42.0}, Vec3{3.0, 4.0, 5.0}, &tilted));
  REQUIRE(ucs::IsRightHandedOrthonormal(tilted));

  const Vec3 world{2143.75, -1288.5, 311.25};
  double off = 0.0;
  const ucs::Point2D p = ucs::WorldToPlane(tilted, world, &off);
  const Vec3 back = ucs::PlaneToWorld(tilted, p, off);
  // REQ-101 is +/-0.01 ft; the plane maths is in double, so the real error is ~1e-12. Asserting the
  // tight bound is the point - a round trip that merely scrapes under 0.01 ft would mean something
  // had been narrowed to float on the way through.
  REQUIRE(back.x == Approx(world.x).margin(1e-9));
  REQUIRE(back.y == Approx(world.y).margin(1e-9));
  REQUIRE(back.z == Approx(world.z).margin(1e-9));
}

TEST_CASE("Signed distance is positive on the plane's +Z side", "[ucs][req311]") {
  Ucs w;
  w.origin = {0.0, 0.0, 10.0};
  REQUIRE(ucs::SignedDistanceToPlane(w, Vec3{5.0, 5.0, 12.0}) == Approx(2.0));
  REQUIRE(ucs::SignedDistanceToPlane(w, Vec3{5.0, 5.0, 8.0}) == Approx(-2.0));
  REQUIRE(ucs::SignedDistanceToPlane(w, Vec3{-100.0, 250.0, 10.0}) == Approx(0.0).margin(1e-12));

  // A 45-degree plane through the origin: the distance from (1,0,0) is cos(45).
  Ucs tilt;
  REQUIRE(ucs::FromNormal(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 1.0}, &tilt));
  REQUIRE(ucs::SignedDistanceToPlane(tilt, Vec3{1.0, 0.0, 0.0}) == Approx(std::sqrt(0.5)));
  REQUIRE(ucs::SignedDistanceToPlane(tilt, Vec3{-1.0, 0.0, 0.0}) == Approx(-std::sqrt(0.5)));
}

TEST_CASE("Projecting onto a plane leaves a point with no offset", "[ucs][req311]") {
  Ucs tilt;
  REQUIRE(ucs::FromNormal(Vec3{5.0, 5.0, 5.0}, Vec3{-2.0, 1.0, 3.0}, &tilt));
  const Vec3 world{101.0, -37.5, 63.25};
  const Vec3 flat = ucs::ProjectOntoPlane(tilt, world);
  REQUIRE(ucs::SignedDistanceToPlane(tilt, flat) == Approx(0.0).margin(1e-9));
  // What was removed is exactly the normal component - the projection moves the point along the
  // normal and in no other direction.
  const Vec3 removed = ray3d::Sub(world, flat);
  const double d = ucs::SignedDistanceToPlane(tilt, world);
  RequireVec(removed, tilt.zAxis.x * d, tilt.zAxis.y * d, tilt.zAxis.z * d);
  // A point already on the plane is left where it is.
  RequireVec(ucs::ProjectOntoPlane(tilt, flat), flat.x, flat.y, flat.z);
}

TEST_CASE("A circle parametrised on the world plane is the familiar cos/sin", "[ucs][req311]") {
  Ucs w;
  w.origin = {10.0, 20.0, 3.0};
  const double r = 4.0;
  RequireVec(ucs::PointOnPlaneCircle(w, r, 0.0), 14.0, 20.0, 3.0);
  RequireVec(ucs::PointOnPlaneCircle(w, r, 3.14159265358979323846 / 2.0), 10.0, 24.0, 3.0);
  RequireVec(ucs::PointOnPlaneCircle(w, r, 3.14159265358979323846), 6.0, 20.0, 3.0);
}

TEST_CASE("A circle on a vertical plane stays in that plane at a constant radius", "[ucs][req311]") {
  Ucs vert;
  // Normal along world +X: the circle lives in the YZ plane, the case a flat-only arc store cannot
  // represent at all.
  REQUIRE(ucs::FromNormal(Vec3{50.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}, &vert));
  const double r = 7.5;
  for (int i = 0; i < 16; ++i) {
    const double a = (2.0 * 3.14159265358979323846) * static_cast<double>(i) / 16.0;
    const Vec3 p = ucs::PointOnPlaneCircle(vert, r, a);
    // Every point is on the plane...
    REQUIRE(ucs::SignedDistanceToPlane(vert, p) == Approx(0.0).margin(1e-9));
    REQUIRE(p.x == Approx(50.0).margin(1e-9));
    // ...and exactly the radius from the centre.
    REQUIRE(ray3d::Length(ray3d::Sub(p, vert.origin)) == Approx(r));
  }
}

TEST_CASE("The circle parametrisation and the plane conversion agree", "[ucs][req311]") {
  // The renderer, the hit test and the DXF writer all go through PointOnPlaneCircle; this is the
  // assertion that its angle really is measured in the frame's own 2D coordinates, so a consumer
  // that reconstructs the frame from the normal alone lands on the same points.
  Ucs plane;
  REQUIRE(ucs::FromNormal(Vec3{-3.0, 8.0, 1.5}, Vec3{2.0, -3.0, 6.0}, &plane));
  const double r = 12.25;
  const double a = 1.1;
  double off = 1.0;
  const ucs::Point2D p = ucs::WorldToPlane(plane, ucs::PointOnPlaneCircle(plane, r, a), &off);
  REQUIRE(off == Approx(0.0).margin(1e-9));
  REQUIRE(p.x == Approx(r * std::cos(a)));
  REQUIRE(p.y == Approx(r * std::sin(a)));
}
