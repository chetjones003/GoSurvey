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

[[nodiscard]] bool AllFinite(std::initializer_list<double> vs) {
  for (double v : vs) {
    if (!std::isfinite(v))
      return false;
  }
  return true;
}

[[nodiscard]] bool FinitePoint(const Vec3& p) { return AllFinite({p.x, p.y, p.z}); }

// ---------------------------------------------------------------------------------------------
// Construction helpers. Every primitive is built in its own canonical local frame and then placed,
// which is what keeps the seven builders readable: the interesting part of a torus is its topology,
// not the six dot products that would otherwise be repeated on every vertex.
// ---------------------------------------------------------------------------------------------

int AddVertex(Solid* s, const Vec3& p) {
  s->vertices.push_back(Vertex{p});
  return static_cast<int>(s->vertices.size()) - 1;
}

int AddLine(Solid* s, int v0, int v1) {
  Edge e;
  e.kind = CurveKind::Line;
  e.v0 = v0;
  e.v1 = v1;
  s->edges.push_back(e);
  return static_cast<int>(s->edges.size()) - 1;
}

/// An arc edge from `v0` to `v1`, sweeping \p sweep radians CCW about \p normal around \p centre.
///
/// The frame is built the same way `ucs::FromNormal` builds one — Z is the normal, and here X is
/// pinned to the start point rather than to the Arbitrary Axis Algorithm's tie-break, because an
/// edge always has a start vertex and anchoring to it means `EdgePointAt(e, 0)` is that vertex
/// exactly rather than to within a rounding of it.
int AddArc(Solid* s, int v0, int v1, const Vec3& centre, const Vec3& normal, double sweep) {
  Edge e;
  e.kind = CurveKind::Arc;
  e.v0 = v0;
  e.v1 = v1;
  const Vec3 z = ray3d::Normalize(normal);
  const Vec3 toStart = ray3d::Sub(s->vertices[v0].p, centre);
  // Orthogonalise defensively: a start point a rounding off the plane would otherwise skew the
  // frame, and a skewed frame silently mis-places every point along the arc.
  const Vec3 x = ray3d::Normalize(ray3d::Sub(toStart, ray3d::Scale(z, ray3d::Dot(toStart, z))));
  e.frame.origin = centre;
  e.frame.zAxis = z;
  e.frame.xAxis = x;
  e.frame.yAxis = ray3d::Normalize(ray3d::Cross(z, x));
  e.radius = ray3d::Length(toStart);
  e.sweep = sweep;
  s->edges.push_back(e);
  return static_cast<int>(s->edges.size()) - 1;
}

/// An ellipse edge from `v0` to `v1` on the plane through \p centre with normal \p normal, semi-major
/// \p a along \p majorDir and semi-minor \p b perpendicular to it in the plane. \p sweep is the
/// signed parameter span (CCW about \p normal); `v0` fixes the start parameter. `2*pi` is a full rim.
int AddEllipse(Solid* s, int v0, int v1, const Vec3& centre, const Vec3& normal, const Vec3& majorDir,
               double a, double b, double sweep) {
  Edge e;
  e.kind = CurveKind::Ellipse;
  e.v0 = v0;
  e.v1 = v1;
  const Vec3 z = ray3d::Normalize(normal);
  const Vec3 x = ray3d::Normalize(ray3d::Sub(majorDir, ray3d::Scale(z, ray3d::Dot(majorDir, z))));
  e.frame.origin = centre;
  e.frame.zAxis = z;
  e.frame.xAxis = x;
  e.frame.yAxis = ray3d::Normalize(ray3d::Cross(z, x));
  e.radius = a;
  e.radius2 = b;
  e.sweep = sweep;
  s->edges.push_back(e);
  return static_cast<int>(s->edges.size()) - 1;
}

[[nodiscard]] Surface PlaneSurface(const Vec3& origin, const Vec3& outwardNormal) {
  Surface sf;
  sf.kind = SurfaceKind::Plane;
  // FromNormal cannot fail here — every call site passes a unit axis — but honour it anyway rather
  // than assume, so a future caller with a degenerate normal gets World rather than NaN axes.
  if (!ucs::FromNormal(origin, outwardNormal, &sf.frame)) {
    sf.frame = ucs::Ucs{};
    sf.frame.origin = origin;
  }
  return sf;
}

Face MakePlaneFace(const Vec3& origin, const Vec3& outwardNormal, std::vector<EdgeUse> uses) {
  Face f;
  f.surface = PlaneSurface(origin, outwardNormal);
  Loop lp;
  lp.uses = std::move(uses);
  f.loops.push_back(std::move(lp));
  return f;
}

/// Map a solid built in the canonical local frame into \p frame.
void PlaceInFrame(Solid* s, const ucs::Ucs& frame) {
  auto mapFrame = [&frame](ucs::Ucs* f) {
    f->origin = ucs::UcsToWorld(frame, f->origin);
    f->xAxis = ucs::UcsVectorToWorld(frame, f->xAxis);
    f->yAxis = ucs::UcsVectorToWorld(frame, f->yAxis);
    f->zAxis = ucs::UcsVectorToWorld(frame, f->zAxis);
  };
  for (Vertex& v : s->vertices)
    v.p = ucs::UcsToWorld(frame, v.p);
  for (Edge& e : s->edges) {
    if (e.kind != CurveKind::Line)
      mapFrame(&e.frame);
    for (Surface& sf : e.isectSurfaces)  // Intersection: carry the stored surfaces into the frame too
      mapFrame(&sf.frame);
  }
  for (Face& f : s->faces)
    mapFrame(&f.surface.frame);
  s->recipe.frame = frame;
}

/// One shell holding every face, which is what all seven primitives produce.
void AddSingleShell(Solid* s) {
  Shell sh;
  sh.faces.reserve(s->faces.size());
  for (int i = 0; i < static_cast<int>(s->faces.size()); ++i)
    sh.faces.push_back(i);
  s->shells.push_back(std::move(sh));
}

[[nodiscard]] bool Fail(Problem why, Problem* outWhy) {
  if (outWhy)
    *outWhy = why;
  return false;
}

[[nodiscard]] bool Succeed(Problem* outWhy) {
  if (outWhy)
    *outWhy = Problem::Ok;
  return true;
}

/// Shared entry check for every builder: a frame that is not orthonormal and right-handed would
/// mirror or shear the solid, and a mirrored solid has a negative volume that nothing downstream
/// would question.
[[nodiscard]] bool FrameOk(const ucs::Ucs& frame) {
  return FinitePoint(frame.origin) && ucs::IsRightHandedOrthonormal(frame, 1e-9);
}

// ---------------------------------------------------------------------------------------------
// Per-face analytic integrals. `Area` is the true surface area; `VolumeTerm` is the face's
// contribution to the closed integral of (p - q) . n dA, so that V = (1/3) * sum of the terms.
//
// Every one of these is evaluated in the FACE's own frame with q transformed into it, which is
// what keeps the arithmetic at model scale: at easting 2e6 the world coordinates are large, but
// (p - q) never is.
// ---------------------------------------------------------------------------------------------

/// Forward declaration: defined below alongside the rest of the Intersection-curve marching machinery.
[[nodiscard]] std::vector<Vec3> MarchIntersectionCurve(const Solid& s, const Edge& e, int n);

namespace {

/// If a cylinder face \p f is bounded by a plane cut (an `Ellipse` edge in its loop), fill \p out
/// with `zLo(u)` and `zHi(u)` and return true. The un-cut rim (base at z=0 or top at z=h) is the
/// constant bound; the ellipse edge gives the sloped one.
/// The z-plane `p[3]` (`p0 + p1 cos u + p2 sin u` in the cylinder's local frame) that an `Ellipse`
/// edge \p e lies on. Returns false if the plane is parallel to the axis (not an ellipse).
[[nodiscard]] bool EllipsePlaneCoeffs(const Surface& sf, const Edge& e, double p[3]) {
  const Vec3 nlw = e.frame.zAxis;
  const Vec3 nl{ray3d::Dot(nlw, sf.frame.xAxis), ray3d::Dot(nlw, sf.frame.yAxis),
                ray3d::Dot(nlw, sf.frame.zAxis)};
  if (!(std::fabs(nl.z) > 1e-9))
    return false;
  const Vec3 cl = ucs::WorldToUcs(sf.frame, e.frame.origin);
  const double d = nl.x * cl.x + nl.y * cl.y + nl.z * cl.z;
  p[0] = d / nl.z;
  p[1] = -sf.radius * nl.x / nl.z;
  p[2] = -sf.radius * nl.y / nl.z;
  return true;
}
} // namespace

