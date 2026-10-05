#include "LibreDwgMaterial.hpp"

#include "CadCommands.hpp"
#include "CadEntities.hpp"
#include "DwgIo.hpp"
#include "LibreDwgCad.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

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
  int hostsWithDiffuse = 0;
  int hostsMapOnly = 0;
  int hostsNoDiffuse = 0;
};

ImportStats gStats{};

Dwg_Object* MaterialFromHandle(Dwg_Data* dwg, BITCODE_H handle) {
  assert(dwg != nullptr);
  if (handle == nullptr || handle->absolute_ref == 0)
    return nullptr;
  Dwg_Object* obj = dwg_resolve_handle_silent(dwg, handle->absolute_ref);
  if (obj == nullptr || obj->fixedtype != DWG_TYPE_MATERIAL || obj->tio.object == nullptr ||
      obj->tio.object->tio.MATERIAL == nullptr)
    return nullptr;
  return obj;
}

bool DiffuseMapPresent(const Dwg_MATERIAL_mapper& map) {
  return map.filename != nullptr && map.filename[0] != '\0';
}

bool TryDiffuseRgb(const Dwg_Object_MATERIAL* mat, float* outR, float* outG, float* outB) {
  assert(mat != nullptr && outR != nullptr && outG != nullptr && outB != nullptr);
  if (mat->diffuse_color.flag != 1)
    return false;
  const double factor = std::clamp(static_cast<double>(mat->diffuse_color.factor), 0.0, 1.0);
  const unsigned rgb = static_cast<unsigned>(mat->diffuse_color.rgb) & 0xFFFFFFu;
  *outR = static_cast<float>(((rgb >> 16) & 0xFFu) / 255.0 * factor);
  *outG = static_cast<float>(((rgb >> 8) & 0xFFu) / 255.0 * factor);
  *outB = static_cast<float>((rgb & 0xFFu) / 255.0 * factor);
  return true;
}

int EnsureMaterialClass(Dwg_Data* dwg) {
  assert(dwg != nullptr);
  if (dwg->dwg_class == nullptr)
    return -1;
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* name = dwg->dwg_class[i].dxfname;
    if (name != nullptr && std::strcmp(name, "MATERIAL") == 0)
      return static_cast<int>(dwg->dwg_class[i].number);
  }
  return dwg_add_class(dwg, "MATERIAL", "AcDbMaterial", "ObjectDBX Classes", false);
}

Dwg_Object* AppendMaterialObject(Dwg_Data* dwg, const char* name, unsigned rgb24, double factor) {
  assert(dwg != nullptr && name != nullptr);
  const int classNumber = EnsureMaterialClass(dwg);
  if (classNumber < 0)
    return nullptr;
  const BITCODE_BL idx = dwg->num_objects;
  const int added = dwg_add_object(dwg);
  if (added > 0)
    return nullptr;
  if (added < 0)
    dwg_resolve_objectrefs_silent(dwg);
  Dwg_Object* obj = &dwg->object[idx];
  obj->supertype = DWG_SUPERTYPE_OBJECT;
  obj->fixedtype = DWG_TYPE_MATERIAL;
  obj->type = static_cast<BITCODE_BS>(classNumber);
  obj->dxfname = (dwg->opts & DWG_OPTS_IN) ? _strdup("MATERIAL") : const_cast<char*>("MATERIAL");
  obj->name = (dwg->opts & DWG_OPTS_IN) ? _strdup("MATERIAL") : const_cast<char*>("MATERIAL");
  obj->tio.object = static_cast<Dwg_Object_Object*>(std::calloc(1, sizeof(Dwg_Object_Object)));
  if (obj->tio.object == nullptr)
    return nullptr;
  obj->tio.object->objid = obj->index;
  obj->tio.object->dwg = dwg;
  auto* mat = static_cast<Dwg_Object_MATERIAL*>(std::calloc(1, sizeof(Dwg_Object_MATERIAL)));
  if (mat == nullptr)
    return nullptr;
  obj->tio.object->tio.MATERIAL = mat;
  mat->parent = obj->tio.object;
  dwg_set_next_objhandle(obj);
  mat->name = dwg_add_u8_input(dwg, name);
  mat->diffuse_color.flag = 1;
  mat->diffuse_color.factor = static_cast<BITCODE_BD>(factor);
  mat->diffuse_color.rgb = static_cast<BITCODE_BL>(rgb24 & 0xFFFFFFu);
  mat->specular_gloss_factor = 0.5;
  mat->opacity_percent = 1.0;
  mat->refraction_index = 1.0;
  return obj;
}

