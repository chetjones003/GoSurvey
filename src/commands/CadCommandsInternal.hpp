// CadCommandsInternal.hpp — command-layer helpers shared BETWEEN the translation
// units that CadCommands.cpp was split into (TASK-150 Phase 2, GitHub issue #142).
//
// These are NOT public product API. They are declared here — rather than being
// file-local statics of CadCommands.cpp — only so a cohesive command slice living
// in its own .cpp (CadCommands_Ucs.cpp, …) can call them. Everything here is
// defined once, in CadCommands.cpp.
//
// If a helper is needed by exactly one slice and nothing else, prefer moving it
// into that slice instead of adding it here.

#pragma once

#include <cassert>
#include <vector>

#include "CadEntities.hpp"  // EntityAttributes
#include "CadCommands.hpp"  // complete AppCommandState and the ParseStoragePointZ declaration
#include <GoSurvey/Core/StringUtil.hpp>

/// A duplicate is a NEW entity, so it takes the source's layer, colour and linetype but NOT its id.
/// Zero is the "unassigned" marker `MakeNewEntityAttrs` already uses; `EnsureEntityIds` sweeps on the
/// next `BumpCadGpuCache` and hands out a fresh one. Copying the id across instead — which both
/// duplicate paths did until 2026-08-20 — leaves two entities claiming one identity, and every
/// id-keyed lookup then resolves to whichever the sweep reaches first. A surface tracking a
/// breakline by id (REQ-069) would silently follow the wrong line. REQ-076; TASK-079 BUG-1.
static EntityAttributes DuplicatedEntityAttrs(EntityAttributes a) {
  a.id = 0;
  return a;
}

