// REQ-501 / GitHub issue #121 Phase 5 - editable alignment properties, COGO along an alignment, and the cursor readout.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "alignment/Alignment.hpp"
#include <GoSurvey/Core/NumFormat.hpp>

using Catch::Approx;

namespace {

constexpr double kDeg = alignment::kPi / 180.0;

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[1024];
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
  st.displayLinearPrecision = 4;
  return st;
}

/// A 100-unit tangent north, then a 90 degree right curve of radius 100: the curve starts at station 1+00 and ends at
/// station 2+57.0796 (100 + 157.0796).
void MakeCurvedRoad(AppCommandState& st, std::vector<std::string>& log) {
  Submit(st, "ALIGNMENT CR1, , 0+00", log);
  Submit(st, "ALIGNMENTADD CR1, TANGENT, 0, 0, 0, 100", log);
  Submit(st, "ALIGNMENTADD CR1, CURVE, 100, 90, RIGHT", log);
}

}  // namespace

TEST_CASE("Properties fields rename and edit the alignment, with uniqueness and the style rule (REQ-501 AC1)",
          "[req501][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurvedRoad(st, log);
  Submit(st, "ALIGNMENT CR2, , 0+00", log);
  log.clear();
  Submit(st, "ALIGNMENTPROPS CR1, NAME, Main Road", log);
  CHECK(st.alignments[0].name == "Main Road");
  CHECK(LogHas(log, "set as the NAME"));

  log.clear();
  Submit(st, "ALIGNMENTPROPS Main Road, NAME, cr2", log);  // a name already in use is refused
  CHECK(LogHas(log, "already exists"));
  CHECK(st.alignments[0].name == "Main Road");

  Submit(st, "ALIGNMENTPROPS Main Road, DESCRIPTION, Two lane county road", log);
  CHECK(st.alignments[0].description == "Two lane county road");
  Submit(st, "ALIGNMENTPROPS Main Road, LAYER, ROADS", log);
  CHECK(st.alignments[0].layer == "ROADS");

  log.clear();
  Submit(st, "ALIGNMENTPROPS Main Road, STYLE, NoSuchStyle", log);
  CHECK(LogHas(log, "no style named"));
  Submit(st, "ALIGNMENTSTYLE NEW, Wide", log);
  Submit(st, "ALIGNMENTPROPS Main Road, STYLE, Wide", log);
  CHECK(st.alignments[0].style == "Wide");

  // The start station moves the stations, so it is refused while station equations would be misplaced.
  Submit(st, "ALIGNMENTSTATION Main Road, EQ, ADD, 1+50, 1+60, INC", log);
  log.clear();
  Submit(st, "ALIGNMENTPROPS Main Road, START, 5+00", log);
  CHECK(LogHas(log, "station equation"));
  CHECK(st.alignments[0].startingStation == Approx(0.0));

  Submit(st, "ALIGNMENTSTATION Main Road, START, 0+00, CLEAR", log);
  Submit(st, "ALIGNMENTPROPS Main Road, START, 5+00", log);
  CHECK(st.alignments[0].startingStation == Approx(500.0));

  // A value already set changes nothing and takes no undo step.
  log.clear();
  Submit(st, "ALIGNMENTPROPS Main Road, LAYER, ROADS", log);
  CHECK(LogHas(log, "already"));
}

TEST_CASE("Inverse along an alignment gives the arc length for a curve and the chord on request (REQ-501 AC3)",
          "[req501][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurvedRoad(st, log);
  log.clear();
  Submit(st, "ALIGNMENTINVERSE CR1, STA, 1+00, 2+57.0796", log);
  REQUIRE(LogHas(log, "distance along 157.0796"));  // the arc: 100 * pi/2
  CHECK_FALSE(LogHas(log, "chord"));

  log.clear();
  Submit(st, "ALIGNMENTINVERSE CR1, STA, 1+00, 2+57.0796, CHORD", log);
  CHECK(LogHas(log, "distance along 157.0796"));
  CHECK(LogHas(log, "chord 141.421"));  // 2 * 100 * sin(45 deg) = 141.42136; the station given is a hair short of the true end

  log.clear();
  Submit(st, "ALIGNMENTINVERSE CR1, STA, 0+00, 9+00", log);
  CHECK(LogHas(log, "outside"));
}

TEST_CASE("Inverse between two points projects them onto the alignment (REQ-501.3)", "[req501][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurvedRoad(st, log);
  log.clear();
  // (2, 40) is 2 right of the first tangent, at station 0+40; (0, 90) is on the first tangent at 0+90.
  Submit(st, "ALIGNMENTINVERSE CR1, PT, 2, 40, 0, 90", log);
  CHECK(LogHas(log, "from station 0+40.00 to 0+90.00"));
  CHECK(LogHas(log, "distance along 50.0000"));
}

TEST_CASE("Bearing at a station and distance along to a point (REQ-501.3)", "[req501][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurvedRoad(st, log);
  log.clear();
  Submit(st, "ALIGNMENTBEARING CR1, 0+50", log);
  CHECK(LogHas(log, "at 0+50.00"));
  CHECK_FALSE(LogHas(log, "outside"));
  log.clear();
  Submit(st, "ALIGNMENTDISTANCE CR1, 2, 40", log);
  CHECK(LogHas(log, "40.0000 along"));
  CHECK(LogHas(log, "at station 0+40.00"));
}

TEST_CASE("GETSTATIONOFFSET takes a survey point by its number (REQ-501.3)", "[req501][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurvedRoad(st, log);
  SurveyPoint sp;
  sp.id = 7;
  sp.easting = 3.0;  // 3 east of the north-running tangent: 3 to the right of travel
  sp.northing = 30.0;
  st.surveyPoints.push_back(sp);
  log.clear();
  Submit(st, "GETSTATIONOFFSET CR1, POINT, 7", log);
  CHECK(LogHas(log, "Station = 0+30.00"));
  CHECK(LogHas(log, "Offset = 3.0000"));
  log.clear();
  Submit(st, "GETSTATIONOFFSET CR1, POINT, 99", log);
  CHECK(LogHas(log, "no survey point numbered 99"));
}

TEST_CASE("The cursor readout is the station and offset that GetStationOffset gives at the cursor (REQ-501 AC2)",
          "[req501][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurvedRoad(st, log);
  // A point 40 along and 3 to the right of the first tangent.
  const double x = 3.0, y = 40.0;
  const alignment::StationOffset so = st.alignments[0].GetStationOffset(x, y);
  const std::string readout = AlignmentCursorReadout(st, x, y, 50.0);
  REQUIRE_FALSE(readout.empty());
  CHECK(readout.find(alignment::FormatStation(st.alignments[0].DisplayedStation(so.internalStation, true))) != std::string::npos);
  CHECK(readout.find("OFF " + FormatLinear(so.offset, st.displayLinearPrecision)) != std::string::npos);
  // Nothing within the reach of the cursor means no readout.
  CHECK(AlignmentCursorReadout(st, 500.0, 500.0, 5.0).empty());
}
