// REQ-521 / GitHub issue #1163 - design vertical profiles: grades, parabolic vertical curves, validity, queries, the derived
// 3D design path, the PVI commands, undo, read-only refusal, and save/reopen with a deleted alignment.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentProfile.hpp"

using Catch::Approx;

namespace {

// A north-running alignment of `length` from (0, 0), with id 1.
alignment::Alignment NorthRoad(double length) {
  alignment::Alignment a;
  a.id = 1;
  a.name = "Road";
  a.AppendTangent(alignment::Point2{0.0, 0.0}, 0.0, length);
  return a;
}

// The REQ-521 acceptance profile on a 500 ft road: PVI at 0 (100), at 250 (110, 100 ft curve), at 500 (100).
alignment::VerticalProfile AcceptanceProfile(std::uint64_t alignmentId) {
  alignment::VerticalProfile p;
  p.id = 2;
  p.name = "Design";
  p.alignmentId = alignmentId;
  p.points = {{0.0, 100.0, 0.0}, {250.0, 110.0, 100.0}, {500.0, 100.0, 0.0}};
  return p;
}

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

bool LogHas(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& line : log)
    if (line.find(needle) != std::string::npos) return true;
  return false;
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  return st;
}

void NorthRoadCommand(AppCommandState& st, std::vector<std::string>& log) {
  Submit(st, "ALIGNMENT Road", log);
  Submit(st, "ALIGNMENTADD Road, TANGENT, 0, 0, 0, 500", log);
}

}  // namespace

// ---- domain: geometry (REQ-521.2, .4, acceptance 1) ----

TEST_CASE("profile: tangents and the parabolic curve match the REQ-521 acceptance numbers", "[req521][profile]") {
  const alignment::VerticalProfile p = AcceptanceProfile(1);
  REQUIRE(alignment::ProfileGeometryRefusal(p).empty());

  const alignment::ProfileSample s50 = alignment::ProfileAtStation(p, 50.0);
  REQUIRE(s50.hasValue);
  CHECK(s50.elevation == Approx(102.0));
  CHECK(s50.grade == Approx(0.04));

  const alignment::ProfileSample s200 = alignment::ProfileAtStation(p, 200.0);  // curve starts
  REQUIRE(s200.hasValue);
  CHECK(s200.elevation == Approx(108.0));
  CHECK(s200.grade == Approx(0.04));

  const alignment::ProfileSample s250 = alignment::ProfileAtStation(p, 250.0);  // the PVI: the curve is 1 ft below it
  REQUIRE(s250.hasValue);
  CHECK(s250.elevation == Approx(109.0));
  CHECK(s250.grade == Approx(0.0).margin(1e-12));

  const alignment::ProfileSample s300 = alignment::ProfileAtStation(p, 300.0);  // curve ends
  REQUIRE(s300.hasValue);
  CHECK(s300.elevation == Approx(108.0));
  CHECK(s300.grade == Approx(-0.04));

  const alignment::ProfileSample s400 = alignment::ProfileAtStation(p, 400.0);
  REQUIRE(s400.hasValue);
  CHECK(s400.elevation == Approx(104.0));
  CHECK(s400.grade == Approx(-0.04));
}

TEST_CASE("profile: the curve grade changes linearly and its elevation is the parabola", "[req521][profile]") {
  const alignment::VerticalProfile p = AcceptanceProfile(1);
  // x = 25 from the curve start: E = 108 + 0.04*25 - 0.08*625/200 = 108.75; G = 0.04 - 0.08*25/100 = 0.02.
  const alignment::ProfileSample s = alignment::ProfileAtStation(p, 225.0);
  REQUIRE(s.hasValue);
  CHECK(s.elevation == Approx(108.75));
  CHECK(s.grade == Approx(0.02));
}

TEST_CASE("profile: a PVI with no curve reports the grade of the tangent that starts there", "[req521][profile]") {
  alignment::VerticalProfile p = AcceptanceProfile(1);
  p.points[1].curveLength = 0.0;  // a sharp corner at 250
  const alignment::ProfileSample at = alignment::ProfileAtStation(p, 250.0);
  REQUIRE(at.hasValue);
  CHECK(at.elevation == Approx(110.0));
  CHECK(at.grade == Approx(-0.04));
  const alignment::ProfileSample mid = alignment::ProfileAtStation(p, 125.0);
  REQUIRE(mid.hasValue);
  CHECK(mid.elevation == Approx(105.0));
  CHECK(mid.grade == Approx(0.04));
}

