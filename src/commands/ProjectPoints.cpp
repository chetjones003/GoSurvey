#include "ProjectPoints.hpp"

#include "ProjectWarnings.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

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

/// The drawing's point group named by its rules, or null (a missing group matches nothing).
const PointGroupRule* RuleGroupOf(const AppCommandState& st) {
  if (st.pointVisibility.group.empty())
    return nullptr;
  const int gi = FindPointGroupIndex(st, st.pointVisibility.group);
  return gi >= 0 ? &st.pointGroups[static_cast<size_t>(gi)].rule : nullptr;
}

/// Makes the tab's points exactly what the database holds AND the drawing's rules show (REQ-377).
/// Points that are no longer shown go (with their label MTEXT); new and changed ones get their label
/// rebuilt; everything else keeps its label link.
void Pull(AppCommandState& st, Tab& tab, const projpts::Db& db) {
  const double ox = st.worldDocumentOriginX;
  const double oy = st.worldDocumentOriginY;
  const std::vector<const projpts::Entry*> shown = projpts::Visible(st.pointVisibility, RuleGroupOf(st), db);
  std::unordered_map<int, size_t> inDb;
  inDb.reserve(shown.size());
  for (size_t i = 0; i < shown.size(); ++i)
    inDb[shown[i]->point.id] = i;

  for (size_t i = st.surveyPoints.size(); i-- > 0;)
    if (!inDb.count(st.surveyPoints[i].id))
      RemoveSurveyPointAt(st, i);

  std::unordered_map<int, const SurveyPoint*> old;
  old.reserve(st.surveyPoints.size());
  for (const SurveyPoint& p : st.surveyPoints)
    old[p.id] = &p;

  std::vector<SurveyPoint> next;
  next.reserve(shown.size());
  std::vector<size_t> relabel;
  std::vector<SurveyPoint> base;
  base.reserve(shown.size());
  int maxId = 0;
  for (const projpts::Entry* ep : shown) {
    const projpts::Entry& e = *ep;
    base.push_back(e.point);
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
  for (const projpts::Entry& e : db.points)  // a number hidden in this drawing is still taken (REQ-376 clause 4)
    maxId = std::max(maxId, e.point.id);
  st.createPointsNextId = std::max(st.createPointsNextId, maxId + 1);
  tab.pointsBaseWorld = std::move(base);
  tab.pointsRevision = db.revision;
  tab.pointsRulesApplied = st.pointVisibility;
  BumpCadGpuCache(st);
}

/// REQ-383: shows the question for this edit. Answers already given for the same edit are kept, so a
/// delete question can follow a number-conflict question.
void RaisePointEditPrompt(AppCommandState& st, const AppCommandState::ProjectSession& s, int tabIdx,
                          const PointEditRisks& risks, const OtherDrawings& others) {
  auto& p = st.pointEditPrompt;
  if (!p.active) {
    p = {};
    p.active = true;
  }
  p.projectUid = s.uid;
  p.tabIdx = tabIdx;
  p.projectName = s.project.name;
  p.removed = risks.removed;
  p.conflicts = risks.conflicts;
  p.othersOpenShowing = others.open;
  p.othersClosedMaybe = others.closedMaybe;
  p.otherNames = others.names;
  p.deletePending = others.Total() > 0 && !risks.removed.empty();
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
  if (st.pointEditPrompt.active && st.pointEditPrompt.tabIdx != idx)
    st.pointEditPrompt = {};  // the user left the drawing; its edit asks again when they return
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
    auto& ask = st.pointEditPrompt;
    using Answer = AppCommandState::PointEditPrompt::Answer;

    // REQ-377 / REQ-383: the drawing holds only the points its rules show, so a number it "adds" may
    // already be in the database, hidden here, and a point it deletes may be shown by other drawings.
    // Either would silently damage shared data, so the edit waits for the user's answer (clauses 2
    // and 4); cancelling puts the view back (REQ-201).
    const PointEditRisks risks = FindPointEditRisks(tab.pointsBaseWorld, cur, db);
    // Still waiting for the answer to the same edit: nothing to recompute (the folder scan below is not
    // something to repeat every frame while a dialog is open).
    if (ask.active && ask.tabIdx == idx && ask.removed == risks.removed && ask.conflicts == risks.conflicts &&
        ((!risks.conflicts.empty() && ask.conflictAnswer == Answer::None) ||
         (ask.deletePending && ask.deleteAnswer == Answer::None)))
      return;
    std::vector<SurveyPoint> keptForDb;  // deleted here but kept in the database ("hide in this drawing only")
    std::vector<int> hideIds;

    if (!risks.conflicts.empty()) {
      if (ask.conflictAnswer == Answer::None) {
        RaisePointEditPrompt(st, *s, idx, risks, OtherDrawings{});
        return;
      }
      if (ask.conflictAnswer == Answer::Cancel) {
        log.push_back("Project " + s->project.name + " - point number " + std::to_string(risks.conflicts.front()) +
                      " already exists in the project (hidden in this drawing); the change was cancelled.");
        ask = {};
        Pull(st, tab, db);
        return;
      }
      if (ask.conflictAnswer == Answer::Renumber) {
        int next = NextFreePointNumber(db, cur, st.createPointsNextId - 1);
        for (const int oldId : risks.conflicts) {
          const int j = SurveyPointIndexForId(st, oldId);
          if (j < 0)
            continue;
          for (CadAnnotation& a : st.cadAnnotations)
            if (a.surveyPointLabelForId == oldId)
              a.surveyPointLabelForId = next;
          st.surveyPoints[static_cast<size_t>(j)].id = next;
          cur[static_cast<size_t>(j)].id = next;
          EnsureSurveyPointLabelMtext(st, static_cast<size_t>(j), nullptr);
          log.push_back("Project " + s->project.name + " - point number " + std::to_string(oldId) +
                        " already exists in the project; the new point was renumbered " + std::to_string(next) + ".");
          ++next;
        }
        st.surveyPointIdBuffers.clear();
        st.createPointsNextId = std::max(st.createPointsNextId, next);
      }
      // Proceed = overwrite: ApplyChanges replaces the database's point and says so in the log.
    }

    if (!risks.removed.empty()) {
      const OtherDrawings others = CountOtherDrawings(st, tab.projectUid, idx, risks.removed);
      if (others.Total() > 0) {
        if (ask.deleteAnswer == Answer::None) {
          RaisePointEditPrompt(st, *s, idx, risks, others);
          return;
        }
        if (ask.deleteAnswer == Answer::Cancel) {
          log.push_back("Project " + s->project.name + " - the delete was cancelled; " +
                        std::to_string(risks.removed.size()) + " point(s) stay in the project.");
          ask = {};
          Pull(st, tab, db);
          return;
        }
        if (ask.deleteAnswer == Answer::HideHere) {
          std::unordered_set<int> keep(risks.removed.begin(), risks.removed.end());
          for (const SurveyPoint& p : tab.pointsBaseWorld)
            if (keep.count(p.id))
              keptForDb.push_back(p);
          hideIds = risks.removed;
        }
      }
    }
    ask = {};

    std::unordered_set<int> baseIds;
    baseIds.reserve(tab.pointsBaseWorld.size());
    for (const SurveyPoint& p : tab.pointsBaseWorld)
      baseIds.insert(p.id);
    std::vector<int> freshIds;  // numbers this drawing did not have a frame ago
    for (const SurveyPoint& p : cur)
      if (!baseIds.count(p.id))
        freshIds.push_back(p.id);

    const bool upToDate = tab.pointsRevision == db.revision;
    if (keptForDb.empty()) {
      projpts::ApplyChanges(&db, tab.pointsBaseWorld, cur, SourceKey(st, *s, idx), now, &log);
    } else {  // the database still holds what this drawing only hides
      std::vector<SurveyPoint> forDb = cur;
      forDb.insert(forDb.end(), keptForDb.begin(), keptForDb.end());
      projpts::ApplyChanges(&db, tab.pointsBaseWorld, forDb, SourceKey(st, *s, idx), now, &log);
      for (const int id : hideIds)
        projpts::AddId(&st.pointVisibility.hidden, id);
      BumpCadGpuCache(st);  // the rules are part of the drawing: it now has unsaved changes
      log.push_back("Project " + s->project.name + " - " + std::to_string(hideIds.size()) +
                    " point(s) hidden in this drawing only; other drawings still show them.");
    }
    // REQ-377 clause 2: a point that appears in this drawing is shown here from now on, whatever the
    // filters say. Points the user removed from view are not touched (they were deleted).
    const bool rulesInSync = tab.pointsRulesApplied == st.pointVisibility;
    // Issue #725: a number the user hid here and then created again (Overwrite, or a point that was
    // hidden and re-added) is a deliberate later action. Left in `hidden` it would win over the pin
    // (REQ-377: hidden beats pinned) and the point would vanish the next time the view is rebuilt,
    // e.g. when the drawing is reopened.
    bool unhid = false;
    for (const int id : freshIds)
      if (projpts::HasId(st.pointVisibility.hidden, id)) {
        projpts::RemoveId(&st.pointVisibility.hidden, id);
        unhid = true;
      }
    if (unhid)
      BumpCadGpuCache(st);  // the rules are part of the drawing: it now has unsaved changes
    for (const int id : freshIds)
      projpts::AddId(&st.pointVisibility.shown, id);
    if (rulesInSync)
      tab.pointsRulesApplied = st.pointVisibility;  // pinning a point changes nothing on screen
    tab.pointsBaseWorld = std::move(cur);
    if (upToDate)
      tab.pointsRevision = db.revision;
  } else if (st.pointEditPrompt.active) {
    st.pointEditPrompt = {};  // the edit that raised the question is gone (undone, or the drawing was reloaded)
  }
  if (tab.pointsRevision != db.revision || tab.pointsRulesApplied != st.pointVisibility)
    Pull(st, tab, db);
}

