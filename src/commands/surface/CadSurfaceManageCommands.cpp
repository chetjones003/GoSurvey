#include "CadCommandsInternal.hpp"
#include "surface/CadSurfaceInternal.hpp"
#include "CadColor.hpp"
#include "CadCoordinateFrame.hpp"
#include "ToolspaceCatalog.hpp"
#include <GoSurvey/Terrain/gridsurface.hpp>
#include <GoSurvey/Terrain/surfaceanalysis.hpp>
#include <GoSurvey/Terrain/surfacequery.hpp>
#include <GoSurvey/Terrain/surfacestats.hpp>
#include <GoSurvey/Terrain/surfacevolume.hpp>
#include <GoSurvey/Terrain/tinbuild.hpp>
#include <GoSurvey/Terrain/tinvolume.hpp>
#include "GoSurvey/Survey/AlignmentGroundModel.hpp"
#include <GoSurvey/Geometry/geom2d.hpp>
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

bool RefreshSurfaceLabelValues(AppCommandState& st, const CadSurface& surface, bool invalidateCache = true);



/// The full definition of every surface, one line each, plus a line per definition item.
///
/// This is the only way to observe a surface without a window: a surface has no entity id, so
/// nothing else in the command layer can report one, and the Surfaces panel is unreachable from the
/// REQ-203 driver. The per-item indices printed here are exactly what UNDESIGNATE takes.
void ReportSurfaces(const AppCommandState& st, std::vector<std::string>& log) {
  if (st.drawing.cadSurfaces.empty()) {
    log.push_back("SURFACELIST — no surfaces in the drawing.");
    return;
  }
  for (size_t surfaceIndex = 0; surfaceIndex < st.drawing.cadSurfaces.size(); ++surfaceIndex) {
    const CadSurface& s = st.drawing.cadSurfaces[surfaceIndex];
    const std::uint64_t id = surfaceIndex < st.drawing.cadSurfaceAttrs.size() ? st.drawing.cadSurfaceAttrs[surfaceIndex].id : 0;
    std::string line = "Surface \"" + s.name + "\": ";
    const char* kn = s.kind == SurfaceKind::Grid          ? "grid"
                     : s.kind == SurfaceKind::TinVolume   ? "tin-volume"
                     : s.kind == SurfaceKind::GridVolume  ? "grid-volume"
                     : s.kind == SurfaceKind::Corridor    ? "corridor"
                                                          : "tin";
    line += kn;
    line += ", ";
    line += s.tin ? (std::to_string(s.vertexCount()) + " points, " + std::to_string(s.triangleCount()) +
                     " triangles")
                  : std::string("not built");
    line += ", " + std::to_string(s.breaklines.size()) + " breakline(s), " +
            std::to_string(s.contourSources.size()) + " contour source(s), " +
            std::to_string(s.boundaries.size()) + " boundary(ies), " +
            std::to_string(s.sourcePointFiles.size()) + " point file(s).";
    line += " Stable surface ID: " + std::to_string(id) + ".";
    log.push_back(line);

    for (size_t i = 0; i < s.sourcePointGroups.size(); ++i) {
      const bool exists = FindPointGroupIndex(st, s.sourcePointGroups[i]) >= 0;
      log.push_back("  group " + std::to_string(i + 1) + ": \"" + s.sourcePointGroups[i] + "\"" +
                    (exists ? "" : "  (missing)"));
    }
    for (size_t i = 0; i < s.sourcePointFiles.size(); ++i) {
      const CadSurfacePointFile& pf = s.sourcePointFiles[i];
      const char* lay = pf.layoutIndex == 1 ? "PENZD" : pf.layoutIndex == 2 ? "NEZ" : pf.layoutIndex == 3 ? "ENZ" : "PNEZD";
      log.push_back("  point file " + std::to_string(i + 1) + ": \"" + pf.path + "\" (" + lay +
                    (pf.skipFirstRow ? ", header" : "") + ")");
    }
    for (size_t i = 0; i < s.breaklines.size(); ++i)
      log.push_back("  breakline " + std::to_string(i + 1) + ": entity id " +
                    std::to_string(s.breaklines[i].entityId) +
                    (s.breaklines[i].description.empty() ? "" : "  \"" + s.breaklines[i].description + "\""));
    for (size_t i = 0; i < s.contourSources.size(); ++i)
      log.push_back("  contour " + std::to_string(i + 1) + ": entity id " +
                    std::to_string(s.contourSources[i].entityId) +
                    (s.contourSources[i].description.empty() ? "" : "  \"" + s.contourSources[i].description + "\""));
    for (size_t i = 0; i < s.boundaries.size(); ++i) {
      const CadSurfaceBoundary& b = s.boundaries[i];
      const char* kindName = b.kind == CadBoundaryKind::Outer ? "outer"
                            : b.kind == CadBoundaryKind::Hide  ? "hide"
                            : b.kind == CadBoundaryKind::Show  ? "show"
                            : b.kind == CadBoundaryKind::Mask  ? "mask"
                                                               : "clip";
      log.push_back("  boundary " + std::to_string(i + 1) + ": " + kindName + ", entity id " +
                    std::to_string(b.entityId) + (b.name.empty() ? "" : "  \"" + b.name + "\""));
    }
    if (!s.lastBuildMessage.empty())
      log.push_back("  last build: " + s.lastBuildMessage);
    if (s.isVolumeSurface())
      log.push_back("  volume surface: \"" + s.volumeBaseName + "\" (base) vs \"" + s.volumeComparisonName +
                    "\" (comparison)");
  }
}

/// `SURFACECREATE <name>, <group>[, <group>…]` — the command-line twin of the Surfaces panel's
/// "New from group…". Every named group must resolve: a typo that silently contributed no points
/// would produce a surface built from less than the user asked for, with nothing on screen saying so.
void RunSurfaceCreate(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.empty() || f[0].empty()) {
    log.push_back("SURFACECREATE — usage: SURFACECREATE <name>[, <point group>…].");
    return;
  }
  std::vector<std::string> groups;
  for (size_t i = 1; i < f.size(); ++i) {
    if (f[i].empty()) {
      log.push_back("SURFACECREATE — empty point group name in the list.");
      return;
    }
    if (FindPointGroupIndex(st, f[i]) < 0) {
      log.push_back("SURFACECREATE — no point group named \"" + f[i] + "\".");
      return;
    }
    groups.push_back(f[i]);
  }
  PushUndoSnapshot(st, "Create surface");
  CreateSurfaceFromPointGroups(st, f[0], groups, log);  // reports its own failure (REQ-201)
}

/// `VOLUMESURFACE <name>, <base>, <comparison>` — REQ-136 TIN volume surface.
void RunVolumeSurfaceCreate(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() != 3 || f[0].empty() || f[1].empty() || f[2].empty()) {
    log.push_back("VOLUMESURFACE — usage: VOLUMESURFACE <name>, <base surface>, <comparison surface>.");
    return;
  }
  if (FindSurfaceIndex(st, f[1]) < 0) {
    log.push_back("VOLUMESURFACE — no surface named \"" + f[1] + "\".");
    return;
  }
  if (FindSurfaceIndex(st, f[2]) < 0) {
    log.push_back("VOLUMESURFACE — no surface named \"" + f[2] + "\".");
    return;
  }
  PushUndoSnapshot(st, "Create volume surface");
  CreateSurfaceFromVolumeParents(st, f[0], f[1], f[2], log);
}

void RunSurfaceCreateGrid(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() < 7 || f[0].empty()) {
    log.push_back("SURFACECREATEGRID — usage: SURFACECREATEGRID <name>, <ox>, <oy>, <sx>, <sy>, <cols>, "
                  "<rows>[, z…].");
    return;
  }
  if (FindSurfaceIndex(st, f[0]) >= 0) {
    log.push_back("A surface named \"" + f[0] + "\" already exists.");
    return;
  }
  auto parseD = [&](const std::string& s, double* o) {
    char* end = nullptr;
    *o = std::strtod(s.c_str(), &end);
    return end && end != s.c_str() && *end == '\0' && std::isfinite(*o);
  };
  double ox = 0, oy = 0, sx = 0, sy = 0;
  if (!parseD(f[1], &ox) || !parseD(f[2], &oy) || !parseD(f[3], &sx) || !parseD(f[4], &sy) || sx <= 0.0 ||
      sy <= 0.0) {
    log.push_back("SURFACECREATEGRID — origin and positive spacing are required.");
    return;
  }
  char* e1 = nullptr;
  char* e2 = nullptr;
  const long cols = std::strtol(f[5].c_str(), &e1, 10);
  const long rows = std::strtol(f[6].c_str(), &e2, 10);
  if (!e1 || *e1 != '\0' || !e2 || *e2 != '\0' || cols < 2 || rows < 2) {
    log.push_back("SURFACECREATEGRID — cols and rows must be integers ≥ 2.");
    return;
  }
  CadSurface s;
  s.name = f[0];
  s.kind = SurfaceKind::Grid;
  s.gridOriginX = ox;
  s.gridOriginY = oy;
  s.gridSpacingX = sx;
  s.gridSpacingY = sy;
  s.gridCols = static_cast<int>(cols);
  s.gridRows = static_cast<int>(rows);
  s.gridZ.assign(static_cast<size_t>(s.gridCols) * static_cast<size_t>(s.gridRows), 0.f);
  const size_t expect = s.gridZ.size();
  for (size_t i = 0; i < expect && 7 + i < f.size(); ++i) {
    double z = 0;
    if (parseD(f[7 + i], &z))
      s.gridZ[i] = static_cast<float>(z);
  }
  PushUndoSnapshot(st, "Create grid surface");
  BumpCadGpuCache(st);
  (void)BuildSurfaceFromSources(st, s, log);
  st.drawing.cadSurfaces.push_back(std::move(s));
  AppendNewObjectAttrs(st, st.drawing.cadSurfaceAttrs, st.drawing.cadSurfaces.size(), ObjectLayerKind::Surface,
                       st.drawing.cadSurfaces.back().name);  // REQ-361
  EnsureAttrCounts(st);
}

void RunSurfaceCreateCorr(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = StringUtil::trimCopy(args);
  if (name.empty()) {
    log.push_back("SURFACECREATECORR — usage: SURFACECREATECORR <name>.");
    return;
  }
  if (FindSurfaceIndex(st, name) >= 0) {
    log.push_back("A surface named \"" + name + "\" already exists.");
    return;
  }
  PushUndoSnapshot(st, "Create corridor surface");
  BumpCadGpuCache(st);
  CadSurface s;
  s.name = name;
  s.kind = SurfaceKind::Corridor;
  (void)BuildSurfaceFromSources(st, s, log);
  st.drawing.cadSurfaces.push_back(std::move(s));
  AppendNewObjectAttrs(st, st.drawing.cadSurfaceAttrs, st.drawing.cadSurfaces.size(), ObjectLayerKind::Surface,
                       st.drawing.cadSurfaces.back().name);  // REQ-361
  EnsureAttrCounts(st);
}

void RunSurfaceCreateVolGrid(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() != 3 || f[0].empty() || f[1].empty() || f[2].empty()) {
    log.push_back("SURFACECREATEVOLGRID — usage: SURFACECREATEVOLGRID <name>, <base grid>, <comparison grid>.");
    return;
  }
  if (FindSurfaceIndex(st, f[0]) >= 0) {
    log.push_back("A surface named \"" + f[0] + "\" already exists.");
    return;
  }
  PushUndoSnapshot(st, "Create grid volume surface");
  BumpCadGpuCache(st);
  CadSurface s;
  s.name = f[0];
  s.kind = SurfaceKind::GridVolume;
  s.volumeBaseName = f[1];
  s.volumeComparisonName = f[2];
  (void)BuildSurfaceFromSources(st, s, log);
  st.drawing.cadSurfaces.push_back(std::move(s));
  AppendNewObjectAttrs(st, st.drawing.cadSurfaceAttrs, st.drawing.cadSurfaces.size(), ObjectLayerKind::Surface,
                       st.drawing.cadSurfaces.back().name);  // REQ-361
  EnsureAttrCounts(st);
}


