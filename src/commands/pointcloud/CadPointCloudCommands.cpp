#include "CadCommandsInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include "GoSurvey/PointCloud/Octree.hpp"
#include "GoSurvey/PointCloud/CylinderFit.hpp"
#include "GoSurvey/IO/PointCloudCacheImport.hpp"
#include <GoSurvey/UI/FileDialogs.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/// the LOD renderer pages real detail from the `.gscloud` cache directly. Keeping this bounded is
/// what keeps the *entity itself* (and every undo snapshot sharing its pointer) cheap regardless of
/// source scan size.
constexpr std::int64_t kPointCloudPreviewCap = 2'000'000;

/// Reads a spatially-representative subset of `cache`'s points, at most `previewCap` of them, by
/// taking every `stride`-th point from EVERY leaf (stride chosen so the total lands near the cap)
/// — sampling per leaf rather than only the first N leaves is what keeps the preview covering the
/// whole cloud instead of just whatever octree region happened to be visited first.
///
/// This is a full read-back of the cache (every leaf, even though only a fraction of each is
/// kept) — a real pass with a real cost, so `leavesReadOut`/`totalLeavesOut` (both optional) exist
/// to report it: earlier this phase had NO progress signal at all, which read as "stuck at 100%"
/// while the worker was genuinely still working (TASK-270 log).
void BuildPreviewFromCache(const pointcloudcache::OpenCache& cache, std::int64_t previewCap,
                           std::vector<double>& outXyz, std::vector<float>& outColors,
                           std::vector<float>& outIntensity,
                           std::atomic<std::int64_t>* leavesReadOut = nullptr,
                           std::atomic<std::int64_t>* totalLeavesOut = nullptr) {
  const std::int64_t stride =
      std::max<std::int64_t>(1, cache.totalPointCount / std::max<std::int64_t>(1, previewCap));
  if (totalLeavesOut) {
    std::int64_t leafCount = 0;
    for (const auto& n : cache.octree.nodes)
      if (n.isLeaf()) ++leafCount;
    totalLeavesOut->store(leafCount, std::memory_order_relaxed);
  }
  for (int i = 0; i < static_cast<int>(cache.octree.nodes.size()); ++i) {
    if (!cache.octree.nodes[static_cast<size_t>(i)].isLeaf()) continue;
    std::vector<double> leafXyz;
    std::vector<float> leafColors, leafIntensity;
    if (pointcloudcache::ReadLeafPoints(cache, i, leafXyz, leafColors, leafIntensity)) {
      const std::int64_t n = static_cast<std::int64_t>(leafXyz.size() / 3);
      for (std::int64_t p = 0; p < n; p += stride) {
        outXyz.push_back(leafXyz[static_cast<size_t>(p) * 3 + 0]);
        outXyz.push_back(leafXyz[static_cast<size_t>(p) * 3 + 1]);
        outXyz.push_back(leafXyz[static_cast<size_t>(p) * 3 + 2]);
        if (cache.hasColor) {
          outColors.push_back(leafColors[static_cast<size_t>(p) * 3 + 0]);
          outColors.push_back(leafColors[static_cast<size_t>(p) * 3 + 1]);
          outColors.push_back(leafColors[static_cast<size_t>(p) * 3 + 2]);
        }
        if (cache.hasIntensity) outIntensity.push_back(leafIntensity[static_cast<size_t>(p)]);
      }
    }
    if (leavesReadOut) leavesReadOut->fetch_add(1, std::memory_order_relaxed);
  }
}

