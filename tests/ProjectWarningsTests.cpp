// REQ-383 (issue #696 P9) — the warnings before destructive or cross-project actions on shared data.
// Domain only: AppCommandState and temp folders, no window. The dialogs are thin: they show the
// question the domain raised and call AnswerPointEdit, which is what these tests do.

#include "CadCommands.hpp"
#include "ProjectPoints.hpp"
#include "ProjectWarnings.hpp"
#include "geo/DrawingConversion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Answer = AppCommandState::PointEditPrompt::Answer;
using Tab = AppCommandState::DrawingTab;

struct TempDir {
  fs::path path;
  explicit TempDir(const char* stem) {
    path = fs::temp_directory_path() / (std::string("gosurvey-pwarn-test-") + stem);
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
  p.labelStyle = SurveyPointLabelStyle::None;
  return p;
}

void OpenSession(AppCommandState& st, const fs::path& dir, std::uint32_t uid, const char* name, bool readOnly = false) {
  AppCommandState::ProjectSession s;
  s.uid = uid;
  s.project.id = std::string("proj-") + name;
  s.project.name = name;
  s.project.layout = gsproj::StandardLayout();
  s.project.file = dir / (std::string(name) + ".gsproj");
  s.readOnly = readOnly;
  fs::create_directories(dir / "Points");
  fs::create_directories(dir / "Drawings");
  std::vector<std::string> log;
  OpenProjectPointDb(s, log);
  st.openProjects.push_back(std::move(s));
}

/// Tabs 1 (active, "EG") and, when \p two, 2 ("FG", held in documents[2]) of one project.
void Project(AppCommandState& st, const fs::path& dir, bool two = true, bool readOnly = false) {
  OpenSession(st, dir, 7, "Job", readOnly);
  st.drawingTabs[1].projectUid = 7;
  st.drawingTabs[1].name = "EG";
  if (two) {
    st.drawingTabs.push_back({"FG", 99u, 7u});
    st.documents.emplace_back();
  }
  st.activeDrawingIdx = st.prevDrawingIdx = 1;
}

void SwitchTo(AppCommandState& st, int idx) {
  SaveDocumentToSnapshot(st, st.activeDrawingIdx);
  RestoreDocumentFromSnapshot(st, idx);
  st.activeDrawingIdx = st.prevDrawingIdx = idx;
}

/// One frame, and when it raised a question: answer it and run the frames that carry the answer out.
void SyncAnswering(AppCommandState& st, std::vector<std::string>& log, double& now, Answer del = Answer::Proceed,
                   Answer conflict = Answer::Proceed) {
  SyncProjectPoints(st, log, now += 0.016);
  for (int guard = 0; st.pointEditPrompt.active && guard < 4; ++guard) {
    const bool conflictTurn = !st.pointEditPrompt.conflicts.empty() && st.pointEditPrompt.conflictAnswer == Answer::None;
    AnswerPointEdit(st, !conflictTurn, conflictTurn ? conflict : del);
    SyncProjectPoints(st, log, now += 0.016);
  }
}

/// EG has points 1-3 and FG is open and shows them all; FG is the active tab.
void ThreePointsSharedActiveFg(AppCommandState& st, std::vector<std::string>& log, double& now) {
  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 10, 10, 1), Pt(2, 20, 20, 2), Pt(3, 30, 30, 3)};
  SyncProjectPoints(st, log, now += 0.016);
  SwitchTo(st, 2);
  SyncProjectPoints(st, log, now += 0.016);
}

}  // namespace

TEST_CASE("req383 deleting a shown point asks first and names the other drawings", "[req383]") {
  TempDir d("delete");
  AppCommandState st;
  Project(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;
  ThreePointsSharedActiveFg(st, log, now);
  REQUIRE(st.surveyPoints.size() == 3);

  st.surveyPoints.erase(st.surveyPoints.begin() + 2);  // FG deletes point 3
  SyncProjectPoints(st, log, now += 0.016);

  const auto& pe = st.pointEditPrompt;
  REQUIRE(pe.active);
  CHECK(pe.removed == std::vector<int>{3});
  CHECK(pe.othersOpenShowing == 1);  // EG
  REQUIRE(pe.otherNames.size() == 1);
  CHECK(pe.otherNames[0] == "EG");
  CHECK(pe.projectName == "Job");
  CHECK(st.openProjects[0].points->points.size() == 3);  // nothing is deleted before the answer

  for (int frame = 0; frame < 3; ++frame)  // the question waits, frame after frame, and changes nothing
    SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.pointEditPrompt.active);
  CHECK(st.pointEditPrompt.deleteAnswer == Answer::None);
  CHECK(st.openProjects[0].points->points.size() == 3);

  AnswerPointEdit(st, true, Answer::Proceed);
  SyncProjectPoints(st, log, now += 0.016);
  CHECK_FALSE(st.pointEditPrompt.active);
  CHECK(st.openProjects[0].points->points.size() == 2);
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.surveyPoints.size() == 2);  // EG lost it too
}

