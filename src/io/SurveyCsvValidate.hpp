#pragma once

// REQ-041 — pure, unit-testable validation helpers for the Import points window.
// These hold the file-state classification and duplicate-ID diagnostics so they can
// be tested without the UI/IO sources (the NumFormat/AngleFormat/CommandBar pattern).
// The actual filesystem probing and CSV parsing stay in SurveyCsv.cpp; this header
// only decides messages from already-gathered facts.

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace survey_csv {

/// Distinguishable states of the selected import file.
enum class FileState {
  Ok,         ///< exists, non-empty, and openable for reading
  NotFound,   ///< the path does not exist
  Empty,      ///< exists but has no bytes
  CannotOpen, ///< exists and non-empty, but open() failed (locked / permission denied)
};

/// Classify a file from already-probed facts (kept pure so it is unit-testable).
inline FileState ClassifyFileState(bool exists, bool empty, bool canOpen) {
  if (!exists)
    return FileState::NotFound;
  if (empty)
    return FileState::Empty;
  if (!canOpen)
    return FileState::CannotOpen;
  return FileState::Ok;
}

/// Human-facing, specific message for a file state. Ok yields an empty string.
inline const char* FileStateMessage(FileState s) {
  switch (s) {
  case FileState::Ok:
    return "";
  case FileState::NotFound:
    return "File not found.";
  case FileState::Empty:
    return "File is empty.";
  case FileState::CannotOpen:
    return "File exists but could not be opened — it may be open in another application.";
  }
  return "";
}

/// A parsed data row's point ID and 1-based source line number.
struct RowId {
  int id = 0;
  std::size_t line = 0;
};

/// Result of scanning parsed-with-ID rows for duplicates.
struct DuplicateScan {
  std::vector<std::string> messages;          ///< one diagnostic per duplicate/collision row
  std::unordered_set<std::size_t> badLines;   ///< source lines that would be skipped on import
};

/// Detect duplicate point IDs, mirroring the importer's skip rules exactly: a row
/// whose ID already exists in the session (\p sessionIds) or was already taken by an
/// earlier file row is skipped. \p rows must list only rows that parsed and carry an
/// explicit ID, in source order. A session collision is reported in preference to a
/// within-file duplicate when both apply.
inline DuplicateScan ScanDuplicateIds(const std::vector<RowId>& rows,
                                      const std::unordered_set<int>& sessionIds) {
  DuplicateScan out;
  std::unordered_map<int, std::size_t> fileFirst; // id -> first file line that claimed it
  for (const RowId& r : rows) {
    if (sessionIds.count(r.id) > 0) {
      out.messages.push_back("Point ID " + std::to_string(r.id) +
                             " already exists in the drawing (line " + std::to_string(r.line) + ").");
      out.badLines.insert(r.line);
      continue;
    }
    auto it = fileFirst.find(r.id);
    if (it == fileFirst.end()) {
      fileFirst.emplace(r.id, r.line);
    } else {
      out.messages.push_back("Duplicate point ID " + std::to_string(r.id) + " (line " +
                             std::to_string(it->second) + " and line " + std::to_string(r.line) + ").");
      out.badLines.insert(r.line);
    }
  }
  return out;
}

/// REQ-041 revision 3 — the validation summary shown **after** an import has run.
///
/// This replaces the pre-import text rather than adding to it, and the panel stops re-validating
/// until the user changes something. The reason is BUG-014: re-validating the file that was just
/// imported scans it against a session containing the points that import created, so every row
/// collides with itself and a successful import renders as "Cannot import — no valid data rows".
/// The counts are reported unconditionally, zeros included — "0 row(s) skipped" is the sentence
/// that tells a surveyor nothing was silently dropped.
inline std::string ImportOutcomeSummary(int imported, int skipped) {
  return "Imported " + std::to_string(imported) + " point(s) — " + std::to_string(skipped) +
         " row(s) skipped.\nAlready imported into this drawing. Pick another file, or change the "
         "column order to import it again.\n";
}

} // namespace survey_csv
