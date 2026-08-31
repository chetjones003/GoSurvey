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
