#include "CadCommandsInternal.hpp"
#include "surface/CadSurfaceInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Terrain/gridsurface.hpp>
#include <GoSurvey/Terrain/surfacequery.hpp>
#include <GoSurvey/Terrain/surfacestats.hpp>
#include <GoSurvey/Terrain/surfacevolume.hpp>
#include <GoSurvey/Terrain/tinbuild.hpp>
#include <GoSurvey/Terrain/tinvolume.hpp>
#include "GoSurvey/Survey/AlignmentGroundModel.hpp"
#include <GoSurvey/IO/SurveyCsv.hpp>
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

/// Resolves a breakline/boundary reference (REQ-069) to its vertex chain. A Line or a Polyline are
/// both valid breakline sources; only a Polyline can be closed, so only a Polyline can ever satisfy
/// \p requireClosed. Returns false (chain left empty) when the id does not resolve to a usable
/// entity — the caller's job is to then drop that id from the definition, not to invent a chain.
bool ResolveDefinitionChain(const AppCommandState& st, std::uint64_t id, bool requireClosed,
                           ResolvedChain& out) {
  out.verts.clear();
  out.closed = false;
  const EntityRef ref = FindEntityById(st, id);
  if (!ref.valid())
    return false;

  if (ref.kind == EntityKind::Line) {
    if (requireClosed)
      return false;  // a 2-point line can never be a closed boundary ring
    const size_t li = static_cast<size_t>(ref.index);
    if (li >= st.drawing.lineCount())
      return false;
    const CadLineRecord ln = st.drawing.line(li);
    out.verts.push_back({ln.x0 + st.drawing.worldDocumentOriginX, ln.y0 + st.drawing.worldDocumentOriginY, ln.z0});
    out.verts.push_back({ln.x1 + st.drawing.worldDocumentOriginX, ln.y1 + st.drawing.worldDocumentOriginY, ln.z1});
    return true;
  }

  if (ref.kind == EntityKind::Polyline) {
    const size_t pi = static_cast<size_t>(ref.index);
    if (pi + 1 >= st.drawing.userPolylineOffsets.size())
      return false;
    const bool closed = pi < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[pi] != 0;
    if (requireClosed && !closed)
      return false;
    const int vBegin = st.drawing.userPolylineOffsets[pi], vEnd = st.drawing.userPolylineOffsets[pi + 1];
    if (vEnd - vBegin < 2)
      return false;
    for (int v = vBegin; v < vEnd; ++v) {
      const size_t base = static_cast<size_t>(v);
      if (base >= st.drawing.vertexCount())
        return false;
      out.verts.push_back({static_cast<double>(st.drawing.vertex(base).x) + st.drawing.worldDocumentOriginX,
                           static_cast<double>(st.drawing.vertex(base).y) + st.drawing.worldDocumentOriginY,
                           static_cast<double>(st.drawing.vertex(base).z)});
    }
    out.closed = closed;
    return true;
  }

  // REQ-087 / REQ-088. Without this a feature line does not merely fail to resolve — it resolves as
  // ABSENT, and ResolveSurfaceInputs treats an unresolvable id as an entity that no longer exists,
  // so it strips the breakline from the STORED definition and reports "breakline(s) no longer
  // exist" about a line sitting in plain view. Designating one was silently self-undoing.
  //
  // Elevation points need no handling here, and that is ADR-035 (a) paying off rather than an
  // omission: a flagged vertex is an ordinary vertex in the plan chain, so the triangulator folds
  // its elevation in with every other vertex's and the constraint edge follows the line exactly.
  if (ref.kind == EntityKind::FeatureLine) {
    const size_t fi = static_cast<size_t>(ref.index);
    if (fi + 1 >= st.drawing.featureLineOffsets.size())
      return false;
    const bool closed = fi < st.drawing.featureLineClosed.size() && st.drawing.featureLineClosed[fi] != 0;
    if (requireClosed && !closed)
      return false;
    const int vBegin = st.drawing.featureLineOffsets[fi], vEnd = st.drawing.featureLineOffsets[fi + 1];
    if (vEnd - vBegin < 2)
      return false;
    for (int v = vBegin; v < vEnd; ++v) {
      const size_t base = static_cast<size_t>(v) * 3;
      if (base + 2 >= st.drawing.featureLineVerts.size())
        return false;
      out.verts.push_back({static_cast<double>(st.drawing.featureLineVerts[base + 0]) + st.drawing.worldDocumentOriginX,
                           static_cast<double>(st.drawing.featureLineVerts[base + 1]) + st.drawing.worldDocumentOriginY,
                           static_cast<double>(st.drawing.featureLineVerts[base + 2])});
    }
    out.closed = closed;
    return true;
  }

  if (ref.kind == EntityKind::Arc) {
    if (requireClosed)
      return false;
    const size_t ai = static_cast<size_t>(ref.index);
    if (ai >= st.drawing.userArcs.size())
      return false;
    const CadArc& arc = st.drawing.userArcs[ai];
    const double sweep = static_cast<double>(arc.sweepRad);
    const double r = std::max(static_cast<double>(arc.r), 1.0e-9);
    const double dangChord = 2.0 * std::acos(std::clamp(1.0 - 1.0 / r, -1.0, 1.0));
    const double dang5 = 5.0 * 3.14159265358979323846 / 180.0;
    double step = std::min(dangChord, dang5);
    if (!(step > 1.0e-6))
      step = dang5;
    int n = static_cast<int>(std::ceil(std::fabs(sweep) / step));
    n = std::clamp(n, 8, 256);
    const double ds = sweep / static_cast<double>(n);
    for (int i = 0; i <= n; ++i) {
      const double ang = static_cast<double>(arc.startRad) + ds * static_cast<double>(i);
      out.verts.push_back({static_cast<double>(arc.cx) + st.drawing.worldDocumentOriginX + r * std::cos(ang),
                           static_cast<double>(arc.cy) + st.drawing.worldDocumentOriginY + r * std::sin(ang),
                           static_cast<double>(arc.z)});
    }
    out.closed = false;
    return true;
  }

  return false;  // any other entity kind is not a valid breakline/boundary source
}

/// REQ-131: closed polyline as a volume clip, in TIN-local XY. \p entityId 0 means no clip (success, empty).
bool VolumeClipRingLocalXy(const AppCommandState& st, std::uint64_t entityId,
                           std::vector<std::pair<double, double>>* out, std::string* err) {
  if (!out)
    return false;
  out->clear();
  if (entityId == 0)
    return true;
  ResolvedChain chain;
  if (!ResolveDefinitionChain(st, entityId, /*requireClosed=*/true, chain) || chain.verts.size() < 3) {
    if (err)
      *err = "clip must be a closed polyline";
    return false;
  }
  out->reserve(chain.verts.size());
  for (const auto& v : chain.verts)
    out->push_back({v[0] - st.drawing.worldDocumentOriginX, v[1] - st.drawing.worldDocumentOriginY});
  return true;
}

/// Appends one \ref TinConstraint per edge of \p chain — consecutive vertex pairs, plus the closing
/// edge (last→first) when \p forceClosed or the source itself is closed.
void AppendChainConstraints(const ResolvedChain& chain, bool forceClosed, std::vector<TinConstraint>& out,
                            std::vector<std::uint64_t>* sourceIds = nullptr, std::uint64_t sourceId = 0) {
  const size_t n = chain.verts.size();
  if (n < 2)
    return;
  for (size_t i = 0; i + 1 < n; ++i) {
    TinConstraint c;
    c.ax = chain.verts[i][0]; c.ay = chain.verts[i][1]; c.az = chain.verts[i][2];
    c.bx = chain.verts[i + 1][0]; c.by = chain.verts[i + 1][1]; c.bz = chain.verts[i + 1][2];
    out.push_back(c);
    if (sourceIds) sourceIds->push_back(sourceId);
  }
  if ((forceClosed || chain.closed) && n >= 3) {
    TinConstraint c;
    c.ax = chain.verts[n - 1][0]; c.ay = chain.verts[n - 1][1]; c.az = chain.verts[n - 1][2];
    c.bx = chain.verts[0][0]; c.by = chain.verts[0][1]; c.bz = chain.verts[0][2];
    out.push_back(c);
    if (sourceIds) sourceIds->push_back(sourceId);
  }
}


void ApplySurfaceCrossingCorrections(const CadSurface& surface, SurfaceBuildInputs& inputs) {
  for (const CadSurfaceCrossingCorrection& correction : surface.crossingCorrections) {
    for (const TinCrossingIssue& issue : TinFindCrossingConflicts(inputs.constraints)) {
      if (issue.constraintIndexA >= inputs.constraintSourceIds.size() ||
          issue.constraintIndexB >= inputs.constraintSourceIds.size()) continue;
      const std::uint64_t idA = inputs.constraintSourceIds[issue.constraintIndexA];
      const std::uint64_t idB = inputs.constraintSourceIds[issue.constraintIndexB];
      const bool sameOrder = idA == correction.entityA && idB == correction.entityB;
      const bool reverseOrder = idA == correction.entityB && idB == correction.entityA;
      if ((!sameOrder && !reverseOrder) ||
          std::hypot(issue.x - inputs.originX - correction.x, issue.y - inputs.originY - correction.y) > 0.05)
        continue;
      const size_t indexA = sameOrder ? issue.constraintIndexA : issue.constraintIndexB;
      const size_t indexB = sameOrder ? issue.constraintIndexB : issue.constraintIndexA;
      if (correction.kind == CadSurfaceCrossingCorrection::Kind::TrimA ||
          correction.kind == CadSurfaceCrossingCorrection::Kind::TrimB) {
        const size_t remove = correction.kind == CadSurfaceCrossingCorrection::Kind::TrimA ? indexA : indexB;
        inputs.constraints.erase(inputs.constraints.begin() + static_cast<std::ptrdiff_t>(remove));
        inputs.constraintSourceIds.erase(inputs.constraintSourceIds.begin() + static_cast<std::ptrdiff_t>(remove));
      } else {
        double z = (issue.zFromA + issue.zFromB) * 0.5;
        switch (correction.kind) {
        case CadSurfaceCrossingCorrection::Kind::Higher: z = std::max(issue.zFromA, issue.zFromB); break;
        case CadSurfaceCrossingCorrection::Kind::Lower: z = std::min(issue.zFromA, issue.zFromB); break;
        case CadSurfaceCrossingCorrection::Kind::Average: break;
        case CadSurfaceCrossingCorrection::Kind::Custom: z = correction.customElevation; break;
        case CadSurfaceCrossingCorrection::Kind::TrimA:
        case CadSurfaceCrossingCorrection::Kind::TrimB: break;
        }
        const auto split = [&](size_t index) {
          const TinConstraint original = inputs.constraints[index];
          TinConstraint first = original, second = original;
          const double x = issue.x;
          const double y = issue.y;
          first.bx = x; first.by = y; first.bz = z;
          second.ax = x; second.ay = y; second.az = z;
          inputs.constraints[index] = first;
          inputs.constraints.insert(inputs.constraints.begin() + static_cast<std::ptrdiff_t>(index + 1), second);
          inputs.constraintSourceIds.insert(inputs.constraintSourceIds.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                             inputs.constraintSourceIds[index]);
        };
        if (indexA > indexB) { split(indexA); split(indexB); }
        else { split(indexB); split(indexA); }
      }
      break;
    }
  }
}