bool RefreshSurfaceProfilesForSurface(AppCommandState& st, const CadSurface& surface, bool invalidateCache,
                                      std::uint64_t alignmentId) {
  const auto surfaceIt = std::find_if(st.drawing.cadSurfaces.begin(), st.drawing.cadSurfaces.end(),
                                     [&](const CadSurface& value) { return &value == &surface; });
  if (surfaceIt == st.drawing.cadSurfaces.end()) return false;
  const size_t surfaceIndex = static_cast<size_t>(surfaceIt - st.drawing.cadSurfaces.begin());
  if (surfaceIndex >= st.drawing.cadSurfaceAttrs.size()) return false;
  const std::uint64_t surfaceId = st.drawing.cadSurfaceAttrs[surfaceIndex].id;
  if (surfaceId == 0) return false;
  bool changed = false;
  auto eraseEntities = [&](const std::vector<std::uint64_t>& ids) {
    const std::unordered_set<std::uint64_t> remove(ids.begin(), ids.end());
    std::vector<CadLineRecord> lines;
    std::vector<EntityAttributes> attrs;
    std::vector<int> oldToNew(st.drawing.userLineAttrs.size(), -1);
    lines.reserve(st.drawing.lineCount()); attrs.reserve(st.drawing.userLineAttrs.size());
    for (size_t i = 0; i < st.drawing.userLineAttrs.size(); ++i) {
      if (remove.find(st.drawing.userLineAttrs[i].id) != remove.end()) continue;
      if (i >= st.drawing.lineCount()) continue;
      oldToNew[i] = static_cast<int>(attrs.size());
      lines.push_back(st.drawing.line(i));
      attrs.push_back(st.drawing.userLineAttrs[i]);
    }
    st.drawing.userLinesFlat.clear();
    for (const CadLineRecord& l : lines) st.drawing.pushLine(l);
    st.drawing.userLineAttrs.swap(attrs);
    for (size_t i = st.selection.size(); i > 0; --i) {
      SelectedEntity& selected = st.selection[i - 1];
      if (selected.type != SelectedEntity::Type::LineSeg || selected.index < 0 ||
          static_cast<size_t>(selected.index) >= oldToNew.size()) continue;
      const int mapped = oldToNew[static_cast<size_t>(selected.index)];
      if (mapped < 0) st.selection.erase(st.selection.begin() + static_cast<std::ptrdiff_t>(i - 1));
      else selected.index = mapped;
    }
  };
  for (CadSurfaceProfile& profile : st.drawing.surfaceProfiles) {
    if (profile.surfaceId != surfaceId || (alignmentId != 0 && profile.alignmentId != alignmentId)) continue;
    auto setStatus = [&](const std::string& status) {
      if (profile.sourceStatus == status) return;
      profile.sourceStatus = status;
      changed = true;
    };
    const auto alignmentIt = std::find_if(st.drawing.alignments.begin(), st.drawing.alignments.end(),
                                          [&](const alignment::Alignment& value) { return value.id == profile.alignmentId; });
    if (alignmentIt == st.drawing.alignments.end()) {
      setStatus("Source alignment unavailable.");
      continue;
    }
    const size_t alignmentIndex = static_cast<size_t>(alignmentIt - st.drawing.alignments.begin());
    auto query = MakeSurfaceQuery(st, surface);
    if (!query) {
      setStatus("Source surface unavailable.");
      continue;
    }
    std::vector<alignment::GroundPoint> sampled;
    std::string why;
    if (!alignment::SampleGround(st.drawing.alignments[alignmentIndex], *query,
                                profile.sampleInterval, 100000, &sampled, &why)) {
      setStatus("Surface profile unavailable: " + why + ".");
      continue;
    }
    std::vector<double> stations, elevations;
    stations.reserve(sampled.size()); elevations.reserve(sampled.size());
    double minZ = std::numeric_limits<double>::infinity(), maxZ = -minZ;
    for (const auto& point : sampled) {
      stations.push_back(point.internalStation);
      elevations.push_back(point.hasGround ? point.ground : std::numeric_limits<double>::quiet_NaN());
      if (point.hasGround) { minZ = std::min(minZ, point.ground); maxZ = std::max(maxZ, point.ground); }
    }
    if (!std::isfinite(minZ)) {
      setStatus("Surface profile unavailable: alignment has no covered stations.");
      continue; // preserve the last good graph while the source is unavailable
    }
    setStatus("");
    eraseEntities(profile.viewEntityIds);
    profile.viewEntityIds.clear(); profile.stations = std::move(stations); profile.elevations = std::move(elevations);
    changed = true;
    const size_t firstLine = st.drawing.userLineAttrs.size();
    auto addLine = [&](double x0, double y0, double x1, double y1) {
      st.drawing.pushLine(CadLineRecord{x0, y0, 0.0, x1, y1, 0.0});
      EntityAttributes attrs = MakeNewEntityAttrs(st);
      attrs.id = st.nextEntityId++;
      st.drawing.userLineAttrs.push_back(std::move(attrs));
    };
    const double endX = profile.viewX + profile.stations.back() * profile.horizontalScale;
    const double graphHeight = std::max(0.1, (maxZ - minZ) * profile.verticalScale);
    addLine(profile.viewX, profile.viewY, endX, profile.viewY); // station axis
    addLine(profile.viewX, profile.viewY, profile.viewX, profile.viewY + graphHeight); // elevation axis
    for (size_t i = 1; i < profile.stations.size(); ++i) {
      if (!std::isfinite(profile.elevations[i - 1]) || !std::isfinite(profile.elevations[i])) continue;
      addLine(profile.viewX + profile.stations[i - 1] * profile.horizontalScale,
              profile.viewY + (profile.elevations[i - 1] - minZ) * profile.verticalScale,
              profile.viewX + profile.stations[i] * profile.horizontalScale,
              profile.viewY + (profile.elevations[i] - minZ) * profile.verticalScale);
    }
    EnsureEntityIds(st);
    for (size_t i = firstLine; i < st.drawing.userLineAttrs.size(); ++i) profile.viewEntityIds.push_back(st.drawing.userLineAttrs[i].id);
    if (invalidateCache) BumpCadGpuCache(st);
  }
  return changed;
}

void RunCreateSurfaceProfile(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const auto f = SplitCommaFields(args);
  if (f.size() != 8) { log.push_back("CREATEPROFILEFROMSURFACE — usage: <surface>, <alignment>, <profile name>, interval, viewX, viewY, horizontal scale, vertical scale."); return; }
  const int si = FindSurfaceIndex(st, StringUtil::trimCopy(f[0]));
  if (si < 0) { log.push_back("CREATEPROFILEFROMSURFACE — no surface named \"" + f[0] + "\"."); return; }
  if (!st.drawing.cadSurfaces[static_cast<size_t>(si)].tin) { log.push_back("CREATEPROFILEFROMSURFACE — surface has no built TIN."); return; }
  int ai = -1;
  for (size_t i = 0; i < st.drawing.alignments.size(); ++i) if (st.drawing.alignments[i].name == StringUtil::trimCopy(f[1])) { ai = static_cast<int>(i); break; }
  if (ai < 0) { log.push_back("CREATEPROFILEFROMSURFACE — no alignment named \"" + f[1] + "\"."); return; }
  const std::string name = StringUtil::trimCopy(f[2]);
  if (name.empty() || std::any_of(st.drawing.surfaceProfiles.begin(), st.drawing.surfaceProfiles.end(), [&](const auto& p) { return p.name == name; })) {
    log.push_back("CREATEPROFILEFROMSURFACE — profile name must be non-empty and unique."); return;
  }
  double values[5]{};
  for (size_t i = 0; i < 5; ++i) { char* end = nullptr; values[i] = std::strtod(f[i + 3].c_str(), &end); if (!end || *end != '\0' || !std::isfinite(values[i])) { log.push_back("CREATEPROFILEFROMSURFACE — interval, view origin, and scales must be finite numbers."); return; } }
  if (values[0] <= 0.0 || values[3] <= 0.0 || values[4] <= 0.0) { log.push_back("CREATEPROFILEFROMSURFACE — interval and profile-view scales must be positive."); return; }
  EnsureEntityIds(st);
  if (static_cast<size_t>(si) >= st.drawing.cadSurfaceAttrs.size() || st.drawing.cadSurfaceAttrs[static_cast<size_t>(si)].id == 0 || st.drawing.alignments[static_cast<size_t>(ai)].id == 0) {
    log.push_back("CREATEPROFILEFROMSURFACE — a source has no stable drawing identifier."); return;
  }
  CadSurfaceProfile profile;
  profile.id = st.nextEntityId++;
  profile.name = name;
  profile.surfaceId = st.drawing.cadSurfaceAttrs[static_cast<size_t>(si)].id;
  profile.alignmentId = st.drawing.alignments[static_cast<size_t>(ai)].id;
  profile.sampleInterval = values[0];
  CadCoord::LocalFromWorld(st, values[1], values[2], &profile.viewX, &profile.viewY);
  profile.horizontalScale = values[3]; profile.verticalScale = values[4];
  PushUndoSnapshot(st, "Create surface profile");
  st.drawing.surfaceProfiles.push_back(profile);
  st.drawing.alignments[static_cast<size_t>(ai)].profileIds.push_back(profile.id);
  RefreshSurfaceProfilesForSurface(st, st.drawing.cadSurfaces[static_cast<size_t>(si)]);
  if (st.drawing.surfaceProfiles.back().sourceStatus.empty())
    log.push_back("CREATEPROFILEFROMSURFACE — profile \"" + name + "\" sampled and profile view placed.");
  else
    log.push_back("CREATEPROFILEFROMSURFACE — profile \"" + name + "\" unavailable: " +
                  st.drawing.surfaceProfiles.back().sourceStatus + " Last saved view retained.");
}

void RunSurfaceProfileView(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const auto f = SplitCommaFields(args);
  if (f.size() != 5) { log.push_back("PROFILEVIEW — usage: <profile>, viewX, viewY, horizontal scale, vertical scale."); return; }
  auto it = std::find_if(st.drawing.surfaceProfiles.begin(), st.drawing.surfaceProfiles.end(), [&](const auto& p) { return p.name == StringUtil::trimCopy(f[0]); });
  if (it == st.drawing.surfaceProfiles.end()) { log.push_back("PROFILEVIEW — no saved profile named \"" + f[0] + "\"."); return; }
  double v[4]{};
  for (size_t i = 0; i < 4; ++i) { char* end = nullptr; v[i] = std::strtod(f[i + 1].c_str(), &end); if (!end || *end != '\0' || !std::isfinite(v[i])) { log.push_back("PROFILEVIEW — origin and scales must be finite numbers."); return; } }
  if (v[2] <= 0.0 || v[3] <= 0.0) { log.push_back("PROFILEVIEW — scales must be positive."); return; }
  const int si = FindSurfaceIndexById(st, it->surfaceId);
  if (si < 0) { log.push_back("PROFILEVIEW — source surface is missing; the last saved view remains unchanged."); return; }
  PushUndoSnapshot(st, "Move profile view");
  CadCoord::LocalFromWorld(st, v[0], v[1], &it->viewX, &it->viewY);
  it->horizontalScale = v[2]; it->verticalScale = v[3];
  RefreshSurfaceProfilesForSurface(st, st.drawing.cadSurfaces[static_cast<size_t>(si)]);
  log.push_back("PROFILEVIEW — \"" + it->name + "\" view updated.");
}


void RunQuickProfile(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() == 1 && !f[0].empty()) {
    StartQuickProfileCommand(st, f[0], log);
    return;
  }
  if (f.size() == 2 && !f[0].empty() && RunQuickProfileAlongAlignment(st, f[0], f[1], log))  // REQ-500 AC3
    return;
  if (f.size() != 5 || f[0].empty()) {
    log.push_back("QUICKPROFILE — usage: QUICKPROFILE <surface>[, <x1>, <y1>, <x2>, <y2>].");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("QUICKPROFILE — no surface named \"" + f[0] + "\".");
    return;
  }
  char* e[4] = {};
  const double w0x = std::strtod(f[1].c_str(), &e[0]);
  const double w0y = std::strtod(f[2].c_str(), &e[1]);
  const double w1x = std::strtod(f[3].c_str(), &e[2]);
  const double w1y = std::strtod(f[4].c_str(), &e[3]);
  if (!e[0] || *e[0] != '\0' || !e[1] || *e[1] != '\0' || !e[2] || *e[2] != '\0' || !e[3] || *e[3] != '\0' ||
      !std::isfinite(w0x) || !std::isfinite(w0y) || !std::isfinite(w1x) || !std::isfinite(w1y)) {
    log.push_back("QUICKPROFILE — x1, y1, x2, and y2 must be numbers.");
    return;
  }
  float lx0 = 0.f, ly0 = 0.f, lx1 = 0.f, ly1 = 0.f;
  CadCoord::LocalFromWorld(st, w0x, w0y, &lx0, &ly0);
  CadCoord::LocalFromWorld(st, w1x, w1y, &lx1, &ly1);
  CommitQuickProfileLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], static_cast<double>(lx0),
                          static_cast<double>(ly0), static_cast<double>(lx1), static_cast<double>(ly1), log);
}

/// `SURFACERENAME <old>, <new>` — same duplicate-name refusal as the panel (REQ-075).
void RunSurfaceRename(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() != 2 || f[0].empty() || f[1].empty()) {
    log.push_back("SURFACERENAME — usage: SURFACERENAME <old name>, <new name>.");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACERENAME — no surface named \"" + f[0] + "\".");
    return;
  }
  const int clash = FindSurfaceIndex(st, f[1]);
  if (clash >= 0 && clash != si) {
    log.push_back("SURFACERENAME — a surface named \"" + f[1] + "\" already exists — rename refused.");
    return;
  }
  PushUndoSnapshot(st, "Rename surface");
  log.push_back("Renamed surface \"" + st.drawing.cadSurfaces[static_cast<size_t>(si)].name + "\" to \"" + f[1] + "\".");
  st.drawing.cadSurfaces[static_cast<size_t>(si)].name = f[1];
  BumpCadGpuCache(st);
}

/// `SURFACEDELETE <name>` — undoable in one step (REQ-068), like the panel's Delete.
void RunSurfaceDelete(AppCommandState& st, const std::string& name, std::vector<std::string>& log) {
  if (name.empty()) {
    log.push_back("SURFACEDELETE — usage: SURFACEDELETE <surface name>.");
    return;
  }
  const int si = FindSurfaceIndex(st, name);
  if (si < 0) {
    log.push_back("SURFACEDELETE — no surface named \"" + name + "\".");
    return;
  }
  PushUndoSnapshot(st, "Delete surface");
  log.push_back("Deleted surface \"" + st.drawing.cadSurfaces[static_cast<size_t>(si)].name + "\".");
  EraseSurfaceAtIndex(st, static_cast<size_t>(si));
}

/// `SURFACEREBUILD [<name>]` — rebuilds one surface, or every surface when the name is omitted.
/// Synchronous (\ref BuildSurfaceFromSources), so the result is in the log by the time the command
/// returns — which is what lets a REQ-203 transcript assert on it without pumping a frame loop.
void RunSurfaceRebuild(AppCommandState& st, const std::string& name, std::vector<std::string>& log) {
  if (st.drawing.cadSurfaces.empty()) {
    log.push_back("SURFACEREBUILD — no surfaces in the drawing.");
    return;
  }
  if (name.empty()) {
    const bool hasEditableSurface = std::any_of(st.drawing.cadSurfaces.begin(), st.drawing.cadSurfaces.end(),
        [](const CadSurface& surface) { return !surface.dataShortcutReadOnly; });
    if (!hasEditableSurface) {
      log.push_back("SURFACEREBUILD — all surfaces are read-only project references.");
      return;
    }
    PushUndoSnapshot(st, "Rebuild surfaces");
    for (size_t i = 0; i < st.drawing.cadSurfaces.size(); ++i) {
      if (st.drawing.cadSurfaces[i].dataShortcutReadOnly) {
        log.push_back("SURFACEREBUILD — reference \"" + st.drawing.cadSurfaces[i].name + "\" skipped; it is refreshed from its project source.");
        continue;
      }
      BuildSurfaceFromSources(st, st.drawing.cadSurfaces[i], log);
      ReDrapeLinkedToSurface(st, i, log);  // ADR-069 (b)
    }
    BumpCadGpuCache(st);
    return;
  }
  const int si = FindSurfaceIndex(st, name);
  if (si < 0) {
    log.push_back("SURFACEREBUILD — no surface named \"" + name + "\".");
    return;
  }
  if (st.drawing.cadSurfaces[static_cast<size_t>(si)].dataShortcutReadOnly) {
    log.push_back("SURFACEREBUILD — surface reference \"" + name + "\" is read-only; refresh it from the project source.");
    return;
  }
  PushUndoSnapshot(st, "Rebuild surface");
  BuildSurfaceFromSources(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], log);
  ReDrapeLinkedToSurface(st, static_cast<size_t>(si), log);  // ADR-069 (b)
  BumpCadGpuCache(st);
}

/// `SURFACEADDFILE <surface>, <path>[, <layout>[, HEADER]]` — links a point file into a surface's
/// definition (REQ-086). The file is NOT imported: its points feed the triangulation and never become
/// drawing survey points. `<layout>` is one of PNEZD / PENZD / NEZ / ENZ, defaulting to the first;
/// `HEADER` says the file's first row is a header.
///

void ExecuteExtractCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void ExecuteDrapeCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void ExecutePadSolidCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void StartPadSolidCommand(AppCommandState& st, std::vector<std::string>& log);
void HandlePadSolidPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log);
void BuildPadSolids(AppCommandState& st, const std::string& surfaceName, std::uint64_t boundaryId,
                    double padZ, std::vector<std::string>& log);

