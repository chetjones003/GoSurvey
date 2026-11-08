// REQ-519 / GitHub issue #1001 - sample lines: station placement, refusals, and section sampling against a TIN.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "alignment/Alignment.hpp"
#include "alignment/AlignmentSampleLines.hpp"
#include "util/surfacequery.hpp"

using Catch::Approx;

namespace {

// A straight alignment running due north from (0, 0), `length` long, in a feet drawing.
alignment::Alignment NorthRoad(double length) {
  alignment::Alignment a;
  a.name = "Road";
  a.AppendTangent(alignment::Point2{0.0, 0.0}, 0.0, length);
  return a;
}

// A sample line through (x, y) running east-west: positive offsets run east (right of a north-running alignment).
alignment::SampleLine EastLine(double x, double y, double left, double right) {
  alignment::SampleLine line;
  line.origin = alignment::Point2{x, y};
  line.direction = alignment::Point2{1.0, 0.0};
  line.leftWidth = left;
  line.rightWidth = right;
  return line;
}

// The section point at (about) `offset`, or nullptr.
const alignment::SectionPoint* PointAt(const alignment::SampleLine& line, double offset) {
  for (const alignment::SectionPoint& p : line.section)
    if (std::abs(p.offset - offset) < 1e-6) return &p;
  return nullptr;
}

}  // namespace

TEST_CASE("sample lines: one line at each 50 ft station from 0+00 to 5+00 (REQ-519 acceptance)",
          "[req519][samplelines]") {
  const alignment::Alignment road = NorthRoad(500.0);
  const alignment::SampleLineSettings s{0.0, 500.0, 50.0, 50.0, 50.0};
  std::vector<alignment::SampleLine> lines;
  std::string why;
  REQUIRE(alignment::ComputeSampleLines(road, s, alignment::StationFormat{}, &lines, &why));
  REQUIRE(lines.size() == 11);
  CHECK(lines.front().name == "0+00");
  CHECK(lines[1].name == "0+50");
  CHECK(lines[2].name == "1+00");
  CHECK(lines.back().name == "5+00");
  // Perpendicular to the north-running alignment: each line runs east-west through its station.
  CHECK(lines[1].origin.x == Approx(0.0).margin(1e-9));
  CHECK(lines[1].origin.y == Approx(50.0));
  CHECK(lines[1].direction.x == Approx(1.0));
  CHECK(lines[1].direction.y == Approx(0.0).margin(1e-9));
  CHECK(lines[1].leftWidth == Approx(50.0));
  CHECK_FALSE(lines[1].hasSection);
  CHECK(lines[1].section.empty());
}

TEST_CASE("sample lines: each refusal names its reason and creates nothing", "[req519][samplelines]") {
  const alignment::Alignment road = NorthRoad(500.0);
  using S = alignment::SampleLineSettings;
  CHECK(alignment::SampleLineSettingsRefusal(road, S{0.0, 500.0, 50.0, 50.0, 50.0}).empty());
  CHECK(alignment::SampleLineSettingsRefusal(road, S{0.0, 500.0, 0.0, 50.0, 50.0}).find("interval") != std::string::npos);
  CHECK(alignment::SampleLineSettingsRefusal(road, S{0.0, 500.0, -5.0, 50.0, 50.0}).find("interval") != std::string::npos);
  CHECK(alignment::SampleLineSettingsRefusal(road, S{500.0, 0.0, 50.0, 50.0, 50.0}).find("before the start") !=
        std::string::npos);
  CHECK(alignment::SampleLineSettingsRefusal(road, S{0.0, 500.0, 50.0, -1.0, 50.0}).find("zero or more") !=
        std::string::npos);
  CHECK(alignment::SampleLineSettingsRefusal(road, S{0.0, 500.0, 50.0, 0.0, 0.0}).find("above zero") !=
        std::string::npos);

  std::vector<alignment::SampleLine> lines;
  std::string why;
  CHECK_FALSE(alignment::ComputeSampleLines(road, S{0.0, 9000.0, 50.0, 50.0, 50.0}, alignment::StationFormat{}, &lines,
                                            &why));  // beyond the end of the alignment
  CHECK_FALSE(why.empty());
  CHECK(lines.empty());

  CHECK_FALSE(alignment::ComputeSampleLines(road, S{0.0, 500.0, 0.001, 50.0, 50.0}, alignment::StationFormat{}, &lines,
                                            &why));  // 500,000 lines is past the limit
  CHECK(why.find("limit") != std::string::npos);
  CHECK(lines.empty());
}

