#pragma once

// REQ-376 / ADR-065 (issue #696 P3) — keeps each project drawing's survey points and the project's
// one point database in step.
//
// A project drawing still works on `st.surveyPoints` (every command already does), so nothing that
// edits points changes. Once a frame this module compares that list with what the tab last agreed
// with the database: a difference is the user's edit and is folded into the database; a database
// that has moved on (another tab edited it) is pulled into the tab. The database is the single copy
// of the truth; the tab's list is its working view of it.

#include "CadCommands.hpp"

#include <string>
#include <vector>

/// Reads the project's point database into \p s.points (a missing file is an empty database).
/// A file that cannot be read leaves \p s.points null and says why in \p s.pointsError and \p log;
/// it is never overwritten (REQ-201).
void OpenProjectPointDb(AppCommandState::ProjectSession& s, std::vector<std::string>& log);

/// One frame of synchronisation for the ACTIVE drawing tab, plus the debounced save of every open
/// project's database. \p now is a monotonic clock in seconds. Standalone tabs are not touched.
void SyncProjectPoints(AppCommandState& st, std::vector<std::string>& log, double now);

/// Writes \p s's database now if it has unsaved changes (the lock holder only; a read-only opener
/// never writes). False when a write was needed and failed.
bool FlushProjectPointDb(AppCommandState::ProjectSession& s, std::vector<std::string>& log);

/// True when the active tab's points live in the project database rather than in the DWG, so the
/// DWG writers leave them out (REQ-376 clause 1, ADR-065 (c)).
inline bool ProjectOwnsActiveTabPoints(const AppCommandState& st) {
  const int i = st.activeDrawingIdx;
  return i >= 1 && i < static_cast<int>(st.drawingTabs.size()) &&
         st.drawingTabs[static_cast<size_t>(i)].projectUid != 0 &&
         st.drawingTabs[static_cast<size_t>(i)].pointsMode == AppCommandState::DrawingTab::PointsMode::Shared;
}

// ---- REQ-377 (issue #696 P4): per-drawing visibility rules -------------------------------------
// The rules themselves are `st.pointVisibility` (projpts::Rules); SyncProjectPoints rebuilds the
// tab's points whenever they change. The helpers below are what the Survey Database toolspace
// section needs; all of them are no-ops / empty for a standalone drawing.

/// The project database of the ACTIVE tab when that tab's points are shared with it, else null.
projpts::Db* ActiveProjectDb(AppCommandState& st);

struct ProjectPointCounts {
  size_t total = 0;   ///< points in the project database
  size_t shown = 0;   ///< of those, shown in the active drawing
  size_t hidden = 0;  ///< and not shown here
};
ProjectPointCounts CountProjectPoints(AppCommandState& st);

/// Distinct source drawings of the database's points, sorted (the Source drawing filter's choices).
std::vector<std::string> ProjectPointSources(AppCommandState& st);

/// REQ-377 clause 4: hides the selected points in the active drawing only; the database keeps them
/// and other drawings are unaffected. Returns how many were hidden.
int HideSelectedPointsHere(AppCommandState& st);

// ---- REQ-400: the Survey Point Database grid panel -------------------------------------------

enum class EditDatabasePointStatus {
  NotFound,   ///< no active project database, or no entry numbered \c pointNumber
  ReadOnly,   ///< the project is open read-only (REQ-382); nothing was changed
  Collision,  ///< \p newValues.id is already used by a DIFFERENT entry; nothing was changed
  Applied,
};
struct EditDatabasePointResult {
  EditDatabasePointStatus status = EditDatabasePointStatus::NotFound;
};

/// Writes \p newValues over the database entry currently numbered \p pointNumber (REQ-400 clause 4):
/// coordinates, elevation, description and the point number itself. Bumps the database's revision so
/// every open tab of the project pulls the change next frame (same bookkeeping as \c ApplyChanges).
/// \p newValues.id may equal \p pointNumber (ordinary edit) or differ (a renumber); a renumber onto a
/// number some OTHER entry already holds is refused (status \c Collision) rather than overwritten —
/// the caller resolves it through the project's existing overwrite/renumber/cancel question.
EditDatabasePointResult EditDatabasePoint(AppCommandState& st, int pointNumber, const SurveyPoint& newValues,
                                          double now, std::vector<std::string>& log);

/// Selects, in the active drawing's point selection, every entry of \p pointNumbers that is currently
/// visible there (REQ-377). Returns how many of \p pointNumbers were NOT selected because they are
/// hidden here or do not exist.
int SelectDatabasePoints(AppCommandState& st, const std::vector<int>& pointNumbers);

enum class GridConflictAnswer { Overwrite, Renumber, Cancel };

/// Carries out the user's answer to \c AppCommandState::surveyPointGridConflict (REQ-400 clause 5):
/// Overwrite removes the entry that held the colliding number and gives it to the edited point;
/// Renumber keeps the colliding entry and gives the edited point the next free number instead;
/// Cancel discards the edit. Clears the conflict either way.
void ResolveGridNumberConflict(AppCommandState& st, GridConflictAnswer answer, double now,
                               std::vector<std::string>& log);
