#pragma once

#include <string>
#include <vector>

struct AppCommandState;

// DWG interchange via GNU LibreDWG (REQ-170 / ADR-041). File Import/Export does not use ODA
// File Converter or AutoCAD. Save is R2000 (AC1015) — LibreDWG 0.13.3's working encode target.
// FindDwgConverter remains for 3D solid tessellation only (not File DWG).

enum class DwgConverterKind {
  None = 0,
  OdaFileConverter,
  AutoCadCore,
};

struct DwgConverter {
  DwgConverterKind kind = DwgConverterKind::None;
  std::string exePath;
  std::string displayName;
  bool available() const { return kind != DwgConverterKind::None; }
};

const DwgConverter& FindDwgConverter(bool forceRescan = false);
std::string DwgVersionNameFromTag(const std::string& tag6);
std::string DwgVersionName(const char* pathUtf8);

bool ImportDwgFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
bool ExportDwgFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