Dwg_Object* AcadMaterialDictionary(Dwg_Data* dwg) {
  assert(dwg != nullptr);
  BITCODE_H ctrl = dwg->header_vars.DICTIONARY_MATERIAL;
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    ctrl = dwg_find_dictionary(dwg, "ACAD_MATERIAL");
  if (ctrl != nullptr && ctrl->absolute_ref != 0) {
    Dwg_Object* dictObj = dwg_resolve_handle_silent(dwg, ctrl->absolute_ref);
    if (dictObj != nullptr && dictObj->fixedtype == DWG_TYPE_DICTIONARY)
      return dictObj;
  }
  if (dwg_add_DICTIONARY(dwg, "ACAD_MATERIAL", nullptr, 0) == nullptr)
    return nullptr;
  ctrl = dwg_find_dictionary(dwg, "ACAD_MATERIAL");
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    return nullptr;
  dwg->header_vars.DICTIONARY_MATERIAL = ctrl;
  return dwg_resolve_handle_silent(dwg, ctrl->absolute_ref);
}

void AddMaterialToDictionary(Dwg_Data* dwg, Dwg_Object* matObj) {
  assert(dwg != nullptr && matObj != nullptr);
  Dwg_Object* dictObj = AcadMaterialDictionary(dwg);
  if (dictObj == nullptr || dictObj->tio.object == nullptr ||
      dictObj->tio.object->tio.DICTIONARY == nullptr)
    return;
  Dwg_Object_MATERIAL* mat = matObj->tio.object->tio.MATERIAL;
  if (mat == nullptr || mat->name == nullptr)
    return;
  dwg_add_DICTIONARY_item(dictObj->tio.object->tio.DICTIONARY, mat->name, matObj->handle.value);
}

}  // namespace

void DwgMaterialImportBegin() { gStats = ImportStats{}; }

void DwgImportApplyEntityMaterial(_dwg_struct* dwgIn, const void* entity, EntityAttributes* at) {
  assert(dwgIn != nullptr && entity != nullptr && at != nullptr);
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  const auto* ent = static_cast<const Dwg_Object_Entity*>(entity);
  if (dwg->header.version < R_2007)
    return;
  int err = 0;
  dwg_object_ref* matRef = dwg_ent_get_material(ent, &err);
  if (matRef == nullptr)
    return;
  Dwg_Object* matObj = MaterialFromHandle(dwg, matRef);
  if (matObj == nullptr)
    return;
  const Dwg_Object_MATERIAL* mat = matObj->tio.object->tio.MATERIAL;
  assert(mat != nullptr);
  float r = 0.f;
  float g = 0.f;
  float b = 0.f;
  const bool hasDiffuse = TryDiffuseRgb(mat, &r, &g, &b);
  const bool hasMap = DiffuseMapPresent(mat->diffusemap);
  if (hasDiffuse) {
    at->materialDiffuseOverride = true;
    at->materialDiffuseR = r;
    at->materialDiffuseG = g;
    at->materialDiffuseB = b;
    const bool utf16Name =
        dwg->header.from_version >= R_2007 && !(dwg->opts & DWG_OPTS_IN);
    const std::string decoded =
        libredwgcad_detail::DecodeDwgString(mat->name, utf16Name);
    if (!decoded.empty())
      at->materialName = decoded;
    ++gStats.hostsWithDiffuse;
    return;
  }
  if (hasMap)
    ++gStats.hostsMapOnly;
  else
    ++gStats.hostsNoDiffuse;
}

void DwgMaterialImportAppendLog(std::vector<std::string>& log) {
  if (gStats.hostsWithDiffuse <= 0 && gStats.hostsMapOnly <= 0 && gStats.hostsNoDiffuse <= 0)
    return;
  if (gStats.hostsWithDiffuse > 0) {
    log.push_back("DWG import — " + std::to_string(gStats.hostsWithDiffuse) +
                  " 3D host(s) with MATERIAL diffuse colour (REQ-372).");
  }
  if (gStats.hostsMapOnly > 0) {
    log.push_back("DWG import — " + std::to_string(gStats.hostsMapOnly) +
                  " 3D host(s) with texture-only MATERIAL (diffuse map not evaluated, REQ-372).");
  }
  if (gStats.hostsNoDiffuse > 0) {
    log.push_back("DWG import — " + std::to_string(gStats.hostsNoDiffuse) +
                  " 3D host(s) with MATERIAL but no override diffuse colour.");
  }
}

const void* DwgTestAddDiffuseMaterial(_dwg_struct* dwgIn, const char* name, unsigned rgb24, double factor) {
  assert(dwgIn != nullptr && name != nullptr);
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  Dwg_Object* obj = AppendMaterialObject(dwg, name, rgb24, factor);
  if (obj == nullptr)
    return nullptr;
  AddMaterialToDictionary(dwg, obj);
  dwg_resolve_objectrefs_silent(dwg);
  return dwg_add_handleref(dwg, 5, obj->handle.value, nullptr);
}

