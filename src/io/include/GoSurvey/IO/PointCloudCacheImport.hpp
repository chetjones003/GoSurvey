#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

/// Format-specific import orchestration for the format-neutral PointCloud cache builder.
namespace pointcloudcache_io {

#define GOSURVEY_DECLARE_POINTCLOUD_CACHE_IMPORT(format)                                      \
  [[nodiscard]] bool BuildFrom##format(                                                       \
      const std::string &sourcePath, const std::string &cachePath,                             \
      std::vector<std::string> &progressLog, std::string *errorMessage,                        \
      std::atomic<std::int64_t> *pointsStreamedOut = nullptr,                                  \
      std::atomic<std::int64_t> *pointsFinalizedOut = nullptr,                                \
      const std::atomic<bool> *cancelRequested = nullptr)

GOSURVEY_DECLARE_POINTCLOUD_CACHE_IMPORT(E57);
GOSURVEY_DECLARE_POINTCLOUD_CACHE_IMPORT(Pts);
GOSURVEY_DECLARE_POINTCLOUD_CACHE_IMPORT(Ptx);
GOSURVEY_DECLARE_POINTCLOUD_CACHE_IMPORT(Las);
GOSURVEY_DECLARE_POINTCLOUD_CACHE_IMPORT(Laz);

#undef GOSURVEY_DECLARE_POINTCLOUD_CACHE_IMPORT

}  // namespace pointcloudcache_io