// ---- domain: queries outside, and no geometry (REQ-521.4, acceptance 3) ----

TEST_CASE("profile: stations outside the first and last PVI give no value", "[req521][profile]") {
  const alignment::VerticalProfile p = AcceptanceProfile(1);
  const alignment::ProfileSample before = alignment::ProfileAtStation(p, -1.0);
  CHECK_FALSE(before.hasValue);
  CHECK(before.reason == "outside the profile");
  const alignment::ProfileSample after = alignment::ProfileAtStation(p, 501.0);
  CHECK_FALSE(after.hasValue);
  CHECK(after.reason == "outside the profile");
}

TEST_CASE("profile: fewer than two PVIs has no geometry", "[req521][profile]") {
  alignment::VerticalProfile p = AcceptanceProfile(1);
  p.points.resize(1);
  CHECK(alignment::ProfileGeometryRefusal(p).empty());
  const alignment::ProfileSample s = alignment::ProfileAtStation(p, 0.0);
  CHECK_FALSE(s.hasValue);
  CHECK(s.reason.find("no geometry") != std::string::npos);

  alignment::VerticalProfile empty;
  CHECK_FALSE(alignment::ProfileAtStation(empty, 0.0).hasValue);
}

// ---- domain: validity refusals (REQ-521.3, acceptance 4) ----

TEST_CASE("profile: a curve on the first or last PVI is refused", "[req521][profile]") {
  alignment::VerticalProfile first = AcceptanceProfile(1);
  first.points[0].curveLength = 50.0;
  CHECK(alignment::ProfileGeometryRefusal(first).find("first PVI") != std::string::npos);

  alignment::VerticalProfile last = AcceptanceProfile(1);
  last.points[2].curveLength = 50.0;
  CHECK(alignment::ProfileGeometryRefusal(last).find("last PVI") != std::string::npos);
}

TEST_CASE("profile: curves that overlap on one tangent are refused; curves that just meet are allowed", "[req521][profile]") {
  alignment::VerticalProfile overlap;
  overlap.points = {{0.0, 100.0, 0.0}, {100.0, 105.0, 150.0}, {200.0, 100.0, 150.0}, {300.0, 100.0, 0.0}};
  // Half-lengths 75 + 75 = 150 > the 100 ft tangent between them.
  CHECK(alignment::ProfileGeometryRefusal(overlap).find("overlap") != std::string::npos);

  alignment::VerticalProfile meet;
  meet.points = {{0.0, 100.0, 0.0}, {100.0, 105.0, 50.0}, {250.0, 110.0, 50.0}, {400.0, 100.0, 0.0}};
  // The tangent between the two curves is 150 ft and the curves use 25 + 25 = 50 ft of it, so this is valid.
  CHECK(alignment::ProfileGeometryRefusal(meet).empty());
}

TEST_CASE("profile: stations must strictly increase, and every value must be a finite number", "[req521][profile]") {
  alignment::VerticalProfile dup;
  dup.points = {{0.0, 100.0, 0.0}, {0.0, 105.0, 0.0}};
  CHECK(alignment::ProfileGeometryRefusal(dup).find("not after") != std::string::npos);

  alignment::VerticalProfile nan = AcceptanceProfile(1);
  nan.points[1].elevation = std::numeric_limits<double>::quiet_NaN();
  CHECK(alignment::ProfileGeometryRefusal(nan).find("not a number") != std::string::npos);

  alignment::VerticalProfile negative = AcceptanceProfile(1);
  negative.points[1].curveLength = -5.0;
  CHECK(alignment::ProfileGeometryRefusal(negative).find("negative") != std::string::npos);
}

// ---- domain: the derived 3D design path (REQ-521.5, acceptance 2) ----

