// REQ-376 / ADR-065 (issue #696 P3) — the project's survey point database file and the diff that folds
// one drawing's edits into it. Pure: temp folders only, no window.

#include "ProjectPointDb.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  explicit TempDir(const char* stem) {
    path = fs::temp_directory_path() / (std::string("gosurvey-pdb-test-") + stem);
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

std::string ReadText(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void WriteText(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << s;
}

SurveyPoint Pt(int id, double e, double n, double z, const char* desc = "") {
  SurveyPoint p;
  p.id = id;
  p.easting = e;
  p.northing = n;
  p.elevation = z;
  p.description = desc;
  return p;
}

}  // namespace

TEST_CASE("req376 a missing database file is an empty database", "[req376]") {
  TempDir d("missing");
  projpts::Db db;
  std::string err;
  REQUIRE(projpts::Load(projpts::DbPath(d.path, "Points"), "proj-1", &db, &err));
  CHECK(db.points.empty());
  CHECK(db.projectId == "proj-1");
}

TEST_CASE("req376 the database round-trips every field and the source drawing", "[req376]") {
  TempDir d("roundtrip");
  const fs::path file = projpts::DbPath(d.path, "Points");
  projpts::Db db;
  db.projectId = "proj-1";
  SurveyPoint a = Pt(7, 2300123.456789, 10456789.125, 101.5, "EG");
  a.rawDescription = "EG";
  a.layer = "V-NODE";
  a.labelStyle = SurveyPointLabelStyle::NumberElev;
  a.labelMtextAnnId = 99;  // the drawing's own link: must not reach the shared file
  db.points.push_back({a, "Drawings/EG.dwg"});
  db.points.push_back({Pt(8, 1, 2, 3), "Drawing 2"});
  std::string err;
  REQUIRE(projpts::Save(file, db, &err));

  projpts::Db back;
  REQUIRE(projpts::Load(file, "proj-1", &back, &err));
  REQUIRE(back.points.size() == 2);
  CHECK(back.points[0].point.id == 7);
  CHECK(back.points[0].point.easting == 2300123.456789);
  CHECK(back.points[0].point.northing == 10456789.125);
  CHECK(back.points[0].point.description == "EG");
  CHECK(back.points[0].point.rawDescription == "EG");
  CHECK(back.points[0].point.layer == "V-NODE");
  CHECK(back.points[0].point.labelStyle == SurveyPointLabelStyle::NumberElev);
  CHECK(back.points[0].point.labelMtextAnnId == 0);
  CHECK(back.points[0].sourceDrawing == "Drawings/EG.dwg");
  CHECK(back.points[1].sourceDrawing == "Drawing 2");
}

TEST_CASE("req376 a damaged, foreign or newer database is refused and left alone", "[req376]") {
  TempDir d("damaged");
  const fs::path file = projpts::DbPath(d.path, "Points");
  projpts::Db db;
  std::string err;

  WriteText(file, "{ not json");
  CHECK_FALSE(projpts::Load(file, "proj-1", &db, &err));
  CHECK_FALSE(err.empty());
  CHECK(ReadText(file) == "{ not json");

  WriteText(file, R"({"formatVersion":1,"projectId":"other","points":[]})");
  CHECK_FALSE(projpts::Load(file, "proj-1", &db, &err));

  WriteText(file, R"({"formatVersion":99,"projectId":"proj-1","points":[]})");
  CHECK_FALSE(projpts::Load(file, "proj-1", &db, &err));

  WriteText(file, R"({"formatVersion":1,"projectId":"proj-1","points":[{"id":1},{"id":1}]})");
  CHECK_FALSE(projpts::Load(file, "proj-1", &db, &err));

  WriteText(file, R"({"formatVersion":1,"projectId":"proj-1","points":[{"easting":1}]})");
  CHECK_FALSE(projpts::Load(file, "proj-1", &db, &err));
}

TEST_CASE("req376 an interrupted write leaves the previous database intact", "[req376]") {
  TempDir d("atomic");
  const fs::path file = projpts::DbPath(d.path, "Points");
  projpts::Db db;
  db.projectId = "proj-1";
  db.points.push_back({Pt(1, 1, 1, 1), "A"});
  std::string err;
  REQUIRE(projpts::Save(file, db, &err));
  const std::string before = ReadText(file);

  // The temp file cannot be created (a folder sits where it goes), so the write fails part-way.
  fs::create_directories(file.parent_path() / "survey-points.gspdb.tmp");
  db.points.push_back({Pt(2, 2, 2, 2), "A"});
  CHECK_FALSE(projpts::Save(file, db, &err));
  CHECK(ReadText(file) == before);
}