// SURFSTYLE (REQ-070) — the command form of the Surface Style editor.
//
// It exists alongside the dialog rather than instead of it for two reasons. A dialog cannot be
// driven by a headless transcript, and REQ-070's acceptance conditions are end-to-end claims —
// "changing the contour interval updates the display without rebuilding the triangulation", "two
// surfaces sharing a style both change" — that no unit test can reach, because they are about what
// the command state machine does to the document. The dialog calls the same helpers.
//
// **Comma-separated arguments**, like every other surface command: style names and surface names
// routinely contain spaces ("Existing Ground", "Contours 1 ft"), so splitting on whitespace would
// make half the names in a real drawing unaddressable. Same reason SURFACECREATE does it.
//
// Every edit goes through PushUndoSnapshot, so a style change is a single undo step. The ADR-020
// document-owned-table pattern makes that free: the table is already in the geometry snapshot.

/// The component \p word names, or nullptr with \p why set — REQ-201, so a typo says what it would
/// have accepted rather than "invalid".

namespace {
SurfaceComponentStyle* SurfaceComponentByName(SurfaceStyle& s, const std::string& word,
                                              std::string* why) {
  const std::string w = StringUtil::toLowerAsciiCopy(word);
  if (w == "triangles" || w == "triangle") return &s.triangles;
  if (w == "border") return &s.border;
  if (w == "major" || w == "majorcontour") return &s.majorContour;
  if (w == "minor" || w == "minorcontour") return &s.minorContour;
  if (w == "points" || w == "point") return &s.points;
  if (why)
    *why = "Unknown component \"" + word + "\" — expected triangles, border, major, minor or points.";
  return nullptr;
}

/// Parse one interval field. A separate function so the message names the FIELD, not just the value:
/// "the minor interval" is what the user has to go and fix.
bool ParseIntervalField(const std::string& text, const char* which, double* out,
                        std::vector<std::string>& log) {
  try {
    size_t used = 0;
    const double v = std::stod(text, &used);
    if (used == StringUtil::trimCopy(text).size()) {
      *out = v;
      return true;
    }
  } catch (...) { // NOLINT(bugprone-empty-catch) - not a number; reported below rather than silently treated as zero
  }
  log.push_back(std::string("SURFSTYLE — the ") + which + " interval must be a number, not \"" +
                text + "\".");
  return false;
}

} // namespace

void ExecuteSurfStyleCommand(AppCommandState& st, const std::string& args,
                             std::vector<std::string>& log) {
  SurfaceStyles::EnsureStandard(st.drawing.surfaceStyles);

  std::string rest;
  const std::string verb = StringUtil::toLowerAsciiCopy(
      StringUtil::trimCopy(args.substr(0, args.find_first_of(" \t,"))));
  {
    const size_t sp = args.find_first_of(" \t");
    rest = sp == std::string::npos ? std::string() : StringUtil::trimCopy(args.substr(sp + 1));
  }

  if (verb.empty()) {
    st.showSurfaceStyleWindow = true;
    st.surfaceStyleUseSurfacesTitle = false;
    log.push_back("SURFSTYLE — surface style editor opened.");
    return;
  }

  const auto usage = [&]() {
    log.push_back("SURFSTYLE — usage: SURFSTYLE (opens the editor) | NEW <style> | DELETE <style> | "
                  "INTERVAL <style>, <minor>, <major> | SHOW|HIDE <style>, "
                  "<triangles|border|major|minor|points> | ASSIGN <surface>, <style> | "
                  "DETACH <surface> | "
                  "ANALYSIS <style>, none|elevation|slope | BAND <style>, <upper bound>, <color> | "
                  "CLEARBANDS <style> | ARROWS <style>, on|off | "
                  "ARROWBAND <style>, <upper bound>, <color> | CLEARARROWBANDS <style>");
  };

  if (verb == "new") {
    const std::string name = StringUtil::trimCopy(rest);
    if (name.empty()) {
      usage();
      return;
    }
    if (SurfaceStyles::Find(st.drawing.surfaceStyles, name)) {
      log.push_back("SURFSTYLE — a style named \"" + name + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    // Copied from Standard rather than value-initialised: a new style that drew nothing would look
    // like the create had failed.
    SurfaceStyle s = SurfaceStyles::StandardSurfaceStyle();
    s.name = name;
    st.drawing.surfaceStyles.push_back(std::move(s));
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — created style \"" + name + "\" (copied from Standard).");
    return;
  }

  if (verb == "delete") {
    const std::string name = StringUtil::trimCopy(rest);
    if (name.empty()) {
      usage();
      return;
    }
    if (name == SurfaceStyles::kStandardName) {
      log.push_back("SURFSTYLE — \"Standard\" cannot be deleted; it is what every unresolved style "
                    "name falls back to.");
      return;
    }
    const auto it = std::find_if(st.drawing.surfaceStyles.begin(), st.drawing.surfaceStyles.end(),
                                 [&](const SurfaceStyle& s) { return s.name == name; });
    if (it == st.drawing.surfaceStyles.end()) {
      log.push_back("SURFSTYLE — no style named \"" + name + "\".");
      return;
    }
    // Deleting a style that surfaces are using is ALLOWED, unlike a text style. REQ-070 makes the
    // fallback an acceptance condition — "a surface whose style was deleted falls back to a default
    // style rather than failing to draw" — so refusing the delete would leave that path unreachable
    // and untested. The surfaces keep their styleName, so re-creating a style with that name adopts
    // them back rather than leaving the reference silently rewritten.
    int usedBy = 0;
    for (const CadSurface& s : st.drawing.cadSurfaces)
      if (s.styleName == name)
        ++usedBy;
    PushUndoSnapshot(st, "Surface style");
    st.drawing.surfaceStyles.erase(it);
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — deleted style \"" + name + "\"." +
                  (usedBy > 0 ? " " + std::to_string(usedBy) +
                                    " surface(s) using it now draw with \"Standard\"."
                              : std::string()));
    return;
  }

  if (verb == "interval") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 3 || f[0].empty()) {
      usage();
      return;
    }
    SurfaceStyle* s = SurfaceStyles::Find(st.drawing.surfaceStyles, f[0]);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + f[0] + "\".");
      return;
    }
    double minor = 0.0, major = 0.0;
    if (!ParseIntervalField(f[1], "minor", &minor, log) ||
        !ParseIntervalField(f[2], "major", &major, log))
      return;
    // REQ-070: rejected with a SPECIFIC message, and rejected BEFORE the value is stored, so the
    // invalid pair never exists to generate mis-labelled contours from.
    std::string why;
    const char* distUnit = CadDrawingInsUnitsAbbrev(st.drawing.drawingInsUnits);  // issue #832
    if (!SurfaceStyles::IntervalsCompatible(minor, major, &why, distUnit)) {
      log.push_back("SURFSTYLE — " + why);
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    s->minorIntervalFt = minor;
    s->majorIntervalFt = major;
    // Bumps the drawing revision because the DISPLAY changed, which is what marks the tab dirty. It
    // does not mark any surface for rebuild: a surface's staleness is its triangulation pointer and
    // its resolved style, never this counter (ADR-036 (e)) — which is exactly why an interval change
    // cannot retriangulate.
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" contours: minor " + SurfaceStyles::FormatFt(minor) +
                  " " + distUnit + ", major " + SurfaceStyles::FormatFt(major) + " " + distUnit + ".");
    return;
  }

  if (verb == "show" || verb == "hide") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 2 || f[0].empty() || f[1].empty()) {
      usage();
      return;
    }
    SurfaceStyle* s = SurfaceStyles::Find(st.drawing.surfaceStyles, f[0]);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + f[0] + "\".");
      return;
    }
    std::string why;
    SurfaceComponentStyle* comp = SurfaceComponentByName(*s, f[1], &why);
    if (!comp) {
      log.push_back("SURFSTYLE — " + why);
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    comp->visible = (verb == "show");
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" " + f[1] + " " +
                  (comp->visible ? "shown." : "hidden."));
    return;
  }

  if (verb == "assign") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 2 || f[0].empty() || f[1].empty()) {
      usage();
      return;
    }
    const int si = FindSurfaceIndex(st, f[0]);
    if (si < 0) {
      log.push_back("SURFSTYLE — no surface named \"" + f[0] + "\".");
      return;
    }
    if (!SurfaceStyles::Find(st.drawing.surfaceStyles, f[1])) {
      log.push_back("SURFSTYLE — no style named \"" + f[1] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    st.drawing.cadSurfaces[static_cast<size_t>(si)].styleName = f[1];
    (void)RefreshSurfaceLabelValues(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], false);
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — surface \"" + st.drawing.cadSurfaces[static_cast<size_t>(si)].name +
                  "\" now uses style \"" + f[1] + "\".");
    return;
  }

  if (verb == "detach") {
    const std::string name = StringUtil::trimCopy(rest);
    if (name.empty()) {
      usage();
      return;
    }
    const int si = FindSurfaceIndex(st, name);
    if (si < 0) {
      log.push_back("SURFSTYLE — no surface named \"" + name + "\".");
      return;
    }
    CadSurface& surf = st.drawing.cadSurfaces[static_cast<size_t>(si)];
    const SurfaceStyle* resolved = SurfaceStyles::Resolve(st.drawing.surfaceStyles, surf.styleName);
    const int users = resolved ? SurfaceStyles::CountSurfacesResolvingTo(st.drawing.cadSurfaces, st.drawing.surfaceStyles,
                                                                         resolved->name)
                               : 0;
    if (users <= 1) {
      DetachSurfaceStyleIfShared(st, static_cast<size_t>(si), &log);
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    DetachSurfaceStyleIfShared(st, static_cast<size_t>(si), &log);
    return;
  }

  // --- REQ-072: the command form of the Analysis tab (TASK-086 §9) -----------------------------
  // The Analysis panel is a Qt window this headless driver cannot reach — the same limitation
  // INTERVAL/SHOW/HIDE already work around — so these verbs call the exact same fields the tab
  // edits, and what a transcript proves is the real edit path rather than a test-only shortcut.

  if (verb == "analysis") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 2 || f[0].empty() || f[1].empty()) {
      usage();
      return;
    }
    SurfaceStyle* s = SurfaceStyles::Find(st.drawing.surfaceStyles, f[0]);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + f[0] + "\".");
      return;
    }
    const std::string mode = StringUtil::toLowerAsciiCopy(f[1]);
    SurfaceAnalysisMode m;
    if (mode == "none") m = SurfaceAnalysisMode::None;
    else if (mode == "elevation") m = SurfaceAnalysisMode::Elevation;
    else if (mode == "slope") m = SurfaceAnalysisMode::Slope;
    else if (mode == "direction") m = SurfaceAnalysisMode::Direction;
    else if (mode == "slopeangle" || mode == "angle") m = SurfaceAnalysisMode::SlopeAngle;
    else {
      log.push_back("SURFSTYLE — analysis type must be none, elevation, slope, direction or slopeangle, not \"" + f[1] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    s->analysisMode = m;
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" analysis: " + mode + ".");
    return;
  }

  if (verb == "usercontour") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 2 || f[0].empty()) {
      usage();
      return;
    }
    SurfaceStyle* s = SurfaceStyles::Find(st.drawing.surfaceStyles, f[0]);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + f[0] + "\".");
      return;
    }
    double elev = 0.0;
    if (!ParseIntervalField(f[1], "user contour elevation", &elev, log))
      return;
    PushUndoSnapshot(st, "Surface style");
    s->userContourFt.push_back(elev);
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" user contour " + FormatLinear(elev, st.displayLinearPrecision) +
                  ".");
    return;
  }

  if (verb == "smooth") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 2 || f[0].empty()) {
      usage();
      return;
    }
    SurfaceStyle* s = SurfaceStyles::Find(st.drawing.surfaceStyles, f[0]);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + f[0] + "\".");
      return;
    }
    char* end = nullptr;
    const long n = std::strtol(f[1].c_str(), &end, 10);
    if (!end || *end != '\0' || n < 0 || n > 5) {
      log.push_back("SURFSTYLE — smooth passes must be 0–5.");
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    s->contourSmoothPasses = static_cast<int>(n);
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" contour smooth " + std::to_string(n) + ".");
    return;
  }

  if (verb == "labels") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 2 || f[0].empty()) {
      usage();
      return;
    }
    SurfaceStyle* s = SurfaceStyles::Find(st.drawing.surfaceStyles, f[0]);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + f[0] + "\".");
      return;
    }
    double sp = 0.0;
    if (!ParseIntervalField(f[1], "label spacing", &sp, log))
      return;
    if (sp < 0.0)
      sp = 0.0;
    PushUndoSnapshot(st, "Surface style");
    s->contourLabelSpacingFt = sp;
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" contour labels " + FormatLinear(sp, st.displayLinearPrecision) +
                  ".");
    return;
  }

  if (verb == "band" || verb == "arrowband") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 3 || f[0].empty() || f[2].empty()) {
      usage();
      return;
    }
    SurfaceStyle* s = SurfaceStyles::Find(st.drawing.surfaceStyles, f[0]);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + f[0] + "\".");
      return;
    }
    double bound = 0.0;
    if (!ParseIntervalField(f[1], "band bound", &bound, log))
      return;
    PushUndoSnapshot(st, "Surface style");
    std::vector<SurfaceBand>& bands = (verb == "band") ? s->bands : s->arrowBands;
    bands.push_back({bound, StringUtil::trimCopy(f[2])});
    // Kept strictly ascending exactly as the Analysis tab's `BandTableEditor` re-sorts on every edit
    // (`ui/CadUi_SurfaceStyles.cpp`) — `AssignBand`'s precondition (`util/surfaceanalysis.hpp`), so a
    // command-built table is never in a different state than a hand-built one.
    std::sort(bands.begin(), bands.end(),
             [](const SurfaceBand& a, const SurfaceBand& b) { return a.upperBound < b.upperBound; });
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" " + verb + " added: <= " + f[1] + " -> " + f[2] + ".");
    return;
  }

  if (verb == "clearbands" || verb == "cleararrowbands") {
    const std::string name = StringUtil::trimCopy(rest);
    SurfaceStyle* s = SurfaceStyles::Find(st.drawing.surfaceStyles, name);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + name + "\".");
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    (verb == "clearbands" ? s->bands : s->arrowBands).clear();
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" " + verb + ".");
    return;
  }

  if (verb == "arrows") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 2 || f[0].empty() || f[1].empty()) {
      usage();
      return;
    }
    SurfaceStyle* s = SurfaceStyles::Find(st.drawing.surfaceStyles, f[0]);
    if (!s) {
      log.push_back("SURFSTYLE — no style named \"" + f[0] + "\".");
      return;
    }
    const std::string onOff = StringUtil::toLowerAsciiCopy(f[1]);
    if (onOff != "on" && onOff != "off") {
      log.push_back("SURFSTYLE — arrows must be on or off, not \"" + f[1] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Surface style");
    s->slopeArrowsOn = (onOff == "on");
    BumpCadGpuCache(st);
    log.push_back("SURFSTYLE — \"" + s->name + "\" slope arrows " + onOff + ".");
    return;
  }

  usage();
}

