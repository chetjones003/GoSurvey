#include "CadCommandsInternal.hpp"

#include <sstream>

namespace {

const char* DrainKindLabel(DrainKind k) {
  return k == DrainKind::Depression ? "depression" : k == DrainKind::Flat ? "flat" : "boundary";
}

AppCommandState::SurfaceWatershedCacheEntry* WatershedCacheOf(AppCommandState& st, size_t si) {
  if (si >= st.drawing.cadSurfaces.size() || si >= st.drawing.cadSurfaceAttrs.size())
    return nullptr;
  const CadSurface& s = st.drawing.cadSurfaces[si];
  if (!s.tin || s.tin->indices.empty())
    return nullptr;
  const std::uint64_t id = st.drawing.cadSurfaceAttrs[si].id;
  auto it = std::find_if(st.surfaceWatershedCache.begin(), st.surfaceWatershedCache.end(),
                         [&](const AppCommandState::SurfaceWatershedCacheEntry& e) { return e.surfaceId == id; });
  if (it != st.surfaceWatershedCache.end() && it->builtFrom.lock() == s.tin && it->analysis.ok)
    return &*it;
  if (it == st.surfaceWatershedCache.end()) {
    st.surfaceWatershedCache.push_back({});
    it = st.surfaceWatershedCache.end() - 1;
    it->surfaceId = id;
  }
  it->builtFrom = s.tin;
  it->analysis = ComputeWatershed(s.tin->vertsXyz, s.tin->indices);
  it->basinOutlines.clear();
  it->waterDropLines.clear();
  it->catchmentLines.clear();
  if (it->analysis.ok)
    AppendWatershedBasinOutlines(it->analysis, s.tin->vertsXyz, s.tin->indices, &it->basinOutlines);
  return &*it;
}

void ReportWatershedOne(AppCommandState& st, size_t si, std::vector<std::string>& log) {
  const CadSurface& s = st.drawing.cadSurfaces[si];
  if (!s.tin || s.tin->indices.empty()) {
    log.push_back("WATERSHED — \"" + s.name + "\": not built.");
    return;
  }
  AppCommandState::SurfaceWatershedCacheEntry* cache = WatershedCacheOf(st, si);
  if (!cache || !cache->analysis.ok) {
    log.push_back("WATERSHED — \"" + s.name + "\": null TIN.");
    return;
  }
  int nBound = 0, nDep = 0, nFlat = 0;
  for (const WatershedBasin& b : cache->analysis.basins) {
    if (b.drain == DrainKind::Depression)
      ++nDep;
    else if (b.drain == DrainKind::Flat)
      ++nFlat;
    else
      ++nBound;
  }
  log.push_back("WATERSHED — \"" + s.name + "\": " + std::to_string(cache->analysis.basins.size()) +
                " basin(s) (" + std::to_string(nBound) + " boundary, " + std::to_string(nDep) +
                " depression, " + std::to_string(nFlat) + " flat).");
}

EntityAttributes WaterDropEntityAttrs(const AppCommandState& st) {
  EntityAttributes a;
  a.layer = st.drawing.currentLayer.empty() ? std::string("0") : st.drawing.currentLayer;
  a.color = "Yellow";
  a.linetype = "ByLayer";
  a.lineweightMm = 0.35f;
  a.transparency = -1.f;
  return a;
}

void AppendYellowLineSegs(AppCommandState& st, const std::vector<float>& glLines) {
  const EntityAttributes a = WaterDropEntityAttrs(st);
  for (size_t i = 0; i + 5 < glLines.size(); i += 6) {
    st.drawing.pushLine(CadLineRecord{glLines[i + 0], glLines[i + 1], glLines[i + 2], glLines[i + 3], glLines[i + 4],
                              glLines[i + 5]});
    st.drawing.userLineAttrs.push_back(a);
  }
}

} // namespace

void RunWatershed(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = StringUtil::trimCopy(args);
  if (name.empty()) {
    if (st.drawing.cadSurfaces.empty()) {
      log.push_back("WATERSHED — no surfaces in the drawing.");
      return;
    }
    for (size_t si = 0; si < st.drawing.cadSurfaces.size(); ++si)
      ReportWatershedOne(st, si, log);
    return;
  }
  const int si = FindSurfaceIndex(st, name);
  if (si < 0) {
    log.push_back("WATERSHED — no surface named \"" + name + "\".");
    return;
  }
  ReportWatershedOne(st, static_cast<size_t>(si), log);
}

