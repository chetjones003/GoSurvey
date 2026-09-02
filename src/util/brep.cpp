#include "brep.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
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
    if (e.kind == CurveKind::Arc)
      mapFrame(&e.frame);
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
    if (e.kind == CurveKind::Arc) {
      // The bulge between the chord and the arc. Signed about the FACE normal, which is not
      // necessarily the arc's own normal — a cap rim and its face can be described the opposite way
      // round, and getting this sign wrong turns a disc into a bow tie.
      double sweep = u.reversed ? -e.sweep : e.sweep;
      if (ray3d::Dot(e.frame.zAxis, fr.zAxis) < 0.0)
        sweep = -sweep;
      acc += 0.5 * e.radius * e.radius * (sweep - std::sin(sweep));
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
    const ConeIntegrals ci = ConicalFaceIntegrals(sf.radius, sf.radius, sf.height, f.uStart, f.uEnd, qLocal);
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
  }
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

/// The outward unit normal of a cone/cylinder side at longitude \p t, in world.
[[nodiscard]] Vec3 ConicalNormal(const Surface& sf, double r0, double r1, double t) {
  const double k = (r0 - r1) / sf.height;
  const double inv = 1.0 / std::sqrt(1.0 + k * k);
  const Vec3 local{std::cos(t) * inv, std::sin(t) * inv, k * inv};
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
  return ucs::UcsVectorToWorld(sf.frame, Vec3{cv * std::cos(t), cv * std::sin(t), std::sin(v)});
}

[[nodiscard]] Vec3 ToroidalPoint(const Surface& sf, double t, double v) {
  const double rho = sf.radius + sf.radius2 * std::cos(v);
  return ucs::UcsToWorld(sf.frame, Vec3{rho * std::cos(t), rho * std::sin(t), sf.radius2 * std::sin(v)});
}

[[nodiscard]] Vec3 ToroidalNormal(const Surface& sf, double t, double v) {
  const double cv = std::cos(v);
  return ucs::UcsVectorToWorld(sf.frame, Vec3{cv * std::cos(t), cv * std::sin(t), std::sin(v)});
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
    return "A profile arc curves inward; this release can extrude outward-curving arcs only.";
  }
  return "The solid is not valid.";
}

// ---------------------------------------------------------------------------------------------
// Edge evaluation — the single parametrisation.
// ---------------------------------------------------------------------------------------------

Vec3 EdgePointAt(const Solid& s, const Edge& e, double t) {
  if (e.kind == CurveKind::Line) {
    const Vec3& a = s.vertices[static_cast<std::size_t>(e.v0)].p;
    const Vec3& b = s.vertices[static_cast<std::size_t>(e.v1)].p;
    return ray3d::Add(a, ray3d::Scale(ray3d::Sub(b, a), t));
  }
  return ucs::PointOnPlaneCircle(e.frame, e.radius, e.sweep * t);
}

Solid Translate(const Solid& s, const Vec3& delta) {
  Solid out = s;
  for (Vertex& v : out.vertices)
    v.p = ray3d::Add(v.p, delta);
  for (Edge& e : out.edges) {
    if (e.kind == CurveKind::Arc)
      e.frame.origin = ray3d::Add(e.frame.origin, delta);
  }
  for (Face& f : out.faces)
    f.surface.frame.origin = ray3d::Add(f.surface.frame.origin, delta);
  out.recipe.frame.origin = ray3d::Add(out.recipe.frame.origin, delta);
  return out;
}

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
  }
  return p;
}

Vec3 ClosestPointOnEdge(const Solid& s, const Edge& e, const Vec3& p) {
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
  for (const ProfileEdge& pe : E) {
    if (pe.arc && !(pe.sweep > 1e-12))
      return Fail(Problem::ProfileArcReflex, outWhy);
  }

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
      f.uStart = u0;
      f.uEnd = u0 + pe.sweep;
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
    } else {
      if (!AllFinite({e.radius, e.sweep}) || !FinitePoint(e.frame.origin))
        return Problem::NonFiniteCoordinate;
      if (!(e.radius > lenEps) || !(std::fabs(e.sweep) > 1e-9))
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

  const Vec3 probe = ray3d::Add(q, Vec3{scale, 0.7 * scale, -1.3 * scale});
  const double probeVolume = VolumeAbout(s, probe, nullptr);
  if (!std::isfinite(probeVolume) ||
      std::fabs(volume - probeVolume) > 1e-8 * scale * scale * scale)
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
      if (f.loops.size() != 1)
        return Fail(Problem::PlaneFaceNotSimple, outWhy);
      // Walk the boundary into a polyline. Arc edges are subdivided by the same chord rule the
      // curved faces use, so the cap and the wall it meets do not disagree.
      std::vector<Vec3> ring;
      for (const EdgeUse& u : f.loops[0].uses) {
        const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
        const int segs =
            e.kind == CurveKind::Arc ? SegmentsForArc(e.radius, e.sweep, chordTolerance) : 1;
        for (int i = 0; i < segs; ++i) {
          const double t = static_cast<double>(i) / static_cast<double>(segs);
          ring.push_back(EdgePointAt(s, e, u.reversed ? 1.0 - t : t));
        }
      }
      if (ring.size() < 3)
        return Fail(Problem::DegenerateFace, outWhy);
      const Vec3 n = sf.frame.zAxis;
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
      const int nu = SegmentsForArc(std::max(r0, r1), f.uEnd - f.uStart, chordTolerance);
      std::vector<std::uint32_t> lower(static_cast<std::size_t>(nu) + 1);
      std::vector<std::uint32_t> upper(static_cast<std::size_t>(nu) + 1);
      for (int i = 0; i <= nu; ++i) {
        const double t = f.uStart + (f.uEnd - f.uStart) * static_cast<double>(i) / static_cast<double>(nu);
        const Vec3 n = ConicalNormal(sf, r0, r1, t);
        lower[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r1, t, 0.0), n);
        upper[static_cast<std::size_t>(i)] = mb.Push(ConicalPoint(sf, r0, r1, t, sf.height), n);
      }
      for (int i = 0; i < nu; ++i) {
        const std::size_t a = static_cast<std::size_t>(i);
        const std::size_t b = static_cast<std::size_t>(i + 1);
        mb.Tri(lower[a], lower[b], upper[b]);
        mb.Tri(lower[a], upper[b], upper[a]);
      }
      break;
    }
    case SurfaceKind::Sphere:
    case SurfaceKind::Torus: {
      const bool sphere = sf.kind == SurfaceKind::Sphere;
      const double uRadius = sphere ? sf.radius : sf.radius + sf.radius2;
      const double vRadius = sphere ? sf.radius : sf.radius2;
      const int nu = SegmentsForArc(uRadius, f.uEnd - f.uStart, chordTolerance);
      const int nv = SegmentsForArc(vRadius, f.vEnd - f.vStart, chordTolerance);
      std::vector<std::uint32_t> grid(static_cast<std::size_t>(nu + 1) * static_cast<std::size_t>(nv + 1));
      for (int i = 0; i <= nu; ++i) {
        const double t = f.uStart + (f.uEnd - f.uStart) * static_cast<double>(i) / static_cast<double>(nu);
        for (int j = 0; j <= nv; ++j) {
          const double v = f.vStart + (f.vEnd - f.vStart) * static_cast<double>(j) / static_cast<double>(nv);
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
    const int n = e.kind == CurveKind::Arc ? SegmentsForArc(e.radius, e.sweep, chordTolerance) : 1;
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
