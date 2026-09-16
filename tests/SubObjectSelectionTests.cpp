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
#include "render/SectionClip.hpp"       // SectionClipPlane, for REQ-338's keep-side check
#include "viewport/CadSnap.hpp"         // CadSnap::SnapClass, for REQ-340's named-feature rule

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

  SECTION("an EDGE gets TWO handles now (REQ-333)") {
    // This section asserted NO gizmo until 2026-09-09, and the reasoning was right: the kernel had
    // no operation that moved an edge, so a handle would have advertised a move that could not
    // happen. `brep::MoveEdge` is that operation, so the premise changed and the assertion with it.
    //
    // TWO handles, not three: the two adjacent faces' normals span exactly the plane perpendicular
    // to the edge, and the along-the-edge direction is not a motion at all (REQ-333 item 4).
    REQUIRE(SubmitSubObjectPick(st, RayAt({0, 100, 100}, {0, 5, 8}), Tol(0.5, 0.5), false, log));
    REQUIRE(st.subObjectSelection.size() == 1);
    REQUIRE(st.subObjectSelection[0].kind == solidpick::Kind::Edge);
    REQUIRE(CadGizmoModeFor(st) == CadGizmoMode::SubObjectEdge);
    REQUIRE(CadGizmoAxisCountFor(st) == 2);
    REQUIRE(CadGizmoVisible(st));
    // The handle directions are the two faces' outward normals — for the box's top-front edge, +Y
    // and +Z — and neither is along the edge itself, which runs in X.
    const ray3d::Vec3 a0 = CadGizmoAxisWorld(st, 0);
    const ray3d::Vec3 a1 = CadGizmoAxisWorld(st, 1);
    REQUIRE(std::fabs(a0.x) == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(std::fabs(a1.x) == Catch::Approx(0.0).margin(1e-9));
    REQUIRE(std::fabs(ray3d::Dot(a0, a1)) == Catch::Approx(0.0).margin(1e-9));
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

// --- A whole SOLID as a pickable, highlightable entity (REQ-318 amended, #149 follow-up) --------
//
// Reported twice in one session, as two separate complaints that turned out to be one gap: a solid
// could not be selected by clicking, and nothing lit up under the cursor before selecting it.
//
// Both had the same cause. `PickClosestCadEntity` — the function behind click-to-select AND the
// hover pre-highlight — returns only `LineSeg`, `Arc`, `Circle`, `Ellipse` and `Polyline`, and
// `AppendEntityHighlight` draws exactly those five. So `ComputeSelectionFromRect` was the only
// thing in the application that ever put a solid in a selection: a solid could be chosen by
// dragging a rectangle around it and by no other gesture, in any command or idle, with no
// highlight beforehand.
//
// These pin the two halves that close it.

TEST_CASE("A whole solid is picked by a ray, as a Solid entity", "[subobject][solidentity]") {
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  // Shaded, where a face is drawn and so answers a click (D-2026-09-16-b). 2D Wireframe — where
  // only edges and vertices do — has its own case below.
  st.viewportVisualStyle = VisualStyle::Shaded;
  AddBox(st, World(), 20.0, 10.0, 8.0);  // x [-10,10], y [-5,5], z [0,8]

  SECTION("a ray onto the top face names the solid") {
    SelectedEntity e{};
    REQUIRE(PickClosestSolidEntity(st, RayAt({0, 0, 100}, {0, 0, 8}), 0.5f, &e));
    CHECK(e.type == SelectedEntity::Type::Solid);
    CHECK(e.index == 0);
  }

  SECTION("an edge and a vertex name the solid too, not just a face") {
    // Any sub-object hit names the solid — that is what makes "click anywhere on it" work rather
    // than only "click exactly on an edge".
    SelectedEntity e{};
    REQUIRE(PickClosestSolidEntity(st, RayAt({0, -40, 40}, {0, -5, 8}), 0.5f, &e));  // top-front edge
    CHECK(e.index == 0);
    REQUIRE(PickClosestSolidEntity(st, RayAt({-40, -40, 40}, {-10, -5, 8}), 0.5f, &e));  // corner
    CHECK(e.index == 0);
  }

  SECTION("a ray that misses picks nothing") {
    SelectedEntity e{};
    CHECK_FALSE(PickClosestSolidEntity(st, RayAt({100, 100, 100}, {200, 200, 200}), 0.5f, &e));
  }

  SECTION("the NEAREST solid wins when two are in line") {
    ucs::Ucs far = World();
    far.origin = {0.0, 0.0, 40.0};
    AddBox(st, far, 20.0, 10.0, 8.0);  // a second box directly above the first
    SelectedEntity e{};
    // Looking down from high above, the upper box is nearer the eye and must be the one named.
    REQUIRE(PickClosestSolidEntity(st, RayAt({0, 0, 200}, {0, 0, 0}), 0.5f, &e));
    CHECK(e.index == 1);
    // ...and looking UP from below, the lower one is.
    REQUIRE(PickClosestSolidEntity(st, RayAt({0, 0, -200}, {0, 0, 0}), 0.5f, &e));
    CHECK(e.index == 0);
  }

  SECTION("a null out is refused rather than crashed on") {
    CHECK_FALSE(PickClosestSolidEntity(st, RayAt({0, 0, 100}, {0, 0, 8}), 0.5f, nullptr));
  }
}

TEST_CASE("In 2D Wireframe only a solid's edges and vertices answer a click, as in AutoCAD",
          "[subobject][solidentity]") {
  // Code review on #478, finding 4 (D-2026-09-16-b). A face hit naming the solid in plan view took
  // every click inside a building pad's footprint — survey points inside it could not be clicked,
  // and a selection box could not be started there. 2D Wireframe draws no faces, so none answers.
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  REQUIRE(st.viewportVisualStyle == VisualStyle::Wireframe2D);  // the shipped default
  AddBox(st, World(), 20.0, 10.0, 8.0);                         // x [-10,10], y [-5,5], z [0,8]
  SelectedEntity e{};

  SECTION("plan view, inside the outline: nothing, so the click can start a box") {
    CHECK_FALSE(PickClosestSolidEntity(st, RayAt({2, 1, 100}, {2, 1, 8}), 0.5f, &e));
  }

  SECTION("an edge and a vertex still name the solid") {
    REQUIRE(PickClosestSolidEntity(st, RayAt({0, -5, 100}, {0, -5, 8}), 0.5f, &e));  // on an edge, in plan
    CHECK(e.index == 0);
    REQUIRE(PickClosestSolidEntity(st, RayAt({-40, -40, 40}, {-10, -5, 8}), 0.5f, &e));  // corner
    CHECK(e.index == 0);
  }

  SECTION("an edge seen THROUGH the solid can be clicked, because no face hides it") {
    // The bottom-front edge (y = -5, z = 0), aimed at from above and behind the top face: in a
    // shaded view the top face occludes it, in wireframe it is drawn and so is clickable.
    REQUIRE(PickClosestSolidEntity(st, RayAt({0, 20, 40}, {0, -5, 0}), 0.3f, &e));
    CHECK(e.index == 0);
  }

  SECTION("Hidden draws faces, so a face click answers there") {
    st.viewportVisualStyle = VisualStyle::Hidden;
    REQUIRE(PickClosestSolidEntity(st, RayAt({2, 1, 100}, {2, 1, 8}), 0.5f, &e));
    CHECK(e.index == 0);
  }
}

TEST_CASE("A hovered solid draws a highlight", "[subobject][solidentity]") {
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  AddBox(st, World(), 20.0, 10.0, 8.0);

  std::vector<float> hoverLines;
  std::vector<float> hoverCircles;

  SECTION("nothing hovered draws nothing") {
    st.viewportHoverEntityValid = false;
    BuildHoverHighlight(st, &hoverLines, &hoverCircles);
    CHECK(hoverLines.empty());
  }

  SECTION("a hovered solid draws its wireframe edges") {
    st.viewportHoverEntityValid = true;
    st.viewportHoverEntity.type = SelectedEntity::Type::Solid;
    st.viewportHoverEntity.index = 0;
    BuildHoverHighlight(st, &hoverLines, &hoverCircles);
    // A box has twelve edges, each two xyz endpoints: 12 * 6 floats. Asserted as a count rather
    // than "non-empty", because half a wireframe is the failure that looks like success.
    CHECK(hoverLines.size() == static_cast<std::size_t>(12 * 6));
    CHECK(hoverCircles.empty());
  }

  SECTION("an ALREADY SELECTED solid draws no hover — selection takes precedence") {
    SelectedEntity sel{};
    sel.type = SelectedEntity::Type::Solid;
    sel.index = 0;
    st.selection.push_back(sel);
    st.viewportHoverEntityValid = true;
    st.viewportHoverEntity = sel;
    BuildHoverHighlight(st, &hoverLines, &hoverCircles);
    CHECK(hoverLines.empty());
  }

  SECTION("a solid index that is not there draws nothing rather than reading past the end") {
    st.viewportHoverEntityValid = true;
    st.viewportHoverEntity.type = SelectedEntity::Type::Solid;
    st.viewportHoverEntity.index = 7;
    BuildHoverHighlight(st, &hoverLines, &hoverCircles);
    CHECK(hoverLines.empty());
  }
}

// --- SECTIONPLANE's face rules (REQ-338 / ADR-058, GitHub issue #479 acceptance 1-2) ------------
//
// These live here rather than in the transcript because they turn on the PICK TOLERANCE, and the
// headless driver cannot state one: `CadOffsetEntityPickTolWorld` is screen-derived, and with no
// window it collapses to a geometric floor of about 0.002 ft. Whether a click on a cylinder wall
// resolves to the face or to a rim a few feet away would then be decided by arithmetic rather than
// by the rule under test — a test that passes or fails for the wrong reason either way. The
// SUBOBJECT verb takes explicit tolerances for exactly this reason; here they are arguments.

TEST_CASE("SECTIONPLANE takes a flat face and refuses a curved one",
          "[subobject][sectionplaneface][req338]") {
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  AddBox(st, World(), 20.0, 10.0, 8.0);  // x [-10,10], y [-5,5], z [0,8]
  std::vector<std::string> log;

  SECTION("the command must be running for a pick to mean anything") {
    // Not merely defensive. The face click is routed by `ViewportClickRouteFor`, which only names
    // this route while SECTIONPLANE is active — so a pick arriving with the command closed is a
    // routing bug, and answering it anyway would hide one.
    CHECK_FALSE(SubmitSectionPlaneFacePick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), log));
    CHECK_FALSE(st.viewportSectionClip);
  }

  SECTION("a flat face places the plane on that face's own plane") {
    StartSectionPlaneCommand(st, log);
    REQUIRE(SubmitSectionPlaneFacePick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), log));
    CHECK(st.viewportSectionClip);
    CHECK(st.viewportSectionClipFrameValid);
    // The TOP face, and its frame Z is the OUTWARD normal — measured in probe P1/P2 across every
    // primitive, both B1 Booleans and an oblique slice, with no counterexample.
    CHECK(st.viewportSectionClipFrame.zAxis.z == Catch::Approx(1.0));
    // The frame's origin lies ON that plane: z = 8 is the top face.
    CHECK(st.viewportSectionClipFrame.origin.z == Catch::Approx(8.0));
    // Offset and flip start clean, so the plane sits exactly on the face it was made from.
    CHECK(st.viewportSectionClipOffset == Catch::Approx(0.0));
    CHECK_FALSE(st.viewportSectionClipFlip);
    // And the command is done — it asked one question and got its answer.
    CHECK(st.active == AppCommandState::Kind::None);
  }

  SECTION("at offset 0 the whole solid survives the clip") {
    // The point of creating a section plane is to SEE a plane, not to lose half the model. With the
    // outward normal and no offset every vertex is on the kept side, so this is a property of the
    // frame choice rather than a coincidence of this box.
    StartSectionPlaneCommand(st, log);
    REQUIRE(SubmitSectionPlaneFacePick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), log));
    const SectionClipPlane p = SectionClipFromUcs(CadEffectiveSectionClipFrame(st),
                                                 st.viewportSectionClipOffset,
                                                 st.viewportSectionClipFlip);
    for (const brep::Vertex& v : st.cadSolids[0]->vertices) {
      INFO("vertex (" << v.p.x << ", " << v.p.y << ", " << v.p.z << ")");
      CHECK(p.KeepsWorldPoint(v.p.x, v.p.y, v.p.z));
    }
  }

  SECTION("a different face re-aims the same plane and resets the offset") {
    StartSectionPlaneCommand(st, log);
    REQUIRE(SubmitSectionPlaneFacePick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), log));
    st.viewportSectionClipOffset = 4.0;  // as though the user had slid it
    st.viewportSectionClipFlip = true;
    // Flipped at +4 the clip keeps only z >= 12 — the whole box is hidden, and since #478 a hidden
    // face cannot be picked. Turn the clip off so the -X face is visible to aim at; the offset and
    // flip being reset by the new pick is still what this section checks.
    st.viewportSectionClip = false;

    // Now the -X face, from outside it.
    StartSectionPlaneCommand(st, log);
    REQUIRE(SubmitSectionPlaneFacePick(st, RayAt({-100, 0, 4}, {-10, 0, 4}), Tol(0.5, 0.5), log));
    CHECK(st.viewportSectionClipFrame.zAxis.x == Catch::Approx(-1.0));
    // Carrying the old offset over would put the plane 4 ft from a face the user never measured
    // from. It is measured from the NEW face, so it starts at zero.
    CHECK(st.viewportSectionClipOffset == Catch::Approx(0.0));
    CHECK_FALSE(st.viewportSectionClipFlip);
  }

  SECTION("a miss keeps the command open") {
    StartSectionPlaneCommand(st, log);
    CHECK_FALSE(SubmitSectionPlaneFacePick(st, RayAt({500, 500, 500}, {600, 600, 600}),
                                           Tol(0.5, 0.5), log));
    CHECK(st.active == AppCommandState::Kind::SectionPlane);
    CHECK_FALSE(st.viewportSectionClip);
  }

  SECTION("an EDGE is refused by name, and the command stays open") {
    // A generous edge tolerance so the edge genuinely wins the pick — which is the case worth
    // pinning, because saying nothing here is how a user ends up clicking repeatedly at what looks
    // like the right place.
    StartSectionPlaneCommand(st, log);
    const size_t before = log.size();
    CHECK_FALSE(SubmitSectionPlaneFacePick(st, RayAt({0, -60, 60}, {0, -5, 8}), Tol(0.0, 3.0), log));
    CHECK(st.active == AppCommandState::Kind::SectionPlane);
    CHECK_FALSE(st.viewportSectionClip);
    bool said = false;
    for (size_t i = before; i < log.size(); ++i)
      if (log[i].find("edge") != std::string::npos)
        said = true;
    CHECK(said);
  }
}

