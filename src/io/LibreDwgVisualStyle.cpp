#include "LibreDwgVisualStyle.hpp"

#include <cassert>
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

constexpr BITCODE_BL kStyle2DWireframe = 4;
constexpr BITCODE_BL kStyleHidden = 6;
constexpr BITCODE_BL kStyleShaded = 26;

const char* StyleTypeName(BITCODE_BL styleType) {
  static const char* const kNames[32] = {"Flat",
                                         "FlatWithEdges",
                                         "Gouraud",
                                         "GouraudWithEdges",
                                         "2DWireframe",
                                         "3DWireFrame",
                                         "Hidden",
                                         "Basic",
                                         "Realistic",
                                         "Conceptual",
                                         "Dim",
                                         "Brighten",
                                         "Thicken",
                                         "LinePattern",
                                         "Facepattern",
                                         "ColorChange",
                                         "FaceOnly",
                                         "EdgeOnly",
                                         "DisplayOnly",
                                         "JitterOff",
                                         "OverhangOff",
                                         "EdgeColorOff",
                                         "Shades of Gray",
                                         "Sketchy",
                                         "X-Ray",
                                         "Shaded with edges",
                                         "Shaded",
                                         "ByViewport",
                                         "ByLayer",
                                         "ByBlock",
                                         "ForEmptyStyle"};
  if (styleType >= 32)
    return "Shaded";
  return kNames[styleType];
}

BITCODE_BL StyleTypeForGoSurvey(VisualStyle style) {
  switch (style) {
  case VisualStyle::Wireframe2D:
    return kStyle2DWireframe;
  case VisualStyle::Hidden:
    return kStyleHidden;
  case VisualStyle::Shaded:
    return kStyleShaded;
  }
  return kStyle2DWireframe;
}

VisualStyle GoSurveyStyleFromType(BITCODE_BL styleType) {
  if (styleType == kStyle2DWireframe || styleType == 5)
    return VisualStyle::Wireframe2D;
  if (styleType == kStyleHidden)
    return VisualStyle::Hidden;
  return VisualStyle::Shaded;
}

VisualStyle GoSurveyStyleFromDescription(const char* desc) {
  assert(desc != nullptr);
  VisualStyle parsed = VisualStyle::Wireframe2D;
  if (VisualStyleFromName(desc, &parsed))
    return parsed;
  if (std::strstr(desc, "Wire") != nullptr || std::strstr(desc, "wire") != nullptr)
    return VisualStyle::Wireframe2D;
  if (std::strstr(desc, "Hidden") != nullptr || std::strstr(desc, "hidden") != nullptr)
    return VisualStyle::Hidden;
  return VisualStyle::Shaded;
}

VisualStyle StyleFromVisualStyleObject(const Dwg_Object_VISUALSTYLE* vs) {
  assert(vs != nullptr);
  if (vs->description != nullptr && vs->description[0] != '\0')
    return GoSurveyStyleFromDescription(vs->description);
  return GoSurveyStyleFromType(vs->style_type);
}

Dwg_Object* VisualStyleObjectFromHandle(Dwg_Data* dwg, const BITCODE_H handle) {
  assert(dwg != nullptr);
  if (handle == nullptr || handle->absolute_ref == 0)
    return nullptr;
  Dwg_Object* obj = dwg_resolve_handle_silent(dwg, handle->absolute_ref);
  if (obj == nullptr || obj->fixedtype != DWG_TYPE_VISUALSTYLE || obj->tio.object == nullptr ||
      obj->tio.object->tio.VISUALSTYLE == nullptr)
    return nullptr;
  return obj;
}

Dwg_Object* FindVisualStyleByType(Dwg_Data* dwg, BITCODE_BL styleType) {
  assert(dwg != nullptr);
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    Dwg_Object* obj = &dwg->object[i];
    if (obj->fixedtype != DWG_TYPE_VISUALSTYLE || obj->tio.object == nullptr ||
        obj->tio.object->tio.VISUALSTYLE == nullptr)
      continue;
    if (obj->tio.object->tio.VISUALSTYLE->style_type == styleType)
      return obj;
  }
  return nullptr;
}

int EnsureVisualStyleClass(Dwg_Data* dwg) {
  assert(dwg != nullptr);
  if (dwg->dwg_class == nullptr)
    return -1;
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* name = dwg->dwg_class[i].dxfname;
    if (name != nullptr && std::strcmp(name, "VISUALSTYLE") == 0)
      return static_cast<int>(dwg->dwg_class[i].number);
  }
  return dwg_add_class(dwg, "VISUALSTYLE", "AcDbVisualStyle", "ObjectDBX Classes", false);
}

Dwg_Object* AppendVisualStyleObject(Dwg_Data* dwg, BITCODE_BL styleType) {
  assert(dwg != nullptr);
  const int classNumber = EnsureVisualStyleClass(dwg);
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
  obj->fixedtype = DWG_TYPE_VISUALSTYLE;
  obj->type = static_cast<BITCODE_BS>(classNumber);
  obj->dxfname = (dwg->opts & DWG_OPTS_IN) ? _strdup("VISUALSTYLE") : const_cast<char*>("VISUALSTYLE");
  obj->name = (dwg->opts & DWG_OPTS_IN) ? _strdup("VISUALSTYLE") : const_cast<char*>("VISUALSTYLE");
  obj->tio.object = static_cast<Dwg_Object_Object*>(std::calloc(1, sizeof(Dwg_Object_Object)));
  if (obj->tio.object == nullptr)
    return nullptr;
  obj->tio.object->objid = obj->index;
  obj->tio.object->dwg = dwg;
  auto* vs = static_cast<Dwg_Object_VISUALSTYLE*>(std::calloc(1, sizeof(Dwg_Object_VISUALSTYLE)));
  if (vs == nullptr)
    return nullptr;
  obj->tio.object->tio.VISUALSTYLE = vs;
  vs->parent = obj->tio.object;
  dwg_set_next_objhandle(obj);
  vs->style_type = styleType;
  vs->description = dwg_add_u8_input(dwg, StyleTypeName(styleType));
  vs->face_lighting_model = 1;
  vs->face_color_mode = 1;
  vs->edge_model = 1;
  return obj;
}

