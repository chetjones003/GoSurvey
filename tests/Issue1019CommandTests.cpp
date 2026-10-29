// REQ-456 / GitHub issue #1019 — LEADER, SPLINE (paper), POINT, PEDIT, MEASURE, DIVIDE, AREA, LIST.
// Driven through ProcessCommandLineSubmit, the same path a typed command uses.

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

bool LogHas(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& line : log) {
    if (line.find(needle) != std::string::npos)
      return true;
  }
  return false;
}

void PushLine(AppCommandState& st, double x0, double y0, double x1, double y1) {
  st.userLinesFlat.push_back(x0);
  st.userLinesFlat.push_back(y0);
  st.userLinesFlat.push_back(0.0);
  st.userLinesFlat.push_back(x1);
  st.userLinesFlat.push_back(y1);
  st.userLinesFlat.push_back(0.0);
  st.userLineAttrs.push_back(EntityAttributes{});
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  return st;
}

AppCommandState PaperDrawing() {
  AppCommandState st = Drawing();
  PaperLayout L;
  L.name = "Sheet";
  L.paperLines = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
  L.paperLineAttrs.push_back(EntityAttributes{});
  st.paperLayouts.push_back(std::move(L));
  st.activeSpaceIndex = 0;
  return st;
}

}  // namespace

TEST_CASE("typed LEADER SPLINE POINT PEDIT MEASURE DIVIDE AREA LIST MLEADER are known",
          "[commands][req456][issue1019]") {
  const char* names[] = {"leader", "spline", "point", "pedit", "measure", "divide", "area", "list",
                         "mleader"};
  for (const char* name : names) {
    AppCommandState st = Drawing();
    std::vector<std::string> log;
    Submit(st, name, log);
    INFO("command " << name);
    CHECK_FALSE(LogHas(log, "Unknown command"));
    CHECK((!log.empty() || st.active != AppCommandState::Kind::None));
  }
}

TEST_CASE("POINT two typed coordinates exist, select, move, and UNDO restores then removes them",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "point", log);
  REQUIRE(st.active == AppCommandState::Kind::Point);
  Submit(st, "2,3", log);
  Submit(st, "4,5", log);
  Submit(st, "", log);
  REQUIRE(st.cadPoints.size() == 2);
  CHECK(st.cadPoints[0].x == Approx(2.0));
  CHECK(st.cadPoints[0].y == Approx(3.0));
  CHECK(st.cadPoints[1].x == Approx(4.0));
  CHECK(st.cadPoints[1].y == Approx(5.0));

  SelectedEntity hit{};
  float d2 = 0.f;
  REQUIRE(PickClosestCadEntity(st, 2.0, 3.0, 0.05f, &hit, &d2));
  CHECK(hit.type == SelectedEntity::Type::Point);
  CHECK(hit.index == 0);

  st.selection = {hit};
  Submit(st, "MOVE", log);
  Submit(st, "0,0", log);
  Submit(st, "1,0", log);
  CancelActiveCommand(st, log);
  CHECK(st.cadPoints[0].x == Approx(3.0));
  CHECK(st.cadPoints[0].y == Approx(3.0));
  Submit(st, "undo", log);
  CHECK(st.cadPoints.size() == 2);
  CHECK(st.cadPoints[0].x == Approx(2.0));
  Submit(st, "undo", log);
  CHECK(st.cadPoints.size() == 1);
  Submit(st, "undo", log);
  CHECK(st.cadPoints.empty());
}

TEST_CASE("PDMODE 0 draws a marker, PDMODE 1 draws nothing, PDSIZE 0 is 5 percent of view",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "pdmode 0", log);
  CHECK(st.pdMode == 0);
  Submit(st, "pdmode 1", log);
  CHECK(st.pdMode == 1);
  Submit(st, "pdsize 0", log);
  CHECK(st.pdSize == Approx(0.0));
  CHECK(CadPointMarkerSize(0.0, 100.0) == Approx(5.0));
  CadPoint p{0.0, 0.0, 0.0};
  std::vector<CadPointStroke> strokes;
  CadPointDisplayStrokes(p, 0, 1.0, &strokes);
  CHECK_FALSE(strokes.empty());
  CadPointDisplayStrokes(p, 1, 1.0, &strokes);
  CHECK(strokes.empty());
}