bool IsSimpleInfillPolygon(const std::vector<double>& xyz, std::string* why) {
  const size_t n = xyz.size() / 3;
  if (n < 3 || xyz.size() % 3 != 0) { if (why) *why = "boundary needs at least three vertices"; return false; }
  double minX = xyz[0], maxX = xyz[0], minY = xyz[1], maxY = xyz[1];
  for (size_t i = 0; i < n; ++i) {
    minX = std::min(minX, xyz[i * 3]); maxX = std::max(maxX, xyz[i * 3]);
    minY = std::min(minY, xyz[i * 3 + 1]); maxY = std::max(maxY, xyz[i * 3 + 1]);
  }
  const double scale = std::max(1.0, std::hypot(maxX - minX, maxY - minY));
  const double eps = scale * 1e-10, crossEps = eps * scale;
  const double ox = xyz[0], oy = xyz[1];
  double area2 = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    const double ax = xyz[i * 3] - ox, ay = xyz[i * 3 + 1] - oy;
    const double bx = xyz[j * 3] - ox, by = xyz[j * 3 + 1] - oy;
    if (std::hypot(ax - bx, ay - by) <= eps) { if (why) *why = "boundary has a zero-length edge"; return false; }
    area2 += ax * by - bx * ay;
  }
  if (std::abs(area2) <= crossEps) { if (why) *why = "boundary has zero plan area"; return false; }
  auto orient = [&](size_t a, size_t b, size_t c) {
    const double abx = xyz[b * 3] - xyz[a * 3], aby = xyz[b * 3 + 1] - xyz[a * 3 + 1];
    const double acx = xyz[c * 3] - xyz[a * 3], acy = xyz[c * 3 + 1] - xyz[a * 3 + 1];
    return abx * acy - aby * acx;
  };
  auto onSegment = [&](size_t a, size_t b, size_t p) {
    return xyz[p * 3] >= std::min(xyz[a * 3], xyz[b * 3]) - eps &&
           xyz[p * 3] <= std::max(xyz[a * 3], xyz[b * 3]) + eps &&
           xyz[p * 3 + 1] >= std::min(xyz[a * 3 + 1], xyz[b * 3 + 1]) - eps &&
           xyz[p * 3 + 1] <= std::max(xyz[a * 3 + 1], xyz[b * 3 + 1]) + eps;
  };
  for (size_t a = 0; a < n; ++a) {
    const size_t b = (a + 1) % n;
    for (size_t c = a + 1; c < n; ++c) {
      const size_t d = (c + 1) % n;
      if (a == c || b == c || d == a) continue;
      const double abC = orient(a, b, c), abD = orient(a, b, d);
      const double cdA = orient(c, d, a), cdB = orient(c, d, b);
      const bool proper = ((abC > crossEps && abD < -crossEps) || (abC < -crossEps && abD > crossEps)) &&
                          ((cdA > crossEps && cdB < -crossEps) || (cdA < -crossEps && cdB > crossEps));
      const bool touch = (std::abs(abC) <= crossEps && onSegment(a, b, c)) ||
                         (std::abs(abD) <= crossEps && onSegment(a, b, d)) ||
                         (std::abs(cdA) <= crossEps && onSegment(c, d, a)) ||
                         (std::abs(cdB) <= crossEps && onSegment(c, d, b));
      if (proper || touch) { if (why) *why = "boundary self-intersects or touches itself"; return false; }
    }
  }
  return true;
}


namespace {
/// Pure: the triangulation + boundary culling, given already-resolved inputs. Touches no
/// \c AppCommandState, so it is safe to run on a worker thread (architecture §8) or synchronously.
TinBuildResult RunSurfaceBuild(const SurfaceBuildInputs& in) {
  if (in.isVolume)
    return BuildTinVolumeSurface(in.volumeBaseVertsXyz, in.volumeBaseIndices, in.volumeCompVertsXyz,
                                 in.volumeCompIndices, in.originX, in.originY);
  TinBuildResult r = BuildTin(in.pts, in.constraints);
  if (r.ok())
    TinCullByBoundaries(r.indices, r.vertsXyz, in.cullLoops);
  return r;
}

bool SurfaceRebuildInFlight(const AppCommandState& st, std::uint64_t surfaceId) {
  using SurfaceJob = AppCommandState::SurfaceRebuildAsync;
  return std::any_of(st.surfaceRebuildAsync.begin(), st.surfaceRebuildAsync.end(),
                     [&](const std::unique_ptr<SurfaceJob>& j) { return j->surfaceId == surfaceId; });
}

/// Fills a volume-surface job. \p waitForParentsCurrent is the live-tick rule: do not sample a parent
/// that is still rebuilding this revision.
bool FillVolumeSurfaceInputs(AppCommandState& st, const CadSurface& surface, bool waitForParentsCurrent,
                             SurfaceBuildInputs* in, std::string* whyNotReady) {
  if (!in)
    return false;
  *in = {};
  in->originX = st.drawing.worldDocumentOriginX;
  in->originY = st.drawing.worldDocumentOriginY;
  in->isVolume = true;
  if (surface.volumeBaseName.empty() || surface.volumeComparisonName.empty()) {
    if (whyNotReady)
      *whyNotReady = "volume surface needs a base and a comparison surface";
    in->inputsIncomplete = true;
    in->incompleteReason = whyNotReady ? *whyNotReady : std::string();
    return false;
  }
  if (surface.volumeBaseName == surface.volumeComparisonName) {
    if (whyNotReady)
      *whyNotReady = "base and comparison must be different surfaces";
    in->inputsIncomplete = true;
    in->incompleteReason = whyNotReady ? *whyNotReady : std::string();
    return false;
  }
  const int bi = FindSurfaceIndex(st, surface.volumeBaseName);
  const int ci = FindSurfaceIndex(st, surface.volumeComparisonName);
  if (bi < 0 || ci < 0) {
    if (whyNotReady)
      *whyNotReady = "a parent surface no longer exists";
    in->inputsIncomplete = true;
    in->incompleteReason = whyNotReady ? *whyNotReady : std::string();
    return false;
  }
  const CadSurface& base = st.drawing.cadSurfaces[static_cast<size_t>(bi)];
  const CadSurface& comp = st.drawing.cadSurfaces[static_cast<size_t>(ci)];
  if (base.isVolumeSurface() || comp.isVolumeSurface()) {
    if (whyNotReady)
      *whyNotReady = "a volume surface cannot use another volume surface as a parent";
    in->inputsIncomplete = true;
    in->incompleteReason = whyNotReady ? *whyNotReady : std::string();
    return false;
  }
  const std::uint64_t baseId =
      static_cast<size_t>(bi) < st.drawing.cadSurfaceAttrs.size() ? st.drawing.cadSurfaceAttrs[static_cast<size_t>(bi)].id : 0;
  const std::uint64_t compId =
      static_cast<size_t>(ci) < st.drawing.cadSurfaceAttrs.size() ? st.drawing.cadSurfaceAttrs[static_cast<size_t>(ci)].id : 0;
  if (waitForParentsCurrent) {
    if (base.builtAtRevision != st.cadGpuRevision || comp.builtAtRevision != st.cadGpuRevision ||
        SurfaceRebuildInFlight(st, baseId) || SurfaceRebuildInFlight(st, compId)) {
      if (whyNotReady)
        *whyNotReady = "waiting for a parent surface to finish rebuilding";
      return false;  // not incomplete — retry next frame
    }
  }
  if (!base.tin || !comp.tin || base.tin->indices.empty() || comp.tin->indices.empty()) {
    if (whyNotReady)
      *whyNotReady = "a parent surface has no triangulation yet";
    in->inputsIncomplete = true;
    in->incompleteReason = whyNotReady ? *whyNotReady : std::string();
    return false;
  }
  in->volumeBaseVertsXyz = base.tin->vertsXyz;
  in->volumeBaseIndices = base.tin->indices;
  in->volumeCompVertsXyz = comp.tin->vertsXyz;
  in->volumeCompIndices = comp.tin->indices;
  return true;
}

} // namespace

void MarkVolumeSurfacesDirtyForParent(AppCommandState& st, const std::string& parentName) {
  for (CadSurface& s : st.drawing.cadSurfaces) {
    if (!s.isVolumeSurface())
      continue;
    if (s.volumeBaseName == parentName || s.volumeComparisonName == parentName)
      s.builtAtRevision = 0xFFFFFFFEu;
  }
}

