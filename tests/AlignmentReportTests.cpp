// REQ-494 / GitHub issue #121 - alignment text and CSV reports.
// Acceptance 1 (the example report's structure) and 2 (CSV values re-read to the same numbers) are tested here.
// Acceptance 3 (design-criteria findings, each entity marked) is tested in AlignmentSpecTests.cpp.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>
#include <vector>

#include "alignment/Alignment.hpp"
#include "alignment/AlignmentReport.hpp"

using Catch::Approx;

namespace {

constexpr double kDeg = alignment::kPi / 180.0;

/// The issue's example shape: a 250-unit tangent, a 500-unit-radius curve of 15 degrees, then a tangent.
alignment::Alignment ExampleAlignment() {
  alignment::Alignment a;
  a.name = "MAIN ROAD";
  REQUIRE(a.AppendTangent({0.0, 0.0}, 45.0 * kDeg, 250.0));
  REQUIRE(a.AppendCurveByDelta(500.0, 15.0 * kDeg, true));
  REQUIRE(a.AppendTangent(200.0));
  return a;
}

bool HasLine(const std::vector<std::string>& lines, const std::string& text) {
  for (const std::string& l : lines) {
    if (l == text) return true;
  }
  return false;
}

bool HasLineContaining(const std::vector<std::string>& lines, const std::string& part) {
  for (const std::string& l : lines) {
    if (l.find(part) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST_CASE("Text report carries the header, each entity block and its values", "[req494][alignment]") {
  const alignment::Alignment a = ExampleAlignment();
  alignment::AlignmentReportFormat fmt;
  std::vector<std::string> lines;
  alignment::BuildAlignmentReportLines(a, fmt, &lines);

  CHECK(HasLine(lines, "ALIGNMENT: MAIN ROAD"));
  CHECK(HasLine(lines, "Type: Centerline"));
  CHECK(HasLineContaining(lines, "Start Station: 0+00"));
  CHECK(HasLineContaining(lines, "Entity count:  3"));
  CHECK(HasLineContaining(lines, "Curve count:   1"));
  CHECK(HasLineContaining(lines, "Spiral count:  0"));

  CHECK(HasLine(lines, "ENTITY 1"));
  CHECK(HasLine(lines, "ENTITY 2"));
  CHECK(HasLine(lines, "ENTITY 3"));
  CHECK(HasLineContaining(lines, "Length: 250.00'"));   // the first tangent
  CHECK(HasLineContaining(lines, "Radius: 500.00'"));  // the curve
  CHECK(HasLineContaining(lines, "Delta: 15"));
  CHECK(HasLineContaining(lines, "Direction: Right"));

  // Entity order is the travel order: the curve block comes after the first tangent block.
  size_t first = 0, curve = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i] == "ENTITY 1") first = i;
    if (lines[i] == "ENTITY 2") curve = i;
  }
  CHECK(first < curve);
}

TEST_CASE("Text report of an empty alignment says so and stops", "[req494][alignment]") {
  alignment::Alignment empty;
  empty.name = "EMPTY";
  alignment::AlignmentReportFormat fmt;
  std::vector<std::string> lines;
  alignment::BuildAlignmentReportLines(empty, fmt, &lines);
  CHECK(HasLine(lines, "ALIGNMENT: EMPTY"));
  CHECK(HasLine(lines, "  (no geometry)"));
  CHECK_FALSE(HasLineContaining(lines, "ENTITY 1"));
}

TEST_CASE("CSV report has a header and one row per entity at full precision", "[req494][alignment]") {
  const alignment::Alignment a = ExampleAlignment();
  alignment::AlignmentReportFormat fmt;
  const SheetGrid::Grid grid = alignment::BuildAlignmentReportCsv(a, fmt);
  REQUIRE(grid.size() == 4);  // header + 3 entities
  CHECK(grid[0].size() == 19);
  CHECK(std::get<std::string>(grid[0][0]) == "alignment");

  // Row 2 is the first tangent: length 250, no radius.
  const SheetGrid::Row& tangent = grid[1];
  REQUIRE(tangent.size() == 19);
  CHECK(std::get<std::string>(tangent[0]) == "MAIN ROAD");
  CHECK(std::get<std::string>(tangent[3]) == "Tangent");
  REQUIRE(std::holds_alternative<double>(tangent[7]));
  CHECK(std::get<double>(tangent[7]) == Approx(250.0).margin(1e-9));
  CHECK(std::holds_alternative<std::monostate>(tangent[8]));  // radius is blank for a tangent

  // Row 3 is the curve: radius 500, delta 15, arc length 500 * 15 degrees in radians.
  const SheetGrid::Row& curve = grid[2];
  CHECK(std::get<std::string>(curve[3]) == "Curve");
  REQUIRE(std::holds_alternative<double>(curve[8]));
  CHECK(std::get<double>(curve[8]) == Approx(500.0).margin(1e-9));
  CHECK(std::get<double>(curve[9]) == Approx(15.0).margin(1e-9));
  alignment::CurveData cd;
  REQUIRE(a.CurveDataFor(1, &cd));
  CHECK(std::get<double>(curve[10]) == cd.arcLength);
  CHECK(std::get<double>(curve[11]) == cd.tangentLength);
  CHECK(std::get<double>(curve[12]) == cd.chord);
  CHECK(std::get<std::string>(curve[17]) == "Right");
}

TEST_CASE("Report values are the same numbers the geometry gives", "[req494][alignment]") {
  const alignment::Alignment a = ExampleAlignment();
  alignment::AlignmentReportFormat fmt;
  const SheetGrid::Grid grid = alignment::BuildAlignmentReportCsv(a, fmt);
  alignment::CurveData cd;
  REQUIRE(a.CurveDataFor(1, &cd));
  CHECK(std::get<double>(grid[2][11]) == cd.tangentLength);
  CHECK(std::get<double>(grid[2][12]) == cd.chord);
}

TEST_CASE("CSV report of an empty alignment is just the header", "[req494][alignment]") {
  alignment::Alignment empty;
  empty.name = "EMPTY";
  alignment::AlignmentReportFormat fmt;
  const SheetGrid::Grid grid = alignment::BuildAlignmentReportCsv(empty, fmt);
  CHECK(grid.size() == 1);
}
