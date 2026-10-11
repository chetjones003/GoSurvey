// REQ-332 / ADR-046 amendment (m), GitHub issue #148 acceptance 4 — rigid rotation and uniform
// scale of a solid.
//
// These are PLACEMENT transforms, so they are checkable exactly rather than within a tolerance, and
// the two invariants below are what the acceptance is built on:
//
//   * a rotation is an ISOMETRY, so volume and surface area are unchanged to the last digit the
//     closed forms give. Any drift is a defect, not a tolerance — the same argument REQ-322 used
//     for translation;
//   * a uniform scale by `k` multiplies volume by exactly `k^3` and area by exactly `k^2`.
//
// The subject of the file, though, is the thing a per-field sweep at a call site gets wrong: **a
// frame is one POINT and three DIRECTIONS**, and the two are transformed by different rules. Under a
// rotation both turn, but the origin turns about the axis LINE and the axes about the axis
// DIRECTION. Under a scale only the origin moves and the axes must not be touched at all.
//
// MEASURED, by deleting the three axis lines from `RotateFrameInPlace` and rebuilding. The result
// splits in a way that is worth knowing:
//
//   * about world Z, `Validate` DOES catch it and the rotation is refused;
//   * about a TILTED axis it does not. The solid comes back closed, manifold and positive-volume —
//     `Validate` returns Ok — and reports a volume of **1142.5693570452 against a true 1600**,
//     **28.6% wrong**, because every face's surface still faces the direction it faced before the
//     solid turned underneath it.
//
// That is the same shape of defect `PushPullTests` documents for its own precondition, and the same
// reason `brep::Translate`'s documentation insists this logic lives in the kernel: a solid that
// half-moved is not a shape at all, and it does not fail loudly.
//
// The refusals carry their own measurement rather than an assertion (see the last case): a ZERO
// rotation axis is not a no-op, it is a uniform shrink by `cos(angle)`, and that is shown as a
// number here so the check that refuses it is demonstrably necessary.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "util/brep.hpp"
#include "util/nurbs.hpp"

using Catch::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;

ucs::Ucs World() { return ucs::Ucs{}; }

brep::Solid Box(double l, double w, double h) {
  brep::Solid s;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(World(), l, w, h, &s, &why));
  return s;
}

brep::Solid Cylinder(double r, double h) {
  brep::Solid s;
  brep::Problem why{};
  REQUIRE(brep::MakeCylinder(World(), r, h, &s, &why));
  return s;
}

brep::MassProperties Mass(const brep::Solid& s) {
  const brep::MassProperties m = brep::ComputeMassProperties(s);
  REQUIRE(m.valid);
  return m;
}

brep::Solid Rotated(const brep::Solid& s, const ray3d::Vec3& axisPoint, const ray3d::Vec3& axisUnit,
                    double angleRad) {
  brep::Solid out;
  brep::Problem why{};
  REQUIRE(brep::Rotate(s, axisPoint, axisUnit, angleRad, &out, &why));
  REQUIRE(why == brep::Problem::Ok);
  return out;
}

brep::Solid Scaled(const brep::Solid& s, const ray3d::Vec3& basePoint, double factor) {
  brep::Solid out;
  brep::Problem why{};
  REQUIRE(brep::Scale(s, basePoint, factor, &out, &why));
  REQUIRE(why == brep::Problem::Ok);
  return out;
}

/// Every frame the solid stores, so a test can assert a property across all of them at once. The
/// point of collecting them is that a rotation has to reach EVERY frame: missing one is the defect
/// this file exists to catch, and it would otherwise hide in whichever frame the test forgot.
std::vector<ucs::Ucs> AllFrames(const brep::Solid& s) {
  std::vector<ucs::Ucs> out;
  for (const brep::Face& f : s.faces)
    out.push_back(f.surface.frame);
  for (const brep::Edge& e : s.edges) {
    if (e.kind != brep::CurveKind::Line)
      out.push_back(e.frame);
    for (const brep::Surface& sf : e.isectSurfaces)
      out.push_back(sf.frame);
  }
  out.push_back(s.recipe.frame);
  return out;
}