/// Converts a world-space \ref TinBuildResult into a local-frame \ref CadTin (the local-storage
/// invariant: world = local + origin — architecture §11.8), or null when \p r has no surviving
/// triangle (a successful build that boundaries clipped to nothing is still "no surface" — REQ-001).
std::shared_ptr<CadTin> ToLocalTin(const TinBuildResult& r, double originX, double originY) {
  if (!r.ok() || r.indices.empty())
    return nullptr;
  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz.resize(r.vertsXyz.size());
  for (int i = 0; i < r.vertexCount(); ++i) {
    tin->vertsXyz[static_cast<size_t>(i) * 3 + 0] = r.vertsXyz[static_cast<size_t>(i) * 3 + 0] - originX;
    tin->vertsXyz[static_cast<size_t>(i) * 3 + 1] = r.vertsXyz[static_cast<size_t>(i) * 3 + 1] - originY;
    tin->vertsXyz[static_cast<size_t>(i) * 3 + 2] = r.vertsXyz[static_cast<size_t>(i) * 3 + 2];  // Z absolute
  }
  tin->indices = r.indices;
  return tin;
}


namespace {
void ApplyDefinitionEdgeSwaps(CadSurface& surface) {
  if (!surface.tin)
    return;
  if (surface.swappedEdgePicks.empty() && surface.deletedEdgePicks.empty())
    return;
  auto mut = std::make_shared<CadTin>(*surface.tin);
  for (const std::pair<double, double>& p : surface.swappedEdgePicks)
    (void)TinSwapInteriorEdgeNear(mut->vertsXyz, mut->indices, p.first, p.second);
  for (const std::pair<double, double>& p : surface.deletedEdgePicks)
    (void)TinDeleteInteriorEdgeNear(mut->indices, mut->vertsXyz, p.first, p.second);
  surface.tin = std::move(mut);
}

bool BuildGridKindSurface(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log) {
  surface.builtAtRevision = st.cadGpuRevision;
  surface.lastBuildIncomplete = false;
  if (surface.kind == SurfaceKind::GridVolume) {
    const int bi = FindSurfaceIndex(st, surface.volumeBaseName);
    const int ci = FindSurfaceIndex(st, surface.volumeComparisonName);
    if (bi < 0 || ci < 0) {
      surface.lastBuildMessage = "Missing grid-volume parent.";
      log.push_back("Surface \"" + surface.name + "\" not built: missing grid-volume parent.");
      return false;
    }
    const CadSurface& base = st.drawing.cadSurfaces[static_cast<size_t>(bi)];
    const CadSurface& comp = st.drawing.cadSurfaces[static_cast<size_t>(ci)];
    if (base.kind != SurfaceKind::Grid || comp.kind != SurfaceKind::Grid) {
      surface.lastBuildMessage = "Grid-volume parents must be grid surfaces.";
      log.push_back("Surface \"" + surface.name + "\" not built: " + surface.lastBuildMessage);
      return false;
    }
    const bool layoutOk = base.gridCols == comp.gridCols && base.gridRows == comp.gridRows &&
                          std::fabs(base.gridOriginX - comp.gridOriginX) < 1.0e-6 &&
                          std::fabs(base.gridOriginY - comp.gridOriginY) < 1.0e-6 &&
                          std::fabs(base.gridSpacingX - comp.gridSpacingX) < 1.0e-6 &&
                          std::fabs(base.gridSpacingY - comp.gridSpacingY) < 1.0e-6;
    if (!layoutOk) {
      surface.lastBuildMessage = "Grid-volume parents must share origin, spacing, and size.";
      log.push_back("Surface \"" + surface.name + "\" not built: " + surface.lastBuildMessage);
      return false;
    }
    std::string why;
    if (!GridVolumeSubtract(base.gridOriginX, base.gridOriginY, base.gridSpacingX, base.gridSpacingY,
                            base.gridCols, base.gridRows, base.gridZ, comp.gridZ, &surface.gridZ, &why)) {
      surface.tin.reset();
      surface.lastBuildMessage = why.empty() ? "grid-volume has no overlapping nodes" : why;
      log.push_back("Surface \"" + surface.name + "\" not built: " + surface.lastBuildMessage + ".");
      return false;
    }
    surface.gridOriginX = base.gridOriginX;
    surface.gridOriginY = base.gridOriginY;
    surface.gridSpacingX = base.gridSpacingX;
    surface.gridSpacingY = base.gridSpacingY;
    surface.gridCols = base.gridCols;
    surface.gridRows = base.gridRows;
  }
  if (!GridIndexValid(surface.gridCols, surface.gridRows, surface.gridZ)) {
    surface.tin.reset();
    surface.lastBuildMessage = "Grid is empty or not 2×2 or larger.";
    log.push_back("Surface \"" + surface.name + "\" not built: " + surface.lastBuildMessage);
    return false;
  }
  TinBuildResult r;
  GridBuildDisplayTin(surface.gridOriginX, surface.gridOriginY, surface.gridSpacingX, surface.gridSpacingY,
                      surface.gridCols, surface.gridRows, surface.gridZ, &r);
  std::shared_ptr<CadTin> tin = ToLocalTin(r, st.drawing.worldDocumentOriginX, st.drawing.worldDocumentOriginY);
  if (!tin) {
    surface.lastBuildMessage = "Grid display TIN is empty.";
    log.push_back("Surface \"" + surface.name + "\" not built: " + surface.lastBuildMessage);
    return false;
  }
  surface.tin = std::move(tin);
  ApplyDefinitionEdgeSwaps(surface);
  surface.lastBuildMessage = r.message;
  log.push_back("Surface \"" + surface.name + "\": grid, " + std::to_string(surface.vertexCount()) +
                " points, " + std::to_string(surface.triangleCount()) + " triangles.");
  return true;
}

bool BuildCorridorSurface(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log) {
  surface.builtAtRevision = st.cadGpuRevision;
  SurfaceBuildInputs in = ResolveSurfaceInputs(st, surface, log);
  in.pts.clear();
  in.constraints.clear();
  in.constraintSourceIds.clear();
  std::vector<CadSurfaceBreakline> kept;
  for (const CadSurfaceBreakline& fl : surface.corridorFeatureLines) {
    ResolvedChain chain;
    if (!ResolveDefinitionChain(st, fl.entityId, /*requireClosed=*/false, chain))
      continue;
    kept.push_back(fl);
    for (const auto& v : chain.verts)
      in.pts.push_back({v[0], v[1], static_cast<float>(v[2])});
    AppendChainConstraints(chain, /*forceClosed=*/false, in.constraints, &in.constraintSourceIds, fl.entityId);
  }
  surface.corridorFeatureLines = std::move(kept);
  if (in.pts.empty()) {
    surface.tin.reset();
    surface.lastBuildIncomplete = false;
    surface.lastBuildMessage = "Corridor surface has no feature lines.";
    log.push_back("Surface \"" + surface.name + "\": " + surface.lastBuildMessage);
    return false;
  }
  const TinBuildResult r = RunSurfaceBuild(in);
  surface.lastBuildIncomplete = false;
  if (!r.ok()) {
    surface.lastBuildMessage = r.message;
    log.push_back("Surface \"" + surface.name + "\" not built: " + r.message);
    return false;
  }
  std::shared_ptr<CadTin> tin = ToLocalTin(r, in.originX, in.originY);
  if (!tin) {
    surface.lastBuildMessage = "Corridor triangulation is empty.";
    log.push_back("Surface \"" + surface.name + "\" not built: " + surface.lastBuildMessage);
    return false;
  }
  surface.tin = std::move(tin);
  ApplyDefinitionEdgeSwaps(surface);
  surface.lastBuildMessage = r.message;
  log.push_back("Surface \"" + surface.name + "\": corridor, " + std::to_string(surface.vertexCount()) +
                " points, " + std::to_string(surface.triangleCount()) + " triangles.");
  return true;
}

} // namespace

