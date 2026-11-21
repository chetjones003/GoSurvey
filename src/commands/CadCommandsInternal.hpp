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