void AddVisualStyleToDictionary(Dwg_Data* dwg, Dwg_Object* vsObj) {
  assert(dwg != nullptr && vsObj != nullptr);
  BITCODE_H ctrl = dwg->header_vars.DICTIONARY_VISUALSTYLE;
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    ctrl = dwg_find_dictionary(dwg, "ACAD_VISUALSTYLE");
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    return;
  Dwg_Object* dictObj = dwg_resolve_handle_silent(dwg, ctrl->absolute_ref);
  if (dictObj == nullptr || dictObj->fixedtype != DWG_TYPE_DICTIONARY ||
      dictObj->tio.object == nullptr || dictObj->tio.object->tio.DICTIONARY == nullptr)
    return;
  Dwg_Object_VISUALSTYLE* vs = vsObj->tio.object->tio.VISUALSTYLE;
  if (vs == nullptr || vs->description == nullptr)
    return;
  dwg_add_DICTIONARY_item(dictObj->tio.object->tio.DICTIONARY, vs->description, vsObj->handle.value);
}

BITCODE_H EnsureVisualStyleHandle(Dwg_Data* dwg, VisualStyle style) {
  assert(dwg != nullptr);
  const BITCODE_BL styleType = StyleTypeForGoSurvey(style);
  Dwg_Object* obj = FindVisualStyleByType(dwg, styleType);
  if (obj == nullptr) {
    obj = AppendVisualStyleObject(dwg, styleType);
    if (obj != nullptr)
      AddVisualStyleToDictionary(dwg, obj);
  }
  if (obj == nullptr)
    return nullptr;
  return dwg_add_handleref(dwg, 5, obj->handle.value, nullptr);
}

}  // namespace

void DwgExportVisualStyleContextInit(DwgExportVisualStyleContext* ctx, bool r2007OrNewer) {
  assert(ctx != nullptr);
  ctx->enabled = r2007OrNewer;
}

VisualStyle DwgImportVisualStyleFromViewport(_dwg_struct* dwgIn, const void* viewportEntity) {
  assert(dwgIn != nullptr);
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  const auto* ent = static_cast<const Dwg_Entity_VIEWPORT*>(viewportEntity);
  assert(ent != nullptr);
  Dwg_Object* vsObj = VisualStyleObjectFromHandle(dwg, ent->visualstyle);
  if (vsObj == nullptr)
    return VisualStyle::Wireframe2D;
  return StyleFromVisualStyleObject(vsObj->tio.object->tio.VISUALSTYLE);
}

void DwgExportSetPaperViewportVisualStyle(_dwg_struct* dwgIn, DwgExportVisualStyleContext* ctx,
                                         void* viewportEntity, VisualStyle style) {
  assert(dwgIn != nullptr && ctx != nullptr);
  if (!ctx->enabled)
    return;
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  auto* ent = static_cast<Dwg_Entity_VIEWPORT*>(viewportEntity);
  assert(ent != nullptr);
  BITCODE_H h = EnsureVisualStyleHandle(dwg, style);
  if (h != nullptr)
    ent->visualstyle = h;
}

Dwg_Object_VPORT* ActiveModelVportRecord(Dwg_Data* dwg) {
  assert(dwg != nullptr);
  BITCODE_H vportRef = dwg_find_tablehandle(dwg, "*Active", "VPORT");
  if (vportRef == nullptr || vportRef->absolute_ref == 0)
    return nullptr;
  Dwg_Object* obj = dwg_resolve_handle_silent(dwg, vportRef->absolute_ref);
  if (obj == nullptr || obj->fixedtype != DWG_TYPE_VPORT || obj->tio.object == nullptr ||
      obj->tio.object->tio.VPORT == nullptr)
    return nullptr;
  return obj->tio.object->tio.VPORT;
}

VisualStyle DwgImportModelVisualStyle(_dwg_struct* dwgIn) {
  assert(dwgIn != nullptr);
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  Dwg_Object_VPORT* vport = ActiveModelVportRecord(dwg);
  if (vport == nullptr)
    return VisualStyle::Wireframe2D;
  Dwg_Object* vsObj = VisualStyleObjectFromHandle(dwg, vport->visualstyle);
  if (vsObj == nullptr)
    return VisualStyle::Wireframe2D;
  return StyleFromVisualStyleObject(vsObj->tio.object->tio.VISUALSTYLE);
}

void DwgExportSetModelVisualStyle(_dwg_struct* dwgIn, DwgExportVisualStyleContext* ctx, VisualStyle style) {
  assert(dwgIn != nullptr && ctx != nullptr);
  if (!ctx->enabled)
    return;
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  BITCODE_H h = EnsureVisualStyleHandle(dwg, style);
  if (h == nullptr)
    return;
  if (Dwg_Object_VPORT* vport = ActiveModelVportRecord(dwg))
    vport->visualstyle = h;
  dwg->header_vars.DRAGVS = dwg_add_handleref(dwg, 5, h->absolute_ref, nullptr);
}
