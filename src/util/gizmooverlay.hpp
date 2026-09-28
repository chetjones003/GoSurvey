#pragma once

#include <vector>

/// The translate gizmo's linework, ready to draw (REQ-060, GitHub issue #148 Phase 5 slice 4b).
///
/// Its own struct, and its own channel in the renderer, for one reason: **colour**. Every other
/// overlay in this viewport is a single colour per batch — selection yellow, hover blue, preview
/// grey — and a gizmo whose three handles were all one colour is not a gizmo, it is a star. X red,
/// Y green, Z blue is what every 3D application on the user's machine already draws, so the axis a
/// handle belongs to is read rather than deduced.
///
/// A plain data carrier, like \c CadSubObjectOverlay next door in `cadsolid.hpp`: the viewport layer
/// fills it from `AppCommandState`, the renderer draws it, and neither has to include the other's
/// headers. Coordinates are storage/world floats, `GL_LINES`, six floats per segment.
struct CadGizmoOverlay {
  /// One buffer per axis, indexed 0 = X, 1 = Y, 2 = Z, holding that handle's shaft and arrowhead.
  std::vector<float> axis[3];
  /// True for the axis under the cursor, or the one being dragged: it draws in the selection accent
  /// instead of its own colour, which is what says "this click will grab THIS handle".
  bool hot[3] = {false, false, false};
  /// The drag guide: the full axis line through the anchor while a drag is armed, so the user can
  /// see the track the selection is sliding along rather than only the short handle.
  std::vector<float> guide;

  /// True when the single handle in `axis[0]` is a solid FACE's own normal rather than a UCS axis
  /// (issue #148 acceptance 4). It then draws PURPLE — the colour a selected face already uses —
  /// instead of the X handle's red, because it is not X and should not claim to be.
  bool faceMode = false;

  /// Which operation the single handle in `axis[0]` belongs to when there is only one, so the
  /// renderer can colour it without re-deriving the mode (TASK-232). 0 = translate (the normal
  /// three-axis case, or the purple face handle), 1 = rotate (three rings, coloured per axis),
  /// 2 = scale.
  ///
  /// An int rather than the `CadGizmoOp` enum for the reason this whole struct exists: it is a plain
  /// carrier between two layers that do not include each other's headers.
  int soloOp = 0;

  /// The corner marks between each pair of axes (D-2026-09-28-c), indexed 0 = XY, 1 = YZ, 2 = ZX:
  /// exactly two segments each — the first parallel to the plane's first axis and drawn in that
  /// axis's colour, the second parallel to (or nearer) its second axis, in that one's. Under
  /// Translate they are the plane handles' squares; under Scale, the triangle marks between axes.
  std::vector<float> plane[3];
  /// True for the plane handle under the cursor or being dragged, like \ref hot.
  bool planeHot[3] = {false, false, false};
  /// A small circle round the anchor, marking the base point the gizmo sits on.
  std::vector<float> center;
  [[nodiscard]] bool empty() const {
    return axis[0].empty() && axis[1].empty() && axis[2].empty() && guide.empty() &&
           plane[0].empty() && plane[1].empty() && plane[2].empty() && center.empty();
  }
};
