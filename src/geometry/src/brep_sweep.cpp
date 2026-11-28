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
// Feature operations — Extrude (REQ-314 / ADR-046 increment 1, GitHub issue #147).
// ---------------------------------------------------------------------------------------------


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



// ---------------------------------------------------------------------------------------------
// Recognising a feature result that IS a cylinder or cone (GitHub issue #515, D-2026-09-16-c).
//
// `Slice` — and so `SectionLoop` — can cut a curved solid only through the cylinder/cone
// recognisers, which read the RECIPE. A circle extruded along its normal, a rectangle revolved a
// full turn about one of its edges and a loft between two coaxial circles are exactly the solids
// `MakeCylinder` / `MakeCone` build, yet carried no recipe, so the same shape cut or was refused
// depending on the command that made it. These helpers let those three operations describe a
// result that is exactly that primitive. They run only AFTER the operation has built and validated
// its own result, so every refusal the operation makes is unchanged, and a profile that merely
// resembles one of these shapes fails a test here and keeps `PrimitiveKind::None`.
// ---------------------------------------------------------------------------------------------


/// A profile that is exactly one full circle: every edge an arc about ONE centre, all sweeping the
/// same way, every vertex at the same distance from it, the sweeps totalling one turn.
[[nodiscard]] bool ProfileIsFullCircle(const Profile& profile, Vec3* outCentre, double* outRadius) {
  const std::size_t n = profile.vertices.size();
  if (n < 2 || profile.edges.size() != n)
    return false;
  const Vec3 c = profile.edges.front().centre;
  const double r = ray3d::Length(ray3d::Sub(profile.vertices.front(), c));
  if (!(r > 0.0) || !std::isfinite(r))
    return false;
  const double tol = 1e-6 * std::max(r, 1.0);  // the kernel's own planeEps scale
  const Vec3 axis = ray3d::Normalize(profile.plane.zAxis);
  double total = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const ProfileEdge& e = profile.edges[i];
    if (!e.arc || ray3d::Length(ray3d::Sub(e.centre, c)) > tol)
      return false;
    if ((e.sweep > 0.0) != (profile.edges.front().sweep > 0.0))
      return false;
    if (std::fabs(ray3d::Length(ray3d::Sub(profile.vertices[i], c)) - r) > tol)
      return false;
    // Each arc must actually carry vertex i to vertex i+1 (code review on #515): a shared centre, a
    // constant radius and sweeps totalling a turn are also true of arcs whose endpoints disagree with
    // their sweeps, and neither Extrude nor Validate checks that.
    const Vec3 v = ray3d::Sub(profile.vertices[i], c);
    const double ca = std::cos(e.sweep), sa = std::sin(e.sweep);
    const Vec3 turned = ray3d::Add(ray3d::Add(ray3d::Scale(v, ca), ray3d::Scale(ray3d::Cross(axis, v), sa)),
                                   ray3d::Scale(axis, ray3d::Dot(axis, v) * (1.0 - ca)));
    if (ray3d::Length(ray3d::Sub(ray3d::Add(c, turned), profile.vertices[(i + 1) % n])) > tol)
      return false;
    total += e.sweep;
  }
  if (std::fabs(std::fabs(total) - kTwoPi) > 1e-7)
    return false;
  *outCentre = c;
  *outRadius = r;
  return true;
}
}  // namespace
[[nodiscard]] bool ConicalBetween(const Vec3& centreA, double rA, const Vec3& centreB, double rB, Conical* out) {
  const Vec3 axis = ray3d::Sub(centreB, centreA);
  const double h = ray3d::Length(axis);
  const double scale = std::max({h, rA, rB, 1.0});
  if (!(h > 1e-9 * scale) || rA < 0.0 || rB < 0.0 || !(std::max(rA, rB) > 1e-9 * scale))
    return false;
  const bool aIsBase = rA >= rB;
  if (!ucs::FromNormal(aIsBase ? centreA : centreB, aIsBase ? axis : ray3d::Scale(axis, -1.0), &out->frame))
    return false;
  out->rBase = aIsBase ? rA : rB;
  out->rTop = aIsBase ? rB : rA;
  out->height = h;
  // Equal radii are judged against the RADIUS, not the height (code review on #515): scaled by the
  // height, a real taper of 0.0009 over 1000 ft passed as a cylinder, and the slice pieces rebuilt
  // from that recipe came out 1.4 cubic ft too large. Radius-relative, the tolerance still absorbs
  // float storage noise on a genuinely equal pair.
  out->cylinder = std::fabs(out->rBase - out->rTop) <= 1e-6 * out->rBase;
  return true;
}

