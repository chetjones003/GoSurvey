// REQ-373 / REQ-374 / REQ-382 (issue #696 P1) — the project file, auto-detect/join and the lock file.
// Pure: each case works in its own temp folder, no window, no %APPDATA%.

#include "Project.hpp"
#include "RecentDrawings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  explicit TempDir(const char* stem) {
    path = fs::temp_directory_path() / (std::string("gosurvey-proj-test-") + stem);
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void WriteText(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << s;
}

std::string ReadText(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

gsproj::LockInfo Me(std::uint32_t pid, const char* machine = "PC1") {
  gsproj::LockInfo l;
  l.user = "chet";
  l.machine = machine;
  l.pid = pid;
  l.sinceUnix = 1000;
  return l;
}

}  // namespace

TEST_CASE("create makes the marker and the six standard folders", "[req373]") {
  TempDir t("create");
  gsproj::Project p;
  std::string err;
  REQUIRE(gsproj::Create(t.path, "MyJob", &p, &err));
  CHECK(fs::exists(t.path / "MyJob" / "MyJob.gsproj"));
  for (const char* d : {"Drawings", "Points", "PointClouds", "PDFs", "Turnovers", "Settings"})
    CHECK(fs::is_directory(t.path / "MyJob" / d));
  CHECK_FALSE(p.id.empty());
  CHECK(p.layout.at("pdfs") == "PDFs");
}

TEST_CASE("create refuses a bad name and an existing project", "[req373]") {
  TempDir t("create-bad");
  std::string err;
  CHECK_FALSE(gsproj::Create(t.path, "", nullptr, &err));
  CHECK_FALSE(gsproj::Create(t.path, "a/b", nullptr, &err));
  CHECK_FALSE(gsproj::Create(t.path, "bad?", nullptr, &err));
  REQUIRE(gsproj::Create(t.path, "Job", nullptr, &err));
  CHECK_FALSE(gsproj::Create(t.path, "Job", nullptr, &err));
}

TEST_CASE("gsproj round-trips, keeping unknown fields and unknown reference kinds", "[req373]") {
  TempDir t("roundtrip");
  gsproj::Project p;
  std::string err;
  REQUIRE(gsproj::Create(t.path, "Job", &p, &err));
  p.items.push_back({"PointClouds/site.e57", gsproj::kKindInProject, {"Drawings/eg.dwg"}});
  p.items.push_back({"C:/scans/big.e57", gsproj::kKindLocalLink, {}});
  p.items.push_back({"s3/bucket/key", "remote", {}});
  p.settingsJson = R"({"x":1})";
  p.extraJson = R"({"futureField":{"a":[1,2]}})";
  REQUIRE(gsproj::Save(p, &err));

  gsproj::Project q;
  REQUIRE(gsproj::Load(p.file, &q, &err));
  CHECK(q.id == p.id);
  CHECK(q.name == "Job");
  REQUIRE(q.items.size() == 3);
  CHECK(q.items[0].associations == std::vector<std::string>{"Drawings/eg.dwg"});
  CHECK(q.items[1].kind == gsproj::kKindLocalLink);
  CHECK(q.items[2].kind == "remote");
  CHECK(q.settingsJson == R"({"x":1})");

  REQUIRE(gsproj::Save(q, &err));  // a second save must still carry the unknown field
  CHECK(ReadText(p.file).find("futureField") != std::string::npos);
}

TEST_CASE("relative-path safety", "[req373]") {
  CHECK(gsproj::IsSafeRelativePath("PointClouds/site.e57"));
  CHECK(gsproj::IsSafeRelativePath("Drawings\\sub\\a.dwg"));
  CHECK(gsproj::IsSafeRelativePath("a..b/c"));
  CHECK_FALSE(gsproj::IsSafeRelativePath(""));
  CHECK_FALSE(gsproj::IsSafeRelativePath("../x"));
  CHECK_FALSE(gsproj::IsSafeRelativePath("a/../../x"));
  CHECK_FALSE(gsproj::IsSafeRelativePath("C:/x"));
  CHECK_FALSE(gsproj::IsSafeRelativePath("/x"));
  CHECK_FALSE(gsproj::IsSafeRelativePath("\\\\server\\share\\x"));
}

TEST_CASE("load rejects an in-project path that escapes the folder", "[req373]") {
  TempDir t("escape");
  const fs::path f = t.path / "Job.gsproj";
  WriteText(f, R"({"formatVersion":1,"id":"g","name":"Job","items":[{"path":"../evil.dwg","kind":"in-project"}]})");
  std::string err;
  CHECK_FALSE(gsproj::Load(f, nullptr, &err));
  CHECK_FALSE(err.empty());
}

TEST_CASE("load rejects garbage and a newer format", "[req373]") {
  TempDir t("garbage");
  std::string err;
  WriteText(t.path / "a.gsproj", "not json");
  CHECK_FALSE(gsproj::Load(t.path / "a.gsproj", nullptr, &err));
  WriteText(t.path / "b.gsproj", R"({"formatVersion":99,"id":"g","name":"B"})");
  CHECK_FALSE(gsproj::Load(t.path / "b.gsproj", nullptr, &err));
  CHECK_FALSE(gsproj::Load(t.path / "missing.gsproj", nullptr, &err));
}

TEST_CASE("an interrupted write leaves the previous project file intact", "[req373]") {
  TempDir t("atomic");
  gsproj::Project p;
  std::string err;
  REQUIRE(gsproj::Create(t.path, "Job", &p, &err));
  const std::string before = ReadText(p.file);
  // A leftover temp file from a crashed save must neither be read as the project nor block the next save.
  WriteText(p.file.parent_path() / "Job.gsproj.tmp", "{ half written");
  CHECK(ReadText(p.file) == before);
  REQUIRE(gsproj::Load(p.file, nullptr, &err));
  REQUIRE(gsproj::Save(p, &err));
  CHECK_FALSE(fs::exists(p.file.parent_path() / "Job.gsproj.tmp"));
}

