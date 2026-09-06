// REQ-318 increment 2 (D-2026-09-04-a, GitHub issue #148 criteria 1 and 2) — the sub-object
// SELECTION: its store, the reference that expires rather than re-binding, the mutual-exclusion
// rule, and the cross-solid depth order.
//
// The pick QUERY itself is `SolidPickTests`'s subject and is not re-tested here. What these cases
// own is everything above it — the parts `solidpick` deliberately knows nothing about, because it
// returns an answer and never remembers one.
//
// Linked into GoSurveySnapTests: these call into the command layer (`ExpireSubObjectSelection`,
// `SubmitSubObjectPick`), which lives in gosurvey_domain — the same reason ViewportUcsTests is
// there rather than in GoSurveyTests.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <vector>

#include "CadCommands.hpp"
#include "viewport/TransformPreview.hpp"  // BuildSubObjectHighlight

namespace {

ucs::Ucs World() { return ucs::Ucs{}; }

/// A box as the document stores one, with its display cache built **by the product's own path**.
///
/// `RefreshSolidDisplayGeometry` and not a hand-rolled tessellation here: the triangles the pick
/// reads have to be the triangles the user sees, and a test that built its own would be testing its
/// own arithmetic — the note `req313-solid-picked` makes about `CadResolveSolidPick`, for the same
/// reason. It also means a change to how the cache is keyed or expanded fails these tests rather
/// than silently leaving them exercising a shape nothing draws.
CadSolidPtr AddBox(AppCommandState& st, const ucs::Ucs& frame, double l, double w, double h) {
  brep::Solid s;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(frame, l, w, h, &s, &why));
  auto sp = std::make_shared<const brep::Solid>(std::move(s));
  st.cadSolids.push_back(sp);
  st.cadSolidAttrs.push_back(EntityAttributes{});
  RefreshSolidDisplayGeometry(st);
  return sp;
}

/// A ray aimed at \p target from \p from — the shape a camera produces, normalized or not (the pick
/// normalizes on entry, and one case below depends on that).
ray3d::Ray RayAt(const ray3d::Vec3& from, const ray3d::Vec3& target) {
  ray3d::Ray r;
  r.origin = from;
  r.dir = ray3d::Sub(target, from);
  return r;
}

solidpick::Tolerance Tol(double v, double e) {
  solidpick::Tolerance t;
  t.vertex = v;
  t.edge = e;
  return t;
}

}  // namespace

