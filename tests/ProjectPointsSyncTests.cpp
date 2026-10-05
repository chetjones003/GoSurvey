// REQ-376 (issue #696 P3) — project drawings and the shared point database stay in step. Domain only:
// AppCommandState and temp folders, no window.

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "ProjectPoints.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;
  explicit TempDir(const char* stem) {
    path = fs::temp_directory_path() / (std::string("gosurvey-psync-test-") + stem);
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

SurveyPoint Pt(int id, double e, double n, double z, const char* desc = "EG") {
  SurveyPoint p;
  p.id = id;
  p.easting = e;
  p.northing = n;
  p.elevation = z;
  p.description = desc;
  p.labelStyle = SurveyPointLabelStyle::None;  // no label MTEXT: these tests are about the points
  return p;
}

/// Tabs 1 (active, "EG") and 2 ("FG", held in documents[2]) of one project in \p dir.
void TwoTabProject(AppCommandState& st, const fs::path& dir, bool readOnly = false) {
  AppCommandState::ProjectSession s;
  s.uid = 7;
  s.project.id = "proj-1";
  s.project.name = "Job";
  s.project.layout = gsproj::StandardLayout();
  s.project.file = dir / "Job.gsproj";
  s.readOnly = readOnly;
  fs::create_directories(dir / "Points");
  std::vector<std::string> log;
  OpenProjectPointDb(s, log);
  st.openProjects.push_back(std::move(s));
  st.drawingTabs[1].projectUid = 7;
  st.drawingTabs[1].name = "EG";
  st.drawingTabs.push_back({"FG", 99u, 7u});
  st.documents.emplace_back();
  st.activeDrawingIdx = st.prevDrawingIdx = 1;
}

void SwitchTo(AppCommandState& st, int idx) {
  SaveDocumentToSnapshot(st, st.activeDrawingIdx);
  RestoreDocumentFromSnapshot(st, idx);
  st.activeDrawingIdx = st.prevDrawingIdx = idx;
}

}  // namespace

TEST_CASE("req376 two project drawings stay in sync on add, edit and delete", "[req376]") {
  TempDir d("sync");
  AppCommandState st;
  TwoTabProject(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;

  SyncProjectPoints(st, log, now);  // EG joins (empty), becomes Shared
  CHECK(st.drawingTabs[1].pointsMode == AppCommandState::DrawingTab::PointsMode::Shared);

  // EG adds three points.
  st.surveyPoints = {Pt(1, 10, 10, 1), Pt(2, 20, 20, 2), Pt(3, 30, 30, 3)};
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.openProjects[0].points->points.size() == 3);
  CHECK(st.openProjects[0].points->points[0].sourceDrawing == "EG");  // unsaved drawing: the tab's name

  // FG sees them.
  SwitchTo(st, 2);
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 3);
  CHECK(st.surveyPoints[1].elevation == 2.0);

  // FG edits point 2, deletes point 3, adds point 4.
  st.surveyPoints[1].elevation = 2.5;
  st.surveyPoints.erase(st.surveyPoints.begin() + 2);
  st.surveyPoints.push_back(Pt(4, 40, 40, 4, "FG"));
  SyncProjectPoints(st, log, now += 0.016);
  const auto& db = *st.openProjects[0].points;
  REQUIRE(db.points.size() == 3);
  CHECK(db.points[1].point.elevation == 2.5);
  CHECK(db.points[2].point.id == 4);
  CHECK(db.points[2].sourceDrawing == "FG");  // REQ-376 clause 3

  // Back in EG: edit, delete and add all arrive.
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 3);
  CHECK(st.surveyPoints[0].id == 1);
  CHECK(st.surveyPoints[1].elevation == 2.5);
  CHECK(st.surveyPoints[2].id == 4);
  // And settled: another frame changes nothing.
  const auto rev = db.revision;
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(db.revision == rev);
}

TEST_CASE("req376 drawings with different document origins share world coordinates", "[req376]") {
  TempDir d("origin");
  AppCommandState st;
  TwoTabProject(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;

  st.worldDocumentOriginX = 2300000.0;
  st.worldDocumentOriginY = 10400000.0;
  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 123.0, 456.0, 9.0)};  // local to EG's origin
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.openProjects[0].points->points[0].point.easting == 2300123.0);

  SwitchTo(st, 2);
  st.worldDocumentOriginX = 2300100.0;  // FG was started from a different origin
  st.worldDocumentOriginY = 10400400.0;
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 1);
  CHECK(st.surveyPoints[0].easting == 23.0);
  CHECK(st.surveyPoints[0].northing == 56.0);
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.openProjects[0].points->revision == 1);  // the conversion is not mistaken for an edit
}

