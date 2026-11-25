#include "CadCommandsInternal.hpp"
#include "surface/CadSurfaceInternal.hpp"
#include "CadColor.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Terrain/contourgen.hpp>
#include <GoSurvey/Terrain/surfaceanalysis.hpp>
#include <GoSurvey/Terrain/tinbuild.hpp>
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

bool SurfaceVisible(const AppCommandState& st, size_t surfaceIndex) {
  if (surfaceIndex >= st.drawing.cadSurfaces.size())
    return false;
  const CadSurface& s = st.drawing.cadSurfaces[surfaceIndex];
  if (!s.tin || s.tin->indices.empty())
    return false;
  if (surfaceIndex >= st.drawing.cadSurfaceAttrs.size())
    return true;  // attrs are length-locked to cadSurfaces; a short array means "defaults", not hidden
  const EntityAttributes& a = st.drawing.cadSurfaceAttrs[surfaceIndex];
  // REQ-084 (d): an isolated-out surface is invisible, so it must not be drawn OR answer a click.
  if (CadEntityIdHidden(&st.hiddenEntityIds, a.id))
    return false;
  // REQ-068: a surface on a frozen or off layer is not drawn.
  const auto it = std::find_if(st.drawing.drawingLayerTable.begin(), st.drawing.drawingLayerTable.end(),
                               [&](const CadLayerRow& r) { return r.name == a.layer; });
  return !(it != st.drawing.drawingLayerTable.end() && (!it->on || it->frozen));
}

SurfaceState SurfaceRebuildStateOf(const AppCommandState& st, size_t surfaceIndex) {
  if (surfaceIndex >= st.drawing.cadSurfaces.size())
    return SurfaceState::Current;
  const CadSurface& s = st.drawing.cadSurfaces[surfaceIndex];
  // By stable id, matching how the job itself is keyed (ADR-036 (a)). Keying this on the name showed
  // "Rebuilding" against the wrong surface the moment one was renamed mid-rebuild.
  const std::uint64_t id =
      surfaceIndex < st.drawing.cadSurfaceAttrs.size() ? st.drawing.cadSurfaceAttrs[surfaceIndex].id : 0;
  if (id != 0)
    for (const auto& job : st.surfaceRebuildAsync)
      if (job && job->surfaceId == id)
        return SurfaceState::Rebuilding;
  return s.builtAtRevision == st.cadGpuRevision ? SurfaceState::Current : SurfaceState::Stale;
}


namespace {

/// Ceiling on how many contour levels one surface may generate at (REQ-070).
///
/// The generator's cost is proportional to total contour LENGTH, not to the level count — its inner
/// loop only visits levels lying inside each triangle's own Z range — so a small interval on a flat
/// surface is cheap and cannot run away on its own. This cap is not for that; it is for the value
/// nobody typed. The style editor refuses an out-of-range interval where the user enters it, but a
/// hand-edited `internal document payload` or a future importer can still present 0.0001 ft, and the display path must
/// degrade instead of locking the UI up. 20,000 is far past any plan sheet — a 1,000 ft relief at a
/// 0.05 ft interval — and well short of a hang.
constexpr size_t kMaxContourLevels = 20000;

/// Flatten \p r's chained polylines into the `GL_LINES` layout the renderer consumes: six floats per
/// segment, both endpoints written out.
///
/// The doubling is deliberate. `GL_LINE_STRIP` would need one draw call per contour — thousands of
/// them — where this needs one for all of them, and the existing surface/marker/snap paths are all
/// already `GL_LINES`, so this keeps one layout in the renderer instead of two.
void AppendContourLinesFrom(const ContourResult& r, std::vector<float>* out) {
  for (int c = 0; c < r.contourCount(); ++c) {
    const int begin = r.offsets[static_cast<size_t>(c)];
    const int end = r.offsets[static_cast<size_t>(c) + 1];
    const auto emit = [&](int v) {
      out->push_back(r.vertsXyz[static_cast<size_t>(v) * 3 + 0]);
      out->push_back(r.vertsXyz[static_cast<size_t>(v) * 3 + 1]);
      out->push_back(r.vertsXyz[static_cast<size_t>(v) * 3 + 2]);
    };
    for (int v = begin; v + 1 < end; ++v) {
      emit(v);
      emit(v + 1);
    }
    // A closed contour's last vertex is not a repeat of its first (contourgen does not emit the seam
    // twice), so the closing segment has to be written here or every ring would render with a gap.
    if (end - begin > 2 && r.closed[static_cast<size_t>(c)]) {
      emit(end - 1);
      emit(begin);
    }
  }
}

/// Every triangulation edge of \p t in `GL_LINES` layout — the appearance a surface had before
/// REQ-070, now the style's "triangles" component.
void AppendTriangleEdges(const std::vector<double>& vertsXyz, const std::vector<std::uint32_t>& indices,
                         std::vector<float>* out) {
  const auto emit = [&](std::uint32_t a, std::uint32_t b) {
    // Narrowed here (ADR-054 (b)): `out` is the render buffer, GPU-bound `float`.
    out->push_back(static_cast<float>(vertsXyz[a * 3 + 0]));
    out->push_back(static_cast<float>(vertsXyz[a * 3 + 1]));
    out->push_back(static_cast<float>(vertsXyz[a * 3 + 2]));
    out->push_back(static_cast<float>(vertsXyz[b * 3 + 0]));
    out->push_back(static_cast<float>(vertsXyz[b * 3 + 1]));
    out->push_back(static_cast<float>(vertsXyz[b * 3 + 2]));
  };
  // Each interior edge is emitted twice, once per adjoining triangle. De-duplicating would cost a
  // hash of every edge to halve a buffer the line pipeline already handles at this size (REQ-100's
  // measured envelope is 750k segments); that trade is worth revisiting only if the surface profile
  // misses its budget.
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    emit(indices[i], indices[i + 1]);
    emit(indices[i + 1], indices[i + 2]);
    emit(indices[i + 2], indices[i]);
  }
}