TEST_CASE("profile: the design path is the plan point with the profile elevation as z", "[req521][profile]") {
  const alignment::Alignment road = NorthRoad(500.0);
  const alignment::VerticalProfile p = AcceptanceProfile(road.id);

  alignment::DesignPoint at250;
  std::string why;
  REQUIRE(alignment::DesignPathPoint(road, p, 250.0, &at250, &why));
  CHECK(at250.x == Approx(0.0).margin(1e-9));
  CHECK(at250.y == Approx(250.0));
  CHECK(at250.z == Approx(109.0));

  alignment::DesignPoint at100;
  REQUIRE(alignment::DesignPathPoint(road, p, 100.0, &at100, &why));
  CHECK(at100.y == Approx(100.0));
  CHECK(at100.z == Approx(104.0));
}

TEST_CASE("profile: the design path refuses another alignment, an outside station, and a missing value", "[req521][profile]") {
  const alignment::Alignment road = NorthRoad(500.0);
  alignment::DesignPoint out;
  std::string why;

  const alignment::VerticalProfile other = AcceptanceProfile(99);
  CHECK_FALSE(alignment::DesignPathPoint(road, other, 250.0, &out, &why));
  CHECK(why.find("different alignment") != std::string::npos);

  const alignment::VerticalProfile p = AcceptanceProfile(road.id);
  CHECK_FALSE(alignment::DesignPathPoint(road, p, 600.0, &out, &why));
  CHECK(why == "outside the alignment");

  alignment::VerticalProfile one = AcceptanceProfile(road.id);
  one.points.resize(1);
  CHECK_FALSE(alignment::DesignPathPoint(road, one, 0.0, &out, &why));
  CHECK(why.find("no geometry") != std::string::npos);
}

// ---- commands: create, PVI add and edit, delete PVI, query, list, delete (REQ-521.6) ----

TEST_CASE("PROFILE commands: create, add three PVIs, query the curve, and list it", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  REQUIRE(LogHas(log, "created with no PVIs"));
  REQUIRE(st.profiles.size() == 1);
  CHECK(st.profiles[0].alignmentId == st.alignments[0].id);

  // The last PVI goes in before the curved middle one: a curve needs a PVI after it (REQ-521.3).
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  Submit(st, "PROFILEPVI Design, 2+50, 110, 100", log);
  REQUIRE(st.profiles[0].points.size() == 3);
  CHECK(st.profiles[0].points[1].curveLength == Approx(100.0));

  Submit(st, "PROFILEQUERY Design, 2+50", log);
  CHECK(LogHas(log, "Design point ("));
  Submit(st, "PROFILEQUERY Design, 0+50", log);
  CHECK(LogHas(log, "grade 4.00%"));
  Submit(st, "PROFILELIST", log);
  CHECK(LogHas(log, "\"Design\" on \"Road\", 3 PVI(s)"));
}

TEST_CASE("PROFILEPVI with a station already used edits that PVI and keeps its station", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  // The last PVI goes in before the curved middle one: a curve needs a PVI after it (REQ-521.3).
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  Submit(st, "PROFILEPVI Design, 2+50, 110, 100", log);
  Submit(st, "PROFILEPVI Design, 2+50, 120, 0", log);  // edit: elevation 120, no curve
  REQUIRE(st.profiles[0].points.size() == 3);
  CHECK(st.profiles[0].points[1].elevation == Approx(120.0));
  CHECK(st.profiles[0].points[1].curveLength == Approx(0.0));
  CHECK(LogHas(log, "PVI at 2+50.00 changed"));
}

TEST_CASE("PROFILEPVI refusals name the reason and change nothing", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100, 50", log);  // a curve on the first PVI
  CHECK(LogHas(log, "refused"));
  CHECK(LogHas(log, "first PVI"));
  CHECK(st.profiles[0].points.empty());

  Submit(st, "PROFILEPVI Design, 6+00, 100", log);  // outside the 500 ft alignment
  CHECK(LogHas(log, "outside the alignment"));
  CHECK(st.profiles[0].points.empty());

  Submit(st, "PROFILEPVI Nope, 0+00, 100", log);
  CHECK(LogHas(log, "no profile named"));
  Submit(st, "PROFILEPVI Design, 0+00, banana", log);
  CHECK(LogHas(log, "not understood"));
  CHECK(st.profiles[0].points.empty());
}

