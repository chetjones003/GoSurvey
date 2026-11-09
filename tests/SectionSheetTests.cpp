// REQ-519 / GitHub issue #1001 - sample lines and section sheets through the typed commands: creation, refusals,
// one-step undo, read-only, the sheets on the title block, and save/reopen.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentSampleLines.hpp"

using Catch::Approx;

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

bool LogHas(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& line : log)
    if (line.find(needle) != std::string::npos) return true;
  return false;
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  return st;
}

// A straight alignment running due north from (0, 0), 500 ft long, in a feet drawing.
void NorthRoad(AppCommandState& st, std::vector<std::string>& log) {
  Submit(st, "ALIGNMENT Road", log);
  Submit(st, "ALIGNMENTADD Road, TANGENT, 0, 0, 0, 500", log);
}

// A flat TIN surface named Ground at elevation 100, covering 2000 ft either side of the origin.
void AddFlatGround(AppCommandState& st) {
  CadSurface surface;
  surface.name = "Ground";
  surface.kind = SurfaceKind::Tin;
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {-2000, -2000, 100, 2000, -2000, 100, 2000, 2000, 100, -2000, 2000, 100};
  tin->indices = {0, 1, 2, 0, 2, 3};
  surface.tin = tin;
  st.cadSurfaces.push_back(surface);
}

bool TitleBlockAvailable() {
  const char* local = std::getenv("LOCALAPPDATA");
  if (local == nullptr) return false;
  const std::filesystem::path path = std::filesystem::path(local) / "Autodesk" / "C3D 2026" / "enu" / "Template" /
                                     "AutoCAD Template" / "Generic 24in x 36in Title Block.dwg";
  return std::filesystem::exists(path);
}

}  // namespace

TEST_CASE("CREATESAMPLELINES makes the 11 lines of the REQ-519 acceptance run, each sampled from the surface",
          "[req519][samplelines]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  AddFlatGround(st);

  Submit(st, "CREATESAMPLELINES Road, 0+00, 5+00, 50, 50, 50, Ground", log);
  REQUIRE(LogHas(log, "11 sample line(s)"));
  REQUIRE(st.sampleLines.size() == 11);
  CHECK(st.sampleLines.front().name == "0+00");
  CHECK(st.sampleLines.back().name == "5+00");
  for (const alignment::SampleLine& line : st.sampleLines) {
    CHECK(line.alignmentId == st.alignments[0].id);
    CHECK(line.hasSection);
    CHECK(line.surfaceName == "Ground");
    REQUIRE_FALSE(line.section.empty());
    for (const alignment::SectionPoint& p : line.section) CHECK(p.ground == Approx(100.0));
  }
}

TEST_CASE("CREATESAMPLELINES without a surface makes lines with no sections", "[req519][samplelines]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  Submit(st, "CREATESAMPLELINES Road, 0+00, 2+00, 50, 50, 50", log);
  REQUIRE(st.sampleLines.size() == 5);
  for (const alignment::SampleLine& line : st.sampleLines) {
    CHECK_FALSE(line.hasSection);
    CHECK(line.section.empty());
  }
  CHECK(LogHas(log, "no sections"));
}

TEST_CASE("CREATESAMPLELINES refuses, names the reason and creates nothing", "[req519][samplelines]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  AddFlatGround(st);

  Submit(st, "CREATESAMPLELINES Road, 0+00, 5+00, 0, 50, 50, Ground", log);
  CHECK(LogHas(log, "interval"));
  CHECK(st.sampleLines.empty());

  Submit(st, "CREATESAMPLELINES Road, 5+00, 0+00, 50, 50, 50, Ground", log);
  CHECK(LogHas(log, "before the start station"));
  CHECK(st.sampleLines.empty());

  Submit(st, "CREATESAMPLELINES Road, 0+00, 5+00, 50, 50, 50, Nowhere", log);
  CHECK(LogHas(log, "no surface named"));
  CHECK(st.sampleLines.empty());

  Submit(st, "CREATESAMPLELINES Nope, 0+00, 5+00, 50, 50, 50", log);
  CHECK(st.sampleLines.empty());

  Submit(st, "CREATESAMPLELINES Road, 0+00, 5+00, 50, 50", log);  // too few fields
  CHECK(LogHas(log, "usage: CREATESAMPLELINES"));
  CHECK(st.sampleLines.empty());
}

