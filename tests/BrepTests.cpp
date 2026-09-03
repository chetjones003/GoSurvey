// The B-rep solid kernel (REQ-313 / ADR-045, GitHub issue #146 — Phase 3 of #120).
//
// Everything below runs with no window, no GL context and no document, which is the property the
// requirement exists to protect. Two whole classes of defect are only catchable here:
//
//   1. A shell that is not closed, or whose faces disagree about which way an edge runs. On screen
//      that renders as a perfectly ordinary-looking solid; it goes wrong later, in a volume report
//      or a Phase 4 boolean, far from the command that built it.
//   2. A volume or surface area that is *plausible*. A sphere whose volume is off by 3% looks
//      exactly like one that is right. So every figure here is asserted against the closed form,
//      not against a previously-recorded output.
//
// The tessellation cross-check is deliberately independent: it re-derives each volume from the
// triangles by the divergence theorem, so the analytic integrals and the triangulation have to
// agree with each other as well as with the textbook.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "util/brep.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using Catch::Approx;
using brep::Problem;
using brep::Solid;
using brep::Vec3;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPiTest = 2.0 * kPi;

ucs::Ucs World() { return ucs::Ucs{}; }

ucs::Ucs At(double x, double y, double z) {
  ucs::Ucs u;
  u.origin = {x, y, z};
  return u;
}

/// A frame that is rotated in all three axes and translated to a state-plane-sized coordinate — the
/// case where a formula that quietly assumes the world frame stops agreeing with one that does not.
ucs::Ucs TiltedAt(double x, double y, double z) {
  ucs::Ucs u;
  REQUIRE(ucs::FromNormal(Vec3{x, y, z}, Vec3{0.3, -0.5, 0.81}, &u));
  REQUIRE(ucs::IsRightHandedOrthonormal(u, 1e-9));
  return u;
}

struct Counts {
  int v = 0;
  int e = 0;
  int f = 0;
};

Counts CountOf(const Solid& s) {
  return Counts{static_cast<int>(s.vertices.size()), static_cast<int>(s.edges.size()),
                static_cast<int>(s.faces.size())};
}

/// Volume re-derived from the triangles alone, by the divergence theorem. Referenced to the first
/// vertex so the arithmetic stays at model scale even when the mesh sits at easting 2e6.
double TessellatedVolume(const brep::Tessellation& t) {
  if (t.triangleCount() == 0)
    return 0.0;
  const Vec3 ref{t.vertsXyz[0], t.vertsXyz[1], t.vertsXyz[2]};
  auto at = [&](std::uint32_t i) {
    return Vec3{t.vertsXyz[3 * i] - ref.x, t.vertsXyz[3 * i + 1] - ref.y, t.vertsXyz[3 * i + 2] - ref.z};
  };
  double acc = 0.0;
  for (std::size_t i = 0; i + 2 < t.indices.size(); i += 3) {
    const Vec3 a = at(t.indices[i]);
    const Vec3 b = at(t.indices[i + 1]);
    const Vec3 c = at(t.indices[i + 2]);
    acc += ray3d::Dot(a, ray3d::Cross(b, c));
  }
  return acc / 6.0;
}

/// Surface area re-derived from the triangles alone.
double TessellatedArea(const brep::Tessellation& t) {
  auto at = [&](std::uint32_t i) {
    return Vec3{t.vertsXyz[3 * i], t.vertsXyz[3 * i + 1], t.vertsXyz[3 * i + 2]};
  };
  double acc = 0.0;
  for (std::size_t i = 0; i + 2 < t.indices.size(); i += 3) {
    const Vec3 a = at(t.indices[i]);
    const Vec3 b = at(t.indices[i + 1]);
    const Vec3 c = at(t.indices[i + 2]);
    acc += 0.5 * ray3d::Length(ray3d::Cross(ray3d::Sub(b, a), ray3d::Sub(c, a)));
  }
  return acc;
}

/// Every triangle's winding must agree with the analytic normal stored on its vertices. A face
/// tessellated inside-out still shades and still fills; it is only wrong once something culls or
/// lights it, which is much later and much harder to attribute.
void RequireWindingMatchesNormals(const brep::Tessellation& t) {
  auto pos = [&](std::uint32_t i) {
    return Vec3{t.vertsXyz[3 * i], t.vertsXyz[3 * i + 1], t.vertsXyz[3 * i + 2]};
  };
  auto nrm = [&](std::uint32_t i) {
    return Vec3{t.normalsXyz[3 * i], t.normalsXyz[3 * i + 1], t.normalsXyz[3 * i + 2]};
  };
  int checked = 0;
  for (std::size_t i = 0; i + 2 < t.indices.size(); i += 3) {
    const Vec3 a = pos(t.indices[i]);
    const Vec3 b = pos(t.indices[i + 1]);
    const Vec3 c = pos(t.indices[i + 2]);
    const Vec3 geo = ray3d::Cross(ray3d::Sub(b, a), ray3d::Sub(c, a));
    if (ray3d::Length(geo) < 1e-12)
      continue;  // a pole sliver; it has no winding to check
    const Vec3 n = nrm(t.indices[i]);
    REQUIRE(ray3d::Dot(ray3d::Normalize(geo), n) > 0.0);
    ++checked;
  }
  REQUIRE(checked > 0);
}

/// The whole tessellation must sit inside the reported bounds. `ComputeBounds` is allowed to be
/// generous and is never allowed to be tight — a box that clips geometry out of zoom extents is the
/// failure this pins.
void RequireBoundsContain(const brep::Bounds& b, const brep::Tessellation& t) {
  REQUIRE(b.valid);
  for (int i = 0; i < t.vertexCount(); ++i) {
    REQUIRE(t.vertsXyz[3 * i] >= b.mn.x - 1e-6);
    REQUIRE(t.vertsXyz[3 * i] <= b.mx.x + 1e-6);
    REQUIRE(t.vertsXyz[3 * i + 1] >= b.mn.y - 1e-6);
    REQUIRE(t.vertsXyz[3 * i + 1] <= b.mx.y + 1e-6);
    REQUIRE(t.vertsXyz[3 * i + 2] >= b.mn.z - 1e-6);
    REQUIRE(t.vertsXyz[3 * i + 2] <= b.mx.z + 1e-6);
  }
}

/// Build, validate, and confirm the analytic figures — the shape every primitive case below takes.
void RequireSolid(const Solid& s, Counts expect, int euler, double volume, double area) {
  REQUIRE(brep::Validate(s) == Problem::Ok);
  const Counts got = CountOf(s);
  REQUIRE(got.v == expect.v);
  REQUIRE(got.e == expect.e);
  REQUIRE(got.f == expect.f);
  REQUIRE(brep::EulerCharacteristic(s) == euler);

  const brep::MassProperties mp = brep::ComputeMassProperties(s);
  REQUIRE(mp.valid);
  REQUIRE(mp.volume == Approx(volume).epsilon(1e-12));
  REQUIRE(mp.surfaceArea == Approx(area).epsilon(1e-12));
}

}  // namespace

// ---------------------------------------------------------------------------
// The seven primitives: topology, then the two numbers a user actually reads.
// ---------------------------------------------------------------------------

TEST_CASE("Box is a closed solid with exact volume and area", "[brep][req313]") {
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeBox(World(), 20.0, 10.0, 8.0, &s, &why));
  REQUIRE(why == Problem::Ok);

  RequireSolid(s, Counts{8, 12, 6}, 2, 20.0 * 10.0 * 8.0,
               2.0 * (20.0 * 10.0 + 20.0 * 8.0 + 10.0 * 8.0));
  REQUIRE(s.recipe.kind == brep::PrimitiveKind::Box);
  REQUIRE(s.recipe.length == Approx(20.0));
  REQUIRE(s.shells.size() == 1);
  REQUIRE(s.shells[0].faces.size() == 6);
}

TEST_CASE("Wedge is a closed solid with exact volume and area", "[brep][req313]") {
  const double L = 12.0;
  const double W = 5.0;
  const double H = 9.0;
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeWedge(World(), L, W, H, &s, &why));

  const double area = L * W                              // base
                      + W * std::sqrt(L * L + H * H)     // the slope
                      + W * H                            // the vertical back
                      + L * H;                           // two triangular ends
  RequireSolid(s, Counts{6, 9, 5}, 2, 0.5 * L * W * H, area);
}

TEST_CASE("Pyramid on a square base is a closed solid with exact volume and area", "[brep][req313]") {
  const int n = 4;
  const double r = 6.0;
  const double h = 15.0;
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakePyramid(World(), n, r, 0.0, h, &s, &why));

  const double baseArea = 0.5 * n * r * r * std::sin(2.0 * kPi / n);
  const double side = 2.0 * r * std::sin(kPi / n);
  const double apothem = r * std::cos(kPi / n);
  const double slant = std::sqrt(h * h + apothem * apothem);
  RequireSolid(s, Counts{5, 8, 5}, 2, baseArea * h / 3.0, baseArea + 0.5 * n * side * slant);
}

TEST_CASE("Truncated pyramid is a closed solid with exact volume and area", "[brep][req313]") {
  const int n = 6;
  const double r0 = 8.0;
  const double r1 = 3.0;
  const double h = 4.0;
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakePyramid(World(), n, r0, r1, h, &s, &why));

  const double a0 = 0.5 * n * r0 * r0 * std::sin(2.0 * kPi / n);
  const double a1 = 0.5 * n * r1 * r1 * std::sin(2.0 * kPi / n);
  const double s0 = 2.0 * r0 * std::sin(kPi / n);
  const double s1 = 2.0 * r1 * std::sin(kPi / n);
  const double dApothem = (r0 - r1) * std::cos(kPi / n);
  const double slant = std::sqrt(h * h + dApothem * dApothem);
  const double lateral = n * 0.5 * (s0 + s1) * slant;
  RequireSolid(s, Counts{2 * n, 3 * n, n + 2}, 2,
               (h / 3.0) * (a0 + a1 + std::sqrt(a0 * a1)), a0 + a1 + lateral);
}

TEST_CASE("Cylinder is a closed solid with exact volume and area", "[brep][req313]") {
  const double r = 4.0;
  const double h = 25.0;
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeCylinder(World(), r, h, &s, &why));

  RequireSolid(s, Counts{4, 6, 4}, 2, kPi * r * r * h, 2.0 * kPi * r * r + 2.0 * kPi * r * h);
}

TEST_CASE("Cone with an apex is a closed solid with exact volume and area", "[brep][req313]") {
  const double r = 3.0;
  const double h = 11.0;
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeCone(World(), r, 0.0, h, &s, &why));

  RequireSolid(s, Counts{3, 4, 3}, 2, kPi * r * r * h / 3.0,
               kPi * r * r + kPi * r * std::sqrt(r * r + h * h));
}

TEST_CASE("Truncated cone is a closed solid with exact volume and area", "[brep][req313]") {
  const double r0 = 7.0;
  const double r1 = 2.5;
  const double h = 6.0;
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeCone(World(), r0, r1, h, &s, &why));

  const double slant = std::sqrt(h * h + (r0 - r1) * (r0 - r1));
  RequireSolid(s, Counts{4, 6, 4}, 2, (kPi * h / 3.0) * (r0 * r0 + r0 * r1 + r1 * r1),
               kPi * (r0 * r0 + r1 * r1) + kPi * (r0 + r1) * slant);
}

TEST_CASE("Sphere is a closed solid with exact volume and area", "[brep][req313]") {
  const double R = 5.0;
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeSphere(World(), R, &s, &why));

  RequireSolid(s, Counts{2, 2, 2}, 2, 4.0 / 3.0 * kPi * R * R * R, 4.0 * kPi * R * R);
}

TEST_CASE("Torus is a closed solid of genus one with exact volume and area", "[brep][req313]") {
  const double R = 10.0;
  const double r = 2.0;
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeTorus(World(), R, r, &s, &why));

  // Euler characteristic 0, not 2 — a torus has a hole, and reporting 2 would mean the topology
  // had quietly closed it.
  RequireSolid(s, Counts{4, 8, 4}, 0, 2.0 * kPi * kPi * R * r * r, 4.0 * kPi * kPi * R * r);
}

// ---------------------------------------------------------------------------
// Placement: the figures cannot depend on where or how the solid is oriented.
// ---------------------------------------------------------------------------

TEST_CASE("Volume and area are invariant under placement and rotation", "[brep][req313]") {
  const double r = 4.0;
  const double h = 25.0;
  const double volume = kPi * r * r * h;
  const double area = 2.0 * kPi * r * r + 2.0 * kPi * r * h;

  for (const ucs::Ucs& frame : {World(), At(100.0, -250.0, 37.5), TiltedAt(12.0, -8.0, 3.0)}) {
    Solid s;
    Problem why = Problem::Ok;
    REQUIRE(brep::MakeCylinder(frame, r, h, &s, &why));
    const brep::MassProperties mp = brep::ComputeMassProperties(s);
    REQUIRE(mp.valid);
    REQUIRE(mp.volume == Approx(volume).epsilon(1e-12));
    REQUIRE(mp.surfaceArea == Approx(area).epsilon(1e-12));
  }
}

TEST_CASE("Solids stay accurate at survey coordinate magnitudes", "[brep][req313]") {
  // A 10 ft object modelled on a state-plane grid: the case issue #120 calls out by name, and the
  // one where a formula that subtracts two world-magnitude numbers loses every digit that matters.
  const double e = 3'500'000.0;
  const double n = 12'400'000.0;
  const double z = 500.0;

  SECTION("box") {
    Solid s;
    Problem why = Problem::Ok;
    REQUIRE(brep::MakeBox(At(e, n, z), 10.0, 10.0, 10.0, &s, &why));
    const brep::MassProperties mp = brep::ComputeMassProperties(s);
    REQUIRE(mp.valid);
    // REQ-101 is +/-0.01 on a coordinate; a 1000 ft^3 volume held to 1e-6 ft^3 is far inside it.
    REQUIRE(mp.volume == Approx(1000.0).margin(1e-6));
    REQUIRE(mp.surfaceArea == Approx(600.0).margin(1e-6));
  }

  SECTION("sphere on a tilted frame") {
    Solid s;
    Problem why = Problem::Ok;
    REQUIRE(brep::MakeSphere(TiltedAt(e, n, z), 5.0, &s, &why));
    const brep::MassProperties mp = brep::ComputeMassProperties(s);
    REQUIRE(mp.valid);
    REQUIRE(mp.volume == Approx(4.0 / 3.0 * kPi * 125.0).margin(1e-6));
    REQUIRE(mp.surfaceArea == Approx(4.0 * kPi * 25.0).margin(1e-6));
  }

  SECTION("torus, whose integrals carry the most cancellation") {
    Solid s;
    Problem why = Problem::Ok;
    REQUIRE(brep::MakeTorus(At(e, n, z), 6.0, 1.5, &s, &why));
    const brep::MassProperties mp = brep::ComputeMassProperties(s);
    REQUIRE(mp.valid);
    REQUIRE(mp.volume == Approx(2.0 * kPi * kPi * 6.0 * 1.5 * 1.5).margin(1e-6));
  }
}

// ---------------------------------------------------------------------------
// Refusals. Each one is a solid a user could ask for and must not get.
// ---------------------------------------------------------------------------

TEST_CASE("Invalid dimensions are refused with a specific reason", "[brep][req313]") {
  Solid s;
  Problem why = Problem::Ok;

  REQUIRE_FALSE(brep::MakeBox(World(), 0.0, 1.0, 1.0, &s, &why));
  REQUIRE(why == Problem::NonPositiveLength);
  REQUIRE_FALSE(brep::MakeBox(World(), 1.0, -2.0, 1.0, &s, &why));
  REQUIRE(why == Problem::NonPositiveWidth);
  REQUIRE_FALSE(brep::MakeBox(World(), 1.0, 1.0, 0.0, &s, &why));
  REQUIRE(why == Problem::NonPositiveHeight);
  REQUIRE_FALSE(brep::MakeBox(World(), std::nan(""), 1.0, 1.0, &s, &why));
  REQUIRE(why == Problem::NonFiniteParameter);

  REQUIRE_FALSE(brep::MakeCylinder(World(), -1.0, 5.0, &s, &why));
  REQUIRE(why == Problem::NonPositiveRadius);

  REQUIRE_FALSE(brep::MakeCone(World(), 5.0, -1.0, 5.0, &s, &why));
  REQUIRE(why == Problem::NegativeTopRadius);
  REQUIRE_FALSE(brep::MakeCone(World(), 5.0, 5.0, 5.0, &s, &why));
  REQUIRE(why == Problem::TopRadiusNotBelowBase);

  REQUIRE_FALSE(brep::MakeSphere(World(), 0.0, &s, &why));
  REQUIRE(why == Problem::NonPositiveRadius);

  // A tube EXACTLY as fat as the ring collapses the inner equator to a point — the inner rim edges
  // have zero radius, so it is not a solid at all and is refused by name rather than left to
  // surface later as "degenerate edge".
  REQUIRE_FALSE(brep::MakeTorus(World(), 4.0, 4.0, &s, &why));
  REQUIRE(why == Problem::MinorRadiusEqualsMajor);

  REQUIRE_FALSE(brep::MakePyramid(World(), 2, 5.0, 0.0, 5.0, &s, &why));
  REQUIRE(why == Problem::SideCountOutOfRange);
  REQUIRE_FALSE(brep::MakePyramid(World(), brep::kMaxPyramidSides + 1, 5.0, 0.0, 5.0, &s, &why));
  REQUIRE(why == Problem::SideCountOutOfRange);

  // Every reason has to be sayable, or the command layer has nothing to print (REQ-201).
  REQUIRE(std::string(brep::ProblemText(Problem::MinorRadiusEqualsMajor)).size() > 0);
  REQUIRE(std::string(brep::ProblemText(Problem::NotClosed)).size() > 0);
}

TEST_CASE("Every failure reason and every primitive has its own name", "[brep][req313]") {
  // Both of these are `switch`es over an enum, and both fail the same silent way: a missing case
  // falls through to the default and every value starts reporting the same string. The command
  // layer would then refuse a torus and tell the user its length was wrong.
  const Problem all[] = {
      Problem::Ok,
      Problem::NonFiniteParameter,
      Problem::NonPositiveLength,
      Problem::NonPositiveWidth,
      Problem::NonPositiveHeight,
      Problem::NonPositiveRadius,
      Problem::NegativeTopRadius,
      Problem::TopRadiusNotBelowBase,
      Problem::MinorRadiusEqualsMajor,
      Problem::SideCountOutOfRange,
      Problem::DegenerateFrame,
      Problem::NoShell,
      Problem::EmptyShell,
      Problem::IndexOutOfRange,
      Problem::LoopNotClosed,
      Problem::EmptyLoop,
      Problem::EdgeNotUsedTwice,
      Problem::EdgeOrientationInconsistent,
      Problem::FaceHasNoLoop,
      Problem::DegenerateFace,
      Problem::DegenerateEdge,
      Problem::NonFiniteCoordinate,
      Problem::NotClosed,
      Problem::UnusedVertex,
      Problem::PlaneFaceNotSimple,
      Problem::NonPositiveTolerance,
  };
  std::vector<std::string> seen;
  for (Problem p : all) {
    const std::string text = brep::ProblemText(p);
    INFO(text);
    REQUIRE_FALSE(text.empty());
    // The distinctness check alone would not catch a *single* missing case, because the first
    // value to fall through picks up the default sentence and nothing has claimed it yet. Naming
    // the sentinel closes that: no enumerated value is allowed to reach the fallthrough.
    REQUIRE(text != "The solid is not valid.");
    REQUIRE(std::find(seen.begin(), seen.end(), text) == seen.end());
    seen.push_back(text);
  }

  const brep::PrimitiveKind kinds[] = {
      brep::PrimitiveKind::None,     brep::PrimitiveKind::Box,      brep::PrimitiveKind::Wedge,
      brep::PrimitiveKind::Pyramid,  brep::PrimitiveKind::Cylinder, brep::PrimitiveKind::Cone,
      brep::PrimitiveKind::Sphere,   brep::PrimitiveKind::Torus,
  };
  std::vector<std::string> names;
  for (brep::PrimitiveKind k : kinds) {
    const std::string name = brep::PrimitiveKindName(k);
    INFO(name);
    REQUIRE_FALSE(name.empty());
    REQUIRE(std::find(names.begin(), names.end(), name) == names.end());
    names.push_back(name);
  }

  // And the name a built solid reports is the one for the kind it actually is.
  Solid built;
  Problem builtWhy = Problem::Ok;
  REQUIRE(brep::MakeTorus(World(), 5.0, 1.0, &built, &builtWhy));
  REQUIRE(std::string(brep::PrimitiveKindName(built.recipe.kind)) == "Torus");
}

TEST_CASE("A skewed or mirrored placement frame is refused", "[brep][req313]") {
  ucs::Ucs mirrored;
  mirrored.xAxis = {1.0, 0.0, 0.0};
  mirrored.yAxis = {0.0, 1.0, 0.0};
  mirrored.zAxis = {0.0, 0.0, -1.0};  // left-handed: X cross Y is +Z, not -Z

  Solid s;
  Problem why = Problem::Ok;
  REQUIRE_FALSE(brep::MakeBox(mirrored, 1.0, 1.0, 1.0, &s, &why));
  REQUIRE(why == Problem::DegenerateFrame);

  ucs::Ucs skewed;
  skewed.yAxis = {0.5, 0.5, 0.0};  // not unit, not perpendicular
  REQUIRE_FALSE(brep::MakeSphere(skewed, 1.0, &s, &why));
  REQUIRE(why == Problem::DegenerateFrame);
}

