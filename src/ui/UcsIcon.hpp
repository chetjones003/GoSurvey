#pragma once

#include "render/Camera.hpp"
#include "util/ucs.hpp"

struct ImDrawList;

/// The UCS icon — the viewport's coordinate-system indicator (REQ-154, GitHub #126).
///
/// Draws the active UCS's X / Y / Z axes as a foreshortened triad in the viewport's bottom-left
/// corner, with a "W" at its origin when the frame is the World Coordinate System.
///
/// **It is tied to the UCS state, not decoration.** Once coordinate entry, ORTHO and the grid all
/// follow a frame the user cannot otherwise see, the icon is the only thing on screen that says
/// which frame that is — a rotated UCS with no indicator makes every coordinate on screen
/// ambiguous. It reads `ucs::Ucs` and the camera and owns no state of its own, so it cannot drift
/// from what the rest of the application believes.
///
/// Pure ImGui (no GL), like ViewCube beside it, so it needs no renderer changes.
namespace ucsicon {

/// Draw the icon for this frame.
///
/// \param dl       draw list to render into (the viewport's window draw list).
/// \param cam      current camera — the triad foreshortens with the view.
/// \param frame    the active UCS, in any space: only its AXES are used, never its origin, because
///                 the icon sits at a fixed screen corner rather than at the UCS origin.
/// \param originX / \param originY  the triad's root, in screen pixels.
/// \param sizePx   length of a full-length axis arm, in pixels.
/// \param isWorld  true when \p frame is the WCS, which draws the "W" marker.
void Draw(ImDrawList* dl, const Camera& cam, const ucs::Ucs& frame, float originX, float originY, float sizePx,
          bool isWorld);

}  // namespace ucsicon
