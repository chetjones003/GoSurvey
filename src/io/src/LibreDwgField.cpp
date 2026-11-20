#include "LibreDwgField.hpp"

#include "CadField.hpp"
#include "CadCommands.hpp"

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

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

std::string FormatHandleHex(std::uint64_t handle) {
  char buf[32];
  const int n = std::snprintf(buf, sizeof(buf), "%" PRIX64, static_cast<unsigned long long>(handle));
  if (n <= 0)
    return "0";
  return std::string(buf, static_cast<size_t>(n));
}

std::string AcdbClassForEntityRef(const AppCommandState& st, std::uint64_t entityId) {
  const EntityRef ref = FindEntityById(st, entityId);
  if (!ref.valid())
    return {};
  if (ref.kind == EntityKind::Polyline)
    return "AcDbPolyline";
  if (ref.kind == EntityKind::Circle)
    return "AcDbCircle";
  return {};
}

std::string AcadFieldFormat(std::string_view goSurveyFormat, std::string_view prop) {
  if (goSurveyFormat == ".2f" && (prop == "Area" || prop == "AREA"))
    return "%lu2";
  if (goSurveyFormat == ".1f")
    return "%lu1";
  if (goSurveyFormat.empty())
    return "%lu2";
  return std::string(goSurveyFormat);
}

int EnsureObjectClass(Dwg_Data* dwg, const char* dxfname, const char* cppname) {
  assert(dwg != nullptr && dxfname != nullptr && cppname != nullptr);
  if (dwg->dwg_class == nullptr)
    return -1;
  for (BITCODE_BS i = 0; i < dwg->num_classes; ++i) {
    const char* name = dwg->dwg_class[i].dxfname;
    if (name != nullptr && std::strcmp(name, dxfname) == 0)
      return static_cast<int>(dwg->dwg_class[i].number);
  }
  return dwg_add_class(dwg, dxfname, cppname, "ObjectDBX Classes", false);
}

void InitChildValue(Dwg_FIELD_ChildValue* cv, Dwg_Object_FIELD* parent, const char* key) {
  assert(cv != nullptr && parent != nullptr && key != nullptr);
  cv->parent = parent;
  cv->key = dwg_add_u8_input(parent->parent->dwg, key);
  std::memset(&cv->value, 0, sizeof(cv->value));
}

void AttachCivilStyleChildval(Dwg_Object_FIELD* field, const char* evaluatedPreview) {
  assert(field != nullptr);
  field->num_childval = 2;
  field->childval = static_cast<Dwg_FIELD_ChildValue*>(std::calloc(2, sizeof(Dwg_FIELD_ChildValue)));
  if (field->childval == nullptr) {
    field->num_childval = 0;
    return;
  }
  InitChildValue(&field->childval[0], field, "ACFD_FIELDTEXT_ATTDEF");
  field->childval[0].value.flags = 2;
  field->childval[0].value.data_type = 1;
  field->childval[0].value.data_long = 1;
  InitChildValue(&field->childval[1], field, "ACFD_FIELDTEXT_CHECKSUM");
  field->childval[1].value.flags = 2;
  field->childval[1].value.data_type = 2;
  field->childval[1].value.data_double = evaluatedPreview != nullptr ? 49.0 : 0.0;
  (void)evaluatedPreview;
}