/// The shaft length of a REQ-072 slope arrow, as a fraction of its triangle's own characteristic
/// length (`sqrt(planArea)`) rather than a fixed world length — so an arrow on a fine TIN and one on
/// a coarse TIN both read as "this triangle's" arrow instead of one swallowing its neighbours or
/// vanishing inside them.
constexpr double kArrowShaftFraction = 0.5;
/// The head barbs' length, as a fraction of the shaft they sit on.
constexpr double kArrowHeadFraction = 0.35;
/// The half-angle each head barb sweeps back from the shaft.
constexpr double kArrowHeadAngleRad = 0.4363;  // 25 degrees

/// Builds REQ-072 band-fill and slope-arrow geometry for one triangulation under one style.
///
/// Both outputs are bucketed exactly as \ref AppCommandState::SurfaceDisplayCacheEntry documents:
/// one buffer per range-table entry, plus one extra "unbanded" buffer at the end for a triangle (or
/// arrow) `AssignBand` placed in no band. Both outputs are cleared unconditionally, and left with
/// zero buckets — not one empty bucket — when the corresponding toggle (`analysisMode` / `slopeArrowsOn`)
/// is off, so a plain-style surface pays nothing here and the assembly pass has nothing to iterate.
void BuildSurfaceAnalysisGeometry(const std::vector<double>& vertsXyz, const std::vector<std::uint32_t>& indices,
                                  const SurfaceStyle& style,
                                  std::vector<std::vector<float>>* bandBuffers,
                                  std::vector<std::vector<float>>* arrowBuffers) {
  bandBuffers->clear();
  arrowBuffers->clear();
  const bool wantBands = style.analysisMode != SurfaceAnalysisMode::None;
  const bool wantArrows = style.slopeArrowsOn;
  if (!wantBands && !wantArrows)
    return;

  std::vector<double> bandBounds;
  bandBounds.reserve(style.bands.size());
  for (const SurfaceBand& sb : style.bands)
    bandBounds.push_back(sb.upperBound);
  std::vector<double> arrowBounds;
  arrowBounds.reserve(style.arrowBands.size());
  for (const SurfaceBand& sb : style.arrowBands)
    arrowBounds.push_back(sb.upperBound);

  if (wantBands)
    bandBuffers->resize(bandBounds.size() + 1);
  if (wantArrows)
    arrowBuffers->resize(arrowBounds.size() + 1);

  const double headCos = std::cos(kArrowHeadAngleRad);
  const double headSin = std::sin(kArrowHeadAngleRad);

  const auto emitSeg = [](std::vector<float>& buf, double ax, double ay, double az, double bx, double by,
                          double bz) {
    buf.push_back(static_cast<float>(ax));
    buf.push_back(static_cast<float>(ay));
    buf.push_back(static_cast<float>(az));
    buf.push_back(static_cast<float>(bx));
    buf.push_back(static_cast<float>(by));
    buf.push_back(static_cast<float>(bz));
  };

  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const std::uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
    AnalysisTriangle t;
    t.x0 = vertsXyz[ia * 3 + 0]; t.y0 = vertsXyz[ia * 3 + 1]; t.z0 = vertsXyz[ia * 3 + 2];
    t.x1 = vertsXyz[ib * 3 + 0]; t.y1 = vertsXyz[ib * 3 + 1]; t.z1 = vertsXyz[ib * 3 + 2];
    t.x2 = vertsXyz[ic * 3 + 0]; t.y2 = vertsXyz[ic * 3 + 1]; t.z2 = vertsXyz[ic * 3 + 2];

    if (wantBands) {
      double value = 0.0;
      bool haveValue = true;
      if (style.analysisMode == SurfaceAnalysisMode::Elevation) {
        value = TriangleCentroidZ(t);
      } else if (style.analysisMode == SurfaceAnalysisMode::Slope) {
        value = TrianglePlaneSlopePct(t);
      } else if (style.analysisMode == SurfaceAnalysisMode::SlopeAngle) {
        value = std::atan(TrianglePlaneSlopePct(t) / 100.0) * 180.0 / 3.14159265358979323846;
      } else {
        haveValue = TriangleDownhillAspectDeg(t, kFlatGradePctDefault, &value);
      }
      const int idx = haveValue ? AssignBand(value, bandBounds) : -1;
      std::vector<float>& buf = (*bandBuffers)[idx >= 0 ? static_cast<size_t>(idx) : bandBounds.size()];
      buf.push_back(static_cast<float>(t.x0)); buf.push_back(static_cast<float>(t.y0)); buf.push_back(static_cast<float>(t.z0));
      buf.push_back(static_cast<float>(t.x1)); buf.push_back(static_cast<float>(t.y1)); buf.push_back(static_cast<float>(t.z1));
      buf.push_back(static_cast<float>(t.x2)); buf.push_back(static_cast<float>(t.y2)); buf.push_back(static_cast<float>(t.z2));
    }

    if (wantArrows) {
      double dx = 0.0, dy = 0.0;
      if (!TriangleDownhillDirection(t, kFlatGradePctDefault, &dx, &dy))
        continue;  // flat: REQ-072's "a perfectly flat triangle produces no arrow direction"

      const double cx = (t.x0 + t.x1 + t.x2) / 3.0;
      const double cy = (t.y0 + t.y1 + t.y2) / 3.0;
      const double cz = (t.z0 + t.z1 + t.z2) / 3.0;
      const double area2 = std::fabs((t.x1 - t.x0) * (t.y2 - t.y0) - (t.x2 - t.x0) * (t.y1 - t.y0));
      const double charLen = std::sqrt(std::max(area2, 0.0) * 0.5);
      const double shaftLen = charLen * kArrowShaftFraction;
      const double tipX = cx + dx * shaftLen, tipY = cy + dy * shaftLen;

      const double grade = TrianglePlaneSlopePct(t);
      const int idx = AssignBand(grade, arrowBounds);
      std::vector<float>& buf = (*arrowBuffers)[idx >= 0 ? static_cast<size_t>(idx) : arrowBounds.size()];
      emitSeg(buf, cx, cy, cz, tipX, tipY, cz);

      // Two head barbs swept back from the tip, built from the shaft's own unit vector so the head
      // turns with the arrow rather than pointing a fixed compass direction.
      const double backX = -dx, backY = -dy;
      const double b1x = backX * headCos - backY * headSin, b1y = backX * headSin + backY * headCos;
      const double b2x = backX * headCos + backY * headSin, b2y = -backX * headSin + backY * headCos;
      const double headLen = shaftLen * kArrowHeadFraction;
      emitSeg(buf, tipX, tipY, cz, tipX + b1x * headLen, tipY + b1y * headLen, cz);
      emitSeg(buf, tipX, tipY, cz, tipX + b2x * headLen, tipY + b2y * headLen, cz);
    }
  }
}

