#include "util/pointcloudcache.hpp"

#include "io/CadPointCloudE57.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>

namespace pointcloudcache {

namespace {

namespace fs = std::filesystem;

constexpr char kMagic[8] = {'G', 'S', 'C', 'L', 'O', 'U', 'D', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::int64_t kLeafPointThreshold = 50'000;  // matches pointcloudoctree.hpp's default
constexpr int kMaxDepth = 16;
constexpr std::int64_t kStreamChunkPoints = 1'000'000;

// A default file stream buffer is a few KB, so a tight per-record read()/write() loop over
// hundreds of millions of points pays an OS transition on nearly every record — the actual cause
// of a build that looks "frozen" rather than merely slow (each of the ~log8(N/leafSize) recursive
// split levels re-reads and re-writes the whole dataset once). Giving every stream a much larger
// buffer turns that into one OS transition per ~1 MB instead of one per ~30 bytes.
constexpr std::size_t kBigBufferBytes = 1 << 20;  // 1 MiB

/// `bufStorage` must outlive `stream` and must not be touched while `stream` is open. Must be
/// called BEFORE `open()` — `pubsetbuf` has no effect on an already-open stream.
template <typename Stream>
void UseBigBuffer(Stream &stream, std::vector<char> &bufStorage) {
  bufStorage.resize(kBigBufferBytes);
  stream.rdbuf()->pubsetbuf(bufStorage.data(), static_cast<std::streamsize>(bufStorage.size()));
}

// Fixed-layout sizes the header/node-table format is built from (see the format note in
// pointcloudcache.hpp). Kept as named constants, computed once, rather than re-derived at each
// read/write call site, so Open() and the writer below cannot silently drift apart.
constexpr std::size_t kFixedHeaderSize =
    8 /*magic*/ + 4 /*version*/ + 8 /*srcSize*/ + 8 /*srcMtime*/ + 1 /*hasColor*/ +
    1 /*hasIntensity*/ + 6 /*pad*/ + 4 /*nodeCount*/ + 8 /*totalPointCount*/;
constexpr std::size_t kNodeRecordSize = 6 * 8 /*bounds*/ + 8 * 4 /*children*/ + 8 /*leafOffset*/ +
                                        8 /*leafCount*/;

std::int64_t PointDataSectionStart(std::uint32_t nodeCount) {
  return static_cast<std::int64_t>(kFixedHeaderSize) +
         static_cast<std::int64_t>(nodeCount) * static_cast<std::int64_t>(kNodeRecordSize);
}

std::size_t RecordSize(bool hasColor, bool hasIntensity) {
  std::size_t sz = 3 * sizeof(double);
  if (hasColor) sz += 3 * sizeof(float);
  if (hasIntensity) sz += sizeof(float);
  return sz;
}

template <typename T>
void WritePod(std::ofstream &out, const T &v) {
  out.write(reinterpret_cast<const char *>(&v), sizeof(v));
}

template <typename T>
void ReadPod(std::ifstream &in, T &v) {
  in.read(reinterpret_cast<char *>(&v), sizeof(v));
}

/// Which of a bounds' 8 octants (x,y,z) falls in — identical convention to
/// `pointcloudoctree.cpp`'s `OctantOf` (duplicated rather than shared: the two are pure, tiny, and
/// live in different translation units for different reasons — one is in-memory, one is a byte
/// format on disk — so a shared header for an 8-line function would be the wrong-sized abstraction).
int OctantOf(const pointcloud::Bounds &b, double x, double y, double z) {
  int oct = 0;
  if (x >= b.centerX()) oct |= 1;
  if (y >= b.centerY()) oct |= 2;
  if (z >= b.centerZ()) oct |= 4;
  return oct;
}

pointcloud::Bounds ChildBounds(const pointcloud::Bounds &parent, int octant) {
  pointcloud::Bounds c = parent;
  const double cx = parent.centerX(), cy = parent.centerY(), cz = parent.centerZ();
  if (octant & 1) c.minX = cx; else c.maxX = cx;
  if (octant & 2) c.minY = cy; else c.maxY = cy;
  if (octant & 4) c.minZ = cz; else c.maxZ = cz;
  return c;
}

/// One node while the cache is being built out-of-core: either a leaf (`leafFile` names a temp
/// file holding exactly `leafPointCount` fixed-size records) or an interior node (`children` are
/// indices into the same vector this struct is stored in).
struct BuildNode {
  pointcloud::Bounds bounds;
  std::array<int, 8> children{-1, -1, -1, -1, -1, -1, -1, -1};
  std::string leafFile;
  std::int64_t leafPointCount = 0;
  /// Same fix, same reason, as `pointcloud::Node::isLeaf()` — see that header's comment. The two
  /// are not shared (one is in-memory, one is a disk-build intermediate) but the bug and its fix
  /// are identical, so keep them in sync if this ever changes again.
  [[nodiscard]] bool isLeaf() const {
    for (int c : children)
      if (c >= 0) return false;
    return true;
  }
};

/// Recursively splits (or keeps as a leaf) the `pointCount`-record temp file at `filePath`, under
/// `bounds`, entirely via local disk IO — no E57/network access below the root call. Consumes
/// (deletes) `filePath` once it has been split into children; a file that stays a leaf is left in
/// place and referenced by the returned node, to be consumed later when the leaf data is copied
/// into the final cache file.
///
/// On failure, `*failed` is set and every temp file this call created (including ones already
/// recursed into) is removed before returning, so a build error never leaves the temp directory
/// half-populated for the caller's cleanup pass to stumble over mid-write.
int SplitOrKeep(const std::string &filePath, std::int64_t pointCount, const pointcloud::Bounds &bounds,
                int depth, std::size_t recordSize, const fs::path &tempDir, int &tempFileCounter,
                std::vector<BuildNode> &nodes, std::string *errorMessage, bool *failed,
                std::atomic<std::int64_t> *pointsFinalizedOut, const std::atomic<bool> *cancelRequested,
                bool *cancelled) {
  if (*failed) return -1;
  if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
    *failed = true;
    *cancelled = true;
    std::error_code rmEc;
    fs::remove(filePath, rmEc);
    return -1;
  }

  if (pointCount <= kLeafPointThreshold || depth >= kMaxDepth) {
    BuildNode n;
    n.bounds = bounds;
    n.leafFile = filePath;
    n.leafPointCount = pointCount;
    nodes.push_back(std::move(n));
    if (pointsFinalizedOut) pointsFinalizedOut->fetch_add(pointCount, std::memory_order_relaxed);
    return static_cast<int>(nodes.size() - 1);
  }

  std::vector<char> inBuf;
  std::ifstream in;
  UseBigBuffer(in, inBuf);
  in.open(filePath, std::ios::binary);
  if (!in) {
    *failed = true;
    if (errorMessage) *errorMessage = "point-cloud cache build: cannot reopen " + filePath;
    return -1;
  }

  std::array<std::string, 8> childPaths;
  std::array<std::unique_ptr<std::ofstream>, 8> childStreams;
  std::array<std::vector<char>, 8> childBufs;  // must outlive each stream (UseBigBuffer's contract)
  std::array<std::int64_t, 8> childCounts{};
  for (int oct = 0; oct < 8; ++oct)
    childPaths[oct] = (tempDir / ("n" + std::to_string(tempFileCounter++) + ".tmp")).string();

  // Read many records into one buffer per iteration rather than one record at a time — the
  // dominant cost at this recursion level is exactly this read/route/write pass over every point,
  // so this is the loop \ref kBigBufferBytes exists to keep off the OS-transition-per-record path.
  const std::int64_t recordsPerBlock =
      std::max<std::int64_t>(1, static_cast<std::int64_t>(kBigBufferBytes) /
                                     static_cast<std::int64_t>(recordSize));
  std::vector<char> block(static_cast<std::size_t>(recordsPerBlock) * recordSize);

  std::int64_t remaining = pointCount;
  while (remaining > 0 && !*failed) {
    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
      *failed = true;
      *cancelled = true;
      break;
    }
    const std::int64_t thisBlock = std::min(remaining, recordsPerBlock);
    const std::streamsize bytes = static_cast<std::streamsize>(thisBlock) * static_cast<std::streamsize>(recordSize);
    in.read(block.data(), bytes);
    if (!in) {
      *failed = true;
      if (errorMessage) *errorMessage = "point-cloud cache build: truncated temp file " + filePath;
      break;
    }
    for (std::int64_t r = 0; r < thisBlock; ++r) {
      const char *rec = block.data() + static_cast<std::size_t>(r) * recordSize;
      double x, y, z;
      std::memcpy(&x, rec, sizeof(double));
      std::memcpy(&y, rec + sizeof(double), sizeof(double));
      std::memcpy(&z, rec + 2 * sizeof(double), sizeof(double));
      const int oct = OctantOf(bounds, x, y, z);
      if (!childStreams[oct]) {
        childStreams[oct] = std::make_unique<std::ofstream>();
        UseBigBuffer(*childStreams[oct], childBufs[oct]);
        childStreams[oct]->open(childPaths[oct], std::ios::binary);
        if (!*childStreams[oct]) {
          *failed = true;
          if (errorMessage) *errorMessage = "point-cloud cache build: cannot create a temp file";
          break;
        }
      }
      childStreams[oct]->write(rec, static_cast<std::streamsize>(recordSize));
      ++childCounts[oct];
    }
    remaining -= thisBlock;
  }
  in.close();
  for (auto &s : childStreams)
    if (s) s->close();
  std::error_code ec;
  fs::remove(filePath, ec);