TEST_CASE("req383 hide in this drawing only leaves the database untouched", "[req383]") {
  TempDir d("hide");
  AppCommandState st;
  Project(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;
  ThreePointsSharedActiveFg(st, log, now);
  const auto revBefore = st.openProjects[0].points->revision;

  st.surveyPoints.erase(st.surveyPoints.begin() + 2);
  SyncAnswering(st, log, now, Answer::HideHere);

  const auto& db = *st.openProjects[0].points;
  CHECK(db.points.size() == 3);
  CHECK(db.revision == revBefore);  // not one byte of the database moved
  CHECK(projpts::HasId(st.pointVisibility.hidden, 3));
  CHECK(st.surveyPoints.size() == 2);  // FG no longer shows it
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(st.surveyPoints.size() == 3);  // EG still does
}

TEST_CASE("req383 cancelling a delete puts the points back", "[req383]") {
  TempDir d("cancel-del");
  AppCommandState st;
  Project(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;
  ThreePointsSharedActiveFg(st, log, now);

  st.surveyPoints.clear();  // FG deletes everything
  SyncAnswering(st, log, now, Answer::Cancel);
  CHECK(st.openProjects[0].points->points.size() == 3);
  CHECK(st.surveyPoints.size() == 3);
  CHECK_FALSE(st.pointEditPrompt.active);
}

TEST_CASE("req383 no other drawing means nothing to warn about; a closed drawing counts as one", "[req383]") {
  TempDir d("alone");
  AppCommandState st;
  Project(st, d.path, /*two=*/false);
  std::vector<std::string> log;
  double now = 0.0;
  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 1, 1, 1), Pt(2, 2, 2, 2)};
  SyncProjectPoints(st, log, now += 0.016);

  // This drawing is saved in the project; it is the only drawing there.
  { std::ofstream(d.path / "Drawings" / "EG.dwg") << "x"; }
  st.activeDocFilePath = (d.path / "Drawings" / "EG.dwg").u8string();
  st.surveyPoints.erase(st.surveyPoints.begin());
  SyncProjectPoints(st, log, now += 0.016);
  CHECK_FALSE(st.pointEditPrompt.active);
  CHECK(st.openProjects[0].points->points.size() == 1);

  // Another drawing exists in the project folder but is not open: it might show the point.
  { std::ofstream(d.path / "Drawings" / "FG.dwg") << "x"; }
  st.surveyPoints.clear();
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.pointEditPrompt.active);
  CHECK(st.pointEditPrompt.othersOpenShowing == 0);
  CHECK(st.pointEditPrompt.othersClosedMaybe == 1);  // FG.dwg, not EG.dwg (open)
}

TEST_CASE("req383 a drawing that does not show the point is not counted as losing it", "[req383]") {
  TempDir d("notshown");
  AppCommandState st;
  Project(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;
  ThreePointsSharedActiveFg(st, log, now);
  projpts::AddId(&st.pointVisibility.hidden, 3);  // FG hides point 3
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 2);
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.surveyPoints.size() == 3);

  st.surveyPoints.pop_back();  // EG deletes 3: FG does not show it, so FG loses nothing
  SyncProjectPoints(st, log, now += 0.016);
  CHECK_FALSE(st.pointEditPrompt.active);
  CHECK(st.openProjects[0].points->points.size() == 2);
}

