#include "GoSurvey/PointCloud/Cache.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main() {
  namespace fs = std::filesystem;
  const fs::path base = fs::temp_directory_path() / "gosurvey_pointcloud_public_consumer";
  const fs::path source = base.string() + ".source";
  const fs::path cache = base.string() + ".gscloud";
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
  std::error_code ignored;
  fs::remove(source, ignored);
  fs::remove(cache, ignored);
  return built && opened.ok ? 0 : 2;
}
