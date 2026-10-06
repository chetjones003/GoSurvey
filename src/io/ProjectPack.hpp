#pragma once

// REQ-380 (issue #696 P7) — Pack Project / Open Packed Project: the project folder as one `.gspack`
// file (a standard ZIP, ADR-066) and back again.
//
// Pure like Project.hpp / ProjectFiles.hpp: <filesystem>, nlohmann::json and the vendored miniz only;
// every path is passed in and nothing here knows about a drawing or a window. Failures come back as a
// message for the caller to log (REQ-201); nothing throws.

#include "Project.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gspack {

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kExtension = ".gspack";
inline constexpr const char* kManifestName = "gspack.json";

/// REQ-380 clause 2: a further size warning appears at this many bytes (ASSUMPTION recorded in
/// TASK-696-p7: a common email attachment limit; the SPEC gives no number).
inline constexpr std::uintmax_t kEmailWarnBytes = 25ull * 1024 * 1024;

struct PackFile {
  std::filesystem::path abs;
  std::string           rel;  ///< project-relative, forward slashes
  std::uintmax_t        sizeBytes = 0;
  std::int64_t          mtimeTicks = 0;  ///< exact modified time (file_time_type ticks), restored on open
  bool                  pointCloud = false;
};

struct PackPlan {
  std::vector<PackFile> files;
  std::uintmax_t        totalBytes = 0;       ///< before compression
  std::uintmax_t        pointCloudBytes = 0;  ///< the point clouds' share of totalBytes
  bool Large() const { return totalBytes >= kEmailWarnBytes; }
};

struct PackOptions {
  bool         excludePointClouds = false;
  std::int64_t nowUnix = 0;
};

struct Manifest {
  int                      formatVersion = kFormatVersion;
  std::string              projectId;
  std::string              projectName;
  std::int64_t             packedUnix = 0;
  std::vector<std::string> excluded;  ///< project-relative files left out of the pack
};

/// Lists what a pack of \p p would hold: every file in the project folder except the lock file and
/// temporary files, and \p skip (the pack's own output when it lies in the project). Changes nothing.
bool PlanPack(const gsproj::Project& p, const std::filesystem::path& skip, PackPlan* out, std::string* err);

/// Writes \p out as a ZIP of the plan (point clouds and their caches left out when asked) plus the
/// `gspack.json` manifest. Written to a temporary file and renamed, so a failure leaves no half pack.
bool WritePack(const gsproj::Project& p, const PackPlan& plan, const PackOptions& opt,
               const std::filesystem::path& out, std::string* err);

/// Checks every entry of \p pack, then extracts it into \p destDir (empty or not yet existing) and
/// points the opened project's left-out point clouds at "unavailable". On any problem nothing is left
/// behind (REQ-380 clause 4). \p gsprojOut receives the extracted marker file.
bool ExtractPack(const std::filesystem::path& pack, const std::filesystem::path& destDir,
                 std::filesystem::path* gsprojOut, Manifest* manifestOut, std::string* err);

}  // namespace gspack
