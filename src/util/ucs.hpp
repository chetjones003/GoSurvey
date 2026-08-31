#pragma once

#include "ray3d.hpp"

#include <cmath>

/// The User Coordinate System: one authoritative WCS <-> UCS implementation (REQ-154, GitHub #126).
///
/// Pure and dependency-free — no GL, no ImGui, no CAD session state — so every transform, every
/// construction rule and the PLAN camera derivation are unit-testable without a window (the same
/// ADR-002 layering pressure that put `ray3d` here).
///
/// **A UCS never moves geometry.** Entities stay in WCS; a UCS is only the frame the *user* works
/// in — coordinate entry, ORTHO, the grid, and the readouts. Anything that transforms a stored
/// coordinate because the UCS changed is a bug, not a feature.
///
/// Convention matches the rest of the codebase: +X east, +Y north, +Z up, right-handed. The basis
/// is kept orthonormal by construction, which is what lets \ref WorldToUcs invert the frame with
/// three dot products instead of a matrix inverse.
namespace ucs {

using ray3d::Cross;
using ray3d::Dot;
using ray3d::Normalize;
using ray3d::Vec3;

/// An origin plus a right-handed orthonormal basis, all expressed in WCS.
///
/// The default is the World Coordinate System itself. The WCS is immutable by definition — nothing
/// here mutates a `Ucs` in place; every construction returns a new one, so "restore World" is a
/// plain assignment and cannot be corrupted by an earlier edit.
struct Ucs {
  Vec3 origin{0.0, 0.0, 0.0};
  Vec3 xAxis{1.0, 0.0, 0.0};
  Vec3 yAxis{0.0, 1.0, 0.0};
  Vec3 zAxis{0.0, 0.0, 1.0};
};

/// True when \p u is the World Coordinate System within \p tol.
///
/// Used for the status readout and for the "is this worth saving to `.gs`?" test, so it compares
/// the whole frame rather than just the origin — a UCS rotated in place is emphatically not World.
[[nodiscard]] inline bool IsWorld(const Ucs& u, double tol = 1e-9) {
  auto near = [tol](double a, double b) { return std::fabs(a - b) <= tol; };
  return near(u.origin.x, 0.0) && near(u.origin.y, 0.0) && near(u.origin.z, 0.0) && near(u.xAxis.x, 1.0) &&
         near(u.xAxis.y, 0.0) && near(u.xAxis.z, 0.0) && near(u.yAxis.x, 0.0) && near(u.yAxis.y, 1.0) &&
         near(u.yAxis.z, 0.0) && near(u.zAxis.x, 0.0) && near(u.zAxis.y, 0.0) && near(u.zAxis.z, 1.0);
}

/// True when the frame is orthonormal and right-handed to within \p tol.
///
/// Every constructor below is expected to satisfy this; the tests assert it on each one. It exists
/// because a silently left-handed or skewed frame does not fail loudly — it quietly mirrors or
/// shears everything the user draws, which is far harder to notice than a refused command.
[[nodiscard]] inline bool IsRightHandedOrthonormal(const Ucs& u, double tol = 1e-9) {
  auto unit = [tol](const Vec3& v) { return std::fabs(ray3d::Length(v) - 1.0) <= tol; };
  if (!unit(u.xAxis) || !unit(u.yAxis) || !unit(u.zAxis))
    return false;
  if (std::fabs(Dot(u.xAxis, u.yAxis)) > tol || std::fabs(Dot(u.yAxis, u.zAxis)) > tol ||
      std::fabs(Dot(u.zAxis, u.xAxis)) > tol)
    return false;
  // Right-handed: X cross Y must be +Z, not -Z. This is the check that catches a mirrored frame.
  const Vec3 c = Cross(u.xAxis, u.yAxis);
  return std::fabs(c.x - u.zAxis.x) <= tol && std::fabs(c.y - u.zAxis.y) <= tol &&
         std::fabs(c.z - u.zAxis.z) <= tol;
}

/// True when two frames are the same to within \p tol — same origin and same basis.
///
/// Used to decide which entry in the UCS dropdown carries the tick. Comparing the WHOLE frame
/// matters: a saved UCS and the active one can share an origin and differ only in rotation, and
/// ticking that name would tell the user they are working in a frame they are not.
[[nodiscard]] inline bool FramesMatch(const Ucs& a, const Ucs& b, double tol = 1e-6) {
  auto near = [tol](const Vec3& p, const Vec3& q) {
    return std::fabs(p.x - q.x) <= tol && std::fabs(p.y - q.y) <= tol && std::fabs(p.z - q.z) <= tol;
  };
  return near(a.origin, b.origin) && near(a.xAxis, b.xAxis) && near(a.yAxis, b.yAxis) && near(a.zAxis, b.zAxis);
}

// ---------------------------------------------------------------------------------------------
// Transformations. These four are the entire public contract the rest of the app codes against.
// ---------------------------------------------------------------------------------------------

/// A **point** expressed in WCS -> the same point expressed in \p u.
[[nodiscard]] inline Vec3 WorldToUcs(const Ucs& u, const Vec3& world) {
  const Vec3 d = ray3d::Sub(world, u.origin);
  return {Dot(d, u.xAxis), Dot(d, u.yAxis), Dot(d, u.zAxis)};
}

/// A **point** expressed in \p u -> the same point expressed in WCS.
[[nodiscard]] inline Vec3 UcsToWorld(const Ucs& u, const Vec3& local) {
  Vec3 w = u.origin;
  w = ray3d::Add(w, ray3d::Scale(u.xAxis, local.x));
  w = ray3d::Add(w, ray3d::Scale(u.yAxis, local.y));
  w = ray3d::Add(w, ray3d::Scale(u.zAxis, local.z));
  return w;
}

/// A **direction** in WCS -> the same direction in \p u. Unlike a point, a vector ignores the
/// origin — which is exactly why it needs its own entry point rather than being open-coded at each
/// call site as `WorldToUcs(a) - WorldToUcs(b)`.
[[nodiscard]] inline Vec3 WorldVectorToUcs(const Ucs& u, const Vec3& v) {
  return {Dot(v, u.xAxis), Dot(v, u.yAxis), Dot(v, u.zAxis)};
}

/// A **direction** in \p u -> the same direction in WCS.
[[nodiscard]] inline Vec3 UcsVectorToWorld(const Ucs& u, const Vec3& v) {
  Vec3 w = ray3d::Scale(u.xAxis, v.x);
  w = ray3d::Add(w, ray3d::Scale(u.yAxis, v.y));
  w = ray3d::Add(w, ray3d::Scale(u.zAxis, v.z));
  return w;
}

/// The UCS XY plane — the work plane a viewport click resolves against (REQ-058).
[[nodiscard]] inline ray3d::Plane WorkPlane(const Ucs& u) {
  ray3d::Plane p;
  p.point = u.origin;
  p.normal = u.zAxis;
  return p;
}

// ---------------------------------------------------------------------------------------------
// Construction. Each returns false rather than producing a degenerate frame (REQ-201): a UCS that
// is silently garbage would misplace every subsequent coordinate, so a refusal the user can see is
// always the better outcome.
// ---------------------------------------------------------------------------------------------

/// Move the origin, keep the orientation — `UCS` with a single point picked.
[[nodiscard]] inline Ucs WithOrigin(const Ucs& u, const Vec3& newOrigin) {
  Ucs r = u;
  r.origin = newOrigin;
  return r;
}

/// AutoCAD's **Arbitrary Axis Algorithm**: derive a stable X axis from a normal alone.
///
/// Any Z axis leaves one degree of freedom — the spin about it — and picking that arbitrarily makes
/// the resulting UCS jitter as the normal crosses a pole. This is the algorithm DXF itself specifies
/// for the 210 extrusion vector, so using it here means a UCS derived from a face normal agrees with
/// what a DXF consumer would reconstruct from the same normal.
///
/// The 1/64 threshold is the published constant, not a tuned one.
[[nodiscard]] inline bool FromNormal(const Vec3& origin, const Vec3& normal, Ucs* out) {
  if (!out)
    return false;
  const Vec3 n = Normalize(normal);
  if (Dot(n, n) < 0.5)  // degenerate normal
    return false;
  const Vec3 worldY{0.0, 1.0, 0.0};
  const Vec3 worldZ{0.0, 0.0, 1.0};
  // Close to the world Z axis (either pole), use world Y to break the tie; otherwise world Z.
  const Vec3 ax = (std::fabs(n.x) < (1.0 / 64.0) && std::fabs(n.y) < (1.0 / 64.0)) ? Cross(worldY, n)
                                                                                   : Cross(worldZ, n);
  const Vec3 x = Normalize(ax);
  if (Dot(x, x) < 0.5)
    return false;
  out->origin = origin;
  out->zAxis = n;
  out->xAxis = x;
  out->yAxis = Normalize(Cross(n, x));
  return Dot(out->yAxis, out->yAxis) > 0.5;
}

/// `UCS ZAxis`: origin plus a point on the positive Z axis. X/Y come from \ref FromNormal, so the
/// XY plane is perpendicular to the given Z and the in-plane orientation is stable.
[[nodiscard]] inline bool FromZAxis(const Vec3& origin, const Vec3& pointOnPositiveZ, Ucs* out) {
  return FromNormal(origin, ray3d::Sub(pointOnPositiveZ, origin), out);
}

/// The three-point UCS: origin, a point giving +X, and a point giving the +Y half of the XY plane.
///
/// \p pointOnXy does **not** need to be perpendicular to the X axis — only non-collinear with it.
/// Its perpendicular component is what defines +Y, which is why the third pick is documented as
/// "a point in the positive-Y half of the plane" rather than "the Y axis".
[[nodiscard]] inline bool FromThreePoints(const Vec3& origin, const Vec3& pointOnX, const Vec3& pointOnXy,
                                          Ucs* out) {
  if (!out)
    return false;
  const Vec3 x = Normalize(ray3d::Sub(pointOnX, origin));
  if (Dot(x, x) < 0.5)  // second pick coincides with the origin
    return false;
  const Vec3 inPlane = ray3d::Sub(pointOnXy, origin);
  const Vec3 z = Normalize(Cross(x, inPlane));
  if (Dot(z, z) < 0.5)  // all three collinear: no plane is defined
    return false;
  out->origin = origin;
  out->xAxis = x;
  out->zAxis = z;
  out->yAxis = Normalize(Cross(z, x));
  return Dot(out->yAxis, out->yAxis) > 0.5;
}

/// A frame whose X axis runs along \p dir, tilted as little as possible from the world XY plane.
///
/// This is what "align to this line" has to mean once the line can be a 3D one; AutoCAD's
/// flat-drawing rule (X along the line, Z = the entity's extrusion) is the special case of it that
/// a horizontal line produces.
///
/// Lives here rather than in the command file because the LIVE PREVIEW needs the same answer the
/// commit does. A preview that derived the frame separately would eventually show one frame and
/// commit another, which is worse than no preview.
[[nodiscard]] inline bool AlignedToDirection(const Vec3& origin, const Vec3& dir, Ucs* out) {
  if (!out)
    return false;
  const Vec3 x = Normalize(dir);
  if (Dot(x, x) < 0.5)
    return false;
  // Z = world up with the along-X part removed. For a vertical line that is degenerate, so world
  // north takes over - any perpendicular will do there, and picking a deterministic one keeps the
  // result from depending on float noise.
  Vec3 ref{0.0, 0.0, 1.0};
  Vec3 z = Normalize(ray3d::Sub(ref, ray3d::Scale(x, Dot(ref, x))));
  if (Dot(z, z) < 0.5) {
    ref = Vec3{0.0, 1.0, 0.0};
    z = Normalize(ray3d::Sub(ref, ray3d::Scale(x, Dot(ref, x))));
    if (Dot(z, z) < 0.5)
      return false;
  }
  out->origin = origin;
  out->xAxis = x;
  out->zAxis = z;
  out->yAxis = Normalize(Cross(z, x));
  return Dot(out->yAxis, out->yAxis) > 0.5;
}

/// Build a UCS from a (possibly imperfect) basis — used by `UCS View`, whose axes come from the
/// camera. Gram-Schmidt against \p right, so a camera basis that is a hair off orthonormal from
/// accumulated float error still yields an exactly orthonormal UCS.
[[nodiscard]] inline bool FromBasis(const Vec3& origin, const Vec3& right, const Vec3& up, Ucs* out) {
  if (!out)
    return false;
  const Vec3 x = Normalize(right);
  if (Dot(x, x) < 0.5)
    return false;
  const Vec3 z = Normalize(Cross(x, up));
  if (Dot(z, z) < 0.5)  // right and up parallel: not a basis
    return false;
  out->origin = origin;
  out->xAxis = x;
  out->zAxis = z;
  out->yAxis = Normalize(Cross(z, x));
  return Dot(out->yAxis, out->yAxis) > 0.5;
}

// ---------------------------------------------------------------------------------------------
// Axis rotations (`UCS X` / `UCS Y` / `UCS Z`). Each spins the frame about one of its OWN axes —
// not a world axis — which is what makes them compose the way a user expects when applied in
// sequence. Positive angles follow the right-hand rule about that axis.
// ---------------------------------------------------------------------------------------------

namespace detail {
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/// Rotate the pair (\p a, \p b) by \p deg in their own plane, leaving the third axis fixed. All
/// three axis rotations are this same operation on a different pair, so the right-hand-rule sign
/// lives in exactly one place instead of being re-derived (and mis-signed) three times.
inline void SpinPair(Vec3* a, Vec3* b, double deg) {
  const double c = std::cos(deg * kDegToRad);
  const double s = std::sin(deg * kDegToRad);
  const Vec3 a0 = *a;
  const Vec3 b0 = *b;
  *a = ray3d::Add(ray3d::Scale(a0, c), ray3d::Scale(b0, s));
  *b = ray3d::Add(ray3d::Scale(a0, -s), ray3d::Scale(b0, c));
}
}  // namespace detail

/// Rotate about the UCS's own X axis: Y sweeps toward Z.
[[nodiscard]] inline Ucs RotatedAboutX(const Ucs& u, double deg) {
  Ucs r = u;
  detail::SpinPair(&r.yAxis, &r.zAxis, deg);
  return r;
}

/// Rotate about the UCS's own Y axis: Z sweeps toward X.
[[nodiscard]] inline Ucs RotatedAboutY(const Ucs& u, double deg) {
  Ucs r = u;
  detail::SpinPair(&r.zAxis, &r.xAxis, deg);
  return r;
}

/// Rotate about the UCS's own Z axis: X sweeps toward Y. This is the common survey case — a UCS
/// squared to a road centreline or a lot line.
[[nodiscard]] inline Ucs RotatedAboutZ(const Ucs& u, double deg) {
  Ucs r = u;
  detail::SpinPair(&r.xAxis, &r.yAxis, deg);
  return r;
}

/// The angle of \p dir within the plane that `RotatedAbout<axis>` spins, in degrees.
///
/// This is the inverse of the three rotations above: feed the result back to `RotatedAbout<axis>`
/// and that rotation's own reference axis ends up pointing along \p dir. So "make my UCS X axis run
/// along this fence line" becomes two picks and no arithmetic by the user — which is the whole point,
/// since a surveyor working to a lot line knows the line and not its bearing.
///
/// Each axis measures in the same pair, and the same order, that \ref detail::SpinPair uses for it,
/// so the two can never disagree about which way is positive:
///
///   - `Z` spins X toward Y, so the angle is measured from the UCS +X, positive toward +Y
///   - `X` spins Y toward Z, so it is measured from +Y, positive toward +Z
///   - `Y` spins Z toward X, so it is measured from +Z, positive toward +X
///
/// Returns false when \p dir is degenerate, or lies (near enough) perpendicular to the rotation
/// plane and so has no angle in it — two picks straight up the Z axis define no rotation about Z.
/// A refusal is the honest outcome there: any angle at all would be invented (REQ-201).
[[nodiscard]] inline bool AngleInRotationPlaneDeg(const Ucs& u, char axis, const Vec3& dir, double* outDeg) {
  if (!outDeg)
    return false;
  Vec3 refAxis{}, towardAxis{};
  switch (axis) {
  case 'X': refAxis = u.yAxis; towardAxis = u.zAxis; break;
  case 'Y': refAxis = u.zAxis; towardAxis = u.xAxis; break;
  case 'Z': refAxis = u.xAxis; towardAxis = u.yAxis; break;
  default: return false;
  }
  const double a = Dot(dir, refAxis);
  const double b = Dot(dir, towardAxis);
  // Compare the in-plane length against the whole vector's, so the test scales with the picks rather
  // than against a fixed distance: two points a foot apart and two a mile apart get the same answer.
  const double inPlane = std::sqrt(a * a + b * b);
  if (inPlane < 1e-9 || inPlane < 1e-6 * ray3d::Length(dir))
    return false;
  *outDeg = std::atan2(b, a) / detail::kDegToRad;
  return true;
}

// ---------------------------------------------------------------------------------------------
// PLAN support.
// ---------------------------------------------------------------------------------------------

/// The camera azimuth/elevation that looks straight down \p u's +Z at its XY plane (the PLAN view).
///
/// Mirrors `Camera`'s stored orientation. The relationships below were **measured** against the
/// real `Camera` rather than re-derived, because the two had to agree on a sign convention and a
/// second derivation is exactly how they would silently drift apart (Camera.hpp: "one derivation,
/// one convention"):
///
///   - the vector from target toward eye is `-Camera::ForwardWorld()`, so `elevation = asin(z)`
///     and, away from the poles, `azimuth = atan2(-x, -y)`;
///   - screen-up at `elevation = +90` is `(sin az, cos az, 0)` — i.e. a **positive** azimuth turns
///     screen-up *clockwise* from north, so a UCS rotated +30 degrees counter-clockwise needs
///     `azimuth = -30`;
///   - screen-up at `elevation = -90` is `(-sin az, -cos az, 0)`.
///
/// At a pole the view direction fixes nothing about the spin, so the azimuth comes from the UCS's
/// own +Y instead. That is the case that matters most in practice: every UCS squared to a road or
/// a lot line is a pure rotation about world Z, and it is precisely there that the eye direction
/// carries no azimuth information at all.
///
/// **This derives azimuth/elevation only; it does not compute the roll.** For a UCS whose Z is
/// world +Z the roll is zero and screen-up is already the UCS +Y. For a tilted UCS the caller pairs
/// this with `Camera::RollToPlaceUp(az, el, u.yAxis)` to turn screen-up onto the UCS +Y — the two
/// steps together make PLAN exact for every frame (GitHub #153). Kept split so this stays pure of
/// the `Camera` convention.
inline void PlanViewAngles(const Ucs& u, float* azimuthDeg, float* elevationDeg) {
  const Vec3 eye = Normalize(u.zAxis);
  if (Dot(eye, eye) < 0.5)  // degenerate: leave the caller's values alone
    return;
  const double clamped = eye.z < -1.0 ? -1.0 : (eye.z > 1.0 ? 1.0 : eye.z);
  if (elevationDeg)
    *elevationDeg = static_cast<float>(std::asin(clamped) / detail::kDegToRad);
  if (!azimuthDeg)
    return;

  const double horiz = std::sqrt(eye.x * eye.x + eye.y * eye.y);
  double az = 0.0;
  if (horiz < 1e-9) {
    // Looking straight down (or straight up): the azimuth is the only thing that can put the UCS
    // +Y on screen-up, so take it from the UCS rather than from the degenerate eye direction.
    const double s = eye.z >= 0.0 ? 1.0 : -1.0;
    az = std::atan2(s * u.yAxis.x, s * u.yAxis.y) / detail::kDegToRad;
  } else {
    az = std::atan2(-eye.x, -eye.y) / detail::kDegToRad;
  }
  while (az < 0.0)
    az += 360.0;
  while (az >= 360.0)
    az -= 360.0;
  *azimuthDeg = static_cast<float>(az);
}

/// True when PLAN of \p u can place the UCS +Y up the screen exactly — now the case for every valid
/// frame (GitHub #153).
///
/// Before `Camera` gained a roll axis this held only for a UCS whose Z was world +Z; a tilted frame
/// was oriented correctly but its in-plane spin could not be set. `Camera::RollToPlaceUp` supplies
/// that missing degree of freedom, so the only frame this now rejects is a degenerate one — which
/// `ucs` construction refuses to produce in the first place.
[[nodiscard]] inline bool PlanViewIsExact(const Ucs& u, double tol = 1e-6) {
  return IsRightHandedOrthonormal(u, tol);
}

/// The UCS's rotation about world +Z, in degrees — what the ViewCube's compass and square-up arrows
/// are relative to (REQ-059). Meaningful only when \ref PlanViewIsExact; a tilted UCS has no single
/// such angle, and this returns the best planar approximation so the widget degrades rather than
/// jumping.
[[nodiscard]] inline float AzimuthAboutWorldZDeg(const Ucs& u) {
  double deg = std::atan2(u.xAxis.y, u.xAxis.x) / detail::kDegToRad;
  while (deg < 0.0)
    deg += 360.0;
  while (deg >= 360.0)
    deg -= 360.0;
  return static_cast<float>(deg);
}

}  // namespace ucs
