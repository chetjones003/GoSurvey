#include "tinvolume.hpp"

#include "surfacevolume.hpp"

#include <cmath>
#include <map>
#include <utility>

namespace {

std::int64_t QuantizePlan(double v) {
  return static_cast<std::int64_t>(std::llround(v / kTinPlanEpsilon));
}

void CollectVerts(const std::vector<float>& vertsXyz, std::vector<std::pair<double, double>>* xy) {
  if (!xy)
    return;
  const int n = static_cast<int>(vertsXyz.size() / 3);
  xy->reserve(xy->size() + static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    xy->push_back({static_cast<double>(vertsXyz[static_cast<size_t>(i) * 3 + 0]),
                   static_cast<double>(vertsXyz[static_cast<size_t>(i) * 3 + 1])});
  }
}

}  // namespace

TinBuildResult BuildTinVolumeSurface(const std::vector<float>& baseVertsXyz,
                                     const std::vector<std::uint32_t>& baseIndices,
                                     const std::vector<float>& comparisonVertsXyz,
                                     const std::vector<std::uint32_t>& comparisonIndices,
                                     double originX, double originY) {
  TinBuildResult empty;
  empty.status = TinBuildStatus::TooFewPoints;
  empty.message = "No overlapping samples between the base and comparison surfaces.";

  if (baseIndices.size() < 3 || comparisonIndices.size() < 3 || baseVertsXyz.size() < 9 ||
      comparisonVertsXyz.size() < 9)
    return empty;

  const TinSpatialIndex baseIdx = BuildTinSpatialIndex(baseVertsXyz, baseIndices);
  const TinSpatialIndex compIdx = BuildTinSpatialIndex(comparisonVertsXyz, comparisonIndices);
  if (baseIdx.empty() || compIdx.empty())
    return empty;

  std::vector<std::pair<double, double>> candidates;
  CollectVerts(baseVertsXyz, &candidates);
  CollectVerts(comparisonVertsXyz, &candidates);

  std::map<std::pair<std::int64_t, std::int64_t>, TinInputPoint> uniq;
  for (const auto& xy : candidates) {
    double zb = 0.0, zc = 0.0;
    if (!TinElevationAtIndexed(baseVertsXyz, baseIndices, baseIdx, xy.first, xy.second, &zb))
      continue;
    if (!TinElevationAtIndexed(comparisonVertsXyz, comparisonIndices, compIdx, xy.first, xy.second, &zc))
      continue;
    const std::pair<std::int64_t, std::int64_t> key{QuantizePlan(xy.first), QuantizePlan(xy.second)};
    if (uniq.find(key) != uniq.end())
      continue;
    TinInputPoint p;
    p.x = xy.first + originX;
    p.y = xy.second + originY;
    p.z = static_cast<float>(zc - zb);
    uniq.emplace(key, p);
  }

  std::vector<TinInputPoint> pts;
  pts.reserve(uniq.size());
  for (const auto& kv : uniq)
    pts.push_back(kv.second);

  if (pts.size() < 3)
    return empty;
  return BuildTin(pts, {});
}
