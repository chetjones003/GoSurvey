#pragma once

// REQ-378 (issue #696 P5) — Add Drawing to Project, without a window.
//
// Three steps, so the dialog can sit between them and a cancel at any point changes nothing:
//   1. PrepareAddDrawing  — reads the chosen DWG into a private copy of the app state and works out the
//                           preview (counts, mismatch, overrides). Reads only; nothing is written.
//   2. ChooseAddConvert   — (optional) converts that private copy to the project's coordinate system
//                           and units. Still nothing is written.
//   3. CommitAddDrawing   — writes the copy into the project's Drawings folder and its points into the
//                           database. The user's original is never opened for writing.

#include "CadCommands.hpp"
#include "geo/DrawingConversion.hpp"
#include "io/ProjectAddDrawing.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct AddDrawingPlan {
  std::uint32_t projectUid = 0;
  std::string   projectName;
  std::string   sourcePath;  ///< the drawing the user picked (UTF-8); never written
  /// Non-empty = the add cannot go ahead at all (unreadable drawing, read-only project, ...). REQ-201 text.
  std::string error;

  /// The drawing, loaded into a private state. Converted in place by ChooseAddConvert.
  std::unique_ptr<AppCommandState> drawing;

  std::vector<SurveyPoint> pointsWorld;  ///< its points, world coordinates, in the project's frame
  projadd::Summary         summary;

  // Mismatch (clause 5)
  geo::ConversionPlan      conversion;  ///< what differs; ok/error say whether Convert is possible
  std::vector<std::string> blockers;    ///< kinds of object Convert cannot move
  bool                     converted = false;

  /// Settings (REQ-375 names) where the drawing differs from the project's defaults: they become overrides.
  std::vector<std::string> overrides;

  /// Clause 2: the user's choice per conflicting number. A conflict with no entry is skipped.
  std::map<int, projadd::Choice> choices;

  /// True while a coordinate-system or unit mismatch stands between the drawing and the project.
  bool Blocked() const { return conversion.Needed() && !converted; }
  /// True when Convert can be offered (the transform is within tolerance and nothing stops it).
  bool CanConvert() const { return conversion.Needed() && !converted && conversion.ok && blockers.empty(); }
};

/// Step 1. Never throws; a failure is in \p out->error.
void PrepareAddDrawing(const AppCommandState& st, std::uint32_t projectUid, const std::string& dwgPath,
                       AddDrawingPlan* out, std::vector<std::string>& log);

/// Step 2. Converts the plan's private copy to the project's coordinate system and units and refreshes
/// the preview. False (with \p plan.error untouched and a REQ-201 line in \p log) when it cannot.
bool ChooseAddConvert(const AppCommandState& st, AddDrawingPlan* plan, std::vector<std::string>& log);

/// What a commit did, for the dialog / log and the caller that opens the new tab.
struct AddDrawingResult {
  std::string        destPath;  ///< absolute path of the copy in the project (UTF-8)
  std::string        destRel;   ///< project-relative, e.g. "Drawings/EG.dwg"
  projadd::Outcome   outcome;
};

/// Step 3. Fails (false, reason in \p log, nothing changed) when the project is read-only or its
/// database is unavailable, the plan is still blocked, or the copy cannot be written. \p now is the
/// caller's monotonic clock in seconds (the database's debounced save).
bool CommitAddDrawing(AppCommandState& st, AddDrawingPlan& plan, double now, AddDrawingResult* out,
                      std::vector<std::string>& log);
