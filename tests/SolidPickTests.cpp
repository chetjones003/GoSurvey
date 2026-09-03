// Sub-object picking: which face, edge or vertex is the cursor over? (REQ-318 / ADR-049, #148.)
//
// These are the tests that let Phase 5's selection be trusted without a window. Three things they
// exist to pin, in order of how expensive the mistake would be:
//
//   1. **The returned point is on the analytic geometry, not on a tessellation chord.** A face pick
//      that reports the chord is wrong by the sagitta - small, plausible, and invisible in a
//      screenshot. The cylinder cases below assert the radius to 1e-9, which a chord answer misses
//      by ~0.01 ft.
//   2. **Precedence: vertex, then edge, then face.** Every vertex lies on an edge and every edge on
//      a face, so nearest-wins alone would make a vertex unpickable.
//   3. **A miss returns nothing.** No coordinate is better than a plausible wrong one (REQ-201).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "util/solidpick.hpp"

using Catch::Approx;
using ray3d::Ray;
using ray3d::Vec3;

namespace {

constexpr double kPi = 3.14159265358979323846;
// The tolerance solids are actually tessellated at (cadsolid.hpp kSolidChordToleranceFt).
constexpr double kChordTol = 0.01;

/// A solid plus the display triangles a pick consumes, built the way the real display cache builds
/// them: brep::Tessellate, then narrowed to float and expanded to nine floats per triangle.
struct Displayed {
  brep::Solid solid;
  std::vector<float> triVerts;
  std::vector<int> triFaceIds;
};

Displayed Display(const brep::Solid& s) {
  Displayed d;
  d.solid = s;
  brep::Tessellation t;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::Tessellate(s, kChordTol, &t, &why));
  const int tris = t.triangleCount();
  d.triFaceIds = t.triFace;
  d.triVerts.reserve(static_cast<std::size_t>(tris) * 9);
  for (int i = 0; i < tris; ++i) {
    for (int k = 0; k < 3; ++k) {
      const std::uint32_t idx = t.indices[static_cast<std::size_t>(i) * 3 + k];
      for (int c = 0; c < 3; ++c)
        d.triVerts.push_back(static_cast<float>(t.vertsXyz[static_cast<std::size_t>(idx) * 3 + c]));
    }
  }
  return d;
}

brep::Solid Box(double l, double w, double h, const Vec3& at = Vec3{0, 0, 0}) {
  ucs::Ucs f;
  f.origin = at;
  brep::Solid s;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(f, l, w, h, &s, &why));
  return s;
}

brep::Solid Cylinder(double r, double h, const Vec3& at = Vec3{0, 0, 0}) {
  ucs::Ucs f;
  f.origin = at;
  brep::Solid s;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeCylinder(f, r, h, &s, &why));
  return s;
}

/// A ray straight down at (x,y) from well above - a plan-view cursor.
Ray Down(double x, double y, double z = 500.0) { return Ray{{x, y, z}, {0.0, 0.0, -1.0}}; }

solidpick::Tolerance NoSnap() { return solidpick::Tolerance{0.0, 0.0}; }

}  // namespace

// ---------------------------------------------------------------------------
// Faces
// ---------------------------------------------------------------------------

TEST_CASE("A click on a box's top face picks that face", "[solidpick]") {
  const Displayed d = Display(Box(10, 6, 4));
  solidpick::Pick p;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(1.0, 1.0), NoSnap(), &p));
  REQUIRE(p.kind == solidpick::Kind::Face);
  REQUIRE(p.index >= 0);
  // MakeBox centres the box on its frame origin in X/Y and rises to +Z, so the top is at z = 4.
  REQUIRE(p.point.z == Approx(4.0));
  REQUIRE(d.solid.faces[static_cast<std::size_t>(p.index)].surface.kind == brep::SurfaceKind::Plane);
}

TEST_CASE("The nearest face wins, not the far side of the solid", "[solidpick]") {
  // A ray down through a box hits the top and the bottom. The pick must be the top.
  const Displayed d = Display(Box(10, 6, 4));
  solidpick::Pick p;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(0.0, 0.0), NoSnap(), &p));
  REQUIRE(p.point.z == Approx(4.0));  // top, not 0.0

  // And from below, the bottom.
  solidpick::Pick q;
  const Ray up{{0.0, 0.0, -500.0}, {0.0, 0.0, 1.0}};
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, up, NoSnap(), &q));
  REQUIRE(q.point.z == Approx(0.0));
}

