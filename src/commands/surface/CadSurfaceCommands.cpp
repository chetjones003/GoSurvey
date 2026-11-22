#include "CadCommands.hpp"
#include "ProjectSettings.hpp"  // REQ-375: NoteUserPlotScale
#include "ProjectFiles.hpp"  // REQ-379: RequestProjectAttach
#include "ProjectWarnings.hpp"  // REQ-383: TagClipboardOrigin / CheckClipboardPaste
#include "CadCommandsInternal.hpp"
#include "CadColor.hpp"
#include "CadBlocks.hpp"
#include "ToolspaceCatalog.hpp"
#include "OrthoConstrain.hpp"
#include "TextStyle.hpp"
#include "CadCoordinateFrame.hpp"
#include "CadLinetype.hpp"
#include "HatchGeom.hpp"
#include <GoSurvey/Geometry/HatchBoundary.hpp>
#include <GoSurvey/Geometry/FilletChamfer.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
#include <GoSurvey/Debug/BenchScene.hpp>
#include "GoSurvey/Document/cadpiperun.hpp"  // CadPipeRun solid generation (issue #486 increment B1 / REQ-345)
#include <GoSurvey/Geometry/meshgeom.hpp>
#include "GoSurvey/PointCloud/Octree.hpp"  // REQ-171/172 out-of-core octree (ADR-060)
#include "GoSurvey/IO/PointCloudCacheImport.hpp"   // ADR-060 .gscloud out-of-core cache (streams the E57 internally)
#include "GoSurvey/PointCloud/CylinderFit.hpp"       // REQ-347 EXTRACTCENTERLINE's least-squares cylinder-axis fit
#include "io/src/CadPointCloudE57.hpp"    // QuickPointCountEstimate, for the import progress bar's total
#include "io/src/CadPointCloudPts.hpp"    // REQ-172 PTS increment — reader/writer, same contract as E57
#include "io/src/CadPointCloudPtx.hpp"    // REQ-172 PTX increment — reader/writer, same contract as E57
#include "io/src/CadPointCloudLas.hpp"    // REQ-172 LAS increment — reader/writer, same contract as E57
#include "io/src/CadPointCloudLaz.hpp"    // REQ-172 LAZ increment — reader/writer, same contract as E57
#include <GoSurvey/Terrain/tinbuild.hpp>
#include <GoSurvey/Terrain/contourgen.hpp>  // REQ-070 contour generation (ADR-036 (f)) — pure, like tinbuild

void RunSurfSwapEdge(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() == 1 && !f[0].empty()) {
    StartSurfSwapEdgeCommand(st, f[0], log);
    return;
  }
  if (f.size() != 3 || f[0].empty()) {
    log.push_back("SURFSWAPEDGE — usage: SURFSWAPEDGE <surface>[, <x>, <y>].");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFSWAPEDGE — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  char* ex = nullptr;
  char* ey = nullptr;
  const double wx = std::strtod(f[1].c_str(), &ex);
  const double wy = std::strtod(f[2].c_str(), &ey);
  if (!ex || *ex != '\0' || !ey || *ey != '\0' || !std::isfinite(wx) || !std::isfinite(wy)) {
    log.push_back("SURFSWAPEDGE — x and y must be numbers.");
    return;
  }
  float lx = 0.f;
  float ly = 0.f;
  CadCoord::LocalFromWorld(st, wx, wy, &lx, &ly);
  CommitSurfSwapEdgeLocal(st, s, static_cast<double>(lx), static_cast<double>(ly), log);
}

