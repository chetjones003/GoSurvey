#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// LAZ (LASzip-compressed LAS) point-cloud reader/writer (REQ-172, `spec/file-format-specs.md`
/// §2/§3.2/§5/§6). A `.laz` file is a LAS file whose Public Header Block and Variable Length
/// Records are byte-for-byte the same as a plain LAS, plus one extra VLR (user ID
/// `"laszip encoded"`) describing the LASzip compression parameters, with the point-data section
/// itself replaced by LASzip's compressed byte stream in place of the raw, fixed-stride point
/// records a plain LAS carries. All of that header/VLR/compressed-stream handling is delegated to
/// vendored LASzip (`third_party/laszip/VENDORED.md`) via its public C API
/// (`laszip_create`/`laszip_open_reader`/`laszip_read_point`/... — `third_party/laszip/dll/laszip_api.h`)
/// — this file never parses the Public Header Block or the LASzip VLR itself; LASzip's
/// `laszip_header_struct` (obtained via `laszip_get_header_pointer`) is the one and only source of
/// scale/offset/point-count/point-format for every read, and `laszip_set_header` +
/// `laszip_open_writer(..., compress=true)` is what emits them (and the LASzip VLR) on write.
///
/// Delivered fourth and last of REQ-172's four remaining formats (E57 -> PTS -> PTX -> LAS -> LAZ)
/// per `spec/file-format-specs.md` §6 (issue #825 increment 4).
///
/// **Point format / colour / intensity convention**, so a LAS and a LAZ of the same points import
/// equal counts and XYZ when the LAS source is formats 0-3 (REQ-172 acceptance):
///   - Point data record formats 0-10 are all read (D-2026-10-15-a): LASzip's `laszip_point_struct`
///     already carries every field any of them needs (GPS time, `rgb[4]`, extended
///     classification, wave packets), so no format-specific decode branch is required. Colour is
///     present for formats 2/3/5/7/8/10, absent for 0/1/4/6/9. GPS time, NIR (formats 8/10), and
///     wave packets (4/5/9/10) are decoded by LASzip into the point struct but discarded —
///     GoSurvey's in-memory cloud (REQ-171) has no field for them, the same treatment formats
///     1/3's GPS time always got. Only format values LASzip itself cannot represent (>10) are
///     refused, naming the format number.
///   - Colour and intensity are both 16-bit channels in LASzip's `laszip_point_struct`
///     (`rgb[0..2]`, `intensity`); this reader/writer always divides/multiplies by 65535.0,
///     exactly like `CadPointCloudLas.hpp` — the same single fixed convention that keeps a
///     write-then-read round trip exact, and keeps a LAS and a LAZ of the same source agreeing.
///   - The writer always emits point data record format 0 (no colour) or 2 (adds RGB) at LAS
///     version 1.2, chosen by presence of colour alone — identical to `WriteLasFile`'s choice —
///     and derives scale/offset from the actual point extents by the identical formula (offset =
///     each axis's midpoint; scale = the finer of a fixed 0.0001 precision target and whatever a
///     larger axis span requires to keep every raw X/Y/Z inside int32), so a LAS and a LAZ written
///     from the same in-memory point set decode to the same raw integers.
///
/// A file whose signature is not "LASF" (LASzip reports this as an open failure), whose point
/// data record format is outside what LASzip itself represents (>10), or whose compressed stream
/// is truncated/corrupt (LASzip reports a read failure mid-stream), is refused with a specific
/// message — no partial cloud (REQ-001).
namespace pointcloud_laz {

struct ReadResult {
  bool ok = false;
  /// Human-readable reason when `!ok` — a bad signature, an unsupported point format, a
  /// truncated/corrupt file, or a missing path. Never populated on success.
  std::string errorMessage;

  /// Interleaved x,y,z (architecture §11.8), already decoded via the file's own scale+offset —
  /// world-origin rebasing (REQ-101) is the caller's job, same as every other importer.
  std::vector<double> pointsXyz;
  /// Per-point RGB, 0..1 (divided by 65535), parallel to `pointsXyz`. Empty when the file's point
  /// data record format carries no colour.
  std::vector<float> colorsRgb;
  /// Per-point intensity, 0..1 (divided by 65535), parallel to `pointsXyz`.
  std::vector<float> intensity;

