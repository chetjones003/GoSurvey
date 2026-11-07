// REQ-445: the PDF plot honours the Page Setup options, checked on the file it writes.
//
// Each case plots a real layout with one 2-inch line on it, then reads the drawn path back from the PDF with PDFium.
// The placement rules themselves are in PlotSheetTests.cpp.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "commands/CadCommands.hpp"
#include "commands/PaperSpace.hpp"
#include "io/PdfPlot.hpp"
#include "io/PlotSheetTransform.hpp"
#include "io/PlotStyleTable.hpp"

#include <fpdf_edit.h>
#include <fpdfview.h>
#include <miniz.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#include <fstream>

using Catch::Matchers::WithinAbs;

namespace {

constexpr float kSheetW = 11.f;  // a landscape 8.5 x 11 sheet is 11 x 8.5 inches
constexpr float kSheetH = 8.5f;
constexpr float kPt = 72.f;

struct Bounds {
  float left = 0.f, bottom = 0.f, right = 0.f, top = 0.f;  // points
};

// Plots one 2-inch line from (1,1) to (3,1) on a landscape sheet with the given Page Setup plot options and returns
// the bounds of the path PDFium reads back from the written file.
Bounds PlotLineBounds(const plotsheet::Settings& settings, const std::string& tag) {
  AppCommandState st;
  PaperLayout L;
  L.name = "Sheet";
  L.landscape = true;
  L.portraitWidthIn = kSheetH;  // landscape: sheet width = portraitHeightIn = 11, height = portraitWidthIn = 8.5
  L.portraitHeightIn = kSheetW;
  L.fitToPaper = settings.fitToPaper;
  L.scaleModelPerPaperIn = settings.plotRatio;
  L.offsetXIn = settings.offsetXIn;
  L.offsetYIn = settings.offsetYIn;
  L.centerPlot = settings.centerPlot;
  L.paperLines = {1.f, 1.f, 0.f, 3.f, 1.f, 0.f};
  L.paperLineAttrs.resize(1);
  st.paperLayouts.push_back(L);

  const std::filesystem::path path = std::filesystem::temp_directory_path() / ("gosurvey-plotsheet-" + tag + ".pdf");
  std::vector<std::string> log;
  // PDFium must be initialised before the plot creates its document (the app does this at start-up).
  FPDF_InitLibrary();
  REQUIRE(PlotLayoutsToPdf(st, {0}, path.string().c_str(), log));

  FPDF_DOCUMENT doc = FPDF_LoadDocument(path.string().c_str(), nullptr);
  REQUIRE(doc != nullptr);
  FPDF_PAGE page = FPDF_LoadPage(doc, 0);
  REQUIRE(page != nullptr);

  Bounds out;
  bool found = false;
  const int count = FPDFPage_CountObjects(page);
  for (int i = 0; i < count && !found; ++i) {
    FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
    if (FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_PATH)
      continue;
    found = FPDFPageObj_GetBounds(obj, &out.left, &out.bottom, &out.right, &out.top) != 0;
  }
  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);
  std::error_code ec;
  std::filesystem::remove(path, ec);
  REQUIRE(found);
  return out;
}

