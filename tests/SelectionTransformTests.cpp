// One transform path for MOVE, COPY, ROTATE, SCALE, MIRROR and ARRAY (ADR-0011 / issue #1577 PR 2).
//
// The phase 0 table (tests/EntityKindOpsTests.cpp) covers one entity of each kind through each
// command. These cases cover what the table cannot: mixed selections, the grouped refusal log,
// MIRROR's erase-source for kinds MIRROR newly copies, and a duplicated PDF underlay.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "CadCommands.hpp"

namespace {

void SubmitLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  char buf[512] = {};
  std::snprintf(buf, sizeof(buf), "%s", line.c_str());
  ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), st, log);
}

size_t CountLogLines(const std::vector<std::string>& log, const std::string& needle) {
  size_t n = 0;
  for (const std::string& line : log)
    if (line.find(needle) != std::string::npos)
      ++n;
  return n;
}

void AddHatch(AppCommandState& st, double x0) {
  CadFilledRegion fr;
  fr.vertsXyz = {x0, 12.0, 0.0, x0 + 4.0, 12.0, 0.0, x0, 15.0, 0.0};
  fr.loopStart = {0};
  fr.patternName = "ANSI31";
  st.drawing.cadFilledRegions.push_back(fr);
  st.drawing.cadFilledRegionAttrs.push_back(EntityAttributes{});
}

void AddMultileader(AppCommandState& st) {
  CadMultileader ml;
  ml.pathXyz = {16.f, 12.f, 0.f, 19.f, 14.f, 0.f};
  ml.label.kind = CadAnnotation::Kind::Mtext;
  ml.label.mtextAttach = 7;
  ml.label.insX = 19.f;
  ml.label.insY = 13.f;
  ml.label.boxMinX = 19.f;
  ml.label.boxMinY = 13.f;
  ml.label.boxMaxX = 22.f;
  ml.label.boxMaxY = 15.f;
  ml.label.text = "ML";
  st.drawing.cadMultileaders.push_back(ml);
  st.drawing.cadMultileaderAttrs.push_back(EntityAttributes{});
}

}  // namespace

TEST_CASE("MIRROR with erase-source removes the originals of every kind it now copies",
          "[issue1577][mirror]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddHatch(st, 16.0);
  AddMultileader(st);
  auto mesh = std::make_shared<CadMesh>();
  mesh->vertsXyz = {16.f, 12.f, 0.f, 20.f, 12.f, 0.f, 16.f, 15.f, 0.f};
  mesh->indices = {0, 1, 2};
  st.drawing.cadMeshes.push_back(mesh);
  st.drawing.cadMeshAttrs.push_back(EntityAttributes{});
  st.selection = {{SelectedEntity::Type::FilledRegion, 0},
                  {SelectedEntity::Type::Multileader, 0},
                  {SelectedEntity::Type::Mesh, 0}};

  REQUIRE(DispatchByPrimaryForTest("mirror", st, log));
  SubmitLine(st, "0,0", log);
  SubmitLine(st, "0,10", log);
  SubmitLine(st, "y", log);  // erase source

  INFO("log: " << (log.empty() ? std::string() : log.back()));
  REQUIRE(st.drawing.cadFilledRegions.size() == 1);
  REQUIRE(st.drawing.cadMultileaders.size() == 1);
  REQUIRE(st.drawing.cadMeshes.size() == 1);
  CHECK(st.drawing.cadFilledRegionAttrs.size() == 1);
  CHECK(st.drawing.cadMultileaderAttrs.size() == 1);
  CHECK(st.drawing.cadMeshAttrs.size() == 1);
  // What is left is the mirror image, not the source.
  CHECK(st.drawing.cadFilledRegions[0].vertsXyz[0] == Catch::Approx(-16.0));
  CHECK(st.drawing.cadMultileaders[0].pathXyz[0] == Catch::Approx(-16.f));
  CHECK(st.drawing.cadMeshes[0]->vertsXyz[0] == Catch::Approx(-16.f));
}