TEST_CASE("CREATEDESIGNPROFILE refuses a duplicate name (case-insensitive) and an unknown alignment", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "CREATEDESIGNPROFILE Road, DESIGN", log);
  CHECK(LogHas(log, "already exists"));
  CHECK(st.profiles.size() == 1);
  Submit(st, "CREATEDESIGNPROFILE Missing, Other", log);
  CHECK(st.profiles.size() == 1);
}

TEST_CASE("PROFILEDELPVI removes a PVI, refuses one that leaves an invalid profile, and reports a missing PVI",
          "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  // The last PVI goes in before the curved middle one: a curve needs a PVI after it (REQ-521.3).
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  Submit(st, "PROFILEPVI Design, 2+50, 110, 100", log);

  Submit(st, "PROFILEDELPVI Design, 0+00", log);  // would leave a curve on the first PVI
  CHECK(LogHas(log, "refused"));
  CHECK(st.profiles[0].points.size() == 3);

  Submit(st, "PROFILEDELPVI Design, 1+00", log);
  CHECK(LogHas(log, "has no PVI at 1+00"));

  Submit(st, "PROFILEDELPVI Design, 2+50", log);
  REQUIRE(st.profiles[0].points.size() == 2);
  CHECK(LogHas(log, "PVI at 2+50.00 removed"));
}

TEST_CASE("PROFILEDELETE removes the profile; a second delete reports it is gone", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEDELETE Design", log);
  CHECK(st.profiles.empty());
  Submit(st, "PROFILEDELETE Design", log);
  CHECK(LogHas(log, "no profile named"));
}

// ---- undo and read-only (REQ-480.4, REQ-521.6) ----

TEST_CASE("PROFILE commands undo in one step each, restoring the previous profile list", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  REQUIRE(st.profiles[0].points.size() == 2);

  Submit(st, "UNDO", log);  // removes the 5+00 PVI
  REQUIRE(st.profiles.size() == 1);
  CHECK(st.profiles[0].points.size() == 1);

  Submit(st, "UNDO", log);  // removes the 0+00 PVI
  CHECK(st.profiles[0].points.empty());

  Submit(st, "UNDO", log);  // removes the profile
  CHECK(st.profiles.empty());
}

TEST_CASE("PROFILE changes are refused on a read-only project and change nothing", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  st.drawingTabs.resize(2);
  AppCommandState::ProjectSession session;
  session.uid = 7;
  session.readOnly = true;
  st.openProjects.push_back(session);
  st.drawingTabs[1].projectUid = 7;

  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  CHECK(LogHas(log, "read-only"));
  CHECK(st.profiles.empty());
}

// ---- save/reopen and a deleted alignment (REQ-521.7, .8, acceptance 5, 6) ----

TEST_CASE("profile survives save and reopen with the same PVIs, ids and queries", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  // The last PVI goes in before the curved middle one: a curve needs a PVI after it (REQ-521.3).
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  Submit(st, "PROFILEPVI Design, 2+50, 110, 100", log);

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.profiles.size() == 1);
  const alignment::VerticalProfile& p = back.profiles[0];
  CHECK(p.name == "Design");
  CHECK(p.id == st.profiles[0].id);
  CHECK(p.alignmentId == back.alignments[0].id);
  REQUIRE(p.points.size() == 3);
  CHECK(p.points[1].station == Approx(250.0));
  CHECK(p.points[1].elevation == Approx(110.0));
  CHECK(p.points[1].curveLength == Approx(100.0));

  const alignment::ProfileSample s = alignment::ProfileAtStation(p, 250.0);
  REQUIRE(s.hasValue);
  CHECK(s.elevation == Approx(109.0));
}