// ---------------------------------------------------------------------------
// Validity: what a deliberately broken solid must be caught doing.
// ---------------------------------------------------------------------------

TEST_CASE("Validate rejects broken topology", "[brep][req313]") {
  Solid good;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeBox(World(), 4.0, 3.0, 2.0, &good, &why));
  REQUIRE(brep::IsValid(good));

  SECTION("a missing face leaves edges bounding only one face") {
    Solid s = good;
    s.faces.pop_back();
    s.shells[0].faces.pop_back();
    REQUIRE(brep::Validate(s) == Problem::EdgeNotUsedTwice);
  }

  SECTION("two faces agreeing on an edge's direction is not orientable") {
    // Reverse one face's boundary but leave its normal alone. The ring still closes — so this is
    // not caught by the cheaper closure check — but every edge it touches is now used twice in the
    // same direction, which is the definition of a shell that cannot be consistently oriented.
    Solid s = good;
    brep::Loop& lp = s.faces[0].loops[0];
    for (brep::EdgeUse& u : lp.uses)
      u.reversed = !u.reversed;
    std::reverse(lp.uses.begin(), lp.uses.end());
    REQUIRE(brep::Validate(s) == Problem::EdgeOrientationInconsistent);
  }

  SECTION("a boundary that does not close") {
    Solid s = good;
    s.faces[0].loops[0].uses.pop_back();
    REQUIRE(brep::Validate(s) == Problem::LoopNotClosed);
  }

  SECTION("a face pointing the wrong way turns the volume negative") {
    // Reverse every face's outward normal and its loop winding: the topology stays manifold and
    // orientable, but the shell now describes the outside of the universe rather than a solid.
    Solid s = good;
    for (brep::Face& f : s.faces) {
      f.surface.frame.zAxis = ray3d::Scale(f.surface.frame.zAxis, -1.0);
      f.surface.frame.xAxis = ray3d::Scale(f.surface.frame.xAxis, -1.0);
      for (brep::Loop& lp : f.loops) {
        for (brep::EdgeUse& u : lp.uses)
          u.reversed = !u.reversed;
        std::reverse(lp.uses.begin(), lp.uses.end());
      }
    }
    REQUIRE(brep::Validate(s) == Problem::NotClosed);
  }

  SECTION("a curved face whose span disagrees with its boundary") {
    // The case the topological checks cannot see: every edge still bounds exactly two faces, every
    // loop still closes, and the shell is still orientable — but one cylinder face now claims a
    // quarter turn while its boundary runs a half turn, so the surface has a hole in it. Caught
    // only by the geometric closure test, and it is exactly the shape a Phase 4 boolean could
    // produce by trimming a face without re-cutting its loop.
    Solid cyl;
    Problem cylWhy = Problem::Ok;
    REQUIRE(brep::MakeCylinder(World(), 3.0, 7.0, &cyl, &cylWhy));
    REQUIRE(brep::IsValid(cyl));

    bool narrowed = false;
    for (brep::Face& f : cyl.faces) {
      if (f.surface.kind == brep::SurfaceKind::Cylinder && !narrowed) {
        f.uEnd = f.uStart + (f.uEnd - f.uStart) * 0.5;
        narrowed = true;
      }
    }
    REQUIRE(narrowed);
    REQUIRE(brep::Validate(cyl) == Problem::NotClosed);
  }

  SECTION("a non-finite coordinate") {
    Solid s = good;
    s.vertices[0].p.x = std::nan("");
    REQUIRE(brep::Validate(s) == Problem::NonFiniteCoordinate);
  }

  SECTION("a solid with no shell is not a solid") {
    Solid s = good;
    s.shells.clear();
    REQUIRE(brep::Validate(s) == Problem::NoShell);
  }

  SECTION("mass properties refuse an invalid solid rather than reporting a number") {
    Solid s = good;
    s.faces.pop_back();
    s.shells[0].faces.pop_back();
    const brep::MassProperties mp = brep::ComputeMassProperties(s);
    REQUIRE_FALSE(mp.valid);
    REQUIRE(mp.volume == 0.0);
  }
}

// ---------------------------------------------------------------------------
// Edges: one parametrisation, and it starts and ends where the topology says.
// ---------------------------------------------------------------------------

TEST_CASE("Every edge runs from its start vertex to its end vertex", "[brep][req313]") {
  for (int which = 0; which < 3; ++which) {
    Solid s;
    Problem why = Problem::Ok;
    if (which == 0)
      REQUIRE(brep::MakeCylinder(TiltedAt(1000.0, 2000.0, 30.0), 4.0, 9.0, &s, &why));
    else if (which == 1)
      REQUIRE(brep::MakeSphere(TiltedAt(1000.0, 2000.0, 30.0), 6.0, &s, &why));
    else
      REQUIRE(brep::MakeTorus(TiltedAt(1000.0, 2000.0, 30.0), 8.0, 2.0, &s, &why));

    for (const brep::Edge& e : s.edges) {
      const Vec3 a = brep::EdgePointAt(s, e, 0.0);
      const Vec3 b = brep::EdgePointAt(s, e, 1.0);
      REQUIRE(ray3d::Length(ray3d::Sub(a, s.vertices[e.v0].p)) == Approx(0.0).margin(1e-9));
      REQUIRE(ray3d::Length(ray3d::Sub(b, s.vertices[e.v1].p)) == Approx(0.0).margin(1e-9));
    }
  }
}

// ---------------------------------------------------------------------------
// Tessellation: derived, cross-checking, and never allowed to change the solid.
// ---------------------------------------------------------------------------

TEST_CASE("Tessellation agrees with the analytic figures and winds outward", "[brep][req313]") {
  struct Case {
    const char* name;
    Solid s;
  };
  std::vector<Case> cases;
  Problem why = Problem::Ok;

  {
    Solid s;
    REQUIRE(brep::MakeBox(World(), 20.0, 10.0, 8.0, &s, &why));
    cases.push_back({"box", s});
  }
  {
    Solid s;
    REQUIRE(brep::MakeWedge(World(), 12.0, 5.0, 9.0, &s, &why));
    cases.push_back({"wedge", s});
  }
  {
    Solid s;
    REQUIRE(brep::MakePyramid(World(), 5, 6.0, 0.0, 15.0, &s, &why));
    cases.push_back({"pyramid", s});
  }
  {
    Solid s;
    REQUIRE(brep::MakePyramid(World(), 6, 8.0, 3.0, 4.0, &s, &why));
    cases.push_back({"pyramid frustum", s});
  }
  {
    Solid s;
    REQUIRE(brep::MakeCylinder(World(), 4.0, 25.0, &s, &why));
    cases.push_back({"cylinder", s});
  }
  {
    Solid s;
    REQUIRE(brep::MakeCone(World(), 3.0, 0.0, 11.0, &s, &why));
    cases.push_back({"cone", s});
  }
  {
    Solid s;
    REQUIRE(brep::MakeCone(World(), 7.0, 2.5, 6.0, &s, &why));
    cases.push_back({"cone frustum", s});
  }
  {
    Solid s;
    REQUIRE(brep::MakeSphere(World(), 5.0, &s, &why));
    cases.push_back({"sphere", s});
  }
  {
    Solid s;
    REQUIRE(brep::MakeTorus(World(), 10.0, 2.0, &s, &why));
    cases.push_back({"torus", s});
  }

  for (const Case& c : cases) {
    INFO(c.name);
    const brep::MassProperties mp = brep::ComputeMassProperties(c.s);
    REQUIRE(mp.valid);

    brep::Tessellation t;
    REQUIRE(brep::Tessellate(c.s, 0.001, &t, &why));
    REQUIRE(t.triangleCount() > 0);
    REQUIRE(t.vertsXyz.size() == t.normalsXyz.size());

    RequireWindingMatchesNormals(t);
    RequireBoundsContain(brep::ComputeBounds(c.s), t);

    // An inscribed triangulation always understates a convex curved surface, so the tolerance is
    // one-sided in spirit; 0.5% at a 0.001 chord tolerance is loose enough not to be brittle and
    // tight enough that a wrong analytic formula cannot hide behind it.
    REQUIRE(TessellatedVolume(t) == Approx(mp.volume).epsilon(0.005));
    REQUIRE(TessellatedArea(t) == Approx(mp.surfaceArea).epsilon(0.005));
  }
}

TEST_CASE("Tessellation quality does not change the solid", "[brep][req313]") {
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeSphere(World(), 5.0, &s, &why));
  const brep::MassProperties before = brep::ComputeMassProperties(s);

  brep::Tessellation coarse;
  brep::Tessellation fine;
  REQUIRE(brep::Tessellate(s, 0.5, &coarse, &why));
  REQUIRE(brep::Tessellate(s, 0.0005, &fine, &why));
  REQUIRE(fine.triangleCount() > coarse.triangleCount());

  const brep::MassProperties after = brep::ComputeMassProperties(s);
  REQUIRE(after.volume == before.volume);
  REQUIRE(after.surfaceArea == before.surfaceArea);

  // The finer mesh must be the closer one, or "quality" would not mean anything.
  const double coarseErr = std::fabs(TessellatedVolume(coarse) - before.volume);
  const double fineErr = std::fabs(TessellatedVolume(fine) - before.volume);
  REQUIRE(fineErr < coarseErr);
}

// ---------------------------------------------------------------------------
// Closest-point queries — what object snapping is built on. The failure these
// pin is not a crash: it is a snap that lands on the CHORD instead of on the
// surface, wrong by the sagitta, plausible on screen, and smaller every time
// the user zooms in to check it.
// ---------------------------------------------------------------------------

TEST_CASE("ClosestPointOnSurface lands exactly on the surface", "[brep][req313]") {
  Problem why = Problem::Ok;

  SECTION("cylinder") {
    Solid s;
    REQUIRE(brep::MakeCylinder(World(), 4.0, 20.0, &s, &why));
    const brep::Surface& side = [&]() -> const brep::Surface& {
      for (const brep::Face& f : s.faces)
        if (f.surface.kind == brep::SurfaceKind::Cylinder)
          return f.surface;
      return s.faces[0].surface;
    }();
    // A point well outside the cylinder comes back on the wall: same height, radius exactly 4.
    const Vec3 got = brep::ClosestPointOnSurface(side, Vec3{30.0, 40.0, 7.5});
    REQUIRE(std::sqrt(got.x * got.x + got.y * got.y) == Approx(4.0).epsilon(1e-12));
    REQUIRE(got.z == Approx(7.5).margin(1e-12));
    // A point on the axis has no nearest point; it must come back unchanged rather than as a NaN
    // or as an arbitrary direction (REQ-201).
    const Vec3 axis = brep::ClosestPointOnSurface(side, Vec3{0.0, 0.0, 3.0});
    REQUIRE(axis.x == Approx(0.0).margin(1e-12));
    REQUIRE(axis.z == Approx(3.0).margin(1e-12));
  }

  SECTION("sphere on a tilted frame at survey magnitude") {
    const ucs::Ucs frame = TiltedAt(3'500'000.0, 12'400'000.0, 500.0);
    Solid s;
    REQUIRE(brep::MakeSphere(frame, 5.0, &s, &why));
    const brep::Surface& sf = s.faces[0].surface;
    const Vec3 probe = ray3d::Add(frame.origin, Vec3{100.0, -40.0, 60.0});
    const Vec3 got = brep::ClosestPointOnSurface(sf, probe);
    REQUIRE(ray3d::Length(ray3d::Sub(got, frame.origin)) == Approx(5.0).margin(1e-6));
  }

  SECTION("cone — the taper is respected, not treated as a cylinder") {
    Solid s;
    REQUIRE(brep::MakeCone(World(), 10.0, 2.0, 8.0, &s, &why));
    const brep::Surface& side = [&]() -> const brep::Surface& {
      for (const brep::Face& f : s.faces)
        if (f.surface.kind == brep::SurfaceKind::Cone)
          return f.surface;
      return s.faces[0].surface;
    }();
    // Straight out from the mid-height point: the radius there is (10+2)/2 = 6.
    const Vec3 got = brep::ClosestPointOnSurface(side, Vec3{50.0, 0.0, 4.0});
    // The nearest point on a slanted wall is not at the same z as the probe, so the check is that
    // the point is ON the cone: its radius matches the cone's radius at its own height.
    const double rho = std::sqrt(got.x * got.x + got.y * got.y);
    const double expected = 10.0 + (2.0 - 10.0) * (got.z / 8.0);
    REQUIRE(rho == Approx(expected).margin(1e-9));
  }

  SECTION("torus") {
    Solid s;
    REQUIRE(brep::MakeTorus(World(), 10.0, 3.0, &s, &why));
    const brep::Surface& sf = s.faces[0].surface;
    const Vec3 got = brep::ClosestPointOnSurface(sf, Vec3{40.0, 0.0, 0.0});
    // On the tube: distance from the tube's centre circle is exactly the minor radius.
    const double rho = std::sqrt(got.x * got.x + got.y * got.y);
    const double dRing = std::sqrt((rho - 10.0) * (rho - 10.0) + got.z * got.z);
    REQUIRE(dRing == Approx(3.0).margin(1e-9));
  }
}

TEST_CASE("ClosestPointOnEdge stays on the edge, not on the line behind it", "[brep][req313]") {
  Problem why = Problem::Ok;
  Solid box;
  REQUIRE(brep::MakeBox(World(), 10.0, 10.0, 10.0, &box, &why));

  // A point far beyond a line edge's end clamps to that end — the whole reason this is not just a
  // projection onto the infinite line.
  for (const brep::Edge& e : box.edges) {
    const Vec3 a = box.vertices[e.v0].p;
    const Vec3 b = box.vertices[e.v1].p;
    const Vec3 beyond = ray3d::Add(b, ray3d::Scale(ray3d::Sub(b, a), 5.0));
    const Vec3 got = brep::ClosestPointOnEdge(box, e, beyond);
    REQUIRE(ray3d::Length(ray3d::Sub(got, b)) == Approx(0.0).margin(1e-9));
  }

  Solid cyl;
  REQUIRE(brep::MakeCylinder(World(), 6.0, 10.0, &cyl, &why));
  for (const brep::Edge& e : cyl.edges) {
    if (e.kind != brep::CurveKind::Arc)
      continue;
    // Every answer is ON the arc's circle, at the arc's own radius from its own centre.
    const Vec3 got = brep::ClosestPointOnEdge(cyl, e, Vec3{100.0, 55.0, -20.0});
    const Vec3 rel = ray3d::Sub(got, e.frame.origin);
    REQUIRE(ray3d::Length(rel) == Approx(e.radius).margin(1e-9));
    REQUIRE(ray3d::Dot(rel, e.frame.zAxis) == Approx(0.0).margin(1e-9));
    // And it is within the SWEPT half, not on the other half of the circle: the point nearest a
    // probe outside the far half would otherwise come back there, which is the clamp's whole job.
    const ucs::Point2D flat = ucs::WorldToPlane(e.frame, got);
    const double angle = std::atan2(flat.y, flat.x);
    const double lo = std::min(0.0, e.sweep) - 1e-9;
    const double hi = std::max(0.0, e.sweep) + 1e-9;
    REQUIRE(angle >= lo);
    REQUIRE(angle <= hi);
  }
}

TEST_CASE("A probe outside an arc gets the NEARER end, not the smaller angle", "[brep][req313]") {
  // The case a review found, and the reason "is the answer on the arc?" is not a sufficient test.
  //
  // A half-arc runs from angle 0 to pi. A probe at -2.0 rad is 2.0 rad from the start and only
  // 1.14 rad from the end, so the end is the nearest point on that arc. Clamping the raw `atan2`
  // value picks the START instead — because -2.0 is the smaller number — and the result is still
  // ON the arc, which is exactly why it went unnoticed.
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeCylinder(World(), 10.0, 5.0, &s, &why));

  const brep::Edge* rim = nullptr;
  for (const brep::Edge& e : s.edges) {
    if (e.kind != brep::CurveKind::Arc)
      continue;
    // The bottom rim half that runs (10,0,0) -> (-10,0,0) counter-clockwise, through +Y.
    if (s.vertices[e.v0].p.x > 9.0 && s.vertices[e.v1].p.x < -9.0 &&
        std::fabs(s.vertices[e.v0].p.z) < 1e-9) {
      rim = &e;
      break;
    }
  }
  REQUIRE(rim != nullptr);

  auto probeAt = [&](double angleRad) {
    return brep::ClosestPointOnEdge(s, *rim, Vec3{30.0 * std::cos(angleRad), 30.0 * std::sin(angleRad), 0.0});
  };

  // -2.0 rad: nearer to the pi end.
  REQUIRE(probeAt(-2.0).x == Approx(-10.0).margin(1e-9));
  REQUIRE(probeAt(-2.0).y == Approx(0.0).margin(1e-9));
  // -0.5 rad: nearer to the 0 end.
  REQUIRE(probeAt(-0.5).x == Approx(10.0).margin(1e-9));
  // Just inside either end stays inside, and the midpoint of the sweep is returned exactly.
  REQUIRE(probeAt(0.1).y > 0.0);
  REQUIRE(probeAt(kPi * 0.5).x == Approx(0.0).margin(1e-9));
  REQUIRE(probeAt(kPi * 0.5).y == Approx(10.0).margin(1e-9));

  // The two halves of a rim tile the whole circle, so for EVERY direction at least one of them
  // returns the exact point rather than an end. That is what masked the defect in the snap path,
  // and it is worth pinning so the masking is a stated property rather than a lucky one.
  for (int i = 0; i < 72; ++i) {
    const double a = -kPi + (kTwoPiTest * i) / 72.0;
    const Vec3 target{10.0 * std::cos(a), 10.0 * std::sin(a), 0.0};
    double best = 1e300;
    for (const brep::Edge& e : s.edges) {
      if (e.kind != brep::CurveKind::Arc || std::fabs(s.vertices[e.v0].p.z) > 1e-9)
        continue;
      const Vec3 got = brep::ClosestPointOnEdge(s, e, ray3d::Scale(target, 3.0));
      best = std::min(best, ray3d::Length(ray3d::Sub(got, target)));
    }
    INFO("direction " << a);
    REQUIRE(best == Approx(0.0).margin(1e-9));
  }
}

TEST_CASE("Every triangle knows which face it came from", "[brep][req313]") {
  // Without this the face snap could find the right triangle and then project onto the wrong
  // surface — a point exactly on a face the user was not pointing at.
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeCylinder(World(), 4.0, 12.0, &s, &why));

  brep::Tessellation t;
  REQUIRE(brep::Tessellate(s, 0.01, &t, &why));
  REQUIRE(t.triFace.size() == static_cast<size_t>(t.triangleCount()));

  std::vector<int> seen(s.faces.size(), 0);
  for (size_t i = 0; i < t.triFace.size(); ++i) {
    const int f = t.triFace[i];
    REQUIRE(f >= 0);
    REQUIRE(f < static_cast<int>(s.faces.size()));
    seen[static_cast<size_t>(f)] = 1;
    // Every vertex of the triangle must lie on the surface its face claims — which is the property
    // the snap projection depends on and the one a mismatched id would break.
    for (int k = 0; k < 3; ++k) {
      const std::uint32_t vi = t.indices[i * 3 + static_cast<size_t>(k)];
      const Vec3 p{t.vertsXyz[vi * 3], t.vertsXyz[vi * 3 + 1], t.vertsXyz[vi * 3 + 2]};
      const Vec3 on = brep::ClosestPointOnSurface(s.faces[static_cast<size_t>(f)].surface, p);
      REQUIRE(ray3d::Length(ray3d::Sub(on, p)) == Approx(0.0).margin(1e-9));
    }
  }
  for (size_t f = 0; f < seen.size(); ++f) {
    INFO("face " << f);
    REQUIRE(seen[f] == 1);  // every face contributes triangles; none is silently dropped
  }
}

TEST_CASE("Edge tessellation follows the same chord rule as the faces", "[brep][req313]") {
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeCylinder(World(), 5.0, 9.0, &s, &why));

  std::vector<double> coarse;
  std::vector<double> fine;
  REQUIRE(brep::TessellateEdges(s, 0.5, &coarse, &why));
  REQUIRE(brep::TessellateEdges(s, 0.001, &fine, &why));
  REQUIRE(coarse.size() % 6 == 0);
  REQUIRE(fine.size() > coarse.size());

  // Every emitted point is on the solid: each segment endpoint lies on one of its edges. Checked
  // against the arcs' own radius, because a wireframe that floats off the shading it outlines is
  // exactly what a divergent chord rule looks like.
  for (size_t i = 0; i + 5 < fine.size(); i += 6) {
    const Vec3 a{fine[i], fine[i + 1], fine[i + 2]};
    double best = 1e300;
    for (const brep::Edge& e : s.edges)
      best = std::min(best, ray3d::Length(ray3d::Sub(brep::ClosestPointOnEdge(s, e, a), a)));
    REQUIRE(best == Approx(0.0).margin(1e-6));
  }

  REQUIRE_FALSE(brep::TessellateEdges(s, 0.0, &fine, &why));
  REQUIRE(why == Problem::NonPositiveTolerance);

  Solid broken = s;
  broken.faces.pop_back();
  broken.shells[0].faces.pop_back();
  REQUIRE_FALSE(brep::TessellateEdges(broken, 0.01, &fine, &why));
  REQUIRE(why == Problem::EdgeNotUsedTwice);
}