double MaxVertexGap(const brep::Solid& a, const brep::Solid& b) {
  REQUIRE(a.vertices.size() == b.vertices.size());
  double worst = 0.0;
  for (size_t i = 0; i < a.vertices.size(); ++i)
    worst = std::max(worst, ray3d::Length(ray3d::Sub(a.vertices[i].p, b.vertices[i].p)));
  return worst;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Rotation.
// ---------------------------------------------------------------------------------------------

TEST_CASE("A rotation is an isometry: volume and area are untouched (REQ-332)", "[rotate]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const brep::MassProperties before = Mass(box);
  REQUIRE(before.volume == Approx(1600.0));
  REQUIRE(before.surfaceArea == Approx(880.0));  // 2*(200 + 160 + 80)

  SECTION("90 degrees about world Z through the origin") {
    const brep::Solid r = Rotated(box, {0, 0, 0}, {0, 0, 1}, 0.5 * kPi);
    const brep::MassProperties after = Mass(r);
    REQUIRE(after.volume == Approx(before.volume));
    REQUIRE(after.surfaceArea == Approx(before.surfaceArea));
    REQUIRE(brep::Validate(r) == brep::Problem::Ok);
  }

  SECTION("a genuinely tilted axis, off the origin") {
    const ray3d::Vec3 axis = ray3d::Normalize({1.0, 2.0, 3.0});
    const brep::Solid r = Rotated(box, {4.0, -7.0, 2.5}, axis, 0.7);
    const brep::MassProperties after = Mass(r);
    REQUIRE(after.volume == Approx(before.volume));
    REQUIRE(after.surfaceArea == Approx(before.surfaceArea));
    REQUIRE(brep::Validate(r) == brep::Problem::Ok);
  }
}

TEST_CASE("Four quarter turns are the identity (REQ-332)", "[rotate]") {
  // The sharpest single check that the rotation reaches everything and reaches it consistently: any
  // frame left behind, or turned by a different rule from its own origin, fails to come home.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const ray3d::Vec3 axis = ray3d::Normalize({1.0, -2.0, 0.5});
  const ray3d::Vec3 pivot{3.0, 1.0, -2.0};

  brep::Solid r = box;
  for (int i = 0; i < 4; ++i)
    r = Rotated(r, pivot, axis, 0.5 * kPi);

  REQUIRE(MaxVertexGap(box, r) < 1e-12);
  REQUIRE(Mass(r).volume == Approx(Mass(box).volume));
}

TEST_CASE("A rotation turns a frame's AXES, not only its origin (REQ-332)", "[rotate]") {
  // The defect a per-field sweep produces: origins moved, axes left behind. It validates, and it is
  // a sheared solid. Asserted on a cylinder because its surface frame's Z axis IS the cylinder's
  // axis — a direction with a meaning that can be checked independently of any vertex.
  const brep::Solid cyl = Cylinder(5.0, 10.0);
  const double angle = 0.5 * kPi;
  const brep::Solid r = Rotated(cyl, {0, 0, 0}, {1, 0, 0}, angle);

  // The cylinder's axis started at +Z; a quarter turn about +X takes it to -Y.
  bool sawCylinder = false;
  for (const brep::Face& f : r.faces) {
    if (f.surface.kind != brep::SurfaceKind::Cylinder)
      continue;
    sawCylinder = true;
    REQUIRE(f.surface.frame.zAxis.x == Approx(0.0).margin(1e-12));
    REQUIRE(f.surface.frame.zAxis.y == Approx(-1.0));
    REQUIRE(f.surface.frame.zAxis.z == Approx(0.0).margin(1e-12));
  }
  REQUIRE(sawCylinder);

  // ...and the radius and height are LENGTHS, invariant under a rotation, so they must not have
  // been "helpfully" recomputed.
  REQUIRE(Mass(r).volume == Approx(Mass(cyl).volume));
}

TEST_CASE("Every frame stays right-handed and orthonormal after a rotation (REQ-332)", "[rotate]") {
  // A rotation preserves handedness; a shear or a mirror does not, and `IsRightHandedOrthonormal` is
  // the check that catches both. Run across EVERY frame the solid stores, on a cylinder so that
  // curved-edge frames and a curved surface frame are both in the set.
  const brep::Solid cyl = Cylinder(5.0, 10.0);
  const ray3d::Vec3 axis = ray3d::Normalize({2.0, -1.0, 4.0});
  const brep::Solid r = Rotated(cyl, {1.0, 2.0, 3.0}, axis, 1.1);

  const std::vector<ucs::Ucs> frames = AllFrames(r);
  REQUIRE(frames.size() > 3);  // it really did collect curved-edge and surface frames, not just one
  for (const ucs::Ucs& f : frames)
    REQUIRE(ucs::IsRightHandedOrthonormal(f, 1e-9));
}

TEST_CASE("A rotated solid keeps its recipe, turned with it (REQ-332)", "[rotate]") {
  // The contrast worth stating. PUSH/PULL and FILLET both DROP the recipe (REQ-319 item 9,
  // REQ-323 item 9) because a pushed or rounded box is no longer the box its recipe describes, and a
  // recipe that no longer describes its solid reads as authoritative while being false.
  //
  // A rotation is not in that class. A rotated box is still exactly a box — the same length, width
  // and height, in a turned frame — so the recipe can follow it precisely, and dropping it would
  // discard a true description rather than a false one. `Translate` already set this precedent by
  // moving `recipe.frame.origin` and keeping everything else.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  REQUIRE(box.recipe.kind == brep::PrimitiveKind::Box);

  const brep::Solid r = Rotated(box, {0, 0, 0}, {0, 0, 1}, 0.5 * kPi);
  REQUIRE(r.recipe.kind == brep::PrimitiveKind::Box);
  REQUIRE(r.recipe.length == Approx(20.0));  // a length is invariant under a rotation
  REQUIRE(r.recipe.width == Approx(10.0));
  REQUIRE(r.recipe.height == Approx(8.0));
  // ...and the placement frame turned: the recipe's X axis started at +X and is now +Y.
  REQUIRE(r.recipe.frame.xAxis.x == Approx(0.0).margin(1e-12));
  REQUIRE(r.recipe.frame.xAxis.y == Approx(1.0));
}

// ---------------------------------------------------------------------------------------------
// Uniform scale.
// ---------------------------------------------------------------------------------------------

TEST_CASE("A uniform scale multiplies volume by k^3 and area by k^2 (REQ-332)", "[scale]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const brep::MassProperties before = Mass(box);

  SECTION("k = 2 about the origin") {
    const brep::Solid s = Scaled(box, {0, 0, 0}, 2.0);
    const brep::MassProperties after = Mass(s);
    REQUIRE(after.volume == Approx(before.volume * 8.0));
    REQUIRE(after.surfaceArea == Approx(before.surfaceArea * 4.0));
    REQUIRE(brep::Validate(s) == brep::Problem::Ok);
  }

  SECTION("k = 0.25 about a base point away from the solid") {
    const brep::Solid s = Scaled(box, {100.0, -50.0, 12.0}, 0.25);
    const brep::MassProperties after = Mass(s);
    REQUIRE(after.volume == Approx(before.volume * 0.25 * 0.25 * 0.25));
    REQUIRE(after.surfaceArea == Approx(before.surfaceArea * 0.25 * 0.25));
    REQUIRE(brep::Validate(s) == brep::Problem::Ok);
  }

  SECTION("a curved solid, where the RADIUS has to scale and the sweep must not") {
    const brep::Solid cyl = Cylinder(5.0, 10.0);
    const brep::Solid s = Scaled(cyl, {0, 0, 0}, 3.0);
    REQUIRE(Mass(s).volume == Approx(Mass(cyl).volume * 27.0));
    REQUIRE(Mass(s).surfaceArea == Approx(Mass(cyl).surfaceArea * 9.0));
    REQUIRE(brep::Validate(s) == brep::Problem::Ok);
  }
}

TEST_CASE("Scaling by k then by 1/k returns the original coordinates (REQ-332)", "[scale]") {
  const brep::Solid cyl = Cylinder(5.0, 10.0);
  const ray3d::Vec3 base{7.0, -3.0, 2.0};
  const brep::Solid there = Scaled(cyl, base, 6.5);
  const brep::Solid back = Scaled(there, base, 1.0 / 6.5);
  REQUIRE(MaxVertexGap(cyl, back) < 1e-12);
  REQUIRE(Mass(back).volume == Approx(Mass(cyl).volume));
}

TEST_CASE("A uniform scale leaves every frame's AXES alone (REQ-332)", "[scale]") {
  // The mirror image of the rotation rule, and the other half of the point/direction split: a scale
  // turns nothing, so an implementation that "helpfully" renormalized or transformed the axes would
  // be wrong in the opposite direction. Asserted by comparing axis-for-axis with the original.
  const brep::Solid cyl = Cylinder(5.0, 10.0);
  const brep::Solid s = Scaled(cyl, {4.0, 4.0, 4.0}, 2.5);

  const std::vector<ucs::Ucs> a = AllFrames(cyl);
  const std::vector<ucs::Ucs> b = AllFrames(s);
  REQUIRE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    REQUIRE(ray3d::Length(ray3d::Sub(a[i].xAxis, b[i].xAxis)) < 1e-15);
    REQUIRE(ray3d::Length(ray3d::Sub(a[i].yAxis, b[i].yAxis)) < 1e-15);
    REQUIRE(ray3d::Length(ray3d::Sub(a[i].zAxis, b[i].zAxis)) < 1e-15);
  }
}

