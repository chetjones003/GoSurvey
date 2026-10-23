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
