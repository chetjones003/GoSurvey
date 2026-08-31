// REQ-308 / D-2026-08-30-b — the recent-drawings MRU store. Pure: driven against a temp file, no
// %APPDATA%, no window. Covers the acceptance conditions that are decidable without a viewport —
// persistence, de-dup/reorder, the cap, thumbnail carry-over, removal, and corruption tolerance.

#include "RecentDrawings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path TempStore(const char* stem) {
  auto p = std::filesystem::temp_directory_path() /
           ("gosurvey-recent-test-" + std::string(stem) + ".json");
  std::error_code ec;
  std::filesystem::remove(p, ec);
  return p;
}

void WriteText(const std::filesystem::path& p, const std::string& s) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << s;
}

}  // namespace

TEST_CASE("recent store round-trips newest-first", "[req308]") {
  const auto store = TempStore("roundtrip");
  recent::Note(store, "C:/jobs/a.dwg", "", 100);
  recent::Note(store, "C:/jobs/b.dwg", "", 200);
  recent::Note(store, "C:/jobs/c.dwg", "", 300);

  auto v = recent::Load(store);
  REQUIRE(v.size() == 3);
  CHECK(v[0].path == "C:/jobs/c.dwg");
  CHECK(v[1].path == "C:/jobs/b.dwg");
  CHECK(v[2].path == "C:/jobs/a.dwg");
  CHECK(v[0].name == "c");
  CHECK(v[0].lastOpenedUnix == 300);
  std::filesystem::remove(store);
}

TEST_CASE("re-noting a path moves it to the front without duplicating", "[req308]") {
  const auto store = TempStore("dedupe");
  recent::Note(store, "C:/jobs/a.dwg", "", 100);
  recent::Note(store, "C:/jobs/b.dwg", "", 200);
  recent::Note(store, "C:/jobs/a.dwg", "", 300);  // touch a again

  auto v = recent::Load(store);
  REQUIRE(v.size() == 2);
  CHECK(v[0].path == "C:/jobs/a.dwg");
  CHECK(v[0].lastOpenedUnix == 300);
  CHECK(v[1].path == "C:/jobs/b.dwg");
  std::filesystem::remove(store);
}

TEST_CASE("path match is case- and separator-insensitive", "[req308]") {
  const auto store = TempStore("pathkey");
  recent::Note(store, "C:/Jobs/A.dwg", "", 100);
  recent::Note(store, "c:\\jobs\\a.dwg", "", 200);
  auto v = recent::Load(store);
  REQUIRE(v.size() == 1);
  CHECK(v[0].lastOpenedUnix == 200);
  std::filesystem::remove(store);
}

TEST_CASE("an earlier thumbnail survives a later note with no thumbnail", "[req308]") {
  const auto store = TempStore("thumbcarry");
  recent::Note(store, "C:/jobs/a.dwg", "", 100);            // noted on open
  recent::Note(store, "C:/jobs/a.dwg", "deadbeef.bmp", 101);  // thumbnail captured
  recent::Note(store, "C:/jobs/a.dwg", "", 200);            // re-opened, no new capture yet

  auto v = recent::Load(store);
  REQUIRE(v.size() == 1);
  CHECK(v[0].thumb == "deadbeef.bmp");
  CHECK(v[0].lastOpenedUnix == 200);
  std::filesystem::remove(store);
}

TEST_CASE("the store is capped at kMaxEntries", "[req308]") {
  const auto store = TempStore("cap");
  for (int i = 0; i < recent::kMaxEntries + 8; ++i)
    recent::Note(store, "C:/jobs/d" + std::to_string(i) + ".dwg", "", i);

  auto v = recent::Load(store);
  REQUIRE(static_cast<int>(v.size()) == recent::kMaxEntries);
  CHECK(v.front().path == "C:/jobs/d" + std::to_string(recent::kMaxEntries + 7) + ".dwg");
  std::filesystem::remove(store);
}

TEST_CASE("Remove drops a path; removing an absent path is a no-op", "[req308]") {
  const auto store = TempStore("remove");
  recent::Note(store, "C:/jobs/a.dwg", "", 100);
  recent::Note(store, "C:/jobs/b.dwg", "", 200);

  recent::Remove(store, "C:/jobs/nope.dwg");
  CHECK(recent::Load(store).size() == 2);

  recent::Remove(store, "C:/jobs/a.dwg");
  auto v = recent::Load(store);
  REQUIRE(v.size() == 1);
  CHECK(v[0].path == "C:/jobs/b.dwg");
  std::filesystem::remove(store);
}

TEST_CASE("a missing or corrupt store loads as an empty list", "[req308]") {
  const auto missing = TempStore("missing");
  CHECK(recent::Load(missing).empty());

  const auto corrupt = TempStore("corrupt");
  WriteText(corrupt, "{ this is not json ][");
  CHECK(recent::Load(corrupt).empty());

  const auto notArray = TempStore("notarray");
  WriteText(notArray, "{\"path\":\"x\"}");
  CHECK(recent::Load(notArray).empty());

  // Note still works over a corrupt file (it overwrites with a valid array).
  recent::Note(corrupt, "C:/jobs/a.dwg", "", 100);
  CHECK(recent::Load(corrupt).size() == 1);

  std::filesystem::remove(corrupt);
  std::filesystem::remove(notArray);
}