TEST_CASE("SECTIONPLANE refuses a cylinder's wall by name", "[subobject][sectionplaneface][req338]") {
  // The refusal that matters most, and the one a plausible implementation gets wrong: a curved face
  // carries a `ucs::Ucs` frame exactly like a flat one, so nothing stops it being used. Its Z is the
  // surface's AXIS, though — straight up the middle of the cylinder — so the plane would come out at
  // right angles to the wall that was clicked and pass through the centre of the solid. Plausible,
  // wrong, and invisible in a screenshot.
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
  std::vector<std::string> log;

  // Straight at the wall, halfway up — far from both rims, and zero tolerances so neither can win.
  StartSectionPlaneCommand(st, log);
  const size_t before = log.size();
  CHECK_FALSE(SubmitSectionPlaneFacePick(st, RayAt({-60, 0, 5}, {-5, 0, 5}), Tol(0.0, 0.0), log));
  CHECK(st.active == AppCommandState::Kind::SectionPlane);
  CHECK_FALSE(st.viewportSectionClip);
  bool named = false;
  for (size_t i = before; i < log.size(); ++i)
    if (log[i].find("cylindrical") != std::string::npos)
      named = true;
  CHECK(named);  // BY NAME — "that is a cylindrical face", not "cannot use that"

  // The flat CAP of the same solid is accepted, from the still-open command.
  REQUIRE(SubmitSectionPlaneFacePick(st, RayAt({0, 0, 100}, {0, 0, 10}), Tol(0.0, 0.0), log));
  CHECK(st.viewportSectionClip);
  CHECK(st.viewportSectionClipFrame.zAxis.z == Catch::Approx(1.0));
  CHECK(st.active == AppCommandState::Kind::None);
}

