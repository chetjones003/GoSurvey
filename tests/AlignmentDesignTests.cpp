// REQ-502 / GitHub issue #121 Phase 6 - design criteria and checks: check sets, the speed-to-radius rule, the
// road/rail/miscellaneous rules, and the save-and-reload of each.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
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

TEST_CASE("FindCheckSet matches names without regard to capitalisation (REQ-502)", "[req502][alignment]") {
  std::vector<alignment::AlignmentCheckSet> sets(1);
  sets[0].name = "Rural";
  CHECK(alignment::FindCheckSet(sets, "rural") == &sets[0]);
  CHECK(alignment::FindCheckSet(sets, "Urban") == nullptr);
  CHECK(alignment::FindCheckSet(sets, "") == nullptr);
}

TEST_CASE("ALIGNMENTLIST warns about design failures and the report shows the check assumptions (REQ-502, REQ-522)",
          "[req502][req522][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeRoadWithCurve(st, log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, RADIUS, 40, 400", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SUPERELEVATION, 0.06", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SIGHT, 300", log);
  Submit(st, "ALIGNMENTSPEED CR1, 0+00, 40", log);
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);

  log.clear();
  Submit(st, "ALIGNMENTLIST CR1", log);
  CHECK(LogHas(log, "WARNING: design check, entity 2: Curve radius 250.00' is below the selected minimum design radius of 400.00'."));

  log.clear();
  Submit(st, "ALIGNMENTREPORT CR1", log);
  CHECK(LogHas(log, "Superelevation: maximum 0.0600, side friction 0.0000, circular curves only; a curve with no design speed is not checked."));
  CHECK(LogHas(log, "Sight distance: not checked (no minimum sight distance or no clearance)."));

  REQUIRE(st.alignmentCheckSets.size() == 1);
  CHECK(st.alignmentCheckSets[0].maxSuperelevation == 0.06);
  CHECK(st.alignmentCheckSets[0].minSightDistance == 300.0);

  // The stubs survive a save and reload, as the check set does.
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignmentCheckSets.size() == 1);
  CHECK(back.alignmentCheckSets[0] == st.alignmentCheckSets[0]);
}

TEST_CASE("A style's check set applies unless the alignment has its own (REQ-502 / D-2026-11-04-b)", "[req502][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeRoadWithCurve(st, log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, RADIUS, 40, 400", log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Lenient", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Lenient, RADIUS, 40, 100", log);
  Submit(st, "ALIGNMENTSPEED CR1, 0+00, 40", log);
  Submit(st, "ALIGNMENTSTYLE NEW, Wide", log);
  Submit(st, "ALIGNMENTSTYLE SET, Wide, CHECKSET, Rural", log);
  Submit(st, "ALIGNMENTSTYLE ASSIGN, CR1, Wide", log);

  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK(LogHas(log, "minimum design radius of 400.00'"));

  // The alignment's own set overrides its style.
  Submit(st, "ALIGNMENTCRITERIA CR1, Lenient", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK(LogHas(log, "meets \"Lenient\""));

  // A style can only name a set that exists.
  log.clear();
  Submit(st, "ALIGNMENTSTYLE SET, Wide, CHECKSET, Nope", log);
  CHECK(LogHas(log, "is not valid"));
}

TEST_CASE("A minimum line length flags short tangents, and zero switches it off (REQ-502 line check)", "[req502][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeRoadWithCurve(st, log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, MINLINE, 150", log);
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK(LogHas(log, "Entity 1: Line 100.00' is shorter than the minimum line length of 150.00'."));

  log.clear();
  Submit(st, "ALIGNMENTLIST CR1", log);
  CHECK(LogHas(log, "WARNING: design check, entity 1: Line 100.00'"));

  Submit(st, "ALIGNMENTCHECKSET SET, Rural, MINLINE, 0", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK(LogHas(log, "meets \"Rural\""));
}

