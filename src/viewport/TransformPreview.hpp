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

/// REQ-103 BREAK, live preview (TASK-101): the material a break at `cmd.breakP1` and the cursor
/// would REMOVE. Both outputs are flat XYZ segment lists (stride 6 — two vertices per segment) and
/// come back empty unless BREAK is active in `SelectSecondPoint`. The cursor is resolved onto the
/// picked entity through the same `ClosestPointOnEntity` the second pick commits.
///
/// This is its OWN channel rather than part of \c BuildTransformPreview's batch, because a
/// removal preview is the one preview that lies exactly on top of the object it describes. The
/// transform batch is drawn translucent (alpha 0.55) at ordinary line width, which reads correctly
/// for a MOVE/COPY ghost sitting in empty space and reads as almost nothing when washed over a
/// full-opacity line underneath it — the span was being drawn correctly and was still invisible.
///
/// \p outSpan is the material itself; \p outMarkers is the X at each break point, sized from
/// \p orthoHalfHeightWorld so it holds its apparent size at any zoom (pass <= 0 for a fixed size).
/// Either may be null. They are separate so a transcript can assert the span's length against a
/// hand-computed figure without the markers' decoration in the total — *which* span disappears is
/// the part that can be silently wrong, since click order decides it on a closed entity.
void BuildBreakRemovalPreview(const AppCommandState& cmd, float cursorWorldX, float cursorWorldY,
                              float orthoHalfHeightWorld, std::vector<float>* outSpan,
                              std::vector<float>* outMarkers);

/// Selection highlight geometry for the viewport (slightly raised Z for depth bias).
void BuildSelectionHighlight(const AppCommandState& cmd, std::vector<float>* outHighlightLines,
                             std::vector<float>* outHighlightCircles);

/// Hover highlight geometry for the viewport (entity under idle cursor, distinct from selection).
void BuildHoverHighlight(const AppCommandState& cmd, std::vector<float>* outHoverLines,
                         std::vector<float>* outHoverCircles);
