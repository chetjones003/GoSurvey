#include "LibreDwgAnnotContext.hpp"

#include "CadCommands.hpp"
#include "CadEntities.hpp"

#include <cstring>

#include <algorithm>
#include <cassert>
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
  int defaultContextMerged = 0;
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
  case DWG_TYPE_ACDB_HATCHSCALECONTEXTDATA_CLASS:
  case DWG_TYPE_ACDB_HATCHVIEWCONTEXTDATA_CLASS:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] bool ContextObjectIsDefault(const Dwg_Object* ctxObj) {
  assert(ctxObj != nullptr && ctxObj->tio.object != nullptr);
  switch (ctxObj->fixedtype) {
  case DWG_TYPE_MTEXTOBJECTCONTEXTDATA:
    return ctxObj->tio.object->tio.MTEXTOBJECTCONTEXTDATA != nullptr &&
           ctxObj->tio.object->tio.MTEXTOBJECTCONTEXTDATA->is_default != 0;
  case DWG_TYPE_TEXTOBJECTCONTEXTDATA:
    return ctxObj->tio.object->tio.TEXTOBJECTCONTEXTDATA != nullptr &&
           ctxObj->tio.object->tio.TEXTOBJECTCONTEXTDATA->is_default != 0;
  case DWG_TYPE_MLEADEROBJECTCONTEXTDATA:
    return ctxObj->tio.object->tio.MLEADEROBJECTCONTEXTDATA != nullptr &&
           ctxObj->tio.object->tio.MLEADEROBJECTCONTEXTDATA->is_default != 0;
  case DWG_TYPE_ACDB_HATCHSCALECONTEXTDATA_CLASS:
  case DWG_TYPE_ANNOTSCALEOBJECTCONTEXTDATA:
    return ctxObj->tio.object->tio.ANNOTSCALEOBJECTCONTEXTDATA != nullptr &&
           ctxObj->tio.object->tio.ANNOTSCALEOBJECTCONTEXTDATA->is_default != 0;
  default:
    return false;
  }
}

[[nodiscard]] const Dwg_Object* ContextManagerFromDictionary(const Dwg_Data* dwg,
                                                           const Dwg_Object_DICTIONARY* dict) {
  assert(dwg != nullptr);
  if (dict == nullptr || dict->texts == nullptr || dict->itemhandles == nullptr)
    return nullptr;
  for (BITCODE_BL i = 0; i < dict->numitems; ++i) {
    const char* key = dict->texts[i];
    if (key == nullptr || std::strcmp(key, "AcDbContextDataManager") != 0)
      continue;
    if (dict->itemhandles[i] == nullptr || dict->itemhandles[i]->absolute_ref == 0)
      return nullptr;
    return dwg_resolve_handle_silent(const_cast<Dwg_Data*>(dwg), dict->itemhandles[i]->absolute_ref);
  }
  return nullptr;
}

[[nodiscard]] const Dwg_Object* ResolveContextManagerObject(const Dwg_Data* dwg,
                                                            const Dwg_Object_Entity* ent) {
  assert(dwg != nullptr && ent != nullptr);
  if (ent->xdicobjhandle != nullptr && ent->xdicobjhandle->absolute_ref != 0) {
    Dwg_Object* xdictObj =
        dwg_resolve_handle_silent(const_cast<Dwg_Data*>(dwg), ent->xdicobjhandle->absolute_ref);
    if (xdictObj != nullptr && xdictObj->fixedtype == DWG_TYPE_DICTIONARY &&
        xdictObj->tio.object != nullptr) {
      const Dwg_Object* mgr = ContextManagerFromDictionary(dwg, xdictObj->tio.object->tio.DICTIONARY);
      if (mgr != nullptr)
        return mgr;
    }
  }
  const Dwg_Object* entObj = &dwg->object[ent->objid];
  const BITCODE_HV entHandle = entObj->handle.value;
  if (entHandle == 0)
    return nullptr;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    const Dwg_Object* o = &dwg->object[i];
    if (o->fixedtype != DWG_TYPE_DICTIONARY || o->tio.object == nullptr)
      continue;
    const Dwg_Object_Object* obj = o->tio.object;
    if (obj->ownerhandle == nullptr || obj->ownerhandle->absolute_ref != entHandle)
      continue;
    const Dwg_Object* mgr = ContextManagerFromDictionary(dwg, obj->tio.DICTIONARY);
    if (mgr != nullptr)
      return mgr;
  }
  return nullptr;
}

