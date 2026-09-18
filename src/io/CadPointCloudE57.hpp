#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// E57 point-cloud reader (REQ-172, ADR-042 (d)) — vendored `libE57Format` (BSL-1.0) does the
/// XML/binary-section decoding; this file adapts its Simple API to GoSurvey's flat interleaved
/// point layout (architecture §11.8) and REQ-001 refusal semantics.
///
/// Delivered first among REQ-172's five formats per D-2026-09-17-d — the octree/LOD engine
/// (`src/util/pointcloudoctree.hpp`, ADR-060) is proven against E57 before PTS/PTX/LAS/LAZ.
namespace pointcloud_e57 {

struct ReadResult {
  bool ok = false;
  /// Human-readable reason when `!ok` — a truncated/malformed file, an empty scan, or a missing
  /// path. Never populated on success.
  std::string errorMessage;

  /// Interleaved x,y,z (architecture §11.8), Cartesian, in the file's own coordinate frame — E57
  /// stores absolute Cartesian XYZ per scan, so unlike PTX there is no setup transform to apply
  /// here (REQ-172); world-origin rebasing (REQ-101) is the caller's job, same as every other
  /// importer.
  std::vector<double> pointsXyz;
  /// Per-point RGB, 0..1, parallel to `pointsXyz`. Empty when the file carries no colour.
  std::vector<float> colorsRgb;
  /// Per-point intensity, normalized 0..1, parallel to `pointsXyz`. Empty when the file carries no
  /// intensity channel.
  std::vector<float> intensity;

  [[nodiscard]] std::int64_t pointCount() const {
    return static_cast<std::int64_t>(pointsXyz.size() / 3);
  }
};

/// One bounded-size chunk of points, handed to \ref StreamE57File's callback. Never holds more
/// than `chunkPointBudget` points (ADR-060 — this is what keeps a multi-GB scan's peak memory
/// bounded during import, unlike \ref ReadE57File below).
struct PointChunk {
  std::vector<double> pointsXyz;
  std::vector<float> colorsRgb;    ///< empty when the source scan carries no colour.
  std::vector<float> intensity;    ///< empty when the source scan carries no intensity channel.
};

/// Streams every Data3D block in the E57 at `pathUtf8` through `onChunk`, `chunkPointBudget`
/// points at a time, without ever holding the whole file's points in memory at once. Returns
/// `false` with `errorMessage` set on a missing/truncated/malformed file (REQ-001) — `onChunk` is
/// never called after a failure is detected, and a caller building an out-of-core cache from the
/// chunks (`pointcloudcache.hpp`) must discard any partial cache on a `false` return.
///
/// `onChunk` returning `false` aborts the stream early (used by a cache builder that hit its own
/// error, e.g. a disk write failure) — `StreamE57File` then also returns `false`.
[[nodiscard]] bool StreamE57File(const std::string &pathUtf8, std::int64_t chunkPointBudget,
                                  const std::function<bool(const PointChunk &)> &onChunk,
                                  std::string *errorMessage);

/// Sums the declared `pointCount` across every Data3D block's header — cheap (metadata only, no
/// point data read) and used to size a progress bar's total before a long streaming build starts.
/// Returns 0 on any failure (missing/malformed file); callers should treat 0 as "unknown," not
/// "empty," since the real read (`StreamE57File`) is what actually validates the file.
[[nodiscard]] std::int64_t QuickPointCountEstimate(const std::string &pathUtf8);

/// Reads every Data3D block in the E57 at `pathUtf8` into one flattened point set (ADR-042 (a):
/// "multiple setups become multiple clouds or one cloud with recorded setup metadata — Workshop
/// picks the smaller option" — increment 1 picks one cloud; per-setup metadata is not yet
/// recorded, filed as a follow-up if a multi-setup E57 is found in practice).
///
/// **Never throws.** A missing file, an empty path, or a truncated/malformed E57 (libE57Format
/// throws `E57Exception` for all of these) comes back as `ok == false` with `errorMessage` naming
/// what failed — `pointsXyz` is left empty, so a caller cannot accidentally commit a partial cloud
/// (REQ-001, REQ-172 acceptance: "a truncated/malformed E57 ... is refused ... no partial cloud").
[[nodiscard]] ReadResult ReadE57File(const std::string &pathUtf8);

}  // namespace pointcloud_e57