/// `VOLUMES <base surface>, <comparison surface>` — REQ-073's cut/fill/net report (TASK-095 §6
/// step 1/2), the synchronous command form of the eventual Volume Dashboard: it exists so the
/// requirement's ORIGINAL (2026-08-12) acceptance is reachable and testable from a headless
/// transcript before any panel or async wiring exists, exactly the role SURFELEV played for REQ-074
/// before REQ-075's manager panel.
///
/// **Self-comparison is ANSWERED, not refused** — REQ-073's own acceptance requires "comparing a
/// surface with itself reports zero net within tolerance", so `VOLUMES Foo, Foo` must compute rather
/// than being rejected as a no-op the way some other entity-vs-itself commands elsewhere are.
void ExecuteVolumesCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() < 2 || f.size() > 3 || f[0].empty() || f[1].empty()) {
    log.push_back("VOLUMES — usage: VOLUMES <base surface>, <comparison surface>[, <clip entity id>].");
    return;
  }
  const int baseIx = FindSurfaceIndex(st, f[0]);
  if (baseIx < 0) {
    log.push_back("VOLUMES — no surface named \"" + f[0] + "\".");
    return;
  }
  const int compIx = FindSurfaceIndex(st, f[1]);
  if (compIx < 0) {
    log.push_back("VOLUMES — no surface named \"" + f[1] + "\".");
    return;
  }
  const std::shared_ptr<const CadTin>& baseTin = st.drawing.cadSurfaces[static_cast<size_t>(baseIx)].tin;
  const std::shared_ptr<const CadTin>& compTin = st.drawing.cadSurfaces[static_cast<size_t>(compIx)].tin;
  if (!baseTin || !compTin) {
    log.push_back("VOLUMES — \"" + f[0] + "\" and \"" + f[1] + "\" must both have a triangulation "
                  "before a volume can be reported.");
    return;
  }

  std::vector<std::pair<double, double>> clipRing;
  const std::vector<std::pair<double, double>>* clipPtr = nullptr;
  if (f.size() == 3) {
    char* end = nullptr;
    const unsigned long long raw = std::strtoull(f[2].c_str(), &end, 10);
    if (f[2].empty() || end == f[2].c_str() || *end != '\0') {
      log.push_back("VOLUMES — clip entity id must be an integer, not \"" + f[2] + "\".");
      return;
    }
    std::string err;
    if (!VolumeClipRingLocalXy(st, static_cast<std::uint64_t>(raw), &clipRing, &err)) {
      log.push_back("VOLUMES — " + err + ".");
      return;
    }
    if (clipRing.size() >= 3)
      clipPtr = &clipRing;
  }

  const SurfaceVolumeResult r =
      ComputeSurfaceVolume(baseTin->vertsXyz, baseTin->indices, compTin->vertsXyz, compTin->indices,
                           nullptr, nullptr, clipPtr);
  const int p = st.displayLinearPrecision;
  if (!r.overlapped) {
    if (clipPtr) {
      log.push_back("VOLUMES — \"" + f[0] + "\" and \"" + f[1] +
                    "\" have no overlap inside the clip. Cut 0, fill 0, net 0.");
    } else {
      log.push_back("VOLUMES — \"" + f[0] + "\" and \"" + f[1] + "\" have no common area. Cut 0, fill "
                    "0, net 0.");
    }
    return;
  }
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "VOLUMES — \"%s\" (base) vs \"%s\" (comparison): cut %s, fill %s, net %s, "
                "cut area %s ft2, fill area %s ft2, common area %s ft2.",
                f[0].c_str(), f[1].c_str(), FormatVolumeYd3(r.cutFt3, p).c_str(),
                FormatVolumeYd3(r.fillFt3, p).c_str(), FormatVolumeYd3(r.netFt3, p).c_str(),
                FormatLinear(r.cutAreaFt2, p).c_str(), FormatLinear(r.fillAreaFt2, p).c_str(),
                FormatLinear(r.commonAreaFt2, p).c_str());
  log.push_back(buf);
  st.lastVolumeReportText = buf;
  st.hasLastVolumeResult = true;
  st.lastVolumeResult = r;
  st.lastVolumeBaseName = f[0];
  st.lastVolumeComparisonName = f[1];
}

/// `VOLDASH [<verb> …]` — the command form of the Volume Dashboard panel (REQ-073 amendment,
/// TASK-095 §8), the same role `SURFSTYLE`'s verbs play for the Analysis panel: the panel is a Qt
/// window this headless driver cannot reach, so PICK/MAP/CLOSE call the exact fields the panel edits.
///
/// **`RECOMPUTE` is the synchronous test surrogate for the real async live-recompute
/// (`TickVolumeDashboard`)** — the same relationship `SURFACEREBUILD` has to `TickSurfaceRebuilds`
/// (REQ-069's own precedent, `req069-surface-definition-commands.txt`'s header): a headless driver
/// with no frame loop cannot race a real background thread deterministically, so the TRUE live
/// behaviour (automatic recompute with no user action, driven by `main.cpp`'s frame loop) is verified
/// manually, exactly as REQ-069's dynamic rebuild itself is. `RECOMPUTE` shares
/// `VolumeDashboardReady` — the identical readiness gate the real async path uses — so what IS
/// testable here (the panel's own state transitions: which pick, which stale/waiting reason, the map
/// toggle) cannot silently disagree with the live path about when a comparison is ready.
void ExecuteVolDashCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AppCommandState::VolumeDashboardState& dash = st.volumeDashboard;

  std::string rest;
  const std::string verb = StringUtil::toLowerAsciiCopy(
      StringUtil::trimCopy(args.substr(0, args.find_first_of(" \t,"))));
  {
    const size_t sp = args.find_first_of(" \t");
    rest = sp == std::string::npos ? std::string() : StringUtil::trimCopy(args.substr(sp + 1));
  }

  if (verb.empty()) {
    dash.open = true;
    log.push_back("VOLDASH — Volume Dashboard opened.");
    return;
  }

  const auto usage = [&]() {
    log.push_back("VOLDASH — usage: VOLDASH (opens the panel) | CLOSE | "
                  "PICK <base surface>, <comparison surface> | CLIP <entity id>|none | MAP on|off | RECOMPUTE");
  };

  if (verb == "close") {
    dash.open = false;
    log.push_back("VOLDASH — Volume Dashboard closed.");
    return;
  }

  if (verb == "pick") {
    const std::vector<std::string> f = SplitCommaFields(rest);
    if (f.size() != 2 || f[0].empty() || f[1].empty()) {
      usage();
      return;
    }
    const int baseIx = FindSurfaceIndex(st, f[0]);
    if (baseIx < 0) {
      log.push_back("VOLDASH — no surface named \"" + f[0] + "\".");
      return;
    }
    const int compIx = FindSurfaceIndex(st, f[1]);
    if (compIx < 0) {
      log.push_back("VOLDASH — no surface named \"" + f[1] + "\".");
      return;
    }
    dash.baseSurfaceId = st.drawing.cadSurfaceAttrs[static_cast<size_t>(baseIx)].id;
    dash.comparisonSurfaceId = st.drawing.cadSurfaceAttrs[static_cast<size_t>(compIx)].id;
    log.push_back("VOLDASH — base \"" + f[0] + "\", comparison \"" + f[1] + "\".");
    return;
  }

  if (verb == "add") {
    SurfaceVolumeResult snap;
    std::uint64_t baseId = dash.baseSurfaceId;
    std::uint64_t compId = dash.comparisonSurfaceId;
    if (dash.hasResult) {
      snap = dash.lastResult;
    } else if (st.hasLastVolumeResult) {
      const std::string b = SurfaceNameForId(st, dash.baseSurfaceId);
      const std::string c = SurfaceNameForId(st, dash.comparisonSurfaceId);
      if (!b.empty() && (b != st.lastVolumeBaseName || c != st.lastVolumeComparisonName)) {
        log.push_back("VOLDASH ADD — no landed volume result to snapshot.");
        return;
      }
      snap = st.lastVolumeResult;
    } else {
      log.push_back("VOLDASH ADD — no landed volume result to snapshot.");
      return;
    }
    AppCommandState::VolumeDashboardState::AnalysisRow row;
    row.label = rest.empty() ? ("A" + std::to_string(dash.rows.size() + 1)) : rest;
    row.baseSurfaceId = baseId;
    row.comparisonSurfaceId = compId;
    row.clipEntityId = dash.clipEntityId;
    row.result = snap;
    row.hasResult = true;
    dash.rows.push_back(std::move(row));
    log.push_back("VOLDASH ADD — row \"" + dash.rows.back().label + "\" (" +
                  std::to_string(dash.rows.size()) + " total).");
    return;
  }

  if (verb == "clip") {
    const std::string tok = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(rest));
    if (tok == "none" || tok.empty()) {
      dash.clipEntityId = 0;
      log.push_back("VOLDASH — clip cleared.");
      return;
    }
    char* end = nullptr;
    const unsigned long long raw = std::strtoull(rest.c_str(), &end, 10);
    if (rest.empty() || end == rest.c_str() || *end != '\0') {
      log.push_back("VOLDASH — clip must be an entity id or none, not \"" + rest + "\".");
      return;
    }
    std::vector<std::pair<double, double>> ring;
    std::string err;
    if (!VolumeClipRingLocalXy(st, static_cast<std::uint64_t>(raw), &ring, &err)) {
      log.push_back("VOLDASH — " + err + ".");
      return;
    }
    dash.clipEntityId = static_cast<std::uint64_t>(raw);
    log.push_back("VOLDASH — clip entity " + rest + ".");
    return;
  }

  if (verb == "map") {
    const std::string onOff = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(rest));
    if (onOff != "on" && onOff != "off") {
      log.push_back("VOLDASH — map must be on or off, not \"" + rest + "\".");
      return;
    }
    dash.showMap = (onOff == "on");
    log.push_back(std::string("VOLDASH — cut/fill map ") + onOff + ".");
    return;
  }

  if (verb == "recompute") {
    if (dash.baseSurfaceId == 0 || dash.comparisonSurfaceId == 0) {
      log.push_back("VOLDASH — pick a base and a comparison surface first.");
      return;
    }
    std::string whyNot;
    if (!VolumeDashboardReady(st, dash.baseSurfaceId, dash.comparisonSurfaceId, &whyNot)) {
      log.push_back("VOLDASH — not ready: " + whyNot + ".");
      return;
    }
    const CadSurface& baseSurf =
        st.drawing.cadSurfaces[static_cast<size_t>(FindSurfaceIndexById(st, dash.baseSurfaceId))];
    const CadSurface& compSurf =
        st.drawing.cadSurfaces[static_cast<size_t>(FindSurfaceIndexById(st, dash.comparisonSurfaceId))];
    dash.mapCutTrianglesXyz.clear();
    dash.mapFillTrianglesXyz.clear();
    std::vector<std::pair<double, double>> clipRing;
    std::string clipErr;
    if (!VolumeClipRingLocalXy(st, dash.clipEntityId, &clipRing, &clipErr)) {
      log.push_back("VOLDASH — " + clipErr + ".");
      return;
    }
    const std::vector<std::pair<double, double>>* clipPtr =
        clipRing.size() >= 3 ? &clipRing : nullptr;
    dash.lastResult = ComputeSurfaceVolume(
        baseSurf.tin->vertsXyz, baseSurf.tin->indices, compSurf.tin->vertsXyz, compSurf.tin->indices,
        dash.showMap ? &dash.mapCutTrianglesXyz : nullptr, dash.showMap ? &dash.mapFillTrianglesXyz : nullptr,
        clipPtr);
    dash.hasResult = true;
    dash.resultForRevision = st.cadGpuRevision;
    dash.resultForBaseSurfaceId = dash.baseSurfaceId;
    dash.resultForComparisonSurfaceId = dash.comparisonSurfaceId;
    dash.resultForClipEntityId = dash.clipEntityId;
    dash.resultHasMap = dash.showMap;
    RememberVolumeResult(st, dash.lastResult, SurfaceNameForId(st, dash.baseSurfaceId),
                         SurfaceNameForId(st, dash.comparisonSurfaceId));
    const int p = st.displayLinearPrecision;
    log.push_back("VOLDASH — cut " + FormatVolumeYd3(dash.lastResult.cutFt3, p) + ", fill " +
                  FormatVolumeYd3(dash.lastResult.fillFt3, p) + ", net " +
                  FormatVolumeYd3(dash.lastResult.netFt3, p) + ", cut area " +
                  FormatLinear(dash.lastResult.cutAreaFt2, p) + " ft2, fill area " +
                  FormatLinear(dash.lastResult.fillAreaFt2, p) + " ft2, common area " +
                  FormatLinear(dash.lastResult.commonAreaFt2, p) + " ft2.");
    return;
  }

  usage();
}

void ExecuteToolspaceCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string verb = StringUtil::toLowerAsciiCopy(
      StringUtil::trimCopy(args.substr(0, args.find_first_of(" \t,"))));

  const auto dumpTree = [&]() {
    std::string tabName = "Prospector";
    std::vector<std::string> lines;
    if (st.toolspaceTab == AppCommandState::ToolspaceTab::Project) {
      tabName = "Project";
      AppendToolspaceProjectLines(st, &lines);
    } else if (st.toolspaceTab == AppCommandState::ToolspaceTab::Settings) {
      tabName = "Settings";
      AppendToolspaceSettingsLines(st, &lines);
    } else {
      AppendToolspaceProspectorLines(st, &lines);
    }
    log.push_back("TOOLSPACE — tab: " + tabName);
    for (const std::string& line : lines)
      log.push_back(std::string("TOOLSPACE  ") + line);
  };

  if (verb.empty()) {
    st.showToolspaceWindow = true;
    st.toolspaceTab = AppCommandState::ToolspaceTab::Prospector;
    log.push_back("TOOLSPACE — opened Prospector.");
    return;
  }
  if (verb == "prospector") {
    st.showToolspaceWindow = true;
    st.toolspaceTab = AppCommandState::ToolspaceTab::Prospector;
    log.push_back("TOOLSPACE — Prospector.");
    return;
  }
  if (verb == "project") {
    st.showToolspaceWindow = true;
    st.toolspaceTab = AppCommandState::ToolspaceTab::Project;
    log.push_back("TOOLSPACE — Project.");
    return;
  }
  if (verb == "settings") {
    st.showToolspaceWindow = true;
    st.toolspaceTab = AppCommandState::ToolspaceTab::Settings;
    log.push_back("TOOLSPACE — Settings.");
    return;
  }
  if (verb == "list") {
    dumpTree();
    return;
  }
  if (verb == "close" || verb == "off") {
    st.showToolspaceWindow = false;
    log.push_back("TOOLSPACE — closed.");
    return;
  }
  log.push_back("TOOLSPACE — usage: TOOLSPACE [PROJECT|PROSPECTOR|SETTINGS|LIST|CLOSE]. Unknown \"" +
                StringUtil::trimCopy(args) + "\".");
}