TEST_CASE("A scaled solid's recipe resizes with it (REQ-332)", "[scale]") {
  // The row a per-field sweep at a call site misses. The recipe is description and never truth, but
  // it is what the Properties panel reports — a solid scaled to twice the size that still claims
  // "Radius 5" states a number that is simply false.
  const brep::Solid cyl = Cylinder(5.0, 10.0);
  REQUIRE(cyl.recipe.kind == brep::PrimitiveKind::Cylinder);
  REQUIRE(cyl.recipe.radius == Approx(5.0));
  REQUIRE(cyl.recipe.height == Approx(10.0));

  const brep::Solid s = Scaled(cyl, {0, 0, 0}, 2.0);
  REQUIRE(s.recipe.kind == brep::PrimitiveKind::Cylinder);
  REQUIRE(s.recipe.radius == Approx(10.0));
  REQUIRE(s.recipe.height == Approx(20.0));

  // ...and it agrees with the geometry it describes, which is the point of updating it at all.
  REQUIRE(Mass(s).volume == Approx(kPi * s.recipe.radius * s.recipe.radius * s.recipe.height));
}

// ---------------------------------------------------------------------------------------------
// The NURBS control net, and why moving only the control points is exact.
// ---------------------------------------------------------------------------------------------

TEST_CASE("Rotating a patch's control net rotates every point of the surface (REQ-332)", "[rotate]") {
  // The claim in `nurbs::Rotate`'s documentation, checked rather than asserted: a NURBS surface is an
  // affine combination of its control points whose basis functions sum to one, so transforming the
  // net transforms the surface EXACTLY. If that were only approximate, a freeform face would drift
  // away from the analytic faces it shares edges with.
  const nurbs::Patch flat = nurbs::RuledLinear({{0, 0, 0}, {10, 0, 0}, {20, 0, 5}},
                                               {{0, 10, 0}, {10, 10, 3}, {20, 10, 5}});
  REQUIRE(nurbs::IsValidPatch(flat));

  const ray3d::Vec3 axis = ray3d::Normalize({1.0, 1.0, 2.0});
  const ray3d::Vec3 pivot{2.0, -1.0, 4.0};
  const double angle = 0.9;
  const nurbs::Patch turned = nurbs::Rotate(flat, pivot, axis, angle);

  REQUIRE(turned.wts == flat.wts);        // weights are ratios: a rotation must not touch them
  REQUIRE(turned.knotsU == flat.knotsU);  // nor the parametrisation
  REQUIRE(turned.knotsV == flat.knotsV);

  for (double u = 0.0; u <= 1.0001; u += 0.25) {
    for (double v = 0.0; v <= 1.0001; v += 0.25) {
      const ray3d::Vec3 onTurned = nurbs::Evaluate(turned, u, v);
      const ray3d::Vec3 turnedPoint =
          ray3d::RotatePointAboutAxis(nurbs::Evaluate(flat, u, v), pivot, axis, angle);
      REQUIRE(ray3d::Length(ray3d::Sub(onTurned, turnedPoint)) < 1e-12);
    }
  }
}

