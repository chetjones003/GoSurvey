// REQ-518 / GitHub issue #1000 - sheets from view frames: each sheet is a paper layout built on the Generic 24in x 36in
// title block (its geometry copied on), with the plan viewport inside the border's margin, filled fields, a north arrow,
// a locked viewport, the refusals, the frame selection, the naming, and one-step undo.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

// Three ARCH D frames at 1 in = 50 ft with 100 ft overlap: 0+00-18+00, 17+00-35+00 and 34+00-52+00.
void MakeThreeFrames(AppCommandState& st, std::vector<std::string>& log) {
  NorthRoad(st, log);
  Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 100", log);
  REQUIRE(st.viewFrameGroups.size() == 1);
  REQUIRE(st.viewFrameGroups[0].frames.size() == 3);
}

int FindLayout(const AppCommandState& st, const std::string& name) {
  for (size_t i = 0; i < st.paperLayouts.size(); ++i)
    if (st.paperLayouts[i].name == name) return static_cast<int>(i);
  return -1;
}

// The Generic title block is an AutoCAD template installed with Civil 3D. Tests that need it skip with a warning when
// it is not installed, so a machine without it still runs the rest.
bool TitleBlockAvailable() {
  const char* local = std::getenv("LOCALAPPDATA");
  if (local == nullptr) return false;
  return std::filesystem::exists(std::string(local) +
                                 "\\Autodesk\\C3D 2026\\enu\\Template\\AutoCAD Template\\Generic 24in x 36in Title Block.dwg");
}

// The text of a sheet's first paper text entry starting with the prefix, or "" when there is none.
std::string TextStartingWith(const PaperLayout& sheet, const std::string& prefix) {
  for (const CadAnnotation& a : sheet.paperTexts)
    if (a.text.rfind(prefix, 0) == 0) return a.text;
  return {};
}

}  // namespace

TEST_CASE("CREATESHEETS makes one ARCH D layout per frame, each built on the title block", "[req518][sheets]") {
  if (!TitleBlockAvailable()) {
    WARN("Generic 24in x 36in title block not installed; skipping");
    return;
  }
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  Submit(st, "CREATESHEETS Road", log);
  REQUIRE(LogHas(log, "3 sheet(s)"));
  REQUIRE(st.paperLayouts.size() == 3);

  for (const char* name : {"VF-1", "VF-2", "VF-3"}) {
    const int ix = FindLayout(st, name);
    REQUIRE(ix >= 0);
    const PaperLayout& sheet = st.paperLayouts[static_cast<size_t>(ix)];
    CHECK(sheet.landscape == true);
    CHECK(sheet.sheetWidthIn() == Approx(36.f));
    CHECK(sheet.sheetHeightIn() == Approx(24.f));
    CHECK(sheet.viewports.size() == 1);
    CHECK(sheet.paperLines.size() >= 6 * 14);  // the title block's 14 lines are copied onto every sheet
    CHECK(sheet.paperPolyOffsets.size() >= 7);  // and its 6 polylines
  }
}

TEST_CASE("the plan viewport sits inside the title block's border, half an inch in from it (REQ-518.10)",
          "[req518][sheets]") {
  if (!TitleBlockAvailable()) return;
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  Submit(st, "CREATESHEETS Road", log);
  const Viewport& vp = st.paperLayouts[static_cast<size_t>(FindLayout(st, "VF-2"))].viewports[0];
  // The Generic border is 1.5..32.25 x 1.25..21.75 in the block's own coordinates. It is centred on the 36 x 24 sheet
  // (+0.75, +0.5), and the viewport is inset 0.5 in on every side: 2.75..32.5 x 2.25..21.75.
  CHECK(vp.paperXIn == Approx(2.75).margin(0.01));
  CHECK(vp.paperYIn == Approx(2.25).margin(0.01));
  CHECK(vp.paperWIn == Approx(29.75).margin(0.01));
  CHECK(vp.paperHIn == Approx(19.5).margin(0.01));
  CHECK(vp.locked == true);  // REQ-518.11
}