TEST_CASE("req383 a number that already exists asks overwrite, renumber or cancel", "[req383]") {
  auto setup = [](AppCommandState& st, const fs::path& dir, std::vector<std::string>& log, double& now) {
    Project(st, dir);
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
    REQUIRE(st.surveyPoints.size() == 1);  // number 2 (FG's) is hidden here
  };

  SECTION("the question names the number") {
    TempDir d("conflict-ask");
    AppCommandState st;
    std::vector<std::string> log;
    double now = 0.0;
    setup(st, d.path, log, now);
    st.surveyPoints.push_back(Pt(2, 99, 99, 99, "EG"));
    SyncProjectPoints(st, log, now += 0.016);
    REQUIRE(st.pointEditPrompt.active);
    CHECK(st.pointEditPrompt.conflicts == std::vector<int>{2});
    CHECK(st.openProjects[0].points->points[1].point.elevation == 2.0);  // not overwritten yet
  }
  SECTION("cancel leaves the database alone and the view clean") {
    TempDir d("conflict-cancel");
    AppCommandState st;
    std::vector<std::string> log;
    double now = 0.0;
    setup(st, d.path, log, now);
    st.surveyPoints.push_back(Pt(2, 99, 99, 99, "EG"));
    SyncAnswering(st, log, now, Answer::Proceed, Answer::Cancel);
    CHECK(st.openProjects[0].points->points.size() == 2);
    CHECK(st.openProjects[0].points->points[1].point.elevation == 2.0);
    CHECK(st.surveyPoints.size() == 1);
  }
  SECTION("overwrite replaces the existing point") {
    TempDir d("conflict-over");
    AppCommandState st;
    std::vector<std::string> log;
    double now = 0.0;
    setup(st, d.path, log, now);
    st.surveyPoints.push_back(Pt(2, 99, 99, 99, "EG"));
    SyncAnswering(st, log, now, Answer::Proceed, Answer::Proceed);
    const auto& db = *st.openProjects[0].points;
    REQUIRE(db.points.size() == 2);
    CHECK(db.points[1].point.elevation == 99.0);
  }
  SECTION("renumber keeps the existing point and gives the new one a free number") {
    TempDir d("conflict-renum");
    AppCommandState st;
    std::vector<std::string> log;
    double now = 0.0;
    setup(st, d.path, log, now);
    st.surveyPoints.push_back(Pt(2, 99, 99, 99, "EG"));
    SyncAnswering(st, log, now, Answer::Proceed, Answer::Renumber);
    const auto& db = *st.openProjects[0].points;
    REQUIRE(db.points.size() == 3);
    CHECK(db.points[1].point.id == 2);
    CHECK(db.points[1].point.elevation == 2.0);  // FG's point is untouched
    CHECK(db.points[2].point.id == 3);
    CHECK(db.points[2].point.elevation == 99.0);
    CHECK(st.surveyPoints.size() == 2);  // 1 and the renumbered 3
    CHECK(st.surveyPoints[1].id == 3);
  }
}

TEST_CASE("req383 one edit that renumbers and deletes asks both questions in turn", "[req383]") {
  TempDir d("both");
  AppCommandState st;
  Project(st, d.path);
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

  st.surveyPoints = {Pt(2, 99, 99, 99, "EG")};  // deletes 1 (FG shows it) and reuses FG's hidden number 2
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.pointEditPrompt.active);
  CHECK(st.pointEditPrompt.conflicts == std::vector<int>{2});
  AnswerPointEdit(st, false, Answer::Renumber);
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.pointEditPrompt.active);  // now the delete question
  CHECK(st.pointEditPrompt.removed == std::vector<int>{1});
  CHECK(st.pointEditPrompt.othersOpenShowing == 1);
  AnswerPointEdit(st, true, Answer::Proceed);
  SyncProjectPoints(st, log, now += 0.016);
  CHECK_FALSE(st.pointEditPrompt.active);

  const auto& db = *st.openProjects[0].points;
  REQUIRE(db.points.size() == 2);
  CHECK(db.points[0].point.id == 2);  // FG's own point 2, untouched
  CHECK(db.points[0].point.elevation == 2.0);
  CHECK(db.points[1].point.id == 3);  // the renumbered new point
}

TEST_CASE("req383 a read-only project never raises a question", "[req383]") {
  TempDir d("ro");
  AppCommandState st;
  Project(st, d.path, true, /*readOnly=*/true);
  std::vector<std::string> log;
  double now = 0.0;
  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 1, 1, 1)};
  SyncProjectPoints(st, log, now += 0.016);
  CHECK_FALSE(st.pointEditPrompt.active);
  CHECK(st.openProjects[0].points->points.empty());
}

TEST_CASE("req383 leaving the drawing drops an unanswered question", "[req383]") {
  TempDir d("leave");
  AppCommandState st;
  Project(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;
  ThreePointsSharedActiveFg(st, log, now);
  st.surveyPoints.pop_back();
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.pointEditPrompt.active);
  SwitchTo(st, 1);
  SyncProjectPoints(st, log, now += 0.016);
  CHECK_FALSE(st.pointEditPrompt.active);
  CHECK(st.openProjects[0].points->points.size() == 3);
}