/// The background half of `StartPointCloudImportAsync` (REQ-172 / ADR-060, D-2026-09-17-d — E57
/// delivered first). Touches ONLY `*job`'s own fields plus the read-only source/cache paths —
/// never `AppCommandState` — so it is safe to run on a worker thread while the main thread keeps
/// using `st` for everything else (architecture §8's one-shot-worker contract).
void RunPointCloudImportWorker(AppCommandState::PointCloudImportAsync* job) {
  std::string errorMessage;
  bool built = true;
  if (pointcloudcache::MatchesSource(job->cachePath, job->sourcePath)) {
    built = true;  // reuse (ADR-060 (b)) — nothing to build
  } else {
    std::vector<std::string> buildLog;  // human-readable lines; not surfaced yet (numeric progress
                                        // is what the UI polls) but kept for a future log dump.
    using Format = AppCommandState::PointCloudImportAsync::SourceFormat;
    if (job->format == Format::Pts) {
      built = pointcloudcache_io::BuildFromPts(job->sourcePath, job->cachePath, buildLog,
                                            &errorMessage, &job->pointsStreamed,
                                            &job->pointsFinalized, &job->cancelRequested);
    } else if (job->format == Format::Ptx) {
      built = pointcloudcache_io::BuildFromPtx(job->sourcePath, job->cachePath, buildLog,
                                            &errorMessage, &job->pointsStreamed,
                                            &job->pointsFinalized, &job->cancelRequested);
    } else if (job->format == Format::Las) {
      built = pointcloudcache_io::BuildFromLas(job->sourcePath, job->cachePath, buildLog,
                                            &errorMessage, &job->pointsStreamed,
                                            &job->pointsFinalized, &job->cancelRequested);
    } else if (job->format == Format::Laz) {
      built = pointcloudcache_io::BuildFromLaz(job->sourcePath, job->cachePath, buildLog,
                                            &errorMessage, &job->pointsStreamed,
                                            &job->pointsFinalized, &job->cancelRequested);
    } else {
      built = pointcloudcache_io::BuildFromE57(job->sourcePath, job->cachePath, buildLog,
                                            &errorMessage, &job->pointsStreamed,
                                            &job->pointsFinalized, &job->cancelRequested);
    }
  }
  if (!built) {
    job->ok = false;
    job->errorMessage = errorMessage.empty() ? "point-cloud cache build failed" : errorMessage;
    job->done.store(true, std::memory_order_release);
    return;
  }

  const pointcloudcache::OpenResult opened = pointcloudcache::Open(job->cachePath);
  if (!opened.ok) {
    job->ok = false;
    job->errorMessage = opened.errorMessage;
    job->done.store(true, std::memory_order_release);
    return;
  }

  job->octree = opened.cache.octree;
  job->totalPointCount = opened.cache.totalPointCount;
  BuildPreviewFromCache(opened.cache, kPointCloudPreviewCap, job->previewXyz, job->previewColors,
                        job->previewIntensity, &job->previewLeavesRead, &job->previewTotalLeaves);
  job->ok = !job->previewXyz.empty();
  if (!job->ok) job->errorMessage = "internal error: preview sample came back empty";
  job->done.store(true, std::memory_order_release);
}


void TickPointCloudImport(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.pointCloudImportAsync) return;
  AppCommandState::PointCloudImportAsync& job = *st.pointCloudImportAsync;
  if (!job.done.load(std::memory_order_acquire)) return;
  job.thread.join();

  if (!job.ok) {
    log.push_back("POINTCLOUDATTACH — " + job.errorMessage);
    st.pointCloudImportAsync.reset();
    if (st.bench.pointCloudImportPending)
      InstallPointCloudBenchScene(st, log);  // no cloud landed — restores the bench-cleared stores
    return;
  }

  auto cloud = std::make_shared<CadPointCloud>();
  cloud->sourcePath = job.sourcePath;
  cloud->cloudCachePath = job.cachePath;
  cloud->octree = std::move(job.octree);
  cloud->totalPointCount = job.totalPointCount;
  cloud->pointsXyz = std::move(job.previewXyz);
  cloud->colorsRgb = std::move(job.previewColors);
  cloud->intensity = std::move(job.previewIntensity);

  PushUndoSnapshot(st, "Import point cloud");
  st.drawing.cadPointClouds.push_back(cloud);
  st.drawing.cadPointCloudAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);

  char msg[384];
  std::snprintf(msg, sizeof(msg),
                "Imported %s — %lld points (out-of-core; %lld in preview sample).",
                std::filesystem::u8path(job.sourcePath).filename().u8string().c_str(),
                static_cast<long long>(cloud->totalPointCount),
                static_cast<long long>(cloud->pointCount()));
  log.push_back(msg);
  st.pointCloudImportAsync.reset();
  if (st.bench.pointCloudImportPending)
    InstallPointCloudBenchScene(st, log);  // BENCH POINTCLOUD was waiting on this import
}

