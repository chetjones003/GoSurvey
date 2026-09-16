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

/// The plane's own in-plane axes, as one function so every consumer agrees on them (REQ-339).
///
/// The rectangle, the hatch, the grips and the grip PICK all work in this basis. If any two of them
/// derived it separately and differed, the user would click one place and grab another — so it is
/// computed once, here, and the fact that its direction is arbitrary becomes harmless.
///
/// The helper axis is the one LEAST aligned with the normal, so the cross product never collapses.
/// A fixed helper breaks precisely when the plane faces along it, which for a level cut (normal =
/// +Z) is the most common case there is.
///
/// False when the normal is not a usable unit vector, in which case there is no plane to speak of.
[[nodiscard]] inline bool SectionClipPlaneBasis(const SectionClipPlane& p, ray3d::Vec3* outU,
                                                ray3d::Vec3* outV, ray3d::Vec3* outN = nullptr) {
  const ray3d::Vec3 n = ray3d::Normalize(ray3d::Vec3{p.nx, p.ny, p.nz});
  if (!(std::isfinite(n.x) && std::isfinite(n.y) && std::isfinite(n.z)))
    return false;
  if (std::fabs(ray3d::Length(n) - 1.0) > 1e-6)
    return false;
  const ray3d::Vec3 helper =
      (std::fabs(n.z) < 0.9) ? ray3d::Vec3{0.0, 0.0, 1.0} : ray3d::Vec3{1.0, 0.0, 0.0};
  const ray3d::Vec3 u = ray3d::Normalize(ray3d::Cross(helper, n));
  if (outU)
    *outU = u;
  if (outV)
    *outV = ray3d::Cross(n, u);
  if (outN)
    *outN = n;
  return true;
}

/// A section plane's rectangle stated in its OWN basis — centre and half-sizes along
/// \ref SectionClipPlaneBasis's u and v (REQ-339).
///
/// Invalid means "derive it from the model", which is what REQ-338 shipped and what a freshly
/// placed plane uses. It becomes valid the moment a stretch grip is dragged, because from then on
/// the size is something the user chose and must not be silently re-derived on the next frame.
///
/// Stated in the plane's basis rather than as four world corners so that sliding the plane along
/// its normal — the common gesture — leaves it completely untouched.
struct SectionPlaneExtent {
  bool valid = false;
  double cu = 0.0;
  double cv = 0.0;
  double halfU = 0.0;
  double halfV = 0.0;
};

/// Build the indicator rectangle for \p p, sized to cover the world box \p bbMin..\p bbMax with a
/// margin so its edges stand clear of the model rather than coinciding with it — or, when \p ext is
/// valid, to the size the user stretched it to (REQ-339).
///
/// The rectangle is built in the plane's OWN axes, not in world X/Y, so it stays a rectangle on the
/// plane under any orientation — a tilted UCS included.
[[nodiscard]] inline SectionClipIndicator SectionClipIndicatorQuad(const SectionClipPlane& p,
                                                                   const ray3d::Vec3& bbMin,
                                                                   const ray3d::Vec3& bbMax,
                                                                   double marginFrac = 0.15,
                                                                   const SectionPlaneExtent& ext = {}) {
  SectionClipIndicator out;
  if (!p.active)
    return out;
  ray3d::Vec3 n{}, u{}, v{};
  if (!SectionClipPlaneBasis(p, &u, &v, &n))
    return out;  // a degenerate normal has no plane to draw

  // Lift a point with the given (u, v) onto the plane: the plane's own offset along the normal is
  // `c`, because n is a unit vector.
  auto at = [&](double su, double sv) {
    return ray3d::Vec3{u.x * su + v.x * sv + n.x * p.c, u.y * su + v.y * sv + n.y * p.c,
                       u.z * su + v.z * sv + n.z * p.c};
  };

  // A stretched rectangle is the user's own size and is used verbatim. Nothing about the model
  // enters here — that is the whole point of the grips.
  if (ext.valid && ext.halfU > 1e-9 && ext.halfV > 1e-9) {
    out.corner[0] = at(ext.cu - ext.halfU, ext.cv - ext.halfV);
    out.corner[1] = at(ext.cu + ext.halfU, ext.cv - ext.halfV);
    out.corner[2] = at(ext.cu + ext.halfU, ext.cv + ext.halfV);
    out.corner[3] = at(ext.cu - ext.halfU, ext.cv + ext.halfV);
    out.valid = true;
    return out;
  }

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

  // The section line runs through the MIDDLE of the rectangle, along its u axis — the bright line
  // across the centre of AutoCAD's section plane, and where its two length grips live (REQ-339,
  // user request 2026-09-11 with a screenshot).
  //
  // REQ-338 put it on the lowest edge instead. That was wrong in the way an edge is always wrong
  // here: it coincides with the rectangle's own outline, so it adds no information, and it leaves
  // the middle of the plane — where the grips have to be — unmarked.
  out.lineA = ray3d::Vec3{org.x + u.x * 0.0 + v.x * (lv * 0.5), org.y + u.y * 0.0 + v.y * (lv * 0.5),
                          org.z + u.z * 0.0 + v.z * (lv * 0.5)};
  out.lineB = ray3d::Vec3{out.lineA.x + u.x * lu, out.lineA.y + u.y * lu, out.lineA.z + u.z * lu};
  out.valid = true;
  return out;
}

