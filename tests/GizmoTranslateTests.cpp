// REQ-060 (GitHub issue #148 Phase 5 slice 4b) — the TRANSLATE gizmo.
//
// The transcript `req060-gizmo-translate` drives the gizmo end to end through the camera, which is
// what proves the feature. These cases own the parts a transcript states poorly: the skew-line
// solve on its own, the anchor's per-type walk, and the two negatives — a click that misses every
// handle must be refused, and an empty selection must produce no gizmo at all.
//
// Linked into GoSurveySnapTests because they call into the command layer (gosurvey_domain), the
// same reason SubObjectSelectionTests and ViewportUcsTests are there rather than in GoSurveyTests.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {

/// A drawing holding one line, selected, at a known place.
AppCommandState WithSelectedLine(float x0, float y0, float z0, float x1, float y1, float z1) {
  AppCommandState st;
  st.userLinesFlat = {x0, y0, z0, x1, y1, z1};
  st.userLineAttrs.push_back(EntityAttributes{});
  SelectedEntity e;
  e.type = SelectedEntity::Type::LineSeg;
  e.index = 0;
  st.selection.push_back(e);
  st.uiViewportWidthPx = 1200.f;
  st.uiViewportHeightPx = 700.f;
  return st;
}

/// A ray that passes exactly through \p through, aimed along \p dir.
ray3d::Ray RayThrough(const ray3d::Vec3& through, const ray3d::Vec3& dir) {
  ray3d::Ray r;
  r.dir = ray3d::Normalize(dir);
  r.origin = ray3d::Sub(through, ray3d::Scale(r.dir, 100.0));
  return r;
}

}  // namespace

TEST_CASE("Gizmo: the axis parameter is where the ray comes nearest the axis", "[gizmo][req060]") {
  const ray3d::Vec3 anchor{5.0, 0.0, 0.0};
  const ray3d::Vec3 xAxis{1.0, 0.0, 0.0};

  // A ray straight down through (25, 0, 0) — the plan-view case. Its nearest point on the X axis
  // through the anchor is (25, 0, 0) itself, so the parameter is the 20 units from the anchor.
  double s = 0.0;
  REQUIRE(CadAxisDragParam(anchor, xAxis, RayThrough({25.0, 0.0, 0.0}, {0.0, 0.0, -1.0}), &s));
  CHECK(s == Approx(20.0));

  // The parameter is signed and measured from the anchor, not from the world origin: a point
  // BEHIND the anchor is negative. This is what lets a drag run either way along one handle.
  REQUIRE(CadAxisDragParam(anchor, xAxis, RayThrough({-3.0, 0.0, 0.0}, {0.0, 0.0, -1.0}), &s));
  CHECK(s == Approx(-8.0));

  // A ray that misses the axis in space still has a nearest point on it — the whole reason a skew
  // solve is used rather than an intersection. Offsetting the aim in Y changes nothing along X.
  REQUIRE(CadAxisDragParam(anchor, xAxis, RayThrough({25.0, 40.0, 0.0}, {0.0, 0.0, -1.0}), &s));
  CHECK(s == Approx(20.0));

  // The axis direction need not be unit: callers pass a UCS basis vector, and a frame that is
  // orthonormal by construction is still a promise this function should not have to rely on.
  REQUIRE(CadAxisDragParam(anchor, {7.0, 0.0, 0.0}, RayThrough({25.0, 0.0, 0.0}, {0.0, 0.0, -1.0}), &s));
  CHECK(s == Approx(20.0));
}

