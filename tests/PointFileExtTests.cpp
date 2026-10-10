// REQ-083 — `.csv` and `.txt` are interchangeable point-file extensions.
//
// The Win32 chooser itself cannot be linked here, so what is pinned is the rule it applies to the
// name the user typed: append the chosen filter's extension, unless the name is already spelled as
// a point file.

#include <catch2/catch_test_macros.hpp>

#include "util/PointFileExt.hpp"

#include <string_view>

using pointfile::ExtensionToAppend;

namespace {
constexpr bool kCsvFilter = false;
constexpr bool kTxtFilter = true;
} // namespace

// REQ-083 happy path: a name with no extension takes the one the user chose in the filter.
TEST_CASE("An extension-less name takes the chosen filter's extension", "[pointfile]") {
  REQUIRE(ExtensionToAppend("points", kCsvFilter) == ".csv");
  REQUIRE(ExtensionToAppend("points", kTxtFilter) == ".txt");

  // The filter only decides the default; it never overrides a name that already says which it is.
  REQUIRE(ExtensionToAppend("points.csv", kTxtFilter).empty());
  REQUIRE(ExtensionToAppend("points.txt", kCsvFilter).empty());
}

// REQ-083: a name already ending in either extension is written exactly as typed — this is the
// `points.txt.csv` regression, and the whole reason the rule is not an unconditional append.
TEST_CASE("A name already spelled as a point file gets nothing appended", "[pointfile]") {
  REQUIRE(ExtensionToAppend("points.csv", kCsvFilter).empty());
  REQUIRE(ExtensionToAppend("points.txt", kTxtFilter).empty());
  REQUIRE(ExtensionToAppend(R"(C:\jobs\2026\topo shots.txt)", kCsvFilter).empty());

  // Case-insensitive: a data collector that writes upper-case names is still writing point files.
  REQUIRE(ExtensionToAppend("POINTS.TXT", kCsvFilter).empty());
  REQUIRE(ExtensionToAppend("Points.Csv", kTxtFilter).empty());
  REQUIRE(ExtensionToAppend("POINTS.CSV", kTxtFilter).empty());
}

// REQ-083 failure mode: matching too loosely would leave a file whose name disagrees with its
// contents. Only the two known extensions suppress the default.
TEST_CASE("An unrelated extension still gets a point-file extension appended", "[pointfile]") {
  REQUIRE(ExtensionToAppend("points.dat", kCsvFilter) == ".csv");
  REQUIRE(ExtensionToAppend("job.2026", kCsvFilter) == ".csv");
  REQUIRE(ExtensionToAppend("points.csv.bak", kCsvFilter) == ".csv");
  REQUIRE(ExtensionToAppend("points.txtt", kTxtFilter) == ".txt");
  REQUIRE(ExtensionToAppend("readme.txt.gz", kTxtFilter) == ".txt");
}

// REQ-083 failure mode: the short-name and empty-name edges, which a `size() - 4` index would read
// out of bounds rather than simply failing to match.
TEST_CASE("Names shorter than an extension are handled, not indexed past", "[pointfile]") {
  REQUIRE(ExtensionToAppend("", kCsvFilter) == ".csv");
  REQUIRE(ExtensionToAppend("", kTxtFilter) == ".txt");
  REQUIRE(ExtensionToAppend("p", kCsvFilter) == ".csv");
  REQUIRE(ExtensionToAppend(".ts", kCsvFilter) == ".csv");
  REQUIRE(ExtensionToAppend("csv", kCsvFilter) == ".csv"); // no dot: not an extension
  REQUIRE(ExtensionToAppend("txt", kTxtFilter) == ".txt");

  // A trailing bare dot is not an extension either, so the default is appended to it as-is. Pinned
  // because it is deliberate rather than overlooked — stripping the dot would be a second rule.
  REQUIRE(ExtensionToAppend("points.", kCsvFilter) == ".csv");
}

// REQ-083: `.csv` and `.txt` are treated as one class — neither is privileged over the other by
// anything except which filter the user had selected.
TEST_CASE("Neither extension is privileged over the other", "[pointfile]") {
  for (const bool filter : {kCsvFilter, kTxtFilter}) {
    REQUIRE(ExtensionToAppend("survey.csv", filter).empty());
    REQUIRE(ExtensionToAppend("survey.txt", filter).empty());
  }
}