TEST_CASE("The clip frame falls back to the UCS until a face is given", "[sectionplaneface][req338]") {
  // One plane, two ways to aim it (D-2026-09-11-b). `CadEffectiveSectionClipFrame` is the single
  // place that decides which, so the renderer and the report line cannot name different planes.
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  AddBox(st, World(), 20.0, 10.0, 8.0);
  std::vector<std::string> log;

  // Nothing placed yet: the active UCS answers.
  CHECK_FALSE(st.viewportSectionClipFrameValid);
  CHECK(CadEffectiveSectionClipFrame(st).zAxis.z == Catch::Approx(CadActiveUcsStorage(st).zAxis.z));

  StartSectionPlaneCommand(st, log);
  REQUIRE(SubmitSectionPlaneFacePick(st, RayAt({-100, 0, 4}, {-10, 0, 4}), Tol(0.5, 0.5), log));
  // Now the FACE answers, and it is not the UCS plane — a level UCS would have given +Z.
  CHECK(st.viewportSectionClipFrameValid);
  CHECK(CadEffectiveSectionClipFrame(st).zAxis.x == Catch::Approx(-1.0));
}

// --- Selecting and dragging the section plane (REQ-339, GitHub #479 acceptance 4-7) ------------
//
// The geometry half is in `SectionClipTests` under `[sectionplane]`. This is what a CLICK and a
// DRAG do, which is where the interaction can go wrong in ways geometry cannot see: a handle that
// jumps to the cursor on the first frame, a stretch that moves the edge you are not touching, a
// slide that goes the wrong way once the plane is flipped.

namespace {

/// Place the section plane on the top face of a box, the way the user does.
AppCommandState SectionPlaneOnBoxTop(std::vector<std::string>& log) {
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  AddBox(st, World(), 20.0, 10.0, 8.0);  // x [-10,10], y [-5,5], z [0,8]
  StartSectionPlaneCommand(st, log);
  REQUIRE(SubmitSectionPlaneFacePick(st, RayAt({0, 0, 100}, {0, 0, 8}), Tol(0.5, 0.5), log));
  return st;
}

/// A ray aimed at a handle from OBLIQUELY above it.
///
/// Oblique on purpose. The fixture's plane is horizontal, so a ray aimed across it lies IN it and
/// grazes every handle at once — the pick then answers with whichever happens to be nearest the
/// sight line, not the one aimed at. Straight down is no better for the drags: the move handle
/// travels along the plane's normal, and sighting down an axis is the one case
/// `CadAxisDragParam` refuses outright, because every point of it projects to the same pixel.
/// A ray aimed at an arbitrary world point from obliquely above it, for the same reason.
ray3d::Ray RayAtWorldPoint(const ray3d::Vec3& target) {
  return RayAt(ray3d::Vec3{target.x + 60.0, target.y + 40.0, target.z + 100.0}, target);
}

ray3d::Ray RayAtGrip(const AppCommandState& st, SectionPlaneGrip k) {
  const SectionPlaneGrips g = CadSectionPlaneGrips(st);
  REQUIRE(g.valid);
  const ray3d::Vec3 target = g.at[static_cast<int>(k)];
  return RayAt(ray3d::Vec3{target.x + 60.0, target.y + 40.0, target.z + 100.0}, target);
}

}  // namespace

TEST_CASE("A placed section plane comes up selected, with handles", "[sectionplanegrip][req339]") {
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  // Selected on creation: the user placed it in order to move it, and making them click it again
  // first is a step with no purpose.
  CHECK(st.sectionPlaneSelected);
  CHECK(CadSectionPlaneGrips(st).valid);

  // Deselecting hides the handles and leaves the CUT exactly where it was — deselecting is not
  // turning the clip off.
  ClearCadSelection(st);
  CHECK_FALSE(st.sectionPlaneSelected);
  CHECK_FALSE(CadSectionPlaneGrips(st).valid);
  CHECK(st.viewportSectionClip);
  CHECK(st.viewportSectionClipOffset == Catch::Approx(0.0));
}

TEST_CASE("The SECTION LINE is what selects the plane, not the rectangle",
          "[sectionplanegrip][req339]") {
  // The rectangle is sized to the model plus a margin, so in plan view its screen projection covers
  // everything in the drawing. Treating its whole interior as a click target consumed every click
  // that landed inside it, and while the clip was on no solid, grip, survey point or selection
  // window could be reached at all. Depth arbitration does not save it either: a plane on a box's
  // top face is genuinely nearer the camera than the box.
  //
  // So the target is the LINE — thin, deliberate, drawn heavier than anything else on the plane,
  // and what AutoCAD uses.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  ClearCadSelection(st);
  REQUIRE_FALSE(st.sectionPlaneSelected);

  const SectionPlaneGraphics gfx = SectionPlaneGraphicsFor(CadSectionClipIndicator(st));
  REQUIRE(gfx.valid);
  const ray3d::Vec3 onLine{0.5 * (gfx.lineA.x + gfx.lineB.x), 0.5 * (gfx.lineA.y + gfx.lineB.y),
                           0.5 * (gfx.lineA.z + gfx.lineB.z)};

  REQUIRE(SubmitSectionPlaneClick(st, RayAtWorldPoint(onLine), 1.0, log));
  CHECK(st.sectionPlaneSelected);

  // A click on the rectangle's INTERIOR but away from the line is not the plane's. This is the
  // whole fix: it must fall through so whatever is under it can be selected.
  ClearCadSelection(st);
  REQUIRE_FALSE(st.sectionPlaneSelected);
  ray3d::Vec3 bu{}, bv{};
  REQUIRE(SectionClipPlaneBasis(CadSectionClipPlane(st), &bu, &bv));
  const ray3d::Vec3 offLine{onLine.x + bv.x * 6.0, onLine.y + bv.y * 6.0, onLine.z + bv.z * 6.0};
  CHECK_FALSE(SubmitSectionPlaneClick(st, RayAtWorldPoint(offLine), 1.0, log));
  CHECK_FALSE(st.sectionPlaneSelected);

  // Selecting the plane clears the ENTITY selection: the two are mutually exclusive, which is what
  // lets DELETE act on one without guessing. Asserted because `StartDeleteCommand` states it as a
  // premise, and it was false until the line pick made it true.
  SelectedEntity e{};
  e.type = SelectedEntity::Type::Solid;
  e.index = 0;
  st.selection.push_back(e);
  REQUIRE(SubmitSectionPlaneClick(st, RayAtWorldPoint(onLine), 1.0, log));
  CHECK(st.sectionPlaneSelected);
  CHECK(st.selection.empty());

  // A click away from the plane deselects it WITHOUT consuming — the click still means whatever it
  // would have meant, so clicking from the plane straight onto another object takes one click.
  CHECK_FALSE(SubmitSectionPlaneClick(st, RayAt({500, 500, 100}, {500, 500, 8}), 1.0, log));
  CHECK_FALSE(st.sectionPlaneSelected);
}

