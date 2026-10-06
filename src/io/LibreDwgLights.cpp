#include "LibreDwgLights.hpp"

#include "CadCommands.hpp"
#include "CadEntities.hpp"
#include "DwgIo.hpp"
#include "DxfColors.hpp"
#include "LibreDwgCad.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
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
  int lights = 0;
  int suns = 0;
  int lightLists = 0;
  int lightListEntries = 0;
};

ImportStats gStats{};

[[nodiscard]] bool ExportVersionSupportsLights(DwgSaveVersion version) {
  switch (version) {
  case DwgSaveVersion::R2010:
  case DwgSaveVersion::R2013:
  case DwgSaveVersion::R2018:
    return true;
  case DwgSaveVersion::R2000:
  case DwgSaveVersion::R2004:
    return false;
  }
  return false;
}

[[nodiscard]] unsigned Rgb24FromCmc(const BITCODE_CMC& cmc) {
  if (cmc.method == DWG_COLOR_METHOD_TRUECOLOR)
    return static_cast<unsigned>(cmc.rgb) & 0xFFFFFFu;
  if (cmc.index >= 0 && cmc.index < 256)
    return DxfRgbPackedFromAci(static_cast<int>(cmc.index)) & 0xFFFFFFu;
  return 0xFFFFFFu;
}

void SetCmcRgb24(BITCODE_CMC* cmc, unsigned rgb24) {
  assert(cmc != nullptr);
  cmc->method = DWG_COLOR_METHOD_TRUECOLOR;
  cmc->rgb = 0xC3000000u | (rgb24 & 0xFFFFFFu);
  cmc->index = 7;
}

[[nodiscard]] int EnsureLightEntityClass(Dwg_Data* dwg) {
  assert(dwg != nullptr && dwg->dwg_class != nullptr);
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* dxfname = dwg->dwg_class[i].dxfname;
    if (dxfname != nullptr && std::strcmp(dxfname, "LIGHT") == 0)
      return static_cast<int>(dwg->dwg_class[i].number);
  }
  return dwg_add_class(dwg, "LIGHT", "AcDbLight", "ObjectDBX Classes", true);
}

[[nodiscard]] int EnsureSunObjectClass(Dwg_Data* dwg) {
  assert(dwg != nullptr && dwg->dwg_class != nullptr);
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* dxfname = dwg->dwg_class[i].dxfname;
    if (dxfname != nullptr && std::strcmp(dxfname, "SUN") == 0)
      return static_cast<int>(dwg->dwg_class[i].number);
  }
  return dwg_add_class(dwg, "SUN", "AcDbSun", "ObjectDBX Classes", false);
}

[[nodiscard]] int EnsureLightListClass(Dwg_Data* dwg) {
  assert(dwg != nullptr && dwg->dwg_class != nullptr);
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* dxfname = dwg->dwg_class[i].dxfname;
    if (dxfname != nullptr && std::strcmp(dxfname, "LIGHTLIST") == 0)
      return static_cast<int>(dwg->dwg_class[i].number);
  }
  return dwg_add_class(dwg, "LIGHTLIST", "AcDbLightList", "ObjectDBX Classes", false);
}

