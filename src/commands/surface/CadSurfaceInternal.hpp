#include "CadCommandsInternal.hpp"
#include <GoSurvey/Terrain/contourgen.hpp>
#include <GoSurvey/Terrain/gridsurface.hpp>
#include <GoSurvey/Terrain/surfacequery.hpp>
#include <GoSurvey/Terrain/tinbuild.hpp>
#include <GoSurvey/Terrain/tinvolume.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/// decision makes that condition true by construction rather than by inspection.
struct SurfaceContourLevels {
  std::vector<double> minor;  ///< Minor levels, with the majors already removed.
  std::vector<double> major;
  bool suppressed = false;    ///< The style asked for more levels than \ref kMaxContourLevels.
  int levelsAsked = 0;        ///< How many, when suppressed. 0 otherwise.
  double minZ = 0.0, maxZ = 0.0;
  bool haveRange = false;     ///< False for a triangulation with no vertices.

  [[nodiscard]] bool empty() const { return minor.empty() && major.empty(); }
};



/// A breakline/boundary source's vertex chain, in WORLD double coordinates (matching the convention
/// \ref BuildSurfaceFromSources already uses for point-group points — ADR-028 (d)). Z is carried
/// through as-is: it is stored absolute, not offset by the document origin (ADR-025 D2), the same
/// rule \ref CadTin and \ref CadFilledRegion already follow.
struct ResolvedChain {
  std::vector<std::array<double, 3>> verts;
  bool closed = false;
};


/// Everything a surface build needs, already resolved against \c AppCommandState — plain data, safe
/// to copy into a worker thread with no further access to drawing state (architecture §8 rule 1).
struct SurfaceBuildInputs {
  std::vector<TinInputPoint> pts;
  std::vector<TinConstraint> constraints;
  std::vector<std::uint64_t> constraintSourceIds;  ///< Stable source entity ID parallel to each constraint.
  std::vector<TinBoundaryLoop> cullLoops;
  double originX = 0.0, originY = 0.0;
  /// A linked point file could not be read (REQ-086). The build is ABANDONED rather than run on what
  /// is left: a surface whose file went missing must keep its last good triangulation, not quietly
  /// rebuild itself smaller (REQ-001 — reject, never absorb). Distinct from a build that fails on its
  /// own merits, because the inputs here are known-incomplete before the triangulator ever runs.
  bool inputsIncomplete = false;
  std::string incompleteReason;
  /// REQ-136: copy of parent TINs for a volume surface. When true, \ref RunSurfaceBuild ignores
  /// \c pts / \c constraints and calls \ref BuildTinVolumeSurface.
  bool isVolume = false;
  std::vector<double> volumeBaseVertsXyz;
  std::vector<std::uint32_t> volumeBaseIndices;
  std::vector<double> volumeCompVertsXyz;
  std::vector<std::uint32_t> volumeCompIndices;
};


