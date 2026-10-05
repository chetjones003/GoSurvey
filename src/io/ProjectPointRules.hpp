#pragma once

// REQ-377 (issue #696 P4) — which points of the project database one drawing shows.
//
// A project drawing keeps its own visibility rules (saved in its ADR-044 trailer). Every filter that
// is filled in must match (AND); an empty filter is unused, and no filter at all shows every point —
// that is also what P3 drawings did, so an existing project opens exactly as before. Two lists sit on
// top of the filters: points the drawing CREATED are pinned visible (clause 2), and points the user
// hid "in this drawing only" are never shown (clause 4). Hidden wins over pinned, pinned wins over
// the filters.
//
// Pure like ProjectPointDb.hpp: no window, no drawing, nothing global.

#include "PointGroupRule.hpp"
#include "ProjectPointDb.hpp"

#include <string>
#include <vector>

namespace projpts {

struct Rules {
  std::string idRanges;       ///< point number ranges as typed, "1-500, 1200"; empty = unused
  std::string description;    ///< wildcard on the description (`EG*`), case-insensitive; empty = unused
  bool        useElevation = false;
  double      elevMin = 0.0;  ///< elevation range, inclusive, in the drawing's linear unit
  double      elevMax = 0.0;
  std::string group;          ///< name of a point group of THIS drawing; empty = unused
  std::string sourceDrawing;  ///< only points created in this drawing (Entry::sourceDrawing); empty = unused
  std::vector<int> shown;     ///< sorted, unique: created here, shown whatever the filters say
  std::vector<int> hidden;    ///< sorted, unique: hidden in this drawing only

  bool Filtering() const {
    return !idRanges.empty() || !description.empty() || useElevation || !group.empty() || !sourceDrawing.empty();
  }
  /// True when nothing differs from a brand-new drawing's rules (so the trailer need not carry them).
  bool IsDefault() const { return !Filtering() && shown.empty() && hidden.empty(); }
  bool operator==(const Rules& o) const {
    return idRanges == o.idRanges && description == o.description && useElevation == o.useElevation &&
           elevMin == o.elevMin && elevMax == o.elevMax && group == o.group && sourceDrawing == o.sourceDrawing &&
           shown == o.shown && hidden == o.hidden;
  }
  bool operator!=(const Rules& o) const { return !(*this == o); }
};

/// Sorted-unique insert / erase / lookup on \ref Rules::shown and \ref Rules::hidden.
void AddId(std::vector<int>* ids, int id);
void RemoveId(std::vector<int>* ids, int id);
bool HasId(const std::vector<int>& ids, int id);

/// The database entries \p rules shows, in database order. \p groupRule is the drawing's point group
/// named by \ref Rules::group (null when that group no longer exists — a missing group matches
/// nothing, never everything). \p badTokens receives idRanges text that could not be parsed.
std::vector<const Entry*> Visible(const Rules& rules, const PointGroupRule* groupRule, const Db& db,
                                  std::vector<std::string>* badTokens = nullptr);

/// How many entries of \p db are NOT visible under \p rules (for the toolspace summary).
size_t HiddenCount(const Rules& rules, const PointGroupRule* groupRule, const Db& db);

/// "1-5,9,11-20" <-> ids; compact so a drawing that created 100k points does not store 100k numbers.
std::string IdsToText(const std::vector<int>& sortedIds);
std::vector<int> IdsFromText(const std::string& text);

}  // namespace projpts
