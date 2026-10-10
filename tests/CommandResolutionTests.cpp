// REQ-405 (GitHub issue #766, follow-up to #751/#753 N2/E5) — the command line must never silently
// auto-run a DIFFERENT command than the one typed. `TryStrongFuzzyDispatch`'s subsequence-match
// auto-run let `ERASE` open Drawing Settings (a subsequence of `editdrawingsettings`), `RENAME` run
// SURFACERENAME, and `TABLE` run VOLTABLE — each silently, with no suggestion logged first. These
// tests pin the three reported examples and the new ERASE alias of DELETE.
#include "CadCommands.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

void Submit(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  std::vector<char> buf(line.begin(), line.end());
  buf.push_back('\0');
  ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), st, log);
}

bool LogContains(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& l : log)
    if (l.find(needle) != std::string::npos)
      return true;
  return false;
}

/// One line entity, selected, so DELETE/ERASE has something to act on.
AppCommandState OneSelectedLine() {
  AppCommandState st;
  st.userLinesFlat = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f};
  st.userLineAttrs.resize(1);
  st.userLineAttrs[0].id = 1;
  st.nextEntityId = 2;
  st.selection.push_back(SelectedEntity{SelectedEntity::Type::LineSeg, 0});
  return st;
}

}  // namespace

TEST_CASE("ERASE is an exact alias of DELETE and never opens Drawing Settings (issue #766)",
          "[req405][command-resolution]") {
  std::vector<std::string> log;
  AppCommandState st = OneSelectedLine();
  Submit(st, "ERASE", log);
  CHECK_FALSE(st.showDrawingSettingsWindow);
  CHECK(st.userLinesFlat.empty());
}

TEST_CASE("lowercase erase also resolves to DELETE (issue #766)", "[req405][command-resolution]") {
  std::vector<std::string> log;
  AppCommandState st = OneSelectedLine();
  Submit(st, "erase", log);
  CHECK_FALSE(st.showDrawingSettingsWindow);
  CHECK(st.userLinesFlat.empty());
}

TEST_CASE("RENAME with no exact match never auto-runs SURFACERENAME (issue #766)",
          "[req405][command-resolution]") {
  std::vector<std::string> log;
  AppCommandState st;
  Submit(st, "RENAME", log);
  CHECK_FALSE(LogContains(log, "SURFACERENAME"));
}

TEST_CASE("TABLE with no exact match never auto-runs VOLTABLE (issue #766)",
          "[req405][command-resolution]") {
  std::vector<std::string> log;
  AppCommandState st;
  Submit(st, "TABLE", log);
  CHECK_FALSE(LogContains(log, "VOLTABLE"));
  CHECK_FALSE(LogContains(log, "VOLREPORT"));
}

TEST_CASE("An unresolved command name is reported, never silently substituted (issue #766)",
          "[req405][command-resolution]") {
  std::vector<std::string> log;
  AppCommandState st;
  Submit(st, "RENAME", log);
  REQUIRE_FALSE(log.empty());
  const bool said =
      LogContains(log, "Did you mean") || LogContains(log, "Unknown command");
  CHECK(said);
  CHECK_FALSE(LogContains(log, "fuzzy command match"));
}