TEST_CASE("LEADER two points and None creates a multileader; Tolerance Copy Block are refused",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "leader", log);
  Submit(st, "0,0", log);
  Submit(st, "1,1", log);
  Submit(st, "none", log);
  Submit(st, "", log);
  REQUIRE(st.cadMultileaders.size() == 1);
  CHECK(st.cadMultileaders.front().pathXyz.size() >= 6);
  CHECK(st.cadMultileaders.front().label.text.empty());

  log.clear();
  Submit(st, "leader", log);
  Submit(st, "tolerance", log);
  CHECK(LogHas(log, "not supported"));
  CHECK(st.cadMultileaders.size() == 1);
  Submit(st, "copy", log);
  CHECK(LogHas(log, "not supported"));
  Submit(st, "block", log);
  CHECK(LogHas(log, "not supported"));
  Submit(st, "", log);
}

TEST_CASE("PEDIT Close on a converted line; Width is refused", "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  PushLine(st, 0.0, 0.0, 10.0, 0.0);
  Submit(st, "pedit", log);
  SubmitViewportPick(st, 5.0, 0.0, log, false, false, nullptr);
  REQUIRE(st.userPolylineOffsets.size() >= 2);
  CHECK(st.userPolylineClosed.back() == 0);
  Submit(st, "close", log);
  REQUIRE_FALSE(st.userPolylineClosed.empty());
  CHECK(st.userPolylineClosed.back() != 0);
  log.clear();
  Submit(st, "width", log);
  CHECK(LogHas(log, "not supported"));
}

TEST_CASE("MEASURE interval 2.5 on a 10-unit line places three points; DIVIDE 4 places three",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  PushLine(st, 0.0, 0.0, 10.0, 0.0);
  Submit(st, "measure", log);
  SubmitViewportPick(st, 0.1, 0.0, log, false, false, nullptr);
  Submit(st, "2.5", log);
  REQUIRE(st.cadPoints.size() == 3);
  CHECK(st.cadPoints[0].x == Approx(2.5));
  CHECK(st.cadPoints[1].x == Approx(5.0));
  CHECK(st.cadPoints[2].x == Approx(7.5));

  st.cadPoints.clear();
  st.cadPointAttrs.clear();
  Submit(st, "divide", log);
  SubmitViewportPick(st, 0.1, 0.0, log, false, false, nullptr);
  Submit(st, "4", log);
  REQUIRE(st.cadPoints.size() == 3);
  CHECK(st.cadPoints[0].x == Approx(2.5));
  CHECK(st.cadPoints[1].x == Approx(5.0));
  CHECK(st.cadPoints[2].x == Approx(7.5));
}

TEST_CASE("AREA of a 10 by 10 rectangle reports 100 and 40; LIST reports LINE",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "area", log);
  Submit(st, "0,0", log);
  Submit(st, "10,0", log);
  Submit(st, "10,10", log);
  Submit(st, "0,10", log);
  Submit(st, "", log);
  REQUIRE(LogHas(log, "100.0000"));
  REQUIRE(LogHas(log, "40.0000"));

  log.clear();
  PushLine(st, 1.0, 2.0, 3.0, 4.0);
  st.selection.push_back({SelectedEntity::Type::LineSeg, 0});
  Submit(st, "list", log);
  REQUIRE(LogHas(log, "LINE"));
  CHECK(LogHas(log, "layer"));
}