[[nodiscard]] Dwg_Object* AppendLightEntityShell(Dwg_Data* dwg, Dwg_Object_BLOCK_HEADER* blkhdr,
                                                 const CadDwgImportedLight& src) {
  assert(dwg != nullptr && blkhdr != nullptr);
  int error = 0;
  Dwg_Object* blkobj = dwg_obj_generic_to_object(blkhdr, &error);
  if (blkobj == nullptr || error != 0)
    return nullptr;
  const int classNumber = EnsureLightEntityClass(dwg);
  if (classNumber < 0)
    return nullptr;
  const BITCODE_BL idx = dwg->num_objects;
  if (dwg_add_object(dwg) < 0)
    dwg_resolve_objectrefs_silent(dwg);
  Dwg_Object* obj = &dwg->object[idx];
  dwg->cur_index++;
  obj->supertype = DWG_SUPERTYPE_ENTITY;
  obj->tio.entity = static_cast<Dwg_Object_Entity*>(std::calloc(1, sizeof(Dwg_Object_Entity)));
  if (obj->tio.entity == nullptr)
    return nullptr;
  obj->tio.entity->objid = obj->index;
  obj->tio.entity->dwg = dwg;
  obj->fixedtype = DWG_TYPE_LIGHT;
  if (dwg->header.version > R_11)
    obj->type = DWG_TYPE_LIGHT;
  else
    obj->type = static_cast<BITCODE_BS>(classNumber);
  (void)classNumber;
  obj->dxfname = (dwg->opts & DWG_OPTS_IN) ? _strdup("LIGHT") : const_cast<char*>("LIGHT");
  obj->name = (dwg->opts & DWG_OPTS_IN) ? _strdup("LIGHT") : const_cast<char*>("LIGHT");
  auto* light = static_cast<Dwg_Entity_LIGHT*>(std::calloc(1, sizeof(Dwg_Entity_LIGHT)));
  if (light == nullptr)
    return nullptr;
  obj->tio.entity->tio.LIGHT = light;
  light->parent = obj->tio.entity;
  dwg_add_entity_defaults(dwg, obj->tio.entity);
  obj->tio.entity->ownerhandle = dwg_add_handleref(dwg, 5, blkobj->handle.value, obj);
  dwg_set_next_objhandle(obj);
  dwg_insert_entity(blkhdr, obj);
  (void)dwg_setup_LIGHT(obj);

  const std::string lightName = src.name.empty() ? "GoSurveyLight" : src.name;
  light->class_version = 1;
  light->name = dwg_add_u8_input(dwg, lightName.c_str());
  light->type = static_cast<BITCODE_BL>(std::clamp(src.type, 1u, 3u));
  light->status = src.on ? 1 : 0;
  SetCmcRgb24(&light->light_color, src.colorRgb24);
  light->plot_glyph = 1;
  light->intensity = src.intensity;
  light->position.x = src.posX;
  light->position.y = src.posY;
  light->position.z = src.posZ;
  light->target.x = src.targetX;
  light->target.y = src.targetY;
  light->target.z = src.targetZ;
  light->hotspot_angle = src.hotspotAngle;
  light->falloff_angle = src.falloffAngle;
  light->cast_shadows = 1;
  light->shadow_type = 1;
  return obj;
}

[[nodiscard]] Dwg_Object* AppendSunObject(Dwg_Data* dwg, const CadDwgImportedSun& src) {
  assert(dwg != nullptr);
  const int classNumber = EnsureSunObjectClass(dwg);
  if (classNumber < 0)
    return nullptr;
  const BITCODE_BL idx = dwg->num_objects;
  if (dwg_add_object(dwg) < 0)
    dwg_resolve_objectrefs_silent(dwg);
  Dwg_Object* obj = &dwg->object[idx];
  obj->supertype = DWG_SUPERTYPE_OBJECT;
  obj->fixedtype = DWG_TYPE_SUN;
  obj->type = static_cast<BITCODE_BS>(classNumber);
  obj->dxfname = (dwg->opts & DWG_OPTS_IN) ? _strdup("SUN") : const_cast<char*>("SUN");
  obj->name = (dwg->opts & DWG_OPTS_IN) ? _strdup("SUN") : const_cast<char*>("SUN");
  obj->tio.object = static_cast<Dwg_Object_Object*>(std::calloc(1, sizeof(Dwg_Object_Object)));
  if (obj->tio.object == nullptr)
    return nullptr;
  obj->tio.object->objid = obj->index;
  obj->tio.object->dwg = dwg;
  auto* sun = static_cast<Dwg_Object_SUN*>(std::calloc(1, sizeof(Dwg_Object_SUN)));
  if (sun == nullptr)
    return nullptr;
  obj->tio.object->tio.SUN = sun;
  sun->parent = obj->tio.object;
  dwg_set_next_objhandle(obj);
  (void)dwg_setup_SUN(obj);
  sun->class_version = 1;
  sun->is_on = src.on ? 1 : 0;
  SetCmcRgb24(&sun->color, src.colorRgb24);
  sun->intensity = src.intensity;
  sun->has_shadow = src.hasShadow ? 1 : 0;
  sun->julian_day = static_cast<BITCODE_BL>(src.julianDay);
  sun->msecs = static_cast<BITCODE_BL>(src.msecs);
  sun->is_dst = src.isDst ? 1 : 0;
  sun->shadow_type = 1;
  sun->shadow_mapsize = 256;
  sun->shadow_softness = 1;
  return obj;
}