static void ApplyDefinitionPointEdits(const CadSurface& surface, SurfaceBuildInputs& in) {
  assert(std::isfinite(in.originX));
  assert(std::isfinite(in.originY));
  const size_t nAdd = surface.addedPointXyz.size();
  if (nAdd % 3u == 0u) {
    for (size_t i = 0; i + 2 < nAdd; i += 3) {
      in.pts.push_back({static_cast<double>(surface.addedPointXyz[i]) + in.originX,
                        static_cast<double>(surface.addedPointXyz[i + 1]) + in.originY,
                        surface.addedPointXyz[i + 2]});
    }
  }
  constexpr size_t kMaxMoves = 1000000;
  const size_t nMove = std::min(surface.movedPoints.size(), kMaxMoves);
  for (size_t m = 0; m < nMove; ++m) {
    if (in.pts.empty())
      break;
    const CadSurface::MovedPoint& mv = surface.movedPoints[m];
    const double wx = mv.fromX + in.originX;
    const double wy = mv.fromY + in.originY;
    size_t best = 0;
    double bestD2 = 1.0e300;
    for (size_t i = 0; i < in.pts.size(); ++i) {
      const double dx = in.pts[i].x - wx;
      const double dy = in.pts[i].y - wy;
      const double d2 = dx * dx + dy * dy;
      if (d2 < bestD2) {
        bestD2 = d2;
        best = i;
      }
    }
    in.pts[best].x = static_cast<double>(mv.toX) + in.originX;
    in.pts[best].y = static_cast<double>(mv.toY) + in.originY;
    in.pts[best].z = mv.toZ;
  }
  constexpr size_t kMaxDeletes = 1000000;
  const size_t nDel = std::min(surface.deletedPointPicks.size(), kMaxDeletes);
  for (size_t d = 0; d < nDel; ++d) {
    if (in.pts.empty())
      break;
    const double wx = surface.deletedPointPicks[d].first + in.originX;
    const double wy = surface.deletedPointPicks[d].second + in.originY;
    size_t best = 0;
    double bestD2 = 1.0e300;
    for (size_t i = 0; i < in.pts.size(); ++i) {
      const double dx = in.pts[i].x - wx;
      const double dy = in.pts[i].y - wy;
      const double d2 = dx * dx + dy * dy;
      if (d2 < bestD2) {
        bestD2 = d2;
        best = i;
      }
    }
    in.pts.erase(in.pts.begin() + static_cast<std::ptrdiff_t>(best));
  }
}


bool RefreshSurfaceProfilesForSurface(AppCommandState& st, const CadSurface& surface, bool invalidateCache = true,
                                      std::uint64_t alignmentId = 0);

static std::unique_ptr<ISurfaceQuery> MakeSurfaceQuery(const AppCommandState& st, const CadSurface& surf) {
  if ((surf.kind == SurfaceKind::Grid || surf.kind == SurfaceKind::GridVolume) &&
      GridIndexValid(surf.gridCols, surf.gridRows, surf.gridZ)) {
    return std::make_unique<GridSurfaceQuery>(
        surf.gridOriginX - st.drawing.worldDocumentOriginX, surf.gridOriginY - st.drawing.worldDocumentOriginY, surf.gridSpacingX,
        surf.gridSpacingY, surf.gridCols, surf.gridRows, surf.gridZ);
  }
  if (surf.tin)
    return std::make_unique<TinSurfaceQuery>(surf.tin->vertsXyz, surf.tin->indices);
  return nullptr;
}


static void CommitQuickProfileLocal(AppCommandState& st, const CadSurface& s, double x0, double y0, double x1,
                                    double y1, std::vector<std::string>& log) {
  auto q = MakeSurfaceQuery(st, s);
  if (!q) {
    log.push_back("QUICKPROFILE — \"" + s.name + "\" is not a built surface.");
    return;
  }
  std::vector<SurfaceProfileSample> samples;
  if (!SampleSurfaceProfileLine(*q, x0, y0, x1, y1, kQuickProfileStepFt, kQuickProfileMaxSamples, &samples)) {
    log.push_back("QUICKPROFILE — the two points must be a finite non-zero length.");
    return;
  }
  int hits = 0;
  double minZ = 0.0;
  double maxZ = 0.0;
  bool anyZ = false;
  for (const SurfaceProfileSample& p : samples) {
    if (!p.onSurface)
      continue;
    ++hits;
    if (!anyZ) {
      minZ = maxZ = p.z;
      anyZ = true;
    } else {
      minZ = std::min(minZ, p.z);
      maxZ = std::max(maxZ, p.z);
    }
  }
  if (hits == 0) {
    log.push_back("QUICKPROFILE — no samples on the surface (outside).");
    return;
  }
  st.quickProfile.open = true;
  st.quickProfile.hasResult = true;
  st.quickProfile.surfaceName = s.name;
  st.quickProfile.length = samples.back().station;
  st.quickProfile.onSurfaceCount = hits;
  st.quickProfile.minZ = minZ;
  st.quickProfile.maxZ = maxZ;
  st.quickProfile.samples = std::move(samples);
  const int p = st.displayLinearPrecision;
  log.push_back("QUICKPROFILE — \"" + s.name + "\": length " + FormatLinear(st.quickProfile.length, p) + ", " +
                std::to_string(st.quickProfile.samples.size()) + " sample(s), " + std::to_string(hits) +
                " on surface, elev " + FormatLinear(minZ, p) + " to " + FormatLinear(maxZ, p) + ".");
}


