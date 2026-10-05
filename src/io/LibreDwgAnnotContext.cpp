#include "LibreDwgAnnotContext.hpp"

#include "CadCommands.hpp"
#include "CadEntities.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string_view>
#include <vector>

#if defined(__cplusplus) && !defined(restrict)
#define restrict
#endif
extern "C" {
#include <dwg.h>
#include <dwg_api.h>
}

extern "C" void dwg_set_next_objhandle(Dwg_Object* obj);
extern "C" void dwg_resolve_objectrefs_silent(Dwg_Data* dwg);

namespace {

struct ImportStats {
  int contextObjects = 0;
};

ImportStats gStats{};

[[nodiscard]] bool IsAnnotationContextType(Dwg_Object_Type ty) {
  switch (ty) {
  case DWG_TYPE_CONTEXTDATAMANAGER:
  case DWG_TYPE_MTEXTOBJECTCONTEXTDATA:
  case DWG_TYPE_TEXTOBJECTCONTEXTDATA:
  case DWG_TYPE_BLKREFOBJECTCONTEXTDATA:
  case DWG_TYPE_LEADEROBJECTCONTEXTDATA:
  case DWG_TYPE_MLEADEROBJECTCONTEXTDATA:
  case DWG_TYPE_ALDIMOBJECTCONTEXTDATA:
  case DWG_TYPE_ANGDIMOBJECTCONTEXTDATA:
  case DWG_TYPE_DMDIMOBJECTCONTEXTDATA:
  case DWG_TYPE_ORDDIMOBJECTCONTEXTDATA:
  case DWG_TYPE_RADIMOBJECTCONTEXTDATA:
  case DWG_TYPE_RADIMLGOBJECTCONTEXTDATA:
  case DWG_TYPE_FCFOBJECTCONTEXTDATA:
  case DWG_TYPE_MTEXTATTRIBUTEOBJECTCONTEXTDATA:
  case DWG_TYPE_ANNOTSCALEOBJECTCONTEXTDATA:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] int CountContextObjectsImpl(const Dwg_Data* dwg) {
  assert(dwg != nullptr);
  int n = 0;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    if (IsAnnotationContextType(dwg->object[i].fixedtype))
      ++n;
  }
  return n;
}

void LinkOwnedObject(Dwg_Data* dwg, Dwg_Object_Object* child, BITCODE_HV ownerHandle) {
  assert(dwg != nullptr && child != nullptr);
  child->ownerhandle = dwg_add_handleref(dwg, 4, ownerHandle, nullptr);
  child->num_reactors = 1;
  child->reactors = static_cast<BITCODE_H*>(std::calloc(1, sizeof(BITCODE_H)));
  if (child->reactors != nullptr)
    child->reactors[0] = dwg_add_handleref(dwg, 4, ownerHandle, nullptr);
  else
    child->num_reactors = 0;
}

[[nodiscard]] int EnsureContextManagerClass(Dwg_Data* dwg) {
  assert(dwg != nullptr && dwg->dwg_class != nullptr);
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* dxfname = dwg->dwg_class[i].dxfname;
    if (dxfname != nullptr && std::strcmp(dxfname, "CONTEXTDATAMANAGER") == 0)
      return static_cast<int>(dwg->dwg_class[i].number);
  }
  return dwg_add_class(dwg, "CONTEXTDATAMANAGER", "AcDbContextDataManager", "ObjectDBX Classes", false);
}

[[nodiscard]] int EnsureMtextContextClass(Dwg_Data* dwg) {
  assert(dwg != nullptr && dwg->dwg_class != nullptr);
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* dxfname = dwg->dwg_class[i].dxfname;
    if (dxfname != nullptr &&
        (std::strcmp(dxfname, "MTEXTOBJECTCONTEXTDATA") == 0 ||
         std::strcmp(dxfname, "ACDB_MTEXTOBJECTCONTEXTDATA_CLASS") == 0))
      return static_cast<int>(dwg->dwg_class[i].number);
  }
  return dwg_add_class(dwg, "ACDB_MTEXTOBJECTCONTEXTDATA_CLASS", "AcDbMTextObjectContextData",
                      "ObjectDBX Classes", false);
}

