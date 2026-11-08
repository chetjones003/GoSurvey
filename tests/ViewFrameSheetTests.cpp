// REQ-518 / GitHub issue #1000 - paper sheets from view frames: one layout per frame, the rotated viewport that covers
// each frame, the refusals, frame selection, naming, read-only and one-step undo.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "PaperSpace.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentViewFrames.hpp"
#include "io/PdfPlot.hpp"
#include "render/ViewportProjection.hpp"

// PDFium last, as the other PDF readback tests do: its headers must not precede the project's own.
#include <fpdf_edit.h>
#include <fpdfview.h>

using Catch::Approx;

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

bool LogHas(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& line : log) {
    if (line.find(needle) != std::string::npos) return true;
  }
  return false;
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  return st;
}

// A straight alignment 0+00 to 50+00 running due north, in a feet drawing.
void NorthRoad(AppCommandState& st, std::vector<std::string>& log) {
  Submit(st, "ALIGNMENT Road", log);
  Submit(st, "ALIGNMENTADD Road, TANGENT, 0, 0, 0, 5000", log);
}

// A landscape ARCH D (36 in x 24 in) template layout with one paper line and one viewport of its own, which the
// sheets must ignore (REQ-518.2). Returns its index.
int AddArchDTemplate(AppCommandState& st, const std::string& name) {
  PaperLayout tmpl;
  tmpl.name = name;
  tmpl.portraitWidthIn = 24.f;   // ARCH D portrait: 24 x 36
  tmpl.portraitHeightIn = 36.f;
  tmpl.landscape = true;         // so the sheet is 36 x 24 across the alignment
  tmpl.paperLines = {0.f, 0.f, 0.f, 2.f, 2.f, 0.f};
  tmpl.paperLineAttrs.resize(1);
  tmpl.viewports.push_back(Viewport{});
  st.paperLayouts.push_back(tmpl);
  return static_cast<int>(st.paperLayouts.size()) - 1;
}

int FindLayout(const AppCommandState& st, const std::string& name) {
  for (size_t i = 0; i < st.paperLayouts.size(); ++i)
    if (st.paperLayouts[i].name == name) return static_cast<int>(i);
  return -1;
}

// The three frames of the north road at 1 in = 50 ft on ARCH D, 100 ft overlap: 0+00-18+00, 17+00-35+00, 34+00-52+00.
void MakeThreeFrames(AppCommandState& st, std::vector<std::string>& log) {
  NorthRoad(st, log);
  Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 100", log);
  REQUIRE(st.viewFrameGroups.size() == 1);
  REQUIRE(st.viewFrameGroups[0].frames.size() == 3);
}

}  // namespace

TEST_CASE("CREATESHEETS makes one layout per frame, each with one viewport, and leaves the template alone",
          "[req518][sheets]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  const int tmplIx = AddArchDTemplate(st, "Title");
  const size_t before = st.paperLayouts.size();

  Submit(st, "CREATESHEETS Road, Title", log);
  REQUIRE(LogHas(log, "3 sheet(s)"));
  REQUIRE(st.paperLayouts.size() == before + 3);

  for (const char* name : {"VF-1", "VF-2", "VF-3"}) {
    const int ix = FindLayout(st, name);
    REQUIRE(ix >= 0);
    const PaperLayout& sheet = st.paperLayouts[static_cast<size_t>(ix)];
    CHECK(sheet.viewports.size() == 1);
    CHECK(sheet.paperLines.size() == 6);  // the template's paper content is copied
    CHECK(sheet.landscape == true);
  }

  // The template is unchanged: its own viewport is still there, and it has no sheet's viewport added.
  const PaperLayout& tmpl = st.paperLayouts[static_cast<size_t>(tmplIx)];
  CHECK(tmpl.name == "Title");
  CHECK(tmpl.viewports.size() == 1);
  CHECK(tmpl.paperLines.size() == 6);
}