TEST_CASE("A face pick on a cylinder lands on the surface and not on a chord", "[solidpick]") {
  // THE precision test. A raw triangle hit is up to ~0.00986 ft inside the true radius at the
  // shipping chord tolerance - inside REQ-101 but nearly all of its budget. Projected onto the
  // analytic cylinder it is exact, so the radius is asserted far tighter than a chord could pass.
  const double r = 5.0, h = 20.0;
  const Displayed d = Display(Cylinder(r, h));

  for (int i = 0; i < 24; ++i) {
    const double az = (i + 0.37) * (2.0 * kPi / 24.0);  // offset so no ray hits a seam exactly
    const Vec3 origin{10.0 * r * std::cos(az), 10.0 * r * std::sin(az), h * 0.5};
    const Vec3 aim{r * std::cos(az), r * std::sin(az), h * 0.5};
    const Ray inward{origin, ray3d::Normalize(ray3d::Sub(aim, origin))};

    solidpick::Pick p;
    REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, inward, NoSnap(), &p));
    REQUIRE(p.kind == solidpick::Kind::Face);
    REQUIRE(d.solid.faces[static_cast<std::size_t>(p.index)].surface.kind ==
            brep::SurfaceKind::Cylinder);
    const double got = std::sqrt(p.point.x * p.point.x + p.point.y * p.point.y);
    REQUIRE(got == Approx(r).margin(1e-9));
  }
}

TEST_CASE("A face pick is exact along the surface, not only across it", "[solidpick]") {
  // The radius assertion above can only fail one way, and it is worth being explicit about why:
  // ClosestPointOnSurface rescales a cylinder hit to exactly `radius`, so ANY input point near the
  // cylinder comes back at radius r. On its own that pins the projection, not the pick - a raw
  // triangle hit a foot off would still pass.
  //
  // The pick's other coordinate is the one that can actually be wrong: where AROUND the cylinder
  // the point sits. The projection does nothing for it. So this asserts the azimuth too.
  const double r = 5.0, h = 20.0;
  const Displayed d = Display(Cylinder(r, h));

  for (int i = 0; i < 24; ++i) {
    const double az = (i + 0.37) * (2.0 * kPi / 24.0);
    const Vec3 origin{10.0 * r * std::cos(az), 10.0 * r * std::sin(az), h * 0.5};
    const Vec3 aim{r * std::cos(az), r * std::sin(az), h * 0.5};
    const Ray inward{origin, ray3d::Normalize(ray3d::Sub(aim, origin))};

    solidpick::Pick p;
    REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, inward, NoSnap(), &p));
    REQUIRE(p.kind == solidpick::Kind::Face);
    // The ray was aimed straight at the axis along `az`, so the hit must be at that azimuth. The
    // chord tolerance is 0.01 ft on a 5 ft radius, so a chord-quantized answer would be off by up
    // to ~0.002 rad; 1e-4 rad is comfortably inside that and comfortably outside float noise.
    const double gotAz = std::atan2(p.point.y, p.point.x);
    double dAz = gotAz - az;
    while (dAz > kPi) dAz -= 2.0 * kPi;
    while (dAz < -kPi) dAz += 2.0 * kPi;
    REQUIRE(std::fabs(dAz) < 1e-4);
    // Arc-length error, in the units REQ-101 is actually written in.
    REQUIRE(std::fabs(dAz) * r < 0.01);
  }
}

