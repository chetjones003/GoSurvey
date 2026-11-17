#include "GoSurvey/PointCloud/Cache.hpp"

#include <chrono>
#include <filesystem>
#include <random>
#include <fstream>
#include <string>
#include <vector>

int main() {
  namespace fs = std::filesystem;
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                      "-" + std::to_string(std::random_device{}());
  const fs::path base = fs::temp_directory_path() / ("gosurvey_pointcloud_consumer-" + unique);
  std::error_code createError;
  if (!fs::create_directory(base, createError)) return 1;
  struct Cleanup {
    fs::path directory;
    ~Cleanup() { std::error_code ignored; fs::remove_all(directory, ignored); }
  } cleanup{base};
  const fs::path source = base / "points.source";
  const fs::path cache = base / "points.gscloud";
  {
    std::ofstream file(source, std::ios::binary | std::ios::trunc);
    file << "fixture";
    if (!file) return 1;
  }

  const std::vector<double> xyz{1.0, 2.0, 3.0};
  const std::vector<float> colors;
  const std::vector<float> intensity;
  pointcloud::StreamedPointReader reader = [&](const std::string &, std::int64_t,
                                                const pointcloud::StreamedChunkCallback &callback,
                                                std::string *) {
    return callback({xyz, colors, intensity});
  };
  std::vector<std::string> progress;
  std::string error;
  const bool built = pointcloudcache::BuildFromStreamedSource(
      source.string(), cache.string(), progress, &error, nullptr, nullptr, nullptr,
      reader, "fixture contains zero points");
  const auto opened = pointcloudcache::Open(cache.string());
  return built && opened.ok ? 0 : 2;
}