TEST_CASE("join: the first marker walking up wins", "[req374]") {
  TempDir t("join");
  gsproj::Project p;
  std::string err;
  REQUIRE(gsproj::Create(t.path, "Job", &p, &err));
  const fs::path dwg = t.path / "Job" / "Drawings" / "sub" / "eg.dwg";
  WriteText(dwg, "x");
  const auto r = gsproj::FindProjectFor(dwg);
  CHECK(r.state == gsproj::FindState::Found);
  CHECK(r.file == p.file);
}

TEST_CASE("join: a nearer project beats an outer one", "[req374]") {
  TempDir t("join-nested");
  std::string err;
  REQUIRE(gsproj::Create(t.path, "Outer", nullptr, &err));
  gsproj::Project inner;
  REQUIRE(gsproj::Create(t.path / "Outer" / "Drawings", "Inner", &inner, &err));
  const fs::path dwg = t.path / "Outer" / "Drawings" / "Inner" / "Drawings" / "a.dwg";
  WriteText(dwg, "x");
  CHECK(gsproj::FindProjectFor(dwg).file == inner.file);
}

TEST_CASE("join: a drawing outside any project is standalone", "[req374]") {
  TempDir t("standalone");
  const fs::path dwg = t.path / "loose" / "a.dwg";
  WriteText(dwg, "x");
  CHECK(gsproj::FindProjectFor(dwg).state == gsproj::FindState::None);
}

TEST_CASE("join: a damaged or doubled marker is reported, never dropped", "[req374]") {
  TempDir t("damaged");
  const fs::path dwg = t.path / "d" / "a.dwg";
  WriteText(dwg, "x");
  WriteText(t.path / "d" / "Bad.gsproj", "{{{");
  auto r = gsproj::FindProjectFor(dwg);
  CHECK(r.state == gsproj::FindState::Damaged);
  CHECK_FALSE(r.message.empty());

  std::string err;
  TempDir t2("two");
  REQUIRE(gsproj::Create(t2.path, "A", nullptr, &err));
  WriteText(t2.path / "A" / "Other.gsproj", "{}");
  const fs::path dwg2 = t2.path / "A" / "Drawings" / "a.dwg";
  WriteText(dwg2, "x");
  CHECK(gsproj::FindProjectFor(dwg2).state == gsproj::FindState::Damaged);
}

TEST_CASE("lock: first opener wins, second sees the holder", "[req382]") {
  TempDir t("lock");
  gsproj::Project p;
  std::string err;
  REQUIRE(gsproj::Create(t.path, "Job", &p, &err));
  CHECK(gsproj::TryAcquire(p.file, Me(10), nullptr) == gsproj::LockResult::Acquired);
  gsproj::LockInfo holder;
  CHECK(gsproj::TryAcquire(p.file, Me(20), &holder) == gsproj::LockResult::HeldByOther);
  CHECK(holder.pid == 10);
  CHECK(holder.user == "chet");
}

TEST_CASE("lock: release only removes our own lock", "[req382]") {
  TempDir t("lock-release");
  gsproj::Project p;
  std::string err;
  REQUIRE(gsproj::Create(t.path, "Job", &p, &err));
  REQUIRE(gsproj::TryAcquire(p.file, Me(10), nullptr) == gsproj::LockResult::Acquired);
  gsproj::Release(p.file, Me(20));
  CHECK(fs::exists(gsproj::LockPath(p.file)));
  gsproj::Release(p.file, Me(10));
  CHECK_FALSE(fs::exists(gsproj::LockPath(p.file)));
}

TEST_CASE("lock: stale only when the holder is a dead process on this machine", "[req382]") {
  const auto dead = [](std::uint32_t) { return false; };
  const auto alive = [](std::uint32_t) { return true; };
  CHECK(gsproj::IsStale(Me(10), Me(20), dead));
  CHECK_FALSE(gsproj::IsStale(Me(10), Me(20), alive));
  CHECK_FALSE(gsproj::IsStale(Me(10, "OTHERPC"), Me(20), dead));  // cannot judge another machine
  CHECK(gsproj::IsStale(gsproj::LockInfo{}, Me(20), alive));      // unreadable lock
}

TEST_CASE("lock: takeover replaces the holder; an unreadable lock reads as empty", "[req382]") {
  TempDir t("lock-takeover");
  gsproj::Project p;
  std::string err;
  REQUIRE(gsproj::Create(t.path, "Job", &p, &err));
  REQUIRE(gsproj::TryAcquire(p.file, Me(10), nullptr) == gsproj::LockResult::Acquired);
  REQUIRE(gsproj::TakeOver(p.file, Me(20)));
  gsproj::LockInfo holder;
  gsproj::TryAcquire(p.file, Me(30), &holder);
  CHECK(holder.pid == 20);

  WriteText(gsproj::LockPath(p.file), "garbage");
  gsproj::TryAcquire(p.file, Me(30), &holder);
  CHECK(holder.pid == 0);
  CHECK(holder.machine.empty());
}

TEST_CASE("a project's recent list reuses the recent store and orders newest first", "[req374]") {
  TempDir t("recent");
  const fs::path store = t.path / "recent-projects.json";
  recent::Note(store, "C:/jobs/A/A.gsproj", "", 100);
  recent::Note(store, "C:/jobs/B/B.gsproj", "", 200);
  const auto v = recent::Load(store);
  REQUIRE(v.size() == 2);
  CHECK(v[0].name == "B");
  recent::Remove(store, "C:/jobs/B/B.gsproj");
  CHECK(recent::Load(store).size() == 1);
}