int PlotSurfaceSegmentCount(bool reducedLod, const std::string& tag) {
  AppCommandState st;
  PaperLayout layout;
  layout.name = "Sheet";
  layout.landscape = true;
  layout.portraitWidthIn = kSheetH;
  layout.portraitHeightIn = kSheetW;
  Viewport viewport;
  viewport.modelCenterX = 50.0;
  viewport.modelCenterY = 50.0;
  viewport.scaleModelPerPaperIn = 50.f;
  layout.viewports.push_back(viewport);
  st.paperLayouts.push_back(layout);

  CadSurface surface;
  surface.name = "EG";
  surface.reducedDisplayLod = reducedLod;
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {0.0, 0.0, 2.0, 100.0, 0.0, 12.0, 100.0, 100.0, 12.0, 0.0, 100.0, 2.0};
  tin->indices = {0, 1, 2, 0, 2, 3};
  surface.tin = std::move(tin);
  st.cadSurfaces.push_back(std::move(surface));
  st.cadSurfaceAttrs.push_back(EntityAttributes{});
  st.cadSurfaceAttrs.back().linetype = "DASHED";
  REQUIRE_FALSE(st.surfaceStyles.empty());
  st.surfaceStyles.front().triangles.visible = true;
  st.surfaceStyles.front().slopeArrowsOn = true;
  EnsureEntityIds(st);

  const auto path = std::filesystem::temp_directory_path() / ("gosurvey-surface-lod-" + tag + ".pdf");
  std::vector<std::string> log;
  FPDF_InitLibrary();
  REQUIRE(PlotLayoutsToPdf(st, {0}, path.string().c_str(), log));
  FPDF_DOCUMENT document = FPDF_LoadDocument(path.string().c_str(), nullptr);
  REQUIRE(document != nullptr);
  FPDF_PAGE page = FPDF_LoadPage(document, 0);
  REQUIRE(page != nullptr);
  int segments = 0;
  for (int i = 0; i < FPDFPage_CountObjects(page); ++i) {
    FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, i);
    if (FPDFPageObj_GetType(object) == FPDF_PAGEOBJ_PATH)
      segments += FPDFPath_CountSegments(object);
  }
  FPDF_ClosePage(page);
  FPDF_CloseDocument(document);
  std::error_code ec;
  std::filesystem::remove(path, ec);
  return segments;
}

}  // namespace

TEST_CASE("Page Setup default plot leaves the line where the layout puts it (REQ-445 no regression, measured)",
          "[plot][req445]") {
  const Bounds b = PlotLineBounds({}, "default");
  CHECK_THAT(b.left, WithinAbs(1.f * kPt, 0.5));
  CHECK_THAT(b.right, WithinAbs(3.f * kPt, 0.5));
  CHECK_THAT(b.bottom, WithinAbs(1.f * kPt, 0.5));
}

TEST_CASE("Page Setup offset changes the PDF output by the stated amount (REQ-445 a, measured)", "[plot][req445]") {
  plotsheet::Settings s;
  s.offsetXIn = 2.f;
  const Bounds moved = PlotLineBounds(s, "offset");
  CHECK_THAT(moved.left, WithinAbs(3.f * kPt, 0.5));
  CHECK_THAT(moved.bottom, WithinAbs(1.f * kPt, 0.5));
}

TEST_CASE("Page Setup centre changes the PDF output to the sheet middle (REQ-445 b, measured)", "[plot][req445]") {
  plotsheet::Settings s;
  s.centerPlot = true;
  const Bounds b = PlotLineBounds(s, "centre");
  // The line's middle is x = 2 inches, which must land at the sheet middle x = 5.5 inches.
  CHECK_THAT((b.left + b.right) * 0.5f, WithinAbs(kSheetW * 0.5f * kPt, 0.5));
}

TEST_CASE("Page Setup fit changes the PDF output to fill the sheet (REQ-445 c, measured)", "[plot][req445]") {
  plotsheet::Settings s;
  s.fitToPaper = true;
  const Bounds b = PlotLineBounds(s, "fit");
  CHECK_THAT(b.left, WithinAbs(0.f, 0.5));
  CHECK_THAT(b.right, WithinAbs(kSheetW * kPt, 0.5));
}

namespace {

// Plots \p L on the sheet and returns the bounds of every page object of type \p wantType (points).
std::vector<Bounds> PlotObjectBounds(AppCommandState& st, int wantType, const std::string& tag) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / ("gosurvey-plotsheet-" + tag + ".pdf");
  std::vector<std::string> log;
  FPDF_InitLibrary();
  REQUIRE(PlotLayoutsToPdf(st, {0}, path.string().c_str(), log));