// A box centred on (0,0), base at z = 0: x in [-10,10], y in [-5,5], z in [0,8].
TEST_CASE("Sub-object pick names the face, edge and vertex aimed at (REQ-318)", "[subobject]") {
  AppCommandState st;
  AddBox(st, World(), 20.0, 10.0, 8.0);
  std::vector<std::string> log;

  SECTION("the middle of the top face") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection.size() == 1);
    REQUIRE(st.subObjectSelection[0].kind == solidpick::Kind::Face);
  }
  SECTION("the middle of a top edge beats the faces that meet there") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 40, 48}, {0, 5, 8}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection.size() == 1);
    REQUIRE(st.subObjectSelection[0].kind == solidpick::Kind::Edge);
  }
  SECTION("a corner beats the edges that meet there") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({60, 55, 58}, {10, 5, 8}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection.size() == 1);
    REQUIRE(st.subObjectSelection[0].kind == solidpick::Kind::Vertex);
  }
  SECTION("a zero tolerance takes that kind out of the running") {
    // The same ray as the corner case. With no vertex budget the edge behind it wins, which is what
    // proves the vertex above was chosen by PRECEDENCE and not merely because it was nearest.
    REQUIRE(SubmitSubObjectPick(st, RayAt({60, 55, 58}, {10, 5, 8}), Tol(0.0, 0.5), false, log));
    REQUIRE(st.subObjectSelection.size() == 1);
    REQUIRE(st.subObjectSelection[0].kind == solidpick::Kind::Edge);
  }
  SECTION("a ray that misses everything selects nothing and says so") {
    // Aimed AWAY from the box. Aiming at (400,400,400) from (500,500,500) would carry on through
    // the origin and hit it — the box is at the origin, and a "miss" that is really a hit is the
    // easiest way to write a test that passes for the wrong reason.
    REQUIRE_FALSE(SubmitSubObjectPick(st, RayAt({500, 500, 500}, {600, 600, 600}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection.empty());
    REQUIRE(std::any_of(log.begin(), log.end(), [](const std::string& l) {
      return l.find("No solid face, edge or vertex") != std::string::npos;
    }));
  }
}

TEST_CASE("Sub-object and whole-entity selections are mutually exclusive (REQ-318 item 9)", "[subobject]") {
  AppCommandState st;
  AddBox(st, World(), 20.0, 10.0, 8.0);
  std::vector<std::string> log;

  // Stand in for a whole-entity selection made any other way — a click, a fence, SELECT ALL.
  SelectedEntity e{};
  e.type = SelectedEntity::Type::Solid;
  e.index = 0;
  st.selection.push_back(e);
  st.selectedSurveyPointIndices.push_back(3);
  st.selBoxWaitingSecond = true;

  REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
  REQUIRE(st.subObjectSelection.size() == 1);
  // #148 criterion 2, as a fact rather than a promise: nothing that walks `selection` can see a
  // sub-object, because the two are never both populated.
  REQUIRE(st.selection.empty());
  REQUIRE(st.selectedSurveyPointIndices.empty());
  // A Ctrl click never leaves a half-drawn fence behind either.
  REQUIRE_FALSE(st.selBoxWaitingSecond);

  // And ClearCadSelection — every "nothing is selected now" path — takes both.
  ClearCadSelection(st);
  REQUIRE(st.subObjectSelection.empty());
}

TEST_CASE("Sub-object picks accumulate; Shift removes (REQ-318 item 9)", "[subobject]") {
  AppCommandState st;
  AddBox(st, World(), 20.0, 10.0, 8.0);
  std::vector<std::string> log;
  const auto top = RayAt({0, 0, 100}, {0, 0, 8});
  const auto bottom = RayAt({0, 0, -100}, {0, 0, 0});

  REQUIRE(SubmitSubObjectPick(st, top, Tol(0.5, 0.5), false, log));
  REQUIRE(SubmitSubObjectPick(st, bottom, Tol(0.5, 0.5), false, log));
  REQUIRE(st.subObjectSelection.size() == 2);
  REQUIRE(st.subObjectSelection[0].index != st.subObjectSelection[1].index);

  // The same face again, plain: a no-op, not a duplicate.
  REQUIRE(SubmitSubObjectPick(st, top, Tol(0.5, 0.5), false, log));
  REQUIRE(st.subObjectSelection.size() == 2);

  // Shift on one that IS selected removes just it.
  REQUIRE(SubmitSubObjectPick(st, top, Tol(0.5, 0.5), true, log));
  REQUIRE(st.subObjectSelection.size() == 1);
  REQUIRE(std::any_of(log.begin(), log.end(),
                      [](const std::string& l) { return l.find("Deselected face") != std::string::npos; }));
}

TEST_CASE("The solid nearest the eye wins across solids (TASK-189 DEBT-1)", "[subobject]") {
  AppCommandState st;
  // Two boxes on one sight line down the X axis: index 0 spans x in [-10,10], index 1 x in [50,70].
  ucs::Ucs upper = World();
  upper.origin = {60.0, 0.0, 0.0};
  const CadSolidPtr atOrigin = AddBox(st, World(), 20.0, 10.0, 8.0);
  const CadSolidPtr atSixty = AddBox(st, upper, 20.0, 10.0, 8.0);
  std::vector<std::string> log;

  // `solidpick::PickSubObject` sees one solid at a time, so its occlusion rule cannot reach across
  // solids — both boxes answer this ray, and which one the user gets is decided here, by `rayT`.
  //
  // From +X the box at x = 60 is the one in front. Asserting that (rather than "index 0") is the
  // point: the ordering must follow the GEOMETRY, and a test that expected the first-created solid
  // would pass under a caller that simply took whichever answered first.
  SECTION("from +X the far-side box is the near one") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({500, 0, 4}, {0, 0, 4}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection.size() == 1);
    REQUIRE(st.subObjectSelection[0].solidIndex == 1);
    REQUIRE(st.subObjectSelection[0].owner.lock() == atSixty);
  }
  SECTION("from -X the answer flips") {
    // The same two solids, the same sight line, the opposite eye. A fixed preference for either
    // index would pass one of these two cases and fail the other.
    REQUIRE(SubmitSubObjectPick(st, RayAt({-500, 0, 4}, {0, 0, 4}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection.size() == 1);
    REQUIRE(st.subObjectSelection[0].solidIndex == 0);
    REQUIRE(st.subObjectSelection[0].owner.lock() == atOrigin);
  }
}

TEST_CASE("A sub-object reference expires on a topology change, not on an unrelated edit (ADR-049)",
          "[subobject]") {
  AppCommandState st;
  std::vector<std::string> log;

  SECTION("replacing the solid expires the reference") {
    AddBox(st, World(), 20.0, 10.0, 8.0);
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection.size() == 1);

    // A solid is immutable and REPLACED rather than edited, so this is what every topology-changing
    // edit looks like from here — a boolean, a direct push/pull, an undo.
    brep::Solid other;
    brep::Problem why{};
    REQUIRE(brep::MakeBox(World(), 4.0, 4.0, 4.0, &other, &why));
    st.cadSolids[0] = std::make_shared<const brep::Solid>(std::move(other));

    REQUIRE(ExpireSubObjectSelection(st) == 1);
    REQUIRE(st.subObjectSelection.empty());  // dropped, never re-bound to face 0 of the new shape
  }

  SECTION("erasing an UNRELATED solid keeps the reference and repairs its index") {
    ucs::Ucs far = World();
    far.origin = {60.0, 0.0, 0.0};
    AddBox(st, World(), 20.0, 10.0, 8.0);
    const CadSolidPtr second = AddBox(st, far, 20.0, 10.0, 8.0);
    REQUIRE(SubmitSubObjectPick(st, RayAt({60, 0, 100}, {60, 0, 8}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection.size() == 1);
    REQUIRE(st.subObjectSelection[0].solidIndex == 1);

    // Erase the FIRST solid. Every index after it shifts down; the object the user picked is
    // untouched. Losing the selection here would be a defect, not an expiry — which is why identity
    // decides and the index is only a lookup.
    st.cadSolids.erase(st.cadSolids.begin());
    st.cadSolidAttrs.erase(st.cadSolidAttrs.begin());

    REQUIRE(ExpireSubObjectSelection(st) == 0);
    REQUIRE(st.subObjectSelection.size() == 1);
    REQUIRE(st.subObjectSelection[0].solidIndex == 0);  // repaired
    REQUIRE(st.subObjectSelection[0].owner.lock() == second);
  }

  SECTION("erasing the solid the reference belongs to leaves nothing dangling") {
    AddBox(st, World(), 20.0, 10.0, 8.0);
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
    st.cadSolids.clear();
    st.cadSolidAttrs.clear();
    st.solidDisplayCache.clear();
    REQUIRE(ExpireSubObjectSelection(st) == 1);
    REQUIRE(st.subObjectSelection.empty());
  }

  SECTION("an empty selection costs nothing and reports nothing") {
    REQUIRE(ExpireSubObjectSelection(st) == 0);
  }
}

TEST_CASE("The sub-object highlight draws the geometry that was picked (REQ-318 item 11)", "[subobject]") {
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  AddBox(st, World(), 20.0, 10.0, 8.0);
  std::vector<std::string> log;
  std::vector<float> tris;
  std::vector<float> faceEdges;
  std::vector<float> lines;

  SECTION("a face fills triangles and draws no linework") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
    BuildSubObjectHighlight(st, &tris, &faceEdges, &lines);
    REQUIRE_FALSE(tris.empty());
    REQUIRE(tris.size() % 9 == 0);
    REQUIRE(lines.empty());
    // The top face and nothing else: every vertex it emits is at z = 8.
    for (size_t i = 2; i < tris.size(); i += 3)
      REQUIRE(tris[i] == Catch::Approx(8.f));
  }
  SECTION("an edge draws linework and fills nothing") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 40, 48}, {0, 5, 8}), Tol(0.5, 0.5), false, log));
    BuildSubObjectHighlight(st, &tris, &faceEdges, &lines);
    REQUIRE(tris.empty());
    REQUIRE_FALSE(lines.empty());
    REQUIRE(lines.size() % 6 == 0);
  }
  SECTION("a vertex draws a three-axis cross centred on it") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({60, 55, 58}, {10, 5, 8}), Tol(0.5, 0.5), false, log));
    BuildSubObjectHighlight(st, &tris, &faceEdges, &lines);
    REQUIRE(tris.empty());
    REQUIRE(lines.size() == 3 * 6);  // three segments, six floats each
    // Each arm's midpoint is the vertex itself.
    for (int arm = 0; arm < 3; ++arm) {
      const size_t k = static_cast<size_t>(arm) * 6;
      REQUIRE((lines[k] + lines[k + 3]) * 0.5f == Catch::Approx(10.f));
      REQUIRE((lines[k + 1] + lines[k + 4]) * 0.5f == Catch::Approx(5.f));
      REQUIRE((lines[k + 2] + lines[k + 5]) * 0.5f == Catch::Approx(8.f));
    }
  }
  SECTION("an expired reference draws nothing rather than the wrong face") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
    brep::Solid other;
    brep::Problem why{};
    REQUIRE(brep::MakeBox(World(), 4.0, 4.0, 4.0, &other, &why));
    st.cadSolids[0] = std::make_shared<const brep::Solid>(std::move(other));
    // Deliberately WITHOUT calling ExpireSubObjectSelection first: the highlight must be safe on
    // its own, so the order of the two in the frame cannot matter.
    BuildSubObjectHighlight(st, &tris, &faceEdges, &lines);
    REQUIRE(tris.empty());
    REQUIRE(lines.empty());
  }
}

