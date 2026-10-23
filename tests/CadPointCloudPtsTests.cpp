// PTS (Cyclone-style ASCII) point-cloud reader/writer (REQ-172, PTS increment —
// `spec/file-format-specs.md` §3.2/§6, delivered after E57 per D-2026-09-17-d).
//
// Fixture PTS files are written as plain text in-code rather than committed binary samples —
// the acceptance this pins is "N points in, N points out, within REQ-101," plus malformed/
// truncated-file refusal (REQ-001's "no partial cloud, no crash").

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "io/CadPointCloudPts.hpp"

using Catch::Approx;

namespace {

std::filesystem::path TempPtsPath(const char *name) {
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

void WriteRawPts(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << contents;
}

}  // namespace

TEST_CASE("ReadPtsFile: a 3-token (X Y Z) fixture round-trips point count and XYZ",
          "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_3tok.pts");
  WriteRawPts(path,
             "4\n"
             "0.0 0.0 0.0\n"
             "1.5 2.5 3.5\n"
             "-10.25 20.5 -0.125\n"
             "100.0 -200.0 300.0\n");

  const auto result = pointcloud_pts::ReadPtsFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == 4);
  CHECK(result.colorsRgb.empty());
  CHECK(result.intensity.empty());
  CHECK(result.pointsXyz[0] == Approx(0.0));
  CHECK(result.pointsXyz[3] == Approx(1.5));
  CHECK(result.pointsXyz[4] == Approx(2.5));
  CHECK(result.pointsXyz[5] == Approx(3.5));
  CHECK(result.pointsXyz[6] == Approx(-10.25));
  CHECK(result.pointsXyz[11] == Approx(300.0));
}

TEST_CASE("ReadPtsFile: a 4-token (X Y Z Intensity) fixture carries intensity, no colour",
          "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_4tok.pts");
  WriteRawPts(path,
             "3\n"
             "1.0 2.0 3.0 0.25\n"
             "4.0 5.0 6.0 0.5\n"
             "7.0 8.0 9.0 1.0\n");

  const auto result = pointcloud_pts::ReadPtsFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == 3);
  CHECK(result.colorsRgb.empty());
  REQUIRE(result.intensity.size() == 3);
  CHECK(result.intensity[0] == Approx(0.25f));
  CHECK(result.intensity[1] == Approx(0.5f));
  CHECK(result.intensity[2] == Approx(1.0f));
}

TEST_CASE("ReadPtsFile: a 7-token (X Y Z Intensity R G B) fixture carries intensity and colour",
          "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_7tok.pts");
  WriteRawPts(path,
             "2\n"
             "1.0 2.0 3.0 0.75 255 128 0\n"
             "4.0 5.0 6.0 0.1 0 64 255\n");

  const auto result = pointcloud_pts::ReadPtsFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == 2);
  REQUIRE(result.intensity.size() == 2);
  REQUIRE(result.colorsRgb.size() == 6);
  CHECK(result.intensity[0] == Approx(0.75f));
  CHECK(result.colorsRgb[0] == Approx(1.0f));        // 255 -> 1.0
  CHECK(result.colorsRgb[1] == Approx(128.0f / 255.0f));
  CHECK(result.colorsRgb[2] == Approx(0.0f));
  CHECK(result.colorsRgb[3] == Approx(0.0f));
  CHECK(result.colorsRgb[5] == Approx(1.0f));        // 255 -> 1.0
}

TEST_CASE("ReadPtsFile: a line with the wrong token count is refused, not partially imported",
          "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_badtokens.pts");
  WriteRawPts(path,
             "2\n"
             "1.0 2.0 3.0\n"
             "4.0 5.0\n");  // only 2 tokens — not 3, 4, or 7

  const auto result = pointcloud_pts::ReadPtsFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadPtsFile: a non-numeric token is refused, not partially imported",
          "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_nonnumeric.pts");
  WriteRawPts(path,
             "2\n"
             "1.0 2.0 3.0\n"
             "four 5.0 6.0\n");

  const auto result = pointcloud_pts::ReadPtsFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadPtsFile: a header count greater than the data lines present (truncated) is refused",
          "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_truncated.pts");
  WriteRawPts(path,
             "5\n"
             "1.0 2.0 3.0\n"
             "4.0 5.0 6.0\n");  // header says 5, only 2 present

  const auto result = pointcloud_pts::ReadPtsFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadPtsFile: a header count smaller than the data lines present (extra data) is refused",
          "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_extradata.pts");
  WriteRawPts(path,
             "1\n"
             "1.0 2.0 3.0\n"
             "4.0 5.0 6.0\n");  // header says 1, 2 lines present

  const auto result = pointcloud_pts::ReadPtsFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
}

