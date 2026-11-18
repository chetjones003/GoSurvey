// Issue #768 (REQ-113, D-2026-10-20-a) - a paper layout owns multileaders: they can be picked,
// box-selected, snapped to, moved, copied and erased like other sheet objects, but (as in model space)
// not rotated, scaled or mirrored. The paper selection is set directly because it is not scriptable
// through the headless driver yet (#767).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {

// Tip (1,1) -> landing (3,1); label box (3,0.8)-(5,1.2).
CadMultileader MakeLeader() {
  CadMultileader ml;
  ml.pathXyz = {1.f, 1.f, 0.f, 3.f, 1.f, 0.f};
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.text = "NOTE";
  ml.label.boxMinX = 3.f; ml.label.boxMinY = 0.8f;
  ml.label.boxMaxX = 5.f; ml.label.boxMaxY = 1.2f;
  return ml;
}

AppCommandState SheetWithLeader() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  PaperLayout L;
  L.name = "Sheet";
  L.paperMultileaders.push_back(MakeLeader());
  L.paperMultileaderAttrs.resize(1);
  st.paperLayouts.push_back(L);
  st.activeSpaceIndex = 0;
  st.selectedPaperEntities.push_back({PaperEntityRef::Type::Leader, 0});
  return st;
}

}  // namespace

TEST_CASE("Paper leader: click picks the leader line and the label box (#768)", "[req113][issue768]") {
  const AppCommandState st = SheetWithLeader();
  PaperEntityRef hit;
  CHECK(PickPaperEntityAt(st.paperLayouts[0], 2.f, 1.02f, 0.05f, &hit, nullptr));
  CHECK(hit.type == PaperEntityRef::Type::Leader);
  CHECK(PickPaperEntityAt(st.paperLayouts[0], 4.f, 1.f, 0.05f, &hit, nullptr));
  CHECK(hit.type == PaperEntityRef::Type::Leader);
  CHECK_FALSE(PickPaperEntityAt(st.paperLayouts[0], 2.f, 3.f, 0.05f, &hit, nullptr));
}

TEST_CASE("Paper leader: window needs all of it, crossing needs a touch (#768)", "[req113][issue768]") {
  const PaperLayout L = SheetWithLeader().paperLayouts[0];
  std::vector<PaperEntityRef> out;
  SelectPaperEntitiesInBox(L, 0.f, 0.f, 10.f, 10.f, /*windowMode=*/true, out);
  CHECK(out.size() == 1);
  out.clear();
  SelectPaperEntitiesInBox(L, 0.f, 0.f, 2.f, 2.f, /*windowMode=*/true, out);  // label is outside
  CHECK(out.empty());
  SelectPaperEntitiesInBox(L, 0.f, 0.f, 2.f, 2.f, /*windowMode=*/false, out);
  CHECK(out.size() == 1);
  out.clear();
  SelectPaperEntitiesInBox(L, 7.f, 7.f, 9.f, 9.f, /*windowMode=*/false, out);
  CHECK(out.empty());
}

TEST_CASE("Paper leader: snaps to the arrow tip and the landing (#768)", "[req113][issue768]") {
  const PaperLayout L = SheetWithLeader().paperLayouts[0];
  float x = 0.f, y = 0.f;
  REQUIRE(SnapPaperInchPoint(L, 1.01f, 1.01f, 0.1f, &x, &y));
  CHECK(x == Approx(1.f));
  REQUIRE(SnapPaperInchPoint(L, 3.01f, 0.99f, 0.1f, &x, &y));
  CHECK(x == Approx(3.f));
}

TEST_CASE("Paper leader: MOVE and COPY act on path and label together (#768)", "[req113][issue768]") {
  AppCommandState st = SheetWithLeader();
  std::vector<std::string> log;
  TranslateSelectedPaperEntities(st, 2.f, 1.f, /*copy=*/false, log);
  const CadMultileader& moved = st.paperLayouts[0].paperMultileaders[0];
  CHECK(moved.pathXyz[0] == Approx(3.f));
  CHECK(moved.pathXyz[1] == Approx(2.f));
  CHECK(moved.label.boxMinX == Approx(5.f));
  CHECK(moved.label.boxMaxY == Approx(2.2f));

  TranslateSelectedPaperEntities(st, 0.f, 1.f, /*copy=*/true, log);
  REQUIRE(st.paperLayouts[0].paperMultileaders.size() == 2);
  CHECK(st.paperLayouts[0].paperMultileaders[0].pathXyz[1] == Approx(2.f));  // original stays
  CHECK(st.paperLayouts[0].paperMultileaders[1].pathXyz[1] == Approx(3.f));
  REQUIRE(st.paperLayouts[0].paperMultileaderAttrs.size() == 2);
  REQUIRE(st.selectedPaperEntities.size() == 1);
  CHECK(st.selectedPaperEntities[0].index == 1);
}

TEST_CASE("Paper leader: ERASE removes it and its attributes (#768)", "[req113][issue768]") {
  AppCommandState st = SheetWithLeader();
  std::vector<std::string> log;
  DeleteSelectedPaperEntities(st, log);
  CHECK(st.paperLayouts[0].paperMultileaders.empty());
  CHECK(st.paperLayouts[0].paperMultileaderAttrs.empty());
  CHECK(st.selectedPaperEntities.empty());
}

TEST_CASE("Paper leader: ROTATE, SCALE and MIRROR refuse it by name (#768)", "[req113][issue768]") {
  AppCommandState st = SheetWithLeader();
  std::vector<std::string> log;
  RotateSelectedPaperEntities(st, 0.f, 0.f, 1.f, log);
  ScaleSelectedPaperEntities(st, 0.f, 0.f, 2.f, log);
  MirrorSelectedPaperEntities(st, 0.f, 0.f, 1.f, 0.f, log);
  const CadMultileader& ml = st.paperLayouts[0].paperMultileaders[0];
  CHECK(ml.pathXyz[0] == Approx(1.f));
  CHECK(ml.pathXyz[3] == Approx(3.f));
  CHECK(st.paperLayouts[0].paperMultileaders.size() == 1);
  bool named = false;
  for (const std::string& line : log)
    named = named || line.find("Multileader") != std::string::npos;
  CHECK(named);
}