[[nodiscard]] bool CylinderCutZExtent(const Solid& s, const Face& f, CylinderCut* out) {
  const Surface& sf = f.surface;
  const double h = sf.height;
  const double zeps = 1e-7 * (std::fabs(h) + 1.0);
  int ellA = -1;
  int ellB = -1;
  bool baseRim = false;
  bool topRim = false;
  for (const Loop& lp : f.loops)
    for (const EdgeUse& u : lp.uses) {
      const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
      if (e.kind == CurveKind::Ellipse) {
        if (ellA < 0)
          ellA = u.edge;
        else
          ellB = u.edge;
      } else if (e.kind == CurveKind::Arc) {
        const double z0 = ucs::WorldToUcs(sf.frame, s.vertices[static_cast<std::size_t>(e.v0)].p).z;
        const double z1 = ucs::WorldToUcs(sf.frame, s.vertices[static_cast<std::size_t>(e.v1)].p).z;
        if (std::fabs(z0) < zeps && std::fabs(z1) < zeps)
          baseRim = true;
        if (std::fabs(z0 - h) < zeps && std::fabs(z1 - h) < zeps)
          topRim = true;
      }
    }
  if (ellB >= 0) {
    // Two ellipse edges — a segment between two plane cuts (a tilted plug). Lower plane = smaller p0.
    double pa[3];
    double pb[3];
    if (!EllipsePlaneCoeffs(sf, s.edges[static_cast<std::size_t>(ellA)], pa) ||
        !EllipsePlaneCoeffs(sf, s.edges[static_cast<std::size_t>(ellB)], pb))
      return false;
    // Order by the constant term (the axis-intercept height). When the two cut planes share it —
    // both pass through the axis, as the two ellipses of a Steinmetz bicylinder do — fall back to
    // comparing the actual height at the middle of the face's u-span.
    double ka = pa[0];
    double kb = pb[0];
    if (std::fabs(pa[0] - pb[0]) <= 1e-9 * (std::fabs(pa[0]) + std::fabs(pb[0]) + 1.0)) {
      const double um = 0.5 * (f.uStart + f.uEnd);
      ka = pa[0] + pa[1] * std::cos(um) + pa[2] * std::sin(um);
      kb = pb[0] + pb[1] * std::cos(um) + pb[2] * std::sin(um);
    }
    const double* lo = ka <= kb ? pa : pb;
    const double* hi = ka <= kb ? pb : pa;
    for (int i = 0; i < 3; ++i) {
      out->lo[i] = lo[i];
      out->hi[i] = hi[i];
    }
    return true;
  }
  if (ellA < 0 || baseRim == topRim)
    return false;
  double p[3];
  if (!EllipsePlaneCoeffs(sf, s.edges[static_cast<std::size_t>(ellA)], p))
    return false;
  if (baseRim) {
    out->lo[0] = out->lo[1] = out->lo[2] = 0.0;
    out->hi[0] = p[0];
    out->hi[1] = p[1];
    out->hi[2] = p[2];
  } else {
    out->lo[0] = p[0];
    out->lo[1] = p[1];
    out->lo[2] = p[2];
    out->hi[0] = h;
    out->hi[1] = out->hi[2] = 0.0;
  }
  return true;
}

namespace {

// ---------------------------------------------------------------------------------------------
// The procedural intersection curve (REQ-314 B2b-2, D-2026-09-03-a). An `Intersection` edge stores
// the two surfaces it lies on and an on-curve witness (`frame.origin`); the curve itself is walked
// by marching — step along the tangent (the cross product of the two surface normals), then correct
// straight back onto both surfaces. Everything downstream (tessellation, bounds, the nearest-point
// query, the numerical face integral) samples this walk.
// ---------------------------------------------------------------------------------------------

/// The geometric outward unit normal of \p sf at a point \p p that lies on it, in world. Ignores
/// `Surface::inward` — the marching tangent only needs the plane the surface bends in.
[[nodiscard]] Vec3 SurfaceNormalGeom(const Surface& sf, const Vec3& p) {
  const Vec3 loc = ucs::WorldToUcs(sf.frame, p);
  Vec3 n{0.0, 0.0, 1.0};
  const double rho = std::sqrt(loc.x * loc.x + loc.y * loc.y);
  switch (sf.kind) {
  case SurfaceKind::Plane:
    n = Vec3{0.0, 0.0, 1.0};
    break;
  case SurfaceKind::Cylinder:
    if (sf.radius2 > 1e-9 && std::fabs(sf.radius - sf.radius2) > 1e-8 * std::max(sf.radius, 1e-9)) {
      // Elliptical cylinder: \ref radius along frame X, \ref radius2 along frame Y. Used for a
      // Plant fillet whose cone record carries a radius ratio other than 1.
      const double a = sf.radius;
      const double b = sf.radius2;
      n = Vec3{loc.x / (a * a), loc.y / (b * b), 0.0};
    } else {
      n = rho > 1e-12 ? Vec3{loc.x / rho, loc.y / rho, 0.0} : Vec3{1.0, 0.0, 0.0};
    }
    break;
  case SurfaceKind::Sphere: {
    const double len = ray3d::Length(loc);
    n = len > 1e-12 ? ray3d::Scale(loc, 1.0 / len) : Vec3{1.0, 0.0, 0.0};
    break;
  }
  case SurfaceKind::Cone: {
    const double k = (sf.radius - sf.radius2) / sf.height;
    const Vec3 rad = rho > 1e-12 ? Vec3{loc.x / rho, loc.y / rho, 0.0} : Vec3{1.0, 0.0, 0.0};
    n = ray3d::Normalize(Vec3{rad.x, rad.y, k});
    break;
  }
  case SurfaceKind::Torus: {
    const Vec3 ring = rho > 1e-12 ? Vec3{loc.x * sf.radius / rho, loc.y * sf.radius / rho, 0.0}
                                  : Vec3{sf.radius, 0.0, 0.0};
    n = ray3d::Normalize(ray3d::Sub(loc, ring));
    break;
  }
  case SurfaceKind::Nurbs:
    // Only ever asked of the two surfaces an Intersection edge lies on, and no Intersection edge
    // lies on a NURBS patch (loft / sweep produce no procedural edges). Unreachable; the frame Z is
    // a harmless placeholder rather than a NaN.
    break;
  }
  return ucs::UcsVectorToWorld(sf.frame, n);
}
} // namespace

/// Signed distance from \p p to \p sf: positive on the outward-normal side, negative on the other.
[[nodiscard]] double SignedDistToSurface(const Surface& sf, const Vec3& p) {
  const Vec3 q = ClosestPointOnSurface(sf, p);
  const double d = ray3d::Length(ray3d::Sub(p, q));
  return ray3d::Dot(ray3d::Sub(p, q), SurfaceNormalGeom(sf, q)) >= 0.0 ? d : -d;
}

namespace {

/// The two surfaces are the same analytic surface (up to rounding). For a cylinder / cone the frame
/// origin may sit anywhere along the axis — only the perpendicular offset has to vanish.
[[nodiscard]] bool SameSurfaceApprox(const Surface& a, const Surface& b) {
  if (a.kind != b.kind)
    return false;
  const double sc = 1.0 + std::fabs(a.radius) + std::fabs(b.radius);
  if (std::fabs(a.radius - b.radius) > 1e-6 * sc ||
      std::fabs(std::fabs(ray3d::Dot(a.frame.zAxis, b.frame.zAxis)) - 1.0) > 1e-9)
    return false;
  Vec3 d = ray3d::Sub(a.frame.origin, b.frame.origin);
  if (a.kind == SurfaceKind::Cylinder || a.kind == SurfaceKind::Cone)
    d = ray3d::Sub(d, ray3d::Scale(a.frame.zAxis, ray3d::Dot(d, a.frame.zAxis)));
  return ray3d::Length(d) < 1e-6 * sc;
}

/// Pull \p p onto the curve where \p a and \p b cross: alternately project onto each surface, which
/// converges to a point on both when the surfaces are not near-tangent (the case B2b-2 handles).
[[nodiscard]] Vec3 SettleOntoIntersection(const Surface& a, const Surface& b, Vec3 p) {
  for (int i = 0; i < 24; ++i) {
    const Vec3 pa = ClosestPointOnSurface(a, p);
    const Vec3 q = ClosestPointOnSurface(b, pa);
    if (ray3d::Length(ray3d::Sub(q, p)) <= 1e-13 * (1.0 + ray3d::Length(q)))
      return q;
    p = q;
  }
  return p;
}
} // namespace

/// March the intersection curve of edge \p e from `v0` to `v1` into a dense world polyline (the way
/// the witness in `e.frame.origin` says to go round). \p n is the number of chords.
[[nodiscard]] std::vector<Vec3> MarchIntersectionCurve(const Solid& s, const Edge& e, int n) {
  std::vector<Vec3> pts;
  if (e.isectSurfaces.size() != 2)
    return pts;
  const Surface& sa = e.isectSurfaces[0];
  const Surface& sb = e.isectSurfaces[1];
  const Vec3 start = s.vertices[static_cast<std::size_t>(e.v0)].p;
  const Vec3 end = s.vertices[static_cast<std::size_t>(e.v1)].p;
  const Vec3 witness = e.frame.origin;
  n = std::clamp(n, 4, 4096);
  pts.reserve(static_cast<std::size_t>(n) + 1);
  pts.push_back(start);

  // A closed fillet stores one edge whose start and end are the same vertex. The open-curve stop
  // at v1 would halt on the first step; leave that point and walk until the loop returns.
  const double closeEps = 1e-8 * (1.0 + ray3d::Length(start));
  const bool closed = ray3d::Length(ray3d::Sub(end, start)) <= closeEps;
  double rough = ray3d::Length(ray3d::Sub(witness, start)) + ray3d::Length(ray3d::Sub(end, witness));
  if (closed) {
    const double radius = ray3d::Length(ray3d::Sub(witness, start));
    rough = std::max(rough, 2.0 * 3.14159265358979323846 * std::max(radius, 1e-6));
  }
  const double step = std::max(rough / static_cast<double>(n), 1e-12);
  Vec3 p = start;
  Vec3 prevDir = ray3d::Normalize(ray3d::Sub(witness, start));
  const int guard = 8 * n + 64;
  bool nearEndArmed = false;
  bool leftHome = false;
  for (int it = 0; it < guard; ++it) {
    Vec3 tangent = ray3d::Cross(SurfaceNormalGeom(sa, p), SurfaceNormalGeom(sb, p));
    const double tl = ray3d::Length(tangent);
    if (!(tl > 1e-14))
      break;
    tangent = ray3d::Scale(tangent, 1.0 / tl);
    if (ray3d::Dot(tangent, prevDir) < 0.0)
      tangent = ray3d::Scale(tangent, -1.0);
    Vec3 next = SettleOntoIntersection(sa, sb, ray3d::Add(p, ray3d::Scale(tangent, step)));
    const Vec3 realDir = ray3d::Sub(next, p);
    const double moved = ray3d::Length(realDir);
    if (!(moved > 1e-15))
      break;
    prevDir = ray3d::Scale(realDir, 1.0 / moved);
    if (closed) {
      const double distHome = ray3d::Length(ray3d::Sub(next, start));
      if (!leftHome && distHome > 4.0 * step)
        leftHome = true;
      pts.push_back(next);
      p = next;
      if (leftHome && distHome <= step)
        break;
      if (static_cast<int>(pts.size()) > 6 * n)
        break;
      continue;
    }
    const double distToEnd = ray3d::Length(ray3d::Sub(next, end));
    if (nearEndArmed && distToEnd >= ray3d::Length(ray3d::Sub(p, end)))
      break;
    if (distToEnd <= step)
      nearEndArmed = true;
    pts.push_back(next);
    p = next;
    if (nearEndArmed && distToEnd <= 1e-9 * (1.0 + ray3d::Length(end)))
      break;
    if (static_cast<int>(pts.size()) > 6 * n)
      break;
  }
  pts.push_back(end);
  return pts;
}

