// REQ-401 (D-2026-10-09-b, issue #751/#753 V3) — typed command-line equivalents for layout
// management: LAYOUT NEW/RENAME/DELETE, PAGESETUP, VPSCALE. Driven the way the command line
// drives them (`ProcessCommandLineSubmit`), the same pattern PropertyCommandTests.cpp uses.

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

bool AnyLogContains(const std::vector<std::string>& log, const std::string& needle) {
  for (const std::string& line : log)
    if (line.find(needle) != std::string::npos)
      return true;
  return false;
}

} // namespace

TEST_CASE("LAYOUT NEW creates a layout with a unique name", "[layoutcmd]") {
  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(st.paperLayouts.empty());

  Submit(st, "LAYOUT NEW", log);
  REQUIRE(st.paperLayouts.size() == 1);

  Submit(st, "LAYOUT NEW", log);
  REQUIRE(st.paperLayouts.size() == 2);
  REQUIRE(st.paperLayouts[0].name != st.paperLayouts[1].name);
}

TEST_CASE("LAYOUT NEW with a name uses it, refusing a duplicate", "[layoutcmd]") {
  AppCommandState st;
  std::vector<std::string> log;

  Submit(st, "LAYOUT NEW Site Plan", log);
  REQUIRE(st.paperLayouts.size() == 1);
  REQUIRE(st.paperLayouts[0].name == "Site Plan");

  log.clear();
  Submit(st, "LAYOUT NEW Site Plan", log);
  REQUIRE(st.paperLayouts.size() == 2);
  REQUIRE(st.paperLayouts[1].name != "Site Plan");  // duplicate refused, auto-named instead
  REQUIRE(AnyLogContains(log, "already exists"));
}

TEST_CASE("LAYOUT RENAME changes the target layout's name", "[layoutcmd]") {
  AppCommandState st;
  std::vector<std::string> log;
  Submit(st, "LAYOUT NEW Sheet1", log);
  log.clear();

  Submit(st, "LAYOUT RENAME Sheet1 Site Plan", log);
  REQUIRE(st.paperLayouts[0].name == "Site Plan");

  // Default target (no name given) is the active layout.
  st.activeSpaceIndex = 0;
  log.clear();
  Submit(st, "LAYOUT RENAME Final Plan", log);
  REQUIRE(st.paperLayouts[0].name == "Final Plan");
}

TEST_CASE("LAYOUT RENAME refuses a name collision", "[layoutcmd]") {
  AppCommandState st;
  std::vector<std::string> log;
  Submit(st, "LAYOUT NEW Sheet1", log);
  Submit(st, "LAYOUT NEW Sheet2", log);
  log.clear();

  Submit(st, "LAYOUT RENAME Sheet1 Sheet2", log);
  REQUIRE(st.paperLayouts[0].name == "Sheet1");  // unchanged
  REQUIRE(AnyLogContains(log, "already exists"));
}

TEST_CASE("LAYOUT DELETE removes the target layout and is undoable", "[layoutcmd]") {
  AppCommandState st;
  std::vector<std::string> log;
  Submit(st, "LAYOUT NEW Sheet1", log);
  Submit(st, "LAYOUT NEW Sheet2", log);
  REQUIRE(st.paperLayouts.size() == 2);

  log.clear();
  Submit(st, "LAYOUT DELETE Sheet1", log);
  REQUIRE(st.paperLayouts.size() == 1);
  REQUIRE(st.paperLayouts[0].name == "Sheet2");

  log.clear();
  Submit(st, "UNDO", log);
  REQUIRE(st.paperLayouts.size() == 2);  // delete is undoable (matches tab-bar delete, #763)
}

TEST_CASE("LAYOUT with no target refuses clearly from model space", "[layoutcmd]") {
  AppCommandState st;
  std::vector<std::string> log;
  st.activeSpaceIndex = kModelSpaceIndex;

  Submit(st, "LAYOUT DELETE", log);
  REQUIRE(st.paperLayouts.empty());
  REQUIRE(AnyLogContains(log, "no paper layout"));
}

TEST_CASE("PAGESETUP opens the dialog for the active layout", "[layoutcmd]") {
  AppCommandState st;
  std::vector<std::string> log;
  Submit(st, "LAYOUT NEW Sheet1", log);
  st.activeSpaceIndex = 0;

  log.clear();
  Submit(st, "PAGESETUP", log);
  REQUIRE(st.showPageSetupManager);
  REQUIRE(st.pageSetupLayoutIdx == 0);
}

TEST_CASE("PAGESETUP refuses in model space", "[layoutcmd]") {
  AppCommandState st;
  std::vector<std::string> log;
  st.activeSpaceIndex = kModelSpaceIndex;

  Submit(st, "PAGESETUP", log);
  REQUIRE_FALSE(st.showPageSetupManager);
  REQUIRE(AnyLogContains(log, "not available in model space"));
}

TEST_CASE("VPSCALE sets the resolved current viewport's scale", "[layoutcmd]") {
  AppCommandState st;
  std::vector<std::string> log;
  Submit(st, "LAYOUT NEW Sheet1", log);
  st.activeSpaceIndex = 0;
  st.paperLayouts[0].viewports.push_back(Viewport{});
  st.selectedViewports = {0};
  st.selectedViewportIndex = 0;

  log.clear();
  Submit(st, "VPSCALE 1:20", log);
  REQUIRE(st.paperLayouts[0].viewports[0].scaleModelPerPaperIn == 20.f);  // Feet default: 1 * 20

  log.clear();
  Submit(st, "VPSCALE 50", log);
  REQUIRE(st.paperLayouts[0].viewports[0].scaleModelPerPaperIn == 50.f);
}

TEST_CASE("VPSCALE refuses clearly with no current viewport", "[layoutcmd]") {
  AppCommandState st;
  std::vector<std::string> log;
  Submit(st, "LAYOUT NEW Sheet1", log);
  st.activeSpaceIndex = 0;

  log.clear();
  Submit(st, "VPSCALE 1:20", log);
  REQUIRE(AnyLogContains(log, "no current viewport"));
}