TEST_CASE("req383 findings are pure: removed and conflicting numbers", "[req383]") {
  projpts::Db db;
  db.points.push_back({Pt(1, 1, 1, 1), "A"});
  db.points.push_back({Pt(2, 2, 2, 2), "A"});
  db.points.push_back({Pt(5, 5, 5, 5), "B"});
  const std::vector<SurveyPoint> base = {Pt(1, 1, 1, 1), Pt(2, 2, 2, 2)};
  const std::vector<SurveyPoint> cur = {Pt(1, 1, 1, 1), Pt(5, 9, 9, 9), Pt(7, 7, 7, 7)};
  const PointEditRisks r = FindPointEditRisks(base, cur, db);
  CHECK(r.removed == std::vector<int>{2});
  CHECK(r.conflicts == std::vector<int>{5});  // 5 differs from the database's 5; 7 is simply new
  CHECK(NextFreePointNumber(db, cur, 0) == 8);
}

// ---- clauses 1 and 3: paste -------------------------------------------------------------------------

namespace {

/// Active tab 1 is in project 8 ("Dest"); the clipboard holds one line.
void PasteSetup(AppCommandState& st, const fs::path& dir) {
  OpenSession(st, dir / "a", 7, "Source");
  OpenSession(st, dir / "b", 8, "Dest");
  st.drawingTabs[1].projectUid = 8;
  st.activeDrawingIdx = st.prevDrawingIdx = 1;
  st.clipboard.lines = {0, 0, 0, 1, 1, 0};
  st.clipboard.srcProjectUid = 7;
  st.clipboard.srcProjectName = "Source";
  st.clipboard.srcZone = "";
  st.drawingInsUnits = 2;  // feet
  st.clipboard.srcMetersPerUnit = geo::MetersPerInsUnit(
      2, st.drawingSettings.footDefinition == DrawingSettings::FootDefinition::UsSurvey);  // the same unit
}

}  // namespace

TEST_CASE("req383 pasting into a different project warns", "[req383]") {
  TempDir d("paste-proj");
  AppCommandState st;
  PasteSetup(st, d.path);
  const PasteCheck pc = CheckClipboardPaste(st);
  CHECK(pc.verdict == PasteCheck::Verdict::Warn);
  CHECK(pc.text.find("Source") != std::string::npos);
  CHECK(pc.text.find("Dest") != std::string::npos);

  std::vector<std::string> log;
  StartPasteCommand(st, log);
  CHECK(st.pastePrompt.active);
  CHECK_FALSE(st.pastePrompt.block);
  CHECK(st.active != AppCommandState::Kind::Paste);  // waits for the answer

  st.pastePrompt = {};
  st.pasteWarningAnswered = true;  // "Paste anyway"
  StartPasteCommand(st, log);
  CHECK(st.active == AppCommandState::Kind::Paste);
  CHECK_FALSE(st.pasteWarningAnswered);  // one use only
}

TEST_CASE("req383 two open projects with the same name are told apart by folder in the paste warning",
          "[req383][issue726]") {
  TempDir d("paste-samename");
  AppCommandState st;
  OpenSession(st, d.path / "sent", 7, "TestJob");
  OpenSession(st, d.path / "received", 8, "TestJob");
  st.drawingTabs[1].projectUid = 7;
  st.activeDrawingIdx = st.prevDrawingIdx = 1;
  TagClipboardOrigin(st);  // copy in the first project's drawing
  st.drawingTabs[1].projectUid = 8;  // paste into the received copy
  const PasteCheck pc = CheckClipboardPaste(st);
  CHECK(pc.verdict == PasteCheck::Verdict::Warn);
  CHECK(pc.text.find("sent") != std::string::npos);      // both folders are named
  CHECK(pc.text.find("received") != std::string::npos);

  // Different names keep the short message.
  AppCommandState st2;
  PasteSetup(st2, d.path / "x");
  const PasteCheck pc2 = CheckClipboardPaste(st2);
  CHECK(pc2.text.find(d.path.string()) == std::string::npos);
}

TEST_CASE("req383 a coordinate-system mismatch blocks the paste", "[req383]") {
  TempDir d("paste-zone");
  AppCommandState st;
  PasteSetup(st, d.path);
  st.drawingTabs[1].projectUid = 7;  // same project: only the zone differs
  st.clipboard.srcZone = "TX83-CF";
  st.drawingSettings.zoneCode = "TX83-NF";
  const PasteCheck pc = CheckClipboardPaste(st);
  CHECK(pc.verdict == PasteCheck::Verdict::Block);

  std::vector<std::string> log;
  StartPasteCommand(st, log);
  CHECK(st.pastePrompt.active);
  CHECK(st.pastePrompt.block);
  CHECK(st.active != AppCommandState::Kind::Paste);
  StartPasteOrigCommand(st, log);  // the other paste route is guarded too
  CHECK(st.pastePrompt.original);
  CHECK(st.pasteWarningAnswered == false);
}