bool RefreshSurfaceLabelValues(AppCommandState& st, const CadSurface& surface, bool invalidateCache = true) {
  const auto surfaceIt = std::find_if(st.drawing.cadSurfaces.begin(), st.drawing.cadSurfaces.end(),
                                     [&](const CadSurface& candidate) { return &candidate == &surface; });
  if (surfaceIt == st.drawing.cadSurfaces.end()) return false;
  const size_t si = static_cast<size_t>(surfaceIt - st.drawing.cadSurfaces.begin());
  if (si >= st.drawing.cadSurfaceAttrs.size() || st.drawing.cadSurfaceAttrs[si].id == 0) return false;
  const std::uint64_t id = st.drawing.cadSurfaceAttrs[si].id;
  const bool hasBuiltTin = surface.tin && !surface.tin->indices.empty();
  auto elevation = [&](double x, double y, double* z) {
    return hasBuiltTin && TinElevationAt(surface.tin->vertsXyz, surface.tin->indices, x, y, z);
  };
  bool changed = false;
  for (CadAnnotation& label : st.drawing.cadAnnotations) {
    if (label.surfaceLabelForId != id) continue;
    if (!hasBuiltTin) {
      const std::string unavailable = "Surface value unavailable";
      if (label.text != unavailable) { label.text = unavailable; changed = true; }
      continue;
    }
    if (label.surfaceLabelValues.empty()) continue;
    bool valueAvailable = false;
    double z0 = 0.0, z1 = 0.0;
    if (label.surfaceLabelKind == 1 && label.surfaceLabelValues.size() == 2 &&
        elevation(label.surfaceLabelValues[0], label.surfaceLabelValues[1], &z0)) {
      label.text = "Elev " + FormatLinear(z0, st.displayLinearPrecision);
      changed = true; valueAvailable = true;
    } else if (label.surfaceLabelKind == 2 && label.surfaceLabelValues.size() == 4 &&
               elevation(label.surfaceLabelValues[0], label.surfaceLabelValues[1], &z0) &&
               elevation(label.surfaceLabelValues[2], label.surfaceLabelValues[3], &z1)) {
      const double run = std::hypot(label.surfaceLabelValues[2] - label.surfaceLabelValues[0],
                                    label.surfaceLabelValues[3] - label.surfaceLabelValues[1]);
      if (run > 1.e-9) {
        label.text = "Slope " + FormatLinear((z1 - z0) / run * 100.0, st.displayLinearPrecision) + "%";
        changed = true; valueAvailable = true;
      }
    } else if (label.surfaceLabelKind == 3 && label.surfaceLabelValues.size() == 1 &&
               elevation(label.insX, label.insY, &z0) &&
               std::abs(z0 - label.surfaceLabelValues[0]) <= 0.01) {
      const std::string text = "Elev " + FormatLinear(label.surfaceLabelValues[0], st.displayLinearPrecision);
      if (label.text != text) { label.text = text; changed = true; }
      valueAvailable = true;
    } else if (label.surfaceLabelKind == 4 && label.surfaceLabelValues.size() == 2) {
      const WatershedResult watersheds = ComputeWatershed(surface.tin->vertsXyz, surface.tin->indices);
      const WatershedBasin* nearest = nullptr;
      double nearestD2 = std::numeric_limits<double>::infinity();
      for (const WatershedBasin& basin : watersheds.basins) {
        const double dx = basin.drainX - label.surfaceLabelValues[0];
        const double dy = basin.drainY - label.surfaceLabelValues[1];
        if (dx * dx + dy * dy < nearestD2) { nearestD2 = dx * dx + dy * dy; nearest = &basin; }
      }
      if (nearest && nearestD2 < 1.e-6) {
        label.text = "Watershed " + FormatLinear(nearest->area2d, st.displayLinearPrecision) + " sq ft";
        changed = true; valueAvailable = true;
      }
    }
    if (!valueAvailable && (label.surfaceLabelKind == 1 || label.surfaceLabelKind == 2 ||
                            label.surfaceLabelKind == 3 || label.surfaceLabelKind == 4)) {
      const std::string unavailable = "Surface value unavailable";
      if (label.text != unavailable) { label.text = unavailable; changed = true; }
    }
  }
  auto legendCells = [&](const std::string& kind) {
    std::vector<std::string> cells = {"Range", "Upper bound", "Color"};
    if (!hasBuiltTin) return std::vector<std::string>{"Analysis", "Surface value unavailable", "—"};
    const SurfaceStyle* style = SurfaceStyles::Find(st.drawing.surfaceStyles, surface.styleName);
    if (kind == "watershed") {
      const WatershedResult result = ComputeWatershed(surface.tin->vertsXyz, surface.tin->indices);
      for (const WatershedBasin& basin : result.basins) {
        cells.push_back("Basin " + std::to_string(basin.id));
        cells.push_back(FormatLinear(basin.area2d, st.displayLinearPrecision) + " sq ft");
        cells.push_back(basin.drain == DrainKind::Depression ? "depression" : basin.drain == DrainKind::Flat ? "flat" : "boundary");
      }
    } else if (kind == "contour" && style) {
      const SurfaceStats stats = ComputeSurfaceStats(surface.tin->vertsXyz, surface.tin->indices,
                                                      static_cast<int>(surface.breaklines.size()), surface.isVolumeSurface());
      std::vector<double> levels = style->userContourFt;
      if (levels.empty() && style->minorIntervalFt > 0.0 && stats.built) {
        const int first = static_cast<int>(std::ceil(stats.minZ / style->minorIntervalFt));
        const int last = static_cast<int>(std::floor(stats.maxZ / style->minorIntervalFt));
        if (last - first < 1000) for (int n = first; n <= last; ++n) levels.push_back(n * style->minorIntervalFt);
      }
      for (double level : levels) { cells.push_back("Contour"); cells.push_back(FormatLinear(level, st.displayLinearPrecision)); cells.push_back("—"); }
    } else if (style) {
      const bool arrows = kind == "slopearrow";
      const std::vector<SurfaceBand>& bands = arrows ? style->arrowBands : style->bands;
      const bool modeMatches = arrows || (kind == "elevation" && style->analysisMode == SurfaceAnalysisMode::Elevation) ||
                               (kind == "slope" && style->analysisMode == SurfaceAnalysisMode::Slope);
      if (modeMatches) {
        double lower = -std::numeric_limits<double>::infinity();
        for (const SurfaceBand& band : bands) {
          cells.push_back(std::isfinite(lower) ? "(" + FormatLinear(lower, st.displayLinearPrecision) + ", " + FormatLinear(band.upperBound, st.displayLinearPrecision) + "]" : "≤ " + FormatLinear(band.upperBound, st.displayLinearPrecision));
          cells.push_back(FormatLinear(band.upperBound, st.displayLinearPrecision)); cells.push_back(band.color); lower = band.upperBound;
        }
      }
    }
    return cells;
  };
  for (CadTable& table : st.drawing.cadTables) {
    if (table.surfaceLegendForId != id) continue;
    std::vector<std::string> cells = legendCells(table.surfaceLegendKind);
    if (table.cells != cells || table.cols != 3) {
      table.cells = std::move(cells);
      table.cols = 3;
      if (table.cells.size() > 3) CadTableFitToContent(&table, st.drawing.modelUnitsPerPlottedInch);
      changed = true;
    }
  }
  if (changed && invalidateCache) BumpCadGpuCache(st);
  return changed;
}

static bool BuildSurfaceFromSourcesImpl(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log) {
  if (surface.gradingGroup && surface.gradingMembers.empty()) {
    surface.tin.reset();
    surface.builtAtRevision = st.cadGpuRevision;
    surface.lastBuildIncomplete = false;
    surface.lastBuildMessage = "Empty grading group.";
    log.push_back("Grading group \"" + surface.name + "\": no members.");
    return true;
  }
  // Synchronous path: explicit user actions (create, the manual Rebuild button) that should show a
  // result immediately rather than waiting a frame for the async path below to pick them up.
  if (surface.kind == SurfaceKind::Grid || surface.kind == SurfaceKind::GridVolume)
    return BuildGridKindSurface(st, surface, log);
  if (surface.kind == SurfaceKind::Corridor)
    return BuildCorridorSurface(st, surface, log);
  if (surface.isVolumeSurface()) {
    SurfaceBuildInputs in;
    std::string why;
    if (!FillVolumeSurfaceInputs(st, surface, /*waitForParentsCurrent=*/false, &in, &why)) {
      surface.builtAtRevision = st.cadGpuRevision;
      surface.lastBuildIncomplete = true;
      surface.lastBuildMessage = "Not rebuilt: " + (why.empty() ? in.incompleteReason : why) + ".";
      log.push_back("Surface \"" + surface.name + "\" not built: " +
                    (why.empty() ? in.incompleteReason : why) + ".");
      return false;
    }
    const TinBuildResult r = RunSurfaceBuild(in);
    surface.builtAtRevision = st.cadGpuRevision;
    surface.lastBuildIncomplete = false;
    if (!r.ok()) {
      surface.lastBuildMessage = r.message;
      log.push_back("Surface \"" + surface.name + "\" not built: " + r.message);
      return false;
    }
    std::shared_ptr<CadTin> tin = ToLocalTin(r, in.originX, in.originY);
    if (!tin) {
      surface.lastBuildMessage = r.message.empty() ? "No overlapping volume surface." : r.message;
      log.push_back("Surface \"" + surface.name + "\" not built: " + surface.lastBuildMessage);
      return false;
    }
    surface.tin = std::move(tin);
    ApplyDefinitionEdgeSwaps(surface);
    surface.lastBuildMessage = r.message;
    log.push_back("Surface \"" + surface.name + "\": volume TIN, " + std::to_string(surface.vertexCount()) +
                  " points, " + std::to_string(surface.triangleCount()) + " triangles.");
    return true;
  }
  const SurfaceBuildInputs in = ResolveSurfaceInputs(st, surface, log);
  if (in.inputsIncomplete) {
    // REQ-086: a source that could not be read leaves the surface exactly as it was — no partial
    // rebuild on what survived. The revision IS advanced so the tick does not reopen a missing file
    // every frame; `lastBuildIncomplete` is what keeps the surface showing as not-current.
    surface.builtAtRevision = st.cadGpuRevision;
    surface.lastBuildIncomplete = true;
    surface.lastBuildMessage = "Not rebuilt: " + in.incompleteReason + ".";
    return false;
  }
  const TinBuildResult r = RunSurfaceBuild(in);
  surface.builtAtRevision = st.cadGpuRevision;
  surface.lastBuildIncomplete = false;
  if (surface.gradingGroup &&
      (r.conflictingDuplicates > 0 || r.constraintsUnresolved > 0)) {
    surface.lastBuildIncomplete = true;
    surface.lastBuildMessage =
        "Conflicting grading elevations or unresolved grading boundary edges.";
    log.push_back("Grading group \"" + surface.name +
                  "\": " + surface.lastBuildMessage);
    return false;
  }
  if (!r.ok()) {
    // No partial surface, and the previous triangulation is left alone (REQ-001).
    surface.lastBuildMessage = r.message;
    log.push_back("Surface \"" + surface.name + "\" not built: " + r.message);
    return false;
  }
  std::shared_ptr<CadTin> tin = ToLocalTin(r, in.originX, in.originY);
  if (!tin) {
    surface.lastBuildMessage = "Boundaries left no surface.";
    log.push_back("Surface \"" + surface.name + "\" not built: boundaries left no surface.");
    return false;
  }

  // Replace the pointer, never write through it (architecture §11.5).
  surface.tin = std::move(tin);
  ApplyDefinitionEdgeSwaps(surface);
  surface.lastBuildMessage = r.message;

  std::string msg = "Surface \"" + surface.name + "\": " + std::to_string(surface.vertexCount()) +
                    " points, " + std::to_string(surface.triangleCount()) + " triangles.";
  if (!r.message.empty())
    msg += " " + r.message;
  if (r.constraintsUnresolved > 0)
    msg += " " + std::to_string(r.constraintsUnresolved) + " constraint edge(s) could not be enforced.";
  log.push_back(msg);
  return true;
}