TEST_CASE("Tessellation refuses a bad tolerance and a bad solid", "[brep][req313]") {
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeBox(World(), 2.0, 2.0, 2.0, &s, &why));

  brep::Tessellation t;
  REQUIRE_FALSE(brep::Tessellate(s, 0.0, &t, &why));
  REQUIRE(why == Problem::NonPositiveTolerance);
  REQUIRE_FALSE(brep::Tessellate(s, -1.0, &t, &why));
  REQUIRE(why == Problem::NonPositiveTolerance);

  Solid broken = s;
  broken.faces.pop_back();
  broken.shells[0].faces.pop_back();
  REQUIRE_FALSE(brep::Tessellate(broken, 0.01, &t, &why));
  REQUIRE(why == Problem::EdgeNotUsedTwice);
}

TEST_CASE("A torus whose tube exceeds its ring is built, and reports no volume", "[brep][req313]") {
  // AutoCAD builds this and users draw it on purpose (ADR-045 (f) as amended): the tube grows
  // through the centre and the surface passes through itself.
  Solid s;
  Problem why = Problem::Ok;
  REQUIRE(brep::MakeTorus(World(), 4.0, 9.0, &s, &why));
  REQUIRE(why == Problem::Ok);

  // The TOPOLOGY is perfectly sound — manifold, orientable, closed — which is why it draws.
  REQUIRE(brep::Validate(s) == Problem::Ok);
  REQUIRE(brep::EulerCharacteristic(s) == 0);
  brep::Tessellation t;
  REQUIRE(brep::Tessellate(s, 0.01, &t, &why));
  REQUIRE(t.triangleCount() > 0);

  // What it is not is a body with a meaningful volume: the surface encloses part of space twice, so
  // `2 pi^2 R r^2` is a number rather than an answer. Reported as unavailable, never as that number.
  REQUIRE(brep::SelfIntersects(s));
  const brep::MassProperties mp = brep::ComputeMassProperties(s);
  REQUIRE_FALSE(mp.valid);
  REQUIRE(mp.volume == 0.0);
  REQUIRE(mp.surfaceArea == 0.0);

  // An ordinary torus is unaffected: still valid, still reports both figures.
  Solid ok;
  REQUIRE(brep::MakeTorus(World(), 10.0, 2.0, &ok, &why));
  REQUIRE_FALSE(brep::SelfIntersects(ok));
  const brep::MassProperties okMp = brep::ComputeMassProperties(ok);
  REQUIRE(okMp.valid);
  REQUIRE(okMp.volume == Approx(2.0 * kPi * kPi * 10.0 * 4.0).epsilon(1e-12));
}

TEST_CASE("Isolines make a curved face read as curved", "[brep][req313]") {
  // A solid's EDGES alone are a poor picture of it: a cylinder's are two rims and two seams, which
  // draws as two circles joined by two lines. These are the extra curves every CAD package adds,
  // and the counts below are what AutoCAD's ISOLINES = 4 produces.
  Problem why = Problem::Ok;

  // How many distinct iso-curves a buffer holds, counted by their start points — each curve is
  // emitted as a run of segments, so counting segments would count tessellation instead.
  auto curveCount = [](const std::vector<double>& segs) {
    int runs = 0;
    for (std::size_t i = 0; i + 5 < segs.size(); i += 6) {
      const bool continues = i >= 6 && std::fabs(segs[i] - segs[i - 3]) < 1e-9 &&
                             std::fabs(segs[i + 1] - segs[i - 2]) < 1e-9 &&
                             std::fabs(segs[i + 2] - segs[i - 1]) < 1e-9;
      if (!continues)
        ++runs;
    }
    return runs;
  };

  SECTION("a cylinder gets four rulings and no rings") {
    Solid s;
    REQUIRE(brep::MakeCylinder(World(), 5.0, 10.0, &s, &why));
    std::vector<double> iso;
    REQUIRE(brep::TessellateIsolines(s, 4, 0.01, &iso, &why));
    // Four lines around the turn. The two at 0 and pi land on the seams and are excluded, so the
    // grid contributes the two at pi/2 and 3pi/2 — one inside each half-face — plus the seams which
    // are already real edges. Four vertical lines on screen, which is what AutoCAD shows.
    REQUIRE(curveCount(iso) == 2);
    // Every ruling is a single straight segment: the surface is ruled, so a chord is exact.
    REQUIRE(iso.size() == 2 * 6);
    // And they are ON the cylinder — at the radius, spanning the full height.
    for (std::size_t i = 0; i + 5 < iso.size(); i += 6) {
      REQUIRE(std::sqrt(iso[i] * iso[i] + iso[i + 1] * iso[i + 1]) == Approx(5.0).margin(1e-9));
      REQUIRE(std::fabs(iso[i + 5] - iso[i + 2]) == Approx(10.0).margin(1e-9));
    }
  }

  SECTION("a sphere gets meridians AND latitude circles") {
    Solid s;
    REQUIRE(brep::MakeSphere(World(), 5.0, &s, &why));
    std::vector<double> iso;
    REQUIRE(brep::TessellateIsolines(s, 4, 0.01, &iso, &why));
    // Two meridians from the global grid (the other two are the seams), plus two latitude circles
    // per half — a net rather than a lens.
    REQUIRE(curveCount(iso) == 6);
    // Every point is on the sphere.
    for (std::size_t i = 0; i + 2 < iso.size(); i += 3)
      REQUIRE(std::sqrt(iso[i] * iso[i] + iso[i + 1] * iso[i + 1] + iso[i + 2] * iso[i + 2]) ==
              Approx(5.0).margin(1e-6));
  }

  SECTION("a torus gets tube circles and ring circles") {
    Solid s;
    REQUIRE(brep::MakeTorus(World(), 10.0, 2.0, &s, &why));
    std::vector<double> iso;
    REQUIRE(brep::TessellateIsolines(s, 4, 0.01, &iso, &why));
    REQUIRE(curveCount(iso) > 0);
    // Every point is on the tube: its distance from the ring's centre circle is the minor radius.
    for (std::size_t i = 0; i + 2 < iso.size(); i += 3) {
      const double rho = std::sqrt(iso[i] * iso[i] + iso[i + 1] * iso[i + 1]);
      const double dRing = std::sqrt((rho - 10.0) * (rho - 10.0) + iso[i + 2] * iso[i + 2]);
      REQUIRE(dRing == Approx(2.0).margin(1e-6));
    }
  }

  SECTION("a box gets none — it is flat, and its edges already say everything") {
    Solid s;
    REQUIRE(brep::MakeBox(World(), 4.0, 4.0, 4.0, &s, &why));
    std::vector<double> iso;
    REQUIRE(brep::TessellateIsolines(s, 4, 0.01, &iso, &why));
    REQUIRE(iso.empty());
  }

  SECTION("zero is a legal setting and means edges only") {
    Solid s;
    REQUIRE(brep::MakeCylinder(World(), 5.0, 10.0, &s, &why));
    std::vector<double> iso;
    REQUIRE(brep::TessellateIsolines(s, 0, 0.01, &iso, &why));
    REQUIRE(iso.empty());
  }

  SECTION("more isolines means more curves, and never one on a seam") {
    Solid s;
    REQUIRE(brep::MakeCylinder(World(), 5.0, 10.0, &s, &why));
    std::vector<double> four;
    std::vector<double> sixteen;
    REQUIRE(brep::TessellateIsolines(s, 4, 0.01, &four, &why));
    REQUIRE(brep::TessellateIsolines(s, 16, 0.01, &sixteen, &why));
    REQUIRE(curveCount(sixteen) > curveCount(four));
    // A ruling exactly on a seam would double an edge that is already drawn. The seams are at
    // angle 0 and pi, so no isoline may sit at either.
    for (std::size_t i = 0; i + 1 < sixteen.size(); i += 6) {
      const double a = std::atan2(sixteen[i + 1], sixteen[i]);
      REQUIRE(std::fabs(a) > 1e-6);
      REQUIRE(std::fabs(std::fabs(a) - kPi) > 1e-6);
    }
  }

  SECTION("refuses a bad tolerance and an invalid solid, like the other tessellators") {
    Solid s;
    REQUIRE(brep::MakeCylinder(World(), 5.0, 10.0, &s, &why));
    std::vector<double> iso;
    REQUIRE_FALSE(brep::TessellateIsolines(s, 4, 0.0, &iso, &why));
    REQUIRE(why == Problem::NonPositiveTolerance);
    Solid broken = s;
    broken.faces.pop_back();
    broken.shells[0].faces.pop_back();
    REQUIRE_FALSE(brep::TessellateIsolines(broken, 4, 0.01, &iso, &why));
    REQUIRE(why == Problem::EdgeNotUsedTwice);
  }
}

// ---------------------------------------------------------------------------
// Feature operations — Extrude (REQ-314 / ADR-046 increment 1, GitHub issue #147).
// ---------------------------------------------------------------------------

namespace {

/// A straight-edged profile from 2D points in \p plane's own coordinates.
brep::Profile PolyProfile(const ucs::Ucs& plane, const std::vector<ucs::Point2D>& pts2) {
  brep::Profile pr;
  pr.plane = plane;
  for (const ucs::Point2D& q : pts2)
    pr.vertices.push_back(ucs::PlaneToWorld(plane, q));
  pr.edges.assign(pts2.size(), brep::ProfileEdge{});
  return pr;
}

/// A full circle expressed the way the cylinder builder expresses its rims: two opposite points,
/// two half-turn arcs.
brep::Profile CircleProfile(const ucs::Ucs& plane, double r) {
  brep::Profile pr;
  pr.plane = plane;
  pr.vertices = {ucs::PlaneToWorld(plane, {r, 0.0}), ucs::PlaneToWorld(plane, {-r, 0.0})};
  brep::ProfileEdge e;
  e.arc = true;
  e.centre = plane.origin;
  e.sweep = kPi;
  pr.edges = {e, e};
  return pr;
}

}  // namespace

TEST_CASE("Extrude of a rectangle is the box the primitive builder makes", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double w = 8.0, d = 5.0, h = 3.0;
  Solid ex;
  REQUIRE(brep::Extrude(PolyProfile(World(), {{-w / 2, -d / 2}, {w / 2, -d / 2}, {w / 2, d / 2}, {-w / 2, d / 2}}),
                        h, &ex, &why));
  Solid box;
  REQUIRE(brep::MakeBox(World(), w, d, h, &box, &why));

  REQUIRE(CountOf(ex).v == CountOf(box).v);
  REQUIRE(CountOf(ex).e == CountOf(box).e);
  REQUIRE(CountOf(ex).f == CountOf(box).f);
  REQUIRE(brep::EulerCharacteristic(ex) == 2);

  const brep::MassProperties me = brep::ComputeMassProperties(ex);
  const brep::MassProperties mb = brep::ComputeMassProperties(box);
  REQUIRE(me.valid);
  REQUIRE(me.volume == Approx(mb.volume).epsilon(1e-9));
  REQUIRE(me.surfaceArea == Approx(mb.surfaceArea).epsilon(1e-9));
  REQUIRE(me.volume == Approx(w * d * h).epsilon(1e-9));
}

TEST_CASE("Extrude of a full circle is the cylinder the primitive builder makes", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double r = 4.0, h = 9.0;
  Solid ex;
  REQUIRE(brep::Extrude(CircleProfile(World(), r), h, &ex, &why));
  Solid cyl;
  REQUIRE(brep::MakeCylinder(World(), r, h, &cyl, &why));

  REQUIRE(CountOf(ex).v == CountOf(cyl).v);
  REQUIRE(CountOf(ex).e == CountOf(cyl).e);
  REQUIRE(CountOf(ex).f == CountOf(cyl).f);

  const brep::MassProperties me = brep::ComputeMassProperties(ex);
  REQUIRE(me.valid);
  REQUIRE(me.volume == Approx(kPi * r * r * h).epsilon(1e-9));
  REQUIRE(me.surfaceArea == Approx(2.0 * kPi * r * r + 2.0 * kPi * r * h).epsilon(1e-9));

  // The swept face is a real cylinder, so a snap lands on it rather than a chord short of it.
  for (const brep::Face& f : ex.faces) {
    if (f.surface.kind != brep::SurfaceKind::Cylinder)
      continue;
    const Vec3 probe{100.0, 0.3, h * 0.5};
    const Vec3 on = brep::ClosestPointOnSurface(f.surface, probe);
    REQUIRE(std::sqrt(on.x * on.x + on.y * on.y) == Approx(r).epsilon(1e-12));
  }
}

TEST_CASE("Extrude of a non-convex L is a valid solid with the hand-computed volume", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double h = 2.0;
  // An L: 3 wide at the bottom, 1 wide at the top, 3 tall. Area = 3*1 + 1*2 = 5.
  const brep::Profile pr =
      PolyProfile(World(), {{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}});
  Solid s;
  REQUIRE(brep::Extrude(pr, h, &s, &why));
  REQUIRE(brep::Validate(s) == Problem::Ok);
  REQUIRE(CountOf(s).v == 12);
  REQUIRE(CountOf(s).e == 18);
  REQUIRE(CountOf(s).f == 8);
  REQUIRE(brep::EulerCharacteristic(s) == 2);

  const brep::MassProperties mp = brep::ComputeMassProperties(s);
  REQUIRE(mp.valid);
  REQUIRE(mp.volume == Approx(5.0 * h).epsilon(1e-9));

  // The tessellation must re-derive the same volume by the divergence theorem — this is what
  // exercises the ear-clipped non-convex cap.
  brep::Tessellation t;
  REQUIRE(brep::Tessellate(s, 0.01, &t, &why));
  REQUIRE(TessellatedVolume(t) == Approx(5.0 * h).epsilon(1e-6));
  RequireWindingMatchesNormals(t);
  RequireBoundsContain(brep::ComputeBounds(s), t);
}

TEST_CASE("Extrude of a half-disk sweeps a cylinder face and a flat face", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double r = 6.0, h = 4.0;
  brep::Profile pr;
  pr.plane = World();
  pr.vertices = {Vec3{r, 0, 0}, Vec3{-r, 0, 0}};
  brep::ProfileEdge arc;
  arc.arc = true;
  arc.centre = Vec3{0, 0, 0};
  arc.sweep = kPi;  // the semicircle, over the top
  pr.edges = {arc, brep::ProfileEdge{}};  // then the diameter, straight

  Solid s;
  REQUIRE(brep::Extrude(pr, h, &s, &why));
  REQUIRE(brep::Validate(s) == Problem::Ok);

  const brep::MassProperties mp = brep::ComputeMassProperties(s);
  REQUIRE(mp.valid);
  REQUIRE(mp.volume == Approx(0.5 * kPi * r * r * h).epsilon(1e-9));

  int cyl = 0, plane = 0;
  for (const brep::Face& f : s.faces)
    (f.surface.kind == brep::SurfaceKind::Cylinder ? cyl : plane)++;
  REQUIRE(cyl == 1);
  REQUIRE(plane == 3);  // two caps + the flat rectangular face
}

TEST_CASE("Extrude stays accurate on a tilted frame at survey magnitude", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double w = 10.0, d = 4.0, h = 7.0;
  const std::vector<ucs::Point2D> rect = {{-w / 2, -d / 2}, {w / 2, -d / 2}, {w / 2, d / 2}, {-w / 2, d / 2}};

  Solid flat;
  REQUIRE(brep::Extrude(PolyProfile(World(), rect), h, &flat, &why));
  Solid tilted;
  REQUIRE(brep::Extrude(PolyProfile(TiltedAt(3.5e6, 1.24e7, 250.0), rect), h, &tilted, &why));

  const brep::MassProperties mf = brep::ComputeMassProperties(flat);
  const brep::MassProperties mt = brep::ComputeMassProperties(tilted);
  REQUIRE(mt.valid);
  REQUIRE(mt.volume == Approx(mf.volume).epsilon(1e-6));
  REQUIRE(mt.surfaceArea == Approx(mf.surfaceArea).epsilon(1e-6));
  REQUIRE(mt.volume == Approx(w * d * h).epsilon(1e-6));
}

TEST_CASE("A negative extrude distance sweeps the other way and still validates", "[brep][req314]") {
  Problem why = Problem::Ok;
  const std::vector<ucs::Point2D> rect = {{0, 0}, {4, 0}, {4, 2}, {0, 2}};
  Solid up, down;
  REQUIRE(brep::Extrude(PolyProfile(World(), rect), 3.0, &up, &why));
  REQUIRE(brep::Extrude(PolyProfile(World(), rect), -3.0, &down, &why));
  REQUIRE(brep::Validate(down) == Problem::Ok);

  const brep::MassProperties mu = brep::ComputeMassProperties(up);
  const brep::MassProperties md = brep::ComputeMassProperties(down);
  REQUIRE(md.valid);
  REQUIRE(md.volume == Approx(mu.volume).epsilon(1e-9));

  brep::Bounds bd = brep::ComputeBounds(down);
  REQUIRE(bd.mn.z == Approx(-3.0).margin(1e-9));
  REQUIRE(bd.mx.z == Approx(0.0).margin(1e-9));
}

TEST_CASE("A profile winding does not matter and the builder orients the result", "[brep][req314]") {
  Problem why = Problem::Ok;
  const std::vector<ucs::Point2D> ccw = {{0, 0}, {4, 0}, {4, 3}, {0, 3}};
  std::vector<ucs::Point2D> cw = ccw;
  std::reverse(cw.begin(), cw.end());

  Solid a, b;
  REQUIRE(brep::Extrude(PolyProfile(World(), ccw), 2.0, &a, &why));
  REQUIRE(brep::Extrude(PolyProfile(World(), cw), 2.0, &b, &why));
  REQUIRE(brep::Validate(a) == Problem::Ok);
  REQUIRE(brep::Validate(b) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(a).volume == Approx(brep::ComputeMassProperties(b).volume).epsilon(1e-9));
  REQUIRE(brep::ComputeMassProperties(a).volume == Approx(24.0).epsilon(1e-9));

  // The same, with an arc in the loop — the reversal path has to permute the edge list and flip
  // each sweep, which the all-straight case above does not exercise.
  const double r = 5.0, hgt = 3.0;
  brep::Profile ccwHalf;
  ccwHalf.plane = World();
  ccwHalf.vertices = {Vec3{r, 0, 0}, Vec3{-r, 0, 0}};
  brep::ProfileEdge topArc;
  topArc.arc = true;
  topArc.centre = Vec3{0, 0, 0};
  topArc.sweep = kPi;
  ccwHalf.edges = {topArc, brep::ProfileEdge{}};

  brep::Profile cwHalf;
  cwHalf.plane = World();
  cwHalf.vertices = {Vec3{r, 0, 0}, Vec3{-r, 0, 0}};
  brep::ProfileEdge topArcCw = topArc;
  topArcCw.sweep = -kPi;  // diameter first, then the arc back over the top the CW way
  cwHalf.edges = {brep::ProfileEdge{}, topArcCw};

  Solid hc, hw;
  REQUIRE(brep::Extrude(ccwHalf, hgt, &hc, &why));
  REQUIRE(brep::Extrude(cwHalf, hgt, &hw, &why));
  REQUIRE(brep::Validate(hw) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(hw).volume ==
          Approx(brep::ComputeMassProperties(hc).volume).epsilon(1e-9));
  REQUIRE(brep::ComputeMassProperties(hw).volume == Approx(0.5 * kPi * r * r * hgt).epsilon(1e-9));
}

TEST_CASE("Extrude refuses bad input by name and stores nothing", "[brep][req314]") {
  Problem why = Problem::Ok;
  const std::vector<ucs::Point2D> rect = {{0, 0}, {4, 0}, {4, 2}, {0, 2}};
  Solid s;

  SECTION("a zero distance") {
    REQUIRE_FALSE(brep::Extrude(PolyProfile(World(), rect), 0.0, &s, &why));
    REQUIRE(why == Problem::NonPositiveDistance);
  }
  SECTION("a non-finite distance") {
    REQUIRE_FALSE(brep::Extrude(PolyProfile(World(), rect), std::nan(""), &s, &why));
    REQUIRE(why == Problem::NonPositiveDistance);
  }
  SECTION("fewer than two edges") {
    REQUIRE_FALSE(brep::Extrude(PolyProfile(World(), {{0, 0}}), 3.0, &s, &why));
    REQUIRE(why == Problem::ProfileTooFewEdges);
  }
  SECTION("vertex and edge counts disagree") {
    brep::Profile pr = PolyProfile(World(), rect);
    pr.edges.pop_back();
    REQUIRE_FALSE(brep::Extrude(pr, 3.0, &s, &why));
    REQUIRE(why == Problem::ProfileMalformed);
  }
  SECTION("a point off the profile plane") {
    brep::Profile pr = PolyProfile(World(), rect);
    pr.vertices[2].z = 1.0;
    REQUIRE_FALSE(brep::Extrude(pr, 3.0, &s, &why));
    REQUIRE(why == Problem::ProfilePointOffPlane);
  }
  SECTION("an arc whose endpoints are not equidistant from its centre") {
    brep::Profile pr;
    pr.plane = World();
    pr.vertices = {Vec3{6, 0, 0}, Vec3{-4, 0, 0}};
    brep::ProfileEdge arc;
    arc.arc = true;
    arc.centre = Vec3{0, 0, 0};
    arc.sweep = kPi;
    pr.edges = {arc, brep::ProfileEdge{}};
    REQUIRE_FALSE(brep::Extrude(pr, 3.0, &s, &why));
    REQUIRE(why == Problem::ProfileArcRadiusMismatch);
  }
  SECTION("a figure-eight self-intersecting loop") {
    REQUIRE_FALSE(brep::Extrude(PolyProfile(World(), {{0, 0}, {4, 0}, {0, 3}, {4, 3}}), 2.0, &s, &why));
    REQUIRE(why == Problem::ProfileSelfIntersects);
  }
}

