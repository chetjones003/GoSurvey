#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// PTX (Leica Cyclone-style ASCII, per-scan-setup) point-cloud reader/writer (REQ-172,
/// `spec/file-format-specs.md` §3.2/§6). In-tree, whitespace-only tokenizing — no delimiter
/// sniffing (same spirit as REQ-083 and `CadPointCloudPts.hpp`).
///
/// Delivered after PTS, per `spec/file-format-specs.md` §6's PTS -> PTX -> LAS -> LAZ order
/// (issue #825 increment 2).
///
/// File shape: one or more "scan" blocks concatenated back to back. Each block is:
///   line 1:        ncols            (grid columns, positive integer)
///   line 2:        nrows            (grid rows, positive integer)
///   line 3:        sx sy sz         (scanner registered position — 3 doubles; informational only,
///                                    NOT used to transform points: the 4x4 transform below is the
///                                    single source of truth)
///   lines 4-6:     3 doubles each   (scanner registered X/Y/Z axes — informational only, same as
///                                    the position line: parsed and validated for shape, not used)
///   lines 7-10:    4 doubles each   (the scan's 4x4 setup transform, row-major: row 0 = m00 m01
///                                    m02 m03, ... row 3 = m30 m31 m32 m33). **Convention used by
///                                    this reader/writer**: standard affine column-vector form,
///                                    `P_world = M * [x y z 1]^T`, i.e. translation lives in
///                                    column 3 of rows 0-2 (m03, m13, m23) and row 3 is normally
///                                    `0 0 0 1`. Row 3 is parsed (for shape validation) but not
///                                    otherwise enforced or used.
///   next ncols*nrows lines:         each "x y z intensity" (4 tokens) or "x y z intensity r g b"
///                                    (7 tokens) — same per-line shapes as PTS. A row whose raw
///                                    (pre-transform) x, y, z, and intensity are ALL exactly 0 is
///                                    PTX's "no return" convention for that grid cell: it still
///                                    counts toward ncols*nrows but is NOT emitted as a point.
///
/// Multiple scan blocks in one file are merged into a SINGLE cloud (each block's points
/// transformed by ITS OWN 4x4, then concatenated) — matching PTS's "one file -> one cloud"
/// precedent, since GoSurvey has no multi-cloud-per-import UI yet. This is a documented choice,
/// not a spec requirement either way; `spec/file-format-specs.md` §3.2's "one cloud per scan
/// setup" phrasing describes each setup's own transform landing its points in world space, not a
/// mandate to surface N separate cloud entities.
///
/// A line with the wrong token count, an unparsable number, a non-positive ncols/nrows, or a
/// header/data-line-count mismatch (too few: truncated; extra trailing non-blank content that
/// does not parse as the next scan's header: also refused) is a malformed file (REQ-001):
/// refused, specific message, no partial cloud.
namespace pointcloud_ptx {

struct ReadResult {
  bool ok = false;
  /// Human-readable reason when `!ok` — a malformed line, a truncated file, or a missing path.
  /// Never populated on success.
  std::string errorMessage;

  /// Interleaved x,y,z (architecture §11.8), already transformed into each scan's own world frame
  /// by that scan's 4x4 — world-origin rebasing (REQ-101) is the caller's job, same as every
  /// other importer.
  std::vector<double> pointsXyz;
  /// Per-point RGB, 0..1, parallel to `pointsXyz`. Empty when no line in the file carried RGB.
  std::vector<float> colorsRgb;
  /// Per-point intensity, normalized 0..1, parallel to `pointsXyz`. PTX data lines always carry an
  /// intensity token (4 or 7 tokens — never a bare 3-token XYZ), so this is empty only when the
  /// file turned out to contain zero emitted points.
  std::vector<float> intensity;

