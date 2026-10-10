// REQ-333 / ADR-046 amendment (n), GitHub issue #148 acceptance 3 — moving a solid's VERTEX or EDGE.
//
// The interesting part is not the arithmetic, it is what the operation had to be DEFINED as. Moving
// a box corner and leaving everything else alone is not representable: the three quads meeting there
// would each end up with four non-coplanar points, and `SurfaceKind::Plane` has nowhere to put that.
// So the move is expressed the only way a plane can move while staying a plane — each adjacent face
// slides along its own normal by `dot(delta, n)` — and every affected corner is then re-solved.
//
// That definition is exact rather than approximate, which is what the first case pins: each offset
// plane reads `dot(n, x) = d + dot(n, delta)`, and `p + delta` satisfies all three identically, so
// the dragged vertex lands precisely where it was asked to. The box also stays a box: 8/12/6, every
// face still planar, and a volume with a closed form.
//
// The edge cases carry the claim that reads like a limitation and is not: the component of the drag
// ALONG the edge is annihilated, because the edge direction lies in both faces and so is
// perpendicular to both normals. It should be. An edge slid along its own line is the same edge.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "util/brep.hpp"

using Catch::Approx;

namespace {

ucs::Ucs World() { return ucs::Ucs{}; }

brep::Solid Box(double l, double w, double h) {
  brep::Solid s;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(World(), l, w, h, &s, &why));
  return s;
}

double Volume(const brep::Solid& s) {
  const brep::MassProperties m = brep::ComputeMassProperties(s);
  REQUIRE(m.valid);
  return m.volume;
}

/// The vertex nearest \p p. By COORDINATE and not by a hard-coded index, for the reason
/// `PushPullTests` picks its faces by direction: these cases are about the geometry, and a change to
/// the order `MakeBox` emits its topology in should not silently make them assert something else.
int VertexAt(const brep::Solid& s, const ray3d::Vec3& p) {
  int best = -1;
  double bestD = 1e30;
  for (size_t i = 0; i < s.vertices.size(); ++i) {
    const double d = ray3d::Length(ray3d::Sub(s.vertices[i].p, p));
    if (d < bestD) {
      bestD = d;
      best = static_cast<int>(i);
    }
  }
  REQUIRE(best >= 0);
  REQUIRE(bestD < 1e-9);
  return best;
}

/// The straight edge whose two endpoints are \p a and \p b, in either order.
int EdgeBetween(const brep::Solid& s, const ray3d::Vec3& a, const ray3d::Vec3& b) {
  const int va = VertexAt(s, a);
  const int vb = VertexAt(s, b);
  for (size_t i = 0; i < s.edges.size(); ++i) {
    const brep::Edge& e = s.edges[i];
    if (e.kind != brep::CurveKind::Line)
      continue;
    if ((e.v0 == va && e.v1 == vb) || (e.v0 == vb && e.v1 == va))
      return static_cast<int>(i);
  }
  REQUIRE(false);  // the caller named an edge the solid does not have
  return -1;
}

bool AllFacesPlanar(const brep::Solid& s) {
  for (const brep::Face& f : s.faces)
    if (f.surface.kind != brep::SurfaceKind::Plane)
      return false;
  return true;
}

}  // namespace

TEST_CASE("Moving a box corner lands it exactly there, and the box stays a box (REQ-333)",
          "[movesub]") {
  // 20 x 10 x 8 centred on (0,0) with its base at z = 0: x in [-10,10], y in [-5,5], z in [0,8].
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  REQUIRE(Volume(box) == Approx(1600.0));
  const ray3d::Vec3 corner{10.0, 5.0, 8.0};
  const int v = VertexAt(box, corner);

  brep::Solid out;
  brep::Problem why{};
  const ray3d::Vec3 delta{2.0, 1.0, 1.0};
  const bool ok = brep::MoveVertex(box, v, delta, &out, &why);
  CAPTURE(brep::ProblemText(why));  // so a regression names its own reason rather than just "false"
  REQUIRE(ok);
  REQUIRE(why == brep::Problem::Ok);

  // THE claim: the dragged vertex is exactly where it was asked to go, not near it.
  const int vOut = VertexAt(out, ray3d::Add(corner, delta));
  CHECK(out.vertices[static_cast<size_t>(vOut)].p.x == Approx(12.0));
  CHECK(out.vertices[static_cast<size_t>(vOut)].p.y == Approx(6.0));
  CHECK(out.vertices[static_cast<size_t>(vOut)].p.z == Approx(9.0));

  // The three planes it slid — +X, +Y and +Z — carried their whole faces with them, so the box is
  // now 22 x 11 x 9. That is the operation being honest: the faces moved, and their other corners
  // came along.
  CHECK(Volume(out) == Approx(22.0 * 11.0 * 9.0));
  CHECK(out.vertices.size() == 8);
  CHECK(out.edges.size() == 12);
  CHECK(out.faces.size() == 6);
  CHECK(AllFacesPlanar(out));
  CHECK(brep::Validate(out) == brep::Problem::Ok);

  // Dropped, not quietly updated: this is no longer the box the recipe describes (REQ-319 item 9).
  // The opposite answer from `Rotate` and `Scale`, which keep it — because those still can.
  CHECK(box.recipe.kind == brep::PrimitiveKind::Box);
  CHECK(out.recipe.kind == brep::PrimitiveKind::None);
}

