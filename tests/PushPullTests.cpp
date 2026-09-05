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
// MEASURED. Removing the curved-neighbour check and pushing a CYLINDER cap by 3 builds a solid that
// `Validate` returns **Ok** for, whose analytic volume is **863.938** against a true 1021.02 for
// r=5 h=13 — **15% wrong**, because the wall surface still says `height = 10` while its top
// boundary moved to 13. That is the case the check exists for.
//
// This file has now been wrong twice about its own subject, both times by asserting instead of
// measuring, and both corrections are kept here because they are the useful part:
//
//   1. the first draft named a WEDGE as the case `Validate` misses. It is not — `Validate` rejects
//      that one at every distance from 0.001 to 2.0. The cylinder is the case;
//   2. the second draft asserted a wedge and a pyramid could not be pushed at all. That was true of
//      the first ALGORITHM, which translated each corner along the push — correct only where every
//      neighbour contains that direction. Measured against the shipped primitives it managed
//      **box 6/6, wedge 2/5, pyramid 0/6**: a pyramid is entirely flat-faced and could not be
//      pushed at all, which is what showed the algorithm was the special case rather than the
//      general one. Re-solving each corner as the meeting point of the planes around it gives
//      **box 6/6, wedge 5/5, pyramid 6/6**, with identical answers on the box.

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
    REQUIRE(why == brep::Problem::PushPullNeighbourCurved);
  }
  SECTION("a TRUE pyramid's side face is refused because its apex would have to split") {
    // Top radius ZERO, so there is a real apex — four planes meeting at one point. Move one of them
    // and no single point satisfies all four any more: the apex would have to become several. A
    // topology change, and a different operation. Refused by name rather than approximated to some
    // nearest point, which would leave the apex off three of the four faces that meet there.
    //
    // A pyramid FRUSTUM (a non-zero top radius) has no apex and pushes fine on all six faces — the
    // positive case below. The distinction is the whole reason this refusal is about the CORNER
    // rather than about pyramids.
    brep::Solid pyr;
    REQUIRE(brep::MakePyramid(World(), 4, 5.0, 0.0, 10.0, &pyr, &why));
    REQUIRE(pyr.faces.size() == 5);  // base + four triangles: an apex, not a top face
    int side = -1;
    for (size_t i = 0; i < pyr.faces.size(); ++i)
      if (std::fabs(ray3d::Normalize(pyr.faces[i].surface.frame.zAxis).z) < 0.9)
        side = static_cast<int>(i);
    REQUIRE(side >= 0);
    REQUIRE_FALSE(brep::PushPullFace(pyr, side, 1.0, &out, &why));
    REQUIRE(why == brep::Problem::PushPullVertexUnsolvable);
    // Its BASE still pushes: those corners are three planes each, apex or no apex.
    REQUIRE(brep::PushPullFace(pyr, FaceFacing(pyr, {0, 0, -1}), 1.0, &out, &why));
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
                            brep::Problem::PushPullNeighbourCurved,
                            brep::Problem::PushPullVertexUnsolvable,
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




// The generality the plane re-solve bought, and the reason it was written.
//
// The first version of this operation TRANSLATED each corner along the push. That is only correct
// when every neighbouring face contains the push direction — true of a box, false of everything
// else — so measured against the shipped primitives it managed **box 6/6, wedge 2/5, pyramid 0/6**.
// A pyramid is entirely flat-faced and could not be pushed at all, which is what showed the
// algorithm was the special case rather than the general one.
//
// Re-solving each corner as the meeting point of the planes around it gives **box 6/6, wedge 5/5,
// pyramid 6/6** (frustum) and identical answers on the box. These cases pin the ones that changed.
TEST_CASE("Slanted neighbours push correctly, by re-solving corners (REQ-319)", "[pushpull]") {
  brep::Problem why{};
  brep::Solid out;

  SECTION("a WEDGE's end face — refused by the old algorithm, exact under the new one") {
    // 20 x 10 x 8 wedge: full height at x = -10, falling to zero at x = +10. Volume is half the box,
    // 800. Its end face is the full-height rectangle; pushing it out by 5 extends the ramp.
    brep::Solid wedge;
    REQUIRE(brep::MakeWedge(World(), 20.0, 10.0, 8.0, &wedge, &why));
    REQUIRE(Volume(wedge) == Approx(800.0));
    const int endFace = FaceFacing(wedge, {-1, 0, 0});
    REQUIRE(brep::PushPullFace(wedge, endFace, 5.0, &out, &why));

    // The added material is NOT a prism, and this figure is the whole point of the re-solve. The
    // SLOPE keeps its plane, so extending the end face outward makes the wedge TALLER there: the
    // ramp falls 8 over 20, i.e. 0.4 per foot, so at x = -15 the slope has risen to z = 10. The new
    // material is a trapezoidal prism 10 high at the far end and 8 at the near one — mean 9, times
    // 10 wide times 5 long = 450.
    //
    // Translating the corners straight out, as the first version of this operation did, would have
    // kept them at z = 8 and produced 400 — a wedge whose top corners no longer touch its own
    // slope. Hand-computed rather than recorded from the output: 450 is what the geometry says.
    REQUIRE(Volume(out) == Approx(800.0 + 450.0));
    REQUIRE(out.vertices.size() == wedge.vertices.size());  // topology untouched
    REQUIRE(out.faces.size() == wedge.faces.size());

    // The thing the old algorithm could not do: every vertex still lies on EVERY face that uses it,
    // the slope included. This is the property `Validate` does not check, asserted directly.
    for (size_t fi = 0; fi < out.faces.size(); ++fi) {
      const brep::Face& f = out.faces[fi];
      const ray3d::Vec3 n = ray3d::Normalize(f.surface.frame.zAxis);
      for (const brep::Loop& lp : f.loops)
        for (const brep::EdgeUse& u : lp.uses) {
          const brep::Edge& e = out.edges[static_cast<size_t>(u.edge)];
          for (int vi : {e.v0, e.v1}) {
            const ray3d::Vec3 d = ray3d::Sub(out.vertices[static_cast<size_t>(vi)].p, f.surface.frame.origin);
            REQUIRE(ray3d::Dot(d, n) == Approx(0.0).margin(1e-9));
          }
        }
    }
  }

  SECTION("a pyramid FRUSTUM pushes on every face") {
    // Square frustum: base circumradius 5, top 2, height 10. Every corner is three planes, so every
    // face moves — including the four slanted walls, which the old algorithm refused outright.
    brep::Solid pyr;
    REQUIRE(brep::MakePyramid(World(), 4, 5.0, 2.0, 10.0, &pyr, &why));
    REQUIRE(pyr.faces.size() == 6);
    const double v0 = Volume(pyr);
    for (size_t i = 0; i < pyr.faces.size(); ++i)
      REQUIRE(brep::PushPullFace(pyr, static_cast<int>(i), 0.5, &out, &why));

    // Pushing the TOP up by 1 with the walls keeping their slope. The frustum tapers inward going
    // up, so the new top is NARROWER than the old one and the volume grows by LESS than a prism of
    // the old top area would add. That inequality is the assertion, and it is the direct evidence
    // that the corners were re-solved along the slope: translating them straight up would have kept
    // the top exactly its old size and added exactly the prism.
    //
    // The numbers, by hand. Base circumradius 5 → a square of side 5√2, area 50; top radius 2 →
    // area 8; frustum volume = h/3·(A₁ + A₂ + √(A₁A₂)) = 10/3·(50 + 8 + 20) = 260. Push to h = 11
    // and the taper puts the top radius at 5 + (2−5)·11/10 = 1.7, area 5.78, so the volume is
    // 11/3·(50 + 5.78 + 17.0) = 266.86 — a gain of 6.86 against the prism's 8.
    REQUIRE(v0 == Approx(260.0));
    REQUIRE(brep::PushPullFace(pyr, FaceFacing(pyr, {0, 0, 1}), 1.0, &out, &why));
    const double topArea = 2.0 * 2.0 * 2.0;  // square of circumradius 2: side 2√2, area 8
    REQUIRE(Volume(out) == Approx(266.86).margin(0.01));
    REQUIRE(Volume(out) < v0 + topArea * 1.0);
    REQUIRE(Volume(out) > v0);  // it did grow — the sign is right
  }
}