// --- REQ-074 spot elevation and grade, REQ-089 rollover readout ---------------------------------

/// One visible surface covering a plan position, and its interpolated elevation there.
///
/// The index is consumed by the caller **within the same call** and never stored, so architecture
/// §11.9 is satisfied: it is a cursor into this walk's result, not a reference held across time.
struct SurfaceCoverage {
  size_t surfaceIndex = 0;
  double z = 0.0;
};

/// Every **visible** surface covering (\p x, \p y), with its interpolated elevation.
///
/// **Every** covering surface, not one of them (TASK-055 Q1): overlapping surfaces are the existing
/// vs proposed case, which is the grading question SURFELEV exists to answer, and a bare number from
/// an unnamed surface would be worse than no number at all. REQ-089's readout inherits the rule —
/// two overlapping surfaces get one block each.
///
/// Invisible surfaces are skipped, via the shared \ref SurfaceVisible — a readout should describe the
/// surfaces the user can see, and REQ-068 already established that rule. Routing through the shared
/// predicate also fixed a real gap here: this walk checked layer on/frozen but not `hiddenEntityIds`,
/// so SURFELEV reported an elevation from a surface REQ-084 (d) had isolated out.
///
/// **One walk, two readers** (SURFELEV and the REQ-089 rollover), for the reason the paragraph above
/// records: the last time this question was asked in two places, the two answers drifted and a hidden
/// surface kept reporting elevations. A second copy of this loop would be the same defect waiting to
/// happen, so the rollover was given the index it needs rather than a walk of its own.
static std::vector<SurfaceCoverage> SurfacesCovering(const AppCommandState& st, double x, double y) {
  std::vector<SurfaceCoverage> out;
  for (size_t si = 0; si < st.drawing.cadSurfaces.size(); ++si) {
    if (!SurfaceVisible(st, si))
      continue;
    const CadSurface& s = st.drawing.cadSurfaces[si];
    if (!s.tin)
      continue;
    double z = 0.0;
    const std::uint64_t id =
        si < st.drawing.cadSurfaceAttrs.size() ? st.drawing.cadSurfaceAttrs[si].id : 0;
    const TinSpatialIndex* index = nullptr;
    for (AppCommandState::SurfaceQueryCacheEntry& e : st.surfaceQueryCache) {
      if (e.surfaceId == id && e.builtFrom.lock() == s.tin) {
        index = &e.index;
        break;
      }
    }
    if (!index) {
      AppCommandState::SurfaceQueryCacheEntry e;
      e.surfaceId = id;
      e.builtFrom = s.tin;
      e.index = BuildTinSpatialIndex(s.tin->vertsXyz, s.tin->indices);
      st.surfaceQueryCache.push_back(std::move(e));
      index = &st.surfaceQueryCache.back().index;
    }
    const bool hit = (index && !index->empty())
                         ? TinElevationAtIndexed(s.tin->vertsXyz, s.tin->indices, *index, x, y, &z)
                         : TinElevationAt(s.tin->vertsXyz, s.tin->indices, x, y, &z);
    if (hit)
      out.push_back({si, z});
  }
  return out;
}

/// The same coverage, named rather than indexed — what REQ-074's two commands consume.
static std::vector<std::pair<std::string, double>> SurfaceElevationsAt(const AppCommandState& st, double x,
                                                                       double y) {
  std::vector<std::pair<std::string, double>> out;
  for (const SurfaceCoverage& c : SurfacesCovering(st, x, y))
    out.emplace_back(st.drawing.cadSurfaces[c.surfaceIndex].name, c.z);
  return out;
}