// ---------------------------------------------------------------------------
// Feature operations — Revolve (REQ-314 / ADR-046 increment 2, GitHub issue #147).
// ---------------------------------------------------------------------------

TEST_CASE("Revolve of a rectangle on the axis is the cylinder the primitive builder makes", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double r = 4.0, h = 9.0;
  // A rectangle with its left edge ON the Z axis: (0,0)-(4,0)-(4,9)-(0,9), in the world XZ plane.
  brep::Profile pr;
  ucs::Ucs xz;
  REQUIRE(ucs::FromNormal(Vec3{0, 0, 0}, Vec3{0, 1, 0}, &xz));  // plane normal +Y -> plane is XZ
  pr.plane = xz;
  pr.vertices = {ucs::PlaneToWorld(xz, {0, 0}), ucs::PlaneToWorld(xz, {r, 0}),
                 ucs::PlaneToWorld(xz, {r, h}), ucs::PlaneToWorld(xz, {0, h})};
  pr.edges.assign(4, brep::ProfileEdge{});

  Solid rev;
  REQUIRE(brep::Revolve(pr, Vec3{0, 0, 0}, Vec3{0, 0, 1}, kTwoPiTest, &rev, &why));
  REQUIRE(brep::Validate(rev) == Problem::Ok);

  const brep::MassProperties m = brep::ComputeMassProperties(rev);
  REQUIRE(m.valid);
  REQUIRE(m.volume == Approx(kPi * r * r * h).epsilon(1e-9));
  REQUIRE(m.surfaceArea == Approx(2.0 * kPi * r * r + 2.0 * kPi * r * h).epsilon(1e-9));
}

TEST_CASE("Revolve of a right triangle on the axis is a cone", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double r = 5.0, h = 12.0;
  ucs::Ucs xz;
  REQUIRE(ucs::FromNormal(Vec3{0, 0, 0}, Vec3{0, 1, 0}, &xz));
  brep::Profile pr;
  pr.plane = xz;
  // (0,0) base centre, (r,0) base rim, (0,h) apex.
  pr.vertices = {ucs::PlaneToWorld(xz, {0, 0}), ucs::PlaneToWorld(xz, {r, 0}), ucs::PlaneToWorld(xz, {0, h})};
  pr.edges.assign(3, brep::ProfileEdge{});

  Solid rev;
  REQUIRE(brep::Revolve(pr, Vec3{0, 0, 0}, Vec3{0, 0, 1}, kTwoPiTest, &rev, &why));
  REQUIRE(brep::Validate(rev) == Problem::Ok);
  const brep::MassProperties m = brep::ComputeMassProperties(rev);
  REQUIRE(m.valid);
  REQUIRE(m.volume == Approx(kPi * r * r * h / 3.0).epsilon(1e-9));
  const double slant = std::sqrt(r * r + h * h);
  REQUIRE(m.surfaceArea == Approx(kPi * r * r + kPi * r * slant).epsilon(1e-9));
}

TEST_CASE("Revolve volume obeys Pappus's theorem, partial and full", "[brep][req314]") {
  Problem why = Problem::Ok;
  ucs::Ucs xz;
  REQUIRE(ucs::FromNormal(Vec3{0, 0, 0}, Vec3{0, 1, 0}, &xz));
  brep::Profile pr;
  pr.plane = xz;
  // An L touching the axis: (0,0)-(3,0)-(3,1)-(1,1)-(1,4)-(0,4). Area = 3*1 + 1*3 = 6.
  // Centroid r = (3*1*1.5 + 1*3*0.5) / 6 = (4.5 + 1.5) / 6 = 1.0.
  const std::vector<ucs::Point2D> pts = {{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 4}, {0, 4}};
  for (const ucs::Point2D& q : pts)
    pr.vertices.push_back(ucs::PlaneToWorld(xz, q));
  pr.edges.assign(pts.size(), brep::ProfileEdge{});
  const double area = 6.0, rc = 1.0;

  SECTION("full turn") {
    Solid rev;
    REQUIRE(brep::Revolve(pr, Vec3{0, 0, 0}, Vec3{0, 0, 1}, kTwoPiTest, &rev, &why));
    REQUIRE(brep::ComputeMassProperties(rev).volume == Approx(kTwoPiTest * rc * area).epsilon(1e-9));
    // A coarse-mesh sanity check: the tessellation tracks the shape (inscribed, so a little under).
    brep::Tessellation t;
    REQUIRE(brep::Tessellate(rev, 0.001, &t, &why));
    const double want = kTwoPiTest * rc * area;
    REQUIRE(TessellatedVolume(t) > 0.99 * want);
    REQUIRE(TessellatedVolume(t) < 1.001 * want);
  }
  SECTION("a 90-degree wedge, with its two caps") {
    Solid rev;
    REQUIRE(brep::Revolve(pr, Vec3{0, 0, 0}, Vec3{0, 0, 1}, kPi / 2.0, &rev, &why));
    REQUIRE(brep::Validate(rev) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(rev).volume == Approx((kPi / 2.0) * rc * area).epsilon(1e-9));
    // The two caps are each the profile area.
    brep::Tessellation t;
    REQUIRE(brep::Tessellate(rev, 0.01, &t, &why));
    RequireWindingMatchesNormals(t);
  }
}

TEST_CASE("Revolve stays accurate on a tilted axis at survey magnitude", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double r = 3.0, h = 8.0;
  // Axis along a tilted direction, profile plane containing it, anchored at a state-plane point.
  const Vec3 anchor{3.5e6, 1.24e7, 300.0};
  const Vec3 axisDir = ray3d::Normalize(Vec3{0.4, -0.2, 1.0});
  ucs::Ucs plane;
  // plane normal perpendicular to the axis: any vector orthogonal to axisDir.
  const Vec3 nrm = ray3d::Normalize(ray3d::Cross(axisDir, Vec3{1, 0, 0}));
  REQUIRE(ucs::FromNormal(anchor, nrm, &plane));
  // Rebuild the plane so its X axis is the radial direction and Y is the axis.
  ucs::Ucs pl2;
  pl2.origin = anchor;
  pl2.zAxis = nrm;
  pl2.yAxis = axisDir;
  pl2.xAxis = ray3d::Normalize(ray3d::Cross(pl2.yAxis, pl2.zAxis));
  REQUIRE(ucs::IsRightHandedOrthonormal(pl2, 1e-9));

  brep::Profile pr;
  pr.plane = pl2;
  pr.vertices = {ucs::PlaneToWorld(pl2, {0, 0}), ucs::PlaneToWorld(pl2, {r, 0}),
                 ucs::PlaneToWorld(pl2, {r, h}), ucs::PlaneToWorld(pl2, {0, h})};
  pr.edges.assign(4, brep::ProfileEdge{});

  Solid rev;
  REQUIRE(brep::Revolve(pr, anchor, axisDir, kTwoPiTest, &rev, &why));
  REQUIRE(brep::Validate(rev) == Problem::Ok);
  const brep::MassProperties m = brep::ComputeMassProperties(rev);
  REQUIRE(m.valid);
  REQUIRE(m.volume == Approx(kPi * r * r * h).epsilon(1e-6));
  REQUIRE(m.surfaceArea == Approx(2.0 * kPi * r * r + 2.0 * kPi * r * h).epsilon(1e-6));
}

TEST_CASE("Revolve refuses bad input by name and stores nothing", "[brep][req314]") {
  Problem why = Problem::Ok;
  ucs::Ucs xz;
  REQUIRE(ucs::FromNormal(Vec3{0, 0, 0}, Vec3{0, 1, 0}, &xz));
  brep::Profile onAxisRect;
  onAxisRect.plane = xz;
  for (const ucs::Point2D& q : {ucs::Point2D{0, 0}, {4, 0}, {4, 6}, {0, 6}})
    onAxisRect.vertices.push_back(ucs::PlaneToWorld(xz, q));
  onAxisRect.edges.assign(4, brep::ProfileEdge{});
  Solid s;

  SECTION("a zero angle") {
    REQUIRE_FALSE(brep::Revolve(onAxisRect, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 0.0, &s, &why));
    REQUIRE(why == Problem::NonPositiveAngle);
  }
  SECTION("an angle past a full turn") {
    REQUIRE_FALSE(brep::Revolve(onAxisRect, Vec3{0, 0, 0}, Vec3{0, 0, 1}, kTwoPiTest * 1.5, &s, &why));
    REQUIRE(why == Problem::NonPositiveAngle);
  }
  SECTION("a zero-length axis") {
    REQUIRE_FALSE(brep::Revolve(onAxisRect, Vec3{0, 0, 0}, Vec3{0, 0, 0}, kPi, &s, &why));
    REQUIRE(why == Problem::RevolveAxisDegenerate);
  }
  SECTION("an axis not in the profile plane") {
    // The plane normal itself is the most out-of-plane a direction can be.
    REQUIRE_FALSE(brep::Revolve(onAxisRect, Vec3{0, 0, 0}, Vec3{0, 1, 0}, kPi, &s, &why));
    REQUIRE(why == Problem::RevolveAxisNotInPlane);
  }
  SECTION("a profile that does not touch the axis") {
    brep::Profile tube;
    tube.plane = xz;
    for (const ucs::Point2D& q : {ucs::Point2D{2, 0}, {4, 0}, {4, 6}, {2, 6}})
      tube.vertices.push_back(ucs::PlaneToWorld(xz, q));
    tube.edges.assign(4, brep::ProfileEdge{});
    REQUIRE_FALSE(brep::Revolve(tube, Vec3{0, 0, 0}, Vec3{0, 0, 1}, kTwoPiTest, &s, &why));
    REQUIRE(why == Problem::RevolveProfileMissesAxis);
  }
  SECTION("a profile that straddles the axis") {
    brep::Profile straddle;
    straddle.plane = xz;
    for (const ucs::Point2D& q : {ucs::Point2D{-2, 0}, {3, 0}, {3, 5}, {-2, 5}})
      straddle.vertices.push_back(ucs::PlaneToWorld(xz, q));
    straddle.edges.assign(4, brep::ProfileEdge{});
    REQUIRE_FALSE(brep::Revolve(straddle, Vec3{0, 0, 0}, Vec3{0, 0, 1}, kTwoPiTest, &s, &why));
    REQUIRE(why == Problem::RevolveProfileCrossesAxis);
  }
  SECTION("an arc in the profile") {
    brep::Profile arced = onAxisRect;
    arced.edges[1].arc = true;
    arced.edges[1].centre = ucs::PlaneToWorld(xz, {4, 3});
    arced.edges[1].sweep = kPi / 4.0;
    REQUIRE_FALSE(brep::Revolve(arced, Vec3{0, 0, 0}, Vec3{0, 0, 1}, kTwoPiTest, &s, &why));
    REQUIRE(why == Problem::RevolveArcInProfile);
  }
}

// ---------------------------------------------------------------------------
// Feature operations — Slice (REQ-314 / ADR-046 increment 3, GitHub issue #147).
// ---------------------------------------------------------------------------

TEST_CASE("Slice of a box in half gives two boxes whose volumes sum to the original", "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid box;
  REQUIRE(brep::MakeBox(At(50, 50, 0), 20, 12, 8, &box, &why));
  const double v0 = brep::ComputeMassProperties(box).volume;

  Solid top, bot;
  // A horizontal plane at z = 3 (the box rises from z = 0 to z = 8).
  REQUIRE(brep::Slice(box, Vec3{50, 50, 3}, Vec3{0, 0, 1}, brep::SliceKeep::Both, &top, &bot, &why));
  REQUIRE(brep::Validate(top) == Problem::Ok);
  REQUIRE(brep::Validate(bot) == Problem::Ok);
  const brep::MassProperties mt = brep::ComputeMassProperties(top);
  const brep::MassProperties mb = brep::ComputeMassProperties(bot);
  REQUIRE(mt.valid);
  REQUIRE(mb.valid);
  REQUIRE(mt.volume + mb.volume == Approx(v0).epsilon(1e-9));
  REQUIRE(mt.volume == Approx(20.0 * 12.0 * 5.0).epsilon(1e-9));  // z 3..8
  REQUIRE(mb.volume == Approx(20.0 * 12.0 * 3.0).epsilon(1e-9));  // z 0..3
}

TEST_CASE("Slice of a box by an oblique plane keeps both wedges", "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid box;
  REQUIRE(brep::MakeBox(World(), 10, 10, 10, &box, &why));
  const double v0 = brep::ComputeMassProperties(box).volume;

  Solid a, b;
  // MakeBox centres the box in X/Y and rises from z=0, so its centre is (0,0,5).
  REQUIRE(brep::Slice(box, Vec3{0, 0, 5}, ray3d::Normalize(Vec3{1, 0, 1}), brep::SliceKeep::Both, &a, &b, &why));
  REQUIRE(brep::Validate(a) == Problem::Ok);
  REQUIRE(brep::Validate(b) == Problem::Ok);
  const double va = brep::ComputeMassProperties(a).volume;
  const double vb = brep::ComputeMassProperties(b).volume;
  REQUIRE(va + vb == Approx(v0).epsilon(1e-9));
  REQUIRE(va == Approx(v0 / 2.0).epsilon(1e-9));  // a plane through the centre halves it

  brep::Tessellation t;
  REQUIRE(brep::Tessellate(a, 0.01, &t, &why));
  RequireWindingMatchesNormals(t);
  REQUIRE(TessellatedVolume(t) == Approx(va).epsilon(1e-9));
}

TEST_CASE("Slice keeps only the requested side", "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid box;
  REQUIRE(brep::MakeBox(World(), 6, 6, 6, &box, &why));

  Solid above;
  Solid untouched;
  brep::Problem w2 = brep::Problem::Ok;
  REQUIRE(brep::Slice(box, Vec3{0, 0, 2}, Vec3{0, 0, 1}, brep::SliceKeep::Above, &above, &untouched, &why));
  REQUIRE(brep::Validate(above) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(above).volume == Approx(6.0 * 6.0 * 4.0).epsilon(1e-9));
  // The `below` output was not requested, so it stays empty.
  REQUIRE(untouched.faces.empty());
  (void)w2;
}

TEST_CASE("Slice of an extruded L is valid and conserves volume", "[brep][req314]") {
  Problem why = Problem::Ok;
  // An L, extruded 4.
  const brep::Profile pr = /* reuse PolyProfile from the extrude section */ [] {
    brep::Profile p;
    p.plane = World();
    for (const ucs::Point2D& q : {ucs::Point2D{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}})
      p.vertices.push_back(ucs::PlaneToWorld(World(), q));
    p.edges.assign(6, brep::ProfileEdge{});
    return p;
  }();
  Solid solid;
  REQUIRE(brep::Extrude(pr, 4.0, &solid, &why));
  const double v0 = brep::ComputeMassProperties(solid).volume;

  Solid a, b;
  REQUIRE(brep::Slice(solid, Vec3{0, 0, 1.5}, Vec3{0, 0, 1}, brep::SliceKeep::Both, &a, &b, &why));
  REQUIRE(brep::Validate(a) == Problem::Ok);
  REQUIRE(brep::Validate(b) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(a).volume + brep::ComputeMassProperties(b).volume ==
          Approx(v0).epsilon(1e-9));
}

TEST_CASE("Slice refuses what it cannot do, by name", "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid box;
  REQUIRE(brep::MakeBox(World(), 8, 8, 8, &box, &why));
  Solid a, b;

  SECTION("a plane that misses the solid") {
    REQUIRE_FALSE(brep::Slice(box, Vec3{0, 0, 20}, Vec3{0, 0, 1}, brep::SliceKeep::Both, &a, &b, &why));
    REQUIRE(why == Problem::SlicePlaneMissesSolid);
  }
  SECTION("a degenerate plane normal") {
    REQUIRE_FALSE(brep::Slice(box, Vec3{0, 0, 4}, Vec3{0, 0, 0}, brep::SliceKeep::Both, &a, &b, &why));
    REQUIRE(why == Problem::SliceDegeneratePlane);
  }
  SECTION("an OBLIQUE cut that would clip a cap is reported, not sliced") {
    Solid cyl;
    REQUIRE(brep::MakeCylinder(World(), 4, 10, &cyl, &why));
    // Steep tilt near the top: the ellipse would run off the end of the cylinder.
    REQUIRE_FALSE(brep::Slice(cyl, Vec3{0, 0, 9.5}, ray3d::Normalize(Vec3{3, 0, 1}),
                              brep::SliceKeep::Both, &a, &b, &why));
    REQUIRE(why == Problem::SliceResultComplex);
  }
  SECTION("a sphere — no primitive pieces") {
    Solid sph;
    REQUIRE(brep::MakeSphere(World(), 5, &sph, &why));
    REQUIRE_FALSE(brep::Slice(sph, Vec3{0, 0, 0}, Vec3{0, 0, 1}, brep::SliceKeep::Both, &a, &b, &why));
    REQUIRE(why == Problem::SliceCurvedFace);
  }
}

TEST_CASE("Curved B2b-1: an oblique plane slices a cylinder into two elliptical-ended pieces",
          "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid cyl;
  REQUIRE(brep::MakeCylinder(World(), 4, 10, &cyl, &why));  // r 4, z 0..10 about +Z
  Solid up;
  Solid dn;
  // Plane through z=4 on the axis, tilted so cos(theta) = 2/sqrt(5) from the axis.
  REQUIRE(brep::Slice(cyl, Vec3{0, 0, 4}, ray3d::Normalize(Vec3{1, 0, 2}), brep::SliceKeep::Both, &up,
                      &dn, &why));
  REQUIRE(brep::Validate(up) == Problem::Ok);
  REQUIRE(brep::Validate(dn) == Problem::Ok);
  REQUIRE_FALSE(brep::SelfIntersects(up));
  REQUIRE_FALSE(brep::SelfIntersects(dn));

  const auto mUp = brep::ComputeMassProperties(up);
  const auto mDn = brep::ComputeMassProperties(dn);
  REQUIRE(mUp.valid);
  REQUIRE(mDn.valid);
  // A cylinder cut by an oblique plane at mean axis height z-bar has volume pi r^2 z-bar.
  REQUIRE(mDn.volume == Approx(kPi * 16.0 * 4.0).epsilon(1e-9));
  REQUIRE(mUp.volume == Approx(kPi * 16.0 * 6.0).epsilon(1e-9));
  REQUIRE(mUp.volume + mDn.volume == Approx(kPi * 16.0 * 10.0).epsilon(1e-9));

  // After the cut the pieces' lateral areas still sum to the whole cylinder's (2 pi r h), the two
  // original circular caps are unchanged, and each piece gains one elliptical cap of area pi a b
  // (b = r, a = r / cos theta).
  const double a = 4.0 / (2.0 / std::sqrt(5.0));
  const double wholeCyl = 2.0 * kPi * 16.0 + 2.0 * kPi * 4.0 * 10.0;  // 2 circular caps + lateral
  REQUIRE(mUp.surfaceArea + mDn.surfaceArea ==
          Approx(wholeCyl + 2.0 * kPi * a * 4.0).epsilon(1e-6));

  brep::Tessellation t;
  REQUIRE(brep::Tessellate(dn, 0.02, &t, &why));
  RequireWindingMatchesNormals(t);
  REQUIRE(TessellatedVolume(t) == Approx(kPi * 16.0 * 4.0).epsilon(0.01));  // chorded ellipse + slant

  // A .gs round-trip preserves the ellipse edge.
  Solid reopened = dn;  // Translate is the cheap in-kernel proxy for the store path
  reopened = brep::Translate(reopened, Vec3{0, 0, 0});
  bool sawEllipse = false;
  for (const auto& e : reopened.edges)
    if (e.kind == brep::CurveKind::Ellipse) {
      sawEllipse = true;
      REQUIRE(e.radius > e.radius2);  // semi-major > semi-minor
      REQUIRE(e.radius2 == Approx(4.0).epsilon(1e-9));
    }
  REQUIRE(sawEllipse);
}

TEST_CASE("Curved B2b-1: a tilted cylinder INTERSECT a box is an oblique elliptical-ended plug",
          "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid box;
  REQUIRE(brep::MakeBox(World(), 20, 20, 10, &box, &why));  // z 0..10, x,y[-10,10]
  ucs::Ucs axis;
  const Vec3 dir = ray3d::Normalize(Vec3{0.3, 0.0, 1.0});
  REQUIRE(ucs::FromNormal(Vec3{-3, 0, -10}, dir, &axis));
  Solid cyl;
  REQUIRE(brep::MakeCylinder(axis, 2, 30, &cyl, &why));  // long enough to cross both faces

  std::vector<Solid> r;
  REQUIRE(brep::BooleanIntersect(box, cyl, &r, &why));
  REQUIRE(r.size() == 1);
  REQUIRE(brep::Validate(r[0]) == Problem::Ok);
  REQUIRE_FALSE(brep::SelfIntersects(r[0]));
  // Volume = pi r^2 * (axial gap) = pi * 4 * (10 / dir.z).
  REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
          Approx(kPi * 4.0 * (10.0 / dir.z)).epsilon(1e-9));
  brep::Tessellation t;
  REQUIRE(brep::Tessellate(r[0], 0.05, &t, &why));
  RequireWindingMatchesNormals(t);

  // SUBTRACT drills a slanted elliptical-mouthed hole through the box.
  r.clear();
  REQUIRE(brep::BooleanSubtract(box, cyl, &r, &why));
  REQUIRE(r.size() == 1);
  REQUIRE(brep::Validate(r[0]) == Problem::Ok);
  REQUIRE_FALSE(brep::SelfIntersects(r[0]));
  REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
          Approx(4000.0 - kPi * 4.0 * (10.0 / dir.z)).epsilon(1e-9));
  brep::Tessellation ts;
  REQUIRE(brep::Tessellate(r[0], 0.05, &ts, &why));
  RequireWindingMatchesNormals(ts);

  // UNION adds the two slanted stubs that stick out past each face (an elliptical-mouthed boss).
  r.clear();
  REQUIRE(brep::BooleanUnion(box, cyl, &r, &why));
  REQUIRE(r.size() == 1);
  REQUIRE(brep::Validate(r[0]) == Problem::Ok);
  REQUIRE_FALSE(brep::SelfIntersects(r[0]));
  REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
          Approx(4000.0 + kPi * 4.0 * (30.0 - 10.0 / dir.z)).epsilon(1e-9));
  brep::Tessellation tu;
  REQUIRE(brep::Tessellate(r[0], 0.05, &tu, &why));
  RequireWindingMatchesNormals(tu);
}