TEST_CASE("req383 a units mismatch warns", "[req383]") {
  TempDir d("paste-units");
  AppCommandState st;
  PasteSetup(st, d.path);
  st.drawingTabs[1].projectUid = 7;
  st.clipboard.srcMetersPerUnit = 1.0;  // copied from a metre drawing, pasted into feet
  const PasteCheck pc = CheckClipboardPaste(st);
  CHECK(pc.verdict == PasteCheck::Verdict::Warn);
  CHECK(pc.text.find("unit") != std::string::npos);
}

TEST_CASE("req383 two standalone drawings and the same project are never checked", "[req383]") {
  TempDir d("paste-ok");
  AppCommandState st;
  PasteSetup(st, d.path);
  st.drawingTabs[1].projectUid = 7;  // same project, same zone, same unit
  CHECK(CheckClipboardPaste(st).verdict == PasteCheck::Verdict::Ok);

  st.drawingTabs[1].projectUid = 0;  // both standalone, even with a zone and unit mismatch
  st.clipboard.srcProjectUid = 0;
  st.clipboard.srcZone = "TX83-CF";
  st.drawingSettings.zoneCode = "TX83-NF";
  st.clipboard.srcMetersPerUnit = 1.0;
  CHECK(CheckClipboardPaste(st).verdict == PasteCheck::Verdict::Ok);
  std::vector<std::string> log;
  StartPasteCommand(st, log);
  CHECK(st.active == AppCommandState::Kind::Paste);  // exactly as before projects existed
}

TEST_CASE("req383 copying records where the objects came from", "[req383]") {
  TempDir d("copy-tag");
  AppCommandState st;
  PasteSetup(st, d.path);
  st.drawingSettings.zoneCode = "TX83-CF";
  st.drawingInsUnits = 2;
  TagClipboardOrigin(st);
  CHECK(st.clipboard.srcProjectUid == 8);
  CHECK(st.clipboard.srcProjectName == "Dest");
  CHECK(st.clipboard.srcZone == "TX83-CF");
  CHECK(st.clipboard.srcMetersPerUnit > 0.30);
  CHECK(st.clipboard.srcMetersPerUnit < 0.31);
}

// ---- clause 6: closing -------------------------------------------------------------------------------

TEST_CASE("req383 unsaved database changes are listed with their project and drawings", "[req383]") {
  TempDir d("unsaved");
  AppCommandState st;
  Project(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;
  SyncProjectPoints(st, log, now);

  // Make the point database impossible to write: its folder is a file.
  std::error_code ec;
  fs::remove_all(d.path / "Points", ec);
  { std::ofstream(d.path / "Points") << "not a folder"; }

  st.surveyPoints = {Pt(1, 1, 1, 1)};
  SyncProjectPoints(st, log, now += 0.016);
  REQUIRE(st.openProjects[0].points->dirty);

  const std::vector<UnsavedProject> unsaved = ProjectsWithUnsavedPoints(st, 0, log);
  REQUIRE(unsaved.size() == 1);
  CHECK(unsaved[0].name == "Job");
  CHECK(unsaved[0].drawings == std::vector<std::string>{"EG", "FG"});

  // The disk recovers: the write now succeeds and nothing is left to warn about.
  fs::remove(d.path / "Points", ec);
  fs::create_directories(d.path / "Points");
  CHECK(ProjectsWithUnsavedPoints(st, 0, log).empty());
  CHECK_FALSE(st.openProjects[0].points->dirty);
}

TEST_CASE("req383 a saved or read-only project has nothing unsaved", "[req383]") {
  TempDir d("unsaved-none");
  AppCommandState st;
  Project(st, d.path);
  std::vector<std::string> log;
  double now = 0.0;
  SyncProjectPoints(st, log, now);
  st.surveyPoints = {Pt(1, 1, 1, 1)};
  SyncProjectPoints(st, log, now += 0.016);
  CHECK(ProjectsWithUnsavedPoints(st, 0, log).empty());  // the write simply succeeds
  CHECK(ProjectsWithUnsavedPoints(st, 99, log).empty());  // an unknown project
}