namespace {

/// Resample a marched polyline to the point at fractional arc length \p t in [0, 1].
[[nodiscard]] Vec3 PointAtArcFraction(const std::vector<Vec3>& poly, double t) {
  if (poly.empty())
    return Vec3{};
  if (poly.size() == 1 || t <= 0.0)
    return poly.front();
  if (t >= 1.0)
    return poly.back();
  double total = 0.0;
  for (std::size_t i = 1; i < poly.size(); ++i)
    total += ray3d::Length(ray3d::Sub(poly[i], poly[i - 1]));
  if (!(total > 0.0))
    return poly.front();
  double target = t * total;
  for (std::size_t i = 1; i < poly.size(); ++i) {
    const double seg = ray3d::Length(ray3d::Sub(poly[i], poly[i - 1]));
    if (target <= seg || i + 1 == poly.size()) {
      const double f = seg > 1e-15 ? target / seg : 0.0;
      return ray3d::Add(poly[i - 1], ray3d::Scale(ray3d::Sub(poly[i], poly[i - 1]), f));
    }
    target -= seg;
  }
  return poly.back();
}
} // namespace

[[nodiscard]] bool LoopHasIntersectionEdge(const Solid& s, const Loop& lp) {
  for (const EdgeUse& u : lp.uses)
    if (s.edges[static_cast<std::size_t>(u.edge)].kind == CurveKind::Intersection)
      return true;
  return false;
}

[[nodiscard]] bool FaceLoopHasIntersectionEdge(const Solid& s, const Face& f) {
  for (const Loop& lp : f.loops)
    if (LoopHasIntersectionEdge(s, lp))
      return true;
  return false;
}

/// A `SurfaceKind::Cone` face bounded by an `Ellipse` edge is a `SliceConeOblique` piece (issue #283,
/// TASK-204) — its material does not span the full `[0, height]` the way an unsliced cone face's does.
[[nodiscard]] bool FaceLoopHasEllipseEdge(const Solid& s, const Face& f) {
  for (const Loop& lp : f.loops)
    for (const EdgeUse& u : lp.uses)
      if (s.edges[static_cast<std::size_t>(u.edge)].kind == CurveKind::Ellipse)
        return true;
  return false;
}

[[nodiscard]] IsectStrip MakeIsectStrip(const Solid& s, const Face& f) {
  IsectStrip st;
  st.sf = &f.surface;
  double zMin = 1e300;
  double zMax = -1e300;
  int nIsect = 0;
  double curveZLo = 1e300;
  double curveZHi = -1e300;   // z of vertices that ARE on an Intersection edge
  double plainZLo = 1e300;
  double plainZHi = -1e300;   // z of vertices on Line / Arc edges only
  // REQ-339 (issue #504): track up to two DISTINCT "other" surfaces among this loop's Intersection
  // edges, each with its own near-boundary coordinate (in that surface's own frame, which for a
  // coaxial-stack wall/cutter pair always shares the stack's axis) — see the IsectStrip comment.
  const Surface* group1 = nullptr;
  double group1Min = 1e300, group1Max = -1e300;
  const Surface* group2 = nullptr;
  double group2Min = 1e300, group2Max = -1e300;
  for (const Loop& lp : f.loops)
    for (const EdgeUse& u : lp.uses) {
      const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
      const bool isI = e.kind == CurveKind::Intersection;
      for (const int vi : {e.v0, e.v1}) {
        const double z = ucs::WorldToUcs(f.surface.frame, s.vertices[static_cast<std::size_t>(vi)].p).z;
        zMin = std::min(zMin, z);
        zMax = std::max(zMax, z);
        if (isI) {
          curveZLo = std::min(curveZLo, z);
          curveZHi = std::max(curveZHi, z);
        } else {
          plainZLo = std::min(plainZLo, z);
          plainZHi = std::max(plainZHi, z);
        }
      }
      if (isI && e.isectSurfaces.size() == 2) {
        ++nIsect;
        const Surface* thisOther = nullptr;
        for (std::size_t k = 0; k < 2; ++k)
          if (!SameSurfaceApprox(e.isectSurfaces[k], f.surface))
            thisOther = &e.isectSurfaces[k];
        if (thisOther == nullptr)
          continue;
        st.other = thisOther;
        if (group1 == nullptr || SameSurfaceApprox(*thisOther, *group1)) {
          group1 = thisOther;
          for (const int vi : {e.v0, e.v1}) {
            const double gz = ucs::WorldToUcs(group1->frame, s.vertices[static_cast<std::size_t>(vi)].p).z;
            group1Min = std::min(group1Min, gz);
            group1Max = std::max(group1Max, gz);
          }
        } else if (group2 == nullptr || SameSurfaceApprox(*thisOther, *group2)) {
          group2 = thisOther;
          for (const int vi : {e.v0, e.v1}) {
            const double gz = ucs::WorldToUcs(group2->frame, s.vertices[static_cast<std::size_t>(vi)].p).z;
            group2Min = std::min(group2Min, gz);
            group2Max = std::max(group2Max, gz);
          }
        }
      }
    }
  // Every existing (pre-REQ-339) `IsectStrip` shape keeps its mouth curve either alone in the loop
  // (`oneSided`) or in a loop separate from the plain rim it is bitten out of (the `!inOuter` full-
  // minus-bite path in `IntegrateFace`, which calls this same function for the bite ALONE, on an
  // inner loop with no plain edges of its own). A coaxial-stack wall band's own notch (issue #504)
  // is the one case where curve and plain edges share a SINGLE loop — restricting both the shoulder-
  // crossing `other2` detection and the `hasClip` detection below to that shape keeps this addition
  // from ever firing on a pre-existing multi-loop face where the plain edges belong to the OUTER
  // loop and the curve to an unrelated INNER one.
  if (f.loops.size() == 1 && group1 != nullptr && group2 != nullptr) {
    // Two distinct "other" surfaces touch this loop: a shoulder crossing (issue #504). Whichever
    // pairing (group1's top against group2's bottom, or the reverse) actually meets — the smaller
    // gap — says which group is the low side; `clipZ` is their shared meeting point. Encounter
    // order in the loop says nothing about which side is which, so both pairings must be tried.
    const double gap12 = std::fabs(group1Max - group2Min);  // group1 low, group2 high
    const double gap21 = std::fabs(group2Max - group1Min);  // group2 low, group1 high
    if (gap12 <= gap21) {
      st.other = group1;
      st.other2 = group2;
      st.clipZ = 0.5 * (group1Max + group2Min);
    } else {
      st.other = group2;
      st.other2 = group1;
      st.clipZ = 0.5 * (group2Max + group1Min);
    }
  }
  if (f.loops.size() == 1 && !st.oneSided && st.other2 == nullptr && nIsect >= 2 &&
      plainZLo <= plainZHi) {
    // REQ-339 (issue #504): a coaxial-stack wall band's own bite against the cutter, where the
    // mouth curve's near end already lands exactly on a plain rim (the shoulder) rather than
    // closing back on itself. Left unclipped, the root search's margin can run past that rim and
    // pick up the OTHER wall's own crossing (irrelevant to this face) as a spurious second root —
    // exactly the silently-wrong-volume failure the withdrawn 2026-09-14 attempt hit. Clipping the
    // computed strip to the rim, on whichever side the curve and a plain edge already coincide,
    // is the "stop at whichever comes first" rule from that revision's own root-cause note.
    // A pre-existing lens/branch-pipe face (REQ-314 B2b-2) also mixes a curve with two plain seams
    // in one loop, but BOTH seams land on the curve's own two extremes (the mouth loop closes on
    // itself, seam-to-curve, at both ends) — that is genuinely the "curve defines the material
    // directly" shape, not a notch. Only a genuine one-sided notch (the curve meets a plain rim at
    // exactly ONE end, and is nowhere near one at the other) gets the bite treatment below.
    const double tol = 1e-6 * (1.0 + std::fabs(zMax));
    const bool hiMatches = std::fabs(plainZHi - curveZHi) < tol;
    const bool loMatches = std::fabs(plainZLo - curveZLo) < tol;
    if (hiMatches && !loMatches) {
      st.hasClip = true;
      st.clipZ = plainZHi;
      st.clipKeepBelow = true;
      st.fullLo = plainZLo;
      st.fullHi = plainZHi;
    } else if (loMatches && !hiMatches) {
      st.hasClip = true;
      st.clipZ = plainZLo;
      st.clipKeepBelow = false;
      st.fullLo = plainZLo;
      st.fullHi = plainZHi;
    }
  }
  if (zMax > zMin) {
    const double margin = 0.3 * (zMax - zMin) + 1e-6 * (1.0 + std::fabs(zMax));
    st.zSearchLo = zMin - margin;
    st.zSearchHi = zMax + margin;
  }
  if (st.hasClip) {
    // REQ-339 (issue #504): the far (clipped-away) root can sit as far as the CUTTER's own radius
    // from the CUTTER's OWN axis — nothing to do with this face's own vertex span, which the margin
    // above is sized from, and which starts (or ends) exactly AT the shoulder for a wall band (so
    // the vertex-based window can fall short of the cutter's full reach on that side, as it does
    // whenever the wall's OTHER end is not comfortably farther away than the cutter radius).
    const double cutterCenterZ = ucs::WorldToUcs(f.surface.frame, st.other->frame.origin).z;
    // A small overshoot past the cutter's own radius, so a root that happens to sit exactly at the
    // window edge (u = the wall's own longitude, i.e. the plane through the cutter axis) is safely
    // inside it rather than coincident with the first scan sample.
    const double reach = st.other->radius * 1.05 + 1e-6 * (1.0 + std::fabs(cutterCenterZ));
    st.zSearchLo = std::min(st.zSearchLo, cutterCenterZ - reach);
    st.zSearchHi = std::max(st.zSearchHi, cutterCenterZ + reach);
  }
  if (nIsect == 1 && plainZHi >= plainZLo) {
    // A stub band: one quartic edge, and a flat rim on the far side. The rim is whichever plain
    // bound is not shared with the curve's own end.
    st.oneSided = true;
    st.rimZ = std::fabs(plainZLo - curveZLo) > std::fabs(plainZHi - curveZHi) ? plainZLo : plainZHi;
  }
  return st;
}

