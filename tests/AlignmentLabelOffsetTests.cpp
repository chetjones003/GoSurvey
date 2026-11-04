// REQ-492 acceptance 2 (equation labels), 4 (station/offset labels) and 5 (major/minor refusal) / GitHub issue #121.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentLabels.hpp"
#include "AlignmentLabelSet.hpp"

using alignment::Alignment;
using alignment::StationOffsetLabel;
using Catch::Approx;

namespace {

Alignment EastLine(double length) {
  Alignment a;
  a.name = "EAST";
  REQUIRE(a.AppendTangent({0.0, 0.0}, 90.0 * alignment::kPi / 180.0, length));
  return a;
}

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
  st.displayLinearPrecision = 2;
  return st;
}

}  // namespace

TEST_CASE("An equation label gives its back and ahead displayed stations (REQ-492 AC2)", "[req492][alignment]") {
  Alignment a = EastLine(2000.0);
  REQUIRE(a.AddStationEquation({1000.0, 1200.0, true}));
  const std::vector<alignment::EquationLabel> eq = alignment::CollectEquationLabels(a);
  REQUIRE(eq.size() == 1);
  CHECK(eq[0].rawStation == Approx(1000.0));
  CHECK(eq[0].backDisplayed == Approx(1000.0));
  CHECK(eq[0].aheadDisplayed == Approx(1200.0));
  CHECK(eq[0].increasing);
}

TEST_CASE("Station labels after an equation show the displayed values (REQ-492 AC2)", "[req492][alignment]") {
  Alignment a = EastLine(2000.0);
  REQUIRE(a.AddStationEquation({1000.0, 1200.0, true}));
  const std::vector<alignment::StationTickLabel> ticks =
      alignment::CollectMajorMinorStationTicks(a, 100.0, 100.0, true, false);
  bool found = false;
  for (const alignment::StationTickLabel& t : ticks) {
    if (std::fabs(t.rawStation - 1500.0) < 1e-6) {
      found = true;
      CHECK(t.displayedStation == Approx(1700.0));  // 1500 raw is 17+00 displayed after a +200 equation
    }
  }
  CHECK(found);
}

TEST_CASE("Minor stations cannot be hidden apart from the major stations (REQ-492 AC5)", "[req492][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTLABELSET NEW, Demo", log);
  log.clear();
  Submit(st, "ALIGNMENTLABELSET HIDE, Demo, MAJOR", log);
  CHECK(LogHas(log, "refused"));  // minor is on, so major cannot go
  CHECK(LogHas(log, "Hide MINOR first"));
  AlignmentLabelSet* demo = AlignmentLabelSets::Find(st.alignmentLabelSets, "Demo");
  REQUIRE(demo != nullptr);
  CHECK(demo->majorStationsOn);

  log.clear();
  Submit(st, "ALIGNMENTLABELSET HIDE, Demo, MINOR", log);  // hiding minor first is allowed
  CHECK(demo->minorStationsOn == false);
  log.clear();
  Submit(st, "ALIGNMENTLABELSET HIDE, Demo, MAJOR", log);  // now major may go
  CHECK(demo->majorStationsOn == false);

  log.clear();
  Submit(st, "ALIGNMENTLABELSET SHOW, Demo, MINOR", log);  // minor needs major
  CHECK(LogHas(log, "refused"));
  CHECK(demo->minorStationsOn == false);
}

TEST_CASE("Equations are an item of the label set and can be hidden (REQ-492.1)", "[req492][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTLABELSET NEW, Demo2", log);
  AlignmentLabelSet* s = AlignmentLabelSets::Find(st.alignmentLabelSets, "Demo2");
  REQUIRE(s != nullptr);
  CHECK(s->equationsOn);
  Submit(st, "ALIGNMENTLABELSET HIDE, Demo2, EQUATIONS", log);
  CHECK_FALSE(s->equationsOn);
}

TEST_CASE("A fixed station label stays on its point and reads the new station; a relative one moves (REQ-492 AC4)",
          "[req492][alignment]") {
  Alignment a = EastLine(1000.0);
  StationOffsetLabel fixedLabel;
  fixedLabel.fixed = true;
  fixedLabel.fixedPoint = {300.0, 10.0};  // 10 north of an eastbound line: left of travel
  StationOffsetLabel relLabel;
  relLabel.internalStation = 500.0;
  relLabel.offset = 5.0;

  const alignment::StationOffsetLabelPlacement f0 = alignment::PlaceStationOffsetLabel(a, fixedLabel, 2);
  CHECK(f0.internalStation == Approx(300.0));
  CHECK(f0.offset == Approx(-10.0));
  CHECK(f0.text.find("3+00.00") != std::string::npos);

  a.Translate(100.0, 0.0);  // move the alignment east by 100

  const alignment::StationOffsetLabelPlacement f1 = alignment::PlaceStationOffsetLabel(a, fixedLabel, 2);
  CHECK(f1.point.x == Approx(300.0));  // the label did not move
  CHECK(f1.point.y == Approx(10.0));
  CHECK(f1.internalStation == Approx(200.0));  // but it now reads two stations further on... 300 - 100
  CHECK(f1.text.find("2+00.00") != std::string::npos);  // ...and its text changed (REQ-492.4)

  const alignment::StationOffsetLabelPlacement r1 = alignment::PlaceStationOffsetLabel(a, relLabel, 2);
  CHECK(r1.point.x == Approx(600.0));  // the relative label moved with the alignment: 100 + 500
  CHECK(r1.point.y == Approx(-5.0));   // 5 to the right of eastbound is south
  CHECK(r1.internalStation == Approx(500.0));
}

TEST_CASE("ALIGNMENTLABEL STATIONLABEL adds fixed and relative labels that survive save and go with the alignment",
          "[req492][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT SL1", log);
  Submit(st, "ALIGNMENTADD SL1, TANGENT, 0, 0, 90, 1000", log);
  log.clear();
  Submit(st, "ALIGNMENTLABEL SL1, STATIONLABEL, FIXED, 300, 10", log);
  Submit(st, "ALIGNMENTLABEL SL1, STATIONLABEL, RELATIVE, 5+00, 5", log);
  CHECK(LogHas(log, "fixed station label"));
  CHECK(LogHas(log, "relative station label"));
  REQUIRE(st.alignments[0].stationLabels.size() == 2);
  CHECK(st.alignments[0].stationLabels[0].fixed);
  CHECK_FALSE(st.alignments[0].stationLabels[1].fixed);
  CHECK(st.alignments[0].stationLabels[1].internalStation == Approx(500.0));

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignments.size() == 1);
  CHECK(back.alignments[0].stationLabels.size() == 2);
  CHECK(back.alignments[0].stationLabels[0].fixedPoint.x == Approx(300.0));

  log.clear();
  Submit(st, "ALIGNMENTDELETE SL1", log);
  CHECK(st.alignments.empty());  // the labels went with it (REQ-492.6)
}

TEST_CASE("ALIGNMENTLABEL STATIONLABEL refuses a station outside the alignment", "[req492][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT SL2", log);
  Submit(st, "ALIGNMENTADD SL2, TANGENT, 0, 0, 90, 100", log);
  log.clear();
  Submit(st, "ALIGNMENTLABEL SL2, STATIONLABEL, RELATIVE, 9+00, 2", log);
  CHECK(LogHas(log, "outside"));
  CHECK(st.alignments[0].stationLabels.empty());
}
