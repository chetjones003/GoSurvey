// LAS (ASPRS binary) point-cloud reader/writer (REQ-172, LAS increment —
// `spec/file-format-specs.md` §2/§3.2/§5/§6, delivered after PTX per issue #825 increment 3).
//
// Fixture LAS files are synthesized as binary bytes in-code rather than committed binary
// samples — the acceptance this pins is "N points in, N points out, within REQ-101," a
// hand-checked scale/offset decode (LAS's equivalent of PTX's hand-checked 4x4 setup transform),
// a write-then-read round trip, and malformed/truncated/unsupported-format/missing-file refusal
// (REQ-001's "no partial cloud, no crash").

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "io/CadPointCloudLas.hpp"

using Catch::Approx;

namespace {

std::filesystem::path TempLasPath(const char *name) {
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

template <typename T>
void AppendLE(std::vector<unsigned char> &buf, const T &v) {
  const auto *p = reinterpret_cast<const unsigned char *>(&v);
  buf.insert(buf.end(), p, p + sizeof(T));
}

/// Builds a minimal, well-formed LAS 1.2 header (227 bytes) with the given scale/offset/format/
/// record-length/point-count, so each test can hand-author exactly the bytes it wants to pin —
/// mirroring how CadPointCloudPtsTests/CadPointCloudPtxTests author their own ASCII fixtures.
std::vector<unsigned char> MakeHeader(double scaleX, double scaleY, double scaleZ, double offsetX,
                                       double offsetY, double offsetZ, std::uint8_t pointFormat,
                                       std::uint16_t recordLength, std::uint32_t pointCount,
                                       double minX = 0, double maxX = 0, double minY = 0,
                                       double maxY = 0, double minZ = 0, double maxZ = 0) {
  std::vector<unsigned char> h(227, 0);
  std::memcpy(h.data() + 0, "LASF", 4);
  h[24] = 1;  // version major
  h[25] = 2;  // version minor (LAS 1.2)
  const std::uint16_t headerSize = 227;
  const std::uint32_t offsetToPointData = 227;
  std::memcpy(h.data() + 94, &headerSize, 2);
  std::memcpy(h.data() + 96, &offsetToPointData, 4);
  const std::uint32_t numVlr = 0;
  std::memcpy(h.data() + 100, &numVlr, 4);
  h[104] = pointFormat;
  std::memcpy(h.data() + 105, &recordLength, 2);
  std::memcpy(h.data() + 107, &pointCount, 4);
  std::memcpy(h.data() + 131, &scaleX, 8);
  std::memcpy(h.data() + 139, &scaleY, 8);
  std::memcpy(h.data() + 147, &scaleZ, 8);
  std::memcpy(h.data() + 155, &offsetX, 8);
  std::memcpy(h.data() + 163, &offsetY, 8);
  std::memcpy(h.data() + 171, &offsetZ, 8);
  std::memcpy(h.data() + 179, &maxX, 8);
  std::memcpy(h.data() + 187, &minX, 8);
  std::memcpy(h.data() + 195, &maxY, 8);
  std::memcpy(h.data() + 203, &minY, 8);
  std::memcpy(h.data() + 211, &maxZ, 8);
  std::memcpy(h.data() + 219, &minZ, 8);
  return h;
}

void WriteRawLas(const std::filesystem::path &path, const std::vector<unsigned char> &bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

/// Format 0 point record (20 bytes): int32 X,Y,Z, uint16 intensity, 6 bytes of
// classification/return/scan-angle/user-data/point-source fields (zeroed — not read by REQ-172).
void AppendFormat0Point(std::vector<unsigned char> &buf, std::int32_t x, std::int32_t y,
                         std::int32_t z, std::uint16_t intensity) {
  AppendLE(buf, x);
  AppendLE(buf, y);
  AppendLE(buf, z);
  AppendLE(buf, intensity);
  const unsigned char zero6[6] = {0, 0, 0, 0, 0, 0};
  buf.insert(buf.end(), zero6, zero6 + 6);
}

/// Format 2 point record (26 bytes): format 0's 20 bytes + uint16 R,G,B.
void AppendFormat2Point(std::vector<unsigned char> &buf, std::int32_t x, std::int32_t y,
                         std::int32_t z, std::uint16_t intensity, std::uint16_t r, std::uint16_t g,
                         std::uint16_t b) {
  AppendFormat0Point(buf, x, y, z, intensity);
  AppendLE(buf, r);
  AppendLE(buf, g);
  AppendLE(buf, b);
}

}  // namespace

TEST_CASE("ReadLasFile: a format-0 (no colour) fixture round-trips point count and XYZ",
          "[pointcloud][las]") {
  const auto path = TempLasPath("gosurvey_test_las_fmt0.las");
  auto bytes = MakeHeader(/*scale*/ 0.01, 0.01, 0.01, /*offset*/ 0.0, 0.0, 0.0,
                           /*format*/ 0, /*recordLength*/ 20, /*count*/ 4);
  AppendFormat0Point(bytes, 0, 0, 0, 0);
  AppendFormat0Point(bytes, 150, 250, 350, 100);
  AppendFormat0Point(bytes, -1025, 2050, -12, 0);
  AppendFormat0Point(bytes, 10000, -20000, 30000, 65535);
  WriteRawLas(path, bytes);

  const auto result = pointcloud_las::ReadLasFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == 4);
  CHECK(result.colorsRgb.empty());
  REQUIRE(result.intensity.size() == 4);
  CHECK(result.pointsXyz[0] == Approx(0.0));
  CHECK(result.pointsXyz[3] == Approx(1.5));   // 150 * 0.01
  CHECK(result.pointsXyz[4] == Approx(2.5));   // 250 * 0.01
  CHECK(result.pointsXyz[5] == Approx(3.5));   // 350 * 0.01
  CHECK(result.pointsXyz[6] == Approx(-10.25));  // -1025 * 0.01
  CHECK(result.intensity[1] == Approx(100.0 / 65535.0));
  CHECK(result.intensity[3] == Approx(1.0));
}

TEST_CASE("ReadLasFile: a format-2 (colour) fixture carries RGB normalized by 65535",
          "[pointcloud][las]") {
  const auto path = TempLasPath("gosurvey_test_las_fmt2.las");
  auto bytes = MakeHeader(0.001, 0.001, 0.001, 0.0, 0.0, 0.0, /*format*/ 2, /*recordLength*/ 26,
                           /*count*/ 2);
  AppendFormat2Point(bytes, 1000, 2000, 3000, 30000, 65535, 32768, 0);
  AppendFormat2Point(bytes, 4000, 5000, 6000, 0, 0, 0, 65535);
  WriteRawLas(path, bytes);

  const auto result = pointcloud_las::ReadLasFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == 2);
  REQUIRE(result.colorsRgb.size() == 6);
  REQUIRE(result.intensity.size() == 2);
  CHECK(result.colorsRgb[0] == Approx(1.0));             // 65535 -> 1.0
  CHECK(result.colorsRgb[1] == Approx(32768.0 / 65535.0));
  CHECK(result.colorsRgb[2] == Approx(0.0));
  CHECK(result.colorsRgb[5] == Approx(1.0));             // 65535 -> 1.0
  CHECK(result.intensity[0] == Approx(30000.0 / 65535.0));
}

