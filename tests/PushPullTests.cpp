// REQ-319 / ADR-046 amendment (i), GitHub issue #148 Phase 5 — push/pull a planar face.
//
// The first operation in this kernel that EDITS a solid rather than building one, so these cases
// carry two burdens the builder tests do not. First, the RESULT has to be right: a push changes the
// volume by a figure that can be computed by hand, and the moved face's own plane has to travel
// with its boundary. Second, and the reason the operation carries its own preconditions at all:
// **`Validate` cannot catch the way this goes wrong.** It checks topology and degeneracy and has no
// test that a face's vertices lie on that face's surface, so a push that slid a slanted neighbour's
// vertices off its own plane can pass validation and produce a solid that tessellates from one
// geometry and integrates its volume from another.
//
// MEASURED, because the first draft of this file asserted it with the wrong shape. Removing the
// neighbour checks and pushing:
//   * a WEDGE end face — refused anyway, by `Validate`, at every distance from 0.001 to 2.0. The
//     precondition is not what saves that case; it only replaces a MISLEADING message ("that push
//     would turn the solid inside out") with a true one.
//   * a CYLINDER cap by 3 — built successfully, `Validate` returned **Ok**, and the analytic volume
//     came out **863.938** against a true 1021.02 for r=5 h=13: **15% wrong**, because the wall
//     surface still says `height = 10` while its top boundary moved to 13.
// So the precondition IS load-bearing, and the cylinder cap is the case that proves it.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

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

/// The index of the face whose outward normal is nearest \p dir. Used instead of a hard-coded index
/// because the tests are about the GEOMETRY, and a face ordering change in `MakeBox` should not
/// silently make them assert something else.
int FaceFacing(const brep::Solid& s, const ray3d::Vec3& dir) {
  int best = -1;
  double bestDot = -2.0;
  for (size_t i = 0; i < s.faces.size(); ++i) {
    if (s.faces[i].surface.kind != brep::SurfaceKind::Plane)
      continue;
    ray3d::Vec3 n = s.faces[i].surface.frame.zAxis;
    if (s.faces[i].surface.inward)
      n = ray3d::Scale(n, -1.0);
    const double d = ray3d::Dot(ray3d::Normalize(n), ray3d::Normalize(dir));
    if (d > bestDot) {
      bestDot = d;
      best = static_cast<int>(i);
    }
  }
  REQUIRE(best >= 0);
  REQUIRE(bestDot > 0.99);  // the box really does have a face pointing that way
  return best;
}

}  // namespace

TEST_CASE("Pushing a box face changes the volume by base area times distance (REQ-319)", "[pushpull]") {
  // 20 x 10 x 8 = 1600.
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  REQUIRE(Volume(box) == Approx(1600.0));

  SECTION("the top face, outward") {
    brep::Solid out;
    brep::Problem why{};
    REQUIRE(brep::PushPullFace(box, FaceFacing(box, {0, 0, 1}), 3.0, &out, &why));
    REQUIRE(why == brep::Problem::Ok);
    // 20 x 10 of new material, 3 deep.
    REQUIRE(Volume(out) == Approx(1600.0 + 200.0 * 3.0));
    // Topology untouched — which is what lets a REQ-318 sub-object reference survive the edit
    // rather than expire (ADR-049).
    REQUIRE(out.vertices.size() == box.vertices.size());
    REQUIRE(out.edges.size() == box.edges.size());
    REQUIRE(out.faces.size() == box.faces.size());
  }
  SECTION("a SIDE face, so the operation is not assuming an axis") {
    // The +X face: pushing it adds width x height = 10 x 8 per unit, not 20 x 10. A push/pull that
    // silently worked along Z would pass the case above and fail this one.
    brep::Solid out;
    brep::Problem why{};
    REQUIRE(brep::PushPullFace(box, FaceFacing(box, {1, 0, 0}), 2.0, &out, &why));
    REQUIRE(Volume(out) == Approx(1600.0 + 80.0 * 2.0));
  }
  SECTION("pulling INWARD removes material") {
    brep::Solid out;
    brep::Problem why{};
    REQUIRE(brep::PushPullFace(box, FaceFacing(box, {0, 0, 1}), -3.0, &out, &why));
    REQUIRE(Volume(out) == Approx(1600.0 - 200.0 * 3.0));
  }
  SECTION("push then pull by the same distance restores the geometry") {
    brep::Solid up;
    brep::Solid back;
    brep::Problem why{};
    const int top = FaceFacing(box, {0, 0, 1});
    REQUIRE(brep::PushPullFace(box, top, 3.0, &up, &why));
    REQUIRE(brep::PushPullFace(up, top, -3.0, &back, &why));
    REQUIRE(Volume(back) == Approx(1600.0));
    // Every vertex back where it started, not merely the same volume — a volume-only check would
    // pass on a solid that had been sheared.
    REQUIRE(back.vertices.size() == box.vertices.size());
    for (size_t i = 0; i < back.vertices.size(); ++i) {
      REQUIRE(back.vertices[i].p.x == Approx(box.vertices[i].p.x));
      REQUIRE(back.vertices[i].p.y == Approx(box.vertices[i].p.y));
      REQUIRE(back.vertices[i].p.z == Approx(box.vertices[i].p.z));
    }
  }
  SECTION("a second push continues from the first") {
    brep::Solid a;
    brep::Solid b;
    brep::Problem why{};
    const int top = FaceFacing(box, {0, 0, 1});
    REQUIRE(brep::PushPullFace(box, top, 3.0, &a, &why));
    REQUIRE(brep::PushPullFace(a, top, 3.0, &b, &why));
    REQUIRE(Volume(b) == Approx(1600.0 + 200.0 * 6.0));
  }
}

