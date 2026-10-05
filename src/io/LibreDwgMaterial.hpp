#pragma once

#include <string>
#include <vector>

struct AppCommandState;
struct EntityAttributes;
struct _dwg_struct;

#include <cstdint>
#include <unordered_map>

/// Caches MATERIAL objects during R2007+ DWG export (REQ-372).
struct DwgExportMaterialContext {
  bool enabled = false;
  std::unordered_map<std::uint32_t, std::uint64_t> diffuseRgbToHandle;
  std::unordered_map<std::string, std::uint64_t> materialNameToHandle;
  int materialsWritten = 0;
};

void DwgExportMaterialContextInit(DwgExportMaterialContext* ctx, bool r2007OrNewer);

/// Attach a MATERIAL handle for \p ent's effective shaded diffuse (entity/layer or import override).
void DwgExportApplyEntityMaterial(_dwg_struct* dwg, DwgExportMaterialContext* ctx, const AppCommandState& st,
                                  void* entity, const EntityAttributes* attr, float defaultR, float defaultG,
                                  float defaultB);

void DwgExportMaterialAppendLog(const DwgExportMaterialContext& ctx, std::vector<std::string>& log);

/// Reset per-import counters (call once at the start of DWG/DXF import).
void DwgMaterialImportBegin();

/// Resolve \p ent's MATERIAL handle (R2007+) and set diffuse override on \p at when present.
void DwgImportApplyEntityMaterial(_dwg_struct* dwg, const void* entity, EntityAttributes* at);

/// Append REQ-201 material import summary lines to \p log.
void DwgMaterialImportAppendLog(std::vector<std::string>& log);

/// Test helper: hand-built MATERIAL with override diffuse (LibreDWG has no dwg_add_MATERIAL).
/// Returns a handle suitable for `Dwg_Object_Entity::material`, or null on failure.
const void* DwgTestAddDiffuseMaterial(_dwg_struct* dwg, const char* name, unsigned rgb24, double factor);