/// How many levels \c ContourLevels would produce, without producing them.
///
/// The cap below has to be checked BEFORE the lists are built, not after: a 0.0001 ft interval over a
/// 33 ft surface is 330,000 doubles materialised and immediately discarded, which took 2.4 s in
/// testing — a visible stall to reach a decision that is pure arithmetic.
double ContourLevelCount(double minZ, double maxZ, double interval) {
  if (!std::isfinite(minZ) || !std::isfinite(maxZ) || !std::isfinite(interval) || interval <= 0.0 ||
      maxZ < minZ)
    return 0.0;
  const double first = std::ceil(minZ / interval);
  const double last = std::floor(maxZ / interval);
  return last < first ? 0.0 : last - first + 1.0;
}

/// What a surface's style says its contours are: the levels, split by component, or the reason there
/// are none.
///
/// **One decision, two callers** — the per-frame display pass and REQ-071's EXTRACT. They MUST agree:
/// REQ-071's first acceptance condition is that extraction produces polylines "at exactly the
/// displayed contour elevations", and two functions that agree today drift tomorrow. Sharing the
} // namespace

namespace {
/// Split a surface's contour levels into major and minor, with the majors removed from the minors so
/// no level is drawn twice.
///
/// A major level is a minor level by construction — the interval rule makes the major interval a
/// whole multiple of the minor — so without the removal every major contour would be drawn once in
/// each colour, and which one a user saw would depend on draw order rather than on the style.
void SplitContourLevels(double minZ, double maxZ, const SurfaceStyle& style,
                        std::vector<double>* minorOut, std::vector<double>* majorOut) {
  minorOut->clear();
  majorOut->clear();
  if (style.majorContour.visible)
    ContourLevels(minZ, maxZ, style.majorIntervalFt, majorOut);
  if (!style.minorContour.visible)
    return;

  std::vector<double> allMinor;
  ContourLevels(minZ, maxZ, style.minorIntervalFt, &allMinor);
  if (majorOut->empty()) {
    *minorOut = std::move(allMinor);
    return;
  }
  // Matched with a tolerance, never `==`: both lists are `step * interval` products, and a level a
  // plan reader calls "110" is not the same double when reached as 55x2 and as 11x10.
  const double tol = std::max(1.0e-9, style.minorIntervalFt * 1.0e-9);
  for (double lv : allMinor) {
    const auto near = std::lower_bound(majorOut->begin(), majorOut->end(), lv - tol);
    if (near != majorOut->end() && std::fabs(*near - lv) <= tol)
      continue;
    minorOut->push_back(lv);
  }
}

/// Everything both contour consumers need to decide from \p tin and \p style: the surface's Z range,
} // namespace