namespace {

/// Resolve the typed base point into STORAGE coordinates: local X/Y, absolute Z.
///
/// `X,Y` are read in the active UCS like every other coordinate in the program (REQ-154), and an
/// optional third component is the point's world ELEVATION — the same rule 3DPOLY and FEATURELINE
/// already use, peeled off before the shared 2D parser sees it so that REQ-101-critical parser is
/// not widened to three components for one feature.
///
/// \p relativeBase (storage coordinates) additionally accepts `@dx,dy[,dz]` measured from it, along
/// the active UCS's axes — PIPERUN's next point from its last vertex (REQ-354). Without one, `@` is
/// refused by name: there is nothing for it to be relative to.
///
/// \p zInUcs reads an absolute `x,y,z` wholly in the active UCS — the typed Z a UCS Z, like X and Y
/// — for a prompt whose dynamic input shows a UCS Z box (PIPERUN, REQ-354). Without it the Z stays
/// the world elevation the solid commands have always taken. The two agree under the WCS.
bool ParseSolidBasePoint(AppCommandState& st, const std::string& raw, ray3d::Vec3* out,
                         std::vector<std::string>& log, const char* verbUpper,
                         const ray3d::Vec3* relativeBase = nullptr, bool zInUcs = false) {
  const std::string trimmed = StringUtil::trimCopy(raw);
  if (zInUcs && !ucs::IsWorld(st.activeUcs) && (trimmed.empty() || trimmed[0] != '@')) {
    float lx = 0.f;
    float ly = 0.f;
    double worldZ = 0.0;
    if (!ParseStoragePointZ(st, trimmed, &lx, &ly, &worldZ, /*allowRelative=*/false, 0.f, 0.f)) {
      log.push_back(std::string(verbUpper) + " — could not read the point. Use X,Y or X,Y,Z.");
      return false;
    }
    *out = ray3d::Vec3{static_cast<double>(lx), static_cast<double>(ly), worldZ};
    return std::isfinite(out->x) && std::isfinite(out->y) && std::isfinite(out->z);
  }
  if (!trimmed.empty() && trimmed[0] == '@') {
    if (!relativeBase) {
      log.push_back(std::string(verbUpper) + " — a relative point (@) needs a previous point. Use X,Y or X,Y,Z.");
      return false;
    }
    const std::string body = StringUtil::trimCopy(trimmed.substr(1));
    const size_t c1 = body.find(',');
    const size_t c2 = (c1 == std::string::npos) ? std::string::npos : body.find(',', c1 + 1);
    if (c2 != std::string::npos && body.find(',', c2 + 1) != std::string::npos) {
      log.push_back(std::string(verbUpper) + " — too many coordinates: @dx,dy or @dx,dy,dz.");
      return false;
    }
    double d[3] = {0.0, 0.0, 0.0};
    const std::string parts[3] = {
        body.substr(0, c1),
        c1 == std::string::npos ? std::string() : body.substr(c1 + 1, c2 == std::string::npos ? std::string::npos
                                                                                              : c2 - c1 - 1),
        c2 == std::string::npos ? std::string("0") : body.substr(c2 + 1)};
    for (int i = 0; i < 3; ++i) {
      const std::string s = StringUtil::trimCopy(parts[i]);
      char* end = nullptr;
      d[i] = std::strtod(s.c_str(), &end);
      if (s.empty() || !end || *end != '\0' || !std::isfinite(d[i])) {
        log.push_back(std::string(verbUpper) + " — could not read the relative point. Use @dx,dy or @dx,dy,dz.");
        return false;
      }
    }
    // Along the UCS axes, as every other `@` in the program is (REQ-154); a vector, so the storage
    // frame's translation does not enter.
    const ray3d::Vec3 delta = ucs::UcsVectorToWorld(st.activeUcs, {d[0], d[1], d[2]});
    *out = ray3d::Add(*relativeBase, delta);
    return std::isfinite(out->x) && std::isfinite(out->y) && std::isfinite(out->z);
  }
  std::string xy = trimmed;
  bool haveZ = false;
  double typedZ = 0.0;

  const size_t c1 = trimmed.find(',');
  const size_t c2 = (c1 == std::string::npos) ? std::string::npos : trimmed.find(',', c1 + 1);
  if (c2 != std::string::npos) {
    // Strict about the field count, because the shared 2D parser is not: it reads two numbers and
    // ignores the rest, so `1,2,3,4` would otherwise land silently at (1,2).
    if (trimmed.find(',', c2 + 1) != std::string::npos) {
      log.push_back(std::string(verbUpper) + " — too many coordinates: X,Y or X,Y,Z.");
      return false;
    }
    const std::string zText = StringUtil::trimCopy(trimmed.substr(c2 + 1));
    char* zEnd = nullptr;
    const double zv = std::strtod(zText.c_str(), &zEnd);
    if (zText.empty() || !zEnd || *zEnd != '\0' || !std::isfinite(zv)) {
      log.push_back(std::string(verbUpper) + " — elevation must be a number: X,Y,Z.");
      return false;
    }
    typedZ = zv;
    haveZ = true;
    xy = trimmed.substr(0, c2);
  }

  float lx = 0.f;
  float ly = 0.f;
  double worldZ = 0.0;
  if (!ParseStoragePointZ(st, xy, &lx, &ly, &worldZ, /*allowRelative=*/false, 0.f, 0.f)) {
    log.push_back(std::string(verbUpper) + " — could not read the base point. Use X,Y or X,Y,Z.");
    return false;
  }
  out->x = static_cast<double>(lx);
  out->y = static_cast<double>(ly);
  out->z = haveZ ? typedZ : worldZ;
  return std::isfinite(out->x) && std::isfinite(out->y) && std::isfinite(out->z);
}

} // namespace

struct AppCommandState;

/// Fresh entity attributes for newly-drawn geometry: current layer, everything
/// else ByLayer / default.
EntityAttributes MakeNewEntityAttrs(const AppCommandState& st);

/// REQ-044: new TEXT / MTEXT adopts the active text style (also a Position Marker's label, REQ-359).
void StampActiveTextStyleOnNewText(AppCommandState& st, CadAnnotation& a);

