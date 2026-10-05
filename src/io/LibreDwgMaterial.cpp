#include "LibreDwgMaterial.hpp"

#include "CadEntities.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
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

void AddMaterialToDictionary(Dwg_Data* dwg, Dwg_Object* matObj) {
  assert(dwg != nullptr && matObj != nullptr);
  BITCODE_H ctrl = dwg->header_vars.DICTIONARY_MATERIAL;
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    ctrl = dwg_find_dictionary(dwg, "ACAD_MATERIAL");
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    return;
  Dwg_Object* dictObj = dwg_resolve_handle_silent(dwg, ctrl->absolute_ref);
  if (dictObj == nullptr || dictObj->fixedtype != DWG_TYPE_DICTIONARY ||
      dictObj->tio.object == nullptr || dictObj->tio.object->tio.DICTIONARY == nullptr)
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