[[nodiscard]] Dwg_Object* AcadLightListDictionary(Dwg_Data* dwg) {
  assert(dwg != nullptr);
  BITCODE_H ctrl = dwg->header_vars.DICTIONARY_LIGHTLIST;
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    ctrl = dwg_find_dictionary(dwg, "ACAD_LIGHTLIST");
  if (ctrl != nullptr && ctrl->absolute_ref != 0) {
    Dwg_Object* dictObj = dwg_resolve_handle_silent(dwg, ctrl->absolute_ref);
    if (dictObj != nullptr && dictObj->fixedtype == DWG_TYPE_DICTIONARY)
      return dictObj;
  }
  if (dwg_add_DICTIONARY(dwg, "ACAD_LIGHTLIST", nullptr, 0) == nullptr)
    return nullptr;
  ctrl = dwg_find_dictionary(dwg, "ACAD_LIGHTLIST");
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    return nullptr;
  dwg->header_vars.DICTIONARY_LIGHTLIST = ctrl;
  return dwg_resolve_handle_silent(dwg, ctrl->absolute_ref);
}

void AddLightListToDictionary(Dwg_Data* dwg, Dwg_Object* listObj, const char* dictKey) {
  assert(dwg != nullptr && listObj != nullptr && dictKey != nullptr);
  Dwg_Object* dictObj = AcadLightListDictionary(dwg);
  if (dictObj == nullptr || dictObj->tio.object == nullptr ||
      dictObj->tio.object->tio.DICTIONARY == nullptr)
    return;
  dwg_add_DICTIONARY_item(dictObj->tio.object->tio.DICTIONARY, dictKey, listObj->handle.value);
}

