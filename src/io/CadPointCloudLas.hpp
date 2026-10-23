#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// LAS (ASPRS binary point-cloud format, versions 1.2 and 1.4) reader/writer (REQ-172,
/// `spec/file-format-specs.md` §2/§3.2/§5/§6). In-tree, no LASlib dependency — the Public Header
/// Block and point-data-record layouts for formats 0-10 are decoded directly from the published
/// ASPRS byte layout (D-2026-10-15-b extended this from the original 0-3 scope to match
/// `CadPointCloudLaz.hpp`'s D-2026-10-15-a).
///
/// Delivered third of REQ-172's four remaining formats (E57 -> PTS -> PTX -> LAS -> LAZ) per
/// `spec/file-format-specs.md` §6 (issue #825 increment 3). LAZ (compressed LAS via LASzip) is a
/// separate follow-up; this reader/writer never reads or writes compressed point data.
///
/// **File shape** (Public Header Block, fixed byte offsets per the ASPRS spec):
///   - bytes 0-3:    file signature, must be "LASF".
///   - bytes 24-25:  version major/minor — this reader accepts 1.2, 1.3, and 1.4 (header-size and
///                   point-count fields are read per the version actually present, not assumed).
///   - bytes 94-95:  header size (used only to sanity-check the file, not to locate point data).
///   - bytes 96-99:  offset (bytes) from the start of the file to the first point record.
///   - bytes 100-103: number of Variable Length Records (skipped, not parsed).
///   - byte 104:     point data record format — this reader supports 0-10 (D-2026-10-15-b). Any
///                   other value is refused with a message naming the unsupported format number.
///   - bytes 105-106: point data record length in bytes (the per-record stride; may be larger
///                   than the format's minimum size — extra trailing bytes per record are
///                   skipped).
///   - bytes 107-110: legacy (32-bit) number of point records — authoritative for versions 1.2
///                   and 1.3, and for 1.4 whenever the 64-bit extended count (below) is 0.
///   - bytes 131-178: 3 scale factors (X,Y,Z, each a double) then 3 offsets (X,Y,Z, each a
///                   double) — every point's raw int32 X/Y/Z is decoded as
///                   `real = raw * scale + offset`.
///   - bytes 179-226: min/max bounding box (6 doubles) — ignored on import, written correctly on
///                   export.
///   - bytes 247-254 (LAS 1.4 only, present when header size >= 375): 64-bit extended number of
///                   point records. Used instead of the legacy 32-bit count when non-zero.
///
/// **Point Data Records** (one per point, `point data record length` bytes each, starting at the
/// header's declared offset): int32 X, int32 Y, int32 Z (decoded via scale+offset above), then
/// uint16 Intensity at byte offset 12 — true for every one of formats 0-10, since formats 0-5's
/// "legacy" 20-byte core and formats 6-10's "extended" 30-byte core both put X/Y/Z/Intensity
/// first, in the same order, at the same offsets; only the colour offset and overall record
/// length differ by format (`ColorOffsetForFormat`/`MinRecordSizeForFormat` in the .cpp). GPS
/// time, extended classification, NIR, and wave packets are all present in various formats'
/// records but ignored (not needed for REQ-171's cloud schema). Colour (RGB, each a uint16) is
/// present for formats 2/3/5/7/8/10 — offset 20 for format 2 (no GPS time ahead of it), offset 28
/// for 3/5 (after an 8-byte GPS time), offset 30 for 7/8/10 (right after the fixed 30-byte
/// extended core) — and absent for 0/1/4/6/9.
///
/// **Colour/intensity normalization convention** (LAS stores both as 16-bit channels): this
/// reader/writer always divides/multiplies by 65535.0, regardless of whether a real-world file's
/// actual values look 8-bit-scaled (<=255) or genuine 16-bit (up to 65535) — a single fixed
/// convention, applied consistently by both the reader and the writer, is what keeps a
/// write-then-read round trip exact; it is not an attempt to detect or correct a source file's own
/// scaling.
///
/// A file whose signature is not "LASF", whose point data record format is outside 0-10, or whose
/// header declares more point records than the file actually holds (truncated), is a malformed
/// file (REQ-001): refused with a specific message, no partial cloud.
namespace pointcloud_las {

struct ReadResult {
  bool ok = false;
  /// Human-readable reason when `!ok` — a bad signature, an unsupported point format, a
  /// truncated file, or a missing path. Never populated on success.
  std::string errorMessage;

