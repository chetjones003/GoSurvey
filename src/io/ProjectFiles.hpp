#pragma once

// REQ-379 (issue #696 P6) — a project's tracked files: attaching an outside file (copy or link),
// recording what a saved drawing holds, finding a file again, and the Project Health check.
//
// Pure like Project.hpp: <filesystem> + nlohmann::json only; every path is passed in and nothing
// here knows about a drawing or a window. Failures come back as a message for the caller to log
// (REQ-201); nothing throws. All stored locations stay project-relative except a "local-link"
// (REQ-373 clause 6).

#include "Project.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace projfiles {

/// REQ-379 clause 1: a copy or link of at least this size gets an extra warning in the prompt.
/// (ASSUMPTION recorded in TASK-696-p6: the SPEC says "size prompt" without a number.)
inline constexpr std::uintmax_t kLargeFileBytes = 100ull * 1024 * 1024;

enum class Role { PointCloud, Pdf, PointFile };

/// The layout role (REQ-373 clause 2) whose folder holds files of \p r.
const char* LayoutRole(Role r);

/// The role a file belongs to by its extension (.e57/.gscloud → point cloud, .pdf → PDF, else points).
Role RoleForFile(const std::filesystem::path& file);

/// True when \p file lies inside the project folder (symlinks and `..` resolved).
bool IsInsideProject(const gsproj::Project& p, const std::filesystem::path& file);

// --- Attach (clause 1) ---------------------------------------------------------------------------

struct AttachPlan {
  std::filesystem::path source;
  std::filesystem::path dest;      ///< where a copy goes (== source when already in the project)
  std::string           destRel;   ///< project-relative form of dest
  std::uintmax_t        sizeBytes = 0;
  bool                  alreadyInProject = false;  ///< nothing to ask: the file is inside the project
  bool                  reuseExisting = false;     ///< dest already holds an identical copy
  bool Large() const { return sizeBytes >= kLargeFileBytes; }
};

/// Works out where a copy of \p source would go (the role's folder, never over a different file of the
/// same name). Changes nothing on disk. False (with \p err) when the source is not a readable file.
bool PlanAttach(const gsproj::Project& p, const std::filesystem::path& source, Role role, AttachPlan* out,
                std::string* err);

/// Copies the plan's file into the project, keeping its modification time (a point cloud's `.gscloud`
/// cache is matched to its source by size and time) and, for a point cloud, its cache beside it. A
/// failed copy leaves no half-written file. False with \p err on failure.
bool CopyIn(const AttachPlan& plan, Role role, std::string* err);

// --- Recording what a saved drawing holds (clause 2) ----------------------------------------------

struct Attached {
  std::filesystem::path file;
  /// PDFs only: one placement as a JSON object text (without the "drawing" member, added here).
  std::string placementJson;
};

/// Makes the project's record of the drawing \p drawingRel match what the drawing now holds: the
/// drawing itself is tracked; every file in \p clouds / \p pdfs is tracked (in-project, or a
/// "local-link" when outside) and associated with the drawing; a file the drawing no longer holds loses
/// its association and placements for it (the file itself stays tracked). Returns true when the
/// project changed (so the caller saves the .gsproj).
bool SyncDrawing(gsproj::Project* p, const std::string& drawingRel, const std::vector<Attached>& clouds,
                 const std::vector<Attached>& pdfs);

/// Absolute path of a tracked item ("" for a kind this version does not know, REQ-373 clause 6).
std::string ResolveItem(const gsproj::Project& p, const gsproj::TrackedItem& item);

/// The file the drawing \p drawingRel should use for what it stored as \p stored: the tracked item
/// associated with that drawing and named like \p stored when it exists on disk, else \p stored itself
/// when that exists, else "". (A project that moved machines keeps working through the first rule.)
std::string FindAttachedFile(const gsproj::Project& p, const std::string& drawingRel, const std::string& stored);

/// Every PDF placement recorded for \p drawingRel: (the PDF's absolute path, the placement JSON text).
std::vector<std::pair<std::string, std::string>> PlacementsFor(const gsproj::Project& p,
                                                              const std::string& drawingRel);

// --- Project Health (clause 4) --------------------------------------------------------------------

struct Health {
  std::vector<std::string> linked;       ///< "local-link" files: will not travel with the project
  std::vector<std::string> missing;      ///< tracked files not on disk
  std::vector<std::string> unavailable;  ///< a kind this version cannot reach (a future "remote")
  std::vector<std::string> unsaved;      ///< open drawings with changes not saved (names, from the caller)
  bool Clean() const { return linked.empty() && missing.empty() && unavailable.empty() && unsaved.empty(); }
};

Health CheckHealth(const gsproj::Project& p, const std::vector<std::string>& unsavedDrawings);

struct CopyLinksResult {
  size_t                   converted = 0;
  std::vector<std::string> failed;  ///< "<path>: why", REQ-201
};

/// "Copy links into the project": each present "local-link" file is copied into its role's folder and
/// its item becomes "in-project" (associations and placements are kept). A missing file cannot be
/// copied and is reported in `failed`. The caller saves the .gsproj when `converted` > 0.
CopyLinksResult CopyLinksIn(gsproj::Project* p);

}  // namespace projfiles