  if (*failed) {
    for (int oct = 0; oct < 8; ++oct)
      if (childCounts[oct] > 0) fs::remove(childPaths[oct], ec);
    return -1;
  }

  BuildNode n;
  n.bounds = bounds;
  for (int oct = 0; oct < 8; ++oct) {
    if (childCounts[oct] == 0) continue;
    n.children[oct] = SplitOrKeep(childPaths[oct], childCounts[oct], ChildBounds(bounds, oct),
                                  depth + 1, recordSize, tempDir, tempFileCounter, nodes, errorMessage,
                                  failed, pointsFinalizedOut, cancelRequested, cancelled);
  }
  nodes.push_back(std::move(n));
  return static_cast<int>(nodes.size() - 1);
}

}  // namespace

bool BuildFromE57(const std::string &sourcePath, const std::string &cachePath,
                   std::vector<std::string> &progressLog, std::string *errorMessage,
                   std::atomic<std::int64_t> *pointsStreamedOut,
                   std::atomic<std::int64_t> *pointsFinalizedOut,
                   const std::atomic<bool> *cancelRequested) {
  const auto fail = [&](const std::string &msg) {
    if (errorMessage) *errorMessage = msg;
    return false;
  };
  if (cancelRequested && cancelRequested->load(std::memory_order_relaxed))
    return fail("Import cancelled.");

  std::error_code ec;
  std::mt19937_64 rng(std::random_device{}());
  const fs::path tempDir =
      fs::temp_directory_path() / ("gosurvey_pcbuild_" + std::to_string(rng()));
  fs::create_directories(tempDir, ec);
  if (ec) return fail("point-cloud cache build: cannot create a temp directory");
  const auto cleanupTemp = [&] {
    std::error_code ec2;
    fs::remove_all(tempDir, ec2);
  };

  const std::string rootFile = (tempDir / "root.tmp").string();
  std::vector<char> rootBuf;
  std::ofstream rootOut;
  UseBigBuffer(rootOut, rootBuf);
  rootOut.open(rootFile, std::ios::binary);
  if (!rootOut) {
    cleanupTemp();
    return fail("point-cloud cache build: cannot create the root temp file");
  }

  pointcloud::Bounds bounds;
  bool boundsInit = false;
  std::int64_t totalCount = 0;
  bool hasColor = false;
  bool hasIntensity = false;
  bool decided = false;
  bool writeFailed = false;
  std::string streamErr;

  // One streaming pass over the source (bounded memory — see CadPointCloudE57.hpp's own note)
  // writes every point straight to disk as a fixed-size record, while also folding bounds/counts.
  // hasColor/hasIntensity are decided from the FIRST chunk that carries any points and held fixed
  // for the whole file — an E57 with genuinely inconsistent per-scan schemas (rare; ADR-042 (a)
  // already treats multi-setup files as a smaller-first simplification) would have a later scan's
  // channel silently zero-filled rather than sized differently mid-stream, which fixed-size
  // records require anyway.
  const bool streamOk = pointcloud_e57::StreamE57File(
      sourcePath, kStreamChunkPoints,
      [&](const pointcloud_e57::PointChunk &chunk) -> bool {
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) return false;
        const std::int64_t n = static_cast<std::int64_t>(chunk.pointsXyz.size() / 3);
        if (n == 0) return true;
        if (!decided) {
          hasColor = !chunk.colorsRgb.empty();
          hasIntensity = !chunk.intensity.empty();
          decided = true;
        }
        for (std::int64_t i = 0; i < n; ++i) {
          const double x = chunk.pointsXyz[static_cast<std::size_t>(i) * 3 + 0];
          const double y = chunk.pointsXyz[static_cast<std::size_t>(i) * 3 + 1];
          const double z = chunk.pointsXyz[static_cast<std::size_t>(i) * 3 + 2];
          if (!boundsInit) {
            bounds.minX = bounds.maxX = x;
            bounds.minY = bounds.maxY = y;
            bounds.minZ = bounds.maxZ = z;
            boundsInit = true;
          } else {
            bounds.minX = std::min(bounds.minX, x);
            bounds.maxX = std::max(bounds.maxX, x);
            bounds.minY = std::min(bounds.minY, y);
            bounds.maxY = std::max(bounds.maxY, y);
            bounds.minZ = std::min(bounds.minZ, z);
            bounds.maxZ = std::max(bounds.maxZ, z);
          }
          // Assembled once and written with a SINGLE call — up to three separate write() calls
          // per point (one each for xyz/rgb/intensity) was needless call overhead on top of the
          // OS-transition cost \ref kBigBufferBytes's stream buffering already fixes.
          char rec[3 * sizeof(double) + 3 * sizeof(float) + sizeof(float)];
          std::size_t off = 0;
          double xyz[3] = {x, y, z};
          std::memcpy(rec + off, xyz, sizeof(xyz));
          off += sizeof(xyz);
          if (hasColor) {
            float rgb[3] = {0.f, 0.f, 0.f};
            if (!chunk.colorsRgb.empty()) {
              rgb[0] = chunk.colorsRgb[static_cast<std::size_t>(i) * 3 + 0];
              rgb[1] = chunk.colorsRgb[static_cast<std::size_t>(i) * 3 + 1];
              rgb[2] = chunk.colorsRgb[static_cast<std::size_t>(i) * 3 + 2];
            }
            std::memcpy(rec + off, rgb, sizeof(rgb));
            off += sizeof(rgb);
          }
          if (hasIntensity) {
            const float inten = chunk.intensity.empty() ? 0.f : chunk.intensity[static_cast<std::size_t>(i)];
            std::memcpy(rec + off, &inten, sizeof(inten));
            off += sizeof(inten);
          }
          rootOut.write(rec, static_cast<std::streamsize>(off));
        }
        totalCount += n;
        if (pointsStreamedOut) pointsStreamedOut->store(totalCount, std::memory_order_relaxed);
        if (!rootOut) {
          writeFailed = true;
          return false;
        }
        return true;
      },
      &streamErr);
  rootOut.close();

  if (!streamOk) {
    cleanupTemp();
    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed))
      return fail("Import cancelled.");
    return fail(writeFailed ? "point-cloud cache build: disk write failed" : streamErr);
  }
  if (totalCount == 0) {
    cleanupTemp();
    return fail("E57 import: contains zero valid points");
  }

  progressLog.push_back("Indexing " + std::to_string(totalCount) + " point(s)...");

  const std::size_t recordSize = RecordSize(hasColor, hasIntensity);
  std::vector<BuildNode> nodes;
  bool splitFailed = false;
  bool splitCancelled = false;
  int tempFileCounter = 0;
  const int rootIdx =
      SplitOrKeep(rootFile, totalCount, bounds, 0, recordSize, tempDir, tempFileCounter, nodes,
                 errorMessage, &splitFailed, pointsFinalizedOut, cancelRequested, &splitCancelled);
  if (splitFailed || rootIdx < 0) {
    cleanupTemp();
    if (splitCancelled) return fail("Import cancelled.");
    return false;  // *errorMessage already set by SplitOrKeep
  }

  // pointcloud::Octree (and every consumer of it) assumes nodes[0] is the root; the recursive
  // builder above appends children before their parent, so the root lands last. Swap it into
  // place and fix up the two indices' cross-references — cheaper than re-deriving the whole tree
  // in root-first order.
  if (rootIdx != 0) {
    for (auto &n : nodes)
      for (auto &c : n.children) {
        if (c == 0) c = rootIdx;
        else if (c == rootIdx) c = 0;
      }
    std::swap(nodes[0], nodes[static_cast<std::size_t>(rootIdx)]);
  }

  progressLog.push_back(std::to_string(nodes.size()) + " octree node(s) built.");

  // --- Write the final .gscloud: header, node table, then each leaf's temp-file bytes -----------
  std::error_code statEc;
  const auto srcSize = fs::file_size(sourcePath, statEc);
  const auto srcMtime = fs::last_write_time(sourcePath, statEc);
  if (statEc) {
    cleanupTemp();
    return fail("point-cloud cache build: cannot stat the source file");
  }

  bool ok = true;
  {
    std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
    if (!out) {
      ok = false;
      if (errorMessage) *errorMessage = "point-cloud cache build: cannot create " + cachePath;
    } else {
      out.write(kMagic, sizeof(kMagic));
      WritePod(out, kVersion);
      WritePod(out, static_cast<std::uint64_t>(srcSize));
      WritePod(out, static_cast<std::int64_t>(srcMtime.time_since_epoch().count()));
      WritePod(out, static_cast<std::uint8_t>(hasColor ? 1 : 0));
      WritePod(out, static_cast<std::uint8_t>(hasIntensity ? 1 : 0));
      const std::uint8_t pad[6] = {0, 0, 0, 0, 0, 0};
      out.write(reinterpret_cast<const char *>(pad), sizeof(pad));
      WritePod(out, static_cast<std::uint32_t>(nodes.size()));
      WritePod(out, static_cast<std::int64_t>(totalCount));

      std::vector<std::int64_t> leafByteOffsets(nodes.size(), -1);
      std::int64_t running = 0;
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].isLeaf()) {
          leafByteOffsets[i] = running;
          running += nodes[i].leafPointCount * static_cast<std::int64_t>(recordSize);
        }
      }
      for (std::size_t i = 0; i < nodes.size() && ok; ++i) {
        const BuildNode &n = nodes[i];
        double b[6] = {n.bounds.minX, n.bounds.minY, n.bounds.minZ,
                      n.bounds.maxX, n.bounds.maxY, n.bounds.maxZ};
        out.write(reinterpret_cast<const char *>(b), sizeof(b));
        for (int c : n.children) {
          const std::int32_t cc = c;
          out.write(reinterpret_cast<const char *>(&cc), sizeof(cc));
        }
        const std::int64_t off = leafByteOffsets[i];
        const std::int64_t cnt = n.isLeaf() ? n.leafPointCount : 0;
        out.write(reinterpret_cast<const char *>(&off), sizeof(off));
        out.write(reinterpret_cast<const char *>(&cnt), sizeof(cnt));
        if (!out) ok = false;
      }

      if (ok) {
        std::vector<char> copyBuf(1 << 20);
        for (std::size_t i = 0; i < nodes.size() && ok; ++i) {
          if (!nodes[i].isLeaf()) continue;
          std::ifstream in(nodes[i].leafFile, std::ios::binary);
          if (!in) {
            ok = false;
            if (errorMessage) *errorMessage = "point-cloud cache build: cannot read a temp leaf file";
            break;
          }
          std::int64_t remaining = nodes[i].leafPointCount * static_cast<std::int64_t>(recordSize);
          while (remaining > 0) {
            const std::streamsize toRead = static_cast<std::streamsize>(
                std::min<std::int64_t>(remaining, static_cast<std::int64_t>(copyBuf.size())));
            in.read(copyBuf.data(), toRead);
            if (!in) {
              ok = false;
              if (errorMessage) *errorMessage = "point-cloud cache build: truncated temp leaf file";
              break;
            }
            out.write(copyBuf.data(), toRead);
            remaining -= toRead;
          }
        }
      }
      if (!out) ok = false;
    }
  }
  cleanupTemp();
  if (!ok) {
    std::error_code rmEc;
    fs::remove(cachePath, rmEc);  // never leave a partial cache (REQ-001)
    if (errorMessage && errorMessage->empty()) *errorMessage = "point-cloud cache build failed";
    return false;
  }
  progressLog.push_back("Wrote cache: " + cachePath);
  return true;
}

