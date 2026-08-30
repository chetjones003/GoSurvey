#pragma once

#include <string>
#include <vector>

struct AppCommandState;

/// Load a drawing through GNU LibreDWG (REQ-170). \p asDxf selects dxf_read_file vs dwg_read_file.
bool ImportLibreCadFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log,
                        bool asDxf);

/// Save through GNU LibreDWG. DWG is R2000 (the encoder this pin actually supports). DXF is ASCII
/// from the same in-memory drawing.
bool ExportLibreCadFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log,
                        bool asDxf);
