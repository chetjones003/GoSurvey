// REQ-381 (issue #696 P8) — turnover records: the pure create / read / list rules. Each case works in its
// own temp folder, no window.

#include "ProjectTurnover.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  explicit TempDir(const char* stem) {
    path = fs::temp_directory_path() / (std::string("gosurvey-turnover-test-") + stem);
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

/// A project "Job" tracking a drawing, a PDF and a point cloud.
gsproj::Project MakeProject(const fs::path& parent) {
  gsproj::Project p;
  std::string err;
  REQUIRE(gsproj::Create(parent, "Job", &p, &err));
  WriteText(p.Folder() / "Drawings" / "EG.dwg", "drawing bytes EG");
  WriteText(p.Folder() / "PDFs" / "plan.pdf", "%PDF fake");
  WriteText(p.Folder() / "PointClouds" / "site.e57", std::string(1000, 'e'));
  for (const char* rel : {"Drawings/EG.dwg", "PDFs/plan.pdf", "PointClouds/site.e57"}) {
    gsproj::TrackedItem t;
    t.path = rel;
    p.items.push_back(t);
  }
  REQUIRE(gsproj::Save(p, &err));
  return p;
}

const projfiles::Health kClean{};

}  // namespace

TEST_CASE("[req381] a turnover record lists exactly the chosen items, the date and the recipient", "[req381]") {
  TempDir tmp("exact");
  gsproj::Project p = MakeProject(tmp.path);
  projturn::Record rec;
  std::string err;
  // 2026-10-05 12:00:00 UTC
  REQUIRE(projturn::Create(&p, "  Acme Design  ", {"Drawings/EG.dwg", "PDFs/plan.pdf"}, 1791201600, kClean, false, &rec,
                           &err));
  CHECK(rec.date == "2026-10-05");
  CHECK(rec.recipient == "Acme Design");
  REQUIRE(rec.items.size() == 2);
  CHECK(rec.items[0].path == "Drawings/EG.dwg");
  CHECK(rec.items[0].sizeBytes == 16);
  CHECK(rec.items[0].crcHex.size() == 8);
  CHECK(rec.items[1].path == "PDFs/plan.pdf");

  // It was written in Turnovers/, read back identically, tracked, and the .gsproj on disk knows it.
  CHECK(rec.file == "Turnovers/2026-10-05_Acme-Design.gsturnover");
  projturn::Record back;
  REQUIRE(projturn::Read(p.Folder() / fs::u8path(rec.file), &back, &err));
  CHECK(back.recipient == "Acme Design");
  CHECK(back.date == "2026-10-05");
  CHECK(back.createdUnix == 1791201600);
  REQUIRE(back.items.size() == 2);
  CHECK(back.items[0].crcHex == rec.items[0].crcHex);
  CHECK(back.projectId == p.id);
  gsproj::Project reloaded;
  REQUIRE(gsproj::Load(p.file, &reloaded, &err));
  bool tracked = false;
  for (const auto& it : reloaded.items)
    tracked |= it.path == rec.file;
  CHECK(tracked);
}

TEST_CASE("[req381] the fingerprint changes when the delivered file changes", "[req381]") {
  TempDir tmp("crc");
  gsproj::Project p = MakeProject(tmp.path);
  projturn::Record a, b;
  std::string err;
  REQUIRE(projturn::Create(&p, "A", {"Drawings/EG.dwg"}, 100, kClean, false, &a, &err));
  WriteText(p.Folder() / "Drawings" / "EG.dwg", "drawing bytes EX");  // same size, different content
  REQUIRE(projturn::Create(&p, "B", {"Drawings/EG.dwg"}, 200, kClean, false, &b, &err));
  CHECK(a.items[0].sizeBytes == b.items[0].sizeBytes);
  CHECK(a.items[0].crcHex != b.items[0].crcHex);
}