namespace {
/// "~2m 30s remaining" / "~45s remaining" — never a negative or absurd figure: `seconds` is
/// clamped to a sane cap so an early, noisy fraction estimate (e.g. the first tick of phase 2,
/// before enough points have finalized to trust the extrapolation) does not print "~11 hours."
std::string FormatEtaRemaining(double seconds) {
  if (!(seconds > 0.0)) return "estimating...";
  seconds = std::min(seconds, 24.0 * 3600.0);
  const int totalSec = static_cast<int>(seconds);
  if (totalSec < 60) return "~" + std::to_string(totalSec) + "s remaining";
  const int mins = totalSec / 60;
  const int secs = totalSec % 60;
  return "~" + std::to_string(mins) + "m " + std::to_string(secs) + "s remaining";
}
}  // namespace

PointCloudImportProgress UpdatePointCloudImportProgress(AppCommandState::PointCloudImportAsync& job) {
  const std::int64_t total = job.totalPointsEstimate;
  const std::int64_t streamed = job.pointsStreamed.load(std::memory_order_relaxed);
  const std::int64_t finalized = job.pointsFinalized.load(std::memory_order_relaxed);
  const std::int64_t leavesRead = job.previewLeavesRead.load(std::memory_order_relaxed);
  const std::int64_t totalLeaves = job.previewTotalLeaves.load(std::memory_order_relaxed);

  // Three real phases, in strict sequence (see pointcloudcache_io::BuildFromE57 and
  // RunPointCloudImportWorker): streaming completes fully before the recursive octree split
  // starts, which completes fully before the preview sample is read back. Phase 3 was invisible
  // before this fix — it re-reads the WHOLE cache off disk to subsample it, a real full pass with
  // no progress signal at all, which is exactly what read as "stuck at 100%" (TASK-270 log): the
  // bar itself was reporting phase 1+2 as 100% while the worker kept working on phase 3.
  PointCloudImportProgress progress;
  const bool phase1Done = total > 0 && streamed >= total;
  const bool phase2Done = phase1Done && finalized >= total;
  if (phase2Done) progress.phase = "Sampling preview...";
  else if (phase1Done) progress.phase = "Building spatial index...";

  // Weighted by rough relative cost, not by point count: phase 2 re-reads/re-writes the dataset
  // across several octree levels (the slowest part), phase 3 is one more full read-back.
  if (total > 0) {
    const float p1 = std::min(1.f, static_cast<float>(streamed) / static_cast<float>(total));
    const float p2 = std::min(1.f, static_cast<float>(finalized) / static_cast<float>(total));
    const float p3 = totalLeaves > 0
                         ? std::min(1.f, static_cast<float>(leavesRead) / static_cast<float>(totalLeaves))
                         : 0.f;
    progress.fraction = 0.35f * p1 + 0.45f * p2 + 0.20f * p3;
  }

  // Recomputed at most once a second — every-frame recomputation made the countdown jitter rather
  // than tick like a clock, which is what a reader expects from a "time remaining" line.
  const auto now = std::chrono::steady_clock::now();
  if (total > 0 && std::chrono::duration<double>(now - job.lastEtaUpdate).count() >= 1.0) {
    const double elapsed = std::chrono::duration<double>(now - job.startTime).count();
    // Only trust the extrapolation once there is real progress to extrapolate FROM — an ETA
    // computed from the first half-percent of a multi-hour build is noise, not information.
    job.cachedEtaText = progress.fraction > 0.005f
                            ? FormatEtaRemaining(elapsed * (1.0 - progress.fraction) / progress.fraction)
                            : "estimating...";
    job.lastEtaUpdate = now;
  }
  if (total > 0) progress.eta = job.cachedEtaText;
  return progress;
}