TEST_CASE("Dragging the centre handle slides the plane along its normal", "[sectionplanegrip][req339]") {
  // Acceptance 5, and the gesture the whole feature exists for.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  REQUIRE(st.viewportSectionClipOffset == Catch::Approx(0.0));

  // Grab it from the side, so the cursor ray is not parallel to the normal it drags along.
  const ray3d::Ray grab = RayAtGrip(st, SectionPlaneGrip::Move);
  REQUIRE(SubmitSectionPlaneClick(st, grab, 1.0, log));
  REQUIRE(st.sectionPlaneGripDrag == static_cast<int>(SectionPlaneGrip::Move));
  // Nothing has moved yet. A handle that jumped to the cursor the instant it was grabbed would put
  // the plane somewhere the user never dragged it to.
  CHECK(st.viewportSectionClipOffset == Catch::Approx(0.0));

  // Now drag 5 ft DOWN the normal, which is +Z here. The ray is aimed straight at the point 5 ft
  // below where the handle started, so the closest approach to the drag axis is that point exactly
  // and the expected offset is arithmetic rather than a fitted number.
  const ray3d::Ray move = RayAt({60, 40, 103}, {0, 0, 3});
  UpdateSectionPlaneGripDrag(st, move);
  CHECK(st.viewportSectionClipOffset == Catch::Approx(-5.0).margin(1e-6));

  // The cut follows immediately — that is what "live" means, and it is why the drag writes the
  // offset every frame rather than only on release.
  //
  // The offset is measured from the FACE, which is the box's top at z = 8, so -5 puts the plane at
  // z = 3 — five feet down into the solid, which is where the cursor was aimed. An offset read as a
  // world elevation would put it at z = -5, under the box, cutting nothing.
  const SectionClipPlane p = CadSectionClipPlane(st);
  CHECK(p.KeepsWorldPoint(0.0, 0.0, 2.5));
  CHECK_FALSE(p.KeepsWorldPoint(0.0, 0.0, 3.5));

  // A HELD cursor must not move the plane. The drag runs once per frame, so this is the ordinary
  // case of the user pausing mid-gesture — and it is where the first implementation broke: it
  // re-derived the drag axis from the handle, which the drag itself had just moved, so the second
  // frame measured a delta of zero and snapped the plane back to where it was grabbed.
  for (int frame = 0; frame < 5; ++frame) {
    UpdateSectionPlaneGripDrag(st, move);
    INFO("frame " << frame);
    CHECK(st.viewportSectionClipOffset == Catch::Approx(-5.0).margin(1e-6));
  }

  // The click that drops it disarms and changes nothing further.
  REQUIRE(SubmitSectionPlaneClick(st, move, 1.0, log));
  CHECK(st.sectionPlaneGripDrag == static_cast<int>(SectionPlaneGrip::None));
  CHECK(st.viewportSectionClipOffset == Catch::Approx(-5.0).margin(1e-6));
}

TEST_CASE("Sliding a flipped plane still follows the drag", "[sectionplanegrip][req339]") {
  // A flipped plane has its normal negated, so the raw drag parameter runs backwards. Without the
  // correction, dragging towards the model would pull the cut away from it — the plane would run
  // away from the cursor, which is the most confusing possible response to a direct manipulation.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  ToggleSectionClipFlip(st, log);
  REQUIRE(st.viewportSectionClipFlip);

  const ray3d::Ray grab = RayAtGrip(st, SectionPlaneGrip::Move);
  REQUIRE(SubmitSectionPlaneClick(st, grab, 1.0, log));
  const SectionPlaneGrips before = CadSectionPlaneGrips(st);
  REQUIRE(before.valid);
  const double z0 = before.at[static_cast<int>(SectionPlaneGrip::Move)].z;

  const ray3d::Ray move = RayAt({60, 40, 103}, {0, 0, 3});  // 5 ft lower, as in the unflipped case
  UpdateSectionPlaneGripDrag(st, move);

  // The test is on where the HANDLE ends up, not on the offset's sign: the offset is measured along
  // a normal that flipping reversed, so its sign is an implementation detail, while "the plane
  // followed my cursor down" is the promise.
  const SectionPlaneGrips after = CadSectionPlaneGrips(st);
  REQUIRE(after.valid);
  CHECK(after.at[static_cast<int>(SectionPlaneGrip::Move)].z - z0 ==
        Catch::Approx(-5.0).margin(1e-6));
}

TEST_CASE("The flip handle is a click, and it shows the other half", "[sectionplanegrip][req339]") {
  // Acceptance 6. Flipping is a discrete choice — there is no halfway between looking at one half
  // and the other — so a drag gesture would be pretending it has a magnitude.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  // Slide it into the middle of the box first, so there genuinely are two halves to swap.
  st.viewportSectionClipOffset = -4.0;

  const SectionClipPlane before = CadSectionClipPlane(st);
  CHECK(before.KeepsWorldPoint(0.0, 0.0, 1.0));        // the bottom half survives
  CHECK_FALSE(before.KeepsWorldPoint(0.0, 0.0, 7.0));  // the top half is hidden

  const ray3d::Ray click = RayAtGrip(st, SectionPlaneGrip::Flip);
  REQUIRE(SubmitSectionPlaneClick(st, click, 1.0, log));
  // No drag is armed: it was a click.
  CHECK(st.sectionPlaneGripDrag == static_cast<int>(SectionPlaneGrip::None));
  CHECK(st.viewportSectionClipFlip);

  const SectionClipPlane after = CadSectionClipPlane(st);
  CHECK_FALSE(after.KeepsWorldPoint(0.0, 0.0, 1.0));  // and now exactly the other way round
  CHECK(after.KeepsWorldPoint(0.0, 0.0, 7.0));

  // The plane itself has not moved: the same point is on it before and after.
  CHECK(before.KeepsWorldPoint(0.0, 0.0, 4.0));
  CHECK(after.KeepsWorldPoint(0.0, 0.0, 4.0));
}

TEST_CASE("Stretch handles resize the plane without changing the cut", "[sectionplanegrip][req339]") {
  // Acceptance 7, and the distinction that matters: resizing changes what you SEE of the plane,
  // never what is hidden. The cut is unbounded. If geometry appeared or disappeared while a user
  // dragged a corner, that would be the bug.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  st.viewportSectionClipOffset = -4.0;

  const SectionClipIndicator before = CadSectionClipIndicator(st);
  REQUIRE(before.valid);
  const double widthBefore = ray3d::Length(ray3d::Sub(before.corner[1], before.corner[0]));

  // Grab the +u end of the section line and pull it 6 ft further out.
  const SectionPlaneGrips g = CadSectionPlaneGrips(st);
  REQUIRE(g.valid);
  const int kLenP = static_cast<int>(SectionPlaneGrip::LengthPos);
  const ray3d::Vec3 handle = g.at[kLenP];
  const ray3d::Vec3 outward = g.dir[kLenP];

  REQUIRE(SubmitSectionPlaneClick(st, RayAtWorldPoint(handle), 1.0, log));
  REQUIRE(st.sectionPlaneGripDrag == kLenP);

  // Pull the grabbed edge 6 ft further out along its own outward direction.
  const ray3d::Vec3 pulled{handle.x + outward.x * 6.0, handle.y + outward.y * 6.0,
                           handle.z + outward.z * 6.0};
  UpdateSectionPlaneGripDrag(st, RayAtWorldPoint(pulled));

  const SectionClipIndicator after = CadSectionClipIndicator(st);
  REQUIRE(after.valid);
  const double widthAfter = ray3d::Length(ray3d::Sub(after.corner[1], after.corner[0]));
  CHECK(widthAfter - widthBefore == Catch::Approx(6.0).margin(1e-6));

  // The OPPOSITE edge did not move. A centre-symmetric stretch would have shifted an edge the user
  // never touched, which is not what a grip on an edge means anywhere else in this application.
  const int kLenN = static_cast<int>(SectionPlaneGrip::LengthNeg);
  const SectionPlaneGrips g2 = CadSectionPlaneGrips(st);
  REQUIRE(g2.valid);
  CHECK(ray3d::Length(ray3d::Sub(g2.at[kLenN], g.at[kLenN])) == Catch::Approx(0.0).margin(1e-6));

  // And the CUT is untouched: the same points survive as before.
  const SectionClipPlane p = CadSectionClipPlane(st);
  CHECK(p.KeepsWorldPoint(0.0, 0.0, 1.0));
  CHECK_FALSE(p.KeepsWorldPoint(0.0, 0.0, 7.0));
  CHECK(st.viewportSectionClipOffset == Catch::Approx(-4.0));
}

TEST_CASE("A stretch cannot turn the plane inside out", "[sectionplanegrip][req339]") {
  // Dragged through zero the rectangle would invert — its corners would cross — and a zero-size one
  // cannot be grabbed again to undo the mistake. Clamping leaves the user a way back.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  const SectionPlaneGrips g = CadSectionPlaneGrips(st);
  REQUIRE(g.valid);
  const int kHgtP = static_cast<int>(SectionPlaneGrip::HeightPos);
  const ray3d::Vec3 handle = g.at[kHgtP];
  const ray3d::Vec3 outward = g.dir[kHgtP];

  REQUIRE(SubmitSectionPlaneClick(st, RayAtWorldPoint(handle), 1.0, log));

  // Push it a very long way INWARD — far past the opposite edge.
  const ray3d::Vec3 target{handle.x - outward.x * 5000.0, handle.y - outward.y * 5000.0,
                           handle.z - outward.z * 5000.0};
  UpdateSectionPlaneGripDrag(st, RayAtWorldPoint(target));

  const SectionClipIndicator ind = CadSectionClipIndicator(st);
  REQUIRE(ind.valid);
  const double h = ray3d::Length(ray3d::Sub(ind.corner[3], ind.corner[0]));
  CHECK(h > 0.0);
  CHECK(h == Catch::Approx(2.0 * kSectionPlaneMinHalfExtent).margin(1e-9));
  // Still a real rectangle: the corners have not crossed.
  CHECK(ray3d::Length(ray3d::Sub(ind.corner[1], ind.corner[0])) > 1.0);
}