TEST_CASE("each sheet is filled from its view frame and carries a north arrow (REQ-518.9)", "[req518][sheets]") {
  if (!TitleBlockAvailable()) return;
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  Submit(st, "CREATESHEETS Road, VF-2", log);
  const PaperLayout& sheet = st.paperLayouts[static_cast<size_t>(FindLayout(st, "VF-2"))];
  CHECK(TextStartingWith(sheet, "ALIGNMENT  Road") == "ALIGNMENT  Road");
  CHECK(TextStartingWith(sheet, "SHEET  VF-2") == "SHEET  VF-2");
  CHECK(TextStartingWith(sheet, "SCALE  ") == "SCALE  1\" = 50'");
  CHECK(TextStartingWith(sheet, "SIZE  ").find("ARCH D") != std::string::npos);
  // VF-2 runs 17+00 to 35+00 (1800 ft at 50 ft per inch).
  const std::string sta = TextStartingWith(sheet, "STA  ");
  CHECK(sta.find("17+00") != std::string::npos);
  CHECK(sta.find("to 35+00") != std::string::npos);
  CHECK(TextStartingWith(sheet, "DATE  ").size() == std::string("DATE  11/08/2026").size());
  CHECK(TextStartingWith(sheet, "N") == "N");  // the north arrow's N
}

TEST_CASE("the title block's layers come into the drawing with the sheets", "[req518][sheets]") {
  if (!TitleBlockAvailable()) return;
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  Submit(st, "CREATESHEETS Road", log);
  const bool hasTitleBlockLayer = std::any_of(st.drawingLayerTable.begin(), st.drawingLayerTable.end(),
                                              [](const CadLayerRow& r) { return r.name == "Title Block"; });
  CHECK(hasTitleBlockLayer);
}

TEST_CASE("the plan's along-alignment edge runs left to right across the viewport, to any bearing", "[req518][sheets]") {
  if (!TitleBlockAvailable()) return;
  for (const char* bearing : {"0", "90", "45", "210"}) {  // north, due east, north-east, south-south-west
    AppCommandState st = Drawing();
    std::vector<std::string> log;
    NorthRoad(st, log);
    if (std::string(bearing) != "0") Submit(st, std::string("ALIGNMENTEDIT Road, 1, BEARING, ") + bearing, log);
    Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 100", log);
    REQUIRE(st.viewFrameGroups.size() == 1);
    // The edit must have taken effect: the tangent at station 0 is (sin b, cos b) for a compass bearing b.
    const double rad = std::stod(bearing) * 3.14159265358979323846 / 180.0;
    const alignment::Point2 t = st.alignments[0].GetTangentAtStation(st.alignments[0].StartStation());
    CHECK(t.x == Approx(std::sin(rad)).margin(1e-6));
    CHECK(t.y == Approx(std::cos(rad)).margin(1e-6));

    Submit(st, "CREATESHEETS Road, VF-2", log);
    const PaperLayout& sheet = st.paperLayouts[static_cast<size_t>(FindLayout(st, "VF-2"))];
    const Viewport& vp = sheet.viewports[0];
    const alignment::ViewFrame& frame = st.viewFrameGroups[0].frames[1];
    // The frame's centre lands on the viewport's centre, and its along-alignment edge is horizontal and 36 in long.
    float cx = 0.f, cy = 0.f;
    ModelToPaperInThroughCamera(vp, frame.centre.x, frame.centre.y, 0.0, &cx, &cy);
    CHECK(cx == Approx(vp.paperXIn + vp.paperWIn / 2).margin(1e-3));
    CHECK(cy == Approx(vp.paperYIn + vp.paperHIn / 2).margin(1e-3));
    float px[2], py[2];
    for (int k = 0; k < 2; ++k) ModelToPaperInThroughCamera(vp, frame.corner[k].x, frame.corner[k].y, 0.0, &px[k], &py[k]);
    CHECK(py[1] == Approx(py[0]).margin(1e-3));
    CHECK(std::fabs(px[1] - px[0]) == Approx(36.0).margin(1e-3));
    CHECK(px[1] > px[0]);  // increasing stationing runs to the right
  }
}

TEST_CASE("CREATESHEETS takes the named frames, and a frame name that is not in the group is refused",
          "[req518][sheets]") {
  if (!TitleBlockAvailable()) return;
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);

  Submit(st, "CREATESHEETS Road, VF-2", log);
  REQUIRE(st.paperLayouts.size() == 1);
  CHECK(FindLayout(st, "VF-2") >= 0);
  CHECK(FindLayout(st, "VF-1") < 0);

  Submit(st, "CREATESHEETS Road, VF-1 VF-3", log);
  CHECK(FindLayout(st, "VF-1") >= 0);
  CHECK(FindLayout(st, "VF-3") >= 0);

  const size_t count = st.paperLayouts.size();
  Submit(st, "CREATESHEETS Road, VF-9", log);
  CHECK(LogHas(log, "no frame named \"VF-9\""));
  CHECK(st.paperLayouts.size() == count);
}