/// The clouds an export covers: the selected ones, or every cloud in the drawing when none is
/// selected (REQ-172: "Export writes the selected cloud(s) (or all, when none selected)").
std::set<int> ResolvePointCloudsForExport(const AppCommandState& st) {
  std::set<int> resolved;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::PointCloud && e.index >= 0 &&
        static_cast<size_t>(e.index) < st.drawing.cadPointClouds.size())
      resolved.insert(e.index);
  }
  if (resolved.empty()) {
    for (int i = 0; i < static_cast<int>(st.drawing.cadPointClouds.size()); ++i) resolved.insert(i);
  }
  return resolved;
}

/// Shared by \ref ExportPointCloudsToFile: resolves the
/// selected `CadPointCloud`(s) from `st.selection`, or every cloud in the drawing when none is
/// selected (REQ-172 acceptance text: "Export writes the selected cloud(s) (or all, when none
/// selected — stated in the command)"), and reads back every point each resolved cloud actually
/// has — the full `.gscloud` out-of-core cache when one exists (every leaf, not the bounded
/// preview sample), else the cloud's own in-memory preview — merged into one flat point set.
///
/// Decided once across every resolved cloud (same "decide once, default-fill the rest" tolerance
/// `BuildFromE57`/`StreamPtsFile`/`StreamPtxFile`/`StreamLasFile`/`StreamLazFile` already use for
/// a mixed-schema source) — a channel is carried in the merged output if ANY resolved cloud has
/// it; a cloud lacking that channel gets zero-filled.
void CollectSelectedPointCloudsForExport(AppCommandState& st, std::set<int>& resolved,
                                         std::vector<double>& xyz, std::vector<float>& colors,
                                         std::vector<float>& intensity,
                                         std::int64_t& totalCount) {
  resolved = ResolvePointCloudsForExport(st);

  bool anyColor = false, anyIntensity = false;
  for (int idx : resolved) {
    const auto& cloud = st.drawing.cadPointClouds[static_cast<size_t>(idx)];
    if (cloud->hasOutOfCoreCache()) {
      const pointcloudcache::OpenResult opened = pointcloudcache::Open(cloud->cloudCachePath);
      if (opened.ok) {
        anyColor = anyColor || opened.cache.hasColor;
        anyIntensity = anyIntensity || opened.cache.hasIntensity;
        continue;
      }
    }
    anyColor = anyColor || cloud->hasColor();
    anyIntensity = anyIntensity || cloud->hasIntensity();
  }

  totalCount = 0;
  for (int idx : resolved) {
    const auto& cloud = st.drawing.cadPointClouds[static_cast<size_t>(idx)];
    std::vector<double> cloudXyz;
    std::vector<float> cloudColors, cloudIntensity;
    bool cloudHasColor = false, cloudHasIntensity = false;
    if (cloud->hasOutOfCoreCache()) {
      const pointcloudcache::OpenResult opened = pointcloudcache::Open(cloud->cloudCachePath);
      if (opened.ok) {
        // previewCap == totalPointCount makes BuildPreviewFromCache's stride compute to 1 — a
        // full read of every leaf, reused rather than writing a second "read everything" loop.
        BuildPreviewFromCache(opened.cache, std::max<std::int64_t>(1, opened.cache.totalPointCount),
                              cloudXyz, cloudColors, cloudIntensity);
        cloudHasColor = opened.cache.hasColor;
        cloudHasIntensity = opened.cache.hasIntensity;
      }
    }
    if (cloudXyz.empty()) {  // no cache (small cloud) or the cache failed to open: fall back
      cloudXyz = cloud->pointsXyz;
      cloudColors = cloud->colorsRgb;
      cloudIntensity = cloud->intensity;
      cloudHasColor = cloud->hasColor();
      cloudHasIntensity = cloud->hasIntensity();
    }

    const std::int64_t n = static_cast<std::int64_t>(cloudXyz.size() / 3);
    xyz.insert(xyz.end(), cloudXyz.begin(), cloudXyz.end());
    if (anyColor) {
      if (cloudHasColor && cloudColors.size() == static_cast<size_t>(n) * 3)
        colors.insert(colors.end(), cloudColors.begin(), cloudColors.end());
      else
        colors.insert(colors.end(), static_cast<size_t>(n) * 3, 0.0f);
    }
    if (anyIntensity) {
      if (cloudHasIntensity && cloudIntensity.size() == static_cast<size_t>(n))
        intensity.insert(intensity.end(), cloudIntensity.begin(), cloudIntensity.end());
      else
        intensity.insert(intensity.end(), static_cast<size_t>(n), 0.0f);
    }
    totalCount += n;
  }
}


