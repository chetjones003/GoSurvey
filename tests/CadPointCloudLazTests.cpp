// LAZ (LASzip-compressed LAS) point-cloud reader/writer (REQ-172, LAZ increment —
// `spec/file-format-specs.md` §2/§3.2/§5/§6, delivered last per issue #825 increment 4).
//
// Unlike CadPointCloudLasTests.cpp's hand-synthesized point RECORDS, a LAZ file's point-data
// section is LASzip's own compressed byte stream — this suite cannot hand-author compressed
// bytes and pin a specific byte offset the way the plain-LAS suite does. What CAN be
// hand-authored, byte-for-byte, is the Public Header Block (identical layout to a plain LAS,
// since LAZ's own vendored LASzip transparently reads either form), which is enough to pin the
// "bad signature" and "unsupported point data record format" refusals without needing any real
// compressed payload at all (both checks fire before a single point is decoded).
//
// Everything else here — the scale/offset decode, colour/intensity normalization, and the
// REQ-172 "LAS and LAZ of the same points import equal counts and XYZ" cross-check — is pinned
// via `WriteLazFile`'s own round trip plus one hand-authored PLAIN (uncompressed) LAS byte
// fixture read back through the *same* `ReadLazFile`: vendored LASzip reads a plain LAS exactly
// as readily as a compressed one (the only difference is the VLR announcing compression), so
// feeding `pointcloud_laz::ReadLazFile` both an uncompressed LAS fixture and a `WriteLazFile`
// LAZ fixture built from the identical point set, and asserting they decode to the same counts
// and XYZ, exercises the cross-check without depending on a separate LAS-specific importer
// module (`CadPointCloudLas.cpp`/`.hpp` are a different, independent increment of issue #825 and
// are not present on every branch this one is built from — see `CadPointCloudLaz.hpp`'s top
// note).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "io/CadPointCloudLaz.hpp"

using Catch::Approx;

namespace {

std::filesystem::path TempLazPath(const char *name) {
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

/// Builds a minimal, well-formed PLAIN (uncompressed) LAS 1.2 Public Header Block (227 bytes) —
/// the exact same byte layout vendored LASzip reads for either a LAS or a LAZ file (LAZ's extra
/// "laszip encoded" VLR is what actually signals compression, not the header itself). Used here
/// only to pin the two checks that fire before any point data is touched (bad signature,
/// unsupported format) and for the LAS/LAZ cross-check — not a general-purpose LAS writer.
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

void WriteRawFile(const std::filesystem::path &path, const std::vector<unsigned char> &bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

/// Format 0 point record (20 bytes): int32 X,Y,Z, uint16 intensity, 6 bytes of
/// classification/return/scan-angle/user-data/point-source fields (zeroed — not read by REQ-172).
void AppendFormat0Point(std::vector<unsigned char> &buf, std::int32_t x, std::int32_t y,
                         std::int32_t z, std::uint16_t intensity) {
  AppendLE(buf, x);
  AppendLE(buf, y);
  AppendLE(buf, z);
  AppendLE(buf, intensity);
  const unsigned char zero6[6] = {0, 0, 0, 0, 0, 0};
  buf.insert(buf.end(), zero6, zero6 + 6);
}

/// Format 6 (extended, LAS 1.4) point record (30 bytes): int32 X,Y,Z, uint16 intensity, 1 byte
/// return info, 1 byte classification flags/channel/direction/edge, 1 byte classification, 1 byte
/// user data, int16 scan angle, uint16 point source ID, f64 GPS time (D-2026-10-15-a: GPS time is
/// decoded by LASzip but not asserted on — REQ-171's cloud schema has no field for it).
void AppendFormat6Point(std::vector<unsigned char> &buf, std::int32_t x, std::int32_t y,
                         std::int32_t z, std::uint16_t intensity, double gpsTime) {
  AppendLE(buf, x);
  AppendLE(buf, y);
  AppendLE(buf, z);
  AppendLE(buf, intensity);
  const unsigned char zero4[4] = {0, 0, 0, 0};  // return info, classification flags, class, user data
  buf.insert(buf.end(), zero4, zero4 + 4);
  AppendLE(buf, static_cast<std::int16_t>(0));   // scan angle
  AppendLE(buf, static_cast<std::uint16_t>(0));  // point source ID
  AppendLE(buf, gpsTime);
}

}  // namespace

TEST_CASE("WriteLazFile + ReadLazFile: a no-colour write-then-read round trip preserves count and XYZ",
          "[pointcloud][laz]") {
  const auto path = TempLazPath("gosurvey_test_laz_roundtrip_xyz.laz");
  constexpr int kCount = 250;
  std::vector<double> xyz;
  for (int i = 0; i < kCount; ++i) {
    xyz.push_back(static_cast<double>(i));
    xyz.push_back(static_cast<double>(i) * 2.0);
    xyz.push_back(static_cast<double>(i) * 3.0);
  }

  const auto wr = pointcloud_laz::WriteLazFile(path.string(), xyz, {}, {});
  REQUIRE(wr.ok);
  CHECK(wr.errorMessage.empty());
  REQUIRE(std::filesystem::exists(path));

  const auto result = pointcloud_laz::ReadLazFile(path.string());
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

TEST_CASE("WriteLazFile + ReadLazFile: a colour+intensity round trip preserves every channel",
          "[pointcloud][laz]") {
  const auto path = TempLazPath("gosurvey_test_laz_roundtrip_full.laz");
  constexpr int kCount = 500;  // large enough to make the compressed file worth truncating later
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

  const auto wr = pointcloud_laz::WriteLazFile(path.string(), xyz, colors, intensity);
  REQUIRE(wr.ok);

  const auto result = pointcloud_laz::ReadLazFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == kCount);
  REQUIRE(result.colorsRgb.size() == static_cast<std::size_t>(kCount) * 3);
  REQUIRE(result.intensity.size() == static_cast<std::size_t>(kCount));
  for (int i = 0; i < kCount; ++i) {
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 0] ==
          Approx(static_cast<double>(i) * 0.1).margin(0.002));
    // RGB/intensity round-trip through a 16-bit channel (0-65535), not a lossless float —
    // tolerance matches that quantization, same as the plain-LAS writer's own test.
    CHECK(result.colorsRgb[static_cast<std::size_t>(i) * 3 + 0] ==
          Approx(colors[static_cast<std::size_t>(i) * 3 + 0]).margin(1.0f / 255.0f));
    CHECK(result.intensity[static_cast<std::size_t>(i)] ==
          Approx(intensity[static_cast<std::size_t>(i)]).margin(1.0f / 255.0f));
  }
}