// --- Draft-state resets (defined together as a cluster in CadCommands.cpp) ---
void ResetCircleDraft(AppCommandState& st);
void ResetPolylineDraft(AppCommandState& st);
void ResetFeatureLineDraft(AppCommandState& st);
void ResetArcDraft(AppCommandState& st);
void ResetEllipseDraft(AppCommandState& st);
void ResetRectDraft(AppCommandState& st);
void ResetTextCmdDraft(AppCommandState& st);
void ResetMtextDraft(AppCommandState& st);
void ResetDimDraft(AppCommandState& st);
void ResetDimAngularDraft(AppCommandState& st);
void ResetSurveyInverseDraft(AppCommandState& st);
void ResetDistDraft(AppCommandState& st);
/// Resets every in-progress draft/prompt tool. Call on command start, cancel, or
/// clear-geometry so no half-collected state leaks into the next command.
void ResetAllCadDraftTools(AppCommandState& st);

static void CommitSurfSwapEdgeLocal(AppCommandState& st, CadSurface& s, double x, double y,
                                    std::vector<std::string>& log) {
  if (s.dataShortcutReadOnly) {
    log.push_back("SURFSWAPEDGE — surface reference \"" + s.name + "\" is read-only.");
    return;
  }
  if (!s.tin || s.tin->indices.size() < 6) {
    log.push_back("SURFSWAPEDGE — \"" + s.name + "\" is not a built TIN.");
    return;
  }
  auto trial = std::make_shared<CadTin>(*s.tin);
  if (!TinSwapInteriorEdgeNear(trial->vertsXyz, trial->indices, x, y)) {
    log.push_back("SURFSWAPEDGE — pick is not on an interior edge.");
    return;
  }
  PushUndoSnapshot(st, "Swap TIN edge");
  s.swappedEdgePicks.emplace_back(x, y);
  s.tin = std::move(trial);
  BumpCadGpuCache(st);
  log.push_back("SURFSWAPEDGE — swapped an interior edge on \"" + s.name + "\".");
}

static bool SurfaceRefusesPointEdits(const CadSurface& s, const char* cmd, std::vector<std::string>& log) {
  assert(cmd != nullptr);
  assert(cmd[0] != '\0');
  if (s.dataShortcutReadOnly) {
    log.push_back(std::string(cmd) + " — surface reference \"" + s.name + "\" is read-only.");
    return true;
  }
  if (s.kind == SurfaceKind::Tin && !s.isVolumeSurface())
    return false;
  log.push_back(std::string(cmd) + " — \"" + s.name + "\" is not a TIN surface.");
  return true;
}

// A TIN imported from a Civil 3D DWG is a fixed triangulation with no definition (no point groups, files, added
// points, breaklines, contours, boundaries or corridor). Point edits would be rebuilt from the edits alone and
// replace the imported triangulation, so they are refused rather than silently losing it.
static bool SurfaceIsImportedFixedTin(const CadSurface& s) {
  return s.importedFixedTin && s.tin && !s.isVolumeSurface() && s.sourcePointGroups.empty() && s.sourcePointFiles.empty() &&
         s.addedPointXyz.empty() && s.breaklines.empty() && s.contourSources.empty() && s.boundaries.empty() &&
         s.movedPoints.empty() && s.corridorFeatureLines.empty();
}

static bool RefusesImportedTinPointEdit(const CadSurface& s, const char* command, std::vector<std::string>& log) {
  if (!SurfaceIsImportedFixedTin(s)) return false;
  log.push_back(std::string(command) + " — \"" + s.name + "\" is an imported fixed triangulation with no definition; "
                "point edits would rebuild it from the edits alone and discard the imported surface. Edit the surface in "
                "Civil 3D, or build a new surface from points.");
  return true;
}