TEST_CASE("ReadLasFile: scale+offset decode matches a hand-computed real-world coordinate",
          "[pointcloud][las]") {
  // Hand check: raw=123456, scale=0.01, offset=500.0 -> real = 123456*0.01 + 500.0 = 1734.56.
  const auto path = TempLasPath("gosurvey_test_las_scaleoffset.las");
  auto bytes = MakeHeader(/*scaleX*/ 0.01, /*scaleY*/ 0.02, /*scaleZ*/ 0.05,
                           /*offsetX*/ 500.0, /*offsetY*/ -1000.0, /*offsetZ*/ 10.0,
                           /*format*/ 0, /*recordLength*/ 20, /*count*/ 1);
  AppendFormat0Point(bytes, 123456, -50000, 2000, 500);
  WriteRawLas(path, bytes);

  const auto result = pointcloud_las::ReadLasFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == 1);
  // X: 123456 * 0.01 + 500.0 = 1734.56
  CHECK(result.pointsXyz[0] == Approx(1734.56));
  // Y: -50000 * 0.02 + -1000.0 = -2000.0
  CHECK(result.pointsXyz[1] == Approx(-2000.0));
  // Z: 2000 * 0.05 + 10.0 = 110.0
  CHECK(result.pointsXyz[2] == Approx(110.0));
}

