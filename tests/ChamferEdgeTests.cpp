// REQ-331 (D-2026-09-08-f, ADR-046 amendment (k), GitHub issue #148 acceptance 5) — CHAMFER:
// replace a sharp edge with a flat bevel.
//
// **Every number here is a closed form, written before the implementation**, which is the discipline
// REQ-323 established after two of ITS first-draft acceptance numbers turned out wrong. A chamfer of
// distance `d` on a 90-degree edge of length `L` removes a triangular prism of cross-section
// `d^2 / 2`, adds a bevel of area `L * d * sqrt(2)`, takes a strip `d * L` off each adjacent face,
// and takes the same `d^2 / 2` cross-section off each end face — because the end face IS the
// cross-section plane.
//
// Two cases carry more weight than the rest:
//
//   * the WEDGE, which proves REQ-331 item 2: the chamfer's setback is exactly `d` at a dihedral of
//     68.199 degrees, where the fillet's would be `r / tan(theta/2)` = 1.4758 r. The fillet has to
//     convert; the chamfer's input already IS the setback;
//   * ALL TWELVE edges, whose decomposition shares two constants with REQ-323's rounded box — the
//     `1120` inner-box-plus-slabs term and the `368` planar-face total both appear verbatim in
//     `1120 + 344*pi/3` and `368 + 120*pi`. Two independent operations landing on the same
//     constants is a cross-check neither could give alone.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "brep.hpp"

using Catch::Approx;
using brep::Problem;

