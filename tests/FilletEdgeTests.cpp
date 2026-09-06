// REQ-323 (D-2026-09-05-c, ADR-046 amendment (j), GitHub issue #148 acceptance 5) — FILLET: round
// an edge with a rolling ball.
//
// **Every number here is a closed form, written before the implementation.** A fillet of radius `r`
// on a 90-degree edge of length `L` removes a prism of cross-section `r^2 (1 - pi/4)` and adds a
// quarter-cylinder of lateral area `(pi r / 2) L`, while each adjacent face loses a strip `r L` and
// each end face loses a quarter-disc `pi r^2 / 4`. Those are the acceptance numbers REQ-323 states,
// and they are what these cases compare against — not a tolerance, and not whatever the code
// happens to produce.
//
// The wedge case is the one that matters most: its dihedral is not 90 degrees, so a fillet that
// simply cut `r` off each face would pass the box and fail here. That is the difference between the
// rolling-ball model and an approximation of it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "brep.hpp"

using Catch::Approx;
using brep::Problem;

namespace {

constexpr double kPi = 3.14159265358979323846;

ucs::Ucs World() { return ucs::Ucs{}; }

brep::Solid Box(double l, double w, double h) {
  brep::Solid s;
  Problem why{};
  REQUIRE(brep::MakeBox(World(), l, w, h, &s, &why));
  return s;
}

/// The index of the one straight edge running along \p dir whose midpoint is \p mid.
int EdgeAt(const brep::Solid& s, const ray3d::Vec3& mid) {
  for (std::size_t i = 0; i < s.edges.size(); ++i) {
    if (s.edges[i].kind != brep::CurveKind::Line)
      continue;
    const ray3d::Vec3 a = s.vertices[static_cast<std::size_t>(s.edges[i].v0)].p;
    const ray3d::Vec3 b = s.vertices[static_cast<std::size_t>(s.edges[i].v1)].p;
    const ray3d::Vec3 m{0.5 * (a.x + b.x), 0.5 * (a.y + b.y), 0.5 * (a.z + b.z)};
    if (ray3d::Length(ray3d::Sub(m, mid)) < 1e-9)
      return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

TEST_CASE("Fillet: a box edge, against the closed form", "[fillet][req323]") {
  // x in [-10,10], y in [-5,5], z in [0,8]. The top-back edge runs along X at y = 5, z = 8, so its
  // midpoint is (0,5,8) and its length is 20. The faces meeting it are the top (+Z) and back (+Y).
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int ei = EdgeAt(box, {0.0, 5.0, 8.0});
  REQUIRE(ei >= 0);

  brep::Solid out;
  Problem why{};
  const bool okFillet = brep::FilletEdge(box, ei, 2.0, &out, &why);
  // (the acceptance number below was CORRECTED by this test - see the note on the area)
  REQUIRE(okFillet);
  REQUIRE(why == Problem::Ok);
  REQUIRE(brep::Validate(out) == Problem::Ok);

  // REQ-323's acceptance, to the last digit the closed forms give.
  //
  // **The AREA here corrected the requirement.** REQ-323 was first written with `800 + 18*pi`, on
  // the reasoning that each end face loses a quarter-disc where the fillet passes. It does not: the
  // end face is the cross-section plane, so what it loses is the same cross-section the removed
  // prism has, `r^2 (1 - pi/4)` — the sliver BETWEEN the square corner and the arc, not the disc
  // inside the arc. Confirmed face by face (160 + 120 + 160 + 200 + 2*79.141593 + 20*pi) before the
  // requirement was changed, so the number below is derived twice by hand and once by the code.
  //
  // Writing the acceptance as arithmetic BEFORE the implementation is what made that visible at all;
  // a test written afterwards would have recorded whatever came out.
  const brep::MassProperties mp = brep::ComputeMassProperties(out);
  REQUIRE(mp.valid);
  CHECK(mp.volume == Approx(1520.0 + 20.0 * kPi).epsilon(1e-12));
  CHECK(mp.surfaceArea == Approx(792.0 + 22.0 * kPi).epsilon(1e-12));

  // The topology delta of ADR-046 amendment (j): V+2, E+3, F+1, Euler unchanged.
  CHECK(out.vertices.size() == 10);
  CHECK(out.edges.size() == 15);
  CHECK(out.faces.size() == 7);
  CHECK(brep::EulerCharacteristic(out) == brep::EulerCharacteristic(box));

  // Exactly one cylindrical face, of the radius asked for. A fillet that built the right volume out
  // of the wrong surface kind would pass the numbers above and be wrong for everything after.
  int cyl = 0;
  for (const brep::Face& f : out.faces)
    if (f.surface.kind == brep::SurfaceKind::Cylinder) {
      ++cyl;
      CHECK(f.surface.radius == Approx(2.0));
      CHECK(f.surface.height == Approx(20.0));
      CHECK(f.uEnd - f.uStart == Approx(kPi * 0.5));  // 90-degree edge -> quarter cylinder
    }
  CHECK(cyl == 1);

  // The recipe is dropped: a filleted box is not the box its recipe describes (REQ-323 item 9).
  CHECK(out.recipe.kind == brep::PrimitiveKind::None);
}

TEST_CASE("Fillet: the setback follows the dihedral, not the radius", "[fillet][req323]") {
  // A WEDGE, and this is the case that separates the rolling-ball model from "cut r off each face".
  // `MakeWedge` is full height at x = -L/2 falling to zero at x = +L/2, so its ridge runs along Y at
  // (x = -L/2, z = h) between the back face and the slant.
  //
  // In the XZ cross-section the ridge's two edges leave it along (0,-1) and (L,-h)/|.|, so the
  // interior dihedral is acos(h / sqrt(L^2 + h^2)) — for 20 x 8 that is 68.199 degrees, not 90 —
  // and the setback is r / tan(theta/2), which is 1.4758 r rather than r.
  brep::Solid wedge;
  Problem why{};
  REQUIRE(brep::MakeWedge(World(), 20.0, 10.0, 8.0, &wedge, &why));

  const int ei = EdgeAt(wedge, {-10.0, 0.0, 8.0});
  REQUIRE(ei >= 0);
  brep::Solid out;
  REQUIRE(brep::FilletEdge(wedge, ei, 2.0, &out, &why));
  REQUIRE(brep::Validate(out) == Problem::Ok);
  CHECK(out.faces.size() == wedge.faces.size() + 1);

  const double theta = std::acos(8.0 / std::sqrt(20.0 * 20.0 + 8.0 * 8.0));
  const double setback = 2.0 / std::tan(0.5 * theta);
  // tan(theta/2) = (1 - cos theta) / sin theta, and with cos theta = h/sqrt(L^2+h^2) and
  // sin theta = L/sqrt(L^2+h^2) that is (sqrt(L^2+h^2) - h) / L. So the setback has its own closed
  // form, 2*L*r / (sqrt(L^2+h^2) - h), which for r = 2, L = 20, h = 8 is 40/(sqrt(464) - 8).
  //
  // Written as the formula rather than as a decimal: a hand-typed 2.9516 here was wrong by 0.0025
  // and this case caught it, which is the same lesson the box's area taught two functions up.
  const double setbackClosed = 40.0 / (std::sqrt(464.0) - 8.0);
  CHECK(setback == Approx(setbackClosed).epsilon(1e-12));
  CHECK(setback == Approx(2.9540659229).margin(1e-9));  // and emphatically not 2.0

  // The tangent line on the BACK face (x = -10, outward -X) sits `setback` below the old ridge, so
  // its z is 8 - setback. Found by looking for a vertex there rather than by index, because the
  // compaction pass renumbers.
  bool found = false;
  for (const brep::Vertex& v : out.vertices)
    if (std::fabs(v.p.x + 10.0) < 1e-9 && std::fabs(v.p.z - (8.0 - setback)) < 1e-6)
      found = true;
  CHECK(found);
}

TEST_CASE("Fillet: a radius that does not fit is refused before anything is built",
          "[fillet][req323]") {
  // ADR-046 amendment (i): `SelfIntersects` is documented as not general, so this cannot be an
  // after-the-fact test. The box's back face is only 8 tall, so the setback (= r at 90 degrees) must
  // be strictly under 8.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int ei = EdgeAt(box, {0.0, 5.0, 8.0});
  REQUIRE(ei >= 0);
  brep::Solid out;
  Problem why{};

  CHECK(brep::FilletEdge(box, ei, 7.99, &out, &why));  // fits, barely

  for (const double r : {8.0, 8.5, 100.0}) {
    brep::Solid none;
    Problem w{};
    CHECK_FALSE(brep::FilletEdge(box, ei, r, &none, &w));
    // At the limit the face does not merely become thin, it vanishes — so equality is refused too.
    CHECK(w == Problem::FilletRadiusTooLarge);
    CHECK(none.faces.empty());  // untouched, not half-built
  }
  for (const double r : {0.0, -1.0}) {
    Problem w{};
    brep::Solid none;
    CHECK_FALSE(brep::FilletEdge(box, ei, r, &none, &w));
    CHECK(w == Problem::FilletRadiusNotPositive);
  }
}

TEST_CASE("Fillet: what it refuses, each by name", "[fillet][req323]") {
  brep::Solid cyl;
  Problem why{};
  REQUIRE(brep::MakeCylinder(World(), 5.0, 10.0, &cyl, &why));

  SECTION("a curved face beside the edge") {
    // A cylinder's vertical seam edge lies between two halves of the WALL: both faces curved.
    int seam = -1;
    for (std::size_t i = 0; i < cyl.edges.size(); ++i)
      if (cyl.edges[i].kind == brep::CurveKind::Line)
        seam = static_cast<int>(i);
    REQUIRE(seam >= 0);
    brep::Solid out;
    Problem w{};
    CHECK_FALSE(brep::FilletEdge(cyl, seam, 1.0, &out, &w));
    CHECK(w == Problem::FilletFaceNotPlanar);
  }

  SECTION("an arc edge") {
    int rim = -1;
    for (std::size_t i = 0; i < cyl.edges.size(); ++i)
      if (cyl.edges[i].kind == brep::CurveKind::Arc)
        rim = static_cast<int>(i);
    REQUIRE(rim >= 0);
    brep::Solid out;
    Problem w{};
    CHECK_FALSE(brep::FilletEdge(cyl, rim, 1.0, &out, &w));
    CHECK(w == Problem::FilletEdgeNotLine);
  }

  SECTION("an index that names nothing") {
    brep::Solid out;
    Problem w{};
    CHECK_FALSE(brep::FilletEdge(cyl, 9999, 1.0, &out, &w));
    CHECK(w == Problem::IndexOutOfRange);
  }
}

TEST_CASE("Fillet: two edges sharing a vertex are refused, and nothing is built",
          "[fillet][req323]") {
  // REQ-323 item 7. Their cylinders would arrive at the corner and leave a curved triangular gap
  // that needs a spherical patch trimmed against both — which is the whole distance between
  // "round an edge" and issue #148's "round a chain", and why that acceptance line is still open.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int topBack = EdgeAt(box, {0.0, 5.0, 8.0});     // along X at y=5, z=8
  const int topLeft = EdgeAt(box, {-10.0, 0.0, 8.0});   // along Y at x=-10, z=8 — shares a corner
  const int topFront = EdgeAt(box, {0.0, -5.0, 8.0});   // along X at y=-5, z=8 — shares nothing
  REQUIRE(topBack >= 0);
  REQUIRE(topLeft >= 0);
  REQUIRE(topFront >= 0);

  brep::Solid out;
  Problem why{};
  CHECK_FALSE(brep::FilletEdges(box, {topBack, topLeft}, 2.0, &out, &why));
  CHECK(why == Problem::FilletEdgesShareVertex);
  CHECK(out.faces.empty());

  // Two OPPOSITE top edges share no vertex, so they are independent and both are rounded in one
  // operation. Twice the volume comes off, and the topology delta applies twice.
  Problem ok{};
  REQUIRE(brep::FilletEdges(box, {topBack, topFront}, 2.0, &out, &ok));
  CHECK(brep::Validate(out) == Problem::Ok);
  CHECK(out.faces.size() == 8);
  CHECK(out.edges.size() == 18);
  CHECK(out.vertices.size() == 12);
  const brep::MassProperties mp = brep::ComputeMassProperties(out);
  CHECK(mp.volume == Approx(1600.0 - 2.0 * 20.0 * (4.0 - kPi)).epsilon(1e-12));
}

TEST_CASE("Fillet: a concave edge is refused, not silently built", "[fillet][req323]") {
  // REQ-323 item 6. A hand-built fixture, same style as the parallel-faces case above: convex vs.
  // concave is decided entirely by `dot(uA, nB)`, where `uA` is `cross(nA, dirA)` and `dirA` follows
  // the edge's traversal direction in face A's own loop. Face A here traverses the edge FORWARD
  // (`reversed = false`), which is the mirror image of a real convex corner's winding for this same
  // pair of face normals -- material occupies more than a quarter-turn, the opposite of a box corner.
  brep::Solid s;
  s.vertices.push_back(brep::Vertex{{0.0, 0.0, 0.0}});
  s.vertices.push_back(brep::Vertex{{10.0, 0.0, 0.0}});
  brep::Edge edge0;
  edge0.kind = brep::CurveKind::Line;
  edge0.v0 = 0;
  edge0.v1 = 1;
  s.edges.push_back(edge0);
  const int e0 = 0;

  brep::Face a;
  a.surface.kind = brep::SurfaceKind::Plane;
  a.surface.frame.zAxis = {0.0, 0.0, 1.0};
  a.loops.push_back(brep::Loop{{brep::EdgeUse{e0, false}}});
  s.faces.push_back(a);

  brep::Face b;
  b.surface.kind = brep::SurfaceKind::Plane;
  b.surface.frame.zAxis = {0.0, 1.0, 0.0};  // perpendicular to a, not parallel
  b.loops.push_back(brep::Loop{{brep::EdgeUse{e0, true}}});
  s.faces.push_back(b);

  brep::Solid out;
  Problem w{};
  CHECK_FALSE(brep::FilletEdge(s, e0, 1.0, &out, &w));
  CHECK(w == Problem::FilletEdgeConcave);
}

TEST_CASE("Fillet: two coplanar faces meeting an edge are refused, not divided by zero",
          "[fillet][req323]") {
  // A hand-built fixture rather than a primitive: no `Make*` shape in the kernel produces two
  // PARALLEL planar faces meeting a straight edge (that is a degenerate fold, not a solid corner),
  // so this pathological case is only reachable by constructing it directly — the way REQ-313's
  // `paramLoops` documents itself as "exercised only by hand-built test fixtures until" its consumer
  // exists. `tan(theta/2)` is undefined at theta = 0, which is exactly why this must be a pre-check.
  brep::Solid s;
  s.vertices.push_back(brep::Vertex{{0.0, 0.0, 0.0}});
  s.vertices.push_back(brep::Vertex{{10.0, 0.0, 0.0}});
  const int v0 = 0;
  const int v1 = 1;
  brep::Edge edge0;
  edge0.kind = brep::CurveKind::Line;
  edge0.v0 = v0;
  edge0.v1 = v1;
  s.edges.push_back(edge0);
  const int e0 = 0;

  brep::Face a;
  a.surface.kind = brep::SurfaceKind::Plane;
  a.surface.frame.zAxis = {0.0, 0.0, 1.0};
  a.loops.push_back(brep::Loop{{brep::EdgeUse{e0, false}}});
  s.faces.push_back(a);

  brep::Face b;
  b.surface.kind = brep::SurfaceKind::Plane;
  b.surface.frame.zAxis = {0.0, 0.0, 1.0};  // same normal as `a` -- coplanar/parallel, not a corner
  b.loops.push_back(brep::Loop{{brep::EdgeUse{e0, true}}});
  s.faces.push_back(b);

  brep::Solid out;
  Problem w{};
  CHECK_FALSE(brep::FilletEdge(s, e0, 1.0, &out, &w));
  CHECK(w == Problem::FilletFacesParallel);
}

TEST_CASE("Fillet: a vertex with more than three edges is refused, not guessed at",
          "[fillet][req323]") {
  // REQ-323's precondition assumes exactly one gap opens at each end when the edge is removed -- true
  // at an ordinary 3-edge solid vertex, but not at a higher-valence one. A hand-built fourth face
  // touching the box's own top-back-left vertex (reusing a real edge already there) manufactures that
  // vertex without needing a primitive that produces one.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int ei = EdgeAt(box, {0.0, 5.0, 8.0});
  REQUIRE(ei >= 0);
  const int v0 = box.edges[static_cast<std::size_t>(ei)].v0;

  int otherEdge = -1;
  for (std::size_t i = 0; i < box.edges.size(); ++i) {
    if (static_cast<int>(i) == ei)
      continue;
    if (box.edges[i].v0 == v0 || box.edges[i].v1 == v0) {
      otherEdge = static_cast<int>(i);
      break;
    }
  }
  REQUIRE(otherEdge >= 0);

  brep::Solid corrupt = box;
  brep::Face phantom;
  phantom.surface.kind = brep::SurfaceKind::Plane;
  phantom.surface.frame.zAxis = {0.0, 0.0, 1.0};
  phantom.loops.push_back(brep::Loop{{brep::EdgeUse{otherEdge, false}}});
  corrupt.faces.push_back(phantom);

  brep::Solid out;
  Problem w{};
  CHECK_FALSE(brep::FilletEdge(corrupt, ei, 2.0, &out, &w));
  CHECK(w == Problem::FilletVertexNotSimple);
}

TEST_CASE("Fillet: an oblique end face is refused, not fudged into a circle",
          "[fillet][req323]") {
  // REQ-323 item 5 / increment 4's boundary: a square PYRAMID's base rim sits between the (planar)
  // base and one (planar) slanted side face -- both meeting FilletEdge's own preconditions -- but at
  // each end of that rim, the ADJACENT side face is oblique to the rim rather than square to it, so
  // the fillet-to-face boundary there would be an ellipse, not the circle increment 1 builds.
  brep::Solid pyramid;
  Problem why{};
  REQUIRE(brep::MakePyramid(World(), 4, 5.0, 0.0, 6.0, &pyramid, &why));

  const ray3d::Vec3 mid{2.5, 2.5, 0.0};  // base rim from (5,0,0) to (0,5,0)
  const int ei = EdgeAt(pyramid, mid);
  REQUIRE(ei >= 0);

  brep::Solid out;
  Problem w{};
  CHECK_FALSE(brep::FilletEdge(pyramid, ei, 0.3, &out, &w));
  CHECK(w == Problem::FilletEndFaceUnsupported);
}

TEST_CASE("Fillet: a filleted solid is still a solid the next operation accepts",
          "[fillet][req323]") {
  // REQ-323's last acceptance bullet. A fillet that left a shape later operations refused would be
  // worse than one that refused up front.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int ei = EdgeAt(box, {0.0, 5.0, 8.0});
  brep::Solid filleted;
  Problem why{};
  REQUIRE(brep::FilletEdge(box, ei, 2.0, &filleted, &why));

  // The bottom face is still a plane with parallel neighbours, so push/pull still moves it.
  int bottom = -1;
  for (std::size_t i = 0; i < filleted.faces.size(); ++i) {
    const brep::Surface& sf = filleted.faces[i].surface;
    if (sf.kind == brep::SurfaceKind::Plane && sf.frame.zAxis.z < -0.9)
      bottom = static_cast<int>(i);
  }
  REQUIRE(bottom >= 0);
  brep::Solid pushed;
  Problem w{};
  REQUIRE(brep::PushPullFace(filleted, bottom, 1.0, &pushed, &w));
  CHECK(brep::Validate(pushed) == Problem::Ok);
}

TEST_CASE("Fillet: every refusal has its own message, not the generic solid-invalid one",
          "[fillet][req323]") {
  // REQ-323 item 5: "refused by name". `ProblemText` is the one place that name reaches a user, so a
  // `Fillet*` value with no case in that switch would fall through to "The solid is not valid." --
  // true of nothing this function refuses, and actively misleading for e.g. a concave edge or an
  // oversized radius, neither of which touches the solid at all.
  const std::vector<Problem> filletProblems = {
      Problem::FilletRadiusNotPositive,  Problem::FilletEdgeNotLine,
      Problem::FilletFaceNotPlanar,      Problem::FilletFacesParallel,
      Problem::FilletEdgeConcave,        Problem::FilletRadiusTooLarge,
      Problem::FilletEndFaceUnsupported, Problem::FilletVertexNotSimple,
      Problem::FilletEdgesShareVertex,   Problem::FilletResultInvalid,
  };
  for (const Problem p : filletProblems) {
    const std::string text = brep::ProblemText(p);
    CHECK(text != "The solid is not valid.");
    CHECK_FALSE(text.empty());
  }
}