TEST_CASE("WriteLazFile: an empty point set fails cleanly", "[pointcloud][laz]") {
  const auto path = TempLazPath("gosurvey_test_laz_empty_write.laz");
  const auto wr = pointcloud_laz::WriteLazFile(path.string(), {}, {}, {});
  CHECK_FALSE(wr.ok);
  CHECK_FALSE(wr.errorMessage.empty());
  CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("ReadLazFile: a bad signature is refused, not partially imported", "[pointcloud][laz]") {
  const auto path = TempLazPath("gosurvey_test_laz_badsig.laz");
  auto bytes = MakeHeader(0.01, 0.01, 0.01, 0, 0, 0, /*format*/ 0, /*recordLength*/ 20,
                          /*count*/ 0);
  std::memcpy(bytes.data(), "XXXX", 4);  // corrupt the signature
  WriteRawFile(path, bytes);

  const auto result = pointcloud_laz::ReadLazFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadLazFile: point data record format 6 (LAS 1.4 extended) is read, not refused "
          "(D-2026-10-15-a, bug: real USGS LPC files use this format)",
          "[pointcloud][laz]") {
  const auto path = TempLazPath("gosurvey_test_laz_format6.laz");
  constexpr int kCount = 10;
  constexpr double kScale = 0.01;
  auto bytes = MakeHeader(kScale, kScale, kScale, 0, 0, 0, /*format*/ 6, /*recordLength*/ 30,
                          static_cast<std::uint32_t>(kCount));
  for (int i = 0; i < kCount; ++i) {
    AppendFormat6Point(bytes, /*x*/ i * 100, /*y*/ i * 200, /*z*/ i * 300,
                        /*intensity*/ static_cast<std::uint16_t>(i * 1000), /*gpsTime*/ i * 1.5);
  }
  WriteRawFile(path, bytes);

  const auto result = pointcloud_laz::ReadLazFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  CHECK(result.errorMessage.empty());
  REQUIRE(result.pointCount() == kCount);
  CHECK(result.colorsRgb.empty());  // format 6 carries no colour
  for (int i = 0; i < kCount; ++i) {
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 0] == Approx(i * 1.0).margin(0.001));
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 1] == Approx(i * 2.0).margin(0.001));
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 2] == Approx(i * 3.0).margin(0.001));
  }
}