TEST_CASE("Re-aiming the plane forgets the size it was stretched to", "[sectionplanegrip][req339]") {
  // The extent is stated in the plane's own basis, and that basis comes from the normal — so a
  // length measured across one face means something else entirely on another.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  st.viewportSectionClipExtent.valid = true;
  st.viewportSectionClipExtent.cu = 0.0;
  st.viewportSectionClipExtent.cv = 0.0;
  st.viewportSectionClipExtent.halfU = 3.0;
  st.viewportSectionClipExtent.halfV = 3.0;

  StartSectionPlaneCommand(st, log);
  REQUIRE(SubmitSectionPlaneFacePick(st, RayAt({-100, 0, 4}, {-10, 0, 4}), Tol(0.5, 0.5), log));
  CHECK_FALSE(st.viewportSectionClipExtent.valid);
  // ...and the rectangle is back to covering the model rather than the 6 x 6 patch.
  const SectionClipIndicator ind = CadSectionClipIndicator(st);
  REQUIRE(ind.valid);
  CHECK(ray3d::Length(ray3d::Sub(ind.corner[1], ind.corner[0])) > 8.0);
}

TEST_CASE("Handles are only pickable while the plane is selected", "[sectionplanegrip][req339]") {
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  const ray3d::Ray atMove = RayAtGrip(st, SectionPlaneGrip::Move);
  CHECK(PickSectionPlaneGrip(st, atMove, 1.0) == SectionPlaneGrip::Move);

  ClearCadSelection(st);
  CHECK(PickSectionPlaneGrip(st, atMove, 1.0) == SectionPlaneGrip::None);

  // And not at all once the clip is off — there is no plane to handle.
  st.sectionPlaneSelected = true;
  st.viewportSectionClip = false;
  CHECK(PickSectionPlaneGrip(st, atMove, 1.0) == SectionPlaneGrip::None);
  CHECK_FALSE(SubmitSectionPlaneClick(st, atMove, 1.0, log));
}

TEST_CASE("ESC drops an armed drag and leaves the plane where it was", "[sectionplanegrip][req339]") {
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  st.viewportSectionClipOffset = -2.0;

  const ray3d::Ray grab = RayAtGrip(st, SectionPlaneGrip::Move);
  REQUIRE(SubmitSectionPlaneClick(st, grab, 1.0, log));
  REQUIRE(st.sectionPlaneGripDrag == static_cast<int>(SectionPlaneGrip::Move));

  CancelSectionPlaneGripDrag(st);
  CHECK(st.sectionPlaneGripDrag == static_cast<int>(SectionPlaneGrip::None));
  // A cancel before any UpdateSectionPlaneGripDrag leaves the offset exactly as grabbed.
  CHECK(st.viewportSectionClipOffset == Catch::Approx(-2.0));
  CHECK(st.viewportSectionClip);
}

// --- Snapping a section-plane drag (REQ-340, GitHub #479) --------------------------------------

TEST_CASE("A snapped point lands the plane exactly through it", "[sectionplanegrip][req340]") {
  // The gesture from the user's screenshot: drag the section plane and drop it on a midpoint, with
  // the Midpoint marker showing. What makes it worth having is that the result is EXACT — the cut
  // is then a measured thing rather than an eyeballed one, which is the whole difference between a
  // section drawing and a picture.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);  // plane on the top face, z = 8

  const ray3d::Ray grab = RayAtGrip(st, SectionPlaneGrip::Move);
  REQUIRE(SubmitSectionPlaneClick(st, grab, 1.0, log));

  // A snapped point out in the model, nowhere near the drag axis: the mid-height of a vertical
  // edge, at (10, 5, 4). The axis runs vertically through the plane's centre at (0, 0, ...), so
  // this point is 11.2 ft off it — which is the normal case, not an edge case. A midpoint is
  // somewhere in the drawing; the axis is a line through the handle.
  const ray3d::Vec3 snapped{10.0, 5.0, 4.0};

  // The cursor is aimed somewhere else entirely, to prove the SNAP is what decides.
  const ray3d::Ray elsewhere = RayAt({60, 40, 101}, {0, 0, 1});
  UpdateSectionPlaneGripDrag(st, elsewhere, &snapped);

  // The plane now passes exactly through the snapped point. That is the only reading of "put the
  // plane on that midpoint" the one-degree-of-freedom constraint allows, and it is the useful one:
  // the plane is perpendicular to the axis it slides along, so projecting the point onto the axis
  // puts the whole plane through it.
  const SectionClipPlane p = CadSectionClipPlane(st);
  const double d = p.nx * snapped.x + p.ny * snapped.y + p.nz * snapped.z - p.c;
  CHECK(d == Catch::Approx(0.0).margin(1e-9));
  // REQ-101's own tolerance, stated separately: this is a coordinate the user placed.
  CHECK(std::fabs(d) < 0.002);
}

TEST_CASE("Without a snap the drag still follows the cursor", "[sectionplanegrip][req340]") {
  // The snap is an addition, not a replacement. A null snap must leave REQ-339's behaviour exactly
  // as it was — including the frozen axis, so the held-cursor case stays stable.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  const ray3d::Ray grab = RayAtGrip(st, SectionPlaneGrip::Move);
  REQUIRE(SubmitSectionPlaneClick(st, grab, 1.0, log));

  const ray3d::Ray move = RayAt({60, 40, 103}, {0, 0, 3});
  UpdateSectionPlaneGripDrag(st, move, nullptr);
  CHECK(st.viewportSectionClipOffset == Catch::Approx(-5.0).margin(1e-6));
  for (int frame = 0; frame < 3; ++frame) {
    UpdateSectionPlaneGripDrag(st, move, nullptr);
    INFO("frame " << frame);
    CHECK(st.viewportSectionClipOffset == Catch::Approx(-5.0).margin(1e-6));
  }
}

TEST_CASE("A snap survives being held, and releasing it hands back to the cursor",
          "[sectionplanegrip][req340]") {
  // Two frames of the same snap must not drift — the snapped parameter is absolute, not an
  // accumulating delta, so this would catch a version that added the projection each frame.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  const ray3d::Ray grab = RayAtGrip(st, SectionPlaneGrip::Move);
  REQUIRE(SubmitSectionPlaneClick(st, grab, 1.0, log));

  const ray3d::Vec3 snapped{10.0, 5.0, 4.0};
  const ray3d::Ray anywhere = RayAt({60, 40, 101}, {0, 0, 1});
  for (int frame = 0; frame < 4; ++frame) {
    UpdateSectionPlaneGripDrag(st, anywhere, &snapped);
    INFO("frame " << frame);
    CHECK(st.viewportSectionClipOffset == Catch::Approx(-4.0).margin(1e-6));  // 8 - 4
  }

  // The cursor leaves the feature: the snap stops being offered, and the plane goes back to
  // following the pointer from the SAME grab — not from where the snap left it.
  const ray3d::Ray move = RayAt({60, 40, 103}, {0, 0, 3});
  UpdateSectionPlaneGripDrag(st, move, nullptr);
  CHECK(st.viewportSectionClipOffset == Catch::Approx(-5.0).margin(1e-6));
}