/// `[zLo, zHi]` at longitude \p u, or false where the strip has pinched to nothing (an end of a
/// lens-shaped face). Scans for the two crossings of the other surface and bisects each.
[[nodiscard]] bool IsectStripAt(const IsectStrip& st, double u, double* zLo, double* zHi) {
  const double r = st.sf->radius;
  const Surface* activeOther = st.other;
  if (st.other2 != nullptr) {
    // REQ-339 (issue #504): this longitude's own transverse position (independent of the axial `z`
    // being searched for — a cylinder's radial position at a given `u` does not move along its own
    // axis) decides which of the two walls a cutter-lining face's mouth curve is actually bounded
    // by here: whichever side of the shoulder height `clipZ` it falls on, read through that
    // surface's own frame (both share the stack's axis by this increment's own scope).
    const Vec3 probe = ucs::UcsToWorld(st.sf->frame, Vec3{r * std::cos(u), r * std::sin(u), 0.0});
    const double side = ucs::WorldToUcs(st.other2->frame, probe).z;
    if (side >= st.clipZ)
      activeOther = st.other2;
  }
  auto g = [&](double z) {
    return SignedDistToSurface(*activeOther,
                               ucs::UcsToWorld(st.sf->frame, Vec3{r * std::cos(u), r * std::sin(u), z}));
  };
  double first = 0.0;
  double last = 0.0;
  int roots = 0;
  const int scan = 96;
  double zp = st.zSearchLo;
  double gp = g(zp);
  for (int i = 1; i <= scan; ++i) {
    const double zc = st.zSearchLo + (st.zSearchHi - st.zSearchLo) * i / scan;
    const double gc = g(zc);
    if ((gp <= 0.0) != (gc <= 0.0)) {
      double lo = zp;
      double hi = zc;
      double glo = gp;
      for (int k = 0; k < 46; ++k) {
        const double m = 0.5 * (lo + hi);
        const double gm = g(m);
        if ((glo <= 0.0) != (gm <= 0.0))
          hi = m;
        else {
          lo = m;
          glo = gm;
        }
      }
      const double root = 0.5 * (lo + hi);
      if (roots == 0)
        first = root;
      last = root;
      ++roots;
    }
    zp = zc;
    gp = gc;
  }
  if (st.oneSided) {
    // one quartic edge and a flat rim: the strip runs from the rim to the crossing nearest it.
    if (roots == 0)
      return false;
    const double cross = std::fabs(first - st.rimZ) <= std::fabs(last - st.rimZ) ? first : last;
    *zLo = std::min(cross, st.rimZ);
    *zHi = std::max(cross, st.rimZ);
  } else {
    if (roots < 2)
      return false;
    *zLo = first;
    *zHi = last;
  }
  if (st.hasClip) {
    // Stop the strip at whichever comes first, the cutter crossing or the shoulder plane (REQ-339,
    // issue #504) — the far root can legitimately fall beyond the shoulder (it is the OTHER wall's
    // own crossing, irrelevant to this face), so clip rather than trust it.
    if (st.clipKeepBelow)
      *zHi = std::min(*zHi, st.clipZ);
    else
      *zLo = std::max(*zLo, st.clipZ);
  }
  return *zHi > *zLo;
}

[[nodiscard]] ConeCutStrip MakeConeCutStrip(const Solid& s, const Face& f) {
  ConeCutStrip st;
  auto takePlane = [&](const Vec3& nWorld, const Vec3& originWorld) {
    if (st.cutCount >= ConeCutStrip::kMaxCuts)
      return;
    const int i = st.cutCount++;
    st.nx[i] = ray3d::Dot(nWorld, f.surface.frame.xAxis);
    st.ny[i] = ray3d::Dot(nWorld, f.surface.frame.yAxis);
    st.nz[i] = ray3d::Dot(nWorld, f.surface.frame.zAxis);
    st.planeC[i] = ray3d::Dot(nWorld, ray3d::Sub(originWorld, f.surface.frame.origin));
  };
  for (const Loop& lp : f.loops)
    for (const EdgeUse& u : lp.uses) {
      const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
      if (e.kind == CurveKind::Arc) {
        st.rimZ = ucs::WorldToUcs(f.surface.frame, s.vertices[static_cast<std::size_t>(e.v0)].p).z;
        st.haveRim = true;
      } else if (e.kind == CurveKind::Ellipse) {
        takePlane(e.frame.zAxis, e.frame.origin);
      } else if (e.kind == CurveKind::Intersection) {
        for (const Surface& sf : e.isectSurfaces)
          if (sf.kind == SurfaceKind::Plane)
            takePlane(sf.frame.zAxis, sf.frame.origin);
      }
    }
  if (st.cutCount >= 2 || (st.haveRim && st.cutCount >= 1))
    st.sf = &f.surface;
  return st;
}

namespace {

[[nodiscard]] bool ConePlaneCutHeightAt(const Surface& sf, double nx, double ny, double nz, double planeC,
                                        double u, double* zCut) {
  const double r0 = sf.radius;
  const double r1 = sf.radius2;
  const double h = sf.height;
  const double kk = (r1 - r0) / h;
  const double a = nx * std::cos(u) + ny * std::sin(u);
  const double denom = kk * a + nz;
  if (!(std::fabs(denom) > 1e-12))
    return false;
  *zCut = (planeC - r0 * a) / denom;
  return std::isfinite(*zCut);
}
} // namespace

/// The exact cone-generator/plane crossing height(s) at azimuth `u`, and the `[zLo, zHi]` band the
/// face occupies (rim-to-cut, or between two plane cuts).
[[nodiscard]] bool ConeCutStripAt(const ConeCutStrip& st, double u, double* zLo, double* zHi) {
  const double h = st.sf->height;
  if (st.cutCount >= 2) {
    double zA = 0.0;
    double zB = 0.0;
    if (!ConePlaneCutHeightAt(*st.sf, st.nx[0], st.ny[0], st.nz[0], st.planeC[0], u, &zA) ||
        !ConePlaneCutHeightAt(*st.sf, st.nx[1], st.ny[1], st.nz[1], st.planeC[1], u, &zB))
      return false;
    *zLo = std::min(zA, zB);
    *zHi = std::max(zA, zB);
    *zLo = std::max(*zLo, 0.0);
    *zHi = std::min(*zHi, h);
    return *zHi > *zLo + 1e-12 * (std::fabs(*zHi) + std::fabs(*zLo) + h);
  }
  if (st.cutCount != 1)
    return false;
  double zCut = 0.0;
  if (!ConePlaneCutHeightAt(*st.sf, st.nx[0], st.ny[0], st.nz[0], st.planeC[0], u, &zCut))
    return false;
  if (st.haveRim) {
    *zLo = std::min(st.rimZ, zCut);
    *zHi = std::max(st.rimZ, zCut);
  } else {
    *zLo = std::min(0.0, zCut);
    *zHi = std::max(0.0, zCut);
  }
  return true;
}

[[nodiscard]] SphereIsectStrip MakeSphereIsectStrip(const Solid& s, const Face& f) {
  SphereIsectStrip st;
  st.sf = &f.surface;
  double vMin = 1e300;
  double vMax = -1e300;
  for (const Loop& lp : f.loops)
    for (const EdgeUse& u : lp.uses) {
      const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
      if (e.kind != CurveKind::Intersection || e.isectSurfaces.size() != 2)
        continue;
      for (const int vi : {e.v0, e.v1}) {
        const Vec3 loc = ucs::WorldToUcs(f.surface.frame, s.vertices[static_cast<std::size_t>(vi)].p);
        const double v = std::asin(std::clamp(loc.z / f.surface.radius, -1.0, 1.0));
        vMin = std::min(vMin, v);
        vMax = std::max(vMax, v);
      }
      for (std::size_t k = 0; k < 2; ++k)
        if (!SameSurfaceApprox(e.isectSurfaces[k], f.surface))
          st.other = &e.isectSurfaces[k];
    }
  if (vMax > vMin) {
    const double margin = 0.35 * (vMax - vMin) + 1e-6;
    // A face whose own latitude span reaches a pole is a polar cap (issue #242, the d < r sub-case):
    // the bite runs right off that end, so the scan has to include the pole itself.
    st.vSearchLo = f.vStart <= -kHalfPi + 1e-9 ? -kHalfPi : std::max(-kHalfPi, vMin - margin);
    st.vSearchHi = f.vEnd >= kHalfPi - 1e-9 ? kHalfPi : std::min(kHalfPi, vMax + margin);
  }
  return st;
}

