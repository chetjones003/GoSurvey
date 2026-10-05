#include "LibreDwgAnnotContext.hpp"

#include <cassert>

#if defined(__cplusplus) && !defined(restrict)
#define restrict
#endif
extern "C" {
#include <dwg.h>
#include <dwg_api.h>
}

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

}  // namespace

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
                "AcadAnnotative/GOSURVEY markers (REQ-384 inc 1 — per-scale context not merged yet).");
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