TEST_CASE("sample sections: a flat surface gives every point the same ground (REQ-519 acceptance)",
          "[req519][samplelines]") {
  // A flat TIN over a 400 ft square centred on the origin, at elevation 100.
  const std::vector<double> verts = {-200, -200, 100, 200, -200, 100, 200, 200, 100, -200, 200, 100};
  const std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
  const TinSurfaceQuery surface(verts, idx);

  alignment::SampleLine line = EastLine(0.0, 0.0, 50.0, 50.0);
  alignment::SampleSection(verts, idx, surface, line);
  REQUIRE_FALSE(line.section.empty());
  for (const alignment::SectionPoint& p : line.section) CHECK(p.ground == Approx(100.0));
  CHECK(line.section.front().offset == Approx(-50.0));
  CHECK(line.section.back().offset == Approx(50.0));
}

TEST_CASE("sample sections: 20 ft out on a surface rising 1 ft per 10 ft is 2.0 ft above the alignment",
          "[req519][samplelines]") {
  // Ground z = 100 + 0.1 x. The triangles split along x = 20, so the swath crosses that edge at offset 20.
  const std::vector<double> verts = {
      -300, -300, 70,   // A
      20,   -300, 102,  // B
      300,  -300, 130,  // C
      300,  300,  130,  // D
      20,   300,  102,  // E
      -300, 300,  70,   // F
  };
  const std::vector<std::uint32_t> idx = {0, 1, 4, 0, 4, 5, 1, 2, 3, 1, 3, 4};
  const TinSurfaceQuery surface(verts, idx);

  alignment::SampleLine line = EastLine(0.0, 0.0, 50.0, 50.0);  // the alignment origin is at x = 0
  alignment::SampleSection(verts, idx, surface, line);
  const alignment::SectionPoint* origin = PointAt(line, 0.0);
  const alignment::SectionPoint* out = PointAt(line, 20.0);
  REQUIRE(origin != nullptr);
  REQUIRE(out != nullptr);  // the triangle edge at x = 20 is a sample point (REQ-519.2)
  CHECK(origin->ground == Approx(100.0));
  CHECK(out->ground == Approx(102.0));
  CHECK(out->ground - origin->ground == Approx(2.0));
}

TEST_CASE("sample sections: a line wholly inside one triangle still produces a section", "[req519][samplelines]") {
  // One triangle, flat at elevation 50. The swath is 10 ft long and lies well inside it.
  const std::vector<double> verts = {0, 0, 50, 100, 0, 50, 0, 100, 50};
  const std::vector<std::uint32_t> idx = {0, 1, 2};
  const TinSurfaceQuery surface(verts, idx);

  alignment::SampleLine line = EastLine(30.0, 10.0, 5.0, 5.0);
  alignment::SampleSection(verts, idx, surface, line);
  REQUIRE(line.section.size() == 3);
  CHECK(line.section[0].offset == Approx(-5.0));
  CHECK(line.section[1].offset == Approx(0.0));
  CHECK(line.section[2].offset == Approx(5.0));
  for (const alignment::SectionPoint& p : line.section) CHECK(p.ground == Approx(50.0));
}

TEST_CASE("sample sections: a swath that runs past the coverage keeps only the covered points", "[req519][samplelines]") {
  // A flat square at x in [-200, 200]. The swath runs from x = 50 to x = 250, so its east end is off the surface.
  const std::vector<double> verts = {-200, -200, 100, 200, -200, 100, 200, 200, 100, -200, 200, 100};
  const std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
  const TinSurfaceQuery surface(verts, idx);

  alignment::SampleLine line = EastLine(150.0, 0.0, 100.0, 100.0);
  alignment::SampleSection(verts, idx, surface, line);
  REQUIRE(line.section.size() == 3);
  CHECK(line.section[0].offset == Approx(-100.0));  // x = 50, the west swath end
  CHECK(line.section[1].offset == Approx(0.0));     // the origin
  CHECK(line.section[2].offset == Approx(50.0));    // x = 200, where the coverage ends
}

TEST_CASE("sample sections: a line the surface does not cover gets an empty section", "[req519][samplelines]") {
  const std::vector<double> verts = {-200, -200, 100, 200, -200, 100, 200, 200, 100, -200, 200, 100};
  const std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
  const TinSurfaceQuery surface(verts, idx);

  alignment::SampleLine line = EastLine(1000.0, 0.0, 50.0, 50.0);
  alignment::SampleSection(verts, idx, surface, line);
  CHECK(line.section.empty());
}

