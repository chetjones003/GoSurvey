// The NURBS patch math (REQ-315 / ADR-048, GitHub issue #147 — Phase 4 of #120).
//
// This is the freeform surface loft and sweep raise over their profiles. It runs with no window and
// no B-rep — the module is pure by ADR-048 (c) — and the defects it catches are the plausible ones:
//
//   1. A rational arc row that is *nearly* on its circle. A quarter circle built from the wrong
//      middle weight bulges by a percent or two; on screen it looks like a fine arc.
//   2. A first derivative that is close but wrong — the quotient rule dropped a term. The surface
//      still evaluates correctly, but every normal, every tessellation crease and (in A2) every
//      numerically-integrated volume leans on dS/du and dS/dv.
//
// So the circle points are asserted exactly against the radius and the derivatives against a
// finite-difference of Evaluate itself.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "util/nurbs.hpp"

#include <cmath>
#include <vector>

using Catch::Approx;
using nurbs::Patch;
using nurbs::PatchProblem;
using ray3d::Vec3;

namespace {

constexpr double kPi = 3.14159265358979323846;

double Dist(const Vec3& a, const Vec3& b) {
  const double dx = a.x - b.x;
  const double dy = a.y - b.y;
  const double dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A flat unit patch on z = 0, corners (0,0), (2,0), (0,3), (2,3), degree 1 in both directions.
Patch UnitRuled() {
  return nurbs::RuledLinear({{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}}, {{0.0, 3.0, 0.0}, {2.0, 3.0, 0.0}});
}

}  // namespace

TEST_CASE("ValidatePatch accepts a well-formed ruled patch and names each fault") {
  const Patch good = UnitRuled();
  REQUIRE(nurbs::ValidatePatch(good) == PatchProblem::Ok);
  REQUIRE(nurbs::IsValidPatch(good));

  SECTION("degree out of range") {
    Patch p = good;
    p.degU = 0;
    REQUIRE(nurbs::ValidatePatch(p) == PatchProblem::DegreeOutOfRange);
    p.degU = 4;
    REQUIRE(nurbs::ValidatePatch(p) == PatchProblem::DegreeOutOfRange);
  }
  SECTION("control-count mismatch") {
    Patch p = good;
    p.ctrl.pop_back();
    REQUIRE(nurbs::ValidatePatch(p) == PatchProblem::ControlCountMismatch);
  }
  SECTION("knot vector wrong length") {
    Patch p = good;
    p.knotsU.push_back(1.0);
    REQUIRE(nurbs::ValidatePatch(p) == PatchProblem::KnotVectorWrongLength);
  }
  SECTION("knots step backward") {
    Patch p = good;
    p.knotsV = {0.0, 0.0, 1.0, 0.5};
    REQUIRE(nurbs::ValidatePatch(p) == PatchProblem::KnotsNotNondecreasing);
  }
  SECTION("knot vector not clamped") {
    Patch p = good;
    p.knotsV = {0.0, 0.25, 0.75, 1.0};
    REQUIRE(nurbs::ValidatePatch(p) == PatchProblem::KnotVectorNotClamped);
  }
  SECTION("degenerate knot domain - clamped at both ends but no span between them") {
    Patch p = good;
    p.knotsV = {5.0, 5.0, 5.0, 5.0};
    REQUIRE(nurbs::ValidatePatch(p) == PatchProblem::DegenerateKnotDomain);
  }
  SECTION("non-finite control point") {
    Patch p = good;
    p.ctrl[1].x = std::nan("");
    REQUIRE(nurbs::ValidatePatch(p) == PatchProblem::NonFiniteControlPoint);
  }
  SECTION("non-positive weight") {
    Patch p = good;
    p.wts[2] = 0.0;
    REQUIRE(nurbs::ValidatePatch(p) == PatchProblem::NonPositiveWeight);
    p.wts[2] = -1.0;
    REQUIRE(nurbs::ValidatePatch(p) == PatchProblem::NonPositiveWeight);
  }
}

TEST_CASE("A ruled patch is exact bilinear interpolation of its four corners") {
  const Patch p = UnitRuled();
  REQUIRE(nurbs::UMin(p) == Approx(0.0));
  REQUIRE(nurbs::UMax(p) == Approx(1.0));

  // Corners.
  REQUIRE(Dist(nurbs::Evaluate(p, 0.0, 0.0), {0.0, 0.0, 0.0}) == Approx(0.0).margin(1e-12));
  REQUIRE(Dist(nurbs::Evaluate(p, 1.0, 0.0), {2.0, 0.0, 0.0}) == Approx(0.0).margin(1e-12));
  REQUIRE(Dist(nurbs::Evaluate(p, 0.0, 1.0), {0.0, 3.0, 0.0}) == Approx(0.0).margin(1e-12));
  REQUIRE(Dist(nurbs::Evaluate(p, 1.0, 1.0), {2.0, 3.0, 0.0}) == Approx(0.0).margin(1e-12));

  // Interior: S(u, v) = (2u, 3v, 0).
  for (double u : {0.1, 0.37, 0.5, 0.83}) {
    for (double v : {0.0, 0.25, 0.6, 1.0}) {
      const Vec3 s = nurbs::Evaluate(p, u, v);
      REQUIRE(Dist(s, {2.0 * u, 3.0 * v, 0.0}) == Approx(0.0).margin(1e-12));
    }
  }

  // The normal of a flat patch is constant +/- Z.
  const nurbs::SurfacePoint sp = nurbs::EvaluateWithDerivs(p, 0.4, 0.7);
  REQUIRE(std::fabs(sp.normal.z) == Approx(1.0).margin(1e-12));
  REQUIRE(std::fabs(sp.normal.x) == Approx(0.0).margin(1e-12));
  REQUIRE(std::fabs(sp.normal.y) == Approx(0.0).margin(1e-12));
}

TEST_CASE("A ruled patch over a 3-point polyline passes through every point") {
  // An L-shaped row: (0,0) -> (4,0) -> (4,3).
  const std::vector<Vec3> row0 = {{0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}, {4.0, 3.0, 0.0}};
  const std::vector<Vec3> row1 = {{0.0, 0.0, 5.0}, {4.0, 0.0, 5.0}, {4.0, 3.0, 5.0}};
  const Patch p = nurbs::RuledLinear(row0, row1);
  REQUIRE(nurbs::IsValidPatch(p));

  // Chord parameters: total length 7, so knots at u = 0, 4/7, 1.
  REQUIRE(Dist(nurbs::Evaluate(p, 0.0, 0.0), row0[0]) == Approx(0.0).margin(1e-12));
  REQUIRE(Dist(nurbs::Evaluate(p, 4.0 / 7.0, 0.0), row0[1]) == Approx(0.0).margin(1e-9));
  REQUIRE(Dist(nurbs::Evaluate(p, 1.0, 0.0), row0[2]) == Approx(0.0).margin(1e-12));
  REQUIRE(Dist(nurbs::Evaluate(p, 4.0 / 7.0, 1.0), row1[1]) == Approx(0.0).margin(1e-9));

  // Midway in v is the average of the two rows.
  const Vec3 mid = nurbs::Evaluate(p, 4.0 / 7.0, 0.5);
  REQUIRE(Dist(mid, {4.0, 0.0, 2.5}) == Approx(0.0).margin(1e-9));
}

TEST_CASE("RationalArc lands exactly on its circle") {
  const Vec3 c{10.0, 20.0, 0.0};
  const Vec3 axis{0.0, 0.0, 1.0};
  const double r = 7.0;
  const Vec3 start{c.x + r, c.y, c.z};

  for (double sweep : {kPi / 2.0, kPi, 1.5 * kPi, 2.0 * kPi, -kPi / 2.0, 0.7}) {
    std::vector<Vec3> pts;
    std::vector<double> wts;
    nurbs::RationalArc(c, start, axis, sweep, &pts, &wts);

    const int segments =
        std::max(1, static_cast<int>(std::ceil(std::fabs(sweep) / (kPi / 2.0) - 1e-9)));
    REQUIRE(static_cast<int>(pts.size()) == 2 * segments + 1);
    REQUIRE(pts.size() == wts.size());

    // Endpoints on the circle; interior control points pulled out by 1/cos(half).
    REQUIRE(Dist(pts.front(), start) == Approx(0.0).margin(1e-12));
    for (std::size_t i = 0; i < pts.size(); i += 2) {
      REQUIRE(Dist(pts[i], c) == Approx(r).margin(1e-9));
      REQUIRE(wts[i] == Approx(1.0));
    }
    // The last endpoint is at the swept angle.
    const Vec3 want{c.x + r * std::cos(sweep), c.y + r * std::sin(sweep), c.z};
    REQUIRE(Dist(pts.back(), want) == Approx(0.0).margin(1e-9));
  }
}

TEST_CASE("An arc ribbon is a partial cylinder - every surface point is exactly on the radius") {
  // Two coaxial quarter arcs, r = 5, one at z = 0 and one at z = 8: a quarter cylinder.
  const Vec3 axis{0.0, 0.0, 1.0};
  const double r = 5.0;
  const double sweep = kPi / 2.0;
  const Patch p = nurbs::ArcRibbon({0.0, 0.0, 0.0}, {r, 0.0, 0.0}, {0.0, 0.0, 8.0}, {r, 0.0, 8.0},
                                   axis, sweep);
  REQUIRE(nurbs::IsValidPatch(p));

  for (double u = nurbs::UMin(p); u <= nurbs::UMax(p) + 1e-9; u += (nurbs::UMax(p) - nurbs::UMin(p)) / 17.0) {
    for (double v : {0.0, 0.2, 0.5, 0.9, 1.0}) {
      const Vec3 s = nurbs::Evaluate(p, u, v);
      const double radial = std::sqrt(s.x * s.x + s.y * s.y);
      REQUIRE(radial == Approx(r).margin(1e-7));
      REQUIRE(s.z == Approx(8.0 * v).margin(1e-9));
    }
  }

  // A full-turn ribbon closes on itself.
  const Patch full = nurbs::ArcRibbon({0.0, 0.0, 0.0}, {r, 0.0, 0.0}, {0.0, 0.0, 8.0},
                                      {r, 0.0, 8.0}, axis, 2.0 * kPi);
  REQUIRE(nurbs::IsValidPatch(full));
  REQUIRE(Dist(nurbs::Evaluate(full, nurbs::UMin(full), 0.3),
              nurbs::Evaluate(full, nurbs::UMax(full), 0.3)) == Approx(0.0).margin(1e-7));
}

TEST_CASE("Analytic derivatives agree with a finite difference of Evaluate") {
  // A rational patch: a quarter-cylinder ribbon, where the u derivative is the interesting one.
  const Patch p = nurbs::ArcRibbon({0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}, {0.0, 0.0, 6.0},
                                   {4.0, 0.0, 6.0}, {0.0, 0.0, 1.0}, kPi / 2.0);
  REQUIRE(nurbs::IsValidPatch(p));

  const double h = 1e-6;
  const double uLo = nurbs::UMin(p);
  const double uHi = nurbs::UMax(p);
  const double vLo = nurbs::VMin(p);
  const double vHi = nurbs::VMax(p);

  for (double u : {uLo + 0.15 * (uHi - uLo), uLo + 0.55 * (uHi - uLo), uLo + 0.9 * (uHi - uLo)}) {
    for (double v : {vLo + 0.2 * (vHi - vLo), vLo + 0.8 * (vHi - vLo)}) {
      const nurbs::SurfacePoint sp = nurbs::EvaluateWithDerivs(p, u, v);

      const Vec3 fdU = ray3d::Scale(
          ray3d::Sub(nurbs::Evaluate(p, u + h, v), nurbs::Evaluate(p, u - h, v)), 1.0 / (2.0 * h));
      const Vec3 fdV = ray3d::Scale(
          ray3d::Sub(nurbs::Evaluate(p, u, v + h), nurbs::Evaluate(p, u, v - h)), 1.0 / (2.0 * h));

      REQUIRE(Dist(sp.du, fdU) == Approx(0.0).margin(1e-4));
      REQUIRE(Dist(sp.dv, fdV) == Approx(0.0).margin(1e-4));

      // Normal is unit and radial (a cylinder's normal has no z component).
      REQUIRE(ray3d::Length(sp.normal) == Approx(1.0).margin(1e-9));
      REQUIRE(std::fabs(sp.normal.z) == Approx(0.0).margin(1e-6));
    }
  }
}

TEST_CASE("Translate moves every surface point by exactly the offset") {
  const Patch p = nurbs::ArcRibbon({0.0, 0.0, 0.0}, {3.0, 0.0, 0.0}, {0.0, 0.0, 4.0},
                                   {3.0, 0.0, 4.0}, {0.0, 0.0, 1.0}, kPi);
  const Vec3 d{2.0e6, -5.0e5, 137.0};  // state-plane magnitude
  const Patch moved = nurbs::Translate(p, d);
  REQUIRE(nurbs::IsValidPatch(moved));

  for (double u : {0.0, 0.3, 0.7, 1.0}) {
    for (double v : {0.0, 0.5, 1.0}) {
      const double uu = nurbs::UMin(p) + u * (nurbs::UMax(p) - nurbs::UMin(p));
      const Vec3 a = ray3d::Add(nurbs::Evaluate(p, uu, v), d);
      const double uuM = nurbs::UMin(moved) + u * (nurbs::UMax(moved) - nurbs::UMin(moved));
      const Vec3 b = nurbs::Evaluate(moved, uuM, v);
      REQUIRE(Dist(a, b) == Approx(0.0).margin(1e-4));
    }
  }
}