  FPDF_DOCUMENT doc = FPDF_LoadDocument(path.string().c_str(), nullptr);
  REQUIRE(doc != nullptr);
  FPDF_PAGE page = FPDF_LoadPage(doc, 0);
  REQUIRE(page != nullptr);
  std::vector<Bounds> out;
  const int count = FPDFPage_CountObjects(page);
  for (int i = 0; i < count; ++i) {
    FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
    if (FPDFPageObj_GetType(obj) != wantType)
      continue;
    Bounds b;
    if (FPDFPageObj_GetBounds(obj, &b.left, &b.bottom, &b.right, &b.top))
      out.push_back(b);
  }
  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);
  std::error_code ec;
  std::filesystem::remove(path, ec);
  return out;
}

std::vector<std::array<float, 2>> PlotPathPoints(AppCommandState& st, const std::string& tag) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / ("gosurvey-plotsheet-" + tag + ".pdf");
  std::vector<std::string> log;
  FPDF_InitLibrary();
  REQUIRE(PlotLayoutsToPdf(st, {0}, path.string().c_str(), log));
  FPDF_DOCUMENT doc = FPDF_LoadDocument(path.string().c_str(), nullptr);
  REQUIRE(doc != nullptr);
  FPDF_PAGE page = FPDF_LoadPage(doc, 0);
  REQUIRE(page != nullptr);
  std::vector<std::array<float, 2>> points;
  for (int i = 0; i < FPDFPage_CountObjects(page); ++i) {
    FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
    if (FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_PATH)
      continue;
    for (int si = 0; si < FPDFPath_CountSegments(obj); ++si) {
      FPDF_PATHSEGMENT segment = FPDFPath_GetPathSegment(obj, si);
      float x = 0.f, y = 0.f;
      if (FPDFPathSegment_GetPoint(segment, &x, &y))
        points.push_back({x, y});
    }
  }
  FPDF_ClosePage(page);
  FPDF_CloseDocument(doc);
  std::error_code ec;
  std::filesystem::remove(path, ec);
  return points;
}

}  // namespace

TEST_CASE("CTB and STB tables load and resolve their defined plot styles (REQ-485)", "[plot][req485]") {
  const auto writeTable = [](const std::string& extension, const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / ("gosurvey-req485" + extension);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << contents;
    file.close();
    return path;
  };
  const auto ctbPath = writeTable(".ctb",
    "PIAFILEVERSION_2.0,CTBVER1\ncustom_lineweight_display_units=0\n"
    "custom_lineweight_table {\n1=0.5\n}\nplot_style {\n6 {\ncolor_policy=4\ncolor=1\nlineweight=1\nscreen=50\n}\n}\n");
  plotstyle::Table ctb;
  std::string error;
  REQUIRE(plotstyle::Load(ctbPath, &ctb, &error));
  REQUIRE(ctb.colorDependent);
  const auto* ctbStyle = plotstyle::Find(ctb, 7, "Normal");
  REQUIRE(ctbStyle != nullptr);
  CHECK(plotstyle::ApplyColor(ctb, ctbStyle, 0x000000u) == 0xFE7F7Fu);
  CHECK_THAT(static_cast<float>(plotstyle::LineweightMm(ctb, ctbStyle, 0.25)), WithinAbs(0.5f, 0.001f));
  std::error_code ec;
  std::filesystem::remove(ctbPath, ec);

  const auto stbPath = writeTable(".stb",
    "PIAFILEVERSION_2.0,STBVER1\nplot_style {\n0 {\nname=Normal\n}\n1 {\nname=Heavy\nlineweight=1\n}\n}\n"
    "custom_lineweight_table {\n1=0.7\n}\n");
  plotstyle::Table stb;
  REQUIRE(plotstyle::Load(stbPath, &stb, &error));
  REQUIRE_FALSE(stb.colorDependent);
  const auto* stbStyle = plotstyle::Find(stb, 1, "Heavy");
  REQUIRE(stbStyle != nullptr);
  CHECK_THAT(static_cast<float>(plotstyle::LineweightMm(stb, stbStyle, 0.25)), WithinAbs(0.7f, 0.001f));
  CHECK(plotstyle::Find(stb, 1, "Missing")->name == "Normal");
  std::filesystem::remove(stbPath, ec);

  const std::string compressedBody = "custom_lineweight_display_units=0\nplot_style {\n0 {\nname=Normal\n}\n}\n";
  std::vector<unsigned char> compressed(mz_compressBound(static_cast<mz_ulong>(compressedBody.size())));
  mz_ulong compressedSize = static_cast<mz_ulong>(compressed.size());
  REQUIRE(mz_compress(compressed.data(), &compressedSize,
                      reinterpret_cast<const unsigned char*>(compressedBody.data()),
                      static_cast<mz_ulong>(compressedBody.size())) == MZ_OK);
  const auto compressedPath = std::filesystem::temp_directory_path() / "gosurvey-req485-compressed.stb";
  {
    std::ofstream file(compressedPath, std::ios::binary | std::ios::trunc);
    file << "PIAFILEVERSION_2.0,STBVER1,compress\r\npmzlibcodec";
    const char tableHeader[8]{};
    file.write(tableHeader, sizeof(tableHeader));
    file.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressedSize));
  }
  plotstyle::Table compressedStb;
  REQUIRE(plotstyle::Load(compressedPath, &compressedStb, &error));
  CHECK(plotstyle::Find(compressedStb, 7, "Normal") != nullptr);
  std::filesystem::remove(compressedPath, ec);

  const std::filesystem::path autodeskStyles =
      "C:/Program Files/Autodesk/AutoCAD 2025/UserDataCache/Plotters/Plot Styles";
  if (std::filesystem::exists(autodeskStyles / "acad.ctb")) {
    plotstyle::Table actualCtb;
    REQUIRE(plotstyle::Load(autodeskStyles / "acad.ctb", &actualCtb, &error));
    CHECK(actualCtb.colorDependent);
    CHECK(plotstyle::Find(actualCtb, 7, "Normal") != nullptr);
  }
  if (std::filesystem::exists(autodeskStyles / "acad.stb")) {
    plotstyle::Table actualStb;
    REQUIRE(plotstyle::Load(autodeskStyles / "acad.stb", &actualStb, &error));
    CHECK_FALSE(actualStb.colorDependent);
    CHECK(plotstyle::Find(actualStb, 7, "Normal") != nullptr);
  }
}