TEST_CASE("ReadLasFile: a bad signature is refused, not partially imported", "[pointcloud][las]") {
  const auto path = TempLasPath("gosurvey_test_las_badsig.las");
  auto bytes = MakeHeader(0.01, 0.01, 0.01, 0, 0, 0, 0, 20, 1);
  std::memcpy(bytes.data(), "XXXX", 4);  // corrupt the signature
  AppendFormat0Point(bytes, 1, 2, 3, 0);
  WriteRawLas(path, bytes);

  const auto result = pointcloud_las::ReadLasFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadLasFile: a header declaring more points than the file holds is refused (truncated)",
          "[pointcloud][las]") {
  const auto path = TempLasPath("gosurvey_test_las_truncated.las");
  auto bytes = MakeHeader(0.01, 0.01, 0.01, 0, 0, 0, 0, 20, /*count*/ 5);
  AppendFormat0Point(bytes, 1, 2, 3, 0);
  AppendFormat0Point(bytes, 4, 5, 6, 0);  // header says 5, only 2 records present
  WriteRawLas(path, bytes);

  const auto result = pointcloud_las::ReadLasFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadLasFile: an unsupported point data record format is refused by name, not crashed",
          "[pointcloud][las]") {
  const auto path = TempLasPath("gosurvey_test_las_unsupported_format.las");
  // Format 6 (extended, GPS-time-heavy) is intentionally unsupported — name the actual minimum
  // record length (30 bytes) so the header alone is self-consistent for this check.
  auto bytes = MakeHeader(0.01, 0.01, 0.01, 0, 0, 0, /*format*/ 6, /*recordLength*/ 30,
                           /*count*/ 1);
  bytes.resize(bytes.size() + 30, 0);
  WriteRawLas(path, bytes);

  const auto result = pointcloud_las::ReadLasFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.errorMessage.find("format 6") != std::string::npos);
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadLasFile: a missing file path is refused without crashing", "[pointcloud][las]") {
  const auto result = pointcloud_las::ReadLasFile("Z:\\path\\that\\does\\not\\exist.las");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadLasFile: an empty path is refused without crashing", "[pointcloud][las]") {
  const auto result = pointcloud_las::ReadLasFile("");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
}

TEST_CASE("WriteLasFile + ReadLasFile: a no-colour write-then-read round trip preserves count and XYZ",
          "[pointcloud][las]") {
  const auto path = TempLasPath("gosurvey_test_las_roundtrip_xyz.las");
  constexpr int kCount = 250;
  std::vector<double> xyz;
  for (int i = 0; i < kCount; ++i) {
    xyz.push_back(static_cast<double>(i));
    xyz.push_back(static_cast<double>(i) * 2.0);
    xyz.push_back(static_cast<double>(i) * 3.0);
  }

  const auto wr = pointcloud_las::WriteLasFile(path.string(), xyz, {}, {});
  REQUIRE(wr.ok);
  CHECK(wr.errorMessage.empty());

  const auto result = pointcloud_las::ReadLasFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == kCount);
  CHECK(result.colorsRgb.empty());
  for (int i = 0; i < kCount; ++i) {
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 0] ==
          Approx(static_cast<double>(i)).margin(0.002));
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 1] ==
          Approx(static_cast<double>(i) * 2.0).margin(0.002));
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 2] ==
          Approx(static_cast<double>(i) * 3.0).margin(0.002));
  }
}