TEST_CASE("a sheet whose name is taken gets a numbered suffix", "[req518][sheets]") {
  if (!TitleBlockAvailable()) return;
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  Submit(st, "CREATESHEETS Road, VF-1", log);
  REQUIRE(FindLayout(st, "VF-1") >= 0);
  Submit(st, "CREATESHEETS Road, VF-1", log);
  CHECK(FindLayout(st, "VF-1 (2)") >= 0);
  CHECK(FindLayout(st, "VF-1 (3)") < 0);
}

TEST_CASE("one undo step removes every sheet that one command made", "[req518][sheets]") {
  if (!TitleBlockAvailable()) return;
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  const size_t before = st.paperLayouts.size();
  Submit(st, "CREATESHEETS Road", log);
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
    Submit(st, "CREATESHEETS Road", log);
    CHECK(LogHas(log, "no alignment named"));
    CHECK(st.paperLayouts.empty());
  }
  {  // alignment with no view frame group
    AppCommandState st = Drawing();
    NorthRoad(st, log);
    Submit(st, "CREATESHEETS Road", log);
    CHECK(LogHas(log, "has no view frame group"));
    CHECK(st.paperLayouts.empty());
  }
  {  // two view frame groups on one alignment: the sheets cannot tell which to use
    AppCommandState st = Drawing();
    MakeThreeFrames(st, log);
    Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 0", log);
    REQUIRE(st.viewFrameGroups.size() == 2);
    Submit(st, "CREATESHEETS Road", log);
    CHECK(LogHas(log, "2 view frame groups"));
    CHECK(st.paperLayouts.empty());
  }
  {  // frames on another sheet size do not match the title block
    AppCommandState st = Drawing();
    NorthRoad(st, log);
    Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ANSI B, 1\" = 50', 100", log);
    Submit(st, "CREATESHEETS Road", log);
    CHECK(LogHas(log, "Create the view frames on ARCH D"));
    CHECK(st.paperLayouts.empty());
  }
  {  // a title block that is not there
    AppCommandState st = Drawing();
    MakeThreeFrames(st, log);
    Submit(st, "CREATESHEETS Road, ALL, C:\\no such folder\\missing.dwg", log);
    CHECK(LogHas(log, "title block not found"));
    CHECK(st.paperLayouts.empty());
  }
  {  // read-only project: refused before any change
    AppCommandState st = Drawing();
    MakeThreeFrames(st, log);
    st.drawingTabs.resize(2);
    AppCommandState::ProjectSession session;
    session.uid = 7;
    session.readOnly = true;
    st.openProjects.push_back(session);
    st.drawingTabs[1].projectUid = 7;
    Submit(st, "CREATESHEETS Road", log);
    CHECK(LogHas(log, "read-only"));
    CHECK(st.paperLayouts.empty());
  }
}

// The sheet's plotted geometry, read back from the PDF. VF-1's viewport shows stations 156.25 to 1643.75 ft (its frame
// centre is 900 ft, and the viewport is 29.75 in = 1487.5 ft wide at 50 ft per inch), with its left edge at 2.75 in. So
// station 300 ft is 2.75 + (300 - 156.25) / 50 = 5.625 in = 405 pt, and 600 ft is 837 pt. The alignment runs across the
// viewport's centre line, at 12 in = 864 pt. PDF points are 72 per inch.
TEST_CASE("a sheet plots its model geometry turned with its frame, read back from the PDF", "[req518][sheets][plot]") {
  if (!TitleBlockAvailable()) return;
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  Submit(st, "CREATESHEETS Road, VF-1", log);
  const int sheetIx = FindLayout(st, "VF-1");
  REQUIRE(sheetIx >= 0);

  // One model line, north from station 300 ft to 600 ft: flat (x0,y0,z0,x1,y1,z1).
  st.userLinesFlat = {0, 300, 0, 0, 600, 0};
  st.userLineAttrs.resize(1);

  const std::filesystem::path path = std::filesystem::temp_directory_path() / "gosurvey-viewframe-sheet.pdf";
  std::vector<std::string> plotLog;
  FPDF_InitLibrary();
  REQUIRE(PlotLayoutsToPdf(st, {sheetIx}, path.string().c_str(), plotLog));
  FPDF_DOCUMENT doc = FPDF_LoadDocument(path.string().c_str(), nullptr);
  REQUIRE(doc != nullptr);
  FPDF_PAGE page = FPDF_LoadPage(doc, 0);
  REQUIRE(page != nullptr);
  auto atPoint = [](float x, float y, float wantX, float wantY) {  // not `near`: windows.h defines that as a macro
    return std::fabs(x - wantX) < 3.f && std::fabs(y - wantY) < 3.f;
  };
  bool found = false;
  for (int i = 0; i < FPDFPage_CountObjects(page) && !found; ++i) {
    FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
    if (FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_PATH) continue;
    std::vector<std::pair<float, float>> points;
    for (int s = 0; s < FPDFPath_CountSegments(obj); ++s) {
      FPDF_PATHSEGMENT segment = FPDFPath_GetPathSegment(obj, s);
      float x = 0.f, y = 0.f;
      if (segment && FPDFPathSegment_GetPoint(segment, &x, &y)) points.emplace_back(x, y);
    }
    for (const auto& a : points)
      for (const auto& b : points)
        if (atPoint(a.first, a.second, 405.f, 864.f) && atPoint(b.first, b.second, 837.f, 864.f)) found = true;
  }
  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);
  std::error_code ec;
  std::filesystem::remove(path, ec);
  CHECK(found);
}

