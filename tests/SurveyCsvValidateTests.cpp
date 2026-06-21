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