  /// Interleaved x,y,z (architecture §11.8), already decoded via the file's own scale+offset —
  /// world-origin rebasing (REQ-101) is the caller's job, same as every other importer.
  std::vector<double> pointsXyz;
  /// Per-point RGB, 0..1 (divided by 65535), parallel to `pointsXyz`. Empty when the file's point
  /// data record format carries no colour (formats 0/1).
  std::vector<float> colorsRgb;
  /// Per-point intensity, 0..1 (divided by 65535), parallel to `pointsXyz`. LAS point records
  /// 0-3 always carry an intensity field, so this is empty only when the file turned out to
  /// contain zero points.
  std::vector<float> intensity;

  [[nodiscard]] std::int64_t pointCount() const {
    return static_cast<std::int64_t>(pointsXyz.size() / 3);
  }
};

/// One bounded-size chunk of points, handed to \ref StreamLasFile's callback. Never holds more
/// than `chunkPointBudget` points — same out-of-core streaming contract as
/// `CadPointCloudE57.hpp`'s / `CadPointCloudPts.hpp`'s `PointChunk`.
struct PointChunk {
  std::vector<double> pointsXyz;
  std::vector<float> colorsRgb;    ///< empty when the file's point format carries no colour.
  std::vector<float> intensity;
};

/// Streams the LAS at `pathUtf8` through `onChunk`, `chunkPointBudget` points at a time, without
/// ever holding the whole file's point records in memory at once (the header itself is tiny and
/// read in one shot). Returns `false` with `errorMessage` set on a missing/truncated/malformed
/// file or an unsupported point data record format (REQ-001) — `onChunk` is never called for a
/// chunk built from a truncated/malformed read; a caller building an out-of-core cache from the
/// chunks (`pointcloudcache.hpp`) must still discard any partial cache on a `false` return.
///
/// `onChunk` returning `false` aborts the stream early (used by a cache builder that hit its own
/// error, e.g. a disk write failure) — `StreamLasFile` then also returns `false`.
[[nodiscard]] bool StreamLasFile(const std::string &pathUtf8, std::int64_t chunkPointBudget,
                                  const std::function<bool(const PointChunk &)> &onChunk,
                                  std::string *errorMessage);

/// Cheap point-count estimate for a progress bar's total: reads just the header's point-count
/// field (the 1.4 extended 64-bit count when present and non-zero, else the legacy 32-bit count)
/// — metadata only, no point data read. Returns 0 on any failure (missing file, bad signature,
/// unreadable header); callers should treat 0 as "unknown," not "empty," since the real read
/// (`StreamLasFile`) is what actually validates the file.
[[nodiscard]] std::int64_t QuickPointCountEstimate(const std::string &pathUtf8);

/// Reads the whole LAS at `pathUtf8` into one flattened point set — a convenience, non-streaming
/// reader for small files and tests. **Never throws.** A missing file, an empty path, a bad
/// signature, an unsupported point data record format, or a truncated LAS comes back as
/// `ok == false` with `errorMessage` naming what failed — `pointsXyz` is left empty, so a caller
/// cannot accidentally commit a partial cloud (REQ-001, REQ-172 acceptance: "a truncated/malformed
/// ... LAS ... is refused ... no partial cloud").
[[nodiscard]] ReadResult ReadLasFile(const std::string &pathUtf8);

struct WriteResult {
  bool ok = false;
  std::string errorMessage;
};

/// Writes `pointsXyz` (+ `colorsRgb`/`intensity` when non-empty) to a LAS 1.2 file at `pathUtf8`.
/// Point data record format is chosen by presence of colour alone: format 0 (no colour) when
/// `colorsRgb` is empty, otherwise format 2 (adds RGB) — never formats 1/3 (GPS time), since
/// GoSurvey's in-memory cloud carries no per-point timestamp. `intensity` is written as 0 when the
/// source cloud does not carry it.
///
/// Scale/offset are derived from the actual point extents: `offset` is each axis's midpoint and
/// `scale` is the smaller of a fixed 0.0001 (sub-millimeter) precision target and whatever a
/// larger axis span requires to keep every raw X/Y/Z inside the int32 range — the same "finest
/// precision the range allows" choice real LAS writers make. Min/max bounding box header fields
/// are written from the actual data (REQ-172: "written correctly on export").
///
/// Fails cleanly (`ok == false` + `errorMessage`, no partial file left behind) on an empty point
/// set, a point count that does not fit this writer's 32-bit legacy count field (the LAS 1.2
/// header it emits), a coordinate whose encoded int32 would overflow even after the scale/offset
/// choice below, or a disk-write failure.
[[nodiscard]] WriteResult WriteLasFile(const std::string &pathUtf8,
                                        const std::vector<double> &pointsXyz,
                                        const std::vector<float> &colorsRgb,
                                        const std::vector<float> &intensity);

}  // namespace pointcloud_las