TEST_CASE("each sheet viewport is centred on its frame, at the frame's scale, and rotated to the alignment",
          "[req518][sheets]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  AddArchDTemplate(st, "Title");
  Submit(st, "CREATESHEETS Road, Title", log);

  const alignment::ViewFrameGroup& group = st.viewFrameGroups[0];
  for (size_t i = 0; i < group.frames.size(); ++i) {
    const alignment::ViewFrame& frame = group.frames[i];
    const PaperLayout& sheet = st.paperLayouts[static_cast<size_t>(FindLayout(st, frame.name))];
    const Viewport& vp = sheet.viewports[0];
    // Centred on the frame RECTANGLE (for CENTRED placement the alignment point is the same point).
    CHECK(vp.modelCenterX == Approx((frame.corner[0].x + frame.corner[2].x) / 2).margin(1e-9));
    CHECK(vp.modelCenterY == Approx((frame.corner[0].y + frame.corner[2].y) / 2).margin(1e-9));
    CHECK(vp.scaleModelPerPaperIn == Approx(50.0).margin(1e-4));  // 1 in = 50 ft
    CHECK(vp.annotationScaleName == "1\" = 50'");
    // A north-running alignment is turned a quarter turn, so it lies across the sheet horizontally.
    CHECK(std::fabs(std::fabs(vp.camRollDeg) - 90.f) < 1e-3f);
  }
}

TEST_CASE("a sheet's viewport covers its frame exactly and the alignment runs left to right across it",
          "[req518][sheets]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  AddArchDTemplate(st, "Title");
  Submit(st, "CREATESHEETS Road, Title", log);

  const alignment::ViewFrameGroup& group = st.viewFrameGroups[0];
  for (const alignment::ViewFrame& frame : group.frames) {
    const PaperLayout& sheet = st.paperLayouts[static_cast<size_t>(FindLayout(st, frame.name))];
    const Viewport& vp = sheet.viewports[0];
    const float w = sheet.sheetWidthIn();   // 36 in across the alignment
    const float h = sheet.sheetHeightIn();  // 24 in across it

    // Map the frame's four corners onto the sheet through the viewport's own camera (what the overlay and PDF use).
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    float px[4], py[4];
    for (int k = 0; k < 4; ++k) {
      ModelToPaperInThroughCamera(vp, frame.corner[k].x, frame.corner[k].y, 0.0, &px[k], &py[k]);
      minX = std::min(minX, static_cast<double>(px[k]));
      maxX = std::max(maxX, static_cast<double>(px[k]));
      minY = std::min(minY, static_cast<double>(py[k]));
      maxY = std::max(maxY, static_cast<double>(py[k]));
    }
    // The frame fills the sheet: its corners are the sheet's corners (REQ-518.2 coverage).
    CHECK(minX == Approx(0.0).margin(1e-3));
    CHECK(maxX == Approx(static_cast<double>(w)).margin(1e-3));
    CHECK(minY == Approx(0.0).margin(1e-3));
    CHECK(maxY == Approx(static_cast<double>(h)).margin(1e-3));

    // The near edge runs along the alignment (corner 0 to corner 1): horizontal on the sheet, 36 in long.
    CHECK(py[1] == Approx(py[0]).margin(1e-3));
    CHECK(std::fabs(px[1] - px[0]) == Approx(static_cast<double>(w)).margin(1e-3));
    // Increasing stationing runs to the right: corner 1 is to the right of corner 0.
    CHECK(px[1] > px[0]);
  }
}

TEST_CASE("CREATESHEETS takes the named frames, and a frame name that is not in the group is refused",
          "[req518][sheets]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  AddArchDTemplate(st, "Title");

  Submit(st, "CREATESHEETS Road, Title, VF-2", log);
  REQUIRE(st.paperLayouts.size() == 2);  // template + VF-2
  CHECK(FindLayout(st, "VF-2") >= 0);
  CHECK(FindLayout(st, "VF-1") < 0);

  Submit(st, "CREATESHEETS Road, Title, VF-1 VF-3", log);
  CHECK(FindLayout(st, "VF-1") >= 0);
  CHECK(FindLayout(st, "VF-3") >= 0);

  const size_t count = st.paperLayouts.size();
  Submit(st, "CREATESHEETS Road, Title, VF-9", log);
  CHECK(LogHas(log, "no frame named \"VF-9\""));
  CHECK(st.paperLayouts.size() == count);
}

TEST_CASE("a sheet whose name is taken gets a numbered suffix", "[req518][sheets]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  AddArchDTemplate(st, "Title");
  Submit(st, "CREATESHEETS Road, Title, VF-1", log);
  REQUIRE(FindLayout(st, "VF-1") >= 0);

  Submit(st, "CREATESHEETS Road, Title, VF-1", log);
  CHECK(FindLayout(st, "VF-1 (2)") >= 0);
  CHECK(FindLayout(st, "VF-1 (3)") < 0);
}