TEST_CASE("Slice of a cylinder or cone perpendicular to its axis cuts it to length", "[brep][req314]") {
  Problem why = Problem::Ok;

  SECTION("cylinder -> two cylinders") {
    Solid cyl;
    REQUIRE(brep::MakeCylinder(World(), 4, 10, &cyl, &why));  // z 0..10 about +Z
    Solid top, bot;
    REQUIRE(brep::Slice(cyl, Vec3{0, 0, 6}, Vec3{0, 0, 1}, brep::SliceKeep::Both, &top, &bot, &why));
    REQUIRE(brep::Validate(top) == Problem::Ok);
    REQUIRE(brep::Validate(bot) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(top).volume == Approx(kPi * 16.0 * 4.0).epsilon(1e-9));  // z 6..10
    REQUIRE(brep::ComputeMassProperties(bot).volume == Approx(kPi * 16.0 * 6.0).epsilon(1e-9));  // z 0..6
    REQUIRE(top.recipe.kind == brep::PrimitiveKind::Cylinder);
  }

  SECTION("truncated cone -> two frustums, radius interpolated at the cut") {
    Solid cone;
    REQUIRE(brep::MakeCone(World(), 6, 2, 8, &cone, &why));  // base r6 at z0, top r2 at z8
    Solid top, bot;
    REQUIRE(brep::Slice(cone, Vec3{0, 0, 2}, Vec3{0, 0, 1}, brep::SliceKeep::Both, &top, &bot, &why));
    REQUIRE(brep::Validate(top) == Problem::Ok);
    REQUIRE(brep::Validate(bot) == Problem::Ok);
    // r at z=2 is 6 + (2-6)*2/8 = 5.  Bottom frustum r6..r5 over h2; top frustum r5..r2 over h6.
    const double vBot = kPi * 2.0 / 3.0 * (36.0 + 30.0 + 25.0);
    const double vTop = kPi * 6.0 / 3.0 * (25.0 + 10.0 + 4.0);
    REQUIRE(brep::ComputeMassProperties(bot).volume == Approx(vBot).epsilon(1e-9));
    REQUIRE(brep::ComputeMassProperties(top).volume == Approx(vTop).epsilon(1e-9));
  }

  SECTION("keep only one side") {
    Solid cyl;
    Solid above, below;
    REQUIRE(brep::MakeCylinder(At(0, 0, 0), 3, 12, &cyl, &why));
    REQUIRE(brep::Slice(cyl, Vec3{0, 0, 5}, Vec3{0, 0, 1}, brep::SliceKeep::Below, &above, &below, &why));
    REQUIRE(brep::ComputeMassProperties(below).volume == Approx(kPi * 9.0 * 5.0).epsilon(1e-9));  // z 0..5
    REQUIRE(above.faces.empty());
  }

  SECTION("a plane that misses the cylinder's height is reported") {
    Solid cyl;
    Solid a, b;
    REQUIRE(brep::MakeCylinder(World(), 4, 10, &cyl, &why));
    REQUIRE_FALSE(brep::Slice(cyl, Vec3{0, 0, 20}, Vec3{0, 0, 1}, brep::SliceKeep::Both, &a, &b, &why));
    REQUIRE(why == Problem::SlicePlaneMissesSolid);
  }
}

// ---------------------------------------------------------------------------
// Feature operations — Booleans, B1 (REQ-314 / ADR-046 increment 4, GitHub issue #147).
// ---------------------------------------------------------------------------

TEST_CASE("Booleans of two overlapping boxes match the hand-computed volumes", "[brep][req314]") {
  Problem why = Problem::Ok;
  // A: x[-5,5] y[-5,5] z[0,10].  B: x[-1,9] y[-5,5] z[4,14].  Overlap: 6 x 10 x 6 = 360.
  Solid a;
  Solid b;
  REQUIRE(brep::MakeBox(World(), 10, 10, 10, &a, &why));
  REQUIRE(brep::MakeBox(At(4, 0, 4), 10, 10, 10, &b, &why));

  std::vector<Solid> r;
  REQUIRE(brep::BooleanIntersect(a, b, &r, &why));
  REQUIRE(r.size() == 1);
  REQUIRE(brep::Validate(r[0]) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(6.0 * 10.0 * 6.0).epsilon(1e-9));

  r.clear();
  REQUIRE(brep::BooleanUnion(a, b, &r, &why));
  REQUIRE(r.size() == 1);
  REQUIRE(brep::Validate(r[0]) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(1000.0 + 1000.0 - 360.0).epsilon(1e-9));
  brep::Tessellation t;
  REQUIRE(brep::Tessellate(r[0], 0.05, &t, &why));
  RequireWindingMatchesNormals(t);
  REQUIRE(TessellatedVolume(t) == Approx(1640.0).epsilon(1e-6));

  r.clear();
  REQUIRE(brep::BooleanSubtract(a, b, &r, &why));
  REQUIRE(r.size() == 1);
  REQUIRE(brep::Validate(r[0]) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(1000.0 - 360.0).epsilon(1e-9));
}

TEST_CASE("Booleans report and refuse the cases B1 does not cover", "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid box;
  Solid farBox;
  Solid cyl;
  REQUIRE(brep::MakeBox(World(), 4, 4, 4, &box, &why));
  REQUIRE(brep::MakeBox(At(100, 0, 0), 4, 4, 4, &farBox, &why));
  REQUIRE(brep::MakeCylinder(World(), 3, 6, &cyl, &why));
  std::vector<Solid> r;

  SECTION("INTERSECT of solids that do not touch is reported as empty") {
    REQUIRE_FALSE(brep::BooleanIntersect(box, farBox, &r, &why));
    REQUIRE(why == Problem::BooleanEmptyResult);
  }
  SECTION("UNION of solids that do not touch returns both, untouched") {
    REQUIRE(brep::BooleanUnion(box, farBox, &r, &why));
    REQUIRE(r.size() == 2);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(64.0).epsilon(1e-9));
    REQUIRE(brep::ComputeMassProperties(r[1]).volume == Approx(64.0).epsilon(1e-9));
  }
  SECTION("SUBTRACT of a solid that is not touched leaves it unchanged") {
    REQUIRE(brep::BooleanSubtract(box, farBox, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(64.0).epsilon(1e-9));
  }
  SECTION("a curved operand is refused") {
    REQUIRE_FALSE(brep::BooleanUnion(box, cyl, &r, &why));
    REQUIRE(why == Problem::BooleanCurvedFace);
  }
}

TEST_CASE("SUBTRACT punches a blind hole through one face of a box", "[brep][req314]") {
  Problem why = Problem::Ok;
  // A 10-cube from z 0..10, minus a 2x2 bar entering the top and stopping at z = 4.
  Solid block;
  Solid bar;
  REQUIRE(brep::MakeBox(World(), 10, 10, 10, &block, &why));
  REQUIRE(brep::MakeBox(At(0, 0, 4), 2, 2, 8, &bar, &why));  // z 4..12, pokes out the top

  std::vector<Solid> r;
  REQUIRE(brep::BooleanSubtract(block, bar, &r, &why));
  REQUIRE(r.size() == 1);
  REQUIRE(brep::Validate(r[0]) == Problem::Ok);
  // Removed volume is the bar's part inside the block: 2 x 2 x 6 (z 4..10) = 24.
  REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(1000.0 - 24.0).epsilon(1e-9));
  REQUIRE(brep::EulerCharacteristic(r[0]) == 2);  // still genus 0 — a blind pocket, not a tunnel
}

TEST_CASE("Booleans chain on a non-convex result", "[brep][req314]") {
  Problem why = Problem::Ok;
  // Start with a 10-cube, cut a corner notch out to make a non-convex L, then subtract again.
  Solid cube;
  Solid notch;
  REQUIRE(brep::MakeBox(World(), 10, 10, 10, &cube, &why));       // x,y[-5,5] z[0,10]
  REQUIRE(brep::MakeBox(At(4, 4, 6), 6, 6, 6, &notch, &why));     // x[1,7] y[1,7] z[6,12]
  // Removed part inside the cube: x[1,5] y[1,5] z[6,10] = 4 x 4 x 4 = 64.
  std::vector<Solid> ell;
  REQUIRE(brep::BooleanSubtract(cube, notch, &ell, &why));
  REQUIRE(ell.size() == 1);
  REQUIRE(brep::Validate(ell[0]) == Problem::Ok);
  const double vEll = brep::ComputeMassProperties(ell[0]).volume;
  REQUIRE(vEll == Approx(1000.0 - 64.0).epsilon(1e-9));

  // Now subtract a second bar from that NON-CONVEX solid.
  Solid bar;
  REQUIRE(brep::MakeBox(At(-3, -3, 5), 2, 2, 20, &bar, &why));  // x[-4,-2] y[-4,-2] z[5,25]
  std::vector<Solid> r;
  REQUIRE(brep::BooleanSubtract(ell[0], bar, &r, &why));
  REQUIRE(r.size() == 1);
  REQUIRE(brep::Validate(r[0]) == Problem::Ok);
  // Bar's part inside the L: x[-4,-2] y[-4,-2] z[5,10] = 2 x 2 x 5 = 20 (that corner is not in the notch).
  REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(vEll - 20.0).epsilon(1e-9));
}

namespace {
constexpr double kPiT = 3.14159265358979323846;
}

