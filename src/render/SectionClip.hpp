#pragma once

#include <cmath>
#include <vector>

#include "util/ray3d.hpp"
#include "util/ucs.hpp"

/// The live section clip plane (REQ-341 / ADR-058, GitHub issue #149 acceptance 6).
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

/// Build the clip plane from the active UCS (REQ-341).
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

/// The four corners of the rectangle drawn to SHOW the user where the clip plane is (REQ-341).
///
/// The plane itself is infinite and invisible; this is the finite patch of it that gets drawn.
/// Without it the command has no visible effect at all in the view a user starts in: a level cut
/// seen from directly above removes the top of a solid and leaves its outline in exactly the same
/// place on screen, so the picture does not change and the feature reads as broken. Measured, not
/// supposed — two captures of that case came back byte-identical.
struct SectionClipIndicator {
  bool valid = false;
  ray3d::Vec3 corner[4]{};  ///< world space, wound counter-clockwise about the plane normal
};

/// Build the indicator rectangle for \p p, sized to cover the world box \p bbMin..\p bbMax with a
/// margin so its edges stand clear of the model rather than coinciding with it.
///
/// The rectangle is built in the plane's OWN axes, not in world X/Y, so it stays a rectangle on the
/// plane under any orientation — a tilted UCS included. The two in-plane axes come from an
/// orthonormal basis around the normal; which way they point is arbitrary and does not matter,
/// because the extent is measured from the model's own corners either way.
[[nodiscard]] inline SectionClipIndicator SectionClipIndicatorQuad(const SectionClipPlane& p,
                                                                   const ray3d::Vec3& bbMin,
                                                                   const ray3d::Vec3& bbMax,
                                                                   double marginFrac = 0.15) {
  SectionClipIndicator out;
  if (!p.active)
    return out;
  const ray3d::Vec3 n = ray3d::Normalize(ray3d::Vec3{p.nx, p.ny, p.nz});
  if (!(std::isfinite(n.x) && std::isfinite(n.y) && std::isfinite(n.z)))
    return out;
  if (std::fabs(ray3d::Length(n) - 1.0) > 1e-6)
    return out;  // a degenerate normal has no plane to draw

  // An in-plane basis. The helper axis is chosen to be the one LEAST aligned with the normal, so
  // the cross product never collapses — picking a fixed axis breaks precisely when the plane faces
  // along it, which for a level cut (normal = +Z) is the most common case there is.
  const ray3d::Vec3 helper = (std::fabs(n.z) < 0.9) ? ray3d::Vec3{0.0, 0.0, 1.0} : ray3d::Vec3{1.0, 0.0, 0.0};
  const ray3d::Vec3 u = ray3d::Normalize(ray3d::Cross(helper, n));
  const ray3d::Vec3 v = ray3d::Cross(n, u);

  // Measure the model's extent in those axes, from all eight box corners: an oblique plane through
  // a box is not covered by projecting only two of them.
  double uMin = 1e300, uMax = -1e300, vMin = 1e300, vMax = -1e300;
  for (int i = 0; i < 8; ++i) {
    const ray3d::Vec3 c{(i & 1) ? bbMax.x : bbMin.x, (i & 2) ? bbMax.y : bbMin.y,
                        (i & 4) ? bbMax.z : bbMin.z};
    const double du = ray3d::Dot(c, u);
    const double dv = ray3d::Dot(c, v);
    uMin = std::fmin(uMin, du);
    uMax = std::fmax(uMax, du);
    vMin = std::fmin(vMin, dv);
    vMax = std::fmax(vMax, dv);
  }
  if (!(uMax >= uMin && vMax >= vMin))
    return out;

  // A degenerate span still has to draw something, or a flat drawing (every solid at one
  // elevation, say) would produce a zero-size rectangle and look like nothing was added.
  const double spanU = std::fmax(uMax - uMin, 1e-6);
  const double spanV = std::fmax(vMax - vMin, 1e-6);
  const double base = std::fmax(spanU, spanV);
  const double padU = std::fmax(spanU * marginFrac, base * 0.02);
  const double padV = std::fmax(spanV * marginFrac, base * 0.02);
  const double u0 = uMin - padU, u1 = uMax + padU;
  const double v0 = vMin - padV, v1 = vMax + padV;

  // Lift the rectangle onto the plane: any point with the right (u, v) plus the plane's own offset
  // along the normal, which is `c` because n is a unit vector.
  auto at = [&](double su, double sv) {
    return ray3d::Vec3{u.x * su + v.x * sv + n.x * p.c, u.y * su + v.y * sv + n.y * p.c,
                       u.z * su + v.z * sv + n.z * p.c};
  };
  out.corner[0] = at(u0, v0);
  out.corner[1] = at(u1, v0);
  out.corner[2] = at(u1, v1);
  out.corner[3] = at(u0, v1);
  out.valid = true;
  return out;
}

