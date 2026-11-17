#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pointcloud {

/// Non-owning view of one bounded point-reader chunk. The referenced vectors remain valid only
/// for the duration of the synchronous reader callback; consumers must not retain these views.
/// XYZ contains complete triples; optional RGB contains the same number of triples, and optional
/// intensity contains one value per point. Readers must preserve these parallel-array invariants.
struct StreamedChunkView {
  const std::vector<double> &pointsXyz;
  const std::vector<float> &colorsRgb;
  const std::vector<float> &intensity;
};

using StreamedChunkCallback = std::function<bool(const StreamedChunkView &)>;
using StreamedPointReader = std::function<bool(
    const std::string &, std::int64_t, const StreamedChunkCallback &, std::string *)>;

}  // namespace pointcloud
