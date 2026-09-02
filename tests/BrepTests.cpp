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
  SECTION("an inward-curving (reflex) arc") {
    // A rectangle whose top edge is an arc bulging DOWN into the rectangle. The face it would sweep
    // has its outward normal pointing toward the cylinder axis, which Surface cannot express.
    brep::Profile pr;
    pr.plane = World();
    pr.vertices = {Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{10, 6, 0}, Vec3{0, 6, 0}};
    pr.edges.assign(4, brep::ProfileEdge{});
    pr.edges[2].arc = true;                 // the top edge, (10,6) -> (0,6)
    pr.edges[2].centre = Vec3{5, 6, 0};
    pr.edges[2].sweep = -kPi;               // bulges down through (5,1), into the rectangle
    REQUIRE_FALSE(brep::Extrude(pr, 3.0, &s, &why));
    REQUIRE(why == Problem::ProfileArcReflex);
  }
}