TEST_CASE("ReadLazFile: point data record format 7 (extended + RGB) decodes colour",
          "[pointcloud][laz]") {
  const auto path = TempLazPath("gosurvey_test_laz_format7.laz");
  constexpr int kCount = 5;
  constexpr double kScale = 0.01;
  // Format 7 = format 6's 30 bytes + 3x uint16 RGB = 36 bytes.
  auto bytes = MakeHeader(kScale, kScale, kScale, 0, 0, 0, /*format*/ 7, /*recordLength*/ 36,
                          static_cast<std::uint32_t>(kCount));
  for (int i = 0; i < kCount; ++i) {
    AppendFormat6Point(bytes, i * 100, i * 200, i * 300,
                        static_cast<std::uint16_t>(i * 1000), i * 1.5);
    AppendLE(bytes, static_cast<std::uint16_t>(10000 + i));  // R
    AppendLE(bytes, static_cast<std::uint16_t>(20000 + i));  // G
    AppendLE(bytes, static_cast<std::uint16_t>(30000 + i));  // B
  }
  WriteRawFile(path, bytes);

  const auto result = pointcloud_laz::ReadLazFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == kCount);
  REQUIRE(result.colorsRgb.size() == static_cast<std::size_t>(kCount) * 3);
  for (int i = 0; i < kCount; ++i) {
    CHECK(result.colorsRgb[static_cast<std::size_t>(i) * 3 + 0] ==
          Approx(static_cast<float>(10000 + i) / 65535.0f).margin(0.0001f));
  }
}