Dwg_Object* AppendFieldObject(Dwg_Data* dwg, std::uint64_t ownerBlockHandle, const char* fieldId,
                              const char* code, const BITCODE_HV* objectHandles, BITCODE_BL numObjects,
                              BITCODE_HV childFieldHandle, bool civilChildval) {
  assert(dwg != nullptr && fieldId != nullptr && code != nullptr);
  const int classNumber = EnsureObjectClass(dwg, "FIELD", "AcDbField");
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
  obj->fixedtype = DWG_TYPE_FIELD;
  obj->type = static_cast<BITCODE_BS>(classNumber);
  obj->dxfname = (dwg->opts & DWG_OPTS_IN) ? _strdup("FIELD") : const_cast<char*>("FIELD");
  obj->name = (dwg->opts & DWG_OPTS_IN) ? _strdup("FIELD") : const_cast<char*>("FIELD");
  obj->tio.object = static_cast<Dwg_Object_Object*>(std::calloc(1, sizeof(Dwg_Object_Object)));
  if (obj->tio.object == nullptr)
    return nullptr;
  obj->tio.object->objid = obj->index;
  obj->tio.object->dwg = dwg;
  auto* field = static_cast<Dwg_Object_FIELD*>(std::calloc(1, sizeof(Dwg_Object_FIELD)));
  if (field == nullptr)
    return nullptr;
  obj->tio.object->tio.FIELD = field;
  field->parent = obj->tio.object;
  dwg_set_next_objhandle(obj);

  if (ownerBlockHandle != 0)
    obj->tio.object->ownerhandle = dwg_add_handleref(dwg, 4, ownerBlockHandle, nullptr);

  field->id = dwg_add_u8_input(dwg, fieldId);
  field->code = dwg_add_u8_input(dwg, code);
  field->evaluation_option = 63;
  field->filing_option = 0;
  field->field_state = 9;
  field->evaluation_status = 2;
  field->evaluation_error_code = 0;
  field->evaluation_error_msg = dwg_add_u8_input(dwg, "");

  if (childFieldHandle != 0) {
    field->num_childs = 1;
    field->childs = static_cast<BITCODE_H*>(std::calloc(1, sizeof(BITCODE_H)));
    if (field->childs != nullptr)
      field->childs[0] = dwg_add_handleref(dwg, 3, childFieldHandle, obj);
    else
      field->num_childs = 0;
  }

  if (numObjects > 0 && objectHandles != nullptr) {
    field->num_objects = numObjects;
    field->objects = static_cast<BITCODE_H*>(std::calloc(static_cast<size_t>(numObjects), sizeof(BITCODE_H)));
    if (field->objects != nullptr) {
      for (BITCODE_BL i = 0; i < numObjects; ++i)
        field->objects[i] = dwg_add_handleref(dwg, 5, objectHandles[i], obj);
    } else {
      field->num_objects = 0;
    }
  }

  if (civilChildval)
    AttachCivilStyleChildval(field, nullptr);

  return obj;
}

void AddEntityReactor(Dwg_Object_Entity* ent, Dwg_Data* dwg, BITCODE_HV fieldHandle) {
  assert(ent != nullptr && dwg != nullptr && fieldHandle != 0);
  const BITCODE_BL n = ent->num_reactors;
  BITCODE_H* next =
      static_cast<BITCODE_H*>(std::realloc(ent->reactors, static_cast<size_t>(n + 1) * sizeof(BITCODE_H)));
  if (next == nullptr)
    return;
  ent->reactors = next;
  ent->reactors[n] = dwg_add_handleref(dwg, 4, fieldHandle, nullptr);
  ent->num_reactors = n + 1;
}

Dwg_Object* CreateIndexField(Dwg_Data* dwg, std::uint64_t ownerBlockHandle, std::uint32_t fldIdx,
                            BITCODE_HV dataFieldHandle) {
  const std::string idxWire = CadFieldMakeFldIdxWire(fldIdx);
  return AppendFieldObject(dwg, ownerBlockHandle, "_text", idxWire.c_str(), nullptr, 0, dataFieldHandle,
                           true);
}

Dwg_Object* CreateDataFieldForEntBinding(Dwg_Data* dwg, std::uint64_t ownerBlockHandle,
                                         const CadFieldGoSurveyEntBinding& bind, BITCODE_HV entHandle,
                                         std::string_view acdbClass, bool civilChildval) {
  const std::string acadFmt = AcadFieldFormat(bind.format, bind.prop);
  const std::string code =
      CadFieldMakeAcObjPropEntWire(entHandle, acdbClass, bind.prop, acadFmt);
  const BITCODE_HV objHandle = entHandle;
  return AppendFieldObject(dwg, ownerBlockHandle, "_text", code.c_str(), &objHandle, 1, 0, civilChildval);
}

