#pragma once

#include "render/Camera.hpp"

struct ImDrawList;

/// ViewCube — the model viewport's orientation widget (REQ-059).
///
/// Draws a labelled cube plus a W/N/S/E compass ring in the viewport's top-right corner, matching
/// the mockup supplied with the original feature request. Clicking a face snaps the camera to that
/// standard view.
///
/// **Why this is in-tree rather than the vendored gizmo:** ImOGuizmo was adopted first (the user's
/// FINDING-2 ruling) and shipped, but its axis-ball collapses to a dot in plan view — which is the
/// default orientation and the one users spend most of their time in — so it conveyed no
/// orientation at all. Replaced after that was observed; see the 2026-08-11 decision-log entry.
///
/// Drawing is pure ImGui (no GL), and the widget owns no state: it reads the camera, and reports a
/// requested orientation back to the caller.
namespace viewcube {

/// Result of one frame of interaction.
struct Result {
  bool changed = false;      ///< True when the user requested a new orientation this frame.
  float azimuthDeg = 0.f;    ///< Requested orientation (valid only when \c changed).
  float elevationDeg = 90.f;
  bool hovered = false;      ///< True when the cursor is over the widget, so the caller can swallow the click.
};

/// Elevation of a standard isometric view: atan(1/sqrt(2)) — the angle at which the three axes
/// foreshorten equally. AutoCAD's SW/SE/NE/NW Isometric presets all use it.
inline constexpr float kIsometricElevationDeg = 35.26438968f;

/// Draw the widget and handle interaction for this frame.
///
/// \param dl        draw list to render into (the viewport's window draw list).
/// \param cam       current camera — the cube tracks its orientation continuously.
/// \param originPx  top-left corner of the widget's square, in screen pixels.
/// \param sizePx    edge length of the widget's square, in screen pixels.
/// \param mousePx   cursor position in screen pixels.
/// \param clicked   whether the left button was pressed this frame.
/// \param ucsAzimuthOffsetDeg  rotation of the active coordinate system about Z. The compass
///        letters and the 90-degree rotation arrows are relative to THIS, so under a rotated UCS
///        "square with north" means square with the UCS's north, not the world's (REQ-059).
///        Zero for the WCS.
Result Draw(ImDrawList* dl, const Camera& cam, float originX, float originY, float sizePx, float mouseX,
            float mouseY, bool clicked, float ucsAzimuthOffsetDeg = 0.f);

}  // namespace viewcube