TEST_CASE("[req381] creation is refused until Health problems are acknowledged", "[req381]") {
  TempDir tmp("health");
  gsproj::Project p = MakeProject(tmp.path);
  projfiles::Health bad;
  bad.unsaved.push_back("EG");
  projturn::Record rec;
  std::string err;
  CHECK_FALSE(projturn::Create(&p, "Acme", {"Drawings/EG.dwg"}, 100, bad, false, &rec, &err));
  CHECK(!err.empty());
  CHECK_FALSE(fs::exists(p.Folder() / "Turnovers" / "1970-01-01_Acme.gsturnover"));  // nothing written
  CHECK(projturn::List(p, nullptr).empty());
  REQUIRE(projturn::Create(&p, "Acme", {"Drawings/EG.dwg"}, 100, bad, true, &rec, &err));
  CHECK(projturn::List(p, nullptr).size() == 1);
}

TEST_CASE("[req381] a blank recipient, an empty choice or an unknown file is refused", "[req381]") {
  TempDir tmp("refuse");
  gsproj::Project p = MakeProject(tmp.path);
  projturn::Record rec;
  std::string err;
  CHECK_FALSE(projturn::Create(&p, "   ", {"Drawings/EG.dwg"}, 100, kClean, false, &rec, &err));
  CHECK_FALSE(projturn::Create(&p, "Acme", {}, 100, kClean, false, &rec, &err));
  CHECK_FALSE(projturn::Create(&p, "Acme", {"Drawings/Nope.dwg"}, 100, kClean, false, &rec, &err));
  CHECK_FALSE(projturn::Create(&p, "Acme", {"../outside.txt"}, 100, kClean, false, &rec, &err));
  CHECK(projturn::List(p, nullptr).empty());
}

TEST_CASE("[req381] a missing file is recorded as missing, a repeated choice once", "[req381]") {
  TempDir tmp("missing");
  gsproj::Project p = MakeProject(tmp.path);
  fs::remove(p.Folder() / "PDFs" / "plan.pdf");
  projfiles::Health bad = projfiles::CheckHealth(p, {});
  REQUIRE_FALSE(bad.Clean());
  projturn::Record rec;
  std::string err;
  REQUIRE(projturn::Create(&p, "Acme", {"PDFs/plan.pdf", "PDFs/plan.pdf"}, 100, bad, true, &rec, &err));
  REQUIRE(rec.items.size() == 1);
  CHECK(rec.items[0].missing);
  CHECK(rec.items[0].crcHex.empty());
}

TEST_CASE("[req381] same day and recipient get distinct records; records are not candidates", "[req381]") {
  TempDir tmp("names");
  gsproj::Project p = MakeProject(tmp.path);
  projturn::Record a, b;
  std::string err;
  REQUIRE(projturn::Create(&p, "Acme", {"Drawings/EG.dwg"}, 1791201600, kClean, false, &a, &err));
  REQUIRE(projturn::Create(&p, "Acme", {"PDFs/plan.pdf"}, 1791201700, kClean, false, &b, &err));
  CHECK(a.file != b.file);
  const auto list = projturn::List(p, nullptr);
  REQUIRE(list.size() == 2);
  CHECK(list[0].file == b.file);  // newest first
  for (const std::string& c : projturn::Candidates(p))
    CHECK_FALSE(projturn::IsRecordPath(p, c));
  CHECK(projturn::Candidates(p).size() == 3);
  CHECK_FALSE(projturn::Create(&p, "Acme", {a.file}, 1, kClean, false, &b, &err));  // a record is not handed over
}

TEST_CASE("[req381] a damaged record is skipped and named, not fatal", "[req381]") {
  TempDir tmp("damaged");
  gsproj::Project p = MakeProject(tmp.path);
  projturn::Record rec;
  std::string err;
  REQUIRE(projturn::Create(&p, "Acme", {"Drawings/EG.dwg"}, 100, kClean, false, &rec, &err));
  WriteText(p.Folder() / "Turnovers" / "broken.gsturnover", "{ not json");
  std::vector<std::string> problems;
  CHECK(projturn::List(p, &problems).size() == 1);
  REQUIRE(problems.size() == 1);
  CHECK(problems[0].find("broken.gsturnover") != std::string::npos);
}

TEST_CASE("[req381] DateText is the UTC calendar date", "[req381]") {
  CHECK(projturn::DateText(0) == "1970-01-01");
  CHECK(projturn::DateText(951782400) == "2000-02-29");
  CHECK(projturn::DateText(1791201600) == "2026-10-05");
}
