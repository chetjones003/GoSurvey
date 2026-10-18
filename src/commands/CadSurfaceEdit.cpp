#include "CadSurfaceEdit.hpp"

#include "StringUtil.hpp"
#include "SurfaceStyle.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace surfedit {
namespace {

const char* kBandPalette[] = {"#3148F5", "#31A2F5", "#31F5C8", "#7CF531", "#F5E631", "#F5A031", "#F53131",
                              "#C831F5"};

bool ValidIndex(const AppCommandState& cmd, int si) {
  return si >= 0 && si < static_cast<int>(cmd.cadSurfaces.size());
}

void SubmitLine(AppCommandState& cmd, std::vector<std::string>& log, const std::string& line) {
  std::vector<char> buf(line.begin(), line.end());
  buf.push_back('\0');
  ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), cmd, log);
}

/// Why \p name cannot be surface \p si's name, or empty when it can. Logged by the callers.
std::string NameRefusal(const AppCommandState& cmd, int si, const std::string& name) {
  const CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(si)];
  if (name.empty())
    return "Surface name cannot be empty \xe2\x80\x94 keeping \"" + s.name + "\".";
  const int clash = FindSurfaceIndex(cmd, name);
  if (clash >= 0 && clash != si)
    return "A surface named \"" + name + "\" already exists \xe2\x80\x94 rename refused.";
  return {};
}

template <typename T>
bool MoveInVector(std::vector<T>& v, size_t i, int delta) {
  const long long j = static_cast<long long>(i) + delta;
  if (i >= v.size() || j < 0 || j >= static_cast<long long>(v.size()))
    return false;
  std::swap(v[i], v[static_cast<size_t>(j)]);
  return true;
}

bool MoveAddedPointTriple(std::vector<double>& v, size_t i, int delta) {
  if (v.size() % 3u != 0u)
    return false;
  const size_t n = v.size() / 3;
  const long long j = static_cast<long long>(i) + delta;
  if (i >= n || j < 0 || j >= static_cast<long long>(n))
    return false;
  for (size_t k = 0; k < 3; ++k)
    std::swap(v[i * 3 + k], v[static_cast<size_t>(j) * 3 + k]);
  return true;
}

/// The created surface carries the dialog's description, style and layer. It was created on its
/// Object Layers layer (REQ-361); a layer picked in the dialog (always one of the drawing's own)
/// replaces it. The unpicked default is left to the creator, which also falls back to the row's layer
/// when the name cannot be part of a layer name.
void ApplyCreatedFields(AppCommandState& cmd, const CreateRequest& req) {
  assert(!req.name.empty());
  const int ni = FindSurfaceIndex(cmd, req.name);
  assert(ni >= 0);
  if (ni < 0)
    return;
  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(ni)];
  s.description = req.description;
  s.styleName = req.styleName;
  const std::string chosen = StringUtil::trimCopy(req.layer);
  if (static_cast<size_t>(ni) < cmd.cadSurfaceAttrs.size() && !chosen.empty() &&
      chosen != ResolveObjectLayer(cmd.drawingSettings, ObjectLayerKind::Surface, req.name) &&
      !cmd.drawingSettings.ObjectLayer(ObjectLayerKind::Surface).locked)
    cmd.cadSurfaceAttrs[static_cast<size_t>(ni)].layer = EnsureDrawingLayer(cmd, chosen);
}

}  // namespace

std::string NextSurfaceName(const AppCommandState& cmd) {
  std::string base = "Surface";
  if (FindSurfaceIndex(cmd, base) < 0)
    return base;
  for (int n = 2; n < 10000; ++n) {
    const std::string candidate = base + " (" + std::to_string(n) + ")";
    if (FindSurfaceIndex(cmd, candidate) < 0)
      return candidate;
  }
  return base;
}

bool ElevationRange(const CadSurface& s, double* lo, double* hi) {
  if (!s.tin || s.tin->vertsXyz.size() < 3)
    return false;
  *lo = *hi = static_cast<double>(s.tin->vertsXyz[2]);
  for (size_t i = 2; i < s.tin->vertsXyz.size(); i += 3) {
    const double z = static_cast<double>(s.tin->vertsXyz[i]);
    *lo = std::min(*lo, z);
    *hi = std::max(*hi, z);
  }
  return true;
}