void RunWaterDropAt(AppCommandState& st, size_t si, double x, double y, std::vector<std::string>& log) {
  const CadSurface& s = st.drawing.cadSurfaces[si];
  if (!s.tin || s.tin->indices.empty()) {
    log.push_back("WATERDROP — \"" + s.name + "\": not built.");
    return;
  }
  const WaterDropResult d = ComputeWaterDrop(s.tin->vertsXyz, s.tin->indices, x, y);
  if (!d.ok) {
    log.push_back("WATERDROP — \"" + s.name + "\": null TIN.");
    return;
  }
  if (d.outside) {
    log.push_back("WATERDROP — \"" + s.name + "\": outside surface. No path.");
    return;
  }
  // Do not run WatershedCacheOf here: that fills basin outlines (TIN-looking edges) as a side
  // effect of a drop. WATERSHED is the command that asks for basins.
  std::vector<float> lifted = d.pathXyz;
  for (size_t i = 2; i < lifted.size(); i += 3)
    lifted[i] += 0.15f;
  PushUndoSnapshot(st, "WATERDROP");
  st.lastWaterDropPathXyz = lifted;
  AppendXyzPathAsPolyline(st, lifted, false);
  if (!st.drawing.userPolylineAttrs.empty())
    st.drawing.userPolylineAttrs.back() = WaterDropEntityAttrs(st);
  std::vector<float> arrows;
  AppendPathFlowArrows(lifted, &arrows);
  AppendYellowLineSegs(st, arrows);
  BumpCadGpuCache(st);
  const int p = st.displayLinearPrecision;
  const size_t n = d.pathXyz.size();
  const float ex = n >= 3 ? d.pathXyz[n - 3] : 0.f;
  const float ey = n >= 3 ? d.pathXyz[n - 2] : 0.f;
  log.push_back("WATERDROP — \"" + s.name + "\": " + DrainKindLabel(d.terminal) + " drain at " +
                FormatLinear(static_cast<double>(ex), p) + ", " + FormatLinear(static_cast<double>(ey), p) +
                " (" + std::to_string(static_cast<int>(n / 3)) + " vertices).");
}

void RunCatchmentAt(AppCommandState& st, size_t si, double x, double y, std::vector<std::string>& log) {
  const CadSurface& s = st.drawing.cadSurfaces[si];
  if (!s.tin || s.tin->indices.empty()) {
    log.push_back("CATCHMENT — \"" + s.name + "\": not built.");
    st.lastCatchmentPathXyz.clear();
    return;
  }
  const CatchmentResult c = ComputeCatchment(s.tin->vertsXyz, s.tin->indices, x, y);
  if (!c.ok) {
    log.push_back("CATCHMENT — \"" + s.name + "\": null TIN.");
    st.lastCatchmentPathXyz.clear();
    return;
  }
  if (c.outside) {
    log.push_back("CATCHMENT — \"" + s.name + "\": outside surface.");
    st.lastCatchmentPathXyz.clear();
    return;
  }
  st.lastCatchmentPathXyz.clear();
  AppendCatchmentBoundary(c, s.tin->vertsXyz, s.tin->indices, &st.lastCatchmentPathXyz);
  const int p = st.displayLinearPrecision;
  log.push_back("CATCHMENT — \"" + s.name + "\": area " + FormatLinear(c.area2d, p) + " ft2, elev " +
                FormatLinear(c.minZ, p) + " to " + FormatLinear(c.maxZ, p) + " mean " +
                FormatLinear(c.meanZ, p) + ", " + std::to_string(c.triangleIds.size()) + " triangle(s).");
}

void RunWaterDropCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string raw = StringUtil::trimCopy(args);
  const std::string rawLower = StringUtil::toLowerAsciiCopy(raw);
  const std::vector<std::string> f = SplitCommaFields(raw);
  const bool extractFl = rawLower == "extract fl" ||
                         (f.size() >= 2 && StringUtil::toLowerAsciiCopy(f[0]) == "extract" &&
                          StringUtil::toLowerAsciiCopy(f[1]) == "fl");
  const bool extractPoly = !extractFl && (rawLower == "extract" ||
                                          (!f.empty() && StringUtil::toLowerAsciiCopy(f[0]) == "extract"));
  if (extractFl) {
      if (st.lastWaterDropPathXyz.size() < 6) {
        log.push_back("WATERDROP EXTRACT FL — nothing to extract.");
        return;
      }
      const size_t nvert = st.lastWaterDropPathXyz.size() / 3;
      PushUndoSnapshot(st, "WATERDROP EXTRACT FL");
      if (st.drawing.featureLineOffsets.empty())
        st.drawing.featureLineOffsets.push_back(0);
      const int baseVert = st.drawing.featureLineOffsets.back();
      st.drawing.featureLineVerts.insert(st.drawing.featureLineVerts.end(), st.lastWaterDropPathXyz.begin(),
                                 st.lastWaterDropPathXyz.end());
      st.drawing.featureLineElevPt.insert(st.drawing.featureLineElevPt.end(), nvert, static_cast<uint8_t>(0));
      st.drawing.featureLineOffsets.push_back(baseVert + static_cast<int>(nvert));
      st.drawing.featureLineClosed.push_back(0);
      CadFeatureLineInfo info;
      info.name = "Water drop";
      st.drawing.featureLineInfo.push_back(std::move(info));
      AppendNewObjectAttrs(st, st.drawing.featureLineAttrs, st.drawing.featureLineInfo.size(), ObjectLayerKind::FeatureLine,
                           st.drawing.featureLineInfo.back().name);  // REQ-361
      EnsureAttrCounts(st);
      BumpCadGpuCache(st);
      log.push_back("WATERDROP EXTRACT FL — 1 feature line (unlinked).");
      return;
  }
  if (extractPoly) {
    if (st.lastWaterDropPathXyz.size() < 6) {
      log.push_back("WATERDROP EXTRACT — nothing to extract.");
      return;
    }
    PushUndoSnapshot(st, "WATERDROP EXTRACT");
    const int n = AppendXyzPathAsPolyline(st, st.lastWaterDropPathXyz, false);
    BumpCadGpuCache(st);
    log.push_back("WATERDROP EXTRACT — " + std::to_string(n) +
                  " polyline (unlinked).");
    return;
  }
  std::string name = f.empty() ? std::string() : f[0];
  if (name.empty() && st.drawing.cadSurfaces.size() == 1)
    name = st.drawing.cadSurfaces[0].name;
  if (name.empty()) {
    if (st.drawing.cadSurfaces.empty()) {
      log.push_back("WATERDROP — no surfaces in the drawing.");
      return;
    }
    log.push_back("WATERDROP — usage: WATERDROP <surface>[, <x>, <y>] or WATERDROP EXTRACT.");
    return;
  }
  const int si = FindSurfaceIndex(st, name);
  if (si < 0) {
    log.push_back("WATERDROP — no surface named \"" + name + "\".");
    return;
  }
  if (f.size() >= 3) {
    double x = 0.0, y = 0.0;
    std::istringstream xy(f[1] + " " + f[2]);
    if (!(xy >> x >> y)) {
      log.push_back("WATERDROP — usage: WATERDROP <surface>[, <x>, <y>].");
      return;
    }
    RunWaterDropAt(st, static_cast<size_t>(si), x, y, log);
    return;
  }
  StartWaterDropCommand(st, name, log);
}

void RunCatchmentCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(StringUtil::trimCopy(args));
  if (!f.empty() && StringUtil::toLowerAsciiCopy(f[0]) == "extract") {
    if (st.lastCatchmentPathXyz.size() < 6) {
      log.push_back("CATCHMENT EXTRACT — nothing to extract.");
      return;
    }
    PushUndoSnapshot(st, "CATCHMENT EXTRACT");
    // Boundary is GL_LINES pairs, not a ring — bake as open segments joined in order of first points.
    std::vector<float> verts;
    for (size_t i = 0; i + 5 < st.lastCatchmentPathXyz.size(); i += 6) {
      if (verts.empty()) {
        verts.push_back(st.lastCatchmentPathXyz[i]);
        verts.push_back(st.lastCatchmentPathXyz[i + 1]);
        verts.push_back(st.lastCatchmentPathXyz[i + 2]);
      }
      verts.push_back(st.lastCatchmentPathXyz[i + 3]);
      verts.push_back(st.lastCatchmentPathXyz[i + 4]);
      verts.push_back(st.lastCatchmentPathXyz[i + 5]);
    }
    const int n = AppendXyzPathAsPolyline(st, verts, false);
    BumpCadGpuCache(st);
    log.push_back("CATCHMENT EXTRACT — " + std::to_string(n) + " polyline (unlinked).");
    return;
  }
  std::string name = f.empty() ? std::string() : f[0];
  if (name.empty() && st.drawing.cadSurfaces.size() == 1)
    name = st.drawing.cadSurfaces[0].name;
  if (name.empty()) {
    if (st.drawing.cadSurfaces.empty()) {
      log.push_back("CATCHMENT — no surfaces in the drawing.");
      return;
    }
    log.push_back("CATCHMENT — usage: CATCHMENT <surface>[, <x>, <y>] or CATCHMENT EXTRACT.");
    return;
  }
  const int si = FindSurfaceIndex(st, name);
  if (si < 0) {
    log.push_back("CATCHMENT — no surface named \"" + name + "\".");
    return;
  }
  if (f.size() >= 3) {
    double x = 0.0, y = 0.0;
    std::istringstream xy(f[1] + " " + f[2]);
    if (!(xy >> x >> y)) {
      log.push_back("CATCHMENT — usage: CATCHMENT <surface>[, <x>, <y>].");
      return;
    }
    RunCatchmentAt(st, static_cast<size_t>(si), x, y, log);
    return;
  }
  StartCatchmentCommand(st, name, log);
}