projpts::Db* ActiveProjectDb(AppCommandState& st) {
  const int i = st.activeDrawingIdx;
  if (i < 1 || i >= static_cast<int>(st.drawingTabs.size()))
    return nullptr;
  const Tab& tab = st.drawingTabs[static_cast<size_t>(i)];
  if (tab.projectUid == 0 || tab.pointsMode != Tab::PointsMode::Shared)
    return nullptr;
  AppCommandState::ProjectSession* s = SessionOf(st, tab.projectUid);
  return s ? s->points.get() : nullptr;
}

ProjectPointCounts CountProjectPoints(AppCommandState& st) {
  ProjectPointCounts c;
  const projpts::Db* db = ActiveProjectDb(st);
  if (!db)
    return c;
  c.total = db->points.size();
  c.hidden = projpts::HiddenCount(st.pointVisibility, RuleGroupOf(st), *db);
  c.shown = c.total - c.hidden;
  return c;
}

std::vector<std::string> ProjectPointSources(AppCommandState& st) {
  std::vector<std::string> out;
  const projpts::Db* db = ActiveProjectDb(st);
  if (!db)
    return out;
  std::unordered_set<std::string> seen;
  for (const projpts::Entry& e : db->points)
    if (!e.sourceDrawing.empty() && seen.insert(e.sourceDrawing).second)
      out.push_back(e.sourceDrawing);
  std::sort(out.begin(), out.end());
  return out;
}

int HideSelectedPointsHere(AppCommandState& st) {
  if (!ActiveProjectDb(st))
    return 0;
  int n = 0;
  for (const int idx : st.selectedSurveyPointIndices)
    if (idx >= 0 && idx < static_cast<int>(st.surveyPoints.size())) {
      projpts::AddId(&st.pointVisibility.hidden, st.surveyPoints[static_cast<size_t>(idx)].id);
      ++n;
    }
  if (n > 0)
    BumpCadGpuCache(st);  // the rules are part of the drawing: it now has unsaved changes
  return n;
}
