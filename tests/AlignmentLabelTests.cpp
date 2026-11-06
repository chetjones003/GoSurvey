#include "AlignmentLabelSet.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentLabels.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  return st;
}

alignment::Alignment Alignment382Foot() {
  alignment::Alignment a;
  a.startingStation = 0.0;
  REQUIRE(a.AppendTangent(alignment::Point2{0.0, 0.0}, 0.0, 382.0));
  return a;
}

}  // namespace

TEST_CASE("Major/minor station ticks for 0+00 to 3+82 at 100/20 (REQ-492 AC1)", "[req492][alignment]") {
  const alignment::Alignment a = Alignment382Foot();
  CHECK(a.EndStationWithEquations() == Catch::Approx(382.0));
  const std::vector<alignment::StationTickLabel> ticks =
      alignment::CollectMajorMinorStationTicks(a, 100.0, 20.0, true, true);
  int majors = 0;
  int minors = 0;
  for (const alignment::StationTickLabel& t : ticks) {
    if (t.major) {
      ++majors;
      const bool okMajor = t.displayedStation == Catch::Approx(100.0).margin(1e-6) ||
                           t.displayedStation == Catch::Approx(200.0).margin(1e-6) ||
                           t.displayedStation == Catch::Approx(300.0).margin(1e-6);
      CHECK(okMajor);
    } else {
      ++minors;
    }
  }
  CHECK(majors == 3);
  CHECK(minors == 15);
  CHECK(ticks.size() == 18);
}

TEST_CASE("ALIGNMENTLABELSET assigns set and intervals persist", "[req492][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT A", log);
  Submit(st, "ALIGNMENTADD A, TANGENT, 0, 0, 0, 382", log);
  Submit(st, "ALIGNMENTLABELSET NEW, Road", log);
  Submit(st, "ALIGNMENTLABELSET MAJORINTERVAL, Road, 50", log);
  Submit(st, "ALIGNMENTLABELSET ASSIGN, A, Road", log);
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].labelSet == "Road");
  const AlignmentLabelSet* road = AlignmentLabelSets::Find(st.alignmentLabelSets, "Road");
  REQUIRE(road != nullptr);
  CHECK(road->majorInterval == Catch::Approx(50.0));
  std::string json = SerializeGoSurveyJson(st);
  AppCommandState loaded = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(loaded, json, loadLog));
  REQUIRE(loaded.alignments.size() == 1);
  CHECK(loaded.alignments[0].labelSet == "Road");
  const AlignmentLabelSet* road2 = AlignmentLabelSets::Find(loaded.alignmentLabelSets, "Road");
  REQUIRE(road2 != nullptr);
  CHECK(road2->majorInterval == Catch::Approx(50.0));
}

namespace {

// The REQ-502 panel road: 90' tangent at N70E, R=80 curve right through 75 degrees, 60' tangent, from 5+00.
AppCommandState MainRoad(std::vector<std::string>& log) {
  AppCommandState st = Drawing();
  Submit(st, "ALIGNMENT Main Road, Two-lane road, 5+00", log);
  Submit(st, "ALIGNMENTADD Main Road, TANGENT, -120, -40, 70, 90", log);
  Submit(st, "ALIGNMENTADD Main Road, CURVE, 80, 75, RIGHT", log);
  Submit(st, "ALIGNMENTADD Main Road, TANGENT, 60", log);
  REQUIRE(st.alignments.size() == 1);
  REQUIRE(st.alignments[0].Entities().size() == 3);
  return st;
}

}  // namespace

TEST_CASE("Geometry-point labels read code and station, as Civil 3D labels them (D-2026-11-04-c)", "[req492][alignment]") {
  std::vector<std::string> log;
  const AppCommandState st = MainRoad(log);
  const std::vector<alignment::GeometryPointLabel> labels = alignment::CollectGeometryPointLabels(st.alignments[0]);
  std::vector<std::string> texts;
  for (const alignment::GeometryPointLabel& l : labels) texts.push_back(l.text);
  // Same five labels Civil 3D 2026 draws for this road with "All Labels"; no line midpoint, curve centre or curve PI.
  CHECK(texts == std::vector<std::string>{"BP: 5+00.00", "PC: 5+90.00", "Mid: 6+42.36", "PT: 6+94.72", "EP: 7+54.72"});
  CHECK(std::string(alignment::GeometryPointAbbreviation(alignment::GeometryPointCode::CurveCenter)).empty());
  CHECK(std::string(alignment::GeometryPointAbbreviation(alignment::GeometryPointCode::TanTan)) == "PI");
  CHECK(alignment::CollectGeometryPointLabels(alignment::Alignment{}).empty());
}