[[nodiscard]] Dwg_Object* AppendLightListObject(
    Dwg_Data* dwg, unsigned classVersion,
    const std::vector<std::pair<std::string, Dwg_Object*>>& namedLights) {
  assert(dwg != nullptr);
  if (namedLights.empty())
    return nullptr;
  const int classNumber = EnsureLightListClass(dwg);
  if (classNumber < 0)
    return nullptr;
  const BITCODE_BL idx = dwg->num_objects;
  if (dwg_add_object(dwg) < 0)
    dwg_resolve_objectrefs_silent(dwg);
  Dwg_Object* obj = &dwg->object[idx];
  obj->supertype = DWG_SUPERTYPE_OBJECT;
  obj->fixedtype = DWG_TYPE_LIGHTLIST;
  obj->type = static_cast<BITCODE_BS>(classNumber);
  obj->dxfname = (dwg->opts & DWG_OPTS_IN) ? _strdup("LIGHTLIST") : const_cast<char*>("LIGHTLIST");
  obj->name = (dwg->opts & DWG_OPTS_IN) ? _strdup("LIGHTLIST") : const_cast<char*>("LIGHTLIST");
  obj->tio.object = static_cast<Dwg_Object_Object*>(std::calloc(1, sizeof(Dwg_Object_Object)));
  if (obj->tio.object == nullptr)
    return nullptr;
  obj->tio.object->objid = obj->index;
  obj->tio.object->dwg = dwg;
  auto* list = static_cast<Dwg_Object_LIGHTLIST*>(std::calloc(1, sizeof(Dwg_Object_LIGHTLIST)));
  if (list == nullptr)
    return nullptr;
  obj->tio.object->tio.LIGHTLIST = list;
  list->parent = obj->tio.object;
  dwg_set_next_objhandle(obj);
  list->class_version = static_cast<BITCODE_BL>(std::max(classVersion, 1u));
  list->num_lights = static_cast<BITCODE_BL>(namedLights.size());
  list->lights = static_cast<Dwg_LIGHTLIST_light*>(std::calloc(namedLights.size(), sizeof(Dwg_LIGHTLIST_light)));
  if (list->lights == nullptr)
    return nullptr;
  for (size_t i = 0; i < namedLights.size(); ++i) {
    Dwg_Object* lightObj = namedLights[i].second;
    if (lightObj == nullptr)
      continue;
    list->lights[i].parent = list;
    std::string regName = namedLights[i].first;
    if (regName.empty() && lightObj->tio.entity != nullptr && lightObj->tio.entity->tio.LIGHT != nullptr &&
        lightObj->tio.entity->tio.LIGHT->name != nullptr) {
      const bool utf16Name = dwg->header.from_version >= R_2007 && !(dwg->opts & DWG_OPTS_IN);
      regName = libredwgcad_detail::DecodeDwgString(lightObj->tio.entity->tio.LIGHT->name, utf16Name);
      if (regName.empty())
        regName = libredwgcad_detail::DecodeDwgString(lightObj->tio.entity->tio.LIGHT->name, false);
    }
    if (regName.empty())
      regName = "Light";
    list->lights[i].name = dwg_add_u8_input(dwg, regName.c_str());
    list->lights[i].handle = dwg_add_handleref(dwg, 5, lightObj->handle.value, obj);
  }
  return obj;
}

void FindLightListDictionaryKey(Dwg_Data* dwg, BITCODE_RLL listHandleAbs, std::string& outKey) {
  assert(dwg != nullptr);
  outKey.clear();
  if (listHandleAbs == 0)
    return;
  BITCODE_H ctrl = dwg->header_vars.DICTIONARY_LIGHTLIST;
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    ctrl = dwg_find_dictionary(dwg, "ACAD_LIGHTLIST");
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    return;
  Dwg_Object* dictObj = dwg_resolve_handle_silent(dwg, ctrl->absolute_ref);
  if (dictObj == nullptr || dictObj->fixedtype != DWG_TYPE_DICTIONARY ||
      dictObj->tio.object == nullptr || dictObj->tio.object->tio.DICTIONARY == nullptr)
    return;
  const Dwg_Object_DICTIONARY* dict = dictObj->tio.object->tio.DICTIONARY;
  const bool utf16 = dwg->header.from_version >= R_2007 && !(dwg->opts & DWG_OPTS_IN);
  for (BITCODE_BL i = 0; i < dict->numitems; ++i) {
    if (dict->itemhandles[i] == nullptr || dict->itemhandles[i]->absolute_ref != listHandleAbs)
      continue;
    if (dict->texts[i] != nullptr)
      outKey = libredwgcad_detail::DecodeDwgString(dict->texts[i], utf16);
    break;
  }
}