std::string CreateSurface(AppCommandState& cmd, const CreateRequest& req, std::vector<std::string>& log) {
  if (req.name.empty())
    return "A surface needs a name.";
  if (FindSurfaceIndex(cmd, req.name) >= 0)
    return "A surface named \"" + req.name + "\" already exists.";
  const auto lastLog = [&](const char* fallback) { return log.empty() ? std::string(fallback) : log.back(); };

  switch (req.kind) {
  case CreateKind::TinVolume:
  case CreateKind::GridVolume: {
    if (req.volumeBase.empty() || req.volumeComparison.empty())
      return "Pick a base surface and a comparison surface.";
    if (req.volumeBase == req.volumeComparison)
      return "Base and comparison must be different surfaces.";
    if (req.kind == CreateKind::TinVolume) {
      PushUndoSnapshot(cmd, "Create volume surface");
      if (CreateSurfaceFromVolumeParents(cmd, req.name, req.volumeBase, req.volumeComparison, log) < 0)
        return lastLog("Could not create the volume surface.");
    } else {
      SubmitLine(cmd, log, "SURFACECREATEVOLGRID " + req.name + ", " + req.volumeBase + ", " + req.volumeComparison);
      if (FindSurfaceIndex(cmd, req.name) < 0)
        return lastLog("Could not create the grid volume surface.");
    }
    break;
  }
  case CreateKind::Grid: {
    if (req.gridSpacingX <= 0.0 || req.gridSpacingY <= 0.0)
      return "Grid spacing must be positive.";
    SubmitLine(cmd, log,
               "SURFACECREATEGRID " + req.name + ", " + std::to_string(req.gridOriginX) + ", " +
                   std::to_string(req.gridOriginY) + ", " + std::to_string(req.gridSpacingX) + ", " +
                   std::to_string(req.gridSpacingY) + ", " + std::to_string(req.gridCols) + ", " +
                   std::to_string(req.gridRows));
    if (FindSurfaceIndex(cmd, req.name) < 0)
      return lastLog("Could not create the grid surface.");
    break;
  }
  case CreateKind::Corridor: {
    SubmitLine(cmd, log, "SURFACECREATECORR " + req.name);
    if (FindSurfaceIndex(cmd, req.name) < 0)
      return lastLog("Could not create the corridor surface.");
    break;
  }
  case CreateKind::Tin:
  default: {
    PushUndoSnapshot(cmd, "Create surface");
    if (CreateSurfaceFromPointGroups(cmd, req.name, {}, log) < 0)
      return lastLog("Could not create the surface.");
    break;
  }
  }
  ApplyCreatedFields(cmd, req);
  return {};
}

bool ApplySurfaceInfo(AppCommandState& cmd, int si, const std::string& name, const std::string& description,
                      const std::string& styleName, std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return false;
  const std::string why = NameRefusal(cmd, si, name);
  if (!why.empty()) {
    log.push_back(why);
    return false;
  }
  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(si)];
  PushUndoSnapshot(cmd, "Surface properties");
  s.name = name;
  s.description = description;
  s.styleName = styleName;
  BumpCadGpuCache(cmd);
  return true;
}

bool RenameSurface(AppCommandState& cmd, int si, const std::string& newName, std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return false;
  const std::string why = NameRefusal(cmd, si, newName);
  if (!why.empty()) {
    log.push_back(why);
    return false;
  }
  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(si)];
  PushUndoSnapshot(cmd, "Rename surface");
  log.push_back("Renamed surface \"" + s.name + "\" to \"" + newName + "\".");
  s.name = newName;
  BumpCadGpuCache(cmd);
  return true;
}

void AssignSurfaceStyle(AppCommandState& cmd, int si, const std::string& styleName, std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return;
  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(si)];
  PushUndoSnapshot(cmd, "Surface style");
  s.styleName = styleName;
  BumpCadGpuCache(cmd);
  log.push_back("Surface \"" + s.name + "\" now uses style \"" + styleName + "\".");
}

void RebuildSurface(AppCommandState& cmd, int si, std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return;
  PushUndoSnapshot(cmd, "Rebuild surface");
  BuildSurfaceFromSources(cmd, cmd.cadSurfaces[static_cast<size_t>(si)], log);
  BumpCadGpuCache(cmd);
}

void DeleteSurface(AppCommandState& cmd, int si, std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return;
  PushUndoSnapshot(cmd, "Delete surface");  // undoable in one step (REQ-068)
  log.push_back("Deleted surface \"" + cmd.cadSurfaces[static_cast<size_t>(si)].name + "\".");
  EraseSurfaceAtIndex(cmd, static_cast<size_t>(si));
}

int CreateFromPointGroups(AppCommandState& cmd, const std::string& name, const std::vector<std::string>& groups,
                          std::vector<std::string>& log) {
  PushUndoSnapshot(cmd, "Create surface");
  return CreateSurfaceFromPointGroups(cmd, name, groups, log);
}