/// The handles on a selected section plane (REQ-339, GitHub issue #479 acceptance 5-7).
///
/// Deliberately a small fixed set, in the order the user asked for them. `Move` slides the plane
/// along its own normal, which is the gesture the whole feature exists for; `Flip` reverses which
/// half survives; the four stretch handles resize the rectangle without changing what is cut.
enum class SectionPlaneGrip : int {
  None = -1,
  Move = 0,    ///< Centre of the section line. Drag along the plane NORMAL.
  Flip,        ///< A click, not a drag. Sits off the line so it cannot be grabbed by accident.
  LengthNeg,   ///< The section line's -u end. Drag along u.
  LengthPos,   ///< The section line's +u end.
  HeightNeg,   ///< Mid-point of the -v edge. Drag along v.
  HeightPos,   ///< Mid-point of the +v edge.
  Count        ///< Not a grip; the size of \ref SectionPlaneGrips::at.
};

inline constexpr int kSectionPlaneGripCount = static_cast<int>(SectionPlaneGrip::Count);

/// How much larger the FLIP handle is drawn, and grabbed, than the other five (REQ-340). From the
/// user's GUI pass (2026-09-16): every other handle was easy to find, the flip symbol "at times
/// hard to see". A first step up, to be tuned from there — so it is one number, used by both the
/// renderer and `PickSectionPlaneGrip`, and what looks bigger is also easier to click.
inline constexpr double kSectionPlaneFlipScale = 1.6;

/// Where each handle sits, in world coordinates.
struct SectionPlaneGrips {
  bool valid = false;
  ray3d::Vec3 at[kSectionPlaneGripCount]{};
  /// Unit directions the draggable handles move along, parallel to \ref at. The normal for `Move`,
  /// ±u for the length pair, ±v for the height pair; zero for `Flip`, which is a click.
  ray3d::Vec3 dir[kSectionPlaneGripCount]{};
};

