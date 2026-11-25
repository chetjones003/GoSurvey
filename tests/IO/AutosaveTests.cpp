#include "catch_amalgamated.hpp"

#include "Autosave.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct TempDir {
  fs::path path;
  TempDir() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path = fs::temp_directory_path() / ("gosurvey_autosave_test_" + std::to_string(now));
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

autosave::EntryMeta MakeMeta(std::uint32_t tabUid, const std::string& name, std::int64_t savedAtUnix,
                             const std::string& origPath = {}, const std::string& projPath = {}) {
  autosave::EntryMeta m;
  m.tabUid = tabUid;
  m.tabName = name;
  m.originalFilePath = origPath;
  m.projectFilePath = projPath;
  m.savedAtUnix = savedAtUnix;
  m.appVersion = "0.8.9";
  return m;
}

}  // namespace

TEST_CASE("Autosave writes, scans dead sessions newest-first, and reads payload", "[autosave]") {
  TempDir tmp;
  const std::string s1 = autosave::MakeSessionId(1111, 1000);
  const std::string s2 = autosave::MakeSessionId(2222, 2000);
  const std::string current = autosave::MakeSessionId(3333, 3000);

  std::string err;
  REQUIRE(autosave::WriteEntry(tmp.path, s1, 1111, 1000, MakeMeta(1, "Drawing 1", 1700000100),
                               R"({"format":"gosurvey","v":1})", &err));
  REQUIRE(autosave::WriteEntry(tmp.path, s1, 1111, 1000,
                               MakeMeta(2, "Boundary", 1700000300, "C:\\Jobs\\Boundary.dwg", "C:\\Jobs\\Job.gsproj"),
                               R"({"format":"gosurvey","v":2})", &err));
  REQUIRE(autosave::WriteEntry(tmp.path, s2, 2222, 2000, MakeMeta(1, "Topo", 1700000200),
                               R"({"format":"gosurvey","v":3})", &err));
  REQUIRE(autosave::WriteEntry(tmp.path, current, 3333, 3000, MakeMeta(1, "LiveTab", 1700000999),
                               R"({"format":"gosurvey","v":4})", &err));

  // No temp files remain after committed writes.
  for (const auto& entry : fs::recursive_directory_iterator(tmp.path)) {
    REQUIRE(entry.path().extension() != ".tmp");
  }

  const auto found = autosave::ScanRecoverable(tmp.path, current, [](std::uint32_t) { return false; });
  REQUIRE(found.size() == 3);
  CHECK(found[0].meta.tabName == "Boundary");
  CHECK(found[0].meta.originalFilePath == "C:\\Jobs\\Boundary.dwg");
  CHECK(found[0].meta.projectFilePath == "C:\\Jobs\\Job.gsproj");
  CHECK(found[1].meta.tabName == "Topo");
  CHECK(found[2].meta.tabName == "Drawing 1");

  std::string payload;
  REQUIRE(autosave::ReadPayload(found[0], &payload, &err));
  CHECK(payload == R"({"format":"gosurvey","v":2})");
}

TEST_CASE("Autosave scanner skips live sessions owned by another running process", "[autosave]") {
  TempDir tmp;
  const std::string liveOther = autosave::MakeSessionId(4444, 1000);
  const std::string deadOther = autosave::MakeSessionId(5555, 2000);
  const std::string current = autosave::MakeSessionId(6666, 3000);

  REQUIRE(autosave::WriteEntry(tmp.path, liveOther, 4444, 1000, MakeMeta(1, "OtherRunning", 1700000500), "{}"));
  REQUIRE(autosave::WriteEntry(tmp.path, deadOther, 5555, 2000, MakeMeta(1, "CrashedDoc", 1700000400), "{}"));

  const auto found = autosave::ScanRecoverable(tmp.path, current, [](std::uint32_t pid) { return pid == 4444; });
  REQUIRE(found.size() == 1);
  CHECK(found[0].meta.tabName == "CrashedDoc");
  // Live session folder must not have been deleted or modified.
  CHECK(fs::exists(autosave::SessionDirectory(tmp.path, liveOther)));
}

