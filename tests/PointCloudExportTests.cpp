// GitHub issue #916 (REQ-172) — the shared point-cloud export and import-progress helpers both the
// ImGui commands and the Qt dialogs call.
//
// Export is driven the way a user reaches it: the typed EXPORTPTS / EXPORTPTX / EXPORTLAS /
// EXPORTLAZ commands (native save dialog answered through the headless queue) and the
// path-taking ExportPointCloudsToFile the Qt dialog uses. Each format is read back with its own
// reader, so "export, then re-import preserves count and XYZ within REQ-101" is a real assertion.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "HeadlessFileDialogs.hpp"
#include "io/CadPointCloudLas.hpp"
#include "io/CadPointCloudLaz.hpp"
#include "io/CadPointCloudPts.hpp"
#include "io/CadPointCloudPtx.hpp"

using Catch::Approx;

namespace {

std::filesystem::path TempFile(const char* name) { return std::filesystem::temp_directory_path() / name; }

void AddCloud(AppCommandState& st, std::vector<double> xyz, const char* source, std::int64_t total = 0) {
  auto cloud = std::make_shared<CadPointCloud>();
  cloud->pointsXyz = std::move(xyz);
  cloud->sourcePath = source;
  cloud->totalPointCount = total;
  st.cadPointClouds.push_back(cloud);
  st.cadPointCloudAttrs.push_back(EntityAttributes{});
}

void Select(AppCommandState& st, int index) {
  SelectedEntity e;
  e.type = SelectedEntity::Type::PointCloud;
  e.index = index;
  st.selection.assign(1, e);
}

// Three points on 0.125 steps: stored exactly by every writer (LAS/LAZ quantize to 0.001).
const std::vector<double> kCloudA = {10.0, 20.0, 30.0, 11.5, 21.25, 31.125, 12.0, 22.5, 32.0};
const std::vector<double> kCloudB = {500.0, 600.0, 70.0, 501.0, 601.0, 71.0};

void CheckSame(const std::vector<double>& actual, const std::vector<double>& expected) {
  REQUIRE(actual.size() == expected.size());
  for (size_t i = 0; i < expected.size(); ++i) CHECK(actual[i] == Approx(expected[i]).margin(0.002));
}

bool LastLogContains(const std::vector<std::string>& log, const std::string& text) {
  return !log.empty() && log.back().find(text) != std::string::npos;
}

}  // namespace

TEST_CASE("PlanPointCloudExport covers the selection, or every cloud when none is selected",
          "[pointcloud][export][issue916]") {
  AppCommandState st;
  AddCloud(st, kCloudA, "C:/scans/ridge.e57");
  AddCloud(st, kCloudB, "C:/scans/creek.laz", 9000);

  auto all = PlanPointCloudExport(st, PointCloudExportFormat::Las);
  CHECK_FALSE(all.fromSelection);
  CHECK(all.cloudCount == 2);
  CHECK(all.pointCount == 3 + 9000);  // a cache-backed cloud counts its full size, not its preview
  CHECK(all.defaultFileName == "pointclouds.las");

  Select(st, 0);
  auto one = PlanPointCloudExport(st, PointCloudExportFormat::Pts);
  CHECK(one.fromSelection);
  CHECK(one.cloudCount == 1);
  CHECK(one.pointCount == 3);
  CHECK(one.defaultFileName == "ridge.pts");
}

TEST_CASE("ExportPointCloudsToFile writes each format and the points re-read within REQ-101",
          "[pointcloud][export][issue916]") {
  AppCommandState st;
  AddCloud(st, kCloudA, "a.pts");
  AddCloud(st, kCloudB, "b.pts");
  Select(st, 1);
  std::vector<std::string> log;

  const auto pts = TempFile("gosurvey_916_export.pts").string();
  const auto ptx = TempFile("gosurvey_916_export.ptx").string();
  const auto las = TempFile("gosurvey_916_export.las").string();
  const auto laz = TempFile("gosurvey_916_export.laz").string();

  REQUIRE(ExportPointCloudsToFile(st, PointCloudExportFormat::Pts, pts, log));
  CHECK(LastLogContains(log, "EXPORTPTS — wrote 2 point(s) from 1 cloud(s) to gosurvey_916_export.pts."));
  REQUIRE(ExportPointCloudsToFile(st, PointCloudExportFormat::Ptx, ptx, log));
  REQUIRE(ExportPointCloudsToFile(st, PointCloudExportFormat::Las, las, log));
  REQUIRE(ExportPointCloudsToFile(st, PointCloudExportFormat::Laz, laz, log));
  CHECK(LastLogContains(log, "EXPORTLAZ — wrote 2 point(s) from 1 cloud(s)"));

  CheckSame(pointcloud_pts::ReadPtsFile(pts).pointsXyz, kCloudB);
  CheckSame(pointcloud_ptx::ReadPtxFile(ptx).pointsXyz, kCloudB);
  CheckSame(pointcloud_las::ReadLasFile(las).pointsXyz, kCloudB);
  CheckSame(pointcloud_laz::ReadLazFile(laz).pointsXyz, kCloudB);

  // Nothing selected: every cloud goes into the one file.
  st.selection.clear();
  REQUIRE(ExportPointCloudsToFile(st, PointCloudExportFormat::Pts, pts, log));
  CHECK(LastLogContains(log, "wrote 5 point(s) from 2 cloud(s)"));
  CHECK(pointcloud_pts::ReadPtsFile(pts).pointsXyz.size() == 15);

  for (const auto& path : {pts, ptx, las, laz}) std::filesystem::remove(path);
}