  [[nodiscard]] std::int64_t pointCount() const {
    return static_cast<std::int64_t>(pointsXyz.size() / 3);
  }
};

/// One bounded-size chunk of points, handed to \ref StreamPtxFile's callback. Never holds more
/// than `chunkPointBudget` points — same out-of-core streaming contract as
/// `CadPointCloudPts.hpp`'s `PointChunk` / `CadPointCloudE57.hpp`'s.
struct PointChunk {
  std::vector<double> pointsXyz;
  std::vector<float> colorsRgb;    ///< empty when no emitted line in this chunk carried RGB.
  std::vector<float> intensity;    ///< empty when this chunk emitted zero points.
};

/// Streams the PTX at `pathUtf8` through `onChunk`, `chunkPointBudget` points at a time, without
/// ever holding the whole file in memory at once. Returns `false` with `errorMessage` set on a
/// missing/truncated/malformed file (REQ-001) — `onChunk` is never called for a chunk built from
/// lines that turned out to be part of a truncated/malformed read; a caller building an
/// out-of-core cache from the chunks (`pointcloudcache.hpp`) must still discard any partial cache
/// on a `false` return, exactly as `StreamPtsFile`/`StreamE57File` document.
///
/// `onChunk` returning `false` aborts the stream early (used by a cache builder that hit its own
/// error) — `StreamPtxFile` then also returns `false`.
[[nodiscard]] bool StreamPtxFile(const std::string &pathUtf8, std::int64_t chunkPointBudget,
                                  const std::function<bool(const PointChunk &)> &onChunk,
                                  std::string *errorMessage);

/// Cheap point-count estimate for a progress bar's total: reads just the FIRST scan block's
/// `ncols`/`nrows` header lines (2 lines) and reports their product. **Under-counts a multi-scan
/// file** (reports only the first block's grid size) — reading every block's header exactly would
/// mean skipping every data line of every earlier block first, which is not "cheap" for a
/// multi-GB file. Callers already treat 0 as "unknown, not empty" (same contract as
/// `pointcloud_pts::QuickPointCountEstimate`); a non-zero-but-low estimate here is the same
/// tolerance extended to the multi-scan case. The real read (`StreamPtxFile`) is what actually
/// validates the file and counts every point. Returns 0 on any failure (missing file, unreadable,
/// non-positive header).
[[nodiscard]] std::int64_t QuickPointCountEstimate(const std::string &pathUtf8);

/// Reads the whole PTX at `pathUtf8` into one flattened, merged point set — a convenience,
/// non-streaming reader for small files and tests. **Never throws.** A missing file, an empty
/// path, or a malformed/truncated PTX comes back as `ok == false` with `errorMessage` naming what
/// failed — `pointsXyz` is left empty, so a caller cannot accidentally commit a partial cloud
/// (REQ-001, REQ-172 acceptance: "a truncated/malformed ... PTX is refused ... no partial cloud").
[[nodiscard]] ReadResult ReadPtxFile(const std::string &pathUtf8);

struct WriteResult {
  bool ok = false;
  std::string errorMessage;
};

/// Writes `pointsXyz` (+ `colorsRgb`/`intensity` when non-empty) to a PTX file at `pathUtf8`, as
/// ONE scan block with an **identity** 4x4 transform — GoSurvey's in-memory cloud is already in
/// world/local coordinates with no "scanner setup" concept, so there is nothing non-identity to
/// write. The block's grid is a degenerate single ROW of the whole point set (`ncols == N`,
/// `nrows == 1`), which is what makes a write-then-reread round trip work even though GoSurvey
/// does not model scan setups internally.
///
/// Line shape, in ONE consistent shape for the whole file (PTX has no bare 3-token XYZ shape):
/// 4-token (X Y Z Intensity) when `colorsRgb` is empty, otherwise 7-token (X Y Z Intensity R G B).
/// A channel the source cloud does not actually carry is filled with a documented default
/// (intensity 1.0, RGB 255/255/255) rather than omitted, since a PTX scan body cannot mix line
/// shapes. Fixed-precision formatting: XYZ `%.6f`, intensity `%.4f`, RGB as clamped/rounded 0-255
/// integers — same precision choices as `WritePtsFile`.
///
/// Fails cleanly (`ok == false` + `errorMessage`, no partial file left behind) on an empty point
/// set or a disk-write failure.
[[nodiscard]] WriteResult WritePtxFile(const std::string &pathUtf8,
                                        const std::vector<double> &pointsXyz,
                                        const std::vector<float> &colorsRgb,
                                        const std::vector<float> &intensity);

}  // namespace pointcloud_ptx
