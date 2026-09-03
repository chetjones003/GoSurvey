#include "solidpick.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace solidpick {
namespace {

using ray3d::Length;
using ray3d::Sub;

constexpr double kPi = 3.14159265358979323846;
constexpr double kFar = 1e30;

/// Chord budget for a curved edge while *searching* for the nearest one.
///
/// This does not set the accuracy of the answer — the winning edge is placed exactly by
/// `brep::ClosestPointOnEdge` — so it only has to be fine enough that the right edge wins the
/// search. π/16 rad is a thirty-second of a turn, whose chord departs from the arc by
/// `r(1 - cos(π/32))` ≈ 0.48% of the radius.
constexpr double kEdgeSearchRadiansPerChord = kPi / 16.0;

/// Chords to walk when searching \p e. One for a straight edge; enough to follow the curve
/// otherwise.
///
/// Keyed on the curve KIND and not on `sweep`. `brep::Edge::sweep` is documented as meaningful for
/// `Arc` and `Ellipse` only: a `CurveKind::Intersection` edge — the procedural surface-crossing
/// curve REQ-314's B2b-2 booleans produce, which carries a witness point and its two surfaces
/// instead — leaves it zero. Keying on `sweep` therefore gave every intersection edge a single
/// straight chord from `v0` to `v1`, and on a cylinder-cylinder seam quarter-curve that chord is up
/// to 0.29·r from the curve. The refinement below would then start from a seed far off the edge,
/// over-estimate the distance, and the edge would silently drop out of the pick — falling through
/// to the face behind it. `CadSnap` already keyed on the kind for exactly this reason.
int EdgeSearchChords(const brep::Edge& e) {
  if (e.kind == brep::CurveKind::Line)
    return 1;
  const double sweep = std::fabs(e.sweep);
  if (e.kind == brep::CurveKind::Intersection || !(sweep > 0.0) || !std::isfinite(sweep)) {
    // No usable sweep: walk a fixed budget fine enough for any curve the kernel builds, rather
    // than dividing by a number that is not there.
    return 64;
  }
  const int n = static_cast<int>(std::ceil(sweep / kEdgeSearchRadiansPerChord));
  return std::clamp(n, 1, 256);
}

/// Distance from \p ray to \p e, and the ray distance and exact on-edge point at that approach.
///
/// Two-stage: walk the edge's chords to find roughly where the closest approach is, then let the
/// kernel place the point exactly. `brep::ClosestPointOnEdge` answers "nearest point on the edge to
/// a POINT", so it is fed the point on the ray, and the pair is re-solved — an alternating
/// projection between the ray and the curve, which is monotone and starts within a fraction of a
/// degree, so two passes converge for picking.
double RayEdgeDistance(const brep::Solid& s, const brep::Edge& e, const Ray& ray, double* outRayT,
                       Vec3* outOnEdge) {
  const int chords = EdgeSearchChords(e);
  double bestDist = kFar;
  double bestT = 0.0;
  Vec3 prev = brep::EdgePointAt(s, e, 0.0);
  for (int i = 1; i <= chords; ++i) {
    const Vec3 cur = brep::EdgePointAt(s, e, static_cast<double>(i) / chords);
    double t = 0.0;
    const double d = ray3d::RaySegmentDistance(ray, prev, cur, &t, nullptr);
    if (d < bestDist) {
      bestDist = d;
      bestT = t;
    }
    prev = cur;
  }
  if (!(bestDist < kFar))
    return kFar;

  Vec3 onEdge = brep::ClosestPointOnEdge(s, e, ray.at(bestT));
  for (int pass = 0; pass < 2; ++pass) {
    double t = 0.0;
    ray3d::RayPointDistance(ray, onEdge, &t);
    onEdge = brep::ClosestPointOnEdge(s, e, ray.at(t));
    bestT = t;
  }
  const double dist = Length(Sub(onEdge, ray.at(bestT)));
  if (!std::isfinite(dist))
    return kFar;
  if (outRayT)
    *outRayT = bestT;
  if (outOnEdge)
    *outOnEdge = onEdge;
  return dist;
}

}  // namespace

bool RayNearBounds(const Ray& ray, const brep::Bounds& b, double pad) {
  if (!b.valid)
    return false;
  const double mn[3] = {b.mn.x - pad, b.mn.y - pad, b.mn.z - pad};
  const double mx[3] = {b.mx.x + pad, b.mx.y + pad, b.mx.z + pad};
  const double o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
  const double d[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
  double tNear = -std::numeric_limits<double>::max();
  double tFar = std::numeric_limits<double>::max();
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(d[i]) < 1e-12) {
      if (o[i] < mn[i] || o[i] > mx[i])
        return false;  // parallel to this slab and outside it
      continue;
    }
    double t0 = (mn[i] - o[i]) / d[i];
    double t1 = (mx[i] - o[i]) / d[i];
    if (t0 > t1)
      std::swap(t0, t1);
    tNear = std::max(tNear, t0);
    tFar = std::min(tFar, t1);
    if (tNear > tFar)
      return false;
  }
  return tFar >= 0.0;
}

const char* KindName(Kind k) {
  switch (k) {
    case Kind::None: return "none";
    case Kind::Face: return "face";
    case Kind::Edge: return "edge";
    case Kind::Vertex: return "vertex";
  }
  return "none";
}