bool BuildSurfaceFromSources(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log) {
  if (surface.dataShortcutReadOnly) {
    log.push_back("Surface reference \"" + surface.name + "\" is read-only; refresh it from its project shortcut instead.");
    return false;
  }
  const bool built = BuildSurfaceFromSourcesImpl(st, surface, log);
  if (built) { RefreshSurfaceLabelValues(st, surface); RefreshSurfaceProfilesForSurface(st, surface); }
  return built;
}

void RefreshSurfaceLegendTablesForStyle(AppCommandState& st, const std::string& styleName) {
  bool changed = false;
  for (const CadSurface& surface : st.drawing.cadSurfaces)
    if (surface.styleName == styleName)
      changed = RefreshSurfaceLabelValues(st, surface, false) || changed;
  if (changed) BumpCadGpuCacheOnly(st);
}

void RefreshSurfaceDependentsAfterReplacement(AppCommandState& st, size_t surfaceIndex,
                                              std::vector<std::string>& log) {
  if (surfaceIndex >= st.drawing.cadSurfaces.size()) return;
  CadSurface& surface = st.drawing.cadSurfaces[surfaceIndex];
  ReDrapeLinkedToSurface(st, surfaceIndex, log, false);
  (void)RefreshSurfaceLabelValues(st, surface, false);
  (void)RefreshSurfaceProfilesForSurface(st, surface, false);
  BumpCadGpuCache(st);
}

void RefreshSurfaceProfilesForAlignment(AppCommandState& st, std::uint64_t alignmentId,
                                        std::vector<std::string>& log) {
  if (alignmentId == 0) return;
  bool changed = false;
  for (const CadSurfaceProfile& profile : st.drawing.surfaceProfiles) {
    if (profile.alignmentId != alignmentId) continue;
    const int surfaceIndex = FindSurfaceIndexById(st, profile.surfaceId);
    if (surfaceIndex < 0) continue;
    changed = RefreshSurfaceProfilesForSurface(st, st.drawing.cadSurfaces[static_cast<size_t>(surfaceIndex)], false,
                                               alignmentId) || changed;
  }
  if (changed) {
    BumpCadGpuCache(st);
    log.push_back("Saved surface profile refreshed for edited alignment.");
  }
}

int CreateSurfaceFromPointGroups(AppCommandState& st, const std::string& name,
                                 const std::vector<std::string>& groupNames,
                                 std::vector<std::string>& log) {
  if (name.empty()) {
    log.push_back("Surface name cannot be empty.");
    return -1;
  }
  if (FindSurfaceIndex(st, name) >= 0) {
    log.push_back("A surface named \"" + name + "\" already exists.");
    return -1;
  }
  // Bumped before the build (not after, as every other surface-mutating call site here does) so the
  // freshly built surface's builtAtRevision already matches the revision this creation settles at —
  // otherwise the very next TickSurfaceRebuilds tick would see it one revision stale and redispatch
  // a redundant rebuild of a surface that was just built moments ago.
  BumpCadGpuCache(st);
  CadSurface s;
  s.name = name;
  s.sourcePointGroups = groupNames;
  const bool built = BuildSurfaceFromSources(st, s, log);
  if (!built && s.lastBuildMessage.empty())
    s.lastBuildMessage = "Not built.";
  if (groupNames.empty() && !s.tin)
    log.push_back("Surface \"" + name + "\" created (empty — add points, breaklines or files, then rebuild).");

  st.drawing.cadSurfaces.push_back(std::move(s));
  AppendNewObjectAttrs(st, st.drawing.cadSurfaceAttrs, st.drawing.cadSurfaces.size(), ObjectLayerKind::Surface,
                       st.drawing.cadSurfaces.back().name);  // REQ-361
  EnsureAttrCounts(st);  // owns attribute-array growth for every entity type, surfaces included
  return static_cast<int>(st.drawing.cadSurfaces.size()) - 1;
}

int CreateSurfaceFromVolumeParents(AppCommandState& st, const std::string& name, const std::string& baseName,
                                   const std::string& comparisonName, std::vector<std::string>& log) {
  if (name.empty()) {
    log.push_back("Surface name cannot be empty.");
    return -1;
  }
  if (FindSurfaceIndex(st, name) >= 0) {
    log.push_back("A surface named \"" + name + "\" already exists.");
    return -1;
  }
  BumpCadGpuCache(st);
  CadSurface s;
  s.name = name;
  s.kind = SurfaceKind::TinVolume;
  s.volumeBaseName = baseName;
  s.volumeComparisonName = comparisonName;
  const bool built = BuildSurfaceFromSources(st, s, log);
  if (!built && s.lastBuildMessage.empty())
    s.lastBuildMessage = "Not built.";
  st.drawing.cadSurfaces.push_back(std::move(s));
  AppendNewObjectAttrs(st, st.drawing.cadSurfaceAttrs, st.drawing.cadSurfaces.size(), ObjectLayerKind::Surface,
                       st.drawing.cadSurfaces.back().name);  // REQ-361
  EnsureAttrCounts(st);
  return static_cast<int>(st.drawing.cadSurfaces.size()) - 1;
}

bool DetachSurfaceStyleIfShared(AppCommandState& st, size_t surfaceIndex, std::vector<std::string>* log) {
  if (surfaceIndex >= st.drawing.cadSurfaces.size())
    return false;
  SurfaceStyles::EnsureStandard(st.drawing.surfaceStyles);
  CadSurface& surf = st.drawing.cadSurfaces[surfaceIndex];
  const SurfaceStyle* resolved = SurfaceStyles::Resolve(st.drawing.surfaceStyles, surf.styleName);
  if (!resolved)
    return false;
  const int users =
      SurfaceStyles::CountSurfacesResolvingTo(st.drawing.cadSurfaces, st.drawing.surfaceStyles, resolved->name);
  if (users <= 1) {
    if (log)
      log->push_back("SURFSTYLE — \"" + surf.name + "\" already uses its own style \"" + resolved->name +
                     "\".");
    return false;
  }
  const std::string from = resolved->name;
  SurfaceStyle copy = *resolved;
  copy.name = SurfaceStyles::UniqueCopyName(st.drawing.surfaceStyles, surf.name);
  st.drawing.surfaceStyles.push_back(copy);
  surf.styleName = copy.name;
  BumpCadGpuCache(st);
  if (log)
    log->push_back("SURFSTYLE — surface \"" + surf.name + "\" now uses style \"" + copy.name +
                   "\" (copied from \"" + from + "\").");
  return true;
}

void EraseSurfaceAtIndex(AppCommandState& st, size_t index) {
  if (index >= st.drawing.cadSurfaces.size())
    return;
  // Surface selections are index-based; remove the deleted reference and shift later surfaces down.
  for (size_t i = st.selection.size(); i > 0; --i) {
    SelectedEntity& selected = st.selection[i - 1];
    if (selected.type != SelectedEntity::Type::Surface)
      continue;
    if (selected.index == static_cast<int>(index))
      st.selection.erase(st.selection.begin() + static_cast<std::ptrdiff_t>(i - 1));
    else if (selected.index > static_cast<int>(index))
      --selected.index;
  }
  st.drawing.cadSurfaces.erase(st.drawing.cadSurfaces.begin() + static_cast<std::ptrdiff_t>(index));
  if (index < st.drawing.cadSurfaceAttrs.size())
    st.drawing.cadSurfaceAttrs.erase(st.drawing.cadSurfaceAttrs.begin() + static_cast<std::ptrdiff_t>(index));
  BumpCadGpuCache(st);
}

