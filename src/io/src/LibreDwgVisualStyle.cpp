#include "LibreDwgVisualStyle.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
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
extern "C" const unsigned char _dwg_VISUALSTYLE_proptypes[58];

namespace {

constexpr BITCODE_BL kStyle2DWireframe = 4;
constexpr BITCODE_BL kStyleHidden = 6;
// AutoCAD's native "Shaded" record uses enum value 27. 26 is "Shaded with
// edges" in the VisualStyle class and AutoCAD rejects it on a paper viewport.
constexpr BITCODE_BL kStyleShaded = 27;

const char* StyleTypeName(BITCODE_BL styleType) {
  if (styleType == kStyleShaded)
    return "Shaded";
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

void NormalizeVisualStyleClasses(Dwg_Data* dwg) {
  if (dwg->dwg_class == nullptr || dwg->num_classes == 0)
    return;

  BITCODE_BS canonicalNumber = 0;
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* name = dwg->dwg_class[i].dxfname;
    if (name != nullptr && std::strcmp(name, "VISUALSTYLE") == 0) {
      canonicalNumber = dwg->dwg_class[i].number;
      break;
    }
  }
  if (canonicalNumber == 0)
    return;

  for (BITCODE_BS i = 0; i < dwg->num_classes;) {
    const char* name = dwg->dwg_class[i].dxfname;
    if (dwg->dwg_class[i].number == canonicalNumber || name == nullptr ||
        std::strcmp(name, "VISUALSTYLE") != 0) {
      ++i;
      continue;
    }

    const BITCODE_BS removedNumber = dwg->dwg_class[i].number;
    Dwg_Class& removed = dwg->dwg_class[i];
    std::free(removed.appname);
    std::free(removed.cppname);
    std::free(removed.dxfname);
    if (dwg->header.from_version >= R_2007)
      std::free(removed.dxfname_u);

    for (BITCODE_BL objectIndex = 0; objectIndex < dwg->num_objects; ++objectIndex) {
      Dwg_Object& object = dwg->object[objectIndex];
      if (object.type == removedNumber)
        object.type = canonicalNumber;
      else if (object.type > removedNumber)
        --object.type;
    }
    for (BITCODE_BS classIndex = i + 1; classIndex < dwg->num_classes; ++classIndex)
      --dwg->dwg_class[classIndex].number;

    std::memmove(&dwg->dwg_class[i], &dwg->dwg_class[i + 1],
                 (dwg->num_classes - i - 1) * sizeof(Dwg_Class));
    --dwg->num_classes;
  }

  BITCODE_BL visualStyleInstances = 0;
  for (BITCODE_BL objectIndex = 0; objectIndex < dwg->num_objects; ++objectIndex) {
    Dwg_Object& object = dwg->object[objectIndex];
    if (object.fixedtype == DWG_TYPE_VISUALSTYLE)
      ++visualStyleInstances;
    if (object.type >= 500 && object.type - 500 < dwg->num_classes)
      object.klass = &dwg->dwg_class[object.type - 500];
  }
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    if (dwg->dwg_class[i].number == canonicalNumber) {
      dwg->dwg_class[i].num_instances = visualStyleInstances;
      break;
    }
  }
}