TEST_CASE("sample lines: one undo step removes every line, and deleting the alignment removes its lines",
          "[req519][samplelines]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  AddFlatGround(st);
  Submit(st, "CREATESAMPLELINES Road, 0+00, 5+00, 50, 50, 50, Ground", log);
  REQUIRE(st.sampleLines.size() == 11);

  Submit(st, "UNDO", log);  // REQ-519.5: one undo step
  CHECK(st.sampleLines.empty());

  Submit(st, "CREATESAMPLELINES Road, 0+00, 5+00, 50, 50, 50, Ground", log);
  REQUIRE(st.sampleLines.size() == 11);
  Submit(st, "ALIGNMENTDELETE Road", log);
  CHECK(st.sampleLines.empty());  // REQ-519.13
}

TEST_CASE("sample lines: a read-only project refuses and changes nothing", "[req519][samplelines]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  AddFlatGround(st);
  st.drawingTabs.resize(2);
  AppCommandState::ProjectSession session;
  session.uid = 7;
  session.readOnly = true;
  st.openProjects.push_back(session);
  st.drawingTabs[1].projectUid = 7;

  Submit(st, "CREATESAMPLELINES Road, 0+00, 5+00, 50, 50, 50, Ground", log);
  CHECK(LogHas(log, "read-only"));
  CHECK(st.sampleLines.empty());
}

TEST_CASE("sample lines: a surface that is not a TIN refuses before any line is made", "[req519][samplelines]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  AddFlatGround(st);
  st.cadSurfaces.back().kind = SurfaceKind::Grid;
  Submit(st, "CREATESAMPLELINES Road, 0+00, 5+00, 50, 50, 50, Ground", log);
  CHECK(LogHas(log, "not a built TIN"));
  CHECK(st.sampleLines.empty());
}

TEST_CASE("sample lines survive save and reopen with their sections", "[req519][samplelines]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  AddFlatGround(st);
  Submit(st, "CREATESAMPLELINES Road, 0+00, 1+00, 50, 50, 50, Ground", log);
  REQUIRE(st.sampleLines.size() == 3);

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.sampleLines.size() == 3);
  const alignment::SampleLine& line = back.sampleLines[1];
  CHECK(line.name == "0+50");
  CHECK(line.id == st.sampleLines[1].id);
  CHECK(line.alignmentId == back.alignments[0].id);
  CHECK(line.origin.y == Approx(50.0));
  CHECK(line.section.size() == st.sampleLines[1].section.size());
  REQUIRE_FALSE(line.section.empty());
  CHECK(line.section.front().ground == Approx(100.0));
}

TEST_CASE("CREATESECTIONSHEETS: three lines, two sections per sheet, make two ARCH D layouts in station order",
          "[req519][sheets]") {
  if (!TitleBlockAvailable()) {
    WARN("Generic 24in x 36in title block not installed; skipping");
    return;
  }
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  AddFlatGround(st);
  Submit(st, "CREATESAMPLELINES Road, 0+00, 1+00, 50, 50, 50, Ground", log);
  REQUIRE(st.sampleLines.size() == 3);
  const size_t layoutsBefore = st.paperLayouts.size();

  Submit(st, "CREATESECTIONSHEETS Road, ALL, 2, 90", log);
  REQUIRE(st.paperLayouts.size() == layoutsBefore + 2);
  const PaperLayout& first = st.paperLayouts[layoutsBefore];
  const PaperLayout& second = st.paperLayouts[layoutsBefore + 1];
  CHECK(first.name == "SEC-1");
  CHECK(second.name == "SEC-2");
  CHECK(first.presetIdx >= 0);
  // The first sheet holds the first two sections, the second the third (station order).
  auto holds = [](const PaperLayout& sheet, const char* name) {
    for (const CadAnnotation& text : sheet.paperTexts)
      if (text.text == name) return true;
    return false;
  };
  CHECK(holds(first, "0+00"));
  CHECK(holds(first, "0+50"));
  CHECK_FALSE(holds(first, "1+00"));
  CHECK(holds(second, "1+00"));
  CHECK_FALSE(holds(second, "0+00"));
  // The sheet states its scales as text, one horizontal and one vertical (REQ-519.11).
  auto hasText = [](const PaperLayout& sheet, const char* needle) {
    for (const CadAnnotation& text : sheet.paperTexts)
      if (text.text.find(needle) != std::string::npos) return true;
    return false;
  };
  CHECK(hasText(first, "Horizontal 1\" = "));
  CHECK(hasText(first, "Vertical 1\" = "));
  CHECK_FALSE(hasText(first, "no data"));  // every section on the first sheet has ground
  CHECK(LogHas(log, "3 section(s) on 2 sheet(s)"));

  Submit(st, "UNDO", log);  // one undo step removes every sheet
  CHECK(st.paperLayouts.size() == layoutsBefore);
}

