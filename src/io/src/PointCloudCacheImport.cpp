#include "GoSurvey/IO/PointCloudCacheImport.hpp"

#include "GoSurvey/PointCloud/StreamedChunk.hpp"
#include "GoSurvey/PointCloud/Cache.hpp"
#include "io/CadPointCloudE57.hpp"
#include "io/CadPointCloudLas.hpp"
#include "io/CadPointCloudLaz.hpp"
#include "io/CadPointCloudPts.hpp"
#include "io/CadPointCloudPtx.hpp"

namespace pointcloudcache_io {
namespace {

template <typename Reader>
pointcloud::StreamedPointReader Adapt(Reader reader) {
  return [reader](const std::string &path, std::int64_t budget,
                  const pointcloud::StreamedChunkCallback &onChunk,
                  std::string *errorMessage) {
    return reader(path, budget, [&](const auto &chunk) {
      return onChunk({chunk.pointsXyz, chunk.colorsRgb, chunk.intensity});
    }, errorMessage);
  };
}

template <typename Reader>
bool Build(const std::string &sourcePath, const std::string &cachePath,
           std::vector<std::string> &progressLog, std::string *errorMessage,
           std::atomic<std::int64_t> *pointsStreamedOut,
           std::atomic<std::int64_t> *pointsFinalizedOut,
           const std::atomic<bool> *cancelRequested, Reader reader,
           const char *zeroPointsMessage) {
  return pointcloudcache::BuildFromStreamedSource(
      sourcePath, cachePath, progressLog, errorMessage, pointsStreamedOut,
      pointsFinalizedOut, cancelRequested, Adapt(reader), zeroPointsMessage);
}

}  // namespace

#define GOSURVEY_DEFINE_POINTCLOUD_CACHE_IMPORT(format, reader, zeroMessage)                   \
  bool BuildFrom##format(const std::string &sourcePath, const std::string &cachePath,          \
                         std::vector<std::string> &progressLog, std::string *errorMessage,     \
                         std::atomic<std::int64_t> *pointsStreamedOut,                         \
                         std::atomic<std::int64_t> *pointsFinalizedOut,                       \
                         const std::atomic<bool> *cancelRequested) {                           \
    return Build(sourcePath, cachePath, progressLog, errorMessage, pointsStreamedOut,           \
                 pointsFinalizedOut, cancelRequested, reader, zeroMessage);                    \
  }

GOSURVEY_DEFINE_POINTCLOUD_CACHE_IMPORT(E57, pointcloud_e57::StreamE57File,
                                        "E57 import: contains zero valid points")
GOSURVEY_DEFINE_POINTCLOUD_CACHE_IMPORT(Pts, pointcloud_pts::StreamPtsFile,
                                        "PTS import: contains zero valid points")
GOSURVEY_DEFINE_POINTCLOUD_CACHE_IMPORT(Ptx, pointcloud_ptx::StreamPtxFile,
                                        "PTX import: contains zero valid points")
GOSURVEY_DEFINE_POINTCLOUD_CACHE_IMPORT(Las, pointcloud_las::StreamLasFile,
                                        "LAS import: contains zero valid points")
GOSURVEY_DEFINE_POINTCLOUD_CACHE_IMPORT(Laz, pointcloud_laz::StreamLazFile,
                                        "LAZ import: contains zero valid points")

#undef GOSURVEY_DEFINE_POINTCLOUD_CACHE_IMPORT

}  // namespace pointcloudcache_io
