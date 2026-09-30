#include "LibreDwg.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__cplusplus) && !defined(restrict)
#define restrict
#endif

extern "C" {
#include <dwg.h>
#include <dwg_api.h>
}

namespace {

void FreeDocument(Dwg_Data* dwg) {
  if (dwg == nullptr) {
    return;
  }
  dwg_free(dwg);
  std::free(dwg);
}

}  // namespace

#ifndef GOSURVEY_LIBREDWG_VERSION
#define GOSURVEY_LIBREDWG_VERSION "unknown"
#endif

const char* LibreDwgPackageVersion() {
  return GOSURVEY_LIBREDWG_VERSION;
}

void LibreDwgLinkBlockEntities(Dwg_Data* dwg) {
  if (dwg == nullptr) {
    return;
  }
  for (BITCODE_BL i = 0; i < dwg->num_objects; ++i) {
    Dwg_Object* blockObj = &dwg->object[i];
    if (blockObj->fixedtype != DWG_TYPE_BLOCK_HEADER || blockObj->tio.object == nullptr) {
      continue;
    }
    const Dwg_Object_BLOCK_HEADER* block = blockObj->tio.object->tio.BLOCK_HEADER;
    std::vector<Dwg_Object*> chain;
    for (BITCODE_BL k = 0; k < block->num_owned; ++k) {
      Dwg_Object* obj = block->entities[k] != nullptr ? dwg_ref_object(dwg, block->entities[k]) : nullptr;
      if (obj != nullptr && obj->supertype == DWG_SUPERTYPE_ENTITY) {
        chain.push_back(obj);
      }
    }
    for (size_t k = 0; k < chain.size(); ++k) {
      const BITCODE_HV prev = k > 0 ? chain[k - 1]->handle.value : 0;
      const BITCODE_HV next = k + 1 < chain.size() ? chain[k + 1]->handle.value : 0;
      Dwg_Object_Entity* ent = chain[k]->tio.entity;
      ent->nolinks = 0;
      ent->prev_entity = dwg_add_handleref(dwg, 4, prev, nullptr);
      ent->next_entity = dwg_add_handleref(dwg, 4, next, nullptr);
    }
  }
}

bool LibreDwgWriteMinimalR2000(const char* pathUtf8) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0') {
    return false;
  }

  Dwg_Data* dwg = dwg_new_Document(R_2000, /*imperial=*/0, /*loglevel=*/0);
  if (dwg == nullptr) {
    return false;
  }

  Dwg_Object* mspace = dwg_model_space_object(dwg);
  if (mspace == nullptr || mspace->tio.object == nullptr) {
    FreeDocument(dwg);
    return false;
  }
  Dwg_Object_BLOCK_HEADER* hdr = mspace->tio.object->tio.BLOCK_HEADER;
  if (hdr == nullptr) {
    FreeDocument(dwg);
    return false;
  }

  dwg_point_3d startPt = {0.0, 0.0, 0.0};
  dwg_point_3d endPt = {10.0, 0.0, 0.0};
  if (dwg_add_LINE(hdr, &startPt, &endPt) == nullptr) {
    FreeDocument(dwg);
    return false;
  }

  LibreDwgLinkBlockEntities(dwg);
  const int err = dwg_write_file(pathUtf8, dwg);
  FreeDocument(dwg);
  // Encode warnings below CRITICAL can still write a corpse that AutoCAD will
  // Recover; increment 1 requires a file we can read back (REQ-170).
  return err == DWG_NOERR;
}

std::string LibreDwgReadVersionName(const char* pathUtf8) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0') {
    return {};
  }

  Dwg_Data dwg;
  std::memset(&dwg, 0, sizeof(dwg));
  const int err = dwg_read_file(pathUtf8, &dwg);
  std::string name;
  if (err < DWG_ERR_CRITICAL) {
    const char* v = dwg_version_type(dwg.header.version);
    if (v != nullptr) {
      name = v;
    }
  }
  dwg_free(&dwg);
  return name;
}