void DwgExportMaterialContextInit(DwgExportMaterialContext* ctx, bool r2007OrNewer) {
  assert(ctx != nullptr);
  ctx->enabled = r2007OrNewer;
  ctx->diffuseRgbToHandle.clear();
  ctx->materialNameToHandle.clear();
  ctx->materialsWritten = 0;
}

static unsigned PackDiffuseRgb24(float r, float g, float b) {
  const auto q = [](float c) {
    return static_cast<unsigned>(std::clamp(static_cast<int>(std::lround(c * 255.f)), 0, 255));
  };
  return (q(r) << 16u) | (q(g) << 8u) | q(b);
}

void DwgExportApplyEntityMaterial(_dwg_struct* dwgIn, DwgExportMaterialContext* ctx, const AppCommandState& st,
                                  void* entity, const EntityAttributes* attr, float defaultR, float defaultG,
                                  float defaultB) {
  assert(dwgIn != nullptr && ctx != nullptr && entity != nullptr);
  if (!ctx->enabled || attr == nullptr)
    return;
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  auto* ent = static_cast<Dwg_Object_Entity*>(entity);
  float rgba[4] = {defaultR, defaultG, defaultB, 1.f};
  if (attr->materialDiffuseOverride) {
    rgba[0] = attr->materialDiffuseR;
    rgba[1] = attr->materialDiffuseG;
    rgba[2] = attr->materialDiffuseB;
  } else {
    const CadLayerRow* lr = FindDrawingLayerRowCi(st, attr->layer);
    ResolveEntityRgbaForViewport(*attr, lr, defaultR, defaultG, defaultB, rgba);
  }
  const unsigned rgb24 = PackDiffuseRgb24(rgba[0], rgba[1], rgba[2]);
  std::uint64_t absRef = 0;
  const bool useNamedMat =
      attr->materialDiffuseOverride && !attr->materialName.empty();
  if (useNamedMat) {
    const auto foundName = ctx->materialNameToHandle.find(attr->materialName);
    if (foundName == ctx->materialNameToHandle.end()) {
      Dwg_Object* obj = AppendMaterialObject(dwg, attr->materialName.c_str(), rgb24, 1.0);
      if (obj == nullptr)
        return;
      AddMaterialToDictionary(dwg, obj);
      absRef = obj->handle.value;
      ctx->materialNameToHandle.emplace(attr->materialName, absRef);
      ctx->diffuseRgbToHandle.emplace(rgb24, absRef);
      ++ctx->materialsWritten;
    } else {
      absRef = foundName->second;
    }
  } else {
    const auto found = ctx->diffuseRgbToHandle.find(rgb24);
    if (found == ctx->diffuseRgbToHandle.end()) {
      char name[32];
      std::snprintf(name, sizeof(name), "GS_D%06X", rgb24);
      Dwg_Object* obj = AppendMaterialObject(dwg, name, rgb24, 1.0);
      if (obj == nullptr)
        return;
      AddMaterialToDictionary(dwg, obj);
      absRef = obj->handle.value;
      ctx->diffuseRgbToHandle.emplace(rgb24, absRef);
      ++ctx->materialsWritten;
    } else {
      absRef = found->second;
    }
  }
  Dwg_Object* entObj = &dwg->object[ent->objid];
  ent->material_flags = 3;
  ent->material = dwg_add_handleref(dwg, 5, absRef, entObj);
}

void DwgExportMaterialAppendLog(const DwgExportMaterialContext& ctx, std::vector<std::string>& log) {
  if (ctx.materialsWritten <= 0)
    return;
  log.push_back("CAD export — wrote " + std::to_string(ctx.materialsWritten) +
                " MATERIAL object(s) (REQ-372, issue #624).");
}

int DwgExportCountMaterialAppearanceLosses(const AppCommandState& st) {
  const bool r2007MaterialExport = st.dwgExportVersion >= DwgSaveVersion::R2010;
  int n = 0;
  auto countOverride = [&](const std::vector<EntityAttributes>& attrs) {
    for (const EntityAttributes& a : attrs) {
      if (a.materialDiffuseOverride)
        ++n;
    }
  };
  if (r2007MaterialExport) {
    countOverride(st.cadSolidAttrs);
  } else {
    countOverride(st.cadMeshAttrs);
    countOverride(st.cadSolidAttrs);
    countOverride(st.cadSurfaceAttrs);
  }
  return n;
}
