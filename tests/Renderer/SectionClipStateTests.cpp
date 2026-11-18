// REQ-341 live section clip — the parts of it that live in the COMMAND layer rather than in the plane
// arithmetic `SectionClipTests` covers: that the clip belongs to one drawing tab, that picking and
// snapping honour it, the SECTION selection it shares a review with, and what the indicator is
// sized from. Each case is one finding from the code review on #478 (D-2026-09-16-b).
//
// Linked into GoSurveySnapTests: these call into gosurvey_domain (`SaveDocumentToSnapshot`,
// `PickClosestSolidEntity`, `CadSnap::FindBest`), like SubObjectSelectionTests.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "CadSnap.hpp"

using Catch::Approx;

namespace {

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

ray3d::Ray RayAt(const ray3d::Vec3& from, const ray3d::Vec3& target) {
  ray3d::Ray r;
  r.origin = from;
  r.dir = ray3d::Sub(target, from);
  return r;
}

bool LogHas(const std::vector<std::string>& log, const std::string& needle) {
  return std::any_of(log.begin(), log.end(), [&](const std::string& l) { return l.find(needle) != std::string::npos; });
}

}  // namespace

TEST_CASE("The section clip belongs to its drawing tab", "[sectionclip][req341]") {
  // Finding 6. The clip lived on the app-wide state, so turning it on in one drawing hid half of
  // every other drawing the user switched to.
  AppCommandState st;
  st.documents.resize(3);  // [0] backs the Start tab; 1 and 2 are drawings

  st.viewportSectionClip = true;
  st.viewportSectionClipOffset = 4.5;
  st.viewportSectionClipFlip = true;
  SaveDocumentToSnapshot(st, 1);

  RestoreDocumentFromSnapshot(st, 2);  // a drawing that never had the clip
  CHECK_FALSE(st.viewportSectionClip);
  CHECK(st.viewportSectionClipOffset == 0.0);
  CHECK_FALSE(st.viewportSectionClipFlip);
  CHECK_FALSE(CadActiveSectionClip(st).active);

  RestoreDocumentFromSnapshot(st, 1);  // and coming back restores it
  CHECK(st.viewportSectionClip);
  CHECK(st.viewportSectionClipOffset == 4.5);
  CHECK(st.viewportSectionClipFlip);
}

TEST_CASE("A solid the clip has hidden is not picked, and does not hide the one behind it",
          "[sectionclip][req341][solidentity]") {
  // Finding 5. Solid 0 stands in front of solid 1 along the ray; the clip removes everything above
  // z = 20, which is all of solid 0. The click must go to what is visible.
  AppCommandState st;
  st.viewportVisualStyle = VisualStyle::Shaded;
  AddBox(st, ucs::Ucs{}, 20.0, 10.0, 8.0);  // solid 0 ... z [0, 8]
  ucs::Ucs high{};
  high.origin = {0.0, 0.0, 40.0};
  AddBox(st, high, 20.0, 10.0, 8.0);  // solid 1 ... z [40, 48]
  // Looking UP from below: solid 0 is nearer the eye.
  const ray3d::Ray up = RayAt({1, 1, -100}, {1, 1, 0});
  SelectedEntity e{};

  REQUIRE(PickClosestSolidEntity(st, up, 0.5f, &e));
  CHECK(e.index == 0);  // no clip: the near one

  st.viewportSectionClip = true;
  st.viewportSectionClipOffset = 20.0;  // World UCS, flip off: keeps z <= 20 ...
  st.viewportSectionClipFlip = true;    // ... flipped: keeps z >= 20, hiding solid 0 entirely
  REQUIRE(PickClosestSolidEntity(st, up, 0.5f, &e));
  CHECK(e.index == 1);

  SECTION("the sub-object pick agrees") {
    SelectedSubObject sub{};
    solidpick::Tolerance tol;
    tol.vertex = tol.edge = 0.5;
    REQUIRE(PickSubObjectAcrossSolids(st, up, tol, &sub));
    CHECK(sub.solidIndex == 1);
  }

  SECTION("a vertex on the removed side is not picked either") {
    // Solid 0's corner (-10,-5,0), aimed at directly: hidden, so nothing answers.
    CHECK_FALSE(PickClosestSolidEntity(st, RayAt({-40, -40, -40}, {-10, -5, 0}), 0.5f, &e));
  }
}

