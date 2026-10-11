// REQ-351 / D-2026-09-28-f, GitHub issue #564 section 4 — mirroring a solid.
//
// A reflection turns a right-hand glove into a left-hand one. Applied naively to a B-rep it leaves
// every frame LEFT-handed and every loop running the wrong way round its outward normal, so the
// solid is inside-out: its volume integrates negative and Validate, the Booleans and export all
// reject it. `brep::Mirror` reflects and then restores the orientation, and these cases pin both
// halves:
//
//   * a reflection is an ISOMETRY, so volume and area are unchanged to the last digit the closed
//     forms give — on flat faces, every curved kind, a bore (an inward face) and a NURBS loft;
//   * every frame is still right-handed and orthonormal, and every vertex is exactly the reflected
//     point, and mirroring twice restores the original;
//   * the result is a working solid — it validates and takes part in a Boolean;
//   * the recipe it keeps describes the geometry it now has.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "util/brep.hpp"
#include "util/nurbs.hpp"

using Catch::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;

ucs::Ucs World() { return ucs::Ucs{}; }

ucs::Ucs PlaneAt(double z) {
  ucs::Ucs u;
  u.origin = {0.0, 0.0, z};
  return u;
}

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

brep::MassProperties Mass(const brep::Solid& s) {
  const brep::MassProperties m = brep::ComputeMassProperties(s);
  REQUIRE(m.valid);
  return m;
}

brep::Solid Mirrored(const brep::Solid& s, const ray3d::Vec3& planePoint, const ray3d::Vec3& planeUnit) {
  brep::Solid out;
  brep::Problem why{};
  REQUIRE(brep::Mirror(s, planePoint, planeUnit, &out, &why));
  REQUIRE(why == brep::Problem::Ok);
  return out;
}

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

/// Every vertex of \p a has a vertex of \p b within \p tol, and the counts agree — order-free, for
/// comparing a solid against one rebuilt from its recipe.
bool SameVertexSet(const brep::Solid& a, const brep::Solid& b, double tol) {
  if (a.vertices.size() != b.vertices.size())
    return false;
  for (const brep::Vertex& va : a.vertices) {
    bool found = false;
    for (const brep::Vertex& vb : b.vertices)
      found = found || ray3d::Length(ray3d::Sub(va.p, vb.p)) < tol;
    if (!found)
      return false;
  }
  return true;
}

// A plane that is tilted and off the origin, so no coordinate happens to survive by symmetry.
const ray3d::Vec3 kPlanePoint{3.0, -2.0, 1.5};
const ray3d::Vec3 kPlaneUnit = ray3d::Normalize({1.0, 2.0, -0.5});

void RequireIsometricMirror(const brep::Solid& s) {
  const brep::Solid m = Mirrored(s, kPlanePoint, kPlaneUnit);
  REQUIRE(brep::Validate(m) == brep::Problem::Ok);
  const brep::MassProperties before = Mass(s);
  const brep::MassProperties after = Mass(m);
  REQUIRE(after.volume > 0.0);  // not inside-out
  REQUIRE(after.volume == Approx(before.volume).epsilon(1e-9));
  REQUIRE(after.surfaceArea == Approx(before.surfaceArea).epsilon(1e-9));
  for (const ucs::Ucs& f : AllFrames(m))
    REQUIRE(ucs::IsRightHandedOrthonormal(f));
  for (size_t i = 0; i < s.vertices.size(); ++i) {
    const ray3d::Vec3 want = ray3d::ReflectPointAcrossPlane(s.vertices[i].p, kPlanePoint, kPlaneUnit);
    REQUIRE(ray3d::Length(ray3d::Sub(m.vertices[i].p, want)) < 1e-9);
  }
  // What the viewport draws must face outward too: the tessellation's signed volume is positive and
  // matches, and every triangle winds the way the normals at its corners point.
  brep::Tessellation t;
  brep::Problem why{};
  REQUIRE(brep::Tessellate(m, 0.001, &t, &why));
  REQUIRE(t.vertsXyz.size() == t.normalsXyz.size());
  const auto vtx = [&](std::uint32_t i, const std::vector<double>& a) {
    return ray3d::Vec3{a[i * 3], a[i * 3 + 1], a[i * 3 + 2]};
  };
  double signedVol = 0.0;
  size_t againstNormals = 0;
  for (size_t k = 0; k + 2 < t.indices.size(); k += 3) {
    const ray3d::Vec3 a = vtx(t.indices[k], t.vertsXyz);
    const ray3d::Vec3 b = vtx(t.indices[k + 1], t.vertsXyz);
    const ray3d::Vec3 c = vtx(t.indices[k + 2], t.vertsXyz);
    const ray3d::Vec3 n = ray3d::Cross(ray3d::Sub(b, a), ray3d::Sub(c, a));
    signedVol += ray3d::Dot(a, ray3d::Cross(b, c)) / 6.0;
    const ray3d::Vec3 vn = ray3d::Add(ray3d::Add(vtx(t.indices[k], t.normalsXyz), vtx(t.indices[k + 1], t.normalsXyz)),
                                      vtx(t.indices[k + 2], t.normalsXyz));
    if (ray3d::Length(n) > 1e-12 && ray3d::Dot(n, vn) < 0.0)
      ++againstNormals;
  }
  REQUIRE(signedVol == Approx(before.volume).epsilon(0.005));
  REQUIRE(againstNormals == 0);

  // Twice is the identity — the sharpest check that every frame was reached by the right rule.
  const brep::Solid back = Mirrored(m, kPlanePoint, kPlaneUnit);
  REQUIRE(MaxVertexGap(back, s) < 1e-9);
  REQUIRE(Mass(back).volume == Approx(before.volume).epsilon(1e-9));
}

}  // namespace