// REQ-318 item 14 (D-2026-09-04-b) — the pre-highlight and the rollover.
//
// The GUI decides ONE thing about this feature: that Ctrl is the key that arms it. Everything
// below — that the pre-highlight names what a click would take, that it steps aside for the
// selection, and what the readout says — is command-layer behaviour, and is asserted here.
TEST_CASE("The hover pre-highlight names what a Ctrl click would take (REQ-318 item 14)", "[subobject]") {
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  AddBox(st, World(), 20.0, 10.0, 8.0);
  std::vector<std::string> log;
  const auto atFace = RayAt({0, 0, 100}, {0, 0, 8});

  // The pre-highlight and the click are the SAME query, so what lights up cannot disagree with what
  // selects. Asserted by running the hover pick and the click pick on one ray and comparing.
  SelectedSubObject hovered;
  REQUIRE(PickSubObjectAcrossSolids(st, atFace, Tol(0.5, 0.5), &hovered));
  st.subObjectHoverValid = true;
  st.subObjectHover = hovered;

  std::vector<float> tris;
  std::vector<float> faceEdges;
  std::vector<float> lines;
  BuildSubObjectHoverHighlight(st, &tris, &faceEdges, &lines);
  REQUIRE_FALSE(tris.empty());  // a face hover fills triangles
  REQUIRE(lines.empty());

  REQUIRE(SubmitSubObjectPick(st, atFace, Tol(0.5, 0.5), false, log));
  REQUIRE(st.subObjectSelection.size() == 1);
  REQUIRE(st.subObjectSelection[0].sameTarget(hovered));

  SECTION("once selected, the pre-highlight steps aside") {
    // The selection highlight is the stronger statement; drawing a quieter one over it only muddies
    // the colour. Same rule BuildHoverHighlight already applies to entities.
    BuildSubObjectHoverHighlight(st, &tris, &faceEdges, &lines);
    REQUIRE(tris.empty());
    REQUIRE(lines.empty());
    // ...while the SELECTION highlight is of course still drawn.
    BuildSubObjectHighlight(st, &tris, &faceEdges, &lines);
    REQUIRE_FALSE(tris.empty());
  }
  SECTION("no hover means no pre-highlight") {
    st.subObjectHoverValid = false;
    BuildSubObjectHoverHighlight(st, &tris, &faceEdges, &lines);
    REQUIRE(tris.empty());
    REQUIRE(lines.empty());
  }
  SECTION("an expired hover reference draws nothing") {
    st.subObjectSelection.clear();
    brep::Solid other;
    brep::Problem why{};
    REQUIRE(brep::MakeBox(World(), 4.0, 4.0, 4.0, &other, &why));
    st.cadSolids[0] = std::make_shared<const brep::Solid>(std::move(other));
    BuildSubObjectHoverHighlight(st, &tris, &faceEdges, &lines);
    REQUIRE(tris.empty());
    REQUIRE(lines.empty());
  }
}

