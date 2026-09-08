#pragma once

#include <cmath>

/// Ray/plane/segment math for the 3D viewport (REQ-058 / ADR-025 (d)).
///
/// Pure and dependency-free — no GL, no ImGui, no CAD session state — so the picking and snapping
/// math is unit-testable without a window or a GL context (the ADR-002 layering pressure that
/// already governs the traverse and hatch modules).
///
/// Coordinate convention matches the rest of the codebase: +X east, +Y north, +Z up, right-handed.
/// Distances are in the drawing's world units (feet, for survey work), so REQ-101's ±0.01 ft
/// tolerance applies to anything these functions feed.

namespace ray3d {

struct Vec3 {
  double x = 0.0, y = 0.0, z = 0.0;
};

inline Vec3 Add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 Sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 Scale(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }

/// Unit vector, or {0,0,0} for a degenerate input. Returning zero rather than NaN keeps a bad
/// input visible and inert instead of poisoning every downstream coordinate (REQ-201).
inline Vec3 Normalize(const Vec3& a) {
  const double len = Length(a);
  if (!(len > 1e-12))
    return {0.0, 0.0, 0.0};
  return {a.x / len, a.y / len, a.z / len};
}

/// Rodrigues' rotation of DIRECTION \p v about UNIT axis \p axisUnit by \p angleRad, radians
/// positive by the right-hand rule. A direction has no position, so there is no axis-point
/// parameter here — that is exactly what distinguishes this from \ref RotatePointAboutAxis below,
/// and why a plane NORMAL (a direction) and a plane's CENTRE (a point) are rotated by two different
/// calls even though they share one angle and one axis (REQ-328).
///
/// \p axisUnit is trusted to already be a unit vector — every call site already has one (a plane
/// normal, a UCS Z axis) and re-normalizing here would hide a degenerate axis instead of surfacing
/// it. Equivalent to `brep.cpp`'s file-private `RotateAbout` (SWEEP/LOFT framing) — not merged into
/// one symbol, since that one has no reason to leave `brep.cpp` and this one has every reason to be
/// callable without a `Solid` in scope.
inline Vec3 RotateVectorAboutAxis(const Vec3& v, const Vec3& axisUnit, double angleRad) {
  const double c = std::cos(angleRad);
  const double s = std::sin(angleRad);
  return Add(Add(Scale(v, c), Scale(Cross(axisUnit, v), s)), Scale(axisUnit, Dot(axisUnit, v) * (1.0 - c)));
}

/// The same rotation, applied to a POINT about the LINE through \p axisPoint with unit direction
/// \p axisUnit: translate the point so the axis passes through the origin, rotate the resulting
/// direction, translate back. Reduces to a plain about-the-origin rotation when \p axisPoint is
/// {0,0,0} — the case \ref RotateVectorAboutAxis already covers directly, which is why direction and
/// point are not the same function with an ignored parameter: a caller rotating a plane normal has
/// no axis point to give it, and a caller rotating a centre point always does.
inline Vec3 RotatePointAboutAxis(const Vec3& p, const Vec3& axisPoint, const Vec3& axisUnit,
                                 double angleRad) {
  return Add(axisPoint, RotateVectorAboutAxis(Sub(p, axisPoint), axisUnit, angleRad));
}

/// Reflect a DIRECTION through a plane with unit normal \p planeUnit (Householder, no translation):
/// v' = v - 2 (v . n) n. \p planeUnit is trusted to be a unit vector, the same contract
/// \ref RotateVectorAboutAxis keeps for its axis.
inline Vec3 ReflectVectorAcrossPlane(const Vec3& v, const Vec3& planeUnit) {
  return Sub(v, Scale(planeUnit, 2.0 * Dot(v, planeUnit)));
}

/// Reflect a POINT through the plane { x : (x - \p planePoint) . \p planeUnit = 0 }: subtract the
/// plane point, reflect the direction, add it back — the point/direction split \ref RotatePointAboutAxis
/// makes, and for the same reason (a caller reflecting a plane normal has no plane point to give).
inline Vec3 ReflectPointAcrossPlane(const Vec3& p, const Vec3& planePoint, const Vec3& planeUnit) {
  return Add(planePoint, ReflectVectorAcrossPlane(Sub(p, planePoint), planeUnit));
}

/// A ray: a point and a direction. \c dir is expected normalized; a zero \c dir marks it invalid.
struct Ray {
  Vec3 origin;
  Vec3 dir;
  [[nodiscard]] bool valid() const { return Dot(dir, dir) > 1e-24; }
  [[nodiscard]] Vec3 at(double t) const { return Add(origin, Scale(dir, t)); }
};

/// An infinite plane through \c point with surface normal \c normal (need not be unit length).
/// The default is the world XY plane at Z = 0 — the default UCS (REQ-058).
struct Plane {
  Vec3 point;
  Vec3 normal{0.0, 0.0, 1.0};
};

/// Intersect \p ray with \p plane. Writes the hit point to \p outHit and returns true only for a
/// real intersection **in front of** the ray origin.
///
/// Returns false — leaving \p outHit untouched — when the ray is parallel to the plane, when the
/// plane is degenerate, or when the plane lies behind the origin. Callers MUST honour the return
/// value: a click that misses the work plane has no world coordinate, and inventing one (0,0 or a
/// NaN) would put geometry somewhere the user never pointed (REQ-201).
inline bool RayPlaneIntersect(const Ray& ray, const Plane& plane, Vec3* outHit, double* outT = nullptr) {
  if (!outHit || !ray.valid())
    return false;
  const Vec3 n = Normalize(plane.normal);
  if (Dot(n, n) < 0.5)  // degenerate normal
    return false;
  const double denom = Dot(n, ray.dir);
  if (std::fabs(denom) < 1e-12)  // parallel: no unique intersection (or the ray lies in the plane)
    return false;
  const double t = Dot(n, Sub(plane.point, ray.origin)) / denom;
  if (!(t > 0.0) || !std::isfinite(t))
    return false;  // behind the origin, or non-finite from an extreme input
  *outHit = ray.at(t);
  if (outT)
    *outT = t;
  return true;
}

/// Intersect \p ray with the triangle \p a → \p b → \p c. Writes the hit point to \p outHit and
/// returns true only for a real intersection **in front of** the ray origin.
///
/// Möller–Trumbore, in `double`. This is the test a solid's FACES are picked with, and it is the
/// piece REQ-313 deliberately left for its first consumer: a face carries an analytic surface, but
/// what a cursor can actually be tested against is the tessellation, so the pick finds the triangle
/// and `brep::Tessellation::triFace` names the face that triangle belongs to.
///
/// **The hit must then be projected onto that face's analytic surface**
/// (`brep::ClosestPointOnSurface`); that step is part of the pick, not a refinement of it. Measured
/// on a cylinder tessellated at the shipping chord tolerance, the raw triangle hit sits 0.00986 ft
/// off the true surface — inside REQ-101's ±0.01 ft, but spending 98.6% of the budget before any
/// other error joins in. The projection takes it to 1e-15 ft at the origin and 1e-10 ft at
/// state-plane magnitude (the double floor there), which is the difference between a face pick that
/// merely passes a tolerance test and one that is actually exact.
///
/// Returns false — leaving \p outHit untouched — for a degenerate ray, a degenerate triangle, a ray
/// parallel to the triangle's plane, a miss, and a hit behind the origin. Callers MUST honour the
/// return value, for the reason \ref RayPlaneIntersect gives: a click that hits nothing has no world
/// coordinate, and inventing one puts geometry where the user never pointed (REQ-201).
///
/// \p outT receives the ray parameter of the hit, which is what depth-orders one candidate triangle
/// against another; \p outU and \p outV the barycentric coordinates of the hit, free here and needed
/// by a caller that interpolates a per-vertex normal across the triangle.
inline bool RayTriangleIntersect(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c,
                                 Vec3* outHit, double* outT = nullptr, double* outU = nullptr,
                                 double* outV = nullptr) {
  if (!outHit || !ray.valid())
    return false;
  const Vec3 e1 = Sub(b, a);
  const Vec3 e2 = Sub(c, a);
  const Vec3 pv = Cross(ray.dir, e2);
  const double det = Dot(e1, pv);
  // A near-zero determinant means the ray lies in (or runs parallel to) the triangle's plane, and
  // the same test catches a degenerate triangle whose two edges are collinear. The threshold is
  // **scale-relative**: an absolute epsilon would reject a legitimate small triangle at survey
  // coordinates, which is precisely where a 0.25 ft feature at easting 2e6 lives.
  const double scale = Length(e1) * Length(e2);
  if (!(std::fabs(det) > 1e-12 * (scale > 0.0 ? scale : 1.0)))
    return false;
  const double inv = 1.0 / det;
  const Vec3 tv = Sub(ray.origin, a);
  // The barycentric tests are given a hair of slack rather than being exact. Within one face the
  // tessellation is indexed, so adjacent triangles share vertices exactly and no slack is needed;
  // ACROSS faces it is deliberately unwelded ("a solid's edges are creases" — brep.hpp), so a ray
  // through a shared boundary is decided by two independent evaluations. Erring outward makes such a
  // ray hit BOTH triangles and lets the nearest win, instead of falling through a hairline crack and
  // reporting a miss on a solid the user clicked squarely. Any point this admits is off the triangle
  // by less than the slack and is projected onto the analytic surface afterwards regardless.
  constexpr double kBaryEps = 1e-9;
  const double u = Dot(tv, pv) * inv;
  if (u < -kBaryEps || u > 1.0 + kBaryEps)
    return false;
  const Vec3 qv = Cross(tv, e1);
  const double v = Dot(ray.dir, qv) * inv;
  if (v < -kBaryEps || u + v > 1.0 + kBaryEps)
    return false;
  const double t = Dot(e2, qv) * inv;
  if (!(t > 0.0) || !std::isfinite(t))
    return false;  // behind the origin, or non-finite from an extreme input
  const Vec3 hit = ray.at(t);
  if (!std::isfinite(hit.x) || !std::isfinite(hit.y) || !std::isfinite(hit.z))
    return false;
  *outHit = hit;
  if (outT)
    *outT = t;
  if (outU)
    *outU = u;
  if (outV)
    *outV = v;
  return true;
}

/// Shortest distance from \p ray to the finite segment \p a → \p b.
///
/// This is the orbited-camera analogue of "how far is the cursor from this line?": under a plan
/// view the answer is a screen-space distance, but once the camera tilts, picking has to measure
/// against the ray the cursor casts through the scene. \p outT receives the parameter along the
/// ray of the closest approach (useful for depth-ordering picks); \p outS the parameter along the
/// segment, clamped to [0,1].
///
/// Returns a large finite value (not NaN, not infinity) for a degenerate ray so callers can
/// compare it against a tolerance without special-casing.
inline double RaySegmentDistance(const Ray& ray, const Vec3& a, const Vec3& b, double* outT = nullptr,
                                 double* outS = nullptr) {
  constexpr double kFar = 1e30;
  if (!ray.valid())
    return kFar;
  const Vec3 seg = Sub(b, a);
  const double segLen2 = Dot(seg, seg);
  if (segLen2 < 1e-24) {  // the "segment" is a point
    const Vec3 ap = Sub(a, ray.origin);
    double t = Dot(ap, ray.dir);
    // Clamped for the same reason the non-degenerate path below clamps, and RayPointDistance
    // with it: a point behind the ray origin must be measured FROM the origin. Left unclamped,
    // this returned a negative outT, which — per this function's own "useful for
    // depth-ordering picks" — sorts as nearer than everything actually in front of the camera.
    if (t < 0.0)
      t = 0.0;
    const Vec3 closest = ray.at(t);
    if (outT)
      *outT = t;
    if (outS)
      *outS = 0.0;
    return Length(Sub(a, closest));
  }
  // Standard closest-approach of two lines, then clamp to the segment and re-solve along the ray
  // so the reported distance is to the CLAMPED point (the unclamped solution is wrong past an end).
  const Vec3 w0 = Sub(ray.origin, a);
  const double aa = Dot(ray.dir, ray.dir);  // 1 for a normalized dir
  const double bb = Dot(ray.dir, seg);
  const double cc = segLen2;
  const double dd = Dot(ray.dir, w0);
  const double ee = Dot(seg, w0);
  // Closest approach of two skew lines: with w0 = rayOrigin − a, the parameter along the SEGMENT
  // is (aa·ee − bb·dd) / (aa·cc − bb·bb). Sign matters — negating the denominator here silently
  // clamps every pick to a segment endpoint, which reads as "picking works but is inaccurate".
  const double denom = aa * cc - bb * bb;
  double s = 0.0;
  if (std::fabs(denom) > 1e-18)
    s = (aa * ee - bb * dd) / denom;
  else
    s = 0.0;  // ray parallel to the segment — clamp to an end and let the distance speak
  if (s < 0.0)
    s = 0.0;
  else if (s > 1.0)
    s = 1.0;
  const Vec3 onSeg = Add(a, Scale(seg, s));
  double t = Dot(Sub(onSeg, ray.origin), ray.dir) / (aa > 1e-18 ? aa : 1.0);
  if (t < 0.0)
    t = 0.0;  // the segment is behind the camera; measure from the origin
  const Vec3 onRay = ray.at(t);
  if (outT)
    *outT = t;
  if (outS)
    *outS = s;
  return Length(Sub(onSeg, onRay));
}

/// The point on the INFINITE line through \p linePoint along \p lineDir closest to \p ray (skew-line
/// closest approach, unclamped — unlike \ref RaySegmentDistance's segment). \p outDegenerate, if
/// given, is set true when the line runs parallel to the ray (no well-conditioned closest point
/// exists), in which case \p linePoint itself is returned unchanged.
///
/// This is the ray/PLANE-intersection alternative for a 1-D constraint (e.g. an ORTHO-locked axis):
/// intersecting the ray with the full plane the line lies in and then projecting onto the line is
/// numerically unstable whenever the plane grazes the ray, even though the LINE itself is nowhere
/// near parallel to it — a tiny screen-pixel move blows up into an enormous, erratic in-plane swing
/// (issue #386). Measuring against the line directly sidesteps that: it degenerates only when the
/// LINE itself is (nearly) parallel to the ray, an unavoidable case no formulation escapes (looking
/// straight down the locked axis has no length to show, in this app or in AutoCAD).
inline Vec3 ClosestPointOnLineToRay(const Ray& ray, const Vec3& linePoint, const Vec3& lineDir,
                                    bool* outDegenerate = nullptr) {
  if (outDegenerate)
    *outDegenerate = false;
  const Vec3 d = Normalize(lineDir);
  if (!ray.valid() || Dot(d, d) < 0.5) {
    if (outDegenerate)
      *outDegenerate = true;
    return linePoint;
  }
  const Vec3 w0 = Sub(linePoint, ray.origin);
  const double b = Dot(d, ray.dir);     // ray.dir is unit per the Ray contract
  const double dDot = Dot(d, w0);
  const double eDot = Dot(ray.dir, w0);
  const double denom = 1.0 - b * b;     // == a*c - b*b with a = d.d = 1, c = ray.dir.ray.dir = 1
  if (!(std::fabs(denom) > 1e-9)) {
    if (outDegenerate)
      *outDegenerate = true;
    return linePoint;
  }
  const double t = (b * eDot - dDot) / denom;
  return Add(linePoint, Scale(d, t));
}

/// Shortest distance from \p ray to \p p. \p outT receives the ray parameter of closest approach.
inline double RayPointDistance(const Ray& ray, const Vec3& p, double* outT = nullptr) {
  if (!ray.valid())
    return 1e30;
  double t = Dot(Sub(p, ray.origin), ray.dir);
  if (t < 0.0)
    t = 0.0;
  if (outT)
    *outT = t;
  return Length(Sub(p, ray.at(t)));
}

}  // namespace ray3d
