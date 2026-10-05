#pragma once

#include <string>
#include <vector>

struct EntityAttributes;
struct _dwg_struct;

/// Reset per-import counters (call once at the start of DWG/DXF import).
void DwgMaterialImportBegin();

/// Resolve \p ent's MATERIAL handle (R2007+) and set diffuse override on \p at when present.
void DwgImportApplyEntityMaterial(_dwg_struct* dwg, const void* entity, EntityAttributes* at);

/// Append REQ-201 material import summary lines to \p log.
void DwgMaterialImportAppendLog(std::vector<std::string>& log);

/// Test helper: hand-built MATERIAL with override diffuse (LibreDWG has no dwg_add_MATERIAL).
/// Returns a handle suitable for `Dwg_Object_Entity::material`, or null on failure.
const void* DwgTestAddDiffuseMaterial(_dwg_struct* dwg, const char* name, unsigned rgb24, double factor);