namespace {

constexpr double kRoot2 = 1.41421356237309504880;

ucs::Ucs World() { return ucs::Ucs{}; }

brep::Solid Box(double l, double w, double h) {
  brep::Solid s;
  Problem why{};
  REQUIRE(brep::MakeBox(World(), l, w, h, &s, &why));
  return s;
}

/// The index of the one straight edge whose midpoint is \p mid.
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

TEST_CASE("Chamfer: a box edge, against the closed form", "[chamfer][req331]") {
  // x in [-10,10], y in [-5,5], z in [0,8]. The top-back edge runs along X at y = 5, z = 8, so its
  // midpoint is (0,5,8) and its length is 20. The faces meeting it are the top (+Z) and back (+Y).
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int ei = EdgeAt(box, {0.0, 5.0, 8.0});
  REQUIRE(ei >= 0);

  brep::Solid out;
  Problem why{};
  REQUIRE(brep::ChamferEdge(box, ei, 2.0, &out, &why));
  REQUIRE(why == Problem::Ok);
  REQUIRE(brep::Validate(out) == Problem::Ok);

  // REQ-331's acceptance, term by term so a failure names its own cause:
  //   volume  1600 - L*d^2/2                       = 1600 - 40             = 1560
  //   area    top 200->160, back 160->120, the two untouched faces 160 + 200,
  //           each end face losing d^2/2 = 2 so 78 + 78, and the bevel L*d*sqrt(2) = 40*sqrt(2)
  //                                                                     = 796 + 40*sqrt(2)
  const brep::MassProperties mp = brep::ComputeMassProperties(out);
  REQUIRE(mp.valid);
  CHECK(mp.volume == Approx(1560.0).epsilon(1e-12));
  CHECK(mp.surfaceArea == Approx(796.0 + 40.0 * kRoot2).epsilon(1e-12));
  CHECK(mp.surfaceArea == Approx(852.5685424949).margin(1e-9));

  // The topology delta is IDENTICAL to the fillet's — 8/12/6 -> 10/15/7 — which is the point of
  // ADR-046 amendment (k)(1). Only the KINDS differ: a Plane here where the fillet builds a
  // Cylinder, a Line where it builds an Arc.
  CHECK(out.vertices.size() == 10);
  CHECK(out.edges.size() == 15);
  CHECK(out.faces.size() == 7);
  CHECK(brep::EulerCharacteristic(out) == brep::EulerCharacteristic(box));

  // Every face is planar and every edge straight — nothing curved is introduced at all, which is
  // exactly what separates this from the fillet.
  for (const brep::Face& f : out.faces)
    CHECK(f.surface.kind == brep::SurfaceKind::Plane);
  for (const brep::Edge& e : out.edges)
    CHECK(e.kind == brep::CurveKind::Line);

  // The bevel's own normal bisects the two faces' outward normals: at a 90-degree edge that is
  // (0, 1, 1)/sqrt(2). Found by looking for the one face that is neither axis-aligned.
  int bevels = 0;
  for (const brep::Face& f : out.faces) {
    const ray3d::Vec3 n = ray3d::Normalize(f.surface.frame.zAxis);
    if (std::fabs(n.y - 1.0 / kRoot2) < 1e-9 && std::fabs(n.z - 1.0 / kRoot2) < 1e-9)
      ++bevels;
  }
  CHECK(bevels == 1);
}

TEST_CASE("Chamfer: the setback does NOT follow the dihedral", "[chamfer][req331]") {
  // The mirror image of REQ-323's wedge case, and REQ-331 item 2's proof. `MakeWedge` is full height
  // at x = -L/2 falling to zero at x = +L/2, so its ridge runs along Y at (x = -L/2, z = h) with an
  // interior dihedral of acos(h / sqrt(L^2 + h^2)) — 68.199 degrees for 20 x 8, not 90.
  //
  // A FILLET there sets back by r / tan(theta/2) = 2.954 for r = 2. A CHAMFER sets back by exactly
  // 2. The input IS the setback; there is nothing to convert.
  brep::Solid wedge;
  Problem why{};
  REQUIRE(brep::MakeWedge(World(), 20.0, 10.0, 8.0, &wedge, &why));

  const int ei = EdgeAt(wedge, {-10.0, 0.0, 8.0});
  REQUIRE(ei >= 0);
  brep::Solid out;
  REQUIRE(brep::ChamferEdge(wedge, ei, 2.0, &out, &why));
  REQUIRE(brep::Validate(out) == Problem::Ok);
  CHECK(out.faces.size() == wedge.faces.size() + 1);

  // The cut line on the BACK face (x = -10, outward -X) sits exactly `d` below the old ridge, so its
  // z is 8 - 2 = 6 — and emphatically not 8 - 2.954.
  bool atSix = false;
  bool atFilletSetback = false;
  const double filletSetback = 40.0 / (std::sqrt(464.0) - 8.0);
  for (const brep::Vertex& v : out.vertices) {
    if (std::fabs(v.p.x + 10.0) > 1e-9)
      continue;
    if (std::fabs(v.p.z - 6.0) < 1e-9)
      atSix = true;
    if (std::fabs(v.p.z - (8.0 - filletSetback)) < 1e-6)
      atFilletSetback = true;
  }
  CHECK(atSix);
  CHECK_FALSE(atFilletSetback);

  // The bevel's WIDTH is what moves with the angle instead: d * sqrt(2 - 2 cos theta), which is
  // d*sqrt(2) at 90 degrees and 2.2425 here.
  const double theta = std::acos(8.0 / std::sqrt(20.0 * 20.0 + 8.0 * 8.0));
  const double width = 2.0 * std::sqrt(2.0 - 2.0 * std::cos(theta));
  CHECK(width == Approx(2.2425140537).margin(1e-9));
  const brep::MassProperties mp = brep::ComputeMassProperties(out);
  REQUIRE(mp.valid);
  // The bevel is `width` x 10 (the wedge's Y extent), and the prism removed has cross-section
  // d^2 * sin(theta) / 2 — the triangle between the two cut points and the ridge.
  const double wedgeVol = 0.5 * 20.0 * 8.0 * 10.0;
  CHECK(mp.volume ==
        Approx(wedgeVol - 0.5 * 2.0 * 2.0 * std::sin(theta) * 10.0).epsilon(1e-12));
}

TEST_CASE("Chamfer: a distance that does not fit is refused before anything is built",
          "[chamfer][req331]") {
  // ADR-046 amendment (i): the operation brings its own precondition, because `Validate` sees
  // topology and not geometry. The box's back face is only 8 tall, so `d` must be strictly under 8.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int ei = EdgeAt(box, {0.0, 5.0, 8.0});
  REQUIRE(ei >= 0);
  brep::Solid out;
  Problem why{};

  CHECK(brep::ChamferEdge(box, ei, 7.99, &out, &why));  // fits, barely

  for (const double d : {8.0, 8.5, 100.0}) {
    brep::Solid none;
    Problem w{};
    CHECK_FALSE(brep::ChamferEdge(box, ei, d, &none, &w));
    // At the limit the face does not merely become thin, it vanishes — so equality is refused too.
    CHECK(w == Problem::ChamferDistanceTooLarge);
    CHECK(none.faces.empty());  // untouched, not half-built
  }
  for (const double d : {0.0, -1.0}) {
    brep::Solid none;
    Problem w{};
    CHECK_FALSE(brep::ChamferEdge(box, ei, d, &none, &w));
    CHECK(w == Problem::ChamferDistanceNotPositive);
  }
  {
    brep::Solid none;
    Problem w{};
    CHECK_FALSE(brep::ChamferEdge(box, ei, std::nan(""), &none, &w));
    CHECK(w == Problem::NonFiniteParameter);
  }
}

TEST_CASE("Chamfer: what it refuses, each by name", "[chamfer][req331]") {
  brep::Solid cyl;
  Problem why{};
  REQUIRE(brep::MakeCylinder(World(), 5.0, 10.0, &cyl, &why));

  SECTION("a curved face beside the edge") {
    int seam = -1;
    for (std::size_t i = 0; i < cyl.edges.size(); ++i)
      if (cyl.edges[i].kind == brep::CurveKind::Line)
        seam = static_cast<int>(i);
    REQUIRE(seam >= 0);
    brep::Solid out;
    Problem w{};
    CHECK_FALSE(brep::ChamferEdge(cyl, seam, 1.0, &out, &w));
    CHECK(w == Problem::ChamferFaceNotPlanar);
  }

  SECTION("an arc edge") {
    int rim = -1;
    for (std::size_t i = 0; i < cyl.edges.size(); ++i)
      if (cyl.edges[i].kind == brep::CurveKind::Arc)
        rim = static_cast<int>(i);
    REQUIRE(rim >= 0);
    brep::Solid out;
    Problem w{};
    CHECK_FALSE(brep::ChamferEdge(cyl, rim, 1.0, &out, &w));
    CHECK(w == Problem::ChamferEdgeNotLine);
  }

  SECTION("an index that names nothing") {
    brep::Solid out;
    Problem w{};
    CHECK_FALSE(brep::ChamferEdge(cyl, 9999, 1.0, &out, &w));
    CHECK(w == Problem::IndexOutOfRange);
  }

  SECTION("the same edge twice") {
    const brep::Solid box = Box(20.0, 10.0, 8.0);
    const int ei = EdgeAt(box, {0.0, 5.0, 8.0});
    brep::Solid out;
    Problem w{};
    CHECK_FALSE(brep::ChamferEdges(box, {ei, ei}, 2.0, &out, &w));
    CHECK(w == Problem::IndexOutOfRange);
  }
}

TEST_CASE("Chamfer: two coplanar faces meeting an edge are refused", "[chamfer][req331]") {
  // A hand-built fixture, as REQ-323's equivalent needs: no `Make*` shape produces two PARALLEL
  // planar faces meeting a straight edge, because that is a degenerate fold rather than a corner.
  // Here it is the bisector that fails rather than a tangent — `uA + uB` is the zero vector when the
  // two in-face perpendiculars are opposed — so there is no bevel plane to build.
  brep::Solid s;
  s.vertices.push_back(brep::Vertex{{0.0, 0.0, 0.0}});
  s.vertices.push_back(brep::Vertex{{10.0, 0.0, 0.0}});
  brep::Edge edge0;
  edge0.kind = brep::CurveKind::Line;
  edge0.v0 = 0;
  edge0.v1 = 1;
  s.edges.push_back(edge0);

  brep::Face a;
  a.surface.kind = brep::SurfaceKind::Plane;
  a.surface.frame.zAxis = {0.0, 0.0, 1.0};
  a.loops.push_back(brep::Loop{{brep::EdgeUse{0, false}}});
  s.faces.push_back(a);

  brep::Face b;
  b.surface.kind = brep::SurfaceKind::Plane;
  b.surface.frame.zAxis = {0.0, 0.0, 1.0};  // same normal as `a` — coplanar, not a corner
  b.loops.push_back(brep::Loop{{brep::EdgeUse{0, true}}});
  s.faces.push_back(b);

  brep::Solid out;
  Problem w{};
  CHECK_FALSE(brep::ChamferEdge(s, 0, 1.0, &out, &w));
  CHECK(w == Problem::ChamferFacesParallel);
}

TEST_CASE("Chamfer: a concave edge is refused, not silently built", "[chamfer][req331]") {
  // REQ-331 item 6, inheriting REQ-323 item 6's reasoning unchanged: a bevel on a crease ADDS
  // material rather than removing it, so the face is `inward`, the adjacent faces grow, and the
  // limit on `d` comes from the far side of the crease. Convex vs. concave is decided entirely by
  // `dot(uA, nB)`, so the fixture only has to wind face A's use of the edge the other way round.
  brep::Solid s;
  s.vertices.push_back(brep::Vertex{{0.0, 0.0, 0.0}});
  s.vertices.push_back(brep::Vertex{{10.0, 0.0, 0.0}});
  brep::Edge edge0;
  edge0.kind = brep::CurveKind::Line;
  edge0.v0 = 0;
  edge0.v1 = 1;
  s.edges.push_back(edge0);

  brep::Face a;
  a.surface.kind = brep::SurfaceKind::Plane;
  a.surface.frame.zAxis = {0.0, 0.0, 1.0};
  a.loops.push_back(brep::Loop{{brep::EdgeUse{0, false}}});
  s.faces.push_back(a);

  brep::Face b;
  b.surface.kind = brep::SurfaceKind::Plane;
  b.surface.frame.zAxis = {0.0, 1.0, 0.0};
  b.loops.push_back(brep::Loop{{brep::EdgeUse{0, true}}});
  s.faces.push_back(b);

  brep::Solid out;
  Problem w{};
  CHECK_FALSE(brep::ChamferEdge(s, 0, 1.0, &out, &w));
  CHECK(w == Problem::ChamferEdgeConcave);
}

TEST_CASE("Chamfer: a vertex with more than three edges is refused, not guessed at",
          "[chamfer][req331]") {
  // The construction assumes exactly one gap opens at each end when the edge is removed — true at an
  // ordinary 3-edge solid vertex and not at a higher-valence one. A hand-built fourth face touching
  // the box's own top-back-left vertex manufactures that without needing a primitive that has one.
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
  CHECK_FALSE(brep::ChamferEdge(corrupt, ei, 2.0, &out, &w));
  CHECK(w == Problem::ChamferVertexNotSimple);
}

TEST_CASE("Chamfer: an oblique end face is refused, and the FIRST answer was wrong",
          "[chamfer][req331]") {
  // REQ-331 item 10 / ADR-046 amendment (k)(5). This case exists because the requirement was
  // initially expected to ACCEPT an oblique end face: a plane cuts a plane in a straight line at any
  // angle, where the fillet's cylinder gives an ellipse and has to be refused. Working the
  // construction showed otherwise — the cut point `p + d*u` lies ON the end face only while that
  // face is square to the edge, since `u` is perpendicular to the edge direction and so the point
  // keeps its position along it. Off-square, the cut lines would have to be trimmed to where the
  // bevel plane crosses the end face instead, which is a second construction.
  //
  // A square PYRAMID's base rim: both adjacent faces (the base and one slanted side) are planar and
  // pass every other precondition, but at each end of that rim the ADJACENT side face is oblique.
  brep::Solid pyramid;
  Problem why{};
  REQUIRE(brep::MakePyramid(World(), 4, 5.0, 0.0, 6.0, &pyramid, &why));

  const int ei = EdgeAt(pyramid, {2.5, 2.5, 0.0});  // base rim from (5,0,0) to (0,5,0)
  REQUIRE(ei >= 0);

  brep::Solid out;
  Problem w{};
  CHECK_FALSE(brep::ChamferEdge(pyramid, ei, 0.3, &out, &w));
  CHECK(w == Problem::ChamferEndFaceUnsupported);
  CHECK(out.faces.empty());
}

TEST_CASE("Chamfer: a chamfered solid is still a solid the next operation accepts",
          "[chamfer][req331]") {
  // REQ-331's `PRESSPULL` acceptance bullet. A chamfer that left a shape later operations refused
  // would be worse than one that refused up front.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int ei = EdgeAt(box, {0.0, 5.0, 8.0});
  brep::Solid chamfered;
  Problem why{};
  REQUIRE(brep::ChamferEdge(box, ei, 2.0, &chamfered, &why));

  int bottom = -1;
  for (std::size_t i = 0; i < chamfered.faces.size(); ++i) {
    const brep::Surface& sf = chamfered.faces[i].surface;
    if (sf.kind == brep::SurfaceKind::Plane && sf.frame.zAxis.z < -0.9)
      bottom = static_cast<int>(i);
  }
  REQUIRE(bottom >= 0);
  brep::Solid pushed;
  Problem w{};
  REQUIRE(brep::PushPullFace(chamfered, bottom, 1.0, &pushed, &w));
  CHECK(brep::Validate(pushed) == Problem::Ok);
}

TEST_CASE("Chamfer: every refusal has its own message, not the generic solid-invalid one",
          "[chamfer][req331]") {
  // REQ-331 item 5: "refused by name". `ProblemText` is the one place that name reaches a user, so a
  // `Chamfer*` value with no case in that switch would fall through to "The solid is not valid." —
  // actively misleading for e.g. an oversized distance, which does not touch the solid at all.
  const std::vector<Problem> chamferProblems = {
      Problem::ChamferDistanceNotPositive, Problem::ChamferEdgeNotLine,
      Problem::ChamferFaceNotPlanar,       Problem::ChamferFacesParallel,
      Problem::ChamferEdgeConcave,         Problem::ChamferDistanceTooLarge,
      Problem::ChamferEndFaceUnsupported,  Problem::ChamferVertexNotSimple,
      Problem::ChamferCornerPartial,       Problem::ChamferCornerNotOrthogonal,
      Problem::ChamferResultInvalid,
  };
  for (const Problem p : chamferProblems) {
    const std::string text = brep::ProblemText(p);
    CHECK(text != "The solid is not valid.");
    CHECK_FALSE(text.empty());
    // And distinct from the fillet's, because "radius" and "distance" are not the same word to a
    // user — the reason these are separate enum values at all.
    CHECK(text != std::string(brep::ProblemText(Problem::FilletRadiusNotPositive)));
  }
}

// --- REQ-331 increment 2: edge chains, and the corner that is a POINT ------------------------------
//
// The closed forms for a fully chamfered box, each term separately checkable. With `a = L-2d`,
// `b = W-2d`, `c = H-2d`:
//
//   V = a*b*c                        the inner box                        384
//     + 2d(ab + ac + bc)             six slabs                            736
//     + (d^2/2)(4a + 4b + 4c)        twelve triangular prisms             208
//     + 8 * d^3/4                    eight corner pieces                   16
//                                                                        ----
//                                                                        1344
//
//   A = 2(ab + ac + bc)              six rectangles                       368
//     + sum over edges of            twelve hexagons: the full rectangle
//       sqrt(2)(d*len - 3d^2/2)      minus a V-notch at each end       232*sqrt(2)
//
// **The first two volume terms sum to 1120 and the planar-face total is 368 — the identical
// constants in REQ-323's rounded box `1120 + 344*pi/3` and `368 + 120*pi`.** The fillet and the
// chamfer share their inner box and their slabs exactly and differ only in what fills the twelve
// edge channels and the eight corners, so two independently derived acceptances cross-check each
// other. Neither could do that alone.

TEST_CASE("Chamfer: all twelve edges of a box, against the bevelled-box closed forms",
          "[chamfer][req331]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  std::vector<int> all;
  for (std::size_t i = 0; i < box.edges.size(); ++i)
    all.push_back(static_cast<int>(i));
  REQUIRE(all.size() == 12);

  brep::Solid out;
  Problem why{};
  REQUIRE(brep::ChamferEdges(box, all, 2.0, &out, &why));
  REQUIRE(brep::Validate(out) == Problem::Ok);

  const brep::MassProperties mp = brep::ComputeMassProperties(out);
  REQUIRE(mp.valid);
  CHECK(mp.volume == Approx(1344.0).epsilon(1e-12));
  CHECK(mp.surfaceArea == Approx(368.0 + 232.0 * kRoot2).epsilon(1e-12));
  CHECK(mp.surfaceArea == Approx(696.0975464706).margin(1e-9));

  // The inner box + slabs term the fillet shares, spelled out so the cross-check is visible rather
  // than merely asserted: 16*6*4 + 4*(96 + 64 + 24) = 384 + 736.
  CHECK(384.0 + 736.0 == Approx(1120.0));

  // 6 rectangles + 12 hexagons, and NO corner face — the whole difference from the fillet's 26.
  // 8 corners x 3 cut vertices + 8 corner points = 32; 12 x 2 cut lines + 8 x 3 spokes = 48.
  // Euler holds: 32 - 48 + 18 = 2.
  CHECK(out.faces.size() == 18);
  CHECK(out.vertices.size() == 32);
  CHECK(out.edges.size() == 48);
  CHECK(brep::EulerCharacteristic(out) == brep::EulerCharacteristic(box));

  for (const brep::Face& f : out.faces)
    CHECK(f.surface.kind == brep::SurfaceKind::Plane);
  for (const brep::Edge& e : out.edges)
    CHECK(e.kind == brep::CurveKind::Line);

  // Six four-sided rectangles and twelve six-sided hexagons. The hexagon is the V-notch made
  // visible: a bevel whose ends are both corners loses a wedge to each neighbour.
  int quads = 0;
  int hexes = 0;
  for (const brep::Face& f : out.faces) {
    REQUIRE(f.loops.size() == 1);
    if (f.loops[0].uses.size() == 4)
      ++quads;
    else if (f.loops[0].uses.size() == 6)
      ++hexes;
  }
  CHECK(quads == 6);
  CHECK(hexes == 12);

  // Every vertex has degree 3, which is what `2E = sum of degrees` reduces to here: 96 = 32 * 3.
  std::vector<int> degree(out.vertices.size(), 0);
  for (const brep::Edge& e : out.edges) {
    ++degree[static_cast<std::size_t>(e.v0)];
    ++degree[static_cast<std::size_t>(e.v1)];
  }
  for (const int dg : degree)
    CHECK(dg == 3);
}

TEST_CASE("Chamfer: three edges at one corner meet at a POINT, not a patch", "[chamfer][req331]") {
  // ADR-046 amendment (k)(3), and the sharpest divergence from the fillet. Three bevel planes in
  // general position meet at exactly one point; three cylinders do not, which is precisely why
  // `FilletEdges` has to close its corner with a spherical octant. So this corner adds one vertex
  // and three edges and NO face.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int e1 = EdgeAt(box, {0.0, 5.0, 8.0});     // along X at y=5, z=8
  const int e2 = EdgeAt(box, {-10.0, 0.0, 8.0});   // along Y at x=-10, z=8
  const int e3 = EdgeAt(box, {-10.0, 5.0, 4.0});   // along Z at x=-10, y=5
  REQUIRE(e1 >= 0);
  REQUIRE(e2 >= 0);
  REQUIRE(e3 >= 0);
  // They all meet at (-10, 5, 8).
  brep::Solid out;
  Problem why{};
  REQUIRE(brep::ChamferEdges(box, {e1, e2, e3}, 2.0, &out, &why));
  REQUIRE(brep::Validate(out) == Problem::Ok);

  // Six originals + three bevels. The fillet's equivalent has ten, the tenth being the patch.
  CHECK(out.faces.size() == 9);

  // The meeting point: the three bevel planes are x = -8 + ... — concretely, at the corner
  // (-10, 5, 8) the three planes are (y-5)+(z-8) = -2 rotated into each pair, and they concur at
  // the corner offset by d/2 along each inward face normal: (-9, 4, 7).
  bool meetPoint = false;
  for (const brep::Vertex& v : out.vertices)
    if (ray3d::Length(ray3d::Sub(v.p, ray3d::Vec3{-9.0, 4.0, 7.0})) < 1e-9)
      meetPoint = true;
  CHECK(meetPoint);

  // Exactly one such point, and it has degree 3 — one spoke into each of the three faces.
  int atMeet = 0;
  for (std::size_t i = 0; i < out.vertices.size(); ++i) {
    if (ray3d::Length(ray3d::Sub(out.vertices[i].p, ray3d::Vec3{-9.0, 4.0, 7.0})) > 1e-9)
      continue;
    ++atMeet;
    int deg = 0;
    for (const brep::Edge& e : out.edges)
      if (e.v0 == static_cast<int>(i) || e.v1 == static_cast<int>(i))
        ++deg;
    CHECK(deg == 3);
  }
  CHECK(atMeet == 1);
}

TEST_CASE("Chamfer: two opposite edges share no corner and are done in one pass",
          "[chamfer][req331]") {
  // The independent-edges case: twice the volume comes off and the topology delta applies twice.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int topBack = EdgeAt(box, {0.0, 5.0, 8.0});
  const int topFront = EdgeAt(box, {0.0, -5.0, 8.0});
  REQUIRE(topBack >= 0);
  REQUIRE(topFront >= 0);

  brep::Solid out;
  Problem why{};
  REQUIRE(brep::ChamferEdges(box, {topBack, topFront}, 2.0, &out, &why));
  CHECK(brep::Validate(out) == Problem::Ok);
  CHECK(out.faces.size() == 8);
  CHECK(out.edges.size() == 18);
  CHECK(out.vertices.size() == 12);
  const brep::MassProperties mp = brep::ComputeMassProperties(out);
  REQUIRE(mp.valid);
  CHECK(mp.volume == Approx(1600.0 - 2.0 * 40.0).epsilon(1e-12));
}

TEST_CASE("Chamfer: a corner with only some of its edges selected is refused",
          "[chamfer][req331]") {
  // REQ-331 item 8. The bevel would have to run off onto an edge staying sharp — a setback blend,
  // and its own construction. Same boundary the fillet draws, for the same reason.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int e1 = EdgeAt(box, {0.0, 5.0, 8.0});
  const int e2 = EdgeAt(box, {-10.0, 0.0, 8.0});
  brep::Solid out;
  Problem why{};
  CHECK_FALSE(brep::ChamferEdges(box, {e1, e2}, 2.0, &out, &why));
  CHECK(why == Problem::ChamferCornerPartial);
  CHECK(out.faces.empty());
}

TEST_CASE("Chamfer: a corner whose faces are not square to each other is refused",
          "[chamfer][req331]") {
  // REQ-331 item 9 / amendment (k)(4) — refused, but NOT for the fillet's reason. There is no patch
  // here to parametrise; three bevel planes still meet at a point. What fails is the CUT VERTEX:
  // the meet of two bevels' cut lines inside a face they share is `p + d*u1 + d*u2` only while `u1`
  // and `u2` are perpendicular, which is what mutual orthogonality of the three faces buys.
  brep::Solid wedge;
  Problem why{};
  REQUIRE(brep::MakeWedge(World(), 20.0, 10.0, 8.0, &wedge, &why));
  const int ridge = EdgeAt(wedge, {-10.0, 0.0, 8.0});
  REQUIRE(ridge >= 0);
  const int rv = wedge.edges[static_cast<std::size_t>(ridge)].v0;
  std::vector<int> atCorner;
  for (std::size_t i = 0; i < wedge.edges.size(); ++i)
    if (wedge.edges[i].v0 == rv || wedge.edges[i].v1 == rv)
      atCorner.push_back(static_cast<int>(i));
  REQUIRE(atCorner.size() == 3);
  brep::Solid out;
  Problem w{};
  CHECK_FALSE(brep::ChamferEdges(wedge, atCorner, 1.0, &out, &w));
  CHECK(w == Problem::ChamferCornerNotOrthogonal);
  CHECK(out.faces.empty());
}

// --- REQ-331 item 4 as amended (D-2026-09-08-g, ADR-046 amendment (l)) ----------------------------
//
// The precondition was measured ONE EDGE AT A TIME against the ORIGINAL solid, so it could not see
// two requested bevels running into each other. `CHAMFER 6` on the two 20-long top edges of a
// 20 x 10 x 8 box — 10 apart across a 10-wide face, so the two setbacks total 12 — was ACCEPTED and
// returned a self-intersecting solid reporting volume 880. Found by `/code-review high` on this
// task; the same defect was in REQ-323's fillet, which this precondition was inherited from.

TEST_CASE("Chamfer: two bevels that would run into each other are refused", "[chamfer][req331]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int back = EdgeAt(box, {0.0, 5.0, 8.0});
  const int front = EdgeAt(box, {0.0, -5.0, 8.0});
  REQUIRE(back >= 0);
  REQUIRE(front >= 0);

  for (const double d : {5.0, 6.0, 7.5}) {
    brep::Solid out;
    Problem why{};
    CHECK_FALSE(brep::ChamferEdges(box, {back, front}, d, &out, &why));
    // Its OWN name, not `ChamferDistanceTooLarge`: nothing is wrong with either edge on its own.
    CHECK(why == Problem::ChamferDistanceOverlapsAnother);
    CHECK(out.faces.empty());
    brep::Solid alone;
    Problem w{};
    CHECK(brep::ChamferEdge(box, back, d, &alone, &w));
  }

  // The largest distance that DOES fit still lands on the closed form — two prisms of `L d^2 / 2`.
  brep::Solid ok;
  Problem why{};
  REQUIRE(brep::ChamferEdges(box, {back, front}, 4.9, &ok, &why));
  CHECK(brep::Validate(ok) == Problem::Ok);
  const brep::MassProperties mp = brep::ComputeMassProperties(ok);
  REQUIRE(mp.valid);
  CHECK(mp.volume == Approx(1600.0 - 20.0 * 4.9 * 4.9).epsilon(1e-12));
  CHECK(mp.volume == Approx(1119.8).margin(1e-9));
}

TEST_CASE("Chamfer: an edge too short for the corners at both its ends is refused",
          "[chamfer][req331]") {
  // A vertical edge is 8 long, and a corner takes `d` off its length at each end. At d = 5 the two
  // corners want 10 of the 8 available.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const std::vector<int> five = {
      EdgeAt(box, {-10.0, 5.0, 4.0}),
      EdgeAt(box, {0.0, 5.0, 8.0}),   EdgeAt(box, {-10.0, 0.0, 8.0}),
      EdgeAt(box, {0.0, 5.0, 0.0}),   EdgeAt(box, {-10.0, 0.0, 0.0}),
  };
  for (const int e : five)
    REQUIRE(e >= 0);

