#pragma once

// REQ-381 (issue #696 P8) — turnover records: what was handed over, when, and to whom.
//
// A turnover is a RECORD, not a bundle (D-2026-10-05-j): a small `.gsturnover` file in the project's
// Turnovers folder naming the tracked items that were included, each with its size and CRC-32 so the
// delivered version can be told apart later, plus the date and the recipient. No file is copied.
//
// Pure like Project.hpp / ProjectFiles.hpp: <filesystem>, nlohmann::json and miniz's CRC only; every
// path and the clock are passed in and nothing here knows about a window. Failures come back as a
// message for the caller to log (REQ-201); nothing throws.

#include "Project.hpp"
#include "ProjectFiles.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace projturn {

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kExtension = ".gsturnover";

struct Entry {
  std::string    path;  ///< the tracked item's path as the project stores it
  std::string    kind;  ///< "in-project" / "local-link" / a future kind
  std::uintmax_t sizeBytes = 0;
  std::string    crcHex;   ///< 8 lower-case hex digits; empty when the file could not be read
  bool           missing = false;  ///< not on disk when the turnover was made (a Health problem the user accepted)
};

struct Record {
  int                formatVersion = kFormatVersion;
  std::string        projectId;
  std::string        projectName;
  std::string        recipient;
  std::int64_t       createdUnix = 0;
  std::string        date;  ///< YYYY-MM-DD (UTC), derived from createdUnix
  std::vector<Entry> items;
  std::string        file;  ///< project-relative path of the record (set by Create / List)
};

/// The tracked items a turnover may include: every tracked item except the turnover records themselves.
std::vector<std::string> Candidates(const gsproj::Project& p);

/// YYYY-MM-DD (UTC) of a Unix time.
std::string DateText(std::int64_t unix);

/// Writes a turnover record for \p chosen (tracked item paths; duplicates are ignored, order kept) into
/// the project's Turnovers folder, tracks the record, and saves the .gsproj. Refused (with \p err, nothing
/// written) when the recipient is blank, nothing is chosen, a chosen path is not a candidate, or Project
/// Health (\p health, REQ-379) has problems and \p acknowledged is false. \p out receives the record.
bool Create(gsproj::Project* p, const std::string& recipient, const std::vector<std::string>& chosen,
            std::int64_t nowUnix, const projfiles::Health& health, bool acknowledged, Record* out,
            std::string* err);

/// Reads one record file. False (with \p err) when it is unreadable, not a record, or newer than this version.
bool Read(const std::filesystem::path& file, Record* out, std::string* err);

/// The records in the project's Turnovers folder, newest first. A damaged one is skipped and named in
/// \p problems ("<file>: why") for the caller to log.
std::vector<Record> List(const gsproj::Project& p, std::vector<std::string>* problems);

/// True when the tracked item \p path is a turnover record (it sits in the project's Turnovers folder).
bool IsRecordPath(const gsproj::Project& p, const std::string& path);

}  // namespace projturn