TEST_CASE("Moving a box corner inward shrinks it by the same rule (REQ-333)", "[movesub]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int v = VertexAt(box, {10.0, 5.0, 8.0});
  brep::Solid out;
  brep::Problem why{};
  // Negative on every axis: the planes slide inward, and the solid shrinks to 17 x 8 x 6.
  REQUIRE(brep::MoveVertex(box, v, {-3.0, -2.0, -2.0}, &out, &why));
  CHECK(Volume(out) == Approx(17.0 * 8.0 * 6.0));
  CHECK(brep::Validate(out) == brep::Problem::Ok);
}

TEST_CASE("Moving a box EDGE moves the two faces along it (REQ-333)", "[movesub]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  // The top edge running along X at y = 5, z = 8. Its faces are +Y and +Z.
  const int e = EdgeBetween(box, {-10.0, 5.0, 8.0}, {10.0, 5.0, 8.0});

  brep::Solid out;
  brep::Problem why{};
  REQUIRE(brep::MoveEdge(box, e, {0.0, 3.0, 2.0}, &out, &why));
  REQUIRE(why == brep::Problem::Ok);
  // +Y slides to y = 8 and +Z to z = 10, so the box is 20 x 13 x 10.
  CHECK(Volume(out) == Approx(20.0 * 13.0 * 10.0));
  CHECK(out.vertices.size() == 8);
  CHECK(out.faces.size() == 6);
  CHECK(AllFacesPlanar(out));
  CHECK(brep::Validate(out) == brep::Problem::Ok);
}

TEST_CASE("The along-the-edge part of a drag is a no-op by geometry, not a shortfall (REQ-333)",
          "[movesub]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int e = EdgeBetween(box, {-10.0, 5.0, 8.0}, {10.0, 5.0, 8.0});  // runs along X
  brep::Problem why{};

  // Purely along the edge: the direction lies in both faces, so it is perpendicular to both normals
  // and offsets neither. An edge slid along its own line IS the same edge, so this expresses no
  // motion at all and is refused as such rather than reported as a move that did nothing.
  brep::Solid nowhere;
  REQUIRE_FALSE(brep::MoveEdge(box, e, {5.0, 0.0, 0.0}, &nowhere, &why));
  CHECK(why == brep::Problem::MoveSubObjectNoMotion);

  // ...and a diagonal drag gives exactly what its perpendicular component alone gives. Same claim,
  // stated as an equality rather than as a refusal.
  brep::Solid diagonal;
  brep::Solid perpendicular;
  REQUIRE(brep::MoveEdge(box, e, {5.0, 3.0, 2.0}, &diagonal, &why));
  REQUIRE(brep::MoveEdge(box, e, {0.0, 3.0, 2.0}, &perpendicular, &why));
  REQUIRE(diagonal.vertices.size() == perpendicular.vertices.size());
  for (size_t i = 0; i < diagonal.vertices.size(); ++i)
    CHECK(ray3d::Length(ray3d::Sub(diagonal.vertices[i].p, perpendicular.vertices[i].p)) < 1e-12);
}