TEST_CASE("Storage coordinates are what keep the float display buffer adequate", "[solidpick]") {
  // solidpick.hpp states a precondition: the triangles are STORAGE coordinates - X/Y local to the
  // document origin - so they stay at model magnitude however far out the drawing sits. This pins
  // that precondition with evidence rather than prose, by violating it.
  //
  // Fed triangles at ABSOLUTE state-plane magnitude, the float buffer quantizes to its ULP there
  // (0.125 ft at easting 2e6, twelve times REQ-101). The projection still returns a point exactly
  // on the analytic cylinder - which is precisely why the radius assertion cannot catch this - but
  // the position AROUND the cylinder is wrong by far more than REQ-101 allows.
  //
  // If a future change makes this pass, the precondition has become unnecessary and the note in
  // solidpick.hpp should go with it.
  const double r = 5.0, h = 20.0;
  const Vec3 far{2000000.0, 500000.0, 0.0};
  const Displayed d = Display(Cylinder(r, h, far));

  // Incidence has to be OBLIQUE for the quantization to show, and that is worth recording because
  // the obvious test does not work: aimed radially at the axis, the displacement of a quantized
  // triangle lies almost entirely ALONG the ray, so it moves the hit's radius and not its azimuth -
  // and the projection then removes it exactly. A first version of this test asserted a tangential
  // error on a radial ray and failed, correctly.
  //
  // At incidence angle t from the normal, a plane displaced by d shifts the hit along the surface by
  // about d*tan(t). With d = 0.125 ft (the float ULP at easting 2e6) and t = 60 degrees that is
  // ~0.22 ft, twenty times REQ-101.
  const double az = 0.61;
  const Vec3 target{far.x + r * std::cos(az), far.y + r * std::sin(az), h * 0.5};
  const Vec3 radial{std::cos(az), std::sin(az), 0.0};
  const Vec3 tangent{-std::sin(az), std::cos(az), 0.0};
  const double lean = kPi / 3.0;  // 60 degrees off the surface normal
  const Vec3 approach = ray3d::Normalize(
      ray3d::Add(ray3d::Scale(radial, -std::cos(lean)), ray3d::Scale(tangent, std::sin(lean))));
  const Ray oblique{ray3d::Sub(target, ray3d::Scale(approach, 40.0)), approach};

  solidpick::Pick p;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, oblique, NoSnap(), &p));
  REQUIRE(p.kind == solidpick::Kind::Face);

  // Radius: still exact, because the projection put it there. This is why a radius assertion alone
  // can never detect the problem.
  const double dx = p.point.x - far.x, dy = p.point.y - far.y;
  REQUIRE(std::sqrt(dx * dx + dy * dy) == Approx(r).margin(1e-6));

  // Position along the surface: NOT exact. This is the error the projection cannot see, and the
  // reason the precondition exists.
  const double gotAz = std::atan2(dy, dx);
  double dAz = gotAz - az;
  while (dAz > kPi) dAz -= 2.0 * kPi;
  while (dAz < -kPi) dAz += 2.0 * kPi;
  REQUIRE(std::fabs(dAz) * r > 0.01);  // arc-length error exceeds REQ-101
}

TEST_CASE("An oblique pick in storage coordinates stays inside REQ-101", "[solidpick]") {
  // The control for the test above: the same oblique geometry at model magnitude, where the float
  // buffer's ULP is ~1e-6 ft instead of 0.125 ft. This is the case that actually ships, and it is
  // what shows the precondition is about the coordinates and not about the obliquity.
  const double r = 5.0, h = 20.0;
  const Displayed d = Display(Cylinder(r, h));

  const double az = 0.61;
  const Vec3 target{r * std::cos(az), r * std::sin(az), h * 0.5};
  const Vec3 radial{std::cos(az), std::sin(az), 0.0};
  const Vec3 tangent{-std::sin(az), std::cos(az), 0.0};
  const double lean = kPi / 3.0;
  const Vec3 approach = ray3d::Normalize(
      ray3d::Add(ray3d::Scale(radial, -std::cos(lean)), ray3d::Scale(tangent, std::sin(lean))));
  const Ray oblique{ray3d::Sub(target, ray3d::Scale(approach, 40.0)), approach};

  solidpick::Pick p;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, oblique, NoSnap(), &p));
  REQUIRE(p.kind == solidpick::Kind::Face);
  REQUIRE(std::sqrt(p.point.x * p.point.x + p.point.y * p.point.y) == Approx(r).margin(1e-9));

  const double gotAz = std::atan2(p.point.y, p.point.x);
  double dAz = gotAz - az;
  while (dAz > kPi) dAz -= 2.0 * kPi;
  while (dAz < -kPi) dAz += 2.0 * kPi;
  // The chord tolerance is 0.01 ft, and an oblique ray sees the chord's own tilt, so the budget here
  // is the tessellation's rather than the projection's: assert REQ-101 itself.
  REQUIRE(std::fabs(dAz) * r < 0.01);
}