/// the cap verdict, and the two level lists.
///
/// The order matters and is the whole point of sharing it. The Z range comes from the triangulation,
/// the cap is checked ARITHMETICALLY before any list is built (a 0.0001 ft interval is ~332,000
/// doubles materialised only to be discarded), and only then are the levels generated and the majors
/// removed from the minors. A second implementation of that sequence would be a second chance to get
/// the order — or the tolerance in the removal — subtly different.
SurfaceContourLevels ResolveSurfaceContourLevels(const CadTin& tin, const SurfaceStyle& style) {
  SurfaceContourLevels out;
  for (size_t v = 2; v < tin.vertsXyz.size(); v += 3) {
    const double z = static_cast<double>(tin.vertsXyz[v]);
    if (!out.haveRange) {
      out.minZ = out.maxZ = z;
      out.haveRange = true;
    } else {
      out.minZ = std::min(out.minZ, z);
      out.maxZ = std::max(out.maxZ, z);
    }
  }
  if (!out.haveRange)
    return out;  // no vertices: no range, no contours, and not an error
  if (!style.minorContour.visible && !style.majorContour.visible)
    return out;

  // The cap is on the PAIR, not on each list: two intervals that are individually sane can still be
  // asked for together, and it is the total that gets paid for.
  const double wanted =
      (style.minorContour.visible ? ContourLevelCount(out.minZ, out.maxZ, style.minorIntervalFt) : 0.0) +
      (style.majorContour.visible ? ContourLevelCount(out.minZ, out.maxZ, style.majorIntervalFt) : 0.0);
  if (wanted > static_cast<double>(kMaxContourLevels)) {
    out.suppressed = true;
    out.levelsAsked = wanted > 2.0e9 ? 2000000000 : static_cast<int>(wanted);  // saturate, never overflow
    return out;
  }

  SplitContourLevels(out.minZ, out.maxZ, style, &out.minor, &out.major);
  for (double u : style.userContourFt) {
    if (!std::isfinite(u) || u < out.minZ || u > out.maxZ)
      continue;
    out.major.push_back(u);
  }
  if (!style.userContourFt.empty()) {
    std::sort(out.major.begin(), out.major.end());
    out.major.erase(std::unique(out.major.begin(), out.major.end(),
                                [](double a, double b) { return std::fabs(a - b) <= 1.0e-9; }),
                    out.major.end());
    const double tol = std::max(1.0e-9, style.minorIntervalFt * 1.0e-9);
    std::vector<double> minorKept;
    for (double lv : out.minor) {
      bool hit = false;
      for (double m : out.major) {
        if (std::fabs(m - lv) <= tol) {
          hit = true;
          break;
        }
      }
      if (!hit)
        minorKept.push_back(lv);
    }
    out.minor = std::move(minorKept);
  }
  return out;
}

/// The resolved RGBA for one "ByLayer"-capable colour string on a surface, folding the ByLayer chain