void TickSurfaceRebuilds(AppCommandState& st, std::vector<std::string>& log) {
  using SurfaceJob = AppCommandState::SurfaceRebuildAsync;
  bool appliedSurface = false;
  std::unordered_set<std::uint64_t> presentSurfaceIds;
  for (size_t i = 0; i < st.drawing.cadSurfaces.size() && i < st.drawing.cadSurfaceAttrs.size(); ++i)
    if (st.drawing.cadSurfaceAttrs[i].id != 0) presentSurfaceIds.insert(st.drawing.cadSurfaceAttrs[i].id);
  for (CadSurfaceProfile& profile : st.drawing.surfaceProfiles) {
    const bool hasAlignment = std::any_of(st.drawing.alignments.begin(), st.drawing.alignments.end(),
        [&](const alignment::Alignment& a) { return a.id == profile.alignmentId; });
    const int surfaceIndex = FindSurfaceIndexById(st, profile.surfaceId);
    std::string unavailable;
    if (!hasAlignment) unavailable = "Source alignment is missing.";
    else if (surfaceIndex < 0) unavailable = "Source surface is missing.";
    else {
      const CadSurface& surface = st.drawing.cadSurfaces[static_cast<size_t>(surfaceIndex)];
      if (!surface.tin || surface.tin->indices.empty()) unavailable = "Source surface has no built TIN.";
    }
    if (!unavailable.empty()) {
      if (profile.sourceStatus != unavailable) {
        profile.sourceStatus = unavailable;
        log.push_back("Profile \"" + profile.name + "\" unavailable: " + unavailable + " Last saved view retained.");
      }
    } else if (profile.sourceStatus == "Source alignment is missing." ||
               profile.sourceStatus == "Source surface is missing." ||
               profile.sourceStatus == "Source surface has no built TIN." ||
               profile.sourceStatus == "Source alignment unavailable." ||
               profile.sourceStatus == "Source surface unavailable.") {
      const std::string priorStatus = profile.sourceStatus;
      appliedSurface = RefreshSurfaceProfilesForSurface(
          st, st.drawing.cadSurfaces[static_cast<size_t>(surfaceIndex)], false,
          profile.alignmentId) || appliedSurface;
      if (profile.sourceStatus.empty())
        log.push_back("Profile \"" + profile.name + "\" source is available again; profile view refreshed.");
      else if (profile.sourceStatus != priorStatus)
        log.push_back("Profile \"" + profile.name + "\" unavailable: " + profile.sourceStatus);
    }
  }
  bool invalidatedMissingLabel = false;
  for (CadAnnotation& label : st.drawing.cadAnnotations) {
    if (label.surfaceLabelForId != 0 && presentSurfaceIds.find(label.surfaceLabelForId) == presentSurfaceIds.end() &&
        label.text != "Surface value unavailable") {
      label.text = "Surface value unavailable";
      invalidatedMissingLabel = true;
    }
  }
  const std::vector<std::string> unavailableLegend = {
      "Analysis", "Surface value unavailable", "—"};
  for (CadTable& table : st.drawing.cadTables) {
    if (table.surfaceLegendForId == 0 || presentSurfaceIds.find(table.surfaceLegendForId) != presentSurfaceIds.end())
      continue;
    if (table.cells != unavailableLegend || table.cols != 3) {
      table.cells = unavailableLegend;
      table.cols = 3;
      invalidatedMissingLabel = true;
    }
  }
  for (const CadSurface& surface : st.drawing.cadSurfaces)
    if (!surface.tin || surface.tin->indices.empty())
      invalidatedMissingLabel = RefreshSurfaceLabelValues(st, surface, false) || invalidatedMissingLabel;
  if (invalidatedMissingLabel) BumpCadGpuCacheOnly(st);

  // Reap finished workers first, so a surface freed up this frame can be redispatched this same
  // frame rather than waiting one extra frame.
  for (size_t i = 0; i < st.surfaceRebuildAsync.size();) {
    SurfaceJob& job = *st.surfaceRebuildAsync[i];
    if (!job.done.load(std::memory_order_acquire)) {
      ++i;
      continue;
    }
    job.thread.join();
    const int si = FindSurfaceIndexById(st, job.surfaceId);
    // Applied only if the surface still exists AND nothing has changed since this job was
    // dispatched (architecture §8 rule 4). Either condition failing means discard: the surface was
    // erased, or an undo / further edit landed while this ran — REQ-069's "the in-flight result is
    // discarded." A discarded surface simply looks dirty again next tick and gets redispatched.
    if (si >= 0 && st.cadGpuRevision == job.generation) {
      CadSurface& surface = st.drawing.cadSurfaces[static_cast<size_t>(si)];
      const TinBuildResult& r = job.result;
      std::shared_ptr<CadTin> tin = ToLocalTin(r, job.originX, job.originY);
      if (tin) {
        surface.tin = std::move(tin);  // replace the pointer, never write through it (§11.5)
        ApplyDefinitionEdgeSwaps(surface);
        surface.lastBuildMessage = r.message;
        std::string msg = "Surface \"" + surface.name + "\": " + std::to_string(surface.vertexCount()) +
                          " points, " + std::to_string(surface.triangleCount()) + " triangles.";
        if (!r.message.empty())
          msg += " " + r.message;
        if (r.constraintsUnresolved > 0)
          msg += " " + std::to_string(r.constraintsUnresolved) + " constraint edge(s) could not be enforced.";
        log.push_back(msg);
        MarkVolumeSurfacesDirtyForParent(st, surface.name);
        // A link means the same thing whichever way the rebuild was driven (ADR-069 (b)): the
        // command path above and this async reap are the only two places a TIN is replaced.
        ReDrapeLinkedToSurface(st, static_cast<size_t>(si), log, false);
        (void)RefreshSurfaceLabelValues(st, surface, false);
        (void)RefreshSurfaceProfilesForSurface(st, surface, false);
        appliedSurface = true;
      } else {
        // No partial surface; the previous triangulation (if any) is left alone (REQ-001).
        surface.lastBuildMessage = r.ok() ? "Boundaries left no surface." : r.message;
        log.push_back("Surface \"" + surface.name + "\" not rebuilt: " + surface.lastBuildMessage);
      }
      surface.builtAtRevision = job.generation;
      surface.lastBuildIncomplete = false;  // a result that landed means the inputs were complete
    }
    st.surfaceRebuildAsync.erase(st.surfaceRebuildAsync.begin() + static_cast<std::ptrdiff_t>(i));
  }

  if (appliedSurface) BumpCadGpuCacheOnly(st);

  // Dispatch a rebuild for every surface whose definition might have changed and does not already
  // have one in flight — one dispatch per surface per revision is REQ-069's "at most one rebuild per
  // command/undo boundary," since a command bumps cadGpuRevision once however many points it moved.
  for (size_t sIdx = 0; sIdx < st.drawing.cadSurfaces.size(); ++sIdx) {
    CadSurface& surface = st.drawing.cadSurfaces[sIdx];
    if (surface.dataShortcutReadOnly) {
      surface.builtAtRevision = st.cadGpuRevision;
      continue; // shortcut TINs update only through RefreshProjectSurfaceReferences
    }
    // REQ-404 / ADR-070: only membership edits and explicit grading rebuilds
    // refresh these snapshots.
    if (surface.gradingGroup) {
      surface.builtAtRevision = st.cadGpuRevision;
      continue;
    }
    if (surface.builtAtRevision == st.cadGpuRevision)
      continue;
    // The id is assigned by EnsureEntityIds, which main.cpp runs immediately before this every frame.
    // A surface created since that sweep has id 0 and is skipped for exactly one frame rather than
    // dispatched under a key that cannot be looked up again.
    const std::uint64_t surfaceId =
        sIdx < st.drawing.cadSurfaceAttrs.size() ? st.drawing.cadSurfaceAttrs[sIdx].id : 0;
    if (surfaceId == 0)
      continue;
    const bool alreadyRunning = SurfaceRebuildInFlight(st, surfaceId);
    if (alreadyRunning)
      continue;
    if (surface.kind == SurfaceKind::Grid || surface.kind == SurfaceKind::GridVolume ||
        surface.kind == SurfaceKind::Corridor) {
      std::vector<std::string> buildLog;
      (void)BuildSurfaceFromSources(st, surface, buildLog);
      for (std::string& m : buildLog)
        log.push_back(std::move(m));
      continue;
    }

    // A fixed triangulation (a TIN imported from a Civil 3D DWG) has no sources to rebuild from, so a
    // revision change must leave it alone rather than "rebuild" it from nothing.
    if (surface.tin && !surface.isVolumeSurface() && surface.sourcePointGroups.empty() &&
        surface.sourcePointFiles.empty() && surface.addedPointXyz.empty() && surface.breaklines.empty() &&
        surface.contourSources.empty() && surface.boundaries.empty() && surface.movedPoints.empty() &&
        surface.corridorFeatureLines.empty()) {
      const bool refreshedLabels = RefreshSurfaceLabelValues(st, surface, false);
      const bool refreshedProfiles = RefreshSurfaceProfilesForSurface(st, surface, false);
      if (refreshedLabels || refreshedProfiles) BumpCadGpuCacheOnly(st);
      surface.builtAtRevision = st.cadGpuRevision;
      continue;
    }

    std::vector<std::string> resolveLog;
    SurfaceBuildInputs inputs;
    if (surface.isVolumeSurface()) {
      std::string why;
      if (!FillVolumeSurfaceInputs(st, surface, /*waitForParentsCurrent=*/true, &inputs, &why)) {
        if (inputs.inputsIncomplete) {
          surface.builtAtRevision = st.cadGpuRevision;
          surface.lastBuildIncomplete = true;
          surface.lastBuildMessage = "Not rebuilt: " + (why.empty() ? inputs.incompleteReason : why) + ".";
          log.push_back("Surface \"" + surface.name + "\" not rebuilt: " +
                        (why.empty() ? inputs.incompleteReason : why) + ".");
        }
        continue;
      }
    } else {
    // Resolution against AppCommandState happens HERE, on the UI thread — the worker below receives
    // only the already-resolved, plain-data result and touches no drawing state (architecture §8
    // rule 1). This is also where dangling breakline/boundary ids actually get dropped from the
    // definition, so that observably happens the very next frame after the referenced entity is
    // deleted, not only once the (possibly slower) background triangulation finishes.
    inputs = ResolveSurfaceInputs(st, surface, resolveLog);
    for (std::string& m : resolveLog)
      log.push_back(std::move(m));

    if (inputs.inputsIncomplete) {
      // REQ-086: don't dispatch a worker to triangulate inputs already known to be short. The surface
      // keeps the triangulation it has. The revision is advanced so this does not re-read a missing
      // file every frame — `lastBuildIncomplete` carries the not-current state instead, and the retry
      // comes with the next drawing change or an explicit rebuild. ResolveSurfaceInputs already
      // logged which file and why.
      surface.builtAtRevision = st.cadGpuRevision;
      surface.lastBuildIncomplete = true;
      surface.lastBuildMessage = "Not rebuilt: " + inputs.incompleteReason + ".";
      continue;
    }
    }

    auto job = std::make_unique<SurfaceJob>();
    job->surfaceId = surfaceId;
    job->generation = st.cadGpuRevision;
    job->originX = st.drawing.worldDocumentOriginX;
    job->originY = st.drawing.worldDocumentOriginY;
    SurfaceJob* jobPtr = job.get();
    jobPtr->thread = std::thread([jobPtr, in = std::move(inputs)]() {
      if (jobPtr->cancel.load(std::memory_order_acquire)) {
        jobPtr->done.store(true, std::memory_order_release);
        return;
      }
      jobPtr->result = RunSurfaceBuild(in);
      jobPtr->done.store(true, std::memory_order_release);
    });
    st.surfaceRebuildAsync.push_back(std::move(job));
  }
}