TEST_CASE("The pushed face's own plane travels with its boundary (REQ-319)", "[pushpull]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int top = FaceFacing(box, {0, 0, 1});
  brep::Solid out;
  brep::Problem why{};
  REQUIRE(brep::PushPullFace(box, top, 3.0, &out, &why));

  // The surface origin moved with the vertices. If it had not, the face's vertices would sit 3 ft
  // off their own plane — the exact inconsistency the neighbour precondition exists to prevent, on
  // the moved face itself. `Validate` would not have said a word about it.
  const brep::Face& f = out.faces[static_cast<size_t>(top)];
  const ray3d::Vec3 n = ray3d::Normalize(f.surface.frame.zAxis);
  for (const brep::Loop& loop : f.loops) {
    for (const brep::EdgeUse& use : loop.uses) {
      const brep::Edge& e = out.edges[static_cast<size_t>(use.edge)];
      for (int vi : {e.v0, e.v1}) {
        const ray3d::Vec3 d = ray3d::Sub(out.vertices[static_cast<size_t>(vi)].p, f.surface.frame.origin);
        REQUIRE(ray3d::Dot(d, n) == Approx(0.0).margin(1e-9));
      }
    }
  }
  REQUIRE(f.surface.frame.origin.z == Approx(11.0));  // the top was at 8
}

