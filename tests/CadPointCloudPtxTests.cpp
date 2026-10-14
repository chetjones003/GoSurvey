// PTX (Leica Cyclone-style ASCII, per-scan-setup) point-cloud reader/writer (REQ-172, issue #825
// increment 2 — `spec/file-format-specs.md` §3.2/§6, delivered after PTS).
//
// Fixture PTX files are written as plain text in-code rather than committed binary samples — the
// acceptance this pins is "N points in, N points out, within REQ-101," the setup-transform
// hand-check REQ-172's PTX acceptance line calls out by name, and malformed/truncated-file
// refusal (REQ-001's "no partial cloud, no crash").
//
// Transform convention under test (see CadPointCloudPtx.hpp's header comment): row-major 4x4,
// `P_world = M * [x y z 1]^T` — translation in column 3 of rows 0-2, row 3 normally `0 0 0 1`.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "io/CadPointCloudPtx.hpp"

using Catch::Approx;

namespace {

std::filesystem::path TempPtxPath(const char *name) {
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

void WriteRawPtx(const std::filesystem::path &path, const std::string &contents) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << contents;
}

/// Builds one scan block's 10-line header: `ncols`, `nrows`, a fixed scanner position/axes
/// (informational only — never used by the transform math; see CadPointCloudPtx.hpp), then the
/// caller's 4x4 transform (row-major, 4 lines of 4 numbers).
std::string ScanHeader(int ncols, int nrows, const double m[4][4]) {
  std::string s;
  s += std::to_string(ncols) + "\n";
  s += std::to_string(nrows) + "\n";
  s += "0 0 0\n";      // scanner position (unused)
  s += "1 0 0\n";      // scanner X axis (unused)
  s += "0 1 0\n";      // scanner Y axis (unused)
  s += "0 0 1\n";      // scanner Z axis (unused)
  for (int row = 0; row < 4; ++row) {
    s += std::to_string(m[row][0]) + " " + std::to_string(m[row][1]) + " " +
         std::to_string(m[row][2]) + " " + std::to_string(m[row][3]) + "\n";
  }
  return s;
}

constexpr double kIdentity[4][4] = {
    {1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1},
};

}  // namespace

TEST_CASE("ReadPtxFile: a single identity-transform scan round-trips point count and XYZ",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_identity.ptx");
  std::string contents = ScanHeader(4, 1, kIdentity);
  contents += "0.0 0.0 0.0 0.5\n";      // intensity 0.5, so NOT filtered as "no return"
  contents += "1.5 2.5 3.5 1.0\n";
  contents += "-10.25 20.5 -0.125 0.2\n";
  contents += "100.0 -200.0 300.0 0.9\n";
  WriteRawPtx(path, contents);

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == 4);
  CHECK(result.colorsRgb.empty());
  REQUIRE(result.intensity.size() == 4);
  CHECK(result.pointsXyz[0] == Approx(0.0));
  CHECK(result.pointsXyz[3] == Approx(1.5));
  CHECK(result.pointsXyz[4] == Approx(2.5));
  CHECK(result.pointsXyz[5] == Approx(3.5));
  CHECK(result.pointsXyz[6] == Approx(-10.25));
  CHECK(result.pointsXyz[11] == Approx(300.0));
  CHECK(result.intensity[0] == Approx(0.5f));
}

TEST_CASE("ReadPtxFile: a 7-token (X Y Z Intensity R G B) scan carries intensity and colour",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_7tok.ptx");
  std::string contents = ScanHeader(2, 1, kIdentity);
  contents += "1.0 2.0 3.0 0.75 255 128 0\n";
  contents += "4.0 5.0 6.0 0.1 0 64 255\n";
  WriteRawPtx(path, contents);

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == 2);
  REQUIRE(result.intensity.size() == 2);
  REQUIRE(result.colorsRgb.size() == 6);
  CHECK(result.intensity[0] == Approx(0.75f));
  CHECK(result.colorsRgb[0] == Approx(1.0f));  // 255 -> 1.0
  CHECK(result.colorsRgb[1] == Approx(128.0f / 255.0f));
  CHECK(result.colorsRgb[5] == Approx(1.0f));  // 255 -> 1.0
}