TEST_CASE("Autosave clean save, tab close, discard, and session cleanup remove files and prune empty folders",
          "[autosave]") {
  TempDir tmp;
  const std::string session = autosave::MakeSessionId(7777, 1000);
  const fs::path sessionDir = autosave::SessionDirectory(tmp.path, session);

  REQUIRE(autosave::WriteEntry(tmp.path, session, 7777, 1000, MakeMeta(1, "Tab A", 100), "{\"a\":1}"));
  REQUIRE(autosave::WriteEntry(tmp.path, session, 7777, 1000, MakeMeta(2, "Tab B", 200), "{\"b\":2}"));
  REQUIRE(fs::exists(sessionDir));

  // Removing one tab leaves the second tab intact.
  autosave::RemoveTabEntry(tmp.path, session, 1);
  CHECK(fs::exists(sessionDir));
  auto found = autosave::ScanRecoverable(tmp.path, "other", [](std::uint32_t) { return false; });
  REQUIRE(found.size() == 1);
  CHECK(found[0].meta.tabUid == 2);

  // Removing the last tab prunes the session directory automatically.
  autosave::RemoveTabEntry(tmp.path, session, 2);
  CHECK_FALSE(fs::exists(sessionDir));

  // DiscardAll removes entries across sessions and prunes empty directories.
  const std::string sA = autosave::MakeSessionId(8001, 1000);
  const std::string sB = autosave::MakeSessionId(8002, 2000);
  REQUIRE(autosave::WriteEntry(tmp.path, sA, 8001, 1000, MakeMeta(1, "A", 100), "{}"));
  REQUIRE(autosave::WriteEntry(tmp.path, sB, 8002, 2000, MakeMeta(1, "B", 200), "{}"));
  found = autosave::ScanRecoverable(tmp.path, "other", [](std::uint32_t) { return false; });
  REQUIRE(found.size() == 2);
  autosave::DiscardAll(found);
  CHECK_FALSE(fs::exists(autosave::SessionDirectory(tmp.path, sA)));
  CHECK_FALSE(fs::exists(autosave::SessionDirectory(tmp.path, sB)));

  // CleanupSession wipes a session directory on clean exit.
  const std::string clean = autosave::MakeSessionId(9001, 1000);
  REQUIRE(autosave::WriteEntry(tmp.path, clean, 9001, 1000, MakeMeta(1, "Exit", 100), "{}"));
  autosave::CleanupSession(tmp.path, clean);
  CHECK_FALSE(fs::exists(autosave::SessionDirectory(tmp.path, clean)));
}

TEST_CASE("Autosave handles corrupt metadata, stray tmp files, invalid inputs, and newer original files",
          "[autosave]") {
  TempDir tmp;
  std::string err;

  // Input validation.
  CHECK_FALSE(autosave::WriteEntry({}, "session-1-1", 1, 1, MakeMeta(1, "X", 1), "{}", &err));
  CHECK_FALSE(autosave::WriteEntry(tmp.path, "", 1, 1, MakeMeta(1, "X", 1), "{}", &err));
  CHECK_FALSE(autosave::WriteEntry(tmp.path, "session-1-1", 1, 1, MakeMeta(0, "X", 1), "{}", &err));
  CHECK_FALSE(autosave::WriteEntry(tmp.path, "session-1-1", 1, 1, MakeMeta(1, "X", 1), "", &err));

  // Stray .tmp files and corrupt entries inside a dead session are cleaned/skipped safely.
  const std::string dead = autosave::MakeSessionId(1234, 1000);
  const fs::path deadDir = autosave::SessionDirectory(tmp.path, dead);
  REQUIRE(autosave::WriteEntry(tmp.path, dead, 1234, 1000, MakeMeta(1, "Good", 100), "{\"ok\":true}"));

  // Write a stray tmp file, a corrupt meta file, a meta file with missing payload, and a future formatVersion.
  {
    std::ofstream(deadDir / "tab-1.gsauto.tmp", std::ios::binary) << "partial";
    std::ofstream(deadDir / "tab-2.meta.json", std::ios::binary) << "{not valid json";
    std::ofstream(deadDir / "tab-3.meta.json", std::ios::binary)
        << R"({"formatVersion":1,"tabUid":3,"tabName":"MissingPayload","savedAtUnix":200})";
    std::ofstream(deadDir / "tab-4.meta.json", std::ios::binary)
        << R"({"formatVersion":99,"tabUid":4,"tabName":"Future","savedAtUnix":300})";
    std::ofstream(deadDir / "tab-4.gsauto", std::ios::binary) << "{}";
  }

  auto found = autosave::ScanRecoverable(tmp.path, "current", [](std::uint32_t) { return false; });
  REQUIRE(found.size() == 1);
  CHECK(found[0].meta.tabName == "Good");
  CHECK_FALSE(fs::exists(deadDir / "tab-1.gsauto.tmp"));

  // A dead session containing only a stray tmp file gets pruned completely.
  const fs::path tmpOnlyDir = tmp.path / "session-9999-1000";
  fs::create_directories(tmpOnlyDir);
  { std::ofstream(tmpOnlyDir / "tab-1.gsauto.tmp", std::ios::binary) << "half"; }
  (void)autosave::ScanRecoverable(tmp.path, "current", [](std::uint32_t) { return false; });
  CHECK_FALSE(fs::exists(tmpOnlyDir));

  // Original file newer than autosave sets originalFileNewer = true.
  const fs::path origDwg = tmp.path / "site.dwg";
  { std::ofstream(origDwg, std::ios::binary) << "dwg"; }
  const std::string oldSession = autosave::MakeSessionId(3210, 1000);
  REQUIRE(autosave::WriteEntry(tmp.path, oldSession, 3210, 1000,
                               MakeMeta(1, "site", 1000000000, origDwg.u8string()), "{}"));
  found = autosave::ScanRecoverable(tmp.path, "current", [](std::uint32_t) { return false; });
  bool sawSite = false;
  for (const auto& entry : found) {
    if (entry.meta.tabName == "site") {
      sawSite = true;
      CHECK(entry.originalFileNewer);
    }
  }
  CHECK(sawSite);
}