/// Whether a Base/Comparison pair is ready to compare: both ids resolve, both are current (REQ-069's
/// own dirty check), and both have a triangulation. Shared by \ref TickVolumeDashboard's real
/// dispatch gate and `VOLDASH RECOMPUTE`'s synchronous test surrogate (TASK-095 §8) — REQ-073's
/// "picking a surface that is itself out of date is reflected as such" cannot drift between the live
/// path and the headless-testable one if both ask this single function.
bool VolumeDashboardReady(const AppCommandState& st, std::uint64_t baseId, std::uint64_t compId,
                          std::string* whyNot) {
  const int baseIx = FindSurfaceIndexById(st, baseId);
  const int compIx = FindSurfaceIndexById(st, compId);
  if (baseIx < 0 || compIx < 0) {
    if (whyNot)
      *whyNot = "a picked surface no longer exists";
    return false;
  }
  const CadSurface& baseSurf = st.drawing.cadSurfaces[static_cast<size_t>(baseIx)];
  const CadSurface& compSurf = st.drawing.cadSurfaces[static_cast<size_t>(compIx)];
  if (baseSurf.builtAtRevision != st.cadGpuRevision || compSurf.builtAtRevision != st.cadGpuRevision) {
    if (whyNot)
      *whyNot = "waiting for a surface to finish rebuilding";
    return false;
  }
  if (!baseSurf.tin || !compSurf.tin) {
    if (whyNot)
      *whyNot = "a surface has no triangulation yet";
    return false;
  }
  return true;
}

void TickVolumeDashboard(AppCommandState& st) {
  using DashJob = AppCommandState::VolumeDashboardAsync;
  AppCommandState::VolumeDashboardState& dash = st.volumeDashboard;

  // Reap a finished job first, so a selection change made while it ran can redispatch this same
  // frame rather than waiting one extra frame — the same ordering TickSurfaceRebuilds itself uses.
  if (dash.job && dash.job->done.load(std::memory_order_acquire)) {
    dash.job->thread.join();
    // Applied only if NOTHING has changed since dispatch: the drawing revision matches, AND the
    // panel's pick is still the pair this job was computed for (architecture §8 rule 4 / REQ-073's
    // own "the panel's surface pick changed... discarded"). Either failing means discard — the panel
    // simply looks stale again and redispatches below on this same tick.
    if (st.cadGpuRevision == dash.job->generation && dash.baseSurfaceId == dash.job->baseSurfaceId &&
        dash.comparisonSurfaceId == dash.job->comparisonSurfaceId &&
        dash.clipEntityId == dash.job->clipEntityId) {
      dash.lastResult = dash.job->result;
      dash.hasResult = true;
      dash.resultForRevision = dash.job->generation;
      dash.resultForBaseSurfaceId = dash.job->baseSurfaceId;
      dash.resultForComparisonSurfaceId = dash.job->comparisonSurfaceId;
      dash.resultForClipEntityId = dash.job->clipEntityId;
      dash.resultHasMap = dash.job->wantMap;
      dash.mapCutTrianglesXyz = std::move(dash.job->cutTrianglesXyz);
      dash.mapFillTrianglesXyz = std::move(dash.job->fillTrianglesXyz);
      RememberVolumeResult(st, dash.lastResult, SurfaceNameForId(st, dash.baseSurfaceId),
                           SurfaceNameForId(st, dash.comparisonSurfaceId));
    }
    dash.job.reset();
  }

  // Nothing to do while the panel is closed (ASSUMPTION-5, TASK-095: no benefit to recomputing what
  // nobody is looking at), nothing picked, or a job is already in flight.
  if (!dash.open || dash.baseSurfaceId == 0 || dash.comparisonSurfaceId == 0 || dash.job)
    return;

  if (!VolumeDashboardReady(st, dash.baseSurfaceId, dash.comparisonSurfaceId, nullptr))
    return;  // not resolvable, or a picked surface is itself out of date — REQ-073's own gate

  const CadSurface& baseSurf = st.drawing.cadSurfaces[static_cast<size_t>(FindSurfaceIndexById(st, dash.baseSurfaceId))];
  const CadSurface& compSurf =
      st.drawing.cadSurfaces[static_cast<size_t>(FindSurfaceIndexById(st, dash.comparisonSurfaceId))];

  // Already current for this exact pick, revision AND map want — nothing to recompute. The map-want
  // half is what makes turning the map ON after a mapless result redispatch: `resultHasMap` was
  // false, `dash.showMap` is now true, so the comparison below fails and a fresh job (this time
  // WITH the map) is dispatched, rather than the panel silently showing stale (absent) map geometry.
  if (dash.hasResult && dash.resultForRevision == st.cadGpuRevision &&
      dash.resultForBaseSurfaceId == dash.baseSurfaceId &&
      dash.resultForComparisonSurfaceId == dash.comparisonSurfaceId &&
      dash.resultForClipEntityId == dash.clipEntityId &&
      (dash.resultHasMap || !dash.showMap))
    return;

  std::vector<std::pair<double, double>> clipRing;
  std::string clipErr;
  if (!VolumeClipRingLocalXy(st, dash.clipEntityId, &clipRing, &clipErr))
    return;  // polyline gone; the panel reports it

  auto job = std::make_unique<DashJob>();
  job->baseSurfaceId = dash.baseSurfaceId;
  job->comparisonSurfaceId = dash.comparisonSurfaceId;
  job->clipEntityId = dash.clipEntityId;
  job->tinBase = baseSurf.tin;              // strong refs: architecture §11.5, see the struct's note
  job->tinComparison = compSurf.tin;
  job->clipRingXy = std::move(clipRing);
  job->wantMap = dash.showMap;
  job->generation = st.cadGpuRevision;
  DashJob* jobPtr = job.get();
  jobPtr->thread = std::thread([jobPtr]() {
    if (jobPtr->cancel.load(std::memory_order_acquire)) {
      jobPtr->done.store(true, std::memory_order_release);
      return;
    }
    const std::vector<std::pair<double, double>>* clip =
        jobPtr->clipRingXy.size() >= 3 ? &jobPtr->clipRingXy : nullptr;
    jobPtr->result = ComputeSurfaceVolume(
        jobPtr->tinBase->vertsXyz, jobPtr->tinBase->indices, jobPtr->tinComparison->vertsXyz,
        jobPtr->tinComparison->indices, jobPtr->wantMap ? &jobPtr->cutTrianglesXyz : nullptr,
        jobPtr->wantMap ? &jobPtr->fillTrianglesXyz : nullptr, clip);
    jobPtr->done.store(true, std::memory_order_release);
  });
  dash.job = std::move(job);
}