TEST_CASE("A missing configured plot-style table fails before writing PDF (REQ-485)", "[plot][req485]") {
  AppCommandState state;
  PaperLayout layout;
  layout.plotStyleTablePath = (std::filesystem::temp_directory_path() / "gosurvey-no-such-table.ctb").string();
  layout.paperLines = {1.f, 1.f, 0.f, 3.f, 1.f, 0.f};
  layout.paperLineAttrs.resize(1);
  state.paperLayouts.push_back(layout);
  const auto output = std::filesystem::temp_directory_path() / "gosurvey-req485-invalid.pdf";
  std::vector<std::string> log;
  CHECK_FALSE(PlotLayoutsToPdf(state, {0}, output.string().c_str(), log));
  CHECK_FALSE(std::filesystem::exists(output));
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("No PDF was written") != std::string::npos);
}

TEST_CASE("STB styles apply to both pages of a vector PDF batch (REQ-485)", "[plot][req485]") {
  const auto table = std::filesystem::temp_directory_path() / "gosurvey-req485-batch.stb";
  {
    std::ofstream file(table, std::ios::binary | std::ios::trunc);
    file << "PIAFILEVERSION_2.0,STBVER1\nplot_style {\n0 {\nname=Normal\n}\n"
            "1 {\nname=Red Pen\ncolor_policy=4\ncolor=1\n}\n"
            "2 {\nname=Blue Pen\ncolor_policy=4\ncolor=5\n}\n}\n";
  }
  AppCommandState state;
  for (const auto& [name, style] : {std::pair<std::string, std::string>{"Red sheet", "Red Pen"},
                                    {"Blue sheet", "Blue Pen"}}) {
    PaperLayout layout;
    layout.name = name;
    layout.plotStyleTablePath = table.string();
    layout.paperLines = {1.f, 1.f, 0.f, 3.f, 1.f, 0.f};
    EntityAttributes attr;
    attr.plotStyleName = style;
    layout.paperLineAttrs.push_back(attr);
    state.paperLayouts.push_back(layout);
  }
  const auto output = std::filesystem::temp_directory_path() / "gosurvey-req485-batch.pdf";
  std::vector<std::string> log;
  FPDF_InitLibrary();
  REQUIRE(PlotLayoutsToPdf(state, {0, 1}, output.string().c_str(), log));
  FPDF_DOCUMENT document = FPDF_LoadDocument(output.string().c_str(), nullptr);
  REQUIRE(document != nullptr);
  REQUIRE(FPDF_GetPageCount(document) == 2);
  std::vector<uint32_t> colors;
  for (int i = 0; i < 2; ++i) {
    FPDF_PAGE page = FPDF_LoadPage(document, i);
    REQUIRE(page != nullptr);
    bool found = false;
    for (int j = 0; j < FPDFPage_CountObjects(page); ++j) {
      FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, j);
      if (FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_PATH) continue;
      unsigned int r = 0, g = 0, b = 0, a = 0;
      if (!FPDFPageObj_GetStrokeColor(object, &r, &g, &b, &a)) continue;
      colors.push_back((r << 16) | (g << 8) | b);
      found = true;
      break;
    }
    CHECK(found);
    FPDF_ClosePage(page);
  }
  CHECK(colors == std::vector<uint32_t>{0xFF0000u, 0x0000FFu});
  FPDF_CloseDocument(document);
  std::error_code ec;
  std::filesystem::remove(table, ec);
  std::filesystem::remove(output, ec);
}

