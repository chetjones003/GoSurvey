#include "AlignmentStyle.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"

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

}  // namespace

TEST_CASE("Alignment style table resolves Standard and counts users", "[req491][alignment]") {
  std::vector<AlignmentStyle> styles = AlignmentStyles::DefaultAlignmentStyles();
  REQUIRE(AlignmentStyles::Find(styles, "Standard") != nullptr);
  alignment::Alignment a;
  a.name = "A1";
  a.style = "Standard";
  REQUIRE(AlignmentStyles::Resolve(styles, a.style) != nullptr);
  REQUIRE(AlignmentStyles::Resolve(styles, "Missing")->name == "Standard");
  REQUIRE(AlignmentStyles::CountAlignmentsResolvingTo({a}, styles, "Standard") == 1);
}

TEST_CASE("ALIGNMENTSTYLE assigns style and hides a component", "[req491][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT S", log);
  Submit(st, "ALIGNMENTADD S, TANGENT, 0, 0, 0, 10", log);
  Submit(st, "ALIGNMENTSTYLE NEW, Road", log);
  Submit(st, "ALIGNMENTSTYLE ASSIGN, S, Road", log);
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].style == "Road");
  Submit(st, "ALIGNMENTSTYLE HIDE, Road, TANGENT", log);
  AlignmentStyle* road = AlignmentStyles::Find(st.alignmentStyles, "Road");
  REQUIRE(road != nullptr);
  CHECK_FALSE(road->tangent.visible);
  Submit(st, "UNDO", log);
  road = AlignmentStyles::Find(st.alignmentStyles, "Road");
  REQUIRE(road != nullptr);
  CHECK(road->tangent.visible);
}

TEST_CASE("Alignment styles round-trip through save", "[req491][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTSTYLE NEW, Custom", log);
  Submit(st, "ALIGNMENTSTYLE COLOR, Custom, CURVE, Red", log);
  std::string json = SerializeGoSurveyJson(st);
  AppCommandState loaded = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(loaded, json, loadLog));
  const AlignmentStyle* custom = AlignmentStyles::Find(loaded.alignmentStyles, "Custom");
  REQUIRE(custom != nullptr);
  CHECK(custom->curve.color == "Red");
}