/// Every latitude interval `[vLo, vHi]` at longitude \p u where the meridian lies inside the other
/// surface — the multi-bite generalisation of \ref SphereStripAt. A hemisphere face carrying the
/// sphere ∩ cylinder offset quartic (issue #242, SUBTRACT / UNION) has *two* lens bites, one near
/// each pole; the INTERSECT plug's lens face has one. Intervals come back in increasing latitude.
void SphereStripsAt(const SphereIsectStrip& st, double u,
                    std::vector<std::pair<double, double>>* out) {
  out->clear();
  const double R = st.sf->radius;
  auto g = [&](double v) {
    return SignedDistToSurface(
        *st.other, ucs::UcsToWorld(st.sf->frame, Vec3{R * std::cos(v) * std::cos(u),
                                                      R * std::cos(v) * std::sin(u), R * std::sin(v)}));
  };
  const int scan = 96;
  double vp = st.vSearchLo;
  double gp = g(vp);
  bool inside = gp <= 0.0;
  double enter = inside ? vp : 0.0;
  for (int i = 1; i <= scan; ++i) {
    const double vc = st.vSearchLo + (st.vSearchHi - st.vSearchLo) * i / scan;
    const double gc = g(vc);
    if ((gp <= 0.0) != (gc <= 0.0)) {
      double lo = vp;
      double hi = vc;
      double glo = gp;
      for (int k = 0; k < 46; ++k) {
        const double m = 0.5 * (lo + hi);
        const double gm = g(m);
        if ((glo <= 0.0) != (gm <= 0.0))
          hi = m;
        else {
          lo = m;
          glo = gm;
        }
      }
      const double root = 0.5 * (lo + hi);
      if (!inside) {
        enter = root;
        inside = true;
      } else {
        out->emplace_back(enter, root);
        inside = false;
      }
    }
    vp = vc;
    gp = gc;
  }
  if (inside)
    out->emplace_back(enter, st.vSearchHi);
}

/// Largest vertex-to-vertex extent, used to scale the degeneracy tolerances so a 1 ft solid and a
/// 1000 ft solid are judged on the same relative terms.
[[nodiscard]] double ModelScale(const Solid& s) {
  if (s.vertices.empty())
    return 1.0;
  Vec3 mn = s.vertices[0].p;
  Vec3 mx = mn;
  for (const Vertex& v : s.vertices) {
    mn.x = std::min(mn.x, v.p.x);
    mn.y = std::min(mn.y, v.p.y);
    mn.z = std::min(mn.z, v.p.z);
    mx.x = std::max(mx.x, v.p.x);
    mx.y = std::max(mx.y, v.p.y);
    mx.z = std::max(mx.z, v.p.z);
  }
  const double ext = std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
  return std::max(ext, 1e-9);
}

[[nodiscard]] Vec3 ConicalPoint(const Surface& sf, double r0, double r1, double t, double z) {
  const double rho = r0 + (r1 - r0) * (z / sf.height);
  return ucs::UcsToWorld(sf.frame, Vec3{rho * std::cos(t), rho * std::sin(t), z});
}

[[nodiscard]] Vec3 CylinderPoint(const Surface& sf, double u, double v) {
  const bool ellipse =
      sf.radius2 > 1e-9 && std::fabs(sf.radius - sf.radius2) > 1e-8 * std::max(sf.radius, 1e-9);
  if (!ellipse)
    return ConicalPoint(sf, sf.radius, sf.radius, u, v);
  return ucs::UcsToWorld(sf.frame, Vec3{sf.radius * std::cos(u), sf.radius2 * std::sin(u), v});
}

[[nodiscard]] Vec3 SphericalPoint(const Surface& sf, double t, double v) {
  const double cv = std::cos(v);
  return ucs::UcsToWorld(sf.frame,
                         Vec3{sf.radius * cv * std::cos(t), sf.radius * cv * std::sin(t),
                              sf.radius * std::sin(v)});
}

[[nodiscard]] Vec3 ToroidalPoint(const Surface& sf, double t, double v) {
  const double rho = sf.radius + sf.radius2 * std::cos(v);
  return ucs::UcsToWorld(sf.frame, Vec3{rho * std::cos(t), rho * std::sin(t), sf.radius2 * std::sin(v)});
}

/// Point and parametric partial derivatives of \p sf at `(u, v)`, all in the SURFACE'S OWN local
/// frame — the frame-relative analogue of `SurfacePointAt`/`EvaluateWithDerivs`, needed only by
/// \ref IntegrateGeneralLoopFace below so a general trim loop can be integrated identically for
/// every `SurfaceKind` via `Su x Sv` (issue #307). Verified by hand to reduce to the exact outward
/// normal each closed-form `*FaceIntegrals` function above was derived against, for every curved
/// kind (Cylinder/Cone/Sphere/Torus) — matching signs and magnitudes to those functions is what lets
/// a general loop that happens to trace a rectangle reproduce the closed-form answer exactly.
void LocalSurfaceDerivs(const Surface& sf, double u, double v, Vec3* p, Vec3* su, Vec3* sv) {
  switch (sf.kind) {
  case SurfaceKind::Plane:
    *p = Vec3{u, v, 0.0};
    *su = Vec3{1.0, 0.0, 0.0};
    *sv = Vec3{0.0, 1.0, 0.0};
    return;
  case SurfaceKind::Cylinder: {
    const double a = sf.radius;
    const bool ellipse =
        sf.radius2 > 1e-9 && std::fabs(sf.radius - sf.radius2) > 1e-8 * std::max(sf.radius, 1e-9);
    const double b = ellipse ? sf.radius2 : a;
    *p = Vec3{a * std::cos(u), b * std::sin(u), v};
    *su = Vec3{-a * std::sin(u), b * std::cos(u), 0.0};
    *sv = Vec3{0.0, 0.0, 1.0};
    return;
  }
  case SurfaceKind::Cone: {
    const double r0 = sf.radius;
    const double r1 = sf.radius2;
    const double drho = (r1 - r0) / sf.height;
    const double rho = r0 + drho * v;
    *p = Vec3{rho * std::cos(u), rho * std::sin(u), v};
    *su = Vec3{-rho * std::sin(u), rho * std::cos(u), 0.0};
    *sv = Vec3{drho * std::cos(u), drho * std::sin(u), 1.0};
    return;
  }
  case SurfaceKind::Sphere: {
    const double r = sf.radius;
    const double cv = std::cos(v);
    const double sv2 = std::sin(v);
    *p = Vec3{r * cv * std::cos(u), r * cv * std::sin(u), r * sv2};
    *su = Vec3{-r * cv * std::sin(u), r * cv * std::cos(u), 0.0};
    *sv = Vec3{-r * sv2 * std::cos(u), -r * sv2 * std::sin(u), r * cv};
    return;
  }
  case SurfaceKind::Torus: {
    const double major = sf.radius;
    const double minor = sf.radius2;
    const double cv = std::cos(v);
    const double sv2 = std::sin(v);
    const double rho = major + minor * cv;
    *p = Vec3{rho * std::cos(u), rho * std::sin(u), minor * sv2};
    *su = Vec3{-rho * std::sin(u), rho * std::cos(u), 0.0};
    *sv = Vec3{-minor * sv2 * std::cos(u), -minor * sv2 * std::sin(u), minor * cv};
    return;
  }
  case SurfaceKind::Nurbs: {
    const nurbs::SurfacePoint spt = nurbs::EvaluateWithDerivs(sf.patch, u, v);
    *p = ucs::WorldToUcs(sf.frame, spt.p);
    *su = ucs::WorldVectorToUcs(sf.frame, spt.du);
    *sv = ucs::WorldVectorToUcs(sf.frame, spt.dv);
    return;
  }
  }
}

/// The `(u, v)` crossings of a horizontal line at parameter `v` against every edge of every loop in
/// \p paramLoops, sorted ascending. Consecutive pairs `[xs[2k], xs[2k+1]]` are the inside intervals
/// at that `v` under the even-odd rule — correct for one outer loop plus disjoint, properly nested
/// holes, exactly the shape `Validate` (issue #306) already requires of `paramLoops`.
[[nodiscard]] std::vector<double> GeneralLoopScanline(
    const std::vector<std::vector<curveisect::Vec2>>& paramLoops, double v) {
  std::vector<double> xs;
  for (const std::vector<curveisect::Vec2>& poly : paramLoops) {
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
      const curveisect::Vec2& a = poly[i];
      const curveisect::Vec2& b = poly[(i + 1) % n];
      if ((a.y <= v) != (b.y <= v)) {
        const double t = (v - a.y) / (b.y - a.y);
        xs.push_back(a.x + t * (b.x - a.x));
      }
    }
  }
  std::sort(xs.begin(), xs.end());
  return xs;
}

// ---------------------------------------------------------------------------------------------
// Names.
// ---------------------------------------------------------------------------------------------

const char* PrimitiveKindName(PrimitiveKind k) {
  switch (k) {
  case PrimitiveKind::None: return "Solid";
  case PrimitiveKind::Box: return "Box";
  case PrimitiveKind::Wedge: return "Wedge";
  case PrimitiveKind::Pyramid: return "Pyramid";
  case PrimitiveKind::Cylinder: return "Cylinder";
  case PrimitiveKind::Cone: return "Cone";
  case PrimitiveKind::Sphere: return "Sphere";
  case PrimitiveKind::Torus: return "Torus";
  case PrimitiveKind::Polysolid: return "Polysolid";
  }
  return "Solid";
}