TEST_CASE("A stretch handle snaps too, and still leaves the cut alone", "[sectionplanegrip][req340]") {
  // Snapping is not special-cased to the Move handle. "Make the plane reach exactly that corner" is
  // the same kind of request as "cut exactly at that midpoint", and the projection onto the
  // handle's own axis means the same thing for both.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  st.viewportSectionClipOffset = -4.0;
  const double offsetBefore = st.viewportSectionClipOffset;

  const SectionPlaneGrips g = CadSectionPlaneGrips(st);
  REQUIRE(g.valid);
  const int kLenP = static_cast<int>(SectionPlaneGrip::LengthPos);
  REQUIRE(SubmitSectionPlaneClick(st, RayAtWorldPoint(g.at[kLenP]), 1.0, log));
  REQUIRE(st.sectionPlaneGripDrag == kLenP);

  // A point 3 ft beyond the current +u edge, displaced off the axis along the OTHER two axes of
  // the plane's own frame. Displacing it in world X/Y instead would quietly put some of that
  // displacement back along u — the plane's u is not a world axis — and the test would then be
  // measuring its own arithmetic.
  const SectionClipPlane plane = CadSectionClipPlane(st);
  ray3d::Vec3 bu{}, bv{}, bn{};
  REQUIRE(SectionClipPlaneBasis(plane, &bu, &bv, &bn));
  const ray3d::Vec3 snapped{
      g.at[kLenP].x + g.dir[kLenP].x * 3.0 + bv.x * 7.0 + bn.x * -5.0,
      g.at[kLenP].y + g.dir[kLenP].y * 3.0 + bv.y * 7.0 + bn.y * -5.0,
      g.at[kLenP].z + g.dir[kLenP].z * 3.0 + bv.z * 7.0 + bn.z * -5.0};
  UpdateSectionPlaneGripDrag(st, RayAt({60, 40, 100}, {0, 0, 0}), &snapped);

  // The edge moved 3 ft, which is the snapped point's projection onto the drag axis.
  const SectionPlaneGrips g2 = CadSectionPlaneGrips(st);
  REQUIRE(g2.valid);
  const double moved = ray3d::Dot(ray3d::Sub(g2.at[kLenP], g.at[kLenP]), g.dir[kLenP]);
  CHECK(moved == Catch::Approx(3.0).margin(1e-6));

  // And resizing still changes nothing about what is hidden.
  CHECK(st.viewportSectionClipOffset == Catch::Approx(offsetBefore));
  const SectionClipPlane p = CadSectionClipPlane(st);
  CHECK(p.KeepsWorldPoint(0.0, 0.0, 1.0));
  CHECK_FALSE(p.KeepsWorldPoint(0.0, 0.0, 7.0));
}

TEST_CASE("A snap lands on the point however off-centre the handle was grabbed",
          "[sectionplanegrip][req340]") {
  // The bug this pins, reported from the real app 2026-09-11: "when trying to snap to the section
  // with the sectionplane it seems to be a little off ... it looks like it is going to snap too far
  // and then snaps too close."
  //
  // The snapped parameter is ABSOLUTE — the distance the handle must travel — but the code was
  // subtracting `sectionPlaneGripStartParam` from it, which is where the CURSOR crossed the drag
  // axis at the grab. That term is zero only when the click lands exactly on the handle's centre,
  // so the plane came out wrong by however far off-centre the grab was, in whichever direction.
  //
  // Every existing case missed it because every fixture aimed its grab ray straight at the handle.
  // These deliberately do not — which is the whole test.
  std::vector<std::string> log;

  // Three grabs, at increasing distances ALONG the drag axis from the handle's centre. The plane
  // must end up in exactly the same place every time: where the grab landed is not information
  // about where the snap is.
  for (const double offCentre : {0.0, 1.5, -2.25}) {
    AppCommandState st = SectionPlaneOnBoxTop(log);  // plane on the top face, z = 8
    const SectionPlaneGrips g = CadSectionPlaneGrips(st);
    REQUIRE(g.valid);
    const int kMove = static_cast<int>(SectionPlaneGrip::Move);
    const ray3d::Vec3 handle = g.at[kMove];
    const ray3d::Vec3 axis = g.dir[kMove];

    // Aim the grab at a point displaced along the axis from the handle — a click inside the grab
    // aperture but not dead centre, which is every real click.
    const ray3d::Vec3 aim{handle.x + axis.x * offCentre, handle.y + axis.y * offCentre,
                          handle.z + axis.z * offCentre};
    REQUIRE(SubmitSectionPlaneClick(st, RayAtWorldPoint(aim), 5.0, log));
    REQUIRE(st.sectionPlaneGripDrag == kMove);

    const ray3d::Vec3 snapped{10.0, 5.0, 4.0};  // mid-height of a vertical edge of the box
    UpdateSectionPlaneGripDrag(st, RayAt({60, 40, 101}, {0, 0, 1}), &snapped);

    const SectionClipPlane p = CadSectionClipPlane(st);
    const double d = p.nx * snapped.x + p.ny * snapped.y + p.nz * snapped.z - p.c;
    INFO("grabbed " << offCentre << " ft off the handle's centre");
    CHECK(std::fabs(d) < 0.002);  // REQ-101: the plane passes THROUGH the snapped point
    // Same answer every time, which is the property that matters: the grab position carries no
    // information about where the snap is, so it must not influence the result at all.
    CHECK(st.viewportSectionClipOffset == Catch::Approx(-4.0).margin(1e-9));
  }
}

TEST_CASE("An off-centre grab still drags relatively when there is no snap",
          "[sectionplanegrip][req340]") {
  // The other half of the same distinction. Without a snap the drag IS relative, and subtracting
  // the grab's own parameter is exactly right — the plane must move by how far the cursor moved,
  // not jump so the handle lands under the cursor.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  const SectionPlaneGrips g = CadSectionPlaneGrips(st);
  REQUIRE(g.valid);
  const int kMove = static_cast<int>(SectionPlaneGrip::Move);
  const ray3d::Vec3 handle = g.at[kMove];
  const ray3d::Vec3 axis = g.dir[kMove];

  // Grab 2 ft off centre along the axis...
  const ray3d::Vec3 aim{handle.x + axis.x * 2.0, handle.y + axis.y * 2.0, handle.z + axis.z * 2.0};
  REQUIRE(SubmitSectionPlaneClick(st, RayAtWorldPoint(aim), 5.0, log));

  // ...then move the cursor 3 ft further along it. The plane moves 3 ft — not 5, which is what a
  // version that treated the cursor position as absolute would give.
  const ray3d::Vec3 moved{handle.x + axis.x * 5.0, handle.y + axis.y * 5.0, handle.z + axis.z * 5.0};
  UpdateSectionPlaneGripDrag(st, RayAtWorldPoint(moved), nullptr);
  CHECK(st.viewportSectionClipOffset == Catch::Approx(3.0).margin(1e-6));
}

TEST_CASE("Releasing the mouse keeps the snapped placement", "[sectionplanegrip][req340]") {
  // The bug: the drop click re-ran the drag from its own ray, and the click path is never given a
  // snapped point — so letting go recomputed the placement from the raw cursor and threw the snap
  // away. The plane jumped off the feature it had just locked onto, at the instant of release.
  //
  // The sequence below is the real one: grab, drag onto a snap, then release with the cursor
  // somewhere that is NOT the snapped point — which is always true, since the snap pulls the plane
  // to a feature the cursor is merely near.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);  // plane on the top face, z = 8

  const ray3d::Ray grab = RayAtGrip(st, SectionPlaneGrip::Move);
  REQUIRE(SubmitSectionPlaneClick(st, grab, 1.0, log));

  const ray3d::Vec3 snapped{10.0, 5.0, 4.0};
  const ray3d::Ray cursor = RayAt({60, 40, 101}, {0, 0, 1});  // aimed well away from the snap
  UpdateSectionPlaneGripDrag(st, cursor, &snapped);
  const double heldOffset = st.viewportSectionClipOffset;
  REQUIRE(heldOffset == Catch::Approx(-4.0).margin(1e-9));

  // Release. The click carries no snap — it cannot; the snap is a viewport quantity — so anything
  // it recomputes is by definition the unsnapped answer.
  REQUIRE(SubmitSectionPlaneClick(st, cursor, 1.0, log));
  CHECK(st.sectionPlaneGripDrag == static_cast<int>(SectionPlaneGrip::None));
  CHECK(st.viewportSectionClipOffset == Catch::Approx(heldOffset).margin(1e-9));

  // And the plane still passes through the snapped point, which is the promise the user cares about.
  const SectionClipPlane p = CadSectionClipPlane(st);
  const double d = p.nx * snapped.x + p.ny * snapped.y + p.nz * snapped.z - p.c;
  CHECK(std::fabs(d) < 0.002);
}