/// Describe \p s as the Cylinder / Cone \p c. The topology is untouched: the recipe is description.
void StampConicalRecipe(Solid* s, const Conical& c) {
  s->recipe = Recipe{};
  s->recipe.kind = c.cylinder ? PrimitiveKind::Cylinder : PrimitiveKind::Cone;
  s->recipe.frame = c.frame;
  s->recipe.radius = c.rBase;
  s->recipe.radius2 = c.cylinder ? c.rBase : c.rTop;  // what BuildConical stores for a cylinder
  s->recipe.height = c.height;
}

namespace {

/// A full-turn revolve that is exactly a cylinder or cone: an all-straight profile of three or four
/// edges with ONE edge on the axis, the profile edges leaving its ends perpendicular to the axis (the
/// caps; a three-edge profile has one cap and an apex), and the far edge joining the rim points.
/// Writes the two circles as (centre, radius); the apex is a circle of radius zero.
[[nodiscard]] bool RevolveProfileIsConical(const Profile& profile, const Vec3& axisPoint, const Vec3& axisUnit,
                                           Vec3* outCentreA, double* outRA, Vec3* outCentreB, double* outRB) {
  const int n = static_cast<int>(profile.vertices.size());
  if ((n != 3 && n != 4) || static_cast<int>(profile.edges.size()) != n)
    return false;
  for (const ProfileEdge& e : profile.edges)
    if (e.arc)
      return false;
  std::vector<double> t(static_cast<std::size_t>(n));
  std::vector<double> d(static_cast<std::size_t>(n));
  double tMin = 1e300, tMax = -1e300, dMax = 0.0;
  for (int i = 0; i < n; ++i) {
    const std::size_t k = static_cast<std::size_t>(i);
    const Vec3 w = ray3d::Sub(profile.vertices[k], axisPoint);
    t[k] = ray3d::Dot(w, axisUnit);
    d[k] = ray3d::Length(ray3d::Sub(w, ray3d::Scale(axisUnit, t[k])));
    tMin = std::min(tMin, t[k]);
    tMax = std::max(tMax, t[k]);
    dMax = std::max(dMax, d[k]);
  }
  // Scaled by the PROFILE's own extent, as Revolve's axisEps is — not by how far along the axis the
  // picked axis point happens to lie, which would loosen the test for a point clicked far away.
  const double tol = 1e-6 * std::max({tMax - tMin, dMax, 1e-9});
  const auto on = [&](int i) { return d[static_cast<std::size_t>((i % n + n) % n)] <= tol; };
  const auto tt = [&](int i) { return t[static_cast<std::size_t>((i % n + n) % n)]; };
  const auto dd = [&](int i) { return d[static_cast<std::size_t>((i % n + n) % n)]; };
  const auto centreAt = [&](double tv) { return ray3d::Add(axisPoint, ray3d::Scale(axisUnit, tv)); };

  int onCount = 0;
  int a = -1;  // the axis edge runs a -> a+1
  for (int i = 0; i < n; ++i) {
    if (on(i))
      ++onCount;
    if (on(i) && on(i + 1))
      a = i;
  }
  if (onCount != 2 || a < 0)
    return false;
  const int b = a + 1;
  if (std::fabs(tt(a) - tt(b)) <= tol)
    return false;

  if (n == 4) {
    // b -> b+1 is one cap, a-1 -> a the other, and b+1 -> a-1 the side.
    if (std::fabs(tt(b + 1) - tt(b)) > tol || std::fabs(tt(a - 1) - tt(a)) > tol)
      return false;
    *outCentreA = centreAt(tt(a));
    *outRA = dd(a - 1);
    *outCentreB = centreAt(tt(b));
    *outRB = dd(b + 1);
    return *outRA > tol && *outRB > tol;
  }
  // n == 3: the rim vertex shares its axial position with exactly one axis vertex; the other is
  // the apex.
  const int rim = b + 1;
  if (std::fabs(tt(rim) - tt(b)) <= tol) {
    *outCentreA = centreAt(tt(b));
    *outCentreB = centreAt(tt(a));
  } else if (std::fabs(tt(rim) - tt(a)) <= tol) {
    *outCentreA = centreAt(tt(a));
    *outCentreB = centreAt(tt(b));
  } else {
    return false;
  }
  *outRA = dd(rim);
  *outRB = 0.0;
  return *outRA > tol;
}

}  // namespace

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
    const Vec3 expectedEnd = ray3d::Add(pe.centre, ray3d::RotateVectorAboutAxis(
        ray3d::Sub(profile.vertices[static_cast<std::size_t>(i)], pe.centre),
        ray3d::Normalize(pl.zAxis), pe.sweep));
    if (ray3d::Length(ray3d::Sub(expectedEnd, profile.vertices[static_cast<std::size_t>((i + 1) % n)])) > planeEps)
      return Fail(Problem::NotClosed, outWhy);
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
  // The topology is the stored truth (ADR-046 (e)); a recipe is added below only when the result is
  // exactly a cylinder (ADR-046 amendment (o)).

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  // Issue #515: a circle extruded along its own normal IS a cylinder. Say so, so Slice and
  // SectionLoop cut it the way they cut the primitive; the topology is untouched.
  {
    Vec3 cc{};
    double rr = 0.0;
    Conical c;
    if (ProfileIsFullCircle(profile, &cc, &rr) &&
        ConicalBetween(cc, rr, ray3d::Add(cc, ray3d::Scale(ray3d::Normalize(pl.zAxis), distance)), rr, &c))
      StampConicalRecipe(&s, c);
  }
  *out = std::move(s);
  return Succeed(outWhy);
}