Dwg_Object* CreateAcVarField(Dwg_Data* dwg, std::uint64_t ownerBlockHandle, std::string_view exprBody) {
  std::string code;
  code.reserve(exprBody.size() + 4);
  code.push_back('%');
  code.push_back('<');
  code.append(exprBody);
  if (code.size() < 2 || code[code.size() - 2] != '>' || code.back() != '%')
    code.append(">%");
  return AppendFieldObject(dwg, ownerBlockHandle, "_text", code.c_str(), nullptr, 0, 0, true);
}

bool ReplaceFieldSegment(std::string* wire, size_t start, size_t endInclusive, std::string_view replacement) {
  assert(wire != nullptr);
  if (endInclusive < start || endInclusive + 2 > wire->size())
    return false;
  wire->replace(start, endInclusive + 2 - start, replacement);
  return true;
}

}  // namespace

void DwgExportFieldContextInit(DwgExportFieldContext* ctx, bool r2004OrNewer) {
  assert(ctx != nullptr);
  *ctx = DwgExportFieldContext{};
  ctx->enabled = r2004OrNewer;
}

void DwgExportRegisterEntityHandle(DwgExportFieldContext* ctx, std::uint64_t entityId, const void* entity) {
  assert(ctx != nullptr);
  if (!ctx->enabled || entityId == 0 || entity == nullptr)
    return;
  int err = 0;
  const Dwg_Object* obj = dwg_obj_generic_to_object(entity, &err);
  if (obj == nullptr || err != 0)
    return;
  ctx->entityIdToHandle[entityId] = obj->handle.value;
}

std::string DwgExportPrepareAnnotationFieldText(DwgExportFieldContext* ctx, _dwg_struct* dwgIn,
                                                const AppCommandState& st, std::string_view wire,
                                                const CadFieldContext& fctx, void* hostEntity,
                                                std::uint64_t ownerBlockHandle) {
  assert(ctx != nullptr && dwgIn != nullptr);
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  auto* hostEnt = static_cast<Dwg_Object_Entity*>(hostEntity);
  std::string out = CadFieldTextForDwgExport(st, wire, fctx, ctx->enabled);
  if (!ctx->enabled || hostEnt == nullptr || !CadTextContainsFieldCodes(out))
    return out;

  size_t i = 0;
  while (i < out.size()) {
    const size_t start = out.find("%<", i);
    if (start == std::string::npos)
      break;
    const size_t end = out.find(">%", start);
    if (end == std::string::npos)
      break;
    const std::string_view expr = std::string_view(out).substr(start + 2, end - (start + 2));

    CadFieldGoSurveyEntBinding bind{};
    if (CadFieldTryParseGoSurveyEntWire(expr, &bind)) {
      const auto hit = ctx->entityIdToHandle.find(bind.entityId);
      if (hit == ctx->entityIdToHandle.end()) {
        i = end + 2;
        continue;
      }
      const std::string acdbClass = AcdbClassForEntityRef(st, bind.entityId);
      if (acdbClass.empty()) {
        i = end + 2;
        continue;
      }
      Dwg_Object* dataField =
          CreateDataFieldForEntBinding(dwg, ownerBlockHandle, bind, hit->second, acdbClass, true);
      if (dataField == nullptr) {
        i = end + 2;
        continue;
      }
      const BITCODE_HV dataHandle = dataField->handle.value;
      const std::uint32_t idx = ctx->nextFldIdx++;
      Dwg_Object* indexField = CreateIndexField(dwg, ownerBlockHandle, idx, dataHandle);
      if (indexField == nullptr) {
        i = end + 2;
        continue;
      }
      const BITCODE_HV indexHandle = indexField->handle.value;
      ctx->fieldListIndexHandles.push_back(indexHandle);
      AddEntityReactor(hostEnt, dwg, indexHandle);
      const std::string idxWire = CadFieldMakeFldIdxWire(idx);
      ReplaceFieldSegment(&out, start, end, idxWire);
      ctx->fieldsWritten += 2;
      i = start + idxWire.size();
      continue;
    }

    if (expr.size() >= 6 && expr[0] == '\\' && expr.substr(1, 5) == "AcVar") {
      Dwg_Object* varField = CreateAcVarField(dwg, ownerBlockHandle, expr);
      if (varField != nullptr) {
        AddEntityReactor(hostEnt, dwg, varField->handle.value);
        ctx->fieldListIndexHandles.push_back(varField->handle.value);
        ++ctx->fieldsWritten;
      }
      i = end + 2;
      continue;
    }

    i = end + 2;
  }
  return out;
}