TEST_CASE("A style's missing check set is reported and cleared on load (REQ-502)", "[req502][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeRoadWithCurve(st, log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTSTYLE NEW, Wide", log);
  Submit(st, "ALIGNMENTSTYLE SET, Wide, CHECKSET, Rural", log);
  REQUIRE(st.alignmentStyles.back().checkSetName == "Rural");
  st.alignmentCheckSets.clear();  // the set goes, the style's reference stays
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  CHECK(LogHas(loadLog, "style \"Wide\" references missing check set \"Rural\""));
  for (const AlignmentStyle& s : back.alignmentStyles) {
    if (s.name == "Wide") CHECK(s.checkSetName.empty());
  }
}

// REQ-522 / GitHub issue #1164: superelevation and sight-distance checks on circular curves.

namespace {

/// A 100-unit northbound tangent, then a right curve of radius `radius` and central angle `delta` degrees.
void MakeCurveOf(AppCommandState& st, const std::string& radius, const std::string& delta, std::vector<std::string>& log) {
  Submit(st, "ALIGNMENT CR1, , 0+00", log);
  Submit(st, "ALIGNMENTADD CR1, TANGENT, 0, 0, 0, 100", log);
  Submit(st, "ALIGNMENTADD CR1, CURVE, " + radius + ", " + delta + ", RIGHT", log);
}

}  // namespace

TEST_CASE("Superelevation is required e = V^2/(gR) - f and fails above the maximum rate (REQ-522 AC1)", "[req522][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurveOf(st, "1000", "30", log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SUPERELEVATION, 0.06", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, FRICTION, 0.10", log);
  Submit(st, "ALIGNMENTSPEED CR1, 0+00, 60", log);
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);

  // 60 mph is 88 ft/s: 88^2 / (32.174 * 1000) - 0.10 = 0.14069.
  CHECK(std::fabs(alignment::RequiredSuperelevation(60.0, 1000.0, 0.10) - 0.1406912) < 1e-6);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK(LogHas(log, "Required superelevation 0.1407 exceeds the maximum of 0.0600 at design speed 60 mph with side friction 0.1000."));

  // A maximum above the required rate passes, and so does one that equals it at display precision.
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SUPERELEVATION, 0.15", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK_FALSE(LogHas(log, "Required superelevation"));
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SUPERELEVATION, 0.1407", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK_FALSE(LogHas(log, "Required superelevation"));

  // A required rate of 0.140707 displays as 0.1407, so a maximum of 0.1407 passes (display rounding, REQ-522 AC1).
  CHECK(std::fabs(alignment::RequiredSuperelevation(60.0, 999.9, 0.10) - 0.140707) < 1e-5);
  AppCommandState rounded = Drawing();
  std::vector<std::string> roundedLog;
  MakeCurveOf(rounded, "999.9", "30", roundedLog);
  Submit(rounded, "ALIGNMENTCHECKSET NEW, Rural", roundedLog);
  Submit(rounded, "ALIGNMENTCHECKSET SET, Rural, SUPERELEVATION, 0.1407", roundedLog);
  Submit(rounded, "ALIGNMENTCHECKSET SET, Rural, FRICTION, 0.10", roundedLog);
  Submit(rounded, "ALIGNMENTSPEED CR1, 0+00, 60", roundedLog);
  Submit(rounded, "ALIGNMENTCRITERIA CR1, Rural", roundedLog);
  roundedLog.clear();
  Submit(rounded, "ALIGNMENTCHECK CR1", roundedLog);
  CHECK_FALSE(LogHas(roundedLog, "Required superelevation"));
}