TEST_CASE("a profile whose alignment is deleted is kept, reported as alignment missing, and never invents a value",
          "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  Submit(st, "ALIGNMENTDELETE Road", log);
  REQUIRE(st.alignments.empty());
  REQUIRE(st.profiles.size() == 1);  // kept (REQ-521.7)

  Submit(st, "PROFILELIST", log);
  CHECK(LogHas(log, "\"Design\" on alignment missing"));
  Submit(st, "PROFILEQUERY Design, 2+00", log);
  CHECK(LogHas(log, "has no alignment (alignment missing)"));
  Submit(st, "PROFILEPVI Design, 3+00, 101", log);
  CHECK(LogHas(log, "PROFILEPVI - profile \"Design\" has no alignment"));  // the edit's own refusal, not the list line
  CHECK(st.profiles[0].points.size() == 2);

  // The same state survives save and reopen, and the reopened drawing reports it the same way.
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.profiles.size() == 1);
  std::vector<std::string> backLog;
  Submit(back, "PROFILELIST", backLog);
  CHECK(LogHas(backLog, "\"Design\" on alignment missing"));
}

// ---- council round 1 (REQ-521): exact range, finite results, overlap at command level, read-only and undo for every
//      changing verb, and no designProfiles key when a drawing has none ----

TEST_CASE("profile: a station a hair outside the range gives no value (exact range, REQ-521.4)", "[req521][profile]") {
  const alignment::VerticalProfile p = AcceptanceProfile(1);
  const alignment::ProfileSample s = alignment::ProfileAtStation(p, 500.0000005);
  CHECK_FALSE(s.hasValue);
  CHECK(s.reason == "outside the profile");
}

TEST_CASE("profile: a tangent too steep to represent is refused and gives no non-finite value", "[req521][profile]") {
  alignment::VerticalProfile p;
  p.points = {{0.0, 0.0, 0.0}, {5e-324, 1.0, 0.0}};
  CHECK(alignment::ProfileGeometryRefusal(p).find("too steep") != std::string::npos);
  CHECK_FALSE(alignment::ProfileAtStation(p, 0.0).hasValue);
}

TEST_CASE("profile: curves overlapping by a micro-foot are still refused", "[req521][profile]") {
  alignment::VerticalProfile p;
  // Tangent 100 ft; halves 50 + 50.000001 exceed it by 1e-6.
  p.points = {{0.0, 100.0, 0.0}, {100.0, 105.0, 100.0}, {200.0, 100.0, 100.000002}, {300.0, 100.0, 0.0}};
  CHECK(alignment::ProfileGeometryRefusal(p).find("overlap") != std::string::npos);
}

TEST_CASE("PROFILEPVI refuses a curve that overlaps the one before it, and changes nothing", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  Submit(st, "PROFILEPVI Design, 1+00, 105, 100", log);
  REQUIRE(st.profiles[0].points.size() == 3);
  // The 2+00 curve (half 75) and the 1+00 curve (half 50) need 125 ft of a 100 ft tangent.
  Submit(st, "PROFILEPVI Design, 2+00, 110, 150", log);
  CHECK(LogHas(log, "overlap"));
  CHECK(st.profiles[0].points.size() == 3);
}

TEST_CASE("PROFILEPVI, PROFILEDELPVI and PROFILEDELETE are refused on a read-only project", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  st.drawingTabs.resize(2);
  AppCommandState::ProjectSession session;
  session.uid = 7;
  session.readOnly = true;
  st.openProjects.push_back(session);
  st.drawingTabs[1].projectUid = 7;

  Submit(st, "PROFILEPVI Design, 2+50, 110", log);
  CHECK(LogHas(log, "read-only"));
  CHECK(st.profiles[0].points.size() == 2);
  Submit(st, "PROFILEDELPVI Design, 0+00", log);
  CHECK(st.profiles[0].points.size() == 2);
  Submit(st, "PROFILEDELETE Design", log);
  CHECK(st.profiles.size() == 1);
}