TEST_CASE("req376 the database is saved with the project, debounced and atomically", "[req376]") {
  TempDir d("save");
  AppCommandState st;
  TwoTabProject(st, d.path);
  std::vector<std::string> log;
  const fs::path file = projpts::DbPath(d.path, "Points");

  SyncProjectPoints(st, log, 0.0);
  st.surveyPoints = {Pt(1, 1, 1, 1)};
  SyncProjectPoints(st, log, 1.0);
  CHECK_FALSE(fs::exists(file));  // inside the debounce window
  SyncProjectPoints(st, log, 2.0);
  CHECK(fs::exists(file));
  CHECK_FALSE(st.openProjects[0].points->dirty);

  // A reopened project sees the points: they are not in any DWG.
  AppCommandState::ProjectSession again;
  again.project = st.openProjects[0].project;
  OpenProjectPointDb(again, log);
  REQUIRE(again.points);
  REQUIRE(again.points->points.size() == 1);
  CHECK(again.points->points[0].point.id == 1);

  // An immediate flush (closing the last tab) writes without waiting.
  st.surveyPoints.push_back(Pt(2, 2, 2, 2));
  SyncProjectPoints(st, log, 2.1);
  CHECK(st.openProjects[0].points->dirty);
  CHECK(FlushProjectPointDb(st.openProjects[0], log));
  CHECK_FALSE(st.openProjects[0].points->dirty);
}

TEST_CASE("req376 a read-only opener sees points but cannot change the database", "[req376]") {
  TempDir d("readonly");
  {
    projpts::Db seed;
    seed.projectId = "proj-1";
    seed.points.push_back({Pt(1, 1, 1, 1), "A"});
    std::string err;
    REQUIRE(projpts::Save(projpts::DbPath(d.path, "Points"), seed, &err));
  }
  AppCommandState st;
  TwoTabProject(st, d.path, /*readOnly=*/true);
  std::vector<std::string> log;
  SyncProjectPoints(st, log, 0.0);
  REQUIRE(st.surveyPoints.size() == 1);  // viewing works

  st.surveyPoints.push_back(Pt(2, 2, 2, 2));  // an attempted add
  st.surveyPoints[0].elevation = 99.0;        // and an attempted edit
  log.clear();
  SyncProjectPoints(st, log, 0.1);
  CHECK(log.size() == 1);  // REQ-201: told
  REQUIRE(st.surveyPoints.size() == 1);        // undone in the view
  CHECK(st.surveyPoints[0].elevation == 1.0);
  CHECK(st.openProjects[0].points->points.size() == 1);
  CHECK_FALSE(st.openProjects[0].points->dirty);
}

TEST_CASE("req376 a drawing that already carries points keeps them and is not shared", "[req376]") {
  TempDir d("detached");
  AppCommandState st;
  TwoTabProject(st, d.path);
  st.surveyPoints = {Pt(1, 1, 1, 1), Pt(2, 2, 2, 2)};  // arrived with the DWG
  std::vector<std::string> log;
  SyncProjectPoints(st, log, 0.0);
  CHECK(st.drawingTabs[1].pointsMode == AppCommandState::DrawingTab::PointsMode::Detached);
  CHECK(log.size() == 1);
  st.surveyPoints.push_back(Pt(3, 3, 3, 3));
  SyncProjectPoints(st, log, 1.0);
  CHECK(st.openProjects[0].points->points.empty());  // nothing leaked into the database
  CHECK(st.surveyPoints.size() == 3);                // nothing taken out of the drawing
  CHECK_FALSE(ProjectOwnsActiveTabPoints(st));       // so the DWG keeps writing them
}