int CreateVolumeFromParents(AppCommandState& cmd, const std::string& name, const std::string& baseName,
                            const std::string& comparisonName, std::vector<std::string>& log) {
  PushUndoSnapshot(cmd, "Create volume surface");
  return CreateSurfaceFromVolumeParents(cmd, name, baseName, comparisonName, log);
}

bool RemoveDefinitionItem(AppCommandState& cmd, int si, DefinitionKind kind, size_t index,
                          std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return false;
  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(si)];
  const char* what = "point group";
  switch (kind) {
  case DefinitionKind::PointGroup:
    if (index >= s.sourcePointGroups.size())
      return false;
    PushUndoSnapshot(cmd, "Remove point group from surface");
    s.sourcePointGroups.erase(s.sourcePointGroups.begin() + static_cast<std::ptrdiff_t>(index));
    break;
  case DefinitionKind::Breakline:
    if (index >= s.breaklines.size())
      return false;
    what = "breakline";
    PushUndoSnapshot(cmd, "Remove surface breakline");
    s.breaklines.erase(s.breaklines.begin() + static_cast<std::ptrdiff_t>(index));
    break;
  case DefinitionKind::Boundary:
    if (index >= s.boundaries.size())
      return false;
    what = "boundary";
    PushUndoSnapshot(cmd, "Remove surface boundary");
    s.boundaries.erase(s.boundaries.begin() + static_cast<std::ptrdiff_t>(index));
    break;
  case DefinitionKind::PointFile:
    if (index >= s.sourcePointFiles.size())
      return false;
    what = "point file link";
    PushUndoSnapshot(cmd, "Unlink surface point file");
    s.sourcePointFiles.erase(s.sourcePointFiles.begin() + static_cast<std::ptrdiff_t>(index));
    break;
  }
  log.push_back("Surface \"" + s.name + "\": removed " + what + " " + std::to_string(index + 1) +
                " from the definition.");
  BumpCadGpuCache(cmd);  // the dynamic rebuild picks this up next frame (REQ-069)
  return true;
}

bool MoveDefinitionItem(AppCommandState& cmd, int si, DefinitionKind kind, size_t index, int delta,
                        std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si) || delta == 0)
    return false;
  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(si)];
  const long long j = static_cast<long long>(index) + delta;
  if (kind == DefinitionKind::Breakline) {
    if (index >= s.breaklines.size() || j < 0 || j >= static_cast<long long>(s.breaklines.size()))
      return false;
    PushUndoSnapshot(cmd, "Reorder surface breaklines");
    MoveInVector(s.breaklines, index, delta);
  } else if (kind == DefinitionKind::Boundary) {
    if (index >= s.boundaries.size() || j < 0 || j >= static_cast<long long>(s.boundaries.size()))
      return false;
    PushUndoSnapshot(cmd, "Reorder surface boundaries");
    MoveInVector(s.boundaries, index, delta);
  } else {
    return false;
  }
  BumpCadGpuCache(cmd);
  log.push_back("Surface \"" + s.name + "\": reordered the definition.");
  return true;
}

bool AddPointGroup(AppCommandState& cmd, int si, const std::string& groupName, std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return false;
  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(si)];
  if (std::find(s.sourcePointGroups.begin(), s.sourcePointGroups.end(), groupName) != s.sourcePointGroups.end())
    return false;
  PushUndoSnapshot(cmd, "Add point group to surface");
  s.sourcePointGroups.push_back(groupName);
  BumpCadGpuCache(cmd);
  log.push_back("Surface \"" + s.name + "\": added point group \"" + groupName + "\".");
  return true;
}

void AddPointFile(AppCommandState& cmd, int si, const std::string& path, const std::string& layoutName,
                  bool firstRowIsHeader, std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return;
  // Straight through the command line, so the window and SURFACEADDFILE cannot disagree about what
  // linking means — including refusing a path that cannot be read, and the undo snapshot.
  std::string line = "SURFACEADDFILE " + cmd.cadSurfaces[static_cast<size_t>(si)].name + ", " + path + ", " + layoutName;
  if (firstRowIsHeader)
    line += ", HEADER";
  SubmitLine(cmd, log, line);
}

void ImportPointFile(AppCommandState& cmd, int si, size_t fileIndex, std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return;
  // REQ-086's break-the-link, routed through the command so the window and SURFACEIMPORTFILE share
  // one definition of what it does — including refusing to drop the link when nothing imported.
  SubmitLine(cmd, log,
             "SURFACEIMPORTFILE " + cmd.cadSurfaces[static_cast<size_t>(si)].name + ", " + std::to_string(fileIndex + 1));
}