// ---------------------------------------------------------------------------
// Precedence
// ---------------------------------------------------------------------------

TEST_CASE("A click near a box corner picks the vertex, not the face or the edge", "[solidpick]") {
  const Displayed d = Display(Box(10, 6, 4));
  // Straight down just inside the (+5, +3) top corner.
  const Ray r = Down(4.98, 2.98);

  solidpick::Pick face;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, r, NoSnap(), &face));
  REQUIRE(face.kind == solidpick::Kind::Face);  // with no tolerances, the face is all there is

  solidpick::Pick p;
  const solidpick::Tolerance tol{0.1, 0.1};  // vertex and edge both generous
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, r, tol, &p));
  REQUIRE(p.kind == solidpick::Kind::Vertex);
  REQUIRE(p.point.x == Approx(5.0));
  REQUIRE(p.point.y == Approx(3.0));
  REQUIRE(p.point.z == Approx(4.0));
}

TEST_CASE("A click near a box edge but away from its ends picks the edge", "[solidpick]") {
  const Displayed d = Display(Box(10, 6, 4));
  // Down at mid-length of the +Y top edge: near the edge, far from either corner.
  const Ray r = Down(0.0, 2.98);

  solidpick::Pick p;
  const solidpick::Tolerance tol{0.1, 0.1};
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, r, tol, &p));
  REQUIRE(p.kind == solidpick::Kind::Edge);
  // The picked point is on the edge: y and z pinned to the edge, x free along it.
  REQUIRE(p.point.y == Approx(3.0));
  REQUIRE(p.point.z == Approx(4.0));
  REQUIRE(p.point.x == Approx(0.0).margin(1e-6));
}

TEST_CASE("Zero tolerance disables the vertex and edge picks", "[solidpick]") {
  // The tolerances are a screen-space budget the caller converts; zero must mean "do not offer
  // this kind" rather than "offer it at an arbitrary distance".
  const Displayed d = Display(Box(10, 6, 4));
  const Ray r = Down(4.99, 2.99);  // practically on the corner

  solidpick::Pick p;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, r, NoSnap(), &p));
  REQUIRE(p.kind == solidpick::Kind::Face);

  solidpick::Pick q;
  const solidpick::Tolerance edgeOnly{0.0, 0.1};
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, r, edgeOnly, &q));
  REQUIRE(q.kind == solidpick::Kind::Edge);  // vertex disabled, so the edge takes it
}

TEST_CASE("An occluded vertex on the far side is not picked", "[solidpick]") {
  // Aiming down the axis of a box at the BOTTOM corner: the ray passes within tolerance of the
  // bottom vertex, but the top face is in front of it. Selecting the hidden corner would let a
  // click on a near face grab the back silhouette.
  const Displayed d = Display(Box(10, 6, 4));
  const Ray r = Down(4.98, 2.98);

  solidpick::Pick p;
  const solidpick::Tolerance tol{0.1, 0.1};
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, r, tol, &p));
  REQUIRE(p.kind == solidpick::Kind::Vertex);
  REQUIRE(p.point.z == Approx(4.0));  // the TOP corner, z = 4, never the bottom one at z = 0
}

// ---------------------------------------------------------------------------
// Curved edges
// ---------------------------------------------------------------------------

TEST_CASE("A click near a cylinder's rim picks the rim edge on the true arc", "[solidpick]") {
  // An arc edge cannot be picked by a segment test alone. The answer must be on the circle of
  // radius r, not on the chord the tessellator drew across it.
  const double r = 5.0, h = 20.0;
  const Displayed d = Display(Cylinder(r, h));

  const double az = 0.61;
  // Just inside the top rim, straight down: within edge tolerance of the rim.
  const Ray ray = Down((r - 0.02) * std::cos(az), (r - 0.02) * std::sin(az));

  solidpick::Pick p;
  const solidpick::Tolerance tol{0.0, 0.2};  // edges only, so the rim is unambiguous
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, ray, tol, &p));
  REQUIRE(p.kind == solidpick::Kind::Edge);
  REQUIRE(p.point.z == Approx(h));
  const double got = std::sqrt(p.point.x * p.point.x + p.point.y * p.point.y);
  REQUIRE(got == Approx(r).margin(1e-9));  // on the arc, not on a chord
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

