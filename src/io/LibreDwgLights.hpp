#pragma once

#include <string>
#include <vector>

struct AppCommandState;
struct _dwg_struct;

void DwgLightImportBegin();
void DwgLightImportCapture(_dwg_struct* dwg, AppCommandState& st);
void DwgLightImportAppendLog(std::vector<std::string>& log);

void DwgExportImportedLightsAndSun(const AppCommandState& st, _dwg_struct* dwg, void* modelSpaceBlock,
                                   std::vector<std::string>& log);

[[nodiscard]] int DwgExportCountLightSunLosses(const AppCommandState& st);

[[nodiscard]] int DwgLightCountEntities(const _dwg_struct* dwg);
[[nodiscard]] int DwgSunCountObjects(const _dwg_struct* dwg);

/// Test helper: append one point LIGHT to model space in an in-memory DWG.
bool DwgTestAddPointLight(_dwg_struct* dwg, void* modelSpaceBlock, const char* name, double x, double y,
                          double z);
