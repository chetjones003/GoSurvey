#include "ProjectPoints.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

constexpr double kSaveDelaySeconds = 0.75;  // collect a burst of edits into one write
constexpr double kRetrySeconds = 5.0;       // after a failed write

using Tab = AppCommandState::DrawingTab;

AppCommandState::ProjectSession* SessionOf(AppCommandState& st, std::uint32_t uid) {
  for (auto& s : st.openProjects)
    if (s.uid == uid)
      return &s;
  return nullptr;
}

std::string PointsFolder(const gsproj::Project& p) {
  const auto it = p.layout.find("points");
  return it != p.layout.end() ? it->second : std::string("Points");
}

fs::path DbFile(const AppCommandState::ProjectSession& s) {
  return projpts::DbPath(s.project.Folder(), PointsFolder(s.project));
}

/// REQ-376 clause 3: the drawing that creates a point is named by its project-relative path, or by the
/// tab's name while it has never been saved.
std::string SourceKey(const AppCommandState& st, const AppCommandState::ProjectSession& s, int tabIdx) {
  if (!st.activeDocFilePath.empty()) {
    const fs::path rel = fs::u8path(st.activeDocFilePath).lexically_relative(s.project.Folder());
    const std::string r = rel.generic_u8string();
    if (!rel.empty() && r.rfind("..", 0) != 0)
      return r;
  }
  return st.drawingTabs[static_cast<size_t>(tabIdx)].name;
}

/// Makes the tab's points exactly the database's. Points the database no longer has go (with their
/// label MTEXT); new and changed ones get their label rebuilt; everything else keeps its label link.
void Pull(AppCommandState& st, Tab& tab, const projpts::Db& db) {
  const double ox = st.worldDocumentOriginX;
  const double oy = st.worldDocumentOriginY;
  std::unordered_map<int, size_t> inDb;
  inDb.reserve(db.points.size());
  for (size_t i = 0; i < db.points.size(); ++i)
    inDb[db.points[i].point.id] = i;

  for (size_t i = st.surveyPoints.size(); i-- > 0;)
    if (!inDb.count(st.surveyPoints[i].id))
      RemoveSurveyPointAt(st, i);

  std::unordered_map<int, const SurveyPoint*> old;
  old.reserve(st.surveyPoints.size());
  for (const SurveyPoint& p : st.surveyPoints)
    old[p.id] = &p;

  std::vector<SurveyPoint> next;
  next.reserve(db.points.size());
  std::vector<size_t> relabel;
  int maxId = 0;
  for (const auto& e : db.points) {
    SurveyPoint p = e.point;
    p.easting -= ox;
    p.northing -= oy;
    maxId = std::max(maxId, p.id);
    const auto o = old.find(p.id);
    if (o != old.end()) {
      SurveyPoint ow = *o->second;
      ow.easting += ox;
      ow.northing += oy;
      p.labelMtextAnnId = o->second->labelMtextAnnId;
      if (!projpts::SamePoint(ow, e.point))
        relabel.push_back(next.size());
    } else {
      relabel.push_back(next.size());
    }
    next.push_back(std::move(p));
  }
  st.surveyPoints = std::move(next);
  st.surveyPointIdBuffers.clear();
  st.selectedSurveyPointIndices.clear();
  for (const size_t i : relabel)
    EnsureSurveyPointLabelMtext(st, i, nullptr);
  st.createPointsNextId = std::max(st.createPointsNextId, maxId + 1);
  tab.pointsBaseWorld = projpts::View(db);
  tab.pointsRevision = db.revision;
  BumpCadGpuCache(st);
}

}  // namespace

void OpenProjectPointDb(AppCommandState::ProjectSession& s, std::vector<std::string>& log) {
  auto db = std::make_shared<projpts::Db>();
  std::string err;
  if (!projpts::Load(DbFile(s), s.project.id, db.get(), &err)) {
    s.points.reset();
    s.pointsError = err;
    log.push_back("Project " + s.project.name + " - " + err +
                  " Its drawings keep their points in the DWG, and the file was not changed.");
    return;
  }
  s.points = std::move(db);
  s.pointsError.clear();
}

bool FlushProjectPointDb(AppCommandState::ProjectSession& s, std::vector<std::string>& log) {
  if (!s.points || !s.points->dirty || s.readOnly)
    return true;
  std::string err;
  if (!projpts::Save(DbFile(s), *s.points, &err)) {
    log.push_back("Project " + s.project.name + " - the point database could not be saved (" + err + ").");
    return false;
  }
  s.points->dirty = false;
  return true;
}

void SyncProjectPoints(AppCommandState& st, std::vector<std::string>& log, double now) {
  if (st.openProjects.empty())
    return;

  // Debounced save of every open project's database (REQ-376 clause 7: with the project, not only on
  // drawing save).
  for (auto& s : st.openProjects) {
    projpts::Db* db = s.points.get();
    if (!db || !db->dirty || s.readOnly || now - db->dirtySince < kSaveDelaySeconds || now < db->nextRetry)
      continue;
    if (!FlushProjectPointDb(s, log))
      db->nextRetry = now + kRetrySeconds;
  }

  const int idx = st.activeDrawingIdx;
  if (idx < 1 || idx >= static_cast<int>(st.drawingTabs.size()) || idx != st.prevDrawingIdx || st.blockEditActive)
    return;  // Start tab, a tab switch still pending, or BEDIT holding the model arrays
  Tab& tab = st.drawingTabs[static_cast<size_t>(idx)];
  if (tab.projectUid == 0 || tab.pointsMode == Tab::PointsMode::Detached)
    return;
  AppCommandState::ProjectSession* s = SessionOf(st, tab.projectUid);
  if (!s || !s->points)
    return;
  projpts::Db& db = *s->points;

  if (tab.pointsMode == Tab::PointsMode::Unattached) {
    if (!st.surveyPoints.empty()) {
      tab.pointsMode = Tab::PointsMode::Detached;
      log.push_back("Project " + s->project.name + " - \"" + tab.name + "\" holds " +
                    std::to_string(st.surveyPoints.size()) +
                    " point(s) of its own. They stay in the drawing and are not shared with the project; "
                    "Add Drawing to Project merges them.");
      return;
    }
    tab.pointsMode = Tab::PointsMode::Shared;
    Pull(st, tab, db);
    return;
  }

  const double ox = st.worldDocumentOriginX;
  const double oy = st.worldDocumentOriginY;
  if (!projpts::EqualsWorld(st.surveyPoints, ox, oy, tab.pointsBaseWorld)) {
    if (s->readOnly) {
      log.push_back("Project " + s->project.name + " is open read-only; the point change was undone.");
      Pull(st, tab, db);
      return;
    }
    std::vector<SurveyPoint> cur = projpts::ToWorld(st.surveyPoints, ox, oy);
    const bool upToDate = tab.pointsRevision == db.revision;
    projpts::ApplyChanges(&db, tab.pointsBaseWorld, cur, SourceKey(st, *s, idx), now, &log);
    tab.pointsBaseWorld = std::move(cur);
    if (upToDate)
      tab.pointsRevision = db.revision;
  }
  if (tab.pointsRevision != db.revision)
    Pull(st, tab, db);
}