[[nodiscard]] Dwg_Object* AppendObjectShell(Dwg_Data* dwg, Dwg_Object_Type fixedtype, int classNumber,
                                            const char* dxfName, const char* objName) {
  assert(dwg != nullptr && dxfName != nullptr && objName != nullptr);
  const BITCODE_BL idx = dwg->num_objects;
  const int added = dwg_add_object(dwg);
  if (added > 0)
    return nullptr;
  if (added < 0)
    dwg_resolve_objectrefs_silent(dwg);
  Dwg_Object* obj = &dwg->object[idx];
  obj->supertype = DWG_SUPERTYPE_OBJECT;
  obj->fixedtype = fixedtype;
  obj->type = static_cast<BITCODE_BS>(classNumber);
  obj->dxfname = (dwg->opts & DWG_OPTS_IN) ? _strdup(dxfName) : const_cast<char*>(dxfName);
  obj->name = (dwg->opts & DWG_OPTS_IN) ? _strdup(objName) : const_cast<char*>(objName);
  obj->tio.object = static_cast<Dwg_Object_Object*>(std::calloc(1, sizeof(Dwg_Object_Object)));
  if (obj->tio.object == nullptr)
    return nullptr;
  obj->tio.object->objid = obj->index;
  obj->tio.object->dwg = dwg;
  dwg_set_next_objhandle(obj);
  return obj;
}

[[nodiscard]] Dwg_Object* AppendMtextContextObject(Dwg_Data* dwg, const Dwg_Entity_MTEXT* src,
                                                   BITCODE_HV scaleAbsRef, bool isDefault) {
  assert(dwg != nullptr && src != nullptr);
  const int classNumber = EnsureMtextContextClass(dwg);
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj =
      AppendObjectShell(dwg, DWG_TYPE_MTEXTOBJECTCONTEXTDATA, classNumber, "ACDB_MTEXTOBJECTCONTEXTDATA_CLASS",
                        "MTEXTOBJECTCONTEXTDATA");
  if (obj == nullptr)
    return nullptr;
  if (dwg_setup_MTEXTOBJECTCONTEXTDATA(obj) != 0)
    return nullptr;
  Dwg_Object_MTEXTOBJECTCONTEXTDATA* ctx = obj->tio.object->tio.MTEXTOBJECTCONTEXTDATA;
  if (ctx == nullptr)
    return nullptr;
  ctx->class_version = dwg->header.version >= R_2010 ? static_cast<BITCODE_BS>(4) : static_cast<BITCODE_BS>(3);
  ctx->is_default = isDefault ? 1 : 0;
  ctx->scale = dwg_add_handleref(dwg, 5, scaleAbsRef, obj);
  ctx->attachment = src->attachment;
  ctx->ins_pt = src->ins_pt;
  ctx->x_axis_dir = src->x_axis_dir;
  ctx->rect_width = src->rect_width;
  ctx->rect_height = src->rect_height;
  ctx->extents_width = src->extents_width > 0.0 ? src->extents_width : src->rect_width;
  ctx->extents_height = src->extents_height > 0.0 ? src->extents_height : src->rect_height;
  ctx->column_type = src->column_type;
  return obj;
}

[[nodiscard]] std::vector<const CadAnnotationScale*> ScalesForExport(const CadAnnotation& an,
                                                                     const AppCommandState& st) {
  std::vector<const CadAnnotationScale*> out;
  if (!an.annotativeVisibleScaleNames.empty()) {
    for (const std::string& vn : an.annotativeVisibleScaleNames) {
      for (const CadAnnotationScale& s : st.annotationScales) {
        if (s.name == vn) {
          out.push_back(&s);
          break;
        }
      }
    }
  } else {
    for (const CadAnnotationScale& s : st.annotationScales)
      out.push_back(&s);
  }
  if (out.empty() && !st.annotationScales.empty())
    out.push_back(&st.annotationScales[static_cast<size_t>(std::max(st.currentAnnotationScaleIndex, 0))]);
  return out;
}

}  // namespace

