// Issue #785 — a merge (PR #680) deleted eight command handlers from CadCommands.cpp while leaving
// their rows in the command registry untouched. The registry is what HELP, the fuzzy matcher and
// autocomplete read, so nothing about the drawing looked wrong; the only symptom was that typing
// e.g. PROJECTSETTINGS produced no error and no effect. That shape of bug — a registry row with no
// code behind it any more — is invisible to every other test, which calls commands by name and
// would not notice `log` staying empty.
//
// This test enumerates the live registry (`RegisteredCommandPrimariesForTest`, which reads the same
// `kRegistry` table HELP does) and runs each primary, bare, through the real command-line entry
// point (`ProcessCommandLineSubmit`) on a freshly constructed `AppCommandState` — the same thing a
// user does the first time they type a command's name. A command that is implemented always leaves
// some trace: a prompt, a usage message, or an observable state change. A command whose handler was
// quietly deleted leaves nothing.
//
// Linked into GoSurveySnapTests: needs gosurvey_domain for ProcessCommandLineSubmit / CadCommands.cpp.

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "CadCommands.hpp"

namespace {

/// Commands that legitimately produce no log line and no detectable state change from a bare,
/// argument-less invocation on a blank drawing — they only raise a UI flag that a human reads on
/// screen, or they need a selection/typed argument to do anything observable at all. Each entry
/// names the reason so a future reviewer can tell "known no-op" from "regression."
const std::set<std::string>& SilentByDesign() {
  static const std::set<std::string> s = {
      // Opens the PDF viewer's Split dialog on the focused viewer; nothing to report if no PDF
      // viewer is focused on a blank drawing (REQ-389).
      "pdfsplit",
      // Sets `st.pendingZoomExtents`, consumed by the next viewport frame — a real, silent state
      // change this test's `HasAnyEffect` does not look at (it only checks `log` and `st.active`).
      "zoomextents",
  };
  return s;
}

bool HasAnyEffect(AppCommandState& before, AppCommandState& st, const std::vector<std::string>& log) {
  if (!log.empty())
    return true;
  // A handler that changes `st.active` (starts an interactive tool) without logging anything is
  // still a handler, not a no-op.
  return st.active != before.active || st.blockEditPickerOpen != before.blockEditPickerOpen;
}

} // namespace

TEST_CASE("every registered command leaves a trace when typed bare", "[commands][registry][issue785]") {
  const std::vector<std::string> primaries = RegisteredCommandPrimariesForTest();
  REQUIRE(primaries.size() > 100); // sanity: the registry itself did not go missing

  std::vector<std::string> silentRegressions;
  for (const std::string& primary : primaries) {
    if (SilentByDesign().count(primary) != 0)
      continue;

    AppCommandState st;
    AppCommandState before;
    std::vector<std::string> log;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", primary.c_str());
    ProcessCommandLineSubmit(buf, sizeof(buf), st, log);

    if (!HasAnyEffect(before, st, log))
      silentRegressions.push_back(primary);
  }

  std::string joined;
  for (const std::string& s : silentRegressions)
    joined += s + " ";
  INFO("Commands registered but producing no effect when typed bare (merge-dropped handler?): " << joined);
  CHECK(silentRegressions.empty());
}

TEST_CASE("Block commands reject invalid numeric fields before changing the drawing", "[commands][issue1658]") {
  AppCommandState st;
  st.documents.resize(1);
  st.activeDrawingIdx = 0;
  std::vector<std::string> log;
  const auto submit = [&](std::string command) {
    log.clear();
    ProcessCommandLineSubmit(command.data(), static_cast<int>(command.size() + 1), st, log);
  };

  submit("MKLINE +1e0, 2.5, -3, 4");
  REQUIRE(st.drawing.lineCount() == 1);
  submit("SELLINE");
  submit("BLOCK Shape, 1, 2, RETAIN, 3");
  REQUIRE(st.drawing.blockDefs.size() == 1);
  submit("INSERT Shape, 5, 6, 2, 3, 15, 4, 7");
  REQUIRE(st.drawing.cadBlockRefs.size() == 1);
  submit("MKLINE 0,0,1,1");
  submit("UNDO");
  REQUIRE(st.drawing.lineCount() == 1);
  submit("SELBLOCK");
  submit("COPYCLIP");
  REQUIRE(st.clipboard.content.cadBlockRefs.size() == 1);

  const auto lines = st.drawing.userLinesFlat;
  const auto xf = st.drawing.cadBlockRefs.front().xf;
  const auto undoDepth = st.documents.front().undoStack.size();
  const auto redoDepth = st.documents.front().redoStack.size();
  REQUIRE(undoDepth > 0);
  REQUIRE(redoDepth > 0);

  // Each marker identifies a numeric field to exercise independently.
  for (const std::string format : {
           "MKLINE @,@,@,@", "BLOCK Other,@,@,CONVERT,@", "-BLOCK Other,@,@,CONVERT,@",
           "INSERT Shape,@,@,@,@,@,@,@", "MOVESEL @,@", "COPYSEL @,@", "ROTATESEL @,@,@",
           "SCALESEL @,@,@", "MIRRORSEL @,@,@,@", "PASTEBLOCK @,@"}) {
    for (size_t pos = format.find('@'); pos != std::string::npos; pos = format.find('@', pos + 1)) {
      for (const std::string invalid : {"nan", "+inf", "-inf", "1junk", "1e100"}) {
        std::string command = format;
        for (char& c : command) {
          if (c == '@')
            c = '1';
        }
        command.replace(pos, 1, invalid);
        CAPTURE(command);
        submit(command);
        REQUIRE_FALSE(log.empty());
        CHECK(log.back().find("must be numbers") != std::string::npos);
        CHECK(st.drawing.userLinesFlat == lines);
        CHECK(st.drawing.blockDefs.size() == 1);
        REQUIRE(st.drawing.cadBlockRefs.size() == 1);
        const auto& current = st.drawing.cadBlockRefs.front().xf;
        CHECK(current.x == xf.x);
        CHECK(current.y == xf.y);
        CHECK(current.z == xf.z);
        CHECK(current.sx == xf.sx);
        CHECK(current.sy == xf.sy);
        CHECK(current.sz == xf.sz);
        CHECK(current.rotZ == xf.rotZ);
        CHECK(st.documents.front().undoStack.size() == undoDepth);
        CHECK(st.documents.front().redoStack.size() == redoDepth);
      }
    }
  }

  for (const char* command : {"INSERT Shape, nan", "INSERT Shape, 5, 6, nan", "PASTEBLOCK nan"}) {
    CAPTURE(command);
    submit(command);
    CHECK(st.drawing.cadBlockRefs.size() == 1);
    CHECK(st.documents.front().undoStack.size() == undoDepth);
    CHECK(st.documents.front().redoStack.size() == redoDepth);
  }
}