// Review artifact, not a check: plots sheet VF-1 with GoSurvey's own PDF plotter and rasterises the page with PDFium
// to build/artifacts/issue1000/sheet-VF-1.bmp, for a person to look at. Run on demand with the [artifact] tag.
TEST_CASE("writes the VF-1 sheet as an image for review", "[artifact][req518]") {
  if (!TitleBlockAvailable()) return;
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeThreeFrames(st, log);
  Submit(st, "CREATESHEETS Road, VF-1", log);
  const int sheetIx = FindLayout(st, "VF-1");
  REQUIRE(sheetIx >= 0);

  std::filesystem::create_directories("build/artifacts/issue1000");
  const std::filesystem::path pdfPath = "build/artifacts/issue1000/sheet-VF-1.pdf";
  std::vector<std::string> plotLog;
  FPDF_InitLibrary();
  REQUIRE(PlotLayoutsToPdf(st, {sheetIx}, pdfPath.string().c_str(), plotLog));
  FPDF_DOCUMENT doc = FPDF_LoadDocument(pdfPath.string().c_str(), nullptr);
  REQUIRE(doc != nullptr);
  FPDF_PAGE page = FPDF_LoadPage(doc, 0);
  REQUIRE(page != nullptr);

  const int w = 2592, h = 1728;  // 36 in x 24 in at 72 dpi
  FPDF_BITMAP bitmap = FPDFBitmap_Create(w, h, 0);
  REQUIRE(bitmap != nullptr);
  FPDFBitmap_FillRect(bitmap, 0, 0, w, h, 0xFFFFFFFF);
  FPDF_RenderPageBitmap(bitmap, page, 0, 0, w, h, 0, FPDF_ANNOT);
  const int stride = FPDFBitmap_GetStride(bitmap);
  const auto* pixels = static_cast<const unsigned char*>(FPDFBitmap_GetBuffer(bitmap));
  REQUIRE(pixels != nullptr);

  // 32-bit BMP, bottom-up rows, BGRA. PDFium writes BGRA too, so the rows are copied as they are.
  const std::uint32_t imageBytes = static_cast<std::uint32_t>(w * h * 4);
  std::vector<unsigned char> file(54 + imageBytes, 0);
  auto put32 = [&file](std::size_t at, std::uint32_t v) {
    for (int b = 0; b < 4; ++b) file[at + b] = static_cast<unsigned char>(v >> (8 * b));
  };
  auto put16 = [&file](std::size_t at, std::uint16_t v) {
    file[at] = static_cast<unsigned char>(v);
    file[at + 1] = static_cast<unsigned char>(v >> 8);
  };
  file[0] = 'B';
  file[1] = 'M';
  put32(2, static_cast<std::uint32_t>(file.size()));
  put32(10, 54);
  put32(14, 40);
  put32(18, static_cast<std::uint32_t>(w));
  put32(22, static_cast<std::uint32_t>(h));
  put16(26, 1);
  put16(28, 32);
  put32(34, imageBytes);
  for (int row = 0; row < h; ++row) {
    const unsigned char* src = pixels + static_cast<std::ptrdiff_t>(h - 1 - row) * stride;
    std::copy(src, src + w * 4, file.begin() + 54 + static_cast<std::ptrdiff_t>(row) * w * 4);
  }
  std::ofstream out("build/artifacts/issue1000/sheet-VF-1.bmp", std::ios::binary);
  out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  out.close();

  FPDFBitmap_Destroy(bitmap);
  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);
  CHECK(std::filesystem::exists("build/artifacts/issue1000/sheet-VF-1.bmp"));
}
