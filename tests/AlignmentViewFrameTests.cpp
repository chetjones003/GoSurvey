// REQ-517 / GitHub issue #999 - view frames along an alignment: geometry, refusals, the typed command, regeneration
// with the alignment, delete, undo and save/reopen.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentViewFrames.hpp"

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

// A straight alignment 0+00 to 50+00 running due north (azimuth 0), in a feet drawing.
void NorthRoad(AppCommandState& st, std::vector<std::string>& log) {
  Submit(st, "ALIGNMENT Road", log);
  Submit(st, "ALIGNMENTADD Road, TANGENT, 0, 0, 0, 5000", log);
}

// Plan-view extent of a frame's corners along the x (east) and y (north) axes.
void Extent(const alignment::ViewFrame& f, double* minX, double* maxX, double* minY, double* maxY) {
  *minX = *maxX = f.corner[0].x;
  *minY = *maxY = f.corner[0].y;
  for (const alignment::Point2& p : f.corner) {
    *minX = std::min(*minX, p.x);
    *maxX = std::max(*maxX, p.x);
    *minY = std::min(*minY, p.y);
    *maxY = std::max(*maxY, p.y);
  }
}

alignment::ViewFrameSettings Settings(double start, double end, alignment::ViewFramePlacement p = alignment::ViewFramePlacement::Centred) {
  alignment::ViewFrameSettings s;
  s.startStation = start;
  s.endStation = end;
  s.sheetWidthIn = 36.0;   // along the alignment
  s.sheetHeightIn = 24.0;  // across
  s.groundPerInch = 50.0;  // 1 in = 50 ft
  s.overlap = 100.0;
  s.placement = p;
  return s;
}

}  // namespace

TEST_CASE("view frames: the REQ-517 acceptance run gives three frames, the last not shortened",
          "[req517][viewframes]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  REQUIRE(st.alignments.size() == 1);
  const std::vector<alignment::ViewFrame> frames =
      alignment::ComputeViewFrames(st.alignments[0], Settings(0.0, 5000.0));
  REQUIRE(frames.size() == 3);
  CHECK(frames[0].name == "VF-1");
  CHECK(frames[0].startStation == Approx(0.0));
  CHECK(frames[0].endStation == Approx(1800.0));
  CHECK(frames[1].startStation == Approx(1700.0));
  CHECK(frames[1].endStation == Approx(3500.0));
  CHECK(frames[2].startStation == Approx(3400.0));
  CHECK(frames[2].endStation == Approx(5200.0));  // past the end station, full length (REQ-517.5)
  CHECK(frames[2].length == Approx(1800.0));
  CHECK(frames[0].width == Approx(1200.0));      // 24 in x 50 ft (REQ-517.2)
}

TEST_CASE("view frames: placement puts the alignment on the centre, left or right edge", "[req517][viewframes]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  const alignment::Alignment& road = st.alignments[0];

  // Due north: right of travel is east, so a frame's across-range is its x range.
  double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;

  auto frame = [&](alignment::ViewFramePlacement p) {
    return alignment::ComputeViewFrames(road, Settings(0.0, 5000.0, p))[0];
  };

  Extent(frame(alignment::ViewFramePlacement::Centred), &minX, &maxX, &minY, &maxY);
  CHECK(minX == Approx(-600.0));
  CHECK(maxX == Approx(600.0));

  Extent(frame(alignment::ViewFramePlacement::Left), &minX, &maxX, &minY, &maxY);
  CHECK(minX == Approx(0.0));    // alignment on the frame's left edge: frame lies east of it
  CHECK(maxX == Approx(1200.0));

  Extent(frame(alignment::ViewFramePlacement::Right), &minX, &maxX, &minY, &maxY);
  CHECK(minX == Approx(-1200.0));  // alignment on the right edge: frame lies west of it
  CHECK(maxX == Approx(0.0));

  // Along the alignment the frame is 1800 ft long, centred on its own centre station (900 ft in).
  Extent(frame(alignment::ViewFramePlacement::Centred), &minX, &maxX, &minY, &maxY);
  CHECK(maxY - minY == Approx(1800.0));
  CHECK(frame(alignment::ViewFramePlacement::Centred).centre.y == Approx(900.0));
}

