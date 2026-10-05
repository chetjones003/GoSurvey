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