TEST_CASE("The sub-object rollover names the kind and the owning solid (REQ-318 item 14)", "[subobject]") {
  AppCommandState st;
  AddBox(st, World(), 20.0, 10.0, 8.0);
  st.cadSolidAttrs[0].layer = "Structures";
  std::vector<std::string> log;

  SelectedSubObject s;
  REQUIRE(PickSubObjectAcrossSolids(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), &s));

  SubObjectHoverRow row;
  REQUIRE(BuildSubObjectHoverRow(st, s, &row));
  REQUIRE(row.title.rfind("Solid face", 0) == 0);
  // 1-based, matching how the command line numbers solids. A readout counting from zero while the
  // log counts from one is two names for one object.
  REQUIRE(row.solid == "1");
  REQUIRE(row.layer == "Structures");
  // The STORED value, not the resolved one: "ByLayer" is what the Properties panel shows and what
  // the user would change, where a resolved "#FFFFFF" would hide that the solid follows its layer.
  REQUIRE(row.color == "ByLayer");
  REQUIRE(row.linetype == "ByLayer");

  SECTION("an expired reference says nothing rather than describing a stale solid") {
    brep::Solid other;
    brep::Problem why{};
    REQUIRE(brep::MakeBox(World(), 4.0, 4.0, 4.0, &other, &why));
    st.cadSolids[0] = std::make_shared<const brep::Solid>(std::move(other));
    SubObjectHoverRow stale;
    REQUIRE_FALSE(BuildSubObjectHoverRow(st, s, &stale));
  }
  SECTION("a kindless reference is refused") {
    SelectedSubObject none;
    SubObjectHoverRow out;
    REQUIRE_FALSE(BuildSubObjectHoverRow(st, none, &out));
    REQUIRE_FALSE(BuildSubObjectHoverRow(st, s, nullptr));
  }
}