const char* ProblemText(Problem p) {
  switch (p) {
  case Problem::Ok: return "OK";
  case Problem::NonFiniteParameter: return "A dimension is not a finite number.";
  case Problem::NonPositiveLength: return "Length must be greater than zero.";
  case Problem::NonPositiveWidth: return "Width must be greater than zero.";
  case Problem::NonPositiveHeight: return "Height must be greater than zero.";
  case Problem::NonPositiveRadius: return "Radius must be greater than zero.";
  case Problem::NegativeTopRadius: return "Top radius cannot be negative.";
  case Problem::TopRadiusNotBelowBase: return "Top radius must be smaller than the base radius.";
  case Problem::MinorRadiusEqualsMajor:
    return "Tube radius cannot exactly equal the torus radius — the inner edge would collapse to a point.";
  case Problem::SideCountOutOfRange:
    static_assert(kMaxPyramidSides == 64, "the sentence below names this limit");
    return "A pyramid needs between 3 and 64 sides.";
  case Problem::DegenerateFrame: return "The placement frame is not a valid right-handed coordinate system.";
  case Problem::NoShell: return "The solid has no shell.";
  case Problem::EmptyShell: return "The solid has a shell with no faces.";
  case Problem::IndexOutOfRange: return "The solid's topology refers to a face, edge or vertex that does not exist.";
  case Problem::LoopNotClosed: return "A face boundary does not close.";
  case Problem::EmptyLoop: return "A face boundary has no edges.";
  case Problem::EdgeNotUsedTwice: return "The surface is not closed: an edge does not bound exactly two faces.";
  case Problem::ShellOpenAtEdge: return "The surface is not closed: an edge bounds only one face.";
  case Problem::EdgeNonManifold: return "The surface is not manifold: an edge bounds more than two faces.";
  case Problem::SliceCutCrossesCurvedEnd:
    return "A tilted cut that crosses the end of a cylinder or cone is not supported yet.";
  case Problem::SliceCutConeOffAxis:
    return "A cut parallel to a cone's axis but not through it is a hyperbola, which is not supported yet.";
  case Problem::SliceCutCrossesCurvedFace:
    return "The cut crosses a curved face of this solid, which is not supported yet.";
  case Problem::SliceCutTorusCurve:
    return "A cut of a torus that is not square to its axis is a curve this release cannot hold.";
  case Problem::SliceCutTooSteepForCone:
    return "A cut steeper than a cone's own side is not supported yet.";
  case Problem::SliceCutSeveralOutlines:
    return "The cut surface would have more than one outline (a hole, or separate islands), which "
           "this release cannot represent.";
  case Problem::SliceResultInvalid:
    return "The cut pieces did not pass validation, so nothing was cut.";
  case Problem::SectionEllipse:
    return "This cut is an ellipse, which a section outline cannot hold yet.";
  case Problem::SectionCurve:
    return "This cut is a curve that is not a line or a circular arc, which a section outline cannot hold yet.";
  case Problem::SectionHasHole:
    return "This section has a hole in it, which a section outline cannot hold yet.";
  case Problem::EdgeOrientationInconsistent: return "Two faces disagree about which way an edge runs.";
  case Problem::FaceHasNoLoop: return "A face has no boundary.";
  case Problem::DegenerateFace: return "A face has no area.";
  case Problem::DegenerateEdge: return "An edge has no length.";
  case Problem::NonFiniteCoordinate: return "A coordinate is not a finite number.";
  case Problem::NotClosed: return "The surface does not enclose a volume.";
  case Problem::UnusedVertex: return "A vertex is not used by any edge.";
  case Problem::GeneralLoopCountMismatch:
    return "A face's general trim loops do not match its 3D loops.";
  case Problem::GeneralLoopOpen: return "A general trim loop has fewer than 3 points.";
  case Problem::GeneralLoopSelfIntersects: return "A general trim loop crosses itself.";
  case Problem::GeneralLoopWrongWinding: return "A general trim loop winds the wrong way.";
  case Problem::GeneralLoopHoleNotNested:
    return "A general trim loop's hole is not inside its outer boundary.";
  case Problem::PathTooShort:
    return "A polysolid needs at least two points, and a closed one at least two segments.";
  case Problem::PathSegmentDegenerate:
    return "A polysolid path has a repeated point, an arc that is not one, or a closed path that "
           "does not return to its start.";
  case Problem::PolysolidCornerCollapsed:
    return "A corner is too sharp, or a segment too short, for a wall of that width to turn it.";
  case Problem::PolysolidCurveTooTight:
    return "A curve is too tight for a wall of that width: its inner face would turn inside out.";
  case Problem::PolysolidPathSelfIntersects:
    return "The path crosses itself, so the wall would enclose the same ground twice.";
  case Problem::PlaneFaceNotSimple:
    return "A flat face has holes or a non-convex boundary, which this build cannot tessellate.";
  case Problem::NonPositiveTolerance: return "Tessellation tolerance must be greater than zero.";
  case Problem::NonPositiveDistance: return "Extrusion distance must be a non-zero finite number.";
  case Problem::ProfileMalformed: return "The profile's vertex and edge counts do not match.";
  case Problem::ProfileTooFewEdges: return "A profile needs at least two edges to enclose an area.";
  case Problem::ProfilePointOffPlane: return "A profile point does not lie on the profile plane.";
  case Problem::ProfileArcRadiusMismatch:
    return "A profile arc's endpoints are not the same distance from its centre.";
  case Problem::ProfileSelfIntersects: return "The profile crosses itself.";
  case Problem::ProfileArcReflex:
    return "A profile arc curves inward; LOFT and SWEEP build outward-curving arcs only.";
  case Problem::NonPositiveAngle: return "The revolve angle must be non-zero and no more than a full turn.";
  case Problem::RevolveAxisDegenerate: return "The revolve axis direction is zero or not a finite number.";
  case Problem::RevolveAxisNotInPlane: return "The revolve axis must lie in the profile's plane.";
  case Problem::RevolveProfileCrossesAxis: return "The profile crosses the revolve axis.";
  case Problem::RevolveProfileMissesAxis:
    return "The profile must touch the revolve axis along one edge or at one point; a hollow revolve is a SUBTRACT.";
  case Problem::RevolveArcInProfile:
    return "This release revolves straight-edged profiles only (an arc would sweep a sphere or torus portion).";
  case Problem::SliceDegeneratePlane: return "The slicing plane's normal is zero or not a finite number.";
  case Problem::SlicePlaneMissesSolid: return "The slicing plane does not pass through the solid.";
  case Problem::SliceCurvedFace:
    return "This release slices solids with flat faces only (a box or a straight extrusion).";
  case Problem::SliceResultComplex:
    return "The cut would split the solid into disjoint pieces, which this release cannot represent.";
  case Problem::BooleanCurvedFace:
    return "This release cannot combine these curved solids (a curved subtraction, a cone / sphere / "
           "torus, or a cylinder that only partly enters the other solid).";
  case Problem::BooleanNonConvex:
    return "This release combines convex solids only.";
  case Problem::BooleanObliqueCylinder:
    return "The cylinder is set at an angle to the other solid's faces; they would meet along an "
           "ellipse, which needs the general Boolean (a later release).";
  case Problem::BooleanEmptyResult: return "The solids do not overlap, so there is nothing to keep.";
  case Problem::BooleanResultInvalid:
    return "The combined solid did not pass validation and was not stored.";
  case Problem::LoftNeedsTwoProfiles: return "A loft needs at least two profiles to skin between.";
  case Problem::LoftProfileMismatch:
    return "The loft profiles do not match edge-for-edge (different edge counts, or a straight edge "
           "paired with an arc).";
  case Problem::SweepPathDegenerate: return "The sweep path has no length.";
  case Problem::SweepProfileTouchesAxis:
    return "The profile reaches the arc path's axis of curvature; move it clear of the axis.";
  case Problem::SweepPathCorner:
    return "The sweep path has a sharp corner touching an arc segment; only a straight-to-straight "
           "corner can be mitred in this version.";
  case Problem::SweepMitreProfileArc:
    return "This corner would mitre, but the profile has an arc edge; a polygonal profile is needed "
           "to mitre a sharp corner in this version.";
  case Problem::SweepMitreCollapsed:
    return "This corner is too sharp to mitre; the two segments fold back on themselves.";
  case Problem::SweepUnsupportedOption:
    return "A twist cannot be applied to a closed sweep path; the seam ring cannot carry two "
           "different orientations at once.";
  case Problem::SweepTwistNeedsStraightPath:
    return "A twist cannot be combined with an arc path segment in this version; twist works on a "
           "straight (or multi-segment straight) path.";
  case Problem::PushPullFaceKindUnsupported:
    return "This kind of face cannot be pushed: a flat face moves, and a cylinder wall changes "
           "radius, but a cone, sphere or torus wall has no single radius to change.";
  case Problem::PushPullDistanceZero:
    return "Push/pull needs a distance that is not zero.";
  case Problem::PushPullNeighbourCurved:
    return "This face cannot be pushed: a curved face meets it, and a curved wall has to be rebuilt "
           "rather than stretched.";
  case Problem::PushPullVertexUnsolvable:
    return "This face cannot be pushed: one of its corners would have to split, because more than "
           "three faces meet there.";
  case Problem::PushPullCurvedDegenerate:
    return "That would flatten the curved wall beside this face, or push it through its own apex.";
  case Problem::PushPullResultInvalid:
    return "That push would turn the solid inside out or flatten it, so it was not applied.";
  case Problem::FilletRadiusNotPositive:
    return "Fillet radius must be greater than zero.";
  case Problem::FilletEdgeNotLine:
    return "Only a straight edge can be filleted; this one is curved.";
  case Problem::FilletFaceNotPlanar:
    return "One of the faces beside this edge is curved, not flat.";
  case Problem::FilletFacesParallel:
    return "The two faces beside this edge are parallel, so there is no corner to round.";
  case Problem::FilletEdgeConcave:
    return "This edge is concave, not convex — rounding it would add material, which this fillet "
           "does not yet do.";
  case Problem::FilletRadiusTooLarge:
    return "That radius is too large for this edge: it would reach past the far side of an "
           "adjacent face.";
  case Problem::FilletRadiusOverlapsAnother:
    return "That radius is too large for this selection: this fillet and the one on another "
           "selected edge would run into each other.";
  case Problem::FilletEdgeTooShortForItsCorners:
    return "This edge is too short to round at both of its corners — the two fillets would meet "
           "before either finished.";
  case Problem::FilletEndFaceUnsupported:
    return "A face at one end of this edge is curved or not square to it, so the fillet cannot "
           "close there.";
  case Problem::FilletVertexNotSimple:
    return "More than three edges meet at one end of this edge, so it cannot be filleted alone.";
  case Problem::FilletCornerPartial:
    return "Some of the edges that share a corner are selected and some are not; select all three, "
           "or none of them.";
  case Problem::FilletCornerNotOrthogonal:
    return "The three faces at that corner are not square to each other, which this fillet does not "
           "yet round.";
  case Problem::FilletResultInvalid:
    return "That fillet would leave the solid invalid, so it was not applied.";
  case Problem::ChamferDistanceNotPositive:
    return "Chamfer distance must be greater than zero.";
  case Problem::ChamferEdgeNotLine:
    return "Only a straight edge can be chamfered; this one is curved.";
  case Problem::ChamferFaceNotPlanar:
    return "One of the faces beside this edge is curved, not flat.";
  case Problem::ChamferFacesParallel:
    return "The two faces beside this edge are parallel, so there is no corner to bevel.";
  case Problem::ChamferEdgeConcave:
    return "This edge is concave, not convex — bevelling it would add material, which this chamfer "
           "does not yet do.";
  case Problem::ChamferDistanceTooLarge:
    return "That distance is too large for this edge: the bevel would reach past the far side of "
           "an adjacent face.";
  case Problem::ChamferDistanceOverlapsAnother:
    return "That distance is too large for this selection: this chamfer and the one on another "
           "selected edge would run into each other.";
  case Problem::ChamferEdgeTooShortForItsCorners:
    return "This edge is too short to bevel at both of its corners — the two chamfers would meet "
           "before either finished.";
  case Problem::ChamferEndFaceUnsupported:
    return "A face at one end of this edge is curved or not square to it, so the bevel cannot close "
           "there.";
  case Problem::ChamferVertexNotSimple:
    return "More than three edges meet at one end of this edge, so it cannot be chamfered alone.";
  case Problem::ChamferCornerPartial:
    return "Some of the edges that share a corner are selected and some are not; select all three, "
           "or none of them.";
  case Problem::ChamferCornerNotOrthogonal:
    return "The three faces at that corner are not square to each other, which this chamfer does "
           "not yet bevel.";
  case Problem::ChamferResultInvalid:
    return "That chamfer would leave the solid invalid, so it was not applied.";
  case Problem::RotateAxisNotUnit:
    return "The rotation axis has no direction, so there is nothing to turn the solid about.";
  case Problem::RotateResultInvalid:
    return "That rotation would leave the solid invalid, so it was not applied.";
  case Problem::ScaleFactorNonPositive:
    return "A scale factor must be greater than zero.";
  case Problem::ScaleResultInvalid:
    return "That scale would leave the solid invalid, so it was not applied.";
  case Problem::MirrorPlaneNotUnit:
    return "The mirror line has no direction, so there is nothing to reflect the solid across.";
  case Problem::MirrorResultInvalid:
    return "That mirror would leave the solid invalid, so it was not applied.";
  case Problem::MoveVertexNotThreePlanes:
    return "That corner is not where exactly three flat faces meet, so it cannot be dragged.";
  case Problem::MoveEdgeFacesParallel:
    return "The two faces along that edge are flat against each other, so the edge cannot be moved.";
  case Problem::MoveSubObjectNeighbourCurved:
    return "A curved face meets there, and a curved face cannot follow a dragged corner yet.";
  case Problem::MoveSubObjectNoMotion:
    return "That drag does not move the geometry anywhere.";
  case Problem::MoveSubObjectCornerUnsolvable:
    return "One of the corners this drag moves cannot be rebuilt, so it was not applied.";
  case Problem::MoveSubObjectResultInvalid:
    return "That move would leave the solid invalid, so it was not applied.";
  }
  return "The solid is not valid.";
}

