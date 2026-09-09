// REQ-333 increment 2 (TASK-234), GitHub issue #148 acceptance 3 — the vertex and edge GRIPS.
//
// TASK-233 gave the kernel `brep::MoveVertex` and `brep::MoveEdge`; these are what makes them a
// grip rather than a capability nothing calls. The cases here own the two things a transcript states
// poorly: the handle COUNTS, which are each a statement about degrees of freedom, and the rule that
// **no handle is drawn where the drag would be refused** — a pyramid's apex and a cylinder's rim are
// both easy to pick and both impossible to move.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {

/// A drawing holding one solid, with one sub-object of \p kind at \p index selected.
AppCommandState WithSelectedSubObject(brep::Solid&& solid, solidpick::Kind kind, int index) {
  AppCommandState st;
  st.uiViewportWidthPx = 1200.f;
  st.uiViewportHeightPx = 700.f;
  auto sp = std::make_shared<const brep::Solid>(std::move(solid));
  st.cadSolids.push_back(sp);
  SelectedSubObject ref;
  ref.solidIndex = 0;
  ref.kind = kind;
  ref.index = index;
  ref.owner = sp;
  st.subObjectSelection.push_back(ref);
  return st;
}

brep::Solid Box(double l, double w, double h) {
  brep::Solid s;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(ucs::Ucs{}, l, w, h, &s, &why));
  return s;
}

int VertexAt(const brep::Solid& s, const ray3d::Vec3& p) {
  for (size_t i = 0; i < s.vertices.size(); ++i)
    if (ray3d::Length(ray3d::Sub(s.vertices[i].p, p)) < 1e-9)
      return static_cast<int>(i);
  REQUIRE(false);
  return -1;
}

int EdgeBetween(const brep::Solid& s, const ray3d::Vec3& a, const ray3d::Vec3& b) {
  const int va = VertexAt(s, a);
  const int vb = VertexAt(s, b);
  for (size_t i = 0; i < s.edges.size(); ++i) {
    const brep::Edge& e = s.edges[i];
    if (e.kind == brep::CurveKind::Line &&
        ((e.v0 == va && e.v1 == vb) || (e.v0 == vb && e.v1 == va)))
      return static_cast<int>(i);
  }
  REQUIRE(false);
  return -1;
}

double Volume(const brep::Solid& s) {
  const brep::MassProperties m = brep::ComputeMassProperties(s);
  REQUIRE(m.valid);
  return m.volume;
}

}  // namespace

TEST_CASE("A vertex grip has three handles, an edge grip two (REQ-333)", "[gizmo][movesub]") {
  const brep::Solid box = Box(20.0, 10.0, 8.0);

  SECTION("a VERTEX: three, because three planes meet and every direction is reachable") {
    const int v = VertexAt(box, {10.0, 5.0, 8.0});
    AppCommandState st = WithSelectedSubObject(Box(20.0, 10.0, 8.0), solidpick::Kind::Vertex, v);
    REQUIRE(CadGizmoModeFor(st) == CadGizmoMode::SubObjectVertex);
    REQUIRE(CadGizmoAxisCountFor(st) == 3);
    ray3d::Vec3 anchor{};
    REQUIRE(CadGizmoAnchorWorld(st, &anchor));
    // The handle hangs ON the vertex, not at a bounding-box centre.
    CHECK(anchor.x == Approx(10.0));
    CHECK(anchor.y == Approx(5.0));
    CHECK(anchor.z == Approx(8.0));
  }

  SECTION("an EDGE: two, and neither is along the edge") {
    const int e = EdgeBetween(box, {-10.0, 5.0, 8.0}, {10.0, 5.0, 8.0});  // runs along X
    AppCommandState st = WithSelectedSubObject(Box(20.0, 10.0, 8.0), solidpick::Kind::Edge, e);
    REQUIRE(CadGizmoModeFor(st) == CadGizmoMode::SubObjectEdge);
    REQUIRE(CadGizmoAxisCountFor(st) == 2);
    ray3d::Vec3 anchor{};
    REQUIRE(CadGizmoAnchorWorld(st, &anchor));
    // The MIDPOINT, so the handle reads as belonging to the edge rather than to one of its ends.
    CHECK(anchor.x == Approx(0.0).margin(1e-9));
    CHECK(anchor.y == Approx(5.0));
    CHECK(anchor.z == Approx(8.0));
    // The two adjacent faces' normals: +Y and +Z. Neither has a component along X, which is the
    // direction the drag cannot express and therefore must not offer.
    const ray3d::Vec3 a0 = CadGizmoAxisWorld(st, 0);
    const ray3d::Vec3 a1 = CadGizmoAxisWorld(st, 1);
    CHECK(std::fabs(a0.x) == Approx(0.0).margin(1e-9));
    CHECK(std::fabs(a1.x) == Approx(0.0).margin(1e-9));
    CHECK(std::fabs(ray3d::Dot(a0, a1)) == Approx(0.0).margin(1e-9));
  }
}