TEST_CASE("Autosave round-trips full GoSurvey drawing geometry and survey points losslessly", "[autosave]") {
  TempDir tmp;
  auto src = std::make_unique<AppCommandState>();
  std::vector<std::string> log;

  // Populate geometry and survey points.
  src->drawing.userLinesFlat.insert(src->drawing.userLinesFlat.end(), {10.0, 20.0, 0.0, 110.0, 220.0, 5.0});
  EntityAttributes lineAttr;
  lineAttr.layer = "0";
  lineAttr.color = "ByLayer";
  src->drawing.userLineAttrs.push_back(lineAttr);
  SurveyPoint pt;
  pt.id = 101;
  pt.northing = 5000.25;
  pt.easting = 3000.75;
  pt.elevation = 412.5;
  pt.description = "IPF";
  src->drawing.surveyPoints.push_back(pt);

  const std::string json = SerializeGoSurveyJson(*src);
  REQUIRE_FALSE(json.empty());

  const std::string crashedSession = autosave::MakeSessionId(4242, 1000);
  std::string err;
  REQUIRE(autosave::WriteEntry(tmp.path, crashedSession, 4242, 1000,
                               MakeMeta(7, "FieldSurvey", 1700000500, "C:\\Jobs\\FieldSurvey.dwg"),
                               json, &err));

  const auto found = autosave::ScanRecoverable(tmp.path, "session-9999-2000", [](std::uint32_t) { return false; });
  REQUIRE(found.size() == 1);
  std::string loadedJson;
  REQUIRE(autosave::ReadPayload(found[0], &loadedJson, &err));

  auto dst = std::make_unique<AppCommandState>();
  REQUIRE(LoadGoSurveyFromJsonUtf8(*dst, loadedJson, log));
  REQUIRE(dst->drawing.lineCount() == 1);
  CHECK(dst->drawing.userLinesFlat[0] == Catch::Approx(10.0));
  CHECK(dst->drawing.userLinesFlat[1] == Catch::Approx(20.0));
  CHECK(dst->drawing.userLinesFlat[3] == Catch::Approx(110.0));
  CHECK(dst->drawing.userLinesFlat[4] == Catch::Approx(220.0));
  CHECK(dst->drawing.userLinesFlat[5] == Catch::Approx(5.0));
  REQUIRE(dst->drawing.surveyPoints.size() == 1);
  CHECK(dst->drawing.surveyPoints[0].id == 101);
  CHECK(dst->drawing.surveyPoints[0].northing == Catch::Approx(5000.25));
  CHECK(dst->drawing.surveyPoints[0].easting == Catch::Approx(3000.75));
  CHECK(dst->drawing.surveyPoints[0].elevation == Catch::Approx(412.5));
  CHECK(dst->drawing.surveyPoints[0].description == "IPF");

  // Default autosave preferences on fresh AppCommandState.
  CHECK(dst->autosaveEnabled == true);
  CHECK(dst->autosaveIntervalMinutes == 5);
}

