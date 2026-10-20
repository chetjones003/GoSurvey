// REQ-075 / issue #264 — a document written BEFORE REQ-075 must not lose its breaklines on load.
//
// Was tests/headless/transcripts/req075-legacy-breaklineids-migrate.txt, driven by `OPEN
// samples/legacy-breaklineids.gs`. Issue #264 retired standalone `.gs` as an openable document
// format, so this fixture can no longer be opened that way; the JSON schema and its migration
// path (LoadGoSurveyFromJsonUtf8 / MigrateGsDocument) are unaffected and still load exactly as
// before — this test now feeds the fixture's bytes to that entry point directly.
//
// REQ-075's Add Breaklines dialog collects a description, so a breakline became an object
// (`breaklines: [{entityId, description}]`) where REQ-069 had written a bare id array
// (`breaklineIds: [7]`). The reader takes both. This is the case that cannot be checked by
// looking at the screen — a dropped breakline looks exactly like a surface that never had one.
//
// The fixture is a real saved drawing with its breaklines block rewritten by hand to the old form.

#include <catch2/catch_test_macros.hpp>


#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"

namespace {


std::filesystem::path FixturePath() {
  return std::filesystem::path(GOSURVEY_SAMPLES_DIR) / "legacy-breaklineids.json";
}

std::string ReadFile(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  REQUIRE(f.good());
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int BreaklineCountFor(AppCommandState& st, const std::string& surfaceName) {
  for (const auto& s : st.cadSurfaces)
    if (s.name == surfaceName)
      return static_cast<int>(s.breaklines.size());
  return -1;
}

}  // namespace

TEST_CASE("A legacy breaklineIds array migrates to the current breaklines form (REQ-075)", "[gs][migrate][req075]") {
  const std::string legacyJson = ReadFile(FixturePath());

  AppCommandState st;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(st, legacyJson, log));

  // Read in the legacy form at all.
  const int n = BreaklineCountFor(st, "Legacy EG");
  REQUIRE(n == 1);

  // Re-serializing migrates it to the current form, once, with no separate step; it survives
  // that trip...
  const std::string migratedA = SerializeGoSurveyJson(st);
  AppCommandState reloadedA;
  std::vector<std::string> logA;
  REQUIRE(LoadGoSurveyFromJsonUtf8(reloadedA, migratedA, logA));
  REQUIRE(BreaklineCountFor(reloadedA, "Legacy EG") == 1);

  // ...and the migration is idempotent, which is REQ-079's rule for every other normalisation.
  const std::string migratedB = SerializeGoSurveyJson(reloadedA);
  REQUIRE(migratedA == migratedB);
}