/// `UNDESIGNATE <surface>, <BREAKLINE|BOUNDARY|POINTFILE>, <n>` — removes one item from a surface's
/// definition (REQ-069's "remove", the counterpart to DESIGNATEBREAKLINE/DESIGNATEBOUNDARY). \p n is
/// 1-based and matches the numbering SURFACELIST prints. Deleting the referenced entity also removes
/// the item, but only that entity's other uses go with it — this removes the item alone.
void RunUndesignate(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() != 3 || f[0].empty()) {
    log.push_back("UNDESIGNATE — usage: UNDESIGNATE <surface name>, <BREAKLINE|BOUNDARY|POINTFILE|CONTOUR>, <number>.");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("UNDESIGNATE — no surface named \"" + f[0] + "\".");
    return;
  }
  const std::string what = StringUtil::toLowerAsciiCopy(f[1]);
  const bool isBoundary = (what == "boundary");
  const bool isPointFile = (what == "pointfile");
  const bool isContour = (what == "contour");
  if (!isBoundary && !isPointFile && !isContour && what != "breakline") {
    log.push_back("UNDESIGNATE — second argument must be BREAKLINE, BOUNDARY, POINTFILE or CONTOUR.");
    return;
  }
  // strtol rather than stoi: a non-numeric argument is a user typo, not an exceptional condition,
  // and this translation unit is compiled without exception unwinding (C4530).
  char* numEnd = nullptr;
  const long parsed = std::strtol(f[2].c_str(), &numEnd, 10);
  const bool numOk = !f[2].empty() && numEnd && *numEnd == '\0';
  const int n = numOk ? static_cast<int>(parsed) : 0;  // 0 fails the range check below

  CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  const size_t count = isBoundary     ? s.boundaries.size()
                       : isPointFile  ? s.sourcePointFiles.size()
                       : isContour    ? s.contourSources.size()
                                      : s.breaklines.size();
  if (n < 1 || static_cast<size_t>(n) > count) {
    log.push_back("UNDESIGNATE — \"" + s.name + "\" has " + std::to_string(count) + " " + what +
                  "(s); " + f[2] + " is out of range (SURFACELIST numbers them).");
    return;
  }
  PushUndoSnapshot(st, isBoundary    ? "Remove surface boundary"
                       : isPointFile ? "Unlink surface point file"
                       : isContour   ? "Remove surface contour source"
                                     : "Remove surface breakline");
  if (isBoundary)
    s.boundaries.erase(s.boundaries.begin() + (n - 1));
  else if (isPointFile)
    s.sourcePointFiles.erase(s.sourcePointFiles.begin() + (n - 1));
  else if (isContour)
    s.contourSources.erase(s.contourSources.begin() + (n - 1));
  else
    s.breaklines.erase(s.breaklines.begin() + (n - 1));
  log.push_back("UNDESIGNATE — removed " + what + " " + std::to_string(n) + " from \"" + s.name + "\".");
  BumpCadGpuCache(st);  // TickSurfaceRebuilds picks the change up; SURFACEREBUILD forces it now
}

void RunSurfaceStats(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = StringUtil::trimCopy(args);
  auto reportOne = [&](const CadSurface& s) {
    const SurfaceStats stt =
        s.tin ? ComputeSurfaceStats(s.tin->vertsXyz, s.tin->indices, static_cast<int>(s.breaklines.size()),
                                    s.isVolumeSurface())
              : SurfaceStats{};
    if (!stt.built) {
      log.push_back("SURFACESTATS — \"" + s.name + "\": not built.");
      return;
    }
    const int p = st.displayLinearPrecision;
    std::string line = "SURFACESTATS — \"" + s.name + "\": " + std::to_string(stt.points) + " points, " +
                       std::to_string(stt.triangles) + " triangles, 2D area " + FormatLinear(stt.area2d, p) +
                       ", 3D area " + FormatLinear(stt.area3d, p) + ", tri area " +
                       FormatLinear(stt.minTriArea2d, p) + " to " + FormatLinear(stt.maxTriArea2d, p) +
                       ", edges " + std::to_string(stt.uniqueEdges) + " (breaklines " +
                       std::to_string(stt.breaklineEdges) + "), elev " + FormatLinear(stt.minZ, p) + " to " +
                       FormatLinear(stt.maxZ, p) + ", slope " + FormatLinear(stt.minSlopePct, 2) + "% to " +
                       FormatLinear(stt.maxSlopePct, 2) + "% (mean " + FormatLinear(stt.meanSlopePct, 2) +
                       "%), slope " + FormatLinear(stt.minSlopeDeg, 2) + " to " +
                       FormatLinear(stt.maxSlopeDeg, 2) + " deg (mean " + FormatLinear(stt.meanSlopeDeg, 2) +
                       " deg).";
    if (s.isVolumeSurface())
      line += " volume cut " + FormatVolumeYd3(stt.volumeCutFt3, p) + ", fill " +
              FormatVolumeYd3(stt.volumeFillFt3, p) + ".";
    log.push_back(std::move(line));
  };
  if (name.empty()) {
    if (st.drawing.cadSurfaces.empty()) {
      log.push_back("SURFACESTATS — no surfaces in the drawing.");
      return;
    }
    for (const CadSurface& s : st.drawing.cadSurfaces)
      reportOne(s);
    return;
  }
  const int si = FindSurfaceIndex(st, name);
  if (si < 0) {
    log.push_back("SURFACESTATS — no surface named \"" + name + "\".");
    return;
  }
  reportOne(st.drawing.cadSurfaces[static_cast<size_t>(si)]);
}



std::unique_ptr<ISurfaceQuery> MakeSurfaceQueryForCommands(const AppCommandState& st, const CadSurface& surf) {
  return MakeSurfaceQuery(st, surf);  // REQ-497: alignment commands read the same ground the QUICKPROFILE command does
}

bool MoveSelectedBlockRefsToSurface(AppCommandState& st, size_t surfaceIndex, std::vector<std::string>& log) {
  if (surfaceIndex >= st.drawing.cadSurfaces.size()) {
    log.push_back("Move to Surface — the selected surface no longer exists.");
    return false;
  }
  const auto query = MakeSurfaceQuery(st, st.drawing.cadSurfaces[surfaceIndex]);
  if (!query) {
    log.push_back("Move to Surface — the selected surface has no built data.");
    return false;
  }
  std::vector<std::pair<size_t, double>> elevations;
  for (const SelectedEntity& selected : st.selection) {
    if (selected.type != SelectedEntity::Type::BlockRef || selected.index < 0 ||
        static_cast<size_t>(selected.index) >= st.drawing.cadBlockRefs.size())
      continue;
    const CadBlockRef& block = st.drawing.cadBlockRefs[static_cast<size_t>(selected.index)];
    double z = 0.0;
    if (!query->elevationAt(block.xf.x, block.xf.y, &z)) {
      log.push_back("Move to Surface — a selected block is outside the surface; no blocks were moved.");
      return false;
    }
    elevations.emplace_back(static_cast<size_t>(selected.index), z);
  }
  if (elevations.empty()) {
    log.push_back("Move to Surface — select one or more block references first.");
    return false;
  }
  PushUndoSnapshot(st, "Move blocks to surface");
  for (const auto& [index, z] : elevations)
    st.drawing.cadBlockRefs[index].xf.z = static_cast<float>(z);
  BumpCadGpuCache(st);
  log.push_back("Move to Surface — moved " + std::to_string(elevations.size()) + " block reference(s) to \"" +
                st.drawing.cadSurfaces[surfaceIndex].name + "\".");
  return true;
}

bool SetSurfaceDisplayLod(AppCommandState& st, size_t surfaceIndex, bool reduced) {
  if (surfaceIndex >= st.drawing.cadSurfaces.size() || st.drawing.cadSurfaces[surfaceIndex].reducedDisplayLod == reduced)
    return false;
  PushUndoSnapshot(st, reduced ? "Reduced surface display detail" : "High surface display detail");
  st.drawing.cadSurfaces[surfaceIndex].reducedDisplayLod = reduced;
  BumpCadGpuCache(st);
  return true;
}


bool SetModelRasterDrape(AppCommandState& st, size_t imageIndex, size_t surfaceIndex,
                         std::vector<std::string>& log) {
  if (imageIndex >= st.drawing.cadRasterImages.size()) {
    log.push_back("SURFDRAPE — the selected image no longer exists; no change was made.");
    return false;
  }
  if (surfaceIndex >= st.drawing.cadSurfaces.size() || !st.drawing.cadSurfaces[surfaceIndex].tin ||
      st.drawing.cadSurfaces[surfaceIndex].tin->indices.empty()) {
    log.push_back("SURFDRAPE — the selected surface has no built TIN; no change was made.");
    return false;
  }
  EnsureEntityIds(st);
  const std::uint64_t surfaceId = st.drawing.cadSurfaceAttrs[surfaceIndex].id;
  if (surfaceId == 0) {
    log.push_back("SURFDRAPE — the selected surface has no stable identity; no change was made.");
    return false;
  }
  if (st.drawing.cadRasterImages[imageIndex].surfaceId == surfaceId) {
    log.push_back("SURFDRAPE — the image is already draped on this surface.");
    return false;
  }
  PushUndoSnapshot(st, "Drape image on surface");
  st.drawing.cadRasterImages[imageIndex].surfaceId = surfaceId;
  BumpCadGpuCache(st);
  log.push_back("SURFDRAPE — image " + std::to_string(st.drawing.cadRasterImages[imageIndex].id) +
                " is draped on \"" + st.drawing.cadSurfaces[surfaceIndex].name + "\".");
  return true;
}

void RunSurfaceLodCommand(AppCommandState& st, const std::string& args, bool reduced,
                          std::vector<std::string>& log) {
  const char* command = reduced ? "SURFLODLOW" : "SURFLODHIGH";
  const std::vector<std::string> fields = SplitCommaFields(args);
  if (fields.size() != 1 || fields.front().empty()) {
    log.push_back(std::string(command) + " — usage: " + command + " <surface name>.");
    return;
  }
  const int index = FindSurfaceIndex(st, fields.front());
  if (index < 0) {
    log.push_back(std::string(command) + " — no surface named \"" + fields.front() + "\".");
    return;
  }
  const bool changed = SetSurfaceDisplayLod(st, static_cast<size_t>(index), reduced);
  log.push_back(std::string(command) + " — \"" + st.drawing.cadSurfaces[static_cast<size_t>(index)].name +
                (changed ? (reduced ? "\" now uses Reduced display detail." : "\" now uses High display detail.")
                         : (reduced ? "\" already uses Reduced display detail." : "\" already uses High display detail.")));
}

void RunMoveToSurfaceCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> fields = SplitCommaFields(args);
  if (fields.size() != 1 || fields.front().empty()) {
    log.push_back("MOVETOSURFACE — usage: MOVETOSURFACE <surface name> (select block references first).");
    return;
  }
  const int index = FindSurfaceIndex(st, fields.front());
  if (index < 0) {
    log.push_back("MOVETOSURFACE — no surface named \"" + fields.front() + "\".");
    return;
  }
  MoveSelectedBlockRefsToSurface(st, static_cast<size_t>(index), log);
}


namespace {

bool SurfaceSightVisible(const CadTin& tin, double ox, double oy, double oz,
                         double tx, double ty, double tz, double* obstructionX = nullptr,
                         double* obstructionY = nullptr, double* obstructionZ = nullptr,
                         bool* fullyCovered = nullptr) {
  struct Interval { double first, last; };
  std::vector<Interval> intervals;
  const double dx = tx - ox, dy = ty - oy;
  for (size_t ti = 0; ti + 2 < tin.indices.size(); ti += 3) {
    const std::uint32_t ia = tin.indices[ti], ib = tin.indices[ti + 1], ic = tin.indices[ti + 2];
    if ((static_cast<size_t>(std::max({ia, ib, ic})) + 1) * 3 > tin.vertsXyz.size()) continue;
    const double ax = tin.vertsXyz[static_cast<size_t>(ia) * 3], ay = tin.vertsXyz[static_cast<size_t>(ia) * 3 + 1];
    const double bx = tin.vertsXyz[static_cast<size_t>(ib) * 3], by = tin.vertsXyz[static_cast<size_t>(ib) * 3 + 1];
    const double cx = tin.vertsXyz[static_cast<size_t>(ic) * 3], cy = tin.vertsXyz[static_cast<size_t>(ic) * 3 + 1];
    const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (std::abs(area) <= 1.e-15) continue;
    const double orientation = area > 0.0 ? 1.0 : -1.0;
    double first = 0.0, last = 1.0;
    const std::array<std::pair<double,double>, 3> a = {{{ax,ay},{bx,by},{cx,cy}}};
    const std::array<std::pair<double,double>, 3> b = {{{bx,by},{cx,cy},{ax,ay}}};
    bool intersects = true;
    for (size_t edge = 0; edge < 3; ++edge) {
      const double ex = b[edge].first - a[edge].first, ey = b[edge].second - a[edge].second;
      const double f0 = orientation * (ex * (oy - a[edge].second) - ey * (ox - a[edge].first));
      const double slope = orientation * (ex * dy - ey * dx);
      if (std::abs(slope) <= 1.e-15) { if (f0 < -1.e-10) { intersects = false; break; } continue; }
      const double crossing = (-1.e-10 - f0) / slope;
      if (slope > 0.0) first = std::max(first, crossing); else last = std::min(last, crossing);
      if (first > last) { intersects = false; break; }
    }
    if (intersects && last >= 0.0 && first <= 1.0) intervals.push_back({std::clamp(first,0.0,1.0), std::clamp(last,0.0,1.0)});
  }
  std::sort(intervals.begin(), intervals.end(), [](const Interval& a, const Interval& b) { return a.first < b.first; });
  double coveredThrough = 0.0;
  bool covered = !intervals.empty();
  for (const Interval& interval : intervals) {
    if (interval.first > coveredThrough + 1.e-9) { covered = false; break; }
    coveredThrough = std::max(coveredThrough, interval.last);
  }
  covered = covered && coveredThrough >= 1.0 - 1.e-9;
  if (fullyCovered) *fullyCovered = covered;
  if (!covered) return false;
  for (const Interval& interval : intervals) {
    double z0 = 0.0, z1 = 0.0;
    const double x0 = ox + dx * interval.first, y0 = oy + dy * interval.first;
    const double x1 = ox + dx * interval.last, y1 = oy + dy * interval.last;
    if (!TinElevationAt(tin.vertsXyz, tin.indices, x0, y0, &z0) ||
        !TinElevationAt(tin.vertsXyz, tin.indices, x1, y1, &z1)) {
      if (fullyCovered) *fullyCovered = false;
      return false;
    }
    const double d0 = z0 - (oz + (tz - oz) * interval.first);
    const double d1 = z1 - (oz + (tz - oz) * interval.last);
    if (d0 > 0.002 || d1 > 0.002 || (d0 <= 0.002 && d1 > 0.002)) {
      double t = interval.first;
      if (d0 <= 0.002 && d1 > 0.002 && d1 != d0)
        t += (interval.last - interval.first) * (0.002 - d0) / (d1 - d0);
      if (obstructionX) *obstructionX = ox + dx * t;
      if (obstructionY) *obstructionY = oy + dy * t;
      if (obstructionZ) (void)TinElevationAt(tin.vertsXyz, tin.indices, ox + dx * t, oy + dy * t, obstructionZ);
      return false;
    }
  }
  return true;
}

void AddSurfaceResultLine(AppCommandState& st, double ax, double ay, double az,
                          double bx, double by, double bz, const char* color) {
  st.drawing.pushLine(CadLineRecord{ax, ay, az, bx, by, bz});
  EntityAttributes attrs = MakeNewEntityAttrs(st);
  attrs.color = color;
  st.drawing.userLineAttrs.push_back(std::move(attrs));
}

}  // namespace