void DwgExportAnnotContextInit(DwgExportAnnotContext* ctx, Dwg_Data* dwg, bool r2010OrNewer) {
  assert(ctx != nullptr);
  *ctx = DwgExportAnnotContext{};
  ctx->enabled = r2010OrNewer && dwg != nullptr;
  ctx->dwg = dwg;
}

void DwgExportAnnotContextRegisterScale(DwgExportAnnotContext* ctx, std::string_view scaleName,
                                        std::uint64_t absoluteRef) {
  assert(ctx != nullptr);
  if (!ctx->enabled || absoluteRef == 0 || scaleName.empty())
    return;
  ctx->scaleNameToHandle[std::string(scaleName)] = absoluteRef;
}

bool DwgExportAttachMtextAnnotationContext(DwgExportAnnotContext* ctx, Dwg_Object_Entity* ent,
                                         const Dwg_Entity_MTEXT* mtext, const CadAnnotation& an,
                                         const AppCommandState& st) {
  assert(ctx != nullptr && ent != nullptr && mtext != nullptr);
  if (!ctx->enabled || ctx->dwg == nullptr || !an.annotative)
    return false;
  if (ent->xdicobjhandle != nullptr && ent->xdicobjhandle->absolute_ref != 0)
    return false;

  Dwg_Data* dwg = ctx->dwg;
  const std::vector<const CadAnnotationScale*> scales = ScalesForExport(an, st);
  if (scales.empty())
    return false;

  std::string defaultScaleName;
  if (st.currentAnnotationScaleIndex >= 0 &&
      st.currentAnnotationScaleIndex < static_cast<int>(st.annotationScales.size()))
    defaultScaleName = st.annotationScales[static_cast<size_t>(st.currentAnnotationScaleIndex)].name;

  struct Pair {
    const CadAnnotationScale* scale;
    Dwg_Object* obj;
  };
  std::vector<Pair> pairs;
  pairs.reserve(scales.size());
  for (const CadAnnotationScale* sc : scales) {
    if (sc == nullptr || sc->name.empty())
      continue;
    const auto it = ctx->scaleNameToHandle.find(sc->name);
    if (it == ctx->scaleNameToHandle.end() || it->second == 0)
      continue;
    const bool isDefault = !defaultScaleName.empty() && sc->name == defaultScaleName;
    Dwg_Object* ctxObj = AppendMtextContextObject(dwg, mtext, it->second, isDefault);
    if (ctxObj == nullptr)
      return false;
    pairs.push_back(Pair{sc, ctxObj});
    ++ctx->mtextContextObjectsWritten;
  }
  if (pairs.empty())
    return false;

  const int mgrClass = EnsureContextManagerClass(dwg);
  if (mgrClass < 0)
    return false;
  Dwg_Object* mgrShell =
      AppendObjectShell(dwg, DWG_TYPE_CONTEXTDATAMANAGER, mgrClass, "CONTEXTDATAMANAGER", "CONTEXTDATAMANAGER");
  if (mgrShell == nullptr)
    return false;
  auto* mgr = static_cast<Dwg_Object_CONTEXTDATAMANAGER*>(std::calloc(1, sizeof(Dwg_Object_CONTEXTDATAMANAGER)));
  if (mgr == nullptr)
    return false;
  mgrShell->tio.object->tio.CONTEXTDATAMANAGER = mgr;
  mgr->parent = mgrShell->tio.object;

  mgr->num_submgrs = 1;
  mgr->submgrs = static_cast<Dwg_CONTEXTDATA_submgr*>(std::calloc(1, sizeof(Dwg_CONTEXTDATA_submgr)));
  if (mgr->submgrs == nullptr)
    return false;
  mgr->submgrs[0].parent = mgr;
  mgr->submgrs[0].num_entries = static_cast<BITCODE_BL>(pairs.size());
  mgr->submgrs[0].entries =
      static_cast<Dwg_CONTEXTDATA_dict*>(std::calloc(pairs.size(), sizeof(Dwg_CONTEXTDATA_dict)));
  if (mgr->submgrs[0].entries == nullptr)
    return false;

  for (size_t i = 0; i < pairs.size(); ++i) {
    mgr->submgrs[0].entries[i].parent = &mgr->submgrs[0];
    mgr->submgrs[0].entries[i].text = dwg_add_u8_input(dwg, pairs[i].scale->name.c_str());
    mgr->submgrs[0].entries[i].itemhandle =
        dwg_add_handleref(dwg, 5, pairs[i].obj->handle.value, mgrShell);
  }

  Dwg_Object* entObj = &dwg->object[ent->objid];
  const BITCODE_HV entHandle = entObj->handle.value;
  const BITCODE_HV mgrHandle = mgrShell->handle.value;

  Dwg_Object_DICTIONARY* xdict = dwg_add_DICTIONARY(dwg, nullptr, "AcDbContextDataManager", mgrHandle);
  int err = 0;
  Dwg_Object* xdictObj = xdict != nullptr ? dwg_obj_generic_to_object(xdict, &err) : nullptr;
  if (xdictObj == nullptr || err != 0)
    return false;

  LinkOwnedObject(dwg, xdictObj->tio.object, entHandle);
  LinkOwnedObject(dwg, mgrShell->tio.object, xdictObj->handle.value);
  ent->xdicobjhandle = dwg_add_handleref(dwg, 3, xdictObj->handle.value, entObj);
  mgrShell->tio.object->xdicobjhandle = dwg_add_handleref(dwg, 3, 0, nullptr);
  return true;
}