[[nodiscard]] const Dwg_Object* ResolveDefaultContextFromManager(const Dwg_Data* dwg,
                                                                 const Dwg_Object* mgrShell) {
  assert(dwg != nullptr && mgrShell != nullptr);
  if (mgrShell->fixedtype != DWG_TYPE_CONTEXTDATAMANAGER || mgrShell->tio.object == nullptr)
    return nullptr;
  const Dwg_Object_CONTEXTDATAMANAGER* mgr = mgrShell->tio.object->tio.CONTEXTDATAMANAGER;
  if (mgr == nullptr || mgr->num_submgrs < 1 || mgr->submgrs == nullptr)
    return nullptr;
  const Dwg_CONTEXTDATA_submgr& sub = mgr->submgrs[0];
  if (sub.num_entries < 1 || sub.entries == nullptr)
    return nullptr;
  const Dwg_Object* fallback = nullptr;
  for (BITCODE_BL i = 0; i < sub.num_entries; ++i) {
    if (sub.entries[i].itemhandle == nullptr || sub.entries[i].itemhandle->absolute_ref == 0)
      continue;
    Dwg_Object* ctxObj =
        dwg_resolve_handle_silent(const_cast<Dwg_Data*>(dwg), sub.entries[i].itemhandle->absolute_ref);
    if (ctxObj == nullptr || !IsAnnotationContextType(ctxObj->fixedtype))
      continue;
    if (ContextObjectIsDefault(ctxObj))
      return ctxObj;
    if (fallback == nullptr)
      fallback = ctxObj;
  }
  return fallback;
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

[[nodiscard]] int EnsureClassByDxfNames(Dwg_Data* dwg, const char* const* dxfCandidates, size_t nCandidates,
                                        const char* addDxf, const char* cppName) {
  assert(dwg != nullptr && dwg->dwg_class != nullptr);
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* dxfname = dwg->dwg_class[i].dxfname;
    if (dxfname == nullptr)
      continue;
    for (size_t j = 0; j < nCandidates; ++j) {
      if (dxfCandidates[j] != nullptr && std::strcmp(dxfname, dxfCandidates[j]) == 0)
        return static_cast<int>(dwg->dwg_class[i].number);
    }
  }
  return dwg_add_class(dwg, addDxf, cppName, "ObjectDBX Classes", false);
}

[[nodiscard]] int EnsureMtextContextClass(Dwg_Data* dwg) {
  static const char* kNames[] = {"MTEXTOBJECTCONTEXTDATA", "ACDB_MTEXTOBJECTCONTEXTDATA_CLASS"};
  return EnsureClassByDxfNames(dwg, kNames, 2, "ACDB_MTEXTOBJECTCONTEXTDATA_CLASS", "AcDbMTextObjectContextData");
}

[[nodiscard]] int EnsureTextContextClass(Dwg_Data* dwg) {
  static const char* kNames[] = {"TEXTOBJECTCONTEXTDATA", "ACDB_TEXTOBJECTCONTEXTDATA_CLASS"};
  return EnsureClassByDxfNames(dwg, kNames, 2, "ACDB_TEXTOBJECTCONTEXTDATA_CLASS", "AcDbTextObjectContextData");
}

[[nodiscard]] int EnsureBlkrefContextClass(Dwg_Data* dwg) {
  static const char* kNames[] = {"BLKREFOBJECTCONTEXTDATA", "ACDB_BLKREFOBJECTCONTEXTDATA_CLASS"};
  return EnsureClassByDxfNames(dwg, kNames, 2, "ACDB_BLKREFOBJECTCONTEXTDATA_CLASS", "AcDbBlkrefObjectContextData");
}