[[nodiscard]] CadDwgImportedLight LightFromEntity(const Dwg_Data* dwg, const Dwg_Entity_LIGHT* light) {
  assert(dwg != nullptr && light != nullptr);
  CadDwgImportedLight out;
  const bool utf16Name = dwg->header.from_version >= R_2007 && !(dwg->opts & DWG_OPTS_IN);
  if (light->name != nullptr) {
    out.name = libredwgcad_detail::DecodeDwgString(light->name, utf16Name);
    if (out.name.empty())
      out.name = libredwgcad_detail::DecodeDwgString(light->name, false);
  }
  out.type = static_cast<unsigned>(std::max<BITCODE_BL>(light->type, 1));
  out.on = light->status != 0;
  out.colorRgb24 = Rgb24FromCmc(light->light_color);
  out.intensity = light->intensity;
  out.posX = light->position.x;
  out.posY = light->position.y;
  out.posZ = light->position.z;
  out.targetX = light->target.x;
  out.targetY = light->target.y;
  out.targetZ = light->target.z;
  out.hotspotAngle = light->hotspot_angle;
  out.falloffAngle = light->falloff_angle;
  return out;
}

[[nodiscard]] CadDwgImportedSun SunFromObject(const Dwg_Object_SUN* sun) {
  assert(sun != nullptr);
  CadDwgImportedSun out;
  out.on = sun->is_on != 0;
  out.colorRgb24 = Rgb24FromCmc(sun->color);
  out.intensity = sun->intensity;
  out.hasShadow = sun->has_shadow != 0;
  out.julianDay = static_cast<unsigned>(sun->julian_day);
  out.msecs = static_cast<unsigned>(sun->msecs);
  out.isDst = sun->is_dst != 0;
  return out;
}

[[nodiscard]] int CountLightsImpl(const Dwg_Data* dwg) {
  assert(dwg != nullptr);
  int n = 0;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    if (dwg->object[i].fixedtype == DWG_TYPE_LIGHT)
      ++n;
  }
  return n;
}

[[nodiscard]] int CountSunsImpl(const Dwg_Data* dwg) {
  assert(dwg != nullptr);
  int n = 0;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    if (dwg->object[i].fixedtype == DWG_TYPE_SUN)
      ++n;
  }
  return n;
}

[[nodiscard]] int CountLightListsImpl(const Dwg_Data* dwg) {
  assert(dwg != nullptr);
  int n = 0;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    if (dwg->object[i].fixedtype == DWG_TYPE_LIGHTLIST)
      ++n;
  }
  return n;
}

[[nodiscard]] Dwg_Object* FindLightObjectByName(const std::vector<std::pair<std::string, Dwg_Object*>>& byName,
                                                const std::string& name) {
  for (const auto& pair : byName) {
    if (pair.first == name)
      return pair.second;
  }
  return nullptr;
}

void CaptureLightListRegistry(Dwg_Data* dwg, AppCommandState& st) {
  assert(dwg != nullptr);
  st.dwgImportedLightList = {};
  st.dwgImportedLightListPresent = false;
  st.dwgImportedLightListDictKey.clear();
  Dwg_Object* firstListObj = nullptr;
  const bool utf16Name = dwg->header.from_version >= R_2007 && !(dwg->opts & DWG_OPTS_IN);
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    Dwg_Object* obj = &dwg->object[i];
    if (obj->fixedtype != DWG_TYPE_LIGHTLIST || obj->tio.object == nullptr ||
        obj->tio.object->tio.LIGHTLIST == nullptr)
      continue;
    ++gStats.lightLists;
    const Dwg_Object_LIGHTLIST* list = obj->tio.object->tio.LIGHTLIST;
    if (!st.dwgImportedLightListPresent) {
      st.dwgImportedLightListPresent = true;
      st.dwgImportedLightList.classVersion =
          static_cast<unsigned>(std::max<BITCODE_BL>(list->class_version, 1));
      firstListObj = obj;
    }
    for (BITCODE_BL j = 0; j < list->num_lights; ++j) {
      CadDwgImportedLightListEntry entry;
      if (list->lights != nullptr && list->lights[j].name != nullptr) {
        entry.name = libredwgcad_detail::DecodeDwgString(list->lights[j].name, utf16Name);
        if (entry.name.empty())
          entry.name = libredwgcad_detail::DecodeDwgString(list->lights[j].name, false);
      }
      st.dwgImportedLightList.entries.push_back(std::move(entry));
      ++gStats.lightListEntries;
    }
  }
  if (firstListObj != nullptr) {
    FindLightListDictionaryKey(dwg, firstListObj->handle.value, st.dwgImportedLightListDictKey);
    if (st.dwgImportedLightListDictKey.empty())
      st.dwgImportedLightListDictKey = "Default";
  }
}