namespace {
/// the same way an entity's own attributes do: "ByLayer" means the surface's own effective colour,
/// which in turn may itself be ByLayer and resolve to the layer's.
///
/// Shared by \ref ResolveComponentBatch and REQ-072's band/arrow colours (TASK-086 §6) — a band's
/// `SurfaceBand::color` uses \ref EntityAttributes::color's same encoding for the same reason a
/// component's does, so both go through the one resolver rather than two hand-rolled copies of the
/// ByLayer chain.
void ResolveSurfaceStoredColorRgba(const AppCommandState& st, const EntityAttributes& surfAttr,
                                   const std::string& colorStorage, float* outRgba) {
  const CadLayerRow* layer = FindDrawingLayerRowCi(st, surfAttr.layer);
  float surfaceRgba[4] = {1.f, 1.f, 1.f, 1.f};
  ResolveEntityRgbaForViewport(surfAttr, layer, 0.42f, 0.62f, 0.78f, surfaceRgba);
  ResolveStoredColorForViewport(colorStorage, surfAttr.transparency < 0.f ? 0.f : surfAttr.transparency,
                                surfaceRgba[0], surfaceRgba[1], surfaceRgba[2], outRgba);
  outRgba[3] = surfaceRgba[3];
}

/// The resolved RGBA and lineweight for one component of a surface — see
/// \ref ResolveSurfaceStoredColorRgba for the colour half.
SurfaceDisplayBatch ResolveComponentBatch(const AppCommandState& st, const EntityAttributes& surfAttr,
                                          const SurfaceComponentStyle& comp,
                                          const std::vector<float>* verts) {
  SurfaceDisplayBatch b;
  b.verts = verts;
  ResolveSurfaceStoredColorRgba(st, surfAttr, comp.color, b.rgba);
  b.lineweightMm = comp.lineweightMm >= 0.f ? comp.lineweightMm
                                            : EffectiveEntityLineweightMm(surfAttr, FindDrawingLayerRowCi(st, surfAttr.layer));
  return b;
}

} // namespace

