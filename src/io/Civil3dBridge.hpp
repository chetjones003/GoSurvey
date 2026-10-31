#pragma once

#include <string>
#include <vector>

struct AppCommandState;

/// Read native Civil 3D point/surface objects through the installed Civil 3D runtime.
/// Returns false when the runtime/bridge is unavailable or the native export fails.
bool ImportCivil3dModelObjects(AppCommandState& state, const char* dwgPathUtf8,
                               std::vector<std::string>& log);

/// Recreate the editable COGO points and TINs in the DWG through the installed Civil 3D database API.
/// Returns false when there are no Civil 3D model objects to write or the bridge cannot run.
bool ExportCivil3dModelObjects(const AppCommandState& state, const char* dwgPathUtf8,
                               std::vector<std::string>& log);