[[nodiscard]] int EnsureAldimContextClass(Dwg_Data* dwg) {
  static const char* kNames[] = {"ALDIMOBJECTCONTEXTDATA", "ACDB_ALDIMOBJECTCONTEXTDATA_CLASS"};
  return EnsureClassByDxfNames(dwg, kNames, 2, "ACDB_ALDIMOBJECTCONTEXTDATA_CLASS",
                               "AcDbAlignedDimensionObjectContextData");
}

[[nodiscard]] int EnsureAngdimContextClass(Dwg_Data* dwg) {
  static const char* kNames[] = {"ANGDIMOBJECTCONTEXTDATA", "ACDB_ANGDIMOBJECTCONTEXTDATA_CLASS"};
  return EnsureClassByDxfNames(dwg, kNames, 2, "ACDB_ANGDIMOBJECTCONTEXTDATA_CLASS",
                               "AcDbAngularDimensionObjectContextData");
}

[[nodiscard]] int EnsureMleaderContextClass(Dwg_Data* dwg) {
  static const char* kNames[] = {"MLEADEROBJECTCONTEXTDATA", "ACDB_MLEADEROBJECTCONTEXTDATA_CLASS"};
  return EnsureClassByDxfNames(dwg, kNames, 2, "ACDB_MLEADEROBJECTCONTEXTDATA_CLASS",
                               "AcDbMLeaderObjectContextData");
}