const char* PointCloudExportCommandName(PointCloudExportFormat format) {
  switch (format) {
    case PointCloudExportFormat::Pts: return "EXPORTPTS";
    case PointCloudExportFormat::Ptx: return "EXPORTPTX";
    case PointCloudExportFormat::Las: return "EXPORTLAS";
    case PointCloudExportFormat::Laz: return "EXPORTLAZ";
  }
  return "EXPORTPTS";
}


PointCloudExportPlan PlanPointCloudExport(const AppCommandState& st, PointCloudExportFormat format) {
  PointCloudExportPlan plan;
  const std::set<int> resolved = ResolvePointCloudsForExport(st);
  plan.cloudCount = static_cast<int>(resolved.size());
  plan.fromSelection = false;
  for (const auto& e : st.selection)
    if (e.type == SelectedEntity::Type::PointCloud && e.index >= 0 &&
        static_cast<size_t>(e.index) < st.drawing.cadPointClouds.size())
      plan.fromSelection = true;
  for (int idx : resolved) {
    const auto& cloud = st.drawing.cadPointClouds[static_cast<size_t>(idx)];
    plan.pointCount += cloud->totalPointCount > 0 ? cloud->totalPointCount : cloud->pointCount();
  }
  const char* ext = format == PointCloudExportFormat::Pts ? ".pts"
                  : format == PointCloudExportFormat::Ptx ? ".ptx"
                  : format == PointCloudExportFormat::Las ? ".las" : ".laz";
  plan.defaultFileName =
      resolved.size() == 1
          ? std::filesystem::u8path(st.drawing.cadPointClouds[static_cast<size_t>(*resolved.begin())]->sourcePath)
                    .stem().u8string() + ext
          : std::string("pointclouds") + ext;
  return plan;
}

/// REQ-172 export (PTS issue #825 increment 1, PTX 2, LAS 3, LAZ 4) — the one writer behind
/// `EXPORTPTS` / `EXPORTPTX` / `EXPORTLAS` / `EXPORTLAZ` and the Qt export dialog, which differ
/// only in how \p pathUtf8 is chosen. See \ref CollectSelectedPointCloudsForExport for cloud
/// resolution; the merged points are written to ONE file.
///
/// **Documented choice, not a spec requirement either way**: multiple resolved clouds are
/// concatenated into a single file rather than written one-file-per-cloud (PTX: into one scan
/// block with an identity setup transform — `WritePtxFile`'s documented choice). These formats
/// have no per-cloud structure beyond a flat point list, and REQ-172's acceptance text only
/// exercises the single-cloud case.

bool PointCloudVisible(const AppCommandState& st, size_t index) {
  if (index >= st.drawing.cadPointClouds.size())
    return false;
  if (!st.drawing.cadPointClouds[index])
    return false;
  if (index >= st.drawing.cadPointCloudAttrs.size())
    return true;  // attrs are length-locked to cadPointClouds; short array means defaults, not hidden
  const EntityAttributes& a = st.drawing.cadPointCloudAttrs[index];
  // REQ-084 (d): an isolated-out cloud is invisible, so it must not be drawn OR answer a click.
  if (CadEntityIdHidden(&st.hiddenEntityIds, a.id))
    return false;
  const CadLayerRow* lr = FindDrawingLayerRowCi(st, a.layer);
  return !(lr && (!lr->on || lr->frozen));
}

void StartExtractCenterlineCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.active = AppCommandState::Kind::ExtractCenterline;
  st.lastCommand = AppCommandState::Kind::ExtractCenterline;
  st.extractCenterlineHoverValid = false;
  st.extractCenterlineHoverDiag = "not hovering yet";
  st.pointCloudOpenCaches.clear();
  st.extractCenterlineLastQueryValid = false;
  st.extractCenterlineLastQueryCloudIndex = -1;
  log.push_back("EXTRACTCENTERLINE — hover a scanned pipe/pole/column and click to place its "
                "centerline (Esc to cancel).");
}