TEST_CASE("ReadPtxFile: a non-identity setup transform lands points at the hand-computed world "
          "coordinates (REQ-172 PTX acceptance)",
          "[pointcloud][ptx]") {
  // Translate by (10, 20, 30) plus a 90-degree rotation about Z:
  //   R = [ 0 -1  0 ]      M = [ 0 -1  0  10 ]
  //       [ 1  0  0 ]          [ 1  0  0  20 ]
  //       [ 0  0  1 ]          [ 0  0  1  30 ]
  //                            [ 0  0  0   1 ]
  // P_world = M * [x y z 1]^T, translation in column 3 (CadPointCloudPtx.hpp's convention).
  //   (0,0,0) -> (0*0 -1*0 +0*0 +10,  1*0 +0*0 +0*0 +20,  0*0 +0*0 +1*0 +30) = (10, 20, 30)
  //   (1,0,0) -> (0*1 -1*0 +0*0 +10,  1*1 +0*0 +0*0 +20,  30)                = (10, 21, 30)
  //   (0,1,0) -> (0*0 -1*1 +0*0 +10,  1*0 +0*1 +0*0 +20,  30)                = (9, 20, 30)
  //   (2,3,5) -> (0*2 -1*3 +0*5 +10,  1*2 +0*3 +0*5 +20,  0*2+0*3+1*5+30)    = (7, 22, 35)
  const double m[4][4] = {
      {0, -1, 0, 10},
      {1, 0, 0, 20},
      {0, 0, 1, 30},
      {0, 0, 0, 1},
  };
  const auto path = TempPtxPath("gosurvey_test_ptx_nonidentity.ptx");
  std::string contents = ScanHeader(4, 1, m);
  contents += "0.0 0.0 0.0 0.5\n";  // nonzero intensity: not a "no return" sample
  contents += "1.0 0.0 0.0 1.0\n";
  contents += "0.0 1.0 0.0 1.0\n";
  contents += "2.0 3.0 5.0 1.0\n";
  WriteRawPtx(path, contents);

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == 4);
  CHECK(result.pointsXyz[0] == Approx(10.0));
  CHECK(result.pointsXyz[1] == Approx(20.0));
  CHECK(result.pointsXyz[2] == Approx(30.0));
  CHECK(result.pointsXyz[3] == Approx(10.0));
  CHECK(result.pointsXyz[4] == Approx(21.0));
  CHECK(result.pointsXyz[5] == Approx(30.0));
  CHECK(result.pointsXyz[6] == Approx(9.0));
  CHECK(result.pointsXyz[7] == Approx(20.0));
  CHECK(result.pointsXyz[8] == Approx(30.0));
  CHECK(result.pointsXyz[9] == Approx(7.0));
  CHECK(result.pointsXyz[10] == Approx(22.0));
  CHECK(result.pointsXyz[11] == Approx(35.0));
}