TEST_CASE("paper POINT LEADER SPLINE leave model stores empty", "[commands][req456][issue1019]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  const size_t modelPts = st.cadPoints.size();
  const size_t modelLeaders = st.cadMultileaders.size();
  const size_t modelSplines = st.cadSplines.size();
  const size_t modelLines = st.userLinesFlat.size();

  Submit(st, "point", log);
  Submit(st, "1,1", log);
  Submit(st, "", log);
  REQUIRE(st.paperLayouts[0].paperPoints.size() == 1);
  CHECK(st.cadPoints.size() == modelPts);

  Submit(st, "leader", log);
  Submit(st, "0,0", log);
  Submit(st, "2,0", log);
  Submit(st, "none", log);
  Submit(st, "", log);
  REQUIRE(st.paperLayouts[0].paperMultileaders.size() == 1);
  CHECK(st.cadMultileaders.size() == modelLeaders);

  Submit(st, "spline", log);
  Submit(st, "0,0", log);
  Submit(st, "1,1", log);
  Submit(st, "2,0", log);
  Submit(st, "end", log);
  REQUIRE(st.paperLayouts[0].paperSplines.size() == 1);
  CHECK(st.cadSplines.size() == modelSplines);
  CHECK(st.userLinesFlat.size() == modelLines);
}

TEST_CASE("paper MEASURE DIVIDE AREA LIST PEDIT leave model geometry unchanged",
          "[commands][req456][issue1019]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  const size_t modelLines = st.userLinesFlat.size();
  const size_t modelPts = st.cadPoints.size();
  const size_t modelPolys = st.userPolylineOffsets.size();

  Submit(st, "measure", log);
  SubmitViewportPick(st, 1.0, 0.0, log, false, false, nullptr);
  Submit(st, "2.5", log);
  CHECK(st.userLinesFlat.size() == modelLines);
  CHECK(st.cadPoints.size() == modelPts);
  CHECK(st.paperLayouts[0].paperPoints.size() == 3);

  Submit(st, "area", log);
  Submit(st, "0,0", log);
  Submit(st, "1,0", log);
  Submit(st, "1,1", log);
  Submit(st, "", log);
  CHECK(st.userLinesFlat.size() == modelLines);

  st.selectedPaperEntities.push_back({PaperEntityRef::Type::Line, 0});
  Submit(st, "list", log);
  CHECK(LogHas(log, "paper"));
  CHECK(st.userLinesFlat.size() == modelLines);

  Submit(st, "pedit", log);
  SubmitViewportPick(st, 1.0, 0.0, log, false, false, nullptr);
  CHECK(st.userPolylineOffsets.size() == modelPolys);
}

TEST_CASE("PEDIT Join concatenates touching polylines; Insert adds a vertex",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  PushLine(st, 0.0, 0.0, 10.0, 0.0);
  PushLine(st, 10.0, 0.0, 20.0, 0.0);
  Submit(st, "pedit", log);
  SubmitViewportPick(st, 5.0, 0.0, log, false, false, nullptr);
  REQUIRE(st.userPolylineOffsets.size() >= 2);
  Submit(st, "join", log);
  SubmitViewportPick(st, 15.0, 0.0, log, false, false, nullptr);
  REQUIRE(st.userPolylineOffsets.size() == 2);
  CHECK(st.userPolylineOffsets.back() == 3);
  CHECK(st.userLinesFlat.empty());
  CHECK(LogHas(log, "joined"));

  log.clear();
  Submit(st, "edit", log);
  Submit(st, "insert", log);
  Submit(st, "5,1", log);
  CHECK(st.userPolylineOffsets.back() == 4);
  CHECK(LogHas(log, "inserted"));
}

TEST_CASE("MEASURE starts at the picked end; DIVIDE of a closed polyline places N points",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  PushLine(st, 0.0, 0.0, 10.0, 0.0);
  Submit(st, "measure", log);
  SubmitViewportPick(st, 9.9, 0.0, log, false, false, nullptr);
  Submit(st, "2.5", log);
  REQUIRE(st.cadPoints.size() == 3);
  CHECK(st.cadPoints[0].x == Approx(7.5));
  CHECK(st.cadPoints[1].x == Approx(5.0));
  CHECK(st.cadPoints[2].x == Approx(2.5));

  st.cadPoints.clear();
  st.cadPointAttrs.clear();
  st.userLinesFlat.clear();
  st.userLineAttrs.clear();
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  st.userPolylineVerts = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 10.0, 10.0, 0.0, 0.0, 10.0, 0.0};
  st.userPolylineOffsets.push_back(4);
  st.userPolylineClosed.push_back(1);
  st.userPolylineAttrs.push_back(EntityAttributes{});
  Submit(st, "divide", log);
  SubmitViewportPick(st, 5.0, 10.0, log, false, false, nullptr);
  Submit(st, "4", log);
  REQUIRE(st.cadPoints.size() == 4);
}