TEST_CASE("A mixed selection transforms what it can and names each refused kind once",
          "[issue1577][refusal]") {
  AppCommandState st;
  std::vector<std::string> log;
  st.drawing.pushLine(CadLineRecord{0.0, 0.0, 0.0, 10.0, 0.0, 0.0});
  st.drawing.userLineAttrs.push_back(EntityAttributes{});
  for (int i = 0; i < 2; ++i) {
    CadPositionMarker pm;
    pm.x = 5.0 + i;
    pm.label.kind = CadAnnotation::Kind::Mtext;
    st.drawing.cadPositionMarkers.push_back(pm);
    st.drawing.cadPositionMarkerAttrs.push_back(EntityAttributes{});
  }
  AddMultileader(st);
  st.selection = {{SelectedEntity::Type::LineSeg, 0},
                  {SelectedEntity::Type::PositionMarker, 0},
                  {SelectedEntity::Type::PositionMarker, 1},
                  {SelectedEntity::Type::Multileader, 0}};

  REQUIRE(DispatchByPrimaryForTest("rotate", st, log));
  SubmitLine(st, "0,0", log);
  SubmitLine(st, "90", log);  // clockwise from north: -90 deg CCW

  // One line for both markers, with the count and the reason.
  CHECK(CountLogLines(log, "ROTATE — 2 Position Marker(s) excluded: a Position Marker can only be moved") == 1);
  // The rest of the selection still turned: the line and the multileader (AutoCAD rotates it).
  const CadLineRecord ln = st.drawing.line(0);
  CHECK(ln.x1 == Catch::Approx(0.0).margin(1e-9));
  CHECK(ln.y1 == Catch::Approx(-10.0));
  CHECK(st.drawing.cadMultileaders[0].pathXyz[0] == Catch::Approx(12.f));
  CHECK(st.drawing.cadMultileaders[0].pathXyz[1] == Catch::Approx(-16.f));
  CHECK(st.drawing.cadPositionMarkers[0].x == Catch::Approx(5.0));
}

TEST_CASE("COPY duplicates a PDF underlay without sharing its GPU texture", "[issue1577][pdf]") {
  AppCommandState st;
  std::vector<std::string> log;
  PdfAttachment pdf;
  pdf.filePath = "plan.pdf";
  pdf.insertX = 16.f;
  pdf.insertY = 12.f;
  pdf.glTexId = 7;  // pretend the source is on the GPU
  pdf.texW = 640;
  pdf.texH = 480;
  st.drawing.pdfAttachments.push_back(pdf);
  st.selection = {{SelectedEntity::Type::PdfUnderlay, 0}};

  REQUIRE(DispatchByPrimaryForTest("copy", st, log));
  SubmitLine(st, "0,0", log);
  SubmitLine(st, "10,20", log);

  REQUIRE(st.drawing.pdfAttachments.size() == 2);
  const PdfAttachment& src = st.drawing.pdfAttachments[0];
  const PdfAttachment& copy = st.drawing.pdfAttachments[1];
  CHECK(src.glTexId == 7u);
  CHECK(copy.glTexId == 0u);  // erasing one can never release the other's texture
  CHECK(copy.needsReload);
  CHECK(copy.filePath == "plan.pdf");
  CHECK(copy.insertX == Catch::Approx(26.f));
  CHECK(copy.insertY == Catch::Approx(32.f));
  st.drawing.pdfAttachments[0].glTexId = 0;  // the test owns no real texture
}

TEST_CASE("ARRAY copies hatch fills and multileaders, and refuses Position Markers by name",
          "[issue1577][array]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddHatch(st, 0.0);
  AddMultileader(st);
  CadPositionMarker pm;
  pm.label.kind = CadAnnotation::Kind::Mtext;
  st.drawing.cadPositionMarkers.push_back(pm);
  st.drawing.cadPositionMarkerAttrs.push_back(EntityAttributes{});
  st.selection = {{SelectedEntity::Type::FilledRegion, 0},
                  {SelectedEntity::Type::Multileader, 0},
                  {SelectedEntity::Type::PositionMarker, 0}};

  REQUIRE(DispatchByPrimaryForTest("array", st, log));
  SubmitLine(st, "p", log);   // polar
  SubmitLine(st, "0,0", log); // centre
  SubmitLine(st, "4", log);   // items
  SubmitLine(st, "360", log); // fill angle
  SubmitLine(st, "y", log);   // rotate items

  INFO("log: " << (log.empty() ? std::string() : log.back()));
  CHECK(CountLogLines(log, "1 Position Marker(s) excluded") == 1);
  CHECK(st.drawing.cadFilledRegions.size() == 4);
  CHECK(st.drawing.cadMultileaders.size() == 4);
  CHECK(st.drawing.cadPositionMarkers.size() == 1);
}
