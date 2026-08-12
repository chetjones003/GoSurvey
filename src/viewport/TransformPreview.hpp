#pragma once

#include <vector>

struct AppCommandState;

/// Translucent preview for MOVE/COPY/SCALE/ROTATE and OFFSET live preview (viewport line/circle batches).
///
/// \p orthoHalfHeightWorld and \p framebufferHeightPx describe the current view so previewed arcs and
/// ellipses are tessellated at the same density the renderer uses for committed geometry. With a fixed
/// segment count the preview's chord polygon cuts visibly inside the true curve as you zoom in, and the
/// previewed object reads as a shrunken copy of the real one.
void BuildTransformPreview(const AppCommandState& cmd, float cursorWorldX, float cursorWorldY,
                           std::vector<float>* outPreviewLines, std::vector<float>* outPreviewCircles,
                           float orthoHalfHeightWorld, int framebufferHeightPx);

/// Selection highlight geometry for the viewport (slightly raised Z for depth bias).
void BuildSelectionHighlight(const AppCommandState& cmd, std::vector<float>* outHighlightLines,
                             std::vector<float>* outHighlightCircles);

/// Hover highlight geometry for the viewport (entity under idle cursor, distinct from selection).
void BuildHoverHighlight(const AppCommandState& cmd, std::vector<float>* outHoverLines,
                         std::vector<float>* outHoverCircles);