TEST_CASE("Scaling a patch's control net scales the surface it defines (REQ-332)", "[scale]") {
  const nurbs::Patch flat = nurbs::RuledLinear({{0, 0, 0}, {10, 0, 0}, {20, 0, 5}},
                                               {{0, 10, 0}, {10, 10, 3}, {20, 10, 5}});
  const ray3d::Vec3 base{1.0, 2.0, 3.0};
  const double k = 2.5;
  const nurbs::Patch bigger = nurbs::Scale(flat, base, k);

  REQUIRE(bigger.wts == flat.wts);  // scaling the WEIGHTS would change the shape, not the size

  for (double u = 0.0; u <= 1.0001; u += 0.5) {
    for (double v = 0.0; v <= 1.0001; v += 0.5) {
      const ray3d::Vec3 got = nurbs::Evaluate(bigger, u, v);
      const ray3d::Vec3 want =
          ray3d::Add(base, ray3d::Scale(ray3d::Sub(nurbs::Evaluate(flat, u, v), base), k));
      REQUIRE(ray3d::Length(ray3d::Sub(got, want)) < 1e-12);
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Refusals — and the measurement that shows the axis check is necessary.
// ---------------------------------------------------------------------------------------------

TEST_CASE("A degenerate rotation axis is a silent SHRINK, which is why it is refused (REQ-332)",
          "[rotate][req201]") {
  // Measured on the primitive, because `brep::Rotate` refuses to let it reach a solid. Rodrigues'
  // formula is `v c + (axis x v) s + axis (axis . v)(1 - c)`. With a ZERO axis the last two terms
  // vanish and the whole thing collapses to `v * cos(angle)` — a uniform shrink wearing a rotation's
  // name. This is the number that argues the check is load-bearing rather than tidy.
  const ray3d::Vec3 v{3.0, 4.0, 12.0};  // length 13
  const double angle = 1.0;
  const ray3d::Vec3 shrunk = ray3d::RotateVectorAboutAxis(v, {0, 0, 0}, angle);
  REQUIRE(ray3d::Length(shrunk) == Approx(13.0 * std::cos(angle)));
  REQUIRE(ray3d::Length(shrunk) < 13.0);  // emphatically not a rotation

  // A non-unit axis is the other half: the cross-product term scales as |axis| and the projection
  // term as |axis|^2, so the result is sheared rather than turned, and its length is not preserved.
  const ray3d::Vec3 sheared = ray3d::RotateVectorAboutAxis(v, {0.0, 0.0, 2.0}, angle);
  REQUIRE(ray3d::Length(sheared) != Approx(13.0));

  // So the kernel refuses both, by name, rather than shipping either as a rotation.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  brep::Solid out;
  brep::Problem why{};

  REQUIRE_FALSE(brep::Rotate(box, {0, 0, 0}, {0, 0, 0}, angle, &out, &why));
  REQUIRE(why == brep::Problem::RotateAxisNotUnit);

  REQUIRE_FALSE(brep::Rotate(box, {0, 0, 0}, {0, 0, 2}, angle, &out, &why));
  REQUIRE(why == brep::Problem::RotateAxisNotUnit);

  REQUIRE_FALSE(brep::Rotate(box, {0, 0, 0}, {0, 0, 1}, std::nan(""), &out, &why));
  REQUIRE(why == brep::Problem::NonFiniteParameter);
}

TEST_CASE("Scale refuses a factor that is not a positive number, by name (REQ-332 / REQ-201)",
          "[scale][req201]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  brep::Solid out;
  brep::Problem why{};

  SECTION("zero would collapse the solid to nothing") {
    REQUIRE_FALSE(brep::Scale(box, {0, 0, 0}, 0.0, &out, &why));
    REQUIRE(why == brep::Problem::ScaleFactorNonPositive);
  }
  SECTION("negative would MIRROR it, which is a different operation") {
    REQUIRE_FALSE(brep::Scale(box, {0, 0, 0}, -2.0, &out, &why));
    REQUIRE(why == brep::Problem::ScaleFactorNonPositive);
  }
  SECTION("NaN") {
    REQUIRE_FALSE(brep::Scale(box, {0, 0, 0}, std::nan(""), &out, &why));
    REQUIRE(why == brep::Problem::ScaleFactorNonPositive);
  }
  SECTION("a non-finite base point") {
    REQUIRE_FALSE(brep::Scale(box, {std::nan(""), 0, 0}, 2.0, &out, &why));
    REQUIRE(why == brep::Problem::NonFiniteParameter);
  }

  // Every refusal leaves the caller's solid untouched — nothing is half-applied.
  REQUIRE(Mass(box).volume == Approx(1600.0));
}

TEST_CASE("Every new refusal has a sentence a user can read (REQ-332 / REQ-201)", "[rotate][scale]") {
  for (const brep::Problem p : {brep::Problem::RotateAxisNotUnit, brep::Problem::RotateResultInvalid,
                                brep::Problem::ScaleFactorNonPositive,
                                brep::Problem::ScaleResultInvalid}) {
    const char* text = brep::ProblemText(p);
    REQUIRE(text != nullptr);
    REQUIRE(std::string(text) != "The solid is not valid.");  // i.e. it did not fall through
  }
}