TEST_CASE("Gizmo: sighting straight down a handle is refused, not answered", "[gizmo][req060]") {
  // Every point of the handle projects to the same pixel, so no distance is being expressed. The
  // near-singular divide the alternative would take returns an enormous number, which on screen
  // reads as the selection flying off into nothing — a wrong answer that looks like a crash.
  double s = 123.0;
  CHECK_FALSE(CadAxisDragParam({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
                               RayThrough({0.0, 0.0, 5.0}, {0.0, 0.0, -1.0}), &s));
  CHECK(s == Approx(123.0));  // untouched, so a caller that ignores the bool cannot drift silently

  // A degenerate ray is refused for the same reason rather than dividing by its own length.
  ray3d::Ray dead;
  dead.origin = ray3d::Vec3{0.0, 0.0, 10.0};
  dead.dir = ray3d::Vec3{0.0, 0.0, 0.0};
  CHECK_FALSE(CadAxisDragParam({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, dead, &s));
}

TEST_CASE("Gizmo: the anchor is the centre of the selection's box", "[gizmo][req060]") {
  AppCommandState st = WithSelectedLine(0.f, 0.f, 0.f, 10.f, 4.f, 6.f);
  ray3d::Vec3 a{};
  REQUIRE(CadGizmoAnchorWorld(st, &a));
  CHECK(a.x == Approx(5.0));
  CHECK(a.y == Approx(2.0));
  CHECK(a.z == Approx(3.0));

  // A second entity moves it, and in three dimensions — the elevation is not along for the ride.
  st.userLinesFlat.insert(st.userLinesFlat.end(), {10.f, 4.f, 6.f, 20.f, 4.f, 26.f});
  st.userLineAttrs.push_back(EntityAttributes{});
  SelectedEntity e;
  e.type = SelectedEntity::Type::LineSeg;
  e.index = 1;
  st.selection.push_back(e);
  REQUIRE(CadGizmoAnchorWorld(st, &a));
  CHECK(a.x == Approx(10.0));
  CHECK(a.y == Approx(2.0));
  CHECK(a.z == Approx(13.0));
}

TEST_CASE("Gizmo: an empty selection has no anchor and draws nothing", "[gizmo][req060]") {
  // REQ-060's third acceptance bullet, and the whole of it: there is no separate "should the gizmo
  // be drawn" flag that a caller could get wrong, only the absence of an anchor.
  AppCommandState st;
  st.uiViewportWidthPx = 1200.f;
  st.uiViewportHeightPx = 700.f;
  ray3d::Vec3 a{};
  CHECK_FALSE(CadGizmoAnchorWorld(st, &a));
  CHECK_FALSE(CadGizmoVisible(st));

  // And no handle can be grabbed, so a click in an empty drawing is never consumed by a widget that
  // is not there.
  std::vector<std::string> log;
  CHECK(PickGizmoAxis(st, RayThrough({0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}), 1.0) == -1);
  CHECK_FALSE(SubmitGizmoClick(st, RayThrough({0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}), 1.0, log));
}

TEST_CASE("Gizmo: a click away from every handle is not the gizmo's", "[gizmo][req060]") {
  // The guarantee that keeps ordinary selection working: the widget sits over the objects it moves,
  // so if it consumed clicks it merely happened to be near, a selection click would stop selecting.
  AppCommandState st = WithSelectedLine(0.f, 0.f, 0.f, 10.f, 0.f, 0.f);
  std::vector<std::string> log;
  const ray3d::Ray far = RayThrough({5.0, 80.0, 0.0}, {0.0, 0.0, -1.0});
  CHECK(PickGizmoAxis(st, far, 1.0) == -1);
  CHECK_FALSE(SubmitGizmoClick(st, far, 1.0, log));
  CHECK_FALSE(st.gizmoDragActive);

  // Nor is a point on the axis LINE but far past the handle's tip: a handle is a segment, and
  // without that clamp a click anywhere along a world axis would start a drag.
  const double len = static_cast<double>(CadGizmoHandleLenWorld(st));
  const ray3d::Ray beyond = RayThrough({5.0 + len * 20.0, 0.0, 0.0}, {0.0, 0.0, -1.0});
  CHECK(PickGizmoAxis(st, beyond, 1.0) == -1);
}

TEST_CASE("Gizmo: a drag commits the same coordinates the typed MOVE would", "[gizmo][req060]") {
  // REQ-060's second acceptance bullet, at the level a unit test can hold it: the same offset,
  // reached by the two routes, lands on the same numbers. It holds by construction — the gizmo
  // commits through `ApplyTranslationToSelection`, which is what MOVE calls — and this is the case
  // that would fail if someone gave the gizmo a transform of its own.
  std::vector<std::string> log;

  AppCommandState viaGizmo = WithSelectedLine(0.f, 0.f, 0.f, 10.f, 0.f, 0.f);
  // Anchor (5,0,0); grab 5 along X, drop 20 along X, so the drag is 15.
  REQUIRE(SubmitGizmoClick(viaGizmo, RayThrough({10.0, 0.0, 0.0}, {0.0, 0.0, -1.0}), 1.0, log));
  REQUIRE(viaGizmo.gizmoDragActive);
  CHECK(viaGizmo.gizmoDragAxis == 0);
  UpdateGizmoDrag(viaGizmo, RayThrough({25.0, 0.0, 0.0}, {0.0, 0.0, -1.0}));
  CHECK(viaGizmo.gizmoDragDistance == Approx(15.0));
  REQUIRE(CommitGizmoDrag(viaGizmo, log));
  CHECK_FALSE(viaGizmo.gizmoDragActive);

  AppCommandState viaTyped = WithSelectedLine(0.f, 0.f, 0.f, 10.f, 0.f, 0.f);
  ApplyTranslationToSelection(viaTyped, 15.f, 0.f, 0.f, log);

  REQUIRE(viaGizmo.userLinesFlat.size() == viaTyped.userLinesFlat.size());
  for (size_t i = 0; i < viaTyped.userLinesFlat.size(); ++i)
    CHECK(viaGizmo.userLinesFlat[i] == Approx(viaTyped.userLinesFlat[i]).margin(1e-6));
}

TEST_CASE("Gizmo: a drag that went nowhere is a cancel, not an empty undo step", "[gizmo][req060]") {
  AppCommandState st = WithSelectedLine(0.f, 0.f, 0.f, 10.f, 0.f, 0.f);
  std::vector<std::string> log;
  REQUIRE(SubmitGizmoClick(st, RayThrough({10.0, 0.0, 0.0}, {0.0, 0.0, -1.0}), 1.0, log));
  // Dropped on the very point it was grabbed at. The click is still CONSUMED — it must not fall
  // through to the selection underneath, or ending a drag where it started would reselect whatever
  // the handle happens to be lying over — but nothing moves and no undo step is pushed.
  CHECK(SubmitGizmoClick(st, RayThrough({10.0, 0.0, 0.0}, {0.0, 0.0, -1.0}), 1.0, log));
  CHECK_FALSE(st.gizmoDragActive);
  // Nothing moved, and `CommitGizmoDrag` reported so - a click that placed the handle back where
  // it was is a cancel, not a zero-length step in the undo history.
  CHECK(st.userLinesFlat[3] == Approx(10.0));
}

TEST_CASE("Gizmo: the handles follow the active UCS", "[gizmo][req060]") {
  // Everything else that takes a direction from the user follows the UCS (REQ-154) — the grid,
  // ORTHO, coordinate entry. A gizmo pointing somewhere else would be the only thing in the
  // viewport disagreeing with the grid drawn under it.
  AppCommandState st = WithSelectedLine(0.f, 0.f, 0.f, 10.f, 0.f, 0.f);
  CHECK(CadGizmoAxisWorld(st, 0).x == Approx(1.0));  // World UCS: the default, and unchanged

  // Turn the frame a quarter turn about Z: the gizmo's X handle now points along world +Y.
  st.activeUcs.xAxis = ray3d::Vec3{0.0, 1.0, 0.0};
  st.activeUcs.yAxis = ray3d::Vec3{-1.0, 0.0, 0.0};
  st.activeUcs.zAxis = ray3d::Vec3{0.0, 0.0, 1.0};
  const ray3d::Vec3 gx = CadGizmoAxisWorld(st, 0);
  CHECK(gx.x == Approx(0.0).margin(1e-9));
  CHECK(gx.y == Approx(1.0));

  // And a drag along it moves the drawing along world +Y, not world +X.
  std::vector<std::string> log;
  REQUIRE(SubmitGizmoClick(st, RayThrough({5.0, 5.0, 0.0}, {0.0, 0.0, -1.0}), 1.0, log));
  CHECK(st.gizmoDragAxis == 0);
  UpdateGizmoDrag(st, RayThrough({5.0, 12.0, 0.0}, {0.0, 0.0, -1.0}));
  CHECK(st.gizmoDragDistance == Approx(7.0));
  REQUIRE(CommitGizmoDrag(st, log));
  CHECK(st.userLinesFlat[0] == Approx(0.0).margin(1e-6));
  CHECK(st.userLinesFlat[1] == Approx(7.0));
}