TEST_CASE("CREATESECTIONSHEETS refuses and creates nothing for a bad request", "[req519][sheets]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  AddFlatGround(st);
  const size_t layoutsBefore = st.paperLayouts.size();

  Submit(st, "CREATESECTIONSHEETS Road, ALL, 2, 90", log);  // no sample lines yet
  CHECK(LogHas(log, "has no sample lines"));

  Submit(st, "CREATESAMPLELINES Road, 0+00, 1+00, 50, 50, 50", log);  // lines with no sections
  Submit(st, "CREATESECTIONSHEETS Road, ALL, 2, 90", log);
  CHECK(LogHas(log, "none of the chosen sample lines has a section"));
  CHECK(st.paperLayouts.size() == layoutsBefore);

  Submit(st, "CREATESECTIONSHEETS Road, 0+00 9+99, 2, 90", log);
  CHECK(LogHas(log, "no sample line named \"9+99\""));
  Submit(st, "CREATESECTIONSHEETS Road, ALL, 0, 90", log);
  CHECK(LogHas(log, "whole number from 1 to 10"));
  Submit(st, "CREATESECTIONSHEETS Road, ALL, 2, low", log);
  CHECK(LogHas(log, "is not a number"));
  CHECK(st.paperLayouts.size() == layoutsBefore);
}

TEST_CASE("CREATESAMPLELINES over a partly covered range logs no data for the uncovered lines and completes",
          "[req519][samplelines]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  // A ground square covering |x| and |y| up to 180 ft: only the first four stations (0+00 to 1+50) reach it.
  CadSurface surface;
  surface.name = "Ground";
  surface.kind = SurfaceKind::Tin;
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {-180, -180, 100, 180, -180, 100, 180, 180, 100, -180, 180, 100};
  tin->indices = {0, 1, 2, 0, 2, 3};
  surface.tin = tin;
  st.cadSurfaces.push_back(surface);

  Submit(st, "CREATESAMPLELINES Road, 0+00, 5+00, 50, 50, 50, Ground", log);
  REQUIRE(st.sampleLines.size() == 11);
  size_t withGround = 0, noData = 0;
  for (const alignment::SampleLine& line : st.sampleLines) (line.section.empty() ? noData : withGround) += 1;
  CHECK(withGround == 4);
  CHECK(noData == 7);
  CHECK(LogHas(log, "4 with ground, 7 with no data"));
  CHECK(LogHas(log, "No data (no surface coverage) at 2+00"));
}

TEST_CASE("deleting an alignment with the Delete key removes its sample lines (REQ-519.13)", "[req519][samplelines]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  AddFlatGround(st);
  Submit(st, "CREATESAMPLELINES Road, 0+00, 1+00, 50, 50, 50, Ground", log);
  REQUIRE(st.sampleLines.size() == 3);

  SelectedEntity selected;
  selected.type = SelectedEntity::Type::Alignment;
  selected.index = 0;
  st.selection.clear();
  st.selection.push_back(selected);
  std::vector<std::string> deleteLog;
  ExecuteDeleteSelection(st, deleteLog);
  CHECK(st.alignments.empty());
  CHECK(st.sampleLines.empty());
}

TEST_CASE("CREATESECTIONSHEETS in a read-only project refuses and changes nothing", "[req519][sheets]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  AddFlatGround(st);
  Submit(st, "CREATESAMPLELINES Road, 0+00, 1+00, 50, 50, 50, Ground", log);
  REQUIRE(st.sampleLines.size() == 3);
  const size_t layoutsBefore = st.paperLayouts.size();

  st.drawingTabs.resize(2);
  AppCommandState::ProjectSession session;
  session.uid = 7;
  session.readOnly = true;
  st.openProjects.push_back(session);
  st.drawingTabs[1].projectUid = 7;

  Submit(st, "CREATESECTIONSHEETS Road, ALL, 2, 90", log);
  CHECK(LogHas(log, "read-only"));
  CHECK(st.paperLayouts.size() == layoutsBefore);
}
