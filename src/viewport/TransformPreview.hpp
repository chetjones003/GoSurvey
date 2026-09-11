#pragma once

#include <vector>

struct AppCommandState;
struct CadGizmoOverlay;

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

/// The SUB-OBJECT selection's drawable form (REQ-318 item 11, issue #148).
///
/// Two buffers because the two halves are drawn under different depth rules, which is the decision
/// (D-2026-09-04-a) and not an implementation detail:
///
///   * \p outFaceTris — `GL_TRIANGLES`, nine floats per triangle, storage coordinates. **Drawn
///     depth-tested.** A never-occluded face tint would show a back face glowing through the body of
///     the solid, which is the one place the renderer's blanket "overlays are never occluded" rule
///     (written for 2D linework) gives the wrong answer.
///   * \p outLines — `GL_LINES`, appended to the ordinary never-occluded highlight channel. An edge
///     is one line thick and a vertex marker is a few pixels; depth-testing them would sink them
///     into the very surface they lie on.
///
/// In **2D Wireframe** — the default style — solids draw no faces and the depth test is off, so
/// there is nothing for the tint to be occluded by and it simply draws. Intended: in wireframe the
/// tint is the only way a face selection can be shown at all.
///
/// Built from the per-solid `CadSolidTessellation`, never from `CadSolidDisplayGeometry`: the
/// display batches coalesce solids that draw identically into shared buffers for REQ-100's
/// draw-call budget (#194) and carry no per-face channel, so a face's own triangles cannot be
/// recovered from them (REQ-318 item 13).
void BuildSubObjectHighlight(const AppCommandState& cmd, std::vector<float>* outFaceTris,
                             std::vector<float>* outFaceEdges, std::vector<float>* outLines);

/// The same, for the sub-object under the cursor that a `Ctrl` click WOULD take (REQ-318 item 14).
///
/// A separate call rather than a flag on the one above, because the two are drawn in different
/// colours through different channels — the hover's linework joins the ordinary blue hover channel
/// and its face tint is quieter than the selection's. Emits nothing when the hovered sub-object is
/// already selected: the selection highlight is the stronger statement and drawing both over one
/// another only muddies it, which is the rule `BuildHoverHighlight` already applies to entities.
void BuildSubObjectHoverHighlight(const AppCommandState& cmd, std::vector<float>* outFaceTris,
                                  std::vector<float>* outFaceEdges, std::vector<float>* outLines);

/// Where a face would land under an armed gizmo drag (REQ-319 increment 2, re-driven by issue #148
/// acceptance 4). Empty unless a gizmo drag is armed on a face.
///
/// The face's boundary translated by the live drag distance, plus a leader from the centroid to it.
/// **The solid is not rebuilt per frame to draw this** — a push is a whole-solid copy and a
/// validation, and the drag runs on the frame path. Translating the boundary shows where the face
/// is heading at a cost that does not grow with the solid, and the real geometry is computed once,
/// on commit, where a refusal can still be reported (ADR-046 (d)).
///
/// The square HANDLE this used to draw beside the preview is gone: slice 4c made the gizmo's arrow
/// the handle, and two handles for one operation is worse than either.
void BuildSubObjectFaceGhost(const AppCommandState& cmd, std::vector<float>* outPreview);

/// Hover highlight geometry for the viewport (entity under idle cursor, distinct from selection).
void BuildHoverHighlight(const AppCommandState& cmd, std::vector<float>* outHoverLines,
                         std::vector<float>* outHoverCircles);

/// The translate gizmo's handles (REQ-060, GitHub issue #148 Phase 5 slice 4b).
///
/// Emits nothing when the selection is empty, which IS REQ-060's third acceptance bullet: there is
/// no separate "should the gizmo be drawn" flag for a caller to get wrong.
void BuildGizmoOverlay(const AppCommandState& cmd, CadGizmoOverlay* out);

/// The selection, drawn where an armed gizmo drag is about to put it. Empty when nothing is armed.
///
/// Rides the ordinary PREVIEW channel at the call site rather than owning one: a ghost of what is
/// about to happen is exactly what that channel already carries for MOVE, ROTATE and OFFSET.
void BuildGizmoDragGhost(const AppCommandState& cmd, std::vector<float>* outLines,
                         std::vector<float>* outCircles);