TEST_CASE("Design-speed labels name the station and the unit (D-2026-11-04-c)", "[req492][alignment]") {
  std::vector<std::string> log;
  AppCommandState st = MainRoad(log);
  Submit(st, "ALIGNMENTSPEED Main Road, 5+00, 40", log);
  Submit(st, "ALIGNMENTSPEED Main Road, 6+50, 42.5", log);
  const std::vector<alignment::DesignSpeedLabel> feet = alignment::CollectDesignSpeedLabels(st.alignments[0], false);
  REQUIRE(feet.size() == 2);
  CHECK(feet[0].text == "STA 5+00.00 DESIGN SPEED = 40 mi/h");
  CHECK(feet[1].text == "STA 6+50.00 DESIGN SPEED = 42.50 mi/h");
  const std::vector<alignment::DesignSpeedLabel> metric = alignment::CollectDesignSpeedLabels(st.alignments[0], true);
  REQUIRE(metric.size() == 2);
  CHECK(metric[0].text == "STA 5+00.00 DESIGN SPEED = 40 km/h");
  CHECK(alignment::CollectDesignSpeedLabels(alignment::Alignment{}, false).empty());
}

TEST_CASE("Labelled bounds take in the leader labels, so ZOOM EXTENTS frames them (D-2026-11-04-c)", "[req492][alignment]") {
  std::vector<std::string> log;
  AppCommandState st = MainRoad(log);
  const alignment::Alignment& a = st.alignments[0];
  alignment::Point2 gmn{}, gmx{};
  REQUIRE(a.Bounds(&gmn, &gmx));
  st.modelUnitsPerPlottedInch = 50.f;
  const double h = AlignmentLabelStyles::TextHeight(AlignmentLabelStyles::Resolve(st.alignmentLabelStyles, "Standard"), 50.0);
  CHECK(h == Catch::Approx(5.0));
  alignment::Point2 mn{}, mx{};
  REQUIRE(AlignmentLabelSets::LabelledBounds(a, CadAlignmentLabelContext(st), &mn, &mx));
  // EP leads off to the north-east (left of travel at S35E): the box reaches the end of "EP: 7+54.72".
  const double reach = alignment::LeaderLabelReach("EP: 7+54.72", h);
  CHECK(reach == Catch::Approx(h * (alignment::kLeaderTextStartHeights + alignment::kLabelCharWidthHeights * 11 +
                                    alignment::kLeaderOvershootHeights)));
  CHECK(mx.x >= gmx.x + 0.8 * reach);
  CHECK(mn.x < gmn.x);
  CHECK(mn.y < gmn.y);

  // REQ-492.5: a taller geometry-point style reaches further.
  Submit(st, "ALIGNMENTLABELSTYLE NEW, Big", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Big, HEIGHT, 0.2", log);
  Submit(st, "ALIGNMENTLABELSET STYLE, Standard, GEOMETRY, Big", log);
  alignment::Point2 bigMn{}, bigMx{};
  REQUIRE(AlignmentLabelSets::LabelledBounds(st.alignments[0], CadAlignmentLabelContext(st), &bigMn, &bigMx));
  CHECK(bigMx.x > mx.x + 0.5 * reach);

  Submit(st, "ALIGNMENTSTYLE NEW, Bare", log);
  Submit(st, "ALIGNMENTSTYLE SET, Bare, LABELS, OFF", log);
  REQUIRE(!AlignmentStyles::Find(st.alignmentStyles, "Bare")->labelsVisible);
  Submit(st, "ALIGNMENTSTYLE ASSIGN, Main Road, Bare", log);
  REQUIRE(st.alignments[0].style == "Bare");
  REQUIRE(AlignmentLabelSets::LabelledBounds(st.alignments[0], CadAlignmentLabelContext(st), &mn, &mx));
  CHECK(mx.x == Catch::Approx(gmx.x));
  CHECK(mn.y == Catch::Approx(gmn.y));
  CHECK_FALSE(alignment::LabelledBounds(alignment::Alignment{}, alignment::AlignmentLabelItems{}, &mn, &mx));
}

TEST_CASE("ALIGNMENTLABELSTYLE: create, edit, refuse bad values, delete (REQ-492.5)", "[req492][alignment]") {
  std::vector<std::string> log;
  AppCommandState st = MainRoad(log);
  REQUIRE(st.alignmentLabelStyles.size() == 1);
  CHECK(st.alignmentLabelStyles[0] == AlignmentLabelStyles::StandardLabelStyle());
  CHECK(st.alignmentLabelStyles[0].textStyle == "Standard");
  CHECK(st.alignmentLabelStyles[0].heightInches == Catch::Approx(0.1));
  CHECK(st.alignmentLabelStyles[0].color == "ByLayer");

  Submit(st, "ALIGNMENTLABELSTYLE NEW, Red Text", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Red Text, HEIGHT, 0.08", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Red Text, COLOR, red", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Red Text, TEXTSTYLE, Standard", log);
  const AlignmentLabelStyle* red = AlignmentLabelStyles::Find(st.alignmentLabelStyles, "Red Text");
  REQUIRE(red != nullptr);
  CHECK(red->heightInches == Catch::Approx(0.08));
  CHECK(red->color != "ByLayer");

  const auto refused = [&](const std::string& cmd, const std::string& why) {
    log.clear();
    const AlignmentLabelStyle before = *AlignmentLabelStyles::Find(st.alignmentLabelStyles, "Red Text");
    Submit(st, cmd, log);
    REQUIRE(!log.empty());
    CHECK(log.back().find(why) != std::string::npos);
    CHECK(*AlignmentLabelStyles::Find(st.alignmentLabelStyles, "Red Text") == before);
  };
  refused("ALIGNMENTLABELSTYLE SET, Red Text, HEIGHT, -1", "positive plotted height");
  refused("ALIGNMENTLABELSTYLE SET, Red Text, COLOR, chartreuse-ish", "is not a colour");
  refused("ALIGNMENTLABELSTYLE SET, Red Text, TEXTSTYLE, NoSuchStyle", "no text style named");
  refused("ALIGNMENTLABELSTYLE SET, Red Text, WEIGHT, 2", "HEIGHT, COLOR, TEXTSTYLE, PRECISION");
  refused("ALIGNMENTLABELSTYLE NEW, Red Text", "already exists");

  Submit(st, "ALIGNMENTLABELSET STYLE, Standard, SPEEDS, Red Text", log);
  CHECK(st.alignmentLabelSets.front().speedStyle == "Red Text");
  log.clear();
  Submit(st, "ALIGNMENTLABELSET STYLE, Standard, CURVES, Red Text", log);
  CHECK(log.back().find("item must be MAJOR") != std::string::npos);
  Submit(st, "ALIGNMENTLABELSET STYLE, Standard, MAJOR, Missing", log);
  CHECK(log.back().find("no alignment label style named") != std::string::npos);
  CHECK(st.alignmentLabelSets.front().majorStyle == "Standard");

  log.clear();
  Submit(st, "ALIGNMENTLABELSTYLE DELETE, Red Text", log);
  CHECK(log.back().find("used by 1 label set") != std::string::npos);
  Submit(st, "ALIGNMENTLABELSTYLE DELETE, Standard", log);
  CHECK(log.back().find("cannot be deleted") != std::string::npos);
  Submit(st, "ALIGNMENTLABELSET STYLE, Standard, SPEEDS, Standard", log);
  Submit(st, "ALIGNMENTLABELSTYLE DELETE, Red Text", log);
  CHECK(AlignmentLabelStyles::Find(st.alignmentLabelStyles, "Red Text") == nullptr);
  Submit(st, "UNDO", log);
  CHECK(AlignmentLabelStyles::Find(st.alignmentLabelStyles, "Red Text") != nullptr);
}

TEST_CASE("Alignment label styles and item assignments are saved; a missing style reads as Standard (REQ-492.5)",
          "[req492][alignment]") {
  std::vector<std::string> log;
  AppCommandState st = MainRoad(log);
  Submit(st, "ALIGNMENTLABELSTYLE NEW, Big", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Big, HEIGHT, 0.15", log);
  Submit(st, "ALIGNMENTLABELSTYLE SET, Big, COLOR, 1", log);
  Submit(st, "ALIGNMENTLABELSET STYLE, Standard, GEOMETRY, Big", log);
  Submit(st, "ALIGNMENTLABELSET STYLE, Standard, SEGMENTS, Big", log);
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState loaded = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(loaded, json, loadLog));
  CHECK(loaded.alignmentLabelStyles == st.alignmentLabelStyles);
  CHECK(loaded.alignmentLabelSets.front() == st.alignmentLabelSets.front());

  // Rename the first "Big": either a label-set reference or the style itself, so one reference no longer resolves.
  std::string orphaned = json;
  const size_t at = orphaned.find("\"Big\"");
  REQUIRE(at != std::string::npos);
  orphaned.replace(at, 5, "\"Gone\"");
  AppCommandState loadedOrphan = Drawing();
  loadLog.clear();
  REQUIRE(LoadGoSurveyFromJsonUtf8(loadedOrphan, orphaned, loadLog));
  const AlignmentLabelSet& set = loadedOrphan.alignmentLabelSets.front();
  const bool fellBack = set.geometryStyle == "Standard" || set.segmentStyle == "Standard";
  CHECK(fellBack);
  CHECK(std::any_of(loadLog.begin(), loadLog.end(),
                    [](const std::string& l) { return l.find("references missing label style") != std::string::npos; }));
}

TEST_CASE("Line and spiral segment label text (REQ-492.3)", "[req492][alignment]") {
  std::vector<std::string> log;
  const AppCommandState st = MainRoad(log);
  const alignment::Entity& line = st.alignments[0].Entities()[0];
  AngleDisplaySettings bearings;
  bearings.type = AngleDisplayType::Surveyor;
  bearings.precision = 2;
  CHECK(alignment::LineLabelLines(line, 2, bearings) ==
        std::vector<std::string>{FormatBearing(70.0, bearings), "90.00'"});
  CHECK(alignment::LineLabelLines(line, 2, bearings).front().rfind("N ", 0) == 0);
  alignment::SpiralData sd;
  sd.length = 60.0;
  sd.startRadius = 0.0;
  sd.endRadius = 300.0;
  sd.parameterA = 134.164;
  CHECK(alignment::SpiralLabelLines(sd, 2) ==
        std::vector<std::string>{"A = 134.16", "Ls = 60.00'", "R1 = \xE2\x88\x9E", "R2 = 300.00'"});
}

TEST_CASE("Geometry Points and Design Speeds label-set items: on by default, switchable, saved", "[req492][alignment]") {
  std::vector<std::string> log;
  AppCommandState st = MainRoad(log);
  const AlignmentLabelSet* standard = AlignmentLabelSets::Find(st.alignmentLabelSets, "Standard");
  REQUIRE(standard != nullptr);
  CHECK(standard->geometryPointsOn);
  CHECK(standard->designSpeedsOn);

  Submit(st, "ALIGNMENTLABELSET NEW, Plan", log);
  Submit(st, "ALIGNMENTLABELSET HIDE, Plan, GEOMETRY", log);
  Submit(st, "ALIGNMENTLABELSET HIDE, Plan, SPEEDS", log);
  log.clear();
  Submit(st, "ALIGNMENTLABELSET HIDE, Plan, CURVES", log);
  REQUIRE(!log.empty());
  CHECK(log.back().find("MAJOR, MINOR, EQUATIONS, GEOMETRY, SPEEDS, PROFILE or") != std::string::npos);

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState loaded = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(loaded, json, loadLog));
  const AlignmentLabelSet* plan = AlignmentLabelSets::Find(loaded.alignmentLabelSets, "Plan");
  REQUIRE(plan != nullptr);
  CHECK_FALSE(plan->geometryPointsOn);
  CHECK_FALSE(plan->designSpeedsOn);

  // A file written before these items existed has neither key; both load switched on.
  std::string older = json;
  for (const std::string key : {"\"geometryPointsOn\"", "\"designSpeedsOn\""}) {
    for (size_t at = older.find(key); at != std::string::npos; at = older.find(key))
      older.replace(at, key.size(), "\"retiredKey\"");
  }
  AppCommandState loadedOlder = Drawing();
  REQUIRE(LoadGoSurveyFromJsonUtf8(loadedOlder, older, loadLog));
  const AlignmentLabelSet* planOlder = AlignmentLabelSets::Find(loadedOlder.alignmentLabelSets, "Plan");
  REQUIRE(planOlder != nullptr);
  CHECK(planOlder->geometryPointsOn);
  CHECK(planOlder->designSpeedsOn);
}
