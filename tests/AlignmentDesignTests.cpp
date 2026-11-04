// REQ-502 / GitHub issue #121 Phase 6 - design criteria and checks: check sets, the speed-to-radius rule, the
// road/rail/miscellaneous rules, and the save-and-reload of each.

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentDesign.hpp"

namespace {

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
  st.displayLinearPrecision = 2;
  return st;
}

/// A 100-unit tangent north, then a 90 degree right curve of radius 250. The curve runs from station 1+00 to 4+92.70.
void MakeRoadWithCurve(AppCommandState& st, std::vector<std::string>& log) {
  Submit(st, "ALIGNMENT CR1, , 0+00", log);
  Submit(st, "ALIGNMENTADD CR1, TANGENT, 0, 0, 0, 100", log);
  Submit(st, "ALIGNMENTADD CR1, CURVE, 250, 90, RIGHT", log);
}

}  // namespace

TEST_CASE("A curve below the minimum radius for its design speed is reported and the alignment is kept (REQ-502 AC1)",
          "[req502][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeRoadWithCurve(st, log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, RADIUS, 40, 400", log);
  Submit(st, "ALIGNMENTSPEED CR1, 0+00, 40", log);
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK(LogHas(log, "Curve radius 250.00' is below the selected minimum design radius of 400.00'."));
  CHECK(st.alignments[0].Entities().size() == 2);  // the finding does not block anything

  // The finding survives a save and reload, and so do the criteria and the speed.
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignments.size() == 1);
  CHECK(back.alignments[0].checkSetName == "Rural");
  REQUIRE(back.alignments[0].designSpeeds.size() == 1);
  CHECK(back.alignments[0].designSpeeds[0].speed == 40.0);
  REQUIRE(back.alignmentCheckSets.size() == 1);
  CHECK(back.alignmentCheckSets[0] == st.alignmentCheckSets[0]);
  CHECK_FALSE(LogHas(loadLog, "missing check set"));
}

TEST_CASE("The highest design speed across an entity governs its check (REQ-502 AC2)", "[req502][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeRoadWithCurve(st, log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, RADIUS, 40, 200", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, RADIUS, 60, 400", log);
  Submit(st, "ALIGNMENTSPEED CR1, 0+00, 40", log);
  Submit(st, "ALIGNMENTSPEED CR1, 2+00, 60", log);  // 2+00 falls inside the curve, which runs 1+00 to 4+92.70
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK(LogHas(log, "minimum design radius of 400.00'"));  // the 60 mph requirement, not the 40 at the start
  CHECK_FALSE(LogHas(log, "minimum design radius of 200.00'"));
}

TEST_CASE("The radius table gives the largest tabled speed not above the design speed", "[req502][alignment]") {
  alignment::AlignmentCheckSet set;
  set.radiusTable = {{40.0, 200.0}, {60.0, 400.0}};
  CHECK(alignment::MinRadiusForSpeed(set, 50.0) == 200.0);
  CHECK(alignment::MinRadiusForSpeed(set, 60.0) == 400.0);
  CHECK(alignment::MinRadiusForSpeed(set, 30.0) < 0.0);  // below the table, it says nothing
}

TEST_CASE("Miscellaneous alignments take no criteria and rail alignments take only a rail set (REQ-502 AC3)",
          "[req502][alignment]") {
  std::string why;
  CHECK_FALSE(alignment::CriteriaAllowed(alignment::AlignmentType::Miscellaneous, alignment::CheckSetKind::Road, &why));
  CHECK(why.find("Miscellaneous") != std::string::npos);
  CHECK_FALSE(alignment::CriteriaAllowed(alignment::AlignmentType::Rail, alignment::CheckSetKind::Road, &why));
  CHECK(why.find("Rail") != std::string::npos);
  CHECK(alignment::CriteriaAllowed(alignment::AlignmentType::Rail, alignment::CheckSetKind::Rail, &why));
  CHECK(alignment::CriteriaAllowed(alignment::AlignmentType::Centerline, alignment::CheckSetKind::Road, &why));
  CHECK_FALSE(alignment::CriteriaAllowed(alignment::AlignmentType::Centerline, alignment::CheckSetKind::Rail, &why));

  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeRoadWithCurve(st, log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Track, RAIL", log);

  st.alignments[0].type = alignment::AlignmentType::Miscellaneous;
  log.clear();
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  CHECK(LogHas(log, "Miscellaneous"));
  CHECK(st.alignments[0].checkSetName.empty());

  st.alignments[0].type = alignment::AlignmentType::Rail;
  log.clear();
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  CHECK(LogHas(log, "a Rail alignment cannot use a road check set"));
  CHECK(st.alignments[0].checkSetName.empty());
  Submit(st, "ALIGNMENTCRITERIA CR1, Track", log);
  CHECK(st.alignments[0].checkSetName == "Track");

  st.alignments[0].type = alignment::AlignmentType::Centerline;
  log.clear();
  Submit(st, "ALIGNMENTCRITERIA CR1, Track", log);
  CHECK(LogHas(log, "only a Rail alignment can use a rail check set"));
}

TEST_CASE("Deleting a check set clears it from the alignments that used it (REQ-502)", "[req502][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeRoadWithCurve(st, log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  REQUIRE(st.alignments[0].checkSetName == "Rural");
  Submit(st, "ALIGNMENTCHECKSET DELETE, Rural", log);
  CHECK(st.alignmentCheckSets.empty());
  CHECK(st.alignments[0].checkSetName.empty());
}

TEST_CASE("A saved check set that is no longer in the drawing is reported on load (REQ-502)", "[req502][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeRoadWithCurve(st, log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  st.alignmentCheckSets.clear();  // the set goes, the alignment's reference to it stays
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  CHECK(back.alignments[0].checkSetName.empty());
  CHECK(LogHas(loadLog, "missing check set \"Rural\""));
}

TEST_CASE("ALIGNMENTCHECKSET SET rejects bad values and leaves the set unchanged (REQ-502)", "[req502][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, RADIUS, 40, -5", log);
  CHECK(LogHas(log, "usage"));
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, MINA, abc", log);
  CHECK(LogHas(log, "takes one value"));
  CHECK(st.alignmentCheckSets[0].radiusTable.empty());
  CHECK(st.alignmentCheckSets[0].minA == 0.0);
}
