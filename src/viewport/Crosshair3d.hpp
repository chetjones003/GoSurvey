#pragma once

#include <cmath>

#include "render/Camera.hpp"
#include "util/ucs.hpp"

/// The 3D crosshair cursor (REQ-310, GitHub #144 / Phase 1 of #120).
///
/// AutoCAD's 3D crosshair replaces the two screen-aligned arms with the **active UCS's three axes,
/// projected into the view**. That is not decoration: with the camera orbited or the UCS rotated,
/// a screen-aligned crosshair says nothing about which way the drawing plane runs, so the user has
/// no cue for where a typed X or Y will actually go. The axes ARE that cue — an arm that
/// foreshortens to nothing is telling you it points at the viewer.
///
/// Pure and dependency-free (no ImGui, no GL, no AppCommandState) so the projection is unit-testable
/// without a window, the same rule `Camera` itself follows (ADR-002). The caller owns the drawing.
namespace crosshair3d {

/// One projected axis, as a screen-space delta from the crosshair centre.
///
/// Screen Y grows DOWNWARD (the ImGui convention the viewport draws in), which is why the up term
/// is negated when this is built.
struct Arm {
  float dx = 0.f;
  float dy = 0.f;
  /// False when the axis projects to less than \ref kMinArmPx — it is pointing at (or away from)
  /// the viewer and has collapsed. Drawing a stub there is noise; its absence is the information.
  bool visible = false;
};

/// The three axes of the active frame, in the order they should be DRAWN (z first).
struct Triad {
  Arm x;
  Arm y;
  Arm z;
};

/// Axis colours, as plain RGB bytes so this header stays free of ImGui (which owns `ImU32`).
///
/// The near-universal CAD convention — X red, Y green, Z blue — so neither the crosshair nor the
/// UCS icon needs a legend. **They are defined once, here, and used by both**: the icon and the
/// cursor describe the same three axes, and a build where they disagreed about which one is green
/// would be worse than either alone.
struct AxisRgb {
  unsigned char r;
  unsigned char g;
  unsigned char b;
};
inline constexpr AxisRgb kAxisColorX{226, 96, 96};
inline constexpr AxisRgb kAxisColorY{112, 198, 112};
inline constexpr AxisRgb kAxisColorZ{110, 150, 235};

/// Below this projected pixel length an axis is treated as collapsed and not drawn.
///
/// Matches the UCS icon's own threshold so the two indicators agree about when an axis has gone
/// edge-on: it would be incoherent for the icon to drop an axis the crosshair still shows.
inline constexpr float kMinArmPx = 6.f;

/// Screen-space direction of a world direction, using the camera's own screen basis.
///
/// Projecting the DIRECTION rather than two projected world points is what keeps this correct for a
/// cursor that lives at an arbitrary screen pixel: the arms describe an orientation, not a position,
/// so they must not depend on where the cursor happens to be. It also means an axis edge-on to the
/// camera collapses to a point, which is the visual cue that it points at the viewer.
inline Arm ProjectAxis(const Camera& cam, const ray3d::Vec3& axis, float lenPx) {
  const ray3d::Vec3 right = cam.RightWorld();
  const ray3d::Vec3 up = cam.UpWorld();
  Arm a;
  a.dx = static_cast<float>(ray3d::Dot(axis, right)) * lenPx;
  a.dy = -static_cast<float>(ray3d::Dot(axis, up)) * lenPx;
  a.visible = std::sqrt(a.dx * a.dx + a.dy * a.dy) >= kMinArmPx;
  return a;
}

/// Project the active frame's three axes for a crosshair of arm length \p armPx.
///
/// \p armPx is the HALF length: each axis is drawn as a full line from `centre - delta` to
/// `centre + delta`, because a crosshair straddles its centre. A caller wanting only the positive
/// half (an icon-style triad) uses the delta alone.
inline Triad Compute(const Camera& cam, const ucs::Ucs& frame, float armPx) {
  Triad t;
  if (!(armPx > 0.f))
    return t;
  t.x = ProjectAxis(cam, frame.xAxis, armPx);
  t.y = ProjectAxis(cam, frame.yAxis, armPx);
  t.z = ProjectAxis(cam, frame.zAxis, armPx);
  return t;
}

/// True when the projected triad has collapsed so far that it no longer reads as a crosshair.
///
/// Only reachable if two axes go edge-on at once, which an orthonormal frame cannot do — so this is
/// a guard against a degenerate (non-orthonormal, hand-edited) UCS rather than an expected state.
/// The caller falls back to the plain 2D crosshair, which is always readable (REQ-201's posture: a
/// bad value degrades to the safe default rather than leaving the user with no cursor).
///
/// This is the pure-geometry guard only. The renderer also gaps each arm by the pickbox, and an
/// arm shorter than that gap draws nothing — so the renderer must layer \ref DrawableCount on top
/// of this before it commits to the triad, or a large pickbox can leave a one-armed crosshair with
/// no fallback.
inline bool Degenerate(const Triad& t) {
  const int n = (t.x.visible ? 1 : 0) + (t.y.visible ? 1 : 0) + (t.z.visible ? 1 : 0);
  return n < 2;
}

/// The pickbox half-extent along this arm's own screen direction — the length the renderer leaves
/// clear at the centre so the selection square stays readable, whatever angle the axis arrives at.
inline float ArmGapPx(const Arm& a, float pickHalfPxX, float pickHalfPxY) {
  const float len = std::sqrt(a.dx * a.dx + a.dy * a.dy);
  if (len <= 0.f)
    return 0.f;
  const float ux = a.dx / len;
  const float uy = a.dy / len;
  return std::sqrt((pickHalfPxX * ux) * (pickHalfPxX * ux) + (pickHalfPxY * uy) * (pickHalfPxY * uy));
}

/// True when this arm actually renders: it cleared \ref kMinArmPx (\ref Arm::visible) AND its
/// projection is longer than the pickbox gap it has to bridge.
inline bool ArmDrawable(const Arm& a, float pickHalfPxX, float pickHalfPxY) {
  return a.visible &&
         std::sqrt(a.dx * a.dx + a.dy * a.dy) > ArmGapPx(a, pickHalfPxX, pickHalfPxY);
}

/// How many of the three axes will actually be drawn at this pickbox size. Fewer than two and the
/// triad is not a crosshair any more — the renderer falls back to the 2D arms.
inline int DrawableCount(const Triad& t, float pickHalfPxX, float pickHalfPxY) {
  return (ArmDrawable(t.x, pickHalfPxX, pickHalfPxY) ? 1 : 0) +
         (ArmDrawable(t.y, pickHalfPxX, pickHalfPxY) ? 1 : 0) +
         (ArmDrawable(t.z, pickHalfPxX, pickHalfPxY) ? 1 : 0);
}

}  // namespace crosshair3d