TEST_CASE("Push/pull refuses what it cannot do, by name (REQ-319 / REQ-201)", "[pushpull]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int top = FaceFacing(box, {0, 0, 1});
  brep::Solid out;
  brep::Problem why{};

  SECTION("a zero or non-finite distance") {
    REQUIRE_FALSE(brep::PushPullFace(box, top, 0.0, &out, &why));
    REQUIRE(why == brep::Problem::PushPullDistanceZero);
    REQUIRE_FALSE(brep::PushPullFace(box, top, std::nan(""), &out, &why));
    REQUIRE(why == brep::Problem::PushPullDistanceZero);
    REQUIRE_FALSE(brep::PushPullFace(box, top, std::numeric_limits<double>::infinity(), &out, &why));
    REQUIRE(why == brep::Problem::PushPullDistanceZero);
  }
  SECTION("a face index that is not a face") {
    REQUIRE_FALSE(brep::PushPullFace(box, -1, 3.0, &out, &why));
    REQUIRE(why == brep::Problem::IndexOutOfRange);
    REQUIRE_FALSE(brep::PushPullFace(box, 999, 3.0, &out, &why));
    REQUIRE(why == brep::Problem::IndexOutOfRange);
    REQUIRE_FALSE(brep::PushPullFace(box, top, 3.0, nullptr, &why));
  }
  SECTION("a CURVED face is refused rather than approximated") {
    brep::Solid cyl;
    REQUIRE(brep::MakeCylinder(World(), 5.0, 10.0, &cyl, &why));
    int wall = -1;
    for (size_t i = 0; i < cyl.faces.size(); ++i)
      if (cyl.faces[i].surface.kind == brep::SurfaceKind::Cylinder)
        wall = static_cast<int>(i);
    REQUIRE(wall >= 0);
    REQUIRE_FALSE(brep::PushPullFace(cyl, wall, 1.0, &out, &why));
    REQUIRE(why == brep::Problem::PushPullFaceNotPlanar);
  }
  SECTION("a cylinder's flat CAP is refused for its NEIGHBOUR — the case Validate misses") {
    // **This is the case the whole precondition exists for**, and the only one measured to slip
    // past `Validate`. The cap is planar, so it passes the face test; it is refused because the
    // wall beside it is a cylinder, whose stored `height` would have to be re-solved rather than
    // translated.
    //
    // With the neighbour checks removed, this push BUILDS: `Validate` returns Ok, and the analytic
    // volume comes out 863.938 against a true 1021.02 for r=5 h=13 — 15% wrong — because the wall
    // surface still reports `height = 10` while its top boundary sits at 13. A closed, manifold,
    // positive-volume solid whose volume is a lie. That is what "Validate checks topology, not
    // geometry" costs when nothing else is standing there.
    brep::Solid cyl;
    REQUIRE(brep::MakeCylinder(World(), 5.0, 10.0, &cyl, &why));
    const int cap = FaceFacing(cyl, {0, 0, 1});
    REQUIRE_FALSE(brep::PushPullFace(cyl, cap, 1.0, &out, &why));
    REQUIRE(why == brep::Problem::PushPullNeighbourNotParallel);
  }
  SECTION("a WEDGE's end face is refused for the slope beside it") {
    // A slanted PLANE neighbour: its normal is not perpendicular to the push, so translating its
    // boundary would leave its vertices off its own plane.
    //
    // **Validate would catch this one anyway** — measured, at every distance from 0.001 to 2.0,
    // via the closed-volume path. So what the pre-check buys here is not safety but a TRUE
    // sentence: without it the user is told "that push would turn the solid inside out or flatten
    // it", which is simply false for a 0.001 ft push on a wedge. REQ-201 asks for a reason the user
    // can read, and a confidently wrong reason is worse than a vague one. Recorded rather than
    // dressed up as a near-miss, because the first draft of this file claimed it was one.
    brep::Solid wedge;
    REQUIRE(brep::MakeWedge(World(), 20.0, 10.0, 8.0, &wedge, &why));
    const int endFace = FaceFacing(wedge, {-1, 0, 0});
    REQUIRE_FALSE(brep::PushPullFace(wedge, endFace, 2.0, &out, &why));
    REQUIRE(why == brep::Problem::PushPullNeighbourNotParallel);
    // Small pushes take the same named refusal, not a different one — the check is about the
    // geometry, not about the size of the move.
    REQUIRE_FALSE(brep::PushPullFace(wedge, endFace, 0.001, &out, &why));
    REQUIRE(why == brep::Problem::PushPullNeighbourNotParallel);
  }
  SECTION("a push that would flatten the solid is refused and the input is untouched") {
    // The box is 8 tall; pushing the top down by 8 collapses it, and by more turns it inside out.
    // A real gesture, not a hypothetical — this is what a dragged grip does when it overshoots.
    REQUIRE_FALSE(brep::PushPullFace(box, top, -8.0, &out, &why));
    REQUIRE(why == brep::Problem::PushPullResultInvalid);
    REQUIRE_FALSE(brep::PushPullFace(box, top, -12.0, &out, &why));
    REQUIRE(why == brep::Problem::PushPullResultInvalid);
    REQUIRE(Volume(box) == Approx(1600.0));  // the operand never changed
  }
  SECTION("every refusal has a sentence of its own") {
    // ProblemText never returns null, and a refusal the user cannot read is REQ-201 unmet.
    for (brep::Problem p : {brep::Problem::PushPullFaceNotPlanar, brep::Problem::PushPullDistanceZero,
                            brep::Problem::PushPullNeighbourNotParallel,
                            brep::Problem::PushPullResultInvalid}) {
      const char* t = brep::ProblemText(p);
      REQUIRE(t != nullptr);
      REQUIRE(std::string(t) != "The solid is not valid.");  // the fallback, i.e. an unhandled case
    }
  }
}

TEST_CASE("A pushed solid drops its recipe rather than lying about it (REQ-319 item 6)", "[pushpull]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  REQUIRE(box.recipe.kind == brep::PrimitiveKind::Box);
  brep::Solid out;
  brep::Problem why{};
  REQUIRE(brep::PushPullFace(box, FaceFacing(box, {0, 0, 1}), 3.0, &out, &why));
  // A pushed box is not the box its recipe describes. Keeping a stale recipe would read as
  // authoritative while being false; ADR-045 already made it optional and never consulted by
  // validity, mass properties or tessellation, so dropping it costs nothing downstream.
  REQUIRE(out.recipe.kind == brep::PrimitiveKind::None);
  // ...and the geometry is still fully described, which is the whole reason dropping it is safe.
  REQUIRE(brep::Validate(out) == brep::Problem::Ok);
  REQUIRE(Volume(out) == Approx(2200.0));
}