TEST_CASE("AREA Add accumulates two rectangles; paper LIST reports line endpoints",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "area", log);
  Submit(st, "add", log);
  Submit(st, "0,0", log);
  Submit(st, "10,0", log);
  Submit(st, "10,10", log);
  Submit(st, "0,10", log);
  Submit(st, "", log);
  REQUIRE(LogHas(log, "100.0000"));
  CHECK(st.active == AppCommandState::Kind::Area);
  Submit(st, "0,0", log);
  Submit(st, "10,0", log);
  Submit(st, "10,10", log);
  Submit(st, "0,10", log);
  Submit(st, "", log);
  REQUIRE(LogHas(log, "200.0000"));

  AppCommandState paper = PaperDrawing();
  log.clear();
  paper.selectedPaperEntities.push_back({PaperEntityRef::Type::Line, 0});
  Submit(paper, "list", log);
  REQUIRE(LogHas(log, "LINE"));
  CHECK(LogHas(log, "(0.0000,0.0000) to (10.0000,0.0000)"));
}

TEST_CASE("paper PEDIT Fit stores bulge; LEADER Format Spline densifies the path",
          "[commands][req456][issue1019]") {
  AppCommandState st = PaperDrawing();
  std::vector<std::string> log;
  Submit(st, "pedit", log);
  SubmitViewportPick(st, 1.0, 0.0, log, false, false, nullptr);
  Submit(st, "fit", log);
  REQUIRE_FALSE(st.paperLayouts[0].paperPolyVertsBulge.empty());
  CHECK(st.paperLayouts[0].paperPolyVertsBulge.front() == Approx(0.5f));
  Submit(st, "decurve", log);
  CHECK(st.paperLayouts[0].paperPolyVertsBulge.front() == Approx(0.0f));
  Submit(st, "", log);

  AppCommandState model = Drawing();
  log.clear();
  Submit(model, "leader", log);
  Submit(model, "0,0", log);
  Submit(model, "1,0", log);
  Submit(model, "1,1", log);
  Submit(model, "format", log);
  Submit(model, "spline", log);
  Submit(model, "none", log);
  Submit(model, "", log);
  REQUIRE(model.cadMultileaders.size() == 1);
  CHECK(model.cadMultileaders.front().splinePath);
  CHECK(model.cadMultileaders.front().pathXyz.size() > 9);
}

TEST_CASE("LIST reports polyline spline and leader geometry; AREA Object refuses an open polyline",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  st.userPolylineVerts = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 10.0, 10.0, 0.0};
  st.userPolylineOffsets.push_back(3);
  st.userPolylineClosed.push_back(0);
  st.userPolylineAttrs.push_back(EntityAttributes{});
  st.selection.push_back({SelectedEntity::Type::Polyline, 0});
  Submit(st, "list", log);
  REQUIRE(LogHas(log, "POLYLINE"));
  CHECK(LogHas(log, "3 vertices"));
  CHECK(LogHas(log, "open"));
  CHECK(LogHas(log, "(0.0000,0.0000) to (10.0000,10.0000)"));

  CadSpline spline;
  std::string why;
  REQUIRE(spline::MakeFit({{0.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {2.0, 0.0, 0.0}}, false, &spline, &why));
  st.cadSplines.push_back(spline);
  st.cadSplineAttrs.push_back(EntityAttributes{});
  log.clear();
  st.selection = {{SelectedEntity::Type::Spline, 0}};
  Submit(st, "list", log);
  REQUIRE(LogHas(log, "SPLINE"));
  CHECK(LogHas(log, "fit 3"));

  log.clear();
  Submit(st, "leader", log);
  Submit(st, "0,0", log);
  Submit(st, "4,0", log);
  Submit(st, "none", log);
  Submit(st, "", log);
  REQUIRE(st.cadMultileaders.size() == 1);
  log.clear();
  st.selection = {{SelectedEntity::Type::Multileader, 0}};
  Submit(st, "list", log);
  REQUIRE(LogHas(log, "LEADER"));
  CHECK(LogHas(log, "path points"));
  CHECK(LogHas(log, "landing"));

  log.clear();
  Submit(st, "area", log);
  Submit(st, "object", log);
  SubmitViewportPick(st, 5.0, 0.0, log, false, false, nullptr);
  REQUIRE(LogHas(log, "closed polyline or circle"));
}

