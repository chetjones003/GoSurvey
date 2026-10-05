#pragma once

// REQ-373 / REQ-374 / REQ-382 (issue #696 P1) — the project file (`<Name>.gsproj`), auto-detect of a
// drawing's project, and the one-editor lock file.
//
// Pure by design, like RecentDrawings: <filesystem> + nlohmann::json only, every path and the wall
// clock / process-liveness check passed in by the caller, so GoSurveyTests drives it against a temp
// folder. Nothing here knows about ImGui or the app state. Failures are returned as a message for the
// caller to log (REQ-201); nothing throws.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gsproj {

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kExtension = ".gsproj";

/// REQ-373 clause 6. `kind` is a string, not an enum, so an unknown future kind (e.g. "remote")
/// survives a load/save instead of failing the load.
inline constexpr const char* kKindInProject = "in-project";
inline constexpr const char* kKindLocalLink = "local-link";

struct TrackedItem {
  std::string              path;  ///< project-relative; for "local-link" an absolute path
  std::string              kind = kKindInProject;
  std::vector<std::string> associations;  ///< project-relative paths of the drawings it is attached to
  /// REQ-379 clause 2 (P6): a JSON array of per-drawing PDF placements, kept verbatim here (the
  /// commands layer reads and writes it). "[]" for everything that is not a placed PDF.
  std::string              placementsJson = "[]";
};

struct Project {
  int                                formatVersion = kFormatVersion;
  std::string                        id;    ///< GUID, created once, never changed
  std::string                        name;
  std::map<std::string, std::string> layout;  ///< role ("drawings") -> project-relative folder
  std::string                        settingsJson = "{}";  ///< REQ-375 fills this; kept verbatim until then
  std::string                        extraJson = "{}";     ///< unknown top-level fields, kept verbatim
  std::vector<TrackedItem>           items;
  std::filesystem::path              file;  ///< absolute path of the .gsproj this was loaded from / saved to

  std::filesystem::path Folder() const { return file.parent_path(); }
};

/// REQ-373 clause 5: true only for a non-empty path that is relative and stays inside the project
/// folder — no drive letter, no leading separator (covers UNC), no ".." segment.
bool IsSafeRelativePath(const std::string& rel);

/// The six standard subfolders (REQ-373 clause 2) as role -> folder.
std::map<std::string, std::string> StandardLayout();

/// Creates `<parentDir>/<name>/` with the marker and the six subfolders. Fails (with \p err) on an
/// empty or filename-illegal name, or when the folder already holds a .gsproj.
bool Create(const std::filesystem::path& parentDir, const std::string& name, Project* out, std::string* err);

/// Reads and validates a .gsproj. Any problem (unreadable, not JSON, wrong shape, unsafe in-project
/// path, newer formatVersion) returns false with \p err; \p out is then unspecified.
bool Load(const std::filesystem::path& gsprojFile, Project* out, std::string* err);

/// Atomic write (REQ-373 clause 7): temp file in the same folder, then rename over the old file.
bool Save(const Project& p, std::string* err);

// --- Auto-detect (REQ-374 clauses 4-5) -----------------------------------------------------------

enum class FindState { None, Found, Damaged };

struct FindResult {
  FindState             state = FindState::None;
  std::filesystem::path file;     ///< the .gsproj (Found), or the folder it was looked for in (Damaged)
  std::string           message;  ///< why it is damaged
};

/// Walks from the drawing's folder up to the drive root and uses the first folder holding a .gsproj.
/// Exactly one that loads → Found; more than one, or one that does not load → Damaged; none → None.
FindResult FindProjectFor(const std::filesystem::path& drawingFile);

// --- One editor at a time (REQ-382) --------------------------------------------------------------

struct LockInfo {
  std::string  user;
  std::string  machine;
  std::uint32_t pid = 0;
  std::int64_t sinceUnix = 0;
};

enum class LockResult { Acquired, HeldByOther };

/// `<Name>.gsproj.lock` beside the project file.
std::filesystem::path LockPath(const std::filesystem::path& gsprojFile);

/// Creates the lock file exclusively. If one already exists, \p holder (when non-null) receives who
/// holds it; an unreadable lock file yields an empty LockInfo (treated as stale by IsStale).
LockResult TryAcquire(const std::filesystem::path& gsprojFile, const LockInfo& me, LockInfo* holder);

/// Stale = the holder is on this machine and its process no longer exists, or the lock is unreadable.
/// A lock held from another machine cannot be judged, so it is never stale here; the user can still
/// take it over with the explicit warning (REQ-382 clause 3).
bool IsStale(const LockInfo& holder, const LockInfo& me, const std::function<bool(std::uint32_t)>& pidAlive);

/// Overwrites the lock with \p me (the user confirmed the takeover warning).
bool TakeOver(const std::filesystem::path& gsprojFile, const LockInfo& me);

/// Removes the lock only if it is still ours (same machine and pid); never deletes another's lock.
void Release(const std::filesystem::path& gsprojFile, const LockInfo& me);

}  // namespace gsproj