TEST_CASE("one undo step removes every sheet that one command made", "[req518][sheets]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  AddArchDTemplate(st, "Title");
  const size_t before = st.paperLayouts.size();

  Submit(st, "CREATESHEETS Road, Title", log);
  REQUIRE(st.paperLayouts.size() == before + 3);
  Submit(st, "UNDO", log);
  CHECK(st.paperLayouts.size() == before);
  CHECK(FindLayout(st, "VF-1") < 0);
  CHECK(FindLayout(st, "VF-3") < 0);
}

TEST_CASE("CREATESHEETS refuses and creates nothing when it cannot run", "[req518][sheets]") {
  std::vector<std::string> log;

  {  // no alignment
    AppCommandState st = Drawing();
    AddArchDTemplate(st, "Title");
    const size_t before = st.paperLayouts.size();
    Submit(st, "CREATESHEETS Road, Title", log);
    CHECK(LogHas(log, "no alignment named"));
    CHECK(st.paperLayouts.size() == before);
  }
  {  // alignment with no view frame group
    AppCommandState st = Drawing();
    NorthRoad(st, log);
    AddArchDTemplate(st, "Title");
    const size_t before = st.paperLayouts.size();
    Submit(st, "CREATESHEETS Road, Title", log);
    CHECK(LogHas(log, "has no view frame group"));
    CHECK(st.paperLayouts.size() == before);
  }
  {  // two view frame groups on one alignment: the sheets cannot tell which to use
    AppCommandState st = Drawing();
    MakeThreeFrames(st, log);
    Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 0", log);
    REQUIRE(st.viewFrameGroups.size() == 2);
    AddArchDTemplate(st, "Title");
    const size_t before = st.paperLayouts.size();
    Submit(st, "CREATESHEETS Road, Title", log);
    CHECK(LogHas(log, "2 view frame groups"));
    CHECK(st.paperLayouts.size() == before);
  }
  {  // missing template
    AppCommandState st = Drawing();
    MakeThreeFrames(st, log);
    const size_t before = st.paperLayouts.size();
    Submit(st, "CREATESHEETS Road, Nowhere", log);
    CHECK(LogHas(log, "no layout named"));
    CHECK(st.paperLayouts.size() == before);
  }
  {  // model space is not a paper layout
    AppCommandState st = Drawing();
    MakeThreeFrames(st, log);
    Submit(st, "CREATESHEETS Road, Model", log);
    CHECK(LogHas(log, "model space"));
    CHECK(st.paperLayouts.empty());
  }
  {  // a template of another sheet size does not cover the frame
    AppCommandState st = Drawing();
    MakeThreeFrames(st, log);
    PaperLayout tmpl;
    tmpl.name = "Small";
    tmpl.portraitWidthIn = 18.f;  // ARCH C landscape: 24 x 18
    tmpl.portraitHeightIn = 24.f;
    tmpl.landscape = true;
    st.paperLayouts.push_back(tmpl);
    Submit(st, "CREATESHEETS Road, Small", log);
    CHECK(LogHas(log, "landscape sheet of that size"));
    CHECK(st.paperLayouts.size() == 1);
  }
  {  // the same ARCH D sheet turned portrait: its width is 24 in, not the frame group's 36 in, so the size check refuses
    AppCommandState st = Drawing();
    MakeThreeFrames(st, log);
    PaperLayout tmpl;
    tmpl.name = "Tall";
    tmpl.portraitWidthIn = 24.f;
    tmpl.portraitHeightIn = 36.f;
    tmpl.landscape = false;
    st.paperLayouts.push_back(tmpl);
    Submit(st, "CREATESHEETS Road, Tall", log);
    CHECK(LogHas(log, "landscape sheet of that size"));  // the size refusal, not some other one
    CHECK(st.paperLayouts.size() == 1);
  }
  {  // read-only project: refused before any change
    AppCommandState st = Drawing();
    st.documents.resize(2);
    MakeThreeFrames(st, log);
    AddArchDTemplate(st, "Title");
    st.drawingTabs.resize(2);
    AppCommandState::ProjectSession session;
    session.uid = 7;
    session.readOnly = true;
    st.openProjects.push_back(session);
    st.drawingTabs[1].projectUid = 7;
    const size_t before = st.paperLayouts.size();
    Submit(st, "CREATESHEETS Road, Title", log);
    CHECK(LogHas(log, "read-only"));
    CHECK(st.paperLayouts.size() == before);
  }
}