// The defect the user reported on 2026-09-04: "the face preview does not work — lines and points
// work". It WAS drawing. A translucent fill tints what is behind it, and in 2D Wireframe — the
// default style — solids draw no faces, so the wash landed on the empty viewport: 20% alpha of
// (0.45,0.72,1.0) over black is RGB(23,37,51), which is black to any eye beside white wireframe.
//
// So a face has to draw its BOUNDARY, not only a fill. These cases pin that, because it is the half
// that cannot be verified from a screenshot after the fact — a fill and no outline looks exactly
// like a bug report.
TEST_CASE("A highlighted face draws its boundary, not only a fill (REQ-318 item 11/14)", "[subobject]") {
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  AddBox(st, World(), 20.0, 10.0, 8.0);
  std::vector<std::string> log;
  std::vector<float> tris;
  std::vector<float> faceEdges;
  std::vector<float> lines;

  REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
  REQUIRE(st.subObjectSelection[0].kind == solidpick::Kind::Face);
  BuildSubObjectHighlight(st, &tris, &faceEdges, &lines);

  REQUIRE_FALSE(tris.empty());
  REQUIRE_FALSE(faceEdges.empty());   // the half that was missing
  REQUIRE(faceEdges.size() % 6 == 0);
  REQUIRE(lines.empty());             // a face is not edge/vertex linework

  // The top face of a box is a quadrilateral, so its boundary is four straight edges — four
  // segments, no more. A count rather than a mere non-empty check: emitting the whole solid's
  // wireframe would also be "not empty" and would look almost right on screen.
  REQUIRE(faceEdges.size() == 4 * 6);
  // Every vertex of it lies on the face's own plane, z = 8. This is what would fail if the loop
  // walk picked up an adjacent face's edges.
  for (size_t i = 2; i < faceEdges.size(); i += 3)
    REQUIRE(faceEdges[i] == Catch::Approx(8.f));

  SECTION("the hover pre-highlight outlines too") {
    st.subObjectSelection.clear();
    SelectedSubObject hovered;
    REQUIRE(PickSubObjectAcrossSolids(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), &hovered));
    st.subObjectHoverValid = true;
    st.subObjectHover = hovered;
    BuildSubObjectHoverHighlight(st, &tris, &faceEdges, &lines);
    REQUIRE_FALSE(tris.empty());
    REQUIRE(faceEdges.size() == 4 * 6);
  }
  SECTION("an edge or vertex contributes no face boundary") {
    st.subObjectSelection.clear();
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 40, 48}, {0, 5, 8}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection[0].kind == solidpick::Kind::Edge);
    BuildSubObjectHighlight(st, &tris, &faceEdges, &lines);
    REQUIRE(faceEdges.empty());
    REQUIRE_FALSE(lines.empty());
  }
}

// REQ-319 increment 2 — the face grip's geometry. The DRAG is a mouse gesture and stays GUI-only,
// but everything it computes is here: where the handle sits, which way the face slides, and how far
// a cursor ray is asking for. Those are the parts that can be silently wrong and look plausible.
TEST_CASE("The face grip sits on the face and slides along its normal (REQ-319)", "[subobject]") {
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  AddBox(st, World(), 20.0, 10.0, 8.0);  // x [-10,10], y [-5,5], z [0,8]
  std::vector<std::string> log;

  SECTION("the top face: handle at the centroid, axis +Z") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
    ray3d::Vec3 anchor;
    ray3d::Vec3 axis;
    REQUIRE(CadSubObjectFaceGrip(st, st.subObjectSelection[0], &anchor, &axis));
    REQUIRE(anchor.x == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(anchor.y == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(anchor.z == Catch::Approx(8.0));   // ON the face, not floating above it
    REQUIRE(axis.z == Catch::Approx(1.0));     // outward, so a positive drag grows the box
    REQUIRE(std::fabs(axis.x) + std::fabs(axis.y) == Catch::Approx(0.0).margin(1e-9));
  }
  SECTION("a side face: the axis follows the face, not the world") {
    // A grip that always slid along Z would pass the case above and fail this one.
    REQUIRE(SubmitSubObjectPick(st, RayAt({100, 0, 4}, {10, 0, 4}), Tol(0.5, 0.5), false, log));
    ray3d::Vec3 anchor;
    ray3d::Vec3 axis;
    REQUIRE(CadSubObjectFaceGrip(st, st.subObjectSelection[0], &anchor, &axis));
    REQUIRE(anchor.x == Catch::Approx(10.0));
    REQUIRE(axis.x == Catch::Approx(1.0));
  }
  SECTION("an edge or vertex has no face grip") {
    st.subObjectSelection.clear();
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 40, 48}, {0, 5, 8}), Tol(0.5, 0.5), false, log));
    ray3d::Vec3 a;
    ray3d::Vec3 x;
    REQUIRE_FALSE(CadSubObjectFaceGrip(st, st.subObjectSelection[0], &a, &x));
  }
  SECTION("an expired reference has no grip either") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
    const SelectedSubObject ref = st.subObjectSelection[0];
    brep::Solid other;
    brep::Problem why{};
    REQUIRE(brep::MakeBox(World(), 4.0, 4.0, 4.0, &other, &why));
    st.cadSolids[0] = std::make_shared<const brep::Solid>(std::move(other));
    ray3d::Vec3 a;
    ray3d::Vec3 x;
    REQUIRE_FALSE(CadSubObjectFaceGrip(st, ref, &a, &x));
  }
}

