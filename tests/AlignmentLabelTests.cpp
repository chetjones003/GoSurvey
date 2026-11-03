#include "AlignmentLabelSet.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentLabels.hpp"

#include <catch2/catch_test_macros.hpp>

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