TEST_CASE("Fit ignores sheet lines on a non-plottable layer (REQ-445 c, measured)", "[plot][req445]") {
  AppCommandState st;
  CadLayerRow hidden;
  hidden.name = "Hidden";
  hidden.on = true;
  hidden.frozen = false;
  hidden.plottable = false;
  st.drawingLayerTable.push_back(hidden);

  PaperLayout L;
  L.name = "Sheet";
  L.landscape = true;
  L.portraitWidthIn = kSheetH;
  L.portraitHeightIn = kSheetW;
  L.fitToPaper = true;
  // A visible 2-inch line at y = 1 and a far-away line on the hidden layer, which must not count towards the fit.
  L.paperLines = {1.f, 1.f, 0.f, 3.f, 1.f, 0.f, 1.f, 7.f, 0.f, 9.f, 7.f, 0.f};
  L.paperLineAttrs.resize(2);
  L.paperLineAttrs[1].layer = "Hidden";
  st.paperLayouts.push_back(L);

  const std::vector<Bounds> paths = PlotObjectBounds(st, FPDF_PAGEOBJ_PATH, "hidden-layer");
  REQUIRE(!paths.empty());
  // Visible line only: fitted across the sheet width, so it runs from the left edge to the right edge.
  CHECK_THAT(paths.front().left, WithinAbs(0.f, 0.5));
  CHECK_THAT(paths.front().right, WithinAbs(kSheetW * kPt, 0.5));
}

TEST_CASE("Page Setup plot ratio changes the PDF output to the stated scale (REQ-445 d, measured)", "[plot][req445]") {
  plotsheet::Settings s;
  s.plotRatio = 2.f;
  const Bounds b = PlotLineBounds(s, "ratio");
  // Half size: the 2-inch line from x = 1 to x = 3 becomes 1 inch long at x = 0.5 to x = 1.5. PDFium's path bounds
  // include the stroke (0.5 pt) around the geometry, so the measured width is about 1 pt longer than the line.
  CHECK_THAT(b.left, WithinAbs(0.5f * kPt, 1.0));
  CHECK_THAT(b.right - b.left, WithinAbs(1.f * kPt, 1.5));
}