std::vector<DefinitionRow> DefinitionRows(const CadSurface& s) {
  std::vector<DefinitionRow> rows;
  rows.reserve(s.sourcePointGroups.size() + s.sourcePointFiles.size() + s.breaklines.size() + s.contourSources.size() +
               s.boundaries.size() + s.swappedEdgePicks.size() + s.addedPointXyz.size() / 3 + s.deletedPointPicks.size());
  const auto numbered = [](const std::string& text, size_t i) { return text.empty() ? "#" + std::to_string(i + 1) : text; };
  for (size_t i = 0; i < s.sourcePointGroups.size(); ++i)
    rows.push_back({"Point Group", s.sourcePointGroups[i], 0, i});
  for (size_t i = 0; i < s.sourcePointFiles.size(); ++i)
    rows.push_back({"Point File", s.sourcePointFiles[i].path, 1, i});
  for (size_t i = 0; i < s.breaklines.size(); ++i)
    rows.push_back({"Breakline", numbered(s.breaklines[i].description, i), 2, i});
  for (size_t i = 0; i < s.contourSources.size(); ++i)
    rows.push_back({"Contour", numbered(s.contourSources[i].description, i), 3, i});
  for (size_t i = 0; i < s.boundaries.size(); ++i)
    rows.push_back({s.boundaries[i].kind == CadBoundaryKind::Mask ? "Mask" : "Boundary",
                    numbered(s.boundaries[i].name, i), 4, i});
  for (size_t i = 0; i < s.swappedEdgePicks.size(); ++i)
    rows.push_back({"Edit", "Edge swap #" + std::to_string(i + 1), 5, i});
  for (size_t i = 0; i < s.addedPointXyz.size() / 3; ++i)
    rows.push_back({"Edit", "Added point #" + std::to_string(i + 1), 6, i});
  for (size_t i = 0; i < s.deletedPointPicks.size(); ++i)
    rows.push_back({"Edit", "Deleted point #" + std::to_string(i + 1), 7, i});
  return rows;
}

bool CanMoveDefinitionRow(const std::vector<DefinitionRow>& rows, int sel, int delta) {
  if (rows.empty() || sel < 0 || sel >= static_cast<int>(rows.size()))
    return false;
  const int j = sel + delta;
  if (j < 0 || j >= static_cast<int>(rows.size()))
    return false;
  return rows[static_cast<size_t>(sel)].kind == rows[static_cast<size_t>(j)].kind;
}

bool MoveDefinitionRow(AppCommandState& cmd, int si, const std::vector<DefinitionRow>& rows, int sel, int delta) {
  if (!ValidIndex(cmd, si) || !CanMoveDefinitionRow(rows, sel, delta))
    return false;
  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(si)];
  const DefinitionRow& r = rows[static_cast<size_t>(sel)];
  PushUndoSnapshot(cmd, "Reorder surface definition");
  switch (r.kind) {
  case 0: MoveInVector(s.sourcePointGroups, r.index, delta); break;
  case 1: MoveInVector(s.sourcePointFiles, r.index, delta); break;
  case 2: MoveInVector(s.breaklines, r.index, delta); break;
  case 3: MoveInVector(s.contourSources, r.index, delta); break;
  case 4: MoveInVector(s.boundaries, r.index, delta); break;
  case 5: MoveInVector(s.swappedEdgePicks, r.index, delta); break;
  case 6: MoveAddedPointTriple(s.addedPointXyz, r.index, delta); break;
  default: MoveInVector(s.deletedPointPicks, r.index, delta); break;
  }
  BumpCadGpuCache(cmd);
  return true;
}

void SetAnalysisMode(AppCommandState& cmd, int si, SurfaceAnalysisMode mode, std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return;
  DetachSurfaceStyleIfShared(cmd, static_cast<size_t>(si), &log);
  SurfaceStyle* st = SurfaceStyles::Find(cmd.surfaceStyles, cmd.cadSurfaces[static_cast<size_t>(si)].styleName);
  if (st == nullptr)
    return;
  PushUndoSnapshot(cmd, "Surface analysis");
  st->analysisMode = mode;
  BumpCadGpuCache(cmd);
}