TEST_CASE("ReadPtsFile: a zero or negative header count is refused", "[pointcloud][pts]") {
  const auto zeroPath = TempPtsPath("gosurvey_test_pts_zero.pts");
  WriteRawPts(zeroPath, "0\n");
  const auto zeroResult = pointcloud_pts::ReadPtsFile(zeroPath.string());
  RemoveWithRetry(zeroPath);
  CHECK_FALSE(zeroResult.ok);
  CHECK_FALSE(zeroResult.errorMessage.empty());

  const auto negPath = TempPtsPath("gosurvey_test_pts_negative.pts");
  WriteRawPts(negPath, "-3\n1.0 2.0 3.0\n");
  const auto negResult = pointcloud_pts::ReadPtsFile(negPath.string());
  RemoveWithRetry(negPath);
  CHECK_FALSE(negResult.ok);
  CHECK_FALSE(negResult.errorMessage.empty());
}

TEST_CASE("ReadPtsFile: a missing file path is refused without crashing", "[pointcloud][pts]") {
  const auto result = pointcloud_pts::ReadPtsFile("Z:\\path\\that\\does\\not\\exist.pts");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadPtsFile: an empty path is refused without crashing", "[pointcloud][pts]") {
  const auto result = pointcloud_pts::ReadPtsFile("");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
}

TEST_CASE("WritePtsFile + ReadPtsFile: a 3-token write-then-read round trip preserves count and XYZ",
          "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_roundtrip_xyz.pts");
  constexpr int kCount = 250;
  std::vector<double> xyz;
  for (int i = 0; i < kCount; ++i) {
    xyz.push_back(static_cast<double>(i));
    xyz.push_back(static_cast<double>(i) * 2.0);
    xyz.push_back(static_cast<double>(i) * 3.0);
  }

  const auto wr = pointcloud_pts::WritePtsFile(path.string(), xyz, {}, {});
  REQUIRE(wr.ok);
  CHECK(wr.errorMessage.empty());

  const auto result = pointcloud_pts::ReadPtsFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == kCount);
  CHECK(result.colorsRgb.empty());
  CHECK(result.intensity.empty());
  for (int i = 0; i < kCount; ++i) {
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 0] == Approx(static_cast<double>(i)));
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 1] ==
          Approx(static_cast<double>(i) * 2.0));
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 2] ==
          Approx(static_cast<double>(i) * 3.0));
  }
}

TEST_CASE("WritePtsFile + ReadPtsFile: a colour+intensity round trip preserves every channel",
          "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_roundtrip_full.pts");
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

  const auto wr = pointcloud_pts::WritePtsFile(path.string(), xyz, colors, intensity);
  REQUIRE(wr.ok);

  const auto result = pointcloud_pts::ReadPtsFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == kCount);
  REQUIRE(result.colorsRgb.size() == static_cast<std::size_t>(kCount) * 3);
  REQUIRE(result.intensity.size() == static_cast<std::size_t>(kCount));
  for (int i = 0; i < kCount; ++i) {
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 0] ==
          Approx(static_cast<double>(i) * 0.1).margin(1e-6));
    // RGB round-trips through an 8-bit byte (0-255), not a lossless float — tolerance matches that.
    CHECK(result.colorsRgb[static_cast<std::size_t>(i) * 3 + 0] ==
          Approx(colors[static_cast<std::size_t>(i) * 3 + 0]).margin(1.0f / 255.0f));
    CHECK(result.intensity[static_cast<std::size_t>(i)] ==
          Approx(intensity[static_cast<std::size_t>(i)]).margin(1e-4));
  }
}

TEST_CASE("WritePtsFile: an empty point set fails cleanly", "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_empty_write.pts");
  const auto wr = pointcloud_pts::WritePtsFile(path.string(), {}, {}, {});
  CHECK_FALSE(wr.ok);
  CHECK_FALSE(wr.errorMessage.empty());
  CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("QuickPointCountEstimate: reads the header without validating the body",
          "[pointcloud][pts]") {
  const auto path = TempPtsPath("gosurvey_test_pts_estimate.pts");
  WriteRawPts(path, "12345\nthis body is never read by the estimate\n");
  CHECK(pointcloud_pts::QuickPointCountEstimate(path.string()) == 12345);
  RemoveWithRetry(path);

  CHECK(pointcloud_pts::QuickPointCountEstimate("Z:\\path\\that\\does\\not\\exist.pts") == 0);
  CHECK(pointcloud_pts::QuickPointCountEstimate("") == 0);
}
