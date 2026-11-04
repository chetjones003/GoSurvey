// REQ-445: PDF plot honours the Page Setup options (offset, centre, fit-to-paper and plot ratio).
//
// The placement rules are pure (io/PlotSheetTransform.hpp). The measured PDF output is checked in PlotSheetPdfTests.cpp.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "io/PlotSheetTransform.hpp"

using Catch::Matchers::WithinAbs;

namespace {

constexpr float kSheetW = 11.f;  // a landscape 8.5 x 11 sheet is 11 x 8.5 inches
constexpr float kSheetH = 8.5f;

// A 2-inch line from (1,1) to (3,1).
plotsheet::Extent LineExtent() {
  plotsheet::Extent e;
  e.Add(1.f, 1.f);
  e.Add(3.f, 1.f);
  return e;
}

}  // namespace

TEST_CASE("Default plot settings keep the sheet identity (REQ-445 no regression)", "[plot][req445]") {
  const plotsheet::Transform t = plotsheet::Compute({}, kSheetW, kSheetH, LineExtent());
  CHECK(t.k == 1.f);
  CHECK(t.dx == 0.f);
  CHECK(t.dy == 0.f);
  CHECK(t.X(2.5f) == 2.5f);
  CHECK(t.Y(1.5f) == 1.5f);
}

TEST_CASE("Plot offset moves the content by the stated inches (REQ-445 a)", "[plot][req445]") {
  plotsheet::Settings s;
  s.offsetXIn = 2.f;
  s.offsetYIn = 0.5f;
  const plotsheet::Transform t = plotsheet::Compute(s, kSheetW, kSheetH, LineExtent());
  CHECK_THAT(t.X(1.f), WithinAbs(3.f, 1.e-5));
  CHECK_THAT(t.Y(1.f), WithinAbs(1.5f, 1.e-5));
}

TEST_CASE("Centre the plot puts the content middle on the sheet middle (REQ-445 b)", "[plot][req445]") {
  plotsheet::Settings s;
  s.centerPlot = true;
  s.offsetXIn = 5.f;  // ignored when centring
  const plotsheet::Transform t = plotsheet::Compute(s, kSheetW, kSheetH, LineExtent());
  // The content spans x 1..3, so its middle (2) lands at the sheet middle (5.5).
  CHECK_THAT(t.X(2.f), WithinAbs(kSheetW * 0.5f, 1.e-5));
}

TEST_CASE("Fit to paper scales the content to fill the sheet (REQ-445 c)", "[plot][req445]") {
  plotsheet::Settings s;
  s.fitToPaper = true;
  // Content spans 2 inches wide and no height, so the sheet width limits the fit: k = 11 / 2.
  const plotsheet::Transform t = plotsheet::Compute(s, kSheetW, kSheetH, LineExtent());
  CHECK_THAT(t.k, WithinAbs(kSheetW / 2.f, 1.e-4));
  CHECK_THAT(t.X(1.f), WithinAbs(0.f, 1.e-4));
  CHECK_THAT(t.X(3.f), WithinAbs(kSheetW, 1.e-4));
}

TEST_CASE("Plot ratio 1 : N scales the content by 1 / N (REQ-445 d)", "[plot][req445]") {
  plotsheet::Settings s;
  s.plotRatio = 2.f;
  const plotsheet::Transform t = plotsheet::Compute(s, kSheetW, kSheetH, LineExtent());
  CHECK_THAT(t.k, WithinAbs(0.5f, 1.e-6));
  CHECK_THAT(t.X(3.f), WithinAbs(1.5f, 1.e-6));
  // A ratio of zero or less plots at 1 : 1 rather than dividing by zero.
  s.plotRatio = 0.f;
  CHECK_THAT(plotsheet::Compute(s, kSheetW, kSheetH, LineExtent()).k, WithinAbs(1.f, 1.e-6));
}

TEST_CASE("An empty content extent counts as the whole sheet", "[plot][req445]") {
  plotsheet::Settings s;
  s.fitToPaper = true;
  const plotsheet::Transform t = plotsheet::Compute(s, kSheetW, kSheetH, plotsheet::Extent{});
  CHECK_THAT(t.k, WithinAbs(1.f, 1.e-6));
  CHECK_THAT(t.dx, WithinAbs(0.f, 1.e-6));
  CHECK_THAT(t.dy, WithinAbs(0.f, 1.e-6));
}