TEST_CASE("A ray that misses the solid picks nothing", "[solidpick]") {
  const Displayed d = Display(Box(10, 6, 4));
  solidpick::Pick p;
  p.kind = solidpick::Kind::Face;  // pre-set, to prove nothing is written on a miss
  p.index = 7;
  REQUIRE_FALSE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(50.0, 50.0),
                                         solidpick::Tolerance{0.1, 0.1}, &p));
  REQUIRE(p.kind == solidpick::Kind::Face);  // untouched
  REQUIRE(p.index == 7);
}

TEST_CASE("A solid behind the cursor picks nothing", "[solidpick]") {
  const Displayed d = Display(Box(10, 6, 4));
  solidpick::Pick p;
  // Below the box, looking further down: every face is behind the origin.
  const Ray away{{0.0, 0.0, -10.0}, {0.0, 0.0, -1.0}};
  REQUIRE_FALSE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, away, NoSnap(), &p));
}

TEST_CASE("A degenerate ray and a null result are refused", "[solidpick]") {
  const Displayed d = Display(Box(10, 6, 4));
  solidpick::Pick p;
  const Ray zero{{0.0, 0.0, 500.0}, {0.0, 0.0, 0.0}};
  REQUIRE_FALSE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, zero, NoSnap(), &p));
  REQUIRE_FALSE(
      solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(0.0, 0.0), NoSnap(), nullptr));
}

TEST_CASE("Mismatched triangle buffers are refused, not read past", "[solidpick]") {
  // triVerts must hold nine floats per face id. A caller that pairs the wrong two buffers is a bug,
  // and reading past the end of one would be a crash in a frame that only asked what is under the
  // cursor.
  Displayed d = Display(Box(10, 6, 4));
  solidpick::Pick p;
  d.triVerts.pop_back();
  REQUIRE_FALSE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(1.0, 1.0), NoSnap(), &p));
}

TEST_CASE("An empty solid is refused", "[solidpick]") {
  const brep::Solid empty;
  solidpick::Pick p;
  REQUIRE_FALSE(solidpick::PickSubObject(empty, {}, {}, Down(0.0, 0.0), NoSnap(), &p));
}

TEST_CASE("A ray whose direction is not unit length still picks correctly", "[solidpick]") {
  // The natural way to build a cursor ray is to unproject a near and a far point and subtract, which
  // gives a direction hundreds of units long. RayTriangleIntersect's parameter scales as 1/|dir|
  // while RayPointDistance's scales as |dir|, so on a non-unit ray the two are a factor of |dir|^2
  // apart and the occlusion comparison is meaningless - vertices and edges silently stop being
  // picked. PickSubObject normalizes on entry; this is what pins that.
  const Displayed d = Display(Box(10, 6, 4));
  const solidpick::Tolerance tol{0.1, 0.1};

  solidpick::Pick unit;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(4.98, 2.98), tol, &unit));
  REQUIRE(unit.kind == solidpick::Kind::Vertex);

  for (double scale : {0.002, 1.0, 750.0}) {
    const Ray scaled{{4.98, 2.98, 500.0}, {0.0, 0.0, -scale}};
    solidpick::Pick p;
    REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, scaled, tol, &p));
    REQUIRE(p.kind == solidpick::Kind::Vertex);
    REQUIRE(p.index == unit.index);
    // rayT is documented as a DISTANCE, so it must not move with |dir|.
    REQUIRE(p.rayT == Approx(unit.rayT).margin(1e-9));
  }
}

TEST_CASE("A ray passing just outside the solid still reaches its edges", "[solidpick]") {
  // The broad-phase bounds reject is padded by the larger tolerance for this case: a cursor aimed a
  // hair outside the silhouette is aiming AT the outline, and an unpadded box test would discard the
  // solid before any edge was considered.
  const Displayed d = Display(Box(10, 6, 4));
  const solidpick::Tolerance tol{0.0, 0.15};

  solidpick::Pick p;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(0.0, 3.08), tol, &p));
  REQUIRE(p.kind == solidpick::Kind::Edge);
  REQUIRE(p.point.y == Approx(3.0));

  // Well outside the pad, the solid is rejected outright.
  solidpick::Pick q;
  REQUIRE_FALSE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(0.0, 4.0), tol, &q));
}