// ---------------------------------------------------------------------------------------------
// Edge evaluation — the single parametrisation.
// ---------------------------------------------------------------------------------------------

/// The ellipse parameter (angle about `frame.zAxis` in the a-normalised, b-normalised space) of a
/// point that lies on the ellipse edge \p e.
[[nodiscard]] double EllipseParamOf(const Edge& e, const Vec3& p) {
  const Vec3 rel = ray3d::Sub(p, e.frame.origin);
  const double x = ray3d::Dot(rel, e.frame.xAxis) / e.radius;
  const double y = ray3d::Dot(rel, e.frame.yAxis) / e.radius2;
  return std::atan2(y, x);
}

Vec3 EdgePointAt(const Solid& s, const Edge& e, double t) {
  if (e.kind == CurveKind::Intersection) {
    if (e.isectSurfaces.size() != 2)
      return s.vertices[static_cast<std::size_t>(e.v0)].p;
    if (t <= 0.0)
      return s.vertices[static_cast<std::size_t>(e.v0)].p;
    if (t >= 1.0)
      return s.vertices[static_cast<std::size_t>(e.v1)].p;
    // The marched polyline places the point; a final settle removes the chord-interpolation error so
    // the result is exactly on both surfaces.
    const Vec3 p = PointAtArcFraction(MarchIntersectionCurve(s, e, 256), t);
    return SettleOntoIntersection(e.isectSurfaces[0], e.isectSurfaces[1], p);
  }
  if (e.kind == CurveKind::Line) {
    const Vec3& a = s.vertices[static_cast<std::size_t>(e.v0)].p;
    const Vec3& b = s.vertices[static_cast<std::size_t>(e.v1)].p;
    return ray3d::Add(a, ray3d::Scale(ray3d::Sub(b, a), t));
  }
  if (e.kind == CurveKind::Ellipse) {
    const double th = EllipseParamOf(e, s.vertices[static_cast<std::size_t>(e.v0)].p) + e.sweep * t;
    return ray3d::Add(e.frame.origin,
                      ray3d::Add(ray3d::Scale(e.frame.xAxis, e.radius * std::cos(th)),
                                 ray3d::Scale(e.frame.yAxis, e.radius2 * std::sin(th))));
  }
  return ucs::PointOnPlaneCircle(e.frame, e.radius, e.sweep * t);
}

Solid Translate(const Solid& s, const Vec3& delta) {
  Solid out = s;
  for (Vertex& v : out.vertices)
    v.p = ray3d::Add(v.p, delta);
  for (Edge& e : out.edges) {
    if (e.kind != CurveKind::Line)
      e.frame.origin = ray3d::Add(e.frame.origin, delta);
    for (Surface& sf : e.isectSurfaces)  // Intersection: the stored surfaces travel with the edge
      sf.frame.origin = ray3d::Add(sf.frame.origin, delta);
  }
  for (Face& f : out.faces) {
    f.surface.frame.origin = ray3d::Add(f.surface.frame.origin, delta);
    if (f.surface.kind == SurfaceKind::Nurbs)  // a NURBS face's shape IS its control net, in world
      f.surface.patch = nurbs::Translate(f.surface.patch, delta);
  }
  out.recipe.frame.origin = ray3d::Add(out.recipe.frame.origin, delta);
  return out;
}

namespace {


/// A frame is one POINT and three DIRECTIONS, and a rotation turns them with two different calls —
/// the origin about the axis LINE, each axis about the axis DIRECTION with no axis-point term
/// (REQ-328's split). Getting it wrong does not crash: it leaves a frame whose origin moved and whose
/// axes did not, which is a solid sheared away from its own surfaces.
void RotateFrameInPlace(ucs::Ucs& f, const Vec3& axisPoint, const Vec3& axisUnit, double angleRad) {
  f.origin = ray3d::RotatePointAboutAxis(f.origin, axisPoint, axisUnit, angleRad);
  f.xAxis = ray3d::RotateVectorAboutAxis(f.xAxis, axisUnit, angleRad);
  f.yAxis = ray3d::RotateVectorAboutAxis(f.yAxis, axisUnit, angleRad);
  f.zAxis = ray3d::RotateVectorAboutAxis(f.zAxis, axisUnit, angleRad);
}

/// Radii and heights are lengths, invariant under a rotation, so they are deliberately not touched
/// here — only the frame and, for a freeform face, the control net that IS its shape.
void RotateSurfaceInPlace(Surface& sf, const Vec3& axisPoint, const Vec3& axisUnit, double angleRad) {
  RotateFrameInPlace(sf.frame, axisPoint, axisUnit, angleRad);
  if (sf.kind == SurfaceKind::Nurbs)
    sf.patch = nurbs::Rotate(sf.patch, axisPoint, axisUnit, angleRad);
}

/// The mirror image of \ref RotateFrameInPlace: a uniform scale turns nothing, so the three axes are
/// left exactly as they are and only the origin moves.
void ScaleFrameOriginInPlace(ucs::Ucs& f, const Vec3& basePoint, double factor) {
  f.origin = ray3d::Add(basePoint, ray3d::Scale(ray3d::Sub(f.origin, basePoint), factor));
}

void ScaleSurfaceInPlace(Surface& sf, const Vec3& basePoint, double factor) {
  ScaleFrameOriginInPlace(sf.frame, basePoint, factor);
  sf.radius *= factor;
  sf.radius2 *= factor;
  sf.height *= factor;
  if (sf.kind == SurfaceKind::Nurbs)
    sf.patch = nurbs::Scale(sf.patch, basePoint, factor);
}

}  // namespace

