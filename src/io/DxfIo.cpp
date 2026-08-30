#include "DxfIo.hpp"

#include "LibreDwgCad.hpp"

bool ImportDxfFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  return ImportLibreCadFile(st, pathUtf8, log, /*asDxf=*/true);
}

bool ExportDxfFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  return ExportLibreCadFile(st, pathUtf8, log, /*asDxf=*/true);
}
