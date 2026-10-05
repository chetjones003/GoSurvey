#include "ProjectWarnings.hpp"

#include "ProjectPoints.hpp"
#include "geo/DrawingConversion.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

using Session = AppCommandState::ProjectSession;

const Session* SessionOf(const AppCommandState& st, std::uint32_t uid) {
  for (const Session& s : st.openProjects)
    if (s.uid == uid)
      return &s;
  return nullptr;
}

std::string LowerExt(const fs::path& p) {
  std::string e = p.extension().u8string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e;
}

std::string TabFilePath(const AppCommandState& st, size_t i) {
  if (static_cast<int>(i) == st.activeDrawingIdx)
    return st.activeDocFilePath;
  return i < st.documents.size() ? st.documents[i].filePath : std::string();
}

const PointGroupRule* GroupRuleOf(const std::vector<PointGroup>& groups, const projpts::Rules& r) {
  if (r.group.empty())
    return nullptr;
  for (const PointGroup& g : groups)
    if (g.name == r.group)
      return &g.rule;
  return nullptr;
}

double MetersPerUnitOf(const AppCommandState& st) {
  return geo::MetersPerInsUnit(st.drawingInsUnits,
                               st.drawingSettings.footDefinition == DrawingSettings::FootDefinition::UsSurvey);
}

}  // namespace

PointEditRisks FindPointEditRisks(const std::vector<SurveyPoint>& baseWorld, const std::vector<SurveyPoint>& curWorld,
                                  const projpts::Db& db) {
  PointEditRisks r;
  std::unordered_set<int> baseIds, curIds;
  baseIds.reserve(baseWorld.size());
  curIds.reserve(curWorld.size());
  for (const SurveyPoint& p : baseWorld)
    baseIds.insert(p.id);
  for (const SurveyPoint& p : curWorld)
    curIds.insert(p.id);
  std::unordered_map<int, const SurveyPoint*> dbById;
  dbById.reserve(db.points.size());
  for (const projpts::Entry& e : db.points)
    dbById[e.point.id] = &e.point;

  for (const SurveyPoint& p : baseWorld)
    if (!curIds.count(p.id) && dbById.count(p.id))
      r.removed.push_back(p.id);
  for (const SurveyPoint& p : curWorld) {
    if (baseIds.count(p.id))
      continue;
    const auto d = dbById.find(p.id);
    if (d != dbById.end() && !projpts::SamePoint(*d->second, p))
      r.conflicts.push_back(p.id);
  }
  return r;
}

OtherDrawings CountOtherDrawings(const AppCommandState& st, std::uint32_t projectUid, int thisTabIdx,
                                 const std::vector<int>& removedIds) {
  OtherDrawings out;
  const Session* s = SessionOf(st, projectUid);
  if (!s || !s->points)
    return out;
  std::vector<const projpts::Entry*> doomed;
  {
    const std::unordered_set<int> ids(removedIds.begin(), removedIds.end());
    for (const projpts::Entry& e : s->points->points)
      if (ids.count(e.point.id))
        doomed.push_back(&e);
  }

  std::unordered_set<std::string> openPaths;  // generic, lexically normal
  for (size_t i = 1; i < st.drawingTabs.size(); ++i) {
    if (st.drawingTabs[i].projectUid != projectUid)
      continue;
    const std::string path = TabFilePath(st, i);
    if (!path.empty())
      openPaths.insert(fs::u8path(path).lexically_normal().generic_u8string());
    if (static_cast<int>(i) == thisTabIdx)
      continue;
    if (st.drawingTabs[i].pointsMode != AppCommandState::DrawingTab::PointsMode::Shared)
      continue;  // it holds its own points (or has not joined yet): nothing here is its to lose
    const bool active = static_cast<int>(i) == st.activeDrawingIdx;
    if (!active && i >= st.documents.size())
      continue;
    const projpts::Rules& rules = active ? st.pointVisibility : st.documents[i].pointVisibility;
    const auto& groups = active ? st.pointGroups : st.documents[i].pointGroups;
    const PointGroupRule* g = GroupRuleOf(groups, rules);
    if (std::any_of(doomed.begin(), doomed.end(),
                    [&](const projpts::Entry* e) { return projpts::ShowsEntry(rules, g, *e); })) {
      ++out.open;
      out.names.push_back(st.drawingTabs[i].name);
    }
  }

  // Drawings of the project that are not open: their rules are saved inside the DWG, so all that can be
  // said honestly is that they might show these points.
  const auto lay = s->project.layout.find("drawings");
  const fs::path dir = s->project.Folder() / (lay != s->project.layout.end() ? lay->second : std::string("Drawings"));
  std::error_code ec;
  if (fs::is_directory(dir, ec)) {
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
      std::error_code fe;
      if (!it->is_regular_file(fe) || LowerExt(it->path()) != ".dwg")
        continue;
      if (!openPaths.count(it->path().lexically_normal().generic_u8string()))
        ++out.closedMaybe;
    }
  }
  return out;
}