void RunSurfaceVisibility(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() < 2) {
    log.push_back("SURFACEVISIBILITY — usage: SURFACEVISIBILITY <surface>, LOS|ZVI, coordinates...");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACEVISIBILITY — no surface named \"" + f[0] + "\".");
    return;
  }
  const CadSurface& surface = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  const auto query = MakeSurfaceQueryForCommands(st, surface);
  if (!query || !surface.tin || surface.tin->indices.empty()) {
    log.push_back("SURFACEVISIBILITY — surface \"" + surface.name + "\" has no built TIN.");
    return;
  }
  const std::string mode = StringUtil::toLowerAsciiCopy(f[1]);
  auto number = [&](size_t i, double* out) {
    if (i >= f.size()) return false;
    char* end = nullptr;
    *out = std::strtod(f[i].c_str(), &end);
    return end && *end == '\0' && std::isfinite(*out);
  };
  auto worldToLocal = [&](double wx, double wy, double* x, double* y) {
    CadCoord::LocalFromWorld(st, wx, wy, x, y);
  };
  if (mode == "los") {
    if (f.size() != 8) {
      log.push_back("SURFACEVISIBILITY — LOS usage: SURFACEVISIBILITY <surface>, LOS, eyeX, eyeY, eyeHeight, targetX, targetY, targetHeight.");
      return;
    }
    double wx0 = 0.0, wy0 = 0.0, eye = 0.0, wx1 = 0.0, wy1 = 0.0, target = 0.0;
    if (!number(2, &wx0) || !number(3, &wy0) || !number(4, &eye) || !number(5, &wx1) ||
        !number(6, &wy1) || !number(7, &target) || eye < 0.0 || target < 0.0) {
      log.push_back("SURFACEVISIBILITY — LOS coordinates and heights must be finite; heights cannot be negative.");
      return;
    }
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0, g0 = 0.0, g1 = 0.0;
    worldToLocal(wx0, wy0, &x0, &y0);
    worldToLocal(wx1, wy1, &x1, &y1);
    if (!query->elevationAt(x0, y0, &g0) || !query->elevationAt(x1, y1, &g1)) {
      log.push_back("SURFACEVISIBILITY — eye or target is outside the surface; no result was created.");
      return;
    }
    const double z0 = g0 + eye, z1 = g1 + target;
    double bx = std::numeric_limits<double>::quiet_NaN(), by = 0.0, bz = 0.0;
    bool covered = false;
    const bool visible = SurfaceSightVisible(*surface.tin, x0, y0, z0, x1, y1, z1, &bx, &by, &bz, &covered);
    if (!covered) {
      log.push_back("SURFACEVISIBILITY — the line crosses an area outside the surface; no result was created.");
      return;
    }
    PushUndoSnapshot(st, "Surface line of sight");
    if (visible) {
      AddSurfaceResultLine(st, x0, y0, z0, x1, y1, z1, "#25a55b");
      log.push_back("SURFACEVISIBILITY LOS — target is visible.");
    } else {
      AddSurfaceResultLine(st, x0, y0, z0, bx, by, bz, "#25a55b");
      AddSurfaceResultLine(st, bx, by, bz, x1, y1, z1, "#d64545");
      log.push_back("SURFACEVISIBILITY LOS — target is obstructed; first obstruction at " +
                    std::to_string(bx + st.drawing.worldDocumentOriginX) + ", " +
                    std::to_string(by + st.drawing.worldDocumentOriginY) + ".");
    }
    BumpCadGpuCache(st);
    return;
  }
  if (mode == "zvi") {
    if (f.size() != 7) {
      log.push_back("SURFACEVISIBILITY — ZVI usage: SURFACEVISIBILITY <surface>, ZVI, observerX, observerY, eyeHeight, targetHeight, radius.");
      return;
    }
    double wx = 0.0, wy = 0.0, eye = 0.0, target = 0.0, radius = 0.0, ground = 0.0;
    if (!number(2, &wx) || !number(3, &wy) || !number(4, &eye) || !number(5, &target) ||
        !number(6, &radius) || eye < 0.0 || target < 0.0 || radius <= 0.0 || radius > 10000.0) {
      log.push_back("SURFACEVISIBILITY — ZVI requires finite coordinates, nonnegative heights, and a radius from 0 to 10000.");
      return;
    }
    double ox = 0.0, oy = 0.0;
    worldToLocal(wx, wy, &ox, &oy);
    if (!query->elevationAt(ox, oy, &ground)) {
      log.push_back("SURFACEVISIBILITY — observer is outside the surface; no result was created.");
      return;
    }
    constexpr int side = 32;
    const double step = 2.0 * radius / side;
    std::vector<std::pair<CadFilledRegion, std::string>> cells;
    cells.reserve(side * side);
    size_t visibleCells = 0, partialCells = 0, hiddenCells = 0;
    std::vector<std::pair<double,double>> footprint;
    constexpr int circleSegments = 128;
    constexpr double circlePi = 3.14159265358979323846;
    footprint.reserve(circleSegments);
    for (int i = 0; i < circleSegments; ++i) {
      const double angle = 2.0 * circlePi * static_cast<double>(i) / circleSegments;
      footprint.emplace_back(ox + radius * std::cos(angle), oy + radius * std::sin(angle));
    }
    const auto clipAxis = [](const std::vector<std::pair<double,double>>& input, int axis, double bound,
                             bool keepGreater) {
      std::vector<std::pair<double,double>> output;
      if (input.empty()) return output;
      const auto coordinate = [axis](const auto& p) { return axis == 0 ? p.first : p.second; };
      const auto inside = [&](const auto& p) {
        return keepGreater ? coordinate(p) >= bound - 1.e-10 : coordinate(p) <= bound + 1.e-10;
      };
      auto previous = input.back();
      bool previousInside = inside(previous);
      for (const auto& current : input) {
        const bool currentInside = inside(current);
        if (currentInside != previousInside) {
          const double denominator = coordinate(current) - coordinate(previous);
          if (std::abs(denominator) > 1.e-15) {
            const double t = (bound - coordinate(previous)) / denominator;
            output.emplace_back(previous.first + t * (current.first - previous.first),
                                previous.second + t * (current.second - previous.second));
          }
        }
        if (currentInside) output.push_back(current);
        previous = current;
        previousInside = currentInside;
      }
      return output;
    };
    for (int row = 0; row < side; ++row) for (int col = 0; col < side; ++col) {
      const double x0 = ox - radius + col * step, x1 = x0 + step;
      const double y0 = oy - radius + row * step, y1 = y0 + step;
      std::vector<std::pair<double,double>> polygon = clipAxis(footprint, 0, x0, true);
      polygon = clipAxis(polygon, 0, x1, false);
      polygon = clipAxis(polygon, 1, y0, true);
      polygon = clipAxis(polygon, 1, y1, false);
      if (polygon.size() < 3) continue;
      int visiblePoints = 0;
      bool completeSightCoverage = true;
      std::vector<double> vertexZ;
      vertexZ.reserve(polygon.size());
      for (const auto& p : polygon) {
        double targetGround = 0.0;
        if (!query->elevationAt(p.first, p.second, &targetGround)) {
          completeSightCoverage = false;
          break;
        }
        vertexZ.push_back(targetGround);
        bool sightCovered = false;
        if (SurfaceSightVisible(*surface.tin, ox, oy, ground + eye, p.first, p.second,
                                targetGround + target, nullptr, nullptr, nullptr, &sightCovered)) ++visiblePoints;
        completeSightCoverage = completeSightCoverage && sightCovered;
      }
      if (!completeSightCoverage || vertexZ.size() != polygon.size()) continue;
      const bool fullyVisible = visiblePoints == static_cast<int>(polygon.size());
      const bool fullyHidden = visiblePoints == 0;
      if (fullyVisible) ++visibleCells;
      else if (fullyHidden) ++hiddenCells;
      else ++partialCells;
      CadFilledRegion region;
      region.vertsXyz.reserve(polygon.size() * 3);
      for (size_t i = 0; i < polygon.size(); ++i) {
        region.vertsXyz.push_back(polygon[i].first);
        region.vertsXyz.push_back(polygon[i].second);
        region.vertsXyz.push_back(vertexZ[i]);
      }
      region.loopStart = {0};
      cells.emplace_back(std::move(region), fullyVisible ? "#35a86b" : fullyHidden ? "#d64545" : "#e4b844");
    }
    if (cells.empty()) {
      log.push_back("SURFACEVISIBILITY — the requested area has no surface coverage.");
      return;
    }
    PushUndoSnapshot(st, "Surface zone of visual influence");
    for (auto& [region, color] : cells) {
      st.drawing.cadFilledRegions.push_back(std::move(region));
      EntityAttributes attrs = MakeNewEntityAttrs(st);
      attrs.color = color;
      st.drawing.cadFilledRegionAttrs.push_back(std::move(attrs));
    }
    EnsureAttrCounts(st);
    BumpCadGpuCache(st);
    log.push_back("SURFACEVISIBILITY ZVI — " + std::to_string(visibleCells) + " visible cell(s), " +
                  std::to_string(partialCells) + " partly visible cell(s), " +
                  std::to_string(hiddenCells) + " hidden cell(s).");
    return;
  }
  log.push_back("SURFACEVISIBILITY — choose LOS or ZVI.");
}

void RunSurfaceLabel(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() < 2) {
    log.push_back("SURFLABEL — usage: SURFLABEL <surface>, SPOT, x, y | SLOPE, x1, y1, x2, y2 | CONTOUR, elevation, x, y | CONTOUR, elevation, GUIDE, x1, y1, ... | WATERSHED.");
    return;
  }
  const int si = FindSurfaceIndex(st, StringUtil::trimCopy(f[0]));
  if (si < 0) { log.push_back("SURFLABEL — no surface named \"" + f[0] + "\"."); return; }
  CadSurface& surface = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (!surface.tin || surface.tin->indices.empty()) { log.push_back("SURFLABEL — surface \"" + surface.name + "\" has no built TIN."); return; }
  const std::string kind = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(f[1]));
  std::vector<std::vector<double>> values;
  std::vector<std::pair<double,double>> anchors;
  auto parse = [&](size_t i, double* value) {
    if (i >= f.size()) return false;
    char* end = nullptr; *value = std::strtod(f[i].c_str(), &end);
    return end && *end == '\0' && std::isfinite(*value);
  };
  auto local = [&](double wx, double wy) { double x = 0.0, y = 0.0; CadCoord::LocalFromWorld(st, wx, wy, &x, &y); return std::pair<double,double>{x,y}; };
  auto query = [&](double x, double y, double* z) { return TinElevationAt(surface.tin->vertsXyz, surface.tin->indices, x, y, z); };
  if (kind == "spot" && f.size() == 4) {
    double x = 0.0, y = 0.0, z = 0.0;
    if (!parse(2, &x) || !parse(3, &y)) { log.push_back("SURFLABEL — spot coordinates must be finite numbers."); return; }
    const auto p = local(x, y);
    if (!query(p.first, p.second, &z)) { log.push_back("SURFLABEL — point is outside surface \"" + surface.name + "\"."); return; }
    values.push_back({p.first,p.second}); anchors.push_back(p);
  } else if (kind == "slope" && f.size() == 6) {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0, z0 = 0.0, z1 = 0.0;
    if (!parse(2,&x0) || !parse(3,&y0) || !parse(4,&x1) || !parse(5,&y1)) { log.push_back("SURFLABEL — slope coordinates must be finite numbers."); return; }
    const auto a = local(x0,y0), b = local(x1,y1);
    if (std::hypot(b.first-a.first,b.second-a.second) <= 1.e-9 || !query(a.first,a.second,&z0) || !query(b.first,b.second,&z1)) {
      log.push_back("SURFLABEL — slope endpoints must be distinct and both covered by surface \"" + surface.name + "\"."); return;
    }
    values.push_back({a.first,a.second,b.first,b.second}); anchors.push_back({(a.first+b.first)*0.5,(a.second+b.second)*0.5});
  } else if (kind == "contour" && f.size() == 5) {
    double level = 0.0, wx = 0.0, wy = 0.0, z = 0.0;
    if (!parse(2,&level) || !parse(3,&wx) || !parse(4,&wy)) { log.push_back("SURFLABEL — contour elevation and coordinates must be finite numbers."); return; }
    const auto p = local(wx,wy);
    if (!query(p.first,p.second,&z) || std::abs(z-level) > 0.01) { log.push_back("SURFLABEL — selected point is not on the requested contour of surface \"" + surface.name + "\"."); return; }
    values.push_back({level}); anchors.push_back(p);
  } else if (kind == "contour" && f.size() >= 8 &&
             StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(f[3])) == "guide" && (f.size() - 4) % 2 == 0) {
    double level = 0.0;
    if (!parse(2, &level) || f.size() < 8) {
      log.push_back("SURFLABEL — repeated contour labels need an elevation and at least two guide points."); return;
    }
    std::vector<std::pair<double,double>> guide;
    for (size_t i = 4; i < f.size(); i += 2) {
      double wx = 0.0, wy = 0.0;
      if (!parse(i, &wx) || !parse(i + 1, &wy)) {
        log.push_back("SURFLABEL — guide coordinates must be finite numbers."); return;
      }
      guide.push_back(local(wx, wy));
    }
    const auto& verts = surface.tin->vertsXyz;
    const auto& indices = surface.tin->indices;
    auto addUnique = [](std::vector<std::pair<double,double>>& points, double x, double y) {
      if (std::none_of(points.begin(), points.end(), [&](const auto& p) { return std::hypot(p.first-x,p.second-y) < 1.e-8; }))
        points.emplace_back(x, y);
    };
    std::vector<std::pair<std::pair<double,double>,std::pair<double,double>>> contourLines;
    for (size_t ti = 0; ti + 2 < indices.size(); ti += 3) {
      std::vector<std::pair<double,double>> points;
      for (int e = 0; e < 3; ++e) {
        const size_t ia = static_cast<size_t>(indices[ti + static_cast<size_t>(e)]) * 3;
        const size_t ib = static_cast<size_t>(indices[ti + static_cast<size_t>((e + 1) % 3)]) * 3;
        if (ia + 2 >= verts.size() || ib + 2 >= verts.size()) continue;
        const double za = verts[ia + 2], zb = verts[ib + 2];
        if (level < std::min(za,zb) || level > std::max(za,zb) || za == zb) continue;
        const double t = (level - za) / (zb - za);
        addUnique(points, verts[ia] + (verts[ib] - verts[ia]) * t, verts[ia + 1] + (verts[ib + 1] - verts[ia + 1]) * t);
      }
      if (points.size() == 2) contourLines.emplace_back(points[0], points[1]);
    }
    for (size_t gi = 1; gi < guide.size(); ++gi) {
      const double px = guide[gi-1].first, py = guide[gi-1].second;
      const double rx = guide[gi].first-px, ry = guide[gi].second-py;
      for (const auto& segment : contourLines) {
        const double qx = segment.first.first, qy = segment.first.second;
        const double sx = segment.second.first-qx, sy = segment.second.second-qy;
        const double den = rx*sy-ry*sx;
        if (std::abs(den) < 1.e-12) continue;
        const double t = ((qx-px)*sy-(qy-py)*sx)/den;
        const double u = ((qx-px)*ry-(qy-py)*rx)/den;
        if (t < -1.e-9 || t > 1.0+1.e-9 || u < -1.e-9 || u > 1.0+1.e-9) continue;
        const double x = px + t*rx, y = py + t*ry;
        if (std::none_of(anchors.begin(), anchors.end(), [&](const auto& a) { return std::hypot(a.first-x,a.second-y) < 0.01; })) {
          anchors.emplace_back(x,y);
          values.push_back({level});
        }
      }
    }
    if (anchors.empty()) { log.push_back("SURFLABEL — guide does not cross the requested contour on surface \"" + surface.name + "\"."); return; }
  } else if (kind == "watershed" && f.size() == 2) {
    const WatershedResult watersheds = ComputeWatershed(surface.tin->vertsXyz, surface.tin->indices);
    if (!watersheds.ok || watersheds.basins.empty()) { log.push_back("SURFLABEL — no watershed basins are available on surface \"" + surface.name + "\"."); return; }
    for (const WatershedBasin& basin : watersheds.basins) {
      values.push_back({basin.drainX,basin.drainY}); anchors.push_back({basin.drainX,basin.drainY});
    }
  } else {
    log.push_back("SURFLABEL — choose SPOT, SLOPE, CONTOUR, or WATERSHED with the documented arguments."); return;
  }
  EnsureEntityIds(st);
  if (static_cast<size_t>(si) >= st.drawing.cadSurfaceAttrs.size() || st.drawing.cadSurfaceAttrs[static_cast<size_t>(si)].id == 0) {
    log.push_back("SURFLABEL — surface has no stable drawing identifier."); return;
  }
  PushUndoSnapshot(st, "Surface labels");
  for (size_t i = 0; i < values.size(); ++i) {
    CadAnnotation label;
    label.insX = static_cast<float>(anchors[i].first); label.insY = static_cast<float>(anchors[i].second);
    label.surfaceLabelForId = st.drawing.cadSurfaceAttrs[static_cast<size_t>(si)].id;
    label.surfaceLabelKind = kind == "spot" ? 1 : kind == "slope" ? 2 : kind == "contour" ? 3 : 4;
    label.surfaceLabelValues = values[i];
    label.plottedHeightInches = st.drawing.defaultPlottedTextHeightInches;
    StampActiveTextStyleOnNewText(st, label);
    st.drawing.cadAnnotations.push_back(std::move(label));
    st.drawing.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
  }
  RefreshSurfaceLabelValues(st, surface);
  BumpCadGpuCache(st);
  log.push_back("SURFLABEL — " + std::to_string(values.size()) + " linked label(s) added to \"" + surface.name + "\".");
}