TEST_CASE("A WEDGE proves the solve is general, not a box special case (REQ-333)", "[movesub]") {
  // A wedge's ramp is not axis-aligned, so the three planes at a top corner are not mutually
  // perpendicular and the corner is genuinely a three-plane solve rather than three independent
  // coordinates. 20 x 10 x 8: full height at x = -10, falling to zero at x = +10, volume 800.
  brep::Solid wedge;
  brep::Problem why{};
  REQUIRE(brep::MakeWedge(World(), 20.0, 10.0, 8.0, &wedge, &why));
  REQUIRE(Volume(wedge) == Approx(800.0));

  const ray3d::Vec3 corner{-10.0, 5.0, 8.0};  // on the ramp, the back face and the side
  const int v = VertexAt(wedge, corner);
  const ray3d::Vec3 delta{-2.0, 1.5, 1.0};
  brep::Solid out;
  REQUIRE(brep::MoveVertex(wedge, v, delta, &out, &why));

  // Exactly where it was asked to go, on geometry where "offset three planes" and "move one point"
  // are not the same arithmetic.
  const int vOut = VertexAt(out, ray3d::Add(corner, delta));
  CHECK(ray3d::Length(ray3d::Sub(out.vertices[static_cast<size_t>(vOut)].p,
                                 ray3d::Add(corner, delta))) < 1e-9);
  CHECK(AllFacesPlanar(out));
  CHECK(brep::Validate(out) == brep::Problem::Ok);
}

TEST_CASE("Moving a vertex or an edge refuses what it cannot do, by name (REQ-333 / REQ-201)",
          "[movesub][req201]") {
  brep::Problem why{};
  brep::Solid out;

  SECTION("a PYRAMID's apex — four planes meet, so the corner would have to split") {
    brep::Solid pyr;
    REQUIRE(brep::MakePyramid(World(), 4, 10.0, 0.0, 12.0, &pyr, &why));
    // The apex is the highest vertex.
    int apex = 0;
    for (size_t i = 1; i < pyr.vertices.size(); ++i)
      if (pyr.vertices[i].p.z > pyr.vertices[static_cast<size_t>(apex)].p.z)
        apex = static_cast<int>(i);
    REQUIRE_FALSE(brep::MoveVertex(pyr, apex, {1.0, 1.0, 1.0}, &out, &why));
    CHECK(why == brep::Problem::MoveVertexNotThreePlanes);
  }

  SECTION("a CYLINDER's rim — a curved face meets there") {
    brep::Solid cyl;
    REQUIRE(brep::MakeCylinder(World(), 5.0, 10.0, &cyl, &why));
    REQUIRE_FALSE(brep::MoveVertex(cyl, 0, {1.0, 0.0, 0.0}, &out, &why));
    CHECK((why == brep::Problem::MoveSubObjectNeighbourCurved ||
           why == brep::Problem::MoveVertexNotThreePlanes));
  }

  SECTION("a zero or non-finite drag") {
    const brep::Solid box = Box(20.0, 10.0, 8.0);
    const int v = VertexAt(box, {10.0, 5.0, 8.0});
    REQUIRE_FALSE(brep::MoveVertex(box, v, {0.0, 0.0, 0.0}, &out, &why));
    CHECK(why == brep::Problem::MoveSubObjectNoMotion);
    REQUIRE_FALSE(brep::MoveVertex(box, v, {std::nan(""), 0.0, 0.0}, &out, &why));
    CHECK(why == brep::Problem::MoveSubObjectNoMotion);
  }

  SECTION("an index that is not there") {
    const brep::Solid box = Box(20.0, 10.0, 8.0);
    REQUIRE_FALSE(brep::MoveVertex(box, 999, {1.0, 0.0, 0.0}, &out, &why));
    CHECK(why == brep::Problem::IndexOutOfRange);
    REQUIRE_FALSE(brep::MoveEdge(box, -1, {1.0, 0.0, 0.0}, &out, &why));
    CHECK(why == brep::Problem::IndexOutOfRange);
  }

  // Every refusal leaves the caller's solid untouched — nothing is half-applied, because the kernel
  // computes into a fresh solid and validates before returning (ADR-046 (d)).
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  CHECK(Volume(box) == Approx(1600.0));
}

TEST_CASE("Every new refusal has a sentence a user can read (REQ-333 / REQ-201)", "[movesub]") {
  for (const brep::Problem p :
       {brep::Problem::MoveVertexNotThreePlanes, brep::Problem::MoveEdgeFacesParallel,
        brep::Problem::MoveSubObjectNeighbourCurved, brep::Problem::MoveSubObjectNoMotion,
        brep::Problem::MoveSubObjectCornerUnsolvable, brep::Problem::MoveSubObjectResultInvalid}) {
    const char* text = brep::ProblemText(p);
    REQUIRE(text != nullptr);
    REQUIRE(std::string(text) != "The solid is not valid.");  // i.e. it did not fall through
  }
}