TEST_CASE("Curved B1: a cylinder axis-aligned through a box - plug, boss, and the refused cases",
          "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid box;
  Solid cyl;
  REQUIRE(brep::MakeBox(World(), 10, 10, 10, &box, &why));       // x,y[-5,5] z[0,10]
  REQUIRE(brep::MakeCylinder(At(0, 0, -5), 2, 20, &cyl, &why));  // z[-5,15], r 2, clear of the edges

  std::vector<Solid> r;

  SECTION("INTERSECT is the plug where they overlap") {
    REQUIRE(brep::BooleanIntersect(box, cyl, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(kPiT * 4.0 * 10.0).epsilon(1e-9));
    // Ties the curved path to the primitive: it must BE a plain cylinder.
    Solid ref;
    REQUIRE(brep::MakeCylinder(World(), 2, 10, &ref, &why));
    REQUIRE(brep::ComputeMassProperties(r[0]).surfaceArea ==
            Approx(brep::ComputeMassProperties(ref).surfaceArea).epsilon(1e-9));
  }

  SECTION("UNION is a boss — the box plus the two stubs, no tunnel") {
    REQUIRE(brep::BooleanUnion(box, cyl, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
            Approx(1000.0 + kPiT * 4.0 * 10.0).epsilon(1e-6));
    // A bored face is an annulus, not a disk, so naive V-E+F is not 2 here — Validate is the check.
    brep::Tessellation t;
    REQUIRE(brep::Tessellate(r[0], 0.02, &t, &why));
    RequireWindingMatchesNormals(t);
    REQUIRE(TessellatedVolume(t) == Approx(1000.0 + kPiT * 4.0 * 10.0).epsilon(2e-3));
  }

  SECTION("SUBTRACT drills a round hole through the box (B2a — an inward wall)") {
    REQUIRE(brep::BooleanSubtract(box, cyl, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    REQUIRE_FALSE(brep::SelfIntersects(r[0]));
    REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
            Approx(1000.0 - kPiT * 4.0 * 10.0).epsilon(1e-9));
    // The two bored faces are annuli, not disks, so naive V-E+F is not the genus formula here.
    brep::Tessellation t;
    REQUIRE(brep::Tessellate(r[0], 0.02, &t, &why));
    RequireWindingMatchesNormals(t);  // the bore wall shades as a concave surface
    REQUIRE(TessellatedVolume(t) == Approx(1000.0 - kPiT * 4.0 * 10.0).epsilon(3e-3));
  }

  SECTION("SUBTRACT of a cylinder that stops inside is a blind round pocket") {
    ucs::Ucs down;
    REQUIRE(ucs::FromNormal(Vec3{0, 0, 12}, Vec3{0, 0, -1}, &down));
    Solid drill;
    REQUIRE(brep::MakeCylinder(down, 2, 8, &drill, &why));  // base z=12, axis down, floor at z=4
    REQUIRE(brep::BooleanSubtract(box, drill, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    REQUIRE_FALSE(brep::SelfIntersects(r[0]));
    // removed = the cylinder's part inside the box: z[4,10] -> pi r^2 * 6
    REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
            Approx(1000.0 - kPiT * 4.0 * 6.0).epsilon(1e-9));
    brep::Tessellation t;
    REQUIRE(brep::Tessellate(r[0], 0.05, &t, &why));
    RequireWindingMatchesNormals(t);
  }

  SECTION("SUBTRACT the other way (cylinder - box) is still refused - its own slice") {
    REQUIRE_FALSE(brep::BooleanSubtract(cyl, box, &r, &why));
    REQUIRE(why == Problem::BooleanCurvedFace);
  }

  SECTION("an oblique cylinder is refused by name") {
    ucs::Ucs oblique;
    REQUIRE(ucs::FromNormal(Vec3{0, 0, -4}, Vec3{0.32, 0.19, 0.93}, &oblique));
    Solid tilted;
    REQUIRE(brep::MakeCylinder(oblique, 1.5, 20, &tilted, &why));
    REQUIRE_FALSE(brep::BooleanUnion(box, tilted, &r, &why));
    REQUIRE(why == Problem::BooleanObliqueCylinder);
  }

  SECTION("a cone operand is still refused as a curved face") {
    Solid cone;
    REQUIRE(brep::MakeCone(At(0, 0, -5), 2, 0, 20, &cone, &why));
    REQUIRE_FALSE(brep::BooleanUnion(box, cone, &r, &why));
    REQUIRE(why == Problem::BooleanCurvedFace);
  }
}

TEST_CASE("Curved B1: a failed curved Boolean leaves the operands untouched", "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid box;
  Solid cyl;
  REQUIRE(brep::MakeBox(World(), 10, 10, 10, &box, &why));
  REQUIRE(brep::MakeCylinder(At(0, 0, -5), 2, 20, &cyl, &why));
  Solid cone;
  REQUIRE(brep::MakeCone(At(0, 0, -5), 2, 0, 20, &cone, &why));
  const Solid boxBefore = box;
  const Solid coneBefore = cone;
  std::vector<Solid> r;
  REQUIRE_FALSE(brep::BooleanSubtract(box, cone, &r, &why));  // a cone operand is still refused
  REQUIRE(box.vertices.size() == boxBefore.vertices.size());
  REQUIRE(cone.faces.size() == coneBefore.faces.size());
  REQUIRE(brep::ComputeMassProperties(box).volume == Approx(1000.0).epsilon(1e-12));
  REQUIRE(brep::ComputeMassProperties(cone).volume ==
          Approx(brep::ComputeMassProperties(coneBefore).volume).epsilon(1e-12));
  (void)cyl;
}

TEST_CASE("Curved B2b-1: the Steinmetz bicylinder - INTERSECT of two equal perpendicular cylinders",
          "[brep][req314]") {
  Problem why = Problem::Ok;
  const double r = 3.0;

  SECTION("axes crossing at the origin - volume 16 r^3 / 3, area 16 r^2, four elliptical edges") {
    Solid cylZ;
    Solid cylX;
    REQUIRE(brep::MakeCylinder(At(0, 0, -6), r, 12, &cylZ, &why));  // axis +z, z in [-6, 6]
    ucs::Ucs alongX;
    REQUIRE(ucs::FromNormal(Vec3{-6, 0, 0}, Vec3{1, 0, 0}, &alongX));
    REQUIRE(brep::MakeCylinder(alongX, r, 12, &cylX, &why));  // axis +x, x in [-6, 6]

    std::vector<Solid> out;
    REQUIRE(brep::BooleanIntersect(cylZ, cylX, &out, &why));
    REQUIRE(out.size() == 1);
    RequireSolid(out[0], Counts{2, 4, 4}, 2, 16.0 * r * r * r / 3.0, 16.0 * r * r);

    int ellipses = 0;
    for (const auto& e : out[0].edges)
      if (e.kind == brep::CurveKind::Ellipse)
        ++ellipses;
    REQUIRE(ellipses == 4);

    brep::Tessellation t;
    REQUIRE(brep::Tessellate(out[0], 0.002, &t, &why));
    RequireWindingMatchesNormals(t);
    REQUIRE(TessellatedVolume(t) == Approx(16.0 * r * r * r / 3.0).epsilon(3e-3));
  }

  SECTION("the same figure on a tilted frame at survey coordinate magnitude, and after Translate") {
    const ucs::Ucs frame = TiltedAt(3.5e6, 1.24e7, 250.0);
    ucs::Ucs alongZ = frame;
    alongZ.origin = ucs::UcsToWorld(frame, Vec3{0, 0, -6});
    ucs::Ucs alongX;
    REQUIRE(ucs::FromNormal(ucs::UcsToWorld(frame, Vec3{-6, 0, 0}), frame.xAxis, &alongX));
    Solid cylZ;
    Solid cylX;
    REQUIRE(brep::MakeCylinder(alongZ, r, 12, &cylZ, &why));
    REQUIRE(brep::MakeCylinder(alongX, r, 12, &cylX, &why));

    std::vector<Solid> out;
    REQUIRE(brep::BooleanIntersect(cylZ, cylX, &out, &why));
    REQUIRE(out.size() == 1);
    REQUIRE(brep::Validate(out[0]) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(out[0]).volume ==
            Approx(16.0 * r * r * r / 3.0).epsilon(1e-9));

    const brep::Solid moved = brep::Translate(out[0], Vec3{-3.5e6, -1.24e7, -250.0});
    REQUIRE(brep::Validate(moved) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(moved).volume ==
            Approx(16.0 * r * r * r / 3.0).epsilon(1e-9));
  }

  SECTION("SUBTRACT bores a clean perpendicular channel - volume vol(A) - 16 r^3 / 3") {
    Solid cylZ;
    Solid cylX;
    REQUIRE(brep::MakeCylinder(At(0, 0, -6), r, 12, &cylZ, &why));  // vol pi r^2 * 12
    ucs::Ucs alongX;
    REQUIRE(ucs::FromNormal(Vec3{-6, 0, 0}, Vec3{1, 0, 0}, &alongX));
    REQUIRE(brep::MakeCylinder(alongX, r, 12, &cylX, &why));

    std::vector<Solid> out;
    REQUIRE(brep::BooleanSubtract(cylZ, cylX, &out, &why));
    REQUIRE(out.size() == 1);
    const double vA = kPi * r * r * 12.0;
    RequireSolid(out[0], Counts{6, 12, 8}, 2, vA - 16.0 * r * r * r / 3.0,
                 // area: A's wall (2 pi r * 12 minus the bicylinder's A-share 8 r^2) + two caps
                 //       + the channel wall (the bicylinder's B-share 8 r^2)
                 2.0 * kPi * r * 12.0 - 8.0 * r * r + 2.0 * kPi * r * r + 8.0 * r * r);
    REQUIRE_FALSE(brep::SelfIntersects(out[0]));

    brep::Tessellation t;
    REQUIRE(brep::Tessellate(out[0], 0.01, &t, &why));
    RequireWindingMatchesNormals(t);
    REQUIRE(TessellatedVolume(t) == Approx(vA - 16.0 * r * r * r / 3.0).epsilon(3e-3));
  }

  SECTION("SUBTRACT stays exact on a tilted survey-magnitude frame and after Translate") {
    const ucs::Ucs frame = TiltedAt(3.5e6, 1.24e7, 250.0);
    ucs::Ucs alongZ = frame;
    alongZ.origin = ucs::UcsToWorld(frame, Vec3{0, 0, -6});
    ucs::Ucs alongX;
    REQUIRE(ucs::FromNormal(ucs::UcsToWorld(frame, Vec3{-6, 0, 0}), frame.xAxis, &alongX));
    Solid cylZ;
    Solid cylX;
    REQUIRE(brep::MakeCylinder(alongZ, r, 12, &cylZ, &why));
    REQUIRE(brep::MakeCylinder(alongX, r, 12, &cylX, &why));
    std::vector<Solid> out;
    REQUIRE(brep::BooleanSubtract(cylZ, cylX, &out, &why));
    REQUIRE(out.size() == 1);
    const double want = kPi * r * r * 12.0 - 16.0 * r * r * r / 3.0;
    REQUIRE(brep::Validate(out[0]) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(out[0]).volume == Approx(want).epsilon(1e-9));
    const brep::Solid moved = brep::Translate(out[0], Vec3{-3.5e6, -1.24e7, -250.0});
    REQUIRE(brep::Validate(moved) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(moved).volume == Approx(want).epsilon(1e-9));
  }

  SECTION("UNION is a T-pipe - volume vol(A) + vol(B) - 16 r^3 / 3") {
    Solid cylZ;
    Solid cylX;
    REQUIRE(brep::MakeCylinder(At(0, 0, -6), r, 12, &cylZ, &why));  // vol pi r^2 * 12
    ucs::Ucs alongX;
    REQUIRE(ucs::FromNormal(Vec3{-9, 0, 0}, Vec3{1, 0, 0}, &alongX));
    REQUIRE(brep::MakeCylinder(alongX, r, 18, &cylX, &why));  // vol pi r^2 * 18, x in [-9, 9]

    std::vector<Solid> out;
    REQUIRE(brep::BooleanUnion(cylZ, cylX, &out, &why));
    REQUIRE(out.size() == 1);
    const double vA = kPi * r * r * 12.0;
    const double vB = kPi * r * r * 18.0;
    const double want = vA + vB - 16.0 * r * r * r / 3.0;
    RequireSolid(out[0], Counts{10, 20, 12}, 2, want,
                 // each cylinder's wall (2 pi r * len) loses its 8 r^2 elliptical bite; four caps.
                 (2.0 * kPi * r * 12.0 - 8.0 * r * r) + (2.0 * kPi * r * 18.0 - 8.0 * r * r) +
                     4.0 * kPi * r * r);
    REQUIRE_FALSE(brep::SelfIntersects(out[0]));

    brep::Tessellation t;
    REQUIRE(brep::Tessellate(out[0], 0.003, &t, &why));
    RequireWindingMatchesNormals(t);
    REQUIRE(TessellatedVolume(t) == Approx(want).epsilon(3e-3));
  }

  SECTION("UNION stays exact on a tilted survey-magnitude frame and after Translate") {
    const ucs::Ucs frame = TiltedAt(3.5e6, 1.24e7, 250.0);
    ucs::Ucs alongZ = frame;
    alongZ.origin = ucs::UcsToWorld(frame, Vec3{0, 0, -6});
    ucs::Ucs alongX;
    REQUIRE(ucs::FromNormal(ucs::UcsToWorld(frame, Vec3{-6, 0, 0}), frame.xAxis, &alongX));
    Solid cylZ;
    Solid cylX;
    REQUIRE(brep::MakeCylinder(alongZ, r, 12, &cylZ, &why));
    REQUIRE(brep::MakeCylinder(alongX, r, 12, &cylX, &why));
    std::vector<Solid> out;
    REQUIRE(brep::BooleanUnion(cylZ, cylX, &out, &why));
    REQUIRE(out.size() == 1);
    const double want = 2.0 * kPi * r * r * 12.0 - 16.0 * r * r * r / 3.0;
    REQUIRE(brep::Validate(out[0]) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(out[0]).volume == Approx(want).epsilon(1e-9));
    const brep::Solid moved = brep::Translate(out[0], Vec3{-3.5e6, -1.24e7, -250.0});
    REQUIRE(brep::Validate(moved) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(moved).volume == Approx(want).epsilon(1e-9));
  }

  SECTION("unequal radii are the branch-pipe recogniser (B2b-2), not the Steinmetz one") {
    Solid cylZ;
    Solid cylX;
    REQUIRE(brep::MakeCylinder(At(0, 0, -6), r, 12, &cylZ, &why));
    ucs::Ucs alongX;
    REQUIRE(ucs::FromNormal(Vec3{-6, 0, 0}, Vec3{1, 0, 0}, &alongX));
    REQUIRE(brep::MakeCylinder(alongX, r * 0.5, 12, &cylX, &why));
    std::vector<Solid> out;
    REQUIRE(brep::BooleanIntersect(cylZ, cylX, &out, &why));  // a pipe-tee lens now
    REQUIRE(out.size() == 1);
    REQUIRE(brep::Validate(out[0]) == Problem::Ok);
    REQUIRE(CountOf(out[0]).f == 4);
  }
}

TEST_CASE("Curved B2b-2: a procedural Intersection edge marches along both cylinders",
          "[brep][req314]") {
  // The pipe-tee intersection curve: small cyl A (axis +x, radius r) meets big cyl B (axis +z,
  // radius R) along x = sqrt(R^2 - r^2 cos^2 theta), y = r cos theta, z = r sin theta.
  const double r = 2.0;
  const double R = 5.0;
  auto curve = [&](double th) {
    return Vec3{std::sqrt(R * R - r * r * std::cos(th) * std::cos(th)), r * std::cos(th),
                r * std::sin(th)};
  };

  brep::Surface a;
  a.kind = brep::SurfaceKind::Cylinder;
  REQUIRE(ucs::FromNormal(Vec3{0, 0, 0}, Vec3{1, 0, 0}, &a.frame));
  a.radius = r;
  a.height = 40.0;
  brep::Surface b;
  b.kind = brep::SurfaceKind::Cylinder;
  b.frame = ucs::Ucs{};
  b.radius = R;
  b.height = 40.0;

  Solid s;
  s.vertices.push_back(brep::Vertex{curve(0.0)});
  s.vertices.push_back(brep::Vertex{curve(kPi * 0.5)});
  brep::Edge e;
  e.kind = brep::CurveKind::Intersection;
  e.v0 = 0;
  e.v1 = 1;
  e.frame.origin = curve(kPi * 0.25);  // the witness
  e.isectSurfaces = {a, b};
  s.edges.push_back(e);

  auto onBoth = [](const Solid& sol, const brep::Edge& ed, double t) {
    const Vec3 p = brep::EdgePointAt(sol, ed, t);
    const Vec3 qa = brep::ClosestPointOnSurface(ed.isectSurfaces[0], p);
    const Vec3 qb = brep::ClosestPointOnSurface(ed.isectSurfaces[1], p);
    REQUIRE(ray3d::Length(ray3d::Sub(p, qa)) < 1e-6);
    REQUIRE(ray3d::Length(ray3d::Sub(p, qb)) < 1e-6);
    return p;
  };

  SECTION("every sampled point lies on both surfaces; the ends are the two vertices") {
    for (int i = 0; i <= 20; ++i)
      onBoth(s, s.edges[0], i / 20.0);
    REQUIRE(ray3d::Length(ray3d::Sub(brep::EdgePointAt(s, s.edges[0], 0.0), curve(0.0))) < 1e-6);
    REQUIRE(ray3d::Length(ray3d::Sub(brep::EdgePointAt(s, s.edges[0], 1.0), curve(kPi * 0.5))) < 1e-6);
    // the walk follows the curvature: its midpoint is well off the straight chord between the ends
    const Vec3 v0p = curve(0.0);
    const Vec3 v1p = curve(kPi * 0.5);
    const Vec3 mid = brep::EdgePointAt(s, s.edges[0], 0.5);
    const Vec3 chordMid = ray3d::Scale(ray3d::Add(v0p, v1p), 0.5);
    REQUIRE(ray3d::Length(ray3d::Sub(mid, chordMid)) > 0.1);
    // and the marched point matches the closed-form curve at its own angle (theta from y,z)
    for (int k = 1; k < 20; ++k) {
      const Vec3 p = brep::EdgePointAt(s, s.edges[0], k / 20.0);
      const double th = std::atan2(p.z / r, p.y / r);
      REQUIRE(ray3d::Length(ray3d::Sub(p, curve(th))) < 1e-6);
    }
  }

  SECTION("Translate carries the stored surfaces, so the curve still lands on them") {
    const brep::Solid moved = brep::Translate(s, Vec3{3.5e6, -1.24e7, 250.0});
    for (int i = 0; i <= 20; ++i)
      onBoth(moved, moved.edges[0], i / 20.0);
  }

  SECTION("bounds contain the marched curve; the edge asks for several segments") {
    const brep::Bounds bnd = brep::ComputeBounds(s);
    REQUIRE(bnd.valid);
    for (int i = 0; i <= 20; ++i) {
      const Vec3 p = brep::EdgePointAt(s, s.edges[0], i / 20.0);
      REQUIRE(p.x >= bnd.mn.x - 1e-6);
      REQUIRE(p.x <= bnd.mx.x + 1e-6);
      REQUIRE(p.y >= bnd.mn.y - 1e-6);
      REQUIRE(p.z <= bnd.mx.z + 1e-6);
    }
  }
}

TEST_CASE("Curved B2b-2: INTERSECT of a thin pipe crossing a thick one is the lens", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double r = 2.0;
  const double R = 5.0;

  // lens volume = 4 * integral_{-r}^{r} sqrt(R^2 - y^2) sqrt(r^2 - y^2) dy  (a fine reference)
  auto lensVolume = [&](double rr, double RR) {
    const int N = 400000;
    double v = 0.0;
    for (int i = 0; i < N; ++i) {
      const double y = -rr + 2.0 * rr * (i + 0.5) / N;
      v += (2.0 * rr / N) * 4.0 * std::sqrt(RR * RR - y * y) * std::sqrt(rr * rr - y * y);
    }
    return v;
  };
  const double vref = lensVolume(r, R);

  SECTION("axes crossing at the origin — volume matches the numerical reference") {
    Solid thin;
    Solid thick;
    ucs::Ucs ax;
    REQUIRE(ucs::FromNormal(Vec3{-12, 0, 0}, Vec3{1, 0, 0}, &ax));
    REQUIRE(brep::MakeCylinder(ax, r, 24, &thin, &why));  // axis +x, x in [-12, 12]
    REQUIRE(brep::MakeCylinder(At(0, 0, -12), R, 24, &thick, &why));  // axis +z

    std::vector<Solid> out;
    REQUIRE(brep::BooleanIntersect(thin, thick, &out, &why));
    REQUIRE(out.size() == 1);
    REQUIRE(brep::Validate(out[0]) == Problem::Ok);
    REQUIRE_FALSE(brep::SelfIntersects(out[0]));
    REQUIRE(CountOf(out[0]).v == 8);
    REQUIRE(CountOf(out[0]).e == 10);
    REQUIRE(CountOf(out[0]).f == 4);
    REQUIRE(brep::EulerCharacteristic(out[0]) == 2);

    int isect = 0;
    for (const auto& e : out[0].edges)
      if (e.kind == brep::CurveKind::Intersection)
        ++isect;
    REQUIRE(isect == 8);

    const brep::MassProperties mp = brep::ComputeMassProperties(out[0]);
    REQUIRE(mp.valid);
    REQUIRE(mp.volume == Approx(vref).epsilon(2e-4));

    brep::Tessellation t;
    REQUIRE(brep::Tessellate(out[0], 0.01, &t, &why));
    RequireWindingMatchesNormals(t);
    REQUIRE(TessellatedVolume(t) == Approx(vref).epsilon(5e-3));
  }

  SECTION("INTERSECT order does not matter") {
    Solid thin;
    Solid thick;
    ucs::Ucs ax;
    REQUIRE(ucs::FromNormal(Vec3{-12, 0, 0}, Vec3{1, 0, 0}, &ax));
    REQUIRE(brep::MakeCylinder(ax, r, 24, &thin, &why));
    REQUIRE(brep::MakeCylinder(At(0, 0, -12), R, 24, &thick, &why));
    std::vector<Solid> out;
    REQUIRE(brep::BooleanIntersect(thick, thin, &out, &why));  // reversed
    REQUIRE(out.size() == 1);
    REQUIRE(brep::ComputeMassProperties(out[0]).volume == Approx(vref).epsilon(2e-4));
  }

  SECTION("stable on a tilted survey-magnitude frame and after Translate") {
    const ucs::Ucs frame = TiltedAt(3.5e6, 1.24e7, 250.0);
    ucs::Ucs ax;
    REQUIRE(ucs::FromNormal(ucs::UcsToWorld(frame, Vec3{-12, 0, 0}), frame.xAxis, &ax));
    ucs::Ucs bx = frame;
    bx.origin = ucs::UcsToWorld(frame, Vec3{0, 0, -12});
    Solid thin;
    Solid thick;
    REQUIRE(brep::MakeCylinder(ax, r, 24, &thin, &why));
    REQUIRE(brep::MakeCylinder(bx, R, 24, &thick, &why));
    std::vector<Solid> out;
    REQUIRE(brep::BooleanIntersect(thin, thick, &out, &why));
    REQUIRE(out.size() == 1);
    REQUIRE(brep::Validate(out[0]) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(out[0]).volume == Approx(vref).epsilon(5e-4));
    const brep::Solid moved = brep::Translate(out[0], Vec3{-3.5e6, -1.24e7, -250.0});
    REQUIRE(brep::Validate(moved) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(moved).volume == Approx(vref).epsilon(5e-4));
  }
}

TEST_CASE("Curved B2b-2: SUBTRACT bores a branch clean through the main pipe", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double r = 2.0;
  const double R = 5.0;
  const double L = 24.0;  // thick cylinder length, z in [-12, 12]
  auto lensVolume = [&](double rr, double RR) {
    const int N = 400000;
    double v = 0.0;
    for (int i = 0; i < N; ++i) {
      const double y = -rr + 2.0 * rr * (i + 0.5) / N;
      v += (2.0 * rr / N) * 4.0 * std::sqrt(RR * RR - y * y) * std::sqrt(rr * rr - y * y);
    }
    return v;
  };
  const double want = kPi * R * R * L - lensVolume(r, R);

  SECTION("main minus branch — volume vol(main) - lens, genus one") {
    Solid thin;
    Solid thick;
    ucs::Ucs ax;
    REQUIRE(ucs::FromNormal(Vec3{-12, 0, 0}, Vec3{1, 0, 0}, &ax));
    REQUIRE(brep::MakeCylinder(ax, r, 24, &thin, &why));
    REQUIRE(brep::MakeCylinder(At(0, 0, -12), R, L, &thick, &why));

    std::vector<Solid> out;
    REQUIRE(brep::BooleanSubtract(thick, thin, &out, &why));
    REQUIRE(out.size() == 1);
    REQUIRE(brep::Validate(out[0]) == Problem::Ok);
    REQUIRE_FALSE(brep::SelfIntersects(out[0]));
    REQUIRE(CountOf(out[0]).v == 8);
    REQUIRE(CountOf(out[0]).e == 12);
    REQUIRE(CountOf(out[0]).f == 6);

    const brep::MassProperties mp = brep::ComputeMassProperties(out[0]);
    REQUIRE(mp.valid);
    REQUIRE(mp.volume == Approx(want).epsilon(5e-4));

    brep::Tessellation t;
    REQUIRE(brep::Tessellate(out[0], 0.01, &t, &why));
    RequireWindingMatchesNormals(t);
    REQUIRE(TessellatedVolume(t) == Approx(want).epsilon(6e-3));
  }

  SECTION("thin − thick (two stubs) is still refused") {
    Solid thin;
    Solid thick;
    ucs::Ucs ax;
    REQUIRE(ucs::FromNormal(Vec3{-12, 0, 0}, Vec3{1, 0, 0}, &ax));
    REQUIRE(brep::MakeCylinder(ax, r, 24, &thin, &why));
    REQUIRE(brep::MakeCylinder(At(0, 0, -12), R, L, &thick, &why));
    std::vector<Solid> out;
    REQUIRE_FALSE(brep::BooleanSubtract(thin, thick, &out, &why));
    REQUIRE(why == Problem::BooleanCurvedFace);
  }

  SECTION("stable on a tilted survey-magnitude frame and after Translate") {
    const ucs::Ucs frame = TiltedAt(3.5e6, 1.24e7, 250.0);
    ucs::Ucs ax;
    REQUIRE(ucs::FromNormal(ucs::UcsToWorld(frame, Vec3{-12, 0, 0}), frame.xAxis, &ax));
    ucs::Ucs bx = frame;
    bx.origin = ucs::UcsToWorld(frame, Vec3{0, 0, -12});
    Solid thin;
    Solid thick;
    REQUIRE(brep::MakeCylinder(ax, r, 24, &thin, &why));
    REQUIRE(brep::MakeCylinder(bx, R, L, &thick, &why));
    std::vector<Solid> out;
    REQUIRE(brep::BooleanSubtract(thick, thin, &out, &why));
    REQUIRE(out.size() == 1);
    REQUIRE(brep::Validate(out[0]) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(out[0]).volume == Approx(want).epsilon(1e-3));
    const brep::Solid moved = brep::Translate(out[0], Vec3{-3.5e6, -1.24e7, -250.0});
    REQUIRE(brep::Validate(moved) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(moved).volume == Approx(want).epsilon(1e-3));
  }
}

TEST_CASE("Curved B2b-2: UNION fuses the branch and the main into a solid pipe-tee", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double r = 2.0;
  const double R = 5.0;
  const double L = 24.0;    // main length, z in [-12, 12]
  const double bl = 24.0;   // branch length, x in [-12, 12]
  auto lensVolume = [&](double rr, double RR) {
    const int N = 400000;
    double v = 0.0;
    for (int i = 0; i < N; ++i) {
      const double y = -rr + 2.0 * rr * (i + 0.5) / N;
      v += (2.0 * rr / N) * 4.0 * std::sqrt(RR * RR - y * y) * std::sqrt(rr * rr - y * y);
    }
    return v;
  };
  const double want = kPi * r * r * bl + kPi * R * R * L - lensVolume(r, R);

  SECTION("volume vol(branch) + vol(main) - lens; a genus-zero solid") {
    Solid thin;
    Solid thick;
    ucs::Ucs ax;
    REQUIRE(ucs::FromNormal(Vec3{-12, 0, 0}, Vec3{1, 0, 0}, &ax));
    REQUIRE(brep::MakeCylinder(ax, r, bl, &thin, &why));
    REQUIRE(brep::MakeCylinder(At(0, 0, -12), R, L, &thick, &why));

    std::vector<Solid> out;
    REQUIRE(brep::BooleanUnion(thin, thick, &out, &why));
    REQUIRE(out.size() == 1);
    REQUIRE(brep::Validate(out[0]) == Problem::Ok);
    REQUIRE_FALSE(brep::SelfIntersects(out[0]));
    REQUIRE(CountOf(out[0]).v == 12);
    REQUIRE(CountOf(out[0]).e == 18);
    REQUIRE(CountOf(out[0]).f == 10);

    const brep::MassProperties mp = brep::ComputeMassProperties(out[0]);
    REQUIRE(mp.valid);
    REQUIRE(mp.volume == Approx(want).epsilon(5e-4));

    brep::Tessellation t;
    REQUIRE(brep::Tessellate(out[0], 0.01, &t, &why));
    RequireWindingMatchesNormals(t);
    REQUIRE(TessellatedVolume(t) == Approx(want).epsilon(6e-3));
  }

  SECTION("operand order does not matter; stays exact on a tilted survey frame and after Translate") {
    const ucs::Ucs frame = TiltedAt(3.5e6, 1.24e7, 250.0);
    ucs::Ucs ax;
    REQUIRE(ucs::FromNormal(ucs::UcsToWorld(frame, Vec3{-12, 0, 0}), frame.xAxis, &ax));
    ucs::Ucs bx = frame;
    bx.origin = ucs::UcsToWorld(frame, Vec3{0, 0, -12});
    Solid thin;
    Solid thick;
    REQUIRE(brep::MakeCylinder(ax, r, bl, &thin, &why));
    REQUIRE(brep::MakeCylinder(bx, R, L, &thick, &why));
    std::vector<Solid> out;
    REQUIRE(brep::BooleanUnion(thick, thin, &out, &why));  // reversed order
    REQUIRE(out.size() == 1);
    REQUIRE(brep::Validate(out[0]) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(out[0]).volume == Approx(want).epsilon(1e-3));
    const brep::Solid moved = brep::Translate(out[0], Vec3{-3.5e6, -1.24e7, -250.0});
    REQUIRE(brep::Validate(moved) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(moved).volume == Approx(want).epsilon(1e-3));
  }
}

TEST_CASE("Curved B1: two coaxial cylinders - union and intersect", "[brep][req314]") {
  Problem why = Problem::Ok;
  std::vector<Solid> r;

  SECTION("equal radius, overlapping — union merges to one cylinder, intersect is the overlap") {
    Solid a;
    Solid b;
    REQUIRE(brep::MakeCylinder(World(), 3, 10, &a, &why));       // z[0,10]
    REQUIRE(brep::MakeCylinder(At(0, 0, 5), 3, 10, &b, &why));   // z[5,15]

    REQUIRE(brep::BooleanUnion(a, b, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(kPiT * 9.0 * 15.0).epsilon(1e-9));

    r.clear();
    REQUIRE(brep::BooleanIntersect(a, b, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(kPiT * 9.0 * 5.0).epsilon(1e-9));
  }

  SECTION("different radius — union is a stepped stack") {
    Solid a;
    Solid b;
    REQUIRE(brep::MakeCylinder(World(), 4, 10, &a, &why));       // z[0,10] r4
    REQUIRE(brep::MakeCylinder(At(0, 0, 4), 2, 10, &b, &why));   // z[4,14] r2

    REQUIRE(brep::BooleanUnion(a, b, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
            Approx(kPiT * 16.0 * 10.0 + kPiT * 4.0 * 4.0).epsilon(1e-9));
    brep::Tessellation t;
    REQUIRE(brep::Tessellate(r[0], 0.02, &t, &why));
    RequireWindingMatchesNormals(t);

    r.clear();
    REQUIRE(brep::BooleanIntersect(a, b, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(kPiT * 4.0 * 6.0).epsilon(1e-9));
  }

  SECTION("disjoint along the axis — union returns both") {
    Solid a;
    Solid b;
    REQUIRE(brep::MakeCylinder(World(), 3, 5, &a, &why));         // z[0,5]
    REQUIRE(brep::MakeCylinder(At(0, 0, 20), 3, 5, &b, &why));    // z[20,25]
    REQUIRE(brep::BooleanUnion(a, b, &r, &why));
    REQUIRE(r.size() == 2);
    r.clear();
    REQUIRE_FALSE(brep::BooleanIntersect(a, b, &r, &why));
    REQUIRE(why == Problem::BooleanEmptyResult);
  }
}

TEST_CASE("Curved B2a: coaxial SUBTRACT bores a tube and a counterbore", "[brep][req314]") {
  Problem why = Problem::Ok;
  std::vector<Solid> r;

  SECTION("a narrower cylinder through the whole length is a tube") {
    Solid a;
    Solid b;
    REQUIRE(brep::MakeCylinder(World(), 4, 10, &a, &why));           // z[0,10] r4
    REQUIRE(brep::MakeCylinder(At(0, 0, -1), 2, 12, &b, &why));      // z[-1,11] r2, clears both ends
    REQUIRE(brep::BooleanSubtract(a, b, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    REQUIRE_FALSE(brep::SelfIntersects(r[0]));
    REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
            Approx(kPiT * (16.0 - 4.0) * 10.0).epsilon(1e-9));  // pi (rA^2 - rB^2) L
    brep::Tessellation t;
    REQUIRE(brep::Tessellate(r[0], 0.05, &t, &why));
    RequireWindingMatchesNormals(t);
  }

  SECTION("a narrower cylinder into one end is a counterbore (blind)") {
    Solid a;
    Solid b;
    REQUIRE(brep::MakeCylinder(World(), 4, 10, &a, &why));           // z[0,10] r4
    REQUIRE(brep::MakeCylinder(At(0, 0, -1), 2, 7, &b, &why));       // z[-1,6] r2, opens at z=0 only
    REQUIRE(brep::BooleanSubtract(a, b, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    // removed = the bore inside A: z[0,6] * pi rB^2 = pi 4 * 6
    REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
            Approx(kPiT * 16.0 * 10.0 - kPiT * 4.0 * 6.0).epsilon(1e-9));
  }

  SECTION("a wider cylinder over the middle splits A in two") {
    Solid a;
    Solid b;
    REQUIRE(brep::MakeCylinder(World(), 2, 12, &a, &why));           // z[0,12] r2
    REQUIRE(brep::MakeCylinder(At(0, 0, 4), 3, 4, &b, &why));        // z[4,8] r3 > rA
    REQUIRE(brep::BooleanSubtract(a, b, &r, &why));
    REQUIRE(r.size() == 2);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(kPiT * 4.0 * 4.0).epsilon(1e-9));
    REQUIRE(brep::ComputeMassProperties(r[1]).volume == Approx(kPiT * 4.0 * 4.0).epsilon(1e-9));
  }

  SECTION("a fully-internal narrower cylinder (a sealed cavity) is refused") {
    Solid a;
    Solid b;
    REQUIRE(brep::MakeCylinder(World(), 4, 10, &a, &why));
    REQUIRE(brep::MakeCylinder(At(0, 0, 3), 2, 4, &b, &why));        // z[3,7], both ends inside A
    REQUIRE_FALSE(brep::BooleanSubtract(a, b, &r, &why));
    REQUIRE(why == Problem::BooleanCurvedFace);
  }
}

TEST_CASE("Curved B1: a sphere cut by one face of a box - cap and boss", "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid sphere;
  Solid box;
  REQUIRE(brep::MakeSphere(World(), 5, &sphere, &why));       // centre origin, r 5
  REQUIRE(brep::MakeBox(At(0, 0, 2), 20, 20, 10, &box, &why)); // z[2,12], x,y[-10,10]
  std::vector<Solid> r;

  SECTION("INTERSECT is the spherical cap inside the box") {
    REQUIRE(brep::BooleanIntersect(sphere, box, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    // Cap of height h = r - d = 3 (d = 2): V = pi h^2 (3r - h) / 3.
    REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
            Approx(kPiT * 9.0 * (15.0 - 3.0) / 3.0).epsilon(1e-6));
    brep::Tessellation t;
    REQUIRE(brep::Tessellate(r[0], 0.02, &t, &why));
    RequireWindingMatchesNormals(t);
  }

  SECTION("UNION is a boss - the box plus the cap that pokes out the bored face") {
    REQUIRE(brep::BooleanUnion(sphere, box, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    // Outside cap height h = r + d = 7: box 4000 + pi 49 (15 - 7) / 3.
    REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
            Approx(4000.0 + kPiT * 49.0 * 8.0 / 3.0).epsilon(1e-6));
    brep::Tessellation t;
    REQUIRE(brep::Tessellate(r[0], 0.02, &t, &why));
    RequireWindingMatchesNormals(t);
  }

  SECTION("SUBTRACT scoops a spherical dimple out of the box face (B2a)") {
    REQUIRE(brep::BooleanSubtract(box, sphere, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::Validate(r[0]) == Problem::Ok);
    REQUIRE_FALSE(brep::SelfIntersects(r[0]));
    // removed = the cap inside the box, height h = r - d = 3: V = pi h^2 (3r - h) / 3
    REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
            Approx(4000.0 - kPiT * 9.0 * (15.0 - 3.0) / 3.0).epsilon(1e-6));
    brep::Tessellation t;
    REQUIRE(brep::Tessellate(r[0], 0.05, &t, &why));
    RequireWindingMatchesNormals(t);
  }

  SECTION("SUBTRACT the other way (sphere - box) is still refused") {
    REQUIRE_FALSE(brep::BooleanSubtract(sphere, box, &r, &why));
    REQUIRE(why == Problem::BooleanCurvedFace);
  }
}

TEST_CASE("Curved B1: a sphere against a box - the refused and trivial cases", "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid sphere;
  std::vector<Solid> r;

  SECTION("a sphere straddling a box corner (many cutting planes) is refused") {
    REQUIRE(brep::MakeSphere(World(), 5, &sphere, &why));
    Solid smallBox;
    REQUIRE(brep::MakeBox(World(), 6, 6, 6, &smallBox, &why));  // x,y[-3,3] z[0,6]
    REQUIRE_FALSE(brep::BooleanUnion(sphere, smallBox, &r, &why));
    REQUIRE(why == Problem::BooleanCurvedFace);
  }

  SECTION("a sphere wholly inside a box - intersect is the sphere, union is the box") {
    REQUIRE(brep::MakeSphere(At(0, 0, 50), 5, &sphere, &why));
    Solid bigBox;
    REQUIRE(brep::MakeBox(World(), 100, 100, 100, &bigBox, &why));
    REQUIRE(brep::BooleanIntersect(sphere, bigBox, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
            Approx(4.0 / 3.0 * kPiT * 125.0).epsilon(1e-9));
    r.clear();
    REQUIRE(brep::BooleanUnion(sphere, bigBox, &r, &why));
    REQUIRE(r.size() == 1);
    REQUIRE(brep::ComputeMassProperties(r[0]).volume == Approx(1.0e6).epsilon(1e-9));
  }

  SECTION("a sphere far from the box - disjoint") {
    REQUIRE(brep::MakeSphere(At(100, 0, 0), 5, &sphere, &why));
    Solid box;
    REQUIRE(brep::MakeBox(World(), 6, 6, 6, &box, &why));
    REQUIRE(brep::BooleanUnion(sphere, box, &r, &why));
    REQUIRE(r.size() == 2);
  }
}

TEST_CASE("Curved B2a: a drilled hole stays exact at survey coordinate magnitude, and survives Translate",
          "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid box;
  Solid cyl;
  REQUIRE(brep::MakeBox(At(3.5e6, 1.24e7, 0), 10, 10, 10, &box, &why));
  REQUIRE(brep::MakeCylinder(At(3.5e6, 1.24e7, -5), 2, 20, &cyl, &why));
  std::vector<Solid> r;
  REQUIRE(brep::BooleanSubtract(box, cyl, &r, &why));
  REQUIRE(r.size() == 1);
  REQUIRE(brep::Validate(r[0]) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
          Approx(1000.0 - kPiT * 4.0 * 10.0).epsilon(1e-6));

  // Translate must carry the inward flag and every coordinate (REQ-101 rebase).
  const brep::Solid moved = brep::Translate(r[0], Vec3{-3.5e6, -1.24e7, 0});
  REQUIRE(brep::Validate(moved) == Problem::Ok);
  bool sawInward = false;
  for (const auto& f : moved.faces)
    if (f.surface.inward)
      sawInward = true;
  REQUIRE(sawInward);
  REQUIRE(brep::ComputeMassProperties(moved).volume ==
          Approx(1000.0 - kPiT * 4.0 * 10.0).epsilon(1e-9));
}

TEST_CASE("Curved B1: a boss stays exact at survey coordinate magnitude", "[brep][req314]") {
  Problem why = Problem::Ok;
  Solid box;
  Solid cyl;
  REQUIRE(brep::MakeBox(At(3.5e6, 1.24e7, 0), 10, 10, 10, &box, &why));
  REQUIRE(brep::MakeCylinder(At(3.5e6, 1.24e7, -5), 2, 20, &cyl, &why));
  std::vector<Solid> r;
  REQUIRE(brep::BooleanUnion(box, cyl, &r, &why));
  REQUIRE(r.size() == 1);
  REQUIRE(brep::Validate(r[0]) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(r[0]).volume ==
          Approx(1000.0 + kPiT * 4.0 * 10.0).epsilon(1e-6));
}

// ---------------------------------------------------------------------------
// Feature operations - Loft (REQ-315 / ADR-048, GitHub issue #241). The side faces are
// SurfaceKind::Nurbs patches integrated by quadrature; every figure below is asserted against the
// closed form, not a recorded output.
// ---------------------------------------------------------------------------

namespace {

/// A square profile of side `side`, centred on `plane`'s origin, on `plane`.
brep::Profile SquareProfile(const ucs::Ucs& plane, double side) {
  const double h = 0.5 * side;
  return PolyProfile(plane, {{-h, -h}, {h, -h}, {h, h}, {-h, h}});
}

/// A plane parallel to `base`, its origin moved `along` up `base`'s normal.
ucs::Ucs PlaneAlong(const ucs::Ucs& base, double along) {
  ucs::Ucs u = base;
  u.origin = {base.origin.x + base.zAxis.x * along, base.origin.y + base.zAxis.y * along,
              base.origin.z + base.zAxis.z * along};
  return u;
}

double ConeFrustumVolume(double ra, double rb, double h) {
  return kPi * h / 3.0 * (ra * ra + ra * rb + rb * rb);
}

}  // namespace

TEST_CASE("Loft between two identical squares is the box the primitive builder makes", "[brep][req315]") {
  Problem why = Problem::Ok;
  const double w = 6.0, h = 4.0;
  Solid lofted;
  REQUIRE(brep::Loft({SquareProfile(World(), w), SquareProfile(PlaneAlong(World(), h), w)}, &lofted, &why));
  Solid box;
  REQUIRE(brep::MakeBox(At(0, 0, h * 0.5), w, w, h, &box, &why));

  REQUIRE(CountOf(lofted).v == CountOf(box).v);
  REQUIRE(CountOf(lofted).e == CountOf(box).e);
  REQUIRE(CountOf(lofted).f == CountOf(box).f);
  REQUIRE(brep::EulerCharacteristic(lofted) == 2);
  REQUIRE_FALSE(brep::SelfIntersects(lofted));

  const brep::MassProperties mp = brep::ComputeMassProperties(lofted);
  REQUIRE(mp.valid);
  REQUIRE(mp.volume == Approx(w * w * h).epsilon(1e-7));
  REQUIRE(mp.surfaceArea == Approx(2.0 * w * w + 4.0 * w * h).epsilon(1e-7));

  brep::Tessellation t;
  REQUIRE(brep::Tessellate(lofted, 0.01, &t, &why));
  REQUIRE(t.triangleCount() > 0);
  std::vector<double> iso;
  REQUIRE(brep::TessellateIsolines(lofted, 4, 0.01, &iso, &why));
}

TEST_CASE("Loft from a square to a smaller square is the pyramid frustum", "[brep][req315]") {
  Problem why = Problem::Ok;
  const double s0 = 8.0, s1 = 3.0, H = 5.0;
  Solid s;
  REQUIRE(brep::Loft({SquareProfile(World(), s0), SquareProfile(PlaneAlong(World(), H), s1)}, &s, &why));
  REQUIRE(brep::Validate(s) == Problem::Ok);

  const double a0 = s0 * s0, a1 = s1 * s1;
  const double expected = H / 3.0 * (a0 + a1 + std::sqrt(a0 * a1));
  REQUIRE(brep::ComputeMassProperties(s).volume == Approx(expected).epsilon(1e-6));
}

TEST_CASE("Loft through three circles is a stack of cone frustums", "[brep][req315]") {
  Problem why = Problem::Ok;
  const double r0 = 5.0, r1 = 8.0, r2 = 3.5;
  const double z1 = 4.0, z2 = 11.0;
  Solid s;
  REQUIRE(brep::Loft({CircleProfile(World(), r0), CircleProfile(PlaneAlong(World(), z1), r1),
                      CircleProfile(PlaneAlong(World(), z2), r2)},
                     &s, &why));
  REQUIRE(brep::Validate(s) == Problem::Ok);
  REQUIRE_FALSE(brep::SelfIntersects(s));

  const double expected = ConeFrustumVolume(r0, r1, z1) + ConeFrustumVolume(r1, r2, z2 - z1);
  const brep::MassProperties mp = brep::ComputeMassProperties(s);
  REQUIRE(mp.valid);
  REQUIRE(mp.volume == Approx(expected).epsilon(1e-5));

  // A face snap lands ON the freeform patch: at mid-height of the lower band the frustum radius is
  // (r0 + r1) / 2, and a probe far out in +X must snap to exactly that radius.
  for (const brep::Face& f : s.faces) {
    if (f.surface.kind != brep::SurfaceKind::Nurbs)
      continue;
    const Vec3 on = brep::ClosestPointOnSurface(f.surface, Vec3{100.0, 0.0, z1 * 0.5});
    if (std::fabs(on.z - z1 * 0.5) < 1e-6) {
      const double rho = std::sqrt(on.x * on.x + on.y * on.y);
      REQUIRE(rho == Approx(0.5 * (r0 + r1)).epsilon(1e-6));
    }
  }
}

TEST_CASE("Loft stays accurate on a tilted frame at survey magnitude", "[brep][req315]") {
  Problem why = Problem::Ok;
  auto barrel = [&](const ucs::Ucs& base) {
    Solid s;
    REQUIRE(brep::Loft({CircleProfile(base, 5.0), CircleProfile(PlaneAlong(base, 4.0), 7.0),
                        CircleProfile(PlaneAlong(base, 10.0), 4.0)},
                       &s, &why));
    return s;
  };
  const Solid flat = barrel(World());
  const Solid tilted = barrel(TiltedAt(3.5e6, 1.24e7, 250.0));
  REQUIRE(brep::Validate(tilted) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(tilted).volume ==
          Approx(brep::ComputeMassProperties(flat).volume).epsilon(1e-6));
}

TEST_CASE("Translate moves every point of a lofted NURBS face by exactly the offset", "[brep][req315]") {
  Problem why = Problem::Ok;
  Solid s;
  REQUIRE(brep::Loft({CircleProfile(World(), 4.0), CircleProfile(PlaneAlong(World(), 6.0), 6.0)}, &s, &why));
  const Vec3 delta{-3.5e6, 1.24e7, -812.0};
  const Solid moved = brep::Translate(s, delta);
  REQUIRE(brep::Validate(moved) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(moved).volume ==
          Approx(brep::ComputeMassProperties(s).volume).epsilon(1e-9));

  bool checkedNurbs = false;
  for (std::size_t fi = 0; fi < s.faces.size(); ++fi) {
    if (s.faces[fi].surface.kind != brep::SurfaceKind::Nurbs)
      continue;
    checkedNurbs = true;
    const nurbs::Patch& a = s.faces[fi].surface.patch;
    const nurbs::Patch& b = moved.faces[fi].surface.patch;
    for (double u : {0.0, 0.3, 1.0})
      for (double v : {0.0, 0.7, 1.0}) {
        const Vec3 pa = nurbs::Evaluate(a, nurbs::UMin(a) + u * (nurbs::UMax(a) - nurbs::UMin(a)),
                                        nurbs::VMin(a) + v * (nurbs::VMax(a) - nurbs::VMin(a)));
        const Vec3 pb = nurbs::Evaluate(b, nurbs::UMin(b) + u * (nurbs::UMax(b) - nurbs::UMin(b)),
                                        nurbs::VMin(b) + v * (nurbs::VMax(b) - nurbs::VMin(b)));
        REQUIRE(pb.x - pa.x == Approx(delta.x));
        REQUIRE(pb.y - pa.y == Approx(delta.y));
        REQUIRE(pb.z - pa.z == Approx(delta.z));
      }
  }
  REQUIRE(checkedNurbs);
}

TEST_CASE("Loft refuses bad input by name and stores nothing", "[brep][req315]") {
  Problem why = Problem::Ok;
  Solid s;
  SECTION("fewer than two profiles") {
    REQUIRE_FALSE(brep::Loft({SquareProfile(World(), 4.0)}, &s, &why));
    REQUIRE(why == Problem::LoftNeedsTwoProfiles);
  }
  SECTION("different edge counts") {
    REQUIRE_FALSE(brep::Loft({SquareProfile(World(), 4.0),
                              PolyProfile(PlaneAlong(World(), 3.0), {{0, 0}, {4, 0}, {2, 4}})},
                             &s, &why));
    REQUIRE(why == Problem::LoftProfileMismatch);
  }
  SECTION("a straight edge paired with an arc") {
    REQUIRE_FALSE(brep::Loft({SquareProfile(World(), 6.0), CircleProfile(PlaneAlong(World(), 3.0), 3.0)},
                             &s, &why));
    REQUIRE(why == Problem::LoftProfileMismatch);
  }
  SECTION("a non-planar profile") {
    brep::Profile bad = SquareProfile(World(), 4.0);
    bad.vertices[2].z += 1.0;
    REQUIRE_FALSE(brep::Loft({bad, SquareProfile(PlaneAlong(World(), 3.0), 4.0)}, &s, &why));
    REQUIRE(why == Problem::ProfilePointOffPlane);
  }
  REQUIRE(s.faces.empty());
}

TEST_CASE("A lofted-prism volume does not move when tessellation quality changes", "[brep][req315]") {
  Problem why = Problem::Ok;
  Solid s;
  REQUIRE(brep::Loft({CircleProfile(World(), 5.0), CircleProfile(PlaneAlong(World(), 8.0), 5.0)}, &s, &why));
  const double v = brep::ComputeMassProperties(s).volume;
  REQUIRE(v == Approx(kPi * 25.0 * 8.0).epsilon(1e-6));

  brep::Tessellation coarse;
  brep::Tessellation fine;
  REQUIRE(brep::Tessellate(s, 0.5, &coarse, &why));
  REQUIRE(brep::Tessellate(s, 0.002, &fine, &why));
  REQUIRE(brep::ComputeMassProperties(s).volume == v);  // unchanged by the tessellation calls
  REQUIRE(TessellatedVolume(fine) == Approx(v).epsilon(0.01));
}

// ---------------------------------------------------------------------------
// Feature operations - Sweep (REQ-315 / ADR-048, GitHub issue #241). One profile along one path
// segment. A straight path reproduces Extrude; a circular-arc path reproduces the solid of
// revolution (Pappus volume). Side faces are SurfaceKind::Nurbs.
// ---------------------------------------------------------------------------

namespace {

brep::SweepPath LinePath(const Vec3& a, const Vec3& b) {
  brep::SweepPath p;
  p.points = {a, b};
  p.segments = {brep::SweepSegment{}};
  return p;
}

brep::SweepPath ArcPath(const Vec3& start, const Vec3& centre, const Vec3& axis, double sweep) {
  brep::SweepSegment seg;
  seg.arc = true;
  seg.centre = centre;
  seg.normal = axis;
  seg.sweep = sweep;
  const Vec3 r0 = ray3d::Sub(start, centre);
  const double c = std::cos(sweep), s = std::sin(sweep);
  const Vec3 k = ray3d::Normalize(axis);
  const Vec3 end = ray3d::Add(
      centre, ray3d::Add(ray3d::Add(ray3d::Scale(r0, c), ray3d::Scale(ray3d::Cross(k, r0), s)),
                         ray3d::Scale(k, ray3d::Dot(k, r0) * (1.0 - c))));
  brep::SweepPath p;
  p.points = {start, end};
  p.segments = {seg};
  return p;
}

}  // namespace

TEST_CASE("Sweep along a straight path is the extrude of the same profile", "[brep][req315]") {
  Problem why = Problem::Ok;
  const double w = 8.0, d = 5.0, h = 4.0;
  const brep::Profile rect =
      PolyProfile(World(), {{-w / 2, -d / 2}, {w / 2, -d / 2}, {w / 2, d / 2}, {-w / 2, d / 2}});

  Solid swept;
  REQUIRE(brep::Sweep(rect, LinePath(Vec3{0, 0, 0}, Vec3{0, 0, h}), brep::SweepOptions{}, &swept, &why));
  Solid extruded;
  REQUIRE(brep::Extrude(rect, h, &extruded, &why));

  REQUIRE(CountOf(swept).v == CountOf(extruded).v);
  REQUIRE(CountOf(swept).e == CountOf(extruded).e);
  REQUIRE(CountOf(swept).f == CountOf(extruded).f);
  REQUIRE(brep::EulerCharacteristic(swept) == 2);

  const brep::MassProperties ms = brep::ComputeMassProperties(swept);
  REQUIRE(ms.valid);
  REQUIRE(ms.volume == Approx(w * d * h).epsilon(1e-7));
  REQUIRE(ms.surfaceArea == Approx(brep::ComputeMassProperties(extruded).surfaceArea).epsilon(1e-7));
}

TEST_CASE("Sweep along an oblique straight path is an extrude perpendicular to the path", "[brep][req315]") {
  Problem why = Problem::Ok;
  const double side = 4.0;
  const brep::Profile sq =
      PolyProfile(World(), {{-side / 2, -side / 2}, {side / 2, -side / 2}, {side / 2, side / 2},
                            {-side / 2, side / 2}});
  const Vec3 a{0, 0, 0};
  const Vec3 b{6, 2, 9};  // an arbitrary 3D direction
  Solid s;
  REQUIRE(brep::Sweep(sq, LinePath(a, b), brep::SweepOptions{}, &s, &why));
  REQUIRE(brep::Validate(s) == Problem::Ok);
  // alignToPath rotates the profile square onto the plane perpendicular to the path, so the solid is
  // a right prism of length |b - a|.
  const double len = std::sqrt(36.0 + 4.0 + 81.0);
  REQUIRE(brep::ComputeMassProperties(s).volume == Approx(side * side * len).epsilon(1e-6));
}

TEST_CASE("Sweep along a circular-arc path is the solid of revolution (Pappus volume)", "[brep][req315]") {
  Problem why = Problem::Ok;
  // A rectangle in the XZ plane, radius 2..5, height 3 — clear of the Z axis. Area 9, centroid r 3.5.
  ucs::Ucs xz;
  REQUIRE(ucs::FromNormal(Vec3{3.5, 0.0, 1.5}, Vec3{0, 1, 0}, &xz));
  brep::Profile pr;
  pr.plane = xz;
  for (const ucs::Point2D& q : {ucs::Point2D{-1.5, -1.5}, ucs::Point2D{1.5, -1.5},
                                ucs::Point2D{1.5, 1.5}, ucs::Point2D{-1.5, 1.5}})
    pr.vertices.push_back(ucs::PlaneToWorld(xz, q));
  pr.edges.assign(4, brep::ProfileEdge{});

  const double area = 9.0;
  const double centroidR = 3.5;

  for (const double ang : {kPi / 2.0, kPi, 1.7 * kPi}) {
    Solid s;
    const bool ok = brep::Sweep(pr, ArcPath(xz.origin, Vec3{0, 0, 1.5}, Vec3{0, 0, 1}, ang),
                                brep::SweepOptions{}, &s, &why);
    INFO("ang=" << ang << " why=" << brep::ProblemText(why));
    REQUIRE(ok);
    REQUIRE(brep::Validate(s) == Problem::Ok);
    REQUIRE_FALSE(brep::SelfIntersects(s));
    REQUIRE(brep::ComputeMassProperties(s).volume == Approx(ang * centroidR * area).epsilon(1e-5));
  }
}

TEST_CASE("Sweep stays accurate on a tilted arc path at survey magnitude", "[brep][req315]") {
  Problem why = Problem::Ok;
  auto build = [&](const ucs::Ucs& base) {
    // A small square at radius ~10 on a plane containing the base normal, swept 120 deg about base Z.
    ucs::Ucs prof;
    const Vec3 o = ucs::UcsToWorld(base, Vec3{10, 0, 0});
    REQUIRE(ucs::FromNormal(o, ucs::UcsVectorToWorld(base, Vec3{0, 1, 0}), &prof));
    brep::Profile pr;
    pr.plane = prof;
    for (const ucs::Point2D& q :
         {ucs::Point2D{-1, -1}, ucs::Point2D{1, -1}, ucs::Point2D{1, 1}, ucs::Point2D{-1, 1}})
      pr.vertices.push_back(ucs::PlaneToWorld(prof, q));
    pr.edges.assign(4, brep::ProfileEdge{});
    const brep::SweepPath path = ArcPath(o, base.origin, base.zAxis, 2.0 * kPi / 3.0);
    Solid s;
    REQUIRE(brep::Sweep(pr, path, brep::SweepOptions{}, &s, &why));
    return s;
  };
  const Solid flat = build(World());
  const Solid tilted = build(TiltedAt(3.5e6, 1.24e7, 250.0));
  REQUIRE(brep::Validate(tilted) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(tilted).volume ==
          Approx(brep::ComputeMassProperties(flat).volume).epsilon(1e-6));
}

TEST_CASE("A twisted straight sweep is a valid closed solid a little under the untwisted prism", "[brep][req315]") {
  Problem why = Problem::Ok;
  const double side = 4.0, h = 10.0;
  const brep::Profile sq =
      PolyProfile(World(), {{-side / 2, -side / 2}, {side / 2, -side / 2}, {side / 2, side / 2},
                            {-side / 2, side / 2}});
  brep::SweepOptions opt;
  opt.twistRad = kPi / 3.0;  // 60 degrees over the length
  Solid s;
  REQUIRE(brep::Sweep(sq, LinePath(Vec3{0, 0, 0}, Vec3{0, 0, h}), opt, &s, &why));
  REQUIRE(brep::Validate(s) == Problem::Ok);
  REQUIRE(brep::EulerCharacteristic(s) == 2);
  // The ruled side faces pinch as they twist, so the solid is smaller than the straight prism but
  // still a genuine positive volume. The real invariant is that the analytic integral and an
  // independent triangle sum agree.
  const double prism = side * side * h;
  const double v = brep::ComputeMassProperties(s).volume;
  REQUIRE(v > 0.6 * prism);
  REQUIRE(v < prism);
  brep::Tessellation t;
  REQUIRE(brep::Tessellate(s, 0.002, &t, &why));
  REQUIRE(TessellatedVolume(t) == Approx(v).epsilon(0.02));
}

TEST_CASE("Sweep translate at state-plane magnitude matches the origin build", "[brep][req315]") {
  Problem why = Problem::Ok;
  const brep::Profile sq = PolyProfile(World(), {{-2, -2}, {2, -2}, {2, 2}, {-2, 2}});
  Solid s;
  REQUIRE(brep::Sweep(sq, LinePath(Vec3{0, 0, 0}, Vec3{1, 0, 5}), brep::SweepOptions{}, &s, &why));
  const Vec3 delta{3.5e6, -1.24e7, 812.0};
  const Solid moved = brep::Translate(s, delta);
  REQUIRE(brep::Validate(moved) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(moved).volume ==
          Approx(brep::ComputeMassProperties(s).volume).epsilon(1e-9));
}

TEST_CASE("Sweep refuses bad input by name and stores nothing", "[brep][req315]") {
  Problem why = Problem::Ok;
  const brep::Profile sq = PolyProfile(World(), {{-2, -2}, {2, -2}, {2, 2}, {-2, 2}});
  Solid s;
  SECTION("a zero-length line path") {
    REQUIRE_FALSE(brep::Sweep(sq, LinePath(Vec3{1, 1, 1}, Vec3{1, 1, 1}), brep::SweepOptions{}, &s, &why));
    REQUIRE(why == Problem::SweepPathDegenerate);
  }
  SECTION("a profile that reaches the arc path axis") {
    ucs::Ucs xz;
    REQUIRE(ucs::FromNormal(Vec3{2, 0, 0}, Vec3{0, 1, 0}, &xz));
    brep::Profile pr;
    pr.plane = xz;
    for (const ucs::Point2D& q : {ucs::Point2D{-2, -1}, ucs::Point2D{2, -1}, ucs::Point2D{2, 1},
                                  ucs::Point2D{-2, 1}})  // spans radius 0..4 -> touches the axis
      pr.vertices.push_back(ucs::PlaneToWorld(xz, q));
    pr.edges.assign(4, brep::ProfileEdge{});
    REQUIRE_FALSE(brep::Sweep(pr, ArcPath(xz.origin, Vec3{0, 0, 0}, Vec3{0, 0, 1}, kPi / 2.0),
                              brep::SweepOptions{}, &s, &why));
    REQUIRE(why == Problem::SweepProfileTouchesAxis);
  }
  SECTION("a twist on a curved path") {
    ucs::Ucs xz;
    REQUIRE(ucs::FromNormal(Vec3{5, 0, 0}, Vec3{0, 1, 0}, &xz));
    brep::Profile pr;
    pr.plane = xz;
    for (const ucs::Point2D& q :
         {ucs::Point2D{-1, -1}, ucs::Point2D{1, -1}, ucs::Point2D{1, 1}, ucs::Point2D{-1, 1}})
      pr.vertices.push_back(ucs::PlaneToWorld(xz, q));
    pr.edges.assign(4, brep::ProfileEdge{});
    brep::SweepOptions opt;
    opt.twistRad = 0.3;
    REQUIRE_FALSE(brep::Sweep(pr, ArcPath(xz.origin, Vec3{0, 0, 0}, Vec3{0, 0, 1}, kPi / 2.0), opt, &s, &why));
    REQUIRE(why == Problem::SweepUnsupportedOption);
  }
  REQUIRE(s.faces.empty());
}

TEST_CASE("Sweep along a bulge polyline path is a bent pipe with the summed volume", "[brep][req315]") {
  Problem why = Problem::Ok;
  // A circle (r 1, area pi) along: line +X of length 10, a tangent quarter arc (radius 5) turning to
  // +Y, then line +Y of length 8. Volume = 18*pi (straight) + (pi/2)*5*pi (Pappus, the elbow).
  const brep::Profile circ = CircleProfile(World(), 1.0);

  brep::SweepPath path;
  path.points = {Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{15, 5, 0}, Vec3{15, 13, 0}};
  brep::SweepSegment arcSeg;
  arcSeg.arc = true;
  arcSeg.centre = Vec3{10, 5, 0};
  arcSeg.normal = Vec3{0, 0, 1};
  arcSeg.sweep = kPi / 2.0;
  path.segments = {brep::SweepSegment{}, arcSeg, brep::SweepSegment{}};

  Solid s;
  const bool ok = brep::Sweep(circ, path, brep::SweepOptions{}, &s, &why);
  INFO("why=" << brep::ProblemText(why));
  REQUIRE(ok);
  REQUIRE(brep::Validate(s) == Problem::Ok);
  REQUIRE(brep::EulerCharacteristic(s) == 2);
  REQUIRE_FALSE(brep::SelfIntersects(s));

  const double expected = 18.0 * kPi + (kPi / 2.0) * 5.0 * kPi;
  REQUIRE(brep::ComputeMassProperties(s).volume == Approx(expected).epsilon(1e-4));
}

TEST_CASE("Sweep refuses a mitred corner in the path by name", "[brep][req315]") {
  Problem why = Problem::Ok;
  const brep::Profile sq = PolyProfile(World(), {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}});
  brep::SweepPath path;
  path.points = {Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{10, 10, 0}};  // a 90-degree corner at the joint
  path.segments = {brep::SweepSegment{}, brep::SweepSegment{}};
  Solid s;
  REQUIRE_FALSE(brep::Sweep(sq, path, brep::SweepOptions{}, &s, &why));
  REQUIRE(why == Problem::SweepPathCorner);
  REQUIRE(s.faces.empty());
}

TEST_CASE("Sweep along collinear polyline segments equals the single-segment sweep", "[brep][req315]") {
  Problem why = Problem::Ok;
  const brep::Profile sq = PolyProfile(World(), {{-2, -2}, {2, -2}, {2, 2}, {-2, 2}});

  Solid one;
  REQUIRE(brep::Sweep(sq, LinePath(Vec3{0, 0, 0}, Vec3{0, 0, 12}), brep::SweepOptions{}, &one, &why));

  brep::SweepPath split;
  split.points = {Vec3{0, 0, 0}, Vec3{0, 0, 5}, Vec3{0, 0, 12}};
  split.segments = {brep::SweepSegment{}, brep::SweepSegment{}};
  Solid two;
  REQUIRE(brep::Sweep(sq, split, brep::SweepOptions{}, &two, &why));
  REQUIRE(brep::Validate(two) == Problem::Ok);

  REQUIRE(brep::ComputeMassProperties(two).volume ==
          Approx(brep::ComputeMassProperties(one).volume).epsilon(1e-9));
  REQUIRE(brep::ComputeMassProperties(two).volume == Approx(4.0 * 4.0 * 12.0).epsilon(1e-7));
}

// ---------------------------------------------------------------------------------------------
// REQ-314 increment 1, amended: a profile arc may curve INTO its loop.
//
// ADR-046 (d) recorded this as "a separate feature, now unblocked" the moment B2a gave `Surface`
// an `inward` flag for the wall of a Boolean bore. A reflex profile arc needs exactly that face and
// nothing else: after the builder's walk the loop runs CCW about the extrusion direction, so an arc
// with a positive sweep has its centre on the INTERIOR side and sweeps an ordinary outward cylinder,
// while a negative one has its centre outside and sweeps a face whose material is on the far side
// from its own axis.
//
// The figures are what make this a test rather than a demonstration: an annular sector has a volume
// and a surface area that can be written down, and both are wrong in a different way if the inner
// face's orientation is mishandled — the volume by twice the void, the area not at all.
// ---------------------------------------------------------------------------------------------
namespace {

/// A quarter annulus between \p rIn and \p rOut, in the first quadrant of \p plane.
brep::Profile QuarterAnnulus(const ucs::Ucs& plane, double rOut, double rIn) {
  brep::Profile pr;
  pr.plane = plane;
  pr.vertices = {ucs::UcsToWorld(plane, Vec3{rOut, 0.0, 0.0}),
                 ucs::UcsToWorld(plane, Vec3{0.0, rOut, 0.0}),
                 ucs::UcsToWorld(plane, Vec3{0.0, rIn, 0.0}),
                 ucs::UcsToWorld(plane, Vec3{rIn, 0.0, 0.0})};
  pr.edges.resize(4);
  pr.edges[0] = {true, plane.origin, kPi * 0.5};   // outer rim, curving away from the material
  pr.edges[1] = {false, Vec3{}, 0.0};              // the far end
  pr.edges[2] = {true, plane.origin, -kPi * 0.5};  // inner rim, curving INTO the material
  pr.edges[3] = {false, Vec3{}, 0.0};              // the near end
  return pr;
}

} // namespace

TEST_CASE("Extrude builds a profile arc that curves inward", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double rOut = 11.0;
  const double rIn = 9.0;
  const double h = 5.0;
  // A quarter of an annulus: (pi/4)(11^2 - 9^2) = 10pi of plan, 5 tall.
  const double wantVolume = 0.25 * kPi * (rOut * rOut - rIn * rIn) * h;
  // Outer and inner walls, two flat ends, and the two caps.
  const double wantArea = kPi * 0.5 * rOut * h + kPi * 0.5 * rIn * h +
                          2.0 * (rOut - rIn) * h + 2.0 * 0.25 * kPi * (rOut * rOut - rIn * rIn);

  Solid s;
  REQUIRE(brep::Extrude(QuarterAnnulus(World(), rOut, rIn), h, &s, &why));
  REQUIRE(brep::Validate(s) == Problem::Ok);

  const brep::MassProperties mp = brep::ComputeMassProperties(s);
  REQUIRE(mp.valid);
  REQUIRE(mp.volume == Approx(wantVolume).margin(1e-9));
  REQUIRE(mp.surfaceArea == Approx(wantArea).margin(1e-9));

  // Exactly one face looks inward: the concave wall. If the flag were set on both curved faces the
  // volume would come out as the DIFFERENCE of the two sectors' negatives and still be positive, so
  // this is asserted directly rather than inferred from the figures.
  std::size_t inward = 0;
  std::size_t cylinders = 0;
  for (const brep::Face& f : s.faces) {
    cylinders += f.surface.kind == brep::SurfaceKind::Cylinder ? 1u : 0u;
    inward += f.surface.inward ? 1u : 0u;
  }
  REQUIRE(cylinders == 2);
  REQUIRE(inward == 1);

  brep::Tessellation t;
  REQUIRE(brep::Tessellate(s, 0.0005, &t, &why));
  RequireWindingMatchesNormals(t);
  RequireBoundsContain(brep::ComputeBounds(s), t);
  REQUIRE(TessellatedVolume(t) == Approx(mp.volume).epsilon(0.005));
  REQUIRE(TessellatedArea(t) == Approx(mp.surfaceArea).epsilon(0.005));
}

TEST_CASE("A reflex extrude is the same solid whichever way it is described", "[brep][req314]") {
  Problem why = Problem::Ok;
  const double wantVolume = 0.25 * kPi * (11.0 * 11.0 - 9.0 * 9.0) * 5.0;

  // The builder normalises the walk, so a profile given the other way round, or extruded downward,
  // has to land on the same solid. That normalisation is where a reflex arc's sign could quietly be
  // read against the wrong direction, which is why it is asserted rather than assumed.
  brep::Profile fwd = QuarterAnnulus(World(), 11.0, 9.0);
  brep::Profile rev;
  rev.plane = fwd.plane;
  const std::size_t n = fwd.vertices.size();
  for (std::size_t i = 0; i < n; ++i) {
    rev.vertices.push_back(fwd.vertices[n - 1 - i]);
    brep::ProfileEdge e = fwd.edges[(2 * n - i - 2) % n];
    e.sweep = -e.sweep;
    rev.edges.push_back(e);
  }

  Solid a;
  Solid b;
  Solid down;
  REQUIRE(brep::Extrude(fwd, 5.0, &a, &why));
  REQUIRE(brep::Extrude(rev, 5.0, &b, &why));
  REQUIRE(brep::Extrude(fwd, -5.0, &down, &why));
  REQUIRE(brep::ComputeMassProperties(a).volume == Approx(wantVolume).margin(1e-9));
  REQUIRE(brep::ComputeMassProperties(b).volume == Approx(wantVolume).margin(1e-9));
  REQUIRE(brep::ComputeMassProperties(down).volume == Approx(wantVolume).margin(1e-9));
  REQUIRE(brep::ComputeMassProperties(b).surfaceArea ==
          Approx(brep::ComputeMassProperties(a).surfaceArea).margin(1e-9));

  // Downward puts the same shape on the other side of the plane, and nowhere else.
  const brep::Bounds bd = brep::ComputeBounds(down);
  REQUIRE(bd.mn.z == Approx(-5.0).margin(1e-9));
  REQUIRE(bd.mx.z == Approx(0.0).margin(1e-9));
}

TEST_CASE("A reflex extrude keeps its figures on a tilted frame at survey scale", "[brep][req314]") {
  Problem why = Problem::Ok;
  ucs::Ucs plane;
  REQUIRE(ucs::FromNormal(Vec3{3500000.0, 850000.0, 420.0},
                          ray3d::Normalize(Vec3{0.3, -0.4, 0.866}), &plane));
  Solid s;
  REQUIRE(brep::Extrude(QuarterAnnulus(plane, 11.0, 9.0), 5.0, &s, &why));
  REQUIRE(brep::Validate(s) == Problem::Ok);
  REQUIRE(brep::ComputeMassProperties(s).volume ==
          Approx(0.25 * kPi * (11.0 * 11.0 - 9.0 * 9.0) * 5.0).epsilon(1e-9));
}

TEST_CASE("A reflex arc can bite a bay out of a rectangle", "[brep][req314]") {
  Problem why = Problem::Ok;
  // The shape REQ-314 increment 1 used to name as the thing it could not build: a 10 x 6 rectangle
  // whose top edge is a half-circle bulging DOWN into it. Kept as the case rather than replaced,
  // because it is a deeper reflex than an annulus wall — the arc reaches most of the way across the
  // shape, so a mishandled orientation cannot hide in a thin sliver.
  brep::Profile pr;
  pr.plane = World();
  pr.vertices = {Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{10, 6, 0}, Vec3{0, 6, 0}};
  pr.edges.assign(4, brep::ProfileEdge{});
  pr.edges[2].arc = true;                  // the top edge, (10,6) -> (0,6)
  pr.edges[2].centre = Vec3{5, 6, 0};
  pr.edges[2].sweep = -kPi;                // bulges down through (5,1), into the rectangle

  Solid s;
  REQUIRE(brep::Extrude(pr, 3.0, &s, &why));
  REQUIRE(brep::Validate(s) == Problem::Ok);

  // 60 square feet of rectangle less a half-disc of radius 5, three feet tall.
  const double plan = 60.0 - 0.5 * kPi * 25.0;
  const brep::MassProperties mp = brep::ComputeMassProperties(s);
  REQUIRE(mp.volume == Approx(plan * 3.0).margin(1e-9));
  // Two caps, three straight walls totalling 22 feet of run, and a half-circle wall of 5pi.
  REQUIRE(mp.surfaceArea == Approx(2.0 * plan + 22.0 * 3.0 + kPi * 5.0 * 3.0).margin(1e-9));

  brep::Tessellation t;
  REQUIRE(brep::Tessellate(s, 0.0005, &t, &why));
  RequireWindingMatchesNormals(t);
  REQUIRE(TessellatedVolume(t) == Approx(mp.volume).epsilon(0.005));
}
