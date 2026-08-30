#include "LibreDwg.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

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