TEST_CASE("Mirroring a flat-faced solid is an isometry with right-handed frames (REQ-351)", "[mirror]") {
  brep::Solid s;
  brep::Problem why{};
  SECTION("box") {
    REQUIRE(brep::MakeBox(World(), 20.0, 10.0, 8.0, &s, &why));
    RequireIsometricMirror(s);
  }
  SECTION("wedge — not symmetric about the mirror, so a vertex that merely moved would show") {
    REQUIRE(brep::MakeWedge(World(), 20.0, 10.0, 8.0, &s, &why));
    RequireIsometricMirror(s);
  }
  SECTION("pyramid") {
    REQUIRE(brep::MakePyramid(World(), 5, 6.0, 0.0, 9.0, &s, &why));
    RequireIsometricMirror(s);
  }
}

TEST_CASE("Mirroring a curved solid is an isometry (REQ-351)", "[mirror]") {
  brep::Solid s;
  brep::Problem why{};
  SECTION("cylinder") {
    REQUIRE(brep::MakeCylinder(World(), 4.0, 10.0, &s, &why));
    RequireIsometricMirror(s);
  }
  SECTION("cone") {
    REQUIRE(brep::MakeCone(World(), 5.0, 2.0, 7.0, &s, &why));
    RequireIsometricMirror(s);
  }
  SECTION("sphere") {
    REQUIRE(brep::MakeSphere(World(), 6.0, &s, &why));
    RequireIsometricMirror(s);
  }
  SECTION("torus") {
    REQUIRE(brep::MakeTorus(World(), 8.0, 2.0, &s, &why));
    RequireIsometricMirror(s);
  }
  SECTION("a bored box — an INWARD cylinder face must stay a void") {
    brep::Solid box, bar;
    REQUIRE(brep::MakeBox(World(), 20.0, 20.0, 10.0, &box, &why));
    ucs::Ucs low = PlaneAt(-5.0);
    REQUIRE(brep::MakeCylinder(low, 3.0, 20.0, &bar, &why));
    std::vector<brep::Solid> cut;
    REQUIRE(brep::BooleanSubtract(box, bar, &cut, &why));
    REQUIRE(cut.size() == 1);
    bool sawInward = false;
    for (const brep::Face& f : cut[0].faces)
      sawInward = sawInward || f.surface.inward;
    REQUIRE(sawInward);
    RequireIsometricMirror(cut[0]);
  }
  SECTION("a loft — NURBS faces") {
    REQUIRE(brep::Loft({CircleProfile(World(), 5.0), CircleProfile(PlaneAt(4.0), 8.0),
                        CircleProfile(PlaneAt(11.0), 3.5)},
                       &s, &why));
    bool sawNurbs = false;
    for (const brep::Face& f : s.faces)
      sawNurbs = sawNurbs || f.surface.kind == brep::SurfaceKind::Nurbs;
    REQUIRE(sawNurbs);
    RequireIsometricMirror(s);
  }
}