bool Rotate(const Solid& s, const Vec3& axisPoint, const Vec3& axisUnit, double angleRad, Solid* out,
            Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!FinitePoint(axisPoint) || !FinitePoint(axisUnit) || !std::isfinite(angleRad))
    return Fail(Problem::NonFiniteParameter, outWhy);
  // Not normalized for the caller: a zero axis would then become an arbitrary one, and a shrink or a
  // shear would ship as a successful rotation. See Problem::RotateAxisNotUnit.
  if (std::fabs(ray3d::Length(axisUnit) - 1.0) > 1e-9)
    return Fail(Problem::RotateAxisNotUnit, outWhy);

  Solid r = s;
  for (Vertex& v : r.vertices)
    v.p = ray3d::RotatePointAboutAxis(v.p, axisPoint, axisUnit, angleRad);
  for (Edge& e : r.edges) {
    if (e.kind != CurveKind::Line)  // a Line edge's frame is unused — the same rule Translate keeps
      RotateFrameInPlace(e.frame, axisPoint, axisUnit, angleRad);
    for (Surface& sf : e.isectSurfaces)  // Intersection: the stored surfaces turn with the edge
      RotateSurfaceInPlace(sf, axisPoint, axisUnit, angleRad);
  }
  for (Face& f : r.faces)
    RotateSurfaceInPlace(f.surface, axisPoint, axisUnit, angleRad);
  // The recipe's DIMENSIONS are lengths and a rotation leaves them alone; only its placement turns.
  RotateFrameInPlace(r.recipe.frame, axisPoint, axisUnit, angleRad);

  const Problem why = Validate(r);
  if (why != Problem::Ok)
    return Fail(Problem::RotateResultInvalid, outWhy);
  *out = std::move(r);
  return Succeed(outWhy);
}

bool Scale(const Solid& s, const Vec3& basePoint, double factor, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!FinitePoint(basePoint))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!std::isfinite(factor) || factor <= 0.0)
    return Fail(Problem::ScaleFactorNonPositive, outWhy);

  Solid r = s;
  for (Vertex& v : r.vertices)
    v.p = ray3d::Add(basePoint, ray3d::Scale(ray3d::Sub(v.p, basePoint), factor));
  for (Edge& e : r.edges) {
    if (e.kind != CurveKind::Line) {
      ScaleFrameOriginInPlace(e.frame, basePoint, factor);
      e.radius *= factor;   // Arc radius / Ellipse semi-major
      e.radius2 *= factor;  // Ellipse semi-minor
      // `sweep` is an angle, so it does not scale.
    }
    for (Surface& sf : e.isectSurfaces)
      ScaleSurfaceInPlace(sf, basePoint, factor);
  }
  for (Face& f : r.faces)
    ScaleSurfaceInPlace(f.surface, basePoint, factor);

  // The recipe resizes with the solid. It is description and never truth (ADR-050 (f)), but a
  // description that still reports the old radius is false rather than merely unused.
  ScaleFrameOriginInPlace(r.recipe.frame, basePoint, factor);
  r.recipe.length *= factor;
  r.recipe.width *= factor;
  r.recipe.height *= factor;
  r.recipe.radius *= factor;
  r.recipe.radius2 *= factor;
  // The polysolid path is 2D in the recipe frame's plane, so it scales with the solid; `sides` is a
  // count and `sweep` an angle, and neither does.
  r.recipe.path.start.x *= factor;
  r.recipe.path.start.y *= factor;
  for (PathSeg& seg : r.recipe.path.segs) {
    seg.end.x *= factor;
    seg.end.y *= factor;
  }

  const Problem why = Validate(r);
  if (why != Problem::Ok)
    return Fail(Problem::ScaleResultInvalid, outWhy);
  *out = std::move(r);
  return Succeed(outWhy);
}

namespace {


/// Reflect \p f and make it right-handed again by negating Y (REQ-351). X and Z are reflected as
/// directions and kept, so a plane's Z stays its outward normal and a curved surface's axis its axis;
/// the price is that an angle measured about Z from X toward Y now runs the other way, which every
/// caller below pays by negating the angles it stores.
void MirrorFrameInPlace(ucs::Ucs& f, const Vec3& planePoint, const Vec3& planeUnit) {
  f.origin = ray3d::ReflectPointAcrossPlane(f.origin, planePoint, planeUnit);
  f.xAxis = ray3d::ReflectVectorAcrossPlane(f.xAxis, planeUnit);
  f.yAxis = ray3d::Scale(ray3d::ReflectVectorAcrossPlane(f.yAxis, planeUnit), -1.0);
  f.zAxis = ray3d::ReflectVectorAcrossPlane(f.zAxis, planeUnit);
}

void MirrorSurfaceInPlace(Surface& sf, const Vec3& planePoint, const Vec3& planeUnit) {
  MirrorFrameInPlace(sf.frame, planePoint, planeUnit);
  if (sf.kind == SurfaceKind::Nurbs)
    sf.patch = nurbs::Mirror(sf.patch, planePoint, planeUnit);
}

/// The span `[lo, hi]` of an angle that has just been negated: `[-hi, -lo]`, shifted by whole turns
/// so it starts in `[0, 2pi)` the way every builder writes one. Adding whole turns to both ends
/// changes no point on the face.
void NegateAngularSpan(double* lo, double* hi) {
  double a = -*hi;
  double b = -*lo;
  while (a < 0.0) {
    a += kTwoPi;
    b += kTwoPi;
  }
  *lo = a;
  *hi = b;
}

}  // namespace

bool Mirror(const Solid& s, const Vec3& planePoint, const Vec3& planeUnit, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!FinitePoint(planePoint) || !FinitePoint(planeUnit))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (std::fabs(ray3d::Length(planeUnit) - 1.0) > 1e-9)
    return Fail(Problem::MirrorPlaneNotUnit, outWhy);

  Solid r = s;
  for (Vertex& v : r.vertices)
    v.p = ray3d::ReflectPointAcrossPlane(v.p, planePoint, planeUnit);
  for (Edge& e : r.edges) {
    if (e.kind != CurveKind::Line) {
      MirrorFrameInPlace(e.frame, planePoint, planeUnit);
      // Arc / Ellipse: the frame's Y was negated, so the same points are reached by the opposite
      // parameter. Intersection: unused (its frame is only a witness point).
      e.sweep = -e.sweep;
    }
    for (Surface& sf : e.isectSurfaces)
      MirrorSurfaceInPlace(sf, planePoint, planeUnit);
  }
  for (Face& f : r.faces) {
    MirrorSurfaceInPlace(f.surface, planePoint, planeUnit);
    // Which face parameter the negated Y reverses, and how. A plane has no span; its general trim
    // loop, if any, is in frame (x, y), so it is v there.
    bool negateU = false;
    bool negateV = false;
    double uShift = 0.0;     // curved: the whole turns NegateAngularSpan added; u -> uShift - u
    double nurbsUSum = 0.0;  // a + b of the patch's U domain; u -> a + b - u
    switch (f.surface.kind) {
    case SurfaceKind::Plane:
      negateV = true;
      break;
    case SurfaceKind::Cylinder:
    case SurfaceKind::Cone:
    case SurfaceKind::Sphere:
    case SurfaceKind::Torus: {
      negateU = true;
      const double oldEnd = f.uEnd;
      NegateAngularSpan(&f.uStart, &f.uEnd);
      uShift = f.uStart + oldEnd;
      break;
    }
    case SurfaceKind::Nurbs: {
      const nurbs::Patch& p = f.surface.patch;  // already reversed; its domain is unchanged
      if (p.knotsU.size() > static_cast<std::size_t>(p.nu) && p.nu > p.degU)
        nurbsUSum = p.knotsU[static_cast<std::size_t>(p.degU)] + p.knotsU[static_cast<std::size_t>(p.nu)];
      const double a = nurbsUSum - f.uEnd;
      const double b = nurbsUSum - f.uStart;
      f.uStart = a;
      f.uEnd = b;
      break;
    }
    }
    for (std::vector<curveisect::Vec2>& poly : f.paramLoops) {
      for (curveisect::Vec2& q : poly) {
        if (f.surface.kind == SurfaceKind::Nurbs)
          q.x = nurbsUSum - q.x;
        else if (negateU)
          q.x = uShift - q.x;
        else if (negateV)
          q.y = -q.y;
      }
      // One parameter negated reverses the polygon's winding; put it back (outer CCW, holes CW).
      std::reverse(poly.begin(), poly.end());
    }
    // A reflection reverses the sense in which every boundary runs round its face.
    for (Loop& loop : f.loops) {
      std::reverse(loop.uses.begin(), loop.uses.end());
      for (EdgeUse& u : loop.uses)
        u.reversed = !u.reversed;
    }
  }

  // The recipe is kept: every primitive is symmetric about its frame's XZ plane (the builders place
  // box / wedge corners at +-width/2 and the pyramid's first corner on +X), so the reflected frame
  // describes the reflected primitive. A polysolid's path is in the frame's XY plane, where the
  // negated Y negates every y, every sweep, and which side of the path is "left".
  MirrorFrameInPlace(r.recipe.frame, planePoint, planeUnit);
  r.recipe.path.start.y = -r.recipe.path.start.y;
  for (PathSeg& seg : r.recipe.path.segs) {
    seg.end.y = -seg.end.y;
    seg.sweep = -seg.sweep;
  }
  if (r.recipe.justify == Justify::Left)
    r.recipe.justify = Justify::Right;
  else if (r.recipe.justify == Justify::Right)
    r.recipe.justify = Justify::Left;

  const Problem why = Validate(r);
  if (why != Problem::Ok)
    return Fail(Problem::MirrorResultInvalid, outWhy);
  *out = std::move(r);
  return Succeed(outWhy);
}

} // namespace brep
