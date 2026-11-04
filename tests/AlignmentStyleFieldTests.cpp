// REQ-491 / GitHub issue #121 Phase 4 - alignment style fields (ticks, markers, arrows, label visibility) and the
// acceptance checks: edits reach every alignment that uses a style, a used style cannot be deleted, a missing reference
// falls back to Standard with a log entry, and the style survives save and reopen.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "AlignmentStyle.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"

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

}  // namespace

TEST_CASE("Standard style: ticks and markers show, direction arrows do not, labels show (REQ-491 defaults)",
          "[req491][alignment]") {
  const AlignmentStyle s = AlignmentStyles::StandardAlignmentStyle();
  CHECK(s.ticks.visible);
  CHECK(s.markers.visible);
  CHECK_FALSE(s.arrows.visible);
  CHECK(s.labelsVisible);
  CHECK(s.tickSide == 0);
  CHECK(s.majorTickLength > s.minorTickLength);
}

TEST_CASE("ALIGNMENTSTYLE SET changes the appearance items and refuses bad values without an undo step",
          "[req491][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTSTYLE NEW, Brisk", log);
  Submit(st, "ALIGNMENTSTYLE SET, Brisk, MAJORTICK, 12", log);
  Submit(st, "ALIGNMENTSTYLE SET, Brisk, TICKSIDE, LEFT", log);
  Submit(st, "ALIGNMENTSTYLE SET, Brisk, ARROWS, ON", log);
  Submit(st, "ALIGNMENTSTYLE SET, Brisk, LABELS, OFF", log);
  const AlignmentStyle* b = AlignmentStyles::Find(st.alignmentStyles, "Brisk");
  REQUIRE(b != nullptr);
  CHECK(b->majorTickLength == Approx(12.0));
  CHECK(b->tickSide == 1);
  CHECK(b->arrows.visible);
  CHECK_FALSE(b->labelsVisible);

  const size_t undoBefore = st.documents[1].undoStack.size();
  log.clear();
  Submit(st, "ALIGNMENTSTYLE SET, Brisk, MAJORTICK, -4", log);
  Submit(st, "ALIGNMENTSTYLE SET, Brisk, TICKSIDE, UP", log);
  Submit(st, "ALIGNMENTSTYLE SET, Brisk, MARKERS, MAYBE", log);
  CHECK(LogHas(log, "not valid"));
  CHECK(b->majorTickLength == Approx(12.0));  // unchanged
  CHECK(st.documents[1].undoStack.size() == undoBefore);  // and no undo step was taken
}

TEST_CASE("An edit reaches every alignment that uses the style and no other (REQ-491 AC1)", "[req491][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT SA, , 0+00", log);
  Submit(st, "ALIGNMENTADD SA, TANGENT, 0, 0, 90, 100", log);
  Submit(st, "ALIGNMENT SB, , 0+00", log);
  Submit(st, "ALIGNMENTADD SB, TANGENT, 0, 50, 90, 100", log);
  Submit(st, "ALIGNMENTSTYLE NEW, Brisk", log);
  Submit(st, "ALIGNMENTSTYLE ASSIGN, SA, Brisk", log);
  Submit(st, "ALIGNMENTSTYLE SET, Brisk, MAJORTICK, 20", log);
  const AlignmentStyle* forA = AlignmentStyles::Resolve(st.alignmentStyles, st.alignments[0].style);
  const AlignmentStyle* forB = AlignmentStyles::Resolve(st.alignmentStyles, st.alignments[1].style);
  REQUIRE(forA != nullptr);
  REQUIRE(forB != nullptr);
  CHECK(forA->majorTickLength == Approx(20.0));  // SA uses Brisk, so it sees the edit
  CHECK(forB->majorTickLength == Approx(8.0));   // SB uses Standard, which is untouched

  // Assigning SB to Brisk makes it take the edit too.
  Submit(st, "ALIGNMENTSTYLE ASSIGN, SB, Brisk", log);
  CHECK(AlignmentStyles::Resolve(st.alignmentStyles, st.alignments[1].style)->majorTickLength == Approx(20.0));
}

TEST_CASE("A style that an alignment uses cannot be deleted (REQ-491 AC2)", "[req491][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT SC, , 0+00", log);
  Submit(st, "ALIGNMENTADD SC, TANGENT, 0, 0, 90, 100", log);
  Submit(st, "ALIGNMENTSTYLE NEW, Used", log);
  Submit(st, "ALIGNMENTSTYLE ASSIGN, SC, Used", log);
  log.clear();
  Submit(st, "ALIGNMENTSTYLE DELETE, Used", log);
  CHECK(LogHas(log, "cannot be deleted"));
  CHECK(AlignmentStyles::Find(st.alignmentStyles, "Used") != nullptr);
}

TEST_CASE("A missing style reference falls back to Standard with a log entry on load (REQ-491 AC2)",
          "[req491][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT SD, , 0+00", log);
  Submit(st, "ALIGNMENTADD SD, TANGENT, 0, 0, 90, 100", log);
  Submit(st, "ALIGNMENTSTYLE NEW, Gone", log);
  Submit(st, "ALIGNMENTSTYLE ASSIGN, SD, Gone", log);
  // Drop the style from the drawing before saving, so the saved alignment names a style that is no longer there.
  st.alignmentStyles.erase(st.alignmentStyles.begin() + static_cast<std::ptrdiff_t>(
      std::find_if(st.alignmentStyles.begin(), st.alignmentStyles.end(),
                   [](const AlignmentStyle& s) { return s.name == "Gone"; }) - st.alignmentStyles.begin()));
  const std::string json = SerializeGoSurveyJson(st);
  // The alignment still says "Gone", which is no longer a style.
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignments.size() == 1);
  CHECK(back.alignments[0].style == AlignmentStyles::kStandardName);
  CHECK(LogHas(loadLog, "references missing style"));
}

TEST_CASE("Style fields survive save and reopen (REQ-491 AC3)", "[req491][req496][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTSTYLE NEW, Keep", log);
  Submit(st, "ALIGNMENTSTYLE SET, Keep, TICKSIDE, RIGHT", log);
  Submit(st, "ALIGNMENTSTYLE SET, Keep, MINORTICK, 3", log);
  Submit(st, "ALIGNMENTSTYLE SET, Keep, MARKERSIZE, 2.5", log);
  Submit(st, "ALIGNMENTSTYLE SET, Keep, ARROWS, ON", log);
  Submit(st, "ALIGNMENTSTYLE SET, Keep, ARROWLENGTH, 14", log);
  Submit(st, "ALIGNMENTSTYLE SET, Keep, LABELS, OFF", log);
  Submit(st, "ALIGNMENTSTYLE SET, Keep, TICKS, OFF", log);
  const AlignmentStyle before = *AlignmentStyles::Find(st.alignmentStyles, "Keep");

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  const AlignmentStyle* after = AlignmentStyles::Find(back.alignmentStyles, "Keep");
  REQUIRE(after != nullptr);
  CHECK(*after == before);
  CHECK(after->tickSide == 2);
  CHECK(after->markerHalfSize == Approx(2.5));
  CHECK(after->arrows.visible);
  CHECK_FALSE(after->labelsVisible);
  CHECK_FALSE(after->ticks.visible);
}