void RunSurfaceLegend(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() < 2) { log.push_back("SURFACELEGEND — usage: SURFACELEGEND <surface>, ELEVATION|SLOPE|SLOPEARROW|CONTOUR|WATERSHED, x, y."); return; }
  const int si = FindSurfaceIndex(st, StringUtil::trimCopy(f[0]));
  if (si < 0) { log.push_back("SURFACELEGEND — no surface named \"" + f[0] + "\"."); return; }
  CadSurface& surface = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (!surface.tin || surface.tin->indices.empty()) { log.push_back("SURFACELEGEND — surface \"" + surface.name + "\" has no built TIN."); return; }
  const std::string kind = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(f[1]));
  double wx = 0.0, wy = 0.0;
  auto number = [&](size_t i, double* out) { if (i >= f.size()) return false; char* end = nullptr; *out = std::strtod(f[i].c_str(), &end); return end && *end == '\0' && std::isfinite(*out); };
  if (f.size() != 4 || !number(2,&wx) || !number(3,&wy)) { log.push_back("SURFACELEGEND — insertion X and Y must be finite numbers."); return; }
  const SurfaceStyle* style = SurfaceStyles::Find(st.drawing.surfaceStyles, surface.styleName);
  CadTable table; table.cols = 3;
  CadCoord::LocalFromWorld(st, wx, wy, &table.insX, &table.insY);
  table.insZ = CadCommitElevation(st); table.surfaceLegendKind = kind;
  EnsureEntityIds(st);
  if (static_cast<size_t>(si) >= st.drawing.cadSurfaceAttrs.size() || st.drawing.cadSurfaceAttrs[static_cast<size_t>(si)].id == 0) { log.push_back("SURFACELEGEND — surface has no stable drawing identifier."); return; }
  table.surfaceLegendForId = st.drawing.cadSurfaceAttrs[static_cast<size_t>(si)].id;
  table.cells = {"Range", "Upper bound", "Color"};
  const int p = st.displayLinearPrecision;
  if ((kind == "elevation" && style && style->analysisMode == SurfaceAnalysisMode::Elevation) ||
      (kind == "slope" && style && style->analysisMode == SurfaceAnalysisMode::Slope) ||
      (kind == "slopearrow" && style)) {
    const bool arrows = kind == "slopearrow";
    const auto& bands = arrows ? style->arrowBands : style->bands;
    double lower = -std::numeric_limits<double>::infinity();
    for (const SurfaceBand& band : bands) {
      table.cells.push_back(std::isfinite(lower) ? "(" + FormatLinear(lower,p) + ", " + FormatLinear(band.upperBound,p) + "]" : "≤ " + FormatLinear(band.upperBound,p));
      table.cells.push_back(FormatLinear(band.upperBound,p)); table.cells.push_back(band.color); lower = band.upperBound;
    }
  } else if (kind == "contour" && style) {
    std::vector<double> levels = style->userContourFt;
    const SurfaceStats stats = ComputeSurfaceStats(surface.tin->vertsXyz, surface.tin->indices, static_cast<int>(surface.breaklines.size()), surface.isVolumeSurface());
    if (levels.empty() && stats.built && style->minorIntervalFt > 0.0) {
      const int first = static_cast<int>(std::ceil(stats.minZ / style->minorIntervalFt)), last = static_cast<int>(std::floor(stats.maxZ / style->minorIntervalFt));
      if (last - first < 1000) for (int n = first; n <= last; ++n) levels.push_back(n * style->minorIntervalFt);
    }
    for (double level : levels) { table.cells.push_back("Contour"); table.cells.push_back(FormatLinear(level,p)); table.cells.push_back("—"); }
  } else if (kind == "watershed") {
    const WatershedResult result = ComputeWatershed(surface.tin->vertsXyz, surface.tin->indices);
    for (const WatershedBasin& basin : result.basins) { table.cells.push_back("Basin " + std::to_string(basin.id)); table.cells.push_back(FormatLinear(basin.area2d,p) + " sq ft"); table.cells.push_back(basin.drain == DrainKind::Depression ? "depression" : basin.drain == DrainKind::Flat ? "flat" : "boundary"); }
  }
  if (table.cells.size() <= 3) { log.push_back("SURFACELEGEND — the requested analysis has no computed rows on surface \"" + surface.name + "\"."); return; }
  if (const TextStyle* ts = ActiveTextStyle(st)) table.fontFamily = ts->fontFamily;
  table.plottedHeightInches = st.drawing.defaultPlottedTextHeightInches;
  CadTableFitToContent(&table, st.drawing.modelUnitsPerPlottedInch);
  PushUndoSnapshot(st, "Surface analysis legend");
  st.drawing.cadTables.push_back(std::move(table));
  AppendNewObjectAttrs(st, st.drawing.cadTableAttrs, st.drawing.cadTables.size(), ObjectLayerKind::Table, {});
  EnsureAttrCounts(st); BumpCadGpuCache(st);
  log.push_back("SURFACELEGEND — linked " + kind + " table added for \"" + surface.name + "\".");
}

void RunSurfaceCrossings(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.empty() || f[0].empty()) {
    log.push_back("SURFCROSSBL — usage: SURFCROSSBL <surface>[, RESOLVE, x, y, HIGHER|LOWER|AVERAGE|TRIM-A|TRIM-B|elevation].");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFCROSSBL — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurface& surface = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (surface.dataShortcutReadOnly && f.size() > 1 && StringUtil::toLowerAsciiCopy(f[1]) != "list") {
    log.push_back("SURFCROSSBL — surface reference \"" + surface.name + "\" is read-only.");
    return;
  }
  if (surface.kind != SurfaceKind::Tin || surface.breaklines.size() < 2) {
    log.push_back("SURFCROSSBL — the selected surface has fewer than two breaklines to compare.");
    return;
  }
  CadSurface candidate = surface;
  std::vector<std::string> ignored;
  SurfaceBuildInputs inputs = ResolveSurfaceInputs(st, candidate, ignored);
  if (inputs.inputsIncomplete) {
    log.push_back("SURFCROSSBL — cannot resolve crossings because the surface definition is incomplete.");
    return;
  }
  const auto issues = TinFindCrossingConflicts(inputs.constraints);
  auto logIssue = [&](const TinCrossingIssue& issue) {
    if (issue.constraintIndexA >= inputs.constraintSourceIds.size() || issue.constraintIndexB >= inputs.constraintSourceIds.size())
      return;
    log.push_back("SURFCROSSBL — breakline IDs " + std::to_string(inputs.constraintSourceIds[issue.constraintIndexA]) +
                  " and " + std::to_string(inputs.constraintSourceIds[issue.constraintIndexB]) + " cross at (" +
                  std::to_string(issue.x) + ", " + std::to_string(issue.y) +
                  "); elevation difference " + std::to_string(issue.zFromA - issue.zFromB) + ".");
  };
  if (f.size() == 1 || (f.size() == 2 && StringUtil::toLowerAsciiCopy(f[1]) == "list")) {
    if (issues.empty()) log.push_back("SURFCROSSBL — no unresolved interior breakline crossings on \"" + surface.name + "\".");
    else for (const TinCrossingIssue& issue : issues) logIssue(issue);
    return;
  }
  if (f.size() != 5 || StringUtil::toLowerAsciiCopy(f[1]) != "resolve") {
    log.push_back("SURFCROSSBL — usage: SURFCROSSBL <surface>, RESOLVE, x, y, HIGHER|LOWER|AVERAGE|TRIM-A|TRIM-B|elevation.");
    return;
  }
  char *ex = nullptr, *ey = nullptr;
  const double wx = std::strtod(f[2].c_str(), &ex), wy = std::strtod(f[3].c_str(), &ey);
  if (!ex || *ex != '\0' || !ey || *ey != '\0' || !std::isfinite(wx) || !std::isfinite(wy)) {
    log.push_back("SURFCROSSBL — crossing coordinates must be finite numbers.");
    return;
  }
  double x = 0.0, y = 0.0;
  CadCoord::LocalFromWorld(st, wx, wy, &x, &y);
  const TinCrossingIssue* selected = nullptr;
  for (const TinCrossingIssue& issue : issues)
    if (std::hypot(issue.x - wx, issue.y - wy) <= 0.05) {
      selected = &issue; break;
    }
  if (!selected || selected->constraintIndexA >= inputs.constraintSourceIds.size() ||
      selected->constraintIndexB >= inputs.constraintSourceIds.size()) {
    log.push_back("SURFCROSSBL — no unresolved crossing was found within 0.05 drawing units of that location.");
    return;
  }
  CadSurfaceCrossingCorrection correction;
  correction.entityA = inputs.constraintSourceIds[selected->constraintIndexA];
  correction.entityB = inputs.constraintSourceIds[selected->constraintIndexB];
  correction.x = x; correction.y = y;
  const std::string action = StringUtil::toLowerAsciiCopy(f[4]);
  if (action == "higher") correction.kind = CadSurfaceCrossingCorrection::Kind::Higher;
  else if (action == "lower") correction.kind = CadSurfaceCrossingCorrection::Kind::Lower;
  else if (action == "average") correction.kind = CadSurfaceCrossingCorrection::Kind::Average;
  else if (action == "trim-a") correction.kind = CadSurfaceCrossingCorrection::Kind::TrimA;
  else if (action == "trim-b") correction.kind = CadSurfaceCrossingCorrection::Kind::TrimB;
  else {
    char* ez = nullptr;
    correction.customElevation = std::strtod(f[4].c_str(), &ez);
    if (!ez || *ez != '\0' || !std::isfinite(correction.customElevation)) {
      log.push_back("SURFCROSSBL — choose HIGHER, LOWER, AVERAGE, TRIM-A, TRIM-B, or a numeric elevation.");
      return;
    }
    correction.kind = CadSurfaceCrossingCorrection::Kind::Custom;
  }
  PushUndoSnapshot(st, "Resolve surface breakline crossing");
  surface.crossingCorrections.erase(std::remove_if(surface.crossingCorrections.begin(), surface.crossingCorrections.end(),
      [&](const CadSurfaceCrossingCorrection& existing) {
        return existing.entityA == correction.entityA && existing.entityB == correction.entityB &&
               std::hypot(existing.x - correction.x, existing.y - correction.y) <= 0.05;
      }), surface.crossingCorrections.end());
  surface.crossingCorrections.push_back(correction);
  BumpCadGpuCache(st);
  BuildSurfaceFromSources(st, surface, log);
  log.push_back("SURFCROSSBL — surface-local crossing decision saved; source breaklines were not changed.");
}


void StartSurfaceElevGradeCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("SURFELEV — finish or cancel the active command first.");
    return;
  }
  // Said before the first pick rather than after it: a user who has not built a surface yet should
  // not have to click to find out there is nothing to read (REQ-201).
  if (st.drawing.cadSurfaces.empty()) {
    log.push_back("SURFELEV — there are no surfaces in the drawing. Build one from a point group first.");
    return;
  }
  ClearPendingViewportZoom(st);
  st.selectedSurveyPointIndices.clear();
  auto sess = std::make_unique<stepengine::SurfaceElevGradeSession>();
  sess->phase = stepengine::SurfaceElevPhase::WaitFirst;
  stepengine::BeginSession(st, std::move(sess), false);
  log.push_back("SURFELEV — pick a point for its surface elevation; pick a second for grade. ESC cancels.");
}

void StartWaterDropCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("WATERDROP — finish or cancel the active command first.");
    return;
  }
  if (FindSurfaceIndex(st, surfaceName) < 0) {
    log.push_back("WATERDROP — no surface named \"" + surfaceName + "\".");
    return;
  }
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::WaterDropSession>();
  sess->surfaceName = surfaceName;
  stepengine::BeginSession(st, std::move(sess), false);
  log.push_back("WATERDROP — pick a plan position on \"" + surfaceName + "\". ESC cancels.");
}

void StartCatchmentCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("CATCHMENT — finish or cancel the active command first.");
    return;
  }
  if (FindSurfaceIndex(st, surfaceName) < 0) {
    log.push_back("CATCHMENT — no surface named \"" + surfaceName + "\".");
    return;
  }
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::CatchmentSession>();
  sess->surfaceName = surfaceName;
  stepengine::BeginSession(st, std::move(sess), false);
  log.push_back("CATCHMENT — pick an outlet on \"" + surfaceName + "\". ESC cancels.");
}

void StartSurfSwapEdgeCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("SURFSWAPEDGE — finish or cancel the active command first.");
    return;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFSWAPEDGE — no surface named \"" + surfaceName + "\".");
    return;
  }
  const CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (!s.tin || s.tin->indices.size() < 6) {
    log.push_back("SURFSWAPEDGE — \"" + s.name + "\" is not a built TIN.");
    return;
  }
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::SwapTinEdgeSession>();
  sess->surfaceName = surfaceName;
  stepengine::BeginSession(st, std::move(sess), true);
  log.push_back("SURFSWAPEDGE — pick an interior edge on \"" + surfaceName + "\". ESC cancels.");
}

void StartSurfAddPointCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("SURFACEADDPOINT — finish or cancel the active command first.");
    return;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFACEADDPOINT — no surface named \"" + surfaceName + "\".");
    return;
  }
  const CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (SurfaceRefusesPointEdits(s, "SURFACEADDPOINT", log))
    return;
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::AddTinPointSession>();
  sess->surfaceName = surfaceName;
  stepengine::BeginSession(st, std::move(sess), true);
  log.push_back("SURFACEADDPOINT — pick a plan position on \"" + surfaceName +
                "\" (elevation = current work plane). ESC cancels.");
}

void StartSurfDelPointCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("SURFACEDELPOINT — finish or cancel the active command first.");
    return;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFACEDELPOINT — no surface named \"" + surfaceName + "\".");
    return;
  }
  const CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (SurfaceRefusesPointEdits(s, "SURFACEDELPOINT", log))
    return;
  const bool hasInputs = !s.sourcePointGroups.empty() || !s.sourcePointFiles.empty() || !s.addedPointXyz.empty();
  if (!hasInputs) {
    log.push_back("SURFACEDELPOINT — \"" + s.name + "\" has no definition points to delete.");
    return;
  }
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::DelTinPointSession>();
  sess->surfaceName = surfaceName;
  stepengine::BeginSession(st, std::move(sess), true);
  log.push_back("SURFACEDELPOINT — pick the point to remove from \"" + surfaceName + "\". ESC cancels.");
}

void StartSurfMovePointCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("SURFACEMOVEPOINT — finish or cancel the active command first.");
    return;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFACEMOVEPOINT — no surface named \"" + surfaceName + "\".");
    return;
  }
  if (SurfaceRefusesPointEdits(st.drawing.cadSurfaces[static_cast<size_t>(si)], "SURFACEMOVEPOINT", log))
    return;
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::MoveTinPointSession>();
  sess->surfaceName = surfaceName;
  sess->phase = stepengine::MoveTinPointPhase::WaitFrom;
  stepengine::BeginSession(st, std::move(sess), true);
  log.push_back("SURFACEMOVEPOINT — pick the point to move on \"" + surfaceName + "\". ESC cancels.");
}

void StartSurfDelLineCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("SURFDELLINE — finish or cancel the active command first.");
    return;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFDELLINE — no surface named \"" + surfaceName + "\".");
    return;
  }
  const CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (SurfaceRefusesPointEdits(s, "SURFDELLINE", log))
    return;
  if (!s.tin || s.tin->indices.size() < 6) {
    log.push_back("SURFDELLINE — \"" + s.name + "\" is not a built TIN.");
    return;
  }
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::DelTinLineSession>();
  sess->surfaceName = surfaceName;
  stepengine::BeginSession(st, std::move(sess), true);
  log.push_back("SURFDELLINE — pick an interior edge on \"" + surfaceName + "\". ESC cancels.");
}

void StartQuickProfileCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("QUICKPROFILE — finish or cancel the active command first.");
    return;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("QUICKPROFILE — no surface named \"" + surfaceName + "\".");
    return;
  }
  const CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (!MakeSurfaceQuery(st, s)) {
    log.push_back("QUICKPROFILE — \"" + s.name + "\" is not a built surface.");
    return;
  }
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::QuickProfileSession>();
  sess->surfaceName = surfaceName;
  sess->phase = stepengine::QuickProfilePhase::WaitFirst;
  stepengine::BeginSession(st, std::move(sess), true);
  log.push_back("QUICKPROFILE — first point of the sample line on \"" + surfaceName + "\". ESC cancels.");
}

namespace {
/// The stable id (REQ-076) of a picked Line or Polyline, or 0 for anything else / an out-of-range
/// index — 0 is never a real id (\ref EntityAttributes::id), so it doubles as "not applicable" here.
std::uint64_t EntityIdOfLineOrPolylinePick(const AppCommandState& st, const SelectedEntity& hit) {
  const auto idOf = [](const std::vector<EntityAttributes>& v, int i) -> std::uint64_t {
    return (i >= 0 && static_cast<size_t>(i) < v.size()) ? v[static_cast<size_t>(i)].id : 0;
  };
  switch (hit.type) {
  case SelectedEntity::Type::LineSeg:     return idOf(st.drawing.userLineAttrs, hit.index);
  case SelectedEntity::Type::Polyline:    return idOf(st.drawing.userPolylineAttrs, hit.index);
  case SelectedEntity::Type::FeatureLine: return idOf(st.drawing.featureLineAttrs, hit.index);  // REQ-087
  case SelectedEntity::Type::Arc:         return idOf(st.drawing.userArcAttrs, hit.index);
  default:                                return 0;
  }
}
} // namespace

void StartDesignateBreaklineCommand(AppCommandState& st, const std::string& surfaceName,
                                    std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("DESIGNATEBREAKLINE — finish or cancel the active command first.");
    return;
  }
  const int surfaceIndex = FindSurfaceIndex(st, surfaceName);
  if (surfaceIndex < 0) {
    log.push_back("DESIGNATEBREAKLINE — no surface named \"" + surfaceName + "\".");
    return;
  }
  if (st.drawing.cadSurfaces[static_cast<size_t>(surfaceIndex)].dataShortcutReadOnly) {
    log.push_back("DESIGNATEBREAKLINE — surface reference \"" + surfaceName + "\" is read-only."); return;
  }
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::DesignateBreaklineSession>();
  sess->surfaceName = surfaceName;
  sess->contourSource = false;
  stepengine::BeginSession(st, std::move(sess), false);
  log.push_back("DESIGNATEBREAKLINE — pick a line or polyline to use as a breakline on \"" + surfaceName +
               "\". ESC cancels.");
}

void StartDesignateContourCommand(AppCommandState& st, const std::string& surfaceName,
                                  std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("DESIGNATECONTOUR — finish or cancel the active command first.");
    return;
  }
  const int surfaceIndex = FindSurfaceIndex(st, surfaceName);
  if (surfaceIndex < 0) {
    log.push_back("DESIGNATECONTOUR — no surface named \"" + surfaceName + "\".");
    return;
  }
  if (st.drawing.cadSurfaces[static_cast<size_t>(surfaceIndex)].dataShortcutReadOnly) {
    log.push_back("DESIGNATECONTOUR — surface reference \"" + surfaceName + "\" is read-only."); return;
  }
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::DesignateBreaklineSession>();
  sess->surfaceName = surfaceName;
  sess->contourSource = true;
  stepengine::BeginSession(st, std::move(sess), false);
  log.push_back("DESIGNATECONTOUR — pick a line or polyline to use as a contour on \"" + surfaceName +
               "\". ESC cancels.");
}

void StartDesignateBoundaryCommand(AppCommandState& st, const std::string& surfaceName, CadBoundaryKind kind,
                                   std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("DESIGNATEBOUNDARY — finish or cancel the active command first.");
    return;
  }
  const int surfaceIndex = FindSurfaceIndex(st, surfaceName);
  if (surfaceIndex < 0) {
    log.push_back("DESIGNATEBOUNDARY — no surface named \"" + surfaceName + "\".");
    return;
  }
  if (st.drawing.cadSurfaces[static_cast<size_t>(surfaceIndex)].dataShortcutReadOnly) {
    log.push_back("DESIGNATEBOUNDARY — surface reference \"" + surfaceName + "\" is read-only."); return;
  }
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::DesignateBoundarySession>();
  sess->surfaceName = surfaceName;
  sess->boundaryKind = kind;
  stepengine::BeginSession(st, std::move(sess), false);
  const char* kindName = kind == CadBoundaryKind::Outer ? "outer"
                       : kind == CadBoundaryKind::Hide  ? "hide"
                       : kind == CadBoundaryKind::Show  ? "show"
                       : kind == CadBoundaryKind::Mask  ? "mask"
                                                        : "clip";
  log.push_back(std::string("DESIGNATEBOUNDARY — pick a CLOSED polyline to use as a ") + kindName +
               " boundary on \"" + surfaceName + "\". ESC cancels.");
}

/// Shared commit for both DESIGNATEBREAKLINE and DESIGNATEBOUNDARY: pick, validate, append to the
/// target surface's definition, and trigger the dynamic rebuild (REQ-069) — a plain BumpCadGpuCache
/// is enough, since TickSurfaceRebuilds' dirty check is exactly "cadGpuRevision moved."
void CommitDesignateAt(AppCommandState& st, float wx, float wy, bool isBoundary, std::vector<std::string>& log) {
  const std::string cmdName = isBoundary ? "DESIGNATEBOUNDARY" : "DESIGNATEBREAKLINE";
  const auto* bndSess = isBoundary ? st.sessionAs<stepengine::DesignateBoundarySession>() : nullptr;
  const auto* blSess = !isBoundary ? st.sessionAs<stepengine::DesignateBreaklineSession>() : nullptr;
  const std::string targetSurfaceName = bndSess ? bndSess->surfaceName : (blSess ? blSess->surfaceName : std::string());
  const CadBoundaryKind boundaryKind = bndSess ? bndSess->boundaryKind : CadBoundaryKind::Outer;
  const bool contourSource = blSess && blSess->contourSource;
  const int si = FindSurfaceIndex(st, targetSurfaceName);
  if (si < 0) {
    log.push_back(cmdName + " — surface \"" + targetSurfaceName + "\" no longer exists.");
    stepengine::EndSession(st);
    return;
  }
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
    log.push_back(cmdName + " — nothing under cursor; try again, or ESC to cancel.");
    return;  // stays active — try again, matching OFFSET's WaitSelectEntity miss behaviour
  }
  // REQ-087: a feature line is design linework whose whole purpose is to be a breakline, so it is
  // accepted here alongside lines and polylines.
  if (hit.type != SelectedEntity::Type::LineSeg && hit.type != SelectedEntity::Type::Polyline &&
      hit.type != SelectedEntity::Type::FeatureLine && hit.type != SelectedEntity::Type::Arc) {
    log.push_back(cmdName + " — that is not a line, polyline, feature line, or arc; try again, or ESC to cancel.");
    return;
  }
  if (isBoundary && hit.type == SelectedEntity::Type::Arc) {
    log.push_back(cmdName + " — a boundary cannot be an arc; try again, or ESC to cancel.");
    return;
  }
  if (isBoundary && hit.type == SelectedEntity::Type::LineSeg) {
    log.push_back(cmdName + " — a boundary must be a closed polyline or feature line, not a line; "
                            "try again, or ESC to cancel.");
    return;
  }
  if (isBoundary) {
    const size_t ix = static_cast<size_t>(hit.index);
    const bool closed = hit.type == SelectedEntity::Type::FeatureLine
                            ? (ix < st.drawing.featureLineClosed.size() && st.drawing.featureLineClosed[ix] != 0)
                            : (ix < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[ix] != 0);
    if (!closed) {
      log.push_back(cmdName + " — that " +
                    (hit.type == SelectedEntity::Type::FeatureLine ? "feature line" : "polyline") +
                    " is not closed; try again, or ESC to cancel.");
      return;
    }
  }
  const std::uint64_t id = EntityIdOfLineOrPolylinePick(st, hit);
  if (id == 0) {
    log.push_back(cmdName + " — could not identify that entity; try again, or ESC to cancel.");
    return;
  }

  CadSurface& surface = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (isBoundary) {
    CadSurfaceBoundary b;
    b.entityId = id;
    b.kind = boundaryKind;
    b.name = st.designateBoundaryName;  // REQ-075; empty when the command was typed, not dialogued
    surface.boundaries.push_back(b);
    const char* kindName = b.kind == CadBoundaryKind::Outer ? "outer"
                          : b.kind == CadBoundaryKind::Hide  ? "hide"
                          : b.kind == CadBoundaryKind::Show  ? "show"
                                                             : "clip";
    log.push_back("DESIGNATEBOUNDARY — added a " + std::string(kindName) + " boundary" +
                  (b.name.empty() ? "" : " \"" + b.name + "\"") + " to \"" + surface.name + "\".");
  } else if (contourSource) {
    CadSurfaceBreakline bl;
    bl.entityId = id;
    bl.description = st.designateBreaklineDescription;
    surface.contourSources.push_back(bl);
    log.push_back("DESIGNATECONTOUR — added a contour source" +
                  (bl.description.empty() ? "" : " \"" + bl.description + "\"") + " to \"" + surface.name + "\".");
  } else {
    CadSurfaceBreakline bl;
    bl.entityId = id;
    bl.description = st.designateBreaklineDescription;
    surface.breaklines.push_back(bl);
    log.push_back("DESIGNATEBREAKLINE — added a breakline" +
                  (bl.description.empty() ? "" : " \"" + bl.description + "\"") + " to \"" + surface.name + "\".");
  }
  // Consumed: a later typed DESIGNATE* must not inherit the last dialog's text.
  st.designateBreaklineDescription.clear();
  st.designateBoundaryName.clear();
  BumpCadGpuCache(st);  // marks every surface's dirty check; TickSurfaceRebuilds picks this one up next frame
  stepengine::EndSession(st);
}