[[nodiscard]] std::vector<std::pair<std::string, Dwg_Object*>> BuildRegistryLights(
    const AppCommandState& st, const std::vector<std::pair<std::string, Dwg_Object*>>& lightsByName,
    const std::vector<Dwg_Object*>& lightsInOrder) {
  std::vector<std::pair<std::string, Dwg_Object*>> out;
  if (st.dwgImportedLightListPresent && !st.dwgImportedLightList.entries.empty()) {
    size_t fallbackIndex = 0;
    for (const CadDwgImportedLightListEntry& entry : st.dwgImportedLightList.entries) {
      Dwg_Object* lightObj = nullptr;
      if (!entry.name.empty())
        lightObj = FindLightObjectByName(lightsByName, entry.name);
      if (lightObj == nullptr && fallbackIndex < lightsInOrder.size())
        lightObj = lightsInOrder[fallbackIndex];
      if (lightObj == nullptr)
        continue;
      ++fallbackIndex;
      std::string regName = entry.name;
      if (regName.empty() && lightObj->tio.entity != nullptr && lightObj->tio.entity->tio.LIGHT != nullptr &&
          lightObj->tio.entity->tio.LIGHT->name != nullptr)
        regName = lightObj->tio.entity->tio.LIGHT->name;  // UTF-8 test helper / export buffer
      out.emplace_back(std::move(regName), lightObj);
    }
    return out;
  }
  for (size_t i = 0; i < lightsInOrder.size(); ++i) {
    std::string regName;
    if (i < st.dwgImportedLights.size() && !st.dwgImportedLights[i].name.empty())
      regName = st.dwgImportedLights[i].name;
    else if (lightsInOrder[i]->tio.entity != nullptr && lightsInOrder[i]->tio.entity->tio.LIGHT != nullptr &&
             lightsInOrder[i]->tio.entity->tio.LIGHT->name != nullptr)
      regName = lightsInOrder[i]->tio.entity->tio.LIGHT->name;  // UTF-8 from dwg_add_u8_input
    out.emplace_back(std::move(regName), lightsInOrder[i]);
  }
  return out;
}

}  // namespace

void DwgLightImportBegin() { gStats = ImportStats{}; }

void DwgLightImportCapture(_dwg_struct* dwgIn, AppCommandState& st) {
  assert(dwgIn != nullptr);
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  if (dwg->header.version < R_2007)
    return;
  st.dwgImportedLights.clear();
  st.dwgImportedSunPresent = false;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    Dwg_Object* obj = &dwg->object[i];
    if (obj->fixedtype == DWG_TYPE_LIGHT && obj->tio.entity != nullptr &&
        obj->tio.entity->tio.LIGHT != nullptr) {
      st.dwgImportedLights.push_back(LightFromEntity(dwg, obj->tio.entity->tio.LIGHT));
      ++gStats.lights;
    }
    if (obj->fixedtype == DWG_TYPE_SUN && obj->tio.object != nullptr &&
        obj->tio.object->tio.SUN != nullptr) {
      st.dwgImportedSun = SunFromObject(obj->tio.object->tio.SUN);
      st.dwgImportedSunPresent = true;
      ++gStats.suns;
    }
  }
  CaptureLightListRegistry(dwg, st);
}