TEST_CASE("Autosave overwrites existing tab atomically and preserves Unicode names and paths", "[autosave]") {
  TempDir tmp;
  const std::string session = autosave::MakeSessionId(5150, 1000);
  const std::string unicodeName = "Parcelle_\xC3\xA9t\xC3\xA9_N\xC2\xB0" "4";
  const std::string unicodePath = "C:\\Projets\\\xC3\x89tude\\plan_\xC3\xA9t\xC3\xA9.dwg";

  std::string err;
  REQUIRE(autosave::WriteEntry(tmp.path, session, 5150, 1000,
                               MakeMeta(3, unicodeName, 1700000100, unicodePath),
                               R"({"rev":1})", &err));
  REQUIRE(autosave::WriteEntry(tmp.path, session, 5150, 1000,
                               MakeMeta(3, unicodeName, 1700000400, unicodePath),
                               R"({"rev":2,"updated":true})", &err));

  const auto found = autosave::ScanRecoverable(tmp.path, "session-9999-2000", [](std::uint32_t) { return false; });
  REQUIRE(found.size() == 1);
  CHECK(found[0].meta.tabName == unicodeName);
  CHECK(found[0].meta.originalFilePath == unicodePath);
  CHECK(found[0].meta.savedAtUnix == 1700000400);

  std::string payload;
  REQUIRE(autosave::ReadPayload(found[0], &payload, &err));
  CHECK(payload == R"({"rev":2,"updated":true})");
}

TEST_CASE("Autosave protects initializing live sessions, preserves future-version entries, and fails atomically",
          "[autosave]") {
  TempDir tmp;

  // 1. Session race: a live instance has created session-<pid>-<ts> and a .tmp file, before
  //    session.json is written. ScanRecoverable must check the directory-name PID and leave both
  //    the directory and its .tmp file untouched.
  const fs::path initializingLiveDir = tmp.path / "session-4444-1000";
  fs::create_directories(initializingLiveDir);
  { std::ofstream(initializingLiveDir / "session.json.tmp", std::ios::binary) << "{\"pid\":4444}"; }

  // Also test an unidentified session-* folder with no parseable PID anywhere: must be left alone.
  const fs::path unknownDir = tmp.path / "session-unidentified";
  fs::create_directories(unknownDir);
  { std::ofstream(unknownDir / "tab-1.gsauto.tmp", std::ios::binary) << "wip"; }

  // 2. Recovery deletion: a dead session containing only a newer formatVersion (or unreadable
  //    metadata) must NOT be deleted when scanned by an older build.
  const fs::path futureDir = tmp.path / "session-5555-2000";
  fs::create_directories(futureDir);
  {
    std::ofstream(futureDir / "session.json", std::ios::binary) << R"({"formatVersion":1,"pid":5555})";
    std::ofstream(futureDir / "tab-1.meta.json", std::ios::binary)
        << R"({"formatVersion":99,"tabUid":1,"tabName":"FutureDoc","savedAtUnix":999})";
    std::ofstream(futureDir / "tab-1.gsauto", std::ios::binary) << R"({"format":"gosurvey","future":true})";
  }

  const auto found = autosave::ScanRecoverable(tmp.path, "session-9999-3000",
                                               [](std::uint32_t pid) { return pid == 4444; });
  CHECK(found.empty());
  CHECK(fs::exists(initializingLiveDir / "session.json.tmp"));
  CHECK(fs::exists(unknownDir / "tab-1.gsauto.tmp"));
  CHECK(fs::exists(futureDir / "tab-1.meta.json"));
  CHECK(fs::exists(futureDir / "tab-1.gsauto"));

  // 3. Atomic commit failure: when the target cannot be replaced by rename (e.g. an obstructing
  //    directory at the payload path), WriteEntry must fail without leaving a stray .tmp file
  //    or clobbering existing entries.
  const std::string blockedSession = autosave::MakeSessionId(6666, 4000);
  const fs::path blockedDir = autosave::SessionDirectory(tmp.path, blockedSession);
  fs::create_directories(blockedDir / "tab-1.gsauto");
  std::string err;
  CHECK_FALSE(autosave::WriteEntry(tmp.path, blockedSession, 6666, 4000,
                                   MakeMeta(1, "Blocked", 1700000500), R"({"ok":true})", &err));
  CHECK_FALSE(err.empty());
  CHECK_FALSE(fs::exists(blockedDir / "tab-1.gsauto.tmp"));
  CHECK(fs::is_directory(blockedDir / "tab-1.gsauto"));
}