void RunSurfMovePoint(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() == 1 && !f[0].empty()) {
    StartSurfMovePointCommand(st, f[0], log);
    return;
  }
  if (f.size() != 6 || f[0].empty()) {
    log.push_back("SURFACEMOVEPOINT — usage: SURFACEMOVEPOINT <surface>[, <x1>, <y1>, <x2>, <y2>, <z2>].");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACEMOVEPOINT — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  char* e[5] = {};
  const double w1x = std::strtod(f[1].c_str(), &e[0]);
  const double w1y = std::strtod(f[2].c_str(), &e[1]);
  const double w2x = std::strtod(f[3].c_str(), &e[2]);
  const double w2y = std::strtod(f[4].c_str(), &e[3]);
  const double w2z = std::strtod(f[5].c_str(), &e[4]);
  if (!e[0] || *e[0] != '\0' || !e[1] || *e[1] != '\0' || !e[2] || *e[2] != '\0' || !e[3] || *e[3] != '\0' ||
      !e[4] || *e[4] != '\0' || !std::isfinite(w1x) || !std::isfinite(w1y) || !std::isfinite(w2x) ||
      !std::isfinite(w2y) || !std::isfinite(w2z)) {
    log.push_back("SURFACEMOVEPOINT — coordinates must be numbers.");
    return;
  }
  float lx1 = 0.f, ly1 = 0.f, lx2 = 0.f, ly2 = 0.f;
  CadCoord::LocalFromWorld(st, w1x, w1y, &lx1, &ly1);
  CadCoord::LocalFromWorld(st, w2x, w2y, &lx2, &ly2);
  CommitSurfMovePointLocal(st, s, static_cast<double>(lx1), static_cast<double>(ly1), static_cast<double>(lx2),
                           static_cast<double>(ly2), static_cast<float>(w2z), log);
}

void RunSurfDelLine(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() == 1 && !f[0].empty()) {
    StartSurfDelLineCommand(st, f[0], log);
    return;
  }
  if (f.size() != 3 || f[0].empty()) {
    log.push_back("SURFDELLINE — usage: SURFDELLINE <surface>[, <x>, <y>].");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFDELLINE — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  char* ex = nullptr;
  char* ey = nullptr;
  const double wx = std::strtod(f[1].c_str(), &ex);
  const double wy = std::strtod(f[2].c_str(), &ey);
  if (!ex || *ex != '\0' || !ey || *ey != '\0' || !std::isfinite(wx) || !std::isfinite(wy)) {
    log.push_back("SURFDELLINE — x and y must be numbers.");
    return;
  }
  float lx = 0.f;
  float ly = 0.f;
  CadCoord::LocalFromWorld(st, wx, wy, &lx, &ly);
  CommitSurfDelLineLocal(st, s, static_cast<double>(lx), static_cast<double>(ly), log);
}

void RunSurfAddPoint(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() == 1 && !f[0].empty()) {
    StartSurfAddPointCommand(st, f[0], log);
    return;
  }
  if (f.size() != 4 || f[0].empty()) {
    log.push_back("SURFACEADDPOINT — usage: SURFACEADDPOINT <surface>[, <x>, <y>, <z>].");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACEADDPOINT — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  char* ex = nullptr;
  char* ey = nullptr;
  char* ez = nullptr;
  const double wx = std::strtod(f[1].c_str(), &ex);
  const double wy = std::strtod(f[2].c_str(), &ey);
  const double wz = std::strtod(f[3].c_str(), &ez);
  if (!ex || *ex != '\0' || !ey || *ey != '\0' || !ez || *ez != '\0' || !std::isfinite(wx) || !std::isfinite(wy) ||
      !std::isfinite(wz)) {
    log.push_back("SURFACEADDPOINT — x, y, and z must be numbers.");
    return;
  }
  double lx = 0.0;
  double ly = 0.0;
  CadCoord::LocalFromWorld(st, wx, wy, &lx, &ly);
  CommitSurfAddPointLocal(st, s, lx, ly, wz, log);
}

void RunSurfDelPoint(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() == 1 && !f[0].empty()) {
    StartSurfDelPointCommand(st, f[0], log);
    return;
  }
  if (f.size() != 3 || f[0].empty()) {
    log.push_back("SURFACEDELPOINT — usage: SURFACEDELPOINT <surface>[, <x>, <y>].");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACEDELPOINT — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  char* ex = nullptr;
  char* ey = nullptr;
  const double wx = std::strtod(f[1].c_str(), &ex);
  const double wy = std::strtod(f[2].c_str(), &ey);
  if (!ex || *ex != '\0' || !ey || *ey != '\0' || !std::isfinite(wx) || !std::isfinite(wy)) {
    log.push_back("SURFACEDELPOINT — x and y must be numbers.");
    return;
  }
  float lx = 0.f;
  float ly = 0.f;
  CadCoord::LocalFromWorld(st, wx, wy, &lx, &ly);
  CommitSurfDelPointLocal(st, s, static_cast<double>(lx), static_cast<double>(ly), log);
}