void RefreshSurfaceDisplayGeometry(AppCommandState& st) {
  // Reap first: an entry whose surface id no longer resolves belongs to an erased surface. Ids are
  // never reused (REQ-076), so "does not resolve" cannot mean "not yet created."
  st.surfaceDisplayCache.erase(
      std::remove_if(st.surfaceDisplayCache.begin(), st.surfaceDisplayCache.end(),
                     [&](const AppCommandState::SurfaceDisplayCacheEntry& e) {
                       return FindSurfaceIndexById(st, e.surfaceId) < 0;
                     }),
      st.surfaceDisplayCache.end());

  for (size_t si = 0; si < st.drawing.cadSurfaces.size(); ++si) {
    const std::uint64_t id = si < st.drawing.cadSurfaceAttrs.size() ? st.drawing.cadSurfaceAttrs[si].id : 0;
    if (id == 0)
      continue;  // not swept yet; it gets an entry next frame rather than one under an unusable key
    const CadSurface& surf = st.drawing.cadSurfaces[si];
    const std::shared_ptr<const CadTin>& tin = surf.tin;

    // Resolve-on-read (ADR-036 (d)): nothing is baked onto the surface, so a style edit touches the
    // table and nothing else — which is why it cannot reach the definition and cannot retriangulate.
    const SurfaceStyle* style = SurfaceStyles::Resolve(st.drawing.surfaceStyles, surf.styleName);
    const SurfaceStyle resolved = style ? *style : SurfaceStyles::StandardSurfaceStyle();

    auto it = std::find_if(st.surfaceDisplayCache.begin(), st.surfaceDisplayCache.end(),
                           [&](const AppCommandState::SurfaceDisplayCacheEntry& e) { return e.surfaceId == id; });
    if (it != st.surfaceDisplayCache.end() && it->builtFrom.lock() == tin && it->style == resolved &&
        it->reducedDisplayLod == surf.reducedDisplayLod)
      continue;  // BEFORE any clear/reserve — see the header comment; this early-out is the budget

    if (!tin) {
      if (it != st.surfaceDisplayCache.end())
        st.surfaceDisplayCache.erase(it);
      continue;  // never built: no geometry, and no empty entry left behind to keep re-checking
    }
    if (it == st.surfaceDisplayCache.end()) {
      st.surfaceDisplayCache.push_back({});
      it = st.surfaceDisplayCache.end() - 1;
      it->surfaceId = id;
    }
    it->builtFrom = tin;
    it->style = resolved;
    it->reducedDisplayLod = surf.reducedDisplayLod;
    ++st.surfaceDisplayRegenCount;  // past the early-out: this frame is doing the real work

    std::vector<std::uint32_t> reducedIndices;
    const std::vector<std::uint32_t>* displayIndices = &tin->indices;
    if (surf.reducedDisplayLod) {
      // Store only sampled indices in the display cache path; copying the full vertex array would
      // duplicate the largest part of a TIN merely to display a reduced overview.
      reducedIndices.reserve((tin->indices.size() + 11) / 12 * 3);
      for (size_t i = 0; i + 2 < tin->indices.size(); i += 12)
        reducedIndices.insert(reducedIndices.end(), tin->indices.begin() + i, tin->indices.begin() + i + 3);
      displayIndices = &reducedIndices;
    }

    // Each component is generated only when its own toggle is on, and its buffer is released when it
    // is off — REQ-070's "a style with triangles off and contours on draws only contours" is then a
    // property of what exists, not of what the renderer remembers to skip. `shrink_to_fit` because a
    // 14 MB triangle-edge buffer left capacity-resident by a `clear()` would defeat turning it off.
    const auto release = [](std::vector<float>* v) {
      v->clear();
      v->shrink_to_fit();
    };

    if (resolved.triangles.visible) {
      // Cleared, not appended to. This entry may already hold the edges generated for the PREVIOUS
      // style — a style edit is a cache miss on a triangulation that did not change — and appending
      // to it would double the buffer on every edit. Capacity is kept, because what follows refills
      // it to the same size.
      it->triangleEdges.clear();
      AppendTriangleEdges(tin->vertsXyz, *displayIndices, &it->triangleEdges);
    } else {
      release(&it->triangleEdges);
    }

    if (resolved.border.visible)
      TinBorderEdges(tin->vertsXyz, tin->indices, &it->borderEdges);
    else
      release(&it->borderEdges);

    release(&it->minorContours);
    release(&it->majorContours);
    it->contourLabels.clear();
    it->contoursSuppressed = false;
    it->suppressedLevelCount = 0;
    if (resolved.minorContour.visible || resolved.majorContour.visible) {
      const SurfaceContourLevels levels = ResolveSurfaceContourLevels(*tin, resolved);
      if (levels.suppressed) {
        it->contoursSuppressed = true;
        it->suppressedLevelCount = levels.levelsAsked;
      } else {
        ContourResult r;
        if (!levels.minor.empty()) {
          GenerateContours(tin->vertsXyz, tin->indices, levels.minor, &r);
          SmoothContoursChaikin(&r, resolved.contourSmoothPasses);
          AppendContourLinesFrom(r, &it->minorContours);
        }
        if (!levels.major.empty()) {
          GenerateContours(tin->vertsXyz, tin->indices, levels.major, &r);
          SmoothContoursChaikin(&r, resolved.contourSmoothPasses);
          AppendContourLinesFrom(r, &it->majorContours);
          std::vector<ContourLabelPoint> labs;
          CollectContourLabels(r, resolved.contourLabelSpacingFt, &labs);
          for (const ContourLabelPoint& lp : labs) {
            AppCommandState::SurfaceDisplayCacheEntry::ContourLabel cl;
            cl.x = lp.x;
            cl.y = lp.y;
            cl.z = lp.z;
            cl.level = lp.level;
            it->contourLabels.push_back(cl);
          }
        }
      }
    }

    // REQ-072: unconditional — the builder itself clears both outputs to zero buckets when neither
    // `analysisMode` nor `slopeArrowsOn` is on, which is what turns them off (§6 (2)/(3)).
    // Analysis data is authoritative surface information, not a viewport mesh simplification.
    // Keep arrows and range assignment full-detail even when triangles use Reduced LOD.
    BuildSurfaceAnalysisGeometry(tin->vertsXyz, tin->indices, resolved,
                                 &it->bandTriangleBuffers, &it->arrowLineBuffers);
  }

  st.surfaceWatershedCache.erase(
      std::remove_if(st.surfaceWatershedCache.begin(), st.surfaceWatershedCache.end(),
                     [&](const AppCommandState::SurfaceWatershedCacheEntry& e) {
                       return FindSurfaceIndexById(st, e.surfaceId) < 0;
                     }),
      st.surfaceWatershedCache.end());
  for (AppCommandState::SurfaceWatershedCacheEntry& e : st.surfaceWatershedCache) {
    const int six = FindSurfaceIndexById(st, e.surfaceId);
    if (six < 0)
      continue;
    const auto& tin = st.drawing.cadSurfaces[static_cast<size_t>(six)].tin;
    if (!tin || e.builtFrom.lock() != tin) {
      e.analysis = {};
      e.basinOutlines.clear();
      // Water-drop / catchment previews live on lastWaterDropPathXyz, not here — a TIN pointer
      // swap (REQ-069) must not blank a path the command just reported (REQ-133).
    }
  }

  st.waterDropPreviewLines.clear();
  st.catchmentPreviewLines = st.lastCatchmentPathXyz;

  // Assemble what the renderer is handed. Cheap by construction: the batches BORROW the buffers
  // above (see SurfaceDisplayBatch), so this pass copies pointers and colours, never vertices, and
  // is therefore safe to redo every frame — which it must be, because layer visibility and isolation
  // can change without any surface's geometry changing at all.
  st.surfaceDisplayGeometry.bandTriangles.clear();
  st.surfaceDisplayGeometry.lines.clear();
  for (size_t si = 0; si < st.drawing.cadSurfaces.size(); ++si) {
    if (!SurfaceVisible(st, si))
      continue;  // layer off/frozen or isolated out — filtered here, so the renderer stays ignorant
    const std::uint64_t id = si < st.drawing.cadSurfaceAttrs.size() ? st.drawing.cadSurfaceAttrs[si].id : 0;
    if (id == 0)
      continue;
    const auto it = std::find_if(st.surfaceDisplayCache.begin(), st.surfaceDisplayCache.end(),
                                 [&](const AppCommandState::SurfaceDisplayCacheEntry& e) { return e.surfaceId == id; });
    if (it == st.surfaceDisplayCache.end())
      continue;
    const EntityAttributes& attr = st.drawing.cadSurfaceAttrs[si];

    // REQ-072 band fills, FIRST — see CadSurfaceDisplayGeometry::bandTriangles. Bucket
    // `it->style.bands.size()` is the "unbanded" overflow (TASK-086 §6 (2)); it takes the plain
    // triangle style's colour rather than being dropped, so a table that does not span the surface's
    // full range does not silently blank the part above it.
    for (size_t bi = 0; bi < it->bandTriangleBuffers.size(); ++bi) {
      const std::vector<float>& buf = it->bandTriangleBuffers[bi];
      if (buf.empty())
        continue;
      const std::string& colorStr = bi < it->style.bands.size() ? it->style.bands[bi].color : it->style.triangles.color;
      SurfaceTriangleBatch tb;
      tb.verts = &buf;
      ResolveSurfaceStoredColorRgba(st, attr, colorStr, tb.rgba);
      st.surfaceDisplayGeometry.bandTriangles.push_back(tb);
    }

    // Draw order: triangles, then contours, then the border, so an outline stays readable over its
    // own triangulation.
    const auto add = [&](const SurfaceComponentStyle& comp, const std::vector<float>* verts) {
      if (verts->empty())
        return;
      st.surfaceDisplayGeometry.lines.push_back(ResolveComponentBatch(st, attr, comp, verts));
    };
    add(it->style.triangles, &it->triangleEdges);
    add(it->style.minorContour, &it->minorContours);
    add(it->style.majorContour, &it->majorContours);
    add(it->style.border, &it->borderEdges);

    // REQ-072 slope arrows, LAST — on top of the border, because an arrow that reads as buried under
    // the outline it crosses is not a readable arrow. Bucket `arrowBands.size()` is the "unbanded"
    // overflow, drawn in the surface's own colour ("ByLayer") rather than dropped.
    for (size_t ai = 0; ai < it->arrowLineBuffers.size(); ++ai) {
      const std::vector<float>& buf = it->arrowLineBuffers[ai];
      if (buf.empty())
        continue;
      const std::string colorStr = ai < it->style.arrowBands.size() ? it->style.arrowBands[ai].color : std::string("ByLayer");
      SurfaceDisplayBatch b;
      b.verts = &buf;
      ResolveSurfaceStoredColorRgba(st, attr, colorStr, b.rgba);
      b.lineweightMm = -1.f;
      st.surfaceDisplayGeometry.lines.push_back(b);
    }

    const auto wit = std::find_if(st.surfaceWatershedCache.begin(), st.surfaceWatershedCache.end(),
                                  [&](const AppCommandState::SurfaceWatershedCacheEntry& e) {
                                    return e.surfaceId == id;
                                  });
    if (wit != st.surfaceWatershedCache.end() && wit->builtFrom.lock() == st.drawing.cadSurfaces[si].tin) {
      const auto addWs = [&](const std::vector<float>* verts, float r, float g, float bch) {
        if (!verts || verts->empty())
          return;
        SurfaceDisplayBatch bat;
        bat.verts = verts;
        bat.rgba[0] = r;
        bat.rgba[1] = g;
        bat.rgba[2] = bch;
        bat.rgba[3] = 1.f;
        bat.lineweightMm = -1.f;
        st.surfaceDisplayGeometry.lines.push_back(bat);
      };
      addWs(&wit->basinOutlines, 0.15f, 0.75f, 0.95f);
    }
  }

  const auto addOverlay = [&](const std::vector<float>* verts, float r, float g, float bch, float lwMm) {
    if (!verts || verts->empty() || verts->size() % 6 != 0)
      return;
    SurfaceDisplayBatch bat;
    bat.verts = verts;
    bat.rgba[0] = r;
    bat.rgba[1] = g;
    bat.rgba[2] = bch;
    bat.rgba[3] = 1.f;
    bat.lineweightMm = lwMm;
    st.surfaceDisplayGeometry.lines.push_back(bat);
  };
  addOverlay(&st.catchmentPreviewLines, 0.95f, 0.35f, 0.75f, 0.35f);
  addOverlay(&st.waterDropPreviewLines, 1.f, 0.92f, 0.12f, 0.50f);
}