TEST_CASE("ExportPointCloudsToFile refuses honestly and writes nothing",
          "[pointcloud][export][issue916]") {
  std::vector<std::string> log;
  AppCommandState empty;
  CHECK_FALSE(ExportPointCloudsToFile(empty, PointCloudExportFormat::Pts, TempFile("gosurvey_916_none.pts").string(), log));
  CHECK(LastLogContains(log, "EXPORTPTS — the drawing has no point clouds."));
  CHECK_FALSE(std::filesystem::exists(TempFile("gosurvey_916_none.pts")));

  AppCommandState st;
  AddCloud(st, kCloudA, "a.pts");
  const auto missingFolder = (TempFile("gosurvey_916_no_such_folder") / "out.las").string();
  CHECK_FALSE(ExportPointCloudsToFile(st, PointCloudExportFormat::Las, missingFolder, log));
  CHECK(LastLogContains(log, "EXPORTLAS — "));
  CHECK_FALSE(std::filesystem::exists(missingFolder));

  AppCommandState hollow;
  AddCloud(hollow, {}, "empty.pts");
  CHECK_FALSE(ExportPointCloudsToFile(hollow, PointCloudExportFormat::Ptx, TempFile("gosurvey_916_hollow.ptx").string(), log));
  CHECK(LastLogContains(log, "EXPORTPTX — the selected point cloud(s) have no points to export."));
}

TEST_CASE("The typed EXPORT commands still ask for a destination and can be cancelled",
          "[pointcloud][export][issue916]") {
  AppCommandState st;
  AddCloud(st, kCloudA, "a.pts");
  std::vector<std::string> log;
  auto run = [&](const char* text) {
    std::string command = text;
    command.push_back('\0');
    ProcessCommandLineSubmit(command.data(), static_cast<int>(command.size()), st, log);
  };

  headless::ClearDialogAnswers();
  const auto target = TempFile("gosurvey_916_typed.las");
  std::filesystem::remove(target);
  headless::QueueDialogAnswer(target.string());
  run("EXPORTLAS");
  CHECK(std::filesystem::exists(target));
  CHECK(LastLogContains(log, "EXPORTLAS — wrote 3 point(s) from 1 cloud(s)"));
  std::filesystem::remove(target);

  headless::QueueDialogCancel();
  run("EXPORTPTS");
  CHECK(LastLogContains(log, "EXPORTPTS — cancelled."));
  CHECK(headless::PendingDialogAnswers() == 0);
}

TEST_CASE("UpdatePointCloudImportProgress reports the real phase, weighted fraction and an ETA",
          "[pointcloud][progress][issue916]") {
  AppCommandState::PointCloudImportAsync job;

  // Unknown total: an indeterminate bar and no ETA text.
  auto unknown = UpdatePointCloudImportProgress(job);
  CHECK(unknown.fraction < 0.f);
  CHECK(unknown.eta.empty());
  CHECK(std::string(unknown.phase) == "Reading scan...");

  job.totalPointsEstimate = 100;
  job.startTime = std::chrono::steady_clock::now() - std::chrono::seconds(60);
  job.pointsStreamed = 50;
  auto reading = UpdatePointCloudImportProgress(job);
  CHECK(std::string(reading.phase) == "Reading scan...");
  CHECK(reading.fraction == Approx(0.175f));
  CHECK(reading.eta.rfind("~", 0) == 0);
  CHECK(reading.eta.find("remaining") != std::string::npos);

  job.pointsStreamed = 100;
  CHECK(std::string(UpdatePointCloudImportProgress(job).phase) == "Building spatial index...");

  job.pointsFinalized = 100;
  job.previewTotalLeaves = 10;
  job.previewLeavesRead = 5;
  auto sampling = UpdatePointCloudImportProgress(job);
  CHECK(std::string(sampling.phase) == "Sampling preview...");
  CHECK(sampling.fraction == Approx(0.9f));
}