TEST_CASE("ReadPtxFile: two scan blocks are merged into one cloud, each transformed by its own "
          "4x4",
          "[pointcloud][ptx]") {
  const double shiftX10[4][4] = {
      {1, 0, 0, 10},
      {0, 1, 0, 0},
      {0, 0, 1, 0},
      {0, 0, 0, 1},
  };
  const double shiftY100[4][4] = {
      {1, 0, 0, 0},
      {0, 1, 0, 100},
      {0, 0, 1, 0},
      {0, 0, 0, 1},
  };
  const auto path = TempPtxPath("gosurvey_test_ptx_multiscan.ptx");
  std::string contents = ScanHeader(2, 1, shiftX10);
  contents += "0.0 0.0 0.0 1.0\n";
  contents += "1.0 1.0 1.0 1.0\n";
  contents += ScanHeader(1, 1, shiftY100);
  contents += "5.0 5.0 5.0 1.0\n";
  WriteRawPtx(path, contents);

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == 3);
  // First scan: shifted +10 in X.
  CHECK(result.pointsXyz[0] == Approx(10.0));
  CHECK(result.pointsXyz[1] == Approx(0.0));
  CHECK(result.pointsXyz[2] == Approx(0.0));
  CHECK(result.pointsXyz[3] == Approx(11.0));
  CHECK(result.pointsXyz[4] == Approx(1.0));
  CHECK(result.pointsXyz[5] == Approx(1.0));
  // Second scan: shifted +100 in Y.
  CHECK(result.pointsXyz[6] == Approx(5.0));
  CHECK(result.pointsXyz[7] == Approx(105.0));
  CHECK(result.pointsXyz[8] == Approx(5.0));
}

TEST_CASE("ReadPtxFile: a 'no return' row (x=y=z=0, intensity=0) is counted but not emitted",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_noreturn.ptx");
  std::string contents = ScanHeader(4, 1, kIdentity);
  contents += "1.0 2.0 3.0 1.0\n";
  contents += "0.0 0.0 0.0 0.0\n";  // no-return sentinel: must NOT appear in the imported cloud
  contents += "0.0 0.0 0.0 0.0\n";  // a second one
  contents += "4.0 5.0 6.0 1.0\n";
  WriteRawPtx(path, contents);

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  // Header declared 4 grid cells; only 2 are real returns.
  REQUIRE(result.pointCount() == 2);
  CHECK(result.pointCount() < 4);
  CHECK(result.pointsXyz[0] == Approx(1.0));
  CHECK(result.pointsXyz[3] == Approx(4.0));
}

TEST_CASE("ReadPtxFile: a data line with the wrong token count is refused, not partially imported",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_badtokens.ptx");
  std::string contents = ScanHeader(2, 1, kIdentity);
  contents += "1.0 2.0 3.0 1.0\n";
  contents += "4.0 5.0 6.0\n";  // only 3 tokens — PTX requires 4 or 7, never bare XYZ
  WriteRawPtx(path, contents);

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadPtxFile: a non-numeric data token is refused, not partially imported",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_nonnumeric.ptx");
  std::string contents = ScanHeader(2, 1, kIdentity);
  contents += "1.0 2.0 3.0 1.0\n";
  contents += "four 5.0 6.0 1.0\n";
  WriteRawPtx(path, contents);

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadPtxFile: a scan declaring more grid cells than data lines present is refused "
          "(truncated)",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_truncated.ptx");
  std::string contents = ScanHeader(5, 1, kIdentity);  // declares 5, only 2 lines follow
  contents += "1.0 2.0 3.0 1.0\n";
  contents += "4.0 5.0 6.0 1.0\n";
  WriteRawPtx(path, contents);

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadPtxFile: a scan header with a malformed transform row is refused",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_badmatrix.ptx");
  std::string contents;
  contents += "1\n1\n0 0 0\n1 0 0\n0 1 0\n0 0 1\n";
  contents += "1 0 0 0\n0 1 0 0\n0 0 1 0\n";
  contents += "not a valid row\n";  // 4th transform row: wrong token count / non-numeric
  contents += "1.0 2.0 3.0 1.0\n";
  WriteRawPtx(path, contents);

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadPtxFile: a zero or negative ncols/nrows is refused", "[pointcloud][ptx]") {
  const auto zeroPath = TempPtxPath("gosurvey_test_ptx_zero.ptx");
  WriteRawPtx(zeroPath, "0\n1\n0 0 0\n1 0 0\n0 1 0\n0 0 1\n1 0 0 0\n0 1 0 0\n0 0 1 0\n0 0 0 1\n");
  const auto zeroResult = pointcloud_ptx::ReadPtxFile(zeroPath.string());
  RemoveWithRetry(zeroPath);
  CHECK_FALSE(zeroResult.ok);
  CHECK_FALSE(zeroResult.errorMessage.empty());

  const auto negPath = TempPtxPath("gosurvey_test_ptx_negative.ptx");
  WriteRawPtx(negPath, "-3\n1\n0 0 0\n1 0 0\n0 1 0\n0 0 1\n1 0 0 0\n0 1 0 0\n0 0 1 0\n0 0 0 1\n");
  const auto negResult = pointcloud_ptx::ReadPtxFile(negPath.string());
  RemoveWithRetry(negPath);
  CHECK_FALSE(negResult.ok);
  CHECK_FALSE(negResult.errorMessage.empty());
}

