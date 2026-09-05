#include "brep.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace brep {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kHalfPi = 0.5 * kPi;

/// Segment counts are clamped here rather than left to the tolerance alone: a chord tolerance of
/// 1e-12 on a survey-scale cylinder would otherwise ask for tens of millions of triangles and take
/// the frame budget with it (REQ-100).
constexpr int kMinArcSegments = 2;
constexpr int kMaxArcSegments = 512;

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

/// Signed area of one loop, measured in the face's plane and about the face's outward normal.
[[nodiscard]] double PlaneLoopSignedArea(const Solid& s, const Face& f, const Loop& lp) {
  const ucs::Ucs& fr = f.surface.frame;
  double acc = 0.0;
  for (const EdgeUse& u : lp.uses) {
    const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
    const int startV = u.reversed ? e.v1 : e.v0;
    const int endV = u.reversed ? e.v0 : e.v1;
    const ucs::Point2D a = ucs::WorldToPlane(fr, s.vertices[static_cast<std::size_t>(startV)].p);
    const ucs::Point2D b = ucs::WorldToPlane(fr, s.vertices[static_cast<std::size_t>(endV)].p);
    acc += 0.5 * (a.x * b.y - b.x * a.y);
    if (e.kind == CurveKind::Arc || e.kind == CurveKind::Ellipse) {
      // The bulge between the chord and the curve. Signed about the FACE normal, which is not
      // necessarily the curve's own normal — a cap rim and its face can be described the opposite way
      // round, and getting this sign wrong turns a disc into a bow tie. For an ellipse the sector
      // area is `0.5 a b dt`, so the bulge term is the circle's with `a b` for `r^2`.
      double sweep = u.reversed ? -e.sweep : e.sweep;
      if (ray3d::Dot(e.frame.zAxis, fr.zAxis) < 0.0)
        sweep = -sweep;
      const double rr = e.kind == CurveKind::Ellipse ? e.radius * e.radius2 : e.radius * e.radius;
      acc += 0.5 * rr * (sweep - std::sin(sweep));
    }
  }
  return acc;
}

[[nodiscard]] double PlaneFaceArea(const Solid& s, const Face& f) {
  double acc = 0.0;
  for (const Loop& lp : f.loops)
    acc += PlaneLoopSignedArea(s, f, lp);
  return acc;
}

struct ConeIntegrals {
  double area = 0.0;
  double volTerm = 0.0;
};

/// A cylinder is the `r0 == r1` case of a cone, so both surface kinds share this one derivation
/// rather than carrying two that could disagree.
[[nodiscard]] ConeIntegrals ConicalFaceIntegrals(double r0, double r1, double h, double u0, double u1,
                                                 const Vec3& q) {
  const double du = u1 - u0;
  const double k = (r0 - r1) / h;
  const double slant = std::sqrt(1.0 + k * k);
  const double iRho = h * (r0 + r1) * 0.5;                    // integral of rho dz
  const double iRho2 = h * (r0 * r0 + r0 * r1 + r1 * r1) / 3.0;  // integral of rho^2 dz
  const double iRhoZ = r0 * h * h * 0.5 + (r1 - r0) * h * h / 3.0;  // integral of rho z dz
  const double cT = std::sin(u1) - std::sin(u0);              // integral of cos t dt
  const double sT = std::cos(u0) - std::cos(u1);              // integral of sin t dt

  ConeIntegrals r;
  r.area = du * slant * iRho;
  r.volTerm = du * iRho2 - q.x * iRho * cT - q.y * iRho * sT + k * du * (iRhoZ - q.z * iRho);
  return r;
}

/// `integral over [u0,u1] of (A0 + A1 cos u + A2 sin u)(B0 + B1 cos u + B2 sin u) du` — closed form.
[[nodiscard]] double IntegrateTrigProduct(double A0, double A1, double A2, double B0, double B1,
                                          double B2, double u0, double u1) {
  auto F = [&](double u) {
    const double c = std::cos(u);
    const double s = std::sin(u);
    const double sc = s * c;
    return A0 * B0 * u + (A0 * B1 + A1 * B0) * s - (A0 * B2 + A2 * B0) * c +
           A1 * B1 * (0.5 * u + 0.5 * sc) + A2 * B2 * (0.5 * u - 0.5 * sc) +
           (A1 * B2 + A2 * B1) * 0.5 * s * s;
  };
  return F(u1) - F(u0);
}

/// The integrals of a **cylinder** face whose z-extent is `[zLo(u), zHi(u)]`, each a linear function
/// of `(cos u, sin u)` — a plane cut. `dz` coefficients are `zHi - zLo = d0 + d1 cos u + d2 sin u`.
/// `q` is the reference point in the surface's own frame. Reduces to \ref ConicalFaceIntegrals for a
/// cylinder when `(d0,d1,d2) = (h,0,0)`.
[[nodiscard]] ConeIntegrals CylinderPlaneCutIntegrals(double r, double u0, double u1, double d0,
                                                      double d1, double d2, const Vec3& q) {
  ConeIntegrals out;
  // area = integral of r * (zHi - zLo) du
  out.area = r * (d0 * (u1 - u0) + d1 * (std::sin(u1) - std::sin(u0)) -
                  d2 * (std::cos(u1) - std::cos(u0)));
  // volTerm = integral of (r - qx cos u - qy sin u) * r * (zHi - zLo) du
  out.volTerm = r * IntegrateTrigProduct(r, -q.x, -q.y, d0, d1, d2, u0, u1);
  return out;
}

struct SphereIntegrals {
  double area = 0.0;
  double volTerm = 0.0;
};

[[nodiscard]] SphereIntegrals SphericalFaceIntegrals(double radius, double u0, double u1, double v0,
                                                     double v1, const Vec3& q) {
  const double du = u1 - u0;
  const double sV = std::sin(v1) - std::sin(v0);
  const double iCos2 = (v1 - v0) * 0.5 + (std::sin(2.0 * v1) - std::sin(2.0 * v0)) * 0.25;
  const double iSinCos = (std::sin(v1) * std::sin(v1) - std::sin(v0) * std::sin(v0)) * 0.5;
  const double cT = std::sin(u1) - std::sin(u0);
  const double sT = std::cos(u0) - std::cos(u1);

  SphereIntegrals r;
  r.area = radius * radius * du * sV;
  r.volTerm = radius * radius * radius * du * sV - radius * radius * (q.x * cT + q.y * sT) * iCos2 -
              radius * radius * q.z * du * iSinCos;
  return r;
}

struct TorusIntegrals {
  double area = 0.0;
  double volTerm = 0.0;
};

[[nodiscard]] TorusIntegrals ToroidalFaceIntegrals(double major, double minor, double u0, double u1,
                                                   double v0, double v1, const Vec3& q) {
  const double R = major;
  const double r = minor;
  const double du = u1 - u0;
  const double dv = v1 - v0;
  const double sV = std::sin(v1) - std::sin(v0);
  const double cV = std::cos(v0) - std::cos(v1);
  const double iCos2 = dv * 0.5 + (std::sin(2.0 * v1) - std::sin(2.0 * v0)) * 0.25;
  const double iSinCos = (std::sin(v1) * std::sin(v1) - std::sin(v0) * std::sin(v0)) * 0.5;
  const double cT = std::sin(u1) - std::sin(u0);
  const double sT = std::cos(u0) - std::cos(u1);

  // The area element is r (R + r cos v) dv dt; the three v-integrals below are that element
  // weighted by 1, cos v and sin v respectively.
  const double iW = r * (R * dv + r * sV);
  const double i1 = (r * R * R + r * r * r) * sV + r * r * R * iCos2 + r * r * R * dv;
  const double i2 = r * R * sV + r * r * iCos2;
  const double i3 = r * R * cV + r * r * iSinCos;

  TorusIntegrals out;
  out.area = du * iW;
  out.volTerm = du * i1 - q.x * cT * i2 - q.y * sT * i2 - q.z * du * i3;
  return out;
}

struct FaceIntegrals {
  double area = 0.0;
  double volTerm = 0.0;
};

/// The z-bounds of a plane-cut cylinder face, each `c0 + c1 cos u + c2 sin u` in the surface frame.
struct CylinderCut {
  double lo[3] = {0.0, 0.0, 0.0};
  double hi[3] = {0.0, 0.0, 0.0};
};

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
    n = rho > 1e-12 ? Vec3{loc.x / rho, loc.y / rho, 0.0} : Vec3{1.0, 0.0, 0.0};
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

/// Signed distance from \p p to \p sf: positive on the outward-normal side, negative on the other.
[[nodiscard]] double SignedDistToSurface(const Surface& sf, const Vec3& p) {
  const Vec3 q = ClosestPointOnSurface(sf, p);
  const double d = ray3d::Length(ray3d::Sub(p, q));
  return ray3d::Dot(ray3d::Sub(p, q), SurfaceNormalGeom(sf, q)) >= 0.0 ? d : -d;
}

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

  // A rough length: start -> witness -> end. The marching step is a fraction of it.
  const double rough = ray3d::Length(ray3d::Sub(witness, start)) + ray3d::Length(ray3d::Sub(end, witness));
  const double step = std::max(rough / static_cast<double>(n), 1e-12);
  Vec3 p = start;
  // Head toward the witness on the first step, then keep the same travel sense.
  Vec3 prevDir = ray3d::Normalize(ray3d::Sub(witness, start));
  const int guard = 8 * n + 64;
  bool nearEndArmed = false;
  for (int it = 0; it < guard; ++it) {
    Vec3 tangent = ray3d::Cross(SurfaceNormalGeom(sa, p), SurfaceNormalGeom(sb, p));
    const double tl = ray3d::Length(tangent);
    if (!(tl > 1e-14))
      break;  // surfaces tangent here — outside B2b-2's remit
    tangent = ray3d::Scale(tangent, 1.0 / tl);
    if (ray3d::Dot(tangent, prevDir) < 0.0)
      tangent = ray3d::Scale(tangent, -1.0);
    Vec3 next = SettleOntoIntersection(sa, sb, ray3d::Add(p, ray3d::Scale(tangent, step)));
    const Vec3 realDir = ray3d::Sub(next, p);
    const double moved = ray3d::Length(realDir);
    if (!(moved > 1e-15))
      break;
    prevDir = ray3d::Scale(realDir, 1.0 / moved);
    const double distToEnd = ray3d::Length(ray3d::Sub(next, end));
    if (nearEndArmed && distToEnd >= ray3d::Length(ray3d::Sub(p, end))) {
      break;  // walked past the closest approach to v1 — stop and close on it
    }
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

// 8-node Gauss–Legendre on [-1, 1] (four symmetric pairs).
constexpr double kGL8x[4] = {0.18343464249564980, 0.52553240991632899, 0.79666647741362674,
                             0.96028985649753623};
constexpr double kGL8w[4] = {0.36268378337836199, 0.31370664587788729, 0.22238103445337447,
                             0.10122853629037626};

/// Integrate \p fn over `[a, b]` with \p panels sub-panels whose boundaries cluster toward both ends
/// (a cosine grading) — so a face whose strip pinches to zero width at an end still integrates
/// accurately — and an 8-node Gauss rule inside each panel.
template <class Fn>
[[nodiscard]] double GradedGaussIntegrate(double a, double b, int panels, Fn fn) {
  const double mid = 0.5 * (a + b);
  const double half = 0.5 * (b - a);
  auto bound = [&](int k) { return mid - half * std::cos(kPi * k / panels); };
  double acc = 0.0;
  for (int k = 0; k < panels; ++k) {
    const double lo = bound(k);
    const double hi = bound(k + 1);
    const double c = 0.5 * (lo + hi);
    const double h = 0.5 * (hi - lo);
    for (int j = 0; j < 4; ++j)
      acc += h * kGL8w[j] * (fn(c + h * kGL8x[j]) + fn(c - h * kGL8x[j]));
  }
  return acc;
}

/// The axial extent of a cylinder face bounded by a procedural `Intersection` edge, as a function of
/// longitude `u`: at each `u` the face spans `[zLo(u), zHi(u)]` on its own surface, where the surface
/// crosses the *other* surface the edge carries. Set up once per face, then queried per `u`.
struct IsectStrip {
  const Surface* sf = nullptr;
  const Surface* other = nullptr;
  double zSearchLo = 0.0;
  double zSearchHi = 0.0;
  bool oneSided = false;  ///< the loop has ONE Intersection edge: the strip runs curve <-> `rimZ`.
  double rimZ = 0.0;      ///< the constant axial bound (a flat rim) opposite the curve, if `oneSided`.
  [[nodiscard]] bool valid() const { return sf && other && zSearchHi > zSearchLo; }
};

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
        for (std::size_t k = 0; k < 2; ++k)
          if (!SameSurfaceApprox(e.isectSurfaces[k], f.surface))
            st.other = &e.isectSurfaces[k];
      }
    }
  if (zMax > zMin) {
    const double margin = 0.3 * (zMax - zMin) + 1e-6 * (1.0 + std::fabs(zMax));
    st.zSearchLo = zMin - margin;
    st.zSearchHi = zMax + margin;
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
  auto g = [&](double z) {
    return SignedDistToSurface(*st.other,
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
    return *zHi > *zLo;
  }
  if (roots < 2)
    return false;
  *zLo = first;
  *zHi = last;
  return true;
}

/// Numerical area / volume term of a **cylinder** face whose boundary loop contains a procedural
/// `Intersection` edge — the one place ADR-045's closed-form rule bends (D-2026-09-02-i). Integrates
/// over the face's own longitude `u` with a graded Gauss rule. `inward` is left for the shared flip
/// in \ref IntegrateFace.
[[nodiscard]] FaceIntegrals IntegrateCylinderFaceNumeric(const Solid& s, const Face& f, const Vec3& q) {
  const Vec3 qL = ucs::WorldToUcs(f.surface.frame, q);
  const double r = f.surface.radius;
  const IsectStrip st = MakeIsectStrip(s, f);
  FaceIntegrals out;
  if (!st.valid())
    return out;
  out.area = GradedGaussIntegrate(f.uStart, f.uEnd, 48, [&](double u) {
    double a = 0.0;
    double b = 0.0;
    return IsectStripAt(st, u, &a, &b) ? r * (b - a) : 0.0;
  });
  out.volTerm = GradedGaussIntegrate(f.uStart, f.uEnd, 48, [&](double u) {
    double a = 0.0;
    double b = 0.0;
    if (!IsectStripAt(st, u, &a, &b))
      return 0.0;
    return r * (r - qL.x * std::cos(u) - qL.y * std::sin(u)) * (b - a);
  });
  return out;
}

/// The latitude extent of a **sphere** face bounded by a procedural `Intersection` edge, as a
/// function of longitude `u`: at each `u` the meridian spans `[vLo(u), vHi(u)]` between the two
/// crossings of the *other* surface the edge carries (REQ-314 B2b-2, GitHub issue #242 — the
/// sphere ∩ cylinder offset quartic). The `v`-analogue of \ref IsectStrip.
struct SphereIsectStrip {
  const Surface* sf = nullptr;     ///< the sphere
  const Surface* other = nullptr;  ///< the surface it crosses (a cylinder)
  double vSearchLo = 0.0;
  double vSearchHi = 0.0;
  [[nodiscard]] bool valid() const { return sf && other && vSearchHi > vSearchLo; }
};

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

/// `[vLo, vHi]` at longitude \p u, or false where the meridian never enters the other surface (past
/// an end of the lens-shaped patch). Scans latitude for the two crossings and bisects each.
[[nodiscard]] bool SphereStripAt(const SphereIsectStrip& st, double u, double* vLo, double* vHi) {
  const double R = st.sf->radius;
  auto g = [&](double v) {
    return SignedDistToSurface(
        *st.other, ucs::UcsToWorld(st.sf->frame, Vec3{R * std::cos(v) * std::cos(u),
                                                      R * std::cos(v) * std::sin(u), R * std::sin(v)}));
  };
  double first = 0.0;
  double last = 0.0;
  int roots = 0;
  const int scan = 96;
  double vp = st.vSearchLo;
  double gp = g(vp);
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
      if (roots == 0)
        first = root;
      last = root;
      ++roots;
    }
    vp = vc;
    gp = gc;
  }
  if (roots == 1) {
    // The bite runs off one end of the search range — a polar cap (issue #242, the d < r sub-case).
    // `inside` above the lone root ⟹ the cap reaches the top of the range (a pole), and vice versa.
    if (g(st.vSearchHi) <= 0.0) {
      *vLo = first;
      *vHi = st.vSearchHi;
      return true;
    }
    if (g(st.vSearchLo) <= 0.0) {
      *vLo = st.vSearchLo;
      *vHi = first;
      return true;
    }
  }
  if (roots < 2)
    return false;
  *vLo = first;
  *vHi = last;
  return true;
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

/// Numerical area / volume term of the lens-shaped **sphere** patch a procedural `Intersection` edge
/// bounds (REQ-314 B2b-2, GitHub issue #242) — the sphere analogue of
/// \ref IntegrateCylinderFaceNumeric. Integrates over the face's own longitude `u`; the per-`u`
/// latitude integrals are the same ones \ref SphericalFaceIntegrals uses, with the limits found by
/// bisecting the quartic instead of read from a rectangle. Sums every lens bite, so a hemisphere
/// face with a bite near each pole reports the total bite; \ref IntegrateFace subtracts it from the
/// analytic hemisphere for SUBTRACT / UNION and uses it directly for the INTERSECT plug.
[[nodiscard]] FaceIntegrals IntegrateSphereFaceNumeric(const Solid& s, const Face& f, const Vec3& q) {
  const Vec3 qL = ucs::WorldToUcs(f.surface.frame, q);
  const double R = f.surface.radius;
  const SphereIsectStrip st = MakeSphereIsectStrip(s, f);
  FaceIntegrals out;
  if (!st.valid())
    return out;
  const double u0 = std::min(f.uStart, f.uEnd);
  const double u1 = std::max(f.uStart, f.uEnd);
  std::vector<std::pair<double, double>> strips;
  out.area = GradedGaussIntegrate(u0, u1, 22, [&](double u) {
    SphereStripsAt(st, u, &strips);
    double acc = 0.0;
    for (const auto& [a, b] : strips)
      acc += R * R * (std::sin(b) - std::sin(a));
    return acc;
  });
  out.volTerm = GradedGaussIntegrate(u0, u1, 22, [&](double u) {
    SphereStripsAt(st, u, &strips);
    double acc = 0.0;
    for (const auto& [a, b] : strips) {
      const double sV = std::sin(b) - std::sin(a);
      const double iCos2 = (b - a) * 0.5 + (std::sin(2.0 * b) - std::sin(2.0 * a)) * 0.25;
      const double iSinCos = (std::sin(b) * std::sin(b) - std::sin(a) * std::sin(a)) * 0.5;
      acc += R * R * R * sV - R * R * (qL.x * std::cos(u) + qL.y * std::sin(u)) * iCos2 -
             R * R * qL.z * iSinCos;
    }
    return acc;
  });
  return out;
}

/// Area and divergence-theorem volume term of a \ref SurfaceKind::Nurbs face, by Gauss–Legendre
/// quadrature over the patch parameter rectangle the face occupies (ADR-045 (b) as widened by
/// D-2026-09-03-b — the numerical carve-out for a face with no closed form). The **un-normalised**
/// cross product `Su x Sv` is exactly the oriented vector area element `n dA`, so the area integrand
/// is its length and the volume integrand is `(S - q) . (Su x Sv)`. Builders orient the patch so
/// `Su x Sv` points outward; \ref Surface::inward is applied by the shared flip in \ref IntegrateFace.
/// The distinct interior knot values of \p knots strictly inside `[lo, hi]`, plus the two ends — the
/// panel boundaries the quadrature must respect, since a (rational) B-spline is only C^(p-1) at a
/// knot and Gauss–Legendre is accurate only on a smooth integrand.
[[nodiscard]] std::vector<double> KnotPanels(const std::vector<double>& knots, double lo, double hi) {
  std::vector<double> b{lo};
  for (double k : knots)
    if (k > lo + 1e-12 && k < hi - 1e-12 && k > b.back() + 1e-12)
      b.push_back(k);
  b.push_back(hi);
  return b;
}

[[nodiscard]] FaceIntegrals IntegrateNurbsFaceNumeric(const Face& f, const Vec3& q) {
  const nurbs::Patch& patch = f.surface.patch;
  const double uLo = std::min(f.uStart, f.uEnd);
  const double uHi = std::max(f.uStart, f.uEnd);
  const double vLo = std::min(f.vStart, f.vEnd);
  const double vHi = std::max(f.vStart, f.vEnd);
  const std::vector<double> uB = KnotPanels(patch.knotsU, uLo, uHi);
  const std::vector<double> vB = KnotPanels(patch.knotsV, vLo, vHi);

  // Integrate each knot cell on its own with a graded Gauss rule, so no panel straddles a knot.
  auto over = [&](auto&& integrand) {
    double acc = 0.0;
    for (std::size_t iu = 0; iu + 1 < uB.size(); ++iu)
      for (std::size_t iv = 0; iv + 1 < vB.size(); ++iv)
        acc += GradedGaussIntegrate(uB[iu], uB[iu + 1], 4, [&](double u) {
          return GradedGaussIntegrate(vB[iv], vB[iv + 1], 4,
                                      [&](double v) { return integrand(u, v); });
        });
    return acc;
  };

  FaceIntegrals out;
  out.area = over([&](double u, double v) {
    const nurbs::SurfacePoint sp = nurbs::EvaluateWithDerivs(patch, u, v);
    return ray3d::Length(ray3d::Cross(sp.du, sp.dv));
  });
  out.volTerm = over([&](double u, double v) {
    const nurbs::SurfacePoint sp = nurbs::EvaluateWithDerivs(patch, u, v);
    return ray3d::Dot(ray3d::Sub(sp.p, q), ray3d::Cross(sp.du, sp.dv));
  });
  return out;
}

/// \p q is the world-frame reference point; each branch transforms it into the surface's own frame.
[[nodiscard]] FaceIntegrals IntegrateFace(const Solid& s, const Face& f, const Vec3& q) {
  const Surface& sf = f.surface;
  const Vec3 qLocal = ucs::WorldToUcs(sf.frame, q);
  FaceIntegrals out;
  switch (sf.kind) {
  case SurfaceKind::Plane: {
    out.area = PlaneFaceArea(s, f);
    // (p - q) . n is constant over a plane face: it is the signed distance from q to the plane,
    // negated because qLocal.z measures from the plane toward q.
    out.volTerm = -qLocal.z * out.area;
    break;
  }
  case SurfaceKind::Cylinder: {
    if (FaceLoopHasIntersectionEdge(s, f)) {
      // ADR-045 (b) numerical carve-out (D-2026-09-02-i). A procedural edge in the OUTER loop bounds
      // the face's material directly; one that appears only in an INNER loop is a bite out of an
      // otherwise-full band (a bored branch mouth), so the term is the full band minus that bite.
      const bool inOuter = LoopHasIntersectionEdge(s, f.loops.front());
      if (inOuter) {
        out = IntegrateCylinderFaceNumeric(s, f, q);
      } else {
        double zLo = 1e300;
        double zHi = -1e300;
        for (const EdgeUse& u : f.loops.front().uses)
          for (const int vi : {s.edges[static_cast<std::size_t>(u.edge)].v0,
                               s.edges[static_cast<std::size_t>(u.edge)].v1}) {
            const double z = ucs::WorldToUcs(sf.frame, s.vertices[static_cast<std::size_t>(vi)].p).z;
            zLo = std::min(zLo, z);
            zHi = std::max(zHi, z);
          }
        const ConeIntegrals full = CylinderPlaneCutIntegrals(sf.radius, f.uStart, f.uEnd, zHi - zLo,
                                                             0.0, 0.0, qLocal);
        const FaceIntegrals bite = IntegrateCylinderFaceNumeric(s, f, q);
        out.area = full.area - bite.area;
        out.volTerm = full.volTerm - bite.volTerm;
      }
      break;
    }
    CylinderCut cc;
    const ConeIntegrals ci =
        CylinderCutZExtent(s, f, &cc)
            ? CylinderPlaneCutIntegrals(sf.radius, f.uStart, f.uEnd, cc.hi[0] - cc.lo[0],
                                        cc.hi[1] - cc.lo[1], cc.hi[2] - cc.lo[2], qLocal)
            : ConicalFaceIntegrals(sf.radius, sf.radius, sf.height, f.uStart, f.uEnd, qLocal);
    out.area = ci.area;
    out.volTerm = ci.volTerm;
    break;
  }
  case SurfaceKind::Cone: {
    const ConeIntegrals ci = ConicalFaceIntegrals(sf.radius, sf.radius2, sf.height, f.uStart, f.uEnd, qLocal);
    out.area = ci.area;
    out.volTerm = ci.volTerm;
    break;
  }
  case SurfaceKind::Sphere: {
    if (FaceLoopHasIntersectionEdge(s, f)) {
      // ADR-045 (b) numerical carve-out (D-2026-09-02-i), extended to sphere faces for the
      // sphere ∩ cylinder offset quartic (issue #242). The INTERSECT plug's face IS the lens
      // patch, so the numeric strip is the whole of it. The SUBTRACT / UNION kept sphere is a
      // hemisphere (full pole-to-pole `v` span) with a lens bite near each pole — the analytic
      // hemisphere minus the numeric bite, mirroring the cylinder `!inOuter` branch above.
      const FaceIntegrals bite = IntegrateSphereFaceNumeric(s, f, q);
      const bool hemisphereWithBite =
          f.vStart <= -kHalfPi + 1e-9 && f.vEnd >= kHalfPi - 1e-9;
      if (hemisphereWithBite) {
        const SphereIntegrals full =
            SphericalFaceIntegrals(sf.radius, f.uStart, f.uEnd, -kHalfPi, kHalfPi, qLocal);
        out.area = full.area - bite.area;
        out.volTerm = full.volTerm - bite.volTerm;
      } else {
        out = bite;
      }
      break;
    }
    const SphereIntegrals si =
        SphericalFaceIntegrals(sf.radius, f.uStart, f.uEnd, f.vStart, f.vEnd, qLocal);
    out.area = si.area;
    out.volTerm = si.volTerm;
    break;
  }
  case SurfaceKind::Torus: {
    const TorusIntegrals ti =
        ToroidalFaceIntegrals(sf.radius, sf.radius2, f.uStart, f.uEnd, f.vStart, f.vEnd, qLocal);
    out.area = ti.area;
    out.volTerm = ti.volTerm;
    break;
  }
  case SurfaceKind::Nurbs:
    out = IntegrateNurbsFaceNumeric(f, q);
    break;
  }
  // An inward curved face (REQ-314 B2a) has its normal flipped, so its divergence-theorem term
  // flips with it: the face then subtracts the void it bounds. Area is a magnitude and does not.
  if (sf.inward)
    out.volTerm = -out.volTerm;
  return out;
}

/// The reference point every volume integral is taken about: the mean of the solid's vertices.
///
/// Any fixed point gives the same volume — the form is rotation and translation invariant — so this
/// is chosen purely for conditioning. A point ON the solid keeps (p - q) at model scale, which is
/// the whole of the answer to "does this stay stable at state-plane coordinates?".
[[nodiscard]] Vec3 ReferencePoint(const Solid& s) {
  if (s.vertices.empty())
    return Vec3{};
  Vec3 acc{};
  for (const Vertex& v : s.vertices)
    acc = ray3d::Add(acc, v.p);
  return ray3d::Scale(acc, 1.0 / static_cast<double>(s.vertices.size()));
}

/// The volume enclosed by \p s, integrated about \p q. Shared by \ref Validate and
/// \ref ComputeMassProperties so the number the validity check accepts is the number the user is
/// later shown, rather than two integrations that could drift apart.
///
/// Worth knowing before touching the `q` terms in the integrals above: on a **closed** surface they
/// sum to exactly zero, because collectively they are `-(1/3) q . (closed integral of n dA)` and
/// that integral vanishes. So they cannot change a valid solid's volume, and a sign error in one of
/// them is provably invisible here — measured, not assumed (a deliberately flipped sign left the
/// whole suite green). They earn their place twice over regardless: they are what make the integral
/// correct for a `q` near the solid rather than at the world origin, which is the whole numerical
/// stability argument at state-plane magnitudes; and they are what make the closure probe below
/// non-vacuous, since without them this function would be trivially independent of `q`.
[[nodiscard]] double VolumeAbout(const Solid& s, const Vec3& q, bool* outFinite) {
  double volTerm = 0.0;
  bool finite = true;
  for (const Face& f : s.faces) {
    const FaceIntegrals fi = IntegrateFace(s, f, q);
    if (!std::isfinite(fi.area) || !std::isfinite(fi.volTerm))
      finite = false;
    volTerm += fi.volTerm;
  }
  if (outFinite)
    *outFinite = finite;
  return volTerm / 3.0;
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

// ---------------------------------------------------------------------------------------------
// Tessellation helpers.
// ---------------------------------------------------------------------------------------------

/// Segments needed so the sagitta of each chord stays within \p tol on a circle of \p radius.
[[nodiscard]] int SegmentsForArc(double radius, double spanRad, double tol) {
  const double span = std::fabs(spanRad);
  if (!(radius > 0.0) || !(span > 0.0))
    return 1;
  if (tol >= radius)
    return kMinArcSegments;
  const double maxStep = 2.0 * std::acos(1.0 - tol / radius);
  if (!(maxStep > 0.0))
    return kMaxArcSegments;
  const int n = static_cast<int>(std::ceil(span / maxStep));
  return std::clamp(n, kMinArcSegments, kMaxArcSegments);
}

/// Segment count to walk a curved edge (Arc / Ellipse / Intersection) within \p tol; 1 for a line.
[[nodiscard]] int SegmentsForEdge(const Edge& e, double tol) {
  if (e.kind == CurveKind::Line)
    return 1;
  if (e.kind == CurveKind::Intersection) {
    // A quartic has no closed segment count. Use the tighter of the two surfaces' curvatures as a
    // proxy radius over a quarter turn — generous, and the marched walk itself is what stays on it.
    double rr = 1.0;
    for (const Surface& sf : e.isectSurfaces)
      if (sf.radius > 1e-9)
        rr = std::min(rr == 1.0 ? sf.radius : rr, sf.radius);
    return SegmentsForArc(rr, kHalfPi, tol);
  }
  const double r = e.kind == CurveKind::Ellipse ? std::max(e.radius, e.radius2) : e.radius;
  return SegmentsForArc(r, e.sweep, tol);
}

struct MeshBuilder {
  Tessellation* out = nullptr;
  /// Which face the triangles being emitted belong to. Set once per face by the loop below, so no
  /// emit site has to remember to pass it and none can pass the wrong one.
  int face = -1;

  std::uint32_t Push(const Vec3& p, const Vec3& n) {
    out->vertsXyz.push_back(p.x);
    out->vertsXyz.push_back(p.y);
    out->vertsXyz.push_back(p.z);
    out->normalsXyz.push_back(n.x);
    out->normalsXyz.push_back(n.y);
    out->normalsXyz.push_back(n.z);
    return static_cast<std::uint32_t>(out->vertsXyz.size() / 3) - 1;
  }

  void Tri(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (a == b || b == c || a == c)
      return;  // a pole fan's degenerate sliver; emitting it would only cost the GPU work
    out->indices.push_back(a);
    out->indices.push_back(b);
    out->indices.push_back(c);
    out->triFace.push_back(face);
  }
};

/// The outward unit normal of a cone/cylinder side at longitude \p t, in world. `Surface::inward`
/// (REQ-314 B2a) flips it: the face then bounds a bore, material on the −radial side.
[[nodiscard]] Vec3 ConicalNormal(const Surface& sf, double r0, double r1, double t) {
  const double k = (r0 - r1) / sf.height;
  const double inv = 1.0 / std::sqrt(1.0 + k * k);
  const double s = sf.inward ? -1.0 : 1.0;
  const Vec3 local{s * std::cos(t) * inv, s * std::sin(t) * inv, s * k * inv};
  return ucs::UcsVectorToWorld(sf.frame, local);
}

[[nodiscard]] Vec3 ConicalPoint(const Surface& sf, double r0, double r1, double t, double z) {
  const double rho = r0 + (r1 - r0) * (z / sf.height);
  return ucs::UcsToWorld(sf.frame, Vec3{rho * std::cos(t), rho * std::sin(t), z});
}

[[nodiscard]] Vec3 SphericalPoint(const Surface& sf, double t, double v) {
  const double cv = std::cos(v);
  return ucs::UcsToWorld(sf.frame,
                         Vec3{sf.radius * cv * std::cos(t), sf.radius * cv * std::sin(t),
                              sf.radius * std::sin(v)});
}

[[nodiscard]] Vec3 SphericalNormal(const Surface& sf, double t, double v) {
  const double cv = std::cos(v);
  const double s = sf.inward ? -1.0 : 1.0;
  return ucs::UcsVectorToWorld(sf.frame, Vec3{s * cv * std::cos(t), s * cv * std::sin(t), s * std::sin(v)});
}

[[nodiscard]] Vec3 ToroidalPoint(const Surface& sf, double t, double v) {
  const double rho = sf.radius + sf.radius2 * std::cos(v);
  return ucs::UcsToWorld(sf.frame, Vec3{rho * std::cos(t), rho * std::sin(t), sf.radius2 * std::sin(v)});
}

[[nodiscard]] Vec3 ToroidalNormal(const Surface& sf, double t, double v) {
  const double cv = std::cos(v);
  const double s = sf.inward ? -1.0 : 1.0;
  return ucs::UcsVectorToWorld(sf.frame, Vec3{s * cv * std::cos(t), s * cv * std::sin(t), s * std::sin(v)});
}

} // namespace

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
  case Problem::EdgeOrientationInconsistent: return "Two faces disagree about which way an edge runs.";
  case Problem::FaceHasNoLoop: return "A face has no boundary.";
  case Problem::DegenerateFace: return "A face has no area.";
  case Problem::DegenerateEdge: return "An edge has no length.";
  case Problem::NonFiniteCoordinate: return "A coordinate is not a finite number.";
  case Problem::NotClosed: return "The surface does not enclose a volume.";
  case Problem::UnusedVertex: return "A vertex is not used by any edge.";
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


// -------------------------------------------------------------------------------------------
// Push/pull a planar face (REQ-319 / ADR-046 amendment (i), GitHub issue #148 Phase 5).
// -------------------------------------------------------------------------------------------

namespace {

/// Every vertex index the loops of \p f use, once each.
void CollectFaceVertices(const Solid& s, const Face& f, std::vector<int>* out) {
  for (const Loop& loop : f.loops) {
    for (const EdgeUse& use : loop.uses) {
      if (use.edge < 0 || static_cast<size_t>(use.edge) >= s.edges.size())
        continue;
      const Edge& e = s.edges[static_cast<size_t>(use.edge)];
      for (int v : {e.v0, e.v1})
        if (std::find(out->begin(), out->end(), v) == out->end())
          out->push_back(v);
    }
  }
}

/// One plane as `n · p = d`, with `n` unit length.
struct PushFacePlane {
  Vec3 n;
  double d = 0.0;
};

/// Where three planes cross, or false when they do not cross in a single point (two parallel, or
/// all three sharing a line). `det` is the scalar triple product of the normals; it vanishes in
/// exactly those cases, which is why the test is on it rather than on the angles between them.
bool ThreePlanePoint(const PushFacePlane& a, const PushFacePlane& b, const PushFacePlane& c, Vec3* out) {
  const Vec3 bc = ray3d::Cross(b.n, c.n);
  const double det = ray3d::Dot(a.n, bc);
  // Scale-relative: the normals are unit, so `det` is already dimensionless and a fixed floor is
  // meaningful here in a way it would not be for a length.
  if (!(std::fabs(det) > 1e-9))
    return false;
  const Vec3 ca = ray3d::Cross(c.n, a.n);
  const Vec3 ab = ray3d::Cross(a.n, b.n);
  *out = ray3d::Scale(ray3d::Add(ray3d::Add(ray3d::Scale(bc, a.d), ray3d::Scale(ca, b.d)),
                                 ray3d::Scale(ab, c.d)),
                      1.0 / det);
  return true;
}


/// A cylinder or cone wall beside the cap being moved, and what the move does to it.
///
/// A cap is a plane and moves by translation; the WALL beside it is not a plane and cannot be
/// intersected, so it is re-parameterised instead: a cylinder's stored height grows or shrinks, and
/// a cone's end radius follows its own slope (D-2026-09-04-c, the taper choice put to the user —
/// **keep the slope, let the radius change**, so pushing a cone's cap extends the same cone rather
/// than bending its wall).
struct CurvedWallMove {
  Vec3 axis;         ///< Unit, the wall's own +Z.
  Vec3 origin;       ///< The wall's frame origin (base centre) BEFORE the move.
  double height = 0.0;
  double radius = 0.0;   ///< At the wall's z = 0.
  double radius2 = 0.0;  ///< At z = height. Equal to \ref radius for a cylinder.
  bool movingTopEnd = true;  ///< Whether the cap being moved is the wall's +Z end.
  // Filled by Solve():
  Vec3 newOrigin;
  double newHeight = 0.0;
  double newRadius = 0.0;
  double newRadius2 = 0.0;
  double movedEndRadius = 0.0;  ///< The radius at the END THAT MOVED, after the move.
  double movedEndAxial = 0.0;   ///< Its axial coordinate measured from \ref newOrigin.
};

/// Work out the wall's new parameters for a cap moved by \p t along the wall's axis.
///
/// The wall spans `origin` to `origin + height*axis`. Whichever end the cap is, the other stays put
/// and the moving one travels by `t` — so a cylinder simply gets taller or shorter, and a cone's
/// moving end takes the radius its own slope puts there. False when that leaves no wall: zero or
/// negative height, or a radius at or through zero, which is a shape change rather than a size one.
bool SolveCurvedWall(CurvedWallMove* w, double t) {
  const double slope = (w->height > 1e-12) ? (w->radius2 - w->radius) / w->height : 0.0;
  if (w->movingTopEnd) {
    w->newOrigin = w->origin;
    w->newHeight = w->height + t;
    w->newRadius = w->radius;
    w->newRadius2 = w->radius + slope * w->newHeight;
    w->movedEndRadius = w->newRadius2;
    w->movedEndAxial = w->newHeight;
  } else {
    // The base end moves: the frame origin travels with it, and the radius there is what the same
    // slope gives at the new position. The top end and its radius are untouched, which is what
    // "keep the slope" means from the other side.
    w->newOrigin = ray3d::Add(w->origin, ray3d::Scale(w->axis, t));
    w->newHeight = w->height - t;
    w->newRadius = w->radius + slope * t;
    w->newRadius2 = w->radius2;
    w->movedEndRadius = w->newRadius;
    w->movedEndAxial = 0.0;
  }
  if (!(w->newHeight > 1e-9))
    return false;  // pushed flat or through itself
  if (!(w->newRadius > 1e-9) || !(w->newRadius2 > 1e-9))
    return false;  // a cone driven to or past its own apex: the cap would become a point
  return true;
}
}  // namespace

bool PushPullFace(const Solid& s, int faceIndex, double distance, Solid* out, Problem* outWhy) {
  const auto fail = [&](Problem p) {
    if (outWhy)
      *outWhy = p;
    return false;
  };
  if (!out)
    return fail(Problem::IndexOutOfRange);
  if (faceIndex < 0 || static_cast<size_t>(faceIndex) >= s.faces.size())
    return fail(Problem::IndexOutOfRange);
  // Zero is refused rather than treated as a successful no-op: a command that reports it moved
  // something it did not is worse than one that declines (REQ-319 item 3).
  if (!std::isfinite(distance) || std::fabs(distance) <= 1e-12)
    return fail(Problem::PushPullDistanceZero);

  const Face& face = s.faces[static_cast<size_t>(faceIndex)];

  // --- A CYLINDER WALL: the one curved face that CAN be pushed, because it has exactly one
  // --- parameter a push corresponds to (REQ-319 increment 4 / D-2026-09-04-e).
  //
  // A wall push is not a translation, and that is the whole reason it needs its own path rather
  // than a wider tolerance somewhere: the outward normal points a different way at every point of
  // the surface, so there is no single direction to move along. What the gesture means is a RADIUS
  // change — every point moves along its own normal by the same amount, which is exactly what
  // adding to the radius does.
  //
  // A cone is deliberately NOT here. Offsetting a cone along its own normal moves both radii by
  // `d / cos(half-angle)` and leaves the apex where it was: an offset surface, not a radius change,
  // and a second decision about what the gesture should mean. Its own increment if it is ever
  // wanted.
  if (face.surface.kind == SurfaceKind::Cylinder) {
    const Surface& w0 = face.surface;
    const Vec3 axis = ray3d::Normalize(w0.frame.zAxis);
    // `inward` flips which side the material is on (REQ-314 B2a): for a hole, the outward normal
    // points AT the axis, so pushing outward makes the hole smaller. Honoured here or a positive
    // push would widen a hole while widening a boss — a sign error that reads as a working feature
    // half the time.
    const double newRadius = w0.radius + (w0.inward ? -distance : distance);
    if (!(newRadius > 1e-9))
      return fail(Problem::PushPullCurvedDegenerate);

    // Every face sharing this surface is part of the same wall — a full cylinder is stored as two
    // half-faces, and pushing one without the other would leave the solid with two different radii
    // meeting along a seam.
    std::vector<int> wallFaces;
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      const Surface& sf = s.faces[fi].surface;
      if (sf.kind != SurfaceKind::Cylinder || sf.inward != w0.inward)
        continue;
      if (std::fabs(sf.radius - w0.radius) > 1e-9 || std::fabs(sf.height - w0.height) > 1e-9)
        continue;
      if (ray3d::Length(ray3d::Sub(sf.frame.origin, w0.frame.origin)) > 1e-9)
        continue;
      if (std::fabs(std::fabs(ray3d::Dot(ray3d::Normalize(sf.frame.zAxis), axis)) - 1.0) > 1e-9)
        continue;
      wallFaces.push_back(static_cast<int>(fi));
    }
    std::vector<int> wallVerts;
    for (int fi : wallFaces)
      CollectFaceVertices(s, s.faces[static_cast<size_t>(fi)], &wallVerts);
    if (wallVerts.empty())
      return fail(Problem::FaceHasNoLoop);
    const auto onWall = [&](int v) {
      return std::find(wallVerts.begin(), wallVerts.end(), v) != wallVerts.end();
    };

    // Every other face at those corners must be a CAP — a plane square to the axis. Such a plane is
    // unchanged by the move: its boundary slides around inside it, which is exactly what growing a
    // circle within its own plane does. A slanted plane would have to be re-solved, and a second
    // curved surface would have its own parameter to argue about.
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      if (std::find(wallFaces.begin(), wallFaces.end(), static_cast<int>(fi)) != wallFaces.end())
        continue;
      std::vector<int> fv;
      CollectFaceVertices(s, s.faces[fi], &fv);
      if (!std::any_of(fv.begin(), fv.end(), onWall))
        continue;
      if (s.faces[fi].surface.kind != SurfaceKind::Plane)
        return fail(Problem::PushPullNeighbourCurved);
      if (std::fabs(std::fabs(ray3d::Dot(ray3d::Normalize(s.faces[fi].surface.frame.zAxis), axis)) - 1.0) >
          1e-9)
        return fail(Problem::PushPullVertexUnsolvable);  // a cut face, not a cap
    }

    Solid rw = s;
    // Each corner keeps its height along the axis and its angle around it, and takes the new
    // radius. Nothing translates: the whole point is that every point of the wall moves a different
    // way and the same amount.
    for (int v : wallVerts) {
      const Vec3 rel = ray3d::Sub(s.vertices[static_cast<size_t>(v)].p, w0.frame.origin);
      const double axial = ray3d::Dot(rel, axis);
      Vec3 radial = ray3d::Sub(rel, ray3d::Scale(axis, axial));
      const double rl = ray3d::Length(radial);
      if (!(rl > 1e-12))
        return fail(Problem::PushPullVertexUnsolvable);  // a corner on the axis has no direction
      radial = ray3d::Scale(radial, newRadius / rl);
      rw.vertices[static_cast<size_t>(v)].p =
          ray3d::Add(ray3d::Add(w0.frame.origin, ray3d::Scale(axis, axial)), radial);
    }
    // The cap boundary arcs grow with it. Their centres stay on the axis where they were — a cap's
    // PLANE does not move when the wall's radius changes, which is the difference between this and
    // the cap push, where the plane moves and the radius does not.
    for (Edge& e : rw.edges) {
      if (e.kind == CurveKind::Line || !onWall(e.v0) || !onWall(e.v1))
        continue;
      if (e.kind != CurveKind::Arc)
        return fail(Problem::PushPullNeighbourCurved);
      e.radius = newRadius;
    }
    for (int fi : wallFaces) {
      Surface& sf = rw.faces[static_cast<size_t>(fi)].surface;
      sf.radius = newRadius;
      sf.radius2 = newRadius;
    }

    rw.recipe = Recipe{};
    const Problem whyW = Validate(rw);
    if (whyW != Problem::Ok)
      return fail(Problem::PushPullResultInvalid);
    *out = std::move(rw);
    if (outWhy)
      *outWhy = Problem::Ok;
    return true;
  }

  if (face.surface.kind != SurfaceKind::Plane)
    return fail(Problem::PushPullFaceKindUnsupported);

  // The push direction is the face's own outward normal. `Surface::inward` flips which side the
  // material is on (REQ-314 B2a), and it has to be honoured here or a positive distance would grow
  // an inward-facing face the wrong way — the one place a sign error would look like a working
  // feature that pushes backwards.
  Vec3 dir = face.surface.frame.zAxis;
  if (face.surface.inward)
    dir = ray3d::Scale(dir, -1.0);
  const double dirLen = ray3d::Length(dir);
  if (!(dirLen > 1e-12))
    return fail(Problem::DegenerateFrame);
  dir = ray3d::Scale(dir, 1.0 / dirLen);

  std::vector<int> moved;
  CollectFaceVertices(s, face, &moved);
  if (moved.empty())
    return fail(Problem::FaceHasNoLoop);
  const auto isMoved = [&](int v) { return std::find(moved.begin(), moved.end(), v) != moved.end(); };

  // Every face that touches a moving corner must be a PLANE, because re-solving that corner means
  // intersecting the surfaces meeting there and a curved surface is not a plane to intersect. This
  // is the check ADR-046 amendment (i) is about — see the header for the measured cost of removing
  // it (a cylinder cap pushed 3 ft validates cleanly and reports a volume 15% wrong).
  //
  // Note what is NOT required any more: the neighbour does not have to be PARALLEL to the push. The
  // first version of this operation translated corners along the push, which only works when every
  // neighbour contains that direction — true of a box, false of a pyramid, whose faces are all
  // planes and none of which could be pushed at all. Re-solving lifted that restriction.
  std::vector<PushFacePlane> planesAt;  // reused per vertex
  // A CURVED neighbour is not automatically a refusal any more. A cylinder or cone wall whose axis
  // runs along the push is re-parameterised instead of intersected — its stored height grows, and a
  // cone's moving end takes the radius its own slope puts there. That is the "make this cylinder
  // taller" gesture, and it is the shape of every other curved case that is NOT supported: a wall
  // pushed sideways is a radius change, a sphere or a torus has no such parameter at all, and an
  // oblique axis is not a translation of anything.
  std::vector<int> curvedWallFaces;
  for (size_t fi = 0; fi < s.faces.size(); ++fi) {
    const Face& nb = s.faces[fi];
    if (nb.surface.kind == SurfaceKind::Plane)
      continue;
    std::vector<int> nbVerts;
    CollectFaceVertices(s, nb, &nbVerts);
    if (!std::any_of(nbVerts.begin(), nbVerts.end(), isMoved))
      continue;
    if (nb.surface.kind != SurfaceKind::Cylinder && nb.surface.kind != SurfaceKind::Cone)
      return fail(Problem::PushPullNeighbourCurved);
    const double axDot = ray3d::Dot(ray3d::Normalize(nb.surface.frame.zAxis), dir);
    if (std::fabs(std::fabs(axDot) - 1.0) > 1e-9)
      return fail(Problem::PushPullNeighbourCurved);  // pushing across the axis, not along it
    curvedWallFaces.push_back(static_cast<int>(fi));
  }

  if (!curvedWallFaces.empty()) {
    // Every wall touching the move must be the SAME wall — the halves a full cylinder is stored as
    // share one surface, differing only in their angular span. Two genuinely different walls at one
    // cap would each want their own re-parameterisation and could disagree about where the boundary
    // lands, so that is refused rather than resolved arbitrarily.
    const Surface& w0 = s.faces[static_cast<size_t>(curvedWallFaces.front())].surface;
    for (int fi : curvedWallFaces) {
      const Surface& sf = s.faces[static_cast<size_t>(fi)].surface;
      if (sf.kind != w0.kind || sf.inward != w0.inward ||
          std::fabs(sf.radius - w0.radius) > 1e-9 || std::fabs(sf.radius2 - w0.radius2) > 1e-9 ||
          std::fabs(sf.height - w0.height) > 1e-9 ||
          ray3d::Length(ray3d::Sub(sf.frame.origin, w0.frame.origin)) > 1e-9)
        return fail(Problem::PushPullNeighbourCurved);
    }
    // Every face at a moving corner must be the cap itself or one of those walls. A corner that also
    // touches an unrelated PLANE — a sliced cylinder — has both a plane constraint and a surface
    // one, and satisfying both is a different solve. Refused rather than silently ignoring one.
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      if (static_cast<int>(fi) == faceIndex ||
          std::find(curvedWallFaces.begin(), curvedWallFaces.end(), static_cast<int>(fi)) !=
              curvedWallFaces.end())
        continue;
      std::vector<int> fv;
      CollectFaceVertices(s, s.faces[fi], &fv);
      if (std::any_of(fv.begin(), fv.end(), isMoved))
        return fail(Problem::PushPullVertexUnsolvable);
    }

    CurvedWallMove wall;
    wall.axis = ray3d::Normalize(w0.frame.zAxis);
    wall.origin = w0.frame.origin;
    wall.height = w0.height;
    wall.radius = w0.radius;
    wall.radius2 = w0.radius2;
    // Which end is moving: the cap's outward normal points along the wall's axis at the top end and
    // against it at the base. `t` is the signed travel measured in the wall's own axis direction, so
    // both ends are one piece of arithmetic rather than two.
    wall.movingTopEnd = ray3d::Dot(dir, wall.axis) > 0.0;
    const double t = distance * ray3d::Dot(dir, wall.axis);
    if (!SolveCurvedWall(&wall, t))
      return fail(Problem::PushPullCurvedDegenerate);

    Solid rc = s;
    const Vec3 deltaC = ray3d::Scale(dir, distance);
    // The corners: each keeps its angular position, takes the axial coordinate of the end it is on,
    // and takes that end's NEW radius. On a cylinder the radius is unchanged and this is a pure
    // translation; on a cone the corner also moves radially, which is the taper following its slope.
    for (int v : moved) {
      const Vec3 p = s.vertices[static_cast<size_t>(v)].p;
      const Vec3 rel = ray3d::Sub(p, wall.origin);
      Vec3 radial = ray3d::Sub(rel, ray3d::Scale(wall.axis, ray3d::Dot(rel, wall.axis)));
      const double rl = ray3d::Length(radial);
      if (!(rl > 1e-12))
        return fail(Problem::PushPullVertexUnsolvable);  // a corner ON the axis has no direction
      radial = ray3d::Scale(radial, wall.movedEndRadius / rl);
      rc.vertices[static_cast<size_t>(v)].p =
          ray3d::Add(ray3d::Add(wall.newOrigin, ray3d::Scale(wall.axis, wall.movedEndAxial)), radial);
    }
    // The cap's own plane, and its boundary arcs: an arc's centre sits on the axis at the moved
    // end, and its radius is that end's radius. Both have to move or the cap's boundary stops
    // matching its own plane — the same inconsistency the plane path guards against, in the one
    // place a curve can express it.
    rc.faces[static_cast<size_t>(faceIndex)].surface.frame.origin =
        ray3d::Add(face.surface.frame.origin, deltaC);
    const Vec3 movedEndCentre =
        ray3d::Add(wall.newOrigin, ray3d::Scale(wall.axis, wall.movedEndAxial));
    for (Edge& e : rc.edges) {
      if (!isMoved(e.v0) || !isMoved(e.v1))
        continue;
      if (e.kind == CurveKind::Line)
        continue;  // a seam chord across the cap: its endpoints already moved
      if (e.kind != CurveKind::Arc)
        return fail(Problem::PushPullNeighbourCurved);
      e.frame.origin = movedEndCentre;
      e.radius = wall.movedEndRadius;
    }
    // The wall itself.
    for (int fi : curvedWallFaces) {
      Surface& sf = rc.faces[static_cast<size_t>(fi)].surface;
      sf.frame.origin = wall.newOrigin;
      sf.height = wall.newHeight;
      sf.radius = wall.newRadius;
      sf.radius2 = wall.newRadius2;
    }

    rc.recipe = Recipe{};
    const Problem whyC = Validate(rc);
    if (whyC != Problem::Ok)
      return fail(Problem::PushPullResultInvalid);
    *out = std::move(rc);
    if (outWhy)
      *outWhy = Problem::Ok;
    return true;
  }

  // The moved face's plane, translated. Every corner is then solved against THIS rather than the
  // original, which is what actually moves the face.
  const Vec3 movedOrigin = ray3d::Add(face.surface.frame.origin, ray3d::Scale(dir, distance));
  const PushFacePlane movedPlane{dir, ray3d::Dot(dir, movedOrigin)};

  Solid r = s;
  for (int v : moved) {
    // Collect the plane of every face using this vertex. The moved face contributes its NEW plane;
    // the others contribute the planes they already have, which is what keeps their own boundaries
    // on their own surfaces.
    planesAt.clear();
    planesAt.push_back(movedPlane);
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      if (static_cast<int>(fi) == faceIndex)
        continue;
      std::vector<int> fv;
      CollectFaceVertices(s, s.faces[fi], &fv);
      if (std::find(fv.begin(), fv.end(), v) == fv.end())
        continue;
      const Surface& sf = s.faces[fi].surface;
      Vec3 n = sf.frame.zAxis;
      const double nl = ray3d::Length(n);
      if (!(nl > 1e-12))
        return fail(Problem::DegenerateFrame);
      n = ray3d::Scale(n, 1.0 / nl);
      planesAt.push_back(PushFacePlane{n, ray3d::Dot(n, sf.frame.origin)});
    }
    if (planesAt.size() < 3)
      return fail(Problem::PushPullVertexUnsolvable);  // a corner needs three planes to be a point

    // Solve with the best-conditioned triple: the three whose normals span space most squarely.
    // Taking simply the first three would pick a near-degenerate triple on any vertex where two
    // faces happen to be nearly coplanar, and produce a corner far from where it belongs.
    Vec3 solved{};
    double bestDet = 0.0;
    bool found = false;
    for (size_t a = 0; a < planesAt.size(); ++a)
      for (size_t b = a + 1; b < planesAt.size(); ++b)
        for (size_t c = b + 1; c < planesAt.size(); ++c) {
          const double det = std::fabs(
              ray3d::Dot(planesAt[a].n, ray3d::Cross(planesAt[b].n, planesAt[c].n)));
          if (det <= bestDet)
            continue;
          Vec3 p;
          if (!ThreePlanePoint(planesAt[a], planesAt[b], planesAt[c], &p))
            continue;
          bestDet = det;
          solved = p;
          found = true;
        }
    if (!found)
      return fail(Problem::PushPullVertexUnsolvable);

    // Every OTHER plane at this corner must also pass through the answer. When more than three
    // faces meet — a pyramid's apex, four planes — moving one of them generally leaves no single
    // point satisfying all of them, and the corner would have to split into several. That is a
    // topology change and a different operation, so it is refused rather than approximated.
    //
    // Scale-relative, because a residual of a millimetre means something different on a 1 ft solid
    // than on one at state-plane magnitude.
    const double tol = 1e-9 * (1.0 + ray3d::Length(solved));
    for (const PushFacePlane& pe : planesAt)
      if (std::fabs(ray3d::Dot(pe.n, solved) - pe.d) > tol)
        return fail(Problem::PushPullVertexUnsolvable);

    r.vertices[static_cast<size_t>(v)].p = solved;
  }

  // The face's own plane travels with its boundary. Without this the corners would move and the
  // surface would stay — the very inconsistency the precondition above prevents on a neighbour,
  // committed on the moved face itself.
  r.faces[static_cast<size_t>(faceIndex)].surface.frame.origin = movedOrigin;

  // An ARC edge carries a centre of its own. One can only exist where a curved face does, and a
  // curved face touching the move was refused above — so an arc edge with both ends on the moved
  // face cannot occur here. Refused rather than assumed: an edge that curves and is silently left
  // behind is exactly the class of error this operation exists to avoid.
  for (const Edge& e : r.edges)
    if (e.kind != CurveKind::Line && isMoved(e.v0) && isMoved(e.v1))
      return fail(Problem::PushPullNeighbourCurved);

  // The recipe is DROPPED, not updated (REQ-319 item 6). A pushed box is not the box its recipe
  // describes, and a recipe that no longer describes its solid reads as authoritative while being
  // false. ADR-045 already made it optional and never consulted by validity, mass properties or
  // tessellation, so nothing downstream misses it.
  r.recipe = Recipe{};

  // ADR-046 (d): validate before returning, never mutate the input. A push far enough to collapse
  // or invert the solid is an ordinary user gesture, and this is what turns it into a refusal.
  const Problem why = Validate(r);
  if (why != Problem::Ok)
    return fail(Problem::PushPullResultInvalid);
  *out = std::move(r);
  if (outWhy)
    *outWhy = Problem::Ok;
  return true;
}
namespace {

/// The patch parameter `(u, v)` whose surface point is nearest \p p: a coarse grid search to pick a
/// basin, then Gauss–Newton on `|S(u,v) - p|^2` using the analytic first derivatives. Clamped to the
/// patch domain. This is what makes a face snap on a lofted surface land **on the patch** rather than
/// on the tessellator's chord (REQ-315 acceptance).
void NurbsInvertParam(const nurbs::Patch& patch, const Vec3& p, double* outU, double* outV) {
  const double u0 = nurbs::UMin(patch);
  const double u1 = nurbs::UMax(patch);
  const double v0 = nurbs::VMin(patch);
  const double v1 = nurbs::VMax(patch);
  double bu = 0.5 * (u0 + u1);
  double bv = 0.5 * (v0 + v1);
  double bd = std::numeric_limits<double>::max();
  constexpr int kGrid = 12;
  for (int i = 0; i <= kGrid; ++i)
    for (int j = 0; j <= kGrid; ++j) {
      const double u = u0 + (u1 - u0) * i / kGrid;
      const double v = v0 + (v1 - v0) * j / kGrid;
      const double d = ray3d::Length(ray3d::Sub(nurbs::Evaluate(patch, u, v), p));
      if (d < bd) {
        bd = d;
        bu = u;
        bv = v;
      }
    }
  for (int it = 0; it < 16; ++it) {
    const nurbs::SurfacePoint sp = nurbs::EvaluateWithDerivs(patch, bu, bv);
    const Vec3 r = ray3d::Sub(sp.p, p);
    const double a = ray3d::Dot(sp.du, sp.du);
    const double b = ray3d::Dot(sp.du, sp.dv);
    const double c = ray3d::Dot(sp.dv, sp.dv);
    const double det = a * c - b * b;
    if (!(std::fabs(det) > 1e-20))
      break;
    const double g1 = ray3d::Dot(r, sp.du);
    const double g2 = ray3d::Dot(r, sp.dv);
    const double du = -(c * g1 - b * g2) / det;
    const double dv = -(a * g2 - b * g1) / det;
    bu = std::clamp(bu + du, u0, u1);
    bv = std::clamp(bv + dv, v0, v1);
    if (std::fabs(du) + std::fabs(dv) < 1e-13 * (1.0 + std::fabs(bu) + std::fabs(bv)))
      break;
  }
  *outU = bu;
  *outV = bv;
}

}  // namespace

Vec3 ClosestPointOnSurface(const Surface& sf, const Vec3& p) {
  const Vec3 local = ucs::WorldToUcs(sf.frame, p);
  auto toWorld = [&sf](const Vec3& v) { return ucs::UcsToWorld(sf.frame, v); };
  // The radial direction in the frame's XY plane. Degenerate exactly on the axis, which is the one
  // input for which "nearest point" has no single answer.
  const double rho = std::sqrt(local.x * local.x + local.y * local.y);

  switch (sf.kind) {
  case SurfaceKind::Plane:
    return toWorld(Vec3{local.x, local.y, 0.0});
  case SurfaceKind::Cylinder: {
    if (!(rho > 1e-12))
      return p;
    const double k = sf.radius / rho;
    return toWorld(Vec3{local.x * k, local.y * k, local.z});
  }
  case SurfaceKind::Cone: {
    if (!(rho > 1e-12))
      return p;
    // Work in the (rho, z) half-plane, where the cone is the straight segment from (r0, 0) to
    // (r1, h) — so this is a point-to-line projection, and the taper is handled by the same
    // arithmetic that handles a cylinder rather than by a special case.
    const Vec3 a{sf.radius, 0.0, 0.0};
    const Vec3 b{sf.radius2, 0.0, sf.height};
    const Vec3 ab = ray3d::Sub(b, a);
    const double denom = ray3d::Dot(ab, ab);
    if (!(denom > 1e-24))
      return p;
    const Vec3 ap{rho - a.x, 0.0, local.z - a.z};
    const double t = ray3d::Dot(ap, ab) / denom;
    const double rhoOn = a.x + ab.x * t;
    const double zOn = a.z + ab.z * t;
    const double k = rhoOn / rho;
    return toWorld(Vec3{local.x * k, local.y * k, zOn});
  }
  case SurfaceKind::Sphere: {
    const double len = ray3d::Length(local);
    if (!(len > 1e-12))
      return p;
    return toWorld(ray3d::Scale(local, sf.radius / len));
  }
  case SurfaceKind::Torus: {
    if (!(rho > 1e-12))
      return p;  // on the axis: every point of the ring is equidistant
    // Walk to the tube's centre circle first, then out along the tube.
    const Vec3 ring{local.x * (sf.radius / rho), local.y * (sf.radius / rho), 0.0};
    const Vec3 out = ray3d::Sub(local, ring);
    const double outLen = ray3d::Length(out);
    if (!(outLen > 1e-12))
      return p;  // exactly on the tube's centre circle
    return toWorld(ray3d::Add(ring, ray3d::Scale(out, sf.radius2 / outLen)));
  }
  case SurfaceKind::Nurbs: {
    double u = 0.0;
    double v = 0.0;
    NurbsInvertParam(sf.patch, p, &u, &v);
    return nurbs::Evaluate(sf.patch, u, v);
  }
  }
  return p;
}

Vec3 ClosestPointOnEdge(const Solid& s, const Edge& e, const Vec3& p) {
  if (e.kind == CurveKind::Intersection) {
    const std::vector<Vec3> poly = MarchIntersectionCurve(s, e, 256);
    Vec3 best = poly.empty() ? Vec3{} : poly.front();
    double bestD = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
      const Vec3 ab = ray3d::Sub(poly[i + 1], poly[i]);
      const double denom = ray3d::Dot(ab, ab);
      const double u = denom > 1e-24 ? std::clamp(ray3d::Dot(ray3d::Sub(p, poly[i]), ab) / denom, 0.0, 1.0) : 0.0;
      const Vec3 c = ray3d::Add(poly[i], ray3d::Scale(ab, u));
      const double d = ray3d::Length(ray3d::Sub(c, p));
      if (d < bestD) {
        bestD = d;
        best = c;
      }
    }
    return best;
  }
  if (e.kind == CurveKind::Line) {
    const Vec3& a = s.vertices[static_cast<std::size_t>(e.v0)].p;
    const Vec3& b = s.vertices[static_cast<std::size_t>(e.v1)].p;
    const Vec3 ab = ray3d::Sub(b, a);
    const double denom = ray3d::Dot(ab, ab);
    if (!(denom > 1e-24))
      return a;
    const double t = std::clamp(ray3d::Dot(ray3d::Sub(p, a), ab) / denom, 0.0, 1.0);
    return ray3d::Add(a, ray3d::Scale(ab, t));
  }
  if (e.kind == CurveKind::Ellipse) {
    // No closed form for the nearest point on an ellipse; sample the swept range and polish the best
    // sample with a few Newton steps on d/dt |E(t) - p|^2. Clamped to the edge's own extent.
    const double t0 = EllipseParamOf(e, s.vertices[static_cast<std::size_t>(e.v0)].p);
    auto at = [&](double th) {
      return ray3d::Add(e.frame.origin,
                        ray3d::Add(ray3d::Scale(e.frame.xAxis, e.radius * std::cos(th)),
                                   ray3d::Scale(e.frame.yAxis, e.radius2 * std::sin(th))));
    };
    double best = 0.0;
    double bestD = std::numeric_limits<double>::max();
    for (int i = 0; i <= 64; ++i) {
      const double u = static_cast<double>(i) / 64.0;
      const double d = ray3d::Length(ray3d::Sub(at(t0 + e.sweep * u), p));
      if (d < bestD) {
        bestD = d;
        best = u;
      }
    }
    double u = best;
    for (int it = 0; it < 8; ++it) {
      const double th = t0 + e.sweep * u;
      const Vec3 E = at(th);
      const Vec3 dE = ray3d::Add(ray3d::Scale(e.frame.xAxis, -e.radius * std::sin(th)),
                                 ray3d::Scale(e.frame.yAxis, e.radius2 * std::cos(th)));
      const Vec3 ddE = ray3d::Add(ray3d::Scale(e.frame.xAxis, -e.radius * std::cos(th)),
                                  ray3d::Scale(e.frame.yAxis, -e.radius2 * std::sin(th)));
      const Vec3 r = ray3d::Sub(E, p);
      const double g = ray3d::Dot(r, dE) * e.sweep;
      const double h = (ray3d::Dot(dE, dE) + ray3d::Dot(r, ddE)) * e.sweep * e.sweep;
      if (!(std::fabs(h) > 1e-18))
        break;
      u = std::clamp(u - g / h, 0.0, 1.0);
    }
    return at(t0 + e.sweep * u);
  }
  // An arc: drop onto its plane, take the angle there, and if that angle is outside the swept range,
  // answer with whichever END is nearer **round the circle**.
  //
  // A plain `clamp` on the raw `atan2` result is wrong and quietly so, which is worth spelling out
  // because it is what this function did until a review caught it. `atan2` returns (-pi, pi], so for
  // a half-arc spanning [0, pi] a probe at -2.0 rad is 2.0 rad from the start and only 1.14 rad from
  // the end — but it clamps to the start, because -2.0 is simply the smaller number. The answer is
  // still ON the arc, which is why nothing crashed and why a test that only checked "is it on the
  // arc" passed: it is just the wrong end of it.
  //
  // Measuring the angle FORWARD from the start, in the sweep's own direction, removes the branch cut
  // entirely — both senses then share one comparison, and a full-circle edge (sweep = 2*pi) falls out
  // as the case where nothing is ever outside.
  const ucs::Point2D flat = ucs::WorldToPlane(e.frame, p);
  if (!(std::fabs(flat.x) > 1e-12 || std::fabs(flat.y) > 1e-12))
    return EdgePointAt(s, e, 0.0);  // on the centre: no angle is defined
  const double angle = std::atan2(flat.y, flat.x);
  const double span = std::fabs(e.sweep);
  const bool forward = e.sweep >= 0.0;

  double t = std::fmod(forward ? angle : -angle, kTwoPi);
  if (t < 0.0)
    t += kTwoPi;  // now in [0, 2*pi): how far round from the start, the way the arc runs

  double param = t;
  if (t > span) {
    // Outside the sweep. Two gaps: past the end, and back round to the start. Nearer wins.
    param = (t - span <= kTwoPi - t) ? span : 0.0;
  }
  return ucs::PointOnPlaneCircle(e.frame, e.radius, forward ? param : -param);
}

// ---------------------------------------------------------------------------------------------
// The seven primitives.
// ---------------------------------------------------------------------------------------------

bool MakeBox(const ucs::Ucs& frame, double length, double width, double height, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({length, width, height}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(length > 0.0))
    return Fail(Problem::NonPositiveLength, outWhy);
  if (!(width > 0.0))
    return Fail(Problem::NonPositiveWidth, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);

  const double hx = length * 0.5;
  const double hy = width * 0.5;
  const double h = height;

  Solid s;
  const int v0 = AddVertex(&s, Vec3{-hx, -hy, 0.0});
  const int v1 = AddVertex(&s, Vec3{hx, -hy, 0.0});
  const int v2 = AddVertex(&s, Vec3{hx, hy, 0.0});
  const int v3 = AddVertex(&s, Vec3{-hx, hy, 0.0});
  const int v4 = AddVertex(&s, Vec3{-hx, -hy, h});
  const int v5 = AddVertex(&s, Vec3{hx, -hy, h});
  const int v6 = AddVertex(&s, Vec3{hx, hy, h});
  const int v7 = AddVertex(&s, Vec3{-hx, hy, h});

  const int b0 = AddLine(&s, v0, v1);
  const int b1 = AddLine(&s, v1, v2);
  const int b2 = AddLine(&s, v2, v3);
  const int b3 = AddLine(&s, v3, v0);
  const int t0 = AddLine(&s, v4, v5);
  const int t1 = AddLine(&s, v5, v6);
  const int t2 = AddLine(&s, v6, v7);
  const int t3 = AddLine(&s, v7, v4);
  const int p0 = AddLine(&s, v0, v4);
  const int p1 = AddLine(&s, v1, v5);
  const int p2 = AddLine(&s, v2, v6);
  const int p3 = AddLine(&s, v3, v7);

  s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, -1.0},
                                  {{b3, true}, {b2, true}, {b1, true}, {b0, true}}));
  s.faces.push_back(
      MakePlaneFace(Vec3{0.0, 0.0, h}, Vec3{0.0, 0.0, 1.0}, {{t0, false}, {t1, false}, {t2, false}, {t3, false}}));
  s.faces.push_back(MakePlaneFace(Vec3{0.0, -hy, 0.0}, Vec3{0.0, -1.0, 0.0},
                                  {{b0, false}, {p1, false}, {t0, true}, {p0, true}}));
  s.faces.push_back(MakePlaneFace(Vec3{hx, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0},
                                  {{b1, false}, {p2, false}, {t1, true}, {p1, true}}));
  s.faces.push_back(MakePlaneFace(Vec3{0.0, hy, 0.0}, Vec3{0.0, 1.0, 0.0},
                                  {{b2, false}, {p3, false}, {t2, true}, {p2, true}}));
  s.faces.push_back(MakePlaneFace(Vec3{-hx, 0.0, 0.0}, Vec3{-1.0, 0.0, 0.0},
                                  {{b3, false}, {p0, false}, {t3, true}, {p3, true}}));

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Box;
  s.recipe.length = length;
  s.recipe.width = width;
  s.recipe.height = height;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

bool MakeWedge(const ucs::Ucs& frame, double length, double width, double height, Solid* out,
               Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({length, width, height}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(length > 0.0))
    return Fail(Problem::NonPositiveLength, outWhy);
  if (!(width > 0.0))
    return Fail(Problem::NonPositiveWidth, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);

  const double hx = length * 0.5;
  const double hy = width * 0.5;
  const double h = height;

  Solid s;
  const int v0 = AddVertex(&s, Vec3{-hx, -hy, 0.0});
  const int v1 = AddVertex(&s, Vec3{hx, -hy, 0.0});
  const int v2 = AddVertex(&s, Vec3{hx, hy, 0.0});
  const int v3 = AddVertex(&s, Vec3{-hx, hy, 0.0});
  const int v4 = AddVertex(&s, Vec3{-hx, -hy, h});
  const int v5 = AddVertex(&s, Vec3{-hx, hy, h});

  const int b0 = AddLine(&s, v0, v1);
  const int b1 = AddLine(&s, v1, v2);
  const int b2 = AddLine(&s, v2, v3);
  const int b3 = AddLine(&s, v3, v0);
  const int ridge = AddLine(&s, v4, v5);
  const int u0 = AddLine(&s, v0, v4);  // vertical, y = -hy
  const int u1 = AddLine(&s, v3, v5);  // vertical, y = +hy
  const int g0 = AddLine(&s, v1, v4);  // slant, y = -hy
  const int g1 = AddLine(&s, v2, v5);  // slant, y = +hy

  s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, -1.0},
                                  {{b3, true}, {b2, true}, {b1, true}, {b0, true}}));
  // The sloping face. Its outward normal leans out and up, away from the ridge.
  {
    const Vec3 n = ray3d::Normalize(Vec3{h, 0.0, 2.0 * hx});
    s.faces.push_back(
        MakePlaneFace(Vec3{hx, 0.0, 0.0}, n, {{b1, false}, {g1, false}, {ridge, true}, {g0, true}}));
  }
  s.faces.push_back(MakePlaneFace(Vec3{-hx, 0.0, 0.0}, Vec3{-1.0, 0.0, 0.0},
                                  {{b3, false}, {u0, false}, {ridge, false}, {u1, true}}));
  s.faces.push_back(
      MakePlaneFace(Vec3{0.0, -hy, 0.0}, Vec3{0.0, -1.0, 0.0}, {{b0, false}, {g0, false}, {u0, true}}));
  s.faces.push_back(
      MakePlaneFace(Vec3{0.0, hy, 0.0}, Vec3{0.0, 1.0, 0.0}, {{b2, false}, {u1, false}, {g1, true}}));

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Wedge;
  s.recipe.length = length;
  s.recipe.width = width;
  s.recipe.height = height;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

bool MakePyramid(const ucs::Ucs& frame, int sides, double baseRadius, double topRadius, double height,
                 Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({baseRadius, topRadius, height}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (sides < 3 || sides > kMaxPyramidSides)
    return Fail(Problem::SideCountOutOfRange, outWhy);
  if (!(baseRadius > 0.0))
    return Fail(Problem::NonPositiveRadius, outWhy);
  if (topRadius < 0.0)
    return Fail(Problem::NegativeTopRadius, outWhy);
  if (topRadius >= baseRadius)
    return Fail(Problem::TopRadiusNotBelowBase, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);

  const bool apex = !(topRadius > 0.0);
  const int n = sides;

  Solid s;
  std::vector<int> base(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double a = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
    base[static_cast<std::size_t>(i)] =
        AddVertex(&s, Vec3{baseRadius * std::cos(a), baseRadius * std::sin(a), 0.0});
  }
  std::vector<int> top;
  int apexV = -1;
  if (apex) {
    apexV = AddVertex(&s, Vec3{0.0, 0.0, height});
  } else {
    top.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      const double a = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      top[static_cast<std::size_t>(i)] =
          AddVertex(&s, Vec3{topRadius * std::cos(a), topRadius * std::sin(a), height});
    }
  }

  std::vector<int> baseEdge(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    baseEdge[static_cast<std::size_t>(i)] =
        AddLine(&s, base[static_cast<std::size_t>(i)], base[static_cast<std::size_t>((i + 1) % n)]);

  std::vector<int> topEdge;
  std::vector<int> sideEdge(static_cast<std::size_t>(n));
  if (apex) {
    for (int i = 0; i < n; ++i)
      sideEdge[static_cast<std::size_t>(i)] = AddLine(&s, base[static_cast<std::size_t>(i)], apexV);
  } else {
    topEdge.resize(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
      topEdge[static_cast<std::size_t>(i)] =
          AddLine(&s, top[static_cast<std::size_t>(i)], top[static_cast<std::size_t>((i + 1) % n)]);
    for (int i = 0; i < n; ++i)
      sideEdge[static_cast<std::size_t>(i)] =
          AddLine(&s, base[static_cast<std::size_t>(i)], top[static_cast<std::size_t>(i)]);
  }

  // Base, wound clockwise as seen from +Z so that it is counter-clockwise about its own -Z normal.
  {
    std::vector<EdgeUse> uses;
    uses.reserve(static_cast<std::size_t>(n));
    for (int i = n - 1; i >= 0; --i)
      uses.push_back(EdgeUse{baseEdge[static_cast<std::size_t>(i)], true});
    s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, -1.0}, std::move(uses)));
  }

  if (!apex) {
    std::vector<EdgeUse> uses;
    uses.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
      uses.push_back(EdgeUse{topEdge[static_cast<std::size_t>(i)], false});
    s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, height}, Vec3{0.0, 0.0, 1.0}, std::move(uses)));
  }

  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    const Vec3& a = s.vertices[static_cast<std::size_t>(base[static_cast<std::size_t>(i)])].p;
    const Vec3& b = s.vertices[static_cast<std::size_t>(base[static_cast<std::size_t>(j)])].p;
    const Vec3& c = apex ? s.vertices[static_cast<std::size_t>(apexV)].p
                         : s.vertices[static_cast<std::size_t>(top[static_cast<std::size_t>(j)])].p;
    const Vec3 nrm = ray3d::Normalize(ray3d::Cross(ray3d::Sub(b, a), ray3d::Sub(c, a)));
    std::vector<EdgeUse> uses;
    if (apex) {
      uses = {EdgeUse{baseEdge[static_cast<std::size_t>(i)], false},
              EdgeUse{sideEdge[static_cast<std::size_t>(j)], false},
              EdgeUse{sideEdge[static_cast<std::size_t>(i)], true}};
    } else {
      uses = {EdgeUse{baseEdge[static_cast<std::size_t>(i)], false},
              EdgeUse{sideEdge[static_cast<std::size_t>(j)], false},
              EdgeUse{topEdge[static_cast<std::size_t>(i)], true},
              EdgeUse{sideEdge[static_cast<std::size_t>(i)], true}};
    }
    s.faces.push_back(MakePlaneFace(a, nrm, std::move(uses)));
  }

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Pyramid;
  s.recipe.sides = sides;
  s.recipe.radius = baseRadius;
  s.recipe.radius2 = topRadius;
  s.recipe.height = height;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

namespace {

/// Cylinder and cone differ only in their top radius and in which `SurfaceKind` the side carries,
/// so one builder serves both. Splitting the rims at a seam (rather than leaving a full-circle
/// edge) is what makes every edge bound exactly two faces, which is the invariant `Validate` leans
/// on hardest.
[[nodiscard]] bool BuildConical(const ucs::Ucs& frame, double r0, double r1, double h, bool asCylinder,
                                Solid* out, Problem* outWhy) {
  const bool apex = !(r1 > 0.0);

  Solid s;
  const int b0 = AddVertex(&s, Vec3{r0, 0.0, 0.0});
  const int b1 = AddVertex(&s, Vec3{-r0, 0.0, 0.0});
  int t0 = -1;
  int t1 = -1;
  int apexV = -1;
  if (apex) {
    apexV = AddVertex(&s, Vec3{0.0, 0.0, h});
  } else {
    t0 = AddVertex(&s, Vec3{r1, 0.0, h});
    t1 = AddVertex(&s, Vec3{-r1, 0.0, h});
  }

  const Vec3 up{0.0, 0.0, 1.0};
  const Vec3 baseCentre{0.0, 0.0, 0.0};
  const int rb0 = AddArc(&s, b0, b1, baseCentre, up, kPi);
  const int rb1 = AddArc(&s, b1, b0, baseCentre, up, kPi);

  int rt0 = -1;
  int rt1 = -1;
  int sm0 = -1;
  int sm1 = -1;
  if (apex) {
    sm0 = AddLine(&s, b0, apexV);
    sm1 = AddLine(&s, b1, apexV);
  } else {
    const Vec3 topCentre{0.0, 0.0, h};
    rt0 = AddArc(&s, t0, t1, topCentre, up, kPi);
    rt1 = AddArc(&s, t1, t0, topCentre, up, kPi);
    sm0 = AddLine(&s, b0, t0);
    sm1 = AddLine(&s, b1, t1);
  }

  // Base cap: counter-clockwise about -Z means clockwise about +Z, so both rim arcs run reversed.
  s.faces.push_back(
      MakePlaneFace(baseCentre, Vec3{0.0, 0.0, -1.0}, {{rb1, true}, {rb0, true}}));
  if (!apex)
    s.faces.push_back(
        MakePlaneFace(Vec3{0.0, 0.0, h}, Vec3{0.0, 0.0, 1.0}, {{rt0, false}, {rt1, false}}));

  auto sideFace = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = asCylinder ? SurfaceKind::Cylinder : SurfaceKind::Cone;
    f.surface.frame = ucs::Ucs{};  // the canonical local frame; PlaceInFrame maps it
    f.surface.radius = r0;
    f.surface.radius2 = r1;
    f.surface.height = h;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };

  if (apex) {
    sideFace(0.0, kPi, {{rb0, false}, {sm1, false}, {sm0, true}});
    sideFace(kPi, kTwoPi, {{rb1, false}, {sm0, false}, {sm1, true}});
  } else {
    sideFace(0.0, kPi, {{rb0, false}, {sm1, false}, {rt0, true}, {sm0, true}});
    sideFace(kPi, kTwoPi, {{rb1, false}, {sm0, false}, {rt1, true}, {sm1, true}});
  }

  AddSingleShell(&s);
  s.recipe.kind = asCylinder ? PrimitiveKind::Cylinder : PrimitiveKind::Cone;
  s.recipe.radius = r0;
  s.recipe.radius2 = r1;
  s.recipe.height = h;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

} // namespace

bool MakeCylinder(const ucs::Ucs& frame, double radius, double height, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({radius, height}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(radius > 0.0))
    return Fail(Problem::NonPositiveRadius, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);
  return BuildConical(frame, radius, radius, height, /*asCylinder=*/true, out, outWhy);
}

bool MakeCone(const ucs::Ucs& frame, double baseRadius, double topRadius, double height, Solid* out,
              Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({baseRadius, topRadius, height}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(baseRadius > 0.0))
    return Fail(Problem::NonPositiveRadius, outWhy);
  if (topRadius < 0.0)
    return Fail(Problem::NegativeTopRadius, outWhy);
  if (topRadius >= baseRadius)
    return Fail(Problem::TopRadiusNotBelowBase, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);
  return BuildConical(frame, baseRadius, topRadius, height, /*asCylinder=*/false, out, outWhy);
}

bool MakeSphere(const ucs::Ucs& frame, double radius, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({radius}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(radius > 0.0))
    return Fail(Problem::NonPositiveRadius, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);

  Solid s;
  const int south = AddVertex(&s, Vec3{0.0, 0.0, -radius});
  const int north = AddVertex(&s, Vec3{0.0, 0.0, radius});

  // The two half-meridians that seam the sphere. Their normals are chosen so that sweeping +pi
  // from the south pole runs through the equator at longitude 0 and pi respectively.
  const Vec3 centre{0.0, 0.0, 0.0};
  const int m0 = AddArc(&s, south, north, centre, Vec3{0.0, -1.0, 0.0}, kPi);
  const int m1 = AddArc(&s, south, north, centre, Vec3{0.0, 1.0, 0.0}, kPi);

  auto half = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Sphere;
    f.surface.radius = radius;
    f.uStart = u0;
    f.uEnd = u1;
    f.vStart = -kHalfPi;
    f.vEnd = kHalfPi;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  half(0.0, kPi, {{m1, false}, {m0, true}});
  half(kPi, kTwoPi, {{m0, false}, {m1, true}});

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Sphere;
  s.recipe.radius = radius;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

bool MakeTorus(const ucs::Ucs& frame, double majorRadius, double minorRadius, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({majorRadius, minorRadius}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(majorRadius > 0.0) || !(minorRadius > 0.0))
    return Fail(Problem::NonPositiveRadius, outWhy);
  // A tube LARGER than the ring is allowed, and self-intersects — the shape AutoCAD builds and that
  // users draw deliberately (ADR-045 (f) as amended). Only the EXACTLY equal case is refused: there
  // the inner equator collapses to a point, both inner rim edges have zero radius, and the result is
  // not a solid at all. `Validate` would reject it a moment later as a degenerate edge, so it is
  // refused here by name instead of by a symptom.
  if (minorRadius == majorRadius)
    return Fail(Problem::MinorRadiusEqualsMajor, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);

  const double R = majorRadius;
  const double r = minorRadius;

  Solid s;
  // Vertices at the four corners of the (t, v) cut pattern: t in {0, pi}, v in {0, pi}.
  const int v00 = AddVertex(&s, Vec3{R + r, 0.0, 0.0});
  const int v0p = AddVertex(&s, Vec3{R - r, 0.0, 0.0});
  const int vp0 = AddVertex(&s, Vec3{-(R + r), 0.0, 0.0});
  const int vpp = AddVertex(&s, Vec3{-(R - r), 0.0, 0.0});

  const Vec3 up{0.0, 0.0, 1.0};
  const Vec3 origin{0.0, 0.0, 0.0};
  // Rings around the axis, at the outer (v = 0) and inner (v = pi) equators.
  const int e1 = AddArc(&s, v00, vp0, origin, up, kPi);
  const int e2 = AddArc(&s, vp0, v00, origin, up, kPi);
  const int e3 = AddArc(&s, v0p, vpp, origin, up, kPi);
  const int e4 = AddArc(&s, vpp, v0p, origin, up, kPi);
  // Rings around the tube, at t = 0 and t = pi. The normals are the ones for which increasing v
  // sweeps counter-clockwise, which is what keeps the tube ring's winding and the surface's own
  // (t, v) parametrisation in agreement.
  const Vec3 tube0Centre{R, 0.0, 0.0};
  const Vec3 tubePCentre{-R, 0.0, 0.0};
  const int e5 = AddArc(&s, v00, v0p, tube0Centre, Vec3{0.0, -1.0, 0.0}, kPi);
  const int e6 = AddArc(&s, v0p, v00, tube0Centre, Vec3{0.0, -1.0, 0.0}, kPi);
  const int e7 = AddArc(&s, vp0, vpp, tubePCentre, Vec3{0.0, 1.0, 0.0}, kPi);
  const int e8 = AddArc(&s, vpp, vp0, tubePCentre, Vec3{0.0, 1.0, 0.0}, kPi);

  auto patch = [&](double u0, double u1, double vs, double ve, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Torus;
    f.surface.radius = R;
    f.surface.radius2 = r;
    f.uStart = u0;
    f.uEnd = u1;
    f.vStart = vs;
    f.vEnd = ve;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  patch(0.0, kPi, 0.0, kPi, {{e1, false}, {e7, false}, {e3, true}, {e5, true}});
  patch(kPi, kTwoPi, 0.0, kPi, {{e2, false}, {e5, false}, {e4, true}, {e7, true}});
  patch(0.0, kPi, kPi, kTwoPi, {{e3, false}, {e8, false}, {e1, true}, {e6, true}});
  patch(kPi, kTwoPi, kPi, kTwoPi, {{e4, false}, {e6, false}, {e2, true}, {e8, true}});

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Torus;
  s.recipe.radius = R;
  s.recipe.radius2 = r;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

// ---------------------------------------------------------------------------------------------
// Feature operations — Extrude (REQ-314 / ADR-046 increment 1, GitHub issue #147).
// ---------------------------------------------------------------------------------------------

namespace {

/// In-plane distance from \p p to \p centre, both already on \p plane.
[[nodiscard]] double InPlaneRadius(const ucs::Ucs& plane, const Vec3& centre, const Vec3& p) {
  const ucs::Point2D c = ucs::WorldToPlane(plane, centre);
  const ucs::Point2D q = ucs::WorldToPlane(plane, p);
  return std::sqrt((q.x - c.x) * (q.x - c.x) + (q.y - c.y) * (q.y - c.y));
}

/// Signed area of the profile in its own plane, about `plane.zAxis`: the shoelace term for every
/// edge plus, for an arc, the signed bulge between its chord and itself — the same decomposition
/// \ref PlaneLoopSignedArea uses on a finished face.
[[nodiscard]] double ProfilePlaneSignedArea(const Profile& pr) {
  const ucs::Ucs& fr = pr.plane;
  const int n = static_cast<int>(pr.vertices.size());
  double acc = 0.0;
  for (int i = 0; i < n; ++i) {
    const ucs::Point2D a = ucs::WorldToPlane(fr, pr.vertices[static_cast<std::size_t>(i)]);
    const ucs::Point2D b = ucs::WorldToPlane(fr, pr.vertices[static_cast<std::size_t>((i + 1) % n)]);
    acc += 0.5 * (a.x * b.y - b.x * a.y);
    const ProfileEdge& pe = pr.edges[static_cast<std::size_t>(i)];
    if (pe.arc) {
      const double r = InPlaneRadius(fr, pe.centre, pr.vertices[static_cast<std::size_t>(i)]);
      acc += 0.5 * r * r * (pe.sweep - std::sin(pe.sweep));
    }
  }
  return acc;
}

/// A cheap self-intersection screen: do any two non-adjacent profile CHORDS cross? It misses an
/// overlap that only the arc bulges create — which is why \ref Validate still gates the result — but
/// it turns the common figure-eight into a clear message rather than a puzzling topology error.
[[nodiscard]] bool ProfileChordsCross(const Profile& pr) {
  const ucs::Ucs& fr = pr.plane;
  const int n = static_cast<int>(pr.vertices.size());
  auto pt = [&](int i) { return ucs::WorldToPlane(fr, pr.vertices[static_cast<std::size_t>(i % n)]); };
  auto cr = [](const ucs::Point2D& o, const ucs::Point2D& a, const ucs::Point2D& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
  };
  auto segCross = [&](const ucs::Point2D& p1, const ucs::Point2D& p2, const ucs::Point2D& p3,
                      const ucs::Point2D& p4) {
    return ((cr(p3, p4, p1) > 0.0) != (cr(p3, p4, p2) > 0.0)) &&
           ((cr(p1, p2, p3) > 0.0) != (cr(p1, p2, p4) > 0.0));
  };
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if ((i + 1) % n == j || (j + 1) % n == i)
        continue;  // adjacent chords legitimately share a vertex
      if (segCross(pt(i), pt(i + 1), pt(j), pt(j + 1)))
        return true;
    }
  }
  return false;
}

} // namespace

bool Extrude(const Profile& profile, double distance, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason
  if (!std::isfinite(distance) || distance == 0.0)
    return Fail(Problem::NonPositiveDistance, outWhy);

  const int n = static_cast<int>(profile.vertices.size());
  if (n != static_cast<int>(profile.edges.size()))
    return Fail(Problem::ProfileMalformed, outWhy);
  if (n < 2)
    return Fail(Problem::ProfileTooFewEdges, outWhy);
  if (!FrameOk(profile.plane))
    return Fail(Problem::DegenerateFrame, outWhy);

  const ucs::Ucs& pl = profile.plane;

  // Model scale from the profile's own extent, so every tolerance below is relative.
  ucs::Point2D lo = ucs::WorldToPlane(pl, profile.vertices[0]);
  ucs::Point2D hi = lo;
  for (const Vec3& v : profile.vertices) {
    if (!FinitePoint(v))
      return Fail(Problem::NonFiniteCoordinate, outWhy);
    const ucs::Point2D q = ucs::WorldToPlane(pl, v);
    lo.x = std::min(lo.x, q.x);
    lo.y = std::min(lo.y, q.y);
    hi.x = std::max(hi.x, q.x);
    hi.y = std::max(hi.y, q.y);
  }
  const double scale = std::max({hi.x - lo.x, hi.y - lo.y, std::fabs(distance), 1e-9});
  const double planeEps = 1e-6 * scale;
  const double lenEps = 1e-9 * scale;

  for (const Vec3& v : profile.vertices) {
    if (std::fabs(ucs::SignedDistanceToPlane(pl, v)) > planeEps)
      return Fail(Problem::ProfilePointOffPlane, outWhy);
  }
  for (int i = 0; i < n; ++i) {
    const ProfileEdge& pe = profile.edges[static_cast<std::size_t>(i)];
    if (!pe.arc)
      continue;
    if (!FinitePoint(pe.centre) || !std::isfinite(pe.sweep))
      return Fail(Problem::NonFiniteCoordinate, outWhy);
    if (std::fabs(ucs::SignedDistanceToPlane(pl, pe.centre)) > planeEps)
      return Fail(Problem::ProfilePointOffPlane, outWhy);
    if (!(std::fabs(pe.sweep) > 1e-9) || std::fabs(pe.sweep) >= kTwoPi)
      return Fail(Problem::DegenerateEdge, outWhy);
    const double r0 = InPlaneRadius(pl, pe.centre, profile.vertices[static_cast<std::size_t>(i)]);
    const double r1 =
        InPlaneRadius(pl, pe.centre, profile.vertices[static_cast<std::size_t>((i + 1) % n)]);
    if (!(r0 > lenEps))
      return Fail(Problem::DegenerateEdge, outWhy);
    if (std::fabs(r0 - r1) > 1e-6 * scale)
      return Fail(Problem::ProfileArcRadiusMismatch, outWhy);
  }
  if (ProfileChordsCross(profile))
    return Fail(Problem::ProfileSelfIntersects, outWhy);

  const double areaZ = ProfilePlaneSignedArea(profile);
  if (std::fabs(areaZ) <= lenEps * lenEps)
    return Fail(Problem::ProfileSelfIntersects, outWhy);  // no enclosed area — not a usable loop

  // Extrusion direction, and whether the walk must be reversed to run CCW about it.
  const double sgn = distance > 0.0 ? 1.0 : -1.0;
  const Vec3 up = ray3d::Scale(pl.zAxis, sgn);
  const double dist = std::fabs(distance);
  const bool rev = (areaZ * sgn) < 0.0;

  // Walk order W[], and each edge's sweep re-expressed about `up`.
  std::vector<Vec3> W(static_cast<std::size_t>(n));
  std::vector<ProfileEdge> E(static_cast<std::size_t>(n));
  for (int k = 0; k < n; ++k) {
    if (!rev) {
      W[static_cast<std::size_t>(k)] = profile.vertices[static_cast<std::size_t>(k)];
      E[static_cast<std::size_t>(k)] = profile.edges[static_cast<std::size_t>(k)];
      E[static_cast<std::size_t>(k)].sweep *= sgn;
    } else {
      W[static_cast<std::size_t>(k)] = profile.vertices[static_cast<std::size_t>((n - k) % n)];
      const ProfileEdge& src = profile.edges[static_cast<std::size_t>((2 * n - k - 1) % n)];
      E[static_cast<std::size_t>(k)] = src;
      E[static_cast<std::size_t>(k)].sweep = -src.sweep * sgn;
    }
  }
  // A REFLEX arc — one curving into the loop rather than out of it — used to be refused here, by a
  // `Problem::ProfileArcReflex` that no longer exists. It is built now: after the walk above, the
  // loop runs CCW about `up`, so an arc whose sweep is still positive has its centre on the INTERIOR
  // side and sweeps an ordinary outward-facing cylinder, while a negative one has its centre outside
  // the loop and sweeps a face whose material is on the far side from its own axis. That is what
  // `Surface::inward` was added for in B2a, and this is ADR-046 (d)'s own "separate feature, now
  // unblocked" being taken up. See the side-face loop below, which is the only place that changes.

  Solid s;
  std::vector<int> baseV(static_cast<std::size_t>(n));
  std::vector<int> topV(static_cast<std::size_t>(n));
  for (int k = 0; k < n; ++k) {
    baseV[static_cast<std::size_t>(k)] = AddVertex(&s, W[static_cast<std::size_t>(k)]);
    topV[static_cast<std::size_t>(k)] =
        AddVertex(&s, ray3d::Add(W[static_cast<std::size_t>(k)], ray3d::Scale(up, dist)));
  }
  std::vector<int> baseE(static_cast<std::size_t>(n));
  std::vector<int> topE(static_cast<std::size_t>(n));
  std::vector<int> vert(static_cast<std::size_t>(n));
  for (int k = 0; k < n; ++k) {
    const int k1 = (k + 1) % n;
    const ProfileEdge& pe = E[static_cast<std::size_t>(k)];
    if (pe.arc) {
      baseE[static_cast<std::size_t>(k)] = AddArc(&s, baseV[static_cast<std::size_t>(k)],
                                                 baseV[static_cast<std::size_t>(k1)], pe.centre, up, pe.sweep);
      topE[static_cast<std::size_t>(k)] =
          AddArc(&s, topV[static_cast<std::size_t>(k)], topV[static_cast<std::size_t>(k1)],
                 ray3d::Add(pe.centre, ray3d::Scale(up, dist)), up, pe.sweep);
    } else {
      baseE[static_cast<std::size_t>(k)] =
          AddLine(&s, baseV[static_cast<std::size_t>(k)], baseV[static_cast<std::size_t>(k1)]);
      topE[static_cast<std::size_t>(k)] =
          AddLine(&s, topV[static_cast<std::size_t>(k)], topV[static_cast<std::size_t>(k1)]);
    }
    vert[static_cast<std::size_t>(k)] =
        AddLine(&s, baseV[static_cast<std::size_t>(k)], topV[static_cast<std::size_t>(k)]);
  }

  // Bottom cap: outward normal -up; CCW about it is CW about up, i.e. the walk backwards, reversed.
  {
    std::vector<EdgeUse> uses;
    uses.reserve(static_cast<std::size_t>(n));
    for (int k = n - 1; k >= 0; --k)
      uses.push_back(EdgeUse{baseE[static_cast<std::size_t>(k)], true});
    s.faces.push_back(MakePlaneFace(pl.origin, ray3d::Scale(up, -1.0), std::move(uses)));
  }
  // Top cap: outward normal +up; CCW about it is the walk forwards.
  {
    std::vector<EdgeUse> uses;
    uses.reserve(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k)
      uses.push_back(EdgeUse{topE[static_cast<std::size_t>(k)], false});
    s.faces.push_back(
        MakePlaneFace(ray3d::Add(pl.origin, ray3d::Scale(up, dist)), up, std::move(uses)));
  }
  // Side faces: one per profile edge. A straight edge sweeps a plane, an arc sweeps a cylinder.
  for (int k = 0; k < n; ++k) {
    const int k1 = (k + 1) % n;
    std::vector<EdgeUse> uses = {EdgeUse{baseE[static_cast<std::size_t>(k)], false},
                                EdgeUse{vert[static_cast<std::size_t>(k1)], false},
                                EdgeUse{topE[static_cast<std::size_t>(k)], true},
                                EdgeUse{vert[static_cast<std::size_t>(k)], true}};
    const ProfileEdge& pe = E[static_cast<std::size_t>(k)];
    if (pe.arc) {
      Face f;
      f.surface.kind = SurfaceKind::Cylinder;
      ucs::Ucs cyl;
      if (!ucs::FromNormal(pe.centre, up, &cyl))
        return Fail(Problem::DegenerateFrame, outWhy);
      const double r = InPlaneRadius(pl, pe.centre, W[static_cast<std::size_t>(k)]);
      f.surface.frame = cyl;
      f.surface.radius = r;
      f.surface.radius2 = r;
      f.surface.height = dist;
      const Vec3 toStart = ray3d::Sub(W[static_cast<std::size_t>(k)], pe.centre);
      const double u0 = std::atan2(ray3d::Dot(toStart, cyl.yAxis), ray3d::Dot(toStart, cyl.xAxis));
      // The span is stored increasing and the ORIENTATION is carried by `inward`, which is the
      // convention the Boolean bore walls already use — a negative `uEnd - uStart` would make the
      // face's own area come out negative instead, and the area is a magnitude.
      const double u1 = u0 + pe.sweep;
      f.surface.inward = pe.sweep < 0.0;
      f.uStart = std::min(u0, u1);
      f.uEnd = std::max(u0, u1);
      Loop lp;
      lp.uses = std::move(uses);
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    } else {
      const Vec3 edgeDir = ray3d::Sub(W[static_cast<std::size_t>(k1)], W[static_cast<std::size_t>(k)]);
      const Vec3 nrm = ray3d::Normalize(ray3d::Cross(edgeDir, up));
      s.faces.push_back(MakePlaneFace(W[static_cast<std::size_t>(k)], nrm, std::move(uses)));
    }
  }

  AddSingleShell(&s);
  // A feature result carries no recipe: the topology is the stored truth (ADR-046 (e)). An extrude
  // recipe is permitted but deferred to the increment that first persists one.

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

// ---------------------------------------------------------------------------------------------
// Feature operations — Loft (REQ-315 / ADR-048, GitHub issue #241). Skin a solid between two or
// more planar profiles. Each corresponding edge pair spans one SurfaceKind::Nurbs patch — ruled
// where the edge is straight, rational where it is an arc — and the end profiles cap it flat. The
// topology is Extrude's, generalised to N profiles; the only new thing is the freeform side face.
// ---------------------------------------------------------------------------------------------

namespace {

/// One profile prepared for lofting: its walk order (CCW about the loft axis), each walk edge's arc
/// flag / re-signed sweep / centre, and the oriented profile normal `up` (pointing along the axis).
struct LoftProfilePrep {
  std::vector<Vec3> walk;
  std::vector<char> arc;
  std::vector<double> sweep;
  std::vector<Vec3> centre;
  Vec3 up{0.0, 0.0, 1.0};
};

/// Validate one profile exactly the way \ref Extrude does, then order its walk CCW about
/// \p axisHint. Returns false with a reason on any fault (REQ-201).
[[nodiscard]] bool PrepLoftProfile(const Profile& pr, const Vec3& axisHint, LoftProfilePrep* out,
                                   Problem* outWhy) {
  const int n = static_cast<int>(pr.vertices.size());
  if (n != static_cast<int>(pr.edges.size()))
    return Fail(Problem::ProfileMalformed, outWhy);
  if (n < 2)
    return Fail(Problem::ProfileTooFewEdges, outWhy);
  if (!FrameOk(pr.plane))
    return Fail(Problem::DegenerateFrame, outWhy);
  const ucs::Ucs& pl = pr.plane;

  ucs::Point2D lo = ucs::WorldToPlane(pl, pr.vertices[0]);
  ucs::Point2D hi = lo;
  for (const Vec3& v : pr.vertices) {
    if (!FinitePoint(v))
      return Fail(Problem::NonFiniteCoordinate, outWhy);
    const ucs::Point2D q = ucs::WorldToPlane(pl, v);
    lo.x = std::min(lo.x, q.x);
    lo.y = std::min(lo.y, q.y);
    hi.x = std::max(hi.x, q.x);
    hi.y = std::max(hi.y, q.y);
  }
  const double scale = std::max({hi.x - lo.x, hi.y - lo.y, 1e-9});
  const double planeEps = 1e-6 * scale;
  const double lenEps = 1e-9 * scale;

  for (const Vec3& v : pr.vertices)
    if (std::fabs(ucs::SignedDistanceToPlane(pl, v)) > planeEps)
      return Fail(Problem::ProfilePointOffPlane, outWhy);
  for (int i = 0; i < n; ++i) {
    const ProfileEdge& pe = pr.edges[static_cast<std::size_t>(i)];
    if (!pe.arc)
      continue;
    if (!FinitePoint(pe.centre) || !std::isfinite(pe.sweep))
      return Fail(Problem::NonFiniteCoordinate, outWhy);
    if (std::fabs(ucs::SignedDistanceToPlane(pl, pe.centre)) > planeEps)
      return Fail(Problem::ProfilePointOffPlane, outWhy);
    if (!(std::fabs(pe.sweep) > 1e-9) || std::fabs(pe.sweep) >= kTwoPi)
      return Fail(Problem::DegenerateEdge, outWhy);
    const double r0 = InPlaneRadius(pl, pe.centre, pr.vertices[static_cast<std::size_t>(i)]);
    const double r1 =
        InPlaneRadius(pl, pe.centre, pr.vertices[static_cast<std::size_t>((i + 1) % n)]);
    if (!(r0 > lenEps))
      return Fail(Problem::DegenerateEdge, outWhy);
    if (std::fabs(r0 - r1) > 1e-6 * scale)
      return Fail(Problem::ProfileArcRadiusMismatch, outWhy);
  }
  if (ProfileChordsCross(pr))
    return Fail(Problem::ProfileSelfIntersects, outWhy);
  const double areaZ = ProfilePlaneSignedArea(pr);
  if (std::fabs(areaZ) <= lenEps * lenEps)
    return Fail(Problem::ProfileSelfIntersects, outWhy);

  const double d = ray3d::Dot(pl.zAxis, axisHint);
  if (!(std::fabs(d) > 1e-9))
    return Fail(Problem::DegenerateFrame, outWhy);  // a profile edge-on to the loft axis
  const double sgn = d > 0.0 ? 1.0 : -1.0;
  out->up = ray3d::Scale(pl.zAxis, sgn);
  const bool rev = (areaZ * sgn) < 0.0;

  out->walk.resize(static_cast<std::size_t>(n));
  out->arc.resize(static_cast<std::size_t>(n));
  out->sweep.resize(static_cast<std::size_t>(n));
  out->centre.resize(static_cast<std::size_t>(n));
  for (int k = 0; k < n; ++k) {
    const int src = rev ? (2 * n - k - 1) % n : k;
    const ProfileEdge& pe = pr.edges[static_cast<std::size_t>(src)];
    out->walk[static_cast<std::size_t>(k)] =
        pr.vertices[static_cast<std::size_t>(rev ? (n - k) % n : k)];
    out->arc[static_cast<std::size_t>(k)] = pe.arc ? 1 : 0;
    out->sweep[static_cast<std::size_t>(k)] = (rev ? -pe.sweep : pe.sweep) * sgn;
    out->centre[static_cast<std::size_t>(k)] = pe.centre;
  }
  for (int k = 0; k < n; ++k)
    if (out->arc[static_cast<std::size_t>(k)] && !(out->sweep[static_cast<std::size_t>(k)] > 1e-12))
      return Fail(Problem::ProfileArcReflex, outWhy);
  return true;
}

}  // namespace

bool Loft(const std::vector<Profile>& profiles, Solid* out, Problem* outWhy) {
  if (!out)
    return false;
  if (profiles.size() < 2)
    return Fail(Problem::LoftNeedsTwoProfiles, outWhy);

  auto centroid = [](const Profile& p) {
    Vec3 c{};
    for (const Vec3& v : p.vertices)
      c = ray3d::Add(c, v);
    return ray3d::Scale(c, 1.0 / static_cast<double>(std::max<std::size_t>(1, p.vertices.size())));
  };
  Vec3 axisHint = ray3d::Sub(centroid(profiles.back()), centroid(profiles.front()));
  if (!(ray3d::Length(axisHint) > 1e-9))
    axisHint = profiles.front().plane.zAxis;
  axisHint = ray3d::Normalize(axisHint);

  const int n = static_cast<int>(profiles.front().vertices.size());
  std::vector<LoftProfilePrep> prep(profiles.size());
  for (std::size_t pi = 0; pi < profiles.size(); ++pi) {
    if (static_cast<int>(profiles[pi].vertices.size()) != n)
      return Fail(Problem::LoftProfileMismatch, outWhy);
    if (!PrepLoftProfile(profiles[pi], axisHint, &prep[pi], outWhy))
      return false;
  }
  for (std::size_t pi = 0; pi + 1 < prep.size(); ++pi)
    for (int j = 0; j < n; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      if (prep[pi].arc[jj] != prep[pi + 1].arc[jj])
        return Fail(Problem::LoftProfileMismatch, outWhy);
      if (prep[pi].arc[jj]) {
        if (std::fabs(prep[pi].sweep[jj] - prep[pi + 1].sweep[jj]) >
            1e-6 * (1.0 + std::fabs(prep[pi].sweep[jj])))
          return Fail(Problem::LoftProfileMismatch, outWhy);
        if (ray3d::Length(ray3d::Sub(prep[pi].up, prep[pi + 1].up)) > 1e-9)
          return Fail(Problem::LoftProfileMismatch, outWhy);  // an arc ribbon needs a shared axis
      }
    }

  Solid s;
  std::vector<std::vector<int>> ringV(prep.size(), std::vector<int>(static_cast<std::size_t>(n)));
  for (std::size_t pi = 0; pi < prep.size(); ++pi)
    for (int j = 0; j < n; ++j)
      ringV[pi][static_cast<std::size_t>(j)] = AddVertex(&s, prep[pi].walk[static_cast<std::size_t>(j)]);

  std::vector<std::vector<int>> ringE(prep.size(), std::vector<int>(static_cast<std::size_t>(n)));
  for (std::size_t pi = 0; pi < prep.size(); ++pi)
    for (int j = 0; j < n; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      const int v0 = ringV[pi][jj];
      const int v1 = ringV[pi][static_cast<std::size_t>((j + 1) % n)];
      ringE[pi][jj] = prep[pi].arc[jj]
                          ? AddArc(&s, v0, v1, prep[pi].centre[jj], prep[pi].up, prep[pi].sweep[jj])
                          : AddLine(&s, v0, v1);
    }

  std::vector<std::vector<int>> railE(prep.size() - 1, std::vector<int>(static_cast<std::size_t>(n)));
  for (std::size_t pi = 0; pi + 1 < prep.size(); ++pi)
    for (int j = 0; j < n; ++j)
      railE[pi][static_cast<std::size_t>(j)] =
          AddLine(&s, ringV[pi][static_cast<std::size_t>(j)], ringV[pi + 1][static_cast<std::size_t>(j)]);

  // Bottom cap (profile 0): outward normal -up0; CCW about it is the walk reversed.
  {
    std::vector<EdgeUse> uses;
    for (int k = n - 1; k >= 0; --k)
      uses.push_back(EdgeUse{ringE.front()[static_cast<std::size_t>(k)], true});
    s.faces.push_back(
        MakePlaneFace(prep.front().walk[0], ray3d::Scale(prep.front().up, -1.0), std::move(uses)));
  }
  // Top cap (last profile): outward +up; the walk forwards.
  {
    std::vector<EdgeUse> uses;
    for (int k = 0; k < n; ++k)
      uses.push_back(EdgeUse{ringE.back()[static_cast<std::size_t>(k)], false});
    s.faces.push_back(MakePlaneFace(prep.back().walk[0], prep.back().up, std::move(uses)));
  }

  // Side faces: one NURBS patch per (band, edge), oriented so du x dv points outward.
  for (std::size_t pi = 0; pi + 1 < prep.size(); ++pi)
    for (int j = 0; j < n; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      const std::size_t j1 = static_cast<std::size_t>((j + 1) % n);
      Face f;
      f.surface.kind = SurfaceKind::Nurbs;
      if (prep[pi].arc[jj])
        f.surface.patch = nurbs::ArcRibbon(prep[pi].centre[jj], prep[pi].walk[jj],
                                           prep[pi + 1].centre[jj], prep[pi + 1].walk[jj],
                                           prep[pi].up, prep[pi].sweep[jj]);
      else
        f.surface.patch = nurbs::RuledLinear({prep[pi].walk[jj], prep[pi].walk[j1]},
                                             {prep[pi + 1].walk[jj], prep[pi + 1].walk[j1]});
      if (nurbs::ValidatePatch(f.surface.patch) != nurbs::PatchProblem::Ok)
        return Fail(Problem::LoftProfileMismatch, outWhy);
      f.uStart = nurbs::UMin(f.surface.patch);
      f.uEnd = nurbs::UMax(f.surface.patch);
      f.vStart = nurbs::VMin(f.surface.patch);
      f.vEnd = nurbs::VMax(f.surface.patch);
      Loop lp;
      lp.uses = {EdgeUse{ringE[pi][jj], false}, EdgeUse{railE[pi][j1], false},
                 EdgeUse{ringE[pi + 1][jj], true}, EdgeUse{railE[pi][jj], true}};
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    }

  AddSingleShell(&s);  // one shell, no recipe — the topology is the stored truth (ADR-046 (e))

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

// ---------------------------------------------------------------------------------------------
// Feature operations — Sweep (REQ-315 / ADR-048, GitHub issue #241). One profile along one path
// segment (a line or a circular arc). A straight segment builds ruled NURBS side faces and
// reproduces Extrude; an arc segment revolves the profile about the arc's axis and reproduces
// Revolve. The topology is Loft's single band; the difference is the V direction of each side patch
// (a straight span vs an exact rational revolution) and that the rails follow the path.
// ---------------------------------------------------------------------------------------------

namespace {

/// Rodrigues' rotation of \p v about unit axis \p k by angle \p a.
[[nodiscard]] Vec3 RotateAbout(const Vec3& v, const Vec3& k, double a) {
  const double c = std::cos(a);
  const double sn = std::sin(a);
  return ray3d::Add(ray3d::Add(ray3d::Scale(v, c), ray3d::Scale(ray3d::Cross(k, v), sn)),
                    ray3d::Scale(k, ray3d::Dot(k, v) * (1.0 - c)));
}

/// The frame that places the profile at a path point: origin \p o, and — when \p align — the profile
/// axes carried by the minimal rotation that takes the profile normal onto \p tangent, then twisted
/// \p twist about that tangent. When \p align is false the profile keeps its own axes (translate
/// only). Returns false if the result is not right-handed orthonormal.
[[nodiscard]] bool SweepFrameAt(const ucs::Ucs& profilePlane, const Vec3& o, const Vec3& tangent,
                                bool align, double twist, ucs::Ucs* out) {
  ucs::Ucs f = profilePlane;
  f.origin = o;
  if (align) {
    const Vec3 t = ray3d::Normalize(tangent);
    const Vec3 nrm = profilePlane.zAxis;
    const double d = std::clamp(ray3d::Dot(nrm, t), -1.0, 1.0);
    Vec3 axis = ray3d::Cross(nrm, t);
    const double al = ray3d::Length(axis);
    if (al > 1e-12) {
      axis = ray3d::Scale(axis, 1.0 / al);
      const double ang = std::acos(d);
      f.xAxis = RotateAbout(profilePlane.xAxis, axis, ang);
      f.yAxis = RotateAbout(profilePlane.yAxis, axis, ang);
      f.zAxis = RotateAbout(profilePlane.zAxis, axis, ang);
    } else if (d < 0.0) {
      // normal antiparallel to the tangent: a half turn about the profile's own X.
      f.yAxis = ray3d::Scale(profilePlane.yAxis, -1.0);
      f.zAxis = ray3d::Scale(profilePlane.zAxis, -1.0);
    }
  }
  if (twist != 0.0) {
    f.xAxis = RotateAbout(f.xAxis, ray3d::Normalize(f.zAxis), twist);
    f.yAxis = RotateAbout(f.yAxis, ray3d::Normalize(f.zAxis), twist);
  }
  if (!ucs::IsRightHandedOrthonormal(f, 1e-6))
    return false;
  *out = f;
  return true;
}

/// Rotate \p f's own axes by the minimal rotation that takes its CURRENT `zAxis` onto \p newTangent
/// (unit), about `cross(f.zAxis, newTangent)` — i.e., continue the SAME frame through a turn, rather
/// than re-deriving one fresh from some other reference. This is the discrete (single-turn) analogue
/// of the incremental `RotateAbout` propagation already used through an arc segment, and is what a
/// mitred corner needs (REQ-315 2026-09-04): re-deriving a leg's frame fresh from `profile.plane`
/// instead gives a DIFFERENT (generally inconsistent) basis whenever the incoming frame was itself
/// already rotated away from the profile's own plane — verified to disagree by direct construction
/// during this feature's implementation. Leaves \p f unchanged (returns true, a no-op) if `zAxis`
/// already matches; returns false only if the result fails to stay right-handed orthonormal.
[[nodiscard]] bool TurnFrameToTangent(ucs::Ucs* f, const Vec3& newTangent) {
  const Vec3 t = ray3d::Normalize(newTangent);
  const double d = std::clamp(ray3d::Dot(f->zAxis, t), -1.0, 1.0);
  Vec3 axis = ray3d::Cross(f->zAxis, t);
  const double al = ray3d::Length(axis);
  if (al > 1e-12) {
    axis = ray3d::Scale(axis, 1.0 / al);
    const double ang = std::acos(d);
    f->xAxis = RotateAbout(f->xAxis, axis, ang);
    f->yAxis = RotateAbout(f->yAxis, axis, ang);
    f->zAxis = RotateAbout(f->zAxis, axis, ang);
  } else if (d < 0.0) {
    f->yAxis = ray3d::Scale(f->yAxis, -1.0);
    f->zAxis = ray3d::Scale(f->zAxis, -1.0);
  }
  return ucs::IsRightHandedOrthonormal(*f, 1e-6);
}

/// A path segment's start / end unit tangents, its axis data, and its derived end point. Returns
/// false for a degenerate segment.
struct SweepSegGeom {
  Vec3 startTan;
  Vec3 endTan;
  Vec3 axis;      ///< arc only: the unit turn axis
  Vec3 centre;    ///< arc only
  double sweep = 0.0;
  Vec3 endPoint;  ///< the segment's geometric end (derived for an arc)
  bool arc = false;
  double length = 0.0;  ///< chord length (straight) or `radius * |sweep|` (arc) — REQ-315 2026-09-04,
                        ///< a segment's own share of a path-length-proportional twist.
};

[[nodiscard]] bool SegGeom(const Vec3& a, const Vec3& b, const SweepSegment& seg, double scale,
                           SweepSegGeom* out) {
  out->arc = seg.arc;
  if (!seg.arc) {
    const Vec3 d = ray3d::Sub(b, a);
    if (!(ray3d::Length(d) > 1e-9 * (1.0 + scale)))
      return false;
    out->startTan = out->endTan = ray3d::Normalize(d);
    out->endPoint = b;
    out->length = ray3d::Length(d);
    return true;
  }
  if (!FinitePoint(seg.centre) || !FinitePoint(seg.normal) || !(ray3d::Length(seg.normal) > 1e-9))
    return false;
  if (!std::isfinite(seg.sweep) || !(std::fabs(seg.sweep) > 1e-9) ||
      std::fabs(seg.sweep) >= kTwoPi - 1e-6)
    return false;
  out->axis = ray3d::Normalize(seg.normal);
  out->centre = seg.centre;
  out->sweep = seg.sweep;
  const Vec3 r0 = ray3d::Sub(a, seg.centre);
  if (!(ray3d::Length(r0) > 1e-9 * (1.0 + scale)))
    return false;
  out->startTan = ray3d::Normalize(ray3d::Cross(out->axis, r0));
  const Vec3 r1 = RotateAbout(r0, out->axis, seg.sweep);
  out->endTan = ray3d::Normalize(ray3d::Cross(out->axis, r1));
  if (seg.sweep < 0.0) {
    out->startTan = ray3d::Scale(out->startTan, -1.0);
    out->endTan = ray3d::Scale(out->endTan, -1.0);
  }
  out->endPoint = ray3d::Add(seg.centre, r1);
  // The nominal next point must be the arc image of this one (a caller consistency check).
  if (ray3d::Length(ray3d::Sub(out->endPoint, b)) > 1e-5 * (1.0 + scale) + 1e-6)
    return false;
  out->length = ray3d::Length(r0) * std::fabs(seg.sweep);
  return true;
}

/// The profile mapped rigidly from its own plane into frame \p F.
[[nodiscard]] Profile PlaceProfileInFrame(const Profile& profile, const ucs::Ucs& F) {
  Profile p;
  p.plane = F;
  p.vertices.reserve(profile.vertices.size());
  for (const Vec3& v : profile.vertices)
    p.vertices.push_back(ucs::UcsToWorld(F, ucs::WorldToUcs(profile.plane, v)));
  p.edges = profile.edges;
  for (std::size_t i = 0; i < p.edges.size(); ++i)
    if (p.edges[i].arc)
      p.edges[i].centre =
          ucs::UcsToWorld(F, ucs::WorldToUcs(profile.plane, profile.edges[i].centre));
  return p;
}

} // namespace

bool Sweep(const Profile& profile, const SweepPath& path, const SweepOptions& options, Solid* out,
           Problem* outWhy) {
  if (!out)
    return false;
  const int n = static_cast<int>(profile.vertices.size());
  if (n != static_cast<int>(profile.edges.size()))
    return Fail(Problem::ProfileMalformed, outWhy);
  if (n < 2)
    return Fail(Problem::ProfileTooFewEdges, outWhy);
  if (!FrameOk(profile.plane))
    return Fail(Problem::DegenerateFrame, outWhy);

  // A single full-circle arc segment (points[0] == points[1], sweep a full turn) splits here into
  // two half-turn segments around a synthetic midpoint, so no analytic edge downstream ever carries
  // a literal 2*pi sweep — the same reason Revolve splits its own full turn in two (brep.hpp: "every
  // primitive below splits its rims at a seam instead, so full-circle edges do not occur"). A
  // multi-segment path that closes on itself (points[0] == points[np-1], REQ-315 2026-09-04) needs
  // no such split: every one of its segments is already < 2*pi.
  SweepPath work = path;
  if (work.points.size() == 2 && work.segments.size() == 1 && work.segments[0].arc) {
    const SweepSegment& seg0 = work.segments[0];
    const double r0len = ray3d::Length(ray3d::Sub(work.points[0], seg0.centre));
    if (r0len > 1e-9 && FinitePoint(seg0.centre) && FinitePoint(seg0.normal) &&
        ray3d::Length(seg0.normal) > 1e-9 && std::isfinite(seg0.sweep) &&
        std::fabs(std::fabs(seg0.sweep) - kTwoPi) < 1e-6 &&
        ray3d::Length(ray3d::Sub(work.points[0], work.points[1])) < 1e-7 * (1.0 + r0len)) {
      const Vec3 axis = ray3d::Normalize(seg0.normal);
      const Vec3 r0 = ray3d::Sub(work.points[0], seg0.centre);
      const double halfSweep = seg0.sweep * 0.5;
      const Vec3 mid = ray3d::Add(seg0.centre, RotateAbout(r0, axis, halfSweep));
      SweepSegment half = seg0;
      half.sweep = halfSweep;
      work.points = {work.points[0], mid, work.points[1]};
      work.segments = {half, half};
    }
  }

  const int np = static_cast<int>(work.points.size());
  if (np < 2 || static_cast<int>(work.segments.size()) != np - 1)
    return Fail(Problem::SweepPathDegenerate, outWhy);
  for (const Vec3& p : work.points)
    if (!FinitePoint(p))
      return Fail(Problem::SweepPathDegenerate, outWhy);

  // A closed path: the last point coincides with the first (a full-circle single arc, now split
  // above, or a multi-segment path that returns to its own start). Built with no end caps and the
  // last ring aliased onto the first (REQ-315 2026-09-04) instead of Loft's two planar caps.
  // Path scale, for the relative tolerances below (including the closed-path check next).
  double scale = 1e-9;
  for (int k = 0; k + 1 < np; ++k)
    scale = std::max(scale, ray3d::Length(ray3d::Sub(work.points[static_cast<std::size_t>(k + 1)],
                                                     work.points[static_cast<std::size_t>(k)])));

  const bool closed =
      np >= 3 && ray3d::Length(ray3d::Sub(work.points[0], work.points[static_cast<std::size_t>(np - 1)])) <
                     1e-7 * (1.0 + scale);

  // A nonzero twist on a closed path is geometrically inconsistent, not an increment boundary: the
  // seam ring is one ring, and "0 at the start, twistRad at the end" would need it to carry two
  // different orientations at once (REQ-315 2026-09-04). Fixed orientation has no such conflict — a
  // constant (never-rotated) frame trivially closes, so it is not gated here.
  if (options.twistRad != 0.0 && closed)
    return Fail(Problem::SweepUnsupportedOption, outWhy);
  // A nonzero twist combined with an arc segment is refused, this increment (REQ-315 2026-09-04):
  // within an arc band, a profile vertex's true trajectory under a continuously varying twist is not
  // a plain circular arc (it compounds the path's own rotation with the twist's), which the arc
  // band's rail/patch construction (a single circular arc, or an exact rational revolve) cannot
  // represent — the same category of gap as a mitred corner touching an arc segment: fixing it needs
  // a genuinely different surface, not a smaller version of the straight-segment case.
  if (options.twistRad != 0.0)
    for (const SweepSegment& seg2 : work.segments)
      if (seg2.arc)
        return Fail(Problem::SweepTwistNeedsStraightPath, outWhy);

  // --- Per-segment geometry, and the sharp-corner classification at every joint -----------------
  // A sharp (tangent-discontinuous) joint where BOTH adjoining segments are straight, and the
  // profile is polygonal (no arc edge), MITRES (REQ-315 2026-09-04) rather than refusing:
  // `mitreAt[k]` records that ring k is cut on the bisector plane of the two tangents meeting there
  // (sheared in once the perpendicular ring is built, below) instead of being refused outright.
  // `mitreTangent[k]` is the tangent to shear ALONG — the incoming segment's, matching whichever
  // perpendicular reference that ring's frame actually is (the outgoing segment's, for a closed
  // path's ring 0, whose frame is built ⊥ to what leaves it, not what arrives).
  const bool profileIsPolygon = std::all_of(profile.edges.begin(), profile.edges.end(),
                                            [](const ProfileEdge& pe) { return !pe.arc; });
  std::vector<bool> mitreAt(static_cast<std::size_t>(np), false);
  std::vector<Vec3> mitreN(static_cast<std::size_t>(np));
  std::vector<Vec3> mitreTangent(static_cast<std::size_t>(np));

  auto classifyCorner = [&](int ring, const SweepSegGeom& gi, const SweepSegGeom& go,
                            const Vec3& shearTangent) -> bool {
    if (gi.arc || go.arc)
      return Fail(Problem::SweepPathCorner, outWhy);
    if (!profileIsPolygon)
      return Fail(Problem::SweepMitreProfileArc, outWhy);
    const Vec3 n = ray3d::Add(gi.endTan, go.startTan);
    const double nl = ray3d::Length(n);
    if (!(nl > 1e-6))
      return Fail(Problem::SweepMitreCollapsed, outWhy);
    mitreAt[static_cast<std::size_t>(ring)] = true;
    mitreN[static_cast<std::size_t>(ring)] = ray3d::Scale(n, 1.0 / nl);
    mitreTangent[static_cast<std::size_t>(ring)] = shearTangent;
    return true;
  };

  std::vector<SweepSegGeom> seg(static_cast<std::size_t>(np - 1));
  for (int k = 0; k + 1 < np; ++k) {
    if (!SegGeom(work.points[static_cast<std::size_t>(k)], work.points[static_cast<std::size_t>(k + 1)],
                 work.segments[static_cast<std::size_t>(k)], scale, &seg[static_cast<std::size_t>(k)]))
      return Fail(Problem::SweepPathDegenerate, outWhy);
    if (k > 0 &&
        ray3d::Dot(seg[static_cast<std::size_t>(k - 1)].endTan, seg[static_cast<std::size_t>(k)].startTan) <
            1.0 - 1e-6) {
      const SweepSegGeom& gi = seg[static_cast<std::size_t>(k - 1)];
      const SweepSegGeom& go = seg[static_cast<std::size_t>(k)];
      if (!classifyCorner(k, gi, go, gi.endTan))
        return false;
    }
  }
  // The closing seam of a closed path is a joint like any other: smooth, mitred, or refused. It
  // shears ring 0 (aliased onto ring np-1 later) — there is no separate ring np-1 to shear.
  bool closingMitred = false;
  if (closed) {
    const SweepSegGeom& gi = seg[static_cast<std::size_t>(np - 2)];
    const SweepSegGeom& go = seg[0];
    if (ray3d::Dot(gi.endTan, go.startTan) < 1.0 - 1e-6) {
      if (!classifyCorner(0, gi, go, go.startTan))
        return false;
      closingMitred = true;
    }
  }

  // --- Carry a rotation-minimizing frame along the path ---------------------------------------
  std::vector<ucs::Ucs> frame(static_cast<std::size_t>(np));
  if (!SweepFrameAt(profile.plane, work.points[0], seg[0].startTan, options.alignToPath, 0.0,
                    &frame[0]))
    return Fail(Problem::DegenerateFrame, outWhy);
  ucs::Ucs running = frame[0];
  for (int k = 1; k < np; ++k) {
    const SweepSegGeom& g = seg[static_cast<std::size_t>(k - 1)];
    ucs::Ucs f = running;
    f.origin = work.points[static_cast<std::size_t>(k)];
    // Fixed orientation (REQ-315 2026-09-04): the frame's axes never rotate, through an arc or a
    // corner — only the origin moves. `alignToPath` gates every rotation in this loop.
    if (g.arc && options.alignToPath) {
      f.xAxis = RotateAbout(f.xAxis, g.axis, g.sweep);
      f.yAxis = RotateAbout(f.yAxis, g.axis, g.sweep);
      f.zAxis = RotateAbout(f.zAxis, g.axis, g.sweep);
    }
    if (!ucs::IsRightHandedOrthonormal(f, 1e-6))
      return Fail(Problem::DegenerateFrame, outWhy);
    frame[static_cast<std::size_t>(k)] = f;
    running = f;
    // A mitred joint's ring (just captured above, ⊥ the INCOMING tangent) is not what the next leg
    // continues from: the next leg needs its own perpendicular reference, but TURNED to the new
    // tangent from the CURRENT frame — not re-derived fresh from `profile.plane` (REQ-315
    // 2026-09-04). Re-deriving fresh gives a different, generally inconsistent basis whenever the
    // incoming frame is itself already rotated away from the profile's own plane (which it is, past
    // the very first leg) — turning the current frame is what keeps this consistent with the shear,
    // the same way arc propagation already turns the frame incrementally instead of re-deriving it.
    if (mitreAt[static_cast<std::size_t>(k)] && options.alignToPath) {
      if (!TurnFrameToTangent(&running, seg[static_cast<std::size_t>(k)].startTan))
        return Fail(Problem::DegenerateFrame, outWhy);
    }
  }
  // A closed path must close its frame too: the profile orientation carried all the way around must
  // match the orientation it started with, or the seam ring would be built twice, inconsistently.
  // A rotation-minimizing frame around a planar closed path closes by construction; this is a
  // safety net for the general (non-planar) case, refused the same way a sharp corner is — but not
  // when the seam is deliberately mitred, where a tangent (and so frame) discontinuity is expected.
  if (closed && !closingMitred) {
    const ucs::Ucs& f0 = frame[0];
    const ucs::Ucs& fN = frame[static_cast<std::size_t>(np - 1)];
    if (ray3d::Dot(f0.xAxis, fN.xAxis) < 1.0 - 1e-6 || ray3d::Dot(f0.zAxis, fN.zAxis) < 1.0 - 1e-6)
      return Fail(Problem::SweepPathCorner, outWhy);
  }
  // Twist accumulates proportionally to distance travelled (REQ-315 2026-09-04): each ring's own
  // share is its cumulative path length so far, divided by the total — 0 at the start, `twistRad` at
  // the end, and (for the single-straight-segment case) exactly the old "rotate only the end frame"
  // rule, since there the only two rings are the start (fraction 0, a no-op) and the end (fraction
  // 1). Applied to each ring's OWN xAxis/yAxis about its OWN zAxis, after alignment/mitre are already
  // resolved — twist spins the cross-section in place, it does not change what the path itself does.
  if (options.twistRad != 0.0) {
    std::vector<double> cumLen(static_cast<std::size_t>(np), 0.0);
    for (int k = 1; k < np; ++k)
      cumLen[static_cast<std::size_t>(k)] =
          cumLen[static_cast<std::size_t>(k - 1)] + seg[static_cast<std::size_t>(k - 1)].length;
    const double totalLen = cumLen[static_cast<std::size_t>(np - 1)];
    for (int k = 0; k < np; ++k) {
      const double frac =
          totalLen > 1e-12 ? std::clamp(cumLen[static_cast<std::size_t>(k)] / totalLen, 0.0, 1.0) : 0.0;
      if (frac == 0.0)
        continue;
      ucs::Ucs& f = frame[static_cast<std::size_t>(k)];
      const double ang = options.twistRad * frac;
      f.xAxis = RotateAbout(f.xAxis, ray3d::Normalize(f.zAxis), ang);
      f.yAxis = RotateAbout(f.yAxis, ray3d::Normalize(f.zAxis), ang);
      if (!ucs::IsRightHandedOrthonormal(f, 1e-6))
        return Fail(Problem::DegenerateFrame, outWhy);
    }
  }

  // --- Place the profile in every frame, and prep each ring -----------------------------------
  std::vector<LoftProfilePrep> prep(static_cast<std::size_t>(np));
  for (int k = 0; k < np; ++k) {
    const Profile pk = PlaceProfileInFrame(profile, frame[static_cast<std::size_t>(k)]);
    if (!PrepLoftProfile(pk, frame[static_cast<std::size_t>(k)].zAxis, &prep[static_cast<std::size_t>(k)],
                         outWhy))
      return false;
  }
  // --- Mitre shear: slide each mitred ring's vertices, individually, along the recorded tangent
  // until they land on that joint's bisector plane (REQ-315 2026-09-04). Every vertex's
  // straight-segment trajectory through the adjoining band is a line parallel to that same tangent
  // (both segments at a mitred joint are straight, by construction — classifyCorner above), so this
  // only slides each vertex along its own already-valid rail; it does not change which straight line
  // it lies on, only where the (shared) ring cuts it — which is what keeps both adjoining bands'
  // rails meeting this ring exactly, without a gap or an overlap.
  // Fixed orientation (REQ-315 2026-09-04) needs no shear at all: the frame never rotates, so every
  // ring — mitred joint or not — already IS the profile translated to that path point, exactly the
  // invariant `alignToPath = false` promises. Shearing it anyway would displace it off the path
  // vertex for no reason, breaking that exact invariant precisely where it is easiest to check (a
  // sharp corner) — caught by an independent review's test on a non-planar closed, fixed-orientation
  // path before this task was called done.
  for (int k = 0; k < np; ++k) {
    if (!mitreAt[static_cast<std::size_t>(k)] || !options.alignToPath)
      continue;
    const Vec3& N = mitreN[static_cast<std::size_t>(k)];
    const Vec3& T = mitreTangent[static_cast<std::size_t>(k)];
    const Vec3& planePoint = work.points[static_cast<std::size_t>(k)];
    const double denom = ray3d::Dot(T, N);  // cos(half the corner angle); > 0, SweepMitreCollapsed guards 0
    for (Vec3& w : prep[static_cast<std::size_t>(k)].walk) {
      const double t = ray3d::Dot(ray3d::Sub(planePoint, w), N) / denom;
      w = ray3d::Add(w, ray3d::Scale(T, t));
    }
  }
  // A closed path's last ring is the same ring as its first — `prep[np-1]` must be an ALIAS of
  // `prep[0]` (whether or not the closing seam mitres), the same way `ringV`/`ringE` already are.
  // Left un-aliased, the last band's own surface patches would be built from the STALE, unsheared
  // `prep[np-1]` while their boundary EDGES point at the (correctly sheared) `ringV[0]` vertices —
  // a patch that does not match its own boundary, which is exactly the defect a closed-surface
  // volume check exists to catch.
  if (closed)
    prep[static_cast<std::size_t>(np - 1)] = prep[0];
  // Every arc segment: both of its rings must be clear of that segment's axis.
  for (int k = 0; k + 1 < np; ++k) {
    if (!seg[static_cast<std::size_t>(k)].arc)
      continue;
    const SweepSegGeom& g = seg[static_cast<std::size_t>(k)];
    for (int side = 0; side < 2; ++side)
      for (const Vec3& w : prep[static_cast<std::size_t>(k + side)].walk) {
        const Vec3 rel = ray3d::Sub(w, g.centre);
        const Vec3 perp = ray3d::Sub(rel, ray3d::Scale(g.axis, ray3d::Dot(rel, g.axis)));
        if (!(ray3d::Length(perp) > 1e-7 * (1.0 + ray3d::Length(rel))))
          return Fail(Problem::SweepProfileTouchesAxis, outWhy);
      }
  }

  // --- Topology: one band per segment, rings shared at the joints (Loft-style) ----------------
  // A closed path shares its LAST ring with its FIRST — the same vertex/edge ids, not a duplicate
  // coincident ring — mirroring Revolve's own full-turn wraparound (brep.cpp, Revolve's `full`
  // branch: `idx = V[k][0]`).
  Solid s;
  const int ringCount = closed ? np - 1 : np;
  std::vector<std::vector<int>> ringV(static_cast<std::size_t>(np),
                                      std::vector<int>(static_cast<std::size_t>(n)));
  for (int k = 0; k < ringCount; ++k)
    for (int j = 0; j < n; ++j)
      ringV[static_cast<std::size_t>(k)][static_cast<std::size_t>(j)] =
          AddVertex(&s, prep[static_cast<std::size_t>(k)].walk[static_cast<std::size_t>(j)]);
  if (closed)
    ringV[static_cast<std::size_t>(np - 1)] = ringV[0];

  std::vector<std::vector<int>> ringE(static_cast<std::size_t>(np),
                                      std::vector<int>(static_cast<std::size_t>(n)));
  for (int k = 0; k < ringCount; ++k)
    for (int j = 0; j < n; ++j) {
      const std::size_t kk = static_cast<std::size_t>(k);
      const std::size_t jj = static_cast<std::size_t>(j);
      const int a = ringV[kk][jj];
      const int b = ringV[kk][static_cast<std::size_t>((j + 1) % n)];
      ringE[kk][jj] = prep[kk].arc[jj]
                          ? AddArc(&s, a, b, prep[kk].centre[jj], prep[kk].up, prep[kk].sweep[jj])
                          : AddLine(&s, a, b);
    }
  if (closed)
    ringE[static_cast<std::size_t>(np - 1)] = ringE[0];

  std::vector<std::vector<int>> railE(static_cast<std::size_t>(np - 1),
                                      std::vector<int>(static_cast<std::size_t>(n)));
  for (int k = 0; k + 1 < np; ++k) {
    const SweepSegGeom& g = seg[static_cast<std::size_t>(k)];
    for (int j = 0; j < n; ++j) {
      const int a = ringV[static_cast<std::size_t>(k)][static_cast<std::size_t>(j)];
      const int b = ringV[static_cast<std::size_t>(k + 1)][static_cast<std::size_t>(j)];
      // Fixed orientation (REQ-315 2026-09-04) never rotates, so every vertex's rail is a straight
      // translation — even through an arc segment: a rigid body under pure translation moves every
      // point by the identical vector, regardless of the path's own shape. Only the ALIGNED case's
      // rail through an arc segment is itself an arc (the profile orbiting the path's own axis).
      if (!g.arc || !options.alignToPath) {
        railE[static_cast<std::size_t>(k)][static_cast<std::size_t>(j)] = AddLine(&s, a, b);
      } else {
        const Vec3 w = prep[static_cast<std::size_t>(k)].walk[static_cast<std::size_t>(j)];
        const Vec3 c = ray3d::Add(
            g.centre, ray3d::Scale(g.axis, ray3d::Dot(ray3d::Sub(w, g.centre), g.axis)));
        railE[static_cast<std::size_t>(k)][static_cast<std::size_t>(j)] =
            AddArc(&s, a, b, c, g.axis, g.sweep);
      }
    }
  }

  // Bottom cap (ring 0, outward −frame0.z), top cap (last ring, outward +frameEnd.z). A closed path
  // has no ends — the "last" ring IS the first — so neither cap is built.
  if (!closed) {
    {
      std::vector<EdgeUse> uses;
      for (int j = n - 1; j >= 0; --j)
        uses.push_back(EdgeUse{ringE.front()[static_cast<std::size_t>(j)], true});
      s.faces.push_back(
          MakePlaneFace(prep.front().walk[0], ray3d::Scale(frame.front().zAxis, -1.0), std::move(uses)));
    }
    {
      std::vector<EdgeUse> uses;
      for (int j = 0; j < n; ++j)
        uses.push_back(EdgeUse{ringE.back()[static_cast<std::size_t>(j)], false});
      s.faces.push_back(MakePlaneFace(prep.back().walk[0], frame.back().zAxis, std::move(uses)));
    }
  }

  // Side faces: one NURBS patch per (band, profile edge).
  for (int k = 0; k + 1 < np; ++k) {
    const SweepSegGeom& g = seg[static_cast<std::size_t>(k)];
    const LoftProfilePrep& p0 = prep[static_cast<std::size_t>(k)];
    const LoftProfilePrep& p1 = prep[static_cast<std::size_t>(k + 1)];
    for (int j = 0; j < n; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      const std::size_t j1 = static_cast<std::size_t>((j + 1) % n);
      const nurbs::Curve c0 =
          p0.arc[jj] ? nurbs::ArcCurve(p0.centre[jj], p0.walk[jj], p0.up, p0.sweep[jj])
                     : nurbs::LineCurve(p0.walk[jj], p0.walk[j1]);
      Face f;
      f.surface.kind = SurfaceKind::Nurbs;
      // Fixed orientation (REQ-315 2026-09-04): the patch is a ruled surface between c0 and its
      // rigid translate by the segment's own displacement, even through an arc — the same reason
      // the rail above is a straight line, not an arc, in this case.
      if (!g.arc || !options.alignToPath) {
        const nurbs::Curve c1 =
            p1.arc[jj] ? nurbs::ArcCurve(p1.centre[jj], p1.walk[jj], p1.up, p1.sweep[jj])
                       : nurbs::LineCurve(p1.walk[jj], p1.walk[j1]);
        f.surface.patch = nurbs::RuledCurveToCurve(c0, c1);
      } else {
        f.surface.patch = nurbs::RevolveCurve(c0, g.centre, g.axis, g.sweep);
      }
      if (nurbs::ValidatePatch(f.surface.patch) != nurbs::PatchProblem::Ok)
        return Fail(Problem::SweepUnsupportedOption, outWhy);
      f.uStart = nurbs::UMin(f.surface.patch);
      f.uEnd = nurbs::UMax(f.surface.patch);
      f.vStart = nurbs::VMin(f.surface.patch);
      f.vEnd = nurbs::VMax(f.surface.patch);
      Loop lp;
      lp.uses = {EdgeUse{ringE[static_cast<std::size_t>(k)][jj], false},
                 EdgeUse{railE[static_cast<std::size_t>(k)][j1], false},
                 EdgeUse{ringE[static_cast<std::size_t>(k + 1)][jj], true},
                 EdgeUse{railE[static_cast<std::size_t>(k)][jj], true}};
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    }
  }

  AddSingleShell(&s);
  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  // Fixed orientation on a curved path (REQ-315 2026-09-04) carries no rotation-minimizing guarantee
  // against the swept envelope folding over itself — unlike every aligned case, where the frame
  // always turns with the path. Not checked: `SelfIntersects` is a narrow, torus-specific check
  // (ADR-045 (f)'s tube-larger-than-ring case), not a general overlap detector, and a real one is a
  // separate undertaking (checking every face against every other). A profile too large, or a path
  // too tightly curved, for this option can build a solid that occupies the same space twice — a
  // known, documented limitation (REQ-315 2026-09-04), not a checked-and-refused case.
  *out = std::move(s);
  return Succeed(outWhy);
}

// ---------------------------------------------------------------------------------------------
// Feature operations — Revolve (REQ-314 / ADR-046 increment 2, GitHub issue #147).
// ---------------------------------------------------------------------------------------------

namespace {

/// The world point at radius \p r, height \p h, angle \p theta about the axis frame
/// {\p rad, \p yc, \p adir} anchored at \p axisPoint.
[[nodiscard]] Vec3 RevolvePoint(const Vec3& axisPoint, const Vec3& rad, const Vec3& yc,
                                const Vec3& adir, double r, double h, double theta) {
  const Vec3 radial =
      ray3d::Add(ray3d::Scale(rad, std::cos(theta)), ray3d::Scale(yc, std::sin(theta)));
  return ray3d::Add(ray3d::Add(axisPoint, ray3d::Scale(adir, h)), ray3d::Scale(radial, r));
}

} // namespace

bool Revolve(const Profile& profile, const Vec3& axisPoint, const Vec3& axisDir, double angleRad,
             Solid* out, Problem* outWhy) {
  if (!out)
    return false;
  const int n = static_cast<int>(profile.vertices.size());
  if (n != static_cast<int>(profile.edges.size()))
    return Fail(Problem::ProfileMalformed, outWhy);
  if (n < 2)
    return Fail(Problem::ProfileTooFewEdges, outWhy);
  if (!FrameOk(profile.plane))
    return Fail(Problem::DegenerateFrame, outWhy);
  for (const ProfileEdge& pe : profile.edges) {
    if (pe.arc)
      return Fail(Problem::RevolveArcInProfile, outWhy);
  }
  if (!std::isfinite(angleRad) || std::fabs(angleRad) <= 1e-9 || std::fabs(angleRad) > kTwoPi + 1e-6)
    return Fail(Problem::NonPositiveAngle, outWhy);
  if (!FinitePoint(axisPoint) || !FinitePoint(axisDir))
    return Fail(Problem::RevolveAxisDegenerate, outWhy);
  if (!(ray3d::Length(axisDir) > 1e-12))
    return Fail(Problem::RevolveAxisDegenerate, outWhy);

  const ucs::Ucs& pl = profile.plane;
  for (const Vec3& v : profile.vertices) {
    if (!FinitePoint(v))
      return Fail(Problem::NonFiniteCoordinate, outWhy);
  }

  ucs::Point2D lo = ucs::WorldToPlane(pl, profile.vertices[0]);
  ucs::Point2D hi = lo;
  for (const Vec3& v : profile.vertices) {
    const ucs::Point2D q = ucs::WorldToPlane(pl, v);
    lo.x = std::min(lo.x, q.x);
    lo.y = std::min(lo.y, q.y);
    hi.x = std::max(hi.x, q.x);
    hi.y = std::max(hi.y, q.y);
  }
  const double scale = std::max({hi.x - lo.x, hi.y - lo.y, 1e-9});
  const double planeEps = 1e-6 * scale;
  const double lenEps = 1e-9 * scale;
  const double axisEps = 1e-6 * scale;

  for (const Vec3& v : profile.vertices) {
    if (std::fabs(ucs::SignedDistanceToPlane(pl, v)) > planeEps)
      return Fail(Problem::ProfilePointOffPlane, outWhy);
  }
  if (ProfileChordsCross(profile))
    return Fail(Problem::ProfileSelfIntersects, outWhy);

  // Axis: normalised, sweep sense folded into its direction so `ang` is positive.
  Vec3 adir = ray3d::Normalize(axisDir);
  double ang = angleRad;
  if (ang < 0.0) {
    adir = ray3d::Scale(adir, -1.0);
    ang = -ang;
  }
  ang = std::min(ang, kTwoPi);
  const bool full = ang >= kTwoPi - 1e-9;
  if (std::fabs(ucs::SignedDistanceToPlane(pl, axisPoint)) > planeEps ||
      std::fabs(ray3d::Dot(adir, pl.zAxis)) > 1e-7)
    return Fail(Problem::RevolveAxisNotInPlane, outWhy);

  Vec3 rad = ray3d::Normalize(ray3d::Cross(pl.zAxis, adir));

  // Profile in (r, h): r = signed distance from the axis along `rad`, h = distance along `adir`.
  std::vector<double> R(static_cast<std::size_t>(n));
  std::vector<double> H(static_cast<std::size_t>(n));
  auto measure = [&]() {
    double rmn = 1e300;
    double rmx = -1e300;
    for (int i = 0; i < n; ++i) {
      const Vec3 d = ray3d::Sub(profile.vertices[static_cast<std::size_t>(i)], axisPoint);
      R[static_cast<std::size_t>(i)] = ray3d::Dot(d, rad);
      H[static_cast<std::size_t>(i)] = ray3d::Dot(d, adir);
      rmn = std::min(rmn, R[static_cast<std::size_t>(i)]);
      rmx = std::max(rmx, R[static_cast<std::size_t>(i)]);
    }
    return std::pair<double, double>{rmn, rmx};
  };
  double rmin = 0.0;
  double rmax = 0.0;
  {
    const auto p = measure();
    rmin = p.first;
    rmax = p.second;
  }
  if (rmin < -axisEps && rmax > axisEps)
    return Fail(Problem::RevolveProfileCrossesAxis, outWhy);
  if (rmax <= axisEps) {
    rad = ray3d::Scale(rad, -1.0);  // profile sits on the -radial side: flip so radii are positive
    const auto p = measure();
    rmin = p.first;
    rmax = p.second;
  }
  if (rmax <= axisEps)
    return Fail(Problem::RevolveProfileMissesAxis, outWhy);  // entirely on the axis — no volume
  (void)rmin;
  for (int i = 0; i < n; ++i) {
    if (R[static_cast<std::size_t>(i)] < 0.0)
      R[static_cast<std::size_t>(i)] = 0.0;  // clamp a rounding-sized negative
  }

  // Increment 2a builds a solid filled from the axis to a single-valued outer curve, so the profile
  // must touch the axis along ONE contiguous run of vertices — that is what makes an inner (+radial-
  // outward) face impossible. A profile that misses the axis, or touches it twice, is refused.
  std::vector<char> onAxis(static_cast<std::size_t>(n), 0);
  int touchCount = 0;
  for (int i = 0; i < n; ++i) {
    if (R[static_cast<std::size_t>(i)] <= axisEps) {
      onAxis[static_cast<std::size_t>(i)] = 1;
      ++touchCount;
    }
  }
  if (touchCount == 0)
    return Fail(Problem::RevolveProfileMissesAxis, outWhy);
  if (touchCount < n) {
    int runs = 0;
    for (int i = 0; i < n; ++i) {
      if (onAxis[static_cast<std::size_t>(i)] && !onAxis[static_cast<std::size_t>((i + n - 1) % n)])
        ++runs;
    }
    if (runs != 1)
      return Fail(Problem::RevolveProfileMissesAxis, outWhy);
  }

  // Orient the profile CCW in (r, h) so the swept faces come out with outward normals.
  double arh = 0.0;
  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    arh += 0.5 * (R[static_cast<std::size_t>(i)] * H[static_cast<std::size_t>(j)] -
                  R[static_cast<std::size_t>(j)] * H[static_cast<std::size_t>(i)]);
  }
  if (std::fabs(arh) <= lenEps * lenEps)
    return Fail(Problem::ProfileSelfIntersects, outWhy);  // zero enclosed area
  std::vector<int> order(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    order[static_cast<std::size_t>(i)] = arh < 0.0 ? (n - i) % n : i;

  std::vector<double> rw(static_cast<std::size_t>(n));
  std::vector<double> hw(static_cast<std::size_t>(n));
  std::vector<char> axw(static_cast<std::size_t>(n));
  for (int k = 0; k < n; ++k) {
    rw[static_cast<std::size_t>(k)] = R[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])];
    hw[static_cast<std::size_t>(k)] = H[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])];
    axw[static_cast<std::size_t>(k)] = onAxis[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])];
  }

  const Vec3 yc = ray3d::Cross(adir, rad);  // {rad, yc, adir} right-handed
  const int segs = full ? 2 : 1;
  const double dth = ang / static_cast<double>(segs);

  Solid s;

  // Vertices: V[k][t] for t in 0..segs. An on-axis vertex does not move — one vertex, reused. A full
  // revolve wraps t == segs back to t == 0.
  std::vector<std::array<int, 3>> V(static_cast<std::size_t>(n));  // segs <= 2 so at most 3 stations
  for (int k = 0; k < n; ++k) {
    for (int t = 0; t <= segs; ++t) {
      int idx;
      if (axw[static_cast<std::size_t>(k)]) {
        idx = (t == 0) ? AddVertex(&s, RevolvePoint(axisPoint, rad, yc, adir, rw[static_cast<std::size_t>(k)],
                                                    hw[static_cast<std::size_t>(k)], 0.0))
                       : V[static_cast<std::size_t>(k)][0];
      } else if (full && t == segs) {
        idx = V[static_cast<std::size_t>(k)][0];
      } else {
        idx = AddVertex(&s, RevolvePoint(axisPoint, rad, yc, adir, rw[static_cast<std::size_t>(k)],
                                         hw[static_cast<std::size_t>(k)], static_cast<double>(t) * dth));
      }
      V[static_cast<std::size_t>(k)][static_cast<std::size_t>(t)] = idx;
    }
  }

  // Meridian edges: the profile edges rotated to each angular station.
  //   - A non-axis edge gets a distinct edge per station (a full revolve wraps station segs to 0).
  //   - An on-axis edge does not move, so ONE shared edge serves every station; in a full revolve it
  //     has no cap to bound it and is dropped entirely.
  std::vector<std::array<int, 3>> merid(static_cast<std::size_t>(n));
  for (int k = 0; k < n; ++k) {
    const int k1 = (k + 1) % n;
    const bool axisEdge = axw[static_cast<std::size_t>(k)] && axw[static_cast<std::size_t>(k1)];
    for (int t = 0; t <= segs; ++t) {
      int idx;
      if (axisEdge) {
        idx = full ? -1
                   : (t == 0 ? AddLine(&s, V[static_cast<std::size_t>(k)][0],
                                       V[static_cast<std::size_t>(k1)][0])
                             : merid[static_cast<std::size_t>(k)][0]);
      } else if (full && t == segs) {
        idx = merid[static_cast<std::size_t>(k)][0];
      } else {
        idx = AddLine(&s, V[static_cast<std::size_t>(k)][static_cast<std::size_t>(t)],
                      V[static_cast<std::size_t>(k1)][static_cast<std::size_t>(t)]);
      }
      merid[static_cast<std::size_t>(k)][static_cast<std::size_t>(t)] = idx;
    }
  }

  // Parallel edges: an arc about the axis at each non-axis vertex's radius, one per angular interval.
  std::vector<std::array<int, 3>> par(static_cast<std::size_t>(n));
  for (int k = 0; k < n; ++k) {
    for (int t = 0; t < segs; ++t) {
      if (axw[static_cast<std::size_t>(k)]) {
        par[static_cast<std::size_t>(k)][static_cast<std::size_t>(t)] = -1;
        continue;
      }
      const Vec3 c = ray3d::Add(axisPoint, ray3d::Scale(adir, hw[static_cast<std::size_t>(k)]));
      par[static_cast<std::size_t>(k)][static_cast<std::size_t>(t)] =
          AddArc(&s, V[static_cast<std::size_t>(k)][static_cast<std::size_t>(t)],
                 V[static_cast<std::size_t>(k)][static_cast<std::size_t>(t + 1)], c, adir, dth);
    }
  }

  // Side faces: one per (profile edge, angular interval), skipping fully-on-axis edges.
  for (int k = 0; k < n; ++k) {
    const int k1 = (k + 1) % n;
    if (axw[static_cast<std::size_t>(k)] && axw[static_cast<std::size_t>(k1)])
      continue;
    const double r0 = rw[static_cast<std::size_t>(k)];
    const double r1 = rw[static_cast<std::size_t>(k1)];
    const double h0 = hw[static_cast<std::size_t>(k)];
    const double h1 = hw[static_cast<std::size_t>(k1)];

    for (int t = 0; t < segs; ++t) {
      const int m0 = merid[static_cast<std::size_t>(k)][static_cast<std::size_t>(t)];
      const int m1 = merid[static_cast<std::size_t>(k)][static_cast<std::size_t>(t + 1)];
      const int pk = par[static_cast<std::size_t>(k)][static_cast<std::size_t>(t)];
      const int pk1 = par[static_cast<std::size_t>(k1)][static_cast<std::size_t>(t)];

      std::vector<EdgeUse> uses;
      if (pk >= 0)
        uses.push_back(EdgeUse{pk, false});
      uses.push_back(EdgeUse{m1, false});
      if (pk1 >= 0)
        uses.push_back(EdgeUse{pk1, true});
      uses.push_back(EdgeUse{m0, true});

      if (std::fabs(h1 - h0) <= lenEps) {
        // Perpendicular edge -> a planar annular sector at height h0. Outward is along the axis,
        // away from the profile interior: the interior is on the +h side when r increases.
        const double sgn = (r1 - r0) > 0.0 ? -1.0 : 1.0;
        const Vec3 origin = ray3d::Add(axisPoint, ray3d::Scale(adir, h0));
        Face f = MakePlaneFace(origin, ray3d::Scale(adir, sgn), std::move(uses));
        s.faces.push_back(std::move(f));
      } else {
        Face f;
        f.surface.kind = std::fabs(r1 - r0) <= lenEps ? SurfaceKind::Cylinder : SurfaceKind::Cone;
        const double hlo = std::min(h0, h1);
        ucs::Ucs fr;
        fr.origin = ray3d::Add(axisPoint, ray3d::Scale(adir, hlo));
        fr.xAxis = rad;
        fr.yAxis = yc;
        fr.zAxis = adir;
        f.surface.frame = fr;
        f.surface.radius = (h0 <= h1) ? r0 : r1;   // radius at z = 0 (the lower end)
        f.surface.radius2 = (h0 <= h1) ? r1 : r0;  // radius at z = height
        f.surface.height = std::fabs(h1 - h0);
        f.uStart = static_cast<double>(t) * dth;
        f.uEnd = static_cast<double>(t + 1) * dth;
        Loop lp;
        lp.uses = std::move(uses);
        f.loops.push_back(std::move(lp));
        s.faces.push_back(std::move(f));
      }
    }
  }

  // Cap faces (partial revolve only): the profile itself, rotated to the start and end angles.
  if (!full) {
    const Vec3 tanStart{-std::sin(0.0) * rad.x + std::cos(0.0) * yc.x,
                        -std::sin(0.0) * rad.y + std::cos(0.0) * yc.y,
                        -std::sin(0.0) * rad.z + std::cos(0.0) * yc.z};
    {
      std::vector<EdgeUse> uses;
      uses.reserve(static_cast<std::size_t>(n));
      for (int k = 0; k < n; ++k)
        uses.push_back(EdgeUse{merid[static_cast<std::size_t>(k)][0], false});
      s.faces.push_back(MakePlaneFace(axisPoint, ray3d::Scale(tanStart, -1.0), std::move(uses)));
    }
    {
      const double te = ang;
      const Vec3 tanEnd{-std::sin(te) * rad.x + std::cos(te) * yc.x,
                        -std::sin(te) * rad.y + std::cos(te) * yc.y,
                        -std::sin(te) * rad.z + std::cos(te) * yc.z};
      std::vector<EdgeUse> uses;
      uses.reserve(static_cast<std::size_t>(n));
      for (int k = n - 1; k >= 0; --k)
        uses.push_back(EdgeUse{merid[static_cast<std::size_t>(k)][static_cast<std::size_t>(segs)], true});
      s.faces.push_back(MakePlaneFace(axisPoint, tanEnd, std::move(uses)));
    }
  }

  AddSingleShell(&s);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

// ---------------------------------------------------------------------------------------------
// Feature operations — Slice (REQ-314 / ADR-046 increment 3, GitHub issue #147).
//
// The first operation that operates on an existing solid's topology rather than building from a
// profile, and the machinery the analytic Booleans reuse: classify each vertex against a plane,
// clip the faces the plane crosses, and stitch the pieces back into closed shells with a new planar
// cap. Increment 3a handles planar-faced solids; a curved face is refused, because an oblique plane
// through a cylinder cuts an ellipse the kernel's `{Line, Arc}` curves cannot hold.
// ---------------------------------------------------------------------------------------------

namespace {

/// A polygon plus the outward normal of the face it will become — the slice's working form before
/// shared vertices and edges are welded back together.
struct PolyFace {
  std::vector<Vec3> ring;
  Vec3 normal;
};

/// Signed area of \p ring about \p n: positive when the ring winds CCW seen from the +n side.
[[nodiscard]] double RingSignedAreaAbout(const std::vector<Vec3>& ring, const Vec3& n) {
  Vec3 acc{};
  const std::size_t m = ring.size();
  for (std::size_t i = 0; i < m; ++i)
    acc = ray3d::Add(acc, ray3d::Cross(ring[i], ring[(i + 1) % m]));
  return 0.5 * ray3d::Dot(acc, n);
}

/// Weld a set of planar polygons into a `Solid`: vertices merged by position, every undirected edge
/// used by exactly two polygons once in each direction, each polygon one plane face. False (with a
/// slice-flavoured \p outWhy) when the result does not `Validate`.
[[nodiscard]] bool WeldPlanarSolid(const std::vector<PolyFace>& polys, double scale,
                                   Problem complexReason, Solid* out, Problem* outWhy) {
  const double weldEps = std::max(1e-7 * scale, 1e-12);
  Solid s;
  std::map<std::tuple<long long, long long, long long>, int> vmap;
  auto quant = [&](double v) { return static_cast<long long>(std::llround(v / weldEps)); };
  auto addV = [&](const Vec3& p) {
    const auto k = std::make_tuple(quant(p.x), quant(p.y), quant(p.z));
    const auto it = vmap.find(k);
    if (it != vmap.end())
      return it->second;
    const int idx = AddVertex(&s, p);
    vmap.emplace(k, idx);
    return idx;
  };
  std::map<std::pair<int, int>, int> emap;

  for (const PolyFace& pf : polys) {
    if (pf.ring.size() < 3)
      continue;
    std::vector<int> vidx;
    for (const Vec3& p : pf.ring) {
      const int vi = addV(p);
      if (vidx.empty() || vidx.back() != vi)
        vidx.push_back(vi);
    }
    while (vidx.size() > 1 && vidx.front() == vidx.back())
      vidx.pop_back();
    if (vidx.size() < 3)
      continue;

    Loop lp;
    const std::size_t m = vidx.size();
    for (std::size_t i = 0; i < m; ++i) {
      const int a = vidx[i];
      const int b = vidx[(i + 1) % m];
      if (a == b)
        return Fail(complexReason, outWhy);
      const std::pair<int, int> key{std::min(a, b), std::max(a, b)};
      const auto it = emap.find(key);
      int ei;
      if (it != emap.end())
        ei = it->second;
      else {
        ei = AddLine(&s, key.first, key.second);
        emap.emplace(key, ei);
      }
      lp.uses.push_back(EdgeUse{ei, a > b});
    }
    Face f;
    f.surface = PlaneSurface(pf.ring[0], ray3d::Normalize(pf.normal));
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  }

  if (s.faces.size() < 4)
    return Fail(complexReason, outWhy);
  AddSingleShell(&s);
  const Problem why = Validate(s);
  if (why != Problem::Ok) {
    const bool topo = why == Problem::EdgeNotUsedTwice || why == Problem::EdgeOrientationInconsistent ||
                      why == Problem::NotClosed;
    return Fail(topo ? complexReason : why, outWhy);
  }
  *out = std::move(s);
  return Succeed(outWhy);
}

/// Slice a cylinder / cone primitive by a plane PERPENDICULAR to its axis — the "cut a pipe or
/// shaft to length" case, where the cross-section is a circle the kernel can hold. The two pieces
/// are rebuilt as fresh primitives (a cylinder into two cylinders, a cone into two frustums).
/// Returns false (leaving \p handled false) for anything else — an oblique or parallel plane, a
/// sphere / torus, or a non-primitive curved solid — so the caller falls back to its refusal.
[[nodiscard]] bool SliceCurvedPrimitive(const Solid& solid, const Vec3& planePoint, const Vec3& pn,
                                        SliceKeep keep, Solid* outAbove, Solid* outBelow, bool* handled,
                                        Problem* outWhy) {
  *handled = false;
  const Recipe& rc = solid.recipe;
  if (rc.kind != PrimitiveKind::Cylinder && rc.kind != PrimitiveKind::Cone)
    return false;
  const ucs::Ucs& fr = rc.frame;
  const Vec3 axis = fr.zAxis;
  // The plane must be perpendicular to the axis (its normal parallel to the axis).
  if (std::fabs(std::fabs(ray3d::Dot(pn, axis)) - 1.0) > 1e-6)
    return false;

  *handled = true;
  const double h = rc.height;
  const double scale = std::max(h, std::max(rc.radius, rc.radius2));
  const double eps = 1e-7 * std::max(scale, 1.0);
  const double d = ray3d::Dot(ray3d::Sub(planePoint, fr.origin), axis);  // cut height above the base
  if (d <= eps || d >= h - eps)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);

  const double rCut = rc.kind == PrimitiveKind::Cylinder
                          ? rc.radius
                          : rc.radius + (rc.radius2 - rc.radius) * (d / h);

  ucs::Ucs upperFrame = fr;
  upperFrame.origin = ray3d::Add(fr.origin, ray3d::Scale(axis, d));

  const bool wantAbove = keep == SliceKeep::Above || keep == SliceKeep::Both;
  const bool wantBelow = keep == SliceKeep::Below || keep == SliceKeep::Both;
  // "Above" is the +pn side. +pn points along +axis iff their dot is positive.
  const bool aboveIsUpper = ray3d::Dot(pn, axis) > 0.0;

  Problem why = Problem::Ok;
  auto buildLower = [&](Solid* o) {
    return rc.kind == PrimitiveKind::Cylinder ? MakeCylinder(fr, rc.radius, d, o, &why)
                                              : MakeCone(fr, rc.radius, rCut, d, o, &why);
  };
  auto buildUpper = [&](Solid* o) {
    return rc.kind == PrimitiveKind::Cylinder ? MakeCylinder(upperFrame, rc.radius, h - d, o, &why)
                                              : MakeCone(upperFrame, rCut, rc.radius2, h - d, o, &why);
  };
  // Prove both build before writing either output (REQ-201).
  Solid probe;
  if (!buildLower(&probe) || !buildUpper(&probe))
    return Fail(why, outWhy);

  Solid* upperOut = aboveIsUpper ? outAbove : outBelow;
  Solid* lowerOut = aboveIsUpper ? outBelow : outAbove;
  const bool wantUpper = aboveIsUpper ? wantAbove : wantBelow;
  const bool wantLower = aboveIsUpper ? wantBelow : wantAbove;
  if (wantUpper && upperOut)
    (void)buildUpper(upperOut);
  if (wantLower && lowerOut)
    (void)buildLower(lowerOut);
  return Succeed(outWhy);
}

/// Cut a right circular cylinder by an **oblique** plane — the cross-section is an ellipse
/// (REQ-314 B2b-1, D-2026-09-02-h). Each piece is a cylinder with one flat rim and one elliptical
/// rim. Handles only a cut that meets the cylinder side all the way round (the ellipse stays
/// strictly between the two caps); anything else leaves \p handled false or reports by name.
[[nodiscard]] bool SliceCylinderOblique(const Solid& solid, const Vec3& planePoint, const Vec3& pn,
                                        SliceKeep keep, Solid* outAbove, Solid* outBelow, bool* handled,
                                        Problem* outWhy) {
  *handled = false;
  const Recipe& rc = solid.recipe;
  if (rc.kind != PrimitiveKind::Cylinder)
    return false;
  const ucs::Ucs& fr = rc.frame;
  const Vec3 Z = fr.zAxis;
  const double r = rc.radius;
  const double h = rc.height;
  const double dotNZ = ray3d::Dot(pn, Z);
  if (std::fabs(dotNZ) < 1e-6 || std::fabs(dotNZ) > 1.0 - 1e-9)
    return false;  // perpendicular / parallel — not this recogniser

  *handled = true;
  const double scale = std::max(h, r);
  const double eps = 1e-7 * std::max(scale, 1.0);

  // z_plane(u) = a0 + a1 cos u + a2 sin u  in the cylinder's local frame.
  const Vec3 pl = ucs::WorldToUcs(fr, planePoint);
  const Vec3 nl{ray3d::Dot(pn, fr.xAxis), ray3d::Dot(pn, fr.yAxis), dotNZ};
  const double a0 = (nl.x * pl.x + nl.y * pl.y + nl.z * pl.z) / nl.z;
  const double a1 = -r * nl.x / nl.z;
  const double a2 = -r * nl.y / nl.z;
  const double amp = std::sqrt(a1 * a1 + a2 * a2);
  if (a0 - amp <= eps || a0 + amp >= h - eps)
    return Fail(Problem::SliceResultComplex, outWhy);  // the ellipse would clip a cap

  // Ellipse geometry, in world.
  const Vec3 ec = ray3d::Add(fr.origin, ray3d::Scale(Z, a0));  // plane ∩ axis
  const Vec3 minorDir = ray3d::Normalize(ray3d::Cross(pn, Z));
  const Vec3 majorDir = ray3d::Normalize(ray3d::Cross(pn, minorDir));
  const double ea = r / std::fabs(dotNZ);  // semi-major
  const double eb = r;                     // semi-minor
  const Vec3 eN = dotNZ > 0.0 ? pn : ray3d::Scale(pn, -1.0);  // ellipse normal, +Z-ish

  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(fr, Vec3{x, y, z}); };
  const double zp0 = a0 + a1;  // z_plane at u = 0  (local +x)
  const double zpP = a0 - a1;  // z_plane at u = pi (local -x)

  // Build one piece: `upper` true keeps the material above the plane (rim at z = h), false below.
  auto build = [&](bool upper, Solid* dst) -> bool {
    Solid s;
    const int se0 = AddVertex(&s, W(r, 0.0, zp0));   // ellipse ∩ seam at u = 0
    const int se1 = AddVertex(&s, W(-r, 0.0, zpP));  // ellipse ∩ seam at u = pi
    const int rimZ0 = AddVertex(&s, W(r, 0.0, upper ? h : 0.0));
    const int rimZ1 = AddVertex(&s, W(-r, 0.0, upper ? h : 0.0));

    const Vec3 rimC = W(0.0, 0.0, upper ? h : 0.0);
    const Vec3 up{0.0, 0.0, 0.0};  // unused
    (void)up;
    const int rr0 = AddArc(&s, rimZ0, rimZ1, rimC, Z, kPi);
    const int rr1 = AddArc(&s, rimZ1, rimZ0, rimC, Z, kPi);
    const int el0 = AddEllipse(&s, se0, se1, ec, eN, majorDir, ea, eb, kPi);
    const int el1 = AddEllipse(&s, se1, se0, ec, eN, majorDir, ea, eb, kPi);
    const int sm0 = AddLine(&s, upper ? se0 : rimZ0, upper ? rimZ0 : se0);  // seam at +x, low→high
    const int sm1 = AddLine(&s, upper ? se1 : rimZ1, upper ? rimZ1 : se1);  // seam at -x

    // Flat rim cap.
    if (upper)
      s.faces.push_back(MakePlaneFace(rimC, Z, {{rr0, false}, {rr1, false}}));
    else
      s.faces.push_back(MakePlaneFace(rimC, ray3d::Scale(Z, -1.0), {{rr1, true}, {rr0, true}}));
    // Elliptical cut cap — outward normal away from this piece's material.
    const Vec3 cutN = upper ? ray3d::Scale(eN, -1.0) : eN;
    if (upper)
      s.faces.push_back(MakePlaneFace(ec, cutN, {{el1, true}, {el0, true}}));
    else
      s.faces.push_back(MakePlaneFace(ec, cutN, {{el0, false}, {el1, false}}));

    auto side = [&](double u0, double u1, std::vector<EdgeUse> uses) {
      Face f;
      f.surface.kind = SurfaceKind::Cylinder;
      f.surface.frame = fr;
      f.surface.radius = r;
      f.surface.radius2 = r;
      f.surface.height = h;
      f.uStart = u0;
      f.uEnd = u1;
      Loop lp;
      lp.uses = std::move(uses);
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    };
    if (upper) {
      side(0.0, kPi, {{el0, false}, {sm1, false}, {rr0, true}, {sm0, true}});
      side(kPi, kTwoPi, {{el1, false}, {sm0, false}, {rr1, true}, {sm1, true}});
    } else {
      side(0.0, kPi, {{rr0, false}, {sm1, false}, {el0, true}, {sm0, true}});
      side(kPi, kTwoPi, {{rr1, false}, {sm0, false}, {el1, true}, {sm1, true}});
    }

    AddSingleShell(&s);
    const Problem why = Validate(s);
    if (why != Problem::Ok)
      return Fail(Problem::SliceResultComplex, outWhy);
    *dst = std::move(s);
    return true;
  };

  const bool wantAbove = keep == SliceKeep::Above || keep == SliceKeep::Both;
  const bool wantBelow = keep == SliceKeep::Below || keep == SliceKeep::Both;
  // "Above" is the +pn side. The upper piece (rim at z = h) is the +pn side iff pn·Z > 0.
  const bool upperIsAbove = dotNZ > 0.0;
  Solid* upperDst = upperIsAbove ? outAbove : outBelow;
  Solid* lowerDst = upperIsAbove ? outBelow : outAbove;
  const bool wantUpper = upperIsAbove ? wantAbove : wantBelow;
  const bool wantLower = upperIsAbove ? wantBelow : wantAbove;

  Solid probe;
  if (!build(true, &probe) || !build(false, &probe))
    return false;  // build() already set outWhy
  if (wantUpper && upperDst && !build(true, upperDst))
    return false;
  if (wantLower && lowerDst && !build(false, lowerDst))
    return false;
  return Succeed(outWhy);
}

} // namespace

bool Slice(const Solid& solid, const Vec3& planePoint, const Vec3& planeNormal, SliceKeep keep,
           Solid* outAbove, Solid* outBelow, Problem* outWhy) {
  if (!FinitePoint(planePoint) || !FinitePoint(planeNormal) || !(ray3d::Length(planeNormal) > 1e-12))
    return Fail(Problem::SliceDegeneratePlane, outWhy);
  const Problem inWhy = Validate(solid);
  if (inWhy != Problem::Ok)
    return Fail(inWhy, outWhy);

  {
    bool hasCurved = false;
    for (const Face& f : solid.faces)
      if (f.surface.kind != SurfaceKind::Plane)
        hasCurved = true;
    if (hasCurved) {
      // Curved cuts the kernel can hold: perpendicular to a cylinder / cone axis (a circle, B1),
      // or oblique through a cylinder (an ellipse, B2b-1).
      const Vec3 upn = ray3d::Normalize(planeNormal);
      bool handled = false;
      bool ok = SliceCurvedPrimitive(solid, planePoint, upn, keep, outAbove, outBelow, &handled, outWhy);
      if (handled)
        return ok;
      ok = SliceCylinderOblique(solid, planePoint, upn, keep, outAbove, outBelow, &handled, outWhy);
      if (handled)
        return ok;
      return Fail(Problem::SliceCurvedFace, outWhy);
    }
  }
  for (const Edge& e : solid.edges) {
    if (e.kind != CurveKind::Line)
      return Fail(Problem::SliceCurvedFace, outWhy);
  }

  const Vec3 pn = ray3d::Normalize(planeNormal);
  const double scale = ModelScale(solid);
  const double eps = 1e-7 * scale;
  auto sd = [&](const Vec3& p) { return ray3d::Dot(ray3d::Sub(p, planePoint), pn); };

  bool anyAbove = false;
  bool anyBelow = false;
  for (const Vertex& v : solid.vertices) {
    const double dv = sd(v.p);
    if (dv > eps)
      anyAbove = true;
    else if (dv < -eps)
      anyBelow = true;
  }
  if (!anyAbove || !anyBelow)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);

  std::vector<PolyFace> above;
  std::vector<PolyFace> below;
  std::vector<std::pair<Vec3, Vec3>> cutSegs;

  const double weldEps = std::max(1e-7 * scale, 1e-12);
  auto same = [&](const Vec3& a, const Vec3& b) { return ray3d::Length(ray3d::Sub(a, b)) <= weldEps; };
  auto addCutSeg = [&](const Vec3& a, const Vec3& b) {
    if (same(a, b))
      return;
    for (const auto& s : cutSegs) {
      if ((same(s.first, a) && same(s.second, b)) || (same(s.first, b) && same(s.second, a)))
        return;  // an on-plane edge is shared by two faces; record it once
    }
    cutSegs.push_back({a, b});
  };

  for (const Face& f : solid.faces) {
    std::vector<Vec3> P;
    for (const EdgeUse& u : f.loops[0].uses) {
      const Edge& e = solid.edges[static_cast<std::size_t>(u.edge)];
      const int startV = u.reversed ? e.v1 : e.v0;
      P.push_back(solid.vertices[static_cast<std::size_t>(startV)].p);
    }
    const std::size_t m = P.size();
    std::vector<double> d(m);
    bool fAbove = false;
    bool fBelow = false;
    for (std::size_t i = 0; i < m; ++i) {
      d[i] = sd(P[i]);
      if (d[i] > eps)
        fAbove = true;
      else if (d[i] < -eps)
        fBelow = true;
    }
    // A boundary edge lying IN the cutting plane is part of the cap loop, whichever side the face is
    // on. (The common case where the plane clips through a box edge.)
    for (std::size_t i = 0; i < m; ++i) {
      if (std::fabs(d[i]) <= eps && std::fabs(d[(i + 1) % m]) <= eps)
        addCutSeg(P[i], P[(i + 1) % m]);
    }
    if (!fBelow) {
      above.push_back(PolyFace{P, f.surface.frame.zAxis});
      continue;
    }
    if (!fAbove) {
      below.push_back(PolyFace{P, f.surface.frame.zAxis});
      continue;
    }

    std::vector<Vec3> ra;
    std::vector<Vec3> rb;
    std::vector<Vec3> cross;
    for (std::size_t i = 0; i < m; ++i) {
      const std::size_t j = (i + 1) % m;
      const double di = d[i];
      const double dj = d[j];
      if (di >= -eps)
        ra.push_back(P[i]);
      if (di <= eps)
        rb.push_back(P[i]);
      if ((di > eps && dj < -eps) || (di < -eps && dj > eps)) {
        const double t = di / (di - dj);
        const Vec3 x = ray3d::Add(P[i], ray3d::Scale(ray3d::Sub(P[j], P[i]), t));
        ra.push_back(x);
        rb.push_back(x);
        cross.push_back(x);
      } else if (std::fabs(di) <= eps && ((dj > eps) != (dj < -eps))) {
        cross.push_back(P[i]);
      }
    }
    if (cross.size() != 2)
      return Fail(Problem::SliceResultComplex, outWhy);
    if (ra.size() >= 3)
      above.push_back(PolyFace{ra, f.surface.frame.zAxis});
    if (rb.size() >= 3)
      below.push_back(PolyFace{rb, f.surface.frame.zAxis});
    addCutSeg(cross[0], cross[1]);
  }

  if (cutSegs.size() < 3)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);

  // Chain the cut segments into one loop.
  std::vector<Vec3> capRing;
  std::vector<char> used(cutSegs.size(), 0);
  capRing.push_back(cutSegs[0].first);
  capRing.push_back(cutSegs[0].second);
  used[0] = 1;
  for (std::size_t guard = 0; guard <= cutSegs.size() + 2; ++guard) {
    const Vec3 tail = capRing.back();
    if (capRing.size() > 2 && same(tail, capRing.front())) {
      capRing.pop_back();
      break;
    }
    bool found = false;
    for (std::size_t k = 0; k < cutSegs.size() && !found; ++k) {
      if (used[k])
        continue;
      if (same(cutSegs[k].first, tail)) {
        capRing.push_back(cutSegs[k].second);
        used[k] = 1;
        found = true;
      } else if (same(cutSegs[k].second, tail)) {
        capRing.push_back(cutSegs[k].first);
        used[k] = 1;
        found = true;
      }
    }
    if (!found)
      break;
  }
  for (char c : used) {
    if (!c)
      return Fail(Problem::SliceResultComplex, outWhy);  // cross-section is more than one loop
  }
  if (capRing.size() < 3)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);

  std::vector<Vec3> capAbove = capRing;
  if (RingSignedAreaAbout(capAbove, ray3d::Scale(pn, -1.0)) < 0.0)
    std::reverse(capAbove.begin(), capAbove.end());
  const std::vector<Vec3> capBelow(capAbove.rbegin(), capAbove.rend());

  const bool wantAbove = keep == SliceKeep::Above || keep == SliceKeep::Both;
  const bool wantBelow = keep == SliceKeep::Below || keep == SliceKeep::Both;

  if (wantAbove && outAbove) {
    std::vector<PolyFace> a = above;
    a.push_back(PolyFace{capAbove, ray3d::Scale(pn, -1.0)});
    if (!WeldPlanarSolid(a, scale, Problem::SliceResultComplex, outAbove, outWhy))
      return false;
  }
  if (wantBelow && outBelow) {
    std::vector<PolyFace> b = below;
    b.push_back(PolyFace{capBelow, pn});
    if (!WeldPlanarSolid(b, scale, Problem::SliceResultComplex, outBelow, outWhy))
      return false;
  }
  return Succeed(outWhy);
}

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

namespace {

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

/// A cheap, always-correct disjoint test: bounding boxes that do not touch cannot share volume.
[[nodiscard]] bool AabbsOverlap(const Solid& a, const Solid& b, double eps) {
  Vec3 amn, amx, bmn, bmx;
  SolidAabb(a, &amn, &amx);
  SolidAabb(b, &bmn, &bmx);
  return amn.x <= bmx.x + eps && bmn.x <= amx.x + eps && amn.y <= bmx.y + eps &&
         bmn.y <= amx.y + eps && amn.z <= bmx.z + eps && bmn.z <= amx.z + eps;
}

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

enum class BoolOp { Union, Subtract, Intersect };

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

// ---------------------------------------------------------------------------------------------
// Curved Boolean operands — the B1 subset, refined by D-2026-09-02-b: a curved operand is handled
// for UNION and INTERSECT only, and only when it is a right circular cylinder that meets the other
// solid along full circles. A curved SUBTRACT bores a hole whose wall faces inward, which
// `Surface` cannot express — deferred to B2, refused `BooleanCurvedFace` here. An oblique cylinder
// (an ellipse) is refused `BooleanObliqueCylinder`. Two configurations are recognised:
//   A. the cylinder's axis is perpendicular to two planar faces of the other solid, its circular
//      footprint clear inside both  — INTERSECT is the plug, UNION is a boss with the two faces bored;
//   B. two coaxial cylinders — INTERSECT is the shared segment, UNION is a merged or stepped stack.
// Anything else falls through and is refused by the planar path.
// ---------------------------------------------------------------------------------------------

struct CylinderShape {
  ucs::Ucs axis;        ///< origin = base-cap centre, zAxis = axis direction.
  double radius = 0.0;
  double length = 0.0;
};

/// Recognise \p s as one right circular cylinder: two cylinder half-faces at a seam plus two disk
/// caps. Read from the faces, not the recipe, so an extruded circle qualifies too.
[[nodiscard]] bool ClassifyCylinder(const Solid& s, CylinderShape* out) {
  if (s.faces.size() != 4 || s.vertices.size() != 4 || s.edges.size() != 6)
    return false;
  int firstCyl = -1;
  int nCyl = 0;
  int nPlane = 0;
  for (int i = 0; i < 4; ++i) {
    const SurfaceKind k = s.faces[static_cast<std::size_t>(i)].surface.kind;
    if (k == SurfaceKind::Cylinder) {
      ++nCyl;
      if (firstCyl < 0)
        firstCyl = i;
    } else if (k == SurfaceKind::Plane) {
      ++nPlane;
    } else {
      return false;
    }
  }
  if (nCyl != 2 || nPlane != 2)
    return false;
  const Surface& sf = s.faces[static_cast<std::size_t>(firstCyl)].surface;
  if (!(sf.radius > 0.0) || !(sf.height > 0.0))
    return false;
  const double sc = sf.radius + sf.height;
  for (int i = 0; i < 4; ++i) {
    const Surface& g = s.faces[static_cast<std::size_t>(i)].surface;
    if (g.kind != SurfaceKind::Cylinder)
      continue;
    if (std::fabs(g.radius - sf.radius) > 1e-9 * sc || std::fabs(g.height - sf.height) > 1e-9 * sc ||
        ray3d::Length(ray3d::Sub(g.frame.origin, sf.frame.origin)) > 1e-9 * sc ||
        std::fabs(std::fabs(ray3d::Dot(g.frame.zAxis, sf.frame.zAxis)) - 1.0) > 1e-9)
      return false;
  }
  ucs::Ucs ax;
  if (!ucs::FromNormal(sf.frame.origin, sf.frame.zAxis, &ax))
    return false;
  out->axis = ax;
  out->radius = sf.radius;
  out->length = sf.height;
  return true;
}

[[nodiscard]] bool AllFacesPlanar(const Solid& s) {
  for (const Face& f : s.faces)
    if (f.surface.kind != SurfaceKind::Plane)
      return false;
  for (const Edge& e : s.edges)
    if (e.kind != CurveKind::Line)
      return false;
  return true;
}

/// A coaxial stack of cylindrical bands: `z` holds `rOut.size()+1` ascending breakpoints along the
/// axis; band `i` (between `z[i]` and `z[i+1]`) is the annulus `[rIn[i], rOut[i]]`. `rIn` may be
/// empty (a solid stack) or parallel to `rOut` with a 0 meaning "solid". An `rIn[i] > 0` band gets
/// an **inward** inner wall — a bore. Built canonically and placed into \p frame (origin at `z[0]`).
[[nodiscard]] bool BuildCoaxialStack(const ucs::Ucs& frame, const std::vector<double>& z,
                                     const std::vector<double>& rOut, const std::vector<double>& rIn,
                                     Solid* out, Problem* outWhy) {
  const int n = static_cast<int>(rOut.size());
  if (n < 1 || static_cast<int>(z.size()) != n + 1 ||
      (!rIn.empty() && static_cast<int>(rIn.size()) != n))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  auto inAt = [&](int i) { return rIn.empty() ? 0.0 : rIn[static_cast<std::size_t>(i)]; };
  Solid s;
  const Vec3 up{0.0, 0.0, 1.0};
  const double z0 = z.front();
  std::map<std::pair<long long, long long>, std::pair<int, int>> vc;
  std::map<std::pair<long long, long long>, std::pair<int, int>> ec;
  auto key = [](double a, double b) {
    return std::make_pair(static_cast<long long>(std::llround(a * 1e7)),
                          static_cast<long long>(std::llround(b * 1e7)));
  };
  auto verts = [&](double zz, double rr) -> std::pair<int, int> {
    const auto k = key(zz, rr);
    const auto it = vc.find(k);
    if (it != vc.end())
      return it->second;
    const int p = AddVertex(&s, Vec3{rr, 0.0, zz - z0});
    const int m = AddVertex(&s, Vec3{-rr, 0.0, zz - z0});
    return vc[k] = {p, m};
  };
  auto circle = [&](double zz, double rr) -> std::pair<int, int> {
    const auto k = key(zz, rr);
    const auto it = ec.find(k);
    if (it != ec.end())
      return it->second;
    const auto v = verts(zz, rr);
    const Vec3 c{0.0, 0.0, zz - z0};
    const int e0 = AddArc(&s, v.first, v.second, c, up, kPi);   // +x -> -x, CCW about +z
    const int e1 = AddArc(&s, v.second, v.first, c, up, kPi);   // -x -> +x
    return ec[k] = {e0, e1};
  };
  // A cylinder wall (outer, +radial) or a bore wall (inner, −radial/inward), radius rr, z in
  // [zl,zh]. `inwardWall` picks which.
  auto addWall = [&](double zl, double zh, double rr, bool inwardWall) {
    const auto cb = circle(zl, rr);
    const auto ct = circle(zh, rr);
    const auto vb = verts(zl, rr);
    const auto vt = verts(zh, rr);
    const int sm0 = AddLine(&s, vb.first, vt.first);
    const int sm1 = AddLine(&s, vb.second, vt.second);
    auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
      Face f;
      f.surface.kind = SurfaceKind::Cylinder;
      f.surface.frame = ucs::Ucs{};
      f.surface.frame.origin = Vec3{0.0, 0.0, zl - z0};
      f.surface.radius = rr;
      f.surface.radius2 = rr;
      f.surface.height = zh - zl;
      f.surface.inward = inwardWall;
      f.uStart = u0;
      f.uEnd = u1;
      Loop lp;
      lp.uses = std::move(uses);
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    };
    if (inwardWall) {  // the boundary loop runs the opposite way, so every rim edge is used once each way
      wall(0.0, kPi, {{sm0, false}, {ct.first, false}, {sm1, true}, {cb.first, true}});
      wall(kPi, kTwoPi, {{sm1, false}, {ct.second, false}, {sm0, true}, {cb.second, true}});
    } else {
      wall(0.0, kPi, {{cb.first, false}, {sm1, false}, {ct.first, true}, {sm0, true}});
      wall(kPi, kTwoPi, {{cb.second, false}, {sm0, false}, {ct.second, true}, {sm1, true}});
    }
  };
  for (int i = 0; i < n; ++i) {
    addWall(z[static_cast<std::size_t>(i)], z[static_cast<std::size_t>(i + 1)],
            rOut[static_cast<std::size_t>(i)], /*inwardWall=*/false);
    if (inAt(i) > 0.0)
      addWall(z[static_cast<std::size_t>(i)], z[static_cast<std::size_t>(i + 1)], inAt(i),
              /*inwardWall=*/true);
  }
  // A horizontal annular face between radii [inner, outer] at height zz; `matBelow` true when the
  // material is on the −z side (so the face's outward normal is +z), false for +z-side material.
  auto addRing = [&](double zz, double inner, double outer, bool matBelow) {
    if (!(outer - inner > 1e-9))
      return;
    const Vec3 nrm = matBelow ? up : Vec3{0.0, 0.0, -1.0};
    const auto co = circle(zz, outer);
    Face f = MakePlaneFace(Vec3{0.0, 0.0, zz - z0}, nrm, {});
    f.loops.clear();
    Loop outerL;
    if (matBelow)
      outerL.uses = {{co.first, false}, {co.second, false}};
    else
      outerL.uses = {{co.second, true}, {co.first, true}};
    f.loops.push_back(std::move(outerL));
    if (inner > 1e-9) {
      const auto ci = circle(zz, inner);
      Loop innerL;
      if (matBelow)
        innerL.uses = {{ci.second, true}, {ci.first, true}};
      else
        innerL.uses = {{ci.first, false}, {ci.second, false}};
      f.loops.push_back(std::move(innerL));
    }
    s.faces.push_back(std::move(f));
  };
  for (int k = 0; k <= n; ++k) {
    const double zz = z[static_cast<std::size_t>(k)];
    const double outL = (k > 0) ? rOut[static_cast<std::size_t>(k - 1)] : 0.0;
    const double outH = (k < n) ? rOut[static_cast<std::size_t>(k)] : 0.0;
    const double inL = (k > 0) ? inAt(k - 1) : 0.0;
    const double inH = (k < n) ? inAt(k) : 0.0;
    if (k == 0) {
      addRing(zz, inH, outH, /*matBelow=*/false);  // bottom cap, material above
    } else if (k == n) {
      addRing(zz, inL, outL, /*matBelow=*/true);  // top cap, material below
    } else {
      // Outer transition: the wider band's material overhangs the thinner one.
      if (std::fabs(outL - outH) > 1e-9)
        addRing(zz, std::min(outL, outH), std::max(outL, outH), /*matBelow=*/outL > outH);
      // Inner transition: the band with the smaller bore has material where the other is void.
      if (std::fabs(inL - inH) > 1e-9)
        addRing(zz, std::min(inL, inH), std::max(inL, inH), /*matBelow=*/inL < inH);
    }
  }
  AddSingleShell(&s);
  PlaceInFrame(&s, frame);
  if (Validate(s) != Problem::Ok)
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// One stub of a boss: the planar face to bore, the bore centre on it, the outward direction, and
/// the stub length.
struct BossStub {
  int face = 0;
  Vec3 centre;
  Vec3 dir;
  double length = 0.0;
};

/// \p planar with a cylindrical \p radius boss added at each stub: the face is bored open (an inner
/// circular loop) and a cylinder + end cap carry the material outward.
[[nodiscard]] bool BuildBoss(const Solid& planar, const std::vector<BossStub>& stubs, double radius,
                             Solid* out, Problem* outWhy) {
  Solid s = planar;
  s.recipe = Recipe{};  // a Boolean result carries no recipe (REQ-314)
  for (const BossStub& stub : stubs) {
    const Vec3 dir = ray3d::Normalize(stub.dir);
    Vec3 xa = s.faces[static_cast<std::size_t>(stub.face)].surface.frame.xAxis;
    xa = ray3d::Sub(xa, ray3d::Scale(dir, ray3d::Dot(xa, dir)));
    if (!(ray3d::Length(xa) > 1e-9))
      xa = s.faces[static_cast<std::size_t>(stub.face)].surface.frame.yAxis;
    xa = ray3d::Normalize(xa);
    const Vec3 ya = ray3d::Normalize(ray3d::Cross(dir, xa));
    const Vec3 capC = ray3d::Add(stub.centre, ray3d::Scale(dir, stub.length));
    const int b0 = AddVertex(&s, ray3d::Add(stub.centre, ray3d::Scale(xa, radius)));
    const int b1 = AddVertex(&s, ray3d::Add(stub.centre, ray3d::Scale(xa, -radius)));
    const int t0 = AddVertex(&s, ray3d::Add(capC, ray3d::Scale(xa, radius)));
    const int t1 = AddVertex(&s, ray3d::Add(capC, ray3d::Scale(xa, -radius)));
    const int rb0 = AddArc(&s, b0, b1, stub.centre, dir, kPi);
    const int rb1 = AddArc(&s, b1, b0, stub.centre, dir, kPi);
    const int rt0 = AddArc(&s, t0, t1, capC, dir, kPi);
    const int rt1 = AddArc(&s, t1, t0, capC, dir, kPi);
    const int sm0 = AddLine(&s, b0, t0);
    const int sm1 = AddLine(&s, b1, t1);
    s.faces.push_back(MakePlaneFace(capC, dir, {{rt0, false}, {rt1, false}}));
    auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
      Face f;
      f.surface.kind = SurfaceKind::Cylinder;
      f.surface.frame.origin = stub.centre;
      f.surface.frame.xAxis = xa;
      f.surface.frame.yAxis = ya;
      f.surface.frame.zAxis = dir;
      f.surface.radius = radius;
      f.surface.radius2 = radius;
      f.surface.height = stub.length;
      f.uStart = u0;
      f.uEnd = u1;
      Loop lp;
      lp.uses = std::move(uses);
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    };
    wall(0.0, kPi, {{rb0, false}, {sm1, false}, {rt0, true}, {sm0, true}});
    wall(kPi, kTwoPi, {{rb1, false}, {sm0, false}, {rt1, true}, {sm1, true}});
    // Bore the face: an inner loop wound opposite the outer one (the missing near-end cap's loop).
    s.faces[static_cast<std::size_t>(stub.face)].loops.push_back(Loop{{{rb1, true}, {rb0, true}}});
  }
  for (int i = static_cast<int>(planar.faces.size()); i < static_cast<int>(s.faces.size()); ++i)
    s.shells[0].faces.push_back(i);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// \p planar with a cylindrical bore of \p radius removed (REQ-314 B2a — a curved SUBTRACT). The
/// entry face is bored open at \p entryC; if \p through, the exit face is too and an inward cylinder
/// wall spans the two; otherwise the bore is blind, \p blindDepth deep, closed by a planar floor.
/// \p nEntry is the entry face's outward normal; the bore runs along `-nEntry` into the solid.
[[nodiscard]] bool BuildBore(const Solid& planar, int entryFace, const Vec3& entryC, const Vec3& nEntry,
                             double radius, bool through, int exitFace, const Vec3& exitC,
                             double blindDepth, Solid* out, Problem* outWhy) {
  const Vec3 nE = ray3d::Normalize(nEntry);
  const Vec3 dir = ray3d::Scale(nE, -1.0);  // into the solid
  Solid s = planar;
  s.recipe = Recipe{};
  Vec3 xa = s.faces[static_cast<std::size_t>(entryFace)].surface.frame.xAxis;
  xa = ray3d::Sub(xa, ray3d::Scale(nE, ray3d::Dot(xa, nE)));
  if (!(ray3d::Length(xa) > 1e-9))
    xa = s.faces[static_cast<std::size_t>(entryFace)].surface.frame.yAxis;
  xa = ray3d::Normalize(xa);
  const Vec3 ya = ray3d::Normalize(ray3d::Cross(dir, xa));
  const double h = through ? ray3d::Length(ray3d::Sub(exitC, entryC)) : blindDepth;
  if (!(h > 1e-9) || !(radius > 0.0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 farC = ray3d::Add(entryC, ray3d::Scale(dir, h));
  const int e0 = AddVertex(&s, ray3d::Add(entryC, ray3d::Scale(xa, radius)));
  const int e1 = AddVertex(&s, ray3d::Add(entryC, ray3d::Scale(xa, -radius)));
  const int f0 = AddVertex(&s, ray3d::Add(farC, ray3d::Scale(xa, radius)));
  const int f1 = AddVertex(&s, ray3d::Add(farC, ray3d::Scale(xa, -radius)));
  // All four rim arcs are built about the ENTRY face's outward normal, so "forward" means the same
  // angular sense at both ends — the BuildBoss convention.
  const int re0 = AddArc(&s, e0, e1, entryC, nE, kPi);
  const int re1 = AddArc(&s, e1, e0, entryC, nE, kPi);
  const int rf0 = AddArc(&s, f0, f1, farC, nE, kPi);
  const int rf1 = AddArc(&s, f1, f0, farC, nE, kPi);
  const int sm0 = AddLine(&s, e0, f0);
  const int sm1 = AddLine(&s, e1, f1);
  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face fc;
    fc.surface.kind = SurfaceKind::Cylinder;
    fc.surface.frame.origin = entryC;
    fc.surface.frame.xAxis = xa;
    fc.surface.frame.yAxis = ya;
    fc.surface.frame.zAxis = dir;
    fc.surface.radius = radius;
    fc.surface.radius2 = radius;
    fc.surface.height = h;
    fc.surface.inward = true;
    fc.uStart = u0;
    fc.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    fc.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(fc));
  };
  wall(0.0, kPi, {{re0, false}, {sm1, false}, {rf0, true}, {sm0, true}});
  wall(kPi, kTwoPi, {{re1, false}, {sm0, false}, {rf1, true}, {sm1, true}});
  s.faces[static_cast<std::size_t>(entryFace)].loops.push_back(Loop{{{re1, true}, {re0, true}}});
  if (through)
    s.faces[static_cast<std::size_t>(exitFace)].loops.push_back(Loop{{{rf0, false}, {rf1, false}}});
  else
    s.faces.push_back(MakePlaneFace(farC, nE, {{rf0, false}, {rf1, false}}));  // floor faces back out
  for (int i = static_cast<int>(planar.faces.size()); i < static_cast<int>(s.faces.size()); ++i)
    s.shells[0].faces.push_back(i);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// The ellipse where a plane (through \p planePt, unit normal \p pn) cuts the cylinder of radius
/// \p r about \p axisFrame. Fills the world centre / major direction / oriented normal / semi-axes.
struct CylEllipse {
  Vec3 centre;
  Vec3 majorDir;
  Vec3 normal;  // oriented so normal·axis > 0
  double a = 0.0;
  double b = 0.0;
  double alpha = 0.0;  // z_plane on the axis, in the axis frame
};
[[nodiscard]] bool EllipseFromPlane(const ucs::Ucs& axisFrame, double r, const Vec3& planePt,
                                    const Vec3& pn, CylEllipse* out) {
  const Vec3 Z = axisFrame.zAxis;
  const double dotNZ = ray3d::Dot(pn, Z);
  if (std::fabs(dotNZ) < 1e-6)
    return false;
  const double tAxis = ray3d::Dot(ray3d::Sub(planePt, axisFrame.origin), pn) / dotNZ;
  out->centre = ray3d::Add(axisFrame.origin, ray3d::Scale(Z, tAxis));
  out->alpha = tAxis;
  const Vec3 minorDir = ray3d::Normalize(ray3d::Cross(pn, Z));
  out->majorDir = ray3d::Normalize(ray3d::Cross(pn, minorDir));
  out->normal = dotNZ > 0.0 ? pn : ray3d::Scale(pn, -1.0);
  out->a = r / std::fabs(dotNZ);
  out->b = r;
  return true;
}

/// A cylinder segment of radius \p r about \p axisFrame bounded by two oblique planes — the plug of
/// an INTERSECT of a tilted cylinder with a planar-faced solid (REQ-314 B2b-1). Both ends are
/// elliptical; no flat cap.
[[nodiscard]] bool BuildObliqueCylinderPlug(const ucs::Ucs& axisFrame, double r, const Vec3& p1,
                                            const Vec3& n1, const Vec3& p2, const Vec3& n2, Solid* out,
                                            Problem* outWhy) {
  CylEllipse eA;
  CylEllipse eB;
  if (!EllipseFromPlane(axisFrame, r, p1, n1, &eA) || !EllipseFromPlane(axisFrame, r, p2, n2, &eB))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const CylEllipse& lo = eA.alpha <= eB.alpha ? eA : eB;
  const CylEllipse& hi = eA.alpha <= eB.alpha ? eB : eA;
  if (hi.alpha - lo.alpha < 1e-9)
    return Fail(Problem::BooleanEmptyResult, outWhy);
  const Vec3 Z = axisFrame.zAxis;
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(axisFrame, Vec3{x, y, z}); };
  // z_plane(u=0) = alpha + beta ; here beta = -r * (n·X)/(n·Z). Recover it for the seam heights.
  auto zAtSeam = [&](const CylEllipse& e, double sx) {
    const Vec3 nl{ray3d::Dot(e.normal, axisFrame.xAxis), ray3d::Dot(e.normal, axisFrame.yAxis),
                  ray3d::Dot(e.normal, Z)};
    return e.alpha - r * nl.x / nl.z * sx;  // sx = cos u  (±1 at the seam)
  };
  Solid s;
  const int lo0 = AddVertex(&s, W(r, 0.0, zAtSeam(lo, 1.0)));
  const int lo1 = AddVertex(&s, W(-r, 0.0, zAtSeam(lo, -1.0)));
  const int hi0 = AddVertex(&s, W(r, 0.0, zAtSeam(hi, 1.0)));
  const int hi1 = AddVertex(&s, W(-r, 0.0, zAtSeam(hi, -1.0)));
  const int elLo0 = AddEllipse(&s, lo0, lo1, lo.centre, lo.normal, lo.majorDir, lo.a, lo.b, kPi);
  const int elLo1 = AddEllipse(&s, lo1, lo0, lo.centre, lo.normal, lo.majorDir, lo.a, lo.b, kPi);
  const int elHi0 = AddEllipse(&s, hi0, hi1, hi.centre, hi.normal, hi.majorDir, hi.a, hi.b, kPi);
  const int elHi1 = AddEllipse(&s, hi1, hi0, hi.centre, hi.normal, hi.majorDir, hi.a, hi.b, kPi);
  const int sm0 = AddLine(&s, lo0, hi0);
  const int sm1 = AddLine(&s, lo1, hi1);
  s.faces.push_back(
      MakePlaneFace(lo.centre, ray3d::Scale(lo.normal, -1.0), {{elLo1, true}, {elLo0, true}}));
  s.faces.push_back(MakePlaneFace(hi.centre, hi.normal, {{elHi0, false}, {elHi1, false}}));
  auto side = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = axisFrame;
    f.surface.frame.origin = lo.centre;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = hi.alpha - lo.alpha;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  side(0.0, kPi, {{elLo0, false}, {sm1, false}, {elHi0, true}, {sm0, true}});
  side(kPi, kTwoPi, {{elLo1, false}, {sm0, false}, {elHi1, true}, {sm1, true}});
  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// \p planar with a **tilted** cylindrical bore of radius \p r removed along \p axisFrame — the
/// elliptical-mouthed SUBTRACT of a tilted cylinder that crosses faces \p fA and \p fB
/// (REQ-314 B2b-1). Each face is bored with an ellipse; an inward cylinder wall spans the two.
[[nodiscard]] bool BuildTiltedBore(const Solid& planar, int fA, const Vec3& nA, int fB, const Vec3& nB,
                                   const ucs::Ucs& axisFrame, double r, Solid* out, Problem* outWhy) {
  Solid s = planar;
  s.recipe = Recipe{};
  const Vec3 Z = axisFrame.zAxis;
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(axisFrame, Vec3{x, y, z}); };

  struct FaceEll {
    int face;
    CylEllipse e;
    Vec3 s0;  // world ellipse ∩ seam at cyl u = 0
    Vec3 s1;  // ... at u = pi
    int e0 = -1;
    int e1 = -1;  // half-ellipse arcs, s0->s1 and s1->s0
    double sweepSign = 1.0;
  };
  auto prep = [&](int face, const Vec3& faceN) -> FaceEll {
    FaceEll fe{face, {}, {}, {}, -1, -1, 1.0};
    const Vec3 pt = s.vertices[static_cast<std::size_t>(
                                   s.edges[static_cast<std::size_t>(s.faces[static_cast<std::size_t>(face)]
                                                                        .loops[0]
                                                                        .uses[0]
                                                                        .edge)]
                                       .v0)]
                        .p;
    (void)EllipseFromPlane(axisFrame, r, pt, faceN, &fe.e);
    // seam heights in the axis frame: z_plane(u) = alpha + beta cos u ; beta = -r*(n·X)/(n·Z).
    const Vec3 nl{ray3d::Dot(fe.e.normal, axisFrame.xAxis), ray3d::Dot(fe.e.normal, axisFrame.yAxis),
                  ray3d::Dot(fe.e.normal, Z)};
    const double beta = -r * nl.x / nl.z;
    const double gamma = -r * nl.y / nl.z;
    fe.s0 = W(r, 0.0, fe.e.alpha + beta);
    fe.s1 = W(-r, 0.0, fe.e.alpha - beta);
    (void)gamma;
    // the ellipse edge is built about the FACE outward normal, so the bored-face hole winds right.
    fe.sweepSign = ray3d::Dot(faceN, Z) > 0.0 ? 1.0 : -1.0;
    return fe;
  };
  FaceEll a = prep(fA, nA);
  FaceEll b = prep(fB, nB);
  // order lo/hi by axis param so the inward wall's two ellipse ends match CylinderCutZExtent.
  if (a.e.alpha > b.e.alpha)
    std::swap(a, b);

  auto addArcs = [&](FaceEll& fe, const Vec3& faceN) {
    const int v0 = AddVertex(&s, fe.s0);
    const int v1 = AddVertex(&s, fe.s1);
    fe.e0 = AddEllipse(&s, v0, v1, fe.e.centre, faceN, fe.e.majorDir, fe.e.a, fe.e.b,
                       fe.sweepSign * kPi);
    fe.e1 = AddEllipse(&s, v1, v0, fe.e.centre, faceN, fe.e.majorDir, fe.e.a, fe.e.b,
                       fe.sweepSign * kPi);
  };
  addArcs(a, a.face == fA ? nA : nB);
  addArcs(b, b.face == fA ? nA : nB);

  const int sm0 = AddLine(&s, s.edges[static_cast<std::size_t>(a.e0)].v0,
                          s.edges[static_cast<std::size_t>(b.e0)].v0);  // +x seam, lo→hi
  const int sm1 = AddLine(&s, s.edges[static_cast<std::size_t>(a.e1)].v0,
                          s.edges[static_cast<std::size_t>(b.e1)].v0);  // -x seam

  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = axisFrame;
    f.surface.frame.origin = a.e.centre;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = b.e.alpha - a.e.alpha;
    f.surface.inward = true;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  // Same edge-use pattern as BuildBore's inward wall.
  wall(0.0, kPi, {{sm0, false}, {b.e0, false}, {sm1, true}, {a.e0, true}});
  wall(kPi, kTwoPi, {{sm1, false}, {b.e1, false}, {sm0, true}, {a.e1, true}});
  // Bore each face with the ellipse, wound opposite the wall's use of that arc.
  s.faces[static_cast<std::size_t>(a.face)].loops.push_back(Loop{{{a.e0, false}, {a.e1, false}}});
  s.faces[static_cast<std::size_t>(b.face)].loops.push_back(Loop{{{b.e1, true}, {b.e0, true}}});
  for (int i = static_cast<int>(planar.faces.size()); i < static_cast<int>(s.faces.size()); ++i)
    s.shells[0].faces.push_back(i);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// Append one boss stub for a tilted cylinder poking through planar face \p faceIdx: the piece of
/// the cylinder between its own circular cap and the elliptical mouth on the face — a faithful port
/// of `SliceCylinderOblique`'s piece, minus the ellipse cap face (the face's inner loop replaces it).
/// \p isEntry true = the stub runs from the cylinder base to the face; false = the face to the top.
[[nodiscard]] bool AddTiltedStub(Solid* s, const Solid& planar, int faceIdx, const Vec3& faceN,
                                 const CylinderShape& C, bool isEntry, Problem* outWhy) {
  const ucs::Ucs& fr = C.axis;
  const Vec3 Z = fr.zAxis;
  const double r = C.radius;
  const double h = C.length;  // used only as a fallback; the real bounds come from CylinderCutZExtent
  const Vec3 pn = faceN;
  const double dotNZ = ray3d::Dot(pn, Z);
  if (std::fabs(dotNZ) < 1e-6)
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 facePt =
      planar.vertices[static_cast<std::size_t>(
                          planar.edges[static_cast<std::size_t>(
                                           planar.faces[static_cast<std::size_t>(faceIdx)].loops[0].uses[0].edge)]
                              .v0)]
          .p;
  const Vec3 pl = ucs::WorldToUcs(fr, facePt);
  const Vec3 nl{ray3d::Dot(pn, fr.xAxis), ray3d::Dot(pn, fr.yAxis), dotNZ};
  const double a0 = (nl.x * pl.x + nl.y * pl.y + nl.z * pl.z) / nl.z;
  const double a1 = -r * nl.x / nl.z;
  const double a2 = -r * nl.y / nl.z;
  const double amp = std::sqrt(a1 * a1 + a2 * a2);
  const double capZ = isEntry ? 0.0 : h;
  // The elliptical mouth must lie strictly between the cap and infinity on the stub side.
  if (isEntry ? (a0 - amp <= 1e-9) : (a0 + amp >= h - 1e-9))
    return Fail(Problem::BooleanResultInvalid, outWhy);

  const Vec3 ec = ray3d::Add(fr.origin, ray3d::Scale(Z, a0));
  const Vec3 minorDir = ray3d::Normalize(ray3d::Cross(pn, Z));
  const Vec3 majorDir = ray3d::Normalize(ray3d::Cross(pn, minorDir));
  const double ea = r / std::fabs(dotNZ);
  const double eb = r;
  const Vec3 eN = dotNZ > 0.0 ? pn : ray3d::Scale(pn, -1.0);
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(fr, Vec3{x, y, z}); };
  const double zp0 = a0 + a1;
  const double zpP = a0 - a1;
  const bool upper = !isEntry;  // "upper" (rim at z = h) mirrors SliceCylinderOblique's exit piece

  const int se0 = AddVertex(s, W(r, 0.0, zp0));
  const int se1 = AddVertex(s, W(-r, 0.0, zpP));
  const int rimZ0 = AddVertex(s, W(r, 0.0, capZ));
  const int rimZ1 = AddVertex(s, W(-r, 0.0, capZ));
  const Vec3 rimC = W(0.0, 0.0, capZ);
  const int rr0 = AddArc(s, rimZ0, rimZ1, rimC, Z, kPi);
  const int rr1 = AddArc(s, rimZ1, rimZ0, rimC, Z, kPi);
  const int el0 = AddEllipse(s, se0, se1, ec, eN, majorDir, ea, eb, kPi);
  const int el1 = AddEllipse(s, se1, se0, ec, eN, majorDir, ea, eb, kPi);
  const int sm0 = AddLine(s, upper ? se0 : rimZ0, upper ? rimZ0 : se0);
  const int sm1 = AddLine(s, upper ? se1 : rimZ1, upper ? rimZ1 : se1);

  if (upper)
    s->faces.push_back(MakePlaneFace(rimC, Z, {{rr0, false}, {rr1, false}}));
  else
    s->faces.push_back(MakePlaneFace(rimC, ray3d::Scale(Z, -1.0), {{rr1, true}, {rr0, true}}));

  auto side = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = fr;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = h;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s->faces.push_back(std::move(f));
  };
  if (upper) {
    side(0.0, kPi, {{el0, false}, {sm1, false}, {rr0, true}, {sm0, true}});
    side(kPi, kTwoPi, {{el1, false}, {sm0, false}, {rr1, true}, {sm1, true}});
    s->faces[static_cast<std::size_t>(faceIdx)].loops.push_back(Loop{{{el1, true}, {el0, true}}});
  } else {
    side(0.0, kPi, {{rr0, false}, {sm1, false}, {el0, true}, {sm0, true}});
    side(kPi, kTwoPi, {{rr1, false}, {sm0, false}, {el1, true}, {sm1, true}});
    s->faces[static_cast<std::size_t>(faceIdx)].loops.push_back(Loop{{{el0, false}, {el1, false}}});
  }
  return Succeed(outWhy);
}

/// \p planar UNION a tilted cylinder that crosses faces \p fA / \p fB at params \p tA / \p tB: each
/// face bored with an ellipse, the cylinder stubs that stick out past each face added (REQ-314 B2b-1).
[[nodiscard]] bool BuildTiltedBoss(const Solid& planar, int fA, const Vec3& nA, double tA, int fB,
                                   const Vec3& nB, double tB, const CylinderShape& C,
                                   std::vector<Solid>* out, Problem* outWhy) {
  Solid s = planar;
  s.recipe = Recipe{};
  const double slack = 1e-7 * (C.radius + C.length);
  const bool aFirst = tA <= tB;
  const int f1 = aFirst ? fA : fB;
  const Vec3 n1 = aFirst ? nA : nB;
  const double t1 = aFirst ? tA : tB;
  const int f2 = aFirst ? fB : fA;
  const Vec3 n2 = aFirst ? nB : nA;
  const double t2 = aFirst ? tB : tA;
  int added = 0;
  if (t1 > slack) {  // a stub from the cylinder base up to face f1
    if (!AddTiltedStub(&s, planar, f1, n1, C, /*isEntry=*/true, outWhy))
      return false;
    ++added;
  }
  if (C.length - t2 > slack) {  // a stub from face f2 out to the cylinder top
    if (!AddTiltedStub(&s, planar, f2, n2, C, /*isEntry=*/false, outWhy))
      return false;
    ++added;
  }
  if (added == 0) {
    out->push_back(planar);
    return Succeed(outWhy);
  }
  for (int i = static_cast<int>(planar.faces.size()); i < static_cast<int>(s.faces.size()); ++i)
    s.shells[0].faces.push_back(i);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  out->push_back(std::move(s));
  return Succeed(outWhy);
}

struct SphereShape {
  Vec3 centre;
  double radius = 0.0;
};

/// Recognise \p s as one sphere (two longitude half-faces, two pole vertices, two meridian seams).
[[nodiscard]] bool ClassifySphere(const Solid& s, SphereShape* out) {
  if (s.faces.size() != 2 || s.vertices.size() != 2 || s.edges.size() != 2)
    return false;
  for (const Face& f : s.faces)
    if (f.surface.kind != SurfaceKind::Sphere)
      return false;
  const Surface& sf = s.faces[0].surface;
  if (!(sf.radius > 0.0))
    return false;
  out->centre = sf.frame.origin;
  out->radius = sf.radius;
  return true;
}

/// The cap of a sphere of \p radius centred at `frame.origin` on the **+`frame.zAxis`** side of the
/// plane at height \p cutZ along that axis (`|cutZ| < radius`). Two longitude half-faces of the
/// sphere plus one planar disk closing the cut. Built canonically and placed into \p frame.
[[nodiscard]] bool BuildSphericalCap(const ucs::Ucs& frame, double radius, double cutZ, Solid* out,
                                     Problem* outWhy) {
  if (!(radius > 0.0) || !(std::fabs(cutZ) < radius))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double vc = std::asin(std::clamp(cutZ / radius, -1.0, 1.0));
  const double rc = std::sqrt(std::max(0.0, radius * radius - cutZ * cutZ));
  Solid s;
  const Vec3 origin{0.0, 0.0, 0.0};
  const int c0 = AddVertex(&s, Vec3{rc, 0.0, cutZ});
  const int c1 = AddVertex(&s, Vec3{-rc, 0.0, cutZ});
  const int np = AddVertex(&s, Vec3{0.0, 0.0, radius});
  const int cc0 = AddArc(&s, c0, c1, Vec3{0.0, 0.0, cutZ}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int cc1 = AddArc(&s, c1, c0, Vec3{0.0, 0.0, cutZ}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int md0 = AddArc(&s, c0, np, origin, Vec3{0.0, -1.0, 0.0}, kHalfPi - vc);
  const int md1 = AddArc(&s, c1, np, origin, Vec3{0.0, 1.0, 0.0}, kHalfPi - vc);
  auto sphereHalf = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Sphere;
    f.surface.radius = radius;
    f.uStart = u0;
    f.uEnd = u1;
    f.vStart = vc;
    f.vEnd = kHalfPi;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  sphereHalf(0.0, kPi, {{md1, false}, {md0, true}, {cc0, false}});
  sphereHalf(kPi, kTwoPi, {{md0, false}, {md1, true}, {cc1, false}});
  s.faces.push_back(
      MakePlaneFace(Vec3{0.0, 0.0, cutZ}, Vec3{0.0, 0.0, -1.0}, {{cc1, true}, {cc0, true}}));
  AddSingleShell(&s);
  PlaceInFrame(&s, frame);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// \p planar with a spherical-cap boss on face \p faceIdx: the face is bored open at the cut circle
/// and the sphere cap (no disk) carries the material outward. \p axis points out of \p planar (the
/// face's outward normal); \p cutZ is the plane's height along \p axis from \p centre.
/// \p planar with a spherical cap on face \p faceIdx: the face is bored open at the cut circle and
/// the cap (no disk) carries geometry along +\p axis to a pole. `inward` marks the cap faces as a
/// SUBTRACT dimple (material on the −radial side) and reverses the loop winding to match.
[[nodiscard]] bool BuildSphereBoss(const Solid& planar, int faceIdx, const Vec3& centre, const Vec3& axis,
                                   double radius, double cutZ, bool inward, Solid* out, Problem* outWhy) {
  if (!(radius > 0.0) || !(std::fabs(cutZ) < radius))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  ucs::Ucs fr;
  if (!ucs::FromNormal(centre, ray3d::Normalize(axis), &fr))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double vc = std::asin(std::clamp(cutZ / radius, -1.0, 1.0));
  const double rc = std::sqrt(std::max(0.0, radius * radius - cutZ * cutZ));
  auto W = [&](double x, double y, double z) {
    return ray3d::Add(fr.origin, ray3d::Add(ray3d::Add(ray3d::Scale(fr.xAxis, x), ray3d::Scale(fr.yAxis, y)),
                                            ray3d::Scale(fr.zAxis, z)));
  };
  Solid s = planar;
  s.recipe = Recipe{};
  const int c0 = AddVertex(&s, W(rc, 0.0, cutZ));
  const int c1 = AddVertex(&s, W(-rc, 0.0, cutZ));
  const int np = AddVertex(&s, W(0.0, 0.0, radius));
  const Vec3 cutC = W(0.0, 0.0, cutZ);
  const int cc0 = AddArc(&s, c0, c1, cutC, fr.zAxis, kPi);
  const int cc1 = AddArc(&s, c1, c0, cutC, fr.zAxis, kPi);
  const int md0 = AddArc(&s, c0, np, fr.origin, ray3d::Scale(fr.yAxis, -1.0), kHalfPi - vc);
  const int md1 = AddArc(&s, c1, np, fr.origin, fr.yAxis, kHalfPi - vc);
  auto sphereHalf = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Sphere;
    f.surface.frame = fr;
    f.surface.radius = radius;
    f.surface.inward = inward;
    f.uStart = u0;
    f.uEnd = u1;
    f.vStart = vc;
    f.vEnd = kHalfPi;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  if (inward) {
    // The cap faces the −radial side (a SUBTRACT dimple): reverse each half-face loop, and bore the
    // planar face with the opposite circle winding so every edge is still used once each way.
    sphereHalf(0.0, kPi, {{cc0, true}, {md0, false}, {md1, true}});
    sphereHalf(kPi, kTwoPi, {{cc1, true}, {md1, false}, {md0, true}});
    s.faces[static_cast<std::size_t>(faceIdx)].loops.push_back(Loop{{{cc0, false}, {cc1, false}}});
  } else {
    sphereHalf(0.0, kPi, {{md1, false}, {md0, true}, {cc0, false}});
    sphereHalf(kPi, kTwoPi, {{md0, false}, {md1, true}, {cc1, false}});
    s.faces[static_cast<std::size_t>(faceIdx)].loops.push_back(Loop{{{cc1, true}, {cc0, true}}});
  }
  for (int i = static_cast<int>(planar.faces.size()); i < static_cast<int>(s.faces.size()); ++i)
    s.shells[0].faces.push_back(i);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

[[nodiscard]] bool TryBooleanSpherePlanar(const Solid& planar, const Solid& sph, const SphereShape& S,
                                          BoolOp op, bool sphIsMinuend, std::vector<Solid>* out,
                                          bool* handled, Problem* outWhy) {
  *handled = true;
  if (op == BoolOp::Subtract && sphIsMinuend)
    return Fail(Problem::BooleanCurvedFace, outWhy);  // sphere − box: its own slice
  const bool dimple = op == BoolOp::Subtract;  // box − sphere: a spherical dimple (B2a)
  const double scale = std::max(ModelScale(planar), S.radius);
  const double eps = 1e-7 * scale;

  int cutFace = -1;
  int cleanCount = 0;
  bool messy = false;
  for (int fi = 0; fi < static_cast<int>(planar.faces.size()); ++fi) {
    const Face& f = planar.faces[static_cast<std::size_t>(fi)];
    if (f.surface.kind != SurfaceKind::Plane)
      continue;
    const std::vector<Vec3> ring = FaceRing(planar, f);
    if (ring.size() < 3)
      continue;
    const Vec3 n = f.surface.frame.zAxis;
    const double d = ray3d::Dot(ray3d::Sub(S.centre, ring[0]), n);  // centre distance along outward n
    if (std::fabs(d) >= S.radius - eps)
      continue;  // this plane does not slice the sphere
    const Vec3 hp = ray3d::Sub(S.centre, ray3d::Scale(n, d));  // sphere centre projected to the plane
    const double rc = std::sqrt(std::max(0.0, S.radius * S.radius - d * d));  // cut-circle radius
    bool onEdge = false;
    const bool inFace = PointInPolygon3D(hp, ring, n, eps, &onEdge);
    double nearest = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < ring.size(); ++i) {
      const Vec3& p0 = ring[i];
      const Vec3& p1 = ring[(i + 1) % ring.size()];
      const Vec3 e = ray3d::Sub(p1, p0);
      const double l2 = ray3d::Dot(e, e);
      double u = l2 > 1e-24 ? ray3d::Dot(ray3d::Sub(hp, p0), e) / l2 : 0.0;
      u = std::clamp(u, 0.0, 1.0);
      nearest = std::min(nearest, ray3d::Length(ray3d::Sub(hp, ray3d::Add(p0, ray3d::Scale(e, u)))));
    }
    if (!inFace && nearest > rc + eps)
      continue;  // the sphere sits entirely off to the side of this face — it does not clip it
    if (inFace && nearest >= rc + eps) {
      ++cleanCount;  // the cut circle lies wholly inside this face
      cutFace = fi;
    } else {
      messy = true;  // the cut circle crosses a face edge — a mixed arc/line curve (B2)
    }
  }

  if (messy)
    return Fail(Problem::BooleanCurvedFace, outWhy);

  if (cleanCount == 0) {
    const bool inside = PointInPlanarSolid(S.centre, planar, scale);
    if (inside) {
      if (op == BoolOp::Intersect)
        out->push_back(sph);
      else
        out->push_back(planar);
      return Succeed(outWhy);
    }
    if (!AabbsOverlap(planar, sph, eps)) {
      if (op == BoolOp::Intersect)
        return Fail(Problem::BooleanEmptyResult, outWhy);
      out->push_back(planar);
      out->push_back(sph);
      return Succeed(outWhy);
    }
    return Fail(Problem::BooleanCurvedFace, outWhy);
  }
  if (cleanCount != 1 || cutFace < 0)
    return Fail(Problem::BooleanCurvedFace, outWhy);  // clipped by more than one plane — B2

  const Face& f = planar.faces[static_cast<std::size_t>(cutFace)];
  const Vec3 n = f.surface.frame.zAxis;
  const double d = ray3d::Dot(ray3d::Sub(S.centre, FaceRing(planar, f)[0]), n);
  if (op == BoolOp::Intersect) {
    // Keep the cap on P's material side: axis points inward (-n), cap sits above the plane at d.
    ucs::Ucs fr;
    if (!ucs::FromNormal(S.centre, ray3d::Scale(n, -1.0), &fr))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    Solid r;
    if (!BuildSphericalCap(fr, S.radius, d, &r, outWhy))
      return false;
    out->push_back(std::move(r));
    return Succeed(outWhy);
  }
  Solid r;
  if (dimple) {
    // box − sphere: the cap on P's material side (axis −n, plane at d) is removed; its sphere face
    // is inward and there is no disk.
    if (!BuildSphereBoss(planar, cutFace, S.centre, ray3d::Scale(n, -1.0), S.radius, d, /*inward=*/true,
                         &r, outWhy))
      return false;
  } else {
    // UNION: the cap outside P (axis +n, plane at −d) becomes a boss on the bored face.
    if (!BuildSphereBoss(planar, cutFace, S.centre, n, S.radius, -d, /*inward=*/false, &r, outWhy))
      return false;
  }
  out->push_back(std::move(r));
  return Succeed(outWhy);
}

/// `sphere ∩ cylinder` with the cylinder axis **through the sphere centre** (REQ-314 B2b-2 first
/// pair, GitHub issue #242). With the axis centred the two surfaces meet along **two plane circles**
/// at `z = ±h`, `h = √(Rs² − r²)` — not a quartic — so every edge is a `CurveKind::Arc` and every
/// face closed-form. The result is a cylindrical mid-band (radius \p r, `z ∈ [−h, h]`) capped by the
/// two spherical zones the cylinder encloses. 6 vertices, 10 edges, 6 faces. Built canonically about
/// `+z` and placed into \p fr (origin = sphere centre, `zAxis` = cylinder axis).
///
/// Volume `2 π r² h + 2 · π (Rs−h)² (2Rs+h) / 3`; area `4 π r h + 4 π Rs (Rs−h)`.
[[nodiscard]] bool BuildSphereCylinderIntersection(const ucs::Ucs& fr, double r, double Rs, Solid* out,
                                                   Problem* outWhy) {
  if (!(Rs > r) || !(r > 0.0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double h = std::sqrt(std::max(0.0, Rs * Rs - r * r));
  const double vc = std::asin(std::clamp(h / Rs, -1.0, 1.0));  // latitude of the cut circle
  const Vec3 zc{0.0, 0.0, 0.0};
  Solid s;
  const int t0 = AddVertex(&s, Vec3{r, 0.0, h});
  const int t1 = AddVertex(&s, Vec3{-r, 0.0, h});
  const int b0 = AddVertex(&s, Vec3{r, 0.0, -h});
  const int b1 = AddVertex(&s, Vec3{-r, 0.0, -h});
  const int np = AddVertex(&s, Vec3{0.0, 0.0, Rs});
  const int sp = AddVertex(&s, Vec3{0.0, 0.0, -Rs});
  // The two cut circles, each split into a +y and a −y half-arc at the x-seam (+x → −x is the +y half).
  const int tc0 = AddArc(&s, t0, t1, Vec3{0.0, 0.0, h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int tc1 = AddArc(&s, t1, t0, Vec3{0.0, 0.0, h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int bc0 = AddArc(&s, b0, b1, Vec3{0.0, 0.0, -h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int bc1 = AddArc(&s, b1, b0, Vec3{0.0, 0.0, -h}, Vec3{0.0, 0.0, 1.0}, kPi);
  // Band seams: +x at longitude 0, −x at longitude π.
  const int sxP = AddLine(&s, b0, t0);
  const int sxN = AddLine(&s, b1, t1);
  // Cap meridians to each pole (the BuildSphericalCap idiom).
  const int tm0 = AddArc(&s, t0, np, zc, Vec3{0.0, -1.0, 0.0}, kHalfPi - vc);
  const int tm1 = AddArc(&s, t1, np, zc, Vec3{0.0, 1.0, 0.0}, kHalfPi - vc);
  const int bm0 = AddArc(&s, b0, sp, zc, Vec3{0.0, 1.0, 0.0}, kHalfPi - vc);
  const int bm1 = AddArc(&s, b1, sp, zc, Vec3{0.0, -1.0, 0.0}, kHalfPi - vc);

  auto cylBand = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame.origin = Vec3{0.0, 0.0, -h};
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = 2.0 * h;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  auto sphereCap = [&](double u0, double u1, double v0, double v1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Sphere;
    f.surface.radius = Rs;
    f.uStart = u0;
    f.uEnd = u1;
    f.vStart = v0;
    f.vEnd = v1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  // Band (coaxial-stack outward-wall winding).
  cylBand(0.0, kPi, {{bc0, false}, {sxN, false}, {tc0, true}, {sxP, true}});
  cylBand(kPi, kTwoPi, {{bc1, false}, {sxP, false}, {tc1, true}, {sxN, true}});
  // Top cap (BuildSphericalCap winding).
  sphereCap(0.0, kPi, vc, kHalfPi, {{tm1, false}, {tm0, true}, {tc0, false}});
  sphereCap(kPi, kTwoPi, vc, kHalfPi, {{tm0, false}, {tm1, true}, {tc1, false}});
  // Bottom cap (the top-cap loop reflected through z = 0 — order and every direction reversed).
  sphereCap(0.0, kPi, -kHalfPi, -vc, {{bc0, true}, {bm0, false}, {bm1, true}});
  sphereCap(kPi, kTwoPi, -kHalfPi, -vc, {{bc1, true}, {bm1, false}, {bm0, true}});

  AddSingleShell(&s);
  PlaceInFrame(&s, fr);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// Shared frames and the four procedural loop half-edges of an offset sphere ∩ cylinder figure
/// (issue #242, slice B). Returns a solid pre-loaded with the four loop vertices `u0, uP, l0, lP`
/// (indices 0..3) and four `CurveKind::Intersection` edges `eUp, eUn, eLp, eLn` (indices 0..3);
/// `z0` / `zP` are the loop half-heights at φ = 0 and φ = π.
struct OffsetScaffold {
  Solid s;
  Surface cSurf;
  Surface sSurf;
  double z0 = 0.0;
  double zP = 0.0;
  ucs::Ucs fr;
  double r = 0.0;
  double Rs = 0.0;
  double d = 0.0;
  [[nodiscard]] Vec3 W(const Vec3& l) const { return ucs::UcsToWorld(fr, l); }
};

/// \p d is the axis-to-centre offset. `d > r` (the axis misses the sphere pole) and `d < r` (the
/// pole-covered sub-case, issue #242) both produce the same four-edge scaffold — a closed quartic
/// loop at every longitude — and differ only in which sphere patches the callers keep. `d ≈ r`
/// (axis tangent to the pole) is degenerate and refused.
[[nodiscard]] bool MakeOffsetScaffold(const ucs::Ucs& fr, double r, double Rs, double d,
                                      OffsetScaffold* sc) {
  if (!(r > 0.0) || !(d > 0.0) || !(d + r < Rs) || std::fabs(d - r) <= 1e-9 * (Rs + r + d))
    return false;
  sc->fr = fr;
  sc->r = r;
  sc->Rs = Rs;
  sc->d = d;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  auto gg = [&](double phi) { return Rs * Rs - d * d - r * r - 2.0 * d * r * std::cos(phi); };
  auto cyl = [&](double phi, double z) {
    return W(Vec3{d + r * std::cos(phi), r * std::sin(phi), z});
  };
  sc->z0 = std::sqrt(std::max(0.0, gg(0.0)));
  sc->zP = std::sqrt(std::max(0.0, gg(kPi)));

  sc->cSurf.kind = SurfaceKind::Cylinder;
  sc->cSurf.frame.origin = W(Vec3{d, 0.0, 0.0});
  sc->cSurf.frame.zAxis = fr.zAxis;
  sc->cSurf.frame.xAxis = fr.xAxis;
  sc->cSurf.frame.yAxis = fr.yAxis;
  sc->cSurf.radius = r;
  sc->cSurf.height = 4.0 * Rs;
  sc->sSurf.kind = SurfaceKind::Sphere;
  sc->sSurf.frame = fr;
  sc->sSurf.radius = Rs;

  AddVertex(&sc->s, cyl(0.0, sc->z0));   // 0: u0
  AddVertex(&sc->s, cyl(kPi, sc->zP));   // 1: uP
  AddVertex(&sc->s, cyl(0.0, -sc->z0));  // 2: l0
  AddVertex(&sc->s, cyl(kPi, -sc->zP));  // 3: lP

  auto isect = [&](int a, int b, double witnessPhi, double witnessZsign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = a;
    e.v1 = b;
    e.frame.origin = cyl(witnessPhi, witnessZsign * std::sqrt(std::max(0.0, gg(witnessPhi))));
    e.isectSurfaces = {sc->cSurf, sc->sSurf};
    sc->s.edges.push_back(e);
  };
  isect(0, 1, 0.5 * kPi, 1.0);   // 0: eUp — upper loop, y > 0
  isect(1, 0, 1.5 * kPi, 1.0);   // 1: eUn — upper loop, y < 0
  isect(2, 3, 0.5 * kPi, -1.0);  // 2: eLp — lower loop, y > 0
  isect(3, 2, 1.5 * kPi, -1.0);  // 3: eLn — lower loop, y < 0
  return true;
}

/// Adds the **kept sphere** of an offset SUBTRACT / UNION figure to \p sc: the two poles, the three
/// kept sub-arcs of the `u = 0` meridian (pole → lower loop, between the loops, upper loop → pole),
/// the `u = π` seam, and the two lens-bitten hemisphere faces (`u` 0→π and π→2π, full pole-to-pole
/// `v` span). Both operations keep exactly this surface — only the cylinder side differs — so the
/// hemisphere winding lives here once. Call **before** adding the operation's own edges/faces; the
/// four `CurveKind::Intersection` loop half-edges (indices 0..3) must already be present.
void AddOffsetKeptHemispheres(OffsetScaffold* sc) {
  Solid& s = sc->s;
  const int eUp = 0;
  const int eUn = 1;
  const int eLp = 2;
  const int eLn = 3;
  const int S = AddVertex(&s, sc->W(Vec3{0.0, 0.0, -sc->Rs}));
  const int N = AddVertex(&s, sc->W(Vec3{0.0, 0.0, sc->Rs}));
  const double v0 = std::asin(std::clamp(sc->z0 / sc->Rs, -1.0, 1.0));
  const double vPi = std::asin(std::clamp(sc->zP / sc->Rs, -1.0, 1.0));
  const Vec3 ctr = sc->fr.origin;
  const Vec3 negY = ray3d::Scale(sc->fr.yAxis, -1.0);
  const int a1 = AddArc(&s, S, 3, ctr, negY, kHalfPi - vPi);   // S → lP
  const int a3 = AddArc(&s, 2, 0, ctr, negY, 2.0 * v0);        // l0 → u0
  const int a5 = AddArc(&s, 1, N, ctr, negY, kHalfPi - vPi);   // uP → N
  const int m1 = AddArc(&s, S, N, ctr, sc->fr.yAxis, kPi);     // the u = π seam
  auto hemi = [&](double a, double b, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = sc->sSurf;
    f.uStart = a;
    f.uEnd = b;
    f.vStart = -kHalfPi;
    f.vEnd = kHalfPi;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  hemi(0.0, kPi, {{m1, false}, {a5, true}, {eUp, true}, {a3, true}, {eLp, false}, {a1, true}});
  hemi(kPi, kTwoPi, {{m1, true}, {a1, false}, {eLn, false}, {a3, false}, {eUn, true}, {a5, false}});
}

/// The `d < r` counterpart of \ref AddOffsetKeptHemispheres (the pole-covered sub-case, issue #242):
/// the cylinder swallows each pole, so the kept sphere is only the **equatorial zone** between the
/// two quartic loops — no pole vertices, one kept sub-arc of each of the `u = 0` and `u = π`
/// meridians, and two zone faces (`u` 0→π and π→2π). The faces keep the full pole-to-pole `v`
/// metadata so \ref IntegrateFace / the tessellator take the "hemisphere minus every lens bite"
/// path — which here removes a polar cap at each pole, leaving exactly the zone. Call **before** the
/// operation's own edges/faces, with the four loop half-edges (indices 0..3) present.
void AddOffsetKeptZone(OffsetScaffold* sc) {
  Solid& s = sc->s;
  const int eUp = 0;
  const int eUn = 1;
  const int eLp = 2;
  const int eLn = 3;
  const int u0 = 0;
  const int uP = 1;
  const int l0 = 2;
  const int lP = 3;
  const double v0 = std::asin(std::clamp(sc->z0 / sc->Rs, -1.0, 1.0));
  const double vPi = std::asin(std::clamp(sc->zP / sc->Rs, -1.0, 1.0));
  const Vec3 ctr = sc->fr.origin;
  const Vec3 negY = ray3d::Scale(sc->fr.yAxis, -1.0);
  const int z0arc = AddArc(&s, l0, u0, ctr, negY, 2.0 * v0);           // u = 0 meridian, l0 → u0
  const int zPiArc = AddArc(&s, lP, uP, ctr, sc->fr.yAxis, 2.0 * vPi);  // u = π meridian, lP → uP
  auto zone = [&](double a, double b, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = sc->sSurf;
    f.uStart = a;
    f.uEnd = b;
    f.vStart = -kHalfPi;
    f.vEnd = kHalfPi;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  zone(0.0, kPi, {{z0arc, true}, {eLp, false}, {zPiArc, false}, {eUp, true}});
  zone(kPi, kTwoPi, {{zPiArc, true}, {eLn, false}, {z0arc, false}, {eUn, true}});
}

/// `sphere ∩ cylinder` with the cylinder axis **parallel to a sphere diameter but offset** by \p d
/// (REQ-314 B2b-2, GitHub issue #242 — the genuine quartic). `d + r < Rs` (the cylinder clears the
/// equator) and both caps clear the sphere: the cylinder pierces the sphere in two closed quartic
/// loops, and the INTERSECT is a plug — the cylinder wall band between the loops, capped by the two
/// sphere patches the cylinder encloses. When `d > r` those caps are lens patches between the poles;
/// when `d < r` (the pole-covered sub-case) the cylinder swallows each pole and the caps are full
/// polar caps. Local frame: sphere centre at the origin, `+z` the cylinder axis direction, the
/// cylinder axis through `(d, 0, 0)`. 4 vertices, 6 edges (4 procedural), 4 faces. Every face
/// integrates numerically. Placed into \p fr.
///
/// The curve: at cylinder longitude `φ`, `z² = Rs² − d² − r² − 2 d r cos φ` (always positive here),
/// one loop per sign of `z`.
[[nodiscard]] bool BuildSphereCylinderOffsetIntersection(const ucs::Ucs& fr, double r, double Rs,
                                                         double d, Solid* out, Problem* outWhy) {
  OffsetScaffold sc;
  if (!MakeOffsetScaffold(fr, r, Rs, d, &sc))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  Solid& s = sc.s;
  const Surface& cSurf = sc.cSurf;
  const Surface& sSurf = sc.sSurf;
  const double z0 = sc.z0;
  const double zP = sc.zP;
  const int u0 = 0;
  const int uP = 1;
  const int l0 = 2;
  const int lP = 3;
  const int eUp = 0;
  const int eUn = 1;
  const int eLp = 2;
  const int eLn = 3;
  const int s0 = AddLine(&s, l0, u0);  // seam at φ = 0
  const int sP = AddLine(&s, lP, uP);  // seam at φ = π

  auto cylFace = [&](double a, double b, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = cSurf;
    f.uStart = a;
    f.uEnd = b;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  auto capFace = [&](double uLo, double uHi, double vLo, double vHi, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = sSurf;
    f.uStart = uLo;
    f.uEnd = uHi;
    f.vStart = vLo;
    f.vEnd = vHi;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  // Cylinder wall: y>0 half (φ 0→π) and y<0 half. Coaxial-outward winding: lower curve forward, up
  // the seam, upper curve reversed, down the seam.
  cylFace(0.0, kPi, {{eLp, false}, {sP, false}, {eUp, true}, {s0, true}});
  cylFace(kPi, kTwoPi, {{eLn, false}, {s0, false}, {eUn, true}, {sP, true}});

  // The two sphere caps the cylinder encloses. Latitude runs between the loop's z-extremes; the
  // numeric integrator / tessellator find the exact band per longitude.
  //   d > r  — the axis misses the pole: each cap is a lens spanning longitude ±asin(r/d) about
  //            fr.xAxis, latitude strictly between the poles.
  //   d < r  — the pole-covered sub-case (issue #242): the cylinder swallows the pole, so each cap
  //            is a full polar cap — every longitude, latitude from the loop up to ±π/2.
  const double vLoU = std::asin(std::clamp(z0 / Rs, -1.0, 1.0));
  const double vHiU = std::asin(std::clamp(zP / Rs, -1.0, 1.0));
  if (d < r) {
    capFace(0.0, kTwoPi, vLoU, kHalfPi, {{eUp, false}, {eUn, false}});
    capFace(0.0, kTwoPi, -kHalfPi, -vLoU, {{eLp, true}, {eLn, true}});
  } else {
    const double uHalf = std::asin(std::clamp(r / d, -1.0, 1.0));
    capFace(-uHalf, uHalf, vLoU, vHiU, {{eUp, false}, {eUn, false}});
    capFace(-uHalf, uHalf, -vHiU, -vLoU, {{eLp, true}, {eLn, true}});
  }

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `sphere − cylinder` with the cylinder axis **parallel to a sphere diameter, offset by** \p d
/// (REQ-314 B2b-2, GitHub issue #242 — the quartic). `d + r < Rs` (clears the equator), both caps
/// clear the sphere: a ball with an off-centre hole drilled clean through it, a genus-1 solid. The
/// bore is an **inward** cylinder wall between the two quartic loops. When `d > r` the kept sphere
/// is two lens-bitten hemispheres (6v / 10e); when `d < r` (pole-covered) it is the two-half
/// equatorial zone between the loops, no poles (4v / 8e). Every sphere and bore face integrates
/// numerically. `.gs` stays v3.
[[nodiscard]] bool BuildSphereCylinderOffsetSubtractSphere(const ucs::Ucs& fr, double r, double Rs,
                                                           double d, Solid* out, Problem* outWhy) {
  OffsetScaffold sc;
  if (!MakeOffsetScaffold(fr, r, Rs, d, &sc))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  Solid& s = sc.s;
  const int u0 = 0;
  const int uP = 1;
  const int l0 = 2;
  const int lP = 3;
  const int eUp = 0;
  const int eUn = 1;
  const int eLp = 2;
  const int eLn = 3;

  if (d < r)
    AddOffsetKeptZone(&sc);
  else
    AddOffsetKeptHemispheres(&sc);
  const int s0 = AddLine(&s, l0, u0);  // bore seam at φ = 0
  const int sP = AddLine(&s, lP, uP);  // bore seam at φ = π

  auto bore = [&](double a, double b, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = sc.cSurf;
    f.surface.inward = true;
    f.uStart = a;
    f.uEnd = b;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  bore(0.0, kPi, {{s0, false}, {eUp, false}, {sP, true}, {eLp, true}});
  bore(kPi, kTwoPi, {{sP, false}, {eUn, false}, {s0, true}, {eLn, true}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `sphere ∪ cylinder`, offset axis (REQ-314 B2b-2, GitHub issue #242 — the quartic), same sub-cases
/// as \ref BuildSphereCylinderOffsetSubtractSphere. The ball with a solid cylindrical boss out each
/// side. Kept sphere: the two lens-bitten hemispheres (`d > r`) or the equatorial zone (`d < r`);
/// each boss is the **outward** cylinder wall from a quartic loop out to a flat end cap. \p zBot /
/// \p zTop are the cap heights measured from the sphere centre along \p fr's `zAxis` (`zBot < −zP`,
/// `zTop > zP`). `.gs` stays v3.
[[nodiscard]] bool BuildSphereCylinderOffsetUnion(const ucs::Ucs& fr, double r, double Rs, double d,
                                                  double zBot, double zTop, Solid* out,
                                                  Problem* outWhy) {
  OffsetScaffold sc;
  if (!MakeOffsetScaffold(fr, r, Rs, d, &sc))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  Solid& s = sc.s;
  if (!(zBot < -sc.zP) || !(zTop > sc.zP))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const int u0 = 0;
  const int uP = 1;
  const int l0 = 2;
  const int lP = 3;
  const int eUp = 0;
  const int eUn = 1;
  const int eLp = 2;
  const int eLn = 3;

  if (d < r)
    AddOffsetKeptZone(&sc);
  else
    AddOffsetKeptHemispheres(&sc);

  // Cap rims at φ = 0 and φ = π on each flat end.
  auto rimPt = [&](double phi, double z) {
    return sc.W(Vec3{sc.d + r * std::cos(phi), r * std::sin(phi), z});
  };
  const int rt0 = AddVertex(&s, rimPt(0.0, zTop));
  const int rtP = AddVertex(&s, rimPt(kPi, zTop));
  const int rb0 = AddVertex(&s, rimPt(0.0, zBot));
  const int rbP = AddVertex(&s, rimPt(kPi, zBot));
  const Vec3 zc = sc.cSurf.frame.origin;
  const Vec3 caxis = fr.zAxis;
  const int rtc0 = AddArc(&s, rt0, rtP, ray3d::Add(zc, ray3d::Scale(caxis, zTop)), caxis, kPi);
  const int rtcP = AddArc(&s, rtP, rt0, ray3d::Add(zc, ray3d::Scale(caxis, zTop)), caxis, kPi);
  const int rbc0 = AddArc(&s, rb0, rbP, ray3d::Add(zc, ray3d::Scale(caxis, zBot)), caxis, kPi);
  const int rbcP = AddArc(&s, rbP, rb0, ray3d::Add(zc, ray3d::Scale(caxis, zBot)), caxis, kPi);
  const int su0 = AddLine(&s, u0, rt0);  // upper boss seams
  const int suP = AddLine(&s, uP, rtP);
  const int sl0 = AddLine(&s, rb0, l0);  // lower boss seams
  const int slP = AddLine(&s, rbP, lP);

  auto boss = [&](double a, double b, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = sc.cSurf;
    f.uStart = a;
    f.uEnd = b;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  // Upper boss: quartic loop → rim, φ 0→π and π→2π. Outward wall winding (loop forward at the
  // sphere end, rim reversed).
  boss(0.0, kPi, {{eUp, false}, {suP, false}, {rtc0, true}, {su0, true}});
  boss(kPi, kTwoPi, {{eUn, false}, {su0, false}, {rtcP, true}, {suP, true}});
  s.faces.push_back(MakePlaneFace(ray3d::Add(zc, ray3d::Scale(caxis, zTop)), caxis,
                                  {{rtc0, false}, {rtcP, false}}));
  // Lower boss: rim → quartic loop.
  boss(0.0, kPi, {{rbc0, false}, {slP, false}, {eLp, true}, {sl0, true}});
  boss(kPi, kTwoPi, {{rbcP, false}, {sl0, false}, {eLn, true}, {slP, true}});
  s.faces.push_back(MakePlaneFace(ray3d::Add(zc, ray3d::Scale(caxis, zBot)),
                                  ray3d::Scale(caxis, -1.0), {{rbc0, true}, {rbcP, true}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// One stub of `cylinder − sphere` with the cylinder axis **parallel to a sphere diameter, offset by**
/// \p d (REQ-314 B2b-2, GitHub issue #242 — the quartic). Sub-case `r < d` (axis misses the pole),
/// `d + r < Rs` (clears the equator), both caps clear the sphere: the sphere bites the cylinder clean
/// in two, leaving two disjoint stubs. This builds the stub on the \p sideSign side (`+1` above the
/// sphere, `−1` below): a short cylinder with a flat cap at `z = zFlat` and, on its inner end, an
/// **inward** spherical dimple — the sphere patch the cylinder encloses on that side, bounded by one
/// quartic loop. When `d > r` the dimple is a lens (longitude ±asin(r/d)); when `d < r`
/// (pole-covered) it is a full inward polar cap. 4 vertices, 6 edges (2 procedural), 4 faces, χ = 2.
/// Built in \p fr (origin = sphere centre, `+z` = cylinder axis, `+x` toward the cylinder axis) and
/// left there. Every sphere and cylinder face integrates numerically. `.gs` stays v3.
[[nodiscard]] bool BuildCylinderSphereOffsetStub(const ucs::Ucs& fr, double r, double Rs, double d,
                                                 double zFlat, int sideSign, Solid* out,
                                                 Problem* outWhy) {
  if (!(r > 0.0) || !(d > 0.0) || !(d + r < Rs) || std::fabs(d - r) <= 1e-9 * (Rs + r + d))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const bool poleCovered = d < r;
  const double sgn = sideSign > 0 ? 1.0 : -1.0;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  auto gg = [&](double phi) { return Rs * Rs - d * d - r * r - 2.0 * d * r * std::cos(phi); };
  auto cyl = [&](double phi, double z) {
    return W(Vec3{d + r * std::cos(phi), r * std::sin(phi), z});
  };
  const double z0 = std::sqrt(std::max(0.0, gg(0.0)));  // loop half-height at φ = 0 (near side)
  const double zP = std::sqrt(std::max(0.0, gg(kPi)));  // loop half-height at φ = π (far side)
  if (!(sgn * zFlat > zP))
    return Fail(Problem::BooleanResultInvalid, outWhy);

  Surface cSurf;
  cSurf.kind = SurfaceKind::Cylinder;
  cSurf.frame.origin = W(Vec3{d, 0.0, 0.0});
  cSurf.frame.zAxis = fr.zAxis;
  cSurf.frame.xAxis = fr.xAxis;
  cSurf.frame.yAxis = fr.yAxis;
  cSurf.radius = r;
  cSurf.height = 4.0 * Rs;
  Surface sSurf;
  sSurf.kind = SurfaceKind::Sphere;
  sSurf.frame = fr;
  sSurf.radius = Rs;

  Solid s;
  const int p0 = AddVertex(&s, cyl(0.0, sgn * z0));  // 0: loop vertex at φ = 0
  const int pP = AddVertex(&s, cyl(kPi, sgn * zP));  // 1: loop vertex at φ = π
  const int q0 = AddVertex(&s, cyl(0.0, zFlat));     // 2: rim vertex at φ = 0
  const int qP = AddVertex(&s, cyl(kPi, zFlat));     // 3: rim vertex at φ = π

  auto isect = [&](int a, int b, double witnessPhi) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = a;
    e.v1 = b;
    e.frame.origin = cyl(witnessPhi, sgn * std::sqrt(std::max(0.0, gg(witnessPhi))));
    e.isectSurfaces = {cSurf, sSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int eP = isect(p0, pP, 0.5 * kPi);  // 4: quartic loop, y > 0 (p0 → pP)
  const int eN = isect(pP, p0, 1.5 * kPi);  // 5: quartic loop, y < 0 (pP → p0)
  const int sp0 = AddLine(&s, p0, q0);      // 6: wall seam at φ = 0 (loop → rim)
  const int spP = AddLine(&s, pP, qP);      // 7: wall seam at φ = π
  const Vec3 fc = W(Vec3{d, 0.0, zFlat});
  const Vec3 caxis = fr.zAxis;
  const int rc0 = AddArc(&s, q0, qP, fc, caxis, kPi);  // 8: rim, y > 0 (q0 → qP)
  const int rcP = AddArc(&s, qP, q0, fc, caxis, kPi);  // 9: rim, y < 0

  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = cSurf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  auto dimple = [&](double vLo, double vHi, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = sSurf;
    f.surface.inward = true;
    if (poleCovered) {
      f.uStart = 0.0;
      f.uEnd = kTwoPi;
    } else {
      const double uHalf = std::asin(std::clamp(r / d, -1.0, 1.0));
      f.uStart = -uHalf;
      f.uEnd = uHalf;
    }
    f.vStart = vLo;
    f.vEnd = vHi;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  const double vLoU = std::asin(std::clamp(z0 / Rs, -1.0, 1.0));
  const double vHiU = std::asin(std::clamp(zP / Rs, -1.0, 1.0));

  if (sideSign > 0) {
    // Loop sits below the rim: bottom edge is the quartic loop, top edge the rim.
    wall(0.0, kPi, {{eP, false}, {spP, false}, {rc0, true}, {sp0, true}});
    wall(kPi, kTwoPi, {{eN, false}, {sp0, false}, {rcP, true}, {spP, true}});
    s.faces.push_back(MakePlaneFace(fc, caxis, {{rc0, false}, {rcP, false}}));
    dimple(vLoU, poleCovered ? kHalfPi : vHiU, {{eN, true}, {eP, true}});
  } else {
    // Rim sits below the loop: bottom edge is the rim, top edge the quartic loop.
    wall(0.0, kPi, {{rc0, false}, {spP, true}, {eP, true}, {sp0, false}});
    wall(kPi, kTwoPi, {{rcP, false}, {sp0, true}, {eN, true}, {spP, false}});
    s.faces.push_back(MakePlaneFace(fc, ray3d::Scale(caxis, -1.0), {{rc0, true}, {rcP, true}}));
    dimple(poleCovered ? -kHalfPi : -vHiU, -vLoU, {{eP, false}, {eN, false}});
  }

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `cylinder − sphere` with the cylinder axis **parallel to a sphere diameter, offset by** \p d and
/// both cylinder caps clear of the sphere (REQ-314 B2b-2, GitHub issue #242 — the quartic): the
/// sphere bites the cylinder clean in two, leaving two disjoint stubs, each with a concave lens-shaped
/// spherical dimple on its inner end. \p zBot / \p zTop are the cylinder cap heights measured from the
/// sphere centre along \p fr's `zAxis` (`zBot < −zP`, `zTop > zP`). Pushes **two** solids.
[[nodiscard]] bool BuildCylinderSphereOffsetSubtract(const ucs::Ucs& fr, double r, double Rs,
                                                     double d, double zBot, double zTop,
                                                     std::vector<Solid>* out, Problem* outWhy) {
  Solid top;
  if (!BuildCylinderSphereOffsetStub(fr, r, Rs, d, zTop, 1, &top, outWhy))
    return false;
  Solid bot;
  if (!BuildCylinderSphereOffsetStub(fr, r, Rs, d, zBot, -1, &bot, outWhy))
    return false;
  out->push_back(std::move(top));
  out->push_back(std::move(bot));
  return Succeed(outWhy);
}

/// `sphere − cylinder` with the cylinder axis **through the sphere centre** (REQ-314 B2b-2, GitHub
/// issue #242). A ball with a clean cylindrical hole drilled straight through it — a genus-1 solid.
/// The kept spherical surface is the equatorial zone `|z| <= h` (`h = √(Rs²−r²)`); the bore is an
/// **inward** cylinder wall of radius \p r spanning `z ∈ [−h, h]`. The zone's two seams are sphere
/// meridians (they bulge to `±Rs`), the bore's two seams the straight segments at `±x`. 4 vertices,
/// 8 edges, 4 faces. Built canonically about `+z` and placed into \p fr (origin = sphere centre,
/// `zAxis` = cylinder axis).
///
/// Volume `(4/3) π Rs³ − (2 π r² h + 2 · π (Rs−h)² (2Rs+h) / 3)`; area `4 π h (Rs + r)`.
[[nodiscard]] bool BuildSphereCylinderSubtractSphere(const ucs::Ucs& fr, double r, double Rs,
                                                     Solid* out, Problem* outWhy) {
  if (!(Rs > r) || !(r > 0.0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double h = std::sqrt(std::max(0.0, Rs * Rs - r * r));
  const double vc = std::asin(std::clamp(h / Rs, -1.0, 1.0));  // latitude of each cut circle
  Solid s;
  const int t0 = AddVertex(&s, Vec3{r, 0.0, h});
  const int t1 = AddVertex(&s, Vec3{-r, 0.0, h});
  const int b0 = AddVertex(&s, Vec3{r, 0.0, -h});
  const int b1 = AddVertex(&s, Vec3{-r, 0.0, -h});
  const int tc0 = AddArc(&s, t0, t1, Vec3{0.0, 0.0, h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int tc1 = AddArc(&s, t1, t0, Vec3{0.0, 0.0, h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int bc0 = AddArc(&s, b0, b1, Vec3{0.0, 0.0, -h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int bc1 = AddArc(&s, b1, b0, Vec3{0.0, 0.0, -h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int mzP = AddArc(&s, b0, t0, Vec3{0.0, 0.0, 0.0}, Vec3{0.0, -1.0, 0.0}, 2.0 * vc);  // +x meridian
  const int mzN = AddArc(&s, b1, t1, Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}, 2.0 * vc);   // -x meridian
  const int szP = AddLine(&s, b0, t0);  // +x bore seam
  const int szN = AddLine(&s, b1, t1);  // -x bore seam

  auto zone = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Sphere;
    f.surface.radius = Rs;
    f.uStart = u0;
    f.uEnd = u1;
    f.vStart = -vc;
    f.vEnd = vc;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame.origin = Vec3{0.0, 0.0, -h};
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = 2.0 * h;
    f.surface.inward = true;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  zone(0.0, kPi, {{bc0, false}, {mzN, false}, {tc0, true}, {mzP, true}});
  zone(kPi, kTwoPi, {{bc1, false}, {mzP, false}, {tc1, true}, {mzN, true}});
  wall(0.0, kPi, {{szP, false}, {tc0, false}, {szN, true}, {bc0, true}});
  wall(kPi, kTwoPi, {{szN, false}, {tc1, false}, {szP, true}, {bc1, true}});

  AddSingleShell(&s);
  PlaceInFrame(&s, fr);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// One stub of `cylinder − sphere` (centred axis): a short cylinder of radius \p r, one flat cap at
/// `z = zFlat`, the other end an **inward** spherical dimple (the sphere's polar cap, `pole = ±Rs`).
/// \p poleSign is `+1` for the stub above the sphere, `−1` for the one below. `|zFlat| > h`. 5
/// vertices, 8 edges, 5 faces. Built canonically about `+z`, placed into \p fr.
[[nodiscard]] bool BuildCylinderSphereStub(const ucs::Ucs& fr, double r, double Rs, double zFlat,
                                           int poleSign, Solid* out, Problem* outWhy) {
  if (!(Rs > r) || !(r > 0.0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double h = std::sqrt(std::max(0.0, Rs * Rs - r * r));
  const double vc = std::asin(std::clamp(h / Rs, -1.0, 1.0));
  const double zCut = poleSign > 0 ? h : -h;
  const double pole = poleSign > 0 ? Rs : -Rs;
  if (!(std::fabs(zFlat) > h))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  Solid s;
  const int d0 = AddVertex(&s, Vec3{r, 0.0, zFlat});
  const int d1 = AddVertex(&s, Vec3{-r, 0.0, zFlat});
  const int c0 = AddVertex(&s, Vec3{r, 0.0, zCut});
  const int c1 = AddVertex(&s, Vec3{-r, 0.0, zCut});
  const int pl = AddVertex(&s, Vec3{0.0, 0.0, pole});
  const int dc0 = AddArc(&s, d0, d1, Vec3{0.0, 0.0, zFlat}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int dc1 = AddArc(&s, d1, d0, Vec3{0.0, 0.0, zFlat}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int cc0 = AddArc(&s, c0, c1, Vec3{0.0, 0.0, zCut}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int cc1 = AddArc(&s, c1, c0, Vec3{0.0, 0.0, zCut}, Vec3{0.0, 0.0, 1.0}, kPi);
  // Wall seams run cut -> flat; meridians run cut -> pole. The +x meridian turns about -y when the
  // pole is +z and about +y when it is -z (so the sweep stays positive either way).
  const int w0 = AddLine(&s, c0, d0);
  const int w1 = AddLine(&s, c1, d1);
  const Vec3 myAxis = poleSign > 0 ? Vec3{0.0, -1.0, 0.0} : Vec3{0.0, 1.0, 0.0};
  const int m0 = AddArc(&s, c0, pl, Vec3{0.0, 0.0, 0.0}, myAxis, kHalfPi - vc);
  const int m1 = AddArc(&s, c1, pl, Vec3{0.0, 0.0, 0.0}, ray3d::Scale(myAxis, -1.0), kHalfPi - vc);

  const double zLo = std::min(zFlat, zCut);
  const double zHi = std::max(zFlat, zCut);
  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame.origin = Vec3{0.0, 0.0, zLo};
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = zHi - zLo;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  auto dimple = [&](double u0, double u1, double v0, double v1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Sphere;
    f.surface.radius = Rs;
    f.surface.inward = true;
    f.uStart = u0;
    f.uEnd = u1;
    f.vStart = v0;
    f.vEnd = v1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };

  if (poleSign > 0) {
    s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, zFlat}, Vec3{0.0, 0.0, 1.0},
                                    {{dc0, false}, {dc1, false}}));
    wall(0.0, kPi, {{cc0, false}, {w1, false}, {dc0, true}, {w0, true}});
    wall(kPi, kTwoPi, {{cc1, false}, {w0, false}, {dc1, true}, {w1, true}});
    dimple(0.0, kPi, vc, kHalfPi, {{m0, false}, {m1, true}, {cc0, true}});
    dimple(kPi, kTwoPi, vc, kHalfPi, {{m1, false}, {m0, true}, {cc1, true}});
  } else {
    s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, zFlat}, Vec3{0.0, 0.0, -1.0},
                                    {{dc1, true}, {dc0, true}}));
    wall(0.0, kPi, {{dc0, false}, {w1, true}, {cc0, true}, {w0, false}});
    wall(kPi, kTwoPi, {{dc1, false}, {w0, true}, {cc1, true}, {w1, false}});
    dimple(0.0, kPi, -kHalfPi, -vc, {{m0, true}, {cc0, false}, {m1, false}});
    dimple(kPi, kTwoPi, -kHalfPi, -vc, {{m1, true}, {cc1, false}, {m0, false}});
  }

  AddSingleShell(&s);
  PlaceInFrame(&s, fr);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `cylinder − sphere` with the cylinder axis **through the sphere centre** and both cylinder caps
/// clear of the sphere: the sphere bites the cylinder clean in two, leaving two disjoint stubs, each
/// with a concave spherical dimple on its inner end (REQ-314 B2b-2, GitHub issue #242). \p zBot /
/// \p zTop are the cylinder cap heights measured from the sphere centre along \p fr's `zAxis`
/// (`zBot < −h < h < zTop`). Pushes **two** solids.
[[nodiscard]] bool BuildCylinderSphereSubtract(const ucs::Ucs& fr, double r, double Rs, double zBot,
                                               double zTop, std::vector<Solid>* out, Problem* outWhy) {
  Solid top;
  if (!BuildCylinderSphereStub(fr, r, Rs, zTop, 1, &top, outWhy))
    return false;
  Solid bot;
  if (!BuildCylinderSphereStub(fr, r, Rs, zBot, -1, &bot, outWhy))
    return false;
  out->push_back(std::move(top));
  out->push_back(std::move(bot));
  return Succeed(outWhy);
}

/// `sphere ∪ cylinder` with the cylinder axis **through the sphere centre**, both caps clear of the
/// sphere (REQ-314 B2b-2, GitHub issue #242): the ball with a solid cylindrical boss out each side.
/// The kept spherical surface is the equatorial zone `|z| <= h`; each boss is the cylinder wall from
/// the cut circle out to its flat cap. 8 vertices, 14 edges, 8 faces. Built canonically about `+z`,
/// placed into \p fr. \p zBot / \p zTop are the cap heights from the sphere centre
/// (`zBot < −h < h < zTop`).
///
/// Volume `(4/3) π Rs³ + π r² (zTop − zBot) − (2 π r² h + 2 · π (Rs−h)² (2Rs+h) / 3)`.
[[nodiscard]] bool BuildSphereCylinderUnion(const ucs::Ucs& fr, double r, double Rs, double zBot,
                                            double zTop, Solid* out, Problem* outWhy) {
  if (!(Rs > r) || !(r > 0.0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double h = std::sqrt(std::max(0.0, Rs * Rs - r * r));
  if (!(zBot < -h) || !(zTop > h))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double vc = std::asin(std::clamp(h / Rs, -1.0, 1.0));
  Solid s;
  const int t0 = AddVertex(&s, Vec3{r, 0.0, h});
  const int t1 = AddVertex(&s, Vec3{-r, 0.0, h});
  const int b0 = AddVertex(&s, Vec3{r, 0.0, -h});
  const int b1 = AddVertex(&s, Vec3{-r, 0.0, -h});
  const int rt0 = AddVertex(&s, Vec3{r, 0.0, zTop});
  const int rt1 = AddVertex(&s, Vec3{-r, 0.0, zTop});
  const int rb0 = AddVertex(&s, Vec3{r, 0.0, zBot});
  const int rb1 = AddVertex(&s, Vec3{-r, 0.0, zBot});
  const int tc0 = AddArc(&s, t0, t1, Vec3{0.0, 0.0, h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int tc1 = AddArc(&s, t1, t0, Vec3{0.0, 0.0, h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int bc0 = AddArc(&s, b0, b1, Vec3{0.0, 0.0, -h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int bc1 = AddArc(&s, b1, b0, Vec3{0.0, 0.0, -h}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int mzP = AddArc(&s, b0, t0, Vec3{0.0, 0.0, 0.0}, Vec3{0.0, -1.0, 0.0}, 2.0 * vc);
  const int mzN = AddArc(&s, b1, t1, Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}, 2.0 * vc);
  const int rtc0 = AddArc(&s, rt0, rt1, Vec3{0.0, 0.0, zTop}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int rtc1 = AddArc(&s, rt1, rt0, Vec3{0.0, 0.0, zTop}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int rbc0 = AddArc(&s, rb0, rb1, Vec3{0.0, 0.0, zBot}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int rbc1 = AddArc(&s, rb1, rb0, Vec3{0.0, 0.0, zBot}, Vec3{0.0, 0.0, 1.0}, kPi);
  const int su0 = AddLine(&s, t0, rt0);
  const int su1 = AddLine(&s, t1, rt1);
  const int sl0 = AddLine(&s, rb0, b0);
  const int sl1 = AddLine(&s, rb1, b1);

  auto zone = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Sphere;
    f.surface.radius = Rs;
    f.uStart = u0;
    f.uEnd = u1;
    f.vStart = -vc;
    f.vEnd = vc;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  auto wall = [&](double zLo, double zHi, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame.origin = Vec3{0.0, 0.0, zLo};
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = zHi - zLo;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  zone(0.0, kPi, {{bc0, false}, {mzN, false}, {tc0, true}, {mzP, true}});
  zone(kPi, kTwoPi, {{bc1, false}, {mzP, false}, {tc1, true}, {mzN, true}});
  wall(h, zTop, 0.0, kPi, {{tc0, false}, {su1, false}, {rtc0, true}, {su0, true}});
  wall(h, zTop, kPi, kTwoPi, {{tc1, false}, {su0, false}, {rtc1, true}, {su1, true}});
  s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, zTop}, Vec3{0.0, 0.0, 1.0},
                                  {{rtc0, false}, {rtc1, false}}));
  wall(zBot, -h, 0.0, kPi, {{rbc0, false}, {sl1, false}, {bc0, true}, {sl0, true}});
  wall(zBot, -h, kPi, kTwoPi, {{rbc1, false}, {sl0, false}, {bc1, true}, {sl1, true}});
  s.faces.push_back(MakePlaneFace(Vec3{0.0, 0.0, zBot}, Vec3{0.0, 0.0, -1.0},
                                  {{rbc0, true}, {rbc1, true}}));

  AddSingleShell(&s);
  PlaceInFrame(&s, fr);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// The Steinmetz bicylinder: the INTERSECT of two right circular cylinders of **equal radius** whose
/// axes **cross at right angles** (REQ-314 B2b-1 coda, D-2026-09-02-i). Their surfaces meet along two
/// full ellipses (the quartic intersection `x⁴ = …` factors into the planes `z = ±x`), so the result
/// is bounded by four cylinder half-patches and four half-ellipse edges — every edge a closed-form
/// `CurveKind::Ellipse`, no procedural curve. \p fr: origin at the axis crossing, `zAxis` along
/// cylinder A, `xAxis` along cylinder B. Volume is the textbook `16 r³ / 3`, area `16 r²`.
[[nodiscard]] bool BuildSteinmetzIntersection(const ucs::Ucs& fr, double r, Solid* out,
                                              Problem* outWhy) {
  Solid s;
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  const Vec3 c = fr.origin;
  const int v0 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, r)));   // cyl-A angle u = +pi/2
  const int v1 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, -r)));  // u = -pi/2
  const double a = r * std::sqrt(2.0);
  const double b = r;
  const Vec3 nP = ray3d::Normalize(ray3d::Sub(Z, X));  // plane z = x
  const Vec3 nM = ray3d::Normalize(ray3d::Add(Z, X));  // plane z = -x
  const Vec3 mP = ray3d::Normalize(ray3d::Add(Z, X));  // its major direction
  const Vec3 mM = ray3d::Normalize(ray3d::Sub(X, Z));
  // Four half-ellipse arcs, each from v0 to v1; +/- picks the x>0 or x<0 half.
  const int ePp = AddEllipse(&s, v0, v1, c, nP, mP, a, b, -kPi);  // z = x,  x > 0
  const int ePn = AddEllipse(&s, v0, v1, c, nP, mP, a, b, kPi);   // z = x,  x < 0
  const int eMp = AddEllipse(&s, v0, v1, c, nM, mM, a, b, -kPi);  // z = -x, x > 0
  const int eMn = AddEllipse(&s, v0, v1, c, nM, mM, a, b, kPi);   // z = -x, x < 0

  auto halfCyl = [&](const ucs::Ucs& sfFrame, double u0, double u1, int loEdge, bool loRev, int hiEdge,
                     bool hiRev) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = sfFrame;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = 2.0 * r;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = {{loEdge, loRev}, {hiEdge, hiRev}};
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  ucs::Ucs frA = fr;
  frA.origin = ray3d::Sub(c, ray3d::Scale(Z, r));
  ucs::Ucs frB;
  frB.origin = ray3d::Sub(c, ray3d::Scale(X, r));
  frB.zAxis = X;
  frB.xAxis = Z;
  frB.yAxis = ray3d::Normalize(ray3d::Cross(X, Z));
  // Each face: one arc forward, one reversed, so every edge is used once each way overall.
  halfCyl(frA, -kHalfPi, kHalfPi, eMp, false, ePp, true);        // cyl A, x > 0
  halfCyl(frA, kHalfPi, kHalfPi + kPi, eMn, false, ePn, true);   // cyl A, x < 0
  halfCyl(frB, -kHalfPi, kHalfPi, ePp, false, eMn, true);        // cyl B, z > 0
  halfCyl(frB, kHalfPi, kHalfPi + kPi, ePn, false, eMp, true);   // cyl B, z < 0
  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A − B` where `A` and `B` are the Steinmetz pair: cylinder `A` (kept) with a full perpendicular
/// channel of cylinder `B` bored clean through it (REQ-314 B2b-1 coda, D-2026-09-02-i). \p fr: origin
/// at the axis crossing, `zAxis` along `A`, `xAxis` along `B`. \p zA0 / \p zA1 are `A`'s cap heights
/// along its axis, measured from the crossing (`zA0 < -r < 0 < r < zA1`). The two intersection
/// ellipses (planes `z = ±x`) split `A`'s wall into a top band and a bottom band; the channel wall is
/// two inward cylinder patches. Volume is `vol(A) − 16 r³ / 3`.
[[nodiscard]] bool BuildSteinmetzSubtract(const ucs::Ucs& fr, double r, double zA0, double zA1,
                                          Solid* out, Problem* outWhy) {
  Solid s;
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  const Vec3 c = fr.origin;
  auto P = [&](const Vec3& base, double z) { return ray3d::Add(base, ray3d::Scale(Z, z)); };
  const int v0 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, r)));   // ellipse crossing, +Y
  const int v1 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, -r)));  // ellipse crossing, -Y
  const int rTa = AddVertex(&s, P(ray3d::Add(c, ray3d::Scale(Y, r)), zA1));
  const int rTb = AddVertex(&s, P(ray3d::Add(c, ray3d::Scale(Y, -r)), zA1));
  const int rBa = AddVertex(&s, P(ray3d::Add(c, ray3d::Scale(Y, r)), zA0));
  const int rBb = AddVertex(&s, P(ray3d::Add(c, ray3d::Scale(Y, -r)), zA0));

  const double a = r * std::sqrt(2.0);
  const double b = r;
  const Vec3 nP = ray3d::Normalize(ray3d::Sub(Z, X));  // plane z = x
  const Vec3 nM = ray3d::Normalize(ray3d::Add(Z, X));  // plane z = -x
  const Vec3 mP = ray3d::Normalize(ray3d::Add(Z, X));
  const Vec3 mM = ray3d::Normalize(ray3d::Sub(X, Z));
  // The four half-ellipses. aP+/aM+ run v1 -> v0 (through the x>0 side); aP-/aM- run v0 -> v1 (x<0).
  const int aPp = AddEllipse(&s, v1, v0, c, nP, mP, a, b, kPi);
  const int aPn = AddEllipse(&s, v0, v1, c, nP, mP, a, b, kPi);
  const int aMp = AddEllipse(&s, v1, v0, c, nM, mM, a, b, kPi);
  const int aMn = AddEllipse(&s, v0, v1, c, nM, mM, a, b, kPi);

  const Vec3 topC = P(c, zA1);
  const Vec3 botC = P(c, zA0);
  const int rtP = AddArc(&s, rTb, rTa, topC, Z, kPi);   // top rim, x>0
  const int rtN = AddArc(&s, rTa, rTb, topC, Z, kPi);   // top rim, x<0
  const int rbP = AddArc(&s, rBb, rBa, botC, Z, kPi);   // bottom rim, x>0
  const int rbN = AddArc(&s, rBa, rBb, botC, Z, kPi);   // bottom rim, x<0
  const int sTa = AddLine(&s, v0, rTa);
  const int sTb = AddLine(&s, v1, rTb);
  const int sBa = AddLine(&s, v0, rBa);
  const int sBb = AddLine(&s, v1, rBb);

  auto cylBand = [&](const ucs::Ucs& sf, double h, double u0, double u1, bool inward,
                     std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = sf;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = h;
    f.surface.inward = inward;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  ucs::Ucs frTop = fr;  // A's wall, crossing at z = 0, cap at z = zA1
  ucs::Ucs frBot = fr;
  frBot.origin = botC;
  ucs::Ucs frCh;  // cylinder B's channel wall
  frCh.origin = c;
  frCh.zAxis = X;
  frCh.xAxis = Z;
  frCh.yAxis = ray3d::Normalize(ray3d::Cross(X, Z));

  // A's top band (z >= |x|): two half-faces split through the crossing points.
  cylBand(frTop, zA1, -kHalfPi, kHalfPi, false, {{aPp, false}, {sTa, false}, {rtP, true}, {sTb, true}});
  cylBand(frTop, zA1, kHalfPi, kHalfPi + kPi, false,
          {{aMn, false}, {sTb, false}, {rtN, true}, {sTa, true}});
  // A's bottom band (z <= -|x|).
  cylBand(frBot, -zA0, -kHalfPi, kHalfPi, false,
          {{aMp, true}, {sBb, false}, {rbP, false}, {sBa, true}});
  cylBand(frBot, -zA0, kHalfPi, kHalfPi + kPi, false,
          {{aPn, true}, {sBa, false}, {rbN, false}, {sBb, true}});
  // A's two flat caps.
  s.faces.push_back(MakePlaneFace(topC, Z, {{rtP, false}, {rtN, false}}));
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{rbP, true}, {rbN, true}}));
  // B's channel wall, inward: two patches bounded only by ellipse arcs.
  cylBand(frCh, 2.0 * r, -kHalfPi, kHalfPi, true, {{aPp, true}, {aMn, true}});
  cylBand(frCh, 2.0 * r, kHalfPi, kHalfPi + kPi, true, {{aPn, false}, {aMp, false}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∪ B` of the Steinmetz pair — a T-pipe (or cross-pipe): both cylinders' walls outside the other,
/// plus all four flat caps (REQ-314 B2b-1 coda, D-2026-09-02-i). \p fr: origin at the crossing,
/// `zAxis` along `A`, `xAxis` along `B`. \p zA0 / \p zA1 and \p xB0 / \p xB1 are the two cylinders'
/// cap positions from the crossing (`… < -r < 0 < r < …`). The two intersection ellipses (planes
/// `z = ±x`) are the internal seam; each cylinder's wall becomes two bands (a top/bottom for `A`, a
/// `±x` pair for `B`), every band split into two half-faces through the crossing points. 10 vertices,
/// 20 edges, 12 faces. Volume `vol(A) + vol(B) − 16 r³ / 3`.
[[nodiscard]] bool BuildSteinmetzUnion(const ucs::Ucs& fr, double r, double zA0, double zA1,
                                       double xB0, double xB1, Solid* out, Problem* outWhy) {
  Solid s;
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  const Vec3 c = fr.origin;
  auto Wc = [&](double sx, double sz) {
    return ray3d::Add(c, ray3d::Add(ray3d::Scale(X, sx), ray3d::Scale(Z, sz)));
  };
  const int v0 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, r)));   // crossing, +Y
  const int v1 = AddVertex(&s, ray3d::Add(c, ray3d::Scale(Y, -r)));  // crossing, -Y
  const int rTa = AddVertex(&s, ray3d::Add(Wc(0.0, zA1), ray3d::Scale(Y, r)));
  const int rTb = AddVertex(&s, ray3d::Add(Wc(0.0, zA1), ray3d::Scale(Y, -r)));
  const int rBa = AddVertex(&s, ray3d::Add(Wc(0.0, zA0), ray3d::Scale(Y, r)));
  const int rBb = AddVertex(&s, ray3d::Add(Wc(0.0, zA0), ray3d::Scale(Y, -r)));
  const int qPa = AddVertex(&s, ray3d::Add(Wc(xB1, 0.0), ray3d::Scale(Y, r)));
  const int qPb = AddVertex(&s, ray3d::Add(Wc(xB1, 0.0), ray3d::Scale(Y, -r)));
  const int qNa = AddVertex(&s, ray3d::Add(Wc(xB0, 0.0), ray3d::Scale(Y, r)));
  const int qNb = AddVertex(&s, ray3d::Add(Wc(xB0, 0.0), ray3d::Scale(Y, -r)));

  const double a = r * std::sqrt(2.0);
  const double b = r;
  const Vec3 nP = ray3d::Normalize(ray3d::Sub(Z, X));  // plane z = x
  const Vec3 nM = ray3d::Normalize(ray3d::Add(Z, X));  // plane z = -x
  const Vec3 mP = ray3d::Normalize(ray3d::Add(Z, X));
  const Vec3 mM = ray3d::Normalize(ray3d::Sub(X, Z));
  const int ePp = AddEllipse(&s, v1, v0, c, nP, mP, a, b, kPi);  // z = x,  x > 0
  const int ePn = AddEllipse(&s, v0, v1, c, nP, mP, a, b, kPi);  // z = x,  x < 0
  const int eMp = AddEllipse(&s, v1, v0, c, nM, mM, a, b, kPi);  // z = -x, x > 0
  const int eMn = AddEllipse(&s, v0, v1, c, nM, mM, a, b, kPi);  // z = -x, x < 0

  const Vec3 topC = Wc(0.0, zA1);
  const Vec3 botC = Wc(0.0, zA0);
  const Vec3 pxC = Wc(xB1, 0.0);
  const Vec3 nxC = Wc(xB0, 0.0);
  const int rtP = AddArc(&s, rTb, rTa, topC, Z, kPi);   // A top rim, x > 0
  const int rtN = AddArc(&s, rTa, rTb, topC, Z, kPi);   // A top rim, x < 0
  const int rbP = AddArc(&s, rBb, rBa, botC, Z, kPi);   // A bottom rim, x > 0
  const int rbN = AddArc(&s, rBa, rBb, botC, Z, kPi);   // A bottom rim, x < 0
  const int rpA = AddArc(&s, qPb, qPa, pxC, X, kPi);    // B +x rim, z < 0
  const int rpB = AddArc(&s, qPa, qPb, pxC, X, kPi);    // B +x rim, z > 0
  const int rnA = AddArc(&s, qNb, qNa, nxC, X, kPi);    // B -x rim, z < 0
  const int rnB = AddArc(&s, qNa, qNb, nxC, X, kPi);    // B -x rim, z > 0
  const int sTa = AddLine(&s, v0, rTa);
  const int sTb = AddLine(&s, v1, rTb);
  const int sBa = AddLine(&s, v0, rBa);
  const int sBb = AddLine(&s, v1, rBb);
  const int sPa = AddLine(&s, v0, qPa);
  const int sPb = AddLine(&s, v1, qPb);
  const int sNa = AddLine(&s, v0, qNa);
  const int sNb = AddLine(&s, v1, qNb);

  auto band = [&](const ucs::Ucs& sf, double h, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = sf;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = h;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  ucs::Ucs frTop = fr;
  ucs::Ucs frBot = fr;
  frBot.origin = botC;
  ucs::Ucs frPx;
  frPx.origin = c;
  frPx.zAxis = X;
  frPx.xAxis = Z;
  frPx.yAxis = ray3d::Normalize(ray3d::Cross(X, Z));
  ucs::Ucs frNx = frPx;
  frNx.origin = nxC;

  // A's wall outside B: a top band (z >= |x|) and a bottom band, each two half-faces.
  band(frTop, zA1, -kHalfPi, kHalfPi, {{ePp, false}, {sTa, false}, {rtP, true}, {sTb, true}});
  band(frTop, zA1, kHalfPi, kHalfPi + kPi, {{eMn, false}, {sTb, false}, {rtN, true}, {sTa, true}});
  band(frBot, -zA0, -kHalfPi, kHalfPi, {{eMp, true}, {sBb, false}, {rbP, false}, {sBa, true}});
  band(frBot, -zA0, kHalfPi, kHalfPi + kPi, {{ePn, true}, {sBa, false}, {rbN, false}, {sBb, true}});
  s.faces.push_back(MakePlaneFace(topC, Z, {{rtP, false}, {rtN, false}}));
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{rbP, true}, {rbN, true}}));
  // B's wall outside A: a +x band and a -x band, each two half-faces (split at world z = 0).
  band(frPx, xB1, -kHalfPi, kHalfPi, {{ePp, true}, {sPb, false}, {rpB, true}, {sPa, true}});
  band(frPx, xB1, kHalfPi, kHalfPi + kPi, {{eMp, false}, {sPa, false}, {rpA, true}, {sPb, true}});
  band(frNx, -xB0, -kHalfPi, kHalfPi, {{eMn, true}, {sNa, false}, {rnB, false}, {sNb, true}});
  band(frNx, -xB0, kHalfPi, kHalfPi + kPi, {{ePn, false}, {sNb, false}, {rnA, false}, {sNa, true}});
  s.faces.push_back(MakePlaneFace(pxC, X, {{rpA, false}, {rpB, false}}));
  s.faces.push_back(MakePlaneFace(nxC, ray3d::Scale(X, -1.0), {{rnA, true}, {rnB, true}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∩ B` of a thin cylinder `A` (radius \p r, axis `fr.xAxis`) fully crossing a thicker cylinder
/// `B` (radius \p R, axis `fr.zAxis`) at right angles through `fr.origin` — the branch-pipe lens
/// (REQ-314 B2b-2, D-2026-09-03-a). The two operands meet along two **quartic** loops (`fr` local:
/// `x = ±√(R² − r² sin² φ)`, `y = −r sin φ`, `z = r cos φ`), stored as eight `CurveKind::Intersection`
/// half-edges. 8 vertices, 10 edges, 4 faces — two half-bands of `A`'s wall inside `B`, two lens-end
/// patches of `B`'s wall inside `A`. Every face integrates numerically.
[[nodiscard]] bool BuildBranchPipeIntersection(const ucs::Ucs& fr, double r, double R, Solid* out,
                                               Problem* outWhy) {
  if (!(R > r) || !(r > 0.0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const double psi0 = std::asin(std::clamp(r / R, -1.0, 1.0));

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = X;
  aSurf.frame.xAxis = Z;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 4.0 * R;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = Z;
  bSurf.frame.xAxis = X;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = 4.0 * R;

  Solid s;
  auto cplus = [&](double phi, int sign) {
    const double x = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi))) * sign;
    return W(Vec3{x, -r * std::sin(phi), r * std::cos(phi)});
  };
  const int p0 = AddVertex(&s, cplus(0.0, 1));
  const int qb = AddVertex(&s, cplus(kHalfPi, 1));
  const int p1 = AddVertex(&s, cplus(kPi, 1));
  const int qa = AddVertex(&s, cplus(1.5 * kPi, 1));
  const int n0 = AddVertex(&s, cplus(0.0, -1));
  const int nb = AddVertex(&s, cplus(kHalfPi, -1));
  const int n1 = AddVertex(&s, cplus(kPi, -1));
  const int na = AddVertex(&s, cplus(1.5 * kPi, -1));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cplus(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int e1 = isect(p0, qb, 0.25 * kPi, 1);
  const int e2 = isect(qb, p1, 0.75 * kPi, 1);
  const int e3 = isect(p1, qa, 1.25 * kPi, 1);
  const int e4 = isect(qa, p0, 1.75 * kPi, 1);
  const int e5 = isect(n0, nb, 0.25 * kPi, -1);
  const int e6 = isect(nb, n1, 0.75 * kPi, -1);
  const int e7 = isect(n1, na, 1.25 * kPi, -1);
  const int e8 = isect(na, n0, 1.75 * kPi, -1);
  const int sTop = AddLine(&s, n0, p0);  // z = r seam of A
  const int sBot = AddLine(&s, n1, p1);  // z = -r seam of A

  auto cylFace = [&](const Surface& surf, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  cylFace(aSurf, 0.0, kPi,
          {{e1, false}, {e2, false}, {sBot, true}, {e6, true}, {e5, true}, {sTop, false}});
  cylFace(aSurf, kPi, kTwoPi,
          {{e3, false}, {e4, false}, {sTop, true}, {e8, true}, {e7, true}, {sBot, false}});
  cylFace(bSurf, -psi0, psi0, {{e4, true}, {e3, true}, {e2, true}, {e1, true}});
  cylFace(bSurf, kPi - psi0, kPi + psi0, {{e5, false}, {e6, false}, {e7, false}, {e8, false}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `B − A`: the thick cylinder `B` (radius \p R, axis `fr.zAxis`, caps at \p zB0 / \p zB1) with the
/// thin branch cylinder `A` (radius \p r, axis `fr.xAxis`) bored clean through it — a genus-1 solid
/// (REQ-314 B2b-2). 8 vertices, 12 edges, 6 faces: `B`'s wall split in two by the `ψ = ±π/2` seams,
/// each half carrying the branch mouth as an inner loop; `B`'s two flat caps; and `A`'s wall inside
/// `B`, inward, in two halves. Volume `vol(B) − 16 r³/3`-analogue (the lens).
[[nodiscard]] bool BuildBranchPipeSubtract(const ucs::Ucs& fr, double r, double R, double zB0,
                                           double zB1, Solid* out, Problem* outWhy) {
  if (!(R > r) || !(r > 0.0) || !(zB1 - zB0 > 2.0 * r))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = X;
  aSurf.frame.xAxis = Z;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 4.0 * R;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = Z;
  bSurf.frame.xAxis = X;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double x = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi))) * sign;
    return W(Vec3{x, -r * std::sin(phi), r * std::cos(phi)});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{0.0, R, zB0}));
  const int bm = AddVertex(&s, W(Vec3{0.0, -R, zB0}));
  const int tp = AddVertex(&s, W(Vec3{0.0, R, zB1}));
  const int tm = AddVertex(&s, W(Vec3{0.0, -R, zB1}));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);   // C+ through Qa (psi > 0)
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);   // C+ through Qb (psi < 0)
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);  // C- through Na (psi < pi)
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);  // C- through Nb (psi > pi)
  const int sA0 = AddLine(&s, n0, p0);            // A seam z = r
  const int sAp = AddLine(&s, n1, p1);            // A seam z = -r
  const int seamP = AddLine(&s, bp, tp);          // B seam psi = +pi/2
  const int seamM = AddLine(&s, bm, tm);          // B seam psi = -pi/2
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brF = AddArc(&s, bm, bp, botC, Z, kPi);  // bottom rim, front (psi -pi/2 -> pi/2)
  const int brB = AddArc(&s, bp, bm, botC, Z, kPi);  // bottom rim, back
  const int trF = AddArc(&s, tm, tp, topC, Z, kPi);  // top rim, front
  const int trB = AddArc(&s, tp, tm, topC, Z, kPi);  // top rim, back

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(bSurf, -kHalfPi, kHalfPi,
       {Loop{{{brF, false}, {seamP, false}, {trF, true}, {seamM, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(bSurf, kHalfPi, kHalfPi + kPi,
       {Loop{{{brB, false}, {seamM, false}, {trB, true}, {seamP, true}}},
        Loop{{{cnU, true}, {cnL, false}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brF, true}, {brB, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trF, false}, {trB, false}}));
  Surface aIn = aSurf;
  aIn.inward = true;
  face(aIn, 0.0, kPi, {Loop{{{cpL, false}, {sAp, true}, {cnL, true}, {sA0, false}}}});
  face(aIn, kPi, kTwoPi, {Loop{{{cpU, true}, {sA0, true}, {cnU, false}, {sAp, false}}}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∩ B` for a **non-perpendicular** branch pipe (REQ-314 B2b-2, GitHub issue #242): the thin
/// cylinder `A` (radius \p r) crosses the thick `B` (radius \p R) with their axes coplanar and
/// crossing at `fr.origin`, but tilted by \p alpha off perpendicular (`|alpha| < π/2`; `alpha = 0`
/// is \ref BuildBranchPipeIntersection). `fr.zAxis` is `B`'s axis and `fr.xAxis` the in-plane
/// component of `A`'s axis, so `A`'s axis is `cos α·x̂ + sin α·ẑ`. Same lens topology as the
/// perpendicular case — 8 vertices, 10 edges (8 procedural `CurveKind::Intersection` quarter-arcs),
/// 4 faces — with the curve `ζ(φ) = (r sinα cosφ ± √(R² − r² sin²φ)) / cosα` along `A`'s axis. Every
/// face integrates numerically.
[[nodiscard]] bool BuildAngledBranchPipeIntersection(const ucs::Ucs& fr, double r, double R,
                                                     double alpha, Solid* out, Problem* outWhy) {
  if (!(R > r) || !(r > 0.0) || !(std::fabs(alpha) < kHalfPi - 1e-6))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  // A's axis (tHat) and the seam direction the wall's u = 0 follows (xaHat), in fr coords.
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 8.0 * R;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = fr.zAxis;
  bSurf.frame.xAxis = fr.xAxis;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = 8.0 * R;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double root = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi)));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, -r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int qb = AddVertex(&s, cpt(kHalfPi, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int qa = AddVertex(&s, cpt(1.5 * kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int nb = AddVertex(&s, cpt(kHalfPi, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int na = AddVertex(&s, cpt(1.5 * kPi, -1));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int e1 = isect(p0, qb, 0.25 * kPi, 1);
  const int e2 = isect(qb, p1, 0.75 * kPi, 1);
  const int e3 = isect(p1, qa, 1.25 * kPi, 1);
  const int e4 = isect(qa, p0, 1.75 * kPi, 1);
  const int e5 = isect(n0, nb, 0.25 * kPi, -1);
  const int e6 = isect(nb, n1, 0.75 * kPi, -1);
  const int e7 = isect(n1, na, 1.25 * kPi, -1);
  const int e8 = isect(na, n0, 1.75 * kPi, -1);
  const int sTop = AddLine(&s, n0, p0);  // φ = 0 seam of A
  const int sBot = AddLine(&s, n1, p1);  // φ = π seam of A

  auto cylFace = [&](const Surface& surf, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  const double psi0 = std::asin(std::clamp(r / R, -1.0, 1.0));
  cylFace(aSurf, 0.0, kPi,
          {{e1, false}, {e2, false}, {sBot, true}, {e6, true}, {e5, true}, {sTop, false}});
  cylFace(aSurf, kPi, kTwoPi,
          {{e3, false}, {e4, false}, {sTop, true}, {e8, true}, {e7, true}, {sBot, false}});
  cylFace(bSurf, -psi0, psi0, {{e4, true}, {e3, true}, {e2, true}, {e1, true}});
  cylFace(bSurf, kPi - psi0, kPi + psi0, {{e5, false}, {e6, false}, {e7, false}, {e8, false}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∩ B` for a **skew** (offset, non-coplanar) perpendicular branch pipe (REQ-314 B2b-2, GitHub
/// issue #242): the thin cylinder `A` (radius \p r, axis `fr.yAxis`, through `(g, 0, 0)`) crosses the
/// thick `B` (radius \p R, axis `fr.zAxis`) at right angles but with their axes missing each other by
/// \p g (`0 ≤ g`, `g + r < R`). Same lens topology as the coplanar cases — 8 vertices, 10 edges
/// (8 procedural), 4 faces — with the curve `s(φ) = ±√(R² − (g + r sinφ)²)` along `A`'s axis and the
/// two thick-wall patches now off-centre (u ∈ [uLo, uHi] and its mirror). Every face integrates
/// numerically.
[[nodiscard]] bool BuildSkewBranchPipeIntersection(const ucs::Ucs& fr, double r, double R, double g,
                                                   Solid* out, Problem* outWhy) {
  if (!(R > r) || !(r > 0.0) || !(g >= 0.0) || !(g + r < R))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = W(Vec3{g, 0.0, 0.0});
  aSurf.frame.zAxis = Y;             // A's axis
  aSurf.frame.xAxis = fr.zAxis;      // φ = 0 points toward +z
  aSurf.frame.yAxis = fr.xAxis;
  aSurf.radius = r;
  aSurf.height = 8.0 * R;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = 8.0 * R;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double px = g + r * std::sin(phi);
    const double sc = std::sqrt(std::max(0.0, R * R - px * px)) * sign;
    return W(Vec3{px, sc, r * std::cos(phi)});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int qb = AddVertex(&s, cpt(kHalfPi, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int qa = AddVertex(&s, cpt(1.5 * kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int nb = AddVertex(&s, cpt(kHalfPi, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int na = AddVertex(&s, cpt(1.5 * kPi, -1));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int e1 = isect(p0, qb, 0.25 * kPi, 1);
  const int e2 = isect(qb, p1, 0.75 * kPi, 1);
  const int e3 = isect(p1, qa, 1.25 * kPi, 1);
  const int e4 = isect(qa, p0, 1.75 * kPi, 1);
  const int e5 = isect(n0, nb, 0.25 * kPi, -1);
  const int e6 = isect(nb, n1, 0.75 * kPi, -1);
  const int e7 = isect(n1, na, 1.25 * kPi, -1);
  const int e8 = isect(na, n0, 1.75 * kPi, -1);
  const int sTop = AddLine(&s, n0, p0);  // φ = 0 seam of A
  const int sBot = AddLine(&s, n1, p1);  // φ = π seam of A

  auto cylFace = [&](const Surface& surf, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  // The +s mouth on B runs from φ = π/2 (px = g + r, nearest B's axis) to φ = 3π/2 (px = g − r).
  const double uLo = std::atan2(std::sqrt(std::max(0.0, R * R - (g + r) * (g + r))), g + r);
  const double uHi = std::atan2(std::sqrt(std::max(0.0, R * R - (g - r) * (g - r))), g - r);
  cylFace(aSurf, 0.0, kPi,
          {{e1, false}, {e2, false}, {sBot, true}, {e6, true}, {e5, true}, {sTop, false}});
  cylFace(aSurf, kPi, kTwoPi,
          {{e3, false}, {e4, false}, {sTop, true}, {e8, true}, {e7, true}, {sBot, false}});
  cylFace(bSurf, uLo, uHi, {{e4, true}, {e3, true}, {e2, true}, {e1, true}});
  cylFace(bSurf, -uHi, -uLo, {{e5, false}, {e6, false}, {e7, false}, {e8, false}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// Shared frame + curve helpers for the skew (offset) perpendicular branch pipe. `A`'s axis is
/// `fr.yAxis` through `(g, 0, 0)`; `B`'s is `fr.zAxis`. Both mouths sit on `B`'s `+x` half, so the
/// thick wall is split at `ψ = 0` and `ψ = π` (not `±π/2`) — one mouth per half.
struct SkewBranch {
  ucs::Ucs fr;
  double r = 0.0;
  double R = 0.0;
  double g = 0.0;
  Surface aSurf;
  Surface bSurf;
  [[nodiscard]] Vec3 W(const Vec3& l) const { return ucs::UcsToWorld(fr, l); }
  [[nodiscard]] Vec3 cpt(double phi, int sign) const {
    const double px = g + r * std::sin(phi);
    return W(Vec3{px, std::sqrt(std::max(0.0, R * R - px * px)) * sign, r * std::cos(phi)});
  }
  [[nodiscard]] Vec3 thinPt(double phi, double y) const {
    return W(Vec3{g + r * std::sin(phi), y, r * std::cos(phi)});
  }
};

[[nodiscard]] bool MakeSkewBranch(const ucs::Ucs& fr, double r, double R, double g, SkewBranch* sb) {
  if (!(R > r) || !(r > 0.0) || !(g >= 0.0) || !(g + r < R))
    return false;
  sb->fr = fr;
  sb->r = r;
  sb->R = R;
  sb->g = g;
  sb->aSurf.kind = SurfaceKind::Cylinder;
  sb->aSurf.frame.origin = sb->W(Vec3{g, 0.0, 0.0});
  sb->aSurf.frame.zAxis = fr.yAxis;
  sb->aSurf.frame.xAxis = fr.zAxis;
  sb->aSurf.frame.yAxis = fr.xAxis;
  sb->aSurf.radius = r;
  sb->aSurf.height = 8.0 * R;
  sb->bSurf.kind = SurfaceKind::Cylinder;
  sb->bSurf.frame = fr;
  sb->bSurf.radius = R;
  sb->bSurf.height = 8.0 * R;
  return true;
}

/// `B − A` for a **skew** (offset) perpendicular branch pipe (REQ-314 B2b-2, GitHub issue #242): the
/// thin `A` (axis `fr.yAxis`, through `(g, 0, 0)`) bored clean through the thick `B` (axis
/// `fr.zAxis`, caps \p zB0 / \p zB1). Both mouths lie on `B`'s `+x` half, so the thick wall splits at
/// `ψ = 0` / `ψ = π` — each half carries one mouth as an inner loop. 8v / 12e / 6f, genus 1.
[[nodiscard]] bool BuildSkewBranchPipeSubtract(const ucs::Ucs& fr, double r, double R, double g,
                                               double zB0, double zB1, Solid* out, Problem* outWhy) {
  SkewBranch sb;
  if (!MakeSkewBranch(fr, r, R, g, &sb) || !(zB1 - zB0 > 2.0 * r))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return sb.W(l); };
  Solid s;
  const int p0 = AddVertex(&s, sb.cpt(0.0, 1));
  const int p1 = AddVertex(&s, sb.cpt(kPi, 1));
  const int n0 = AddVertex(&s, sb.cpt(0.0, -1));
  const int n1 = AddVertex(&s, sb.cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{R, 0.0, zB0}));    // ψ = 0
  const int bm = AddVertex(&s, W(Vec3{-R, 0.0, zB0}));   // ψ = π
  const int tp = AddVertex(&s, W(Vec3{R, 0.0, zB1}));
  const int tm = AddVertex(&s, W(Vec3{-R, 0.0, zB1}));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = sb.cpt(witnessPhi, sign);
    e.isectSurfaces = {sb.aSurf, sb.bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int sA0 = AddLine(&s, n0, p0);
  const int sAp = AddLine(&s, n1, p1);
  const int seam0 = AddLine(&s, bp, tp);   // B seam ψ = 0
  const int seamPi = AddLine(&s, bm, tm);  // B seam ψ = π
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brU = AddArc(&s, bp, bm, botC, Z, kPi);  // bottom rim ψ 0 -> π (through +y)
  const int brD = AddArc(&s, bm, bp, botC, Z, kPi);  // bottom rim ψ π -> 2π (through -y)
  const int trU = AddArc(&s, tp, tm, topC, Z, kPi);
  const int trD = AddArc(&s, tm, tp, topC, Z, kPi);

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(sb.bSurf, 0.0, kPi,
       {Loop{{{brU, false}, {seamPi, false}, {trU, true}, {seam0, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(sb.bSurf, kPi, kTwoPi,
       {Loop{{{brD, false}, {seam0, false}, {trD, true}, {seamPi, true}}},
        Loop{{{cnU, true}, {cnL, false}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brU, true}, {brD, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trU, false}, {trD, false}}));
  Surface aIn = sb.aSurf;
  aIn.inward = true;
  face(aIn, 0.0, kPi, {Loop{{{cpL, false}, {sAp, true}, {cnL, true}, {sA0, false}}}});
  face(aIn, kPi, kTwoPi, {Loop{{{cpU, true}, {sA0, true}, {cnU, false}, {sAp, false}}}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∪ B` for a **skew** (offset) perpendicular branch pipe (REQ-314 B2b-2, GitHub issue #242). Same
/// thick-wall split as \ref BuildSkewBranchPipeSubtract; the two thin stubs run out along `±fr.yAxis`
/// to flat caps at \p yA0 / \p yA1 (measured from `fr.origin`). 12v / 18e / 10f.
[[nodiscard]] bool BuildSkewBranchPipeUnion(const ucs::Ucs& fr, double r, double R, double g,
                                            double zB0, double zB1, double yA0, double yA1, Solid* out,
                                            Problem* outWhy) {
  SkewBranch sb;
  const double sMax = std::sqrt(std::max(0.0, R * R - (g - r) * (g - r)));
  if (!MakeSkewBranch(fr, r, R, g, &sb) || !(zB1 - zB0 > 2.0 * r) || !(yA0 < -sMax) || !(yA1 > sMax))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return sb.W(l); };
  Solid s;
  const int p0 = AddVertex(&s, sb.cpt(0.0, 1));
  const int p1 = AddVertex(&s, sb.cpt(kPi, 1));
  const int n0 = AddVertex(&s, sb.cpt(0.0, -1));
  const int n1 = AddVertex(&s, sb.cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{R, 0.0, zB0}));
  const int bm = AddVertex(&s, W(Vec3{-R, 0.0, zB0}));
  const int tp = AddVertex(&s, W(Vec3{R, 0.0, zB1}));
  const int tm = AddVertex(&s, W(Vec3{-R, 0.0, zB1}));
  const int k0 = AddVertex(&s, sb.thinPt(0.0, yA1));  // +y stub rim
  const int k1 = AddVertex(&s, sb.thinPt(kPi, yA1));
  const int m0 = AddVertex(&s, sb.thinPt(0.0, yA0));  // -y stub rim
  const int m1 = AddVertex(&s, sb.thinPt(kPi, yA0));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = sb.cpt(witnessPhi, sign);
    e.isectSurfaces = {sb.aSurf, sb.bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int seam0 = AddLine(&s, bp, tp);
  const int seamPi = AddLine(&s, bm, tm);
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brU = AddArc(&s, bp, bm, botC, Z, kPi);
  const int brD = AddArc(&s, bm, bp, botC, Z, kPi);
  const int trU = AddArc(&s, tp, tm, topC, Z, kPi);
  const int trD = AddArc(&s, tm, tp, topC, Z, kPi);
  const int ks0 = AddLine(&s, p0, k0);
  const int ksP = AddLine(&s, p1, k1);
  const int ms0 = AddLine(&s, n0, m0);
  const int msP = AddLine(&s, n1, m1);
  const Vec3 kC = W(Vec3{g, yA1, 0.0});
  const Vec3 mC = W(Vec3{g, yA0, 0.0});
  const Vec3 negY = ray3d::Scale(Y, -1.0);
  const int krF = AddArc(&s, k0, k1, kC, Y, kPi);
  const int krB = AddArc(&s, k1, k0, kC, Y, kPi);
  const int mrF = AddArc(&s, m0, m1, mC, negY, kPi);
  const int mrB = AddArc(&s, m1, m0, mC, negY, kPi);

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(sb.bSurf, 0.0, kPi,
       {Loop{{{brU, false}, {seamPi, false}, {trU, true}, {seam0, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(sb.bSurf, kPi, kTwoPi,
       {Loop{{{brD, false}, {seam0, false}, {trD, true}, {seamPi, true}}},
        Loop{{{cnU, false}, {cnL, true}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brU, true}, {brD, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trU, false}, {trD, false}}));
  face(sb.aSurf, 0.0, kPi, {Loop{{{cpL, false}, {ksP, false}, {krF, true}, {ks0, true}}}});
  face(sb.aSurf, kPi, kTwoPi, {Loop{{{cpU, true}, {ks0, false}, {krB, true}, {ksP, true}}}});
  face(sb.aSurf, 0.0, kPi, {Loop{{{cnL, false}, {msP, false}, {mrF, true}, {ms0, true}}}});
  face(sb.aSurf, kPi, kTwoPi, {Loop{{{cnU, true}, {ms0, false}, {mrB, true}, {msP, true}}}});
  s.faces.push_back(MakePlaneFace(kC, Y, {{krF, false}, {krB, false}}));
  s.faces.push_back(MakePlaneFace(mC, negY, {{mrF, false}, {mrB, false}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// One stub of `A − B` (thin − thick) for a **skew** (offset) perpendicular branch pipe (REQ-314
/// B2b-2, GitHub issue #242): the thick `B` bites the offset thin `A` in two. The \p sideSign stub —
/// `A`'s wall in two u halves from the flat cap at `fr.yAxis`-parameter \p yFlat to the quartic mouth,
/// plus an **inward** off-centre patch of `B`'s wall as the concave end. 4v / 6e / 4f, χ = 2.
[[nodiscard]] bool BuildSkewBranchPipeThinStub(const ucs::Ucs& fr, double r, double R, double g,
                                               double yFlat, int sideSign, Solid* out,
                                               Problem* outWhy) {
  SkewBranch sb;
  const double sMax = std::sqrt(std::max(0.0, R * R - (g - r) * (g - r)));
  if (!MakeSkewBranch(fr, r, R, g, &sb) || !(std::fabs(yFlat) > sMax + 1e-9 * (R + r)))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const int msign = sideSign > 0 ? 1 : -1;
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return sb.W(l); };
  auto cpt = [&](double phi) { return sb.cpt(phi, msign); };

  Solid s;
  const int p0 = AddVertex(&s, cpt(0.0));                 // mouth φ = 0
  const int pP = AddVertex(&s, cpt(kPi));                 // mouth φ = π
  const int q0 = AddVertex(&s, sb.thinPt(0.0, yFlat));    // rim φ = 0
  const int qP = AddVertex(&s, sb.thinPt(kPi, yFlat));    // rim φ = π

  auto isect = [&](int a, int b, double witnessPhi) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = a;
    e.v1 = b;
    e.frame.origin = cpt(witnessPhi);
    e.isectSurfaces = {sb.aSurf, sb.bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int eP = isect(p0, pP, 0.5 * kPi);
  const int eN = isect(pP, p0, 1.5 * kPi);
  const int sp0 = AddLine(&s, p0, q0);
  const int spP = AddLine(&s, pP, qP);
  const Vec3 fc = W(Vec3{g, yFlat, 0.0});
  const int rc0 = AddArc(&s, q0, qP, fc, Y, kPi);
  const int rcP = AddArc(&s, qP, q0, fc, Y, kPi);

  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = sb.aSurf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  const double uLo = std::atan2(std::sqrt(std::max(0.0, R * R - (g + r) * (g + r))), g + r);
  const double uHi = std::atan2(std::sqrt(std::max(0.0, R * R - (g - r) * (g - r))), g - r);
  auto dimple = [&](std::vector<EdgeUse> uses) {
    Face f;
    f.surface = sb.bSurf;
    f.surface.inward = true;
    f.uStart = msign > 0 ? uLo : -uHi;
    f.uEnd = msign > 0 ? uHi : -uLo;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };

  if (msign > 0) {
    wall(0.0, kPi, {{eP, false}, {spP, false}, {rc0, true}, {sp0, true}});
    wall(kPi, kTwoPi, {{eN, false}, {sp0, false}, {rcP, true}, {spP, true}});
    s.faces.push_back(MakePlaneFace(fc, Y, {{rc0, false}, {rcP, false}}));
    dimple({{eN, true}, {eP, true}});
  } else {
    wall(0.0, kPi, {{rc0, false}, {spP, true}, {eP, true}, {sp0, false}});
    wall(kPi, kTwoPi, {{rcP, false}, {sp0, true}, {eN, true}, {spP, false}});
    s.faces.push_back(MakePlaneFace(fc, ray3d::Scale(Y, -1.0), {{rc0, true}, {rcP, true}}));
    dimple({{eP, false}, {eN, false}});
  }

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∩ B` for the **fully general** branch pipe (REQ-314 B2b-2, GitHub issue #242): the thin
/// cylinder `A` (radius \p r) crosses the thick `B` (radius \p R) with axes that are neither
/// coplanar nor perpendicular — tilted **and** offset at once. `fr.zAxis` is `B`'s axis; `fr.xAxis`
/// is `A`'s own in-plane (⊥ `fr.zAxis`) direction, so `A`'s axis is `cos α·x̂ + sin α·ẑ` (as in
/// \ref BuildAngledBranchPipeIntersection); it passes through `(0, g, 0)` in that frame, `g` the
/// perpendicular offset (as in \ref BuildSkewBranchPipeIntersection). Both prior builders are the
/// `g = 0` and `α = 0` special cases of the one quadratic here:
/// `s(φ) = (r sinα cosφ ± √(R² − (g − r sinφ)²)) / cosα`. Same 8v / 10e (8 procedural) / 4f, χ = 2
/// lens topology; the thick-wall mouth patches no longer have a closed-form angular extent, so their
/// `u` bounds are found by a one-time sample scan with margin (the numeric strip search at
/// integration time finds the exact band regardless). Every face integrates numerically.
[[nodiscard]] bool BuildGeneralBranchPipeIntersection(const ucs::Ucs& fr, double r, double R,
                                                       double alpha, double g, Solid* out,
                                                       Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  if (!(R > r) || !(r > 0.0) || !(ca > 1e-6))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = W(Vec3{0.0, g, 0.0});
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 8.0 * (R + std::fabs(g));
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = 8.0 * (R + std::fabs(g));

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double K = r * std::cos(phi);
    const double py = g - r * std::sin(phi);
    const double root = std::sqrt(std::max(0.0, R * R - py * py));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return W(Vec3{zeta * ca - K * sa, py, zeta * sa + K * ca});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int qb = AddVertex(&s, cpt(kHalfPi, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int qa = AddVertex(&s, cpt(1.5 * kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int nb = AddVertex(&s, cpt(kHalfPi, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int na = AddVertex(&s, cpt(1.5 * kPi, -1));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int e1 = isect(p0, qb, 0.25 * kPi, 1);
  const int e2 = isect(qb, p1, 0.75 * kPi, 1);
  const int e3 = isect(p1, qa, 1.25 * kPi, 1);
  const int e4 = isect(qa, p0, 1.75 * kPi, 1);
  const int e5 = isect(n0, nb, 0.25 * kPi, -1);
  const int e6 = isect(nb, n1, 0.75 * kPi, -1);
  const int e7 = isect(n1, na, 1.25 * kPi, -1);
  const int e8 = isect(na, n0, 1.75 * kPi, -1);
  const int sTop = AddLine(&s, n0, p0);  // φ = 0 seam of A
  const int sBot = AddLine(&s, n1, p1);  // φ = π seam of A

  auto cylFace = [&](const Surface& surf, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  // The thick-wall mouth has no closed-form angular extent once both alpha and g are nonzero, so
  // sample it: track each side's u-range (unwrapped about its phi = pi/2 / 3pi/2 sample so a mouth
  // near the +/-pi seam doesn't spuriously wrap) and pad with a margin — the strip search at
  // integration/tessellation time finds the exact band regardless of how generous this is.
  auto thickU = [&](const Vec3& p) {
    const Vec3 d = ray3d::Sub(p, fr.origin);
    return std::atan2(ray3d::Dot(d, fr.yAxis), ray3d::Dot(d, fr.xAxis));
  };
  auto scanRange = [&](int sign, double refPhi) {
    const double uRef = thickU(cpt(refPhi, sign));
    double lo = uRef;
    double hi = uRef;
    constexpr int kSamples = 360;
    for (int i = 0; i <= kSamples; ++i) {
      const double phi = kTwoPi * static_cast<double>(i) / kSamples;
      double u = thickU(cpt(phi, sign));
      u -= kTwoPi * std::round((u - uRef) / kTwoPi);  // unwrap near uRef
      lo = std::min(lo, u);
      hi = std::max(hi, u);
    }
    const double margin = 0.15 * (hi - lo) + 1e-6;
    return std::pair<double, double>{lo - margin, hi + margin};
  };
  const auto [uLoP, uHiP] = scanRange(1, kHalfPi);
  const auto [uLoN, uHiN] = scanRange(-1, -kHalfPi);
  cylFace(aSurf, 0.0, kPi,
          {{e1, false}, {e2, false}, {sBot, true}, {e6, true}, {e5, true}, {sTop, false}});
  cylFace(aSurf, kPi, kTwoPi,
          {{e3, false}, {e4, false}, {sTop, true}, {e8, true}, {e7, true}, {sBot, false}});
  cylFace(bSurf, uLoP, uHiP, {{e4, true}, {e3, true}, {e2, true}, {e1, true}});
  cylFace(bSurf, uLoN, uHiN, {{e5, false}, {e6, false}, {e7, false}, {e8, false}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// The two mouths a general (tilted-and-skew) branch pipe cuts into `B`'s wall do not sit at fixed,
/// predictable longitudes the way every narrower special case's do, so the wall's two seams have to
/// be placed dynamically: at the midpoints of the two angular gaps between the mouths. Scans both
/// mouths' `u`-extent the same way \ref BuildGeneralBranchPipeIntersection bounds them, orders the
/// two by longitude, and returns the two seam angles (`seamA < seamB < seamA + 2π`) plus which
/// mouth (`pFirst`) falls in `[seamA, seamB]`.
struct GeneralBranchSeams {
  double seamA = 0.0;
  double seamB = 0.0;
  bool pFirst = true;  ///< true: the `+` mouth (sign 1) is the one spanning [seamA, seamB].
};

[[nodiscard]] GeneralBranchSeams FindGeneralBranchSeams(const ucs::Ucs& fr,
                                                        const std::function<Vec3(double, int)>& cpt) {
  auto thickU = [&](const Vec3& p) {
    const Vec3 d = ray3d::Sub(p, fr.origin);
    return std::atan2(ray3d::Dot(d, fr.yAxis), ray3d::Dot(d, fr.xAxis));
  };
  auto scan = [&](int sign, double refPhi, double* lo, double* hi) {
    const double uRef = thickU(cpt(refPhi, sign));
    *lo = uRef;
    *hi = uRef;
    constexpr int kSamples = 360;
    for (int i = 0; i <= kSamples; ++i) {
      const double phi = kTwoPi * static_cast<double>(i) / kSamples;
      double u = thickU(cpt(phi, sign));
      u -= kTwoPi * std::round((u - uRef) / kTwoPi);
      *lo = std::min(*lo, u);
      *hi = std::max(*hi, u);
    }
  };
  double pLo = 0.0, pHi = 0.0, nLo = 0.0, nHi = 0.0;
  scan(1, kHalfPi, &pLo, &pHi);
  scan(-1, -kHalfPi, &nLo, &nHi);
  const double pC = 0.5 * (pLo + pHi);
  double nC = 0.5 * (nLo + nHi);
  const double shift = -kTwoPi * std::round((nC - pC) / kTwoPi);
  nLo += shift;
  nHi += shift;
  nC += shift;
  GeneralBranchSeams gs;
  gs.pFirst = pC < nC;
  const double lo1 = gs.pFirst ? pLo : nLo;
  const double hi1 = gs.pFirst ? pHi : nHi;
  const double lo2 = gs.pFirst ? nLo : pLo;
  const double hi2 = gs.pFirst ? nHi : pHi;
  gs.seamA = 0.5 * (hi1 + lo2);          // the gap between mouth 1 and mouth 2
  gs.seamB = 0.5 * (hi2 + lo1 + kTwoPi);  // the gap between mouth 2 and mouth 1, wrapped
  return gs;
}

/// `B − A` for the **fully general** branch pipe (REQ-314 B2b-2, GitHub issue #242): the thin `A`
/// (tilted by \p alpha, offset by \p g — see \ref BuildGeneralBranchPipeIntersection) bored clean
/// through the thick `B` (axis `fr.zAxis`, caps \p zB0 / \p zB1). `B`'s wall splits at the two
/// dynamic seams \ref FindGeneralBranchSeams finds; everything else — the bore, the caps, the mouth
/// loops' windings (fixed per edge, independent of which seam-bounded face ends up hosting them) —
/// mirrors \ref BuildBranchPipeSubtract. 8 vertices, 12 edges, 6 faces; genus 1.
[[nodiscard]] bool BuildGeneralBranchPipeSubtract(const ucs::Ucs& fr, double r, double R, double alpha,
                                                  double g, double zB0, double zB1, Solid* out,
                                                  Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  if (!(R > r) || !(r > 0.0) || !(ca > 1e-6) || !(zB1 - zB0 > 2.0 * r))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = W(Vec3{0.0, g, 0.0});
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 8.0 * (R + std::fabs(g));
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double K = r * std::cos(phi);
    const double py = g - r * std::sin(phi);
    const double root = std::sqrt(std::max(0.0, R * R - py * py));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return W(Vec3{zeta * ca - K * sa, py, zeta * sa + K * ca});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int sA0 = AddLine(&s, n0, p0);
  const int sAp = AddLine(&s, n1, p1);

  const GeneralBranchSeams gs = FindGeneralBranchSeams(fr, cpt);
  auto rimPt = [&](double th, double z) { return W(Vec3{R * std::cos(th), R * std::sin(th), z}); };
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int bA = AddVertex(&s, rimPt(gs.seamA, zB0));
  const int bB = AddVertex(&s, rimPt(gs.seamB, zB0));
  const int tA = AddVertex(&s, rimPt(gs.seamA, zB1));
  const int tB = AddVertex(&s, rimPt(gs.seamB, zB1));
  const int seamLineA = AddLine(&s, bA, tA);
  const int seamLineB = AddLine(&s, bB, tB);
  const int brAB = AddArc(&s, bA, bB, botC, Z, gs.seamB - gs.seamA);
  const int brBA = AddArc(&s, bB, bA, botC, Z, kTwoPi - (gs.seamB - gs.seamA));
  const int trAB = AddArc(&s, tA, tB, topC, Z, gs.seamB - gs.seamA);
  const int trBA = AddArc(&s, tB, tA, topC, Z, kTwoPi - (gs.seamB - gs.seamA));

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  // [seamA, seamB] hosts mouth 2 (whichever sign comes second in the unwrapped order); the other
  // face hosts mouth 1. Each mouth loop's own winding is fixed by the edge (see FindGeneralBranchSeams
  // doc) — independent of which face slot it lands in.
  const Loop pMouth{{{cpU, false}, {cpL, true}}};
  const Loop nMouth{{{cnU, true}, {cnL, false}}};
  const Loop& mouth2 = gs.pFirst ? nMouth : pMouth;
  const Loop& mouth1 = gs.pFirst ? pMouth : nMouth;
  face(bSurf, gs.seamA, gs.seamB,
       {Loop{{{brAB, false}, {seamLineB, false}, {trAB, true}, {seamLineA, true}}}, mouth2});
  face(bSurf, gs.seamB, gs.seamA + kTwoPi,
       {Loop{{{brBA, false}, {seamLineA, false}, {trBA, true}, {seamLineB, true}}}, mouth1});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brAB, true}, {brBA, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trAB, false}, {trBA, false}}));
  Surface aIn = aSurf;
  aIn.inward = true;
  face(aIn, 0.0, kPi, {Loop{{{cpL, false}, {sAp, true}, {cnL, true}, {sA0, false}}}});
  face(aIn, kPi, kTwoPi, {Loop{{{cpU, true}, {sA0, true}, {cnU, false}, {sAp, false}}}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∪ B` for the **fully general** branch pipe (REQ-314 B2b-2, GitHub issue #242): same dynamic
/// seam placement as \ref BuildGeneralBranchPipeSubtract; the two thin stubs run out along `±tHat`
/// (`A`'s tilted axis) to flat caps at the axis parameters \p zetaA0 / \p zetaA1, exactly as
/// \ref BuildAngledBranchPipeUnion's stubs do. 12 vertices, 18 edges, 10 faces.
[[nodiscard]] bool BuildGeneralBranchPipeUnion(const ucs::Ucs& fr, double r, double R, double alpha,
                                               double g, double zB0, double zB1, double zetaA0,
                                               double zetaA1, Solid* out, Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  if (!(R > r) || !(r > 0.0) || !(ca > 1e-6) || !(zB1 - zB0 > 2.0 * r) || !(zetaA1 > zetaA0))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));
  auto thinPt = [&](double phi, double zeta) {
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, g - r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = W(Vec3{0.0, g, 0.0});
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = zetaA1 - zetaA0;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double root = std::sqrt(std::max(0.0, R * R - (g - r * std::sin(phi)) * (g - r * std::sin(phi))));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return thinPt(phi, zeta);
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int k0 = AddVertex(&s, thinPt(0.0, zetaA1));
  const int k1 = AddVertex(&s, thinPt(kPi, zetaA1));
  const int m0 = AddVertex(&s, thinPt(0.0, zetaA0));
  const int m1 = AddVertex(&s, thinPt(kPi, zetaA0));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int ks0 = AddLine(&s, p0, k0);
  const int ksP = AddLine(&s, p1, k1);
  const int ms0 = AddLine(&s, n0, m0);
  const int msP = AddLine(&s, n1, m1);
  const Vec3 kC = W(Vec3{zetaA1 * ca, g, zetaA1 * sa});
  const Vec3 mC = W(Vec3{zetaA0 * ca, g, zetaA0 * sa});
  const Vec3 negT = ray3d::Scale(tHat, -1.0);
  const int krF = AddArc(&s, k0, k1, kC, tHat, kPi);
  const int krB = AddArc(&s, k1, k0, kC, tHat, kPi);
  const int mrF = AddArc(&s, m0, m1, mC, negT, kPi);
  const int mrB = AddArc(&s, m1, m0, mC, negT, kPi);

  const GeneralBranchSeams gs = FindGeneralBranchSeams(fr, cpt);
  auto rimPt = [&](double th, double z) { return W(Vec3{R * std::cos(th), R * std::sin(th), z}); };
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int bA = AddVertex(&s, rimPt(gs.seamA, zB0));
  const int bB = AddVertex(&s, rimPt(gs.seamB, zB0));
  const int tA = AddVertex(&s, rimPt(gs.seamA, zB1));
  const int tB = AddVertex(&s, rimPt(gs.seamB, zB1));
  const int seamLineA = AddLine(&s, bA, tA);
  const int seamLineB = AddLine(&s, bB, tB);
  const int brAB = AddArc(&s, bA, bB, botC, Z, gs.seamB - gs.seamA);
  const int brBA = AddArc(&s, bB, bA, botC, Z, kTwoPi - (gs.seamB - gs.seamA));
  const int trAB = AddArc(&s, tA, tB, topC, Z, gs.seamB - gs.seamA);
  const int trBA = AddArc(&s, tB, tA, topC, Z, kTwoPi - (gs.seamB - gs.seamA));

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  const Loop pMouth{{{cpU, false}, {cpL, true}}};
  const Loop nMouth{{{cnU, false}, {cnL, true}}};
  const Loop& mouth2 = gs.pFirst ? nMouth : pMouth;
  const Loop& mouth1 = gs.pFirst ? pMouth : nMouth;
  face(bSurf, gs.seamA, gs.seamB,
       {Loop{{{brAB, false}, {seamLineB, false}, {trAB, true}, {seamLineA, true}}}, mouth2});
  face(bSurf, gs.seamB, gs.seamA + kTwoPi,
       {Loop{{{brBA, false}, {seamLineA, false}, {trBA, true}, {seamLineB, true}}}, mouth1});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brAB, true}, {brBA, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trAB, false}, {trBA, false}}));
  face(aSurf, 0.0, kPi, {Loop{{{cpL, false}, {ksP, false}, {krF, true}, {ks0, true}}}});
  face(aSurf, kPi, kTwoPi, {Loop{{{cpU, true}, {ks0, false}, {krB, true}, {ksP, true}}}});
  face(aSurf, 0.0, kPi, {Loop{{{cnL, false}, {msP, false}, {mrF, true}, {ms0, true}}}});
  face(aSurf, kPi, kTwoPi, {Loop{{{cnU, true}, {ms0, false}, {mrB, true}, {msP, true}}}});
  s.faces.push_back(MakePlaneFace(kC, tHat, {{krF, false}, {krB, false}}));
  s.faces.push_back(MakePlaneFace(mC, negT, {{mrF, false}, {mrB, false}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// One stub of `A − B` (thin − thick) for the **fully general** (tilted-and-skew) branch pipe
/// (REQ-314 B2b-2, GitHub issue #283): the thick `B` bites the offset-and-tilted thin `A` in two.
/// Generalises \ref BuildBranchPipeThinStub (tilt only) and \ref BuildSkewBranchPipeThinStub (offset
/// only) exactly the way \ref BuildGeneralBranchPipeIntersection generalises their non-stub
/// siblings: identical 4v / 6e (2 procedural) / 4f, χ = 2 topology, the mouth curve is the general
/// `ζ(φ)`, and the thick-wall dimple's angular extent has no closed form once both `alpha` and `g`
/// are nonzero, so it is found the same sample-scan way \ref BuildGeneralBranchPipeIntersection
/// bounds its mouth patches.
[[nodiscard]] bool BuildGeneralBranchPipeThinStub(const ucs::Ucs& fr, double r, double R, double alpha,
                                                  double g, double zetaFlat, int sideSign, Solid* out,
                                                  Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  const double zetaMax = (r * std::fabs(sa) + R) / ca;
  if (!(R > r) || !(r > 0.0) || !(ca > 1e-6) || !(std::fabs(g) + r < R) ||
      !(std::fabs(zetaFlat) > zetaMax + 1e-9 * (R + r)))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const int msign = sideSign > 0 ? 1 : -1;
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));
  auto thinPt = [&](double phi, double zeta) {
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, g - r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };
  auto cpt = [&](double phi) {
    const double py = g - r * std::sin(phi);
    const double root = std::sqrt(std::max(0.0, R * R - py * py));
    return thinPt(phi, (r * sa * std::cos(phi) + msign * root) / ca);
  };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = W(Vec3{0.0, g, 0.0});
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 4.0 * (std::fabs(zetaFlat) + R);
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = 8.0 * (R + std::fabs(g));

  Solid s;
  const int p0 = AddVertex(&s, cpt(0.0));               // mouth phi = 0
  const int pP = AddVertex(&s, cpt(kPi));               // mouth phi = pi
  const int q0 = AddVertex(&s, thinPt(0.0, zetaFlat));  // rim phi = 0
  const int qP = AddVertex(&s, thinPt(kPi, zetaFlat));  // rim phi = pi

  auto isect = [&](int a, int b, double witnessPhi) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = a;
    e.v1 = b;
    e.frame.origin = cpt(witnessPhi);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int eP = isect(p0, pP, 0.5 * kPi);  // mouth, y > 0 side
  const int eN = isect(pP, p0, 1.5 * kPi);  // mouth, y < 0 side
  const int sp0 = AddLine(&s, p0, q0);      // wall seam phi = 0
  const int spP = AddLine(&s, pP, qP);      // wall seam phi = pi
  const Vec3 fc = W(Vec3{zetaFlat * ca, g, zetaFlat * sa});
  const int rc0 = AddArc(&s, q0, qP, fc, tHat, kPi);  // rim, y > 0
  const int rcP = AddArc(&s, qP, q0, fc, tHat, kPi);  // rim, y < 0

  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = aSurf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  // The mouth's u-extent on B's wall has no closed form once both alpha and g are nonzero (see
  // BuildGeneralBranchPipeIntersection::scanRange); sample it the same way, about this one mouth.
  auto thickU = [&](const Vec3& p) {
    const Vec3 d = ray3d::Sub(p, fr.origin);
    return std::atan2(ray3d::Dot(d, fr.yAxis), ray3d::Dot(d, fr.xAxis));
  };
  const double refPhi = msign > 0 ? kHalfPi : -kHalfPi;
  const double uRef = thickU(cpt(refPhi));
  double uLo = uRef;
  double uHi = uRef;
  constexpr int kSamples = 360;
  for (int i = 0; i <= kSamples; ++i) {
    const double phi = kTwoPi * static_cast<double>(i) / kSamples;
    double u = thickU(cpt(phi));
    u -= kTwoPi * std::round((u - uRef) / kTwoPi);
    uLo = std::min(uLo, u);
    uHi = std::max(uHi, u);
  }
  const double margin = 0.15 * (uHi - uLo) + 1e-6;
  uLo -= margin;
  uHi += margin;
  auto dimple = [&](std::vector<EdgeUse> uses) {
    Face f;
    f.surface = bSurf;
    f.surface.inward = true;
    f.uStart = uLo;
    f.uEnd = uHi;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };

  if (msign > 0) {
    wall(0.0, kPi, {{eP, false}, {spP, false}, {rc0, true}, {sp0, true}});
    wall(kPi, kTwoPi, {{eN, false}, {sp0, false}, {rcP, true}, {spP, true}});
    s.faces.push_back(MakePlaneFace(fc, tHat, {{rc0, false}, {rcP, false}}));
    dimple({{eN, true}, {eP, true}});
  } else {
    wall(0.0, kPi, {{rc0, false}, {spP, true}, {eP, true}, {sp0, false}});
    wall(kPi, kTwoPi, {{rcP, false}, {sp0, true}, {eN, true}, {spP, false}});
    s.faces.push_back(MakePlaneFace(fc, ray3d::Scale(tHat, -1.0), {{rc0, true}, {rcP, true}}));
    dimple({{eP, false}, {eN, false}});
  }

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `B − A` for a **non-perpendicular** branch pipe (REQ-314 B2b-2, GitHub issue #242): the thin
/// branch `A` (radius \p r, tilted by \p alpha) bored clean through the thick main `B` (radius \p R,
/// axis `fr.zAxis`, caps \p zB0 / \p zB1). `BuildBranchPipeSubtract` generalised the same way
/// \ref BuildAngledBranchPipeIntersection generalises the perpendicular lens — only `A`'s surface
/// frame and the procedural curve `ζ(φ)` change; `B`'s wall halves, caps and the two inward bore
/// halves keep their topology. 8 vertices, 12 edges, 6 faces; genus 1.
[[nodiscard]] bool BuildAngledBranchPipeSubtract(const ucs::Ucs& fr, double r, double R, double alpha,
                                                 double zB0, double zB1, Solid* out, Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  const double zSpan = (R + r * std::fabs(sa)) / ca * std::fabs(sa) + r;  // mouth half-height on B's axis
  if (!(R > r) || !(r > 0.0) || !(std::fabs(alpha) < kHalfPi - 1e-6) || !(zB1 - zB0 > 2.0 * zSpan))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 8.0 * R;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = Z;
  bSurf.frame.xAxis = fr.xAxis;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double root = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi)));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, -r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{0.0, R, zB0}));
  const int bm = AddVertex(&s, W(Vec3{0.0, -R, zB0}));
  const int tp = AddVertex(&s, W(Vec3{0.0, R, zB1}));
  const int tm = AddVertex(&s, W(Vec3{0.0, -R, zB1}));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int sA0 = AddLine(&s, n0, p0);
  const int sAp = AddLine(&s, n1, p1);
  const int seamP = AddLine(&s, bp, tp);
  const int seamM = AddLine(&s, bm, tm);
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brF = AddArc(&s, bm, bp, botC, Z, kPi);
  const int brB = AddArc(&s, bp, bm, botC, Z, kPi);
  const int trF = AddArc(&s, tm, tp, topC, Z, kPi);
  const int trB = AddArc(&s, tp, tm, topC, Z, kPi);

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(bSurf, -kHalfPi, kHalfPi,
       {Loop{{{brF, false}, {seamP, false}, {trF, true}, {seamM, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(bSurf, kHalfPi, kHalfPi + kPi,
       {Loop{{{brB, false}, {seamM, false}, {trB, true}, {seamP, true}}},
        Loop{{{cnU, true}, {cnL, false}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brF, true}, {brB, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trF, false}, {trB, false}}));
  Surface aIn = aSurf;
  aIn.inward = true;
  face(aIn, 0.0, kPi, {Loop{{{cpL, false}, {sAp, true}, {cnL, true}, {sA0, false}}}});
  face(aIn, kPi, kTwoPi, {Loop{{{cpU, true}, {sA0, true}, {cnU, false}, {sAp, false}}}});

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∪ B` for a **non-perpendicular** branch pipe (REQ-314 B2b-2, GitHub issue #242): the thin
/// branch `A` (radius \p r, tilted by \p alpha, axis-parameter caps \p zetaA0 / \p zetaA1 measured
/// from `fr.origin` along `A`'s axis) fused onto the thick main `B`. Generalised from
/// \ref BuildBranchPipeUnion exactly as the other two — only `A`'s frame, the curve `ζ(φ)`, and the
/// stub caps (now perpendicular to `A`'s tilted axis) change. 12 vertices, 18 edges, 10 faces.
[[nodiscard]] bool BuildAngledBranchPipeUnion(const ucs::Ucs& fr, double r, double R, double alpha,
                                              double zB0, double zB1, double zetaA0, double zetaA1,
                                              Solid* out, Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  const double zetaMax = (R + r * std::fabs(sa)) / ca;
  const double zSpan = zetaMax * std::fabs(sa) + r;
  if (!(R > r) || !(r > 0.0) || !(std::fabs(alpha) < kHalfPi - 1e-6) || !(zB1 - zB0 > 2.0 * zSpan) ||
      !(zetaA0 < -zetaMax) || !(zetaA1 > zetaMax))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));
  const Vec3 tHatW = tHat;  // fr axes are already world vectors
  // A point on A's wall at angle phi, axis-parameter zeta.
  auto thinPt = [&](double phi, double zeta) {
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, -r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = zetaA1 - zetaA0;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = Z;
  bSurf.frame.xAxis = fr.xAxis;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double root = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi)));
    const double zeta = (r * sa * std::cos(phi) + sign * root) / ca;
    return thinPt(phi, zeta);
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{0.0, R, zB0}));
  const int bm = AddVertex(&s, W(Vec3{0.0, -R, zB0}));
  const int tp = AddVertex(&s, W(Vec3{0.0, R, zB1}));
  const int tm = AddVertex(&s, W(Vec3{0.0, -R, zB1}));
  const int k0 = AddVertex(&s, thinPt(0.0, zetaA1));
  const int k1 = AddVertex(&s, thinPt(kPi, zetaA1));
  const int m0 = AddVertex(&s, thinPt(0.0, zetaA0));
  const int m1 = AddVertex(&s, thinPt(kPi, zetaA0));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int seamP = AddLine(&s, bp, tp);
  const int seamM = AddLine(&s, bm, tm);
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brF = AddArc(&s, bm, bp, botC, Z, kPi);
  const int brB = AddArc(&s, bp, bm, botC, Z, kPi);
  const int trF = AddArc(&s, tm, tp, topC, Z, kPi);
  const int trB = AddArc(&s, tp, tm, topC, Z, kPi);
  const int ks0 = AddLine(&s, p0, k0);
  const int ksP = AddLine(&s, p1, k1);
  const int ms0 = AddLine(&s, n0, m0);
  const int msP = AddLine(&s, n1, m1);
  const Vec3 kCc = W(Vec3{zetaA1 * ca, 0.0, zetaA1 * sa});  // stub cap centres, on A's axis
  const Vec3 mCc = W(Vec3{zetaA0 * ca, 0.0, zetaA0 * sa});
  const Vec3 negT = ray3d::Scale(tHatW, -1.0);
  const int krF = AddArc(&s, k0, k1, kCc, tHatW, kPi);
  const int krB = AddArc(&s, k1, k0, kCc, tHatW, kPi);
  const int mrF = AddArc(&s, m0, m1, mCc, negT, kPi);
  const int mrB = AddArc(&s, m1, m0, mCc, negT, kPi);

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(bSurf, -kHalfPi, kHalfPi,
       {Loop{{{brF, false}, {seamP, false}, {trF, true}, {seamM, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(bSurf, kHalfPi, kHalfPi + kPi,
       {Loop{{{brB, false}, {seamM, false}, {trB, true}, {seamP, true}}},
        Loop{{{cnU, false}, {cnL, true}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brF, true}, {brB, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trF, false}, {trB, false}}));
  face(aSurf, 0.0, kPi, {Loop{{{cpL, false}, {ksP, false}, {krF, true}, {ks0, true}}}});
  face(aSurf, kPi, kTwoPi, {Loop{{{cpU, true}, {ks0, false}, {krB, true}, {ksP, true}}}});
  face(aSurf, 0.0, kPi, {Loop{{{cnL, false}, {msP, false}, {mrF, true}, {ms0, true}}}});
  face(aSurf, kPi, kTwoPi, {Loop{{{cnU, true}, {ms0, false}, {mrB, true}, {msP, true}}}});
  s.faces.push_back(MakePlaneFace(kCc, tHatW, {{krF, false}, {krB, false}}));
  s.faces.push_back(MakePlaneFace(mCc, negT, {{mrF, false}, {mrB, false}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// One stub of `A − B` (thin − thick) for a branch pipe (REQ-314 B2b-2, GitHub issue #242): the
/// thick main `B` (radius \p R, axis `fr.zAxis`) bites the thin branch `A` (radius \p r, tilted by
/// \p alpha, `fr.xAxis` = its in-plane component) clean in two. This is the \p sideSign stub — the
/// piece of `A` from its flat cap at axis-parameter \p zetaFlat to the quartic mouth `B` cut: `A`'s
/// wall in two u halves, the flat cap, and an **inward** patch of `B`'s wall as the concave end.
/// 4 vertices, 6 edges (2 procedural), 4 faces, χ = 2. Built in \p fr and left there. `alpha = 0` is
/// the perpendicular tee. Every curved face integrates numerically.
[[nodiscard]] bool BuildBranchPipeThinStub(const ucs::Ucs& fr, double r, double R, double alpha,
                                           double zetaFlat, int sideSign, Solid* out,
                                           Problem* outWhy) {
  const double ca = std::cos(alpha);
  const double sa = std::sin(alpha);
  const double zetaMax = (R + r * std::fabs(sa)) / ca;
  if (!(R > r) || !(r > 0.0) || !(std::fabs(alpha) < kHalfPi - 1e-6) ||
      !(std::fabs(zetaFlat) > zetaMax + 1e-9 * (R + r)))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const int msign = sideSign > 0 ? 1 : -1;
  const Vec3 Y = fr.yAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };
  const Vec3 tHat = ray3d::Add(ray3d::Scale(fr.xAxis, ca), ray3d::Scale(fr.zAxis, sa));
  const Vec3 xaHat = ray3d::Add(ray3d::Scale(fr.xAxis, -sa), ray3d::Scale(fr.zAxis, ca));
  auto thinPt = [&](double phi, double zeta) {
    return W(Vec3{zeta * ca - r * std::cos(phi) * sa, -r * std::sin(phi),
                  zeta * sa + r * std::cos(phi) * ca});
  };
  auto cpt = [&](double phi) {
    const double root = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi)));
    return thinPt(phi, (r * sa * std::cos(phi) + msign * root) / ca);
  };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = tHat;
  aSurf.frame.xAxis = xaHat;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = 4.0 * (std::fabs(zetaFlat) + R);
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame = fr;
  bSurf.radius = R;
  bSurf.height = 8.0 * R;

  Solid s;
  const int p0 = AddVertex(&s, cpt(0.0));               // mouth φ = 0
  const int pP = AddVertex(&s, cpt(kPi));               // mouth φ = π
  const int q0 = AddVertex(&s, thinPt(0.0, zetaFlat));  // rim φ = 0
  const int qP = AddVertex(&s, thinPt(kPi, zetaFlat));  // rim φ = π

  auto isect = [&](int a, int b, double witnessPhi) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = a;
    e.v1 = b;
    e.frame.origin = cpt(witnessPhi);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int eP = isect(p0, pP, 0.5 * kPi);  // mouth, y > 0
  const int eN = isect(pP, p0, 1.5 * kPi);  // mouth, y < 0
  const int sp0 = AddLine(&s, p0, q0);      // wall seam φ = 0
  const int spP = AddLine(&s, pP, qP);      // wall seam φ = π
  const Vec3 fc = W(Vec3{zetaFlat * ca, 0.0, zetaFlat * sa});
  const int rc0 = AddArc(&s, q0, qP, fc, tHat, kPi);  // rim, y > 0
  const int rcP = AddArc(&s, qP, q0, fc, tHat, kPi);  // rim, y < 0

  auto wall = [&](double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface = aSurf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };
  const double psi0 = std::asin(std::clamp(r / R, -1.0, 1.0));
  auto dimple = [&](std::vector<EdgeUse> uses) {
    Face f;
    f.surface = bSurf;
    f.surface.inward = true;
    f.uStart = msign > 0 ? -psi0 : kPi - psi0;
    f.uEnd = msign > 0 ? psi0 : kPi + psi0;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };

  if (msign > 0) {
    // Loop below the rim: mouth is the bottom edge of each wall half, rim the top.
    wall(0.0, kPi, {{eP, false}, {spP, false}, {rc0, true}, {sp0, true}});
    wall(kPi, kTwoPi, {{eN, false}, {sp0, false}, {rcP, true}, {spP, true}});
    s.faces.push_back(MakePlaneFace(fc, tHat, {{rc0, false}, {rcP, false}}));
    dimple({{eN, true}, {eP, true}});
  } else {
    wall(0.0, kPi, {{rc0, false}, {spP, true}, {eP, true}, {sp0, false}});
    wall(kPi, kTwoPi, {{rcP, false}, {sp0, true}, {eN, true}, {spP, false}});
    s.faces.push_back(MakePlaneFace(fc, ray3d::Scale(tHat, -1.0), {{rc0, true}, {rcP, true}}));
    dimple({{eP, false}, {eN, false}});
  }

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// `A ∪ B`: the solid pipe-tee — the thick main `B` (radius \p R, axis `fr.zAxis`, caps \p zB0 /
/// \p zB1) fused with the thin branch `A` (radius \p r, axis `fr.xAxis`, caps \p xA0 / \p xA1)
/// (REQ-314 B2b-2). 12 vertices, 18 edges, 10 faces: `B`'s wall outside `A` (two halves, each with
/// the branch mouth as an inner loop) + `B`'s two caps + `A`'s wall outside `B` (four stub halves)
/// + `A`'s two caps. Volume `vol(A) + vol(B) − lens`.
[[nodiscard]] bool BuildBranchPipeUnion(const ucs::Ucs& fr, double r, double R, double zB0, double zB1,
                                        double xA0, double xA1, Solid* out, Problem* outWhy) {
  if (!(R > r) || !(r > 0.0) || !(zB1 - zB0 > 2.0 * r) || !(xA0 < -R) || !(xA1 > R))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  auto W = [&](const Vec3& l) { return ucs::UcsToWorld(fr, l); };

  Surface aSurf;
  aSurf.kind = SurfaceKind::Cylinder;
  aSurf.frame.origin = fr.origin;
  aSurf.frame.zAxis = X;
  aSurf.frame.xAxis = Z;
  aSurf.frame.yAxis = ray3d::Scale(Y, -1.0);
  aSurf.radius = r;
  aSurf.height = xA1 - xA0;
  Surface bSurf;
  bSurf.kind = SurfaceKind::Cylinder;
  bSurf.frame.origin = fr.origin;
  bSurf.frame.zAxis = Z;
  bSurf.frame.xAxis = X;
  bSurf.frame.yAxis = Y;
  bSurf.radius = R;
  bSurf.height = zB1 - zB0;

  Solid s;
  auto cpt = [&](double phi, int sign) {
    const double x = std::sqrt(std::max(0.0, R * R - r * r * std::sin(phi) * std::sin(phi))) * sign;
    return W(Vec3{x, -r * std::sin(phi), r * std::cos(phi)});
  };
  const int p0 = AddVertex(&s, cpt(0.0, 1));
  const int p1 = AddVertex(&s, cpt(kPi, 1));
  const int n0 = AddVertex(&s, cpt(0.0, -1));
  const int n1 = AddVertex(&s, cpt(kPi, -1));
  const int bp = AddVertex(&s, W(Vec3{0.0, R, zB0}));
  const int bm = AddVertex(&s, W(Vec3{0.0, -R, zB0}));
  const int tp = AddVertex(&s, W(Vec3{0.0, R, zB1}));
  const int tm = AddVertex(&s, W(Vec3{0.0, -R, zB1}));
  const int k0 = AddVertex(&s, W(Vec3{xA1, 0.0, r}));
  const int k1 = AddVertex(&s, W(Vec3{xA1, 0.0, -r}));
  const int m0 = AddVertex(&s, W(Vec3{xA0, 0.0, r}));
  const int m1 = AddVertex(&s, W(Vec3{xA0, 0.0, -r}));

  auto isect = [&](int v0, int v1, double witnessPhi, int sign) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    e.frame.origin = cpt(witnessPhi, sign);
    e.isectSurfaces = {aSurf, bSurf};
    s.edges.push_back(e);
    return static_cast<int>(s.edges.size()) - 1;
  };
  const int cpU = isect(p0, p1, 1.75 * kPi, 1);
  const int cpL = isect(p0, p1, 0.25 * kPi, 1);
  const int cnU = isect(n0, n1, 1.75 * kPi, -1);
  const int cnL = isect(n0, n1, 0.25 * kPi, -1);
  const int seamP = AddLine(&s, bp, tp);
  const int seamM = AddLine(&s, bm, tm);
  const Vec3 botC = W(Vec3{0.0, 0.0, zB0});
  const Vec3 topC = W(Vec3{0.0, 0.0, zB1});
  const int brF = AddArc(&s, bm, bp, botC, Z, kPi);
  const int brB = AddArc(&s, bp, bm, botC, Z, kPi);
  const int trF = AddArc(&s, tm, tp, topC, Z, kPi);
  const int trB = AddArc(&s, tp, tm, topC, Z, kPi);
  const int ks0 = AddLine(&s, p0, k0);  // +X stub, A seam phi = 0
  const int ksP = AddLine(&s, p1, k1);  // +X stub, A seam phi = pi
  const int ms0 = AddLine(&s, n0, m0);  // -X stub, A seam phi = 0
  const int msP = AddLine(&s, n1, m1);
  const Vec3 kC = W(Vec3{xA1, 0.0, 0.0});
  const Vec3 mC = W(Vec3{xA0, 0.0, 0.0});
  const Vec3 negX = ray3d::Scale(X, -1.0);
  const int krF = AddArc(&s, k0, k1, kC, X, kPi);
  const int krB = AddArc(&s, k1, k0, kC, X, kPi);
  // The -X cap's rim arcs run about −X, so with the same loop pattern its face still winds outward.
  const int mrF = AddArc(&s, m0, m1, mC, negX, kPi);
  const int mrB = AddArc(&s, m1, m0, mC, negX, kPi);

  auto face = [&](const Surface& surf, double u0, double u1, std::vector<Loop> loops) {
    Face f;
    f.surface = surf;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops = std::move(loops);
    s.faces.push_back(std::move(f));
  };
  face(bSurf, -kHalfPi, kHalfPi,
       {Loop{{{brF, false}, {seamP, false}, {trF, true}, {seamM, true}}},
        Loop{{{cpU, false}, {cpL, true}}}});
  face(bSurf, kHalfPi, kHalfPi + kPi,
       {Loop{{{brB, false}, {seamM, false}, {trB, true}, {seamP, true}}},
        Loop{{{cnU, false}, {cnL, true}}}});
  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{brF, true}, {brB, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{trF, false}, {trB, false}}));
  face(aSurf, 0.0, kPi,
       {Loop{{{cpL, false}, {ksP, false}, {krF, true}, {ks0, true}}}});
  face(aSurf, kPi, kTwoPi,
       {Loop{{{cpU, true}, {ks0, false}, {krB, true}, {ksP, true}}}});
  face(aSurf, 0.0, kPi,
       {Loop{{{cnL, false}, {msP, false}, {mrF, true}, {ms0, true}}}});
  face(aSurf, kPi, kTwoPi,
       {Loop{{{cnU, true}, {ms0, false}, {mrB, true}, {msP, true}}}});
  s.faces.push_back(MakePlaneFace(kC, X, {{krF, false}, {krB, false}}));
  s.faces.push_back(MakePlaneFace(mC, negX, {{mrF, false}, {mrB, false}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// Recognise a thin cylinder crossing a thicker one through an interior point, radii differ — the
/// pipe-tee (B2b-2). INTERSECT the lens, SUBTRACT bores the branch clean through the main (or, for a
/// thin minuend, splits the branch into two stubs), UNION fuses them — at **right angles**, at any
/// **non-perpendicular** coplanar angle (issue #242), and, offset **and** tilted at once, fully
/// general (issue #242 / #283). `*handled` stays false when the pair is not this configuration.
[[nodiscard]] bool TryBooleanBranchPipe(const CylinderShape& A, const CylinderShape& B, BoolOp op,
                                        std::vector<Solid>* out, bool* handled, Problem* outWhy) {
  const double sc = A.radius + B.radius + A.length + B.length;
  const double eps = 1e-7 * sc;
  if (std::fabs(A.radius - B.radius) <= eps)
    return false;  // equal radius — plane ellipses, not this recogniser
  const Vec3 az = A.axis.zAxis;
  const Vec3 bz = B.axis.zAxis;
  const double axDot = ray3d::Dot(az, bz);
  if (std::fabs(std::fabs(axDot) - 1.0) < 1e-7)
    return false;  // parallel / coaxial — a different recogniser
  const Vec3 wBA = ray3d::Sub(B.axis.origin, A.axis.origin);
  const Vec3 axCross = ray3d::Cross(az, bz);
  const double axCross2 = ray3d::Dot(axCross, axCross);
  if (axCross2 < 1e-18)
    return false;
  const double gap = std::fabs(ray3d::Dot(wBA, axCross)) / std::sqrt(axCross2);

  // thin / thick roles
  const CylinderShape& thin = A.radius < B.radius ? A : B;
  const CylinderShape& thick = A.radius < B.radius ? B : A;
  const double r = thin.radius;
  const double R = thick.radius;
  const bool perp = std::fabs(axDot) <= 1e-7;

  if (gap > eps) {
    // Skew axes — the offset branch pipe (issue #242). The closest-approach points of the two axis
    // lines, needed by both the perpendicular-offset and the fully-general cases below.
    const Vec3 td = thin.axis.zAxis;
    const Vec3 kd = thick.axis.zAxis;
    const Vec3 rv = ray3d::Sub(thin.axis.origin, thick.axis.origin);
    const double den = 1.0 - axDot * axDot;
    const double tPar = (axDot * ray3d::Dot(kd, rv) - ray3d::Dot(td, rv)) / den;
    const double kPar = (ray3d::Dot(kd, rv) - axDot * ray3d::Dot(td, rv)) / den;
    const Vec3 cThin = ray3d::Add(thin.axis.origin, ray3d::Scale(td, tPar));
    const Vec3 cThick = ray3d::Add(thick.axis.origin, ray3d::Scale(kd, kPar));
    const bool minuendThick = A.radius > B.radius;

    if (perp) {
      // Right angles, missing by `gap`, the thin fully crossing, caps clear.
      if (!(gap + r < R - eps))
        return false;
      ucs::Ucs sfr;
      if (!ucs::FromNormal(cThick, kd, &sfr))
        return Fail(Problem::BooleanResultInvalid, outWhy);
      const Vec3 gx = ray3d::Sub(cThin, cThick);  // thick axis -> thin axis
      if (!(ray3d::Length(gx) > 1e-9 * sc))
        return false;
      sfr.xAxis = ray3d::Normalize(gx);
      sfr.yAxis = ray3d::Normalize(ray3d::Cross(sfr.zAxis, sfr.xAxis));
      if (std::fabs(ray3d::Dot(td, sfr.yAxis)) < 1.0 - 1e-6)
        return false;  // the thin axis is also tilted — the general case below
      const double sMax = std::sqrt(std::max(0.0, R * R - (gap - r) * (gap - r)));
      const double t0 = ray3d::Dot(ray3d::Sub(thin.axis.origin, cThick), sfr.yAxis);
      const double t1 = t0 + thin.length * ray3d::Dot(td, sfr.yAxis);
      if (std::min(t0, t1) > -sMax - eps || std::max(t0, t1) < sMax + eps)
        return false;  // the thin does not fully cross
      const double kAlong = ray3d::Dot(ray3d::Sub(cThick, thick.axis.origin), kd);
      if (kAlong - r < eps || thick.length - kAlong - r < eps)
        return false;  // a thick cap sits inside the lens
      *handled = true;
      const double zB0 = -kAlong;
      const double zB1 = thick.length - kAlong;
      // thin cap parameters along fr.yAxis, measured from cThick (= sfr.origin).
      const double yLo = std::min(t0, t1);
      const double yHi = std::max(t0, t1);
      if (op == BoolOp::Subtract && !minuendThick) {
        Solid up;
        Solid down;
        if (!BuildSkewBranchPipeThinStub(sfr, r, R, gap, yHi, 1, &up, outWhy) ||
            !BuildSkewBranchPipeThinStub(sfr, r, R, gap, yLo, -1, &down, outWhy))
          return false;
        out->push_back(std::move(up));
        out->push_back(std::move(down));
        return Succeed(outWhy);
      }
      Solid result;
      bool ok = false;
      if (op == BoolOp::Intersect)
        ok = BuildSkewBranchPipeIntersection(sfr, r, R, gap, &result, outWhy);
      else if (op == BoolOp::Subtract)
        ok = BuildSkewBranchPipeSubtract(sfr, r, R, gap, zB0, zB1, &result, outWhy);
      else
        ok = BuildSkewBranchPipeUnion(sfr, r, R, gap, zB0, zB1, yLo, yHi, &result, outWhy);
      if (!ok)
        return false;
      out->push_back(std::move(result));
      return Succeed(outWhy);
    }

    // Fully general: tilted AND skew (issue #242, thin-thick stub added for #283). `gfr.xAxis` is
    // the thin axis's own in-plane component (as in the coplanar-tilted case), `gfr.origin` sits on
    // the thick axis at the closest approach, and the offset `g` — the thin axis's constant
    // coordinate along `gfr.yAxis` — is the true common-perpendicular gap (may be negative; the
    // builders don't care).
    ucs::Ucs gfr;
    if (!ucs::FromNormal(cThick, kd, &gfr))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    const Vec3 gxperp = ray3d::Sub(td, ray3d::Scale(gfr.zAxis, ray3d::Dot(td, gfr.zAxis)));
    if (!(ray3d::Length(gxperp) > 1e-9 * sc))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    gfr.xAxis = ray3d::Normalize(gxperp);
    gfr.yAxis = ray3d::Normalize(ray3d::Cross(gfr.zAxis, gfr.xAxis));
    const double galpha =
        std::atan2(ray3d::Dot(td, gfr.zAxis), ray3d::Dot(td, gfr.xAxis));
    const double gca = std::cos(galpha);
    const double gsa = std::sin(galpha);
    if (!(gca > 1e-6))
      return false;  // (near-)perpendicular — the branch above handles that exactly
    const double gOff = ray3d::Dot(ray3d::Sub(cThin, cThick), gfr.yAxis);
    if (!(std::fabs(gOff) + r < R - eps))
      return false;  // the thin does not clear the thick's equator at every longitude
    // Conservative (not tight) bounds on the curve's extent, enough to gate "fully crosses" /
    // "caps clear": |s(phi)| <= (r|sin a| + R)/cos a, and the thick-axis component of the curve is
    // bounded the same way the coplanar-tilted case bounds zetaMax / zThickMax.
    const double sBound = (r * std::fabs(gsa) + R) / gca;
    const double zBound = sBound * std::fabs(gsa) + r * gca;
    const double sThinAt = ray3d::Dot(ray3d::Sub(cThin, thin.axis.origin), td);
    const double sThickAt = ray3d::Dot(ray3d::Sub(cThick, thick.axis.origin), kd);
    if (sThinAt - sBound < eps || thin.length - sThinAt - sBound < eps ||
        sThickAt - zBound < eps || thick.length - sThickAt - zBound < eps)
      return false;
    *handled = true;
    const double zB0 = -sThickAt;
    const double zB1 = thick.length - sThickAt;
    const double zetaA0 = -sThinAt;
    const double zetaA1 = thin.length - sThinAt;
    if (op == BoolOp::Subtract && !minuendThick) {
      Solid up;
      Solid down;
      if (!BuildGeneralBranchPipeThinStub(gfr, r, R, galpha, gOff, zetaA1, 1, &up, outWhy) ||
          !BuildGeneralBranchPipeThinStub(gfr, r, R, galpha, gOff, zetaA0, -1, &down, outWhy))
        return false;
      out->push_back(std::move(up));
      out->push_back(std::move(down));
      return Succeed(outWhy);
    }
    Solid result;
    bool gok = false;
    if (op == BoolOp::Intersect)
      gok = BuildGeneralBranchPipeIntersection(gfr, r, R, galpha, gOff, &result, outWhy);
    else if (op == BoolOp::Subtract)
      gok = BuildGeneralBranchPipeSubtract(gfr, r, R, galpha, gOff, zB0, zB1, &result, outWhy);
    else
      gok = BuildGeneralBranchPipeUnion(gfr, r, R, galpha, gOff, zB0, zB1, zetaA0, zetaA1, &result,
                                        outWhy);
    if (!gok)
      return false;
    out->push_back(std::move(result));
    return Succeed(outWhy);
  }

  const double pAlong = ray3d::Dot(wBA, az);
  const double qAlong = ray3d::Dot(wBA, bz);
  const Vec3 meet =
      ray3d::Add(A.axis.origin, ray3d::Scale(az, (pAlong - axDot * qAlong) / (1.0 - axDot * axDot)));

  ucs::Ucs fr;
  if (!ucs::FromNormal(meet, thick.axis.zAxis, &fr))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const Vec3 xperp = ray3d::Sub(
      thin.axis.zAxis, ray3d::Scale(fr.zAxis, ray3d::Dot(thin.axis.zAxis, fr.zAxis)));
  if (!(ray3d::Length(xperp) > 1e-9 * sc))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  fr.xAxis = ray3d::Normalize(xperp);
  fr.yAxis = ray3d::Normalize(ray3d::Cross(fr.zAxis, fr.xAxis));
  // `fr.xAxis` is `thin`'s own in-plane component, so `thin`'s axis is cos α·x̂ + sin α·ẑ with
  // α ∈ (−π/2, π/2); α ≈ 0 is the perpendicular tee.
  const double alpha =
      std::atan2(ray3d::Dot(thin.axis.zAxis, fr.zAxis), ray3d::Dot(thin.axis.zAxis, fr.xAxis));
  const double ca = std::cos(alpha);

  const double sThin = ray3d::Dot(ray3d::Sub(meet, thin.axis.origin), thin.axis.zAxis);
  const double sThick = ray3d::Dot(ray3d::Sub(meet, thick.axis.origin), thick.axis.zAxis);
  // The thin cylinder must fully cross the thick one and the lens must clear the thick one's caps —
  // widened for the tilt (at α = 0 these reduce to `sThin ≥ R`, `sThick ≥ r`).
  const double zetaMax = (R + r * std::fabs(std::sin(alpha))) / ca;
  const double zThickMax = zetaMax * std::fabs(std::sin(alpha)) + r;
  if (sThin - zetaMax < eps || thin.length - sThin - zetaMax < eps || sThick - zThickMax < eps ||
      thick.length - sThick - zThickMax < eps)
    return false;

  const bool minuendIsThick = A.radius > B.radius;

  if (op == BoolOp::Subtract && !minuendIsThick) {
    // thin − thick: the thick bites the thin in two — a pair of stubs, each a short branch with an
    // inward dimple where the main cut it (issue #242). Handles perpendicular and tilted alike.
    *handled = true;
    Solid up;
    Solid down;
    if (!BuildBranchPipeThinStub(fr, r, R, alpha, thin.length - sThin, 1, &up, outWhy) ||
        !BuildBranchPipeThinStub(fr, r, R, alpha, -sThin, -1, &down, outWhy))
      return false;
    out->push_back(std::move(up));
    out->push_back(std::move(down));
    return Succeed(outWhy);
  }

  if (!perp) {
    *handled = true;
    const double zB0n = -sThick;
    const double zB1n = thick.length - sThick;
    Solid tilted;
    bool ok = false;
    if (op == BoolOp::Intersect)
      ok = BuildAngledBranchPipeIntersection(fr, r, R, alpha, &tilted, outWhy);
    else if (op == BoolOp::Subtract)
      ok = BuildAngledBranchPipeSubtract(fr, r, R, alpha, zB0n, zB1n, &tilted, outWhy);
    else
      ok = BuildAngledBranchPipeUnion(fr, r, R, alpha, zB0n, zB1n, -sThin, thin.length - sThin,
                                      &tilted, outWhy);
    if (!ok)
      return false;
    out->push_back(std::move(tilted));
    return Succeed(outWhy);
  }

  *handled = true;

  const double zB0 = -sThick;
  const double zB1 = thick.length - sThick;
  const double sThinFr = ray3d::Dot(ray3d::Sub(meet, thin.axis.origin), fr.xAxis);
  const double xA0 = -sThinFr;
  const double xA1 = thin.length - sThinFr;
  Solid result;
  if (op == BoolOp::Intersect) {
    if (!BuildBranchPipeIntersection(fr, r, R, &result, outWhy))
      return false;
  } else if (op == BoolOp::Subtract) {
    if (!BuildBranchPipeSubtract(fr, r, R, zB0, zB1, &result, outWhy))
      return false;
  } else {
    if (!BuildBranchPipeUnion(fr, r, R, zB0, zB1, xA0, xA1, &result, outWhy))
      return false;
  }
  out->push_back(std::move(result));
  return Succeed(outWhy);
}

/// Recognise two equal-radius cylinders whose axes cross at right angles, both piercing clear of the
/// other's caps. INTERSECT is a Steinmetz bicylinder, `A − B` is `A` with a clean perpendicular
/// channel, and `A ∪ B` is a T-pipe — all closed form. `*handled` stays false when the pair is not
/// this configuration, so the caller falls through to the coaxial recogniser.
[[nodiscard]] bool TryBooleanSteinmetz(const CylinderShape& A, const CylinderShape& B, BoolOp op,
                                       std::vector<Solid>* out, bool* handled, Problem* outWhy) {
  const double sc = A.radius + A.length + B.length;
  const double eps = 1e-7 * sc;
  if (std::fabs(A.radius - B.radius) > eps)
    return false;
  const Vec3 az = A.axis.zAxis;
  const Vec3 bz = B.axis.zAxis;
  if (std::fabs(ray3d::Dot(az, bz)) > 1e-7)
    return false;  // axes not perpendicular
  // Closest points of the two axis lines; with az ⟂ bz the normal equations decouple.
  const Vec3 w0 = ray3d::Sub(A.axis.origin, B.axis.origin);
  const Vec3 pA = ray3d::Add(A.axis.origin, ray3d::Scale(az, -ray3d::Dot(w0, az)));
  const Vec3 pB = ray3d::Add(B.axis.origin, ray3d::Scale(bz, ray3d::Dot(w0, bz)));
  if (ray3d::Length(ray3d::Sub(pA, pB)) > eps)
    return false;  // axes are skew, not intersecting
  const Vec3 meet = ray3d::Scale(ray3d::Add(pA, pB), 0.5);
  const double r = A.radius;
  const double sa = ray3d::Dot(ray3d::Sub(meet, A.axis.origin), az);
  const double sb = ray3d::Dot(ray3d::Sub(meet, B.axis.origin), bz);
  if (sa < r - eps || sa > A.length - r + eps || sb < r - eps || sb > B.length - r + eps)
    return false;  // an intersection ellipse would run off a cap — not the clean bicylinder

  *handled = true;

  ucs::Ucs fr;
  if (!ucs::FromNormal(meet, az, &fr))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  Vec3 xa = ray3d::Sub(bz, ray3d::Scale(az, ray3d::Dot(bz, az)));
  if (!(ray3d::Length(xa) > 1e-9))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  fr.xAxis = ray3d::Normalize(xa);
  fr.yAxis = ray3d::Normalize(ray3d::Cross(fr.zAxis, fr.xAxis));
  const double zA0 = -sa;
  const double zA1 = A.length - sa;
  const double xB0 = -sb;
  const double xB1 = B.length - sb;
  Solid r2;
  bool ok = false;
  if (op == BoolOp::Intersect)
    ok = BuildSteinmetzIntersection(fr, r, &r2, outWhy);
  else if (op == BoolOp::Subtract)
    ok = BuildSteinmetzSubtract(fr, r, zA0, zA1, &r2, outWhy);
  else
    ok = BuildSteinmetzUnion(fr, r, zA0, zA1, xB0, xB1, &r2, outWhy);
  if (!ok)
    return false;
  out->push_back(std::move(r2));
  return Succeed(outWhy);
}

[[nodiscard]] bool TryBooleanCoaxialCylinders(const Solid& a, const Solid& b, const CylinderShape& A,
                                              const CylinderShape& B, BoolOp op, std::vector<Solid>* out,
                                              bool* handled, Problem* outWhy) {
  const Vec3 az = A.axis.zAxis;
  const Vec3 d = ray3d::Sub(B.axis.origin, A.axis.origin);
  const double sc = A.radius + A.length + B.length;
  const double perp = ray3d::Length(ray3d::Sub(d, ray3d::Scale(az, ray3d::Dot(d, az))));
  if (std::fabs(std::fabs(ray3d::Dot(az, B.axis.zAxis)) - 1.0) > 1e-7 || perp > 1e-6 * sc) {
    *handled = true;  // two cylinders we cannot combine analytically in B1
    return Fail(Problem::BooleanCurvedFace, outWhy);
  }
  *handled = true;
  const double eps = 1e-7 * sc;
  const double a0 = 0.0;
  const double a1 = A.length;
  double b0 = ray3d::Dot(d, az);
  double b1 = b0 + (ray3d::Dot(B.axis.zAxis, az) > 0.0 ? B.length : -B.length);
  if (b0 > b1)
    std::swap(b0, b1);

  if (op == BoolOp::Subtract) {
    // A − B, coaxial. Overlap along the axis:
    const double ov0 = std::max(a0, b0);
    const double ov1 = std::min(a1, b1);
    if (ov1 - ov0 <= eps) {  // no shared length — A is untouched
      out->push_back(a);
      return Succeed(outWhy);
    }
    auto oneCyl = [&](double zlo, double zhi, double rr, std::vector<Solid>* dst) {
      ucs::Ucs fr = A.axis;
      fr.origin = ray3d::Add(A.axis.origin, ray3d::Scale(az, zlo));
      Solid r;
      Problem w = Problem::Ok;
      if (!MakeCylinder(fr, rr, zhi - zlo, &r, &w))
        return false;
      dst->push_back(std::move(r));
      return true;
    };
    if (B.radius >= A.radius - eps) {
      // B is at least as wide as A: it removes a slab. A shrinks, splits, or vanishes.
      const bool keepLow = ov0 - a0 > eps;
      const bool keepHigh = a1 - ov1 > eps;
      if (!keepLow && !keepHigh)
        return Fail(Problem::BooleanEmptyResult, outWhy);
      if (keepLow && !oneCyl(a0, ov0, A.radius, out))
        return Fail(Problem::BooleanResultInvalid, outWhy);
      if (keepHigh && !oneCyl(ov1, a1, A.radius, out))
        return Fail(Problem::BooleanResultInvalid, outWhy);
      return Succeed(outWhy);
    }
    // B is narrower: a bore. It must open at an end of A, else it leaves a sealed cavity.
    const bool openLow = b0 <= a0 + eps;
    const bool openHigh = b1 >= a1 - eps;
    if (!openLow && !openHigh)
      return Fail(Problem::BooleanCurvedFace, outWhy);  // a floating internal cavity — its own slice
    std::vector<double> zs;
    std::vector<double> rOut;
    std::vector<double> rIn;
    zs.push_back(a0);
    if (ov0 - a0 > eps) {  // solid band below the bore
      rOut.push_back(A.radius);
      rIn.push_back(0.0);
      zs.push_back(ov0);
    }
    rOut.push_back(A.radius);  // the bored band
    rIn.push_back(B.radius);
    zs.push_back(ov1);
    if (a1 - ov1 > eps) {  // solid band above the bore
      rOut.push_back(A.radius);
      rIn.push_back(0.0);
      zs.push_back(a1);
    }
    ucs::Ucs fr = A.axis;
    fr.origin = ray3d::Add(A.axis.origin, ray3d::Scale(az, zs.front()));
    Solid r;
    if (!BuildCoaxialStack(fr, zs, rOut, rIn, &r, outWhy))
      return false;
    out->push_back(std::move(r));
    return Succeed(outWhy);
  }

  if (op == BoolOp::Intersect) {
    const double lo = std::max(a0, b0);
    const double hi = std::min(a1, b1);
    if (hi - lo <= eps)
      return Fail(Problem::BooleanEmptyResult, outWhy);
    ucs::Ucs fr = A.axis;
    fr.origin = ray3d::Add(A.axis.origin, ray3d::Scale(az, lo));
    Solid r;
    Problem w = Problem::Ok;
    if (!MakeCylinder(fr, std::min(A.radius, B.radius), hi - lo, &r, &w))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    out->push_back(std::move(r));
    return Succeed(outWhy);
  }

  // UNION.
  if (std::max(a0, b0) > std::min(a1, b1) + eps) {
    out->push_back(a);
    out->push_back(b);
    return Succeed(outWhy);
  }
  const double lo = std::min(a0, b0);
  const double hi = std::max(a1, b1);
  auto oneCylinder = [&](double zlo, double zhi, double rr) {
    ucs::Ucs fr = A.axis;
    fr.origin = ray3d::Add(A.axis.origin, ray3d::Scale(az, zlo));
    Solid r;
    Problem w = Problem::Ok;
    if (!MakeCylinder(fr, rr, zhi - zlo, &r, &w))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    out->push_back(std::move(r));
    return Succeed(outWhy);
  };
  if (std::fabs(A.radius - B.radius) <= 1e-7 * sc)
    return oneCylinder(lo, hi, A.radius);

  std::vector<double> brk{a0, a1, b0, b1};
  std::sort(brk.begin(), brk.end());
  std::vector<double> uniq;
  for (double v : brk)
    if (uniq.empty() || v - uniq.back() > eps)
      uniq.push_back(v);
  std::vector<double> zs{uniq.front()};
  std::vector<double> rs;
  for (std::size_t i = 0; i + 1 < uniq.size(); ++i) {
    const double m = 0.5 * (uniq[i] + uniq[i + 1]);
    double rr = 0.0;
    if (m > a0 - eps && m < a1 + eps)
      rr = std::max(rr, A.radius);
    if (m > b0 - eps && m < b1 + eps)
      rr = std::max(rr, B.radius);
    if (!(rr > 0.0))
      continue;
    if (!rs.empty() && std::fabs(rs.back() - rr) <= 1e-7 * sc)
      zs.back() = uniq[i + 1];
    else {
      rs.push_back(rr);
      zs.push_back(uniq[i + 1]);
    }
  }
  if (rs.size() == 1)
    return oneCylinder(zs.front(), zs.back(), rs.front());
  ucs::Ucs fr = A.axis;
  fr.origin = ray3d::Add(A.axis.origin, ray3d::Scale(az, zs.front()));
  Solid r;
  if (!BuildCoaxialStack(fr, zs, rs, /*rIn=*/{}, &r, outWhy))
    return false;
  out->push_back(std::move(r));
  return Succeed(outWhy);
}

/// A right circular cylinder (base at \p axisFrame.origin, height \p h, radius \p r) with one planar
/// flat milled the full length parallel to the axis: the cut plane sits at local x = \p px
/// (`-r < px < r`), the material with x > px removed, the flat's outward normal along local +x. The
/// result is 4v / 6e / 4f, χ = 2 — two D-shaped end caps, one rectangular flat, one partial
/// cylindrical wall (u ∈ [φ, 2π−φ], φ = acos(px/r)). Used by `cylinder − box` (GitHub issue #242).
[[nodiscard]] bool BuildCylinderLongitudinalFlat(const ucs::Ucs& axisFrame, double r, double h,
                                                 double px, Solid* out, Problem* outWhy) {
  const double q = std::sqrt(std::max(0.0, r * r - px * px));
  const double phi = std::acos(std::clamp(px / r, -1.0, 1.0));
  const double sweep = kTwoPi - 2.0 * phi;
  if (q <= 1e-12 || sweep <= 1e-9 || sweep >= kTwoPi - 1e-9)
    return Fail(Problem::BooleanResultInvalid, outWhy);
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(axisFrame, Vec3{x, y, z}); };
  Solid s;
  const int v0 = AddVertex(&s, W(px, -q, 0.0));
  const int v1 = AddVertex(&s, W(px, q, 0.0));
  const int v2 = AddVertex(&s, W(px, q, h));
  const int v3 = AddVertex(&s, W(px, -q, h));
  const Vec3 Z = axisFrame.zAxis;
  const Vec3 botC = W(0.0, 0.0, 0.0);
  const Vec3 topC = W(0.0, 0.0, h);
  const int eb = AddArc(&s, v1, v0, botC, Z, sweep);  // bottom major arc, u: φ → 2π−φ
  const int et = AddArc(&s, v2, v3, topC, Z, sweep);  // top major arc, same sense
  const int chordBot = AddLine(&s, v0, v1);
  const int chordTop = AddLine(&s, v2, v3);
  const int seamPos = AddLine(&s, v1, v2);  // u = φ seam, base → top
  const int seamNeg = AddLine(&s, v0, v3);  // u = 2π−φ seam, base → top

  s.faces.push_back(MakePlaneFace(botC, ray3d::Scale(Z, -1.0), {{chordBot, true}, {eb, true}}));
  s.faces.push_back(MakePlaneFace(topC, Z, {{chordTop, true}, {et, false}}));
  s.faces.push_back(MakePlaneFace(
      W(px, 0.0, 0.5 * h), axisFrame.xAxis,
      {{chordBot, false}, {seamPos, false}, {chordTop, false}, {seamNeg, true}}));
  {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = axisFrame;
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = h;
    f.uStart = phi;
    f.uEnd = kTwoPi - phi;
    Loop lp;
    lp.uses = {{seamNeg, false}, {et, true}, {seamPos, true}, {eb, false}};
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  }
  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

/// A right circular cylinder (base at \p axisFrame.origin, height \p h, radius \p r) with a
/// rectangular **pocket** milled into one side that does not reach either end: the flat at local
/// x = \p px (`-r < px < r`) removes material x > px only for z ∈ [\p za, \p zb]
/// (`0 < za < zb < h`). The pocket floor and ceiling are the circular segment `x > px` at each
/// level; the caps and the wall above/below the pocket are untouched. 8 vertices, 16 edges,
/// 10 faces, χ = 2. Arcs and lines only — closed form, no numeric integration (GitHub issue #242).
[[nodiscard]] bool BuildCylinderPocket(const ucs::Ucs& axisFrame, double r, double h, double px,
                                       double za, double zb, Solid* out, Problem* outWhy) {
  const double q = std::sqrt(std::max(0.0, r * r - px * px));
  const double phi = std::acos(std::clamp(px / r, -1.0, 1.0));
  const double minorSweep = 2.0 * phi;
  const double majorSweep = kTwoPi - 2.0 * phi;
  const double eps = 1e-9 * std::max(h, r);
  if (q <= 1e-12 || minorSweep <= 1e-9 || majorSweep <= 1e-9 || !(za > eps) || !(zb > za + eps) ||
      !(h - zb > eps))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(axisFrame, Vec3{x, y, z}); };
  const Vec3 Z = axisFrame.zAxis;

  Solid s;
  const int v0Lo = AddVertex(&s, W(px, -q, 0.0));
  const int v0Hi = AddVertex(&s, W(px, q, 0.0));
  const int vALo = AddVertex(&s, W(px, -q, za));
  const int vAHi = AddVertex(&s, W(px, q, za));
  const int vBLo = AddVertex(&s, W(px, -q, zb));
  const int vBHi = AddVertex(&s, W(px, q, zb));
  const int vLLo = AddVertex(&s, W(px, -q, h));
  const int vLHi = AddVertex(&s, W(px, q, h));

  // At each level, the "minor" arc runs the short way through u = 0 (the +x point, r,0,z) and the
  // "major" arc the long way through u = π (the −x point) — the same pair used everywhere the
  // kernel needs a full circle split to match a chord elsewhere (issue #242).
  auto level = [&](double z, int lo, int hi, int* mn, int* mj) {
    const Vec3 c = W(0.0, 0.0, z);
    *mn = AddArc(&s, lo, hi, c, Z, minorSweep);
    *mj = AddArc(&s, hi, lo, c, Z, majorSweep);
  };
  int mn0 = -1, mj0 = -1, mnA = -1, mjA = -1, mnB = -1, mjB = -1, mnL = -1, mjL = -1;
  level(0.0, v0Lo, v0Hi, &mn0, &mj0);
  level(za, vALo, vAHi, &mnA, &mjA);
  level(zb, vBLo, vBHi, &mnB, &mjB);
  level(h, vLLo, vLHi, &mnL, &mjL);

  const int sHi0A = AddLine(&s, v0Hi, vAHi);
  const int sLo0A = AddLine(&s, v0Lo, vALo);
  const int sHiAB = AddLine(&s, vAHi, vBHi);
  const int sLoAB = AddLine(&s, vALo, vBLo);
  const int sHiBL = AddLine(&s, vBHi, vLHi);
  const int sLoBL = AddLine(&s, vBLo, vLLo);
  const int chordA = AddLine(&s, vALo, vAHi);
  const int chordB = AddLine(&s, vBLo, vBHi);

  // Each partial band gets its OWN frame origin (shifted to its own local z = 0) and its own
  // `height` — `CylinderCutZExtent` only recognises a cut against an `Ellipse` edge, so a face
  // bounded by plain rim arcs falls back to `ConicalFaceIntegrals`, which trusts `sf.height` as
  // the face's actual span. Sharing the full-cylinder frame/height across every sub-band here
  // would integrate each one as if it ran the whole way from the base to the top.
  auto cyl = [&](double zBase, double zSpan, double u0, double u1, std::vector<EdgeUse> uses) {
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.frame = axisFrame;
    f.surface.frame.origin = W(0.0, 0.0, zBase);
    f.surface.radius = r;
    f.surface.radius2 = r;
    f.surface.height = zSpan;
    f.uStart = u0;
    f.uEnd = u1;
    f.loops.push_back(Loop{std::move(uses)});
    s.faces.push_back(std::move(f));
  };

  s.faces.push_back(MakePlaneFace(W(0.0, 0.0, 0.0), ray3d::Scale(Z, -1.0), {{mn0, true}, {mj0, true}}));
  s.faces.push_back(MakePlaneFace(W(0.0, 0.0, h), Z, {{mnL, false}, {mjL, false}}));
  cyl(0.0, za, -phi, phi, {{mn0, false}, {sHi0A, false}, {mnA, true}, {sLo0A, true}});
  cyl(0.0, za, phi, kTwoPi - phi, {{mj0, false}, {sLo0A, false}, {mjA, true}, {sHi0A, true}});
  cyl(zb, h - zb, -phi, phi, {{mnB, false}, {sHiBL, false}, {mnL, true}, {sLoBL, true}});
  cyl(zb, h - zb, phi, kTwoPi - phi, {{mjB, false}, {sLoBL, false}, {mjL, true}, {sHiBL, true}});
  cyl(za, zb - za, phi, kTwoPi - phi, {{mjA, false}, {sLoAB, false}, {mjB, true}, {sHiAB, true}});
  s.faces.push_back(MakePlaneFace(W(px, 0.0, za), Z, {{mnA, false}, {chordA, true}}));
  s.faces.push_back(
      MakePlaneFace(W(px, 0.0, zb), ray3d::Scale(Z, -1.0), {{mnB, true}, {chordB, false}}));
  s.faces.push_back(MakePlaneFace(
      W(px, 0.0, 0.5 * (za + zb)), axisFrame.xAxis,
      {{chordA, false}, {sHiAB, false}, {chordB, true}, {sLoAB, true}}));

  AddSingleShell(&s);
  if (Validate(s) != Problem::Ok || SelfIntersects(s))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

[[nodiscard]] bool TryBooleanCylinderThroughPlanar(const Solid& planar, const Solid& cyl,
                                                   const CylinderShape& C, BoolOp op, bool cylIsMinuend,
                                                   std::vector<Solid>* out, bool* handled,
                                                   Problem* outWhy) {
  *handled = true;
  if (op == BoolOp::Subtract && cylIsMinuend) {
    // cylinder − box: the single-flat notch. Recognise exactly one box face parallel to the axis
    // that cuts within the radius, the box otherwise square to the axis and engulfing the removed
    // slab; then the subtraction is the cylinder minus that one half-space. A partial-length pocket,
    // a slot bounded by two faces, or a tilted box falls through to the refusal (GitHub issue #242).
    const Vec3 az2 = C.axis.zAxis;
    const double sc2 = std::max(ModelScale(planar), C.radius + C.length);
    const double e2 = 1e-7 * sc2;
    if (!AabbsOverlap(planar, cyl, e2)) {
      out->push_back(cyl);  // disjoint — the minuend is unchanged
      return Succeed(outWhy);
    }
    std::vector<int> cutFaces;
    bool boxSquareToAxis = true;
    for (int fi = 0; fi < static_cast<int>(planar.faces.size()); ++fi) {
      const Face& f = planar.faces[static_cast<std::size_t>(fi)];
      if (f.surface.kind != SurfaceKind::Plane)
        continue;
      const std::vector<Vec3> ring = FaceRing(planar, f);
      if (ring.size() < 3)
        continue;
      const Vec3 n = f.surface.frame.zAxis;
      const double dn = std::fabs(ray3d::Dot(n, az2));
      if (dn > 1e-6 && dn < 1.0 - 1e-6)
        boxSquareToAxis = false;  // a face neither parallel nor perpendicular to the axis
      if (dn > 1e-6)
        continue;  // not parallel to the axis — cannot be the lengthwise flat
      const double px = ray3d::Dot(ray3d::Sub(C.axis.origin, ring[0]), n);  // axis offset, +n out of box
      if (px > -(C.radius - e2) && px < C.radius - e2)
        cutFaces.push_back(fi);
    }
    const int cutCount = static_cast<int>(cutFaces.size());
    if (boxSquareToAxis && cutCount == 1) {
      const Face& f = planar.faces[static_cast<std::size_t>(cutFaces[0])];
      const std::vector<Vec3> ring = FaceRing(planar, f);
      const Vec3 n = f.surface.frame.zAxis;
      const double px = ray3d::Dot(ray3d::Sub(C.axis.origin, ring[0]), n);
      const double q = std::sqrt(std::max(0.0, C.radius * C.radius - px * px));
      // Local frame: base at the cylinder base, +x into the removed slab (−n), +z the axis.
      ucs::Ucs lf = C.axis;
      const Vec3 wx0 = ray3d::Scale(n, -1.0);
      lf.xAxis = ray3d::Normalize(ray3d::Sub(wx0, ray3d::Scale(lf.zAxis, ray3d::Dot(wx0, lf.zAxis))));
      lf.yAxis = ray3d::Normalize(ray3d::Cross(lf.zAxis, lf.xAxis));
      auto WL = [&](double x, double y, double z) { return ucs::UcsToWorld(lf, Vec3{x, y, z}); };
      auto insideEveryBoxFace = [&](const std::vector<Vec3>& probe) {
        for (const Face& bf : planar.faces) {
          if (bf.surface.kind != SurfaceKind::Plane)
            continue;
          const std::vector<Vec3> br = FaceRing(planar, bf);
          if (br.size() < 3)
            continue;
          const Vec3 bn = bf.surface.frame.zAxis;
          for (const Vec3& p : probe)
            if (ray3d::Dot(ray3d::Sub(p, br[0]), bn) > e2)
              return false;
        }
        return true;
      };
      // Every extreme point of the removed segment must lie inside the box, so that
      // cylinder − box == cylinder − (this one half-space).
      const std::vector<Vec3> fullProbe = {WL(px, -q, 0.0),       WL(px, q, 0.0),
                                           WL(px, q, C.length),   WL(px, -q, C.length),
                                           WL(C.radius, 0.0, 0.0), WL(C.radius, 0.0, C.length)};
      if (insideEveryBoxFace(fullProbe)) {
        Solid r;
        if (!BuildCylinderLongitudinalFlat(lf, C.radius, C.length, px, &r, outWhy))
          return false;
        out->push_back(std::move(r));
        return Succeed(outWhy);
      }
      // Not a full-span flat — try a partial-length pocket: the box's two axis-perpendicular faces,
      // both strictly inside the cylinder's length, bound a rectangular bite in the middle
      // (GitHub issue #242).
      std::vector<double> perpHeights;
      bool perpOk = true;
      for (const Face& bf : planar.faces) {
        if (bf.surface.kind != SurfaceKind::Plane)
          continue;
        const std::vector<Vec3> br = FaceRing(planar, bf);
        if (br.size() < 3)
          continue;
        const Vec3 bn = bf.surface.frame.zAxis;
        if (std::fabs(std::fabs(ray3d::Dot(bn, az2)) - 1.0) > 1e-6)
          continue;
        const double t = ray3d::Dot(ray3d::Sub(br[0], C.axis.origin), az2);
        if (t <= e2 || t >= C.length - e2) {
          perpOk = false;  // a perpendicular face at or beyond a cap — not this shape
          continue;
        }
        perpHeights.push_back(t);
      }
      if (perpOk && perpHeights.size() == 2) {
        const double za = std::min(perpHeights[0], perpHeights[1]);
        const double zb = std::max(perpHeights[0], perpHeights[1]);
        const std::vector<Vec3> pocketProbe = {WL(px, -q, za),          WL(px, q, za),
                                               WL(px, -q, zb),          WL(px, q, zb),
                                               WL(C.radius, 0.0, za),   WL(C.radius, 0.0, zb)};
        if (insideEveryBoxFace(pocketProbe)) {
          Solid r;
          if (!BuildCylinderPocket(lf, C.radius, C.length, px, za, zb, &r, outWhy))
            return false;
          out->push_back(std::move(r));
          return Succeed(outWhy);
        }
      }
    }
    if (boxSquareToAxis && cutCount == 2) {
      // A slot: two parallel cutting faces bound a full-length slab between them, leaving two
      // disjoint "wing" pieces (GitHub issue #242). Each wing has exactly the single-flat notch's
      // shape, so it reuses BuildCylinderLongitudinalFlat; the second wing is built in a frame with
      // its +x mirrored (still right-handed) so "kept x ≤ px" reads as the far side of the slot.
      const Face& f0 = planar.faces[static_cast<std::size_t>(cutFaces[0])];
      const std::vector<Vec3> ring0 = FaceRing(planar, f0);
      const Vec3 n0 = f0.surface.frame.zAxis;
      const double px0 = ray3d::Dot(ray3d::Sub(C.axis.origin, ring0[0]), n0);
      ucs::Ucs lf0 = C.axis;
      const Vec3 wx00 = ray3d::Scale(n0, -1.0);
      lf0.xAxis =
          ray3d::Normalize(ray3d::Sub(wx00, ray3d::Scale(lf0.zAxis, ray3d::Dot(wx00, lf0.zAxis))));
      lf0.yAxis = ray3d::Normalize(ray3d::Cross(lf0.zAxis, lf0.xAxis));
      // The other face's threshold, expressed in lf0's coordinates. Its own local frame has
      // xAxis' = -n1 = n0 = -lf0.xAxis (n1 is antiparallel to n0 for a box's two opposing faces), so
      // its "kept x' <= px1" becomes "lf0-x >= -px1" — the far wing is kept where lf0-x >= qx1Signed.
      const Face& f1 = planar.faces[static_cast<std::size_t>(cutFaces[1])];
      const std::vector<Vec3> ring1 = FaceRing(planar, f1);
      const Vec3 n1 = f1.surface.frame.zAxis;
      if (ray3d::Dot(n0, n1) > -1.0 + 1e-6)
        return Fail(Problem::BooleanCurvedFace, outWhy);  // not a simple opposing-face slot
      const double px1 = ray3d::Dot(ray3d::Sub(C.axis.origin, ring1[0]), n1);
      const double qx1Signed = -px1;
      if (px0 < qx1Signed - e2) {
        auto WL0 = [&](double x, double y, double z) { return ucs::UcsToWorld(lf0, Vec3{x, y, z}); };
        const double q0 = std::sqrt(std::max(0.0, C.radius * C.radius - px0 * px0));
        const double q1 = std::sqrt(std::max(0.0, C.radius * C.radius - qx1Signed * qx1Signed));
        auto insideEveryBoxFace = [&](const std::vector<Vec3>& probe) {
          for (const Face& bf : planar.faces) {
            if (bf.surface.kind != SurfaceKind::Plane)
              continue;
            const std::vector<Vec3> br = FaceRing(planar, bf);
            if (br.size() < 3)
              continue;
            const Vec3 bn = bf.surface.frame.zAxis;
            for (const Vec3& p : probe)
              if (ray3d::Dot(ray3d::Sub(p, br[0]), bn) > e2)
                return false;
          }
          return true;
        };
        const std::vector<Vec3> slotProbe = {
            WL0(px0, -q0, 0.0),       WL0(px0, q0, 0.0),       WL0(px0, -q0, C.length),
            WL0(px0, q0, C.length),   WL0(qx1Signed, -q1, 0.0), WL0(qx1Signed, q1, 0.0),
            WL0(qx1Signed, -q1, C.length), WL0(qx1Signed, q1, C.length)};
        if (insideEveryBoxFace(slotProbe)) {
          ucs::Ucs mf = lf0;
          mf.xAxis = ray3d::Scale(lf0.xAxis, -1.0);
          mf.yAxis = ray3d::Scale(lf0.yAxis, -1.0);
          Solid wingLo;
          Solid wingHi;
          if (!BuildCylinderLongitudinalFlat(lf0, C.radius, C.length, px0, &wingLo, outWhy) ||
              !BuildCylinderLongitudinalFlat(mf, C.radius, C.length, -qx1Signed, &wingHi, outWhy))
            return false;
          out->push_back(std::move(wingLo));
          out->push_back(std::move(wingHi));
          return Succeed(outWhy);
        }
      }
    }
    if (boxSquareToAxis && cutCount == 0 && PointInPlanarSolid(C.axis.origin, planar, sc2) &&
        PointInPlanarSolid(ray3d::Add(C.axis.origin, ray3d::Scale(az2, C.length)), planar, sc2))
      return Fail(Problem::BooleanEmptyResult, outWhy);  // box swallows the whole cylinder
    return Fail(Problem::BooleanCurvedFace, outWhy);  // cylinder − box: other cases, their own slice
  }
  const bool bore = op == BoolOp::Subtract;  // box − cylinder: drill a hole (B2a)
  const Vec3 az = C.axis.zAxis;
  const double scale = std::max(ModelScale(planar), C.radius + C.length);
  const double eps = 1e-7 * scale;

  struct Hit {
    int face = -1;
    double t = 0.0;
    Vec3 point;
  };
  Hit entry;
  Hit exitH;
  bool anyPerp = false;
  for (int fi = 0; fi < static_cast<int>(planar.faces.size()); ++fi) {
    const Face& f = planar.faces[static_cast<std::size_t>(fi)];
    if (f.surface.kind != SurfaceKind::Plane)
      continue;
    const Vec3 n = f.surface.frame.zAxis;
    const double dn = ray3d::Dot(n, az);
    if (std::fabs(std::fabs(dn) - 1.0) > 1e-6)
      continue;
    const std::vector<Vec3> ring = FaceRing(planar, f);
    if (ring.size() < 3)
      continue;
    const double t = ray3d::Dot(ray3d::Sub(ring[0], C.axis.origin), n) / dn;
    const Vec3 hp = ray3d::Add(C.axis.origin, ray3d::Scale(az, t));
    bool onEdge = false;
    if (!PointInPolygon3D(hp, ring, n, eps, &onEdge))
      continue;
    anyPerp = true;
    double clr = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < ring.size(); ++i) {
      const Vec3& p0 = ring[i];
      const Vec3& p1 = ring[(i + 1) % ring.size()];
      const Vec3 e = ray3d::Sub(p1, p0);
      const double len2 = ray3d::Dot(e, e);
      double u = len2 > 1e-24 ? ray3d::Dot(ray3d::Sub(hp, p0), e) / len2 : 0.0;
      u = std::clamp(u, 0.0, 1.0);
      clr = std::min(clr, ray3d::Length(ray3d::Sub(hp, ray3d::Add(p0, ray3d::Scale(e, u)))));
    }
    if (clr < C.radius + eps)
      continue;  // the footprint crosses this face's edge — a mixed arc/line intersection (B2)
    Hit& slot = dn < 0.0 ? entry : exitH;
    if (slot.face < 0)
      slot = Hit{fi, t, hp};
  }

  const bool havePair = entry.face >= 0 && exitH.face >= 0 && exitH.t > entry.t + eps;
  if (havePair) {
    const double lo = std::max(entry.t, 0.0);
    const double hi = std::min(exitH.t, C.length);
    if (hi - lo <= eps) {  // the cylinder does not actually reach the solid
      if (op == BoolOp::Intersect)
        return Fail(Problem::BooleanEmptyResult, outWhy);
      out->push_back(planar);
      out->push_back(cyl);
      return Succeed(outWhy);
    }
    if (op == BoolOp::Intersect) {
      ucs::Ucs fr = C.axis;
      fr.origin = ray3d::Add(C.axis.origin, ray3d::Scale(az, lo));
      Solid r;
      Problem w = Problem::Ok;
      if (!MakeCylinder(fr, C.radius, hi - lo, &r, &w))
        return Fail(Problem::BooleanResultInvalid, outWhy);
      out->push_back(std::move(r));
      return Succeed(outWhy);
    }
    if (bore) {
      if (entry.t < -eps || entry.t > C.length + eps)
        return Fail(Problem::BooleanCurvedFace, outWhy);  // cylinder base inside — a floating pocket, defer
      const Vec3 nE = planar.faces[static_cast<std::size_t>(entry.face)].surface.frame.zAxis;
      const Vec3 entryC = entry.point;
      const bool through = exitH.t <= C.length + eps;
      Solid r;
      if (through) {
        if (!BuildBore(planar, entry.face, entryC, nE, C.radius, /*through=*/true, exitH.face,
                       exitH.point, 0.0, &r, outWhy))
          return false;
      } else {
        const double depth = C.length - std::max(entry.t, 0.0);
        if (depth <= eps)
          return Fail(Problem::BooleanCurvedFace, outWhy);
        if (!BuildBore(planar, entry.face, entryC, nE, C.radius, /*through=*/false, -1, Vec3{}, depth,
                       &r, outWhy))
          return false;
      }
      out->push_back(std::move(r));
      return Succeed(outWhy);
    }
    std::vector<BossStub> stubs;
    if (entry.t > eps)
      stubs.push_back(BossStub{entry.face, entry.point,
                               planar.faces[static_cast<std::size_t>(entry.face)].surface.frame.zAxis,
                               entry.t});
    if (C.length - exitH.t > eps)
      stubs.push_back(BossStub{exitH.face, exitH.point,
                               planar.faces[static_cast<std::size_t>(exitH.face)].surface.frame.zAxis,
                               C.length - exitH.t});
    if (stubs.empty()) {
      out->push_back(planar);
      return Succeed(outWhy);
    }
    Solid r;
    if (!BuildBoss(planar, stubs, C.radius, &r, outWhy))
      return false;
    out->push_back(std::move(r));
    return Succeed(outWhy);
  }

  if (!AabbsOverlap(planar, cyl, eps)) {
    if (op == BoolOp::Intersect)
      return Fail(Problem::BooleanEmptyResult, outWhy);
    out->push_back(planar);
    out->push_back(cyl);
    return Succeed(outWhy);
  }

  // A TILTED cylinder (B2b-1): the axis crosses two planar faces at an angle, cutting an ellipse on
  // each. INTERSECT -> a plug with two oblique elliptical ends; SUBTRACT -> an elliptical-mouthed bore.
  {
    struct EHit {
      int face = -1;
      double t = 0.0;
      Vec3 n;
      Vec3 pt;
    };
    std::vector<EHit> hits;
    bool messy = false;
    for (int fi = 0; fi < static_cast<int>(planar.faces.size()); ++fi) {
      const Face& f = planar.faces[static_cast<std::size_t>(fi)];
      if (f.surface.kind != SurfaceKind::Plane)
        continue;
      const Vec3 n = f.surface.frame.zAxis;
      const double dn = ray3d::Dot(n, az);
      if (std::fabs(dn) < 1e-6)
        continue;  // axis parallel to the face — no crossing
      const std::vector<Vec3> ring = FaceRing(planar, f);
      if (ring.size() < 3)
        continue;
      const double t = ray3d::Dot(ray3d::Sub(ring[0], C.axis.origin), n) / dn;
      const Vec3 hp = ray3d::Add(C.axis.origin, ray3d::Scale(az, t));
      bool onEdge = false;
      if (!PointInPolygon3D(hp, ring, n, eps, &onEdge))
        continue;
      double clr = std::numeric_limits<double>::max();
      for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec3& p0 = ring[i];
        const Vec3& p1 = ring[(i + 1) % ring.size()];
        const Vec3 e = ray3d::Sub(p1, p0);
        const double l2 = ray3d::Dot(e, e);
        double u = l2 > 1e-24 ? ray3d::Dot(ray3d::Sub(hp, p0), e) / l2 : 0.0;
        u = std::clamp(u, 0.0, 1.0);
        clr = std::min(clr, ray3d::Length(ray3d::Sub(hp, ray3d::Add(p0, ray3d::Scale(e, u)))));
      }
      if (clr < C.radius / std::fabs(dn) + eps) {  // the elliptical footprint runs over a face edge
        messy = true;
        continue;
      }
      hits.push_back(EHit{fi, t, n, hp});
    }
    if (!messy && hits.size() == 2) {
      double t0 = hits[0].t;
      double t1 = hits[1].t;
      if (t0 > t1) {
        std::swap(t0, t1);
        std::swap(hits[0], hits[1]);
      }
      if (t1 - t0 <= eps || t0 < -eps || t1 > C.length + eps)
        return Fail(Problem::BooleanCurvedFace, outWhy);  // partial penetration — a later slice
      Solid r;
      if (op == BoolOp::Intersect) {
        if (!BuildObliqueCylinderPlug(C.axis, C.radius, hits[0].pt, hits[0].n, hits[1].pt, hits[1].n,
                                      &r, outWhy))
          return false;
      } else if (op == BoolOp::Subtract && !cylIsMinuend) {
        if (!BuildTiltedBore(planar, hits[0].face, hits[0].n, hits[1].face, hits[1].n, C.axis,
                             C.radius, &r, outWhy))
          return false;
      } else if (op == BoolOp::Union) {
        return BuildTiltedBoss(planar, hits[0].face, hits[0].n, t0, hits[1].face, hits[1].n, t1, C,
                               out, outWhy)
                   ? Succeed(outWhy)
                   : false;
      } else {
        return Fail(Problem::BooleanObliqueCylinder, outWhy);  // cyl − box tilted — later
      }
      out->push_back(std::move(r));
      return Succeed(outWhy);
    }
  }

  if (!anyPerp)
    return Fail(Problem::BooleanObliqueCylinder, outWhy);
  return Fail(Problem::BooleanCurvedFace, outWhy);  // partial penetration / a footprint over an edge
}

/// `sphere` × `cylinder` (GitHub issue #242, REQ-314 B2b-2 first pair). Handles two axis positions,
/// both with the cylinder caps clear of the sphere on each side and `r < Rs`:
///   - **centred** — the cylinder axis through the sphere centre; the intersection is two plane
///     circles and every result is closed-form.
///   - **offset** — the axis at perpendicular distance `d` from the sphere centre, `d + r < Rs`; the
///     intersection is a quartic loop each side and the curved faces integrate numerically. Because
///     a sphere is rotationally symmetric, `d` (plus the axial position and length) is the *only*
///     invariant of the cylinder's placement — this covers every axis direction and position, not
///     just some "coplanar" special case; there is no separate skew-axis configuration to handle.
///     Both `d > r` (axis misses the pole) and `d < r` (pole-covered, issue #242) do all three
///     operations; the `d < r` results keep the sphere's equatorial zone in place of two hemispheres.
/// `d ≈ r` tangency (axis grazing the pole) falls through unhandled, refused as degenerate.
///
/// All three operations are built: INTERSECT (a spherical-ended barrel / plug), UNION (the ball with
/// a cylindrical boss each side), and SUBTRACT — `sphere − cylinder` is the drilled ball (genus 1),
/// `cylinder − sphere` two stubs each with a spherical dimple. \p sphereIsMinuend picks the
/// SUBTRACT direction and is ignored for the other two.
[[nodiscard]] bool TryBooleanSphereCylinder(const SphereShape& S, const CylinderShape& C, BoolOp op,
                                            bool sphereIsMinuend, std::vector<Solid>* out,
                                            bool* handled, Problem* outWhy) {
  const double sc = S.radius + C.radius + C.length;
  const double eps = 1e-7 * sc;
  // The sphere centre must lie on the cylinder axis line.
  const Vec3 w = ray3d::Sub(S.centre, C.axis.origin);
  const double along = ray3d::Dot(w, C.axis.zAxis);
  const Vec3 foot = ray3d::Add(C.axis.origin, ray3d::Scale(C.axis.zAxis, along));
  const double offset = ray3d::Length(ray3d::Sub(S.centre, foot));  // axis-to-centre distance

  if (offset > eps) {
    // The offset quartic (issue #242, slice B). The cylinder must clear the equator (`d + r < Rs`)
    // and both caps must clear the sphere. `d > r` (axis misses the pole) and `d < r` (pole-covered)
    // both do all three operations — the quartic scaffold is the same, only which sphere patches are
    // kept differs. `d` alone (a sphere has no other invariant of the axis's position — every axis
    // direction and placement is covered here). `d ≈ r` tangency falls through unhandled.
    const double d = offset;
    if (!(d + C.radius < S.radius - eps) || std::fabs(d - C.radius) <= eps)
      return false;
    // The loop reaches |z| = zP at φ = π; both caps must clear that for a clean plug / boss / hole.
    const double zP = std::sqrt(std::max(0.0, S.radius * S.radius - (d - C.radius) * (d - C.radius)));
    if (along - zP < eps || C.length - (along + zP) < eps)
      return false;  // a cap sits inside the sphere
    *handled = true;
    ucs::Ucs fq;
    if (!ucs::FromNormal(S.centre, C.axis.zAxis, &fq))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    Vec3 x = ray3d::Sub(foot, S.centre);  // from the sphere centre toward the cylinder axis
    x = ray3d::Sub(x, ray3d::Scale(fq.zAxis, ray3d::Dot(x, fq.zAxis)));
    if (!(ray3d::Length(x) > 1e-9 * sc))
      return Fail(Problem::BooleanResultInvalid, outWhy);
    fq.xAxis = ray3d::Normalize(x);
    fq.yAxis = ray3d::Normalize(ray3d::Cross(fq.zAxis, fq.xAxis));
    if (op == BoolOp::Subtract && !sphereIsMinuend)
      return BuildCylinderSphereOffsetSubtract(fq, C.radius, S.radius, d, -along, C.length - along,
                                               out, outWhy);
    Solid rq;
    if (op == BoolOp::Intersect) {
      if (!BuildSphereCylinderOffsetIntersection(fq, C.radius, S.radius, d, &rq, outWhy))
        return false;
    } else if (op == BoolOp::Union) {
      if (!BuildSphereCylinderOffsetUnion(fq, C.radius, S.radius, d, -along, C.length - along, &rq,
                                          outWhy))
        return false;
    } else {
      if (!BuildSphereCylinderOffsetSubtractSphere(fq, C.radius, S.radius, d, &rq, outWhy))
        return false;
    }
    out->push_back(std::move(rq));
    return Succeed(outWhy);
  }

  if (S.radius <= C.radius + eps)
    return false;  // the cylinder does not fit inside the sphere — not this shape
  if (along < S.radius - eps || along > C.length - S.radius + eps)
    return false;  // a cylinder cap reaches into the sphere — not the clean spherical-ended barrel
                   // (the cap must clear the sphere entirely, else the result keeps a flat disk piece)

  *handled = true;
  ucs::Ucs fr;
  if (!ucs::FromNormal(S.centre, C.axis.zAxis, &fr))
    return Fail(Problem::BooleanResultInvalid, outWhy);
  const double zBot = -along;
  const double zTop = C.length - along;
  Solid r;
  switch (op) {
  case BoolOp::Intersect:
    if (!BuildSphereCylinderIntersection(fr, C.radius, S.radius, &r, outWhy))
      return false;
    out->push_back(std::move(r));
    return Succeed(outWhy);
  case BoolOp::Union:
    if (!BuildSphereCylinderUnion(fr, C.radius, S.radius, zBot, zTop, &r, outWhy))
      return false;
    out->push_back(std::move(r));
    return Succeed(outWhy);
  case BoolOp::Subtract:
    if (sphereIsMinuend) {
      if (!BuildSphereCylinderSubtractSphere(fr, C.radius, S.radius, &r, outWhy))
        return false;
      out->push_back(std::move(r));
      return Succeed(outWhy);
    }
    if (!BuildCylinderSphereSubtract(fr, C.radius, S.radius, zBot, zTop, out, outWhy))
      return false;
    return Succeed(outWhy);
  }
  return false;
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
// ---------------------------------------------------------------------------------------------
// REQ-317 POLYSOLID: a wall swept along a path (ADR-050).
//
// The whole of the difficulty is the corners. Offsetting each segment to each side is easy and
// gives a run of disconnected pieces; making one wall out of them means INTERSECTING adjacent
// offsets so the corner is mitred and counted once. A box per straight run would be far easier and
// is wrong three ways at once — the runs overlap, the drawing holds N objects where the user drew
// one, and the volume double-counts every bend (ADR-050 (b)).
// ---------------------------------------------------------------------------------------------
namespace {

/// A 2D point in the path frame's plane. `ucs::Point2D` with arithmetic, kept local because it
/// exists only for the two hundred lines below.
struct P2 {
  double x = 0.0;
  double y = 0.0;
};

[[nodiscard]] P2 Sub2(const P2& a, const P2& b) { return P2{a.x - b.x, a.y - b.y}; }
[[nodiscard]] P2 Add2(const P2& a, const P2& b) { return P2{a.x + b.x, a.y + b.y}; }
[[nodiscard]] P2 Mul2(const P2& a, double s) { return P2{a.x * s, a.y * s}; }
[[nodiscard]] double Dot2(const P2& a, const P2& b) { return a.x * b.x + a.y * b.y; }
[[nodiscard]] double Cross2v(const P2& a, const P2& b) { return a.x * b.y - a.y * b.x; }
[[nodiscard]] double Len2(const P2& a) { return std::sqrt(a.x * a.x + a.y * a.y); }
/// Rotate 90 degrees counter-clockwise: the LEFT of a direction of travel.
[[nodiscard]] P2 Left2(const P2& a) { return P2{-a.y, a.x}; }

/// One segment of the centreline, resolved into geometry.
///
/// A straight segment carries its direction; a curved one its centre, radius and signed sweep. Both
/// carry the tangent at each end, which is what the corner code actually asks for — it never needs
/// to know which kind it is holding to decide whether two segments meet smoothly.
struct Seg {
  bool arc = false;
  P2 a{};        ///< start point
  P2 b{};        ///< end point
  P2 dir{};      ///< straight only: unit direction
  P2 centre{};   ///< arc only
  double radius = 0.0;
  double sweep = 0.0;  ///< arc only, signed, CCW positive
  P2 tanA{};     ///< unit tangent at `a`, pointing along travel
  P2 tanB{};     ///< unit tangent at `b`, pointing along travel
};

/// The offset of a \ref Seg to one side. A straight segment offsets to a parallel line; a curved one
/// to a concentric arc about the same centre — which is why an offset keeps the centre rather than
/// recomputing one.
struct OffsetSeg {
  bool arc = false;
  P2 a{};
  P2 b{};
  P2 dir{};
  P2 centre{};
  double radius = 0.0;
  double sweep = 0.0;
};

/// Angle of \p p about \p centre, in [-pi, pi].
[[nodiscard]] double AngleAt(const P2& centre, const P2& p) {
  return std::atan2(p.y - centre.y, p.x - centre.x);
}

/// \p a advanced to \p b in the direction \p sign, as a value in (0, 2*pi) times that sign.
[[nodiscard]] double SweepBetween(double a, double b, double sign) {
  double d = b - a;
  while (d <= 0.0)
    d += kTwoPi;
  while (d > kTwoPi)
    d -= kTwoPi;
  return sign >= 0.0 ? d : d - kTwoPi;
}

/// Resolve one path segment into geometry, or say why it is not one.
[[nodiscard]] bool ResolveSeg(const P2& a, const P2& b, double sweep, double eps, Seg* out) {
  const P2 chord = Sub2(b, a);
  const double c = Len2(chord);
  if (!(c > eps))
    return false;  // a repeated point; a full circle in ONE segment lands here too, by design
  out->a = a;
  out->b = b;
  if (std::fabs(sweep) <= 1e-12) {
    out->arc = false;
    out->dir = Mul2(chord, 1.0 / c);
    out->tanA = out->dir;
    out->tanB = out->dir;
    return true;
  }
  if (std::fabs(sweep) >= kTwoPi)
    return false;  // a segment cannot go all the way round: its own endpoints would coincide
  // Centre from the chord and the included angle. Positive (counter-clockwise) sweep puts the centre
  // to the LEFT of a->b, which is the same convention `AddArc` and every other curve here use.
  const double half = sweep * 0.5;
  const P2 mid = Mul2(Add2(a, b), 0.5);
  const P2 n = Left2(Mul2(chord, 1.0 / c));
  out->arc = true;
  out->centre = Add2(mid, Mul2(n, (c * 0.5) * (std::cos(half) / std::sin(half))));
  out->radius = c / (2.0 * std::fabs(std::sin(half)));
  out->sweep = sweep;
  const double s = sweep > 0.0 ? 1.0 : -1.0;
  // The tangent of a counter-clockwise arc is the outward radius turned a further quarter turn.
  out->tanA = Mul2(Left2(Mul2(Sub2(a, out->centre), 1.0 / out->radius)), s);
  out->tanB = Mul2(Left2(Mul2(Sub2(b, out->centre), 1.0 / out->radius)), s);
  return true;
}

/// \p seg offset by the signed distance \p t along its own left normal.
///
/// \return false when a curve is too tight for the offset — the inner radius reaching zero, where
/// the wall would turn inside out around the bend.
[[nodiscard]] bool OffsetOf(const Seg& seg, double t, double eps, OffsetSeg* out) {
  out->arc = seg.arc;
  if (!seg.arc) {
    const P2 n = Left2(seg.dir);
    out->a = Add2(seg.a, Mul2(n, t));
    out->b = Add2(seg.b, Mul2(n, t));
    out->dir = seg.dir;
    return true;
  }
  // The centre is on the left of a counter-clockwise arc, so moving left means moving toward it.
  const double r = seg.radius - (seg.sweep > 0.0 ? t : -t);
  if (!(r > eps))
    return false;
  const double scale = r / seg.radius;
  out->centre = seg.centre;
  out->radius = r;
  out->sweep = seg.sweep;
  out->a = Add2(seg.centre, Mul2(Sub2(seg.a, seg.centre), scale));
  out->b = Add2(seg.centre, Mul2(Sub2(seg.b, seg.centre), scale));
  return true;
}

/// Where two offset carriers meet, taking the root nearest \p near.
///
/// Three cases and no fourth: line/line solves two lines, line/circle a quadratic, circle/circle the
/// radical line. All closed form; nothing here iterates. Tangent joins — every arc drawn by the
/// command, which is tangent to the segment before it by construction — are handled by the caller
/// before this is reached, so the near-zero discriminant they produce is never relied on.
[[nodiscard]] bool IntersectCarriers(const OffsetSeg& p, const OffsetSeg& q, const P2& near,
                                     double eps, P2* out) {
  auto pick = [&](const P2& r0, const P2& r1, bool two) {
    if (!two) {
      *out = r0;
      return;
    }
    *out = Len2(Sub2(r0, near)) <= Len2(Sub2(r1, near)) ? r0 : r1;
  };

  if (!p.arc && !q.arc) {
    const double d = Cross2v(p.dir, q.dir);
    if (std::fabs(d) <= 1e-12) {
      // Parallel. Continuing straight needs no mitre and the two offsets already coincide; a
      // reversal has no corner point at all and is refused by the caller's collapse check.
      *out = p.b;
      return Dot2(p.dir, q.dir) > 0.0;
    }
    const P2 w = Sub2(q.a, p.a);
    *out = Add2(p.a, Mul2(p.dir, Cross2v(w, q.dir) / d));
    return true;
  }

  if (p.arc != q.arc) {
    const OffsetSeg& line = p.arc ? q : p;
    const OffsetSeg& circ = p.arc ? p : q;
    const P2 f = Sub2(line.a, circ.centre);
    const double b = 2.0 * Dot2(f, line.dir);
    const double c = Dot2(f, f) - circ.radius * circ.radius;
    double disc = b * b - 4.0 * c;
    if (disc < 0.0) {
      if (disc < -eps)
        return false;
      disc = 0.0;  // a tangent join, arriving here only from a path the command did not build
    }
    const double sq = std::sqrt(disc);
    pick(Add2(line.a, Mul2(line.dir, (-b - sq) * 0.5)),
         Add2(line.a, Mul2(line.dir, (-b + sq) * 0.5)), sq > 0.0);
    return true;
  }

  const P2 d = Sub2(q.centre, p.centre);
  const double dist = Len2(d);
  if (!(dist > 1e-12))
    return false;  // concentric: no corner
  const double a = (p.radius * p.radius - q.radius * q.radius + dist * dist) / (2.0 * dist);
  double h2 = p.radius * p.radius - a * a;
  if (h2 < 0.0) {
    if (h2 < -eps)
      return false;
    h2 = 0.0;
  }
  const P2 base = Add2(p.centre, Mul2(d, a / dist));
  const P2 perp = Mul2(Left2(Mul2(d, 1.0 / dist)), std::sqrt(h2));
  pick(Add2(base, perp), Sub2(base, perp), h2 > 0.0);
  return true;
}

/// Do the two segments \p a0-a1 and \p b0-b1 properly cross?
[[nodiscard]] bool SegsCross2D(const P2& a0, const P2& a1, const P2& b0, const P2& b1, double eps) {
  const double d1 = Cross2v(Sub2(a1, a0), Sub2(b0, a0));
  const double d2 = Cross2v(Sub2(a1, a0), Sub2(b1, a0));
  const double d3 = Cross2v(Sub2(b1, b0), Sub2(a0, b0));
  const double d4 = Cross2v(Sub2(b1, b0), Sub2(a1, b0));
  return ((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) &&
         ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps));
}

} // namespace

bool MakePolysolid(const ucs::Ucs& frame, const Path& path, double width, double height,
                   Justify justify, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!AllFinite({width, height, path.start.x, path.start.y}))
    return Fail(Problem::NonFiniteParameter, outWhy);
  if (!(width > 0.0))
    return Fail(Problem::NonPositiveWidth, outWhy);
  if (!(height > 0.0))
    return Fail(Problem::NonPositiveHeight, outWhy);
  if (!FrameOk(frame))
    return Fail(Problem::DegenerateFrame, outWhy);
  const std::size_t n = path.segs.size();
  if (n < 1 || (path.closed && n < 2))
    return Fail(Problem::PathTooShort, outWhy);
  for (const PathSeg& ps : path.segs) {
    if (!AllFinite({ps.end.x, ps.end.y, ps.sweep}))
      return Fail(Problem::NonFiniteParameter, outWhy);
  }

  // Tolerances scale with the drawing, so a 1 ft wall and a 1000 ft wall are judged on the same
  // relative terms — the argument `ModelScale` already makes for the validity checks.
  double extent = 0.0;
  {
    P2 mn{path.start.x, path.start.y};
    P2 mx = mn;
    for (const PathSeg& ps : path.segs) {
      mn.x = std::min(mn.x, ps.end.x);
      mn.y = std::min(mn.y, ps.end.y);
      mx.x = std::max(mx.x, ps.end.x);
      mx.y = std::max(mx.y, ps.end.y);
    }
    extent = std::max({mx.x - mn.x, mx.y - mn.y, width, 1e-9});
  }
  const double lenEps = 1e-9 * extent;
  const double areaEps = lenEps * extent;

  // --- The centreline ---------------------------------------------------------------------------
  std::vector<Seg> segs(n);
  {
    P2 prev{path.start.x, path.start.y};
    for (std::size_t i = 0; i < n; ++i) {
      const P2 next{path.segs[i].end.x, path.segs[i].end.y};
      if (!ResolveSeg(prev, next, path.segs[i].sweep, lenEps, &segs[i]))
        return Fail(Problem::PathSegmentDegenerate, outWhy);
      prev = next;
    }
    if (path.closed && Len2(Sub2(prev, P2{path.start.x, path.start.y})) > lenEps)
      return Fail(Problem::PathSegmentDegenerate, outWhy);
  }

  // Which way the closed ring winds decides which of the two rails is the OUTER boundary of the cap
  // faces and which is the hole. Shoelace over the stations plus each arc's own bulge — the same
  // decomposition `PlaneLoopSignedArea` uses on a face, for the same reason.
  double ringArea2 = 0.0;
  for (const Seg& sg : segs) {
    ringArea2 += sg.a.x * sg.b.y - sg.b.x * sg.a.y;
    if (sg.arc)
      ringArea2 += sg.radius * sg.radius * (sg.sweep - std::sin(sg.sweep));
  }
  const bool pathCcw = ringArea2 > 0.0;

  // --- The two offset rails ---------------------------------------------------------------------
  double tLeft = 0.0;
  double tRight = 0.0;
  switch (justify) {
  case Justify::Left:   tLeft = 0.0;          tRight = -width;      break;
  case Justify::Center: tLeft = width * 0.5;  tRight = -width * 0.5; break;
  case Justify::Right:  tLeft = width;        tRight = 0.0;         break;
  }

  const std::size_t stations = path.closed ? n : n + 1;
  // One rail: the offset of every segment, mitred at every station it shares with its neighbour.
  auto buildRail = [&](double t, std::vector<OffsetSeg>* rail, std::vector<P2>* pts) -> Problem {
    rail->assign(n, OffsetSeg{});
    for (std::size_t i = 0; i < n; ++i) {
      if (!OffsetOf(segs[i], t, lenEps, &(*rail)[i]))
        return Problem::PolysolidCurveTooTight;
    }
    pts->assign(stations, P2{});
    for (std::size_t j = 0; j < stations; ++j) {
      const bool interior = path.closed || (j > 0 && j < stations - 1);
      if (!interior) {
        (*pts)[j] = j == 0 ? (*rail)[0].a : (*rail)[n - 1].b;
        continue;
      }
      const std::size_t prev = (j + n - 1) % n;
      const std::size_t next = j % n;
      // A SMOOTH join needs no mitre and must not be given one: its two offsets are tangent, so the
      // intersection is a double root and solving for it would trade an exact answer for a
      // near-singular one. Every arc the command draws is tangent to the segment before it, so this
      // is the common path and not the exception.
      const P2& tOut = segs[prev].tanB;
      const P2& tIn = segs[next].tanA;
      if (std::fabs(Cross2v(tOut, tIn)) <= 1e-9 && Dot2(tOut, tIn) > 0.0) {
        (*pts)[j] = Add2(segs[next].a, Mul2(Left2(tIn), t));
        continue;
      }
      if (Dot2(tOut, tIn) <= -1.0 + 1e-9)
        return Problem::PolysolidCornerCollapsed;  // a full reversal has no corner point at all
      if (!IntersectCarriers((*rail)[prev], (*rail)[next], segs[next].a, areaEps, &(*pts)[j]))
        return Problem::PolysolidCornerCollapsed;
    }
    // Re-anchor each offset segment onto the mitred stations, then check it still describes the
    // piece of wall it started as. A corner too sharp, or a segment shorter than the mitre its
    // neighbours demand, shows up here as a piece that has reversed or wrapped right round.
    for (std::size_t i = 0; i < n; ++i) {
      OffsetSeg& os = (*rail)[i];
      os.a = (*pts)[i];
      os.b = (*pts)[(i + 1) % stations];
      if (!os.arc) {
        const P2 d = Sub2(os.b, os.a);
        if (!(Len2(d) > lenEps) || Dot2(d, os.dir) <= 0.0)
          return Problem::PolysolidCornerCollapsed;
        continue;
      }
      const double sign = os.sweep > 0.0 ? 1.0 : -1.0;
      const double sweep = SweepBetween(AngleAt(os.centre, os.a), AngleAt(os.centre, os.b), sign);
      if (!(std::fabs(sweep) > 1e-9) || std::fabs(std::fabs(sweep) - std::fabs(segs[i].sweep)) > kPi)
        return Problem::PolysolidCornerCollapsed;
      os.sweep = sweep;
    }
    return Problem::Ok;
  };

  std::vector<OffsetSeg> railL;
  std::vector<OffsetSeg> railR;
  std::vector<P2> ptsL;
  std::vector<P2> ptsR;
  if (const Problem why = buildRail(tLeft, &railL, &ptsL); why != Problem::Ok)
    return Fail(why, outWhy);
  if (const Problem why = buildRail(tRight, &railR, &ptsR); why != Problem::Ok)
    return Fail(why, outWhy);

  // A path that crosses its own run sweeps a wall enclosing part of the ground twice, and its volume
  // would count that part twice — the silent wrong answer REQ-201 forbids. ADR-045 (f) lets a TORUS
  // pass through itself because that is a shape people draw on purpose and only its mass properties
  // are withheld; a wall crossing itself is an authoring mistake, so it is refused outright.
  //
  // The test runs on paths made ENTIRELY of straight segments, where a rail is a polygon and the
  // answer is exact. With an arc in the path a rail is not a polygon, and testing its chords instead
  // would refuse walls that are perfectly fine — a false refusal being strictly worse than the
  // absence of a check. The general case is the same Phase 4 self-intersection test ADR-045 already
  // defers, and it is stated as a boundary rather than approximated here.
  {
    bool anyArc = false;
    for (const Seg& sg : segs)
      anyArc = anyArc || sg.arc;
    auto crosses = [&](const std::vector<P2>& pts) {
      const std::size_t m = pts.size();
      const std::size_t chords = path.closed ? m : m - 1;
      for (std::size_t i = 0; i < chords; ++i) {
        for (std::size_t j = i + 2; j < chords; ++j) {
          if (path.closed && i == 0 && j == chords - 1)
            continue;  // the ring's first and last chords legitimately share a station
          if (SegsCross2D(pts[i], pts[(i + 1) % m], pts[j], pts[(j + 1) % m], areaEps))
            return true;
        }
      }
      return false;
    };
    if (!anyArc && (crosses(ptsL) || crosses(ptsR)))
      return Fail(Problem::PolysolidPathSelfIntersects, outWhy);
  }

  // --- Topology ---------------------------------------------------------------------------------
  Solid s;
  std::vector<int> bl(stations);
  std::vector<int> br(stations);
  std::vector<int> tl(stations);
  std::vector<int> tr(stations);
  for (std::size_t j = 0; j < stations; ++j) {
    bl[j] = AddVertex(&s, Vec3{ptsL[j].x, ptsL[j].y, 0.0});
    br[j] = AddVertex(&s, Vec3{ptsR[j].x, ptsR[j].y, 0.0});
    tl[j] = AddVertex(&s, Vec3{ptsL[j].x, ptsL[j].y, height});
    tr[j] = AddVertex(&s, Vec3{ptsR[j].x, ptsR[j].y, height});
  }

  const Vec3 up{0.0, 0.0, 1.0};
  auto rail = [&](const OffsetSeg& os, int v0, int v1, double z) {
    return os.arc ? AddArc(&s, v0, v1, Vec3{os.centre.x, os.centre.y, z}, up, os.sweep)
                  : AddLine(&s, v0, v1);
  };
  std::vector<int> eBL(n), eBR(n), eTL(n), eTR(n);
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t k = (i + 1) % stations;
    eBL[i] = rail(railL[i], bl[i], bl[k], 0.0);
    eBR[i] = rail(railR[i], br[i], br[k], 0.0);
    eTL[i] = rail(railL[i], tl[i], tl[k], height);
    eTR[i] = rail(railR[i], tr[i], tr[k], height);
  }
  std::vector<int> eVL(stations), eVR(stations);
  for (std::size_t j = 0; j < stations; ++j) {
    eVL[j] = AddLine(&s, bl[j], tl[j]);
    eVR[j] = AddLine(&s, br[j], tr[j]);
  }

  // --- Side faces -------------------------------------------------------------------------------
  //
  // The left rail is the wall's left extreme whatever the justification, so its face looks LEFT;
  // the right one looks right. A curved run gives a cylinder rather than a torus — a polysolid
  // extrudes a flat profile straight up, and extruding a planar arc perpendicular to its own plane
  // sweeps a cylinder (ADR-048 (e)).
  auto sideFace = [&](const OffsetSeg& os, const Seg& seg, bool leftSide, std::vector<EdgeUse> uses) {
    Face f;
    if (!os.arc) {
      const P2 nrm = leftSide ? Left2(os.dir) : Mul2(Left2(os.dir), -1.0);
      f.surface = PlaneSurface(Vec3{os.a.x, os.a.y, 0.0}, Vec3{nrm.x, nrm.y, 0.0});
    } else {
      f.surface.kind = SurfaceKind::Cylinder;
      f.surface.frame.origin = Vec3{os.centre.x, os.centre.y, 0.0};
      f.surface.frame.xAxis = Vec3{1.0, 0.0, 0.0};
      f.surface.frame.yAxis = Vec3{0.0, 1.0, 0.0};
      f.surface.frame.zAxis = up;
      f.surface.radius = os.radius;
      f.surface.height = height;
      // Which side of the cylinder the material is on. For a counter-clockwise bend the centre is to
      // the left, so the LEFT rail is the inner one and its face looks INWARD - which is exactly the
      // `Surface::inward` flag REQ-314 B2a added for the wall of a bore, seen from the other side.
      const bool inner = (seg.sweep > 0.0) == leftSide;
      f.surface.inward = inner;
      const double a0 = AngleAt(os.centre, os.a);
      const double a1 = a0 + os.sweep;
      f.uStart = std::min(a0, a1);
      f.uEnd = std::max(a0, a1);
    }
    Loop lp;
    lp.uses = std::move(uses);
    f.loops.push_back(std::move(lp));
    s.faces.push_back(std::move(f));
  };
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t k = (i + 1) % stations;
    sideFace(railL[i], segs[i], true,
             {{eVL[i], false}, {eTL[i], false}, {eVL[k], true}, {eBL[i], true}});
    sideFace(railR[i], segs[i], false,
             {{eBR[i], false}, {eVR[k], false}, {eTR[i], true}, {eVR[i], true}});
  }

  // --- Caps and the two flat faces --------------------------------------------------------------
  auto railLoop = [&](const std::vector<int>& e, bool forward) {
    std::vector<EdgeUse> uses;
    uses.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
      uses.push_back(EdgeUse{e[forward ? i : n - 1 - i], !forward});
    return uses;
  };

  if (path.closed) {
    // No end caps, and each flat face has a hole: the ring's inner rail. Which rail that is depends
    // on which way the ring winds, so it is decided once, here, from the centreline's own area.
    std::vector<EdgeUse> bottomL = railLoop(eBL, true);
    std::vector<EdgeUse> bottomR = railLoop(eBR, false);
    std::vector<EdgeUse> topR = railLoop(eTR, true);
    std::vector<EdgeUse> topL = railLoop(eTL, false);

    Face bottom;
    bottom.surface = PlaneSurface(Vec3{ptsL[0].x, ptsL[0].y, 0.0}, Vec3{0.0, 0.0, -1.0});
    bottom.loops.push_back(Loop{pathCcw ? std::move(bottomR) : std::move(bottomL)});
    bottom.loops.push_back(Loop{pathCcw ? std::move(bottomL) : std::move(bottomR)});
    s.faces.push_back(std::move(bottom));

    Face top;
    top.surface = PlaneSurface(Vec3{ptsL[0].x, ptsL[0].y, height}, up);
    top.loops.push_back(Loop{pathCcw ? std::move(topR) : std::move(topL)});
    top.loops.push_back(Loop{pathCcw ? std::move(topL) : std::move(topR)});
    s.faces.push_back(std::move(top));
  } else {
    const std::size_t last = stations - 1;
    const int cb0 = AddLine(&s, bl[0], br[0]);
    const int cbN = AddLine(&s, bl[last], br[last]);
    const int ct0 = AddLine(&s, tl[0], tr[0]);
    const int ctN = AddLine(&s, tl[last], tr[last]);

    std::vector<EdgeUse> bottom = railLoop(eBL, true);
    bottom.push_back(EdgeUse{cbN, false});
    for (EdgeUse& u : railLoop(eBR, false))
      bottom.push_back(u);
    bottom.push_back(EdgeUse{cb0, true});
    s.faces.push_back(
        MakePlaneFace(Vec3{ptsL[0].x, ptsL[0].y, 0.0}, Vec3{0.0, 0.0, -1.0}, std::move(bottom)));

    std::vector<EdgeUse> top = railLoop(eTR, true);
    top.push_back(EdgeUse{ctN, true});
    for (EdgeUse& u : railLoop(eTL, false))
      top.push_back(u);
    top.push_back(EdgeUse{ct0, false});
    s.faces.push_back(MakePlaneFace(Vec3{ptsL[0].x, ptsL[0].y, height}, up, std::move(top)));

    const P2 startN = Mul2(segs[0].tanA, -1.0);
    const P2 endN = segs[n - 1].tanB;
    s.faces.push_back(MakePlaneFace(Vec3{ptsL[0].x, ptsL[0].y, 0.0}, Vec3{startN.x, startN.y, 0.0},
                                    {{cb0, false}, {eVR[0], false}, {ct0, true}, {eVL[0], true}}));
    s.faces.push_back(MakePlaneFace(Vec3{ptsL[last].x, ptsL[last].y, 0.0}, Vec3{endN.x, endN.y, 0.0},
                                    {{cbN, true}, {eVL[last], false}, {ctN, false}, {eVR[last], true}}));
  }

  AddSingleShell(&s);
  s.recipe.kind = PrimitiveKind::Polysolid;
  s.recipe.width = width;
  s.recipe.height = height;
  s.recipe.path = path;
  s.recipe.justify = justify;
  PlaceInFrame(&s, frame);

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}




// ---------------------------------------------------------------------------------------------
// Validity.
// ---------------------------------------------------------------------------------------------

int EulerCharacteristic(const Solid& s) {
  return static_cast<int>(s.vertices.size()) - static_cast<int>(s.edges.size()) +
         static_cast<int>(s.faces.size());
}

Problem Validate(const Solid& s) {
  const int vn = static_cast<int>(s.vertices.size());
  const int en = static_cast<int>(s.edges.size());
  const int fn = static_cast<int>(s.faces.size());

  if (s.shells.empty())
    return Problem::NoShell;
  if (vn == 0 || en == 0 || fn == 0)
    return Problem::EmptyShell;

  for (const Vertex& v : s.vertices) {
    if (!FinitePoint(v.p))
      return Problem::NonFiniteCoordinate;
  }

  const double scale = ModelScale(s);
  const double lenEps = 1e-9 * scale;
  const double areaEps = lenEps * lenEps;

  for (const Edge& e : s.edges) {
    if (e.v0 < 0 || e.v0 >= vn || e.v1 < 0 || e.v1 >= vn)
      return Problem::IndexOutOfRange;
    if (e.kind == CurveKind::Line) {
      if (ray3d::Length(ray3d::Sub(s.vertices[static_cast<std::size_t>(e.v1)].p,
                                   s.vertices[static_cast<std::size_t>(e.v0)].p)) <= lenEps)
        return Problem::DegenerateEdge;
    } else if (e.kind == CurveKind::Intersection) {
      // The curve is defined entirely by its two surfaces and its endpoints — check them here, not a
      // radius or sweep (an Intersection edge carries neither).
      if (e.isectSurfaces.size() != 2)
        return Problem::DegenerateEdge;
      const double onEps = 1e-7 * scale;
      for (const Surface& sf : e.isectSurfaces) {
        if (!AllFinite({sf.radius, sf.radius2, sf.height}) || !FinitePoint(sf.frame.origin))
          return Problem::NonFiniteCoordinate;
        for (const Vec3& p : {s.vertices[static_cast<std::size_t>(e.v0)].p,
                              s.vertices[static_cast<std::size_t>(e.v1)].p, e.frame.origin}) {
          if (!FinitePoint(p))
            return Problem::NonFiniteCoordinate;
          if (ray3d::Length(ray3d::Sub(p, ClosestPointOnSurface(sf, p))) > onEps)
            return Problem::DegenerateEdge;
        }
      }
      if (ray3d::Length(ray3d::Sub(s.vertices[static_cast<std::size_t>(e.v1)].p,
                                   s.vertices[static_cast<std::size_t>(e.v0)].p)) <= lenEps)
        return Problem::DegenerateEdge;
    } else {
      if (!AllFinite({e.radius, e.radius2, e.sweep}) || !FinitePoint(e.frame.origin))
        return Problem::NonFiniteCoordinate;
      if (!(e.radius > lenEps) || !(std::fabs(e.sweep) > 1e-9))
        return Problem::DegenerateEdge;
      if (e.kind == CurveKind::Ellipse && !(e.radius2 > lenEps))
        return Problem::DegenerateEdge;
    }
  }

  // Every face belongs to exactly one shell, and every shell has faces. A face nobody owns would
  // never be drawn and would still contribute to the volume, which is exactly the sort of quiet
  // disagreement this check exists to prevent.
  std::vector<int> faceShellCount(static_cast<std::size_t>(fn), 0);
  for (const Shell& sh : s.shells) {
    if (sh.faces.empty())
      return Problem::EmptyShell;
    for (int fi : sh.faces) {
      if (fi < 0 || fi >= fn)
        return Problem::IndexOutOfRange;
      ++faceShellCount[static_cast<std::size_t>(fi)];
    }
  }
  for (int c : faceShellCount) {
    if (c != 1)
      return Problem::IndexOutOfRange;
  }

  // Edge-use tally: manifold (twice) and orientable (once each way).
  std::vector<int> forwardUses(static_cast<std::size_t>(en), 0);
  std::vector<int> reverseUses(static_cast<std::size_t>(en), 0);
  std::vector<char> vertexUsed(static_cast<std::size_t>(vn), 0);

  for (const Face& f : s.faces) {
    if (f.loops.empty())
      return Problem::FaceHasNoLoop;
    for (const Loop& lp : f.loops) {
      if (lp.uses.empty())
        return Problem::EmptyLoop;
      for (const EdgeUse& u : lp.uses) {
        if (u.edge < 0 || u.edge >= en)
          return Problem::IndexOutOfRange;
        if (u.reversed)
          ++reverseUses[static_cast<std::size_t>(u.edge)];
        else
          ++forwardUses[static_cast<std::size_t>(u.edge)];
      }
      // Ring closure: each use ends where the next begins, and the last closes onto the first.
      const std::size_t n = lp.uses.size();
      for (std::size_t i = 0; i < n; ++i) {
        const Edge& a = s.edges[static_cast<std::size_t>(lp.uses[i].edge)];
        const Edge& b = s.edges[static_cast<std::size_t>(lp.uses[(i + 1) % n].edge)];
        const int aEnd = lp.uses[i].reversed ? a.v0 : a.v1;
        const int bStart = lp.uses[(i + 1) % n].reversed ? b.v1 : b.v0;
        if (aEnd != bStart)
          return Problem::LoopNotClosed;
      }
    }
    if (f.surface.kind == SurfaceKind::Plane) {
      if (std::fabs(PlaneFaceArea(s, f)) <= areaEps)
        return Problem::DegenerateFace;
    } else if (f.surface.kind == SurfaceKind::Nurbs) {
      // A freeform face is judged on its patch (the kernel's own validator is the truth) and on
      // having a non-empty parameter rectangle in both directions.
      if (nurbs::ValidatePatch(f.surface.patch) != nurbs::PatchProblem::Ok)
        return Problem::DegenerateFace;
      if (!(std::fabs(f.uEnd - f.uStart) > 1e-12) || !(std::fabs(f.vEnd - f.vStart) > 1e-12))
        return Problem::DegenerateFace;
    } else {
      if (!(std::fabs(f.uEnd - f.uStart) > 1e-12))
        return Problem::DegenerateFace;
      if (!(f.surface.radius > lenEps))
        return Problem::DegenerateFace;
      if ((f.surface.kind == SurfaceKind::Sphere || f.surface.kind == SurfaceKind::Torus) &&
          !(std::fabs(f.vEnd - f.vStart) > 1e-12))
        return Problem::DegenerateFace;
    }
  }

  for (int i = 0; i < en; ++i) {
    const int total = forwardUses[static_cast<std::size_t>(i)] + reverseUses[static_cast<std::size_t>(i)];
    if (total != 2)
      return Problem::EdgeNotUsedTwice;
    if (forwardUses[static_cast<std::size_t>(i)] != 1)
      return Problem::EdgeOrientationInconsistent;
    vertexUsed[static_cast<std::size_t>(s.edges[static_cast<std::size_t>(i)].v0)] = 1;
    vertexUsed[static_cast<std::size_t>(s.edges[static_cast<std::size_t>(i)].v1)] = 1;
  }
  for (char c : vertexUsed) {
    if (!c)
      return Problem::UnusedVertex;
  }

  // Finally, the two geometric questions the topology cannot answer.
  //
  // (1) Does the surface close *geometrically*? Everything above checks that the faces are stitched
  //     together correctly, but a face's parametric span is carried alongside its loop, and nothing
  //     so far compares the two: a cylinder face spanning a quarter turn while its boundary runs a
  //     half turn is topologically flawless and geometrically a hole. The test is that the volume
  //     integral is independent of the point it is taken about, which holds for a closed surface
  //     (the integral of n dA over it is zero) and fails for anything else. The offset below is
  //     deliberately generic — nonzero along all three axes, and irrational relative to the model —
  //     so no face's own frame can happen to cancel it.
  // (2) Does it enclose a positive volume, i.e. do the faces point outward rather than inward?
  const Vec3 q = ReferencePoint(s);
  bool finite = true;
  const double volume = VolumeAbout(s, q, &finite);
  if (!finite)
    return Problem::NonFiniteCoordinate;

  // A solid with a procedural `Intersection` edge has a face (or two) whose integral is numerical,
  // not closed form (ADR-045 (b) amendment, D-2026-09-02-i), so the point-invariance residual is a
  // quadrature error rather than zero. Relaxed to `1e-5 * scale^3` for those solids — still three
  // orders inside REQ-101's ±0.01 ft on a model-scale part; every analytic solid keeps `1e-8`.
  bool hasNumericFace = false;
  for (const Edge& e : s.edges)
    if (e.kind == CurveKind::Intersection)
      hasNumericFace = true;
  for (const Face& f : s.faces)
    if (f.surface.kind == SurfaceKind::Nurbs)  // REQ-315: a NURBS face is integrated by quadrature too
      hasNumericFace = true;
  const double closeTol = (hasNumericFace ? 1e-5 : 1e-8) * scale * scale * scale;

  const Vec3 probe = ray3d::Add(q, Vec3{scale, 0.7 * scale, -1.3 * scale});
  const double probeVolume = VolumeAbout(s, probe, nullptr);
  if (!std::isfinite(probeVolume) || std::fabs(volume - probeVolume) > closeTol)
    return Problem::NotClosed;

  if (!(volume > areaEps * lenEps))
    return Problem::NotClosed;

  return Problem::Ok;
}

// ---------------------------------------------------------------------------------------------
// Mass properties.
// ---------------------------------------------------------------------------------------------

bool SelfIntersects(const Solid& s) {
  for (const Face& f : s.faces) {
    if (f.surface.kind == SurfaceKind::Torus && f.surface.radius2 >= f.surface.radius)
      return true;
  }
  return false;
}

MassProperties ComputeMassProperties(const Solid& s) {
  MassProperties mp;
  if (Validate(s) != Problem::Ok)
    return mp;
  // A self-intersecting solid draws fine and its integrals still evaluate — to a number that is not
  // its volume, because the surface encloses part of space twice. Reporting that number would be the
  // silent-wrong-answer failure REQ-201 exists to prevent, so the answer is "unavailable" instead.
  if (SelfIntersects(s))
    return mp;

  const Vec3 q = ReferencePoint(s);
  double area = 0.0;
  for (const Face& f : s.faces)
    area += std::fabs(IntegrateFace(s, f, q).area);
  mp.valid = true;
  mp.volume = VolumeAbout(s, q, nullptr);
  mp.surfaceArea = area;
  return mp;
}

// ---------------------------------------------------------------------------------------------
// Bounds.
// ---------------------------------------------------------------------------------------------

namespace {

void Expand(Bounds* b, const Vec3& p) {
  if (!b->valid) {
    b->valid = true;
    b->mn = p;
    b->mx = p;
    return;
  }
  b->mn.x = std::min(b->mn.x, p.x);
  b->mn.y = std::min(b->mn.y, p.y);
  b->mn.z = std::min(b->mn.z, p.z);
  b->mx.x = std::max(b->mx.x, p.x);
  b->mx.y = std::max(b->mx.y, p.y);
  b->mx.z = std::max(b->mx.z, p.z);
}

/// The exact world bounds of a full circle: along each world axis a circle of radius r whose plane
/// has unit normal n reaches r * sqrt(1 - n_axis^2) from its centre.
void ExpandCircle(Bounds* b, const Vec3& centre, const Vec3& normal, double r) {
  const Vec3 n = ray3d::Normalize(normal);
  const Vec3 ext{r * std::sqrt(std::max(0.0, 1.0 - n.x * n.x)),
                 r * std::sqrt(std::max(0.0, 1.0 - n.y * n.y)),
                 r * std::sqrt(std::max(0.0, 1.0 - n.z * n.z))};
  Expand(b, ray3d::Sub(centre, ext));
  Expand(b, ray3d::Add(centre, ext));
}

} // namespace

Bounds ComputeBounds(const Solid& s) {
  Bounds b;
  for (const Vertex& v : s.vertices)
    Expand(&b, v.p);
  for (const Edge& e : s.edges) {
    if (e.kind == CurveKind::Arc)
      ExpandCircle(&b, e.frame.origin, e.frame.zAxis, e.radius);
    else if (e.kind == CurveKind::Ellipse)  // conservative: the semi-major circle bounds the ellipse
      ExpandCircle(&b, e.frame.origin, e.frame.zAxis, e.radius);
    else if (e.kind == CurveKind::Intersection)
      for (const Vec3& p : MarchIntersectionCurve(s, e, 64))
        Expand(&b, p);
  }
  for (const Face& f : s.faces) {
    const Surface& sf = f.surface;
    switch (sf.kind) {
    case SurfaceKind::Plane:
      break;  // already covered by its edges
    case SurfaceKind::Cylinder:
    case SurfaceKind::Cone: {
      const Vec3 top = ray3d::Add(sf.frame.origin, ray3d::Scale(sf.frame.zAxis, sf.height));
      ExpandCircle(&b, sf.frame.origin, sf.frame.zAxis, sf.radius);
      ExpandCircle(&b, top, sf.frame.zAxis, sf.radius2);
      break;
    }
    case SurfaceKind::Sphere: {
      const Vec3 ext{sf.radius, sf.radius, sf.radius};
      Expand(&b, ray3d::Sub(sf.frame.origin, ext));
      Expand(&b, ray3d::Add(sf.frame.origin, ext));
      break;
    }
    case SurfaceKind::Torus: {
      const double reach = sf.radius + sf.radius2;
      const Vec3 ext{reach, reach, reach};
      Expand(&b, ray3d::Sub(sf.frame.origin, ext));
      Expand(&b, ray3d::Add(sf.frame.origin, ext));
      break;
    }
    case SurfaceKind::Nurbs:
      for (const Vec3& c : sf.patch.ctrl)  // the convex hull of the control net contains the patch
        Expand(&b, c);
      break;
    }
  }
  return b;
}

// ---------------------------------------------------------------------------------------------
// Tessellation.
// ---------------------------------------------------------------------------------------------

namespace {

/// Signed area of a 2D ring (CCW positive).
[[nodiscard]] double SignedArea2D(const std::vector<ucs::Point2D>& p) {
  double a = 0.0;
  const std::size_t n = p.size();
  for (std::size_t i = 0; i < n; ++i) {
    const ucs::Point2D& u = p[i];
    const ucs::Point2D& v = p[(i + 1) % n];
    a += u.x * v.y - v.x * u.y;
  }
  return 0.5 * a;
}

/// True when \p p turns the same way at every corner — a convex polygon, for which the centroid fan
/// below is exact. A straight-through vertex does not count against it.
[[nodiscard]] bool Polygon2DIsConvex(const std::vector<ucs::Point2D>& p) {
  const std::size_t n = p.size();
  if (n < 3)
    return false;
  double sign = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const ucs::Point2D& a = p[i];
    const ucs::Point2D& b = p[(i + 1) % n];
    const ucs::Point2D& c = p[(i + 2) % n];
    const double cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
    if (std::fabs(cross) < 1e-14)
      continue;
    if (sign == 0.0)
      sign = cross;
    else if ((cross > 0.0) != (sign > 0.0))
      return false;
  }
  return true;
}

[[nodiscard]] bool PointInTriangle2D(const ucs::Point2D& p, const ucs::Point2D& a, const ucs::Point2D& b,
                                     const ucs::Point2D& c) {
  const double d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
  const double d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
  const double d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
  const bool hasNeg = d1 < 0.0 || d2 < 0.0 || d3 < 0.0;
  const bool hasPos = d1 > 0.0 || d2 > 0.0 || d3 > 0.0;
  return !(hasNeg && hasPos);
}

/// Ear-clip a **CCW** 2D ring into triangles, each a triple of indices into \p ring. O(n^2), which
/// is ample for a profile cap. A profile is the first thing to hand \ref Tessellate a non-convex
/// plane face — ADR-045 named that "Phase 4's problem, when a boolean first produces a face that
/// needs one", and an extruded L-shape needs it now.
void EarClip(const std::vector<ucs::Point2D>& ring, std::vector<std::array<int, 3>>* tris) {
  const int n = static_cast<int>(ring.size());
  std::vector<int> idx(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i)
    idx[static_cast<std::size_t>(i)] = i;

  int guard = 0;
  while (idx.size() > 3 && guard++ < 4 * n) {
    const int m = static_cast<int>(idx.size());
    bool clipped = false;
    for (int i = 0; i < m; ++i) {
      const int i0 = idx[static_cast<std::size_t>((i + m - 1) % m)];
      const int i1 = idx[static_cast<std::size_t>(i)];
      const int i2 = idx[static_cast<std::size_t>((i + 1) % m)];
      const ucs::Point2D& a = ring[static_cast<std::size_t>(i0)];
      const ucs::Point2D& b = ring[static_cast<std::size_t>(i1)];
      const ucs::Point2D& c = ring[static_cast<std::size_t>(i2)];
      if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 0.0)
        continue;  // reflex or straight — not an ear tip
      bool contains = false;
      for (int j = 0; j < m && !contains; ++j) {
        const int ij = idx[static_cast<std::size_t>(j)];
        if (ij == i0 || ij == i1 || ij == i2)
          continue;
        contains = PointInTriangle2D(ring[static_cast<std::size_t>(ij)], a, b, c);
      }
      if (contains)
        continue;
      tris->push_back({i0, i1, i2});
      idx.erase(idx.begin() + i);
      clipped = true;
      break;
    }
    if (!clipped)
      break;  // no ear found (degenerate input) — fan whatever is left rather than loop
  }
  for (std::size_t i = 1; i + 1 < idx.size(); ++i)
    tris->push_back({idx[0], idx[i], idx[i + 1]});
}

} // namespace

bool Tessellate(const Solid& s, double chordTolerance, Tessellation* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (!std::isfinite(chordTolerance) || !(chordTolerance > 0.0))
    return Fail(Problem::NonPositiveTolerance, outWhy);
  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);

  Tessellation mesh;
  MeshBuilder mb{&mesh};

  for (std::size_t fi = 0; fi < s.faces.size(); ++fi) {
    const Face& f = s.faces[fi];
    mb.face = static_cast<int>(fi);
    const Surface& sf = f.surface;
    switch (sf.kind) {
    case SurfaceKind::Plane: {
      if (f.loops.size() > 2)
        return Fail(Problem::PlaneFaceNotSimple, outWhy);
      // Walk each loop into a polyline. Arc edges are subdivided by the same chord rule the curved
      // faces use, so the cap and the wall it meets do not disagree.
      auto sampleLoop = [&](const Loop& lp) {
        std::vector<Vec3> r;
        for (const EdgeUse& u : lp.uses) {
          const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
          const int segs =
              SegmentsForEdge(e, chordTolerance);
          for (int i = 0; i < segs; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(segs);
            r.push_back(EdgePointAt(s, e, u.reversed ? 1.0 - t : t));
          }
        }
        return r;
      };
      const Vec3 n = sf.frame.zAxis;

      if (f.loops.size() == 2) {
        // An annular face (a bored boss face, a stepped-stack ring — REQ-314 B1): strip it between
        // the outer and inner loop by angle about the hole centre, which is convex-outer, star-shaped
        // territory — all B1 produces. Both loops are sampled by the shared chord rule.
        std::vector<Vec3> outer3 = sampleLoop(f.loops[0]);
        std::vector<Vec3> inner3 = sampleLoop(f.loops[1]);
        if (outer3.size() < 3 || inner3.size() < 3)
          return Fail(Problem::DegenerateFace, outWhy);
        std::vector<ucs::Point2D> outer2;
        std::vector<ucs::Point2D> inner2;
        for (const Vec3& p : outer3)
          outer2.push_back(ucs::WorldToPlane(sf.frame, p));
        for (const Vec3& p : inner3)
          inner2.push_back(ucs::WorldToPlane(sf.frame, p));
        ucs::Point2D hc{0.0, 0.0};
        for (const ucs::Point2D& p : inner2) {
          hc.x += p.x / static_cast<double>(inner2.size());
          hc.y += p.y / static_cast<double>(inner2.size());
        }
        // The far intersection of the ray hc + t*dir (t > 0) with a closed 2D polyline.
        auto rayHit = [&](const std::vector<ucs::Point2D>& poly, double dx, double dy) {
          double bestT = 0.0;
          ucs::Point2D hit{hc.x + dx, hc.y + dy};
          for (std::size_t i = 0; i < poly.size(); ++i) {
            const ucs::Point2D& a = poly[i];
            const ucs::Point2D& b = poly[(i + 1) % poly.size()];
            const double ex = b.x - a.x;
            const double ey = b.y - a.y;
            const double den = dx * ey - dy * ex;
            if (std::fabs(den) < 1e-15)
              continue;
            const double t = ((a.x - hc.x) * ey - (a.y - hc.y) * ex) / den;
            const double s2 = ((a.x - hc.x) * dy - (a.y - hc.y) * dx) / den;
            if (t > 1e-12 && s2 >= -1e-9 && s2 <= 1.0 + 1e-9 && t > bestT) {
              bestT = t;
              hit = ucs::Point2D{hc.x + dx * t, hc.y + dy * t};
            }
          }
          return hit;
        };
        std::vector<double> angs;
        for (const ucs::Point2D& p : outer2)
          angs.push_back(std::atan2(p.y - hc.y, p.x - hc.x));
        for (const ucs::Point2D& p : inner2)
          angs.push_back(std::atan2(p.y - hc.y, p.x - hc.x));
        std::sort(angs.begin(), angs.end());
        angs.erase(std::unique(angs.begin(), angs.end(),
                               [](double u, double v) { return std::fabs(u - v) < 1e-7; }),
                   angs.end());
        const std::size_t m = angs.size();
        auto backToWorld = [&](const ucs::Point2D& q) { return ucs::PlaneToWorld(sf.frame, q); };
        for (std::size_t k = 0; k < m; ++k) {
          const double a0 = angs[k];
          const double a1 = angs[(k + 1) % m];
          const double d0x = std::cos(a0);
          const double d0y = std::sin(a0);
          const double d1x = std::cos(a1);
          const double d1y = std::sin(a1);
          const Vec3 oi = backToWorld(rayHit(outer2, d0x, d0y));
          const Vec3 oj = backToWorld(rayHit(outer2, d1x, d1y));
          const Vec3 ii = backToWorld(rayHit(inner2, d0x, d0y));
          const Vec3 ij = backToWorld(rayHit(inner2, d1x, d1y));
          const std::uint32_t voi = mb.Push(oi, n);
          const std::uint32_t voj = mb.Push(oj, n);
          const std::uint32_t vii = mb.Push(ii, n);
          const std::uint32_t vij = mb.Push(ij, n);
          // Orient the first quad against n, then keep that winding for the ring.
          const Vec3 g = ray3d::Cross(ray3d::Sub(oj, oi), ray3d::Sub(ii, oi));
          if (ray3d::Dot(g, n) >= 0.0) {
            mb.Tri(voi, voj, vii);
            mb.Tri(voj, vij, vii);
          } else {
            mb.Tri(voi, vii, voj);
            mb.Tri(voj, vii, vij);
          }
        }
        break;
      }

      std::vector<Vec3> ring = sampleLoop(f.loops[0]);
      if (ring.size() < 3)
        return Fail(Problem::DegenerateFace, outWhy);
      std::vector<ucs::Point2D> ring2;
      ring2.reserve(ring.size());
      for (const Vec3& p : ring)
        ring2.push_back(ucs::WorldToPlane(sf.frame, p));

      if (Polygon2DIsConvex(ring2)) {
        // The centroid fan — unchanged from REQ-313, so every primitive tessellates as it did.
        Vec3 centroid{};
        for (const Vec3& p : ring)
          centroid = ray3d::Add(centroid, p);
        centroid = ray3d::Scale(centroid, 1.0 / static_cast<double>(ring.size()));
        const std::uint32_t c = mb.Push(centroid, n);
        std::vector<std::uint32_t> idx;
        idx.reserve(ring.size());
        for (const Vec3& p : ring)
          idx.push_back(mb.Push(p, n));
        for (std::size_t i = 0; i < idx.size(); ++i)
          mb.Tri(c, idx[i], idx[(i + 1) % idx.size()]);
      } else {
        // Non-convex cap (REQ-314): ear-clip. The clipper wants a CCW ring; the 2D points are the
        // same either way, so a triangle it returns is still CCW about `n` once mapped back.
        const bool ccw = SignedArea2D(ring2) >= 0.0;
        std::vector<int> order(ring.size());
        for (std::size_t i = 0; i < ring.size(); ++i)
          order[i] = static_cast<int>(ccw ? i : ring.size() - 1 - i);
        std::vector<ucs::Point2D> ccwRing;
        ccwRing.reserve(ring.size());
        for (int o : order)
          ccwRing.push_back(ring2[static_cast<std::size_t>(o)]);
        std::vector<std::array<int, 3>> tris;
        EarClip(ccwRing, &tris);
        std::vector<std::uint32_t> idx;
        idx.reserve(ring.size());
        for (const Vec3& p : ring)
          idx.push_back(mb.Push(p, n));
        for (const std::array<int, 3>& t : tris)
          mb.Tri(idx[static_cast<std::size_t>(order[static_cast<std::size_t>(t[0])])],
                 idx[static_cast<std::size_t>(order[static_cast<std::size_t>(t[1])])],
                 idx[static_cast<std::size_t>(order[static_cast<std::size_t>(t[2])])]);
      }
      break;
    }
    case SurfaceKind::Cylinder:
    case SurfaceKind::Cone: {
      const double r0 = sf.radius;
      const double r1 = sf.kind == SurfaceKind::Cylinder ? sf.radius : sf.radius2;
      // A holed band (a branch mouth bored out of an otherwise-full cylinder wall — REQ-314 B2b-2):
      // the Intersection edge is in an INNER loop, so the face is `[zFull0, zFull1]` minus a bite
      // `[bz0(u), bz1(u)]`. Drawn as a lower and an upper sub-strip (both collapse where there is no
      // bite).
      if (sf.kind == SurfaceKind::Cylinder && f.loops.size() > 1 &&
          !LoopHasIntersectionEdge(s, f.loops.front()) && FaceLoopHasIntersectionEdge(s, f)) {
        const IsectStrip hStrip = MakeIsectStrip(s, f);
        double zF0 = 1e300;
        double zF1 = -1e300;
        for (const EdgeUse& u : f.loops.front().uses)
          for (const int vi : {s.edges[static_cast<std::size_t>(u.edge)].v0,
                               s.edges[static_cast<std::size_t>(u.edge)].v1}) {
            const double z = ucs::WorldToUcs(sf.frame, s.vertices[static_cast<std::size_t>(vi)].p).z;
            zF0 = std::min(zF0, z);
            zF1 = std::max(zF1, z);
          }
        const int hnu = std::clamp(
            SegmentsForArc(r0, f.uEnd - f.uStart, 0.25 * chordTolerance), 32, 320);
        std::vector<std::uint32_t> rA(static_cast<std::size_t>(hnu) + 1);
        std::vector<std::uint32_t> rB(static_cast<std::size_t>(hnu) + 1);
        std::vector<std::uint32_t> rC(static_cast<std::size_t>(hnu) + 1);
        std::vector<std::uint32_t> rD(static_cast<std::size_t>(hnu) + 1);
        for (int i = 0; i <= hnu; ++i) {
          const double t = f.uStart + (f.uEnd - f.uStart) * i / static_cast<double>(hnu);
          const Vec3 nn = ConicalNormal(sf, r0, r0, t);
          double b0 = zF1;
          double b1 = zF1;
          if (hStrip.valid())
            (void)IsectStripAt(hStrip, t, &b0, &b1);
          b0 = std::clamp(b0, zF0, zF1);
          b1 = std::clamp(b1, zF0, zF1);
          rA[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r0, t, zF0), nn);
          rB[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r0, t, b0), nn);
          rC[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r0, t, b1), nn);
          rD[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r0, t, zF1), nn);
        }
        for (int i = 0; i < hnu; ++i) {
          const auto a = static_cast<std::size_t>(i);
          const auto b = static_cast<std::size_t>(i + 1);
          mb.Tri(rA[a], rA[b], rB[b]);
          mb.Tri(rA[a], rB[b], rB[a]);
          mb.Tri(rC[a], rC[b], rD[b]);
          mb.Tri(rC[a], rD[b], rD[a]);
        }
        break;
      }
      const bool isect = sf.kind == SurfaceKind::Cylinder && FaceLoopHasIntersectionEdge(s, f);
      const int nu = isect ? std::clamp(SegmentsForArc(std::max(r0, r1), f.uEnd - f.uStart,
                                                       0.25 * chordTolerance),
                                       24, 256)
                           : SegmentsForArc(std::max(r0, r1), f.uEnd - f.uStart, chordTolerance);
      CylinderCut cc;
      const bool cut = sf.kind == SurfaceKind::Cylinder && !isect && CylinderCutZExtent(s, f, &cc);
      const IsectStrip strip = isect ? MakeIsectStrip(s, f) : IsectStrip{};
      auto bound = [](const double c[3], double u) {
        return c[0] + c[1] * std::cos(u) + c[2] * std::sin(u);
      };
      std::vector<std::uint32_t> lower(static_cast<std::size_t>(nu) + 1);
      std::vector<std::uint32_t> upper(static_cast<std::size_t>(nu) + 1);
      for (int i = 0; i <= nu; ++i) {
        const double t = f.uStart + (f.uEnd - f.uStart) * static_cast<double>(i) / static_cast<double>(nu);
        const Vec3 n = ConicalNormal(sf, r0, r1, t);
        double zA = cut ? bound(cc.lo, t) : 0.0;
        double zB = cut ? bound(cc.hi, t) : sf.height;
        if (isect && strip.valid()) {
          if (!IsectStripAt(strip, t, &zA, &zB)) {
            const double zc = 0.5 * (strip.zSearchLo + strip.zSearchHi);
            zA = zB = zc;  // the strip has pinched — a degenerate sliver at this end of the lens
          }
        }
        lower[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r1, t, zA), n);
        upper[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r1, t, zB), n);
      }
      for (int i = 0; i < nu; ++i) {
        const std::size_t a = static_cast<std::size_t>(i);
        const std::size_t b = static_cast<std::size_t>(i + 1);
        if (sf.inward) {  // REQ-314 B2a: a bore wall — reverse winding to match the flipped normal
          mb.Tri(lower[a], upper[b], lower[b]);
          mb.Tri(lower[a], upper[a], upper[b]);
        } else {
          mb.Tri(lower[a], lower[b], upper[b]);
          mb.Tri(lower[a], upper[b], upper[a]);
        }
      }
      break;
    }
    case SurfaceKind::Sphere:
    case SurfaceKind::Torus: {
      const bool sphere = sf.kind == SurfaceKind::Sphere;
      const double uRadius = sphere ? sf.radius : sf.radius + sf.radius2;
      const double vRadius = sphere ? sf.radius : sf.radius2;
      // A sphere patch bounded by a procedural Intersection edge (REQ-314 B2b-2, issue #242): the
      // latitude band is a function of longitude, found the same way IntegrateSphereFaceNumeric does.
      const bool isectSphere = sphere && FaceLoopHasIntersectionEdge(s, f);
      const SphereIsectStrip sStrip = isectSphere ? MakeSphereIsectStrip(s, f) : SphereIsectStrip{};
      // A hemisphere (full pole-to-pole v span) carrying a procedural edge is the SUBTRACT / UNION
      // kept sphere (issue #242): draw its three kept latitude bands — below the lower lens bite,
      // between the two bites, above the upper bite — the complement of what SphereStripsAt returns.
      const bool sphereComplement = isectSphere && sStrip.valid() &&
                                    f.vStart <= -kHalfPi + 1e-9 && f.vEnd >= kHalfPi - 1e-9;
      if (sphereComplement) {
        // Draw the full hemisphere grid and drop any quad whose centre falls inside a lens bite.
        // The lens boundary lands ragged at grid resolution — well inside REQ-101 on a model part —
        // but every emitted quad keeps the clean outward grid winding (no pinch slivers).
        const int cnu =
            std::clamp(SegmentsForArc(uRadius, std::fabs(f.uEnd - f.uStart), 0.25 * chordTolerance),
                       48, 320);
        const int cnv = std::clamp(SegmentsForArc(vRadius, kPi, 0.5 * chordTolerance), 24, 200);
        std::vector<std::pair<double, double>> strips;
        auto inBite = [&](double u, double v) {
          SphereStripsAt(sStrip, u, &strips);
          for (const auto& [a, b] : strips)
            if (v > a && v < b)
              return true;
          return false;
        };
        std::vector<std::uint32_t> row0(static_cast<std::size_t>(cnv) + 1);
        std::vector<std::uint32_t> row1(static_cast<std::size_t>(cnv) + 1);
        auto fillRow = [&](double u, std::vector<std::uint32_t>& row) {
          for (int j = 0; j <= cnv; ++j) {
            const double v = f.vStart + (f.vEnd - f.vStart) * static_cast<double>(j) / cnv;
            row[static_cast<std::size_t>(j)] = mb.Push(SphericalPoint(sf, u, v), SphericalNormal(sf, u, v));
          }
        };
        double uPrev = f.uStart;
        fillRow(uPrev, row0);
        for (int i = 1; i <= cnu; ++i) {
          const double uCur = f.uStart + (f.uEnd - f.uStart) * static_cast<double>(i) / cnu;
          fillRow(uCur, row1);
          const double uMid = 0.5 * (uPrev + uCur);
          for (int j = 0; j < cnv; ++j) {
            const double vMid =
                f.vStart + (f.vEnd - f.vStart) * (static_cast<double>(j) + 0.5) / cnv;
            if (inBite(uMid, vMid))
              continue;
            const auto a = static_cast<std::size_t>(j);
            mb.Tri(row0[a], row1[a], row1[a + 1]);
            mb.Tri(row0[a], row1[a + 1], row0[a + 1]);
          }
          row0.swap(row1);
          uPrev = uCur;
        }
        break;
      }
      const int nu = isectSphere
                         ? std::clamp(SegmentsForArc(uRadius, f.uEnd - f.uStart, 0.25 * chordTolerance),
                                      24, 256)
                         : SegmentsForArc(uRadius, f.uEnd - f.uStart, chordTolerance);
      const int nv = SegmentsForArc(vRadius, f.vEnd - f.vStart, chordTolerance);
      std::vector<std::uint32_t> grid(static_cast<std::size_t>(nu + 1) * static_cast<std::size_t>(nv + 1));
      for (int i = 0; i <= nu; ++i) {
        const double t = f.uStart + (f.uEnd - f.uStart) * static_cast<double>(i) / static_cast<double>(nu);
        double vA = f.vStart;
        double vB = f.vEnd;
        if (isectSphere && sStrip.valid() && !SphereStripAt(sStrip, t, &vA, &vB))
          vA = vB = 0.5 * (sStrip.vSearchLo + sStrip.vSearchHi);  // strip pinched — a sliver
        for (int j = 0; j <= nv; ++j) {
          const double v = vA + (vB - vA) * static_cast<double>(j) / static_cast<double>(nv);
          const Vec3 p = sphere ? SphericalPoint(sf, t, v) : ToroidalPoint(sf, t, v);
          const Vec3 n = sphere ? SphericalNormal(sf, t, v) : ToroidalNormal(sf, t, v);
          grid[static_cast<std::size_t>(i) * static_cast<std::size_t>(nv + 1) +
               static_cast<std::size_t>(j)] = mb.Push(p, n);
        }
      }
      const std::size_t stride = static_cast<std::size_t>(nv + 1);
      for (int i = 0; i < nu; ++i) {
        for (int j = 0; j < nv; ++j) {
          const std::size_t a = static_cast<std::size_t>(i) * stride + static_cast<std::size_t>(j);
          const std::size_t b = a + stride;
          if (sf.inward) {  // REQ-314 B2a: reverse winding to match the flipped normal
            mb.Tri(grid[a], grid[b + 1], grid[b]);
            mb.Tri(grid[a], grid[a + 1], grid[b + 1]);
          } else {
            mb.Tri(grid[a], grid[b], grid[b + 1]);
            mb.Tri(grid[a], grid[b + 1], grid[a + 1]);
          }
        }
      }
      break;
    }
    case SurfaceKind::Nurbs: {
      // A uniform (u, v) grid over the face's parameter rectangle, with the analytic patch normal at
      // every node so the shading matches the isolines the same evaluator draws. A ruled straight
      // span is exact at one quad; a rational (arc) span gets a curvature-driven division count.
      const nurbs::Patch& patch = sf.patch;
      const double uLo = std::min(f.uStart, f.uEnd);
      const double uHi = std::max(f.uStart, f.uEnd);
      const double vLo = std::min(f.vStart, f.vEnd);
      const double vHi = std::max(f.vStart, f.vEnd);
      double netStep = 0.0;
      for (std::size_t k = 0; k + 1 < patch.ctrl.size(); ++k)
        netStep = std::max(netStep, ray3d::Length(ray3d::Sub(patch.ctrl[k + 1], patch.ctrl[k])));
      // A patch is flat enough for one quad only when it is bilinear AND its control net is planar —
      // a *twisted* ruled patch (a swept-with-twist side face) is degree 1 in both directions but
      // genuinely curved.
      bool curved = patch.degU > 1 || patch.degV > 1;
      if (!curved && patch.ctrl.size() >= 4) {
        const Vec3 e1 = ray3d::Sub(patch.ctrl[1], patch.ctrl[0]);
        const Vec3 e2 = ray3d::Sub(patch.ctrl[static_cast<std::size_t>(patch.nu)], patch.ctrl[0]);
        Vec3 nrm = ray3d::Cross(e1, e2);
        const double nl = ray3d::Length(nrm);
        if (nl > 1e-12) {
          nrm = ray3d::Scale(nrm, 1.0 / nl);
          for (const Vec3& c : patch.ctrl)
            if (std::fabs(ray3d::Dot(ray3d::Sub(c, patch.ctrl[0]), nrm)) > chordTolerance) {
              curved = true;
              break;
            }
        }
      }
      const int n = curved ? std::clamp(SegmentsForArc(std::max(netStep, 1e-9), kHalfPi, chordTolerance),
                                        8, 128)
                           : 1;
      std::vector<std::uint32_t> grid(static_cast<std::size_t>(n + 1) * static_cast<std::size_t>(n + 1));
      for (int i = 0; i <= n; ++i)
        for (int j = 0; j <= n; ++j) {
          const double u = uLo + (uHi - uLo) * static_cast<double>(i) / static_cast<double>(n);
          const double v = vLo + (vHi - vLo) * static_cast<double>(j) / static_cast<double>(n);
          const nurbs::SurfacePoint sp = nurbs::EvaluateWithDerivs(patch, u, v);
          Vec3 nrm = sp.normal;
          if (!(ray3d::Dot(nrm, nrm) > 0.25))
            nrm = Vec3{0.0, 0.0, 1.0};  // a collapsed edge (a pole) — a loft patch has none
          if (sf.inward)
            nrm = ray3d::Scale(nrm, -1.0);
          grid[static_cast<std::size_t>(i) * static_cast<std::size_t>(n + 1) +
               static_cast<std::size_t>(j)] = mb.Push(sp.p, nrm);
        }
      const std::size_t stride = static_cast<std::size_t>(n + 1);
      for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
          const std::size_t a = static_cast<std::size_t>(i) * stride + static_cast<std::size_t>(j);
          const std::size_t b = a + stride;
          if (sf.inward) {
            mb.Tri(grid[a], grid[b + 1], grid[b]);
            mb.Tri(grid[a], grid[a + 1], grid[b + 1]);
          } else {
            mb.Tri(grid[a], grid[b], grid[b + 1]);
            mb.Tri(grid[a], grid[b + 1], grid[a + 1]);
          }
        }
      break;
    }
    }
  }

  *out = std::move(mesh);
  return Succeed(outWhy);
}


namespace {

/// A point on a curved face at parameters (u, v), in world.
///
/// One evaluator for every surface kind, so an isoline and the shaded triangles beside it cannot
/// disagree about where the surface is. `v` is ignored for the ruled kinds, where the second
/// parameter is a height rather than an angle.
[[nodiscard]] Vec3 SurfacePointAt(const Surface& sf, double u, double v) {
  switch (sf.kind) {
  case SurfaceKind::Plane:
    return sf.frame.origin;
  case SurfaceKind::Cylinder:
    return ConicalPoint(sf, sf.radius, sf.radius, u, v);
  case SurfaceKind::Cone:
    return ConicalPoint(sf, sf.radius, sf.radius2, u, v);
  case SurfaceKind::Sphere:
    return SphericalPoint(sf, u, v);
  case SurfaceKind::Torus:
    return ToroidalPoint(sf, u, v);
  case SurfaceKind::Nurbs:
    return nurbs::Evaluate(sf.patch, u, v);
  }
  return sf.frame.origin;
}

/// Walk one iso-curve and emit it as `GL_LINES` segments.
///
/// \p fixedIsU says which parameter is held constant: the curve runs along the other one.
void AppendIsoCurve(const Surface& sf, bool fixedIsU, double fixed, double from, double to, int steps,
                    std::vector<double>* out) {
  if (steps < 1)
    return;
  Vec3 prev = fixedIsU ? SurfacePointAt(sf, fixed, from) : SurfacePointAt(sf, from, fixed);
  for (int i = 1; i <= steps; ++i) {
    const double t = from + (to - from) * static_cast<double>(i) / static_cast<double>(steps);
    const Vec3 cur = fixedIsU ? SurfacePointAt(sf, fixed, t) : SurfacePointAt(sf, t, fixed);
    out->push_back(prev.x);
    out->push_back(prev.y);
    out->push_back(prev.z);
    out->push_back(cur.x);
    out->push_back(cur.y);
    out->push_back(cur.z);
    prev = cur;
  }
}

/// The angles of a global grid of \p count divisions of a full turn that fall STRICTLY inside
/// (\p lo, \p hi).
///
/// Strictly, so an isoline never lands on a seam and doubles an edge that is already drawn. Global,
/// so the lines are evenly spaced around the whole solid rather than around each face — the
/// difference is visible the moment a solid is split into two half-faces, which every curved
/// primitive here is.
void GridAnglesInside(int count, double lo, double hi, std::vector<double>* out) {
  out->clear();
  if (count < 1)
    return;
  const double step = kTwoPi / static_cast<double>(count);
  // Walk a window wide enough to cover any face span, including a full turn.
  const int first = static_cast<int>(std::floor(lo / step)) - 1;
  const int last = static_cast<int>(std::ceil(hi / step)) + 1;
  const double eps = 1e-9;
  for (int k = first; k <= last; ++k) {
    const double a = static_cast<double>(k) * step;
    if (a > lo + eps && a < hi - eps)
      out->push_back(a);
  }
}

} // namespace

bool TessellateIsolines(const Solid& s, int isolineCount, double chordTolerance, std::vector<double>* out,
                        Problem* outWhy) {
  if (!out)
    return false;
  if (!std::isfinite(chordTolerance) || !(chordTolerance > 0.0))
    return Fail(Problem::NonPositiveTolerance, outWhy);
  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);

  std::vector<double> segs;
  if (isolineCount < 1) {
    *out = std::move(segs);  // zero is a legal setting: it means "edges only"
    return Succeed(outWhy);
  }

  std::vector<double> angles;
  for (const Face& f : s.faces) {
    const Surface& sf = f.surface;
    if (sf.kind == SurfaceKind::Plane)
      continue;  // flat: its boundary already says everything

    if (sf.kind == SurfaceKind::Nurbs) {
      const nurbs::Patch& patch = sf.patch;
      if (patch.degU == 1 && patch.degV == 1 && patch.ctrl.size() >= 4) {
        // A planar bilinear span is flat — its boundary already says everything. A twisted one is not.
        const Vec3 e1 = ray3d::Sub(patch.ctrl[1], patch.ctrl[0]);
        const Vec3 e2 = ray3d::Sub(patch.ctrl[static_cast<std::size_t>(patch.nu)], patch.ctrl[0]);
        Vec3 nrm = ray3d::Cross(e1, e2);
        const double nl = ray3d::Length(nrm);
        bool planar = true;
        if (nl > 1e-12) {
          nrm = ray3d::Scale(nrm, 1.0 / nl);
          for (const Vec3& c : patch.ctrl)
            if (std::fabs(ray3d::Dot(ray3d::Sub(c, patch.ctrl[0]), nrm)) > chordTolerance) {
              planar = false;
              break;
            }
        }
        if (planar)
          continue;
      }
      // Evenly spaced interior parameter lines both ways — the patch domain is not a full turn, so
      // the global-angle grid the analytic kinds use does not apply. Same evaluator as the shaded
      // triangles (SurfacePointAt), so the wireframe cannot float off the shading.
      const double uA = std::min(f.uStart, f.uEnd);
      const double uB = std::max(f.uStart, f.uEnd);
      const double vA = std::min(f.vStart, f.vEnd);
      const double vB = std::max(f.vStart, f.vEnd);
      double netStep = 0.0;
      for (std::size_t k = 0; k + 1 < patch.ctrl.size(); ++k)
        netStep = std::max(netStep, ray3d::Length(ray3d::Sub(patch.ctrl[k + 1], patch.ctrl[k])));
      const int steps = std::clamp(SegmentsForArc(std::max(netStep, 1e-9), kHalfPi, chordTolerance),
                                   8, 128);
      for (int k = 1; k <= isolineCount; ++k) {
        const double fr = static_cast<double>(k) / static_cast<double>(isolineCount + 1);
        AppendIsoCurve(sf, /*fixedIsU=*/true, uA + (uB - uA) * fr, vA, vB, steps, &segs);
        AppendIsoCurve(sf, /*fixedIsU=*/false, vA + (vB - vA) * fr, uA, uB, steps, &segs);
      }
      continue;
    }

    const double uLo = std::min(f.uStart, f.uEnd);
    const double uHi = std::max(f.uStart, f.uEnd);

    // --- Lines along the face, at constant u -----------------------------------------------------
    GridAnglesInside(isolineCount, uLo, uHi, &angles);
    for (double u : angles) {
      if (sf.kind == SurfaceKind::Cylinder || sf.kind == SurfaceKind::Cone) {
        // A straight ruling from base to top: one segment is exact, since the surface is ruled.
        AppendIsoCurve(sf, /*fixedIsU=*/true, u, 0.0, sf.height, 1, &segs);
      } else {
        const double vLo = std::min(f.vStart, f.vEnd);
        const double vHi = std::max(f.vStart, f.vEnd);
        const double r = sf.kind == SurfaceKind::Sphere ? sf.radius : sf.radius2;
        AppendIsoCurve(sf, true, u, vLo, vHi, SegmentsForArc(r, vHi - vLo, chordTolerance), &segs);
      }
    }

    // --- Rings across the face, at constant v ----------------------------------------------------
    //
    // Skipped for the ruled kinds on purpose. A horizontal ring part way up a cylinder is not
    // something AutoCAD draws, and it reads as an edge that is not there — a seam, or the join of
    // two stacked solids.
    if (sf.kind == SurfaceKind::Cylinder || sf.kind == SurfaceKind::Cone)
      continue;

    const double vLo = std::min(f.vStart, f.vEnd);
    const double vHi = std::max(f.vStart, f.vEnd);
    if (sf.kind == SurfaceKind::Torus) {
      GridAnglesInside(isolineCount, vLo, vHi, &angles);
    } else {
      // A sphere's v is a LATITUDE over [-pi/2, pi/2], not a full turn, so the global-grid rule does
      // not apply: half of that grid's lines would fall outside the surface entirely. Evenly spaced
      // interior latitudes instead, at half the count — a sphere with four meridians and four
      // latitude circles reads as a net rather than as a ball.
      angles.clear();
      const int nv = std::max(1, isolineCount / 2);
      for (int i = 1; i <= nv; ++i)
        angles.push_back(vLo + (vHi - vLo) * static_cast<double>(i) / static_cast<double>(nv + 1));
    }
    for (double v : angles) {
      const double ringR = sf.kind == SurfaceKind::Sphere
                               ? sf.radius * std::cos(v)
                               : sf.radius + sf.radius2 * std::cos(v);
      AppendIsoCurve(sf, /*fixedIsU=*/false, v, uLo, uHi,
                     SegmentsForArc(std::fabs(ringR), uHi - uLo, chordTolerance), &segs);
    }
  }

  *out = std::move(segs);
  return Succeed(outWhy);
}

bool TessellateEdges(const Solid& s, double chordTolerance, std::vector<double>* out, Problem* outWhy) {
  if (!out)
    return false;
  if (!std::isfinite(chordTolerance) || !(chordTolerance > 0.0))
    return Fail(Problem::NonPositiveTolerance, outWhy);
  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);

  std::vector<double> segs;
  for (const Edge& e : s.edges) {
    const int n = SegmentsForEdge(e, chordTolerance);
    Vec3 prev = EdgePointAt(s, e, 0.0);
    for (int i = 1; i <= n; ++i) {
      const Vec3 next = EdgePointAt(s, e, static_cast<double>(i) / static_cast<double>(n));
      segs.push_back(prev.x);
      segs.push_back(prev.y);
      segs.push_back(prev.z);
      segs.push_back(next.x);
      segs.push_back(next.y);
      segs.push_back(next.z);
      prev = next;
    }
  }
  *out = std::move(segs);
  return Succeed(outWhy);
}

} // namespace brep