// The plotted PDF, not just the viewport maths: a model line along the alignment (stations 100 to 700 ft, inside VF-1)
// must come out of PlotLayoutsToPdf as a horizontal path on the sheet, starting 2 in from the sheet's left edge (100 ft
// at 50 ft per inch) and 12 in long, across the middle of the 24 in sheet. PDF points are 72 per inch.
TEST_CASE("a sheet plots its model geometry turned with its frame, read back from the PDF", "[req518][sheets][plot]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 100", log);
  REQUIRE(st.viewFrameGroups.size() == 1);
  AddArchDTemplate(st, "Title");
  Submit(st, "CREATESHEETS Road, Title, VF-1", log);
  const int sheetIx = FindLayout(st, "VF-1");
  REQUIRE(sheetIx >= 0);

  // One model line, north from station 100 ft to 700 ft: flat (x0,y0,z0,x1,y1,z1).
  st.userLinesFlat = {0, 100, 0, 0, 700, 0};
  st.userLineAttrs.resize(1);

  const std::filesystem::path path = std::filesystem::temp_directory_path() / "gosurvey-viewframe-sheet.pdf";
  std::vector<std::string> plotLog;
  FPDF_InitLibrary();
  REQUIRE(PlotLayoutsToPdf(st, {sheetIx}, path.string().c_str(), plotLog));
  FPDF_DOCUMENT doc = FPDF_LoadDocument(path.string().c_str(), nullptr);
  REQUIRE(doc != nullptr);
  FPDF_PAGE page = FPDF_LoadPage(doc, 0);
  REQUIRE(page != nullptr);
  // The plotter may merge several entities into one path object, so match the model line's end points rather than an
  // object's bounds: some pair of points must be the line from (144 pt, 864 pt) to (1008 pt, 864 pt), in either order.
  auto atPoint = [](float x, float y, float wantX, float wantY) {  // not `near`: windows.h defines that as a macro
    return std::fabs(x - wantX) < 3.f && std::fabs(y - wantY) < 3.f;
  };
  bool found = false;
  std::string dump;  // every path's points, shown only if the model line is not among them
  for (int i = 0; i < FPDFPage_CountObjects(page) && !found; ++i) {
    FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
    if (FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_PATH) continue;
    std::vector<std::pair<float, float>> points;
    for (int s = 0; s < FPDFPath_CountSegments(obj); ++s) {
      FPDF_PATHSEGMENT segment = FPDFPath_GetPathSegment(obj, s);
      float x = 0.f, y = 0.f;
      if (segment && FPDFPathSegment_GetPoint(segment, &x, &y)) points.emplace_back(x, y);
    }
    for (const auto& [x, y] : points) dump += "(" + std::to_string(x) + "," + std::to_string(y) + ")";
    dump += " | ";
    for (const auto& a : points)
      for (const auto& b : points)
        if (atPoint(a.first, a.second, 144.f, 864.f) && atPoint(b.first, b.second, 1008.f, 864.f)) found = true;
  }
  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);
  std::error_code ec;
  std::filesystem::remove(path, ec);
  INFO("path points on the sheet, in points: " << dump);
  // 144 pt is station 100 ft (2 in from the frame's start at 50 ft per inch); 1008 pt is station 700 ft; 864 pt is the
  // middle of the 24 in sheet, where CENTRED placement puts the alignment.
  REQUIRE(found);
}