const pointcloudcache::OpenCache* GetOrOpenPointCloudCache(AppCommandState& st, int cloudIdx) {
  const CadPointCloud& pc = *st.drawing.cadPointClouds[static_cast<size_t>(cloudIdx)];
  for (const auto& entry : st.pointCloudOpenCaches)
    if (entry.cloudIndex == cloudIdx && entry.cachePath == pc.cloudCachePath)
      return entry.ok ? &entry.cache : nullptr;
  // No entry, or the cloud at this index was re-imported/replaced since the last open (a
  // different `cloudCachePath`) — drop any stale entry for this index and open fresh.
  st.pointCloudOpenCaches.erase(
      std::remove_if(st.pointCloudOpenCaches.begin(), st.pointCloudOpenCaches.end(),
                     [&](const auto& e) { return e.cloudIndex == cloudIdx; }),
      st.pointCloudOpenCaches.end());
  AppCommandState::PointCloudOpenCacheEntry entry;
  entry.cloudIndex = cloudIdx;
  entry.cachePath = pc.cloudCachePath;
  const pointcloudcache::OpenResult opened = pointcloudcache::Open(pc.cloudCachePath);
  entry.ok = opened.ok;
  if (opened.ok)
    entry.cache = opened.cache;
  st.pointCloudOpenCaches.push_back(std::move(entry));
  return st.pointCloudOpenCaches.back().ok ? &st.pointCloudOpenCaches.back().cache : nullptr;
}

// Reads every point within `radius` of (cx,cy,cz) from `cache`'s leaves near that point (ADR-060) —
// real scan density, not the bounded REQ-171 preview sample.
static void ReadExtractCenterlineNeighborhood(const pointcloudcache::OpenCache& cache, double cx,
                                              double cy, double cz, double radius,
                                              std::vector<double>& outNeighborhood) {
  const std::vector<std::int32_t> leaves =
      pointcloud::QueryLeavesNearPoint(cache.octree, cx, cy, cz, radius);
  const double r2 = radius * radius;
  std::vector<double> leafXyz;
  std::vector<float> leafColors, leafIntensity;
  for (std::int32_t leafIdx : leaves) {
    leafXyz.clear();
    leafColors.clear();
    leafIntensity.clear();
    if (!pointcloudcache::ReadLeafPoints(cache, leafIdx, leafXyz, leafColors, leafIntensity))
      continue;
    for (size_t i = 0; i + 2 < leafXyz.size(); i += 3) {
      const double dx = leafXyz[i] - cx, dy = leafXyz[i + 1] - cy, dz = leafXyz[i + 2] - cz;
      if (dx * dx + dy * dy + dz * dz <= r2) {
        outNeighborhood.push_back(leafXyz[i]);
        outNeighborhood.push_back(leafXyz[i + 1]);
        outNeighborhood.push_back(leafXyz[i + 2]);
      }
    }
  }
}

