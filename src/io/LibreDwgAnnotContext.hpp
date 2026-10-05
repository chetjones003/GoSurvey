#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct AppCommandState;
struct CadAnnotation;
struct CadBlockRef;
struct _dwg_DIMENSION_common;
struct _dwg_entity_INSERT;
struct _dwg_entity_MTEXT;
struct _dwg_entity_TEXT;
struct _dwg_object_entity;
struct _dwg_struct;

enum class DwgExportDimContextKind { Aligned, Linear, Angular };

/// Caches SCALE handles and counts during R2010+ DWG export (REQ-384 inc 2+).
struct DwgExportAnnotContext {
  bool enabled = false;
  _dwg_struct* dwg = nullptr;
  std::unordered_map<std::string, std::uint64_t> scaleNameToHandle;
  int mtextContextObjectsWritten = 0;
  int textContextObjectsWritten = 0;
  int blkrefContextObjectsWritten = 0;
  int dimContextObjectsWritten = 0;
};

void DwgExportAnnotContextInit(DwgExportAnnotContext* ctx, _dwg_struct* dwg, bool r2010OrNewer);

void DwgExportAnnotContextRegisterScale(DwgExportAnnotContext* ctx, std::string_view scaleName,
                                        std::uint64_t absoluteRef);

bool DwgExportAttachMtextAnnotationContext(DwgExportAnnotContext* ctx, _dwg_object_entity* ent,
                                           const _dwg_entity_MTEXT* mtext, const CadAnnotation& an,
                                           const AppCommandState& st);

bool DwgExportAttachTextAnnotationContext(DwgExportAnnotContext* ctx, _dwg_object_entity* ent,
                                          const _dwg_entity_TEXT* text, const CadAnnotation& an,
                                          const AppCommandState& st);

bool DwgExportAttachBlkrefAnnotationContext(DwgExportAnnotContext* ctx, _dwg_object_entity* ent,
                                            const _dwg_entity_INSERT* insert, const CadBlockRef& ref,
                                            const AppCommandState& st);

bool DwgExportAttachDimensionAnnotationContext(DwgExportAnnotContext* ctx, _dwg_object_entity* ent,
                                               const _dwg_DIMENSION_common* common,
                                               DwgExportDimContextKind kind, const CadAnnotation& an,
                                               const AppCommandState& st);

void DwgExportAnnotContextAppendLog(const DwgExportAnnotContext& ctx, std::vector<std::string>& log);

void DwgAnnotContextImportBegin();
void DwgAnnotContextImportScan(const _dwg_struct* dwg);
void DwgAnnotContextImportAppendLog(std::vector<std::string>& log);
int DwgAnnotContextCountObjects(const _dwg_struct* dwg);
bool DwgTestAddBareMtextContextObject(_dwg_struct* dwg);