void DwgLightImportAppendLog(std::vector<std::string>& log) {
  if (gStats.lights <= 0 && gStats.suns <= 0 && gStats.lightLists <= 0)
    return;
  if (gStats.lights > 0) {
    log.push_back("DWG import — " + std::to_string(gStats.lights) +
                  " LIGHT entit" + std::string(gStats.lights == 1 ? "y" : "ies") +
                  " preserved for DWG export (REQ-385; not used in GoSurvey shading).");
  }
  if (gStats.suns > 0) {
    log.push_back("DWG import — " + std::to_string(gStats.suns) +
                  " SUN object(s) preserved for DWG export (REQ-385).");
  }
  if (gStats.lightLists > 0) {
    log.push_back("DWG import — " + std::to_string(gStats.lightLists) + " LIGHTLIST object(s) with " +
                  std::to_string(gStats.lightListEntries) + " registered light(s) preserved (REQ-386, issue #715).");
  }
}

void DwgExportImportedLightsAndSun(const AppCommandState& st, _dwg_struct* dwgIn, void* modelSpaceBlockIn,
                                   std::vector<std::string>& log) {
  assert(dwgIn != nullptr && modelSpaceBlockIn != nullptr);
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  auto* modelSpaceBlock = static_cast<Dwg_Object_BLOCK_HEADER*>(modelSpaceBlockIn);
  if (!ExportVersionSupportsLights(st.dwgExportVersion))
    return;
  if (st.dwgImportedLights.empty() && !st.dwgImportedSunPresent)
    return;
  int lightsWritten = 0;
  std::vector<std::pair<std::string, Dwg_Object*>> lightsByName;
  std::vector<Dwg_Object*> lightsInOrder;
  for (const CadDwgImportedLight& light : st.dwgImportedLights) {
    Dwg_Object* written = AppendLightEntityShell(dwg, modelSpaceBlock, light);
    if (written != nullptr) {
      ++lightsWritten;
      lightsInOrder.push_back(written);
      if (!light.name.empty())
        lightsByName.emplace_back(light.name, written);
    }
  }
  if (st.dwgImportedSunPresent)
    (void)AppendSunObject(dwg, st.dwgImportedSun);
  int lightListsWritten = 0;
  if (lightsWritten > 0) {
    const std::vector<std::pair<std::string, Dwg_Object*>> registry =
        BuildRegistryLights(st, lightsByName, lightsInOrder);
    if (!registry.empty()) {
      const unsigned classVersion =
          st.dwgImportedLightListPresent ? st.dwgImportedLightList.classVersion : 1u;
      Dwg_Object* listObj = AppendLightListObject(dwg, classVersion, registry);
      if (listObj != nullptr) {
        const char* dictKey =
            st.dwgImportedLightListDictKey.empty() ? "Default" : st.dwgImportedLightListDictKey.c_str();
        AddLightListToDictionary(dwg, listObj, dictKey);
        ++lightListsWritten;
      }
    }
  }
  if (lightsWritten > 0 || st.dwgImportedSunPresent || lightListsWritten > 0) {
    dwg_resolve_objectrefs_silent(dwg);
    std::string line = "CAD export — wrote " + std::to_string(lightsWritten) + " LIGHT entit" +
                       std::string(lightsWritten == 1 ? "y" : "ies");
    if (st.dwgImportedSunPresent)
      line += " and SUN";
    if (lightListsWritten > 0)
      line += " and LIGHTLIST registry";
    line += " (REQ-385";
    if (lightListsWritten > 0)
      line += "/REQ-386";
    line += ", issue #624";
    if (lightListsWritten > 0)
      line += "/#715";
    line += ").";
    log.push_back(std::move(line));
  }
}

int DwgExportCountLightSunLosses(const AppCommandState& st) {
  if (ExportVersionSupportsLights(st.dwgExportVersion))
    return 0;
  int n = static_cast<int>(st.dwgImportedLights.size()) + (st.dwgImportedSunPresent ? 1 : 0);
  if (st.dwgImportedLightListPresent)
    ++n;
  return n;
}