void UpdateExtractCenterlineHover(AppCommandState& st, const ray3d::Ray& ray) {
  st.extractCenterlineHoverValid = false;
  if (!ray.valid()) {
    st.extractCenterlineHoverDiag = "invalid pick ray";
    return;
  }

  // Search radius for BOTH steps below: locating a real point near the cursor at all, and then the
  // fit neighborhood around it. One radius for both is deliberate — step 1 only has to land
  // anywhere on the same pipe for step 2's neighborhood (centred on step 1's own answer) to still
  // cover real cylinder surface. Ribbon-exposed (Point Cloud tab, "Centerline" section) — field-
  // tested on a real 188M-point plant scan: the shipped default of 2.0 ft found 56,238 real points
  // in one neighborhood, spanning more than one member (a crossing beam, a bend, a bracket), which
  // is what made it configurable rather than a hand-picked constant.
  const double radius = std::max(0.05, st.extractCenterlineSearchRadiusFt);
  const double r2 = radius * radius;
  const cylinderfit::FitParams fitParams{12, std::max(0.01, st.extractCenterlineMaxResidualRatio)};

  // 1. Nearest REAL point to the ray, across every visible point cloud. A multi-hundred-million-
  // point scan's bounded REQ-171 preview sample is field-tested too sparse for this on its own — a
  // 188M-point scan strided to a 2M preview left the nearest PREVIEW point routinely many feet from
  // the ray even sitting squarely on a visible pipe, failing the pixel-derived hover tolerance
  // outright. So a cloud with an out-of-core cache (ADR-060) is searched at real density instead:
  // `SelectLodLeavesInCylinder` gives the leaves actually near the ray's LINE (not just one probe
  // point), the same primitive the renderer's own camera-proximity LOD pass uses, at a MUCH smaller
  // leaf cap here (this is a single-command hover query, not a whole-viewport redraw). Only a
  // cache-less cloud falls back to the thin preview sample.
  int bestCloud = -1;
  double bestD2 = 1e300;
  double bestX = 0, bestY = 0, bestZ = 0;
  bool anyCacheOpenFailed = false;
  constexpr int kMaxProbeLeaves = 24;
  for (size_t pci = 0; pci < st.drawing.cadPointClouds.size(); ++pci) {
    if (!PointCloudVisible(st, pci))
      continue;
    const std::shared_ptr<const CadPointCloud>& pc = st.drawing.cadPointClouds[pci];
    if (!pc)
      continue;
    const int cloudIdx = static_cast<int>(pci);
    bool searchedRealDensity = false;
    if (pc->hasOutOfCoreCache()) {
      const pointcloudcache::OpenCache* cache = GetOrOpenPointCloudCache(st, cloudIdx);
      if (!cache)
        anyCacheOpenFailed = true;
      if (cache) {
        searchedRealDensity = true;
        const std::vector<pointcloud::CylinderLodLeaf> leaves = pointcloud::SelectLodLeavesInCylinder(
            cache->octree, ray.origin.x, ray.origin.y, ray.origin.z, ray.dir.x, ray.dir.y, ray.dir.z,
            radius, kMaxProbeLeaves);
        std::vector<double> leafXyz;
        std::vector<float> leafColors, leafIntensity;
        for (const pointcloud::CylinderLodLeaf& leaf : leaves) {
          leafXyz.clear();
          leafColors.clear();
          leafIntensity.clear();
          if (!pointcloudcache::ReadLeafPoints(*cache, leaf.leafNodeIndex, leafXyz, leafColors,
                                               leafIntensity))
            continue;
          for (size_t i = 0; i + 2 < leafXyz.size(); i += 3) {
            const double d = ray3d::RayPointDistance(
                ray, ray3d::Vec3{leafXyz[i], leafXyz[i + 1], leafXyz[i + 2]});
            const double d2 = d * d;
            if (d2 < bestD2) {
              bestD2 = d2;
              bestCloud = cloudIdx;
              bestX = leafXyz[i];
              bestY = leafXyz[i + 1];
              bestZ = leafXyz[i + 2];
            }
          }
        }
      }
    }
    if (!searchedRealDensity) {
      const std::vector<double>& P = pc->pointsXyz;
      for (size_t i = 0; i + 2 < P.size(); i += 3) {
        const double d = ray3d::RayPointDistance(ray, ray3d::Vec3{P[i], P[i + 1], P[i + 2]});
        const double d2 = d * d;
        if (d2 < bestD2) {
          bestD2 = d2;
          bestCloud = cloudIdx;
          bestX = P[i];
          bestY = P[i + 1];
          bestZ = P[i + 2];
        }
      }
    }
  }
  if (bestCloud < 0) {
    st.extractCenterlineHoverDiag =
        anyCacheOpenFailed ? "no visible point cloud (its out-of-core cache failed to open)"
                           : "no visible point cloud";
    return;
  }
  const float tol = CadHoverEntityPickTolWorld(st);
  const double gate = std::max(static_cast<double>(tol), radius);
  if (bestD2 > gate * gate) {
    // nothing close enough to the cursor to be "hovering a cloud" at all
    st.extractCenterlineHoverDiag = "nearest scan point is " + std::to_string(std::sqrt(bestD2)) +
                                    " ft from the cursor ray (need <= " + std::to_string(gate) + " ft)" +
                                    (anyCacheOpenFailed ? " [a cloud's out-of-core cache failed to open]"
                                                        : "");
    return;
  }

  const double cx = bestX, cy = bestY, cz = bestZ;

  // 2. The fit neighborhood around that point, at the same real density when the cache is open —
  // re-read only when the hover has moved past a fraction of the radius since the last read
  // (reusing the last neighborhood otherwise), so a nearly-still cursor does not re-hit disk every
  // frame; this stays nowhere near the renderer's own much larger per-frame LOD reselect cost.
  const pointcloudcache::OpenCache* cache = GetOrOpenPointCloudCache(st, bestCloud);
  const double moveHysteresis = radius * 0.3;
  const bool reuseLast =
      cache && st.extractCenterlineLastQueryValid && st.extractCenterlineLastQueryCloudIndex == bestCloud &&
      (cx - st.extractCenterlineLastQueryX) * (cx - st.extractCenterlineLastQueryX) +
              (cy - st.extractCenterlineLastQueryY) * (cy - st.extractCenterlineLastQueryY) +
              (cz - st.extractCenterlineLastQueryZ) * (cz - st.extractCenterlineLastQueryZ) <=
          moveHysteresis * moveHysteresis;

  std::vector<double> neighborhood;
  if (reuseLast) {
    neighborhood = st.extractCenterlineLastNeighborhood;
  } else {
    if (cache)
      ReadExtractCenterlineNeighborhood(*cache, cx, cy, cz, radius, neighborhood);
    if (neighborhood.empty()) {
      // No cache, or no cached leaves fell in range — fall back to the bounded preview sample.
      const std::vector<double>& P = st.drawing.cadPointClouds[static_cast<size_t>(bestCloud)]->pointsXyz;
      for (size_t i = 0; i + 2 < P.size(); i += 3) {
        const double dx = P[i] - cx, dy = P[i + 1] - cy, dz = P[i + 2] - cz;
        if (dx * dx + dy * dy + dz * dz <= r2) {
          neighborhood.push_back(P[i]);
          neighborhood.push_back(P[i + 1]);
          neighborhood.push_back(P[i + 2]);
        }
      }
    }
    if (cache) {
      st.extractCenterlineLastQueryValid = true;
      st.extractCenterlineLastQueryCloudIndex = bestCloud;
      st.extractCenterlineLastQueryX = cx;
      st.extractCenterlineLastQueryY = cy;
      st.extractCenterlineLastQueryZ = cz;
      st.extractCenterlineLastNeighborhood = neighborhood;
    }
  }

  const cylinderfit::FitResult fit = cylinderfit::FitCylinderAxisLeastSquares(neighborhood, fitParams);
  if (!fit.ok) {
    const size_t n = neighborhood.size() / 3;
    st.extractCenterlineHoverDiag =
        n < 12 ? "only " + std::to_string(n) + " points in the neighborhood (need at least 12)"
               : std::to_string(n) + " points nearby, but they don't fit a cylinder closely enough";
    return;  // too few points, or not cylindrical enough (REQ-347: no misleading preview)
  }

  st.extractCenterlineHoverValid = true;
  st.extractCenterlineP0X = fit.p0X;
  st.extractCenterlineP0Y = fit.p0Y;
  st.extractCenterlineP0Z = fit.p0Z;
  st.extractCenterlineP1X = fit.p1X;
  st.extractCenterlineP1Y = fit.p1Y;
  st.extractCenterlineP1Z = fit.p1Z;
}

void SubmitExtractCenterlineViewportPick(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.extractCenterlineHoverValid) {
    log.push_back("EXTRACTCENTERLINE — no cylinder found there (" + st.extractCenterlineHoverDiag +
                  "); hover a scanned pipe/pole/column and click (Esc to cancel).");
    return;
  }
  PushUndoSnapshot(st, "Extract Centerline");
  st.drawing.pushLine(CadLineRecord{st.extractCenterlineP0X, st.extractCenterlineP0Y, st.extractCenterlineP0Z, st.extractCenterlineP1X, st.extractCenterlineP1Y, st.extractCenterlineP1Z});
  st.drawing.userLineAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  log.push_back("EXTRACTCENTERLINE — line placed along the fitted cylinder axis.");
}