TEST_CASE("req376 a standalone drawing is never touched", "[req376]") {
  AppCommandState st;
  st.surveyPoints = {Pt(1, 1, 1, 1)};
  std::vector<std::string> log;
  SyncProjectPoints(st, log, 0.0);
  CHECK(st.surveyPoints.size() == 1);
  CHECK(log.empty());
  CHECK_FALSE(ProjectOwnsActiveTabPoints(st));

  // Inside a project, the standalone tab next to a project tab is still left alone.
  TempDir d("standalone");
  AppCommandState two;
  TwoTabProject(two, d.path);
  two.drawingTabs[2].projectUid = 0;
  SyncProjectPoints(two, log, 0.0);
  two.surveyPoints = {Pt(1, 1, 1, 1)};
  SyncProjectPoints(two, log, 0.1);
  SwitchTo(two, 2);
  two.surveyPoints = {Pt(9, 9, 9, 9)};
  SyncProjectPoints(two, log, 0.2);
  CHECK(two.surveyPoints.size() == 1);
  CHECK(two.surveyPoints[0].id == 9);
  CHECK(two.openProjects[0].points->points.size() == 1);
}

TEST_CASE("req376 a damaged database is never written over", "[req376]") {
  TempDir d("corrupt");
  fs::create_directories(d.path / "Points");
  {
    std::ofstream f(projpts::DbPath(d.path, "Points"), std::ios::binary);
    f << "{ broken";
  }
  AppCommandState st;
  TwoTabProject(st, d.path);
  CHECK(st.openProjects[0].points == nullptr);
  CHECK_FALSE(st.openProjects[0].pointsError.empty());
  std::vector<std::string> log;
  st.surveyPoints = {Pt(1, 1, 1, 1)};
  SyncProjectPoints(st, log, 0.0);
  SyncProjectPoints(st, log, 5.0);
  CHECK(st.surveyPoints.size() == 1);  // the drawing keeps its points
  std::ifstream f(projpts::DbPath(d.path, "Points"), std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  CHECK(text == "{ broken");
}

// ---- REQ-377 (issue #696 P4): per-drawing visibility rules ---------------------------------------

TEST_CASE("req377 two drawings of one database show different subsets", "[req377]") {
  TempDir d("rules");
  AppCommandState st;
  TwoTabProject(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;

  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 10, 10, 1, "EG"), Pt(2, 20, 20, 2, "EG"), Pt(3, 30, 30, 3, "FG")};
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.openProjects[0].points->points.size() == 3);

  // FG shows only FG-coded points.
  SwitchTo(st, 2);
  st.pointVisibility.description = "FG";
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 1);
  CHECK(st.surveyPoints[0].id == 3);
  CHECK(st.openProjects[0].points->points.size() == 3);  // the database still holds all three

  // EG, with no rules, still shows everything.
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.surveyPoints.size() == 3);
}

TEST_CASE("req377 a point created in a drawing is visible there whatever its filters say", "[req377]") {
  TempDir d("pin");
  AppCommandState st;
  TwoTabProject(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;

  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 10, 10, 1, "EG")};
  SyncProjectPoints(st, log, now += 0.016);

  SwitchTo(st, 2);  // FG: filter "FG*", then create a point coded "EG"
  st.pointVisibility.description = "FG*";
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.surveyPoints.empty());
  st.surveyPoints.push_back(Pt(2, 20, 20, 2, "EG"));
  SyncProjectPoints(st, log, now += 0.016);
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 1);  // still shown in FG
  CHECK(st.surveyPoints[0].id == 2);
  CHECK(projpts::HasId(st.pointVisibility.shown, 2));
  CHECK(st.openProjects[0].points->points.size() == 2);

  // EG (no rules) shows both.
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.surveyPoints.size() == 2);
}

TEST_CASE("req377 hiding a point here keeps it in the database and in other drawings", "[req377]") {
  TempDir d("hide");
  AppCommandState st;
  TwoTabProject(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;

  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 10, 10, 1), Pt(2, 20, 20, 2), Pt(3, 30, 30, 3)};
  SyncProjectPoints(st, log, now += 0.016);

  st.selectedSurveyPointIndices = {1};
  CHECK(HideSelectedPointsHere(st) == 1);
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 2);
  CHECK(st.surveyPoints[0].id == 1);
  CHECK(st.surveyPoints[1].id == 3);
  CHECK(st.openProjects[0].points->points.size() == 3);  // not a delete
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.openProjects[0].points->points.size() == 3);

  SwitchTo(st, 2);
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.surveyPoints.size() == 3);  // FG still shows it
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.surveyPoints.size() == 2);  // and EG still hides it
}