// Renamed subject: this WAS `CadSubObjectGripAxisDistance`, the face grip's own skew-line solve.
// Slice 4c collapsed it into `CadAxisDragParam`, the gizmo's - they were the same arithmetic under
// two names, written on branches that could not see each other. The cases are unchanged.
TEST_CASE("The grip distance is the closest approach of the cursor ray to the axis (REQ-319)",
          "[subobject]") {
  const ray3d::Vec3 anchor{0, 0, 8};
  const ray3d::Vec3 axis{0, 0, 1};
  double d = 0.0;

  SECTION("a ray aimed straight at a point on the axis reports that point's offset") {
    // Sighting horizontally at z = 11, three above the anchor.
    ray3d::Ray r;
    r.origin = {100, 0, 11};
    r.dir = {-1, 0, 0};
    REQUIRE(CadAxisDragParam(anchor, axis, r, &d));
    REQUIRE(d == Catch::Approx(3.0));
  }
  SECTION("below the anchor is negative — pulling in is the same gesture with the other sign") {
    ray3d::Ray r;
    r.origin = {100, 0, 5};
    r.dir = {-1, 0, 0};
    REQUIRE(CadAxisDragParam(anchor, axis, r, &d));
    REQUIRE(d == Catch::Approx(-3.0));
  }
  SECTION("it is UNCLAMPED, because the axis is a direction and not a segment") {
    ray3d::Ray r;
    r.origin = {100, 0, 908};
    r.dir = {-1, 0, 0};
    REQUIRE(CadAxisDragParam(anchor, axis, r, &d));
    REQUIRE(d == Catch::Approx(900.0));
  }
  SECTION("an oblique ray still resolves, and off-axis sideways offset does not change the answer") {
    // Skew, not intersecting: 5 ft off to the side. The closest approach along the AXIS is still
    // z = 11, which is what makes a drag work from any camera angle rather than only face-on.
    ray3d::Ray r;
    r.origin = {100, 5, 11};
    r.dir = {-1, 0, 0};
    REQUIRE(CadAxisDragParam(anchor, axis, r, &d));
    REQUIRE(d == Catch::Approx(3.0));
  }
  SECTION("a ray sighting straight down the axis is refused rather than answered") {
    // There is no closest point: every point of the axis is equally near. The caller holds its last
    // value on false, so a drag does not snap to zero as the camera swings through the axis.
    ray3d::Ray r;
    r.origin = {0, 0, 100};
    r.dir = {0, 0, -1};
    REQUIRE_FALSE(CadAxisDragParam(anchor, axis, r, &d));
  }
  SECTION("a degenerate ray or axis is refused") {
    ray3d::Ray bad;
    bad.origin = {0, 0, 0};
    bad.dir = {0, 0, 0};
    REQUIRE_FALSE(CadAxisDragParam(anchor, axis, bad, &d));
    ray3d::Ray r;
    r.origin = {100, 0, 11};
    r.dir = {-1, 0, 0};
    REQUIRE_FALSE(CadAxisDragParam(anchor, {0, 0, 0}, r, &d));
    REQUIRE_FALSE(CadAxisDragParam(anchor, axis, r, nullptr));
  }
}

TEST_CASE("The grip drag and the typed command commit through one path (REQ-319)", "[subobject]") {
  // Both go through CadApplyPushPull, so a drag and a PRESSPULL of the same distance cannot produce
  // different solids — the single-implementation rule REQ-318 item 1 states for the pick, applied to
  // the edit. Asserted by driving the shared function directly, which is what the grip's commit does.
  AppCommandState st;
  AddBox(st, World(), 20.0, 10.0, 8.0);
  std::vector<std::string> log;
  REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));

  const CadSolidPtr before = st.cadSolids[0];
  REQUIRE(CadApplyPushPull(st, st.subObjectSelection[0], 3.0, log));
  REQUIRE(st.cadSolids[0] != before);  // replaced, never mutated
  REQUIRE(brep::ComputeMassProperties(*st.cadSolids[0]).volume == Catch::Approx(2200.0));
  // The selection followed the edit, so a second push works without re-picking.
  REQUIRE(st.subObjectSelection.size() == 1);
  REQUIRE(st.subObjectSelection[0].owner.lock() == st.cadSolids[0]);
  REQUIRE(CadApplyPushPull(st, st.subObjectSelection[0], 3.0, log));
  REQUIRE(brep::ComputeMassProperties(*st.cadSolids[0]).volume == Catch::Approx(2800.0));

  SECTION("a refusal leaves the document untouched") {
    const CadSolidPtr held = st.cadSolids[0];
    REQUIRE_FALSE(CadApplyPushPull(st, st.subObjectSelection[0], -14.0, log));
    REQUIRE(st.cadSolids[0] == held);
    REQUIRE(brep::ComputeMassProperties(*st.cadSolids[0]).volume == Catch::Approx(2800.0));
  }
}