TEST_CASE("Fit includes a polyline bulge that bows past its end points (REQ-445 c, measured)", "[plot][req445]") {
  AppCommandState st;
  PaperLayout L;
  L.name = "Sheet";
  L.landscape = true;
  L.portraitWidthIn = kSheetH;
  L.portraitHeightIn = kSheetW;
  L.fitToPaper = true;
  // A semicircle from (1,1) to (3,1). A positive bulge bows it down to y = 0, so the fit must lift that point to the
  // sheet's bottom edge rather than let it fall below the page.
  L.paperPolyOffsets = {0, 2};
  L.paperPolyVerts = {1.f, 1.f, 0.f, 3.f, 1.f, 0.f};
  L.paperPolyVertsBulge = {1.f, 0.f};
  L.paperPolyClosed = {0};
  L.paperPolyAttrs.resize(1);
  st.paperLayouts.push_back(L);

  const std::vector<Bounds> paths = PlotObjectBounds(st, FPDF_PAGEOBJ_PATH, "bulge");
  REQUIRE(!paths.empty());
  float bottom = paths.front().bottom, top = paths.front().top;
  for (const Bounds& b : paths) {
    bottom = std::min(bottom, b.bottom);
    top = std::max(top, b.top);
  }
  // Fit scales by 11 / 2 (width-limited), so the arc's top at y = 1 lands at 5.5 inches and its bottom at the page edge.
  CHECK_THAT(bottom, WithinAbs(0.f, 1.0));
  CHECK_THAT(top, WithinAbs(5.5f * kPt, 1.0));
}

TEST_CASE("Page Setup plot ratio scales native text in the PDF (REQ-445 d, measured)", "[plot][req445]") {
  // The same 0.5-inch text at 1 : 1 and at 1 : 2. The ratio halves its size and scales its position about the sheet's
  // lower-left corner, so the measured height and left edge both halve (the glyph side bearing scales with the text).
  auto plotText = [](float ratio, const std::string& tag) {
    AppCommandState st;
    PaperLayout L;
    L.name = "Sheet";
    L.landscape = true;
    L.portraitWidthIn = kSheetH;
    L.portraitHeightIn = kSheetW;
    L.scaleModelPerPaperIn = ratio;
    CadAnnotation text;
    text.text = "PLOT";
    text.insX = 1.f;
    text.insY = 1.f;
    text.plottedHeightInches = 0.5f;
    L.paperTexts.push_back(text);
    st.paperLayouts.push_back(L);
    const std::vector<Bounds> objs = PlotObjectBounds(st, FPDF_PAGEOBJ_TEXT, tag);
    REQUIRE(!objs.empty());
    return objs.front();
  };
  const Bounds full = plotText(1.f, "text-1to1");
  const Bounds half = plotText(2.f, "text-1to2");
  const float fullH = full.top - full.bottom, halfH = half.top - half.bottom;
  REQUIRE(fullH > 0.f);
  CHECK_THAT(halfH, WithinAbs(fullH * 0.5f, 1.0));
  CHECK_THAT(half.left, WithinAbs(full.left * 0.5f, 1.0));
}

TEST_CASE("Fit measures an arc as sheet content (REQ-445 c, measured)", "[plot][req445]") {
  AppCommandState st;
  PaperLayout L;
  L.name = "Sheet";
  L.landscape = true;
  L.portraitWidthIn = kSheetH;
  L.portraitHeightIn = kSheetW;
  L.fitToPaper = true;
  // A semicircle of radius 1 inch centred at (2,2), running from (1,2) through the bottom to (3,2). Its only content
  // is the arc, so without arcs in the extent the sheet would not be fitted and the arc would stay 2 inches wide.
  CadArc arc;
  arc.cx = 2.f;
  arc.cy = 2.f;
  arc.r = 1.f;
  arc.startRad = 3.14159265f;
  arc.sweepRad = 3.14159265f;
  L.paperArcs.push_back(arc);
  st.paperLayouts.push_back(L);

  const std::vector<Bounds> paths = PlotObjectBounds(st, FPDF_PAGEOBJ_PATH, "arc-fit");
  REQUIRE(!paths.empty());
  // PDFium's path bounds include the stroke (about 0.5 pt each side), so the edges allow 1 pt.
  CHECK_THAT(paths.front().left, WithinAbs(0.f, 1.0));
  CHECK_THAT(paths.front().right, WithinAbs(kSheetW * kPt, 1.0));
}

