#pragma once

// REQ-378 (issue #696 P5) — what adding a drawing's survey points to a project's database means: the
// preview counts, the per-conflict choices, and the visibility rules the added drawing gets.
//
// Pure like ProjectPointDb.hpp: no window, no drawing, no clock of its own. Points are WORLD
// coordinates (the caller converts at the boundary). Nothing here writes a file.

#include "ProjectPointDb.hpp"
#include "ProjectPointRules.hpp"

#include <map>
#include <string>
#include <vector>

namespace projadd {

/// What the user decides for a number the project already holds with DIFFERENT data (clause 2).
enum class Choice { Skip, Overwrite, Renumber };

/// One incoming point whose number the database holds with different data.
struct Conflict {
  int         id = 0;
  SurveyPoint incoming;
  SurveyPoint existing;
};

/// Clause 1: "47 points found. 12 numbers already exist (3 identical, 9 differ)."
struct Summary {
  size_t total = 0;      ///< distinct point numbers in the drawing
  size_t fresh = 0;      ///< not in the database
  size_t identical = 0;  ///< in the database with the same data
  size_t differing = 0;  ///< in the database with different data (= conflicts.size())
  std::vector<Conflict> conflicts;  ///< in the drawing's order
  size_t duplicates = 0;  ///< a number the drawing holds twice; only the first is considered

  size_t Existing() const { return identical + differing; }
};

/// Compares \p incomingWorld with \p db. Changes nothing.
Summary Analyze(const projpts::Db& db, const std::vector<SurveyPoint>& incomingWorld);

/// What Apply did.
struct Outcome {
  size_t added = 0;        ///< new numbers written
  size_t shared = 0;       ///< identical to a database point: kept as is, shown by the drawing
  size_t skipped = 0;      ///< conflicts left out (Skip, or no choice given)
  size_t overwritten = 0;  ///< conflicts that replaced the database's point
  size_t renumbered = 0;   ///< conflicts added under a new number
  /// Sorted, unique: the numbers (in the database) the drawing shows — exactly the points it brought.
  std::vector<int> shownIds;
  /// old number -> new number, for the renumbered conflicts.
  std::vector<std::pair<int, int>> renumberMap;
};

/// Writes \p incomingWorld into \p db under \p choices (keyed by point number; a conflict with no
/// entry is skipped, the safe outcome). New and overwritten points are tagged \p source; an identical
/// one keeps its own tag. A renumbered point takes the next number above everything the database and
/// the drawing hold. Bumps the database revision and marks it dirty when anything changed.
Outcome Apply(projpts::Db* db, const std::vector<SurveyPoint>& incomingWorld,
              const std::map<int, Choice>& choices, const std::string& source, double now);

/// Clause 5a: the rules that show exactly \p shownIds. A drawing with no points keeps the default
/// rules (an empty filter would show everything, which a drawing that brought nothing should not
/// claim to be a deliberate choice about — see the task's recorded assumption).
projpts::Rules RulesShowing(const std::vector<int>& sortedShownIds);

}  // namespace projadd
