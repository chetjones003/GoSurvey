#pragma once

// REQ-308 / D-2026-08-30-b — the recent-drawings MRU store (`gosurvey-recent.json`).
//
// Pure by design: every function takes the store path (and "now") as a parameter and touches only
// <filesystem>/<fstream> + nlohmann::json. The %APPDATA% path resolution and the wall clock live in
// the caller (src/ui/CadUi_StartScreen.cpp), same split as RecentDrawingsTests, which drives these
// against a temp file. Load is best-effort: a missing, unparseable, or non-array file yields an
// empty list — never an exception, never a dialog.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace recent {

/// Newest-first cap. A round number; the panel scrolls, so this is about store size, not layout.
inline constexpr int kMaxEntries = 20;

struct Entry {
  std::string   path;               ///< absolute drawing path (the identity; dedup key)
  std::string   name;               ///< display name (the path stem at the time it was noted)
  std::string   thumb;              ///< thumbnail file name within the thumbnails dir, or "" if none
  std::int64_t  lastOpenedUnix = 0; ///< seconds since epoch when last opened/saved
};

/// Reads the store. Returns newest-first, at most kMaxEntries. Any error → empty vector.
std::vector<Entry> Load(const std::filesystem::path& jsonFile);

/// Moves \p drawingPath to the front (creating it if new), stamps \p nowUnix, writes the store.
/// \p thumbFileName replaces the entry's thumbnail when non-empty; an empty string keeps whatever
/// thumbnail the entry already had (so "noted on open" then "thumbnail captured a frame later" both
/// land on the same entry). \p drawingPath is stored as given — the caller passes an absolute path.
void Note(const std::filesystem::path& jsonFile, const std::string& drawingPath,
          const std::string& thumbFileName, std::int64_t nowUnix);

/// Drops \p drawingPath from the store if present (used when a recent entry fails to open).
/// A path that is not in the store is a silent no-op.
void Remove(const std::filesystem::path& jsonFile, const std::string& drawingPath);

}  // namespace recent
