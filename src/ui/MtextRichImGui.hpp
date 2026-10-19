#pragma once

#include <imgui.h>

#include <string>

// ImGui-only MTEXT rich-text layout (draw + measure). Kept out of MtextRichFormat.hpp so the shared
// domain library (and the Qt frontend that links it) never depends on ImGui core (issue #917).

/// WYSIWYG layout: draws wrapped rich text at \p origin with max line width \p maxWidth. \p baseFontFamily
/// is the typeface used for runs without a [[font:…]] override (empty = the fallback \p font).
void MtextRichDrawWrapped(ImDrawList* dl, ImFont* font, float fontPx, ImVec2 origin, float maxWidth, ImU32 baseRgb,
                          const std::string& wire, const std::string& baseFontFamily = {});

/// Vertical size (px) of the same wrapped layout as \ref MtextRichDrawWrapped (no drawing).
[[nodiscard]] float MtextRichWrappedHeight(ImFont* font, float fontPx, float maxWidth, const std::string& wire,
                                           const std::string& baseFontFamily = {});

/// Unwrapped content size in pixels (only hard newlines break lines; \p maxWidth should be very large).
void MtextRichNaturalContentPx(ImFont* font, float fontPx, const std::string& wire, float* outW, float* outH,
                               const std::string& baseFontFamily = {});

/// Measures an annotation through the ImGui default font (the AppCommandState::measureAnnotationText
/// signature). Falls back to the no-font estimate when no ImGui context exists. The ImGui app and the
/// headless driver install this on their AppCommandState; Qt installs MeasureQtText instead.
void MtextRichMeasureAnnotationImGui(float fontPx, const std::string& wire, const std::string& family, float* outW,
                                     float* outH);