int DwgLightCountEntities(const _dwg_struct* dwgIn) {
  assert(dwgIn != nullptr);
  return CountLightsImpl(reinterpret_cast<const Dwg_Data*>(dwgIn));
}

int DwgSunCountObjects(const _dwg_struct* dwgIn) {
  assert(dwgIn != nullptr);
  return CountSunsImpl(reinterpret_cast<const Dwg_Data*>(dwgIn));
}

int DwgLightListCountObjects(const _dwg_struct* dwgIn) {
  assert(dwgIn != nullptr);
  return CountLightListsImpl(reinterpret_cast<const Dwg_Data*>(dwgIn));
}

int DwgLightListRegistryEntryCount(const _dwg_struct* dwgIn) {
  assert(dwgIn != nullptr);
  const auto* dwg = reinterpret_cast<const Dwg_Data*>(dwgIn);
  int total = 0;
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    const Dwg_Object* obj = &dwg->object[i];
    if (obj->fixedtype != DWG_TYPE_LIGHTLIST || obj->tio.object == nullptr ||
        obj->tio.object->tio.LIGHTLIST == nullptr)
      continue;
    total += static_cast<int>(obj->tio.object->tio.LIGHTLIST->num_lights);
  }
  return total;
}

bool DwgTestAddPointLight(_dwg_struct* dwgIn, void* modelSpaceBlockIn, const char* name, double x,
                          double y, double z) {
  assert(dwgIn != nullptr && modelSpaceBlockIn != nullptr && name != nullptr);
  auto* modelSpaceBlock = static_cast<Dwg_Object_BLOCK_HEADER*>(modelSpaceBlockIn);
  CadDwgImportedLight light;
  light.name = name;
  light.type = 2;
  light.on = true;
  light.colorRgb24 = 0xFFFFFFu;
  light.intensity = 1.0;
  light.posX = x;
  light.posY = y;
  light.posZ = z;
  light.targetX = x;
  light.targetY = y + 1.0;
  light.targetZ = z;
  return AppendLightEntityShell(reinterpret_cast<Dwg_Data*>(dwgIn), modelSpaceBlock, light) != nullptr;
}

bool DwgTestAddLightListForLights(_dwg_struct* dwgIn, const char* dictKey) {
  assert(dwgIn != nullptr && dictKey != nullptr);
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  std::vector<std::pair<std::string, Dwg_Object*>> registry;
  const bool utf16Name = dwg->header.from_version >= R_2007 && !(dwg->opts & DWG_OPTS_IN);
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    Dwg_Object* obj = &dwg->object[i];
    if (obj->fixedtype != DWG_TYPE_LIGHT || obj->tio.entity == nullptr ||
        obj->tio.entity->tio.LIGHT == nullptr)
      continue;
    std::string regName;
    if (obj->tio.entity->tio.LIGHT->name != nullptr) {
      regName = libredwgcad_detail::DecodeDwgString(obj->tio.entity->tio.LIGHT->name, utf16Name);
      if (regName.empty())
        regName = libredwgcad_detail::DecodeDwgString(obj->tio.entity->tio.LIGHT->name, false);
    }
    registry.emplace_back(std::move(regName), obj);
  }
  if (registry.empty())
    return false;
  Dwg_Object* listObj = AppendLightListObject(dwg, 1u, registry);
  if (listObj == nullptr || listObj->tio.object == nullptr ||
      listObj->tio.object->tio.LIGHTLIST == nullptr ||
      listObj->tio.object->tio.LIGHTLIST->num_lights == 0)
    return false;
  AddLightListToDictionary(dwg, listObj, dictKey);
  dwg_resolve_objectrefs_silent(dwg);
  return true;
}

void DwgLightImportScanForTests(_dwg_struct* dwgIn, AppCommandState& st) {
  DwgLightImportBegin();
  DwgLightImportCapture(dwgIn, st);
}
