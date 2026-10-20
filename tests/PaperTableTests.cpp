// Issue #768 (REQ-113, D-2026-10-20-a) - a paper layout owns first-class tables (REQ-148): they can be
// picked, box-selected, snapped to, moved, copied, rotated, scaled, mirrored and erased like the other
// sheet objects. The paper selection is set directly because it is not scriptable through the headless
// driver yet (#767).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {

// A 4 x 2 in table (2 columns, 2 rows) whose top-left is (1, 5).
CadTable MakeTable() {
  CadTable t;
  t.insX = 1.f;
  t.insY = 5.f;
  t.width = 4.f;
  t.height = 2.f;
  t.cols = 2;
  t.cells = {"A", "B", "C", "D"};
  t.plottedHeightInches = 0.1f;
  return t;
}

AppCommandState SheetWithTable() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  PaperLayout L;
  L.name = "Sheet";
  L.paperTables.push_back(MakeTable());
  L.paperTableAttrs.resize(1);
  st.paperLayouts.push_back(L);
  st.activeSpaceIndex = 0;
  st.selectedPaperEntities.push_back({PaperEntityRef::Type::Table, 0});
  return st;
}

}  // namespace

TEST_CASE("Paper table: click inside picks it, outside does not (#768)", "[req113][issue768]") {
  const AppCommandState st = SheetWithTable();
  PaperEntityRef hit;
  CHECK(PickPaperEntityAt(st.paperLayouts[0], 3.f, 4.f, 0.05f, &hit, nullptr));
  CHECK(hit.type == PaperEntityRef::Type::Table);
  CHECK_FALSE(PickPaperEntityAt(st.paperLayouts[0], 8.f, 8.f, 0.05f, &hit, nullptr));
}

TEST_CASE("Paper table: window needs all of it, crossing needs a touch (#768)", "[req113][issue768]") {
  const PaperLayout L = SheetWithTable().paperLayouts[0];
  std::vector<PaperEntityRef> out;
  SelectPaperEntitiesInBox(L, 0.f, 0.f, 10.f, 10.f, /*windowMode=*/true, out);
  CHECK(out.size() == 1);
  out.clear();
  SelectPaperEntitiesInBox(L, 0.f, 0.f, 3.f, 6.f, /*windowMode=*/true, out);
  CHECK(out.empty());
  SelectPaperEntitiesInBox(L, 0.f, 0.f, 3.f, 6.f, /*windowMode=*/false, out);
  CHECK(out.size() == 1);
}

TEST_CASE("Paper table: snaps to its corners (#768)", "[req113][issue768]") {
  const PaperLayout L = SheetWithTable().paperLayouts[0];
  float x = 0.f, y = 0.f;
  REQUIRE(SnapPaperInchPoint(L, 5.02f, 3.01f, 0.1f, &x, &y));  // bottom-right corner
  CHECK(x == Approx(5.f));
  CHECK(y == Approx(3.f));
}

TEST_CASE("Paper table: MOVE, COPY and ERASE (#768)", "[req113][issue768]") {
  AppCommandState st = SheetWithTable();
  std::vector<std::string> log;
  TranslateSelectedPaperEntities(st, 2.f, 1.f, /*copy=*/false, log);
  CHECK(st.paperLayouts[0].paperTables[0].insX == Approx(3.f));
  CHECK(st.paperLayouts[0].paperTables[0].insY == Approx(6.f));
  TranslateSelectedPaperEntities(st, 0.f, 1.f, /*copy=*/true, log);
  REQUIRE(st.paperLayouts[0].paperTables.size() == 2);
  REQUIRE(st.paperLayouts[0].paperTableAttrs.size() == 2);
  CHECK(st.paperLayouts[0].paperTables[1].insY == Approx(7.f));
  CHECK(st.paperLayouts[0].paperTables[1].cells.size() == 4);
  DeleteSelectedPaperEntities(st, log);  // the copy is the selection
  CHECK(st.paperLayouts[0].paperTables.size() == 1);
  CHECK(st.paperLayouts[0].paperTableAttrs.size() == 1);
}

TEST_CASE("Paper table: ROTATE, SCALE and MIRROR (#768)", "[req113][issue768]") {
  std::vector<std::string> log;
  {
    AppCommandState st = SheetWithTable();
    RotateSelectedPaperEntities(st, 1.f, 5.f, 1.5707963f, log);  // 90 deg about its own top-left
    CHECK(st.paperLayouts[0].paperTables[0].rotationRad == Approx(1.5707963f));
    CHECK(st.paperLayouts[0].paperTables[0].insX == Approx(1.f));
  }
  {
    AppCommandState st = SheetWithTable();
    ScaleSelectedPaperEntities(st, 1.f, 5.f, 2.f, log);
    CHECK(st.paperLayouts[0].paperTables[0].width == Approx(8.f));
    CHECK(st.paperLayouts[0].paperTables[0].height == Approx(4.f));
  }
  {
    AppCommandState st = SheetWithTable();
    MirrorSelectedPaperEntities(st, 0.f, 0.f, 0.f, 1.f, log);  // across the Y axis; the source stays
    REQUIRE(st.paperLayouts[0].paperTables.size() == 2);
    CHECK(st.paperLayouts[0].paperTables[0].insX == Approx(1.f));
  }
}
