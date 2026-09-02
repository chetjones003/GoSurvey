#include "brep.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
      // The one curved case B1 can hold: a cut perpendicular to a cylinder / cone axis (a circle).
      bool handled = false;
      const bool ok = SliceCurvedPrimitive(solid, planePoint, ray3d::Normalize(planeNormal), keep,
                                           outAbove, outBelow, &handled, outWhy);
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

/// A coaxial stack of full cylinders: `z` holds `r.size()+1` ascending breakpoints along the axis,
/// `r[i]` is the radius of the band between `z[i]` and `z[i+1]`. Adjacent radii must differ. Built in
/// the canonical frame and placed into \p frame (whose origin is the world point at `z.front()`).
[[nodiscard]] bool BuildCoaxialStack(const ucs::Ucs& frame, const std::vector<double>& z,
                                     const std::vector<double>& r, Solid* out, Problem* outWhy) {
  const int n = static_cast<int>(r.size());
  if (n < 1 || static_cast<int>(z.size()) != n + 1)
    return Fail(Problem::BooleanResultInvalid, outWhy);
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
  for (int i = 0; i < n; ++i) {
    const double zl = z[static_cast<std::size_t>(i)];
    const double zh = z[static_cast<std::size_t>(i + 1)];
    const double rr = r[static_cast<std::size_t>(i)];
    const auto vb = verts(zl, rr);
    const auto vt = verts(zh, rr);
    const auto cb = circle(zl, rr);
    const auto ct = circle(zh, rr);
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
      f.uStart = u0;
      f.uEnd = u1;
      Loop lp;
      lp.uses = std::move(uses);
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    };
    wall(0.0, kPi, {{cb.first, false}, {sm1, false}, {ct.first, true}, {sm0, true}});
    wall(kPi, kTwoPi, {{cb.second, false}, {sm0, false}, {ct.second, true}, {sm1, true}});
  }
  for (int k = 0; k <= n; ++k) {
    const double zz = z[static_cast<std::size_t>(k)];
    const double rl = (k > 0) ? r[static_cast<std::size_t>(k - 1)] : 0.0;
    const double rh = (k < n) ? r[static_cast<std::size_t>(k)] : 0.0;
    if (k == 0) {
      const auto c = circle(zz, rh);
      s.faces.push_back(
          MakePlaneFace(Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, -1.0}, {{c.second, true}, {c.first, true}}));
    } else if (k == n) {
      const auto c = circle(zz, rl);
      s.faces.push_back(
          MakePlaneFace(Vec3{0.0, 0.0, zz - z0}, up, {{c.first, false}, {c.second, false}}));
    } else {
      const double outer = std::max(rl, rh);
      const double inner = std::min(rl, rh);
      const bool faceUp = rh < rl;  // narrowing upward leaves an annulus facing +z
      const Vec3 nrm = faceUp ? up : Vec3{0.0, 0.0, -1.0};
      const auto co = circle(zz, outer);
      const auto ci = circle(zz, inner);
      Face f = MakePlaneFace(Vec3{0.0, 0.0, zz - z0}, nrm, {});
      f.loops.clear();
      Loop outerL;
      Loop innerL;
      if (faceUp) {
        outerL.uses = {{co.first, false}, {co.second, false}};
        innerL.uses = {{ci.second, true}, {ci.first, true}};
      } else {
        outerL.uses = {{co.second, true}, {co.first, true}};
        innerL.uses = {{ci.first, false}, {ci.second, false}};
      }
      f.loops.push_back(std::move(outerL));
      f.loops.push_back(std::move(innerL));
      s.faces.push_back(std::move(f));
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

[[nodiscard]] bool TryBooleanCoaxialCylinders(const Solid& a, const Solid& b, const CylinderShape& A,
                                              const CylinderShape& B, BoolOp op, std::vector<Solid>* out,
                                              bool* handled, Problem* outWhy) {
  if (op == BoolOp::Subtract) {
    *handled = true;
    return Fail(Problem::BooleanCurvedFace, outWhy);
  }
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
  if (!BuildCoaxialStack(fr, zs, rs, &r, outWhy))
    return false;
  out->push_back(std::move(r));
  return Succeed(outWhy);
}

[[nodiscard]] bool TryBooleanCylinderThroughPlanar(const Solid& planar, const Solid& cyl,
                                                   const CylinderShape& C, BoolOp op,
                                                   std::vector<Solid>* out, bool* handled,
                                                   Problem* outWhy) {
  if (op == BoolOp::Subtract) {
    *handled = true;
    return Fail(Problem::BooleanCurvedFace, outWhy);
  }
  *handled = true;
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
  if (!anyPerp)
    return Fail(Problem::BooleanObliqueCylinder, outWhy);
  return Fail(Problem::BooleanCurvedFace, outWhy);  // partial penetration / a footprint over an edge
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
  if (aCyl && bCyl)
    return TryBooleanCoaxialCylinders(a, b, ca, cb, op, out, handled, outWhy);
  if (aCyl && AllFacesPlanar(b))
    return TryBooleanCylinderThroughPlanar(b, a, ca, op, out, handled, outWhy);
  if (bCyl && AllFacesPlanar(a))
    return TryBooleanCylinderThroughPlanar(a, b, cb, op, out, handled, outWhy);
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
      if (f.loops.size() > 2)
        return Fail(Problem::PlaneFaceNotSimple, outWhy);
      // Walk each loop into a polyline. Arc edges are subdivided by the same chord rule the curved
      // faces use, so the cap and the wall it meets do not disagree.
      auto sampleLoop = [&](const Loop& lp) {
        std::vector<Vec3> r;
        for (const EdgeUse& u : lp.uses) {
          const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
          const int segs =
              e.kind == CurveKind::Arc ? SegmentsForArc(e.radius, e.sweep, chordTolerance) : 1;
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
