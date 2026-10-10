#include <catch2/catch_test_macros.hpp>

#include "io/SurveyCsvValidate.hpp"

#include <string>
#include <unordered_set>
#include <vector>

using survey_csv::ClassifyFileState;
using survey_csv::FileState;
using survey_csv::RowId;
using survey_csv::ScanDuplicateIds;

// REQ-041: the import file is classified into distinct, specific states.
TEST_CASE("File-state classification distinguishes missing/empty/locked/ok", "[surveycsv]") {
  REQUIRE(ClassifyFileState(/*exists*/ false, /*empty*/ false, /*canOpen*/ false) == FileState::NotFound);
  // Non-existence dominates whatever else was probed.
  REQUIRE(ClassifyFileState(false, true, true) == FileState::NotFound);

  REQUIRE(ClassifyFileState(true, /*empty*/ true, false) == FileState::Empty);

  // Exists, has bytes, but could not be opened -> locked / permission denied.
  REQUIRE(ClassifyFileState(true, false, /*canOpen*/ false) == FileState::CannotOpen);

  REQUIRE(ClassifyFileState(true, false, true) == FileState::Ok);

  // Each non-Ok state carries a specific, non-empty message; Ok has none.
  REQUIRE(std::string(survey_csv::FileStateMessage(FileState::NotFound)).find("not found") != std::string::npos);
  REQUIRE(std::string(survey_csv::FileStateMessage(FileState::Empty)).find("empty") != std::string::npos);
  REQUIRE(std::string(survey_csv::FileStateMessage(FileState::CannotOpen)).find("another application") !=
          std::string::npos);
  REQUIRE(std::string(survey_csv::FileStateMessage(FileState::Ok)).empty());
}

// REQ-041 happy path: unique IDs with no session collisions yield no diagnostics.
TEST_CASE("Unique IDs produce no duplicate diagnostics", "[surveycsv]") {
  const std::vector<RowId> rows = {{100, 2}, {101, 3}, {102, 4}};
  const std::unordered_set<int> session = {1, 2, 3};
  const auto scan = ScanDuplicateIds(rows, session);
  REQUIRE(scan.messages.empty());
  REQUIRE(scan.badLines.empty());
}

// REQ-041 failure mode: a duplicate within the file names both lines and marks the later as skipped.
TEST_CASE("Within-file duplicate ID is flagged and skipped", "[surveycsv]") {
  const std::vector<RowId> rows = {{1042, 4}, {200, 9}, {1042, 17}};
  const std::unordered_set<int> session; // empty session
  const auto scan = ScanDuplicateIds(rows, session);

  REQUIRE(scan.messages.size() == 1);
  REQUIRE(scan.messages[0].find("Duplicate point ID 1042") != std::string::npos);
  REQUIRE(scan.messages[0].find("line 4") != std::string::npos);
  REQUIRE(scan.messages[0].find("line 17") != std::string::npos);

  // Only the second occurrence is skipped; the first is kept.
  REQUIRE(scan.badLines.count(17) == 1);
  REQUIRE(scan.badLines.count(4) == 0);
}

// REQ-041 failure mode: an ID colliding with an existing session point is named as such.
TEST_CASE("Session collision is reported and skipped", "[surveycsv]") {
  const std::vector<RowId> rows = {{1042, 17}};
  const std::unordered_set<int> session = {1042};
  const auto scan = ScanDuplicateIds(rows, session);

  REQUIRE(scan.messages.size() == 1);
  REQUIRE(scan.messages[0].find("Point ID 1042 already exists in the drawing") != std::string::npos);
  REQUIRE(scan.messages[0].find("line 17") != std::string::npos);
  REQUIRE(scan.badLines.count(17) == 1);
}

// BUG-014 / REQ-041 revision 3: the summary shown after an import reports what that import DID.
// The guard that matters is the last one — a completed import must never carry the pre-import
// failure wording, which is exactly what the panel showed when it re-validated the file it had
// just consumed.
TEST_CASE("A completed import is reported as an outcome, not as a validation failure", "[surveycsv]") {
  const std::string clean = survey_csv::ImportOutcomeSummary(5, 0);
  REQUIRE(clean.find("Imported 5 point(s)") != std::string::npos);
  REQUIRE(clean.find("0 row(s) skipped") != std::string::npos);

  // Skips are named, never rounded away — the count is why the panel stays open.
  const std::string partial = survey_csv::ImportOutcomeSummary(3, 2);
  REQUIRE(partial.find("Imported 3 point(s)") != std::string::npos);
  REQUIRE(partial.find("2 row(s) skipped") != std::string::npos);

  // An import that placed nothing still reports honestly rather than falling back to a file-level
  // verdict it no longer has any basis for.
  const std::string none = survey_csv::ImportOutcomeSummary(0, 4);
  REQUIRE(none.find("Imported 0 point(s)") != std::string::npos);
  REQUIRE(none.find("4 row(s) skipped") != std::string::npos);

  // The regression itself: none of these may read as a refusal.
  for (const std::string& s : {clean, partial, none}) {
    REQUIRE(s.find("Cannot import") == std::string::npos);
    REQUIRE(s.find("no valid data rows") == std::string::npos);
    REQUIRE(s.find("already exists in the drawing") == std::string::npos);
  }
}

// REQ-041: when an ID both collides with the session and repeats in the file, the
// session-collision message takes precedence and every offending row is skipped.
TEST_CASE("Session collision takes precedence over within-file duplicate", "[surveycsv]") {
  const std::vector<RowId> rows = {{1042, 5}, {1042, 8}};
  const std::unordered_set<int> session = {1042};
  const auto scan = ScanDuplicateIds(rows, session);

  REQUIRE(scan.messages.size() == 2);
  for (const auto& m : scan.messages)
    REQUIRE(m.find("already exists in the drawing") != std::string::npos);
  REQUIRE(scan.badLines.count(5) == 1);
  REQUIRE(scan.badLines.count(8) == 1);
}