// REQ-089's readout is built from this same walk, but it is a public entry point (the UI calls it),
// so its definition cannot live inside this anonymous namespace — see `BuildSurfaceHoverRows` beside
// `CommitClipboardPasteAt` after the namespace closes.

/// First pick: report the elevation on each covering surface, and remember them for the grade.
static void ReportSurfaceElevationAt(AppCommandState& st, double x, double y, std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::SurfaceElevGradeSession>();
  if (!sess)
    return;
  sess->fromX = x;
  sess->fromY = y;
  sess->fromZ = SurfaceElevationsAt(st, x, y);

  if (st.drawing.cadSurfaces.empty()) {
    log.push_back("SURFELEV — there are no surfaces in the drawing. Build one from a point group first.");
    return;
  }
  if (sess->fromZ.empty()) {
    // REQ-074: "a pick outside the surface reports that it is outside; it never extrapolates."
    log.push_back("SURFELEV — outside surface. No elevation at that point.");
    return;
  }
  const int p = st.displayLinearPrecision;
  for (const SurfaceCoverage& c : SurfacesCovering(st, x, y)) {
    const CadSurface& surf = st.drawing.cadSurfaces[c.surfaceIndex];
    std::string line = "SURFELEV — " + surf.name + ": elevation " + FormatLinear(c.z, p);
    std::unique_ptr<ISurfaceQuery> q;
    if ((surf.kind == SurfaceKind::Grid || surf.kind == SurfaceKind::GridVolume) &&
        GridIndexValid(surf.gridCols, surf.gridRows, surf.gridZ)) {
      q = std::make_unique<GridSurfaceQuery>(
          surf.gridOriginX - st.drawing.worldDocumentOriginX, surf.gridOriginY - st.drawing.worldDocumentOriginY,
          surf.gridSpacingX, surf.gridSpacingY, surf.gridCols, surf.gridRows, surf.gridZ);
    } else if (surf.tin) {
      q = std::make_unique<TinSurfaceQuery>(surf.tin->vertsXyz, surf.tin->indices);
    }
    if (q) {
      double pct = 0.0, ang = 0.0, asp = 0.0;
      if (q->slopePercentAt(x, y, &pct) && q->slopeAngleDegAt(x, y, &ang))
        line += ", grade " + FormatLinear(pct, 2) + "%, slope " + FormatLinear(ang, 2) + " deg";
      if (q->aspectDegAt(x, y, &asp))
        line += ", aspect " + FormatLinear(asp, 2) + " deg";
    }
    log.push_back(std::move(line));
  }
}

/// Second pick: grade from the first, computed **within each surface**, never across two.
static void ReportSurfaceGradeTo(AppCommandState& st, double x, double y, std::vector<std::string>& log) {
  auto* sess = st.sessionAs<stepengine::SurfaceElevGradeSession>();
  if (!sess)
    return;
  const int p = st.displayLinearPrecision;

  const double dx = x - sess->fromX;
  const double dy = y - sess->fromY;
  const double run = std::hypot(dx, dy);

  // REQ-074: "two picks at the same location report zero distance rather than dividing by zero."
  // The threshold is kTinPlanEpsilon, not an arbitrary epsilon: below it the project already
  // considers two positions to be the same site (it is what BuildTin de-duplicates on).
  if (run < kTinPlanEpsilon) {
    log.push_back("SURFELEV — both picks are at the same location: horizontal distance 0. No grade.");
    stepengine::EndSession(st);
    return;
  }

  const std::vector<std::pair<std::string, double>> to = SurfaceElevationsAt(st, x, y);
  bool reportedAny = false;
  for (const auto& from : sess->fromZ) {
    // `from.first` rather than a structured binding: capturing one in the lambda below is a C++20
    // extension, and this file is built as C++17.
    const std::string& name = from.first;
    const double z1 = from.second;
    const auto it = std::find_if(to.begin(), to.end(), [&](const auto& e) { return e.first == name; });
    if (it == to.end()) {
      log.push_back("SURFELEV — " + name + ": second point is outside this surface. No grade.");
      continue;
    }
    const double rise = it->second - z1;
    const double slopePct = rise / run * 100.0;
    char buf[320];
    if (std::abs(rise) < 1e-9) {
      // Flat: a run:rise ratio would be a division by zero, and "level" is what a surveyor would
      // write on the sheet anyway.
      std::snprintf(buf, sizeof(buf), "SURFELEV — %s: level (0.00%%)  horiz %s  vert %s", name.c_str(),
                    FormatLinear(run, p).c_str(), FormatLinear(rise, p).c_str());
    } else {
      // Both conventions, because both are used: percent for the grade, run:rise for the slope.
      std::snprintf(buf, sizeof(buf), "SURFELEV — %s: grade %.2f%%  slope %.2f:1  horiz %s  vert %s",
                    name.c_str(), slopePct, run / std::abs(rise), FormatLinear(run, p).c_str(),
                    FormatLinear(rise, p).c_str());
    }
    log.push_back(buf);
    reportedAny = true;
  }
  if (!reportedAny && sess->fromZ.empty())
    log.push_back("SURFELEV — outside surface at the first point; no grade to report.");

  stepengine::EndSession(st);
}