TEST_CASE("PROFILEPVI edit, PROFILEDELPVI and PROFILEDELETE each undo in one step", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  Submit(st, "PROFILEPVI Design, 2+50, 110, 100", log);
  REQUIRE(st.profiles[0].points.size() == 3);

  Submit(st, "PROFILEPVI Design, 2+50, 120, 0", log);  // edit
  REQUIRE(st.profiles[0].points[1].elevation == Approx(120.0));
  Submit(st, "UNDO", log);
  CHECK(st.profiles[0].points[1].elevation == Approx(110.0));
  CHECK(st.profiles[0].points[1].curveLength == Approx(100.0));

  Submit(st, "PROFILEDELPVI Design, 2+50", log);  // remove
  REQUIRE(st.profiles[0].points.size() == 2);
  Submit(st, "UNDO", log);
  REQUIRE(st.profiles[0].points.size() == 3);
  CHECK(st.profiles[0].points[1].station == Approx(250.0));

  Submit(st, "PROFILEDELETE Design", log);  // delete the profile
  REQUIRE(st.profiles.empty());
  Submit(st, "UNDO", log);
  REQUIRE(st.profiles.size() == 1);
  CHECK(st.profiles[0].points.size() == 3);
}

TEST_CASE("a drawing with no design profiles writes no designProfiles key (REQ-521.8)", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  CHECK(SerializeGoSurveyJson(st).find("designProfiles") == std::string::npos);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  CHECK(SerializeGoSurveyJson(st).find("designProfiles") != std::string::npos);
}

// ---- council round 2 (REQ-521): exact touch, exact undo, malformed and wrong-typed stored data ----

namespace {

bool SamePoints(const std::vector<alignment::VerticalPoint>& a, const std::vector<alignment::VerticalPoint>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::abs(a[i].station - b[i].station) > 1e-12 || std::abs(a[i].elevation - b[i].elevation) > 1e-12 ||
        std::abs(a[i].curveLength - b[i].curveLength) > 1e-12)
      return false;
  }
  return true;
}

bool SameProfiles(const std::vector<alignment::VerticalProfile>& a, const std::vector<alignment::VerticalProfile>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].id != b[i].id || a[i].name != b[i].name || a[i].alignmentId != b[i].alignmentId ||
        !SamePoints(a[i].points, b[i].points))
      return false;
  }
  return true;
}

}  // namespace

TEST_CASE("profile: curves that exactly meet on one tangent are allowed (REQ-521.3, touching is allowed)", "[req521][profile]") {
  alignment::VerticalProfile p;
  // Tangent 1-2 is 100 ft; the two curves use 50 + 50 = 100 ft of it.
  p.points = {{0.0, 100.0, 0.0}, {100.0, 105.0, 100.0}, {200.0, 110.0, 100.0}, {300.0, 100.0, 0.0}};
  CHECK(alignment::ProfileGeometryRefusal(p).empty());
  const alignment::ProfileSample at150 = alignment::ProfileAtStation(p, 150.0);
  REQUIRE(at150.hasValue);
  CHECK(at150.elevation == Approx(107.5));  // both curves give the same value at the touch point
}

TEST_CASE("UNDO restores the profile list exactly (every id, name, alignment and PVI field)", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  Submit(st, "PROFILEPVI Design, 2+50, 110, 100", log);
  const std::vector<alignment::VerticalProfile> before = st.profiles;

  Submit(st, "PROFILEPVI Design, 2+50, 120, 0", log);
  Submit(st, "UNDO", log);
  CHECK(SameProfiles(st.profiles, before));

  Submit(st, "PROFILEDELPVI Design, 2+50", log);
  Submit(st, "UNDO", log);
  CHECK(SameProfiles(st.profiles, before));
}

TEST_CASE("a design profile with a malformed stored point is not loaded, and the load log names it", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);

  nlohmann::json doc = nlohmann::json::parse(SerializeGoSurveyJson(st));
  REQUIRE(doc["document"]["designProfiles"].is_array());
  doc["document"]["designProfiles"][0]["points"][1] = "not a point";
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, doc.dump(), loadLog));
  CHECK(back.profiles.empty());
  CHECK(LogHas(loadLog, "was not loaded"));
}

TEST_CASE("a wrong-typed stored field never fails the drawing load (REQ-521.8)", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);

  nlohmann::json doc = nlohmann::json::parse(SerializeGoSurveyJson(st));
  doc["document"]["designProfiles"][0]["alignmentId"] = "x";  // wrong type: the profile reads as alignment missing
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, doc.dump(), loadLog));
  REQUIRE(back.profiles.size() == 1);
  CHECK(back.profiles[0].alignmentId == 0);
}

