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
