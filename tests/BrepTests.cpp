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

  // A tube as fat as the ring would pass through its own axis — the one way a primitive here can
  // self-intersect, which is why it is caught at construction rather than left to a later check.
  REQUIRE_FALSE(brep::MakeTorus(World(), 4.0, 4.0, &s, &why));
  REQUIRE(why == Problem::MinorRadiusNotBelowMajor);
  REQUIRE_FALSE(brep::MakeTorus(World(), 4.0, 9.0, &s, &why));
  REQUIRE(why == Problem::MinorRadiusNotBelowMajor);

  REQUIRE_FALSE(brep::MakePyramid(World(), 2, 5.0, 0.0, 5.0, &s, &why));
  REQUIRE(why == Problem::SideCountOutOfRange);
  REQUIRE_FALSE(brep::MakePyramid(World(), brep::kMaxPyramidSides + 1, 5.0, 0.0, 5.0, &s, &why));
  REQUIRE(why == Problem::SideCountOutOfRange);

  // Every reason has to be sayable, or the command layer has nothing to print (REQ-201).
  REQUIRE(std::string(brep::ProblemText(Problem::MinorRadiusNotBelowMajor)).size() > 0);
  REQUIRE(std::string(brep::ProblemText(Problem::NotClosed)).size() > 0);
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