/// How the section plane is DRAWN (REQ-338 / ADR-058, GitHub issue #479 acceptance 3).
///
/// A translucent rectangle with an outline is enough to say "a plane is here"; it is not enough to
/// find at a glance in a busy drawing, and at a grazing angle it is very nearly nothing at all.
/// AutoCAD draws its section plane **hatched, with a heavy line along its base**, and that is what
/// the user asked for by name — "visually similar to AutoCAD's, for ease of use".
///
/// The geometry is in world coordinates, like the indicator's corners: the renderer rebases it to
/// the view anchor at draw time, along with everything else.
struct SectionPlaneGraphics {
  bool valid = false;
  /// Hatch segments as consecutive PAIRS — `hatch[2i]` to `hatch[2i+1]` — which is `GL_LINES`
  /// order, so the renderer uploads the vector without rearranging it.
  std::vector<ray3d::Vec3> hatch;
  /// The section line: the rectangle's base edge, drawn heavier than the outline. In AutoCAD this
  /// is the edge the direction arrows hang off; the arrows are slice 3's, with the grips.
  ray3d::Vec3 lineA{};
  ray3d::Vec3 lineB{};
};

/// Hatch density, as a count across the rectangle's diagonal.
///
/// Derived from the rectangle's own size rather than from a world distance, so the pattern reads
/// the same on a 4 ft manhole and a 900 ft parcel, and so the segment count cannot run away at
/// survey scale. It is deliberately not screen-derived: this geometry is built once per frame from
/// world quantities and must not change with zoom, or the plane would shimmer while the view moves.
inline constexpr int kSectionPlaneHatchAcrossDiagonal = 22;

/// A hard ceiling on emitted segments. Nothing in the sizing above should approach it; it is here
/// so a degenerate rectangle cannot turn into an unbounded upload.
inline constexpr int kSectionPlaneHatchMaxSegments = 256;

/// Build the hatch and section line for the rectangle in \p ind.
///
/// The hatch runs at **45 degrees in the plane's own axes**, so it is diagonal on the plane however
/// the plane is oriented in space, and it is clipped to the rectangle analytically rather than
/// drawn long and masked — there is no mask available in the overlay pass this is drawn in.
[[nodiscard]] inline SectionPlaneGraphics SectionPlaneGraphicsFor(const SectionClipIndicator& ind) {
  SectionPlaneGraphics out;
  if (!ind.valid)
    return out;

  // Rebuild the rectangle's own frame from its corners: corner[0] is the origin, and the two edges
  // leaving it are the in-plane axes. Taken from the corners rather than recomputed from the plane
  // normal so the hatch cannot land on a different basis than the quad it fills.
  const ray3d::Vec3 org = ind.corner[0];
  const ray3d::Vec3 eu = ray3d::Sub(ind.corner[1], ind.corner[0]);
  const ray3d::Vec3 ev = ray3d::Sub(ind.corner[3], ind.corner[0]);
  const double lu = ray3d::Length(eu);
  const double lv = ray3d::Length(ev);
  if (!(lu > 1e-9 && lv > 1e-9))
    return out;
  const ray3d::Vec3 u = ray3d::Scale(eu, 1.0 / lu);
  const ray3d::Vec3 v = ray3d::Scale(ev, 1.0 / lv);

  // Work in (s, t) = distance along u, distance along v. The rectangle is [0, lu] x [0, lv], and a
  // 45-degree line is `s - t = k`. Perpendicular spacing between consecutive k is k/sqrt(2), so the
  // k step for a wanted spacing is spacing*sqrt(2).
  const double diag = std::sqrt(lu * lu + lv * lv);
  const double spacing = diag / static_cast<double>(kSectionPlaneHatchAcrossDiagonal);
  if (!(spacing > 1e-12))
    return out;
  const double kStep = spacing * 1.4142135623730951;

  auto at = [&](double s, double t) {
    return ray3d::Vec3{org.x + u.x * s + v.x * t, org.y + u.y * s + v.y * t,
                       org.z + u.z * s + v.z * t};
  };

  // k spans (-lv, lu): at k = -lv the line touches the corner (0, lv), at k = lu the corner (lu, 0).
  // Both ends are skipped, since a line through one corner has zero length.
  int emitted = 0;
  for (double k = -lv + kStep; k < lu - 1e-12 && emitted < kSectionPlaneHatchMaxSegments; k += kStep) {
    // s = t + k, so t is bounded by both the rectangle's t range and its s range.
    const double t0 = std::fmax(0.0, -k);
    const double t1 = std::fmin(lv, lu - k);
    if (!(t1 - t0 > 1e-9))
      continue;
    out.hatch.push_back(at(t0 + k, t0));
    out.hatch.push_back(at(t1 + k, t1));
    ++emitted;
  }

  // The section line is the rectangle's LOWEST edge in world Z — "base" meaning what a person
  // looking at the model would call the bottom. A level plane has four edges at one elevation, so
  // the comparison is strict and the first edge wins: an arbitrary but stable choice, and a plane
  // seen face-on has no visible base anyway.
  int best = 0;
  double bestZ = 1e300;
  for (int i = 0; i < 4; ++i) {
    const ray3d::Vec3& a = ind.corner[i];
    const ray3d::Vec3& b = ind.corner[(i + 1) & 3];
    const double midZ = 0.5 * (a.z + b.z);
    if (midZ < bestZ) {
      bestZ = midZ;
      best = i;
    }
  }
  out.lineA = ind.corner[best];
  out.lineB = ind.corner[(best + 1) & 3];
  out.valid = true;
  return out;
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
