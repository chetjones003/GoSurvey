#pragma once

// REQ-458 — model editor viewport zoom ratio presets (separate from annotation scales).

#include "CadEntities.hpp"

#include <string>
#include <vector>

struct AppCommandState;

inline constexpr const char* kViewportZoomScaleDwgPrefix = "GS_VZ_";

[[nodiscard]] inline float CadViewportZoomScaleModelUnitsPerInch(const CadAnnotationScale& scale) {
  return CadAnnotationScaleModelUnitsPerPlottedInch(scale);
}

void EnsureDefaultViewportZoomScales(std::vector<CadAnnotationScale>& scales);

[[nodiscard]] bool TryParseViewportZoomRatio(const std::string& text, float* paperUnits, float* drawingUnits);

bool AddViewportZoomScale(AppCommandState& st, const std::string& name, float paperUnits, float drawingUnits);

[[nodiscard]] float ViewportZoomForModelUnitsPerInch(float modelUnitsPerInch, float viewportHeightPx,
                                                       float dpiY);

void ApplyViewportZoomScaleIndex(AppCommandState& st, int index, float viewportHeightPx, float dpiY);

void SyncCurrentViewportZoomScaleIndex(AppCommandState& st, float viewportHeightPx, float dpiY);

[[nodiscard]] std::string CadViewportZoomScaleStatusLabel(const CadAnnotationScale& scale);