TEST_CASE("A mirrored solid is a working solid: it takes part in a Boolean (REQ-351)", "[mirror]") {
  // x in [-10, 10] mirrored across x = 5 is x in [0, 20]; the union spans [-10, 20].
  brep::Solid box;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(World(), 20.0, 10.0, 8.0, &box, &why));
  const brep::Solid m = Mirrored(box, {5.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
  std::vector<brep::Solid> u;
  REQUIRE(brep::BooleanUnion(box, m, &u, &why));
  REQUIRE(u.size() == 1);
  REQUIRE(Mass(u[0]).volume == Approx(30.0 * 10.0 * 8.0));
}

TEST_CASE("A mirrored primitive keeps a recipe that describes it (REQ-351)", "[mirror]") {
  brep::Problem why{};
  SECTION("wedge: rebuilding from the mirrored recipe gives the mirrored corners") {
    brep::Solid w;
    REQUIRE(brep::MakeWedge(World(), 20.0, 10.0, 8.0, &w, &why));
    const brep::Solid m = Mirrored(w, kPlanePoint, kPlaneUnit);
    REQUIRE(m.recipe.kind == brep::PrimitiveKind::Wedge);
    brep::Solid rebuilt;
    REQUIRE(brep::MakeWedge(m.recipe.frame, m.recipe.length, m.recipe.width, m.recipe.height, &rebuilt, &why));
    REQUIRE(SameVertexSet(m, rebuilt, 1e-9));
  }
  SECTION("pyramid") {
    brep::Solid p;
    REQUIRE(brep::MakePyramid(World(), 5, 6.0, 2.0, 9.0, &p, &why));
    const brep::Solid m = Mirrored(p, kPlanePoint, kPlaneUnit);
    brep::Solid rebuilt;
    REQUIRE(brep::MakePyramid(m.recipe.frame, m.recipe.sides, m.recipe.radius, m.recipe.radius2,
                              m.recipe.height, &rebuilt, &why));
    REQUIRE(SameVertexSet(m, rebuilt, 1e-9));
  }
  SECTION("polysolid: the path's y and sweep negate and Left becomes Right") {
    brep::Path path;
    path.start = {0.0, 0.0};
    path.segs = {{{10.0, 0.0}, 0.0}, {{10.0, 10.0}, kPi / 2.0}};
    brep::Solid ps;
    REQUIRE(brep::MakePolysolid(World(), path, 1.0, 3.0, brep::Justify::Left, &ps, &why));
    const brep::Solid m = Mirrored(ps, kPlanePoint, kPlaneUnit);
    REQUIRE(m.recipe.justify == brep::Justify::Right);
    brep::Solid rebuilt;
    REQUIRE(brep::MakePolysolid(m.recipe.frame, m.recipe.path, 1.0, 3.0, m.recipe.justify, &rebuilt, &why));
    REQUIRE(SameVertexSet(m, rebuilt, 1e-9));
  }
}

TEST_CASE("nurbs::Mirror reflects every point and keeps the normal outward (REQ-351)", "[mirror]") {
  const nurbs::Patch p = nurbs::RuledLinear({{0, 0, 0}, {10, 0, 0}, {20, 0, 5}},
                                            {{0, 10, 0}, {10, 10, 3}, {20, 10, 5}});
  REQUIRE(nurbs::IsValidPatch(p));
  const nurbs::Patch m = nurbs::Mirror(p, kPlanePoint, kPlaneUnit);
  REQUIRE(nurbs::IsValidPatch(m));
  const double a = p.knotsU[static_cast<size_t>(p.degU)];
  const double b = p.knotsU[static_cast<size_t>(p.nu)];
  for (double u = 0.0; u <= 1.0001; u += 0.25) {
    for (double v = 0.0; v <= 1.0001; v += 0.25) {
      const nurbs::SurfacePoint orig = nurbs::EvaluateWithDerivs(p, u, v);
      const nurbs::SurfacePoint got = nurbs::EvaluateWithDerivs(m, a + b - u, v);
      const ray3d::Vec3 wantP = ray3d::ReflectPointAcrossPlane(orig.p, kPlanePoint, kPlaneUnit);
      const ray3d::Vec3 wantN = ray3d::ReflectVectorAcrossPlane(orig.normal, kPlaneUnit);
      REQUIRE(ray3d::Length(ray3d::Sub(got.p, wantP)) < 1e-12);
      REQUIRE(ray3d::Length(ray3d::Sub(got.normal, wantN)) < 1e-9);
    }
  }
}

TEST_CASE("Mirror refuses a plane with no usable normal, by name (REQ-351 / REQ-201)", "[mirror][req201]") {
  brep::Solid box;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(World(), 20.0, 10.0, 8.0, &box, &why));
  brep::Solid out;
  REQUIRE_FALSE(brep::Mirror(box, {0, 0, 0}, {0, 0, 0}, &out, &why));
  REQUIRE(why == brep::Problem::MirrorPlaneNotUnit);
  REQUIRE_FALSE(brep::Mirror(box, {0, 0, 0}, {2, 0, 0}, &out, &why));
  REQUIRE(why == brep::Problem::MirrorPlaneNotUnit);
  REQUIRE_FALSE(brep::Mirror(box, {std::nan(""), 0, 0}, {1, 0, 0}, &out, &why));
  REQUIRE(why == brep::Problem::NonFiniteParameter);
  for (const brep::Problem p : {brep::Problem::MirrorPlaneNotUnit, brep::Problem::MirrorResultInvalid}) {
    const char* text = brep::ProblemText(p);
    REQUIRE(text != nullptr);
    REQUIRE(std::string(text) != "The solid is not valid.");
  }
}
