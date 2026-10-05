#include "ProjectPointRules.hpp"

#include <algorithm>

namespace projpts {

void AddId(std::vector<int>* ids, int id) {
  const auto it = std::lower_bound(ids->begin(), ids->end(), id);
  if (it == ids->end() || *it != id)
    ids->insert(it, id);
}

void RemoveId(std::vector<int>* ids, int id) {
  const auto it = std::lower_bound(ids->begin(), ids->end(), id);
  if (it != ids->end() && *it == id)
    ids->erase(it);
}

bool HasId(const std::vector<int>& ids, int id) { return std::binary_search(ids.begin(), ids.end(), id); }

namespace {

struct Matcher {
  const Rules& r;
  const PointGroupRule* groupRule;
  std::vector<PointIdRange> ranges;
  std::vector<PointIdRange> groupRanges;

  Matcher(const Rules& rules, const PointGroupRule* g, std::vector<std::string>* bad) : r(rules), groupRule(g) {
    if (!r.idRanges.empty())
      ranges = ParseIdRanges(r.idRanges, bad);
    if (groupRule)
      groupRanges = ParseIdRanges(groupRule->idRangesText);
  }

  bool Filters(const Entry& e) const {
    const SurveyPoint& p = e.point;
    if (!r.idRanges.empty() && !IdInRanges(ranges, p.id))
      return false;
    if (!r.description.empty() && !WildcardMatchCI(r.description, p.description))
      return false;
    if (r.useElevation &&
        (p.elevation < std::min(r.elevMin, r.elevMax) || p.elevation > std::max(r.elevMin, r.elevMax)))
      return false;
    if (!r.group.empty() &&
        (!groupRule || !PointMatchesRule(*groupRule, groupRanges, p.id, p.description, p.rawDescription)))
      return false;
    if (!r.sourceDrawing.empty() && e.sourceDrawing != r.sourceDrawing)
      return false;
    return true;
  }

  bool Shows(const Entry& e) const {
    if (HasId(r.hidden, e.point.id))
      return false;
    if (HasId(r.shown, e.point.id))
      return true;
    return Filters(e);
  }
};

}  // namespace

std::vector<const Entry*> Visible(const Rules& rules, const PointGroupRule* groupRule, const Db& db,
                                  std::vector<std::string>* badTokens) {
  const Matcher m(rules, groupRule, badTokens);
  std::vector<const Entry*> out;
  out.reserve(db.points.size());
  for (const Entry& e : db.points)
    if (m.Shows(e))
      out.push_back(&e);
  return out;
}

size_t HiddenCount(const Rules& rules, const PointGroupRule* groupRule, const Db& db) {
  const Matcher m(rules, groupRule, nullptr);
  size_t n = 0;
  for (const Entry& e : db.points)
    if (!m.Shows(e))
      ++n;
  return n;
}

std::string IdsToText(const std::vector<int>& ids) {
  std::string out;
  for (size_t i = 0; i < ids.size();) {
    size_t j = i;
    while (j + 1 < ids.size() && ids[j + 1] == ids[j] + 1)
      ++j;
    if (!out.empty())
      out += ',';
    out += std::to_string(ids[i]);
    if (j > i)
      out += "-" + std::to_string(ids[j]);
    i = j + 1;
  }
  return out;
}

std::vector<int> IdsFromText(const std::string& text) {
  std::vector<int> out;
  for (const PointIdRange& r : ParseIdRanges(text))
    for (int v = r.lo; v <= r.hi; ++v)
      out.push_back(v);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace projpts
