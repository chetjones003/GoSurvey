#pragma once

// REQ-383 (issue #696 P9) — the warnings that stand between a user and a destructive action on the
// project's shared data: deleting points other drawings show, reusing a number that already exists,
// pasting across projects / coordinate systems / units, and closing with a database that could not be
// saved. The rules are here, without a window, so GoSurveyTests drives them; the dialogs that ask are
// in ui/CadUi_Projects.cpp and only call AnswerPointEdit / the paste and close helpers below.

#include "CadCommands.hpp"

#include <string>
#include <vector>

/// What one frame's edits to a shared drawing put at risk (pure: databases and point lists only).
struct PointEditRisks {
  std::vector<int> removed;    ///< numbers the drawing deleted that the database still holds
  std::vector<int> conflicts;  ///< numbers the drawing "added" that the database holds with other data
};
PointEditRisks FindPointEditRisks(const std::vector<SurveyPoint>& baseWorld, const std::vector<SurveyPoint>& curWorld,
                                  const projpts::Db& db);

/// Clause 4: how many other drawings of the project would lose \p removedIds. Open drawings are judged
/// by their saved visibility rules; closed ones (their rules live in their own DWG, which is not read for
/// a warning) are counted as "might". \p thisTabIdx is the drawing doing the deleting.
struct OtherDrawings {
  int open = 0;                      ///< open drawings of the project that show at least one of the points
  int closedMaybe = 0;               ///< project drawings that are not open
  std::vector<std::string> names;    ///< the open ones
  int Total() const { return open + closedMaybe; }
};
OtherDrawings CountOtherDrawings(const AppCommandState& st, std::uint32_t projectUid, int thisTabIdx,
                                 const std::vector<int>& removedIds);

/// Clause 2: the number a renumbered point gets — above every number in the database, the drawing and
/// \p floorId. Successive calls hand out successive numbers.
int NextFreePointNumber(const projpts::Db& db, const std::vector<SurveyPoint>& drawingPoints, int floorId);

/// Stores the user's answer to the active point-edit question. \p deleteQuestion picks which of the two
/// questions it answers. The next SyncProjectPoints frame carries it out.
void AnswerPointEdit(AppCommandState& st, bool deleteQuestion, AppCommandState::PointEditPrompt::Answer a);

// ---- clause 6: closing with a database that could not be saved -------------------------------

/// Tries to write every open project's database, then lists the projects whose changes are STILL unsaved
/// (a full or locked disk). \p onlyUid != 0 limits it to that project. A read-only project never writes.
std::vector<UnsavedProject> ProjectsWithUnsavedPoints(AppCommandState& st, std::uint32_t onlyUid,
                                                      std::vector<std::string>& log);

// ---- clauses 1 and 3: paste ---------------------------------------------------------------------

/// Remembers where the copied objects came from (the ACTIVE drawing).
void TagClipboardOrigin(AppCommandState& st);

struct PasteCheck {
  enum class Verdict { Ok, Warn, Block } verdict = Verdict::Ok;
  std::string text;  ///< plain-English reason; empty when Ok
  bool canConvert = false;  ///< Convert (D-2026-10-07-a) would make the paste match: within tolerance, nothing in the way
};
/// Compares the clipboard's origin with the ACTIVE drawing. Two standalone drawings are never checked, so
/// nothing changes outside projects.
PasteCheck CheckClipboardPaste(const AppCommandState& st);

/// Convert the clipboard into the ACTIVE drawing's coordinate system and units (REQ-383 clause 7): the same
/// similarity transform as Add Drawing, applied to the copied objects so they keep their ground position.
/// On success the clipboard belongs to the active drawing's system, so the paste check passes. On failure
/// (out of tolerance, an object the transform cannot move, a paper-space copy) the clipboard is untouched and
/// the reason is logged.
bool ConvertClipboardForPaste(AppCommandState& st, std::vector<std::string>& log);
