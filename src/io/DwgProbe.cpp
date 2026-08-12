#include "DwgIo.hpp"

// Version detection + converter discovery for DWG (REQ-052, ADR-024).
//
// Split out of DwgIo.cpp on purpose: this half depends on nothing but <filesystem>, so the test
// target links it directly. DwgIo.cpp — the conversion orchestration — calls into DxfIo, which
// pulls in CadCommands.cpp and with it pdfium/WinRT/ImGui, and cannot be linked by tests today.
// Keeping the testable logic in its own translation unit is what makes TASK-031's coverage
// possible without restructuring the whole application.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

std::string ToLowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

std::string EnvVar(const char* name) {
#ifdef _WIN32
  char* buf = nullptr;
  size_t len = 0;
  if (_dupenv_s(&buf, &len, name) != 0 || !buf)
    return std::string();
  std::string v(buf);
  free(buf);
  return v;
#else
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
#endif
}

/// Scans \p parent for immediate subdirectories containing \p exeName, keeping the
/// lexicographically greatest match — release directories sort with the newest last
/// ("AutoCAD 2024" < "AutoCAD 2026").
bool FindExeUnderSubdirs(const std::filesystem::path& parent,
                         const char* exeName,
                         std::filesystem::path* out) {
  std::error_code ec;
  if (!std::filesystem::is_directory(parent, ec))
    return false;
  std::filesystem::path best;
  for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
    if (ec)
      break;
    if (!entry.is_directory(ec))
      continue;
    const std::filesystem::path candidate = entry.path() / exeName;
    if (std::filesystem::exists(candidate, ec)) {
      if (best.empty() || candidate.string() > best.string())
        best = candidate;
    }
  }
  if (best.empty())
    return false;
  *out = best;
  return true;
}

DwgConverter DetectConverter() {
  DwgConverter c;
  std::error_code ec;

  // 1. Explicit override — lets a user point at an install we do not know about.
  const std::string overridePath = EnvVar("GOSURVEY_DWG_CONVERTER");
  if (!overridePath.empty() && std::filesystem::exists(overridePath, ec)) {
    const std::string fileName = ToLowerAscii(std::filesystem::path(overridePath).filename().string());
    c.exePath = overridePath;
    if (fileName.find("accoreconsole") != std::string::npos) {
      c.kind = DwgConverterKind::AutoCadCore;
      c.displayName = "AutoCAD accoreconsole (GOSURVEY_DWG_CONVERTER)";
    } else {
      c.kind = DwgConverterKind::OdaFileConverter;
      c.displayName = "ODA File Converter (GOSURVEY_DWG_CONVERTER)";
    }
    return c;
  }

  // 2. ODA File Converter — the redistributable, vendor-neutral option.
  std::filesystem::path oda;
  for (const char* root : {"C:/Program Files/ODA", "C:/Program Files (x86)/ODA"}) {
    if (FindExeUnderSubdirs(root, "ODAFileConverter.exe", &oda)) {
      c.kind = DwgConverterKind::OdaFileConverter;
      c.exePath = oda.string();
      c.displayName = "ODA File Converter";
      return c;
    }
  }

  // 3. Any installed AutoCAD / Civil 3D console.
  std::filesystem::path acad;
  if (FindExeUnderSubdirs("C:/Program Files/Autodesk", "accoreconsole.exe", &acad)) {
    c.kind = DwgConverterKind::AutoCadCore;
    c.exePath = acad.string();
    c.displayName = acad.parent_path().filename().string() + " (accoreconsole)";
    return c;
  }

  return c;
}

} // namespace

const DwgConverter& FindDwgConverter(bool forceRescan) {
  static DwgConverter cached;
  static bool scanned = false;
  if (!scanned || forceRescan) {
    cached = DetectConverter();
    scanned = true;
  }
  return cached;
}

std::string DwgVersionNameFromTag(const std::string& tag6) {
  if (tag6.size() != 6)
    return std::string();
  if (tag6 == "AC1032") return "AutoCAD 2018";
  if (tag6 == "AC1027") return "AutoCAD 2013";
  if (tag6 == "AC1024") return "AutoCAD 2010";
  if (tag6 == "AC1021") return "AutoCAD 2007";
  if (tag6 == "AC1018") return "AutoCAD 2004";
  if (tag6 == "AC1015") return "AutoCAD 2000";
  if (tag6 == "AC1014") return "AutoCAD R14";
  if (tag6 == "AC1012") return "AutoCAD R13";
  if (tag6 == "AC1009") return "AutoCAD R11/R12";
  if (tag6 == "AC1006") return "AutoCAD R10";
  // An AC-prefixed tag we do not recognise is still a DWG — say so rather than claim it is not one.
  if (tag6.rfind("AC", 0) == 0) return "unknown DWG (" + tag6 + ")";
  return std::string();
}

std::string DwgVersionName(const char* pathUtf8) {
  if (!pathUtf8 || pathUtf8[0] == '\0')
    return std::string();
  std::ifstream f(std::filesystem::path(pathUtf8), std::ios::binary);
  if (!f)
    return std::string();
  char tag[7]{};
  f.read(tag, 6);
  if (f.gcount() != 6)
    return std::string();
  return DwgVersionNameFromTag(std::string(tag, 6));
}
