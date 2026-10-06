#pragma once

// REQ-376 / ADR-065 (issue #696 P3) — a project's one survey point database: the file
// `Points/survey-points.gspdb`, the in-memory copy every drawing tab of the project shares, and the
// pure diff that folds one drawing's edits into it.
//
// Pure by design, like Project.hpp: <filesystem> + nlohmann::json + the SurveyPoint struct only, the
// clock passed in by the caller, so GoSurveyTests drives it against a temp folder. Nothing here knows
// about ImGui, drawings or tabs. Failures come back as a message for the caller to log (REQ-201);
// nothing throws.
//
// COORDINATES ARE WORLD (state-plane), not drawing-local. A drawing stores points local to its own
// document origin (project_local_storage_invariant); two drawings of one job can have different
// origins, so the shared database must hold the one coordinate every drawing agrees on. The tab glue
// (src/commands/ProjectPoints.cpp) converts at the boundary.

#include "SurveyPoints.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace projpts {

inline constexpr int kFormatVersion = 1;

/// A point plus the drawing that created it (REQ-376 clause 3). `point.labelMtextAnnId` is always 0
/// here: a label MTEXT belongs to one drawing, not to the shared point.
struct Entry {
  SurveyPoint point;
  std::string sourceDrawing;  ///< project-relative drawing path, or the tab name for an unsaved drawing
};

struct Db {
  std::string        projectId;
  std::vector<Entry> points;
  /// Bumped on every change. A tab remembers the revision it last saw; a different one means "pull".
  std::uint64_t revision = 0;
  bool          dirty = false;      ///< changed since the last successful Save
  double        dirtySince = 0.0;   ///< caller's clock when it first became dirty
  double        nextRetry = 0.0;    ///< after a failed Save, do not retry before this time
};

/// What one drawing's edits did to the database.
struct Change {
  int added = 0;
  int edited = 0;
  int removed = 0;
  bool Any() const { return added + edited + removed > 0; }
};

/// `<projectFolder>/<pointsFolder>/survey-points.gspdb` (pointsFolder is the project's "points" role).
std::filesystem::path DbPath(const std::filesystem::path& projectFolder, const std::string& pointsFolder);

/// Reads the database. A missing file is an empty database (a new project) and succeeds. A file that
/// is unreadable, is not a database, is a newer format, or belongs to a different project fails with
/// \p err — the caller must then NOT write over it.
bool Load(const std::filesystem::path& file, const std::string& projectId, Db* out, std::string* err);

/// Atomic write (ADR-065 (d)): temp file in the same folder, then rename over the old file.
/// Creates the folder when it is missing. A failure leaves the previous file untouched.
bool Save(const std::filesystem::path& file, const Db& db, std::string* err);

/// Same data apart from the label link (which is the drawing's own). Coordinates are compared to
/// 1e-7, so converting local->world->local after a document-origin rebase is not read as an edit.
bool SamePoint(const SurveyPoint& a, const SurveyPoint& b);

/// True when \p local, shifted by (\p ox, \p oy) into world coordinates, is exactly \p baseWorld point
/// for point and in order. The common "nothing changed this frame" case — no allocation.
bool EqualsWorld(const std::vector<SurveyPoint>& local, double ox, double oy, const std::vector<SurveyPoint>& baseWorld);

/// \p local as world points (label links cleared).
std::vector<SurveyPoint> ToWorld(const std::vector<SurveyPoint>& local, double ox, double oy);

/// The database's points, world coordinates, in database order.
std::vector<SurveyPoint> View(const Db& db);

/// Folds one drawing's changes into the database: \p baseWorld is what the drawing last agreed with
/// the database, \p curWorld what it holds now. By point number: a number only in \p curWorld is added
/// (tagged \p source), one only in \p baseWorld is removed, one in both whose data differs is edited
/// (its source is kept). A number the drawing "adds" that the database already has is overwritten and
/// reported through \p log — it cannot normally happen (a drawing shows every database point), so it
/// is told rather than hidden (REQ-201).
Change ApplyChanges(Db* db, const std::vector<SurveyPoint>& baseWorld, const std::vector<SurveyPoint>& curWorld,
                    const std::string& source, double now, std::vector<std::string>* log);

}  // namespace projpts
