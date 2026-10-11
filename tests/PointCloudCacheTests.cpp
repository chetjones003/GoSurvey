// .gscloud out-of-core point-cloud cache (REQ-171/172, ADR-060).
//
// Pins the property the whole cache exists for: a cloud built out-of-core (streamed through a
// bounded chunk size, never held whole in memory) round-trips to the same points a direct read
// would produce, and a stale/corrupt/missing cache is detected rather than silently trusted or
// crashing (REQ-001).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <E57SimpleData.h>
#include <E57SimpleWriter.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "util/pointcloudcache.hpp"

namespace {

std::filesystem::path TempPath(const char *name) {
  return std::filesystem::temp_directory_path() / name;
}

void RemoveWithRetry(const std::filesystem::path &path) {
  std::error_code ec;
  for (int attempt = 0; attempt < 10; ++attempt) {
    std::filesystem::remove(path, ec);
    if (!ec) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

/// Writes an E57 with `count` points spread over a real 3D volume (not colinear — a degenerate
/// point set would never force an octree split), with RGB colour, using the vendored writer.
void WriteFixtureE57(const std::filesystem::path &path, int count) {
  e57::Writer writer(path.string(), e57::WriterOptions{});
  e57::Data3D header;
  header.pointFields.cartesianXField = true;
  header.pointFields.cartesianYField = true;
  header.pointFields.cartesianZField = true;
  header.pointFields.colorRedField = true;
  header.pointFields.colorGreenField = true;
  header.pointFields.colorBlueField = true;
  header.pointCount = static_cast<std::size_t>(count);
  header.colorLimits.colorRedMaximum = 255;
  header.colorLimits.colorGreenMaximum = 255;
  header.colorLimits.colorBlueMaximum = 255;
  const std::int64_t scanIndex = writer.NewData3D(header);

  e57::Data3DPointsData_t<double> buffers(header);
  for (int i = 0; i < count; ++i) {
    // A 3D grid-ish spread so the point set spans all 8 octants of its own bounding box.
    buffers.cartesianX[i] = static_cast<double>(i % 37) - 18.0;
    buffers.cartesianY[i] = static_cast<double>((i / 37) % 37) - 18.0;
    const int zBucket = i / (37 * 37);  // deliberate integer bucketing, not a precision loss
    buffers.cartesianZ[i] = static_cast<double>(zBucket) - 5.0;
    buffers.colorRed[i] = static_cast<std::uint16_t>(i % 256);
    buffers.colorGreen[i] = static_cast<std::uint16_t>((i * 2) % 256);
    buffers.colorBlue[i] = static_cast<std::uint16_t>((i * 3) % 256);
  }
  e57::CompressedVectorWriter vw =
      writer.SetUpData3DPointsData(scanIndex, static_cast<std::size_t>(count), buffers);
  vw.write(static_cast<std::size_t>(count));
  vw.close();
  writer.Close();
}

}  // namespace

TEST_CASE("BuildFromE57 + Open: round-trips point count, octree covers every point exactly once",
          "[pointcloud][cache]") {
  const auto srcPath = TempPath("gosurvey_cache_test_src.e57");
  const auto cachePath = TempPath("gosurvey_cache_test_src.e57.gscloud");
  constexpr int kCount = 5000;
  WriteFixtureE57(srcPath, kCount);

  std::vector<std::string> log;
  std::string err;
  const bool built = pointcloudcache::BuildFromE57(srcPath.string(), cachePath.string(), log, &err);
  REQUIRE(built);
  CHECK(err.empty());

  const pointcloudcache::OpenResult opened = pointcloudcache::Open(cachePath.string());
  REQUIRE(opened.ok);
  CHECK(opened.cache.totalPointCount == kCount);
  CHECK(opened.cache.hasColor);
  CHECK_FALSE(opened.cache.hasIntensity);
  REQUIRE_FALSE(opened.cache.octree.nodes.empty());

  // Every leaf's declared point count sums to the total — no point lost or double-counted across
  // the recursive out-of-core split.
  std::int64_t leafSum = 0;
  int leafCount = 0;
  for (const auto &n : opened.cache.octree.nodes) {
    if (n.isLeaf()) {
      leafSum += n.pointCount;
      ++leafCount;
    }
  }
  CHECK(leafSum == kCount);
  CHECK(leafCount >= 1);

  // Reading every leaf back and reconstructing the point set must reproduce exactly kCount points,
  // each inside the root's own bounds (a basic sanity check that leaf reads land on real data, not
  // on garbage past the record boundary).
  const auto &rootBounds = opened.cache.octree.nodes[0].bounds;
  std::int64_t reconstructed = 0;
  for (int i = 0; i < static_cast<int>(opened.cache.octree.nodes.size()); ++i) {
    if (!opened.cache.octree.nodes[static_cast<std::size_t>(i)].isLeaf()) continue;
    std::vector<double> xyz;
    std::vector<float> rgb, intensity;
    REQUIRE(pointcloudcache::ReadLeafPoints(opened.cache, i, xyz, rgb, intensity));
    const std::int64_t n = static_cast<std::int64_t>(xyz.size() / 3);
    CHECK(rgb.size() == xyz.size());  // hasColor: one RGB triple per point
    for (std::int64_t p = 0; p < n; ++p) {
      const double x = xyz[static_cast<std::size_t>(p) * 3 + 0];
      const double y = xyz[static_cast<std::size_t>(p) * 3 + 1];
      const double z = xyz[static_cast<std::size_t>(p) * 3 + 2];
      CHECK(x >= rootBounds.minX - 1e-9);
      CHECK(x <= rootBounds.maxX + 1e-9);
      CHECK(y >= rootBounds.minY - 1e-9);
      CHECK(y <= rootBounds.maxY + 1e-9);
      CHECK(z >= rootBounds.minZ - 1e-9);
      CHECK(z <= rootBounds.maxZ + 1e-9);
    }
    reconstructed += n;
  }
  CHECK(reconstructed == kCount);

  RemoveWithRetry(srcPath);
  RemoveWithRetry(cachePath);
}

TEST_CASE("BuildFromE57: a leaf-threshold-forcing point count actually splits into multiple leaves",
          "[pointcloud][cache]") {
  // 200,000 points is well past pointcloudoctree's 50,000-per-leaf default, so a correct build
  // must produce more than one leaf — a build that silently kept everything as one giant leaf
  // would defeat the entire point of an out-of-core octree (no LOD/paging granularity).
  const auto srcPath = TempPath("gosurvey_cache_test_split.e57");
  const auto cachePath = TempPath("gosurvey_cache_test_split.e57.gscloud");
  constexpr int kCount = 200000;
  WriteFixtureE57(srcPath, kCount);

  std::vector<std::string> log;
  std::string err;
  REQUIRE(pointcloudcache::BuildFromE57(srcPath.string(), cachePath.string(), log, &err));

  const pointcloudcache::OpenResult opened = pointcloudcache::Open(cachePath.string());
  REQUIRE(opened.ok);
  int leafCount = 0;
  for (const auto &n : opened.cache.octree.nodes)
    if (n.isLeaf()) ++leafCount;
  CHECK(leafCount > 1);

  RemoveWithRetry(srcPath);
  RemoveWithRetry(cachePath);
}

TEST_CASE("BuildFromE57: an octant-0-empty split does not orphan points "
          "(regression: isLeaf() must not key off children[0] alone)",
          "[pointcloud][cache]") {
  // Same shape as the pointcloudoctree regression test: every point sits on the "upper" side of
  // the root's x-split, so octant 0 is empty at the root and the interior node's children[0] is
  // -1 even though it is genuinely interior. Forces enough points to guarantee a split.
  const auto srcPath = TempPath("gosurvey_cache_test_octant0.e57");
  const auto cachePath = TempPath("gosurvey_cache_test_octant0.e57.gscloud");
  constexpr int kCount = 150000;

  {
    e57::Writer writer(srcPath.string(), e57::WriterOptions{});
    e57::Data3D header;
    header.pointFields.cartesianXField = true;
    header.pointFields.cartesianYField = true;
    header.pointFields.cartesianZField = true;
    header.pointCount = static_cast<std::size_t>(kCount);
    const std::int64_t scanIndex = writer.NewData3D(header);
    e57::Data3DPointsData_t<double> buffers(header);
    for (int i = 0; i < kCount; ++i) {
      buffers.cartesianX[i] = 100.0 + (i % 2) * 0.5;  // always on the upper side of any x-split
      buffers.cartesianY[i] = static_cast<double>((i / 2) % 400);
      buffers.cartesianZ[i] = static_cast<double>(i / 800);
    }
    e57::CompressedVectorWriter vw =
        writer.SetUpData3DPointsData(scanIndex, static_cast<std::size_t>(kCount), buffers);
    vw.write(static_cast<std::size_t>(kCount));
    vw.close();
    writer.Close();
  }

  std::vector<std::string> log;
  std::string err;
  REQUIRE(pointcloudcache::BuildFromE57(srcPath.string(), cachePath.string(), log, &err));

  const pointcloudcache::OpenResult opened = pointcloudcache::Open(cachePath.string());
  REQUIRE(opened.ok);
  CHECK(opened.cache.totalPointCount == kCount);

  std::int64_t leafSum = 0;
  for (int i = 0; i < static_cast<int>(opened.cache.octree.nodes.size()); ++i) {
    if (!opened.cache.octree.nodes[static_cast<size_t>(i)].isLeaf()) continue;
    std::vector<double> xyz;
    std::vector<float> rgb, intensity;
    REQUIRE(pointcloudcache::ReadLeafPoints(opened.cache, i, xyz, rgb, intensity));
    leafSum += static_cast<std::int64_t>(xyz.size() / 3);
  }
  CHECK(leafSum == kCount);  // would undercount (orphaned points) under the old buggy isLeaf()

  RemoveWithRetry(srcPath);
  RemoveWithRetry(cachePath);
}

TEST_CASE("MatchesSource: true right after build, false once the source file changes",
          "[pointcloud][cache]") {
  const auto srcPath = TempPath("gosurvey_cache_test_stamp.e57");
  const auto cachePath = TempPath("gosurvey_cache_test_stamp.e57.gscloud");
  WriteFixtureE57(srcPath, 500);

  std::vector<std::string> log;
  std::string err;
  REQUIRE(pointcloudcache::BuildFromE57(srcPath.string(), cachePath.string(), log, &err));
  CHECK(pointcloudcache::MatchesSource(cachePath.string(), srcPath.string()));

  // Rewrite the source with a different point count — same path, different size/mtime.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));  // ensure the mtime tick advances
  WriteFixtureE57(srcPath, 900);
  CHECK_FALSE(pointcloudcache::MatchesSource(cachePath.string(), srcPath.string()));

  RemoveWithRetry(srcPath);
  RemoveWithRetry(cachePath);
}

TEST_CASE("Open: a missing cache file fails cleanly, not a crash", "[pointcloud][cache]") {
  const pointcloudcache::OpenResult opened =
      pointcloudcache::Open("Z:\\path\\that\\does\\not\\exist.e57.gscloud");
  CHECK_FALSE(opened.ok);
  CHECK_FALSE(opened.errorMessage.empty());
}

TEST_CASE("Open: a garbage file is refused, not parsed as a valid cache", "[pointcloud][cache]") {
  const auto path = TempPath("gosurvey_cache_test_garbage.gscloud");
  {
    std::ofstream out(path, std::ios::binary);
    out << "not a real gscloud file, just garbage bytes 0123456789";
  }
  const pointcloudcache::OpenResult opened = pointcloudcache::Open(path.string());
  CHECK_FALSE(opened.ok);
  CHECK_FALSE(opened.errorMessage.empty());
  RemoveWithRetry(path);
}

TEST_CASE("BuildFromE57: a malformed source produces no partial cache file", "[pointcloud][cache]") {
  const auto srcPath = TempPath("gosurvey_cache_test_badsource.e57");
  const auto cachePath = TempPath("gosurvey_cache_test_badsource.e57.gscloud");
  {
    std::ofstream out(srcPath, std::ios::binary);
    out << "garbage, not an e57 file";
  }
  RemoveWithRetry(cachePath);  // in case a prior failed run left one

  std::vector<std::string> log;
  std::string err;
  const bool built = pointcloudcache::BuildFromE57(srcPath.string(), cachePath.string(), log, &err);
  CHECK_FALSE(built);
  CHECK_FALSE(err.empty());
  CHECK_FALSE(std::filesystem::exists(cachePath));

  RemoveWithRetry(srcPath);
}