bool SurfaceContoursSuppressed(const AppCommandState& st, size_t surfaceIndex, int* levelsAsked) {
  if (levelsAsked)
    *levelsAsked = 0;
  if (surfaceIndex >= st.drawing.cadSurfaceAttrs.size())
    return false;
  const std::uint64_t id = st.drawing.cadSurfaceAttrs[surfaceIndex].id;
  if (id == 0)
    return false;
  for (const auto& e : st.surfaceDisplayCache) {
    if (e.surfaceId != id)
      continue;
    if (levelsAsked)
      *levelsAsked = e.suppressedLevelCount;
    return e.contoursSuppressed;
  }
  return false;
}

int SurfaceDisplayContourSegs(const AppCommandState& st) {
  size_t floats = 0;
  for (const auto& e : st.surfaceDisplayCache)
    floats += e.minorContours.size() + e.majorContours.size();
  return static_cast<int>(floats / 6);
}

const std::vector<float>* SurfaceBorderEdges(const AppCommandState& st, size_t surfaceIndex) {
  if (surfaceIndex >= st.drawing.cadSurfaceAttrs.size())
    return nullptr;
  const std::uint64_t id = st.drawing.cadSurfaceAttrs[surfaceIndex].id;
  if (id == 0)
    return nullptr;
  for (const auto& e : st.surfaceDisplayCache)
    if (e.surfaceId == id)
      return e.borderEdges.empty() ? nullptr : &e.borderEdges;
  return nullptr;
}

