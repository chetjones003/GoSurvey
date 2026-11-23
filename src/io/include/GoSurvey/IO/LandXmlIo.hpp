#pragma once
// Public IO seam for LandXML 1.2 export and import.

#include <string>
#include <vector>

struct AppCommandState;

bool ExportLandXmlFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
bool ImportLandXmlFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
