// Depth-ordered viewport picking (GitHub issue #564 §2, D-2026-09-28-e).
//
// `ResolveViewportPick` is the one answer the hover and both click paths use. These tests drive it
// with the rays a camera would cast and check the user-facing rule: what is visible under the pixel
// answers, nothing behind an opaque solid does, and the hover and the click agree.

#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"

#include <cmath>
#include <memory>
#include <vector>

namespace {

/// A 20 x 10 x 8 box: x [-10,10], y [-5,5], z [0,8].
void AddBox(AppCommandState& st) {
  brep::Solid s;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 10.0, 8.0, &s, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(s)));
  st.cadSolidAttrs.push_back(EntityAttributes{});
  // A definite view, so the solid edge tolerance is a known ~2 units rather than a degenerate default.
  st.viewportLastSurveyLayoutHeightPx = 700.f;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  RefreshSolidDisplayGeometry(st);
}

void AddLine(AppCommandState& st, float x0, float y0, float z0, float x1, float y1, float z1) {
  st.userLinesFlat.insert(st.userLinesFlat.end(), {x0, y0, z0, x1, y1, z1});
  st.userLineAttrs.push_back(EntityAttributes{});
}

/// A pick aimed at \p aim along \p dir. Plan (\p orbited false): the ray is straight down and the
/// linework keeps its plan XY test. Orbited: the same ray drives every family.
ViewportPickRequest Aim(ray3d::Vec3 aim, ray3d::Vec3 dir, bool orbited, float tol) {
  ViewportPickRequest rq;
  const double len = std::sqrt(ray3d::Dot(dir, dir));
  dir = ray3d::Scale(dir, 1.0 / len);
  static ray3d::Ray ray;  // outlives the request for the orbitRay pointer
  ray.dir = dir;
  ray.origin = ray3d::Sub(aim, ray3d::Scale(dir, 500.0));
  rq.eyeRay = ray;
  rq.eyeRayValid = true;
  rq.orbitRay = orbited ? &ray : nullptr;
  // The work-plane (z = 0) cursor, as the viewport computes it.
  const double t = -ray.origin.z / ray.dir.z;
  rq.rawX = ray.origin.x + t * ray.dir.x;
  rq.rawY = ray.origin.y + t * ray.dir.y;
  rq.lineTol = tol;
  return rq;
}

const ray3d::Vec3 kDown{0.0, 0.0, -1.0};

}  // namespace

TEST_CASE("Plan, Shaded: a solid hides the line beneath it; beside it the line answers",
          "[pick][issue564]") {
  AppCommandState st;
  st.viewportVisualStyle = VisualStyle::Shaded;
  AddBox(st);
  AddLine(st, -30.f, 0.f, 0.f, 30.f, 0.f, 0.f);  // runs UNDER the box, at z = 0
  ViewportPickResult r = ResolveViewportPick(st, Aim({0, 0, 0}, kDown, false, 0.5f));
  CHECK(r.family == ViewportPickFamily::Solid);
  r = ResolveViewportPick(st, Aim({25, 0, 0}, kDown, false, 0.5f));
  CHECK(r.family == ViewportPickFamily::Linework);
}

TEST_CASE("Plan, 2D Wireframe: the solid is see-through, so the line beneath answers (Q1)",
          "[pick][issue564]") {
  AppCommandState st;
  st.viewportVisualStyle = VisualStyle::Wireframe2D;
  AddBox(st);
  AddLine(st, -30.f, 0.f, 0.f, 30.f, 0.f, 0.f);
  const ViewportPickResult r = ResolveViewportPick(st, Aim({0, 0, 0}, kDown, false, 0.5f));
  CHECK(r.family == ViewportPickFamily::Linework);
}

TEST_CASE("Linework in front of a solid still wins; linework ON its surface counts as in front",
          "[pick][issue564]") {
  AppCommandState st;
  st.viewportVisualStyle = VisualStyle::Shaded;
  AddBox(st);
  AddLine(st, -30.f, 2.f, 20.f, 30.f, 2.f, 20.f);  // above the box
  AddLine(st, -5.f, -2.f, 8.f, 5.f, -2.f, 8.f);    // drawn on the top face
  CHECK(ResolveViewportPick(st, Aim({0, 2, 0}, kDown, false, 0.5f)).family == ViewportPickFamily::Linework);
  const ViewportPickResult r = ResolveViewportPick(st, Aim({0, -2, 0}, kDown, false, 0.5f));
  REQUIRE(r.family == ViewportPickFamily::Linework);
  CHECK(r.entity.index == 1);
}

TEST_CASE("Orbited, Shaded: a line behind the solid is hidden; one in front is not",
          "[pick][issue564]") {
  AppCommandState st;
  st.viewportVisualStyle = VisualStyle::Shaded;
  AddBox(st);
  // Looking along +X, slightly down. A vertical line BEHIND the box (x = 20) and one IN FRONT (x = -20).
  AddLine(st, 20.f, 0.f, 0.f, 20.f, 0.f, 8.f);
  AddLine(st, -20.f, 3.f, 0.f, -20.f, 3.f, 8.f);
  // Nearly level, so the ray to the back line passes the box through its faces, well clear of the
  // edges (which in 2D Wireframe are what the box shows, and would rightly answer).
  const ray3d::Vec3 look{1.0, 0.0, -0.05};
  // Aim at the back line through the box: the box's -X face is in the way.
  ViewportPickResult r = ResolveViewportPick(st, Aim({20, 0, 3}, look, true, 0.5f));
  CHECK(r.family == ViewportPickFamily::Solid);
  // Wireframe: see-through, so the back line answers.
  st.viewportVisualStyle = VisualStyle::Wireframe2D;
  r = ResolveViewportPick(st, Aim({20, 0, 3}, look, true, 0.5f));
  CHECK(r.family == ViewportPickFamily::Linework);
  CHECK(r.entity.index == 0);
  // The front line wins over the box in every style.
  st.viewportVisualStyle = VisualStyle::Shaded;
  r = ResolveViewportPick(st, Aim({-20, 3, 4}, look, true, 0.5f));
  REQUIRE(r.family == ViewportPickFamily::Linework);
  CHECK(r.entity.index == 1);
}