TEST_CASE("ReadPtxFile: a missing file path is refused without crashing", "[pointcloud][ptx]") {
  const auto result = pointcloud_ptx::ReadPtxFile("Z:\\path\\that\\does\\not\\exist.ptx");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadPtxFile: an empty path is refused without crashing", "[pointcloud][ptx]") {
  const auto result = pointcloud_ptx::ReadPtxFile("");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
}

TEST_CASE("ReadPtxFile: an empty file (no scan blocks) is refused without crashing",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_emptyfile.ptx");
  WriteRawPtx(path, "");
  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
}

TEST_CASE("WritePtxFile + ReadPtxFile: a write-then-read round trip preserves count and XYZ "
          "(REQ-172 PTX acceptance)",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_roundtrip_xyz.ptx");
  constexpr int kCount = 250;
  std::vector<double> xyz;
  for (int i = 0; i < kCount; ++i) {
    xyz.push_back(static_cast<double>(i));
    xyz.push_back(static_cast<double>(i) * 2.0);
    xyz.push_back(static_cast<double>(i) * 3.0);
  }

  const auto wr = pointcloud_ptx::WritePtxFile(path.string(), xyz, {}, {});
  REQUIRE(wr.ok);
  CHECK(wr.errorMessage.empty());

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == kCount);
  CHECK(result.colorsRgb.empty());
  for (int i = 0; i < kCount; ++i) {
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 0] == Approx(static_cast<double>(i)));
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 1] ==
          Approx(static_cast<double>(i) * 2.0));
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 2] ==
          Approx(static_cast<double>(i) * 3.0));
  }
}

TEST_CASE("WritePtxFile + ReadPtxFile: a colour+intensity round trip preserves every channel",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_roundtrip_full.ptx");
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
    // +1 so point 0 (x=y=z=0) never also gets intensity exactly 0 — that exact combination is
    // PTX's "no return" sentinel (CadPointCloudPtx.hpp), which this round trip is not testing.
    intensity.push_back(static_cast<float>(i % 100 + 1) / 100.0f);
  }

  const auto wr = pointcloud_ptx::WritePtxFile(path.string(), xyz, colors, intensity);
  REQUIRE(wr.ok);

  const auto result = pointcloud_ptx::ReadPtxFile(path.string());
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

TEST_CASE("WritePtxFile: an empty point set fails cleanly", "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_empty_write.ptx");
  const auto wr = pointcloud_ptx::WritePtxFile(path.string(), {}, {}, {});
  CHECK_FALSE(wr.ok);
  CHECK_FALSE(wr.errorMessage.empty());
  CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("QuickPointCountEstimate: reads the first scan's ncols/nrows without validating the "
          "body",
          "[pointcloud][ptx]") {
  const auto path = TempPtxPath("gosurvey_test_ptx_estimate.ptx");
  WriteRawPtx(path, "111\n3\nthis body is never read by the estimate\n");
  CHECK(pointcloud_ptx::QuickPointCountEstimate(path.string()) == 333);
  RemoveWithRetry(path);

  CHECK(pointcloud_ptx::QuickPointCountEstimate("Z:\\path\\that\\does\\not\\exist.ptx") == 0);
  CHECK(pointcloud_ptx::QuickPointCountEstimate("") == 0);
}