TEST_CASE("Closed-polyline viewport boundary clips plotted model strokes as vector paths (REQ-490)",
          "[plot][req490]") {
  AppCommandState st;
  PaperLayout layout;
  layout.name = "Sheet";
  layout.landscape = true;
  layout.portraitWidthIn = 8.5f;
  layout.portraitHeightIn = 11.f;
  Viewport viewport;
  viewport.paperXIn = 2.f;
  viewport.paperYIn = 2.f;
  viewport.paperWIn = 10.f;
  viewport.paperHIn = 8.f;
  viewport.modelCenterX = 0.0;
  viewport.modelCenterY = 0.0;
  viewport.scaleModelPerPaperIn = 1.f;
  viewport.clipBoundary = {{2.f, 2.f, 0.f}, {12.f, 2.f, 0.f}, {5.f, 10.f, 0.f}};
  layout.viewports.push_back(viewport);
  st.paperLayouts.push_back(layout);
  // This horizontal model line maps to paper x=[0,14], y=4; the triangle intersects it at x=3.25..10.75.
  st.userLinesFlat = {-7.f, -2.f, 0.f, 7.f, -2.f, 0.f};
  st.userLineAttrs.resize(1);
  const auto points = PlotPathPoints(st, "req480-viewport-clip");
  bool clippedLineFound = false;
  for (size_t i = 0; i + 1 < points.size(); ++i) {
    const float x0 = points[i][0] / kPt, x1 = points[i + 1][0] / kPt;
    const float y0 = points[i][1] / kPt, y1 = points[i + 1][1] / kPt;
    if (std::fabs(x0 - 2.75f) < 0.02f && std::fabs(x1 - 10.25f) < 0.02f &&
        std::fabs(y0 - 4.f) < 0.02f && std::fabs(y1 - 4.f) < 0.02f)
      clippedLineFound = true;
  }
  CHECK(clippedLineFound);
}

TEST_CASE("Two-vertex bulge viewport clips plotted model strokes to its curved boundary (REQ-490)",
          "[plot][req490]") {
  AppCommandState st;
  PaperLayout layout;
  layout.name = "Sheet";
  layout.landscape = true;
  layout.portraitWidthIn = 8.5f;
  layout.portraitHeightIn = 11.f;
  Viewport viewport;
  viewport.paperXIn = 2.f;
  viewport.paperYIn = 2.f;
  viewport.paperWIn = 4.f;
  viewport.paperHIn = 4.f;
  viewport.scaleModelPerPaperIn = 1.f;
  // Two semicircles form a circle centred at (4,4), with radius 2 inches.
  viewport.clipBoundary = {{2.f, 4.f, 1.f}, {6.f, 4.f, 1.f}};
  layout.viewports.push_back(viewport);
  st.paperLayouts.push_back(layout);
  st.userLinesFlat = {-7.f, -1.5f, 0.f, 7.f, -1.5f, 0.f};
  st.userLineAttrs.resize(1);

  const auto points = PlotPathPoints(st, "req490-two-bulge-clip");
  float minX = std::numeric_limits<float>::infinity();
  float maxX = -minX;
  for (const auto& point : points) {
    if (std::fabs(point[1] / kPt - 2.5f) > 0.03f) continue;
    minX = std::min(minX, point[0] / kPt);
    maxX = std::max(maxX, point[0] / kPt);
  }
  REQUIRE(std::isfinite(minX));
  CHECK_THAT(minX, WithinAbs(4.f - std::sqrt(4.f - 2.25f), 0.03));
  CHECK_THAT(maxX, WithinAbs(4.f + std::sqrt(4.f - 2.25f), 0.03));
}

TEST_CASE("Reduced LOD does not change plotted TIN edge detail", "[plot][req508]") {
  CHECK(PlotSurfaceSegmentCount(true, "reduced") == PlotSurfaceSegmentCount(false, "high"));
}