TEST_CASE("sample sections: a surface with no triangles gives no points", "[req519][samplelines]") {
  const std::vector<double> verts;
  const std::vector<std::uint32_t> idx;
  const TinSurfaceQuery surface(verts, idx);
  alignment::SampleLine line = EastLine(0.0, 0.0, 50.0, 50.0);
  alignment::SampleSection(verts, idx, surface, line);
  CHECK(line.section.empty());
}

TEST_CASE("the nice scale series steps 1, 2, 5 per decade and never goes finer than 1 ft per inch",
          "[req519][samplelines]") {
  CHECK(alignment::NiceFeetPerInchAtLeast(0.0) == Approx(1.0));
  CHECK(alignment::NiceFeetPerInchAtLeast(1.0) == Approx(1.0));
  CHECK(alignment::NiceFeetPerInchAtLeast(1.5) == Approx(2.0));
  CHECK(alignment::NiceFeetPerInchAtLeast(2.0) == Approx(2.0));
  CHECK(alignment::NiceFeetPerInchAtLeast(3.0) == Approx(5.0));
  CHECK(alignment::NiceFeetPerInchAtLeast(7.0) == Approx(10.0));
  CHECK(alignment::NiceFeetPerInchAtLeast(120.0) == Approx(200.0));
}

TEST_CASE("sheet scales: the finest step at which every section fits its column and plot (REQ-519.11)",
          "[req519][samplelines]") {
  // A sheet with no points is 1 in = 10 ft both ways.
  const alignment::SectionScale none = alignment::FitSectionScale(false, 0.0, 0.0, 4.0, 8.0);
  CHECK(none.feetPerInchAcross == Approx(10.0));
  CHECK(none.feetPerInchDown == Approx(10.0));
  // 100 ft out to a 4 in half-column needs 25 ft per inch, so the next step, 50, is the finest that fits.
  CHECK(alignment::FitSectionScale(true, 100.0, 0.0, 4.0, 8.0).feetPerInchAcross == Approx(50.0));
  // 9 ft of deviation in an 8.75 in half-plot needs about 1.03 ft per inch, so 2 is the finest that fits.
  CHECK(alignment::FitSectionScale(true, 0.0, 9.0, 4.0, 8.75).feetPerInchDown == Approx(2.0));
  // A title block too small for any room still gets a finite scale.
  CHECK(std::isfinite(alignment::FitSectionScale(true, 50.0, 50.0, 0.0, 0.0).feetPerInchAcross));
}

TEST_CASE("sample lines of an alignment that is gone are dropped (REQ-519.13)", "[req519][samplelines]") {
  alignment::Alignment road = NorthRoad(500.0);
  road.id = 7;
  alignment::SampleLine kept;
  kept.alignmentId = 7;
  alignment::SampleLine orphan;
  orphan.alignmentId = 9;
  std::vector<alignment::SampleLine> lines{kept, orphan};
  alignment::SyncSampleLines({road}, lines);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].alignmentId == 7);
}

TEST_CASE("sample sections: a crossing just past the coverage edge keeps its ground (REQ-519.2)",
          "[req519][samplelines]") {
  // A flat TIN over x in [-200, 200]. The swath runs east from (10, -13) on a bearing of 0.1021 rad, so it leaves the
  // square at x = 200, a distance of 190 / cos(az) along the swath. That crossing is a sample point, not a lost one.
  const std::vector<double> verts = {-200, -500, 100, 200, -500, 100, 200, 500, 100, -200, 500, 100};
  const std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
  const TinSurfaceQuery surface(verts, idx);
  const double az = 0.1021;
  alignment::SampleLine line;
  line.origin = alignment::Point2{10.0, -13.0};
  line.direction = alignment::Point2{std::cos(az), -std::sin(az)};
  line.leftWidth = 100.0;
  line.rightWidth = 300.0;
  alignment::SampleSection(verts, idx, surface, line);
  REQUIRE_FALSE(line.section.empty());
  CHECK(line.section.front().offset == Approx(-100.0));
  REQUIRE(line.section.back().offset == Approx(190.0 / std::cos(az)).margin(1e-6));
  CHECK(line.section.back().ground == Approx(100.0));
}

TEST_CASE("sample lines: an interval too fine for the station format is refused, not given repeated names",
          "[req519][samplelines]") {
  const alignment::Alignment road = NorthRoad(500.0);
  std::vector<alignment::SampleLine> lines;
  std::string why;
  CHECK_FALSE(alignment::ComputeSampleLines(road, alignment::SampleLineSettings{0.0, 0.003, 0.001, 50.0, 50.0},
                                            alignment::StationFormat{}, &lines, &why));
  CHECK(why.find("share a name") != std::string::npos);
  CHECK(lines.empty());
}