TEST_CASE("Tangents and spirals are never checked for superelevation or sight distance (REQ-522 AC5)", "[req522][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurveOf(st, "500", "90", log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SUPERELEVATION, 0.01", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SIGHT, 5000", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, CLEARANCE, 25", log);
  Submit(st, "ALIGNMENTSPEED CR1, 0+00, 80", log);
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  // The tangent (entity 1) breaks neither limit; only the curve (entity 2) is reported.
  CHECK(LogHas(log, "Entity 2: Required superelevation"));
  CHECK(LogHas(log, "Entity 2: Sight distance"));
  CHECK_FALSE(LogHas(log, "Entity 1:"));
}

TEST_CASE("The REQ-522 findings reach LIST and the report, and a reloaded drawing gives the same findings (REQ-522 AC7, AC8)",
          "[req522][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurveOf(st, "1000", "30", log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SUPERELEVATION, 0.06", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, FRICTION, 0.10", log);
  Submit(st, "ALIGNMENTSPEED CR1, 0+00, 60", log);
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  log.clear();
  Submit(st, "ALIGNMENTLIST CR1", log);
  CHECK(LogHas(log, "Required superelevation 0.1407 exceeds the maximum of 0.0600"));
  log.clear();
  Submit(st, "ALIGNMENTREPORT CR1", log);
  CHECK(LogHas(log, "Required superelevation 0.1407 exceeds the maximum of 0.0600"));

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  std::vector<std::string> backLog;
  Submit(back, "ALIGNMENTCHECK CR1", backLog);
  CHECK(LogHas(backLog, "Required superelevation 0.1407 exceeds the maximum of 0.0600"));

  // A drawing that never set the new values saves without their keys, as a file from before REQ-522 does: the checks load off.
  AppCommandState legacy = Drawing();
  std::vector<std::string> legacyLog;
  Submit(legacy, "ALIGNMENTCHECKSET NEW, Plain", legacyLog);
  const std::string legacyJson = SerializeGoSurveyJson(legacy);
  AppCommandState legacyBack = Drawing();
  std::vector<std::string> legacyLoadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(legacyBack, legacyJson, legacyLoadLog));
  REQUIRE(legacyBack.alignmentCheckSets.size() == 1);
  CHECK(legacyBack.alignmentCheckSets[0].sideFriction == 0.0);
  CHECK(legacyBack.alignmentCheckSets[0].sightClearance == 0.0);
}

TEST_CASE("Superelevation uses the highest design speed across the curve and is off with no maximum (REQ-522 AC4)",
          "[req522][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurveOf(st, "1000", "30", log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SUPERELEVATION, 0.06", log);
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);

  // No design speed at all: the curve is not checked.
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK_FALSE(LogHas(log, "Required superelevation"));

  // 30 mph at the start, then 60 mph inside the curve's span: the curve is checked at 60.
  Submit(st, "ALIGNMENTSPEED CR1, 0+00, 30", log);
  Submit(st, "ALIGNMENTSPEED CR1, 1+50, 60", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK(LogHas(log, "at design speed 60 mph"));

  // A maximum rate of zero switches the check off.
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SUPERELEVATION, 0", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK_FALSE(LogHas(log, "Required superelevation"));
}

