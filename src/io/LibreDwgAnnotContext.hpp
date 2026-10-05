#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct AppCommandState;
struct CadAnnotation;
struct _dwg_entity_MTEXT;
struct _dwg_object_entity;
struct _dwg_struct;

/// Caches SCALE handles and counts during R2010+ DWG export (REQ-384 inc 2+).
struct DwgExportAnnotContext {
  bool enabled = false;
  _dwg_struct* dwg = nullptr;
  std::unordered_map<std::string, std::uint64_t> scaleNameToHandle;
  int mtextContextObjectsWritten = 0;
};

void DwgExportAnnotContextInit(DwgExportAnnotContext* ctx, _dwg_struct* dwg, bool r2010OrNewer);

void DwgExportAnnotContextRegisterScale(DwgExportAnnotContext* ctx, std::string_view scaleName,
                                        std::uint64_t absoluteRef);

/// Hand-build `MTEXTOBJECTCONTEXTDATA` + `CONTEXTDATAMANAGER` on annotative MTEXT (REQ-384 inc 2).
bool DwgExportAttachMtextAnnotationContext(DwgExportAnnotContext* ctx, _dwg_object_entity* ent,
                                           const _dwg_entity_MTEXT* mtext, const CadAnnotation& an,
                                           const AppCommandState& st);

void DwgExportAnnotContextAppendLog(const DwgExportAnnotContext& ctx, std::vector<std::string>& log);

/// Reset per-import counters (call once at the start of DWG import).
void DwgAnnotContextImportBegin();

/// Scan \p dwg for AutoCAD annotation context objects (issue #688 / REQ-384).
void DwgAnnotContextImportScan(const _dwg_struct* dwg);

/// Append REQ-201 import summary when context objects were present.
void DwgAnnotContextImportAppendLog(std::vector<std::string>& log);

/// Count decoded `*_OBJECTCONTEXTDATA` and `CONTEXTDATAMANAGER` objects (tests / diagnostics).
int DwgAnnotContextCountObjects(const _dwg_struct* dwg);

/// Test helper: append a minimal `MTEXTOBJECTCONTEXTDATA` object (LibreDWG has no public writer).
bool DwgTestAddBareMtextContextObject(_dwg_struct* dwg);