TEST_CASE("req377 editing or deleting a shown point never touches hidden ones", "[req377]") {
  TempDir d("hidden-safe");
  AppCommandState st;
  TwoTabProject(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;

  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 10, 10, 1, "EG")};
  SyncProjectPoints(st, log, now += 0.016);
  SwitchTo(st, 2);  // FG creates its own point
  SyncProjectPoints(st, log, now += 0.016);
  st.surveyPoints.push_back(Pt(2, 20, 20, 2, "FG"));
  SyncProjectPoints(st, log, now += 0.016);
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  st.pointVisibility.description = "EG";
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 1);

  st.surveyPoints.clear();  // the user deletes the one point EG shows
  SyncProjectPoints(st, log, now += 0.016);
  const auto& db = *st.openProjects[0].points;
  REQUIRE(db.points.size() == 1);
  CHECK(db.points[0].point.id == 2);  // FG's point is untouched
}

TEST_CASE("req377 a number hidden here is not overwritten by a new point", "[req377]") {
  TempDir d("collide");
  AppCommandState st;
  TwoTabProject(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;

  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 10, 10, 1, "EG")};
  SyncProjectPoints(st, log, now += 0.016);
  SwitchTo(st, 2);
  SyncProjectPoints(st, log, now += 0.016);
  st.surveyPoints.push_back(Pt(2, 20, 20, 2, "FG"));
  SyncProjectPoints(st, log, now += 0.016);
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  st.pointVisibility.description = "EG";
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 1);

  log.clear();
  st.surveyPoints.push_back(Pt(2, 99, 99, 99, "EG"));  // number 2 is FG's, hidden here
  SyncProjectPoints(st, log, now += 0.016);
  const auto& db = *st.openProjects[0].points;
  REQUIRE(db.points.size() == 2);
  CHECK(db.points[1].point.elevation == 2.0);  // not overwritten
  CHECK(st.surveyPoints.size() == 1);          // the refused point is out of the view again
  CHECK_FALSE(log.empty());                     // and the user was told
}

TEST_CASE("req377 new point numbers skip numbers hidden in this drawing", "[req377]") {
  TempDir d("nextid");
  AppCommandState st;
  TwoTabProject(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;

  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 1, 1, 1, "EG")};
  SyncProjectPoints(st, log, now += 0.016);
  SwitchTo(st, 2);
  SyncProjectPoints(st, log, now += 0.016);
  st.surveyPoints.push_back(Pt(50, 2, 2, 2, "FG"));
  SyncProjectPoints(st, log, now += 0.016);
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  st.pointVisibility.description = "EG";
  st.createPointsNextId = 1;
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 1);  // 50 is hidden here ...
  CHECK(st.createPointsNextId >= 51);    // ... yet its number is not offered again
}

TEST_CASE("req377 rules survive a save and load of the drawing", "[req377]") {
  AppCommandState st;
  st.pointVisibility.description = "EG*";
  st.pointVisibility.idRanges = "1-500";
  st.pointVisibility.useElevation = true;
  st.pointVisibility.elevMin = 90.5;
  st.pointVisibility.elevMax = 120.25;
  st.pointVisibility.group = "Trees";
  st.pointVisibility.sourceDrawing = "Drawings/EG.dwg";
  for (int i = 1; i <= 100; ++i)
    projpts::AddId(&st.pointVisibility.shown, i);
  projpts::AddId(&st.pointVisibility.hidden, 7);
  const std::string json = SerializeGoSurveyJson(st);
  CHECK(json.find("\"1-100\"") != std::string::npos);  // compact, not 100 numbers

  AppCommandState loaded;
  loaded.pointVisibility.description = "stale";
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(loaded, json, log));
  CHECK(loaded.pointVisibility == st.pointVisibility);

  // A drawing with default rules writes nothing, and loading it resets the rules.
  AppCommandState plain;
  const std::string plainJson = SerializeGoSurveyJson(plain);
  CHECK(plainJson.find("pointVisibility") == std::string::npos);
  REQUIRE(LoadGoSurveyFromJsonUtf8(loaded, plainJson, log));
  CHECK(loaded.pointVisibility.IsDefault());
}

TEST_CASE("req377 a standalone drawing has no rules and no helpers act", "[req377]") {
  AppCommandState st;
  CHECK(ActiveProjectDb(st) == nullptr);
  CHECK(CountProjectPoints(st).total == 0);
  CHECK(ProjectPointSources(st).empty());
  st.surveyPoints = {Pt(1, 1, 1, 1)};
  st.selectedSurveyPointIndices = {0};
  CHECK(HideSelectedPointsHere(st) == 0);
  CHECK(st.pointVisibility.hidden.empty());
}
