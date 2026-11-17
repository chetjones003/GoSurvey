#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "GoSurvey/PointCloud/StreamedChunk.hpp"
#include "GoSurvey/PointCloud/Octree.hpp"

/// `.gscloud` out-of-core point-cloud cache (REQ-171/172, ADR-060).
///
/// A sidecar file next to the source scan holding a pre-built octree (`pointcloud::Octree`) plus,
/// per leaf, its point data on disk — built once from a source scan via a purely
/// **out-of-core** construction: the source is streamed once into a root temp file (bounded
/// memory), then recursively bucket-split into child
/// temp files entirely by local disk IO, never holding more than one chunk of points in memory at
/// once. This is what makes importing a multi-GB scan not exhaust RAM (the bug ADR-060 exists to
/// fix — see the point-cloud epic's TASK-270 log).
namespace pointcloudcache {

/// Format-neutral out-of-core cache construction. The reader streams bounded chunks synchronously;
/// each view is valid only during its callback. The caller supplies the established no-points error.
[[nodiscard]] bool BuildFromStreamedSource(
    const std::string &sourcePath, const std::string &cachePath,
    std::vector<std::string> &progressLog, std::string *errorMessage,
    std::atomic<std::int64_t> *pointsStreamedOut,
    std::atomic<std::int64_t> *pointsFinalizedOut,
    const std::atomic<bool> *cancelRequested, const pointcloud::StreamedPointReader &reader,
    const char *zeroPointsMessage);

/// Header stamp (ADR-060 (b)): identifies the source file this cache was built from, so a stale or
/// mismatched cache is detected and rebuilt rather than silently trusted.
struct SourceStamp {
  std::uint64_t sizeBytes = 0;
  /// Opaque `std::filesystem::file_time_type` tick count — never a real calendar time (that would
  /// need `clock_cast`, whose MSVC support is uneven). Only ever compared to another tick count
  /// read the same way, on the same machine, which is all a staleness check needs.
  std::int64_t mtimeTicks = 0;
};

/// The small, always-in-memory part of an open cache: the octree structure and the flags/counts
/// needed to interpret leaf point data. Point data itself is read on demand via \ref ReadLeafPoints
/// — this struct alone is typically a few thousand nodes, not gigabytes.
///
/// **Field reuse note**: for an `Octree` obtained this way, each leaf `Node::pointIndexBegin`
/// holds a **byte offset into the cache file's point-data section** — not an index into
/// `Octree::pointIndices` (which `pointcloudcache` leaves empty; that indexing scheme is
/// `pointcloudoctree.hpp`'s in-memory-build convention, meaningless for a disk-backed tree). This
/// is a pragmatic reuse of one field for two purposes across the two ways an `Octree` gets built,
/// documented here rather than forking the type — see TASK-270 for the tradeoff.
struct OpenCache {
  std::string cachePath;
  pointcloud::Octree octree;
  bool hasColor = false;
  bool hasIntensity = false;
  std::int64_t totalPointCount = 0;
  SourceStamp stamp;
};

/// Reads a cache's header + octree structure (not point data) from `cachePath`. `ok` is false and
/// `errorMessage` is set if the file is missing, truncated, or its magic/version does not match —
/// a caller should treat this the same as ADR-060 (d)'s "missing file is a logged rebuild, never a
/// load failure."
struct OpenResult {
  bool ok = false;
  std::string errorMessage;
  OpenCache cache;
};
[[nodiscard]] OpenResult Open(const std::string &cachePath);

/// Reads leaf `leafNodeIndex`'s point data from `cache.cachePath` on disk into `outXyz` (+
/// `outColorsRgb`/`outIntensity` if the cache carries those channels). Returns false (leaving the
/// outputs empty) if `leafNodeIndex` does not name a leaf node or the read fails.
[[nodiscard]] bool ReadLeafPoints(const OpenCache &cache, int leafNodeIndex,
                                   std::vector<double> &outXyz, std::vector<float> &outColorsRgb,
                                   std::vector<float> &outIntensity);

/// Compares `cachePath`'s stored \ref SourceStamp against `sourcePath`'s current size/mtime
/// (ADR-060 (b)). `true` only when the cache file exists, parses, and matches exactly — any other
/// outcome (missing cache, parse failure, mismatch) is `false`, meaning "rebuild."
[[nodiscard]] bool MatchesSource(const std::string &cachePath, const std::string &sourcePath);

}  // namespace pointcloudcache