/// Build the handles for the rectangle in \p ind on plane \p p (REQ-339).
///
/// Positions come from the RECTANGLE, not from the stored extent, so a plane still sized to the
/// model gets grips in the right place before it has ever been stretched — and so the grip a user
/// aims at is by construction the grip that is drawn.
[[nodiscard]] inline SectionPlaneGrips SectionPlaneGripsFor(const SectionClipIndicator& ind,
                                                            const SectionClipPlane& p) {
  SectionPlaneGrips g;
  if (!ind.valid)
    return g;
  ray3d::Vec3 n{}, u{}, v{};
  if (!SectionClipPlaneBasis(p, &u, &v, &n))
    return g;

  const ray3d::Vec3 eu = ray3d::Sub(ind.corner[1], ind.corner[0]);
  const ray3d::Vec3 ev = ray3d::Sub(ind.corner[3], ind.corner[0]);
  const double lu = ray3d::Length(eu);
  const double lv = ray3d::Length(ev);
  if (!(lu > 1e-9 && lv > 1e-9))
    return g;
  const ray3d::Vec3 centre{ind.corner[0].x + 0.5 * (eu.x + ev.x),
                           ind.corner[0].y + 0.5 * (eu.y + ev.y),
                           ind.corner[0].z + 0.5 * (eu.z + ev.z)};
  auto off = [](const ray3d::Vec3& base, const ray3d::Vec3& d, double s) {
    return ray3d::Vec3{base.x + d.x * s, base.y + d.y * s, base.z + d.z * s};
  };

  const int kMove = static_cast<int>(SectionPlaneGrip::Move);
  const int kFlip = static_cast<int>(SectionPlaneGrip::Flip);
  const int kLenN = static_cast<int>(SectionPlaneGrip::LengthNeg);
  const int kLenP = static_cast<int>(SectionPlaneGrip::LengthPos);
  const int kHgtN = static_cast<int>(SectionPlaneGrip::HeightNeg);
  const int kHgtP = static_cast<int>(SectionPlaneGrip::HeightPos);

  g.at[kMove] = centre;
  g.dir[kMove] = n;

  // The flip handle sits a quarter of the way along +u from the centre, ON the section line. Off
  // the centre so it cannot be confused with the move handle, and on the line so it reads as part
  // of the same control rather than as a stray marker floating on the plane.
  g.at[kFlip] = off(centre, u, lu * 0.25);
  g.dir[kFlip] = ray3d::Vec3{0.0, 0.0, 0.0};  // a click, not a drag

  g.at[kLenN] = off(centre, u, -lu * 0.5);
  g.dir[kLenN] = ray3d::Vec3{-u.x, -u.y, -u.z};
  g.at[kLenP] = off(centre, u, lu * 0.5);
  g.dir[kLenP] = u;

  g.at[kHgtN] = off(centre, v, -lv * 0.5);
  g.dir[kHgtN] = ray3d::Vec3{-v.x, -v.y, -v.z};
  g.at[kHgtP] = off(centre, v, lv * 0.5);
  g.dir[kHgtP] = v;

  g.valid = true;
  return g;
}

/// The rectangle in \p ind restated as a stored extent (REQ-339).
///
/// Used to SEED the stored extent the first time a stretch grip is dragged: the plane keeps exactly
/// the size it is showing, and only the dragged edge moves. Without this, the first stretch would
/// snap the rectangle to some default and then resize it, which reads as the plane jumping.
[[nodiscard]] inline SectionPlaneExtent SectionPlaneExtentFromQuad(const SectionClipIndicator& ind,
                                                                   const SectionClipPlane& p) {
  SectionPlaneExtent e;
  if (!ind.valid)
    return e;
  ray3d::Vec3 n{}, u{}, v{};
  if (!SectionClipPlaneBasis(p, &u, &v, &n))
    return e;
  const ray3d::Vec3 eu = ray3d::Sub(ind.corner[1], ind.corner[0]);
  const ray3d::Vec3 ev = ray3d::Sub(ind.corner[3], ind.corner[0]);
  const double lu = ray3d::Length(eu);
  const double lv = ray3d::Length(ev);
  if (!(lu > 1e-9 && lv > 1e-9))
    return e;
  const ray3d::Vec3 centre{ind.corner[0].x + 0.5 * (eu.x + ev.x),
                           ind.corner[0].y + 0.5 * (eu.y + ev.y),
                           ind.corner[0].z + 0.5 * (eu.z + ev.z)};
  e.cu = ray3d::Dot(centre, u);
  e.cv = ray3d::Dot(centre, v);
  e.halfU = lu * 0.5;
  e.halfV = lv * 0.5;
  e.valid = true;
  return e;
}

/// Smallest half-size a stretch grip may leave, in drawing units (REQ-339).
///
/// A rectangle dragged through zero would invert — the corners would cross and the hatch would run
/// the other way — and a zero-size one cannot be grabbed again to undo the mistake. Clamping is the
/// behaviour that leaves the user a way back.
inline constexpr double kSectionPlaneMinHalfExtent = 1.0e-3;

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
