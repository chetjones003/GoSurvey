#pragma once

#include <vector>

struct AppCommandState;

/// Translucent preview for MOVE/COPY/SCALE/ROTATE and OFFSET live preview (viewport line/circle batches).
void BuildTransformPreview(const AppCommandState& cmd, float cursorWorldX, float cursorWorldY,
                           std::vector<float>* outPreviewLines, std::vector<float>* outPreviewCircles);

/// Selection highlight geometry for the viewport (slightly raised Z for depth bias).
void BuildSelectionHighlight(const AppCommandState& cmd, std::vector<float>* outHighlightLines,
                             std::vector<float>* outHighlightCircles);

/// Hover highlight geometry for the viewport (entity under idle cursor, distinct from selection).
void BuildHoverHighlight(const AppCommandState& cmd, std::vector<float>* outHoverLines,
                         std::vector<float>* outHoverCircles);