TEST_CASE("ReadLazFile: a point data record format LASzip cannot represent (>10) is refused by "
          "name, not crashed",
          "[pointcloud][laz]") {
  const auto path = TempLazPath("gosurvey_test_laz_unsupported_format.laz");
  auto bytes = MakeHeader(0.01, 0.01, 0.01, 0, 0, 0, /*format*/ 11, /*recordLength*/ 30,
                          /*count*/ 0);
  WriteRawFile(path, bytes);

  const auto result = pointcloud_laz::ReadLazFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.errorMessage.find("format 11") != std::string::npos);
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadLazFile: a truncated/corrupt compressed stream is refused, not crashed",
          "[pointcloud][laz]") {
  const auto fullPath = TempLazPath("gosurvey_test_laz_trunc_source.laz");
  const auto truncPath = TempLazPath("gosurvey_test_laz_trunc.laz");
  constexpr int kCount = 500;
  std::vector<double> xyz;
  for (int i = 0; i < kCount; ++i) {
    xyz.push_back(static_cast<double>(i) * 0.01);
    xyz.push_back(static_cast<double>(i) * 0.02);
    xyz.push_back(static_cast<double>(i) * 0.03);
  }
  const auto wr = pointcloud_laz::WriteLazFile(fullPath.string(), xyz, {}, {});
  REQUIRE(wr.ok);

  // Chop the file to half its length — for 500 points this lands well inside the compressed
  // point-data section (past the ~300-byte header+VLR), guaranteeing a mid-stream EOF rather
  // than an accidentally-still-valid (shorter) file.
  std::vector<unsigned char> fullBytes;
  {
    std::ifstream in(fullPath, std::ios::binary);
    REQUIRE(in.good());
    fullBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  REQUIRE(fullBytes.size() > 600);  // sanity: big enough that half still lands past the header
  fullBytes.resize(fullBytes.size() / 2);
  WriteRawFile(truncPath, fullBytes);
  RemoveWithRetry(fullPath);

  const auto result = pointcloud_laz::ReadLazFile(truncPath.string());
  RemoveWithRetry(truncPath);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadLazFile: a missing file path is refused without crashing", "[pointcloud][laz]") {
  const auto result = pointcloud_laz::ReadLazFile("Z:\\path\\that\\does\\not\\exist.laz");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadLazFile: an empty path is refused without crashing", "[pointcloud][laz]") {
  const auto result = pointcloud_laz::ReadLazFile("");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
}

TEST_CASE("QuickPointCountEstimate: matches the written point count; 0 for missing/empty",
          "[pointcloud][laz]") {
  const auto path = TempLazPath("gosurvey_test_laz_estimate.laz");
  std::vector<double> xyz = {1, 2, 3, 4, 5, 6, 7, 8, 9};  // 3 points
  const auto wr = pointcloud_laz::WriteLazFile(path.string(), xyz, {}, {});
  REQUIRE(wr.ok);
  CHECK(pointcloud_laz::QuickPointCountEstimate(path.string()) == 3);
  RemoveWithRetry(path);

  CHECK(pointcloud_laz::QuickPointCountEstimate("Z:\\path\\that\\does\\not\\exist.laz") == 0);
  CHECK(pointcloud_laz::QuickPointCountEstimate("") == 0);
}

TEST_CASE("LAS/LAZ cross-check: the same points import equal counts and XYZ (REQ-172)",
          "[pointcloud][laz]") {
  // "LAS fixture": a hand-authored, PLAIN (uncompressed) LAS 1.2 byte buffer — the same byte
  // layout a real .las file has. "LAZ fixture": the identical point set written through
  // WriteLazFile (genuinely LASzip-compressed). Both are read back through the SAME
  // `pointcloud_laz::ReadLazFile` (see this file's header comment for why) and must agree.
  constexpr int kCount = 40;
  std::vector<double> xyz;
  for (int i = 0; i < kCount; ++i) {
    xyz.push_back(100.0 + static_cast<double>(i) * 1.5);
    xyz.push_back(-50.0 + static_cast<double>(i) * 0.75);
    xyz.push_back(10.0 + static_cast<double>(i) * 0.25);
  }

  // Hand-build the plain LAS fixture with the exact same scale/offset convention WriteLazFile
  // itself derives (offset = axis midpoint, scale = 0.0001 since this span is well under the
  // int32-range threshold that would force a coarser scale) so the two fixtures decode to
  // matching raw integers, not just "close" doubles.
  double minX = xyz[0], maxX = xyz[0], minY = xyz[1], maxY = xyz[1], minZ = xyz[2], maxZ = xyz[2];
  for (int i = 0; i < kCount; ++i) {
    minX = std::min(minX, xyz[static_cast<std::size_t>(i) * 3 + 0]);
    maxX = std::max(maxX, xyz[static_cast<std::size_t>(i) * 3 + 0]);
    minY = std::min(minY, xyz[static_cast<std::size_t>(i) * 3 + 1]);
    maxY = std::max(maxY, xyz[static_cast<std::size_t>(i) * 3 + 1]);
    minZ = std::min(minZ, xyz[static_cast<std::size_t>(i) * 3 + 2]);
    maxZ = std::max(maxZ, xyz[static_cast<std::size_t>(i) * 3 + 2]);
  }
  const double offsetX = (minX + maxX) * 0.5, offsetY = (minY + maxY) * 0.5,
               offsetZ = (minZ + maxZ) * 0.5;
  constexpr double kScale = 0.0001;

  auto lasBytes = MakeHeader(kScale, kScale, kScale, offsetX, offsetY, offsetZ, /*format*/ 0,
                             /*recordLength*/ 20, static_cast<std::uint32_t>(kCount), minX, maxX,
                             minY, maxY, minZ, maxZ);
  for (int i = 0; i < kCount; ++i) {
    const std::int32_t rx = static_cast<std::int32_t>(
        std::llround((xyz[static_cast<std::size_t>(i) * 3 + 0] - offsetX) / kScale));
    const std::int32_t ry = static_cast<std::int32_t>(
        std::llround((xyz[static_cast<std::size_t>(i) * 3 + 1] - offsetY) / kScale));
    const std::int32_t rz = static_cast<std::int32_t>(
        std::llround((xyz[static_cast<std::size_t>(i) * 3 + 2] - offsetZ) / kScale));
    AppendFormat0Point(lasBytes, rx, ry, rz, 0);
  }
  const auto lasPath = TempLazPath("gosurvey_test_laz_crosscheck.las_as_bytes");
  WriteRawFile(lasPath, lasBytes);

  const auto lazPath = TempLazPath("gosurvey_test_laz_crosscheck.laz");
  const auto wr = pointcloud_laz::WriteLazFile(lazPath.string(), xyz, {}, {});
  REQUIRE(wr.ok);

  const auto lasResult = pointcloud_laz::ReadLazFile(lasPath.string());
  const auto lazResult = pointcloud_laz::ReadLazFile(lazPath.string());
  RemoveWithRetry(lasPath);
  RemoveWithRetry(lazPath);

  REQUIRE(lasResult.ok);
  REQUIRE(lazResult.ok);
  REQUIRE(lasResult.pointCount() == kCount);
  REQUIRE(lazResult.pointCount() == lasResult.pointCount());
  for (int i = 0; i < kCount; ++i) {
    CHECK(lazResult.pointsXyz[static_cast<std::size_t>(i) * 3 + 0] ==
          Approx(lasResult.pointsXyz[static_cast<std::size_t>(i) * 3 + 0]).margin(0.001));
    CHECK(lazResult.pointsXyz[static_cast<std::size_t>(i) * 3 + 1] ==
          Approx(lasResult.pointsXyz[static_cast<std::size_t>(i) * 3 + 1]).margin(0.001));
    CHECK(lazResult.pointsXyz[static_cast<std::size_t>(i) * 3 + 2] ==
          Approx(lasResult.pointsXyz[static_cast<std::size_t>(i) * 3 + 2]).margin(0.001));
  }
}
