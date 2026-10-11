// E57 point-cloud reader (REQ-172, ADR-042 (d)).
//
// A fixture E57 is written with libE57Format's own Simple Writer rather than committing a binary
// sample to the repo — the acceptance this pins is "N points in, N points out, within REQ-101",
// and the writer/reader pairing is the smallest way to prove that without a real scan on disk.
// Malformed/missing-file refusal is REQ-001's "no partial cloud, no crash" acceptance.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <E57SimpleData.h>
#include <E57SimpleWriter.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "io/CadPointCloudE57.hpp"

using Catch::Approx;

namespace {

std::filesystem::path TempE57Path(const char *name) {
  return std::filesystem::temp_directory_path() / name;
}

/// On Windows, a file handle libE57Format opened internally can outlive the exception it threw
/// by a few milliseconds (the OS releases the handle asynchronously on process/thread cleanup),
/// so a `remove()` immediately after a refused open can race it. This is a test-cleanup detail,
/// not a production code path — `ReadE57File` never deletes anything.
void RemoveWithRetry(const std::filesystem::path &path) {
  std::error_code ec;
  for (int attempt = 0; attempt < 10; ++attempt) {
    std::filesystem::remove(path, ec);
    if (!ec) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

/// Writes a small E57 with `count` points on a line, plus RGB colour, using the vendored writer.
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
  header.colorLimits.colorRedMinimum = 0;
  header.colorLimits.colorRedMaximum = 255;
  header.colorLimits.colorGreenMinimum = 0;
  header.colorLimits.colorGreenMaximum = 255;
  header.colorLimits.colorBlueMinimum = 0;
  header.colorLimits.colorBlueMaximum = 255;

  const std::int64_t scanIndex = writer.NewData3D(header);

  e57::Data3DPointsData_t<double> buffers(header);
  for (int i = 0; i < count; ++i) {
    buffers.cartesianX[i] = static_cast<double>(i);
    buffers.cartesianY[i] = static_cast<double>(i) * 2.0;
    buffers.cartesianZ[i] = static_cast<double>(i) * 3.0;
    buffers.colorRed[i] = static_cast<std::uint16_t>(i % 256);
    buffers.colorGreen[i] = static_cast<std::uint16_t>((i * 2) % 256);
    buffers.colorBlue[i] = static_cast<std::uint16_t>((i * 3) % 256);
  }

  e57::CompressedVectorWriter vectorWriter =
      writer.SetUpData3DPointsData(scanIndex, static_cast<std::size_t>(count), buffers);
  vectorWriter.write(static_cast<std::size_t>(count));
  vectorWriter.close();
  writer.Close();
}

}  // namespace

TEST_CASE("ReadE57File: a known-good fixture round-trips point count and XYZ",
          "[pointcloud][e57]") {
  const auto path = TempE57Path("gosurvey_test_fixture_good.e57");
  constexpr int kCount = 500;
  WriteFixtureE57(path, kCount);

  const auto result = pointcloud_e57::ReadE57File(path.string());
  RemoveWithRetry(path);

  REQUIRE(result.ok);
  REQUIRE(result.pointCount() == kCount);
  REQUIRE(result.pointsXyz.size() == static_cast<std::size_t>(kCount) * 3);
  REQUIRE(result.colorsRgb.size() == static_cast<std::size_t>(kCount) * 3);

  // REQ-101 tolerance (±0.002 ft) is far looser than what a lossless double round-trip needs —
  // this is checking the reader did not silently transpose or scale an axis, not measuring noise.
  for (int i = 0; i < kCount; ++i) {
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 0] == Approx(static_cast<double>(i)));
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 1] ==
          Approx(static_cast<double>(i) * 2.0));
    CHECK(result.pointsXyz[static_cast<std::size_t>(i) * 3 + 2] ==
          Approx(static_cast<double>(i) * 3.0));
  }
  CHECK(result.colorsRgb[3] == Approx(1.0f / 255.0f));  // point 1's red channel
}

TEST_CASE("ReadE57File: a truncated/malformed file is refused, not partially imported",
          "[pointcloud][e57]") {
  const auto path = TempE57Path("gosurvey_test_fixture_garbage.e57");
  {
    std::ofstream out(path, std::ios::binary);
    out << "this is not an E57 file, just garbage bytes to force a parse failure 0123456789";
  }

  const auto result = pointcloud_e57::ReadE57File(path.string());
  RemoveWithRetry(path);

  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadE57File: a missing file path is refused without crashing", "[pointcloud][e57]") {
  const auto result = pointcloud_e57::ReadE57File("Z:\\path\\that\\does\\not\\exist.e57");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
  CHECK(result.pointsXyz.empty());
}

TEST_CASE("ReadE57File: an empty path is refused without crashing", "[pointcloud][e57]") {
  const auto result = pointcloud_e57::ReadE57File("");
  CHECK_FALSE(result.ok);
  CHECK_FALSE(result.errorMessage.empty());
}