int FindSurfaceIndex(const AppCommandState& st, const std::string& name) {
  auto eqCI = [](const std::string& a, const std::string& b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
        return false;
    return true;
  };
  for (size_t i = 0; i < st.drawing.cadSurfaces.size(); ++i)
    if (eqCI(st.drawing.cadSurfaces[i].name, name))
      return static_cast<int>(i);
  return -1;
}

int FindSurfaceIndexById(const AppCommandState& st, std::uint64_t id) {
  if (id == 0)
    return -1;  // 0 is "unassigned", never a real id — it must not match the first id-less surface
  for (size_t i = 0; i < st.drawing.cadSurfaceAttrs.size() && i < st.drawing.cadSurfaces.size(); ++i)
    if (st.drawing.cadSurfaceAttrs[i].id == id)
      return static_cast<int>(i);
  return -1;
}



// one row per visible surface covering it. Calls the file-local SurfacesCovering — the same walk
// SURFELEV uses — which is visible here via the anonymous namespace's using-directive, exactly as
// CommitClipboardPasteAt below reaches CommitPasteFromClipboard.
//
// Called ONCE per cursor rest, never per frame; the reason and the mechanism are in
// `util/hoverdwell.hpp`, and REQ-089 makes it an acceptance condition.
void BuildSurfaceHoverRows(const AppCommandState& st, double x, double y,
                           std::vector<SurfaceHoverRow>* out) {
  if (!out)
    return;
  out->clear();
  for (const SurfaceCoverage& c : SurfacesCovering(st, x, y)) {
    const CadSurface& s = st.drawing.cadSurfaces[c.surfaceIndex];
    SurfaceHoverRow row;
    row.name = s.name;

    // The EFFECTIVE style, resolved on read (REQ-070 / ADR-036 (d)) — so a surface whose style was
    // deleted, and one saved before the field existed, both read "Standard" here rather than blank.
    // The empty-table case is the only one that has no name to give.
    const SurfaceStyle* style = SurfaceStyles::Resolve(st.drawing.surfaceStyles, s.styleName);
    row.style = style ? style->name : std::string("\xE2\x80\x94");

    // `cadSurfaceAttrs` is length-locked to `cadSurfaces`; a short array means defaults, exactly as
    // SurfaceVisible reads it, and the default layer is "0".
    row.layer = (c.surfaceIndex < st.drawing.cadSurfaceAttrs.size() &&
                 !st.drawing.cadSurfaceAttrs[c.surfaceIndex].layer.empty())
                    ? st.drawing.cadSurfaceAttrs[c.surfaceIndex].layer
                    : std::string("0");

    // Defensive, and expected never to fire: a row exists only where a triangle covered the point,
    // so an elevation exists (TASK-088 ASSUMPTION-2). It survives for the degenerate case
    // TinElevationAt guards — a collinear triangle whose plane has no solution — because printing
    // "nan" as an elevation would be a statement about the ground that happens to be false.
    row.elevation = std::isfinite(c.z) ? FormatLinear(c.z, st.displayLinearPrecision)
                                       : std::string("\xE2\x80\x94");
    out->push_back(std::move(row));
  }
}

bool SurfaceSnapElevation(const AppCommandState& st, double x, double y, float* outZ) {
  if (!outZ)
    return false;
  const std::vector<SurfaceCoverage> cov = SurfacesCovering(st, x, y);
  if (cov.empty())
    return false;
  *outZ = static_cast<float>(cov.back().z);
  return true;
}

// Public paste entry point (REQ-038): place the clipboard at point (x,y) in the ACTIVE space's coordinates
