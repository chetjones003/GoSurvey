#include "ProjectAddDrawing.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace projadd {

Summary Analyze(const projpts::Db& db, const std::vector<SurveyPoint>& incomingWorld) {
  Summary s;
  std::unordered_map<int, const SurveyPoint*> have;
  have.reserve(db.points.size());
  for (const projpts::Entry& e : db.points)
    have[e.point.id] = &e.point;
  std::unordered_set<int> seen;
  seen.reserve(incomingWorld.size());
  for (const SurveyPoint& p : incomingWorld) {
    if (!seen.insert(p.id).second) {
      ++s.duplicates;
      continue;
    }
    ++s.total;
    const auto it = have.find(p.id);
    if (it == have.end()) {
      ++s.fresh;
    } else if (projpts::SamePoint(*it->second, p)) {
      ++s.identical;
    } else {
      ++s.differing;
      s.conflicts.push_back({p.id, p, *it->second});
    }
  }
  return s;
}

Outcome Apply(projpts::Db* db, const std::vector<SurveyPoint>& incomingWorld,
              const std::map<int, Choice>& choices, const std::string& source, double now) {
  Outcome o;
  std::unordered_map<int, size_t> at;
  at.reserve(db->points.size());
  int maxId = 0;
  for (size_t i = 0; i < db->points.size(); ++i) {
    at[db->points[i].point.id] = i;
    maxId = std::max(maxId, db->points[i].point.id);
  }
  for (const SurveyPoint& p : incomingWorld)
    maxId = std::max(maxId, p.id);

  std::unordered_set<int> seen;
  seen.reserve(incomingWorld.size());
  std::vector<projpts::Entry> appended;
  bool changed = false;
  for (const SurveyPoint& in : incomingWorld) {
    if (!seen.insert(in.id).second)
      continue;  // a number the drawing holds twice: the first one counts
    SurveyPoint p = in;
    p.labelMtextAnnId = 0;  // a label belongs to one drawing, not to the shared point
    const auto d = at.find(p.id);
    if (d == at.end()) {
      appended.push_back({p, source});
      o.shownIds.push_back(p.id);
      ++o.added;
      changed = true;
      continue;
    }
    projpts::Entry& existing = db->points[d->second];
    if (projpts::SamePoint(existing.point, p)) {
      o.shownIds.push_back(p.id);
      ++o.shared;
      continue;
    }
    const auto c = choices.find(p.id);
    const Choice choice = c == choices.end() ? Choice::Skip : c->second;
    if (choice == Choice::Overwrite) {
      existing.point = p;
      existing.sourceDrawing = source;
      o.shownIds.push_back(p.id);
      ++o.overwritten;
      changed = true;
    } else if (choice == Choice::Renumber) {
      const int fresh = ++maxId;
      SurveyPoint np = p;
      np.id = fresh;
      appended.push_back({np, source});
      o.shownIds.push_back(fresh);
      o.renumberMap.emplace_back(p.id, fresh);
      ++o.renumbered;
      changed = true;
    } else {
      ++o.skipped;
    }
  }
  for (projpts::Entry& e : appended)
    db->points.push_back(std::move(e));
  if (changed) {
    ++db->revision;
    if (!db->dirty) {
      db->dirty = true;
      db->dirtySince = now;
    }
  }
  std::sort(o.shownIds.begin(), o.shownIds.end());
  o.shownIds.erase(std::unique(o.shownIds.begin(), o.shownIds.end()), o.shownIds.end());
  return o;
}

projpts::Rules RulesShowing(const std::vector<int>& sortedShownIds) {
  projpts::Rules r;
  if (!sortedShownIds.empty())
    r.idRanges = projpts::IdsToText(sortedShownIds);
  return r;
}

}  // namespace projadd