SurfaceBuildInputs ResolveSurfaceInputs(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log) {
  SurfaceBuildInputs in;
  in.originX = st.drawing.worldDocumentOriginX;
  in.originY = st.drawing.worldDocumentOriginY;

  if (surface.gradingGroup) {
    for (const auto& member : surface.gradingMembers) {
      if (!member.strips) {
        in.inputsIncomplete = true;
        in.incompleteReason = "missing baked grading strips";
        log.push_back("Surface \"" + surface.name + "\": " + in.incompleteReason);
        return in;
      }
      for (const auto& strip : *member.strips) {
        if (((member.infill && (strip.size() < 9 || strip.size() % 3 != 0)) ||
             (!member.infill && strip.size() != 12)) ||
            !std::all_of(strip.begin(), strip.end(),
                         [](double v) { return std::isfinite(v); })) {
          in.inputsIncomplete = true;
          in.incompleteReason = "invalid baked grading strip";
          log.push_back("Surface \"" + surface.name + "\": " + in.incompleteReason);
          return in;
        }
        if (member.infill) {
          std::string polygonError;
          if (!IsSimpleInfillPolygon(strip, &polygonError)) {
            in.inputsIncomplete = true;
            in.incompleteReason = "infill " + polygonError;
            log.push_back("Surface \"" + surface.name + "\": " + in.incompleteReason);
            return in;
          }
        }
        ResolvedChain chain;
        TinBoundaryLoop loop;
        loop.kind = TinBoundaryKind::Clip;
        for (size_t v = 0; v < strip.size(); v += 3) {
          const double x = strip[v] + in.originX, y = strip[v + 1] + in.originY;
          chain.verts.push_back({x, y, strip[v + 2]});
          loop.ring.emplace_back(x, y);
        }
        AppendChainConstraints(chain, true, in.constraints, &in.constraintSourceIds, member.gradingId);
        in.cullLoops.push_back(std::move(loop));
      }
    }
    if (!TinFindCrossingConflicts(in.constraints).empty()) {
      in.inputsIncomplete = true;
      in.incompleteReason = "grading strips cross at different elevations";
      log.push_back("Surface \"" + surface.name + "\": " + in.incompleteReason);
    }
    return in;
  }

  int unresolvedGroups = 0;
  for (const std::string& gname : surface.sourcePointGroups) {
    const int gi = FindPointGroupIndex(st, gname);
    if (gi < 0) {
      ++unresolvedGroups;
      log.push_back("Surface \"" + surface.name + "\": point group \"" + gname + "\" no longer exists.");
      continue;
    }
    for (int pi : ResolvePointGroup(st, st.drawing.pointGroups[static_cast<size_t>(gi)], &log)) {
      const SurveyPoint& p = st.drawing.surveyPoints[static_cast<size_t>(pi)];
      in.pts.push_back({p.easting + st.drawing.worldDocumentOriginX, p.northing + st.drawing.worldDocumentOriginY,
                        static_cast<float>(p.elevation)});
    }
  }
  (void)unresolvedGroups;

  for (const CadSurfacePointFile& pf : surface.sourcePointFiles) {
    std::vector<SurveyFilePoint> filePts;
    int skipped = 0;
    std::string err;
    if (!SurveyCsvReadPointsOnly(pf.path.c_str(), SurveyCsvLayoutFromUiIndex(pf.layoutIndex),
                                 pf.skipFirstRow, &filePts, &skipped, &err)) {
      in.inputsIncomplete = true;
      in.incompleteReason = "point file \"" + pf.path + "\" could not be read (" + err + ")";
      log.push_back("Surface \"" + surface.name + "\": " + in.incompleteReason +
                    ". Keeping the previous triangulation.");
      continue;
    }
    if (skipped > 0)
      log.push_back("Surface \"" + surface.name + "\": point file \"" + pf.path + "\" — " +
                    std::to_string(skipped) + " unreadable row(s) skipped.");
    for (const SurveyFilePoint& p : filePts)
      in.pts.push_back({p.easting, p.northing, p.elevation});
  }

  ApplyDefinitionPointEdits(surface, in);

  std::vector<CadSurfaceBreakline> resolvedBreaklines;
  int droppedBreaklines = 0;
  for (const CadSurfaceBreakline& bl : surface.breaklines) {
    ResolvedChain chain;
    if (!ResolveDefinitionChain(st, bl.entityId, /*requireClosed=*/false, chain)) {
      ++droppedBreaklines;
      continue;
    }
    resolvedBreaklines.push_back(bl);
    AppendChainConstraints(chain, /*forceClosed=*/false, in.constraints, &in.constraintSourceIds, bl.entityId);
  }
  surface.breaklines = std::move(resolvedBreaklines);
  if (droppedBreaklines > 0)
    log.push_back("Surface \"" + surface.name + "\": " + std::to_string(droppedBreaklines) +
                  " breakline(s) no longer exist and were removed from the definition.");

  std::vector<CadSurfaceBreakline> resolvedContours;
  int droppedContours = 0;
  for (const CadSurfaceBreakline& c : surface.contourSources) {
    ResolvedChain chain;
    if (!ResolveDefinitionChain(st, c.entityId, /*requireClosed=*/false, chain)) {
      ++droppedContours;
      continue;
    }
    resolvedContours.push_back(c);
    AppendChainConstraints(chain, /*forceClosed=*/false, in.constraints, &in.constraintSourceIds, c.entityId);
  }
  surface.contourSources = std::move(resolvedContours);
  if (droppedContours > 0)
    log.push_back("Surface \"" + surface.name + "\": " + std::to_string(droppedContours) +
                  " contour source(s) no longer exist and were removed from the definition.");

  std::vector<CadSurfaceBoundary> resolvedBoundaries;
  int droppedBoundaries = 0;
  for (const CadSurfaceBoundary& b : surface.boundaries) {
    ResolvedChain chain;
    if (!ResolveDefinitionChain(st, b.entityId, /*requireClosed=*/true, chain)) {
      ++droppedBoundaries;
      continue;
    }
    resolvedBoundaries.push_back(b);
    AppendChainConstraints(chain, /*forceClosed=*/true, in.constraints, &in.constraintSourceIds, b.entityId);
    TinBoundaryLoop loop;
    loop.kind = b.kind == CadBoundaryKind::Outer ? TinBoundaryKind::Outer
              : b.kind == CadBoundaryKind::Hide  ? TinBoundaryKind::Hide
              : b.kind == CadBoundaryKind::Show  ? TinBoundaryKind::Show
              : b.kind == CadBoundaryKind::Mask  ? TinBoundaryKind::Mask
                                                 : TinBoundaryKind::Clip;
    for (const auto& v : chain.verts)
      loop.ring.push_back({v[0], v[1]});
    in.cullLoops.push_back(std::move(loop));
  }
  surface.boundaries = std::move(resolvedBoundaries);
  if (droppedBoundaries > 0)
    log.push_back("Surface \"" + surface.name + "\": " + std::to_string(droppedBoundaries) +
                  " boundary(ies) no longer exist and were removed from the definition.");

  {
    std::vector<const TinBoundaryLoop*> clips;
    for (const TinBoundaryLoop& loop : in.cullLoops) {
      if (loop.kind == TinBoundaryKind::Clip)
        clips.push_back(&loop);
    }
    if (!clips.empty()) {
      std::vector<TinInputPoint> kept;
      kept.reserve(in.pts.size());
      for (const TinInputPoint& p : in.pts) {
        bool inside = false;
        for (const TinBoundaryLoop* clip : clips) {
          if (TinPointInPolygon(p.x, p.y, clip->ring)) {
            inside = true;
            break;
          }
        }
        if (inside)
          kept.push_back(p);
      }
      in.pts = std::move(kept);
    }
  }

  ApplySurfaceCrossingCorrections(surface, in);
  for (const TinCrossingIssue& issue : TinFindCrossingConflicts(in.constraints))
    log.push_back("Surface \"" + surface.name + "\": breaklines cross at (" +
                  std::to_string(issue.x) + ", " + std::to_string(issue.y) +
                  ") with different elevations (" + std::to_string(issue.zFromA) + " vs " +
                  std::to_string(issue.zFromB) + ").");

  return in;
}

void RunSurfaceAddFile(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() < 2 || f[0].empty() || f[1].empty()) {
    log.push_back("SURFACEADDFILE — usage: SURFACEADDFILE <surface>, <path>[, <PNEZD|PENZD|NEZ|ENZ>[, HEADER]].");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACEADDFILE — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurfacePointFile pf;
  if (SurfaceRefusesPointEdits(st.drawing.cadSurfaces[static_cast<size_t>(si)], "SURFACEADDFILE", log)) return;
  pf.path = f[1];
  if (f.size() >= 3 && !f[2].empty()) {
    const std::string lay = StringUtil::toLowerAsciiCopy(f[2]);
    if (lay == "pnezd")      pf.layoutIndex = 0;
    else if (lay == "penzd") pf.layoutIndex = 1;
    else if (lay == "nez")   pf.layoutIndex = 2;
    else if (lay == "enz")   pf.layoutIndex = 3;
    else {
      log.push_back("SURFACEADDFILE — layout must be PNEZD, PENZD, NEZ or ENZ.");
      return;
    }
  }
  for (size_t i = 3; i < f.size(); ++i)
    if (StringUtil::toLowerAsciiCopy(f[i]) == "header")
      pf.skipFirstRow = true;

  std::vector<SurveyFilePoint> probe;
  int skipped = 0;
  std::string err;
  if (!SurveyCsvReadPointsOnly(pf.path.c_str(), SurveyCsvLayoutFromUiIndex(pf.layoutIndex), pf.skipFirstRow,
                               &probe, &skipped, &err)) {
    log.push_back("SURFACEADDFILE — cannot read \"" + pf.path + "\": " + err + ". Not linked.");
    return;
  }

  PushUndoSnapshot(st, "Link point file to surface");
  CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (SurfaceRefusesPointEdits(s, "SURFACEIMPORTFILE", log)) return;
  log.push_back("SURFACEADDFILE — linked \"" + pf.path + "\" to \"" + s.name + "\" (" +
                std::to_string(probe.size()) + " point(s)" +
                (skipped > 0 ? ", " + std::to_string(skipped) + " row(s) unreadable" : "") + ").");
  s.sourcePointFiles.push_back(std::move(pf));
  BumpCadGpuCache(st);
}

void RunSurfaceImportFile(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() != 2 || f[0].empty()) {
    log.push_back("SURFACEIMPORTFILE — usage: SURFACEIMPORTFILE <surface>, <number> (SURFACELIST numbers them).");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACEIMPORTFILE — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  char* numEnd = nullptr;
  const long parsed = std::strtol(f[1].c_str(), &numEnd, 10);
  const bool numOk = !f[1].empty() && numEnd && *numEnd == '\0';
  const int n = numOk ? static_cast<int>(parsed) : 0;
  if (n < 1 || static_cast<size_t>(n) > s.sourcePointFiles.size()) {
    log.push_back("SURFACEIMPORTFILE — \"" + s.name + "\" has " + std::to_string(s.sourcePointFiles.size()) +
                  " point file(s); " + f[1] + " is out of range.");
    return;
  }

  const CadSurfacePointFile pf = s.sourcePointFiles[(static_cast<size_t>(n) - 1)];
  PushUndoSnapshot(st, "Import surface point file");

  const int savedLayout = st.surveyImportCsvLayoutIdx;
  const bool savedSkip = st.surveyImportCsvSkipFirstRow;
  std::string savedPath = st.surveyImportCsvPath;
  std::snprintf(st.surveyImportCsvPath, sizeof(st.surveyImportCsvPath), "%s", pf.path.c_str());
  st.surveyImportCsvLayoutIdx = pf.layoutIndex;
  st.surveyImportCsvSkipFirstRow = pf.skipFirstRow;
  const size_t before = st.drawing.surveyPoints.size();
  const bool ok = SurveyCsvImportFile(st, log);
  std::snprintf(st.surveyImportCsvPath, sizeof(st.surveyImportCsvPath), "%s", savedPath.c_str());
  st.surveyImportCsvLayoutIdx = savedLayout;
  st.surveyImportCsvSkipFirstRow = savedSkip;

  if (!ok) {
    log.push_back("SURFACEIMPORTFILE — import failed; the link is left in place.");
    return;
  }
  const size_t added = st.drawing.surveyPoints.size() - before;
  if (added == 0) {
    log.push_back("SURFACEIMPORTFILE — no points were imported (see the lines above; duplicate point "
                  "ids are skipped). The link to \"" + pf.path + "\" is left in place.");
    return;
  }

  PointGroup g;
  g.name = "Imported: " + std::filesystem::path(pf.path).filename().string();
  for (int i = 1; i < 10000 && FindPointGroupIndex(st, g.name) >= 0; ++i)
    g.name = "Imported: " + std::filesystem::path(pf.path).filename().string() + " (" + std::to_string(i + 1) + ")";
  for (size_t i = before; i < st.drawing.surveyPoints.size(); ++i)
    g.rule.explicitIds.push_back(st.drawing.surveyPoints[i].id);
  st.drawing.pointGroups.push_back(g);

  s.sourcePointGroups.push_back(g.name);
  s.sourcePointFiles.erase(s.sourcePointFiles.begin() + (n - 1));
  log.push_back("SURFACEIMPORTFILE — imported " + std::to_string(added) + " point(s) from \"" + pf.path +
                "\" into point group \"" + g.name + "\"; the link is broken.");
  BumpCadGpuCache(st);
}
