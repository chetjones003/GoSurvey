#pragma once

// Crash-resilient autosave storage and recovery scanner.
//
// Pure IO by design, matching RecentDrawings and Project: <filesystem> + nlohmann::json only,
// with the storage root, wall clock, and process-liveness check passed in by the caller so unit
// tests drive every path against a temporary directory with zero UI or OS-process coupling.
//
// Storage layout under <autosaveRoot>:
//   session-<pid>-<startUnixMs>/
//     session.json              -- owning process id, start timestamp, app version
//     tab-<tabUid>.gsauto       -- full lossless GoSurvey document JSON (from SerializeGoSurveyJson)
//     tab-<tabUid>.meta.json    -- small recovery metadata read during startup scan
//
// Writes are staged to sibling *.tmp files and atomically renamed into place (.gsauto before
// .meta.json) so a crash mid-write never leaves a corrupted recovery entry or overwrites the
// previous valid snapshot.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace autosave {

inline constexpr int kFormatVersion = 1;
inline constexpr int kDefaultIntervalMinutes = 5;
inline constexpr int kMinIntervalMinutes = 1;
inline constexpr int kMaxIntervalMinutes = 120;

/// Metadata written alongside each autosaved drawing payload.
struct EntryMeta {
  int          formatVersion = kFormatVersion;
  std::uint32_t tabUid = 0;
  std::string  tabName;
  std::string  originalFilePath;  ///< Absolute .dwg path if previously saved, empty otherwise (UTF-8)
  std::string  projectFilePath;   ///< Absolute .gsproj path if tab belonged to a project, empty otherwise (UTF-8)
  std::int64_t savedAtUnix = 0;   ///< Seconds since Unix epoch when autosave completed
  std::string  appVersion;
};

/// One recoverable drawing discovered in a dead (non-running) session folder.
struct RecoveryEntry {
  std::string           sessionId;
  std::filesystem::path sessionDir;
  std::filesystem::path metaPath;
  std::filesystem::path payloadPath;
  EntryMeta             meta;
  std::uint64_t         payloadSizeBytes = 0;
  /// True when meta.originalFilePath exists on disk and its last write time is strictly newer
  /// than meta.savedAtUnix (beyond a small filesystem timestamp granularity tolerance).
  bool                  originalFileNewer = false;
};

/// Builds a stable, filesystem-safe session directory name for one running GoSurvey instance.
std::string MakeSessionId(std::uint32_t pid, std::int64_t startUnixMs);

/// Directory for a given session under \p autosaveRoot.
std::filesystem::path SessionDirectory(const std::filesystem::path& autosaveRoot,
                                       const std::string& sessionId);

/// Atomically writes \p documentJsonUtf8 and \p meta into \p sessionId under \p autosaveRoot.
/// Lazily creates the session directory and session.json. On failure, sets \p err (when non-null),
/// cleans up temporary files, and leaves any previous committed autosave untouched.
bool WriteEntry(const std::filesystem::path& autosaveRoot,
                const std::string& sessionId,
                std::uint32_t pid,
                std::int64_t sessionStartedAtUnix,
                const EntryMeta& meta,
                std::string_view documentJsonUtf8,
                std::string* err = nullptr);

/// Removes the autosave entry (payload, metadata, and any temp siblings) for \p tabUid in
/// \p sessionId. Removes the session directory too when no tab entries remain in it.
void RemoveTabEntry(const std::filesystem::path& autosaveRoot,
                    const std::string& sessionId,
                    std::uint32_t tabUid);

/// Removes the entire session directory for \p sessionId (called on clean application exit).
void CleanupSession(const std::filesystem::path& autosaveRoot,
                    const std::string& sessionId);

/// Scans \p autosaveRoot for recoverable drawings left behind by terminated sessions.
/// - Skips \p currentSessionId.
/// - Skips any session whose session.json records a pid for which \p pidAlive(pid) returns true.
/// - Cleans up stray *.tmp files and empty dead session directories as it scans.
/// - Returns valid entries sorted newest-first (descending savedAtUnix).
std::vector<RecoveryEntry> ScanRecoverable(const std::filesystem::path& autosaveRoot,
                                           const std::string& currentSessionId,
                                           const std::function<bool(std::uint32_t)>& pidAlive);

/// Reads the full GoSurvey JSON payload of \p entry into \p jsonOut.
bool ReadPayload(const RecoveryEntry& entry, std::string* jsonOut, std::string* err = nullptr);

/// Permanently removes one recovery entry from disk and prunes its parent session directory if
/// it is now empty.
void DiscardEntry(const RecoveryEntry& entry);

/// Permanently removes every entry in \p entries and prunes any resulting empty session folders.
void DiscardAll(const std::vector<RecoveryEntry>& entries);

}  // namespace autosave