static void CommitSurfAddPointLocal(AppCommandState& st, CadSurface& s, double x, double y, double z,
                                    std::vector<std::string>& log) {
  if (SurfaceRefusesPointEdits(s, "SURFACEADDPOINT", log))
    return;
  if (RefusesImportedTinPointEdit(s, "SURFACEADDPOINT", log))
    return;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
    log.push_back("SURFACEADDPOINT — x, y, and z must be finite numbers.");
    return;
  }
  PushUndoSnapshot(st, "Add surface point");
  s.addedPointXyz.push_back(x);
  s.addedPointXyz.push_back(y);
  s.addedPointXyz.push_back(z);
  BumpCadGpuCache(st);
  log.push_back("SURFACEADDPOINT — added a definition point on \"" + s.name + "\".");
}

static void CommitSurfDelPointLocal(AppCommandState& st, CadSurface& s, double x, double y,
                                    std::vector<std::string>& log) {
  if (SurfaceRefusesPointEdits(s, "SURFACEDELPOINT", log))
    return;
  if (RefusesImportedTinPointEdit(s, "SURFACEDELPOINT", log))
    return;
  if (!std::isfinite(x) || !std::isfinite(y)) {
    log.push_back("SURFACEDELPOINT — x and y must be finite numbers.");
    return;
  }
  const bool hasInputs = !s.sourcePointGroups.empty() || !s.sourcePointFiles.empty() || !s.addedPointXyz.empty();
  if (!hasInputs) {
    log.push_back("SURFACEDELPOINT — \"" + s.name + "\" has no definition points to delete.");
    return;
  }
  PushUndoSnapshot(st, "Delete surface point");
  s.deletedPointPicks.emplace_back(x, y);
  BumpCadGpuCache(st);
  log.push_back("SURFACEDELPOINT — recorded a point-delete edit on \"" + s.name + "\".");
}

static void CommitSurfMovePointLocal(AppCommandState& st, CadSurface& s, double x1, double y1, double x2, double y2,
                                     double z2, std::vector<std::string>& log) {
  if (SurfaceRefusesPointEdits(s, "SURFACEMOVEPOINT", log))
    return;
  if (RefusesImportedTinPointEdit(s, "SURFACEMOVEPOINT", log))
    return;
  if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) || !std::isfinite(y2) || !std::isfinite(z2)) {
    log.push_back("SURFACEMOVEPOINT — coordinates must be finite numbers.");
    return;
  }
  PushUndoSnapshot(st, "Move surface point");
  CadSurface::MovedPoint m;
  m.fromX = x1;
  m.fromY = y1;
  m.toX = x2;
  m.toY = y2;
  m.toZ = z2;
  s.movedPoints.push_back(m);
  BumpCadGpuCache(st);
  log.push_back("SURFACEMOVEPOINT — recorded a point-move edit on \"" + s.name + "\".");
}

static void CommitSurfDelLineLocal(AppCommandState& st, CadSurface& s, double x, double y,
                                   std::vector<std::string>& log) {
  if (SurfaceRefusesPointEdits(s, "SURFDELLINE", log))
    return;
  if (!s.tin || s.tin->indices.size() < 6) {
    log.push_back("SURFDELLINE — \"" + s.name + "\" is not a built TIN.");
    return;
  }
  auto probe = s.tin->indices;
  if (!TinDeleteInteriorEdgeNear(probe, s.tin->vertsXyz, x, y)) {
    log.push_back("SURFDELLINE — no interior edge within 1 ft of the pick.");
    return;
  }
  PushUndoSnapshot(st, "Delete TIN line");
  s.deletedEdgePicks.emplace_back(x, y);
  BumpCadGpuCache(st);
  log.push_back("SURFDELLINE — recorded an edge-delete edit on \"" + s.name + "\".");
}
void RunSurfSwapEdge(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfMovePoint(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfDelLine(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfAddPoint(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void RunSurfDelPoint(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
std::vector<std::string> SplitCommaFields(const std::string& s);