void RunElevationBands(AppCommandState& cmd, int si, int count, std::vector<std::string>& log) {
  if (!ValidIndex(cmd, si))
    return;
  double lo = 0.0, hi = 0.0;
  DetachSurfaceStyleIfShared(cmd, static_cast<size_t>(si), &log);
  SurfaceStyle* st = SurfaceStyles::Find(cmd.surfaceStyles, cmd.cadSurfaces[static_cast<size_t>(si)].styleName);
  if (st == nullptr)
    return;
  PushUndoSnapshot(cmd, "Surface analysis ranges");
  if (ElevationRange(cmd.cadSurfaces[static_cast<size_t>(si)], &lo, &hi) && count >= 1) {
    if (hi <= lo)
      hi = lo + 1.0;
    st->analysisMode = SurfaceAnalysisMode::Elevation;
    st->bands.clear();
    const int n = std::min(count, 64);
    for (int i = 1; i <= n; ++i) {
      SurfaceBand b;
      b.upperBound = lo + (hi - lo) * (static_cast<double>(i) / static_cast<double>(n));
      b.color = kBandPalette[static_cast<size_t>((i - 1) % 8)];
      st->bands.push_back(b);
    }
  }
  BumpCadGpuCache(cmd);
}

std::string TrimStyleName(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
  return s.substr(b, e - b);
}

bool RenameStyle(AppCommandState& cmd, size_t idx, const std::string& newName, std::vector<std::string>& log,
                 bool recordUndo) {
  if (idx >= cmd.surfaceStyles.size())
    return false;
  SurfaceStyle& s = cmd.surfaceStyles[idx];
  const std::string nm = TrimStyleName(newName);
  if (s.name == SurfaceStyles::kStandardName)
    return false;
  if (nm.empty()) {
    log.push_back("A surface style name cannot be empty \xe2\x80\x94 keeping \"" + s.name + "\".");
    return false;
  }
  if (SurfaceStyles::Find(cmd.surfaceStyles, nm) && nm != s.name) {
    log.push_back("A surface style named \"" + nm + "\" already exists \xe2\x80\x94 rename refused.");
    return false;
  }
  if (recordUndo)
    PushUndoSnapshot(cmd, "Rename surface style");
  // Every surface referencing the old name is re-pointed, so a rename does not silently orphan them
  // into the Standard fallback.
  for (CadSurface& surf : cmd.cadSurfaces)
    if (surf.styleName == s.name)
      surf.styleName = nm;
  log.push_back("Renamed surface style \"" + s.name + "\" to \"" + nm + "\".");
  s.name = nm;
  BumpCadGpuCache(cmd);
  return true;
}

bool AddStyle(AppCommandState& cmd, size_t copyFrom, const std::string& name, std::vector<std::string>& log,
              bool recordUndo) {
  const std::string nm = TrimStyleName(name);
  if (copyFrom >= cmd.surfaceStyles.size() || nm.empty() || SurfaceStyles::Find(cmd.surfaceStyles, nm) != nullptr)
    return false;
  if (recordUndo)
    PushUndoSnapshot(cmd, "Add surface style");
  SurfaceStyle ns = cmd.surfaceStyles[copyFrom];
  ns.name = nm;
  cmd.surfaceStyles.push_back(ns);
  log.push_back("Surface style added: " + nm);
  BumpCadGpuCache(cmd);
  return true;
}

bool DeleteStyle(AppCommandState& cmd, size_t idx, std::vector<std::string>& log, bool recordUndo) {
  if (idx >= cmd.surfaceStyles.size() || cmd.surfaceStyles[idx].name == SurfaceStyles::kStandardName)
    return false;
  if (recordUndo)
    PushUndoSnapshot(cmd, "Delete surface style");
  const std::string nm = cmd.surfaceStyles[idx].name;
  const int used = SurfaceStyles::CountSurfacesResolvingTo(cmd.cadSurfaces, cmd.surfaceStyles, nm);
  cmd.surfaceStyles.erase(cmd.surfaceStyles.begin() + static_cast<std::ptrdiff_t>(idx));
  // Surfaces keep their styleName on purpose (REQ-070): they fall back to Standard for now, and
  // re-creating a style with that name adopts them back rather than leaving the reference rewritten.
  log.push_back("Surface style deleted: " + nm +
                (used > 0 ? ". " + std::to_string(used) + " surface(s) using it now draw with \"Standard\"."
                          : std::string()));
  BumpCadGpuCache(cmd);
  return true;
}

void SetStyleIntervals(AppCommandState& cmd, size_t idx, double minorFt, double majorFt, bool recordUndo) {
  if (idx >= cmd.surfaceStyles.size())
    return;
  if (recordUndo)
    PushUndoSnapshot(cmd, "Surface style interval");
  cmd.surfaceStyles[idx].minorIntervalFt = minorFt;
  cmd.surfaceStyles[idx].majorIntervalFt = majorFt;
  BumpCadGpuCache(cmd);
}

}  // namespace surfedit