TEST_CASE("A snap point the clip has hidden is not offered", "[sectionclip][req341][CadSnap]") {
  // Finding 5, the snap half: SECTION and DIST points snapped to vertices the user could not see.
  AppCommandState st;
  st.objectSnapEnabled = true;
  st.objectSnapEndpoint = true;
  st.userLinesFlat = {0.f, 0.f, 0.f, 10.f, 0.f, 12.f};  // endpoint (10, 0, 12) sits above z = 6
  st.userLineAttrs.emplace_back();

  const CadSnap::Hit before = CadSnap::FindBest(10.0, 0.0, st, /*commandActive=*/true, 1.f, {}, nullptr);
  REQUIRE(before.valid);
  CHECK(before.kind == CadSnap::Kind::Endpoint);
  CHECK(before.z == Approx(12.f));

  st.viewportSectionClip = true;
  st.viewportSectionClipOffset = 6.0;  // keeps z <= 6
  const CadSnap::Hit after = CadSnap::FindBest(10.0, 0.0, st, /*commandActive=*/true, 1.f, {}, nullptr);
  CHECK_FALSE((after.valid && after.kind == CadSnap::Kind::Endpoint && after.z > 6.f));

  // The kept endpoint is still offered.
  const CadSnap::Hit kept = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/true, 1.f, {}, nullptr);
  REQUIRE(kept.valid);
  CHECK(kept.kind == CadSnap::Kind::Endpoint);
  CHECK(kept.z == Approx(0.f));
}

TEST_CASE("SECTION refuses when the solids changed between selecting them and picking the plane",
          "[section][req335]") {
  // Finding 9. The selection is resolved to indices, and an index outlives the solid it named: here
  // slot 0 is replaced by a different solid before the plane is given. It must not be sectioned.
  AppCommandState st;
  AddBox(st, ucs::Ucs{}, 20.0, 10.0, 8.0);
  SelectedEntity sel{};
  sel.type = SelectedEntity::Type::Solid;
  sel.index = 0;
  st.selection.push_back(sel);
  std::vector<std::string> log;

  StartSectionCommand(st, log);
  REQUIRE(st.active == AppCommandState::Kind::Section);
  REQUIRE(st.sectionPhase == AppCommandState::SectionPhase::WaitP1);

  SECTION("unchanged, the section is made") {
    ucs::Ucs mid{};
    st.activeUcs = ucs::RotatedAboutX(mid, 90.0);  // a vertical plane through the box
    REQUIRE(HandleSectionTextInput("UCS", st, log));
    CHECK_FALSE(st.userPolylineOffsets.empty());
  }

  SECTION("replaced in the same slot, nothing is sectioned and the user is told") {
    brep::Solid other;
    brep::Problem why{};
    REQUIRE(brep::MakeBox(ucs::Ucs{}, 4.0, 4.0, 4.0, &other, &why));
    st.cadSolids[0] = std::make_shared<const brep::Solid>(std::move(other));
    RefreshSolidDisplayGeometry(st);
    st.activeUcs = ucs::RotatedAboutX(ucs::Ucs{}, 90.0);
    REQUIRE(HandleSectionTextInput("UCS", st, log));
    CHECK(st.userPolylineOffsets.empty());
    CHECK(LogHas(log, "the selected solids changed"));
    CHECK(st.active == AppCommandState::Kind::None);
  }
}

TEST_CASE("A bad point at SECTION's prompt says so once", "[section][req335]") {
  // Finding 11: the parser had already logged its reason, and SECTION added a second line.
  AppCommandState st;
  AddBox(st, ucs::Ucs{}, 20.0, 10.0, 8.0);
  SelectedEntity sel{};
  sel.type = SelectedEntity::Type::Solid;
  sel.index = 0;
  st.selection.push_back(sel);
  std::vector<std::string> log;
  StartSectionCommand(st, log);
  const std::size_t before = log.size();
  REQUIRE(HandleSectionTextInput("abc", st, log));
  CHECK(log.size() == before + 1);
  CHECK(st.sectionPhase == AppCommandState::SectionPhase::WaitP1);  // still waiting for the point
}

TEST_CASE("The clip indicator is sized from the drawing, not the UCS origin", "[sectionclip][req341]") {
  // Finding 8. A state-plane drawing with linework and no solids: the old fallback centred the
  // rectangle on the UCS origin, which in storage coordinates sat millions of feet off screen.
  AppCommandState st;
  st.worldDocumentOriginX = 2196000.0;
  st.worldDocumentOriginY = 715000.0;
  st.activeUcs.origin = {0.0, 0.0, 0.0};  // World UCS — storage origin at (-2196000, -715000)
  st.userLinesFlat = {100.f, 200.f, 50.f, 400.f, 260.f, 55.f};
  st.userLineAttrs.emplace_back();

  ray3d::Vec3 mn{}, mx{};
  REQUIRE(ComputeSectionClipIndicatorBounds(st, &mn, &mx));
  CHECK(mn.x == Approx(100.0));
  CHECK(mx.x == Approx(400.0));
  CHECK(mn.y == Approx(200.0));
  CHECK(mx.y == Approx(260.0));

  SECTION("with a solid, its Z range is covered") {
    AddBox(st, ucs::Ucs{}, 20.0, 10.0, 8.0);
    REQUIRE(ComputeSectionClipIndicatorBounds(st, &mn, &mx));
    CHECK(mn.z == Approx(0.0));
    CHECK(mx.z == Approx(8.0));
    CHECK(mn.x == Approx(-10.0));  // and the XY extents grow to include it
  }

  SECTION("an empty drawing reports nothing, so the caller centres on the view") {
    AppCommandState empty;
    CHECK_FALSE(ComputeSectionClipIndicatorBounds(empty, &mn, &mx));
  }
}