OpenResult Open(const std::string &cachePath) {
  OpenResult res;
  std::ifstream in(cachePath, std::ios::binary);
  if (!in) {
    res.errorMessage = "cannot open '" + cachePath + "'";
    return res;
  }
  char magic[8];
  in.read(magic, sizeof(magic));
  if (!in || std::memcmp(magic, kMagic, sizeof(magic)) != 0) {
    res.errorMessage = "'" + cachePath + "' is not a .gscloud file";
    return res;
  }
  std::uint32_t version = 0;
  ReadPod(in, version);
  if (version != kVersion) {
    res.errorMessage = "'" + cachePath + "' is a newer/older .gscloud version (" +
                       std::to_string(version) + ")";
    return res;
  }
  std::uint64_t srcSize = 0;
  std::int64_t srcMtime = 0;
  std::uint8_t hasColorByte = 0, hasIntensityByte = 0;
  ReadPod(in, srcSize);
  ReadPod(in, srcMtime);
  ReadPod(in, hasColorByte);
  ReadPod(in, hasIntensityByte);
  std::uint8_t pad[6];
  in.read(reinterpret_cast<char *>(pad), sizeof(pad));
  std::uint32_t nodeCount = 0;
  std::int64_t totalPointCount = 0;
  ReadPod(in, nodeCount);
  ReadPod(in, totalPointCount);
  if (!in) {
    res.errorMessage = "'" + cachePath + "' header is truncated";
    return res;
  }

  pointcloud::Octree tree;
  tree.totalPointCount = totalPointCount;
  tree.nodes.resize(nodeCount);
  for (auto &n : tree.nodes) {
    double b[6];
    in.read(reinterpret_cast<char *>(b), sizeof(b));
    n.bounds = pointcloud::Bounds{b[0], b[1], b[2], b[3], b[4], b[5]};
    for (auto &c : n.children) {
      std::int32_t cc = 0;
      ReadPod(in, cc);
      c = cc;
    }
    std::int64_t off = 0, cnt = 0;
    ReadPod(in, off);
    ReadPod(in, cnt);
    n.pointIndexBegin = off;  // byte offset — see the OpenCache field-reuse note in the header
    n.pointCount = cnt;
  }
  if (!in) {
    res.errorMessage = "'" + cachePath + "' node table is truncated";
    return res;
  }

  res.ok = true;
  res.cache.cachePath = cachePath;
  res.cache.octree = std::move(tree);
  res.cache.hasColor = hasColorByte != 0;
  res.cache.hasIntensity = hasIntensityByte != 0;
  res.cache.totalPointCount = totalPointCount;
  res.cache.stamp.sizeBytes = srcSize;
  res.cache.stamp.mtimeTicks = srcMtime;
  return res;
}

