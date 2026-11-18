#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// PTS (Leica Cyclone-style ASCII) point-cloud reader/writer (REQ-172, `spec/file-format-specs.md`
/// §3.2/§6). In-tree, whitespace-only tokenizing — no delimiter sniffing (same spirit as REQ-083).
///
/// Delivered first of REQ-172's four remaining formats (PTS -> PTX -> LAS -> LAZ) per
/// `spec/file-format-specs.md` §6, after E57 (D-2026-09-17-d).
///
/// File shape:
///   line 1: a single positive integer N, the point count.
///   each of the next N lines, whitespace-separated:
///     3 tokens: X Y Z
///     4 tokens: X Y Z Intensity        (Intensity normalized 0.0-1.0)
///     7 tokens: X Y Z Intensity R G B  (R/G/B integers 0-255)
/// A line with any other token count, or a token that fails to parse as a number, is a malformed
/// file (refused, REQ-001). A header count that does not match the number of data lines actually
/// present (too few: truncated; extra trailing non-blank content: also refused) is refused too.
namespace pointcloud_pts {

struct ReadResult {
  bool ok = false;
  /// Human-readable reason when `!ok` — a malformed line, a truncated file, or a missing path.
  /// Never populated on success.
  std::string errorMessage;

  /// Interleaved x,y,z (architecture §11.8), in the file's own coordinate frame — world-origin
  /// rebasing (REQ-101) is the caller's job, same as every other importer.
  std::vector<double> pointsXyz;
  /// Per-point RGB, 0..1, parallel to `pointsXyz`. Empty when no line in the file carried RGB.
  std::vector<float> colorsRgb;
  /// Per-point intensity, normalized 0..1, parallel to `pointsXyz`. Empty when no line in the file
  /// carried an intensity token.
  std::vector<float> intensity;

  [[nodiscard]] std::int64_t pointCount() const {
    return static_cast<std::int64_t>(pointsXyz.size() / 3);
  }
};

/// One bounded-size chunk of points, handed to \ref StreamPtsFile's callback. Never holds more than
/// `chunkPointBudget` points — this is what keeps a multi-GB PTS file's peak memory bounded during
/// import (ADR-060), the same contract `CadPointCloudE57.hpp`'s `PointChunk` makes.
struct PointChunk {
  std::vector<double> pointsXyz;
  std::vector<float> colorsRgb;    ///< empty when no line in this chunk carried RGB.
  std::vector<float> intensity;    ///< empty when no line in this chunk carried intensity.
};

/// Streams the PTS at `pathUtf8` through `onChunk`, `chunkPointBudget` points at a time, without
/// ever holding the whole file in memory at once. Returns `false` with `errorMessage` set on a
/// missing/truncated/malformed file (REQ-001) — `onChunk` is never called for a chunk built from
/// lines that turned out to be part of a truncated/malformed read; a caller building an out-of-core
/// cache from the chunks (`pointcloudcache.hpp`) must still discard any partial cache on a `false`
/// return, exactly as `StreamE57File` documents.
///
/// `onChunk` returning `false` aborts the stream early (used by a cache builder that hit its own
/// error, e.g. a disk write failure) — `StreamPtsFile` then also returns `false`.
[[nodiscard]] bool StreamPtsFile(const std::string &pathUtf8, std::int64_t chunkPointBudget,
                                  const std::function<bool(const PointChunk &)> &onChunk,
                                  std::string *errorMessage);

/// Reads just the header line (the declared point count) — cheap, used to size a progress bar's
/// total before a long streaming build starts. Returns 0 on any failure (missing file, unreadable
/// or non-positive header); callers should treat 0 as "unknown," not "empty," since the real read
/// (`StreamPtsFile`) is what actually validates the file.
[[nodiscard]] std::int64_t QuickPointCountEstimate(const std::string &pathUtf8);

/// Reads the whole PTS at `pathUtf8` into one flattened point set — a convenience, non-streaming
/// reader for small files and tests. **Never throws.** A missing file, an empty path, or a
/// malformed/truncated PTS comes back as `ok == false` with `errorMessage` naming what failed —
/// `pointsXyz` is left empty, so a caller cannot accidentally commit a partial cloud (REQ-001,
/// REQ-172 acceptance: "a truncated/malformed ... PTS is refused ... no partial cloud").
[[nodiscard]] ReadResult ReadPtsFile(const std::string &pathUtf8);

struct WriteResult {
  bool ok = false;
  std::string errorMessage;
};

/// Writes `pointsXyz` (+ `colorsRgb`/`intensity` when non-empty) to a PTS file at `pathUtf8`, in
/// ONE consistent line shape for the whole file: 3-token (X Y Z) when neither `colorsRgb` nor
/// `intensity` is supplied, otherwise 7-token (X Y Z Intensity R G B) — a channel GoSurvey's source
/// cloud does not actually carry is filled with a documented default (intensity 1.0, RGB 255/255/255)
/// rather than omitted, since a PTS file cannot mix line shapes within one write. Fixed-precision
/// formatting: XYZ `%.6f` (keeps survey-grade precision), intensity `%.4f`, RGB as clamped/rounded
/// 0-255 integers.
///
/// Fails cleanly (`ok == false` + `errorMessage`, no partial file left behind) on an empty point set
/// or a disk-write failure.
[[nodiscard]] WriteResult WritePtsFile(const std::string &pathUtf8,
                                        const std::vector<double> &pointsXyz,
                                        const std::vector<float> &colorsRgb,
                                        const std::vector<float> &intensity);

}  // namespace pointcloud_pts