TEST_CASE("A nearest-on-face snap is not a placement", "[sectionplanegrip][req340]") {
  // `Surface`, `Edge` and `Face` answer with the point on the object nearest the cursor, so with 3D
  // OSNAP on there is one under the cursor at essentially every position on a solid. Fed to an
  // absolute placement they stop being snaps and become "put the plane wherever the pointer is
  // touching the model" — the plane skates across the box as the cursor moves.
  //
  // The gate lives in the viewport, which is where the snap KIND is known, so what is asserted here
  // is the rule it applies: `SnapClass` separates a named feature from the nearest-anywhere family,
  // and only the first may steer a drag. Kept beside the drag tests because this is the reason the
  // drag behaves, and a change to `SnapClass` that silently reclassified `Face` would break the
  // section plane with nothing else to notice.
  CHECK(CadSnap::SnapClass(CadSnap::Kind::Face) == 0);
  CHECK(CadSnap::SnapClass(CadSnap::Kind::Surface) == 0);
  CHECK(CadSnap::SnapClass(CadSnap::Kind::Edge) == 0);

  // The ones that MUST place it — the whole point of the feature.
  CHECK(CadSnap::SnapClass(CadSnap::Kind::Midpoint) == 1);
  CHECK(CadSnap::SnapClass(CadSnap::Kind::Endpoint) == 1);
  CHECK(CadSnap::SnapClass(CadSnap::Kind::Center) == 1);
  CHECK(CadSnap::SnapClass(CadSnap::Kind::Quadrant) == 1);
  CHECK(CadSnap::SnapClass(CadSnap::Kind::Intersection) == 1);
  CHECK(CadSnap::SnapClass(CadSnap::Kind::CenterOfFace) == 1);
  CHECK(CadSnap::SnapClass(CadSnap::Kind::Knot) == 1);
}

TEST_CASE("The snapped point is read in STORAGE coordinates, not world",
          "[sectionplanegrip][req340]") {
  // The bug behind "it is snapping too far the other direction" (2026-09-11). The viewport was
  // converting the snapped point to WORLD before handing it to the drag, but every other quantity
  // in that drag is in the LOCAL storage frame — the clip frame comes from a solid's face, solids
  // are stored local like every other store, and the camera ray is the one the sub-object pick
  // casts at them. The snap alone had `worldDocumentOrigin` added to it, so it landed a whole
  // origin from the anchor it is measured against.
  //
  // The document origin is ZERO in a fresh drawing, which is why this survived three rounds of
  // testing: at the origin the wrong frame and the right one are the same frame — the same shape as
  // the anchor-rebasing hazard REQ-337 records. So this case sets one.
  std::vector<std::string> log;
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  // A survey-magnitude document origin: the drawing is stored around zero and lives at state-plane
  // coordinates, which is the arrangement `worldDocumentOrigin` exists for.
  st.worldDocumentOriginX = 2196000.0;
  st.worldDocumentOriginY = 1400000.0;
  AddBox(st, World(), 20.0, 10.0, 8.0);  // stored at x [-10,10], y [-5,5], z [0,8]

  // A SIDE face, so the plane's normal is horizontal. On a level plane the normal is +Z while the
  // document origin offsets X and Y, so the wrong frame and the right one give the same answer and
  // this case would prove nothing — the same blind spot REQ-337 records for its own anchor
  // rebasing, where "a horizontal cut is exact in both versions".
  StartSectionPlaneCommand(st, log);
  REQUIRE(SubmitSectionPlaneFacePick(st, RayAt({-100, 0, 4}, {-10, 0, 4}), Tol(0.5, 0.5), log));
  REQUIRE(st.viewportSectionClipFrame.zAxis.x == Catch::Approx(-1.0));

  const ray3d::Ray grab = RayAtGrip(st, SectionPlaneGrip::Move);
  REQUIRE(SubmitSectionPlaneClick(st, grab, 1.0, log));

  // A snapped point as `CadSnap` reports one: STORAGE coordinates. Mid-height of a vertical edge.
  const ray3d::Vec3 snapped{10.0, 5.0, 4.0};
  UpdateSectionPlaneGripDrag(st, RayAt({60, 40, 101}, {0, 0, 1}), &snapped);

  const SectionClipPlane p = CadSectionClipPlane(st);
  const double d = p.nx * snapped.x + p.ny * snapped.y + p.nz * snapped.z - p.c;
  CHECK(std::fabs(d) < 0.002);  // REQ-101, at a document origin 2.2 million feet out

  // And the WORLD-coordinate version of that same point — what the viewport used to pass — must not
  // give the same answer, or this case would pass against the very bug it exists to catch. The drag
  // is still armed against the same anchor, so feeding it the other frame is the whole experiment.
  const double rightOffset = st.viewportSectionClipOffset;
  const ray3d::Vec3 asWorld{snapped.x + st.worldDocumentOriginX,
                            snapped.y + st.worldDocumentOriginY, snapped.z};
  UpdateSectionPlaneGripDrag(st, RayAt({60, 40, 101}, {0, 0, 1}), &asWorld);
  const double wrongOffset = st.viewportSectionClipOffset;
  INFO("storage frame gave " << rightOffset << ", world frame gave " << wrongOffset);
  // Not merely different — different by the document origin's component along the plane's normal,
  // which here is the whole 2,196,000 ft easting. That is the size of the mistake, and stating it
  // as a magnitude rather than an inequality is what stops the case passing on a rounding wobble.
  CHECK(std::fabs(wrongOffset - rightOffset) == Catch::Approx(2196000.0).margin(1e-3));
}

TEST_CASE("DELETE erases a selected section plane", "[sectionplanegrip][req339]") {
  // Reported 2026-09-15: "it will not let me use the delete command or button ... to delete it."
  //
  // The plane is deliberately not in `st.selection` (ADR-058 (h)), which is what keeps every
  // consumer of that vector free of a branch for a view state — and the cost, unnoticed until
  // someone tried it, was that DELETE walked past a plane the user could see was selected and
  // opened a "click objects" prompt instead. The flag has to be tested somewhere, and DELETE is
  // where "erase what is selected" is decided.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  st.viewportSectionClipOffset = -4.0;
  REQUIRE(st.sectionPlaneSelected);
  REQUIRE(st.viewportSectionClip);
  REQUIRE(CadSectionClipPlane(st).KeepsWorldPoint(0.0, 0.0, 1.0));
  REQUIRE_FALSE(CadSectionClipPlane(st).KeepsWorldPoint(0.0, 0.0, 7.0));  // the top half is hidden

  StartDeleteCommand(st, log);

  // The clip is off and the whole model is visible again — "deleted" for a thing whose only
  // manifestation is the cut.
  CHECK_FALSE(st.viewportSectionClip);
  CHECK_FALSE(st.sectionPlaneSelected);
  CHECK_FALSE(CadSectionClipPlane(st).active);
  CHECK(CadSectionClipPlane(st).KeepsWorldPoint(0.0, 0.0, 7.0));
  CHECK_FALSE(CadSectionClipIndicator(st).valid);   // nothing left to draw
  CHECK_FALSE(CadSectionPlaneGrips(st).valid);      // and no handles

  // DELETE must NOT have fallen through to its selection prompt, which is the reported symptom.
  CHECK(st.active == AppCommandState::Kind::None);

  // The SOLID is untouched. Deleting the plane deletes the plane.
  CHECK(st.cadSolids.size() == 1u);
}

TEST_CASE("A deleted section plane does not come back on SECTIONCLIP ON",
          "[sectionplanegrip][req339]") {
  // The frame and the stretched size are cleared too. Leaving them would make the next
  // `SECTIONCLIP ON` resurrect the plane in its old place on its old face, which is not what
  // "delete" means anywhere else in the application.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  st.viewportSectionClipOffset = -4.0;
  st.viewportSectionClipFlip = true;
  st.viewportSectionClipExtent.valid = true;
  st.viewportSectionClipExtent.halfU = 3.0;
  st.viewportSectionClipExtent.halfV = 3.0;
  REQUIRE(st.viewportSectionClipFrameValid);

  StartDeleteCommand(st, log);
  CHECK_FALSE(st.viewportSectionClipFrameValid);
  CHECK_FALSE(st.viewportSectionClipExtent.valid);
  CHECK(st.viewportSectionClipOffset == Catch::Approx(0.0));
  CHECK_FALSE(st.viewportSectionClipFlip);

  // Turning the clip back on gives the UCS plane, not the old face. Driven through the state the
  // way `SECTIONCLIP ON` sets it — `ApplySectionClipValue` is deliberately not in the header, and
  // widening its visibility for a test would be the test changing the design to suit itself.
  st.viewportSectionClip = true;
  CHECK_FALSE(st.viewportSectionClipFrameValid);
  const ucs::Ucs effective = CadEffectiveSectionClipFrame(st);
  const ucs::Ucs worldUcs = CadActiveUcsStorage(st);
  CHECK(effective.zAxis.z == Catch::Approx(worldUcs.zAxis.z));  // the UCS plane, not the -Z face
  CHECK(CadSectionClipPlane(st).nz == Catch::Approx(1.0));
}

