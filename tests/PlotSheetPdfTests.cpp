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

#include <fpdf_edit.h>
#include <fpdfview.h>

#include <filesystem>
#include <string>
#include <vector>

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

TEST_CASE("Page Setup plot ratio changes the PDF output to the stated scale (REQ-445 d, measured)", "[plot][req445]") {
  plotsheet::Settings s;
  s.plotRatio = 2.f;
  const Bounds b = PlotLineBounds(s, "ratio");
  // Half size: the 2-inch line from x = 1 to x = 3 becomes 1 inch long at x = 0.5 to x = 1.5. PDFium's path bounds
  // include the stroke (0.5 pt) around the geometry, so the measured width is about 1 pt longer than the line.
  CHECK_THAT(b.left, WithinAbs(0.5f * kPt, 1.0));
  CHECK_THAT(b.right - b.left, WithinAbs(1.f * kPt, 1.5));
}