  brep::Solid out;
  Problem why{};
  CHECK_FALSE(brep::ChamferEdges(box, five, 5.0, &out, &why));
  CHECK(why == Problem::ChamferEdgeTooShortForItsCorners);
  CHECK(out.faces.empty());

  brep::Solid ok;
  Problem w{};
  REQUIRE(brep::ChamferEdges(box, five, 3.0, &ok, &w));
  CHECK(brep::Validate(ok) == Problem::Ok);
}

TEST_CASE("Chamfer: all twelve edges have a real upper bound now", "[chamfer][req331]") {
  // `d = 4` is the exact limit for a box 8 in its shortest dimension, and `d = 2` — the case the
  // bevelled-box acceptance uses — is unaffected, which is why this runs beside it.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  std::vector<int> all;
  for (std::size_t i = 0; i < box.edges.size(); ++i)
    all.push_back(static_cast<int>(i));

  for (const double d : {4.0, 5.0}) {
    brep::Solid out;
    Problem why{};
    CHECK_FALSE(brep::ChamferEdges(box, all, d, &out, &why));
    CHECK(why == Problem::ChamferEdgeTooShortForItsCorners);
    CHECK(out.faces.empty());
  }

  brep::Solid ok;
  Problem w{};
  REQUIRE(brep::ChamferEdges(box, all, 3.9, &ok, &w));
  CHECK(brep::Validate(ok) == Problem::Ok);
}

TEST_CASE("Chamfer: the two new refusals have their own messages", "[chamfer][req331]") {
  for (const Problem p :
       {Problem::ChamferDistanceOverlapsAnother, Problem::ChamferEdgeTooShortForItsCorners}) {
    const std::string text = brep::ProblemText(p);
    CHECK(text != "The solid is not valid.");
    CHECK(text != std::string(brep::ProblemText(Problem::ChamferDistanceTooLarge)));
    CHECK(text != std::string(brep::ProblemText(Problem::FilletRadiusOverlapsAnother)));
  }
}