TEST_CASE("DELETE with no section plane selected behaves exactly as before",
          "[sectionplanegrip][req339]") {
  // The new branch must not shadow DELETE's existing behaviour. Two cases: a plane that exists but
  // is NOT selected is left alone, and an empty selection still opens the selection step.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  st.viewportSectionClipOffset = -4.0;
  ClearCadSelection(st);  // deselects the plane; the CUT stays, which is REQ-339's own rule
  REQUIRE_FALSE(st.sectionPlaneSelected);
  REQUIRE(st.viewportSectionClip);

  StartDeleteCommand(st, log);
  // The plane survives — an unselected thing is not what DELETE acts on...
  CHECK(st.viewportSectionClip);
  // ...and DELETE opened its ordinary selection step instead.
  CHECK(st.active == AppCommandState::Kind::Delete);
}

TEST_CASE("Flipping a stretched plane leaves it where it is", "[sectionplanegrip][req339]") {
  // `SectionClipPlaneBasis` derives u from the normal, so flipping — which negates the normal —
  // negates u while leaving v alone. The stored extent's `cu` is an ABSOLUTE `dot(centre, u)`, so
  // leaving it untouched mirrors the rectangle about the STORAGE ORIGIN.
  //
  // Measured on a model 400 ft out: the plane jumped 800 ft, off screen, taking every handle with
  // it and leaving a cut that was still correct with no visible plane to grab. Invisible at the
  // origin, which is where every other flip case runs — the same blind spot REQ-337 records.
  std::vector<std::string> log;
  AppCommandState st;
  st.viewportLastSurveyLayoutOrthoHalfH = 50.f;
  ucs::Ucs farFrame;
  farFrame.origin = ray3d::Vec3{0.0, 400.0, 0.0};
  AddBox(st, farFrame, 20.0, 10.0, 8.0);  // centred on y = 400

  StartSectionPlaneCommand(st, log);
  REQUIRE(SubmitSectionPlaneFacePick(st, RayAt({0, 400, 100}, {0, 400, 8}), Tol(0.5, 0.5), log));

  // Stretch it, so the extent is stored rather than derived.
  st.viewportSectionClipExtent = SectionPlaneExtentFromQuad(CadSectionClipIndicator(st),
                                                            CadSectionClipPlane(st));
  REQUIRE(st.viewportSectionClipExtent.valid);
  st.viewportSectionClipExtent.halfU = 14.0;
  st.viewportSectionClipExtent.halfV = 9.0;

  const SectionClipIndicator before = CadSectionClipIndicator(st);
  REQUIRE(before.valid);
  const ray3d::Vec3 cBefore{
      0.25 * (before.corner[0].x + before.corner[1].x + before.corner[2].x + before.corner[3].x),
      0.25 * (before.corner[0].y + before.corner[1].y + before.corner[2].y + before.corner[3].y),
      0.25 * (before.corner[0].z + before.corner[1].z + before.corner[2].z + before.corner[3].z)};

  ToggleSectionClipFlip(st, log);

  const SectionClipIndicator after = CadSectionClipIndicator(st);
  REQUIRE(after.valid);
  const ray3d::Vec3 cAfter{
      0.25 * (after.corner[0].x + after.corner[1].x + after.corner[2].x + after.corner[3].x),
      0.25 * (after.corner[0].y + after.corner[1].y + after.corner[2].y + after.corner[3].y),
      0.25 * (after.corner[0].z + after.corner[1].z + after.corner[2].z + after.corner[3].z)};

  // The rectangle has not moved. Flip changes which HALF survives, never where the plane is.
  INFO("centre before (" << cBefore.x << ", " << cBefore.y << ", " << cBefore.z << ") after ("
                         << cAfter.x << ", " << cAfter.y << ", " << cAfter.z << ")");
  CHECK(ray3d::Length(ray3d::Sub(cAfter, cBefore)) == Catch::Approx(0.0).margin(1e-6));
  // ...and it is still the size the user stretched it to.
  CHECK(ray3d::Length(ray3d::Sub(after.corner[1], after.corner[0])) == Catch::Approx(28.0));
}

TEST_CASE("ESC puts an aborted drag back where it was grabbed", "[sectionplanegrip][req339]") {
  // A section-plane drag writes the live offset every frame — that is what makes the cut follow the
  // handle — so by the time ESC is pressed the plane has already moved. Disarming alone would
  // COMMIT it, and REQ-339 records that a slide makes no undo entry, so there is no way back.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  st.viewportSectionClipOffset = -2.0;

  const ray3d::Ray grab = RayAtGrip(st, SectionPlaneGrip::Move);
  REQUIRE(SubmitSectionPlaneClick(st, grab, 1.0, log));
  UpdateSectionPlaneGripDrag(st, RayAt({60, 40, 103}, {0, 0, 3}), nullptr);
  REQUIRE(st.viewportSectionClipOffset != Catch::Approx(-2.0));  // it really did move

  AbortSectionPlaneGripDrag(st);
  CHECK(st.sectionPlaneGripDrag == static_cast<int>(SectionPlaneGrip::None));
  CHECK(st.viewportSectionClipOffset == Catch::Approx(-2.0));  // ...and it is back
  CHECK(st.viewportSectionClip);                               // the plane is still there
}

TEST_CASE("An aborted first stretch does not pin the plane's size", "[sectionplanegrip][req339]") {
  // The stretch arithmetic needs a valid extent to work from, so a FIRST stretch seeds one from the
  // drawn rectangle. Restoring that on abort would leave the plane user-sized and no longer
  // tracking the model — a state the user never asked for and cannot see, since the size is
  // identical at the moment it happens.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  REQUIRE_FALSE(st.viewportSectionClipExtent.valid);

  const SectionPlaneGrips g = CadSectionPlaneGrips(st);
  REQUIRE(g.valid);
  const int kLenP = static_cast<int>(SectionPlaneGrip::LengthPos);
  REQUIRE(SubmitSectionPlaneClick(st, RayAtWorldPoint(g.at[kLenP]), 1.0, log));
  const ray3d::Vec3 pulled{g.at[kLenP].x + g.dir[kLenP].x * 6.0, g.at[kLenP].y + g.dir[kLenP].y * 6.0,
                           g.at[kLenP].z + g.dir[kLenP].z * 6.0};
  UpdateSectionPlaneGripDrag(st, RayAtWorldPoint(pulled), nullptr);
  REQUIRE(st.viewportSectionClipExtent.valid);  // the stretch made it user-sized

  AbortSectionPlaneGripDrag(st);
  CHECK_FALSE(st.viewportSectionClipExtent.valid);  // ...and the abort took that back too
}

TEST_CASE("Starting a command disarms a live section-plane drag", "[sectionplanegrip][req339]") {
  // The drag runs off the frame loop, gated only on the clip being on. Left armed it kept rewriting
  // the offset while the NEXT command took its picks: grab the Move handle, type LINE, and the cut
  // slid across the model as the line was drawn.
  std::vector<std::string> log;
  AppCommandState st = SectionPlaneOnBoxTop(log);
  REQUIRE(SubmitSectionPlaneClick(st, RayAtGrip(st, SectionPlaneGrip::Move), 1.0, log));
  REQUIRE(st.sectionPlaneGripDrag >= 0);

  StartLineCommand(st, log);
  CHECK(st.sectionPlaneGripDrag == static_cast<int>(SectionPlaneGrip::None));

  // And re-running SECTIONPLANE, which had the same hole: a refused face pick used to leave the
  // plane tracking the cursor while the user hunted for a flat face.
  REQUIRE(SubmitSectionPlaneClick(st, RayAtGrip(st, SectionPlaneGrip::Move), 1.0, log));
  REQUIRE(st.sectionPlaneGripDrag >= 0);
  StartSectionPlaneCommand(st, log);
  CHECK(st.sectionPlaneGripDrag == static_cast<int>(SectionPlaneGrip::None));
}