bool PickSubObject(const brep::Solid& solid, const std::vector<float>& triVerts,
                   const std::vector<int>& triFaceIds, const Ray& rayIn, const Tolerance& tol,
                   Pick* out) {
  if (!out || !rayIn.valid())
    return false;
  if (solid.vertices.empty() || solid.faces.empty())
    return false;
  // Inconsistent buffers are a caller bug, and reading past the end of one would be a crash in a
  // frame that only wanted to know what the cursor is over. Refuse by name instead (REQ-201).
  if (triVerts.size() != triFaceIds.size() * 9)
    return false;

  // Normalize once, so every depth below is a DISTANCE and the face, edge and vertex parameters are
  // on the same scale. See the header: they scale oppositely in `|dir|`, so a non-unit ray makes the
  // occlusion comparison meaningless rather than merely imprecise.
  const Ray ray{rayIn.origin, ray3d::Normalize(rayIn.dir)};
  if (!ray.valid())
    return false;

  // Broad phase, shared with the snap path. Padded by the larger tolerance so a ray passing just
  // outside the solid still reaches the triangles.
  const double pad = std::max(tol.vertex, tol.edge);
  if (!RayNearBounds(ray, brep::ComputeBounds(solid), pad))
    return false;

  // --- Faces: the nearest triangle, then the face it belongs to, then the analytic surface. ---
  //
  // `frontT` is the nearest triangle hit whatever its face id, and it is what occlusion is measured
  // against; `faceT`/`faceIndex` are the nearest hit that names a real face. Keeping them apart
  // matters: a triangle with a corrupt id still proves a front surface is there, and taking the
  // next valid face's depth instead would let the back of the solid pass as unoccluded.
  const std::size_t tris = triFaceIds.size();
  double frontT = kFar;
  double faceT = kFar;
  int faceIndex = -1;
  Vec3 faceRawHit;
  for (std::size_t i = 0; i < tris; ++i) {
    const std::size_t b = i * 9;
    const Vec3 a{triVerts[b + 0], triVerts[b + 1], triVerts[b + 2]};
    const Vec3 bb{triVerts[b + 3], triVerts[b + 4], triVerts[b + 5]};
    const Vec3 c{triVerts[b + 6], triVerts[b + 7], triVerts[b + 8]};
    Vec3 hit;
    double t = 0.0;
    if (!ray3d::RayTriangleIntersect(ray, a, bb, c, &hit, &t))
      continue;
    if (t < frontT)
      frontT = t;
    const int fi = triFaceIds[i];
    if (fi < 0 || static_cast<std::size_t>(fi) >= solid.faces.size())
      continue;  // a stale or corrupt id names no face; it still counted toward frontT above
    if (t >= faceT)
      continue;
    faceT = t;
    faceIndex = fi;
    faceRawHit = hit;
  }

  const bool hasFront = frontT < kFar;

  // --- Vertices: highest precedence, because every vertex lies on an edge and a face. ---
  double bestVertDist = kFar;
  int vertIndex = -1;
  double vertT = 0.0;
  if (tol.vertex > 0.0) {
    for (std::size_t i = 0; i < solid.vertices.size(); ++i) {
      double t = 0.0;
      const double d = ray3d::RayPointDistance(ray, solid.vertices[i].p, &t);
      if (d > tol.vertex || d >= bestVertDist)
        continue;
      if (hasFront && t > frontT + tol.vertex)
        continue;  // behind the front surface by more than its own tolerance: occluded
      bestVertDist = d;
      vertIndex = static_cast<int>(i);
      vertT = t;
    }
  }
  if (vertIndex >= 0) {
    out->kind = Kind::Vertex;
    out->index = vertIndex;
    out->point = solid.vertices[static_cast<std::size_t>(vertIndex)].p;  // exact, by definition
    out->rayT = vertT;
    return true;
  }

  // --- Edges next. ---
  double bestEdgeDist = kFar;
  int edgeIndex = -1;
  double edgeT = 0.0;
  Vec3 edgePoint;
  if (tol.edge > 0.0) {
    for (std::size_t i = 0; i < solid.edges.size(); ++i) {
      double t = 0.0;
      Vec3 onEdge;
      const double d = RayEdgeDistance(solid, solid.edges[i], ray, &t, &onEdge);
      if (d > tol.edge || d >= bestEdgeDist)
        continue;
      if (hasFront && t > frontT + tol.edge)
        continue;  // occluded, as above
      bestEdgeDist = d;
      edgeIndex = static_cast<int>(i);
      edgeT = t;
      edgePoint = onEdge;
    }
  }
  if (edgeIndex >= 0) {
    out->kind = Kind::Edge;
    out->index = edgeIndex;
    out->point = edgePoint;  // on the true curve — ClosestPointOnEdge, not a chord
    out->rayT = edgeT;
    return true;
  }

  // --- The face, last. ---
  if (faceIndex < 0)
    return false;
  out->kind = Kind::Face;
  out->index = faceIndex;
  // THE projection. Off the chord, onto the surface the face actually carries.
  out->point = brep::ClosestPointOnSurface(solid.faces[static_cast<std::size_t>(faceIndex)].surface,
                                           faceRawHit);
  out->rayT = faceT;
  return true;
}

}  // namespace solidpick