int EnsureVisualStyleClass(Dwg_Data* dwg) {
  assert(dwg != nullptr);
  NormalizeVisualStyleClasses(dwg);
  if (dwg->dwg_class == nullptr)
    return -1;
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* name = dwg->dwg_class[i].dxfname;
    if (name != nullptr && std::strcmp(name, "VISUALSTYLE") == 0) {
      // The default drawing can carry a legacy VISUALSTYLE class record with
      // zero schema versions. Reuse its class number and give it the R2018
      // class version; a second class with the same DXF name makes AutoCAD
      // treat viewport visual-style handles as the wrong type.
      dwg->dwg_class[i].proxyflag = 0x0fff;
      dwg->dwg_class[i].dwg_version = 26;
      dwg->dwg_class[i].maint_version = 0;
      return static_cast<int>(dwg->dwg_class[i].number);
    }
  }
  // The encoder can infer the stable class from an existing VISUALSTYLE
  // object before it materializes that class in dwg_class. Reuse that class
  // number so newly appended styles do not create a duplicate DXF class.
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    const Dwg_Object& object = dwg->object[i];
    if (object.fixedtype == DWG_TYPE_VISUALSTYLE && object.type >= 500)
      return static_cast<int>(object.type);
  }
  const int number =
      dwg_add_class(dwg, "VISUALSTYLE", "AcDbVisualStyle", "ObjectDBX Classes", false);
  if (number >= 500) {
    const BITCODE_BL index = static_cast<BITCODE_BL>(number - 500);
    if (index < dwg->num_classes) {
      dwg->dwg_class[index].proxyflag = 0x0fff;
      dwg->dwg_class[index].dwg_version = 26;
      dwg->dwg_class[index].maint_version = 0;
    }
  }
  return number;
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
  obj->tio.object->is_xdic_missing = 1;
  auto* vs = static_cast<Dwg_Object_VISUALSTYLE*>(std::calloc(1, sizeof(Dwg_Object_VISUALSTYLE)));
  if (vs == nullptr)
    return nullptr;
  obj->tio.object->tio.VISUALSTYLE = vs;
  vs->parent = obj->tio.object;
  dwg_set_next_objhandle(obj);
  vs->style_type = styleType;
  // VISUALSTYLE.description is a TU field in R2007+ DWG objects.
  vs->description = dwg_add_u8_input(dwg, StyleTypeName(styleType));
  vs->num_props = 58;
  vs->ext_lighting_model = 2;
  vs->face_lighting_model_int = 1;
  vs->face_lighting_quality_int = 1;
  vs->face_color_mode_int = 1;
  vs->face_modifier_int = 1;
  vs->face_opacity_int = 1;
  vs->face_specular_int = 1;
  vs->face_mono_color_int = 1;
  vs->edge_model_int = 1;
  vs->edge_style_int = 1;
  vs->edge_intersection_color_int = 1;
  vs->edge_obscured_color_int = 1;
  vs->edge_obscured_ltype_int = 1;
  vs->edge_intersection_ltype_int = 1;
  vs->edge_crease_angle_int = 1;
  vs->edge_modifier_int = 1;
  vs->edge_color_int = 1;
  vs->edge_opacity_int = 1;
  vs->edge_width_int = 1;
  vs->edge_overhang_int = 1;
  vs->edge_jitter_int = 1;
  vs->edge_silhouette_color_int = 1;
  vs->edge_silhouette_width_int = 1;
  vs->edge_halo_gap_int = 1;
  vs->edge_isolines_int = 1;
  vs->edge_do_hide_precision_int = 1;
  vs->display_settings_int = 1;
  vs->display_brightness_int = 1;
  vs->display_shadow_type_int = 1;
  BITCODE_BS* propTypes[] = {
      &vs->b_prop1c_int, &vs->b_prop1d_int, &vs->b_prop1e_int, &vs->b_prop1f_int,
      &vs->b_prop20_int, &vs->b_prop21_int, &vs->b_prop22_int, &vs->b_prop23_int,
      &vs->b_prop24_int, &vs->bl_prop25_int, &vs->bd_prop26_int, &vs->bd_prop27_int,
      &vs->bl_prop28_int, &vs->c_prop29_int, &vs->bl_prop2a_int, &vs->bl_prop2b_int,
      &vs->c_prop2c_int, &vs->b_prop2d_int, &vs->bl_prop2e_int, &vs->bl_prop2f_int,
      &vs->bl_prop30_int, &vs->b_prop31_int, &vs->bl_prop32_int, &vs->c_prop33_int,
      &vs->bd_prop34_int, &vs->edge_wiggle_int, &vs->strokes_int, &vs->b_prop37_int,
      &vs->bd_prop38_int, &vs->bd_prop39_int};
  static_assert(sizeof(propTypes) / sizeof(propTypes[0]) == 30);
  // LibreDWG models the R2013+ VisualStyle extension with 58 typed slots.
  // The named fields above cover the final 30 slots; the first 28 are the
  // schema's compact fields that share its dynapi storage.
  for (std::size_t i = 0; i < sizeof(propTypes) / sizeof(propTypes[0]); ++i)
    *propTypes[i] = _dwg_VISUALSTYLE_proptypes[i + 28];
  // Match AutoCAD's stock properties for the styles GoSurvey currently
  // exports. A zero-filled record is not a valid native VisualStyle even
  // though LibreDWG can read it back as one.
  vs->face_lighting_model = styleType == kStyle2DWireframe ? 0 : 2;
  vs->face_lighting_quality = 2;
  vs->face_color_mode = styleType == kStyle2DWireframe ? 0 : 1;
  vs->face_opacity = 0.6;
  vs->face_specular = 30.0;
  vs->face_modifier = styleType == kStyle2DWireframe ? 0 : 2;
  if (styleType == kStyleShaded) {
    // Match the color method and payloads read from AutoCAD's stock R2018
    // Shaded object. CMC colors encode their method in the high byte of rgb.
    vs->face_mono_color.index = 7;
    vs->face_mono_color.method = DWG_COLOR_METHOD_ACI;
    vs->face_mono_color.rgb = 0x00FFFFFFu;
    vs->edge_intersection_color.index = 256;
    vs->edge_intersection_color.method = DWG_COLOR_METHOD_TRUECOLOR;
    vs->edge_intersection_color.rgb = 0x00000007u;
    vs->edge_obscured_color.index = 0;
    vs->edge_obscured_color.method = DWG_COLOR_METHOD_NONE;
    vs->edge_color.index = 0;
    vs->edge_color.method = DWG_COLOR_METHOD_NONE;
    vs->edge_silhouette_color.index = 256;
    vs->edge_silhouette_color.method = DWG_COLOR_METHOD_ACI;
    vs->edge_silhouette_color.rgb = 0x00787878u;
  }
  vs->edge_model = styleType == kStyleShaded ? 0 : 1;
  vs->edge_style = styleType == kStyleShaded ? 4 : (styleType == kStyle2DWireframe ? 4 : 2);
  vs->edge_obscured_ltype = 1;
  vs->edge_intersection_ltype = 1;
  vs->edge_crease_angle = 1.0;
  vs->edge_modifier = styleType == kStyleShaded ? 8 : 0;
  vs->edge_opacity = 1.0;
  vs->edge_width = 1;
  vs->edge_overhang = 6;
  vs->edge_jitter = 2;
  vs->edge_silhouette_width = styleType == kStyle2DWireframe ? 5 : 3;
  vs->display_settings = styleType == kStyleShaded ? 5 : 1;
  vs->display_brightness = 0.0;
  vs->display_shadow_type = 0;
  vs->bd2007_45 = 0.0;
  return obj;
}

