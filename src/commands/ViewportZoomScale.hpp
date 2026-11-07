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
/// `drawingInsUnits` is the drawing's INSUNITS code. Metric drawings (4 mm, 6 m) get the 1:1 to 100:1 list
/// with its drawing units in millimetres or metres; every other drawing gets the imperial 1" = 1" to
/// 1" = 5000' list, in feet (INSUNITS 2, the default) or inches (1). A non-empty list is left alone.
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