bool ResolveDefinitionChain(const AppCommandState& st, std::uint64_t id, bool requireClosed, ResolvedChain& out);
bool VolumeClipRingLocalXy(const AppCommandState& st, std::uint64_t entityId, std::vector<std::pair<double, double>>* out, std::string* err);
void AppendChainConstraints(const ResolvedChain& chain, bool forceClosed, std::vector<TinConstraint>& out, std::vector<std::uint64_t>* sourceIds, std::uint64_t sourceId);
void ApplySurfaceCrossingCorrections(const CadSurface& surface, SurfaceBuildInputs& inputs);
bool IsSimpleInfillPolygon(const std::vector<double>& xyz, std::string* why = nullptr);
SurfaceBuildInputs ResolveSurfaceInputs(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log);
void MarkVolumeSurfacesDirtyForParent(AppCommandState& st, const std::string& parentName);
std::shared_ptr<CadTin> ToLocalTin(const TinBuildResult& r, double originX, double originY);
bool VolumeDashboardReady(const AppCommandState& st, std::uint64_t baseId, std::uint64_t compId, std::string* whyNot);
SurfaceContourLevels ResolveSurfaceContourLevels(const CadTin& tin, const SurfaceStyle& style);
void ReportSurfaces(const AppCommandState& st, std::vector<std::string>& log);
void RunSurfaceCreate(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunVolumeSurfaceCreate(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceCreateGrid(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceCreateCorr(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceCreateVolGrid(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunCreateSurfaceProfile(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceProfileView(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunQuickProfile(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceRename(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceDelete(AppCommandState& st, const std::string& name, std::vector<std::string>& log);
void RunSurfaceRebuild(AppCommandState& st, const std::string& name, std::vector<std::string>& log);
void RunSurfaceLodCommand(AppCommandState& st, const std::string& args, bool reduced, std::vector<std::string>& log);
void RunMoveToSurfaceCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void ExecuteSurfStyleCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void ExecuteVolumesCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void ExecuteVolDashCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void ExecuteToolspaceCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunUndesignate(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceStats(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceVisibility(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceLabel(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceLegend(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceCrossings(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void ExecuteDrapeLinksCommand(AppCommandState& st, std::vector<std::string>& log);
void ExecuteDrapeCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void BuildPadSolids(AppCommandState& st, const std::string& surfaceName, std::uint64_t boundaryId, double padZ, std::vector<std::string>& log);
void StartPadSolidCommand(AppCommandState& st, std::vector<std::string>& log);
void HandlePadSolidText(AppCommandState& st, const std::string& line, std::vector<std::string>& log);
void HandlePadSolidPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log);
void ExecutePadSolidCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void ExecuteExtractCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void CommitDesignateAt(AppCommandState& st, float wx, float wy, bool isBoundary, std::vector<std::string>& log);
void RunSurfaceAddFile(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfaceImportFile(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