TEST_CASE("a design profile with no name, a repeated name, or no points is not loaded, and the load log names it",
          "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  const nlohmann::json base = nlohmann::json::parse(SerializeGoSurveyJson(st));

  nlohmann::json noName = base;
  noName["document"]["designProfiles"][0]["name"] = 7;  // not a string: no name
  AppCommandState a = Drawing();
  std::vector<std::string> aLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(a, noName.dump(), aLog));
  CHECK(a.profiles.empty());
  CHECK(LogHas(aLog, "entry 1 was not loaded: its name is missing or blank"));

  nlohmann::json repeated = base;
  repeated["document"]["designProfiles"].push_back(repeated["document"]["designProfiles"][0]);
  repeated["document"]["designProfiles"][1]["name"] = "DESIGN";  // same name, different capitals
  AppCommandState b = Drawing();
  std::vector<std::string> bLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(b, repeated.dump(), bLog));
  CHECK(b.profiles.size() == 1);
  CHECK(LogHas(bLog, "already has that name"));

  nlohmann::json noPoints = base;
  noPoints["document"]["designProfiles"][0].erase("points");
  AppCommandState c = Drawing();
  std::vector<std::string> cLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(c, noPoints.dump(), cLog));
  CHECK(c.profiles.empty());
  CHECK(LogHas(cLog, "stored points are missing"));
}

// ---- council round 4 (REQ-521): a query past the profile's end, the geometry-not-valid listing, and the stricter load rules ----

TEST_CASE("PROFILEQUERY past the profile's last PVI says outside the profile, not outside the alignment", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  Submit(st, "PROFILEQUERY Design, 6+00", log);
  CHECK(LogHas(log, "outside the profile"));
  CHECK_FALSE(LogHas(log, "outside the alignment"));
}

TEST_CASE("PROFILELIST marks a loaded profile whose curves overlap as geometry not valid", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  nlohmann::json doc = nlohmann::json::parse(SerializeGoSurveyJson(st));
  doc["document"]["designProfiles"][0]["points"] = nlohmann::json::parse("[[0,100,0],[100,105,150],[200,100,150],[500,100,0]]");
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, doc.dump(), loadLog));
  REQUIRE(back.profiles.size() == 1);
  std::vector<std::string> listLog;
  Submit(back, "PROFILELIST", listLog);
  CHECK(LogHas(listLog, "geometry not valid"));
}

TEST_CASE("a stored profile name is trimmed, and a blank one is not loaded", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  const nlohmann::json base = nlohmann::json::parse(SerializeGoSurveyJson(st));

  nlohmann::json padded = base;
  padded["document"]["designProfiles"][0]["name"] = "  Design  ";
  AppCommandState a = Drawing();
  std::vector<std::string> aLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(a, padded.dump(), aLog));
  REQUIRE(a.profiles.size() == 1);
  CHECK(a.profiles[0].name == "Design");

  nlohmann::json blank = base;
  blank["document"]["designProfiles"][0]["name"] = "   ";
  AppCommandState b = Drawing();
  std::vector<std::string> bLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(b, blank.dump(), bLog));
  CHECK(b.profiles.empty());
  CHECK(LogHas(bLog, "name is missing or blank"));
}

TEST_CASE("a stored point with an extra value, or an entry that is not an object, is not loaded", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  const nlohmann::json base = nlohmann::json::parse(SerializeGoSurveyJson(st));

  nlohmann::json extra = base;
  extra["document"]["designProfiles"][0]["points"][1] = nlohmann::json::array({100.0, 110.0, 100.0, 7});
  AppCommandState a = Drawing();
  std::vector<std::string> aLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(a, extra.dump(), aLog));
  CHECK(a.profiles.empty());
  CHECK(LogHas(aLog, "malformed"));

  nlohmann::json notObject = base;
  notObject["document"]["designProfiles"] = nlohmann::json::array({"junk"});
  AppCommandState b = Drawing();
  std::vector<std::string> bLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(b, notObject.dump(), bLog));
  CHECK(b.profiles.empty());
  CHECK(LogHas(bLog, "not an object"));
}

// ---- council round 5 (REQ-521): the removal path for a missing alignment, and a non-finite value typed at the command line ----

