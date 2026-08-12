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
    const double t = Dot(ap, ray.dir);
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