TEST_CASE("WriteLasFile + ReadLasFile: a colour+intensity round trip preserves every channel",
          "[pointcloud][las]") {
  const auto path = TempLasPath("gosurvey_test_las_roundtrip_full.las");
  constexpr int kCount = 50;
  std::vector<double> xyz;
  std::vector<float> colors, intensity;
  for (int i = 0; i < kCount; ++i) {
    xyz.push_back(static_cast<double>(i) * 0.1);
    xyz.push_back(static_cast<double>(i) * 0.2);
    xyz.push_back(static_cast<double>(i) * 0.3);
    colors.push_back(static_cast<float>(i % 256) / 255.0f);
    colors.push_back(static_cast<float>((i * 2) % 256) / 255.0f);
    colors.push_back(static_cast<float>((i * 3) % 256) / 255.0f);
    intensity.push_back(static_cast<float>(i % 100) / 100.0f);
  }

  const auto wr = pointcloud_las::WriteLasFile(path.string(), xyz, colors, intensity);
  REQUIRE(wr.ok);

  const auto result = pointcloud_las::ReadLasFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == kCount);
  REQUIRE(result.colorsRgb.size() == static_cast<std::size_t>(kCount) * 3);
  REQUIRE(result.intensity.size() == static_cast<std::size_t>(kCount));
  for (int i = 0; i < kCount; ++i) {
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 0] ==
          Approx(static_cast<double>(i) * 0.1).margin(0.002));
    // RGB/intensity round-trip through a 16-bit channel (0-65535), not a lossless float —
    // tolerance matches that quantization.
    CHECK(result.colorsRgb[static_cast<std::size_t>(i) * 3 + 0] ==
          Approx(colors[static_cast<std::size_t>(i) * 3 + 0]).margin(1.0f / 255.0f));
    CHECK(result.intensity[static_cast<std::size_t>(i)] ==
          Approx(intensity[static_cast<std::size_t>(i)]).margin(1.0f / 255.0f));
  }
}

TEST_CASE("WriteLasFile: an empty point set fails cleanly", "[pointcloud][las]") {
  const auto path = TempLasPath("gosurvey_test_las_empty_write.las");
  const auto wr = pointcloud_las::WriteLasFile(path.string(), {}, {}, {});
  CHECK_FALSE(wr.ok);
  CHECK_FALSE(wr.errorMessage.empty());
  CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("QuickPointCountEstimate: reads the header without validating the point records",
          "[pointcloud][las]") {
  const auto path = TempLasPath("gosurvey_test_las_estimate.las");
  auto bytes = MakeHeader(0.01, 0.01, 0.01, 0, 0, 0, 0, 20, /*count*/ 777);
  // No point records appended at all — QuickPointCountEstimate must not need them.
  WriteRawLas(path, bytes);
  CHECK(pointcloud_las::QuickPointCountEstimate(path.string()) == 0);  // header-only file is truncated
  RemoveWithRetry(path);

  const auto path2 = TempLasPath("gosurvey_test_las_estimate_ok.las");
  auto bytes2 = MakeHeader(0.01, 0.01, 0.01, 0, 0, 0, 0, 20, /*count*/ 3);
  AppendFormat0Point(bytes2, 1, 2, 3, 0);
  AppendFormat0Point(bytes2, 4, 5, 6, 0);
  AppendFormat0Point(bytes2, 7, 8, 9, 0);
  WriteRawLas(path2, bytes2);
  CHECK(pointcloud_las::QuickPointCountEstimate(path2.string()) == 3);
  RemoveWithRetry(path2);

  CHECK(pointcloud_las::QuickPointCountEstimate("Z:\\path\\that\\does\\not\\exist.las") == 0);
  CHECK(pointcloud_las::QuickPointCountEstimate("") == 0);
}