TEST_CASE("LEADER Format None omits the arrow; Arrow keeps it; helper builds a tip triangle",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "leader", log);
  Submit(st, "0,0", log);
  Submit(st, "2,0", log);
  Submit(st, "format", log);
  Submit(st, "none", log);
  Submit(st, "none", log);
  Submit(st, "", log);
  REQUIRE(st.cadMultileaders.size() == 1);
  CHECK_FALSE(st.cadMultileaders.front().showArrow);
  const CadLeaderArrowHead hidden =
      CadMultileaderArrow(st.cadMultileaders.front(), st.cadMultileaders.front().pathXyz, 0.2f);
  CHECK_FALSE(hidden.valid);

  log.clear();
  Submit(st, "leader", log);
  Submit(st, "0,0", log);
  Submit(st, "2,0", log);
  Submit(st, "format", log);
  Submit(st, "arrow", log);
  Submit(st, "none", log);
  Submit(st, "", log);
  REQUIRE(st.cadMultileaders.size() == 2);
  CHECK(st.cadMultileaders.back().showArrow);
  const CadLeaderArrowHead shown =
      CadMultileaderArrow(st.cadMultileaders.back(), st.cadMultileaders.back().pathXyz, 0.2f);
  REQUIRE(shown.valid);
  CHECK(shown.tipX == Approx(0.f));
  CHECK(shown.tipY == Approx(0.f));
  CHECK(shown.b1x > 0.f);
}

TEST_CASE("MEASURE open polyline starts at the nearer end; PEDIT Fit sets the closing bulge",
          "[commands][req456][issue1019]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  if (st.userPolylineOffsets.empty())
    st.userPolylineOffsets.push_back(0);
  st.userPolylineVerts = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 20.0, 0.0, 0.0};
  st.userPolylineOffsets.push_back(3);
  st.userPolylineClosed.push_back(0);
  st.userPolylineAttrs.push_back(EntityAttributes{});
  Submit(st, "measure", log);
  SubmitViewportPick(st, 19.9, 0.0, log, false, false, nullptr);
  Submit(st, "5", log);
  REQUIRE(st.cadPoints.size() == 3);
  CHECK(st.cadPoints[0].x == Approx(15.0));
  CHECK(st.cadPoints[1].x == Approx(10.0));
  CHECK(st.cadPoints[2].x == Approx(5.0));

  AppCommandState closed = Drawing();
  log.clear();
  if (closed.userPolylineOffsets.empty())
    closed.userPolylineOffsets.push_back(0);
  closed.userPolylineVerts = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 10.0, 10.0, 0.0, 0.0, 10.0, 0.0};
  closed.userPolylineOffsets.push_back(4);
  closed.userPolylineClosed.push_back(1);
  closed.userPolylineAttrs.push_back(EntityAttributes{});
  Submit(closed, "pedit", log);
  SubmitViewportPick(closed, 5.0, 0.0, log, false, false, nullptr);
  Submit(closed, "fit", log);
  REQUIRE(closed.userPolylineVertsBulge.size() >= 4);
  CHECK(closed.userPolylineVertsBulge[3] == Approx(0.5f));
}