TEST_CASE("A curved edge with no sweep is still walked as a curve", "[solidpick]") {
  // EdgeSearchChords keys off the curve KIND, not off `sweep`. brep::Edge::sweep is documented for
  // Arc and Ellipse only, so a CurveKind::Intersection edge - the procedural surface-crossing curve
  // the B2b-2 booleans produce - leaves it zero. Keying on sweep gave those edges a single straight
  // chord end to end, which on a seam quarter-curve is up to 0.29*r off the true curve; the refine
  // then started from a bad seed, over-estimated the distance, and the edge silently became
  // unpickable. A cylinder's rim stands in for the shape of that failure: it is an Arc, so it has a
  // sweep, but if the chord budget collapses to 1 the rim's own pick goes wrong the same way.
  const double r = 5.0, h = 20.0;
  const Displayed d = Display(Cylinder(r, h));

  // Each rim of this cylinder is seamed into two half-turn arcs. Pick near the middle of one, where
  // a single end-to-end chord would be a full diameter away from the curve.
  const double az = kPi * 0.5;
  const Ray ray = Down((r - 0.02) * std::cos(az), (r - 0.02) * std::sin(az));
  solidpick::Pick p;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, ray, {0.0, 0.2}, &p));
  REQUIRE(p.kind == solidpick::Kind::Edge);
  REQUIRE(std::sqrt(p.point.x * p.point.x + p.point.y * p.point.y) == Approx(r).margin(1e-9));
}

TEST_CASE("A corrupt face id is skipped rather than trusted", "[solidpick]") {
  // A face id out of range names no face. Indexing on it would read past Solid::faces; the pick
  // skips that triangle instead, and reports whatever legitimate geometry remains.
  Displayed d = Display(Box(10, 6, 4));
  for (auto& id : d.triFaceIds)
    id = 999;
  solidpick::Pick p;
  REQUIRE_FALSE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(1.0, 1.0), NoSnap(), &p));

  // With an edge tolerance the edges are still real geometry and still pickable - and it must be the
  // NEAR edge. Asserting only `kind == Edge` here would pass whether the top or the bottom edge won,
  // which is the whole question: a corrupt id must not disable the occlusion rule.
  solidpick::Pick q;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(0.0, 2.98),
                                   solidpick::Tolerance{0.0, 0.1}, &q));
  REQUIRE(q.kind == solidpick::Kind::Edge);
  REQUIRE(q.point.z == Approx(4.0));  // the top edge, never the bottom one at z = 0
}

TEST_CASE("A corrupt id on the nearest triangle does not lift the occlusion rule", "[solidpick]") {
  // The occlusion baseline is the nearest TRIANGLE hit, not the nearest usable face. A triangle
  // whose face id is out of range still proves a front surface is there. Taking the next valid
  // face's depth instead would put the baseline on the far side of the solid, and every back-side
  // vertex would then pass as unoccluded.
  Displayed d = Display(Box(10, 6, 4));

  // Corrupt the id of every triangle on the TOP face, found by picking it first.
  solidpick::Pick top;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(1.0, 1.0), NoSnap(), &top));
  REQUIRE(top.kind == solidpick::Kind::Face);
  const int topFace = top.index;
  for (auto& id : d.triFaceIds)
    if (id == topFace)
      id = 999;

  // Aimed down at the top corner: the top face's triangles are unusable, but they are still in
  // front, so the BOTTOM corner must stay occluded and the top corner must still win.
  solidpick::Pick p;
  REQUIRE(solidpick::PickSubObject(d.solid, d.triVerts, d.triFaceIds, Down(4.98, 2.98),
                                   solidpick::Tolerance{0.1, 0.1}, &p));
  REQUIRE(p.kind == solidpick::Kind::Vertex);
  REQUIRE(p.point.z == Approx(4.0));
}

TEST_CASE("KindName is defined for every kind", "[solidpick]") {
  REQUIRE(std::string(solidpick::KindName(solidpick::Kind::None)) == "none");
  REQUIRE(std::string(solidpick::KindName(solidpick::Kind::Face)) == "face");
  REQUIRE(std::string(solidpick::KindName(solidpick::Kind::Edge)) == "edge");
  REQUIRE(std::string(solidpick::KindName(solidpick::Kind::Vertex)) == "vertex");
}