TEST_CASE("Sight distance is 2R acos(1 - m/R), capped at the curve length, and fails below the minimum (REQ-522 AC2)",
          "[req522][alignment]") {
  // R = 500 and m = 25 give 317.56 ft; m = R gives the arc length, not the half circle.
  CHECK(std::fabs(alignment::AvailableSightDistance(500.0, 785.398, 25.0) - 317.5604) < 1e-3);
  CHECK(std::fabs(alignment::AvailableSightDistance(500.0, 785.398, 500.0) - 785.398) < 1e-3);
  CHECK(std::fabs(alignment::AvailableSightDistance(500.0, 1000.0, 500.0) - 1000.0) < 1e-3);

  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurveOf(st, "500", "90", log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SIGHT, 300", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, CLEARANCE, 25", log);
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK_FALSE(LogHas(log, "Sight distance"));  // 317.56 ft clears 300 ft

  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SIGHT, 350", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK(LogHas(log, "Sight distance 317.56' is below the minimum of 350.00' with a sight clearance of 25.00'."));

  // A 20 degree curve is shorter than the formula's distance, so its arc length is the sight distance counted.
  AppCommandState shortCurve = Drawing();
  std::vector<std::string> shortLog;
  MakeCurveOf(shortCurve, "500", "20", shortLog);
  Submit(shortCurve, "ALIGNMENTCHECKSET NEW, Rural", shortLog);
  Submit(shortCurve, "ALIGNMENTCHECKSET SET, Rural, SIGHT, 200", shortLog);
  Submit(shortCurve, "ALIGNMENTCHECKSET SET, Rural, CLEARANCE, 25", shortLog);
  Submit(shortCurve, "ALIGNMENTCRITERIA CR1, Rural", shortLog);
  shortLog.clear();
  Submit(shortCurve, "ALIGNMENTCHECK CR1", shortLog);
  CHECK(LogHas(shortLog, "Sight distance 174.53' is below the minimum of 200.00' with a sight clearance of 25.00'."));
}

TEST_CASE("The sight check is off without a clearance, and the criteria column shows the limits (REQ-522 AC3, AC6)",
          "[req522][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurveOf(st, "500", "90", log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SIGHT, 350", log);  // no clearance
  Submit(st, "ALIGNMENTCRITERIA CR1, Rural", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECK CR1", log);
  CHECK_FALSE(LogHas(log, "Sight distance"));

  Submit(st, "ALIGNMENTCHECKSET SET, Rural, CLEARANCE, 25", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, SUPERELEVATION, 0.06", log);
  Submit(st, "ALIGNMENTSPEED CR1, 0+00, 60", log);  // the curve has a design speed, so its e limit is shown
  REQUIRE(st.alignmentCheckSets.size() == 1);
  const alignment::AlignmentCheckSet& set = st.alignmentCheckSets[0];
  // Entity 2 is the curve: its limits are the maximum rate and the minimum sight distance. Entity 1 is the tangent.
  CHECK(alignment::CriteriaRequirementText(st.alignments[0], set, 1, 2) == "e \xE2\x89\xA4 0.0600, S \xE2\x89\xA5 350.00'");
  CHECK(alignment::CriteriaRequirementText(st.alignments[0], set, 0, 2).empty());
}

TEST_CASE("Negative friction or clearance is refused and the set is unchanged (REQ-522 AC6)", "[req522][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  log.clear();
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, FRICTION, -0.1", log);
  CHECK(LogHas(log, "takes one value, zero or more"));
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, CLEARANCE, -5", log);
  CHECK(LogHas(log, "takes one value, zero or more"));
  CHECK(st.alignmentCheckSets[0].sideFriction == 0.0);
  CHECK(st.alignmentCheckSets[0].sightClearance == 0.0);
}

TEST_CASE("Friction and clearance save and reload with the set; a drawing without them omits them (REQ-522 AC7)",
          "[req522][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  MakeCurveOf(st, "1000", "30", log);
  Submit(st, "ALIGNMENTCHECKSET NEW, Rural", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, FRICTION, 0.10", log);
  Submit(st, "ALIGNMENTCHECKSET SET, Rural, CLEARANCE, 25", log);
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignmentCheckSets.size() == 1);
  CHECK(back.alignmentCheckSets[0].sideFriction == 0.10);
  CHECK(back.alignmentCheckSets[0].sightClearance == 25.0);
  CHECK(back.alignmentCheckSets[0] == st.alignmentCheckSets[0]);

  AppCommandState plain = Drawing();
  std::vector<std::string> plainLog;
  Submit(plain, "ALIGNMENTCHECKSET NEW, Plain", plainLog);
  const std::string plainJson = SerializeGoSurveyJson(plain);
  CHECK(plainJson.find("sideFriction") == std::string::npos);
  CHECK(plainJson.find("sightClearance") == std::string::npos);
}