void DwgExportFinalizeFieldObjects(DwgExportFieldContext* ctx, _dwg_struct* dwgIn,
                                   std::vector<std::string>& log) {
  assert(ctx != nullptr && dwgIn != nullptr);
  auto* dwg = reinterpret_cast<Dwg_Data*>(dwgIn);
  if (!ctx->enabled || ctx->fieldListIndexHandles.empty())
    return;

  const int listClass = EnsureObjectClass(dwg, "FIELDLIST", "AcDbFieldList");
  if (listClass < 0)
    return;

  const BITCODE_BL idx = dwg->num_objects;
  const int added = dwg_add_object(dwg);
  if (added > 0)
    return;
  if (added < 0)
    dwg_resolve_objectrefs_silent(dwg);

  Dwg_Object* listObj = &dwg->object[idx];
  listObj->supertype = DWG_SUPERTYPE_OBJECT;
  listObj->fixedtype = DWG_TYPE_FIELDLIST;
  listObj->type = static_cast<BITCODE_BS>(listClass);
  listObj->dxfname = (dwg->opts & DWG_OPTS_IN) ? _strdup("FIELDLIST") : const_cast<char*>("FIELDLIST");
  listObj->name = (dwg->opts & DWG_OPTS_IN) ? _strdup("FIELDLIST") : const_cast<char*>("FIELDLIST");
  listObj->tio.object = static_cast<Dwg_Object_Object*>(std::calloc(1, sizeof(Dwg_Object_Object)));
  if (listObj->tio.object == nullptr)
    return;
  listObj->tio.object->objid = listObj->index;
  listObj->tio.object->dwg = dwg;
  auto* fl = static_cast<Dwg_Object_FIELDLIST*>(std::calloc(1, sizeof(Dwg_Object_FIELDLIST)));
  if (fl == nullptr)
    return;
  listObj->tio.object->tio.FIELDLIST = fl;
  fl->parent = listObj->tio.object;
  dwg_set_next_objhandle(listObj);

  const BITCODE_BL n = static_cast<BITCODE_BL>(ctx->fieldListIndexHandles.size());
  fl->num_fields = n;
  fl->fields = static_cast<BITCODE_H*>(std::calloc(static_cast<size_t>(n), sizeof(BITCODE_H)));
  if (fl->fields == nullptr) {
    fl->num_fields = 0;
    return;
  }
  for (BITCODE_BL i = 0; i < n; ++i)
    fl->fields[i] = dwg_add_handleref(dwg, 4, ctx->fieldListIndexHandles[static_cast<size_t>(i)], listObj);

  BITCODE_H nodRef = dwg->header_vars.DICTIONARY_NAMED_OBJECT;
  if (nodRef == nullptr)
    return;
  Dwg_Object* nod = dwg_resolve_handle_silent(dwg, nodRef->absolute_ref);
  if (nod == nullptr || nod->fixedtype != DWG_TYPE_DICTIONARY || nod->tio.object == nullptr ||
      nod->tio.object->tio.DICTIONARY == nullptr)
    return;
  dwg_add_DICTIONARY_item(nod->tio.object->tio.DICTIONARY, "ACAD_FIELDLIST", listObj->handle.value);

  log.push_back("DWG export — wrote " + std::to_string(ctx->fieldsWritten) +
                " FIELD object(s) and FIELDLIST (issue #617).");
}