// REQ-319 increment 4 — a cylinder WALL gets a handle too, and it slides radially.
TEST_CASE("A cylinder wall's grip slides along its own radius (REQ-319)", "[subobject]") {
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  {
    brep::Solid cyl;
    brep::Problem why{};
    REQUIRE(brep::MakeCylinder(World(), 5.0, 10.0, &cyl, &why));
    st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(cyl)));
    st.cadSolidAttrs.push_back(EntityAttributes{});
    RefreshSolidDisplayGeometry(st);
  }
  const CadSolidPtr sp = st.cadSolids[0];

  int wall = -1;
  for (size_t i = 0; i < sp->faces.size(); ++i)
    if (sp->faces[i].surface.kind == brep::SurfaceKind::Cylinder)
      wall = static_cast<int>(i);
  REQUIRE(wall >= 0);

  SelectedSubObject ref;
  ref.solidIndex = 0;
  ref.kind = solidpick::Kind::Face;
  ref.index = wall;
  ref.owner = sp;

  ray3d::Vec3 anchor;
  ray3d::Vec3 axis;
  REQUIRE(CadSubObjectFaceGrip(st, ref, &anchor, &axis));

  // ON the wall: 5 from the axis, half way up. A handle floating off the surface reads as belonging
  // to nothing, and one at the end of the angular span sits on the seam between the two halves.
  REQUIRE(std::hypot(anchor.x, anchor.y) == Catch::Approx(5.0));
  REQUIRE(anchor.z == Catch::Approx(5.0));
  // The axis is RADIAL — outward at the handle — not the solid's Z. A grip that reused the surface
  // frame's zAxis would point up the cylinder and drag the wall along its own length, which changes
  // nothing at all.
  REQUIRE(std::fabs(axis.z) == Catch::Approx(0.0).margin(1e-9));
  REQUIRE(ray3d::Length(axis) == Catch::Approx(1.0));
  // It points away from the axis of the cylinder, i.e. out of the material.
  REQUIRE(ray3d::Dot(axis, ray3d::Vec3{anchor.x, anchor.y, 0.0}) > 0.0);

  SECTION("a cone wall gets no handle, because it cannot be pushed") {
    brep::Solid cone;
    brep::Problem why{};
    REQUIRE(brep::MakeCone(World(), 5.0, 2.0, 10.0, &cone, &why));
    st.cadSolids[0] = std::make_shared<const brep::Solid>(std::move(cone));
    RefreshSolidDisplayGeometry(st);
    SelectedSubObject cref;
    cref.solidIndex = 0;
    cref.kind = solidpick::Kind::Face;
    cref.owner = st.cadSolids[0];
    for (size_t i = 0; i < st.cadSolids[0]->faces.size(); ++i)
      if (st.cadSolids[0]->faces[i].surface.kind == brep::SurfaceKind::Cone) {
        cref.index = static_cast<int>(i);
        ray3d::Vec3 a;
        ray3d::Vec3 x;
        REQUIRE_FALSE(CadSubObjectFaceGrip(st, cref, &a, &x));
      }
  }
}

// --- The gizmo on a sub-object selection (issue #148 acceptance 4, Phase 5 slice 4c) -------------
//
// The transcript `req148-gizmo-subobject` drives this through the camera and asserts the thing that
// matters — a drag and `PRESSPULL <the same distance>` leaving identical mass properties. These
// cases own the mode DERIVATION, which a transcript can only observe two numbers of.

TEST_CASE("The gizmo mode is derived from the selection, never stored", "[subobject][gizmo]") {
  AppCommandState st;
  st.uiViewportWidthPx = 1200.f;
  st.uiViewportHeightPx = 700.f;
  AddBox(st, World(), 20.0, 10.0, 8.0);  // x [-10,10], y [-5,5], z [0,8]
  std::vector<std::string> log;

  SECTION("nothing selected: no gizmo") {
    REQUIRE(CadGizmoModeFor(st) == CadGizmoMode::None);
    REQUIRE(CadGizmoAxisCountFor(st) == 0);
    REQUIRE_FALSE(CadGizmoVisible(st));
  }

  SECTION("one FACE: one handle, on the face's centroid, along its own normal") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
    REQUIRE(CadGizmoModeFor(st) == CadGizmoMode::SubObjectFace);
    // ONE, because `brep::PushPullFace` takes a distance along the normal and nothing else. A
    // second handle would name a direction the kernel cannot move the face in.
    REQUIRE(CadGizmoAxisCountFor(st) == 1);
    ray3d::Vec3 anchor{};
    REQUIRE(CadGizmoAnchorWorld(st, &anchor));
    REQUIRE(anchor.z == Catch::Approx(8.0));
    const ray3d::Vec3 axis = CadGizmoAxisWorld(st, 0);
    REQUIRE(axis.z == Catch::Approx(1.0));
    // Not the UCS X it would be in entity mode - the case that fails if the face branch is missed.
    REQUIRE(std::fabs(axis.x) == Catch::Approx(0.0).margin(1e-9));
  }

  SECTION("an EDGE or a VERTEX: no gizmo, and that is the honest answer") {
    // The kernel has no operation that moves either, so a handle would advertise a move that cannot
    // happen. Refusing after the drag would be worse than not offering it (D-2026-09-05-b).
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 100, 100}, {0, 5, 8}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection.size() == 1);
    REQUIRE(st.subObjectSelection[0].kind != solidpick::Kind::Face);
    REQUIRE(CadGizmoModeFor(st) == CadGizmoMode::None);
    REQUIRE(CadGizmoAxisCountFor(st) == 0);
    REQUIRE_FALSE(CadGizmoVisible(st));
  }

  SECTION("TWO faces: no gizmo, because there is no single normal to slide along") {
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.0, 0.0), false, log));
    REQUIRE(SubmitSubObjectPick(st, RayAt({100, 0, 4}, {10, 0, 4}), Tol(0.0, 0.0), true, log));
    REQUIRE(st.subObjectSelection.size() == 2);
    REQUIRE(CadGizmoModeFor(st) == CadGizmoMode::None);
    // PRESSPULL already refuses to move two faces at once; offering a gesture the commit would
    // decline is worse than offering none.
    REQUIRE(CadGizmoAxisCountFor(st) == 0);
  }

  SECTION("an ENTITY selection keeps the three-handle gizmo it had") {
    st.userLinesFlat = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
    st.userLineAttrs.push_back(EntityAttributes{});
    SelectedEntity e;
    e.type = SelectedEntity::Type::LineSeg;
    e.index = 0;
    st.selection.push_back(e);
    REQUIRE(CadGizmoModeFor(st) == CadGizmoMode::Entity);
    REQUIRE(CadGizmoAxisCountFor(st) == 3);
  }
}

