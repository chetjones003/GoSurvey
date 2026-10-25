#include "CrashBuildInfo.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>

namespace crashReporter {
namespace {

constexpr unsigned long kCppExceptionCode = 0xE06D7363UL;

struct CodeViewRsds {
  DWORD signature;  // 'RSDS'
  GUID  guid;
  DWORD age;
};

#if defined(_M_X64)
// MSVC x64 C++ exception metadata. Every pointer is a 32-bit RVA from the image base that the
// record carries in ExceptionInformation[3].
struct PmdRva {
  int mdisp;
  int pdisp;
  int vdisp;
};
struct CatchableTypeRva {
  unsigned properties;
  int      pType;
  PmdRva   thisDisplacement;
  int      sizeOrOffset;
  int      copyFunction;
};
struct CatchableTypeArrayRva {
  int nCatchableTypes;
  int arrayOfCatchableTypes[1];
};
struct ThrowInfoRva {
  unsigned attributes;
  int      pmfnUnwind;
  int      pForwardCompat;
  int      pCatchableTypeArray;
};
struct TypeDescriptorHeader {
  const void* pVFTable;
  void*       spare;
  char        name[1];
};

// Copies the thrown type name and what() into the buffers. No C++ objects with destructors here,
// so it may use SEH to survive a corrupt exception record.
bool ReadThrownTypeAndWhat(const EXCEPTION_RECORD* rec, char* typeOut, size_t typeCap, char* whatOut,
                           size_t whatCap) {
  __try {
    const auto objectPtr = static_cast<std::uintptr_t>(rec->ExceptionInformation[1]);
    const auto throwInfo = reinterpret_cast<const ThrowInfoRva*>(rec->ExceptionInformation[2]);
    const auto base      = static_cast<std::uintptr_t>(rec->ExceptionInformation[3]);
    if (!throwInfo || !base || !objectPtr)
      return false;
    const auto* types = reinterpret_cast<const CatchableTypeArrayRva*>(base + throwInfo->pCatchableTypeArray);
    if (types->nCatchableTypes <= 0)
      return false;

    bool hasType = false;
    for (int i = 0; i < types->nCatchableTypes && i < 16; ++i) {
      const auto* ct = reinterpret_cast<const CatchableTypeRva*>(base + types->arrayOfCatchableTypes[i]);
      const auto* td = reinterpret_cast<const TypeDescriptorHeader*>(base + ct->pType);
      if (i == 0) {
        std::strncpy(typeOut, td->name, typeCap - 1);
        typeOut[typeCap - 1] = '\0';
        hasType              = true;
      }
      if (std::strcmp(td->name, ".?AVexception@std@@") == 0 && ct->thisDisplacement.pdisp == -1) {
        const auto* asStd =
            reinterpret_cast<const std::exception*>(objectPtr + static_cast<std::uintptr_t>(ct->thisDisplacement.mdisp));
        const char* what = asStd->what();
        if (what) {
          std::strncpy(whatOut, what, whatCap - 1);
          whatOut[whatCap - 1] = '\0';
        }
        break;
      }
    }
    return hasType;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
#endif

// SEH-guarded reader; no objects with destructors so __try is allowed.
bool ReadPdbIdentity(const void* moduleBase, char* out, size_t cap) {
  __try {
    const auto* bytes = static_cast<const unsigned char*>(moduleBase);
    const auto* dos   = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
      return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(bytes + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
      return false;
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (dir.VirtualAddress == 0 || dir.Size == 0)
      return false;
    const auto*  entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(bytes + dir.VirtualAddress);
    const size_t count   = dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
    for (size_t i = 0; i < count; ++i) {
      if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || entries[i].AddressOfRawData == 0)
        continue;
      const auto* rsds = reinterpret_cast<const CodeViewRsds*>(bytes + entries[i].AddressOfRawData);
      if (rsds->signature != 0x53445352)  // 'RSDS'
        continue;
      const GUID& g = rsds->guid;
      std::snprintf(out, cap, "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X,%lu", g.Data1, g.Data2,
                    g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5],
                    g.Data4[6], g.Data4[7], static_cast<unsigned long>(rsds->age));
      return true;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return false;
}

}  // namespace

std::string PdbIdentityOfModule(const void* moduleBase) {
  char buf[64] = "";
  if (moduleBase && ReadPdbIdentity(moduleBase, buf, sizeof(buf)))
    return buf;
  return "unavailable";
}

std::string DescribeThrownCppException(const void* exceptionRecord) {
#if defined(_M_X64)
  const auto* rec = static_cast<const EXCEPTION_RECORD*>(exceptionRecord);
  if (!rec || rec->ExceptionCode != kCppExceptionCode || rec->NumberParameters < 4)
    return std::string();

  char typeName[256] = "";
  char what[512]     = "";
  if (!ReadThrownTypeAndWhat(rec, typeName, sizeof(typeName), what, sizeof(what)))
    return std::string();

  std::string out = "Thrown C++ type: ";
  out += typeName;
  out += "\n";
  if (what[0]) {
    out += "what(): ";
    out += what;
    out += "\n";
  }
  return out;
#else
  (void)exceptionRecord;
  return std::string();
#endif
}

}  // namespace crashReporter
#endif