[[nodiscard]] int EnsureAnnotScaleOnlyContextClass(Dwg_Data* dwg) {
  static const char* kNames[] = {"ANNOTSCALEOBJECTCONTEXTDATA", "ACDB_ANNOTSCALEOBJECTCONTEXTDATA_CLASS"};
  return EnsureClassByDxfNames(dwg, kNames, 2, "ACDB_ANNOTSCALEOBJECTCONTEXTDATA_CLASS",
                               "AcDbAnnotScaleObjectContextData");
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

void InitAnnotScaleBase(Dwg_Data* dwg, Dwg_Object* ctxObj, BITCODE_HV scaleAbsRef, bool isDefault) {
  assert(dwg != nullptr && ctxObj != nullptr && ctxObj->tio.object != nullptr);
  const BITCODE_BS ver = dwg->header.version >= R_2010 ? static_cast<BITCODE_BS>(4) : static_cast<BITCODE_BS>(3);
  switch (ctxObj->fixedtype) {
  case DWG_TYPE_MTEXTOBJECTCONTEXTDATA: {
    auto* ctx = ctxObj->tio.object->tio.MTEXTOBJECTCONTEXTDATA;
    if (ctx != nullptr) {
      ctx->class_version = ver;
      ctx->is_default = isDefault ? 1 : 0;
      ctx->scale = dwg_add_handleref(dwg, 5, scaleAbsRef, ctxObj);
    }
    break;
  }
  case DWG_TYPE_TEXTOBJECTCONTEXTDATA: {
    auto* ctx = ctxObj->tio.object->tio.TEXTOBJECTCONTEXTDATA;
    if (ctx != nullptr) {
      ctx->class_version = ver;
      ctx->is_default = isDefault ? 1 : 0;
      ctx->scale = dwg_add_handleref(dwg, 5, scaleAbsRef, ctxObj);
    }
    break;
  }
  case DWG_TYPE_BLKREFOBJECTCONTEXTDATA: {
    auto* ctx = ctxObj->tio.object->tio.BLKREFOBJECTCONTEXTDATA;
    if (ctx != nullptr) {
      ctx->class_version = ver;
      ctx->is_default = isDefault ? 1 : 0;
      ctx->scale = dwg_add_handleref(dwg, 5, scaleAbsRef, ctxObj);
    }
    break;
  }
  case DWG_TYPE_ALDIMOBJECTCONTEXTDATA: {
    auto* ctx = ctxObj->tio.object->tio.ALDIMOBJECTCONTEXTDATA;
    if (ctx != nullptr) {
      ctx->class_version = ver;
      ctx->is_default = isDefault ? 1 : 0;
      ctx->scale = dwg_add_handleref(dwg, 5, scaleAbsRef, ctxObj);
    }
    break;
  }
  case DWG_TYPE_ANGDIMOBJECTCONTEXTDATA: {
    auto* ctx = ctxObj->tio.object->tio.ANGDIMOBJECTCONTEXTDATA;
    if (ctx != nullptr) {
      ctx->class_version = ver;
      ctx->is_default = isDefault ? 1 : 0;
      ctx->scale = dwg_add_handleref(dwg, 5, scaleAbsRef, ctxObj);
    }
    break;
  }
  case DWG_TYPE_MLEADEROBJECTCONTEXTDATA: {
    auto* ctx = ctxObj->tio.object->tio.MLEADEROBJECTCONTEXTDATA;
    if (ctx != nullptr) {
      ctx->class_version = ver;
      ctx->is_default = isDefault ? 1 : 0;
      ctx->scale = dwg_add_handleref(dwg, 5, scaleAbsRef, ctxObj);
    }
    break;
  }
  case DWG_TYPE_ACDB_HATCHSCALECONTEXTDATA_CLASS:
  case DWG_TYPE_ANNOTSCALEOBJECTCONTEXTDATA: {
    auto* ctx = ctxObj->tio.object->tio.ANNOTSCALEOBJECTCONTEXTDATA;
    if (ctx != nullptr) {
      ctx->class_version = ver;
      ctx->is_default = isDefault ? 1 : 0;
      ctx->scale = dwg_add_handleref(dwg, 5, scaleAbsRef, ctxObj);
    }
    break;
  }
  default:
    break;
  }
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
  if (obj == nullptr || dwg_setup_MTEXTOBJECTCONTEXTDATA(obj) != 0)
    return nullptr;
  Dwg_Object_MTEXTOBJECTCONTEXTDATA* ctx = obj->tio.object->tio.MTEXTOBJECTCONTEXTDATA;
  if (ctx == nullptr)
    return nullptr;
  InitAnnotScaleBase(dwg, obj, scaleAbsRef, isDefault);
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

[[nodiscard]] Dwg_Object* AppendTextContextObject(Dwg_Data* dwg, const Dwg_Entity_TEXT* src, BITCODE_HV scaleAbsRef,
                                                  bool isDefault) {
  assert(dwg != nullptr && src != nullptr);
  const int classNumber = EnsureTextContextClass(dwg);
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj = AppendObjectShell(dwg, DWG_TYPE_TEXTOBJECTCONTEXTDATA, classNumber,
                                      "ACDB_TEXTOBJECTCONTEXTDATA_CLASS", "TEXTOBJECTCONTEXTDATA");
  if (obj == nullptr || dwg_setup_TEXTOBJECTCONTEXTDATA(obj) != 0)
    return nullptr;
  Dwg_Object_TEXTOBJECTCONTEXTDATA* ctx = obj->tio.object->tio.TEXTOBJECTCONTEXTDATA;
  if (ctx == nullptr)
    return nullptr;
  InitAnnotScaleBase(dwg, obj, scaleAbsRef, isDefault);
  ctx->horizontal_mode = src->horiz_alignment;
  ctx->rotation = src->rotation;
  ctx->ins_pt.x = src->ins_pt.x;
  ctx->ins_pt.y = src->ins_pt.y;
  ctx->alignment_pt.x = src->alignment_pt.x;
  ctx->alignment_pt.y = src->alignment_pt.y;
  return obj;
}

[[nodiscard]] Dwg_Object* AppendBlkrefContextObject(Dwg_Data* dwg, const Dwg_Entity_INSERT* src,
                                                    BITCODE_HV scaleAbsRef, bool isDefault) {
  assert(dwg != nullptr && src != nullptr);
  const int classNumber = EnsureBlkrefContextClass(dwg);
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj = AppendObjectShell(dwg, DWG_TYPE_BLKREFOBJECTCONTEXTDATA, classNumber,
                                      "ACDB_BLKREFOBJECTCONTEXTDATA_CLASS", "BLKREFOBJECTCONTEXTDATA");
  if (obj == nullptr || dwg_setup_BLKREFOBJECTCONTEXTDATA(obj) != 0)
    return nullptr;
  Dwg_Object_BLKREFOBJECTCONTEXTDATA* ctx = obj->tio.object->tio.BLKREFOBJECTCONTEXTDATA;
  if (ctx == nullptr)
    return nullptr;
  InitAnnotScaleBase(dwg, obj, scaleAbsRef, isDefault);
  ctx->rotation = src->rotation;
  ctx->ins_pt = src->ins_pt;
  ctx->scale_factor.x = src->scale.x;
  ctx->scale_factor.y = src->scale.y;
  ctx->scale_factor.z = src->scale.z;
  return obj;
}

void FillOcdDimension(Dwg_OCD_Dimension* dim, const Dwg_DIMENSION_common* common) {
  assert(dim != nullptr && common != nullptr);
  dim->def_pt.x = common->text_midpt.x;
  dim->def_pt.y = common->text_midpt.y;
  dim->text_rotation = common->text_rotation;
  dim->is_def_textloc = 1;
  dim->block = common->block;
}

[[nodiscard]] Dwg_Object* AppendAldimContextObject(Dwg_Data* dwg, const Dwg_DIMENSION_common* common,
                                                   BITCODE_HV scaleAbsRef, bool isDefault) {
  assert(dwg != nullptr && common != nullptr);
  const int classNumber = EnsureAldimContextClass(dwg);
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj = AppendObjectShell(dwg, DWG_TYPE_ALDIMOBJECTCONTEXTDATA, classNumber,
                                      "ACDB_ALDIMOBJECTCONTEXTDATA_CLASS", "ALDIMOBJECTCONTEXTDATA");
  if (obj == nullptr || dwg_setup_ALDIMOBJECTCONTEXTDATA(obj) != 0)
    return nullptr;
  Dwg_Object_ALDIMOBJECTCONTEXTDATA* ctx = obj->tio.object->tio.ALDIMOBJECTCONTEXTDATA;
  if (ctx == nullptr)
    return nullptr;
  InitAnnotScaleBase(dwg, obj, scaleAbsRef, isDefault);
  FillOcdDimension(&ctx->dimension, common);
  ctx->dimline_pt = common->def_pt;
  return obj;
}

[[nodiscard]] Dwg_Object* AppendAngdimContextObject(Dwg_Data* dwg, const Dwg_DIMENSION_common* common,
                                                    BITCODE_HV scaleAbsRef, bool isDefault) {
  assert(dwg != nullptr && common != nullptr);
  const int classNumber = EnsureAngdimContextClass(dwg);
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj = AppendObjectShell(dwg, DWG_TYPE_ANGDIMOBJECTCONTEXTDATA, classNumber,
                                      "ACDB_ANGDIMOBJECTCONTEXTDATA_CLASS", "ANGDIMOBJECTCONTEXTDATA");
  if (obj == nullptr)
    return nullptr;
  auto* ctx = static_cast<Dwg_Object_ANGDIMOBJECTCONTEXTDATA*>(std::calloc(1, sizeof(Dwg_Object_ANGDIMOBJECTCONTEXTDATA)));
  if (ctx == nullptr)
    return nullptr;
  obj->tio.object->tio.ANGDIMOBJECTCONTEXTDATA = ctx;
  ctx->parent = obj->tio.object;
  InitAnnotScaleBase(dwg, obj, scaleAbsRef, isDefault);
  FillOcdDimension(&ctx->dimension, common);
  ctx->arc_pt = common->def_pt;
  return obj;
}

[[nodiscard]] Dwg_Object* AppendMleaderContextObject(Dwg_Data* dwg, BITCODE_HV scaleAbsRef, bool isDefault) {
  assert(dwg != nullptr);
  const int classNumber = EnsureMleaderContextClass(dwg);
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj = AppendObjectShell(dwg, DWG_TYPE_MLEADEROBJECTCONTEXTDATA, classNumber,
                                      "ACDB_MLEADEROBJECTCONTEXTDATA_CLASS", "MLEADEROBJECTCONTEXTDATA");
  if (obj == nullptr)
    return nullptr;
  auto* ctx = static_cast<Dwg_Object_MLEADEROBJECTCONTEXTDATA*>(
      std::calloc(1, sizeof(Dwg_Object_MLEADEROBJECTCONTEXTDATA)));
  if (ctx == nullptr)
    return nullptr;
  obj->tio.object->tio.MLEADEROBJECTCONTEXTDATA = ctx;
  ctx->parent = obj->tio.object;
  InitAnnotScaleBase(dwg, obj, scaleAbsRef, isDefault);
  return obj;
}

[[nodiscard]] Dwg_Object* AppendHatchScaleContextObject(Dwg_Data* dwg, BITCODE_HV scaleAbsRef, bool isDefault) {
  assert(dwg != nullptr);
  const int classNumber = EnsureAnnotScaleOnlyContextClass(dwg);
  if (classNumber < 0)
    return nullptr;
  Dwg_Object* obj = AppendObjectShell(dwg, DWG_TYPE_ANNOTSCALEOBJECTCONTEXTDATA, classNumber,
                                      "ACDB_ANNOTSCALEOBJECTCONTEXTDATA_CLASS", "ANNOTSCALEOBJECTCONTEXTDATA");
  if (obj == nullptr)
    return nullptr;
  auto* ctx = static_cast<Dwg_Object_ANNOTSCALEOBJECTCONTEXTDATA*>(
      std::calloc(1, sizeof(Dwg_Object_ANNOTSCALEOBJECTCONTEXTDATA)));
  if (ctx == nullptr)
    return nullptr;
  obj->tio.object->tio.ANNOTSCALEOBJECTCONTEXTDATA = ctx;
  ctx->parent = obj->tio.object;
  InitAnnotScaleBase(dwg, obj, scaleAbsRef, isDefault);
  return obj;
}

[[nodiscard]] CadAnnotation AnnotHostFromFilledRegion(const CadFilledRegion& fr) {
  CadAnnotation host;
  host.annotative = fr.annotative;
  host.annotativeVisibleScaleNames = fr.annotativeVisibleScaleNames;
  return host;
}

[[nodiscard]] CadAnnotation AnnotHostFromMultileader(const CadMultileader& ml) {
  CadAnnotation host;
  host.annotative = ml.annotative;
  host.annotativeVisibleScaleNames = ml.annotativeVisibleScaleNames;
  return host;
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

[[nodiscard]] std::string DefaultScaleName(const AppCommandState& st) {
  if (st.currentAnnotationScaleIndex >= 0 &&
      st.currentAnnotationScaleIndex < static_cast<int>(st.annotationScales.size()))
    return st.annotationScales[static_cast<size_t>(st.currentAnnotationScaleIndex)].name;
  return {};
}

enum class CtxObjectKind { Mtext, Text, Blkref, Aldim, Angdim, Mleader, HatchScale };

struct ContextSource {
  CtxObjectKind kind;
  const Dwg_Entity_MTEXT* mtext;
  const Dwg_Entity_TEXT* text;
  const Dwg_Entity_INSERT* insert;
  const Dwg_DIMENSION_common* dimension;
};

struct ContextPair {
  const CadAnnotationScale* scale;
  Dwg_Object* obj;
};

[[nodiscard]] Dwg_Object* MakeContextObject(Dwg_Data* dwg, const ContextSource& src, BITCODE_HV scaleAbsRef,
                                            bool isDefault) {
  assert(dwg != nullptr);
  switch (src.kind) {
  case CtxObjectKind::Mtext:
    return AppendMtextContextObject(dwg, src.mtext, scaleAbsRef, isDefault);
  case CtxObjectKind::Text:
    return AppendTextContextObject(dwg, src.text, scaleAbsRef, isDefault);
  case CtxObjectKind::Blkref:
    return AppendBlkrefContextObject(dwg, src.insert, scaleAbsRef, isDefault);
  case CtxObjectKind::Aldim:
    return AppendAldimContextObject(dwg, src.dimension, scaleAbsRef, isDefault);
  case CtxObjectKind::Angdim:
    return AppendAngdimContextObject(dwg, src.dimension, scaleAbsRef, isDefault);
  case CtxObjectKind::Mleader:
    return AppendMleaderContextObject(dwg, scaleAbsRef, isDefault);
  case CtxObjectKind::HatchScale:
    return AppendHatchScaleContextObject(dwg, scaleAbsRef, isDefault);
  default:
    return nullptr;
  }
}

[[nodiscard]] bool AttachContextObjects(DwgExportAnnotContext* ctx, Dwg_Object_Entity* ent, const AppCommandState& st,
                                        const CadAnnotation& an, const ContextSource& src, int* objectsWritten) {
  assert(ctx != nullptr && ent != nullptr && objectsWritten != nullptr);
  if (!ctx->enabled || ctx->dwg == nullptr || !an.annotative)
    return false;
  if (ent->xdicobjhandle != nullptr && ent->xdicobjhandle->absolute_ref != 0)
    return false;

  Dwg_Data* dwg = ctx->dwg;
  const std::vector<const CadAnnotationScale*> scales = ScalesForExport(an, st);
  if (scales.empty())
    return false;

  const std::string defaultScaleName = DefaultScaleName(st);
  std::vector<ContextPair> pairs;
  pairs.reserve(scales.size());
  for (const CadAnnotationScale* sc : scales) {
    if (sc == nullptr || sc->name.empty())
      continue;
    const auto it = ctx->scaleNameToHandle.find(sc->name);
    if (it == ctx->scaleNameToHandle.end() || it->second == 0)
      continue;
    const bool isDefault = !defaultScaleName.empty() && sc->name == defaultScaleName;
    Dwg_Object* ctxObj = MakeContextObject(dwg, src, it->second, isDefault);
    if (ctxObj == nullptr)
      return false;
    pairs.push_back(ContextPair{sc, ctxObj});
    ++(*objectsWritten);
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
  dwg_resolve_objectrefs_silent(dwg);
  return true;
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
  assert(mtext != nullptr);
  const ContextSource src{CtxObjectKind::Mtext, mtext, nullptr, nullptr, nullptr};
  return AttachContextObjects(ctx, ent, st, an, src, &ctx->mtextContextObjectsWritten);
}

bool DwgExportAttachTextAnnotationContext(DwgExportAnnotContext* ctx, Dwg_Object_Entity* ent,
                                          const Dwg_Entity_TEXT* text, const CadAnnotation& an,
                                          const AppCommandState& st) {
  assert(text != nullptr);
  const ContextSource src{CtxObjectKind::Text, nullptr, text, nullptr, nullptr};
  return AttachContextObjects(ctx, ent, st, an, src, &ctx->textContextObjectsWritten);
}

bool DwgExportAttachBlkrefAnnotationContext(DwgExportAnnotContext* ctx, Dwg_Object_Entity* ent,
                                            const Dwg_Entity_INSERT* insert, const CadBlockRef& ref,
                                            const AppCommandState& st) {
  assert(insert != nullptr);
  if (!ref.annotative)
    return false;
  CadAnnotation host;
  host.annotative = true;
  host.annotativeVisibleScaleNames = ref.annotativeVisibleScaleNames;
  const ContextSource src{CtxObjectKind::Blkref, nullptr, nullptr, insert, nullptr};
  return AttachContextObjects(ctx, ent, st, host, src, &ctx->blkrefContextObjectsWritten);
}

bool DwgExportAttachDimensionAnnotationContext(DwgExportAnnotContext* ctx, Dwg_Object_Entity* ent,
                                               const Dwg_DIMENSION_common* common, DwgExportDimContextKind kind,
                                               const CadAnnotation& an, const AppCommandState& st) {
  assert(common != nullptr);
  const CtxObjectKind objKind =
      kind == DwgExportDimContextKind::Angular ? CtxObjectKind::Angdim : CtxObjectKind::Aldim;
  const ContextSource src{objKind, nullptr, nullptr, nullptr, common};
  return AttachContextObjects(ctx, ent, st, an, src, &ctx->dimContextObjectsWritten);
}

bool DwgExportAttachMleaderAnnotationContext(DwgExportAnnotContext* ctx, Dwg_Object_Entity* ent,
                                             const Dwg_Entity_MULTILEADER* mleader, const CadMultileader& ml,
                                             const AppCommandState& st) {
  assert(mleader != nullptr);
  (void)mleader;
  if (!ml.annotative)
    return false;
  const CadAnnotation host = AnnotHostFromMultileader(ml);
  const ContextSource src{CtxObjectKind::Mleader, nullptr, nullptr, nullptr, nullptr};
  return AttachContextObjects(ctx, ent, st, host, src, &ctx->mleaderContextObjectsWritten);
}

bool DwgExportAttachHatchAnnotationContext(DwgExportAnnotContext* ctx, Dwg_Object_Entity* ent,
                                           const Dwg_Entity_HATCH* hatch, const CadFilledRegion& fr,
                                           const AppCommandState& st) {
  assert(hatch != nullptr);
  (void)hatch;
  if (!fr.annotative)
    return false;
  const CadAnnotation host = AnnotHostFromFilledRegion(fr);
  const ContextSource src{CtxObjectKind::HatchScale, nullptr, nullptr, nullptr, nullptr};
  return AttachContextObjects(ctx, ent, st, host, src, &ctx->hatchContextObjectsWritten);
}

void DwgExportAnnotContextAppendLog(const DwgExportAnnotContext& ctx, std::vector<std::string>& log) {
  if (ctx.mtextContextObjectsWritten > 0) {
    log.push_back("CAD export — wrote " + std::to_string(ctx.mtextContextObjectsWritten) +
                  " MTEXT annotation context object(s) (REQ-384, issue #688).");
  }
  if (ctx.textContextObjectsWritten > 0) {
    log.push_back("CAD export — wrote " + std::to_string(ctx.textContextObjectsWritten) +
                  " TEXT annotation context object(s) (REQ-384 inc 3, issue #688).");
  }
  if (ctx.blkrefContextObjectsWritten > 0) {
    log.push_back("CAD export — wrote " + std::to_string(ctx.blkrefContextObjectsWritten) +
                  " INSERT annotation context object(s) (REQ-384 inc 3, issue #688).");
  }
  if (ctx.dimContextObjectsWritten > 0) {
    log.push_back("CAD export — wrote " + std::to_string(ctx.dimContextObjectsWritten) +
                  " DIMENSION annotation context object(s) (REQ-384 inc 3, issue #688).");
  }
  if (ctx.mleaderContextObjectsWritten > 0) {
    log.push_back("CAD export — wrote " + std::to_string(ctx.mleaderContextObjectsWritten) +
                  " MULTILEADER annotation context object(s) (REQ-384 inc 4, issue #688).");
  }
  if (ctx.hatchContextObjectsWritten > 0) {
    log.push_back("CAD export — wrote " + std::to_string(ctx.hatchContextObjectsWritten) +
                  " HATCH scale context object(s) (REQ-384 inc 4, issue #688).");
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
  std::string line = "DWG import — found " + std::to_string(gStats.contextObjects) +
                     " AutoCAD annotation context object(s); GoSurvey uses entity geometry and "
                     "AcadAnnotative/GOSURVEY markers";
  if (gStats.defaultContextMerged > 0) {
    line += " (merged default-scale context geometry for " + std::to_string(gStats.defaultContextMerged) +
            " entit";
    line += gStats.defaultContextMerged == 1 ? "y" : "ies";
    line += ", REQ-384 inc 4)";
  } else {
    line += " (REQ-384 — per-scale context merge partial; default-scale MTEXT/TEXT only)";
  }
  line += ".";
  log.push_back(std::move(line));
}

const Dwg_Object* DwgImportResolveDefaultContextObject(const Dwg_Data* dwg, const Dwg_Object_Entity* ent) {
  assert(dwg != nullptr && ent != nullptr);
  const Dwg_Object* mgrShell = ResolveContextManagerObject(dwg, ent);
  if (mgrShell == nullptr)
    return nullptr;
  return ResolveDefaultContextFromManager(dwg, mgrShell);
}

int DwgImportDefaultContextMergeCount() {
  return gStats.defaultContextMerged;
}

void DwgImportNoteDefaultContextMerged() {
  ++gStats.defaultContextMerged;
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