TEST_CASE("view frames: each refusal names its reason and creates nothing", "[req517][viewframes]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  const alignment::Alignment& road = st.alignments[0];

  CHECK(alignment::ViewFrameSettingsRefusal(road, Settings(3000.0, 1000.0)).find("reversed") != std::string::npos);
  CHECK(alignment::ViewFrameSettingsRefusal(road, Settings(0.0, 6000.0)).find("outside") != std::string::npos);

  alignment::ViewFrameSettings wide = Settings(0.0, 5000.0);
  wide.overlap = 1800.0;  // not less than the 1800 ft frame
  CHECK(alignment::ViewFrameSettingsRefusal(road, wide).find("overlap") != std::string::npos);
  CHECK(alignment::ComputeViewFrames(road, wide).empty());

  alignment::ViewFrameSettings tiny = Settings(0.0, 5000.0);
  tiny.overlap = 1799.9;  // 1800 - 1799.9 = 0.1 ft steps: too many frames
  CHECK(alignment::ViewFrameSettingsRefusal(road, tiny).find("more than") != std::string::npos);

  alignment::Alignment empty;
  CHECK(alignment::ViewFrameSettingsRefusal(empty, Settings(0.0, 5000.0)).find("no geometry") != std::string::npos);
}

TEST_CASE("CREATEVIEWFRAMES builds a group of frames on the drawn alignment", "[req517][viewframes]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 100", log);
  REQUIRE(LogHas(log, "3 view frame(s)"));
  REQUIRE(st.viewFrameGroups.size() == 1);
  const alignment::ViewFrameGroup& g = st.viewFrameGroups[0];
  CHECK(g.name == "Road");
  CHECK(g.alignmentId == st.alignments[0].id);
  CHECK(g.frames.size() == 3);
  CHECK(g.settings.sheetName.find("ARCH D") == 0);
  CHECK(g.settings.scaleName == "1\" = 50'");  // the drawing's own scale name
}

TEST_CASE("CREATEVIEWFRAMES refuses an unnamed scale, an unknown sheet and a bad placement", "[req517][viewframes]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);

  Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 37', 100", log);
  CHECK(LogHas(log, "not a named scale"));
  Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, NO SUCH SHEET, 1\" = 50', 100", log);
  CHECK(LogHas(log, "no sheet size"));
  Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 100, SIDEWAYS", log);
  CHECK(LogHas(log, "placement must be"));
  Submit(st, "CREATEVIEWFRAMES Road, 50+00, 0+00, ARCH D, 1\" = 50', 100", log);
  CHECK(LogHas(log, "reversed"));

  CHECK(st.viewFrameGroups.empty());
}

TEST_CASE("view frames follow the alignment and are removed with it", "[req517][viewframes]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 100", log);
  REQUIRE(st.viewFrameGroups.size() == 1);
  const double firstCentreX = st.viewFrameGroups[0].frames[0].centre.x;

  // Turn the first entity to due east: the frames are regenerated along the new bearing (REQ-517.8).
  Submit(st, "ALIGNMENTEDIT Road, 1, BEARING, 90", log);
  REQUIRE(st.viewFrameGroups.size() == 1);
  CHECK(st.viewFrameGroups[0].frames[0].centre.x != Approx(firstCentreX));
  CHECK(st.viewFrameGroups[0].frames[0].centre.y == Approx(0.0).margin(1e-6));

  // Undo restores the old alignment and the old frames together (one undo step).
  Submit(st, "UNDO", log);
  REQUIRE(st.viewFrameGroups.size() == 1);
  CHECK(st.viewFrameGroups[0].frames[0].centre.x == Approx(firstCentreX));

  // Deleting the alignment deletes its frame groups.
  Submit(st, "ALIGNMENTDELETE Road", log);
  CHECK(st.alignments.empty());
  CHECK(st.viewFrameGroups.empty());

  // ...and one undo brings both back.
  Submit(st, "UNDO", log);
  CHECK(st.alignments.size() == 1);
  CHECK(st.viewFrameGroups.size() == 1);
}

TEST_CASE("view frame groups survive save and reopen with their frames rebuilt", "[req517][viewframes]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoad(st, log);
  Submit(st, "CREATEVIEWFRAMES Road, 0+00, 50+00, ARCH D, 1\" = 50', 100, LEFT", log);
  REQUIRE(st.viewFrameGroups.size() == 1);

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.viewFrameGroups.size() == 1);
  const alignment::ViewFrameGroup& g = back.viewFrameGroups[0];
  CHECK(g.id == st.viewFrameGroups[0].id);
  CHECK(g.alignmentId == back.alignments[0].id);
  CHECK(g.settings.placement == alignment::ViewFramePlacement::Left);
  REQUIRE(g.frames.size() == 3);
  CHECK(g.frames[2].endStation == Approx(5200.0));
}
