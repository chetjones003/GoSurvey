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

/// Fills an empty annotation scale list with AutoCAD's standard list (D-2026-11-06-a), in AutoCAD's order.
/// `drawingInsUnits` is the drawing's INSUNITS code: 1 (inches) and 2 (feet) get the imperial list (1" = 1"
/// to 1" = 5000'), anything else the metric list (1:1 to 100:1). The imperial ratios are written for inch
/// drawings; for a feet drawing each drawing-unit count is divided by 12 so 1" = 5' is 5 feet per inch.
/// A non-empty list is left alone.
void EnsureStandardAnnotationScales(std::vector<CadAnnotationScale>& scales, int drawingInsUnits);

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