TEST_CASE("No handle is drawn where the kernel would refuse the drag (REQ-333)",
          "[gizmo][movesub]") {
  brep::Problem why{};

  SECTION("a PYRAMID's apex — four planes meet, so nothing can move it") {
    brep::Solid pyr;
    REQUIRE(brep::MakePyramid(ucs::Ucs{}, 4, 10.0, 0.0, 12.0, &pyr, &why));
    int apex = 0;
    for (size_t i = 1; i < pyr.vertices.size(); ++i)
      if (pyr.vertices[i].p.z > pyr.vertices[static_cast<size_t>(apex)].p.z)
        apex = static_cast<int>(i);
    AppCommandState st = WithSelectedSubObject(std::move(pyr), solidpick::Kind::Vertex, apex);
    // No gizmo at all, rather than one that appears and then declines on release. That is the same
    // discipline the face grip already keeps by returning false for a non-planar face.
    CHECK(CadGizmoModeFor(st) == CadGizmoMode::None);
    CHECK(CadGizmoAxisCountFor(st) == 0);
    CHECK_FALSE(CadGizmoVisible(st));
  }

  SECTION("a CYLINDER's rim vertex — a curved face meets there") {
    brep::Solid cyl;
    REQUIRE(brep::MakeCylinder(ucs::Ucs{}, 5.0, 10.0, &cyl, &why));
    AppCommandState st = WithSelectedSubObject(std::move(cyl), solidpick::Kind::Vertex, 0);
    CHECK(CadGizmoModeFor(st) == CadGizmoMode::None);
    CHECK_FALSE(CadGizmoVisible(st));
  }
}

TEST_CASE("A vertex grip drag moves the solid exactly as the kernel does (REQ-333)",
          "[gizmo][movesub]") {
  // The single-implementation rule, asserted the only way it can be here: there is no typed command
  // for this, so what is compared is the grip's result against `brep::MoveVertex` called directly
  // with the same delta. If the commit ever grows its own arithmetic, these diverge.
  std::vector<std::string> log;
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int v = VertexAt(box, {10.0, 5.0, 8.0});
  AppCommandState st = WithSelectedSubObject(Box(20.0, 10.0, 8.0), solidpick::Kind::Vertex, v);

  const ray3d::Vec3 delta{3.0, 0.0, 0.0};
  REQUIRE(CadApplyMoveVertex(st, st.subObjectSelection.front(), delta, log));

  brep::Solid direct;
  brep::Problem why{};
  REQUIRE(brep::MoveVertex(box, v, delta, &direct, &why));
  REQUIRE(st.cadSolids[0]);
  REQUIRE(st.cadSolids[0]->vertices.size() == direct.vertices.size());
  for (size_t i = 0; i < direct.vertices.size(); ++i)
    CHECK(ray3d::Length(ray3d::Sub(st.cadSolids[0]->vertices[i].p, direct.vertices[i].p)) < 1e-12);
  // +X slid by 3, so the box is 23 x 10 x 8.
  CHECK(Volume(*st.cadSolids[0]) == Approx(23.0 * 10.0 * 8.0));
}

TEST_CASE("The sub-object selection survives the edit (REQ-333)", "[gizmo][movesub]") {
  // The reference is keyed on the solid's IDENTITY (ADR-049) and the solid has just been replaced,
  // so without re-pointing it would expire and a second drag would need a re-pick. Push/pull already
  // had to solve this; these operations preserve topology counts for the same reason.
  std::vector<std::string> log;
  const brep::Solid box = Box(20.0, 10.0, 8.0);
  const int e = EdgeBetween(box, {-10.0, 5.0, 8.0}, {10.0, 5.0, 8.0});
  AppCommandState st = WithSelectedSubObject(Box(20.0, 10.0, 8.0), solidpick::Kind::Edge, e);

  REQUIRE(CadApplyMoveEdge(st, st.subObjectSelection.front(), {0.0, 2.0, 0.0}, log));
  REQUIRE(st.subObjectSelection.size() == 1);
  CHECK_FALSE(st.subObjectSelection.front().owner.expired());
  CHECK(st.subObjectSelection.front().owner.lock() == st.cadSolids[0]);
  // ...and the grip still resolves, so a second drag needs no re-pick.
  CHECK(CadGizmoModeFor(st) == CadGizmoMode::SubObjectEdge);

  // A second drag compounds on the first: +Y has now slid 2 then 1, so the box is 20 x 13 x 8.
  REQUIRE(CadApplyMoveEdge(st, st.subObjectSelection.front(), {0.0, 1.0, 0.0}, log));
  CHECK(Volume(*st.cadSolids[0]) == Approx(20.0 * 13.0 * 8.0));
}

TEST_CASE("A refused drag leaves the document untouched and says why (REQ-333 / REQ-201)",
          "[gizmo][movesub]") {
  std::vector<std::string> log;
  brep::Solid pyr;
  brep::Problem why{};
  REQUIRE(brep::MakePyramid(ucs::Ucs{}, 4, 10.0, 0.0, 12.0, &pyr, &why));
  const double before = Volume(pyr);
  int apex = 0;
  for (size_t i = 1; i < pyr.vertices.size(); ++i)
    if (pyr.vertices[i].p.z > pyr.vertices[static_cast<size_t>(apex)].p.z)
      apex = static_cast<int>(i);
  AppCommandState st = WithSelectedSubObject(std::move(pyr), solidpick::Kind::Vertex, apex);

  const size_t logBefore = log.size();
  REQUIRE_FALSE(CadApplyMoveVertex(st, st.subObjectSelection.front(), {1.0, 0.0, 0.0}, log));
  CHECK(log.size() > logBefore);                       // it said something
  CHECK(Volume(*st.cadSolids[0]) == Approx(before));   // and changed nothing
}