void AddVisualStyleToDictionary(Dwg_Data* dwg, Dwg_Object* vsObj) {
  assert(dwg != nullptr && vsObj != nullptr);
  BITCODE_H ctrl = dwg->header_vars.DICTIONARY_VISUALSTYLE;
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    ctrl = dwg_find_dictionary(dwg, "ACAD_VISUALSTYLE");
  if (ctrl == nullptr || ctrl->absolute_ref == 0) {
    Dwg_Object_DICTIONARY* dictionary =
        dwg_add_DICTIONARY(dwg, "ACAD_VISUALSTYLE", nullptr, 0);
    if (dictionary != nullptr) {
      int error = 0;
      Dwg_Object* dictionaryObject = dwg_obj_generic_to_object(dictionary, &error);
      if (dictionaryObject != nullptr && error == 0) {
        ctrl = dwg_add_handleref(dwg, 5, dictionaryObject->handle.value, nullptr);
        dwg->header_vars.DICTIONARY_VISUALSTYLE = ctrl;
      }
    }
  }
  if (ctrl == nullptr || ctrl->absolute_ref == 0)
    return;
  Dwg_Object* dictObj = dwg_resolve_handle_silent(dwg, ctrl->absolute_ref);
  if (dictObj == nullptr || dictObj->fixedtype != DWG_TYPE_DICTIONARY ||
      dictObj->tio.object == nullptr || dictObj->tio.object->tio.DICTIONARY == nullptr)
    return;
  Dwg_Object_VISUALSTYLE* vs = vsObj->tio.object->tio.VISUALSTYLE;
  if (vs == nullptr || vs->description == nullptr)
    return;
  const BITCODE_RLL dictionaryHandle = dictObj->handle.value;
  dwg_add_DICTIONARY_item(dictObj->tio.object->tio.DICTIONARY,
                          StyleTypeName(vs->style_type), vsObj->handle.value);
  // VISUALSTYLE records are owned by ACAD_VISUALSTYLE. Merely adding the
  // dictionary entry leaves a valid handle that AutoCAD still rejects as the
  // wrong object type when a VIEWPORT points at it.
  vsObj->tio.object->ownerhandle = dwg_add_handleref(dwg, 4, dictionaryHandle, vsObj);
  vsObj->tio.object->num_reactors = 1;
  vsObj->tio.object->reactors = static_cast<BITCODE_H*>(std::calloc(1, sizeof(BITCODE_H)));
  if (vsObj->tio.object->reactors != nullptr)
    vsObj->tio.object->reactors[0] = dwg_add_handleref(dwg, 4, dictionaryHandle, nullptr);
}

BITCODE_H EnsureVisualStyleHandle(Dwg_Data* dwg, VisualStyle style) {
  assert(dwg != nullptr);
  NormalizeVisualStyleClasses(dwg);
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
  BITCODE_H styleRef = ent->parent != nullptr && ent->parent->has_full_visualstyle
                           ? ent->parent->full_visualstyle
                           : ent->visualstyle;
  Dwg_Object* vsObj = VisualStyleObjectFromHandle(dwg, styleRef);
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
  BITCODE_H baseStyle = EnsureVisualStyleHandle(dwg, VisualStyle::Wireframe2D);
  BITCODE_H fullStyle = EnsureVisualStyleHandle(dwg, style);
  if (baseStyle != nullptr)
    ent->visualstyle = baseStyle;
  if (ent->parent != nullptr) {
    ent->parent->has_full_visualstyle = style != VisualStyle::Wireframe2D;
    ent->parent->full_visualstyle =
        style != VisualStyle::Wireframe2D ? fullStyle : nullptr;
  }
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
