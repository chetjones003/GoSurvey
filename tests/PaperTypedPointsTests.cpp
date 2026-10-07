// Issue #764 (REQ-039) - paper-space MOVE / COPY / ROTATE / MIRROR / SCALE accept typed points, and
// paper SCALE acts on the existing paper selection. The paper selection is set directly because it is
// not scriptable through the headless driver yet (#767).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

// One layout, active, holding a line (0,0)-(2,0) and a circle at (1,1) r=0.5, both selected.
AppCommandState PaperDrawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  PaperLayout L;
  L.name = "Sheet";
  L.paperLines = {0.f, 0.f, 0.f, 2.f, 0.f, 0.f};
  L.paperCircles = {1.f, 1.f, 0.5f};
  st.paperLayouts.push_back(L);
  st.activeSpaceIndex = 0;
  st.selectedPaperEntities.push_back({PaperEntityRef::Type::Line, 0});
  st.selectedPaperEntities.push_back({PaperEntityRef::Type::Circle, 0});
  return st;
}

PaperLayout& Sheet(AppCommandState& st) { return st.paperLayouts[0]; }

}  // namespace

TEST_CASE("Paper MOVE takes a typed base point and a typed destination (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartMoveCommand(st, log);
  REQUIRE(st.paperMovePhase == 1);
  Submit(st, "0,0", log);
  REQUIRE(st.paperMovePhase == 2);
  Submit(st, "3,4", log);
  CHECK(st.paperMovePhase == 0);
  CHECK(Sheet(st).paperLines[0] == Approx(3.f));
  CHECK(Sheet(st).paperLines[1] == Approx(4.f));
  CHECK(Sheet(st).paperCircles[0] == Approx(4.f));
  CHECK(Sheet(st).paperLines.size() == 6);  // MOVE does not duplicate
}

TEST_CASE("Paper MOVE accepts a relative destination (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartMoveCommand(st, log);
  Submit(st, "1,1", log);
  Submit(st, "@2,0", log);  // delta from the base point
  CHECK(Sheet(st).paperLines[0] == Approx(2.f));
  CHECK(Sheet(st).paperLines[1] == Approx(0.f));
}

TEST_CASE("Paper COPY with typed points duplicates and keeps the originals (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartCopyCommand(st, log);
  Submit(st, "0,0", log);
  Submit(st, "0,5", log);
  REQUIRE(Sheet(st).paperLines.size() == 12);
  CHECK(Sheet(st).paperLines[1] == Approx(0.f));   // original stays
  CHECK(Sheet(st).paperLines[7] == Approx(5.f));   // copy moved up
  CHECK(Sheet(st).paperCircles.size() == 6);
}

TEST_CASE("Paper ROTATE takes a typed base point and a typed angle point (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartRotateCommand(st, log);
  REQUIRE(st.paperRotatePhase == 1);
  Submit(st, "0,0", log);
  REQUIRE(st.paperRotatePhase == 2);
  Submit(st, "0,1", log);  // direction +Y from the base = a 90 degree turn
  CHECK(st.paperRotatePhase == 0);
  CHECK(Sheet(st).paperLines[3] == Approx(0.f).margin(1e-5));
  CHECK(Sheet(st).paperLines[4] == Approx(2.f));
}

TEST_CASE("Paper MIRROR takes two typed mirror-line points (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartMirrorCommand(st, log);
  REQUIRE(st.paperMirrorPhase == 1);
  Submit(st, "0,0", log);
  REQUIRE(st.paperMirrorPhase == 2);
  Submit(st, "0,1", log);  // mirror about the Y axis
  CHECK(st.paperMirrorPhase == 0);
  REQUIRE(Sheet(st).paperLines.size() == 12);  // paper MIRROR keeps the source
  CHECK(Sheet(st).paperLines[9] == Approx(-2.f));
}

TEST_CASE("Paper MIRROR refuses a zero-length mirror line (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartMirrorCommand(st, log);
  Submit(st, "1,1", log);
  Submit(st, "1,1", log);
  CHECK(st.paperMirrorPhase == 0);
  CHECK(Sheet(st).paperLines.size() == 6);
}

TEST_CASE("Paper SCALE uses the existing selection and a typed factor (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartScaleCommand(st, log);
  REQUIRE(st.paperScalePhase == 1);  // went straight to the base point, not "click objects"
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(st.selectedPaperEntities.size() == 2);
  Submit(st, "0,0", log);
  REQUIRE(st.paperScalePhase == 2);
  Submit(st, "2", log);
  CHECK(st.paperScalePhase == 0);
  CHECK(Sheet(st).paperLines[3] == Approx(4.f));
  CHECK(Sheet(st).paperCircles[0] == Approx(2.f));
  CHECK(Sheet(st).paperCircles[2] == Approx(1.f));  // radius scales too
}

TEST_CASE("Paper SCALE accepts a typed point as the factor (distance from the base) (#764)",
          "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartScaleCommand(st, log);
  Submit(st, "0,0", log);
  Submit(st, "3,0", log);  // 3 paper inches from the base
  CHECK(Sheet(st).paperLines[3] == Approx(6.f));
}

TEST_CASE("Paper SCALE rejects a bad factor and keeps waiting (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartScaleCommand(st, log);
  Submit(st, "0,0", log);
  Submit(st, "-2", log);
  CHECK(st.paperScalePhase == 2);
  CHECK(Sheet(st).paperLines[3] == Approx(2.f));  // unchanged
  Submit(st, "0", log);
  CHECK(st.paperScalePhase == 2);
}

TEST_CASE("Paper SCALE with nothing selected asks for a selection (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  st.selectedPaperEntities.clear();
  std::vector<std::string> log;
  StartScaleCommand(st, log);
  CHECK(st.paperScalePhase == 0);
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("select paper object") != std::string::npos);
}

TEST_CASE("Paper SCALE is undoable in one step (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartScaleCommand(st, log);
  Submit(st, "0,0", log);
  Submit(st, "2", log);
  REQUIRE(DoUndo(st, log));
  CHECK(Sheet(st).paperLines[3] == Approx(2.f));
  CHECK(Sheet(st).paperCircles[2] == Approx(0.5f));
}

TEST_CASE("A command name typed mid-gesture still reaches normal dispatch (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartMoveCommand(st, log);
  Submit(st, "0,0", log);
  Submit(st, "LINE", log);  // not a point - must not be swallowed as a bad destination
  CHECK(st.active == AppCommandState::Kind::Line);
}

TEST_CASE("A stale paper gesture does not swallow typed points in model space (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartMoveCommand(st, log);
  REQUIRE(st.paperMovePhase == 1);
  st.activeSpaceIndex = kModelSpaceIndex;  // switched to model space without finishing the gesture
  Submit(st, "0,0", log);
  CHECK(st.paperMovePhase == 1);  // untouched: the text was not consumed as a paper base point
}

TEST_CASE("Paper SCALE reads a comma pair as a point, not as its first number (#764)", "[req039][issue764]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  StartScaleCommand(st, log);
  Submit(st, "0,0", log);
  Submit(st, "3,4", log);  // distance 5 from the base -> factor 5 (a prefix parse would give 3)
  CHECK(Sheet(st).paperLines[3] == Approx(10.f));
}