bool ReadLeafPoints(const OpenCache &cache, int leafNodeIndex, std::vector<double> &outXyz,
                    std::vector<float> &outColorsRgb, std::vector<float> &outIntensity) {
  outXyz.clear();
  outColorsRgb.clear();
  outIntensity.clear();
  if (leafNodeIndex < 0 || static_cast<std::size_t>(leafNodeIndex) >= cache.octree.nodes.size())
    return false;
  const pointcloud::Node &node = cache.octree.nodes[static_cast<std::size_t>(leafNodeIndex)];
  if (!node.isLeaf()) return false;

  std::ifstream in(cache.cachePath, std::ios::binary);
  if (!in) return false;
  const std::int64_t sectionStart =
      PointDataSectionStart(static_cast<std::uint32_t>(cache.octree.nodes.size()));
  in.seekg(sectionStart + node.pointIndexBegin, std::ios::beg);
  if (!in) return false;

  const std::size_t recordSize = RecordSize(cache.hasColor, cache.hasIntensity);
  const std::int64_t count = node.pointCount;
  outXyz.reserve(static_cast<std::size_t>(count) * 3);
  if (cache.hasColor) outColorsRgb.reserve(static_cast<std::size_t>(count) * 3);
  if (cache.hasIntensity) outIntensity.reserve(static_cast<std::size_t>(count));

  // One read() for the whole leaf rather than one per point — a leaf is bounded to
  // kLeafPointThreshold points (a few MB at most), so reading it whole is always safe, and this is
  // called once per leaf every time a preview/renderer pages one in.
  std::vector<char> raw(static_cast<std::size_t>(count) * recordSize);
  in.read(raw.data(), static_cast<std::streamsize>(raw.size()));
  if (!in) {
    outXyz.clear();
    outColorsRgb.clear();
    outIntensity.clear();
    return false;
  }
  for (std::int64_t i = 0; i < count; ++i) {
    const char *rec = raw.data() + static_cast<std::size_t>(i) * recordSize;
    double xyz[3];
    std::memcpy(xyz, rec, sizeof(xyz));
    outXyz.push_back(xyz[0]);
    outXyz.push_back(xyz[1]);
    outXyz.push_back(xyz[2]);
    std::size_t off = sizeof(xyz);
    if (cache.hasColor) {
      float rgb[3];
      std::memcpy(rgb, rec + off, sizeof(rgb));
      off += sizeof(rgb);
      outColorsRgb.push_back(rgb[0]);
      outColorsRgb.push_back(rgb[1]);
      outColorsRgb.push_back(rgb[2]);
    }
    if (cache.hasIntensity) {
      float inten;
      std::memcpy(&inten, rec + off, sizeof(inten));
      outIntensity.push_back(inten);
    }
  }
  return true;
}

bool MatchesSource(const std::string &cachePath, const std::string &sourcePath) {
  const OpenResult res = Open(cachePath);
  if (!res.ok) return false;
  std::error_code ec;
  const auto srcSize = fs::file_size(sourcePath, ec);
  if (ec) return false;
  const auto srcMtime = fs::last_write_time(sourcePath, ec);
  if (ec) return false;
  return res.cache.stamp.sizeBytes == static_cast<std::uint64_t>(srcSize) &&
        res.cache.stamp.mtimeTicks == static_cast<std::int64_t>(srcMtime.time_since_epoch().count());
}

}  // namespace pointcloudcache