TEST_CASE("req376 ApplyChanges folds add, edit and delete into the database", "[req376]") {
  projpts::Db db;
  db.points.push_back({Pt(1, 10, 10, 1, "EG"), "Drawings/EG.dwg"});
  db.points.push_back({Pt(2, 20, 20, 2, "EG"), "Drawings/EG.dwg"});
  db.points.push_back({Pt(3, 30, 30, 3, "EG"), "Drawings/EG.dwg"});
  const std::vector<SurveyPoint> base = projpts::View(db);

  std::vector<SurveyPoint> cur = base;
  cur[1].elevation = 2.5;                 // edit point 2
  cur.erase(cur.begin() + 2);             // delete point 3
  cur.push_back(Pt(4, 40, 40, 4, "FG"));  // add point 4

  std::vector<std::string> log;
  const projpts::Change c = projpts::ApplyChanges(&db, base, cur, "Drawings/FG.dwg", 5.0, &log);
  CHECK(c.added == 1);
  CHECK(c.edited == 1);
  CHECK(c.removed == 1);
  CHECK(log.empty());
  REQUIRE(db.points.size() == 3);
  CHECK(db.points[0].point.id == 1);
  CHECK(db.points[1].point.id == 2);
  CHECK(db.points[1].point.elevation == 2.5);
  CHECK(db.points[1].sourceDrawing == "Drawings/EG.dwg");  // an edit keeps the creating drawing
  CHECK(db.points[2].point.id == 4);
  CHECK(db.points[2].sourceDrawing == "Drawings/FG.dwg");  // REQ-376 clause 3
  CHECK(db.revision == 1);
  CHECK(db.dirty);
  CHECK(db.dirtySince == 5.0);

  // No change: nothing happens, the revision does not move.
  const projpts::Change none = projpts::ApplyChanges(&db, cur, cur, "x", 9.0, &log);
  CHECK_FALSE(none.Any());
  CHECK(db.revision == 1);
}

TEST_CASE("req376 a number the database already holds is overwritten and reported", "[req376]") {
  projpts::Db db;
  db.points.push_back({Pt(5, 1, 1, 1, "old"), "Drawings/EG.dwg"});
  std::vector<SurveyPoint> cur = {Pt(5, 9, 9, 9, "new")};
  std::vector<std::string> log;
  projpts::ApplyChanges(&db, {}, cur, "Drawings/FG.dwg", 0.0, &log);
  REQUIRE(db.points.size() == 1);
  CHECK(db.points[0].point.description == "new");
  CHECK(log.size() == 1);
}

TEST_CASE("req376 a number held twice by a drawing is stored once", "[req376]") {
  projpts::Db db;
  std::vector<SurveyPoint> cur = {Pt(5, 1, 1, 1, "a"), Pt(5, 2, 2, 2, "b")};
  std::vector<std::string> log;
  projpts::ApplyChanges(&db, {}, cur, "D", 0.0, &log);
  REQUIRE(db.points.size() == 1);
  CHECK(db.points[0].point.description == "a");
  CHECK(log.size() == 1);
}

TEST_CASE("req376 a document-origin shift is not read as an edit", "[req376]") {
  std::vector<SurveyPoint> local = {Pt(1, 123.456, 789.012, 5.0), Pt(2, -4.5, 6.25, 7.0)};
  const std::vector<SurveyPoint> baseWorld = projpts::ToWorld(local, 2300000.0, 10400000.0);
  CHECK(projpts::EqualsWorld(local, 2300000.0, 10400000.0, baseWorld));

  // The drawing rebased: its local numbers moved by the origin delta, its world numbers did not.
  for (SurveyPoint& p : local) {
    p.easting -= 1000.0;
    p.northing -= 2000.0;
  }
  CHECK(projpts::EqualsWorld(local, 2301000.0, 10402000.0, baseWorld));
  local[0].elevation += 0.01;
  CHECK_FALSE(projpts::EqualsWorld(local, 2301000.0, 10402000.0, baseWorld));
}

TEST_CASE("req376 one hundred thousand points save and load promptly", "[req376]") {
  TempDir d("big");
  const fs::path file = projpts::DbPath(d.path, "Points");
  projpts::Db db;
  db.projectId = "proj-1";
  for (int i = 1; i <= 100000; ++i)
    db.points.push_back({Pt(i, 2300000.0 + i * 0.37, 10400000.0 + i * 0.11, 100.0 + (i % 50) * 0.01, "EG"), "Drawings/EG.dwg"});
  std::string err;
  const auto t0 = std::chrono::steady_clock::now();
  REQUIRE(projpts::Save(file, db, &err));
  const auto t1 = std::chrono::steady_clock::now();
  projpts::Db back;
  REQUIRE(projpts::Load(file, "proj-1", &back, &err));
  const auto t2 = std::chrono::steady_clock::now();
  CHECK(back.points.size() == 100000);
  const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  INFO("save " << ms(t0, t1) << " ms, load " << ms(t1, t2) << " ms");
  CHECK(ms(t0, t1) < 5000.0);  // a sanity bound only; the real figure is reported in the task file
  CHECK(ms(t1, t2) < 5000.0);
}