namespace {

// The frame, mapped through the sheet's viewport (world coordinates: the frame's local corners plus the document origin),
// fills the sheet exactly, with its along-alignment edge horizontal and running left to right.
void CheckCoversSheet(const PaperLayout& sheet, const alignment::ViewFrame& frame, double originX, double originY) {
  const Viewport& vp = sheet.viewports[0];
  const double w = sheet.sheetWidthIn();
  const double h = sheet.sheetHeightIn();
  double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
  float px[4], py[4];
  for (int k = 0; k < 4; ++k) {
    ModelToPaperInThroughCamera(vp, frame.corner[k].x + originX, frame.corner[k].y + originY, 0.0, &px[k], &py[k]);
    minX = std::min(minX, static_cast<double>(px[k]));
    maxX = std::max(maxX, static_cast<double>(px[k]));
    minY = std::min(minY, static_cast<double>(py[k]));
    maxY = std::max(maxY, static_cast<double>(py[k]));
  }
  CHECK(minX == Approx(0.0).margin(1e-3));
  CHECK(maxX == Approx(w).margin(1e-3));
  CHECK(minY == Approx(0.0).margin(1e-3));
  CHECK(maxY == Approx(h).margin(1e-3));
  CHECK(py[1] == Approx(py[0]).margin(1e-3));
  CHECK(std::fabs(px[1] - px[0]) == Approx(w).margin(1e-3));
  CHECK(px[1] > px[0]);
}

}  // namespace

TEST_CASE("sheet viewports are placed in world coordinates when the drawing has a document origin",
          "[req518][sheets]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  AddArchDTemplate(st, "Title");
  // Alignment geometry is stored local; the viewport's model centre is world (local + origin), as the Qt views read it.
  const double originX = 500000.0, originY = 2000000.0;
  st.worldDocumentOriginX = originX;
  st.worldDocumentOriginY = originY;
  Submit(st, "CREATESHEETS Road, Title", log);

  for (const alignment::ViewFrame& frame : st.viewFrameGroups[0].frames) {
    const PaperLayout& sheet = st.paperLayouts[static_cast<size_t>(FindLayout(st, frame.name))];
    const Viewport& vp = sheet.viewports[0];
    CHECK(vp.modelCenterX == Approx((frame.corner[0].x + frame.corner[2].x) / 2 + originX).margin(1e-6));
    CHECK(vp.modelCenterY == Approx((frame.corner[0].y + frame.corner[2].y) / 2 + originY).margin(1e-6));
    CheckCoversSheet(sheet, frame, originX, originY);
  }
}

TEST_CASE("LEFT and RIGHT placed frames are covered exactly by their sheets", "[req518][sheets]") {
  for (const char* placement : {"LEFT", "RIGHT"}) {
    AppCommandState st = Drawing();
    std::vector<std::string> log;
    NorthRoad(st, log);
    Submit(st, std::string("CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 100, ") + placement, log);
    REQUIRE(st.viewFrameGroups.size() == 1);
    AddArchDTemplate(st, "Title");
    Submit(st, "CREATESHEETS Road, Title", log);
    REQUIRE(st.viewFrameGroups[0].frames.size() == 3);
    for (const alignment::ViewFrame& frame : st.viewFrameGroups[0].frames) {
      const PaperLayout& sheet = st.paperLayouts[static_cast<size_t>(FindLayout(st, frame.name))];
      CheckCoversSheet(sheet, frame, 0.0, 0.0);
    }
  }
}

TEST_CASE("sheets cover their frames on alignments at other bearings, not only due north", "[req518][sheets]") {
  for (const char* bearing : {"90", "45", "210"}) {  // due east, north-east, and south-south-west
    AppCommandState st = Drawing();
    std::vector<std::string> log;
    NorthRoad(st, log);
    Submit(st, std::string("ALIGNMENTEDIT Road, 1, BEARING, ") + bearing, log);
    // Prove the edit took effect: a refused edit would leave the road due north and the cases would test nothing new.
    // A compass bearing b (0 = north, 90 = east) is the tangent (sin b, cos b).
    const double rad = std::stod(bearing) * 3.14159265358979323846 / 180.0;
    const alignment::Point2 t = st.alignments[0].GetTangentAtStation(st.alignments[0].StartStation());
    CHECK(t.x == Approx(std::sin(rad)).margin(1e-6));
    CHECK(t.y == Approx(std::cos(rad)).margin(1e-6));
    Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 100", log);
    REQUIRE(st.viewFrameGroups.size() == 1);
    REQUIRE(st.viewFrameGroups[0].frames.size() == 3);
    AddArchDTemplate(st, "Title");
    Submit(st, "CREATESHEETS Road, Title", log);
    for (const alignment::ViewFrame& frame : st.viewFrameGroups[0].frames) {
      const PaperLayout& sheet = st.paperLayouts[static_cast<size_t>(FindLayout(st, frame.name))];
      CheckCoversSheet(sheet, frame, 0.0, 0.0);
    }
  }
}