TEST_CASE("PROFILEDELETE removes a profile whose alignment is missing (REQ-521.7's removal path)", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "ALIGNMENTDELETE Road", log);
  REQUIRE(st.profiles.size() == 1);
  Submit(st, "PROFILEDELETE Design", log);
  CHECK(st.profiles.empty());
  CHECK(LogHas(log, "profile \"Design\" deleted"));
}

TEST_CASE("PROFILEPVI refuses a non-finite elevation typed at the command line and changes nothing", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, nan", log);
  CHECK(LogHas(log, "No PVI was changed"));
  CHECK(st.profiles[0].points.empty());
}

// ---- council round 6 (REQ-521): an overflowing station span, a NaN station, a stale PVI past a shortened alignment ----

TEST_CASE("profile: stations so far apart that the tangent grade overflows are refused, not read as zero", "[req521][profile]") {
  alignment::VerticalProfile p;
  p.points = {{-1e308, 0.0, 0.0}, {1e308, 1.0, 0.0}};
  CHECK(alignment::ProfileGeometryRefusal(p).find("too steep") != std::string::npos);
  CHECK_FALSE(alignment::ProfileAtStation(p, 0.0).hasValue);
}

TEST_CASE("profile: a NaN station is refused as not a number", "[req521][profile]") {
  alignment::VerticalProfile p;
  p.points = {{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, {5.0, 1.0, 0.0}};
  CHECK(alignment::ProfileGeometryRefusal(p).find("not a number") != std::string::npos);
}

TEST_CASE("PROFILEDELPVI removes a PVI left beyond a shortened alignment (REQ-521.6)", "[req521][profile]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  NorthRoadCommand(st, log);
  Submit(st, "CREATEDESIGNPROFILE Road, Design", log);
  Submit(st, "PROFILEPVI Design, 0+00, 100", log);
  Submit(st, "PROFILEPVI Design, 5+00, 100", log);
  nlohmann::json doc = nlohmann::json::parse(SerializeGoSurveyJson(st));
  // A PVI at 600 ft, past the 500 ft alignment: what a shortened alignment leaves behind.
  doc["document"]["designProfiles"][0]["points"] = nlohmann::json::parse("[[0,100,0],[500,100,0],[600,100,0]]");
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, doc.dump(), loadLog));
  REQUIRE(back.profiles.size() == 1);
  REQUIRE(back.profiles[0].points.size() == 3);
  std::vector<std::string> delLog;
  Submit(back, "PROFILEDELPVI Design, 6+00", delLog);
  CHECK(LogHas(delLog, "removed"));
  CHECK(back.profiles[0].points.size() == 2);
}

// ---- PR #1179 review (REQ-521): an overflowing grade difference, and no extrapolation past a tiny curve ----

TEST_CASE("profile: a curve between grades so large that their difference overflows still gives its value", "[req521][profile]") {
  alignment::VerticalProfile p;
  // Grades +1e308 then -1e308; the midpoint is 1e308 - (1e308 / 4) = 7.5e307 with zero grade.
  p.points = {{0.0, 0.0, 0.0}, {1.0, 1e308, 1.0}, {2.0, 0.0, 0.0}};
  REQUIRE(alignment::ProfileGeometryRefusal(p).empty());
  const alignment::ProfileSample s = alignment::ProfileAtStation(p, 1.0);
  REQUIRE(s.hasValue);
  CHECK(s.elevation == Approx(7.5e307).epsilon(1e-12));
  CHECK(s.grade == Approx(0.0).margin(1.0));
}

TEST_CASE("profile: a station just before a tiny curve follows the tangent, not the parabola", "[req521][profile]") {
  alignment::VerticalProfile p;
  // The curve is 1e-12 ft long around station 1, so the tangent from 0 to 1 is the grade line of slope 1.
  p.points = {{0.0, 0.0, 0.0}, {1.0, 1.0, 1e-12}, {2.0, 0.0, 0.0}};
  REQUIRE(alignment::ProfileGeometryRefusal(p).empty());
  const alignment::ProfileSample s = alignment::ProfileAtStation(p, 0.9999995);
  REQUIRE(s.hasValue);
  CHECK(s.elevation == Approx(0.9999995).margin(1e-9));
}