  [[nodiscard]] std::int64_t pointCount() const {
    return static_cast<std::int64_t>(pointsXyz.size() / 3);
  }
};

/// One bounded-size chunk of points, handed to \ref StreamLazFile's callback. Never holds more
/// than `chunkPointBudget` points — same out-of-core streaming contract as
/// `CadPointCloudLas.hpp`'s/`CadPointCloudE57.hpp`'s `PointChunk`.
struct PointChunk {
  std::vector<double> pointsXyz;
  std::vector<float> colorsRgb;    ///< empty when the file's point format carries no colour.
  std::vector<float> intensity;
};

/// Streams the LAZ at `pathUtf8` through `onChunk`, `chunkPointBudget` points at a time, decoding
/// via LASzip point-by-point but never holding the whole file's points in memory at once. Returns
/// `false` with `errorMessage` set on a missing/malformed file, an unsupported point data record
/// format, or a corrupt/truncated compressed stream (REQ-001) — `onChunk` is never called for a
/// chunk built from a failed read; a caller building an out-of-core cache from the chunks
/// (`pointcloudcache.hpp`) must still discard any partial cache on a `false` return.
///
/// `onChunk` returning `false` aborts the stream early (used by a cache builder that hit its own
/// error, e.g. a disk write failure) — `StreamLazFile` then also returns `false`.
[[nodiscard]] bool StreamLazFile(const std::string &pathUtf8, std::int64_t chunkPointBudget,
                                  const std::function<bool(const PointChunk &)> &onChunk,
                                  std::string *errorMessage);

/// Cheap point-count estimate for a progress bar's total: opens the file and reads just LASzip's
/// header structure (the 1.4 extended 64-bit count when present and non-zero, else the legacy
/// 32-bit count) — no point data decoded. Returns 0 on any failure (missing file, bad signature,
/// unreadable header); callers should treat 0 as "unknown," not "empty," since the real read
/// (`StreamLazFile`) is what actually validates the file.
[[nodiscard]] std::int64_t QuickPointCountEstimate(const std::string &pathUtf8);

/// Reads the whole LAZ at `pathUtf8` into one flattened point set — a convenience, non-streaming
/// reader for small files and tests. **Never throws.** A missing file, an empty path, a bad
/// signature, an unsupported point data record format, or a corrupt/truncated compressed stream
/// comes back as `ok == false` with `errorMessage` naming what failed — `pointsXyz` is left empty,
/// so a caller cannot accidentally commit a partial cloud (REQ-001, REQ-172 acceptance: "a
/// truncated/malformed ... LAZ ... is refused ... no partial cloud").
[[nodiscard]] ReadResult ReadLazFile(const std::string &pathUtf8);

struct WriteResult {
  bool ok = false;
  std::string errorMessage;
};

/// Writes `pointsXyz` (+ `colorsRgb`/`intensity` when non-empty) to a LASzip-compressed LAS 1.2
/// file at `pathUtf8`, via `laszip_open_writer(..., compress=true)`. Point data record format,
/// scale/offset derivation, and intensity/colour defaulting are identical to `WriteLasFile`'s
/// (see this header's top note) — a LAS and a LAZ written from the same in-memory point set
/// decode to the same raw int32 X/Y/Z, which is what makes the LAS/LAZ cross-check in
/// `tests/CadPointCloudLazTests.cpp` exact rather than approximate.
///
/// Fails cleanly (`ok == false` + `errorMessage`, no partial file left behind) on an empty point
/// set, a point count that does not fit this writer's 32-bit legacy count field, a coordinate
/// whose encoded int32 would overflow even after the scale/offset choice below, or a LASzip/disk
/// write failure.
[[nodiscard]] WriteResult WriteLazFile(const std::string &pathUtf8,
                                        const std::vector<double> &pointsXyz,
                                        const std::vector<float> &colorsRgb,
                                        const std::vector<float> &intensity);

}  // namespace pointcloud_laz