TEST_CASE("Text behind an opaque solid does not answer", "[pick][issue564]") {
  AppCommandState st;
  st.viewportVisualStyle = VisualStyle::Shaded;
  AddBox(st);
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Text;
  a.text = "UNDER";
  a.insX = -2.f;
  a.insY = 0.5f;
  a.insZ = 0.f;
  a.plottedHeightInches = 0.1f;
  st.cadAnnotations.push_back(a);
  st.cadAnnotationAttrs.push_back(EntityAttributes{});
  ViewportPickRequest rq = Aim({-1.5, 0, 0}, kDown, false, 0.5f);
  CHECK(ResolveViewportPick(st, rq).family == ViewportPickFamily::Solid);
  st.viewportVisualStyle = VisualStyle::Wireframe2D;
  CHECK(ResolveViewportPick(st, rq).family == ViewportPickFamily::Annotation);
}

TEST_CASE("Flat drawing: the line under the cursor wins, not the first one drawn (D-2026-09-28-e)",
          "[pick][issue564]") {
  // Two parallel lines 1 unit apart, both inside a 1.5-unit click radius. The cursor is ON the
  // second. Before, the default click took the highest Z, first-drawn on a tie: the FIRST line.
  AppCommandState st;
  AddLine(st, 0.f, 0.f, 0.f, 50.f, 0.f, 0.f);
  AddLine(st, 0.f, 1.f, 0.f, 50.f, 1.f, 0.f);
  ViewportPickRequest rq;
  rq.rawX = 25.0;
  rq.rawY = 1.0;
  rq.lineTol = 1.5f;
  const ViewportPickResult r = ResolveViewportPick(st, rq);
  REQUIRE(r.family == ViewportPickFamily::Linework);
  CHECK(r.entity.index == 1);
  CHECK(r.candidates.size() == 2);  // both still offered to the disambiguation popup
}

TEST_CASE("Plan, no solids: a lone line under the cursor is the pre-change answer", "[pick][issue564]") {
  AppCommandState st;
  AddLine(st, 0.f, 0.f, 0.f, 10.f, 10.f, 0.f);
  AddLine(st, 30.f, 0.f, 0.f, 40.f, 0.f, 0.f);
  for (const double x : {2.0, 5.0, 35.0, 60.0}) {
    ViewportPickRequest rq;
    rq.rawX = x;
    rq.rawY = x < 20 ? x + 0.2 : 0.2;
    rq.lineTol = 0.5f;
    SelectedEntity old{};
    float d2 = 0.f;
    const bool had = PickClosestCadEntity(st, rq.rawX, rq.rawY, rq.lineTol, &old, &d2, nullptr);
    const ViewportPickResult r = ResolveViewportPick(st, rq);
    INFO("x " << x);
    CHECK((r.family == ViewportPickFamily::Linework) == had);
    if (had)
      CHECK(r.entity.index == old.index);
  }
}

TEST_CASE("Whatever the hover lights, the click at that pixel takes (issue #564 s2)", "[pick][issue564]") {
  // A solid, a line in front of it, a line behind it and a line on its top, sampled over a grid of
  // pixels in an orbited Shaded view. The click asks with its wider radius; wherever the hover (tight
  // radius) lit something, the click must have taken exactly that.
  AppCommandState st;
  st.viewportVisualStyle = VisualStyle::Shaded;
  AddBox(st);
  AddLine(st, -20.f, -8.f, 2.f, -20.f, 8.f, 2.f);
  AddLine(st, 15.f, -8.f, 2.f, 15.f, 8.f, 2.f);
  AddLine(st, -9.f, 0.f, 8.f, 9.f, 0.f, 8.f);
  AddLine(st, -9.f, 0.4f, 8.f, 9.f, 0.4f, 8.f);
  const ray3d::Vec3 look{1.0, 0.3, -0.5};
  int lit = 0;
  for (int iy = -12; iy <= 12; ++iy) {
    for (int iz = 0; iz <= 12; ++iz) {
      const ray3d::Vec3 aim{0.0, iy * 1.0, iz * 1.0};
      const ViewportPickResult hover = ResolveViewportPick(st, Aim(aim, look, true, 0.3f));
      const ViewportPickResult click = ResolveViewportClickPick(st, Aim(aim, look, true, 1.2f), 0.3f);
      if (hover.family == ViewportPickFamily::None)
        continue;
      ++lit;
      INFO("aim y " << iy << " z " << iz);
      CHECK(click.family == hover.family);
      CHECK(click.entity.type == hover.entity.type);
      CHECK(click.entity.index == hover.entity.index);
    }
  }
  CHECK(lit > 20);  // the grid really crossed the geometry
}