namespace {

// ---------------------------------------------------------------------------------------------
// Feature operations — Loft (REQ-315 / ADR-048, GitHub issue #241). Skin a solid between two or
// more planar profiles. Each corresponding edge pair spans one SurfaceKind::Nurbs patch — ruled
// where the edge is straight, rational where it is an arc — and the end profiles cap it flat. The
// topology is Extrude's, generalised to N profiles; the only new thing is the freeform side face.
// ---------------------------------------------------------------------------------------------


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
    const Vec3 expectedEnd = ray3d::Add(pe.centre, ray3d::RotateVectorAboutAxis(
        ray3d::Sub(pr.vertices[static_cast<std::size_t>(i)], pe.centre),
        ray3d::Normalize(pl.zAxis), pe.sweep));
    if (ray3d::Length(ray3d::Sub(expectedEnd, pr.vertices[static_cast<std::size_t>((i + 1) % n)])) > planeEps)
      return Fail(Problem::NotClosed, outWhy);
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



/// Whether the quadrilateral \p q (in loop order) is flat, and if so its unit normal — the Newell
/// normal, which for a loop wound CCW about the outward direction points outward (GitHub #519).
///
/// Flat means every corner within `1e-9` of the strip's size of the plane through them, measured
/// from the first corner so survey-magnitude coordinates do not enter the arithmetic.
[[nodiscard]] bool RuledStripIsFlat(const std::array<Vec3, 4>& q, Vec3* outNormal) {
  Vec3 acc{};
  double size = 0.0;
  for (std::size_t i = 0; i < 4; ++i) {
    const Vec3 a = ray3d::Sub(q[i], q[0]);
    const Vec3 b = ray3d::Sub(q[(i + 1) % 4], q[0]);
    acc = ray3d::Add(acc, ray3d::Cross(a, b));
    size = std::max(size, ray3d::Length(ray3d::Sub(q[(i + 1) % 4], q[i])));
  }
  const double len = ray3d::Length(acc);
  if (!(size > 0.0) || !(len > 1e-12 * size * size))
    return false;
  // The two ring edges must run the same way round. Reversed, the strip is a bow-tie: its four
  // corners still lie in one plane, but the face they would make crosses itself, so it stays the
  // ruled patch it was.
  if (!(ray3d::Dot(ray3d::Sub(q[1], q[0]), ray3d::Sub(q[2], q[3])) > 0.0))
    return false;
  const Vec3 nrm = ray3d::Scale(acc, 1.0 / len);
  const double tol = std::max(1e-9 * size, 1e-9);
  for (const Vec3& p : q)
    if (std::fabs(ray3d::Dot(ray3d::Sub(p, q[0]), nrm)) > tol)
      return false;
  *outNormal = nrm;
  return true;
}

} // namespace

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

  // Issue #515: a loft between two coaxial circles on parallel planes IS a cylinder or cone. It is
  // BUILT as one — analytic cylinder / cone faces rather than the NURBS ribbons below, with the
  // primitive's recipe — so every planar-and-conical operation treats it as the shape it is. Only a
  // loft whose profiles have passed every check above reaches this, so every refusal stands. It runs
  // BEFORE the ribbon faces are built (code review on #515): building, validating and discarding
  // them cost the live LOFT preview that work every frame.
  //
  // "Coaxial" is not enough (code review on #515): the loft pairs vertex j of one circle with vertex
  // j of the other, and when those are not at the same angle about the axis the ribbon between them
  // TWISTS — two circles 90 degrees out of step loft to a pinched band of about two thirds the
  // cylinder's volume. So each rail must lie in a plane through the axis. Tolerances are relative to
  // the model, like planeEps, because a circle's normal is stored in float while its centre is double:
  // a fixed 1e-9 refused every coaxial pair drawn in a tilted UCS.
  if (profiles.size() == 2) {
    Vec3 c0{}, c1{};
    double r0 = 0.0, r1 = 0.0;
    Conical c;
    if (ProfileIsFullCircle(profiles[0], &c0, &r0) && ProfileIsFullCircle(profiles[1], &c1, &r1) &&
        ConicalBetween(c0, r0, c1, r1, &c)) {
      const Vec3 axisU = c.frame.zAxis;
      const double modelEps = 1e-6 * std::max({c.height, r0, r1});
      // The second centre on the first circle's normal: its offset from that line, in model units.
      const Vec3 axisVec = ray3d::Sub(c1, c0);
      bool straight = ray3d::Length(ray3d::Cross(axisVec, ray3d::Normalize(prep[0].up))) <= modelEps;
      // Every rail in a plane through the axis, running the same way round (no twist).
      for (int j = 0; straight && j < n; ++j) {
        const std::size_t jj = static_cast<std::size_t>(j);
        const Vec3 w0 = ray3d::Sub(prep[0].walk[jj], c0);
        const Vec3 w1 = ray3d::Sub(prep[1].walk[jj], c1);
        const Vec3 rad0 = ray3d::Sub(w0, ray3d::Scale(axisU, ray3d::Dot(w0, axisU)));
        const Vec3 rad1 = ray3d::Sub(w1, ray3d::Scale(axisU, ray3d::Dot(w1, axisU)));
        const double l0 = ray3d::Length(rad0), l1 = ray3d::Length(rad1);
        if (!(l0 > modelEps) || !(l1 > modelEps) || ray3d::Dot(rad0, rad1) <= 0.0) {
          straight = false;
          break;
        }
        // The rim point of profile 1 against the direction of profile 0's: sideways offset, in units.
        const double sideways = ray3d::Length(ray3d::Cross(ray3d::Scale(rad0, 1.0 / l0), rad1));
        if (sideways > modelEps)
          straight = false;
      }
      Solid prim;
      Problem pw = Problem::Ok;
      if (straight && (c.cylinder ? MakeCylinder(c.frame, c.rBase, c.height, &prim, &pw)
                                  : MakeCone(c.frame, c.rBase, c.rTop, c.height, &prim, &pw))) {
        *out = std::move(prim);
        return Succeed(outWhy);
      }
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

  // Side faces: one NURBS patch per (band, edge), oriented so du x dv points outward — except a
  // straight edge whose ruled strip is exactly flat (its two ring edges coplanar, as between similar
  // parallel polygons), which is stored as the `Plane` face it is (GitHub #519). A NURBS patch of a
  // flat strip is the right shape but the wrong kind: SECTION, SLICE, SECTIONPLANE and every other
  // planar-only operation refused it.
  for (std::size_t pi = 0; pi + 1 < prep.size(); ++pi)
    for (int j = 0; j < n; ++j) {
      const std::size_t jj = static_cast<std::size_t>(j);
      const std::size_t j1 = static_cast<std::size_t>((j + 1) % n);
      Loop lp;
      lp.uses = {EdgeUse{ringE[pi][jj], false}, EdgeUse{railE[pi][j1], false},
                 EdgeUse{ringE[pi + 1][jj], true}, EdgeUse{railE[pi][jj], true}};
      if (!prep[pi].arc[jj]) {
        Vec3 normal{};
        if (RuledStripIsFlat({prep[pi].walk[jj], prep[pi].walk[j1], prep[pi + 1].walk[j1], prep[pi + 1].walk[jj]},
                             &normal)) {
          s.faces.push_back(MakePlaneFace(prep[pi].walk[jj], normal, std::move(lp.uses)));
          continue;
        }
      }
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
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    }

  AddSingleShell(&s);  // one shell, no recipe — the topology is the stored truth (ADR-046 (e)); see above

  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

namespace {

// ---------------------------------------------------------------------------------------------
// Feature operations — Sweep (REQ-315 / ADR-048, GitHub issue #241). One profile along one path
// segment (a line or a circular arc). A straight segment builds ruled NURBS side faces and
// reproduces Extrude; an arc segment revolves the profile about the arc's axis and reproduces
// Revolve. The topology is Loft's single band; the difference is the V direction of each side patch
// (a straight span vs an exact rational revolution) and that the rails follow the path.
// ---------------------------------------------------------------------------------------------


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

namespace {


/// Re-aim one loop: walk its edges the other way round. Reversing the order AND flipping each use's
/// own direction is what keeps the chain connected — the last edge's far end becomes the first's
/// start — so a loop that was traversed CCW about its face normal is now traversed CW, which is
/// exactly what turns a sweep's own cap loop into the HOLE loop of an annulus, or an inner band's
/// outward-facing boundary into an inward-facing one.
void ReverseLoopInPlace(Loop* lp, int edgeOffset) {
  std::reverse(lp->uses.begin(), lp->uses.end());
  for (EdgeUse& u : lp->uses) {
    u.edge += edgeOffset;
    u.reversed = !u.reversed;
  }
}

} // namespace

bool SweepTube(const Profile& outerProfile, const Profile& innerProfile, const SweepPath& path,
               const SweepOptions& options, Solid* out, Problem* outWhy) {
  if (!out)
    return false;
  // Matching cross-sections, or the two sweeps produce rings that do not correspond and the caps
  // cannot close into one annulus. Checked before either sweep runs, so the reason reported is this
  // one rather than whichever sweep happened to fail first for an unrelated cause.
  if (outerProfile.vertices.size() != innerProfile.vertices.size() ||
      outerProfile.edges.size() != innerProfile.edges.size() ||
      outerProfile.vertices.size() != outerProfile.edges.size())
    return Fail(Problem::ProfileMalformed, outWhy);

  Solid o;
  if (!Sweep(outerProfile, path, options, &o, outWhy))
    return false;
  Solid i;
  if (!Sweep(innerProfile, path, options, &i, outWhy))
    return false;

  // Sweep builds an open path's two planar caps FIRST and every side face after (and a closed path
  // no caps at all). Rather than trust that order, find them: exactly two planar faces, at indices
  // 0 and 1, is the shape this merge needs — a closed path (no caps) lands here as "not two", which
  // is the documented refusal.
  auto capsAreFirstTwo = [](const Solid& s) {
    if (s.faces.size() < 3)
      return false;
    if (s.faces[0].surface.kind != SurfaceKind::Plane || s.faces[1].surface.kind != SurfaceKind::Plane)
      return false;
    for (std::size_t k = 2; k < s.faces.size(); ++k)
      if (s.faces[k].surface.kind == SurfaceKind::Plane)
        return false;
    return true;
  };
  if (!capsAreFirstTwo(o) || !capsAreFirstTwo(i) || o.faces.size() != i.faces.size())
    return Fail(Problem::SweepUnsupportedOption, outWhy);

  Solid s;
  s.vertices = o.vertices;
  const int vOff = static_cast<int>(s.vertices.size());
  s.vertices.insert(s.vertices.end(), i.vertices.begin(), i.vertices.end());
  s.edges = o.edges;
  const int eOff = static_cast<int>(s.edges.size());
  for (Edge e : i.edges) {
    e.v0 += vOff;
    e.v1 += vOff;
    s.edges.push_back(std::move(e));
  }

  // The outer wall, exactly as swept.
  for (std::size_t fi = 2; fi < o.faces.size(); ++fi)
    s.faces.push_back(o.faces[fi]);

  // The inner wall, turned inside out: every loop re-aimed (so each inner edge is still used twice,
  // once each way, now that its cap use has gone) and the surface marked `inward`, which is what
  // makes its normal point into the bore and its volume term subtract the void it bounds.
  for (std::size_t fi = 2; fi < i.faces.size(); ++fi) {
    Face f = i.faces[fi];
    f.surface.inward = true;
    for (Loop& lp : f.loops)
      ReverseLoopInPlace(&lp, eOff);
    s.faces.push_back(std::move(f));
  }

  // Each end cap becomes an annulus: the outer sweep's cap face (already on the right plane, with
  // the right outward normal and the right outer loop) with the inner sweep's cap loop, re-aimed,
  // added as the hole.
  for (std::size_t capIdx = 0; capIdx < 2; ++capIdx) {
    if (o.faces[capIdx].loops.empty() || i.faces[capIdx].loops.empty())
      return Fail(Problem::SweepUnsupportedOption, outWhy);
    Face cap = o.faces[capIdx];
    Loop hole = i.faces[capIdx].loops.front();
    ReverseLoopInPlace(&hole, eOff);
    cap.loops.push_back(std::move(hole));
    s.faces.push_back(std::move(cap));
  }

  AddSingleShell(&s);
  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return Fail(why, outWhy);
  *out = std::move(s);
  return Succeed(outWhy);
}

namespace {

// ---------------------------------------------------------------------------------------------
// Feature operations — Revolve (REQ-314 / ADR-046 increment 2, GitHub issue #147).
// ---------------------------------------------------------------------------------------------


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
  // Issue #515: a full-turn revolve of a rectangle / right trapezoid / right triangle about one of
  // its edges IS a cylinder or cone. Say so, so Slice and SectionLoop cut it the way they cut the primitive.
  if (full) {
    Vec3 ca{}, cb{};
    double ra = 0.0, rb = 0.0;
    Conical c;
    if (RevolveProfileIsConical(profile, axisPoint, adir, &ca, &ra, &cb, &rb) && ConicalBetween(ca, ra, cb, rb, &c))
      StampConicalRecipe(&s, c);
  }
  *out = std::move(s);
  return Succeed(outWhy);
}

} // namespace brep
