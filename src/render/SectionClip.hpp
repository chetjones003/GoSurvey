#pragma once

#include <cmath>

#include "util/ray3d.hpp"
#include "util/ucs.hpp"

/// The live section clip plane (REQ-336 / ADR-056, GitHub issue #149 acceptance 6).
///
/// **Header-only and GL-free, for the reason `Camera.hpp` gives for the same choice**: the plane
/// arithmetic is where this feature can silently go wrong, and it must be unit-testable without a
/// window (ADR-002). Everything the renderer does with a clip plane beyond setting one uniform is
/// in this file.
///
/// The plane is stated in **world coordinates**, because that is the only frame the command layer
/// and the document share. The renderer rebases it — see \ref SectionClipToShaderVec4, which is the
/// half of this that a plausible implementation gets wrong.
struct SectionClipPlane {
  bool active = false;
  /// Unit normal in WORLD axes. The half **kept** is the one this points away from:
  /// `dot(n, p) <= c`. Material on the +n side is what disappears.
  double nx = 0.0;
  double ny = 0.0;
  double nz = 1.0;
  /// The plane is `dot(n, p) = c`.
  double c = 0.0;

  /// True when \p p survives the clip. The renderer's shader computes the same predicate; this is
  /// the CPU statement of it, and the tests hold the two to each other.
  [[nodiscard]] bool KeepsWorldPoint(double x, double y, double z) const {
    if (!active)
      return true;
    return (nx * x + ny * y + nz * z) <= c;
  }
};

/// Build the clip plane from the active UCS (REQ-336).
///
/// **The plane IS the active UCS plane**, offset along its own Z — the same decision `SECTION`
/// made (D-2026-09-09-i) and for the same reason: `ucs::Ucs` is this project's plane abstraction
/// (REQ-311, D-2026-08-31-e), and "set the work plane, then look at the cut" is the gesture this
/// codebase already teaches. A one-off three-point plane is the natural second increment, exactly
/// as it is for `SECTION`.
///
/// \p offset slides the plane along the UCS Z from the UCS origin, in drawing units. \p flip
/// reverses which half survives.
[[nodiscard]] inline SectionClipPlane SectionClipFromUcs(const ucs::Ucs& frame, double offset, bool flip) {
  SectionClipPlane p;
  p.active = true;
  const double s = flip ? -1.0 : 1.0;
  p.nx = frame.zAxis.x * s;
  p.ny = frame.zAxis.y * s;
  p.nz = frame.zAxis.z * s;
  // The plane passes through origin + offset * zAxis, so c = dot(n, origin) + offset*dot(n, zAxis),
  // and dot(n, zAxis) is +/-1 because n IS the (possibly negated) unit z axis.
  const double dOrigin = p.nx * frame.origin.x + p.ny * frame.origin.y + p.nz * frame.origin.z;
  p.c = dOrigin + offset * s;
  return p;
}

/// Rewrite a world-space clip plane into the space the renderer's vertex shaders actually receive,
/// and pack it as the `uClipPlane` vec4 they read.
///
/// **This is the load-bearing function in the whole feature.** Vertices do NOT arrive in world
/// coordinates: `ViewportRenderer` uploads them with XY relative to the view anchor and Z absolute
/// ("Vertices arrive with XY relative to the view anchor but Z ABSOLUTE"), and the anchor **is the
/// pan point**, so it moves whenever the user pans. A plane handed to the shader in world
/// coordinates is therefore in the wrong frame.
///
/// Measured (probe P7, 2026-09-10), for the uncorrected version:
///   - at the origin ................................. exact, 0.000000 ft
///   - at easting 2,196,000 .......................... **2,196,000 ft out**
///   - on an oblique plane at state-plane coordinates . 2,542,755.99 ft out
///   - view panned 250 ft ............................ **the clip moves 250 ft**
///   - a HORIZONTAL cut (n = +Z) ..................... exact, in both versions
///
/// The last line is why this needs a comment and a test rather than care: the anchoring only ever
/// covered X and Y, so a level cut — the first plane anyone tries — cannot expose the bug, and
/// neither can any test written at the origin.
///
/// The shader keeps a vertex when `dot(out.xyz, aPos) + out.w >= 0`. Substituting
/// `p = (aPos.x + ax, aPos.y + ay, aPos.z)` into `c - dot(n, p) >= 0` gives the packing below.
///
/// The subtraction is done in **double and narrowed once**: at survey magnitude `c` and the anchor
/// term are both ~2.2e6 and their difference is small, so the float that reaches the GPU carries
/// the offset at full precision. Computing it in float would quantize `c` to about 0.25 ft, which
/// is 125x REQ-101's +/-0.002 ft.
inline void SectionClipToShaderVec4(const SectionClipPlane& p, double anchorX, double anchorY, float* out4) {
  if (!out4)
    return;
  out4[0] = static_cast<float>(-p.nx);
  out4[1] = static_cast<float>(-p.ny);
  out4[2] = static_cast<float>(-p.nz);
  // No Z term: the anchoring covers X and Y only, because vertex Z is already absolute.
  out4[3] = static_cast<float>(p.c - p.nx * anchorX - p.ny * anchorY);
}

/// A vec4 that keeps every vertex, for the passes that must not clip (the grid, and every UI
/// overlay). Stated as a function rather than written out at each call site so "what does 'do not
/// clip' look like?" has one answer.
inline void SectionClipDisabledVec4(float* out4) {
  if (!out4)
    return;
  out4[0] = 0.f;
  out4[1] = 0.f;
  out4[2] = 0.f;
  out4[3] = 1.f;  // dot(0, aPos) + 1 == 1 >= 0 for every vertex
}