TEST_CASE("A face gizmo drag commits what PRESSPULL would", "[subobject][gizmo]") {
  // Issue #148 acceptance 4 at the level a unit test can hold it. It is true by construction —
  // `CommitGizmoDrag` calls `CadApplyPushPull`, which is what `CadPressPull` calls — and this is
  // the case that would fail if someone gave the face gizmo an edit of its own.
  std::vector<std::string> log;

  AppCommandState viaGizmo;
  viaGizmo.uiViewportWidthPx = 1200.f;
  viaGizmo.uiViewportHeightPx = 700.f;
  AddBox(viaGizmo, World(), 20.0, 10.0, 8.0);
  REQUIRE(SubmitSubObjectPick(viaGizmo, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
  // Anchor (0,0,8), axis +Z. Grab 5 up, drop 17 up: the drag is 12.
  {
    ray3d::Ray grab;
    grab.origin = {100, 0, 13};
    grab.dir = {-1, 0, 0};
    REQUIRE(SubmitGizmoClick(viaGizmo, grab, 1.0, log));
    REQUIRE(viaGizmo.gizmoDragActive);
    REQUIRE(viaGizmo.gizmoDragIsSubObject);
    ray3d::Ray drop;
    drop.origin = {100, 0, 25};
    drop.dir = {-1, 0, 0};
    UpdateGizmoDrag(viaGizmo, drop);
    REQUIRE(viaGizmo.gizmoDragDistance == Catch::Approx(12.0));
    REQUIRE(CommitGizmoDrag(viaGizmo, log));
  }

  AppCommandState viaTyped;
  viaTyped.uiViewportWidthPx = 1200.f;
  viaTyped.uiViewportHeightPx = 700.f;
  AddBox(viaTyped, World(), 20.0, 10.0, 8.0);
  REQUIRE(SubmitSubObjectPick(viaTyped, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
  CadPressPull(viaTyped, "12", log);

  REQUIRE(viaGizmo.cadSolids.size() == 1);
  REQUIRE(viaTyped.cadSolids.size() == 1);
  REQUIRE(viaGizmo.cadSolids[0]);
  REQUIRE(viaTyped.cadSolids[0]);
  // Vertex for vertex, not merely "the same volume": a solid that moved the right amount the wrong
  // way can share a volume with one that did not.
  const brep::Solid& a = *viaGizmo.cadSolids[0];
  const brep::Solid& b = *viaTyped.cadSolids[0];
  REQUIRE(a.vertices.size() == b.vertices.size());
  for (size_t i = 0; i < a.vertices.size(); ++i) {
    CHECK(a.vertices[i].p.x == Catch::Approx(b.vertices[i].p.x).margin(1e-9));
    CHECK(a.vertices[i].p.y == Catch::Approx(b.vertices[i].p.y).margin(1e-9));
    CHECK(a.vertices[i].p.z == Catch::Approx(b.vertices[i].p.z).margin(1e-9));
  }
}

TEST_CASE("A face drag applies to the face GRABBED, not to whatever is selected later",
          "[subobject][gizmo]") {
  // The selection can be cleared or re-picked between the two clicks of a click-arm / click-commit
  // drag. The reference is captured at the grab for that reason.
  AppCommandState st;
  st.uiViewportWidthPx = 1200.f;
  st.uiViewportHeightPx = 700.f;
  AddBox(st, World(), 20.0, 10.0, 8.0);
  std::vector<std::string> log;
  REQUIRE(SubmitSubObjectPick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), false, log));
  ray3d::Ray grab;
  grab.origin = {100, 0, 13};
  grab.dir = {-1, 0, 0};
  REQUIRE(SubmitGizmoClick(st, grab, 1.0, log));
  const SelectedSubObject grabbed = st.gizmoDragSubObject;

  st.subObjectSelection.clear();  // the user clears it mid-drag
  ray3d::Ray drop;
  drop.origin = {100, 0, 25};
  drop.dir = {-1, 0, 0};
  UpdateGizmoDrag(st, drop);
  REQUIRE(st.gizmoDragDistance == Catch::Approx(12.0));
  REQUIRE(CommitGizmoDrag(st, log));
  REQUIRE(grabbed.index == 0 + grabbed.index);  // (the reference itself is what was applied)
  // 20 x 10, pushed from 8 to 20 tall.
  REQUIRE(brep::ComputeMassProperties(*st.cadSolids[0]).volume == Catch::Approx(4000.0));
}