void DwgExportAnnotContextAppendLog(const DwgExportAnnotContext& ctx, std::vector<std::string>& log) {
  if (ctx.mtextContextObjectsWritten > 0) {
    log.push_back("CAD export — wrote " + std::to_string(ctx.mtextContextObjectsWritten) +
                  " MTEXT annotation context object(s) (REQ-384 inc 2, issue #688).");
  }
}

void DwgAnnotContextImportBegin() {
  gStats = ImportStats{};
}

void DwgAnnotContextImportScan(const Dwg_Data* dwg) {
  assert(dwg != nullptr);
  gStats.contextObjects = CountContextObjectsImpl(dwg);
}

void DwgAnnotContextImportAppendLog(std::vector<std::string>& log) {
  assert(log.size() < 1000000);
  if (gStats.contextObjects <= 0)
    return;
  log.push_back("DWG import — found " + std::to_string(gStats.contextObjects) +
                " AutoCAD annotation context object(s); GoSurvey uses entity geometry and "
                "AcadAnnotative/GOSURVEY markers (REQ-384 — per-scale context merge not yet on import).");
}

int DwgAnnotContextCountObjects(const Dwg_Data* dwg) {
  return CountContextObjectsImpl(dwg);
}

bool DwgTestAddBareMtextContextObject(Dwg_Data* dwg) {
  assert(dwg != nullptr);
  const BITCODE_BL idx = dwg->num_objects;
  const int added = dwg_add_object(dwg);
  if (added != 0)
    return false;
  Dwg_Object* obj = &dwg->object[idx];
  obj->supertype = DWG_SUPERTYPE_OBJECT;
  obj->fixedtype = DWG_TYPE_MTEXTOBJECTCONTEXTDATA;
  return dwg_setup_MTEXTOBJECTCONTEXTDATA(obj) == 0;
}
