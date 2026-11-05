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

/// Updates the drawing's current viewport zoom preset index only (status bar). Does not change
/// \c viewportZoom or pan — wheel zoom and ZOOM commands stay independent (TASK-308 follow-up).
void SetCurrentViewportZoomScaleIndex(AppCommandState& st, int index);

/// Sets the current preset and adjusts \c viewportZoom so the model view matches the ratio (tests
/// and explicit zoom-to-scale callers only).
void ApplyViewportZoomScaleIndex(AppCommandState& st, int index, float viewportHeightPx, float dpiY);

/// Keeps \c currentViewportZoomScaleIndex valid for status-bar display. Does not change \c viewportZoom
/// when the user has wheel-zoomed away from the preset (REQ-458).
void EnsureViewportZoomScaleIndexForDisplay(AppCommandState& st);

[[nodiscard]] std::string CadViewportZoomScaleStatusLabel(const CadAnnotationScale& scale);
