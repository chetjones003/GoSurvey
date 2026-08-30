#include "DwgIo.hpp"

#include "LibreDwgCad.hpp"

bool ImportDwgFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  return ImportLibreCadFile(st, pathUtf8, log, /*asDxf=*/false);
}

bool ExportDwgFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  return ExportLibreCadFile(st, pathUtf8, log, /*asDxf=*/false);
}