TEST_CASE("PDF viewport and filled-region RGB packing preserves the blue channel", "[plot][req485]") {
  AppCommandState st;
  PaperLayout layout;
  layout.name = "Sheet";
  layout.landscape = true;
  layout.portraitWidthIn = 8.5f;
  layout.portraitHeightIn = 11.f;
  Viewport viewport;
  viewport.paperXIn = 1.f;
  viewport.paperYIn = 1.f;
  viewport.paperWIn = 8.f;
  viewport.paperHIn = 6.f;
  viewport.scaleModelPerPaperIn = 1.f;
  layout.viewports.push_back(viewport);
  CadFilledRegion fill;
  fill.vertsXyz = {1.0, 1.0, 0.0, 3.0, 1.0, 0.0, 2.0, 3.0, 0.0};
  fill.loopStart = {0};
  layout.paperFilledRegions.push_back(fill);
  EntityAttributes fillAttrs;
  fillAttrs.color = "#0000FF";
  layout.paperFilledRegionAttrs.push_back(fillAttrs);
  st.paperLayouts.push_back(layout);
  st.userLinesFlat = {-1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  st.userLineAttrs.resize(1);
  st.userLineAttrs[0].color = "#0000FF";

  const auto path = std::filesystem::temp_directory_path() / "gosurvey-req485-blue-channel.pdf";
  std::vector<std::string> log;
  FPDF_InitLibrary();
  REQUIRE(PlotLayoutsToPdf(st, {0}, path.string().c_str(), log));
  FPDF_DOCUMENT document = FPDF_LoadDocument(path.string().c_str(), nullptr);
  REQUIRE(document != nullptr);
  FPDF_PAGE page = FPDF_LoadPage(document, 0);
  REQUIRE(page != nullptr);
  bool blueStrokeFound = false;
  bool blueFillFound = false;
  for (int i = 0; i < FPDFPage_CountObjects(page); ++i) {
    FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, i);
    if (FPDFPageObj_GetType(object) != FPDF_PAGEOBJ_PATH) continue;
    unsigned int r = 0, g = 0, b = 0, a = 0;
    if (FPDFPageObj_GetStrokeColor(object, &r, &g, &b, &a))
      blueStrokeFound = blueStrokeFound || (r == 0 && g == 0 && b == 255);
    if (FPDFPageObj_GetFillColor(object, &r, &g, &b, &a))
      blueFillFound = blueFillFound || (r == 0 && g == 0 && b == 255);
  }
  CHECK(blueStrokeFound);
  CHECK(blueFillFound);
  FPDF_ClosePage(page);
  FPDF_CloseDocument(document);
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST_CASE("Off and hide-plot viewports emit no model content to PDF (REQ-482)", "[plot][req482]") {
  AppCommandState state;
  PaperLayout layout;
  layout.name = "Sheet";
  layout.portraitWidthIn = 11.f;
  layout.portraitHeightIn = 17.f;
  Viewport viewport;
  viewport.paperXIn = 1.f;
  viewport.paperYIn = 1.f;
  viewport.paperWIn = 8.f;
  viewport.paperHIn = 6.f;
  viewport.modelCenterX = 0.0;
  viewport.modelCenterY = 0.0;
  viewport.scaleModelPerPaperIn = 1.f;
  layout.viewports.push_back(viewport);
  state.paperLayouts.push_back(layout);
  state.userLinesFlat = {-2.f, 0.f, 0.f, 2.f, 0.f, 0.f};
  state.userLineAttrs.resize(1);

  CHECK_FALSE(PlotPathPoints(state, "req482-viewport-on").empty());
  state.paperLayouts[0].viewports[0].hidePlot = true;
  CHECK(PlotPathPoints(state, "req482-viewport-hide-plot").empty());
  state.paperLayouts[0].viewports[0].hidePlot = false;
  state.paperLayouts[0].viewports[0].on = false;
  CHECK(PlotPathPoints(state, "req482-viewport-off").empty());
}