int NextFreePointNumber(const projpts::Db& db, const std::vector<SurveyPoint>& drawingPoints, int floorId) {
  int m = floorId;
  for (const projpts::Entry& e : db.points)
    m = std::max(m, e.point.id);
  for (const SurveyPoint& p : drawingPoints)
    m = std::max(m, p.id);
  return m + 1;
}

void AnswerPointEdit(AppCommandState& st, bool deleteQuestion, AppCommandState::PointEditPrompt::Answer a) {
  if (!st.pointEditPrompt.active)
    return;
  (deleteQuestion ? st.pointEditPrompt.deleteAnswer : st.pointEditPrompt.conflictAnswer) = a;
}

std::vector<UnsavedProject> ProjectsWithUnsavedPoints(AppCommandState& st, std::uint32_t onlyUid,
                                                      std::vector<std::string>& log) {
  std::vector<UnsavedProject> out;
  for (Session& s : st.openProjects) {
    if (onlyUid != 0 && s.uid != onlyUid)
      continue;
    if (!s.points || !s.points->dirty || s.readOnly)
      continue;
    if (FlushProjectPointDb(s, log))
      continue;
    UnsavedProject u;
    u.name = s.project.name;
    for (size_t i = 1; i < st.drawingTabs.size(); ++i)
      if (st.drawingTabs[i].projectUid == s.uid)
        u.drawings.push_back(st.drawingTabs[i].name);
    out.push_back(std::move(u));
  }
  return out;
}

void TagClipboardOrigin(AppCommandState& st) {
  CadClipboard& cb = st.clipboard;
  const int i = st.activeDrawingIdx;
  cb.srcProjectUid = (i >= 1 && i < static_cast<int>(st.drawingTabs.size())) ? st.drawingTabs[static_cast<size_t>(i)].projectUid : 0u;
  const Session* s = cb.srcProjectUid ? SessionOf(st, cb.srcProjectUid) : nullptr;
  cb.srcProjectName = s ? s->project.name : std::string();
  cb.srcZone = st.drawingSettings.zoneCode;
  cb.srcMetersPerUnit = MetersPerUnitOf(st);
}

PasteCheck CheckClipboardPaste(const AppCommandState& st) {
  PasteCheck pc;
  const CadClipboard& cb = st.clipboard;
  const int i = st.activeDrawingIdx;
  const std::uint32_t dest =
      (i >= 1 && i < static_cast<int>(st.drawingTabs.size())) ? st.drawingTabs[static_cast<size_t>(i)].projectUid : 0u;
  if (cb.srcProjectUid == 0 && dest == 0)
    return pc;  // two standalone drawings: exactly as before projects existed

  const Session* ds = dest ? SessionOf(st, dest) : nullptr;
  const std::string destName = ds ? ds->project.name : std::string("(no project)");
  const std::string srcName = cb.srcProjectName.empty() ? std::string("(no project)") : cb.srcProjectName;

  const bool zoneDiffers = !cb.srcZone.empty() && !st.drawingSettings.zoneCode.empty() &&
                           cb.srcZone != st.drawingSettings.zoneCode;
  const double destUnit = MetersPerUnitOf(st);
  const bool unitsDiffer = cb.srcMetersPerUnit > 0.0 && destUnit > 0.0 && std::fabs(cb.srcMetersPerUnit - destUnit) > 1e-12;

  if (zoneDiffers) {
    pc.verdict = PasteCheck::Verdict::Block;
    pc.text = "The copied objects use the coordinate system " + cb.srcZone + ", but this drawing (project " + destName +
              ") uses " + st.drawingSettings.zoneCode +
              ". Pasting would put them in the wrong place on the ground, so this paste is blocked. "
              "Converting pasted objects is not available yet; open the source drawing and use Add Drawing to Project "
              "(which can convert), or cancel.";
    return pc;
  }
  std::string text;
  if (cb.srcProjectUid != 0 && dest != 0 && cb.srcProjectUid != dest)
    text += "The copied objects come from project \"" + srcName + "\" and are being pasted into project \"" + destName +
            "\". Anything pasted becomes part of the destination project, and point numbers that collide there are "
            "renumbered or you are asked.\n";
  if (unitsDiffer)
    text += "The copied objects are drawn in a different unit than this drawing, so they will come in at the wrong size "
            "unless you scale them afterwards.\n";
  if (!text.empty()) {
    pc.verdict = PasteCheck::Verdict::Warn;
    pc.text = std::move(text);
  }
  return pc;
}
