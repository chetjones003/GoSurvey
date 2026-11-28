#include <GoSurvey/Geometry/brep.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <unordered_map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>
#include "brep_internal.hpp"

namespace brep {

namespace {

// ---------------------------------------------------------------------------------------------
// Feature operations — Booleans, the B1 subset (REQ-314 / ADR-046, GitHub issue #147).
//
// B1 combines CONVEX, planar-faced solids. Every face of A is split by B's face planes into
// fragments that are each wholly inside or wholly outside B (which, for a convex B, its face planes
// alone decide), and vice versa. Then per operation the right fragments are kept — union: the parts
// of each outside the other; intersect: the parts inside; subtract: A outside B plus B inside A with
// its normals flipped — and welded into a solid. Coincident faces cancel automatically, because a
// fragment on a plane classifies as "inside" under the same `<= eps` test. A curved face or a
// non-convex operand needs B2's general intersection curve and is refused here.
// ---------------------------------------------------------------------------------------------


struct PlaneEq {
  Vec3 point;
  Vec3 normal;  // unit, outward
};

/// Face planes of \p s (one per face; a convex solid has no two faces sharing a plane).
[[nodiscard]] std::vector<PlaneEq> FacePlanes(const Solid& s) {
  std::vector<PlaneEq> out;
  out.reserve(s.faces.size());
  for (const Face& f : s.faces) {
    const int v = f.loops[0].uses.empty()
                      ? 0
                      : (f.loops[0].uses[0].reversed ? s.edges[static_cast<std::size_t>(f.loops[0].uses[0].edge)].v1
                                                     : s.edges[static_cast<std::size_t>(f.loops[0].uses[0].edge)].v0);
    out.push_back(PlaneEq{s.vertices[static_cast<std::size_t>(v)].p, f.surface.frame.zAxis});
  }
  return out;
}


/// Split \p ring by the plane into an above part (points with `sd >= -eps`) and a below part
/// (`sd <= eps`). An on-plane point goes to both. Each part is empty or a >= 3 polygon.
void ClipPolygon(const std::vector<Vec3>& ring, const PlaneEq& pl, double eps, std::vector<Vec3>* above,
                 std::vector<Vec3>* below) {
  above->clear();
  below->clear();
  const std::size_t m = ring.size();
  std::vector<double> d(m);
  double maxAbs = 0.0;
  for (std::size_t i = 0; i < m; ++i) {
    d[i] = ray3d::Dot(ray3d::Sub(ring[i], pl.point), pl.normal);
    maxAbs = std::max(maxAbs, std::fabs(d[i]));
  }
  if (maxAbs <= eps) {
    *above = ring;  // coplanar with the cut plane: one fragment, not two
    return;
  }
  for (std::size_t i = 0; i < m; ++i) {
    const std::size_t j = (i + 1) % m;
    if (d[i] >= -eps)
      above->push_back(ring[i]);
    if (d[i] <= eps)
      below->push_back(ring[i]);
    if ((d[i] > eps && d[j] < -eps) || (d[i] < -eps && d[j] > eps)) {
      const double t = d[i] / (d[i] - d[j]);
      const Vec3 x = ray3d::Add(ring[i], ray3d::Scale(ray3d::Sub(ring[j], ring[i]), t));
      above->push_back(x);
      below->push_back(x);
    }
  }
  if (above->size() < 3)
    above->clear();
  if (below->size() < 3)
    below->clear();
}

[[nodiscard]] Vec3 RingCentroid(const std::vector<Vec3>& r) {
  Vec3 c{};
  for (const Vec3& p : r)
    c = ray3d::Add(c, p);
  return r.empty() ? c : ray3d::Scale(c, 1.0 / static_cast<double>(r.size()));
}
} // namespace

/// The directed boundary points of face \p f, in loop order.
[[nodiscard]] std::vector<Vec3> FaceRing(const Solid& s, const Face& f) {
  std::vector<Vec3> r;
  for (const EdgeUse& u : f.loops[0].uses) {
    const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
    r.push_back(s.vertices[static_cast<std::size_t>(u.reversed ? e.v1 : e.v0)].p);
  }
  return r;
}

/// True when \p hit lies inside the planar polygon \p ring (which lies on the plane with normal
/// \p n): a 2D even-odd test in the plane's own coordinates.
[[nodiscard]] bool PointInPolygon3D(const Vec3& hit, const std::vector<Vec3>& ring, const Vec3& n,
                                    double eps, bool* onEdge) {
  ucs::Ucs fr;
  if (!ucs::FromNormal(ring.empty() ? hit : ring[0], n, &fr))
    return false;
  const ucs::Point2D q = ucs::WorldToPlane(fr, hit);
  const std::size_t m = ring.size();
  bool inside = false;
  for (std::size_t i = 0, j = m - 1; i < m; j = i++) {
    const ucs::Point2D a = ucs::WorldToPlane(fr, ring[i]);
    const ucs::Point2D b = ucs::WorldToPlane(fr, ring[j]);
    // near an edge?
    const double ex = b.x - a.x;
    const double ey = b.y - a.y;
    const double len2 = ex * ex + ey * ey;
    if (len2 > 1e-24) {
      double t = ((q.x - a.x) * ex + (q.y - a.y) * ey) / len2;
      t = std::clamp(t, 0.0, 1.0);
      const double dx = q.x - (a.x + ex * t);
      const double dy = q.y - (a.y + ey * t);
      if (dx * dx + dy * dy <= eps * eps) {
        if (onEdge)
          *onEdge = true;
      }
    }
    if (((a.y > q.y) != (b.y > q.y)) &&
        (q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x))
      inside = !inside;
  }
  return inside;
}

/// True when \p p is inside \p s — an even-odd ray cast against the solid's planar faces. Robust to
/// the ray grazing an edge by retrying along a few incommensurate directions.
[[nodiscard]] bool PointInPlanarSolid(const Vec3& p, const Solid& s, double scale) {
  const double eps = 1e-9 * scale;
  static const Vec3 dirs[] = {{0.3123, 0.5237, 0.7911},
                              {0.8117, -0.2903, 0.5061},
                              {-0.4409, 0.6673, 0.6011},
                              {0.1277, -0.9013, 0.4139}};
  for (const Vec3& d0 : dirs) {
    const Vec3 dir = ray3d::Normalize(d0);
    int crossings = 0;
    bool graze = false;
    for (const Face& f : s.faces) {
      const Vec3 n = f.surface.frame.zAxis;
      const double denom = ray3d::Dot(dir, n);
      if (std::fabs(denom) < 1e-12)
        continue;
      const std::vector<Vec3> ring = FaceRing(s, f);
      if (ring.size() < 3)
        continue;
      const double t = ray3d::Dot(ray3d::Sub(ring[0], p), n) / denom;
      if (t <= eps)
        continue;
      const Vec3 hit = ray3d::Add(p, ray3d::Scale(dir, t));
      bool onEdge = false;
      if (PointInPolygon3D(hit, ring, n, std::max(eps, 1e-7 * scale), &onEdge))
        ++crossings;
      if (onEdge) {
        graze = true;
        break;
      }
    }
    if (!graze)
      return (crossings % 2) == 1;
  }
  return false;  // every direction grazed — treat as outside rather than guess
}

namespace {

/// Axis-aligned bounds of \p s, padded by \p pad.
void SolidAabb(const Solid& s, Vec3* mn, Vec3* mx) {
  *mn = *mx = s.vertices.empty() ? Vec3{} : s.vertices[0].p;
  for (const Vertex& v : s.vertices) {
    mn->x = std::min(mn->x, v.p.x);
    mn->y = std::min(mn->y, v.p.y);
    mn->z = std::min(mn->z, v.p.z);
    mx->x = std::max(mx->x, v.p.x);
    mx->y = std::max(mx->y, v.p.y);
    mx->z = std::max(mx->z, v.p.z);
  }
}
} // namespace

/// A cheap, always-correct disjoint test: bounding boxes that do not touch cannot share volume.
[[nodiscard]] bool AabbsOverlap(const Solid& a, const Solid& b, double eps) {
  Vec3 amn, amx, bmn, bmx;
  SolidAabb(a, &amn, &amx);
  SolidAabb(b, &bmn, &bmx);
  return amn.x <= bmx.x + eps && bmn.x <= amx.x + eps && amn.y <= bmx.y + eps &&
         bmn.y <= amx.y + eps && amn.z <= bmx.z + eps && bmn.z <= amx.z + eps;
}

namespace {

/// True when the two solids share some volume (a vertex of one inside the other, or any edge of one
/// crossing a face of the other). Used only to route the trivial disjoint case.
[[nodiscard]] bool SolidsOverlap(const Solid& a, const Solid& b, double scale) {
  if (!AabbsOverlap(a, b, 1e-9 * scale))
    return false;
  for (const Vertex& v : b.vertices)
    if (PointInPlanarSolid(v.p, a, scale))
      return true;
  for (const Vertex& v : a.vertices)
    if (PointInPlanarSolid(v.p, b, scale))
      return true;
  // Interlocking solids can overlap with no vertex of one inside the other; probe face centroids too.
  for (const Face& f : a.faces) {
    const std::vector<Vec3> r = FaceRing(a, f);
    if (r.size() >= 3 && PointInPlanarSolid(RingCentroid(r), b, scale))
      return true;
  }
  for (const Face& f : b.faces) {
    const std::vector<Vec3> r = FaceRing(b, f);
    if (r.size() >= 3 && PointInPlanarSolid(RingCentroid(r), a, scale))
      return true;
  }
  return false;
}

/// True when every point of \p ring lies within \p eps of one of \p planes.
[[nodiscard]] bool RingOnAnyPlane(const std::vector<Vec3>& ring, const std::vector<PlaneEq>& planes,
                                  double eps) {
  for (const PlaneEq& pl : planes) {
    bool on = true;
    for (const Vec3& p : ring) {
      if (std::fabs(ray3d::Dot(ray3d::Sub(p, pl.point), pl.normal)) > eps) {
        on = false;
        break;
      }
    }
    if (on)
      return true;
  }
  return false;
}

/// Fragments of \p src's faces, each split by \p cutPlanes until it is wholly inside or wholly
/// outside the cutter. A fragment that lies ON one of the cutter's planes (a coincident face) is put
/// in \p coplanar for later op-aware resolution; every other fragment is kept in \p out when
/// `keepInside == (it is inside the cutter)`. \p flipNormal reverses both the normal and the winding.
void CollectFragments(const Solid& src, const Solid& cutter, const std::vector<PlaneEq>& cutPlanes,
                      bool keepInside, bool flipNormal, double eps, double scale,
                      std::vector<PolyFace>* out, std::vector<PolyFace>* coplanar) {
  for (const Face& f : src.faces) {
    std::vector<Vec3> ring;
    for (const EdgeUse& u : f.loops[0].uses) {
      const Edge& e = src.edges[static_cast<std::size_t>(u.edge)];
      ring.push_back(src.vertices[static_cast<std::size_t>(u.reversed ? e.v1 : e.v0)].p);
    }
    std::vector<std::vector<Vec3>> frags{ring};
    for (const PlaneEq& pl : cutPlanes) {
      std::vector<std::vector<Vec3>> next;
      for (const std::vector<Vec3>& fr : frags) {
        std::vector<Vec3> a;
        std::vector<Vec3> bl;
        ClipPolygon(fr, pl, eps, &a, &bl);
        if (!a.empty())
          next.push_back(std::move(a));
        if (!bl.empty())
          next.push_back(std::move(bl));
      }
      frags = std::move(next);
    }
    for (const std::vector<Vec3>& fr : frags) {
      if (fr.size() < 3)
        continue;
      Vec3 nrm = f.surface.frame.zAxis;
      std::vector<Vec3> r = fr;
      if (flipNormal) {
        nrm = ray3d::Scale(nrm, -1.0);
        std::reverse(r.begin(), r.end());
      }
      if (RingOnAnyPlane(fr, cutPlanes, eps)) {
        coplanar->push_back(PolyFace{std::move(r), nrm});
        continue;
      }
      if (PointInPlanarSolid(RingCentroid(fr), cutter, scale) == keepInside)
        out->push_back(PolyFace{std::move(r), nrm});
    }
  }
}

/// Resolve the coincident (on-a-cutter-plane) fragments both operands produced. A patch that has a
/// matching patch from the other operand is a shared face: kept once if the two normals agree,
/// cancelled entirely if they oppose (an internal wall). A patch with no partner is an ordinary
/// exterior fragment and is kept when `keepInside` matches its position relative to the cutter.
void MergeCoplanar(std::vector<PolyFace>* ca, std::vector<PolyFace>* cb, const Solid& cutterForA,
                   const Solid& cutterForB, bool keepInsideA, bool keepInsideB, double eps, double scale,
                   std::vector<PolyFace>* out) {
  std::vector<char> deadB(cb->size(), 0);
  auto match = [&](const PolyFace& p) {
    const Vec3 c = RingCentroid(p.ring);
    for (std::size_t j = 0; j < cb->size(); ++j) {
      if (deadB[j])
        continue;
      const PolyFace& q = (*cb)[j];
      if (ray3d::Length(ray3d::Sub(c, RingCentroid(q.ring))) > eps)
        continue;
      if (std::fabs(std::fabs(ray3d::Dot(ray3d::Normalize(p.normal), ray3d::Normalize(q.normal))) - 1.0) > 1e-6)
        continue;
      return static_cast<int>(j);
    }
    return -1;
  };
  for (PolyFace& p : *ca) {
    if (p.ring.size() < 3)
      continue;
    const int j = match(p);
    if (j >= 0) {
      deadB[static_cast<std::size_t>(j)] = 1;
      if (ray3d::Dot(ray3d::Normalize(p.normal), ray3d::Normalize((*cb)[static_cast<std::size_t>(j)].normal)) > 0.0)
        out->push_back(std::move(p));  // agree: one shared face
      // oppose: an internal wall, both drop
    } else if (PointInPlanarSolid(RingCentroid(p.ring), cutterForA, scale) == keepInsideA) {
      out->push_back(std::move(p));
    }
  }
  for (std::size_t j = 0; j < cb->size(); ++j) {
    if (deadB[j] || (*cb)[j].ring.size() < 3)
      continue;
    if (PointInPlanarSolid(RingCentroid((*cb)[j].ring), cutterForB, scale) == keepInsideB)
      out->push_back(std::move((*cb)[j]));
  }
}

/// Try the curved recognisers. `*handled` true means the result (success or a named refusal) is
/// final; false means no curved recogniser applied and the caller refuses the pair itself.
[[nodiscard]] bool TryBooleanCurved(const Solid& a, const Solid& b, BoolOp op, std::vector<Solid>* out,
                                    bool* handled, Problem* outWhy) {
  *handled = false;
  CylinderShape ca;
  CylinderShape cb;
  const bool aCyl = ClassifyCylinder(a, &ca);
  const bool bCyl = ClassifyCylinder(b, &cb);
  if (aCyl && bCyl) {
    const bool okS = TryBooleanSteinmetz(ca, cb, op, out, handled, outWhy);
    if (*handled)
      return okS;
    const bool okBp = TryBooleanBranchPipe(ca, cb, op, out, handled, outWhy);
    if (*handled)
      return okBp;
    return TryBooleanCoaxialCylinders(a, b, ca, cb, op, out, handled, outWhy);
  }
  if (aCyl && AllFacesPlanar(b))
    return TryBooleanCylinderThroughPlanar(b, a, ca, op, /*cylIsMinuend=*/true, out, handled, outWhy);
  if (bCyl && AllFacesPlanar(a))
    return TryBooleanCylinderThroughPlanar(a, b, cb, op, /*cylIsMinuend=*/false, out, handled, outWhy);

  // issue #495 / REQ-338 338c (338d-1 adds Cone bands) — general coaxial multi-segment stack
  // folding, UNION only. Neither operand classified as one bare cylinder above (that pair is already
  // handled by aCyl&&bCyl), so try recognising either or both as an already-stepped/tapered coaxial
  // stack (REQ-337's own composite cutter, or a target left over from a prior 338a/UNION step)
  // sharing one axis line, and fold the new piece onto it via the same closed-form interval-merge
  // `TryBooleanCoaxialCylinders`'s own UNION branch already uses for a single pair, generalised to N
  // segments per side.
  if (op == BoolOp::Union) {
    Vec3 apA{}, adA{};
    Vec3 apB{}, adB{};
    std::vector<CoaxialSeg> segsA, segsB;
    const bool aStack = ExtractCoaxialStack(a, &apA, &adA, &segsA);
    const bool bStack = ExtractCoaxialStack(b, &apB, &adB, &segsB);
    // Enter this general path once either side is more than a single bare cylinder: an existing
    // multi-segment stack (338c), or — 338d-1 — a single Cone band (a plain cone isn't `aCyl`, so a
    // bare cylinder + bare cone pair never reaches `TryBooleanCoaxialCylinders` above and needs this
    // path even at N==1 segment per side).
    auto hasTaper = [](const std::vector<CoaxialSeg>& segs) {
      for (const CoaxialSeg& s : segs)
        if (std::fabs(s.r0 - s.r1) > 1e-9 * std::max({1.0, s.r0, s.r1}))
          return true;
      return false;
    };
    if (aStack && bStack &&
       (segsA.size() > 1 || segsB.size() > 1 || hasTaper(segsA) || hasTaper(segsB))) {
      const double sc = std::max(ModelScale(a), ModelScale(b));
      const Vec3 d = ray3d::Sub(apB, apA);
      const double perp = ray3d::Length(ray3d::Sub(d, ray3d::Scale(adA, ray3d::Dot(d, adA))));
      if (std::fabs(std::fabs(ray3d::Dot(adA, adB)) - 1.0) <= 1e-7 && perp <= 1e-6 * sc) {
        // Re-express B's segments along A's own axis parametrisation (same line, opposite-facing
        // axis direction flips the sign) before merging.
        const double dOnA = ray3d::Dot(d, adA);
        const double sign = ray3d::Dot(adA, adB) >= 0.0 ? 1.0 : -1.0;
        std::vector<CoaxialSeg> segsBonA;
        segsBonA.reserve(segsB.size());
        for (const CoaxialSeg& s : segsB) {
          double z0 = dOnA + sign * s.z0;
          double z1 = dOnA + sign * s.z1;
          double r0 = s.r0, r1 = s.r1;
          if (z0 > z1) {
            std::swap(z0, z1);
            std::swap(r0, r1);  // B's own low/high ends flip along A's parametrisation too
          }
          segsBonA.push_back({z0, z1, r0, r1});
        }
        std::sort(segsBonA.begin(), segsBonA.end(),
                 [](const CoaxialSeg& x, const CoaxialSeg& y) { return x.z0 < y.z0; });
        *handled = true;
        return TryBooleanCoaxialStackUnion(a, b, apA, adA, segsA, segsBonA, out, outWhy);
      }
    }
  }

  SphereShape sa;
  SphereShape sb;
  const bool aSph = ClassifySphere(a, &sa);
  const bool bSph = ClassifySphere(b, &sb);
  if (aSph && bCyl) {
    const bool ok = TryBooleanSphereCylinder(sa, cb, op, /*sphereIsMinuend=*/true, out, handled, outWhy);
    if (*handled)
      return ok;
  }
  if (bSph && aCyl) {
    const bool ok =
        TryBooleanSphereCylinder(sb, ca, op, /*sphereIsMinuend=*/false, out, handled, outWhy);
    if (*handled)
      return ok;
  }
  if (aSph && AllFacesPlanar(b))
    return TryBooleanSpherePlanar(b, a, sa, op, /*sphIsMinuend=*/true, out, handled, outWhy);
  if (bSph && AllFacesPlanar(a))
    return TryBooleanSpherePlanar(a, b, sb, op, /*sphIsMinuend=*/false, out, handled, outWhy);
  return false;
}

[[nodiscard]] bool BooleanPlanar(const Solid& a, const Solid& b, BoolOp op, std::vector<Solid>* out,
                                 Problem* outWhy) {
  if (!out)
    return false;
  out->clear();
  const Problem va = Validate(a);
  if (va != Problem::Ok)
    return Fail(va, outWhy);
  const Problem vb = Validate(b);
  if (vb != Problem::Ok)
    return Fail(vb, outWhy);
  const bool aCurved = !AllFacesPlanar(a);
  const bool bCurved = !AllFacesPlanar(b);
  if (aCurved || bCurved) {
    bool handled = false;
    const bool ok = TryBooleanCurved(a, b, op, out, &handled, outWhy);
    if (handled)
      return ok;
    return Fail(Problem::BooleanCurvedFace, outWhy);
  }
  const double scale = std::max(ModelScale(a), ModelScale(b));
  const double eps = 1e-7 * scale;

  const std::vector<PlaneEq> pa = FacePlanes(a);
  const std::vector<PlaneEq> pb = FacePlanes(b);
  const bool overlap = SolidsOverlap(a, b, scale);

  // Per operation: which side of each operand's surface is kept, and whether B's kept faces flip.
  bool keepInA = false;  // keep A's fragments that are INSIDE B?
  bool keepInB = false;  // keep B's fragments that are INSIDE A?
  bool flipB = false;
  Problem weldFail = Problem::BooleanResultInvalid;
  switch (op) {
  case BoolOp::Intersect:
    if (!overlap)
      return Fail(Problem::BooleanEmptyResult, outWhy);
    keepInA = true;
    keepInB = true;
    weldFail = Problem::BooleanEmptyResult;
    break;
  case BoolOp::Union:
    if (!overlap) {
      out->push_back(a);
      out->push_back(b);
      return Succeed(outWhy);
    }
    keepInA = false;
    keepInB = false;
    break;
  case BoolOp::Subtract:
    if (!overlap) {
      out->push_back(a);
      return Succeed(outWhy);
    }
    keepInA = false;  // A outside B
    keepInB = true;   // B inside A
    flipB = true;     // ...with its normals flipped, to bound the removed volume
    break;
  }

  std::vector<PolyFace> polys;
  std::vector<PolyFace> copA;
  std::vector<PolyFace> copB;
  CollectFragments(a, b, pb, keepInA, /*flip=*/false, eps, scale, &polys, &copA);
  CollectFragments(b, a, pa, keepInB, flipB, eps, scale, &polys, &copB);
  MergeCoplanar(&copA, &copB, b, a, keepInA, keepInB, eps, scale, &polys);

  Solid r;
  if (!WeldPlanarSolid(polys, scale, weldFail, &r, outWhy))
    return false;
  out->push_back(std::move(r));
  return Succeed(outWhy);
}

} // namespace

bool BooleanUnion(const Solid& a, const Solid& b, std::vector<Solid>* out, Problem* outWhy) {
  return BooleanPlanar(a, b, BoolOp::Union, out, outWhy);
}
bool BooleanSubtract(const Solid& a, const Solid& b, std::vector<Solid>* out, Problem* outWhy) {
  return BooleanPlanar(a, b, BoolOp::Subtract, out, outWhy);
}
bool BooleanIntersect(const Solid& a, const Solid& b, std::vector<Solid>* out, Problem* outWhy) {
  return BooleanPlanar(a, b, BoolOp::Intersect, out, outWhy);
}

bool SubtractCircle(const Solid& base, const Vec3& centre, const Vec3& normal, double radius,
                    double depth, Solid* out, Problem* outWhy) {
  if (!out) return Fail(Problem::NonPositiveRadius, outWhy);
  if (!(radius > 0.0) || !std::isfinite(radius)) return Fail(Problem::NonPositiveRadius, outWhy);
  if (!(depth > 0.0) || !std::isfinite(depth)) return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FinitePoint(centre) || !AllFinite({normal.x, normal.y, normal.z}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  const double nLen = ray3d::Length(normal);
  if (!(nLen > 1e-12)) return Fail(Problem::SliceDegeneratePlane, outWhy);
  const Vec3 nHat = ray3d::Scale(normal, 1.0 / nLen);
  ucs::Ucs fr;
  if (!ucs::FromNormal(centre, nHat, &fr)) return Fail(Problem::DegenerateFrame, outWhy);
  Solid cutter;
  if (!MakeCylinder(fr, radius, depth, &cutter, outWhy)) return false;
  std::vector<Solid> r;
  if (!BooleanSubtract(base, cutter, &r, outWhy)) return false;
  if (r.size() != 1) return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(r[0]);
  return Succeed(outWhy);
}

// Try to build a through-hole bore directly without a cutter solid. Handles both polygonal
// caps (boxes) and circular disk caps (cylinders) via the same disk-aware hit test used in
// TryBooleanCylinderThroughPlanar. This is what makes flange bolt holes work when the base
// itself is a cylinder.
bool TryBoreThroughDirect(const Solid& base, const Vec3& centre, const Vec3& nHat, double radius, Solid* out, Problem* outWhy) {
  double scale = 0;
  for (auto &v: base.vertices) scale = std::max(scale, std::fabs(v.p.x)+std::fabs(v.p.y)+std::fabs(v.p.z));
  scale = std::max(scale, radius+1.0);
  double eps = 1e-7 * std::max(scale, 1.0);
  struct Hit { int face=-1; Vec3 point; Vec3 n; };
  Hit entry, exitH;
  bool haveEntry=false, haveExit=false;
  for (int fi=0; fi<(int)base.faces.size(); ++fi){
    const Face& f = base.faces[static_cast<size_t>(fi)];
    if (f.surface.kind != SurfaceKind::Plane) continue;
    Vec3 n = f.surface.frame.zAxis;
    double dn = ray3d::Dot(n, nHat);
    if (std::fabs(std::fabs(dn)-1.0) > 1e-6) continue;
    bool isDisk=false; Vec3 diskC{}; double diskR=0;
    std::vector<Vec3> ring = FaceRing(base, f);
    if (ring.size() < 3){
      if (f.loops.size()>=1 && f.loops[0].uses.size()==2){
        const Edge& e0 = base.edges[static_cast<size_t>(f.loops[0].uses[0].edge)];
        const Edge& e1 = base.edges[static_cast<size_t>(f.loops[0].uses[1].edge)];
        if (e0.kind==CurveKind::Arc && e1.kind==CurveKind::Arc && std::fabs(e0.radius-e1.radius)<1e-9*std::max(e0.radius,1.0) && ray3d::Length(ray3d::Sub(e0.frame.origin,e1.frame.origin))<1e-9*std::max(e0.radius,1.0)){
          isDisk=true; diskC=e0.frame.origin; diskR=e0.radius;
        }
      }
      if(!isDisk) continue;
    }
    Vec3 hp;
    if (isDisk){
      double t = ray3d::Dot(ray3d::Sub(diskC, centre), n) / dn;
      hp = ray3d::Add(centre, ray3d::Scale(nHat, t));
      double d = ray3d::Length(ray3d::Sub(hp, diskC));
      if (d > diskR + eps) continue;
      if (diskR - d < radius + eps) continue;
      // Also check inner holes (already drilled) – hp must not be inside any existing hole
      bool insideHole=false;
      for(size_t li=1; li<f.loops.size(); ++li){
        if(f.loops[li].uses.size()!=2) continue;
        const Edge& he0 = base.edges[static_cast<size_t>(f.loops[li].uses[0].edge)];
        const Edge& he1 = base.edges[static_cast<size_t>(f.loops[li].uses[1].edge)];
        if(he0.kind!=CurveKind::Arc || he1.kind!=CurveKind::Arc) continue;
        Vec3 hc = he0.frame.origin; double hr = he0.radius;
        double hd = ray3d::Length(ray3d::Sub(hp, hc));
        if(hd < hr + radius + eps){ insideHole=true; break; }
      }
      if(insideHole) continue;
    } else {
      double t = ray3d::Dot(ray3d::Sub(ring[0], centre), n) / dn;
      hp = ray3d::Add(centre, ray3d::Scale(nHat, t));
      bool onEdge=false;
      if(!PointInPolygon3D(hp, ring, n, eps, &onEdge)) continue;
      double clr = std::numeric_limits<double>::max();
      for(size_t i=0;i<ring.size();++i){
        Vec3 p0=ring[i], p1=ring[(i+1)%ring.size()];
        Vec3 e=ray3d::Sub(p1,p0); double len2=ray3d::Dot(e,e);
        double u=len2>1e-24 ? ray3d::Dot(ray3d::Sub(hp,p0),e)/len2 : 0; u=std::clamp(u,0.0,1.0);
        clr=std::min(clr, ray3d::Length(ray3d::Sub(hp, ray3d::Add(p0, ray3d::Scale(e,u)))));
      }
      if(clr < radius + eps) continue;
    }
    Hit h; h.face=fi; h.point=hp; h.n=n;
    if(dn < 0){ if(!haveEntry){ entry=h; haveEntry=true; } } else { if(!haveExit){ exitH=h; haveExit=true; } }
  }
  if(!haveEntry || !haveExit) return false;
  Solid r;
  if(!BuildBore(base, entry.face, entry.point, entry.n, radius, true, exitH.face, exitH.point, 0, &r, outWhy)) return false;
  *out = std::move(r);
  return true;
}

/// \ref TryBoreThroughDirect's own entry/exit planar-face search, generalized to a stepped bore
/// (issue #493 / REQ-337): the entry side is cleared against \p radii.front() (the widest, by
/// \ref BuildSteppedBore's own non-increasing scope) and the exit side against \p radii.back().
/// \p internalShoulders is the same absolute-position list \ref BuildSteppedBore takes — the found
/// entry/exit hit points become \p entryC / the exit point passed on to it, never the caller's own
/// (possibly overhanging) cutter extent.
bool TrySteppedBoreThroughDirect(const Solid& base, const Vec3& centre, const Vec3& nHat,
                                 const std::vector<double>& radii,
                                 const std::vector<Vec3>& internalShoulders, Solid* out,
                                 Problem* outWhy) {
  if (radii.empty() || internalShoulders.size() + 1 != radii.size())
    return false;
  const double entryRadius = radii.front();
  const double exitRadius = radii.back();
  double scale = 0;
  for (auto &v: base.vertices) scale = std::max(scale, std::fabs(v.p.x)+std::fabs(v.p.y)+std::fabs(v.p.z));
  scale = std::max(scale, std::max(entryRadius, exitRadius)+1.0);
  double eps = 1e-7 * std::max(scale, 1.0);
  struct Hit { int face=-1; Vec3 point; Vec3 n; };
  Hit entry, exitH;
  bool haveEntry=false, haveExit=false;
  for (int fi=0; fi<(int)base.faces.size(); ++fi){
    const Face& f = base.faces[static_cast<size_t>(fi)];
    if (f.surface.kind != SurfaceKind::Plane) continue;
    Vec3 n = f.surface.frame.zAxis;
    double dn = ray3d::Dot(n, nHat);
    if (std::fabs(std::fabs(dn)-1.0) > 1e-6) continue;
    const double radius = dn < 0 ? entryRadius : exitRadius;  // which side this face could be
    bool isDisk=false; Vec3 diskC{}; double diskR=0;
    std::vector<Vec3> ring = FaceRing(base, f);
    if (ring.size() < 3){
      if (f.loops.size()>=1 && f.loops[0].uses.size()==2){
        const Edge& e0 = base.edges[static_cast<size_t>(f.loops[0].uses[0].edge)];
        const Edge& e1 = base.edges[static_cast<size_t>(f.loops[0].uses[1].edge)];
        if (e0.kind==CurveKind::Arc && e1.kind==CurveKind::Arc && std::fabs(e0.radius-e1.radius)<1e-9*std::max(e0.radius,1.0) && ray3d::Length(ray3d::Sub(e0.frame.origin,e1.frame.origin))<1e-9*std::max(e0.radius,1.0)){
          isDisk=true; diskC=e0.frame.origin; diskR=e0.radius;
        }
      }
      if(!isDisk) continue;
    }
    Vec3 hp;
    if (isDisk){
      double t = ray3d::Dot(ray3d::Sub(diskC, centre), n) / dn;
      hp = ray3d::Add(centre, ray3d::Scale(nHat, t));
      double d = ray3d::Length(ray3d::Sub(hp, diskC));
      if (d > diskR + eps) continue;
      if (diskR - d < radius + eps) continue;
      bool insideHole=false;
      for(size_t li=1; li<f.loops.size(); ++li){
        if(f.loops[li].uses.size()!=2) continue;
        const Edge& he0 = base.edges[static_cast<size_t>(f.loops[li].uses[0].edge)];
        const Edge& he1 = base.edges[static_cast<size_t>(f.loops[li].uses[1].edge)];
        if(he0.kind!=CurveKind::Arc || he1.kind!=CurveKind::Arc) continue;
        Vec3 hc = he0.frame.origin; double hr = he0.radius;
        double hd = ray3d::Length(ray3d::Sub(hp, hc));
        if(hd < hr + radius + eps){ insideHole=true; break; }
      }
      if(insideHole) continue;
    } else {
      double t = ray3d::Dot(ray3d::Sub(ring[0], centre), n) / dn;
      hp = ray3d::Add(centre, ray3d::Scale(nHat, t));
      bool onEdge=false;
      if(!PointInPolygon3D(hp, ring, n, eps, &onEdge)) continue;
      double clr = std::numeric_limits<double>::max();
      for(size_t i=0;i<ring.size();++i){
        Vec3 p0=ring[i], p1=ring[(i+1)%ring.size()];
        Vec3 e=ray3d::Sub(p1,p0); double len2=ray3d::Dot(e,e);
        double u=len2>1e-24 ? ray3d::Dot(ray3d::Sub(hp,p0),e)/len2 : 0; u=std::clamp(u,0.0,1.0);
        clr=std::min(clr, ray3d::Length(ray3d::Sub(hp, ray3d::Add(p0, ray3d::Scale(e,u)))));
      }
      if(clr < radius + eps) continue;
    }
    Hit h; h.face=fi; h.point=hp; h.n=n;
    if(dn < 0){ if(!haveEntry){ entry=h; haveEntry=true; } } else { if(!haveExit){ exitH=h; haveExit=true; } }
  }
  if(!haveEntry || !haveExit) return false;
  Solid r;
  if(!BuildSteppedBore(base, entry.face, entry.point, entry.n, radii, internalShoulders, exitH.face,
                       exitH.point, &r, outWhy)) return false;
  *out = std::move(r);
  return true;
}

/// Radial cross-hole entirely inside ONE segment of a coaxial cylinder stack (issue #497 / REQ-339,
/// ADR-057, scope narrowed by the D-2026-09-14 revision below): a plain cylindrical cutter whose axis
/// is perpendicular to the stack's own axis and passes through it (zero offset, matching
/// \ref BuildBranchPipeSubtract's own pose), entering and exiting through the curved wall of a
/// single segment. \p base need not be a bare cylinder — this is exactly the case that still refuses
/// when the target is an N-segment coaxial stack: the affected segment is isolated (via
/// \ref BuildBranchPipeSubtract, unchanged), cut, and welded (\ref WeldCoaxialCap, which also
/// inserts the connecting shoulder ring when the neighbour's radius differs) back onto whichever
/// untouched segments remain. Returns false (not a refusal by name) when the shape falls outside
/// this increment's scope — a Cone band, an offset or non-perpendicular cutter, or a cut spanning
/// more than one segment — so the caller's own ordinary refusal stands.
bool SubtractRadialCrossHoleThroughStack(const Solid& base, const Vec3& centre,
                                                       const Vec3& axis, double radius, Solid* out,
                                                       Problem* outWhy) {
  Vec3 axisPoint{}, axisDir{};
  std::vector<CoaxialSeg> segs;
  if (!ExtractCoaxialStack(base, &axisPoint, &axisDir, &segs) || segs.size() < 2 || !(radius > 0.0))
    return false;  // not a multi-segment stack — the ordinary recognisers already cover N==1
  const double sc = std::max(1.0, ModelScale(base));
  const Vec3 cutDirRaw = ray3d::Normalize(axis);
  if (!(ray3d::Length(cutDirRaw) > 0.5) || std::fabs(ray3d::Dot(cutDirRaw, axisDir)) > 1e-6)
    return false;  // degenerate, or not perpendicular to the stack axis — out of scope
  const Vec3 toC = ray3d::Sub(centre, axisPoint);
  const double zh = ray3d::Dot(toC, axisDir);
  const Vec3 perp = ray3d::Sub(toC, ray3d::Scale(axisDir, zh));
  if (ray3d::Length(perp) > 1e-6 * sc)
    return false;  // the cutter axis does not pass through the stack's own axis — offset, not covered

  int loIdx = -1, hiIdx = -1;
  for (int i = 0; i < static_cast<int>(segs.size()); ++i) {
    if (segs[static_cast<std::size_t>(i)].z1 > zh - radius + 1e-7 * sc &&
        segs[static_cast<std::size_t>(i)].z0 < zh + radius - 1e-7 * sc) {
      if (loIdx < 0)
        loIdx = i;
      hiIdx = i;
    }
  }
  // A cut spanning MORE than one shoulder (two or more segment boundaries) is out of REQ-339's own
  // scope boundaries — the isolate/cut/weld primitive is not structurally excluded from it, but is
  // not tested or claimed working beyond one shoulder.
  if (loIdx < 0 || hiIdx > loIdx + 1)
    return false;

  // A right-handed frame: Z the stack axis, Y the cutter's own direction, X the seam direction.
  ucs::Ucs fr;
  fr.origin = axisPoint;
  fr.zAxis = axisDir;
  fr.yAxis = cutDirRaw;
  fr.xAxis = ray3d::Normalize(ray3d::Cross(fr.yAxis, fr.zAxis));
  if (!(ray3d::Length(fr.xAxis) > 0.5))
    return false;
  fr.yAxis = ray3d::Normalize(ray3d::Cross(fr.zAxis, fr.xAxis));

  auto capFaceAt = [&](const Solid& piece, double z, bool top) -> int {
    const Vec3 want = ray3d::Add(axisPoint, ray3d::Scale(axisDir, z));
    for (int i = 0; i < static_cast<int>(piece.faces.size()); ++i) {
      const Face& f = piece.faces[static_cast<std::size_t>(i)];
      if (f.surface.kind != SurfaceKind::Plane ||
          ray3d::Length(ray3d::Sub(f.surface.frame.origin, want)) > 1e-6 * sc)
        continue;
      const double d = ray3d::Dot(f.surface.frame.zAxis, axisDir);
      if (top ? d > 1.0 - 1e-6 : d < -1.0 + 1e-6)
        return i;
    }
    return -1;
  };

  Solid result;
  Problem whyMid = Problem::Ok;
  if (hiIdx == loIdx) {
    if (std::fabs(segs[static_cast<std::size_t>(loIdx)].r0 - segs[static_cast<std::size_t>(loIdx)].r1) >
        1e-7 * sc)
      return false;  // a Cone band — out of scope
    ucs::Ucs bfr;
    bfr.origin = ray3d::Add(axisPoint, ray3d::Scale(axisDir, zh));  // its cutter sits at local z=0
    bfr.zAxis = axisDir;
    bfr.xAxis = fr.yAxis;
    bfr.yAxis = ray3d::Scale(fr.xAxis, -1.0);
    const double zB0 = segs[static_cast<std::size_t>(loIdx)].z0 - zh;
    const double zB1 = segs[static_cast<std::size_t>(loIdx)].z1 - zh;
    if (!BuildBranchPipeSubtract(bfr, radius, segs[static_cast<std::size_t>(loIdx)].r0, zB0, zB1,
                                 &result, &whyMid))
      return Fail(whyMid, outWhy);
  } else {
    // Exactly one shoulder crossed (REQ-339, GitHub issue #504): isolate the two affected segments
    // and cut them as a single stepped-wall solid rather than two independently-cut bare cylinders,
    // so the mouth curve on each side of the shoulder is built (and, via `IsectStripAt`'s clipping
    // extension, measured) correctly.
    if (std::fabs(segs[static_cast<std::size_t>(loIdx)].r0 - segs[static_cast<std::size_t>(loIdx)].r1) >
            1e-7 * sc ||
        std::fabs(segs[static_cast<std::size_t>(hiIdx)].r0 - segs[static_cast<std::size_t>(hiIdx)].r1) >
            1e-7 * sc)
      return false;  // a Cone band on either side of the shoulder — out of scope
    ucs::Ucs bfr;
    bfr.origin = ray3d::Add(axisPoint, ray3d::Scale(axisDir, zh));  // its cutter sits at local z=0
    bfr.zAxis = axisDir;
    bfr.xAxis = fr.yAxis;
    bfr.yAxis = ray3d::Scale(fr.xAxis, -1.0);
    const double zB0 = segs[static_cast<std::size_t>(loIdx)].z0 - zh;
    const double shoulderZ = segs[static_cast<std::size_t>(hiIdx)].z0 - zh;
    const double zB1 = segs[static_cast<std::size_t>(hiIdx)].z1 - zh;
    if (!BuildCoaxialStepRadialSubtract(bfr, radius, segs[static_cast<std::size_t>(loIdx)].r0,
                                        segs[static_cast<std::size_t>(hiIdx)].r0, zB0, shoulderZ, zB1,
                                        &result, &whyMid))
      return Fail(whyMid, outWhy);
  }

  // `BuildBranchPipeSubtract` places its own two cap rims' pair of vertices at LOCAL (0, +-R, z) —
  // its own frame's +-Y — while `BuildCoaxialStack` places them at +-X. Both are proven-correct on
  // their own; they simply disagree about which axis carries the cap seam. `WeldCoaxialCap`'s equal-
  // radius fallback (`WeldAtSharedFace`) matches vertices by position, so on the rare occasion a
  // neighbour's radius happens to equal `result`'s own, the neighbour pieces built here must use
  // whichever axis `result`'s own two cap-rim vertices actually sit on (unequal radii, the common
  // case, do not need this — `WeldCoaxialCap` reuses each side's rim edges directly, no matching).
  const Vec3 capXAxis = ray3d::Scale(fr.xAxis, -1.0);
  if (loIdx > 0) {
    std::vector<double> zb, rb0, rb1;
    for (int i = 0; i < loIdx; ++i) {
      zb.push_back(segs[static_cast<std::size_t>(i)].z0);
      rb0.push_back(segs[static_cast<std::size_t>(i)].r0);
      rb1.push_back(segs[static_cast<std::size_t>(i)].r1);
    }
    zb.push_back(segs[static_cast<std::size_t>(loIdx - 1)].z1);
    ucs::Ucs bfr = fr;
    bfr.xAxis = capXAxis;
    bfr.yAxis = ray3d::Normalize(ray3d::Cross(bfr.zAxis, bfr.xAxis));
    bfr.origin = ray3d::Add(axisPoint, ray3d::Scale(axisDir, zb.front()));
    Solid below;
    Problem whyB = Problem::Ok;
    if (!BuildCoaxialStack(bfr, zb, rb0, rb1, {}, &below, &whyB))
      return Fail(whyB, outWhy);
    const int fBelow = capFaceAt(below, zb.back(), /*top=*/true);
    const int fMid = capFaceAt(result, segs[static_cast<std::size_t>(loIdx)].z0, /*top=*/false);
    if (fBelow < 0 || fMid < 0)
      return Fail(Problem::BooleanResultInvalid, outWhy);
    Solid welded;
    if (!WeldCoaxialCap(below, fBelow, result, fMid, &welded, outWhy))
      return false;
    result = std::move(welded);
  }
  if (hiIdx + 1 < static_cast<int>(segs.size())) {
    std::vector<double> za, ra0, ra1;
    za.push_back(segs[static_cast<std::size_t>(hiIdx + 1)].z0);
    for (int i = hiIdx + 1; i < static_cast<int>(segs.size()); ++i) {
      ra0.push_back(segs[static_cast<std::size_t>(i)].r0);
      ra1.push_back(segs[static_cast<std::size_t>(i)].r1);
      za.push_back(segs[static_cast<std::size_t>(i)].z1);
    }
    ucs::Ucs afr = fr;
    afr.xAxis = capXAxis;
    afr.yAxis = ray3d::Normalize(ray3d::Cross(afr.zAxis, afr.xAxis));
    afr.origin = ray3d::Add(axisPoint, ray3d::Scale(axisDir, za.front()));
    Solid above;
    Problem whyA = Problem::Ok;
    if (!BuildCoaxialStack(afr, za, ra0, ra1, {}, &above, &whyA))
      return Fail(whyA, outWhy);
    const int fAbove = capFaceAt(above, za.front(), /*top=*/false);
    const int fMid = capFaceAt(result, segs[static_cast<std::size_t>(hiIdx)].z1, /*top=*/true);
    if (fAbove < 0 || fMid < 0)
      return Fail(Problem::BooleanResultInvalid, outWhy);
    Solid welded;
    if (!WeldCoaxialCap(result, fMid, above, fAbove, &welded, outWhy))
      return false;
    result = std::move(welded);
  }
  *out = std::move(result);
  return Succeed(outWhy);
}


bool SubtractCircleThrough(const Solid& base, const Vec3& centre, const Vec3& normal, double radius,
                           Solid* out, Problem* outWhy) {
  if (base.vertices.empty()) return Fail(Problem::NoShell, outWhy);
  if (!(radius > 0.0) || !std::isfinite(radius)) return Fail(Problem::NonPositiveRadius, outWhy);
  if (!FinitePoint(centre) || !AllFinite({normal.x, normal.y, normal.z}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  const double nLen = ray3d::Length(normal);
  if (!(nLen > 1e-12)) return Fail(Problem::SliceDegeneratePlane, outWhy);
  const Vec3 nHat = ray3d::Scale(normal, 1.0 / nLen);
  // First try direct bore (handles cylinder caps as well as boxes)
  {
    Solid bore;
    if (TryBoreThroughDirect(base, centre, nHat, radius, &bore, outWhy)) {
      *out = std::move(bore);
      return Succeed(outWhy);
    }
  }
  Vec3 mn, mx;
  SolidAabb(base, &mn, &mx);
  const double diag = ray3d::Length(ray3d::Sub(mx, mn));
  const double depth = diag * 2.0 + 20.0 * std::max(radius, 1.0);
  const Vec3 baseOrigin = ray3d::Sub(centre, ray3d::Scale(nHat, depth * 0.5));
  ucs::Ucs fr;
  if (!ucs::FromNormal(baseOrigin, nHat, &fr)) return Fail(Problem::DegenerateFrame, outWhy);
  Solid cutter;
  if (!MakeCylinder(fr, radius, depth, &cutter, outWhy)) return false;
  std::vector<Solid> r;
  if (!BooleanSubtract(base, cutter, &r, outWhy)) return false;
  if (r.size() != 1) return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(r[0]);
  return Succeed(outWhy);
}

} // namespace brep
