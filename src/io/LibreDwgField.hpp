#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct AppCommandState;
struct CadFieldContext;
struct _dwg_struct;

/// Tracks entity ids → DWG handles and accumulates native FIELD objects during R2004+ export.
struct DwgExportFieldContext {
  bool enabled = false;
  std::unordered_map<std::uint64_t, std::uint64_t> entityIdToHandle;
  std::vector<std::uint64_t> fieldListIndexHandles;
  std::uint32_t nextFldIdx = 0;
  int fieldsWritten = 0;
};

void DwgExportFieldContextInit(DwgExportFieldContext* ctx, bool r2004OrNewer);

void DwgExportRegisterEntityHandle(DwgExportFieldContext* ctx, std::uint64_t entityId, const void* entity);

/// R2004+ with native fields: rewrites GoSurvey wires, creates FIELD/FIELDLIST objects, attaches reactors.
/// \p hostEntity is a LibreDWG `Dwg_Object_Entity*` (opaque here so IO headers need not include dwg.h).
[[nodiscard]] std::string DwgExportPrepareAnnotationFieldText(DwgExportFieldContext* ctx, _dwg_struct* dwg,
                                                              const AppCommandState& st, std::string_view wire,
                                                              const CadFieldContext& fctx, void* hostEntity,
                                                              std::uint64_t ownerBlockHandle);

void DwgExportFinalizeFieldObjects(DwgExportFieldContext* ctx, _dwg_struct* dwg,
                                   std::vector<std::string>& log);
