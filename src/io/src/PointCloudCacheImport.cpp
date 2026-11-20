#include "GoSurvey/IO/PointCloudCacheImport.hpp"

#include "GoSurvey/PointCloud/StreamedChunk.hpp"
#include "GoSurvey/PointCloud/Cache.hpp"
#include "io/src/CadPointCloudE57.hpp"
#include "io/src/CadPointCloudLas.hpp"
#include "io/src/CadPointCloudLaz.hpp"
#include "io/src/CadPointCloudPts.hpp"
#include "io/src/CadPointCloudPtx.hpp"

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

bool BuildFromE57(const std::string& sourcePath, const std::string& cachePath,
                  std::vector<std::string>& progressLog, std::string* errorMessage,
                  std::atomic<std::int64_t>* pointsStreamedOut,
                  std::atomic<std::int64_t>* pointsFinalizedOut,
                  const std::atomic<bool>* cancelRequested) {
  return Build(sourcePath, cachePath, progressLog, errorMessage, pointsStreamedOut,
               pointsFinalizedOut, cancelRequested, pointcloud_e57::StreamE57File,
               "E57 import: contains zero valid points");
}

bool BuildFromPts(const std::string& sourcePath, const std::string& cachePath,
                  std::vector<std::string>& progressLog, std::string* errorMessage,
                  std::atomic<std::int64_t>* pointsStreamedOut,
                  std::atomic<std::int64_t>* pointsFinalizedOut,
                  const std::atomic<bool>* cancelRequested) {
  return Build(sourcePath, cachePath, progressLog, errorMessage, pointsStreamedOut,
               pointsFinalizedOut, cancelRequested, pointcloud_pts::StreamPtsFile,
               "PTS import: contains zero valid points");
}

bool BuildFromPtx(const std::string& sourcePath, const std::string& cachePath,
                  std::vector<std::string>& progressLog, std::string* errorMessage,
                  std::atomic<std::int64_t>* pointsStreamedOut,
                  std::atomic<std::int64_t>* pointsFinalizedOut,
                  const std::atomic<bool>* cancelRequested) {
  return Build(sourcePath, cachePath, progressLog, errorMessage, pointsStreamedOut,
               pointsFinalizedOut, cancelRequested, pointcloud_ptx::StreamPtxFile,
               "PTX import: contains zero valid points");
}

bool BuildFromLas(const std::string& sourcePath, const std::string& cachePath,
                  std::vector<std::string>& progressLog, std::string* errorMessage,
                  std::atomic<std::int64_t>* pointsStreamedOut,
                  std::atomic<std::int64_t>* pointsFinalizedOut,
                  const std::atomic<bool>* cancelRequested) {
  return Build(sourcePath, cachePath, progressLog, errorMessage, pointsStreamedOut,
               pointsFinalizedOut, cancelRequested, pointcloud_las::StreamLasFile,
               "LAS import: contains zero valid points");
}

bool BuildFromLaz(const std::string& sourcePath, const std::string& cachePath,
                  std::vector<std::string>& progressLog, std::string* errorMessage,
                  std::atomic<std::int64_t>* pointsStreamedOut,
                  std::atomic<std::int64_t>* pointsFinalizedOut,
                  const std::atomic<bool>* cancelRequested) {
  return Build(sourcePath, cachePath, progressLog, errorMessage, pointsStreamedOut,
               pointsFinalizedOut, cancelRequested, pointcloud_laz::StreamLazFile,
               "LAZ import: contains zero valid points");
}

}  // namespace pointcloudcache_io
