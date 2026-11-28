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

/// Signed area of \p ring about \p n: positive when the ring winds CCW seen from the +n side.
[[nodiscard]] double RingSignedAreaAbout(const std::vector<Vec3>& ring, const Vec3& n) {
  Vec3 acc{};
  const std::size_t m = ring.size();
  for (std::size_t i = 0; i < m; ++i)
    acc = ray3d::Add(acc, ray3d::Cross(ring[i], ring[(i + 1) % m]));
  return 0.5 * ray3d::Dot(acc, n);
}
} // namespace

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
    const bool topo = why == Problem::EdgeNotUsedTwice || why == Problem::ShellOpenAtEdge ||
                      why == Problem::EdgeNonManifold || why == Problem::EdgeOrientationInconsistent ||
                      why == Problem::NotClosed;
    return Fail(topo ? complexReason : why, outWhy);
  }
  *out = std::move(s);
  return Succeed(outWhy);
}

namespace {

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
    return Fail(Problem::SliceCutCrossesCurvedEnd, outWhy);  // the ellipse would clip a cap

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
      return Fail(Problem::SliceResultInvalid, outWhy);
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

[[nodiscard]] bool ComputeConeObliqueEllipse(const ucs::Ucs& fr, double r0, double r1, double h,
                                             const Vec3& planePoint, const Vec3& pn, double eps,
                                             ConeObliqueEllipse* out) {
  const double k = (r1 - r0) / h;
  const Vec3 X = fr.xAxis;
  const Vec3 Y = fr.yAxis;
  const Vec3 Z = fr.zAxis;
  const bool posZ = ray3d::Dot(pn, Z) > 0.0;
  const Vec3 eN = posZ ? pn : ray3d::Scale(pn, -1.0);
  const double nx = ray3d::Dot(eN, X);
  const double ny = ray3d::Dot(eN, Y);
  const double nz = ray3d::Dot(eN, Z);
  const double amp = std::sqrt(nx * nx + ny * ny);
  if (!(std::fabs(nz) > std::fabs(k) * amp + eps))
    return false;  // parabola / hyperbola / tangent regime — not this recogniser

  const double C = ray3d::Dot(eN, ray3d::Sub(planePoint, fr.origin));
  const double z0 = C / nz;  // axis ∩ plane, local
  const double zApex = -r0 / k;
  const double zc0 = z0 - zApex;  // z0 in apex-centred local coordinates

  const Vec3 e1 = ray3d::Normalize(ray3d::Cross(eN, Z));
  const Vec3 e2 = ray3d::Normalize(ray3d::Cross(eN, e1));
  const Vec3 Q0 = ray3d::Add(fr.origin, ray3d::Scale(Z, z0));
  const double e1x = ray3d::Dot(e1, X), e1y = ray3d::Dot(e1, Y), e1z = ray3d::Dot(e1, Z);
  const double e2x = ray3d::Dot(e2, X), e2y = ray3d::Dot(e2, Y), e2z = ray3d::Dot(e2, Z);

  const double Acoef = (e1x * e1x + e1y * e1y) - k * k * e1z * e1z;
  const double Bcoef = (e2x * e2x + e2y * e2y) - k * k * e2z * e2z;
  const double Dcoef = (e1x * e2x + e1y * e2y) - k * k * e1z * e2z;
  const double Ecoef = -2.0 * k * k * zc0 * e1z;
  const double Fcoef = -2.0 * k * k * zc0 * e2z;
  const double Gcoef = -k * k * zc0 * zc0;

  // Centre in (p, q): solves [2A 2D; 2D 2B] v0 = [-E; -F].
  const double a11 = 2.0 * Acoef, a12 = 2.0 * Dcoef, a21 = 2.0 * Dcoef, a22 = 2.0 * Bcoef;
  const double det = a11 * a22 - a12 * a21;
  if (!(std::fabs(det) > 1e-12))
    return false;
  const double v0p = (-Ecoef * a22 - a12 * (-Fcoef)) / det;
  const double v0q = (a11 * (-Fcoef) - (-Ecoef) * a21) / det;
  const double K = Gcoef - (Acoef * v0p * v0p + 2.0 * Dcoef * v0p * v0q + Bcoef * v0q * v0q);

  const double trM = Acoef + Bcoef;
  const double diffM = Acoef - Bcoef;
  const double rad = std::sqrt((0.5 * diffM) * (0.5 * diffM) + Dcoef * Dcoef);
  const double lambda1 = 0.5 * trM + rad;
  const double lambda2 = 0.5 * trM - rad;
  const double theta = 0.5 * std::atan2(2.0 * Dcoef, diffM);
  const double a1sq = -K / lambda1;
  const double a2sq = -K / lambda2;
  if (!(a1sq > 0.0) || !(a2sq > 0.0))
    return false;  // not a real bounded ellipse (shouldn't happen once the regime test passed)
  const double a1 = std::sqrt(a1sq);
  const double a2 = std::sqrt(a2sq);
  const Vec3 dir1 = ray3d::Add(ray3d::Scale(e1, std::cos(theta)), ray3d::Scale(e2, std::sin(theta)));
  const Vec3 dir2 = ray3d::Add(ray3d::Scale(e1, -std::sin(theta)), ray3d::Scale(e2, std::cos(theta)));
  const Vec3 centre = ray3d::Add(Q0, ray3d::Add(ray3d::Scale(e1, v0p), ray3d::Scale(e2, v0q)));

  ConeObliqueEllipse r;
  r.centre = centre;
  r.normal = eN;
  if (a1 >= a2) {
    r.majorDir = dir1;
    r.majorSemi = a1;
    r.minorSemi = a2;
  } else {
    r.majorDir = dir2;
    r.majorSemi = a2;
    r.minorSemi = a1;
  }
  // Axial (local Z) extent: any ellipse point is centre + majorSemi*cos(t)*majorDir +
  // minorSemi*sin(t)*minorDir (minorDir = cross(normal, majorDir), matching AddEllipse's own frame),
  // so its local-Z coordinate is zCentre + majorSemi*cos(t)*d1z + minorSemi*sin(t)*d2z, whose extremes
  // are zCentre +/- sqrt((majorSemi*d1z)^2 + (minorSemi*d2z)^2).
  const Vec3 minorDir = ray3d::Cross(eN, r.majorDir);
  const double zCentre = ray3d::Dot(ray3d::Sub(centre, fr.origin), Z);
  const double d1z = ray3d::Dot(r.majorDir, Z);
  const double d2z = ray3d::Dot(minorDir, Z);
  const double R = std::sqrt((r.majorSemi * d1z) * (r.majorSemi * d1z) +
                             (r.minorSemi * d2z) * (r.minorSemi * d2z));
  r.zLo = zCentre - R;
  r.zHi = zCentre + R;
  *out = r;
  return true;
}

namespace {

/// Cut a right circular **cone** by an oblique plane whose angle keeps the cross-section an ellipse
/// (REQ-314 B2b-2 tail, GitHub issue #283, TASK-204). Mirrors `SliceCylinderOblique`'s topology and
/// piece-building exactly (a truncated cone with one flat rim and one elliptical rim), generalised via
/// \ref ComputeConeObliqueEllipse for the cone's non-constant radius. Parabola / hyperbola regimes are
/// not handled here (`*handled` stays false, so the caller's by-name refusal applies) — a later slice.
[[nodiscard]] bool SliceConeOblique(const Solid& solid, const Vec3& planePoint, const Vec3& pn,
                                    SliceKeep keep, Solid* outAbove, Solid* outBelow, bool* handled,
                                    Problem* outWhy) {
  *handled = false;
  const Recipe& rc = solid.recipe;
  if (rc.kind != PrimitiveKind::Cone)
    return false;
  const ucs::Ucs& fr = rc.frame;
  const Vec3 Z = fr.zAxis;
  const double r0 = rc.radius;
  const double r1 = rc.radius2;
  const double h = rc.height;
  const double dotNZ = ray3d::Dot(pn, Z);
  if (std::fabs(dotNZ) < 1e-6 || std::fabs(dotNZ) > 1.0 - 1e-9)
    return false;  // perpendicular / parallel — not this recogniser
  if (std::fabs(r1 - r0) < 1e-9 * std::max(h, 1.0))
    return false;  // r0 == r1 is a cylinder in a Cone recipe (shouldn't happen) — not this recogniser

  const double scale = std::max({h, r0, r1});
  const double eps = 1e-7 * std::max(scale, 1.0);

  ConeObliqueEllipse ce;
  if (!ComputeConeObliqueEllipse(fr, r0, r1, h, planePoint, pn, eps, &ce))
    return false;  // parabola / hyperbola / tangent — a later slice
  *handled = true;
  if (ce.zLo <= eps || ce.zHi >= h - eps)
    return Fail(Problem::SliceCutCrossesCurvedEnd, outWhy);  // the ellipse would clip a cap

  const double k = (r1 - r0) / h;
  auto radiusAt = [&](double z) { return r0 + k * z; };
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(fr, Vec3{x, y, z}); };
  const Vec3 minorDir = ray3d::Cross(ce.normal, ce.majorDir);
  auto tOf = [&](const Vec3& p) {
    const Vec3 rel = ray3d::Sub(p, ce.centre);
    const double c = ray3d::Dot(rel, ce.majorDir) / ce.majorSemi;
    const double sn = ray3d::Dot(rel, minorDir) / ce.minorSemi;
    return std::atan2(sn, c);
  };
  // The cone-generator/plane crossing at azimuth u = 0 / pi (the seam where the ellipse meets the
  // cone's own u = 0 / pi generators) — the exact rational z(u) from ComputeConeObliqueEllipse's own
  // derivation, evaluated directly rather than re-deriving through the ellipse parametrisation.
  const double nx = ray3d::Dot(ce.normal, fr.xAxis);
  const double ny = ray3d::Dot(ce.normal, fr.yAxis);
  const double nz = ray3d::Dot(ce.normal, fr.zAxis);
  const double C = ray3d::Dot(ce.normal, ray3d::Sub(planePoint, fr.origin));
  auto zOfU = [&](double u) {
    const double a = nx * std::cos(u) + ny * std::sin(u);
    return (C - r0 * a) / (k * a + nz);
  };
  const double z0seam = zOfU(0.0);
  const double zPseam = zOfU(kPi);
  const Vec3 P0 = W(radiusAt(z0seam), 0.0, z0seam);
  const Vec3 PP = W(-radiusAt(zPseam), 0.0, zPseam);
  const double t0 = tOf(P0);
  const double tP = tOf(PP);
  // Cone-azimuth u and the ellipse's own parameter t need not advance in the same rotational sense,
  // so each half's signed sweep is found independently — via its own interior witness point — rather
  // than assumed to complement the other to a full 2*pi.
  auto onArc = [&](double tStart, double sweep, double tTest) {
    double d = tTest - tStart;
    while (d <= -kPi) d += kTwoPi;
    while (d > kPi) d -= kTwoPi;
    if (sweep > 0.0 && d < 0.0) d += kTwoPi;
    if (sweep < 0.0 && d > 0.0) d -= kTwoPi;
    return sweep > 0.0 ? (d >= -1e-6 && d <= sweep + 1e-6) : (d <= 1e-6 && d >= sweep - 1e-6);
  };
  auto arcSweep = [&](double tStart, double tEnd, double tWitness) {
    double sweepPos = tEnd - tStart;
    while (sweepPos <= 0.0) sweepPos += kTwoPi;
    while (sweepPos > kTwoPi) sweepPos -= kTwoPi;
    return onArc(tStart, sweepPos, tWitness) ? sweepPos : sweepPos - kTwoPi;
  };
  const double zMidLower = zOfU(kHalfPi);
  const double tMidLower = tOf(W(0.0, radiusAt(zMidLower), zMidLower));
  const double sweepLower = arcSweep(t0, tP, tMidLower);
  const double zMidUpper = zOfU(1.5 * kPi);
  const double tMidUpper = tOf(W(0.0, -radiusAt(zMidUpper), zMidUpper));
  const double sweepUpper = arcSweep(tP, t0, tMidUpper);

  auto build = [&](bool upper, Solid* dst) -> bool {
    Solid s;
    const int se0 = AddVertex(&s, P0);
    const int se1 = AddVertex(&s, PP);
    const double rimR = upper ? r1 : r0;
    const int rimZ0 = AddVertex(&s, W(rimR, 0.0, upper ? h : 0.0));
    const int rimZ1 = AddVertex(&s, W(-rimR, 0.0, upper ? h : 0.0));

    const Vec3 rimC = W(0.0, 0.0, upper ? h : 0.0);
    const int rr0 = AddArc(&s, rimZ0, rimZ1, rimC, Z, kPi);
    const int rr1 = AddArc(&s, rimZ1, rimZ0, rimC, Z, kPi);
    const int el0 = AddEllipse(&s, se0, se1, ce.centre, ce.normal, ce.majorDir, ce.majorSemi,
                               ce.minorSemi, sweepLower);
    const int el1 = AddEllipse(&s, se1, se0, ce.centre, ce.normal, ce.majorDir, ce.majorSemi,
                               ce.minorSemi, sweepUpper);
    const int sm0 = AddLine(&s, upper ? se0 : rimZ0, upper ? rimZ0 : se0);
    const int sm1 = AddLine(&s, upper ? se1 : rimZ1, upper ? rimZ1 : se1);

    if (upper)
      s.faces.push_back(MakePlaneFace(rimC, Z, {{rr0, false}, {rr1, false}}));
    else
      s.faces.push_back(MakePlaneFace(rimC, ray3d::Scale(Z, -1.0), {{rr1, true}, {rr0, true}}));
    const Vec3 cutN = upper ? ray3d::Scale(ce.normal, -1.0) : ce.normal;
    if (upper)
      s.faces.push_back(MakePlaneFace(ce.centre, cutN, {{el1, true}, {el0, true}}));
    else
      s.faces.push_back(MakePlaneFace(ce.centre, cutN, {{el0, false}, {el1, false}}));

    auto side = [&](double u0, double u1, std::vector<EdgeUse> uses) {
      Face f;
      f.surface.kind = SurfaceKind::Cone;
      f.surface.frame = fr;
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
      return Fail(Problem::SliceResultInvalid, outWhy);
    *dst = std::move(s);
    return true;
  };

  const bool wantAbove = keep == SliceKeep::Above || keep == SliceKeep::Both;
  const bool wantBelow = keep == SliceKeep::Below || keep == SliceKeep::Both;
  const bool upperIsAbove = dotNZ > 0.0;
  Solid* upperDst = upperIsAbove ? outAbove : outBelow;
  Solid* lowerDst = upperIsAbove ? outBelow : outAbove;
  const bool wantUpper = upperIsAbove ? wantAbove : wantBelow;
  const bool wantLower = upperIsAbove ? wantBelow : wantAbove;

  Solid probe;
  if (!build(true, &probe) || !build(false, &probe))
    return false;
  if (wantUpper && upperDst && !build(true, upperDst))
    return false;
  if (wantLower && lowerDst && !build(false, lowerDst))
    return false;
  return Succeed(outWhy);
}

/// One place the cone-generator/plane crossing `z(u)` enters or leaves `(0, h)` (TASK-204 slice (b)):
/// `rim` says which rim it touches there (`false` = base, `true` = top).
struct ConeCutTransition {
  double u = 0.0;
  bool rim = false;
};

/// Every point around the full circle where the exact rational `z(u) = (C - r0*A(u)) / (k*A(u) + nz)`
/// enters or leaves `(0, h)` — the parabola/hyperbola regime's cut can, in general, touch this band
/// along zero, one or two disjoint stretches of `u` (TASK-204's "PROGRESS 2026-09-05 (2)" finding,
/// checked by direct sampling before this was coded). A dense scan-and-bisect, mirroring the witness
/// scans used elsewhere in this file (e.g. `FindGeneralBranchSeams`) — `z(u)` is cheap and exact, so
/// there is no marching error to worry about, only where the level crossings land. Samples that jump
/// straight past the whole `(0, h)` band between two adjacent grid points (only possible immediately
/// next to a pole, where `z(u)` runs to `+-infinity`) are dropped rather than guessed at: they never
/// bound a real cut interval (the frustum's own rim is what actually stops the material there).
[[nodiscard]] std::vector<ConeCutTransition> FindConeCutTransitions(double r0, double h, double k,
                                                                    double nx, double ny, double nz,
                                                                    double C) {
  auto A = [&](double u) { return nx * std::cos(u) + ny * std::sin(u); };
  auto zOfU = [&](double u) { return (C - r0 * A(u)) / (k * A(u) + nz); };
  constexpr int kSamples = 2000;
  auto state = [&](double z) {
    if (!std::isfinite(z))
      return z > 0.0 ? 1 : -1;
    if (z <= 0.0)
      return -1;
    if (z >= h)
      return 1;
    return 0;
  };
  auto bisect = [&](double u0, double u1, double level) {
    double lo = u0, hi = u1;
    double glo = zOfU(lo) - level;
    for (int k2 = 0; k2 < 60; ++k2) {
      const double mid = 0.5 * (lo + hi);
      const double gm = zOfU(mid) - level;
      if ((glo <= 0.0) != (gm <= 0.0))
        hi = mid;
      else {
        lo = mid;
        glo = gm;
      }
    }
    return 0.5 * (lo + hi);
  };
  std::vector<double> us(kSamples);
  std::vector<double> zs(kSamples);
  for (int i = 0; i < kSamples; ++i) {
    us[static_cast<std::size_t>(i)] = kTwoPi * static_cast<double>(i) / kSamples;
    zs[static_cast<std::size_t>(i)] = zOfU(us[static_cast<std::size_t>(i)]);
  }
  std::vector<ConeCutTransition> out;
  for (int i = 0; i < kSamples; ++i) {
    const int j = (i + 1) % kSamples;
    const double ua = us[static_cast<std::size_t>(i)];
    const double ub = j == 0 ? ua + kTwoPi : us[static_cast<std::size_t>(j)];
    const int sa = state(zs[static_cast<std::size_t>(i)]);
    const int sb = state(zs[static_cast<std::size_t>(j)]);
    if (sa == sb)
      continue;
    if ((sa == -1 && sb == 0) || (sa == 0 && sb == -1))
      out.push_back({bisect(ua, ub, 0.0), false});
    else if ((sa == 0 && sb == 1) || (sa == 1 && sb == 0))
      out.push_back({bisect(ua, ub, h), true});
    // sa/sb == {-1,1} (skipped straight past the band, only next to a pole): no real crossing here.
  }
  for (ConeCutTransition& t : out) t.u = std::fmod(std::fmod(t.u, kTwoPi) + kTwoPi, kTwoPi);
  std::sort(out.begin(), out.end(), [](const ConeCutTransition& a, const ConeCutTransition& b) {
    return a.u < b.u;
  });
  return out;
}

/// Cut a right circular **cone** by an oblique plane whose angle puts the cross-section in the
/// parabola/hyperbola regime (TASK-204 slice (b), GitHub issue #283) — narrowly, for now: only when
/// the cut touches the frustum's lateral surface along exactly **one** stretch of azimuth, with both
/// ends landing on the **same** rim (both base, or both top). This is a single wedge-shaped bite out
/// of one side of the frustum. Every other configuration (two disjoint stretches, confirmed possible
/// in this regime, or one stretch spanning both rims) falls through to `Problem::SliceCurvedFace` —
/// see TASK-204's progress notes for the full topology and why it is scoped narrower here.
[[nodiscard]] bool SliceConeObliqueOpenNotch(const Solid& solid, const Vec3& planePoint, const Vec3& pn,
                                             SliceKeep keep, Solid* outAbove, Solid* outBelow,
                                             bool* handled, Problem* outWhy) {
  *handled = false;
  const Recipe& rc = solid.recipe;
  if (rc.kind != PrimitiveKind::Cone)
    return false;
  const ucs::Ucs& fr = rc.frame;
  const Vec3 Z = fr.zAxis;
  const double r0 = rc.radius;
  const double r1 = rc.radius2;
  const double h = rc.height;
  const double dotNZ = ray3d::Dot(pn, Z);
  if (std::fabs(dotNZ) < 1e-6 || std::fabs(dotNZ) > 1.0 - 1e-9)
    return false;  // perpendicular / parallel — not this recogniser
  if (std::fabs(r1 - r0) < 1e-9 * std::max(h, 1.0))
    return false;  // r0 == r1 is a cylinder in a Cone recipe (shouldn't happen) — not this recogniser

  const double k = (r1 - r0) / h;
  const double nx = ray3d::Dot(pn, fr.xAxis);
  const double ny = ray3d::Dot(pn, fr.yAxis);
  const double nz = ray3d::Dot(pn, fr.zAxis);
  const double amp = std::sqrt(nx * nx + ny * ny);
  const double scale = std::max({h, r0, r1});
  const double eps = 1e-7 * std::max(scale, 1.0);
  if (std::fabs(nz) > std::fabs(k) * amp + eps)
    return false;  // the ellipse regime — SliceConeOblique's own remit

  const double C = ray3d::Dot(pn, ray3d::Sub(planePoint, fr.origin));
  const std::vector<ConeCutTransition> trans = FindConeCutTransitions(r0, h, k, nx, ny, nz, C);
  if (trans.size() != 2 || trans[0].rim != trans[1].rim)
    return false;  // 0 or 2+ disjoint notches, or a notch spanning both rims — a later slice

  *handled = true;
  const bool onTop = trans[0].rim;
  const double uA = trans[0].u;
  const double uB = trans[1].u;
  const double notchSweep = uB - uA;
  if (!(notchSweep > eps) || !(notchSweep < kTwoPi - eps))
    return Fail(Problem::SliceCutCrossesCurvedEnd, outWhy);

  auto radiusAt = [&](double z) { return r0 + k * z; };
  auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(fr, Vec3{x, y, z}); };
  auto A = [&](double u) { return nx * std::cos(u) + ny * std::sin(u); };
  auto zOfU = [&](double u) { return (C - r0 * A(u)) / (k * A(u) + nz); };
  const double cutRimZ = onTop ? h : 0.0;
  const double cutRimR = onTop ? r1 : r0;
  const double otherRimZ = onTop ? 0.0 : h;
  const double otherRimR = onTop ? r0 : r1;
  const Vec3 pA = W(cutRimR * std::cos(uA), cutRimR * std::sin(uA), cutRimZ);
  const Vec3 pB = W(cutRimR * std::cos(uB), cutRimR * std::sin(uB), cutRimZ);

  Surface coneSurf;
  coneSurf.kind = SurfaceKind::Cone;
  coneSurf.frame = fr;
  coneSurf.radius = r0;
  coneSurf.radius2 = r1;
  coneSurf.height = h;
  Surface planeSurf = PlaneSurface(planePoint, pn);

  auto addCutEdge = [&](Solid* s, int v0, int v1, double witnessU) {
    Edge e;
    e.kind = CurveKind::Intersection;
    e.v0 = v0;
    e.v1 = v1;
    const double zw = zOfU(witnessU);
    e.frame.origin = W(radiusAt(zw) * std::cos(witnessU), radiusAt(zw) * std::sin(witnessU), zw);
    e.isectSurfaces = {coneSurf, planeSurf};
    s->edges.push_back(e);
    return static_cast<int>(s->edges.size()) - 1;
  };
  const double uMid = 0.5 * (uA + uB);

  // The small wedge sliced off: 2v / 3e / 3f, chi = 2 (the rim-disk segment, the cutting-plane cap,
  // one cone wall band). Built with the CUT rim's own material kept — the "other" side of the plane
  // from the big notched piece below.
  auto buildWedge = [&](Solid* dst) -> bool {
    Solid s;
    const int v0 = AddVertex(&s, pA);
    const int v1 = AddVertex(&s, pB);
    const Vec3 rimC = W(0.0, 0.0, cutRimZ);
    const int rimMinor = AddArc(&s, v0, v1, rimC, Z, notchSweep);
    const int chord = AddLine(&s, v1, v0);
    const int cut = addCutEdge(&s, v0, v1, uMid);
    const Vec3 rimN = onTop ? Z : ray3d::Scale(Z, -1.0);
    // rimMinor is CCW about +Z by construction (AddArc's own convention); the base rim's outward
    // normal is -Z, so its loop must run the OPPOSITE sense from the top rim's — same flip
    // SliceConeOblique's own build() lambda applies between its "upper" and "lower" caps.
    s.faces.push_back(
        MakePlaneFace(rimC, rimN, {{rimMinor, !onTop}, {chord, !onTop}}));
    // chord is shared with the rim cap above, which now uses it as `!onTop` — the two faces sharing
    // an edge must use it once each way, so this one uses `onTop` (cut must match, by the same
    // same-two-vertices closure constraint as chord+rimMinor above). That forces this loop's own
    // winding, so the normal that makes its area positive is found empirically, not guessed:
    // opposite of the naive onTop-follows-pn guess.
    const Vec3 cutN = onTop ? ray3d::Scale(pn, -1.0) : pn;
    s.faces.push_back(MakePlaneFace(planePoint, cutN, {{chord, onTop}, {cut, onTop}}));
    Face wall;
    wall.surface = coneSurf;
    wall.uStart = uA;
    wall.uEnd = uB;
    // rimMinor and cut are each shared with one of the two caps above (`!onTop` and `onTop`
    // respectively) — the wall's own uses must be the opposite of those, once each way.
    wall.loops.push_back(Loop{{{rimMinor, onTop}, {cut, !onTop}}});
    s.faces.push_back(std::move(wall));
    AddSingleShell(&s);
    if (Validate(s) != Problem::Ok || SelfIntersects(s))
      return Fail(Problem::SliceResultInvalid, outWhy);
    *dst = std::move(s);
    return true;
  };

  // The big notched piece: 4v / 7e / 5f, chi = 2. Same wedge cap reused with opposite winding, plus
  // the untouched rim (both caps at the FAR end) and the majority wall band spanning the rest of the
  // circle at full height.
  auto buildNotched = [&](Solid* dst) -> bool {
    Solid s;
    const int v0 = AddVertex(&s, pA);
    const int v1 = AddVertex(&s, pB);
    const Vec3 cutRimC = W(0.0, 0.0, cutRimZ);
    const int rimMajor = AddArc(&s, v1, v0, cutRimC, Z, kTwoPi - notchSweep);
    const int chord = AddLine(&s, v0, v1);
    const int cut = addCutEdge(&s, v0, v1, uMid);
    const Vec3 rimN = onTop ? Z : ray3d::Scale(Z, -1.0);
    s.faces.push_back(MakePlaneFace(cutRimC, rimN, {{rimMajor, !onTop}, {chord, !onTop}}));
    const Vec3 cutN = onTop ? ray3d::Scale(pn, -1.0) : pn;
    // chord must be used oppositely from the rim cap's `!onTop` (shared edge, once each way) — here
    // `chord`'s own vertex order (built the other way round from the wedge's) needs `cut` opposite
    // to `chord` in turn for this loop's own closure, unlike the wedge's matching pair above.
    s.faces.push_back(MakePlaneFace(planePoint, cutN, {{chord, onTop}, {cut, !onTop}}));

    const Vec3 qA = W(otherRimR * std::cos(uA), otherRimR * std::sin(uA), otherRimZ);
    const Vec3 qB = W(otherRimR * std::cos(uB), otherRimR * std::sin(uB), otherRimZ);
    const int q0 = AddVertex(&s, qA);
    const int q1 = AddVertex(&s, qB);
    const Vec3 otherRimC = W(0.0, 0.0, otherRimZ);
    const int otherMajor = AddArc(&s, q1, q0, otherRimC, Z, kTwoPi - notchSweep);
    const int otherMinor = AddArc(&s, q0, q1, otherRimC, Z, notchSweep);
    const Vec3 otherN = onTop ? ray3d::Scale(Z, -1.0) : Z;
    s.faces.push_back(MakePlaneFace(otherRimC, otherN, {{otherMajor, onTop}, {otherMinor, onTop}}));

    const int seamA = AddLine(&s, v0, q0);
    const int seamB = AddLine(&s, v1, q1);

    Face wallMajor;
    wallMajor.surface = coneSurf;
    wallMajor.uStart = uB;
    wallMajor.uEnd = uA + kTwoPi;
    // The base/top rim major arcs and the two seams, chased vertex-by-vertex (independently for
    // each case — cone-azimuth "which is bottom" swaps with onTop, and the two cases are not a
    // single ternary-per-slot away from each other, so each is spelled out explicitly).
    if (onTop) {
      wallMajor.loops.push_back(
          Loop{{{otherMajor, false}, {seamA, true}, {rimMajor, true}, {seamB, false}}});
    } else {
      wallMajor.loops.push_back(
          Loop{{{rimMajor, false}, {seamA, false}, {otherMajor, true}, {seamB, true}}});
    }
    s.faces.push_back(std::move(wallMajor));

    Face wallNotch;
    wallNotch.surface = coneSurf;
    wallNotch.uStart = uA;
    wallNotch.uEnd = uB;
    if (onTop) {
      wallNotch.loops.push_back(
          Loop{{{otherMinor, false}, {seamB, true}, {cut, true}, {seamA, false}}});
    } else {
      wallNotch.loops.push_back(
          Loop{{{cut, false}, {seamB, false}, {otherMinor, true}, {seamA, true}}});
    }
    s.faces.push_back(std::move(wallNotch));

    AddSingleShell(&s);
    if (Validate(s) != Problem::Ok || SelfIntersects(s))
      return Fail(Problem::SliceResultInvalid, outWhy);
    *dst = std::move(s);
    return true;
  };

  const bool wantAbove = keep == SliceKeep::Above || keep == SliceKeep::Both;
  const bool wantBelow = keep == SliceKeep::Below || keep == SliceKeep::Both;
  // The wedge keeps the material on the -pn side (confirmed empirically against an independent
  // reference — see TASK-204); "above" is the +pn side.
  Solid* wedgeDst = outBelow;
  Solid* notchedDst = outAbove;
  bool wantWedge = wantBelow;
  bool wantNotched = wantAbove;
  if (dotNZ < 0.0) {
    // pn points toward -Z-ish: swap so "above" still means the +pn side.
    std::swap(wedgeDst, notchedDst);
    std::swap(wantWedge, wantNotched);
  }

  Solid probe;
  if (!buildWedge(&probe) || !buildNotched(&probe))
    return false;
  if (wantWedge && wedgeDst && !buildWedge(wedgeDst))
    return false;
  if (wantNotched && notchedDst && !buildNotched(notchedDst))
    return false;
  return Succeed(outWhy);
}

/// Cut a right circular cylinder or cone by a plane **parallel to its axis** — the vertical cut
/// through a standing pipe, culvert or manhole (GitHub issue #517, REQ-314 / REQ-335). Every edge the
/// cut makes is straight: two seam lines where the plane meets the side, and a chord across each cap.
/// So a cylinder cut anywhere across its width leaves a rectangle, and a cone cut through its axis
/// leaves a trapezoid (a triangle for a cone that comes to a point).
///
/// A cone cut parallel to its axis but NOT through it meets the side in a hyperbola, which the
/// kernel's curves cannot hold; that is refused by name (\ref Problem::SliceCutConeOffAxis).
///
/// Each piece is the solid between one arc of the rim and its chord: a planar cap at each end (arc
/// plus chord), one side face spanning that arc, and the planar cut face. A plane whose normal is
/// within 1e-6 of perpendicular to the axis is taken as parallel — the oblique recognisers decline
/// the same planes — which moves a vertex off the true plane by at most 1e-6 of the height.
[[nodiscard]] bool SliceConicalAlongAxis(const Solid& solid, const Vec3& planePoint, const Vec3& pn,
                                         SliceKeep keep, Solid* outAbove, Solid* outBelow, bool* handled,
                                         Problem* outWhy) {
  *handled = false;
  const Recipe& rc = solid.recipe;
  if (rc.kind != PrimitiveKind::Cylinder && rc.kind != PrimitiveKind::Cone)
    return false;
  const ucs::Ucs& fr = rc.frame;
  const Vec3 Z = fr.zAxis;
  if (std::fabs(ray3d::Dot(pn, Z)) >= 1e-6)
    return false;  // not parallel to the axis — another recogniser's cut

  *handled = true;
  const bool cone = rc.kind == PrimitiveKind::Cone;
  const double r0 = rc.radius;
  const double r1 = cone ? rc.radius2 : rc.radius;
  const double h = rc.height;
  const bool apex = !(r1 > 0.0);
  const double scale = std::max(h, r0);
  const double eps = 1e-7 * std::max(scale, 1.0);

  // The plane's trace on the base: the points p with n.p = s, n the in-plane unit normal.
  const Vec3 pl = ucs::WorldToUcs(fr, planePoint);
  const double nx0 = ray3d::Dot(pn, fr.xAxis);
  const double ny0 = ray3d::Dot(pn, fr.yAxis);
  const double nLen = std::sqrt(nx0 * nx0 + ny0 * ny0);
  const double nx = nx0 / nLen;
  const double ny = ny0 / nLen;
  const double s = nx * pl.x + ny * pl.y;  // signed distance of the axis from the plane, along -n
  if (std::fabs(s) >= r0 - eps)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);
  if (cone && std::fabs(s) > eps)
    return Fail(Problem::SliceCutConeOffAxis, outWhy);  // a hyperbola, not a straight line
  const double sCut = cone ? 0.0 : s;

  // The rim points the plane passes through sit at angles phi -/+ alpha; the arc between them
  // through phi is on the +n side ("above").
  const double phi = std::atan2(ny, nx);
  const double alpha = std::acos(std::clamp(sCut / r0, -1.0, 1.0));
  auto wrap = [](double u) {
    u = std::fmod(u, kTwoPi);
    return u < 0.0 ? u + kTwoPi : u;
  };

  auto W = [&](double r, double u, double z) {
    return ucs::UcsToWorld(fr, Vec3{r * std::cos(u), r * std::sin(u), z});
  };

  // Build the piece whose side runs CCW from u0 through u0 + span.
  auto build = [&](double u0, double span, Solid* dst) -> bool {
    const double u1 = u0 + span;
    Solid s1;
    const int b0 = AddVertex(&s1, W(r0, u0, 0.0));
    const int b1 = AddVertex(&s1, W(r0, u1, 0.0));
    const int t0 = apex ? AddVertex(&s1, ucs::UcsToWorld(fr, Vec3{0.0, 0.0, h})) : AddVertex(&s1, W(r1, u0, h));
    const int t1 = apex ? t0 : AddVertex(&s1, W(r1, u1, h));

    const Vec3 baseC = fr.origin;
    const Vec3 topC = ray3d::Add(fr.origin, ray3d::Scale(Z, h));
    const int arcB = AddArc(&s1, b0, b1, baseC, Z, span);
    const int chordB = AddLine(&s1, b0, b1);
    const int sm0 = AddLine(&s1, b0, t0);
    const int sm1 = AddLine(&s1, b1, t1);
    int arcT = -1;
    int chordT = -1;
    if (!apex) {
      arcT = AddArc(&s1, t0, t1, topC, Z, span);
      chordT = AddLine(&s1, t0, t1);
    }

    // Caps: the material is between the arc and its chord.
    s1.faces.push_back(MakePlaneFace(baseC, ray3d::Scale(Z, -1.0), {{chordB, false}, {arcB, true}}));
    if (!apex)
      s1.faces.push_back(MakePlaneFace(topC, Z, {{arcT, false}, {chordT, true}}));

    Face side;
    side.surface.kind = cone ? SurfaceKind::Cone : SurfaceKind::Cylinder;
    side.surface.frame = fr;
    side.surface.radius = r0;
    side.surface.radius2 = r1;
    side.surface.height = h;
    side.uStart = u0;
    side.uEnd = u1;
    Loop lp;
    if (apex)
      lp.uses = {{arcB, false}, {sm1, false}, {sm0, true}};
    else
      lp.uses = {{arcB, false}, {sm1, false}, {arcT, true}, {sm0, true}};
    side.loops.push_back(std::move(lp));
    s1.faces.push_back(std::move(side));

    // The cut face lies on the caller's plane, so it takes the caller's point and normal exactly;
    // it faces away from the material, which is on the side of the arc's midpoint.
    const Vec3 mid{std::cos(u0 + 0.5 * span), std::sin(u0 + 0.5 * span), 0.0};
    const Vec3 cutN = mid.x * nx + mid.y * ny > 0.0 ? ray3d::Scale(pn, -1.0) : pn;
    if (apex)
      s1.faces.push_back(MakePlaneFace(planePoint, cutN, {{sm0, false}, {sm1, true}, {chordB, true}}));
    else
      s1.faces.push_back(
          MakePlaneFace(planePoint, cutN, {{sm0, false}, {chordT, false}, {sm1, true}, {chordB, true}}));

    AddSingleShell(&s1);
    if (Validate(s1) != Problem::Ok)
      return Fail(Problem::SliceResultInvalid, outWhy);
    *dst = std::move(s1);
    return true;
  };

  const double aboveU0 = wrap(phi - alpha);
  const double belowU0 = wrap(phi + alpha);
  const double aboveSpan = 2.0 * alpha;
  const double belowSpan = kTwoPi - 2.0 * alpha;
  const bool wantAbove = keep == SliceKeep::Above || keep == SliceKeep::Both;
  const bool wantBelow = keep == SliceKeep::Below || keep == SliceKeep::Both;

  // Prove both build before writing either output (REQ-201).
  Solid probe;
  if (!build(aboveU0, aboveSpan, &probe) || !build(belowU0, belowSpan, &probe))
    return false;  // build() already set outWhy
  if (wantAbove && outAbove && !build(aboveU0, aboveSpan, outAbove))
    return false;
  if (wantBelow && outBelow && !build(belowU0, belowSpan, outBelow))
    return false;
  return Succeed(outWhy);
}

/// The signed-distance range `[lo, hi]` a boundary edge covers relative to a plane (GitHub #518).
///
/// Exact for a line (its endpoints), and for an arc or ellipse, whose distance along the curve is
/// `k + A cos(th) + B sin(th)`: the extremes are the endpoints and the stationary angles that fall
/// inside the sweep. An intersection curve is marched and widened by \p margin, because its polyline
/// is only close to the curve.
struct DistRange {
  double lo = 1e300;
  double hi = -1e300;
  void add(double d) {
    lo = std::min(lo, d);
    hi = std::max(hi, d);
  }
};

[[nodiscard]] DistRange EdgeDistRange(const Solid& s, const Edge& e, const Vec3& planePoint, const Vec3& pn,
                                      double margin) {
  auto sd = [&](const Vec3& p) { return ray3d::Dot(ray3d::Sub(p, planePoint), pn); };
  DistRange r;
  r.add(sd(s.vertices[static_cast<std::size_t>(e.v0)].p));
  r.add(sd(s.vertices[static_cast<std::size_t>(e.v1)].p));
  if (e.kind == CurveKind::Line)
    return r;
  if (e.kind == CurveKind::Arc || e.kind == CurveKind::Ellipse) {
    const double r2 = e.kind == CurveKind::Arc ? e.radius : e.radius2;
    const double k = sd(e.frame.origin);
    const double A = e.radius * ray3d::Dot(e.frame.xAxis, pn);
    const double B = r2 * ray3d::Dot(e.frame.yAxis, pn);
    const double th0 = e.kind == CurveKind::Arc ? 0.0 : EllipseParamOf(e, s.vertices[static_cast<std::size_t>(e.v0)].p);
    const double lo = std::min(th0, th0 + e.sweep);
    const double hi = std::max(th0, th0 + e.sweep);
    const double star = std::atan2(B, A);  // the maximum; the minimum is half a turn on
    for (const double base : {star, star + kPi}) {
      // Every angle base + 2*pi*n inside [lo, hi].
      double th = base + kTwoPi * std::ceil((lo - base) / kTwoPi);
      for (; th <= hi; th += kTwoPi)
        r.add(k + A * std::cos(th) + B * std::sin(th));
    }
    return r;
  }
  for (const Vec3& p : MarchIntersectionCurve(s, e, 256))
    r.add(sd(p));
  r.lo -= margin;
  r.hi += margin;
  return r;
}

/// Cut a solid that has curved faces by a plane that crosses only its FLAT faces (GitHub #518).
///
/// A filleted box, or a box with a drilled hole, was refused at every plane because one of its faces
/// is curved — including planes nowhere near that face. Here each face is classified by the range of
/// signed distance it covers:
/// - a face wholly on one side (touching the plane is allowed) goes to that piece **unchanged**, with
///   its surface, parameter range and every loop, so a fillet or a bore is carried, not rebuilt;
/// - a planar face the plane crosses is split along the plane, as the all-planar cutter does, keeping
///   each whole arc or ellipse edge of its boundary on the side it lies on, and any hole loop on the
///   side it lies on;
/// - a curved face, or a curved edge, the plane crosses is refused as
///   \ref Problem::SliceCutCrossesCurvedFace.
///
/// The range of a plane, cylinder or cone face is the range of its boundary: a linear function on
/// those surfaces is linear along each straight generator, so an interior extreme would run out to
/// the boundary. A sphere, torus or NURBS face can bulge past its boundary, so its range also takes
/// the tessellation's vertices and is widened by the chord tolerance — conservative, so a plane
/// grazing such a face is refused rather than cut wrongly.
///
/// The all-planar solids keep their own cutter: this one runs only for a solid with a curved face or
/// edge that the primitive recognisers did not take.
[[nodiscard]] bool SliceCarryingCurvedFaces(const Solid& solid, const Vec3& planePoint, const Vec3& pn,
                                            SliceKeep keep, Solid* outAbove, Solid* outBelow, Problem* outWhy) {
  const double scale = ModelScale(solid);
  const double eps = 1e-7 * scale;
  const double margin = 1e-4 * scale;
  auto sd = [&](const Vec3& p) { return ray3d::Dot(ray3d::Sub(p, planePoint), pn); };

  // --- Classify every face by the distance range it covers -------------------------------------
  std::vector<DistRange> edgeRange(solid.edges.size());
  for (std::size_t i = 0; i < solid.edges.size(); ++i)
    edgeRange[i] = EdgeDistRange(solid, solid.edges[i], planePoint, pn, margin);

  bool needMesh = false;
  for (const Face& f : solid.faces)
    if (f.surface.kind == SurfaceKind::Sphere || f.surface.kind == SurfaceKind::Torus ||
        f.surface.kind == SurfaceKind::Nurbs)
      needMesh = true;
  std::vector<DistRange> meshRange(solid.faces.size());
  if (needMesh) {
    Tessellation t;
    Problem tw = Problem::Ok;
    if (!Tessellate(solid, margin, &t, &tw))
      return Fail(tw, outWhy);
    for (std::size_t tri = 0; tri < t.triFace.size(); ++tri) {
      const std::size_t fi = static_cast<std::size_t>(t.triFace[tri]);
      for (int c = 0; c < 3; ++c) {
        const std::size_t vi = t.indices[3 * tri + static_cast<std::size_t>(c)];
        meshRange[fi].add(sd(Vec3{t.vertsXyz[3 * vi], t.vertsXyz[3 * vi + 1], t.vertsXyz[3 * vi + 2]}));
      }
    }
  }

  enum class Side { Above, Below, Split };
  std::vector<Side> side(solid.faces.size());
  bool anyAbove = false;
  bool anyBelow = false;
  for (std::size_t fi = 0; fi < solid.faces.size(); ++fi) {
    const Face& f = solid.faces[fi];
    DistRange r;
    for (const Loop& lp : f.loops)
      for (const EdgeUse& u : lp.uses) {
        const DistRange& er = edgeRange[static_cast<std::size_t>(u.edge)];
        r.add(er.lo);
        r.add(er.hi);
      }
    const bool meshed = f.surface.kind == SurfaceKind::Sphere || f.surface.kind == SurfaceKind::Torus ||
                        f.surface.kind == SurfaceKind::Nurbs;
    if (meshed && meshRange[fi].hi >= meshRange[fi].lo) {
      r.add(meshRange[fi].lo - margin);
      r.add(meshRange[fi].hi + margin);
    }
    if (r.hi > eps)
      anyAbove = true;
    if (r.lo < -eps)
      anyBelow = true;
    if (r.lo >= -eps)
      side[fi] = Side::Above;
    else if (r.hi <= eps)
      side[fi] = Side::Below;
    else if (f.surface.kind != SurfaceKind::Plane)
      return Fail(Problem::SliceCutCrossesCurvedFace, outWhy);
    else
      side[fi] = Side::Split;
  }
  if (!anyAbove || !anyBelow)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);

  // --- Each piece as a list of faces, each loop a ring of segments ------------------------------
  // A segment runs from `p` to the next segment's `p`. `src` is the source edge it reproduces whole
  // (with its use direction), or -1 for a straight segment the cut made or shortened.
  struct Seg {
    Vec3 p;
    int src = -1;
    bool rev = false;
  };
  struct PieceFace {
    Face face;  // surface and parameter data; its loops are rebuilt from `rings`
    std::vector<std::vector<Seg>> rings;
  };
  std::vector<PieceFace> above;
  std::vector<PieceFace> below;
  std::vector<std::pair<Vec3, Vec3>> cutSegs;

  const double weldEps = std::max(1e-7 * scale, 1e-12);
  auto same = [&](const Vec3& a, const Vec3& b) { return ray3d::Length(ray3d::Sub(a, b)) <= weldEps; };
  auto addCutSeg = [&](const Vec3& a, const Vec3& b) {
    if (same(a, b))
      return;
    for (const auto& c : cutSegs)
      if ((same(c.first, a) && same(c.second, b)) || (same(c.first, b) && same(c.second, a)))
        return;  // an on-plane edge is shared by two faces; record it once
    cutSegs.push_back({a, b});
  };
  auto startOf = [&](const EdgeUse& u) {
    const Edge& e = solid.edges[static_cast<std::size_t>(u.edge)];
    return solid.vertices[static_cast<std::size_t>(u.reversed ? e.v1 : e.v0)].p;
  };
  auto wholeRing = [&](const Loop& lp) {
    std::vector<Seg> ring;
    for (const EdgeUse& u : lp.uses) {
      const bool curved = solid.edges[static_cast<std::size_t>(u.edge)].kind != CurveKind::Line;
      ring.push_back(Seg{startOf(u), curved ? u.edge : -1, u.reversed});
    }
    return ring;
  };

  for (std::size_t fi = 0; fi < solid.faces.size(); ++fi) {
    const Face& f = solid.faces[fi];
    // A straight boundary edge lying IN the plane is part of the cap loop, whichever side it is on.
    for (const Loop& lp : f.loops)
      for (const EdgeUse& u : lp.uses) {
        const Edge& e = solid.edges[static_cast<std::size_t>(u.edge)];
        const DistRange& er = edgeRange[static_cast<std::size_t>(u.edge)];
        if (er.lo >= -eps && er.hi <= eps) {
          if (e.kind != CurveKind::Line)
            return Fail(Problem::SliceCutCrossesCurvedFace, outWhy);  // the cap would need a curve
          addCutSeg(solid.vertices[static_cast<std::size_t>(e.v0)].p, solid.vertices[static_cast<std::size_t>(e.v1)].p);
        }
      }

    if (side[fi] != Side::Split) {
      PieceFace pf{f, {}};
      for (const Loop& lp : f.loops)
        pf.rings.push_back(wholeRing(lp));
      (side[fi] == Side::Above ? above : below).push_back(std::move(pf));
      continue;
    }

    if (!f.paramLoops.empty())
      return Fail(Problem::SliceResultInvalid, outWhy);  // a trimmed plane is not split here

    // Split the outer loop. A curved edge on it lies wholly on one side (its face was not crossed,
    // or this face would have been refused above), so it is copied whole to that side.
    const std::vector<EdgeUse>& uses = f.loops[0].uses;
    const std::size_t m = uses.size();
    std::vector<Vec3> P(m);
    std::vector<double> d(m);
    for (std::size_t i = 0; i < m; ++i) {
      P[i] = startOf(uses[i]);
      d[i] = sd(P[i]);
    }
    std::vector<Seg> ra;
    std::vector<Seg> rb;
    std::vector<Vec3> cross;
    for (std::size_t i = 0; i < m; ++i) {
      const std::size_t j = (i + 1) % m;
      const double di = d[i];
      const double dj = d[j];
      const Edge& e = solid.edges[static_cast<std::size_t>(uses[i].edge)];
      const bool curved = e.kind != CurveKind::Line;
      const DistRange& er = edgeRange[static_cast<std::size_t>(uses[i].edge)];
      if (curved && er.lo < -eps && er.hi > eps)
        return Fail(Problem::SliceCutCrossesCurvedFace, outWhy);
      if (curved && std::fabs(di) <= eps && std::fabs(dj) <= eps)
        return Fail(Problem::SliceCutCrossesCurvedFace, outWhy);  // its chord would need a curved cap
      const bool edgeAbove = curved ? er.lo >= -eps : (di >= -eps && dj >= -eps);
      const bool edgeBelow = curved ? er.hi <= eps : (di <= eps && dj <= eps);
      if (di >= -eps)
        ra.push_back(Seg{P[i], curved && edgeAbove ? uses[i].edge : -1, uses[i].reversed});
      if (di <= eps)
        rb.push_back(Seg{P[i], curved && edgeBelow ? uses[i].edge : -1, uses[i].reversed});
      if (!curved && ((di > eps && dj < -eps) || (di < -eps && dj > eps))) {
        const double t = di / (di - dj);
        const Vec3 x = ray3d::Add(P[i], ray3d::Scale(ray3d::Sub(P[j], P[i]), t));
        ra.push_back(Seg{x});
        rb.push_back(Seg{x});
        cross.push_back(x);
      } else if (std::fabs(di) <= eps && ((dj > eps) != (dj < -eps))) {
        cross.push_back(P[i]);
      }
    }
    if (cross.size() != 2)
      return Fail(Problem::SliceCutSeveralOutlines, outWhy);
    addCutSeg(cross[0], cross[1]);

    PieceFace fa{f, {}};
    PieceFace fb{f, {}};
    if (ra.size() >= 2)
      fa.rings.push_back(std::move(ra));
    if (rb.size() >= 2)
      fb.rings.push_back(std::move(rb));
    // A hole in the face lies wholly on one side (a plane through it would cross the hole's wall).
    for (std::size_t li = 1; li < f.loops.size(); ++li) {
      DistRange r;
      for (const EdgeUse& u : f.loops[li].uses) {
        r.add(edgeRange[static_cast<std::size_t>(u.edge)].lo);
        r.add(edgeRange[static_cast<std::size_t>(u.edge)].hi);
      }
      if (r.lo >= -eps && !fa.rings.empty())
        fa.rings.push_back(wholeRing(f.loops[li]));
      else if (r.hi <= eps && !fb.rings.empty())
        fb.rings.push_back(wholeRing(f.loops[li]));
      else
        return Fail(Problem::SliceCutSeveralOutlines, outWhy);
    }
    if (!fa.rings.empty())
      above.push_back(std::move(fa));
    if (!fb.rings.empty())
      below.push_back(std::move(fb));
  }

  if (cutSegs.size() < 3)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);

  // --- Chain the cut into one loop, as the all-planar cutter does -------------------------------
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
  for (char c : used)
    if (!c)
      return Fail(Problem::SliceCutSeveralOutlines, outWhy);
  if (capRing.size() < 3)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);
  std::vector<Vec3> capAbove = capRing;
  if (RingSignedAreaAbout(capAbove, ray3d::Scale(pn, -1.0)) < 0.0)
    std::reverse(capAbove.begin(), capAbove.end());
  const std::vector<Vec3> capBelow(capAbove.rbegin(), capAbove.rend());
  auto capFace = [&](const std::vector<Vec3>& ring, const Vec3& outward) {
    PieceFace pf;
    pf.face.surface = PlaneSurface(planePoint, outward);  // exactly the caller's plane
    std::vector<Seg> segs;
    for (const Vec3& p : ring)
      segs.push_back(Seg{p});
    pf.rings.push_back(std::move(segs));
    return pf;
  };

  // --- Weld one piece: vertices by position, straight edges by their ends, curved edges by source -
  auto weld = [&](const std::vector<PieceFace>& faces, Solid* dst) -> bool {
    Solid s;
    std::map<std::tuple<long long, long long, long long>, int> vmap;
    auto quant = [&](double v) { return static_cast<long long>(std::llround(v / weldEps)); };
    auto addV = [&](const Vec3& p) {
      const auto key = std::make_tuple(quant(p.x), quant(p.y), quant(p.z));
      const auto it = vmap.find(key);
      if (it != vmap.end())
        return it->second;
      const int idx = AddVertex(&s, p);
      vmap.emplace(key, idx);
      return idx;
    };
    std::map<std::tuple<int, int, int>, int> emap;  // (lower vertex, higher vertex, source edge or -1)

    for (const PieceFace& pf : faces) {
      Face out = pf.face;
      out.loops.clear();
      for (const std::vector<Seg>& ring : pf.rings) {
        // Drop repeated points so a split exactly at a vertex makes no zero-length segment.
        std::vector<Seg> segs;
        for (const Seg& sg : ring) {
          if (!segs.empty() && segs.back().src < 0 && same(segs.back().p, sg.p)) {
            segs.back() = sg;  // keep a whole curved edge that starts at the same point
            continue;
          }
          segs.push_back(sg);
        }
        while (segs.size() > 1 && segs.back().src < 0 && same(segs.back().p, segs.front().p))
          segs.pop_back();
        Loop lp;
        const std::size_t n = segs.size();
        for (std::size_t i = 0; i < n; ++i) {
          const Seg& sg = segs[i];
          if (sg.src >= 0) {
            const Edge& e = solid.edges[static_cast<std::size_t>(sg.src)];
            const int a = addV(solid.vertices[static_cast<std::size_t>(e.v0)].p);
            const int b = addV(solid.vertices[static_cast<std::size_t>(e.v1)].p);
            const std::tuple<int, int, int> key{std::min(a, b), std::max(a, b), sg.src};
            auto it = emap.find(key);
            int ei;
            if (it != emap.end()) {
              ei = it->second;
            } else {
              Edge copy = e;
              copy.v0 = a;
              copy.v1 = b;
              s.edges.push_back(std::move(copy));
              ei = static_cast<int>(s.edges.size()) - 1;
              emap.emplace(key, ei);
            }
            lp.uses.push_back(EdgeUse{ei, sg.rev});
            continue;
          }
          const int a = addV(sg.p);
          const int b = addV(segs[(i + 1) % n].p);
          if (a == b)
            return Fail(Problem::SliceResultComplex, outWhy);
          const std::tuple<int, int, int> key{std::min(a, b), std::max(a, b), -1};
          auto it = emap.find(key);
          int ei;
          if (it != emap.end()) {
            ei = it->second;
          } else {
            ei = AddLine(&s, std::min(a, b), std::max(a, b));
            emap.emplace(key, ei);
          }
          lp.uses.push_back(EdgeUse{ei, a > b});
        }
        if (!lp.uses.empty())
          out.loops.push_back(std::move(lp));
      }
      if (out.loops.empty())
        continue;
      s.faces.push_back(std::move(out));
    }
    if (s.faces.size() < 3)
      return Fail(Problem::SliceResultComplex, outWhy);
    AddSingleShell(&s);
    const Problem why = Validate(s);
    if (why != Problem::Ok) {
      const bool topo = why == Problem::EdgeNotUsedTwice || why == Problem::ShellOpenAtEdge ||
                        why == Problem::EdgeNonManifold || why == Problem::EdgeOrientationInconsistent ||
                        why == Problem::NotClosed || why == Problem::LoopNotClosed;
      return Fail(topo ? Problem::SliceResultComplex : Problem::SliceResultInvalid, outWhy);
    }
    *dst = std::move(s);
    return true;
  };

  above.push_back(capFace(capAbove, ray3d::Scale(pn, -1.0)));
  below.push_back(capFace(capBelow, pn));
  // Prove both pieces before writing either (REQ-201).
  Solid pieceAbove;
  Solid pieceBelow;
  if (!weld(above, &pieceAbove) || !weld(below, &pieceBelow))
    return false;
  const bool wantAbove = keep == SliceKeep::Above || keep == SliceKeep::Both;
  const bool wantBelow = keep == SliceKeep::Below || keep == SliceKeep::Both;
  if (wantAbove && outAbove)
    *outAbove = std::move(pieceAbove);
  if (wantBelow && outBelow)
    *outBelow = std::move(pieceBelow);
  return Succeed(outWhy);
}

/// Cut a sphere by any plane through it — the cross-section is a circle, which the kernel holds
/// (GitHub #520, REQ-314 / REQ-335). Each piece is a spherical cap: the sphere's own surface over the
/// latitudes the plane leaves, closed by the flat disc of the cut.
///
/// The construction works in a frame whose +Z is the kept side's own outward direction, so one
/// builder serves both caps: the sphere is symmetric under that flip.
[[nodiscard]] bool SameMeasuredShape(const Solid& a, const Solid& b);

/// A `Sphere` or `Torus` recipe that describes its solid, the way `ConicalRecipeFitsSolid` vets a
/// cylinder's (D-2026-09-17-a): these builders rebuild the pieces from the recipe, so a recipe whose
/// primitive is a different shape — a damaged `internal document payload` frame, a solid edited since — would cut in the
/// wrong place. A recipe that does not fit is not used, and the cut falls through to the refusal the
/// solid's geometry earns.
[[nodiscard]] bool RoundRecipeFitsSolid(const Solid& solid) {
  const Recipe& rc = solid.recipe;
  Solid prim;
  Problem why = Problem::Ok;
  const bool built = rc.kind == PrimitiveKind::Sphere
                         ? MakeSphere(rc.frame, rc.radius, &prim, &why)
                         : (rc.kind == PrimitiveKind::Torus
                                ? MakeTorus(rc.frame, rc.radius, rc.radius2, &prim, &why)
                                : false);
  return built && SameMeasuredShape(solid, prim);
}

[[nodiscard]] bool SliceSpherePrimitive(const Solid& solid, const Vec3& planePoint, const Vec3& pn,
                                        SliceKeep keep, Solid* outAbove, Solid* outBelow, bool* handled,
                                        Problem* outWhy) {
  *handled = false;
  const Recipe& rc = solid.recipe;
  if (rc.kind != PrimitiveKind::Sphere || !RoundRecipeFitsSolid(solid))
    return false;

  *handled = true;
  const Vec3 centre = rc.frame.origin;
  const double R = rc.radius;
  const double eps = 1e-7 * std::max(R, 1.0);
  const double d = ray3d::Dot(ray3d::Sub(planePoint, centre), pn);  // signed height of the plane
  if (std::fabs(d) >= R - eps)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);

  // `up` is the outward direction of the piece being built; `dUp` the plane's height along it.
  auto build = [&](const Vec3& up, double dUp, Solid* dst) -> bool {
    ucs::Ucs fr{};
    if (!ucs::FromNormal(centre, up, &fr))
      return Fail(Problem::SliceDegeneratePlane, outWhy);
    const double rCut = std::sqrt(std::max(0.0, R * R - dUp * dUp));
    const double vCut = std::asin(std::clamp(dUp / R, -1.0, 1.0));  // latitude of the cut circle
    auto W = [&](const Vec3& local) { return ucs::UcsToWorld(fr, local); };

    Solid s;
    const Vec3 cutC = W(Vec3{0.0, 0.0, dUp});
    const int a = AddVertex(&s, W(Vec3{rCut, 0.0, dUp}));   // cut circle at u = 0
    const int b = AddVertex(&s, W(Vec3{-rCut, 0.0, dUp}));  // cut circle at u = pi
    const int pole = AddVertex(&s, W(Vec3{0.0, 0.0, R}));

    const Vec3 Z = fr.zAxis;
    const int cut0 = AddArc(&s, a, b, cutC, Z, kPi);  // the u in [0, pi] half of the cut circle
    const int cut1 = AddArc(&s, b, a, cutC, Z, kPi);
    // Meridians from the cut circle to the pole. About -Y the local +X turns toward +Z; about +Y the
    // local -X does, which is why the two seams carry opposite normals.
    const double mSweep = kHalfPi - vCut;
    const int mA = AddArc(&s, a, pole, centre, ray3d::Scale(fr.yAxis, -1.0), mSweep);
    const int mB = AddArc(&s, b, pole, centre, fr.yAxis, mSweep);

    // The flat disc of the cut, facing away from the material (which is on the +up side).
    s.faces.push_back(MakePlaneFace(planePoint, ray3d::Scale(Z, -1.0), {{cut1, true}, {cut0, true}}));

    auto capFace = [&](double u0, double u1, std::vector<EdgeUse> uses) {
      Face f;
      f.surface.kind = SurfaceKind::Sphere;
      f.surface.frame = fr;
      f.surface.radius = R;
      f.uStart = u0;
      f.uEnd = u1;
      f.vStart = vCut;
      f.vEnd = kHalfPi;
      Loop lp;
      lp.uses = std::move(uses);
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    };
    capFace(0.0, kPi, {{cut0, false}, {mB, false}, {mA, true}});
    capFace(kPi, kTwoPi, {{cut1, false}, {mA, false}, {mB, true}});

    AddSingleShell(&s);
    if (Validate(s) != Problem::Ok)
      return Fail(Problem::SliceResultInvalid, outWhy);
    *dst = std::move(s);
    return true;
  };

  const bool wantAbove = keep == SliceKeep::Above || keep == SliceKeep::Both;
  const bool wantBelow = keep == SliceKeep::Below || keep == SliceKeep::Both;
  Solid probe;
  if (!build(pn, d, &probe) || !build(ray3d::Scale(pn, -1.0), -d, &probe))
    return false;  // build() already set outWhy
  if (wantAbove && outAbove && !build(pn, d, outAbove))
    return false;
  if (wantBelow && outBelow && !build(ray3d::Scale(pn, -1.0), -d, outBelow))
    return false;
  return Succeed(outWhy);
}

/// Cut a torus by a plane **square to its axis** — the cross-section is a ring, two concentric
/// circles (GitHub #520, REQ-314 / REQ-335). Each piece is the part of the tube on its side of the
/// plane, closed by the flat ring.
///

/// Cut a torus by a plane **through its axis** — the section is two separate circles, one through
/// each side of the ring (GitHub #520 increment 3, the half this release's first pass left refused).
///
/// Each piece is half the doughnut: the tube over half a turn, closed by a flat disc of the tube's
/// own radius at each end. Both discs face the same way — away from the material, which is all on one
/// side of the plane — which is what makes a half-doughnut, rather than a wedge.
///
/// The two halves are the same construction in two frames: the second is the first turned half a turn
/// about the axis, which a torus is symmetric under. A plane parallel to the axis but NOT through it
/// cuts a quartic (two ovals, or one waisted curve) and keeps its own refusal.
[[nodiscard]] bool SliceTorusThroughAxis(const Solid& solid, const Vec3& planePoint, const Vec3& pn,
                                         SliceKeep keep, Solid* outAbove, Solid* outBelow, bool* handled,
                                         Problem* outWhy) {
  *handled = false;
  const Recipe& rc = solid.recipe;
  if (rc.kind != PrimitiveKind::Torus)
    return false;
  const Vec3 axis = ray3d::Normalize(rc.frame.zAxis);
  if (std::fabs(ray3d::Dot(pn, axis)) > 1e-6)
    return false;  // not parallel to the axis — SliceTorusSquareToAxis' remit, or a quartic

  const double R = rc.radius;
  const double r = rc.radius2;
  *handled = true;
  if (r >= R)
    return Fail(Problem::SliceCutTorusCurve, outWhy);  // a self-intersecting tube is not two circles
  const double eps = 1e-7 * std::max(R + r, 1.0);
  // Through the axis, or beside it? Beside it the section is a quartic, not two circles.
  const double d = ray3d::Dot(ray3d::Sub(planePoint, rc.frame.origin), pn);
  if (std::fabs(d) > eps)
    return Fail(Problem::SliceCutTorusCurve, outWhy);
  if (!RoundRecipeFitsSolid(solid))
    return false;  // a recipe that does not describe its solid never places a cut (D-2026-09-17-a)

  // The frame this half is built in: +Y is the side the material is on, +Z the torus axis, so the
  // cut plane is its own XZ plane.
  auto halfFrame = [&](bool second) {
    ucs::Ucs f{};
    f.origin = rc.frame.origin;
    f.zAxis = axis;
    f.yAxis = ray3d::Normalize(second ? ray3d::Scale(pn, -1.0) : pn);
    f.xAxis = ray3d::Normalize(ray3d::Cross(f.yAxis, f.zAxis));
    return f;
  };

  auto build = [&](bool second, Solid* dst) -> bool {
    const ucs::Ucs fr = halfFrame(second);
    auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(fr, Vec3{x, y, z}); };
    Solid s;
    // The four corners of the (u, v) pattern, exactly as MakeTorus names them: u in {0, pi} is the
    // two cut ends, v in {0, pi} the outer and inner equators.
    const int v00 = AddVertex(&s, W(R + r, 0.0, 0.0));
    const int v0p = AddVertex(&s, W(R - r, 0.0, 0.0));
    const int vp0 = AddVertex(&s, W(-(R + r), 0.0, 0.0));
    const int vpp = AddVertex(&s, W(-(R - r), 0.0, 0.0));

    const Vec3 Z = fr.zAxis;
    const Vec3 Y = fr.yAxis;
    const Vec3 origin = fr.origin;
    // Ring arcs over this half turn: the outer equator (v = 0) and the inner one (v = pi).
    const int eOuter = AddArc(&s, v00, vp0, origin, Z, kPi);
    const int eInner = AddArc(&s, v0p, vpp, origin, Z, kPi);
    // The tube's own circle at each cut end, in two half sweeps — the ends ARE the section.
    const Vec3 tube0 = W(R, 0.0, 0.0);
    const Vec3 tubeP = W(-R, 0.0, 0.0);
    const int t0a = AddArc(&s, v00, v0p, tube0, ray3d::Scale(Y, -1.0), kPi);
    const int t0b = AddArc(&s, v0p, v00, tube0, ray3d::Scale(Y, -1.0), kPi);
    const int tPa = AddArc(&s, vp0, vpp, tubeP, Y, kPi);
    const int tPb = AddArc(&s, vpp, vp0, tubeP, Y, kPi);

    // The two flat discs, both facing -Y: the material of this half is the +Y side of the plane, so
    // each end looks away from it. Their loops wind counter-clockwise about -Y, which is the
    // direction the tube arcs were built to sweep in.
    s.faces.push_back(MakePlaneFace(tube0, ray3d::Scale(Y, -1.0), {{t0a, false}, {t0b, false}}));
    s.faces.push_back(MakePlaneFace(tubeP, ray3d::Scale(Y, -1.0), {{tPb, true}, {tPa, true}}));

    auto tubeFace = [&](double v0, double v1, std::vector<EdgeUse> uses) {
      Face f;
      f.surface.kind = SurfaceKind::Torus;
      f.surface.frame = fr;
      f.surface.radius = R;
      f.surface.radius2 = r;
      f.uStart = 0.0;
      f.uEnd = kPi;
      f.vStart = v0;
      f.vEnd = v1;
      Loop lp;
      lp.uses = std::move(uses);
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    };
    // MakeTorus' own two patches over this half turn, unchanged in winding.
    tubeFace(0.0, kPi, {{eOuter, false}, {tPa, false}, {eInner, true}, {t0a, true}});
    tubeFace(kPi, kTwoPi, {{eInner, false}, {tPb, false}, {eOuter, true}, {t0b, true}});

    AddSingleShell(&s);
    if (Validate(s) != Problem::Ok)
      return Fail(Problem::SliceResultInvalid, outWhy);
    *dst = std::move(s);
    return true;
  };

  const bool wantAbove = keep == SliceKeep::Above || keep == SliceKeep::Both;
  const bool wantBelow = keep == SliceKeep::Below || keep == SliceKeep::Both;
  Solid probe;
  if (!build(false, &probe) || !build(true, &probe))
    return false;  // build() already set outWhy
  if (wantAbove && outAbove && !build(false, outAbove))
    return false;
  if (wantBelow && outBelow && !build(true, outBelow))
    return false;
  return Succeed(outWhy);
}

/// The tube angle `v` runs from the outer equator (`v = 0`) through the top (`v = pi/2`) to the inner
/// equator (`v = pi`), so a plane at height `d` cuts the tube at `v = asin(d/r)` and `pi - asin(d/r)`,
/// and the ring's radii are `R ± sqrt(r^2 - d^2)`. As with a sphere, one builder serves both pieces
/// because the torus is symmetric about its own centre plane.
[[nodiscard]] bool SliceTorusSquareToAxis(const Solid& solid, const Vec3& planePoint, const Vec3& pn,
                                          SliceKeep keep, Solid* outAbove, Solid* outBelow, bool* handled,
                                          Problem* outWhy) {
  *handled = false;
  const Recipe& rc = solid.recipe;
  if (rc.kind != PrimitiveKind::Torus)
    return false;
  const double R = rc.radius;
  const double r = rc.radius2;
  // A tube as wide as its ring self-intersects, so it has no mass properties to vet its recipe
  // against — and no ring-shaped cut either. It is refused by name here, before that check: a
  // refusal cannot place a cut in the wrong place, which is what vetting the recipe guards against.
  if (r >= R) {
    *handled = true;
    return Fail(Problem::SliceCutTorusCurve, outWhy);
  }
  if (!RoundRecipeFitsSolid(solid))
    return false;

  *handled = true;
  const double eps = 1e-7 * std::max(R + r, 1.0);
  const Vec3 axis = rc.frame.zAxis;
  if (std::fabs(std::fabs(ray3d::Dot(pn, axis)) - 1.0) > 1e-6)
    return Fail(Problem::SliceCutTorusCurve, outWhy);  // any other angle is a quartic curve
  const double d = ray3d::Dot(ray3d::Sub(planePoint, rc.frame.origin), axis);
  if (std::fabs(d) >= r - eps)
    return Fail(Problem::SlicePlaneMissesSolid, outWhy);

  auto build = [&](double sign, Solid* dst) -> bool {
    // A frame flipped about X keeps the torus (its z -> -z image is itself) and turns the piece
    // below the plane into the piece above it.
    ucs::Ucs fr = rc.frame;
    if (sign < 0.0) {
      fr.yAxis = ray3d::Scale(fr.yAxis, -1.0);
      fr.zAxis = ray3d::Scale(fr.zAxis, -1.0);
    }
    const double dUp = sign * d;
    const double rc2 = std::sqrt(std::max(0.0, r * r - dUp * dUp));  // half-width of the ring
    const double vCut = std::asin(std::clamp(dUp / r, -1.0, 1.0));
    auto W = [&](double x, double y, double z) { return ucs::UcsToWorld(fr, Vec3{x, y, z}); };

    Solid s;
    const int out0 = AddVertex(&s, W(R + rc2, 0.0, dUp));
    const int outP = AddVertex(&s, W(-(R + rc2), 0.0, dUp));
    const int in0 = AddVertex(&s, W(R - rc2, 0.0, dUp));
    const int inP = AddVertex(&s, W(-(R - rc2), 0.0, dUp));

    const Vec3 Z = fr.zAxis;
    const Vec3 cutC = W(0.0, 0.0, dUp);
    const int eOut0 = AddArc(&s, out0, outP, cutC, Z, kPi);  // outer ring, u in [0, pi]
    const int eOutP = AddArc(&s, outP, out0, cutC, Z, kPi);
    const int eIn0 = AddArc(&s, in0, inP, cutC, Z, kPi);  // inner ring
    const int eInP = AddArc(&s, inP, in0, cutC, Z, kPi);
    // Tube arcs at u = 0 and u = pi, over the top: from the outer cut point to the inner one.
    const double tSweep = kPi - 2.0 * vCut;
    const int tube0 = AddArc(&s, out0, in0, W(R, 0.0, 0.0), ray3d::Scale(fr.yAxis, -1.0), tSweep);
    const int tubeP = AddArc(&s, outP, inP, W(-R, 0.0, 0.0), fr.yAxis, tSweep);

    // The flat ring of the cut, facing away from the material: the outer loop first, then the hole
    // wound the other way, which is what makes the face's signed area the ring's own.
    Face cut;
    cut.surface = PlaneSurface(planePoint, ray3d::Scale(Z, -1.0));
    Loop outer;
    outer.uses = {EdgeUse{eOutP, true}, EdgeUse{eOut0, true}};
    Loop hole;
    hole.uses = {EdgeUse{eIn0, false}, EdgeUse{eInP, false}};
    cut.loops.push_back(std::move(outer));
    cut.loops.push_back(std::move(hole));
    s.faces.push_back(std::move(cut));

    auto tubeFace = [&](double u0, double u1, std::vector<EdgeUse> uses) {
      Face f;
      f.surface.kind = SurfaceKind::Torus;
      f.surface.frame = fr;
      f.surface.radius = R;
      f.surface.radius2 = r;
      f.uStart = u0;
      f.uEnd = u1;
      f.vStart = vCut;
      f.vEnd = kPi - vCut;
      Loop lp;
      lp.uses = std::move(uses);
      f.loops.push_back(std::move(lp));
      s.faces.push_back(std::move(f));
    };
    tubeFace(0.0, kPi, {{eOut0, false}, {tubeP, false}, {eIn0, true}, {tube0, true}});
    tubeFace(kPi, kTwoPi, {{eOutP, false}, {tube0, false}, {eInP, true}, {tubeP, true}});

    AddSingleShell(&s);
    if (Validate(s) != Problem::Ok)
      return Fail(Problem::SliceResultInvalid, outWhy);
    *dst = std::move(s);
    return true;
  };

  const bool wantAbove = keep == SliceKeep::Above || keep == SliceKeep::Both;
  const bool wantBelow = keep == SliceKeep::Below || keep == SliceKeep::Both;
  // "Above" is the +pn side; the builder's +1 piece is the one above the plane along the torus axis.
  const bool upperIsAbove = ray3d::Dot(pn, axis) > 0.0;
  Solid* upperDst = upperIsAbove ? outAbove : outBelow;
  Solid* lowerDst = upperIsAbove ? outBelow : outAbove;
  const bool wantUpper = upperIsAbove ? wantAbove : wantBelow;
  const bool wantLower = upperIsAbove ? wantBelow : wantAbove;

  Solid probe;
  if (!build(1.0, &probe) || !build(-1.0, &probe))
    return false;
  if (wantUpper && upperDst && !build(1.0, upperDst))
    return false;
  if (wantLower && lowerDst && !build(-1.0, lowerDst))
    return false;
  return Succeed(outWhy);
}



// ---------------------------------------------------------------------------------------------
// Which recipe a cut may trust (GitHub issue #515 follow-up, D-2026-09-17-a).
//
// The curved slice recognisers rebuild both pieces from `solid.recipe`, so a recipe that does not
// describe its solid — a `internal document payload` whose frame was damaged, or any future path that forgets to drop a
// recipe after changing the shape — would cut the wrong geometry with no error. And a solid that
// IS a cylinder or cone but carries no recipe — one saved before #515, a Boolean result, a straight
// sweep — was refused. Both are answered here, from the geometry: a Cylinder / Cone recipe is used
// only when the primitive it describes has the solid's volume, area and bounds, and a solid without
// a trustworthy recipe is recognised from its edges and faces when it is exactly one.
// ---------------------------------------------------------------------------------------------


/// True when \p a and \p b agree in volume, area and bounds to 1e-6 of their size.
[[nodiscard]] bool SameMeasuredShape(const Solid& a, const Solid& b) {
  const MassProperties ma = ComputeMassProperties(a);
  const MassProperties mb = ComputeMassProperties(b);
  if (!ma.valid || !mb.valid)
    return false;
  const Bounds ba = ComputeBounds(a);
  const Bounds bb = ComputeBounds(b);
  if (!ba.valid || !bb.valid)
    return false;
  const double size = std::max({ba.mx.x - ba.mn.x, ba.mx.y - ba.mn.y, ba.mx.z - ba.mn.z, 1e-9});
  const double eps = 1e-6 * size;
  const auto near = [&](const Vec3& p, const Vec3& q) { return ray3d::Length(ray3d::Sub(p, q)) <= eps; };
  return std::fabs(ma.volume - mb.volume) <= 1e-6 * std::max(std::fabs(mb.volume), size * size * size) &&
         std::fabs(ma.surfaceArea - mb.surfaceArea) <= 1e-6 * std::max(mb.surfaceArea, size * size) &&
         near(ba.mn, bb.mn) && near(ba.mx, bb.mx);
}

[[nodiscard]] bool BuildConicalPrimitive(const Conical& c, Solid* out) {
  Problem why = Problem::Ok;
  return c.cylinder ? MakeCylinder(c.frame, c.rBase, c.height, out, &why)
                    : MakeCone(c.frame, c.rBase, c.rTop, c.height, out, &why);
}

/// A Cylinder / Cone recipe that describes its solid. Any other kind — and a recipe whose primitive
/// is a different shape — is not trusted.
[[nodiscard]] bool ConicalRecipeFitsSolid(const Solid& solid) {
  const Recipe& rc = solid.recipe;
  if (rc.kind != PrimitiveKind::Cylinder && rc.kind != PrimitiveKind::Cone)
    return false;
  Solid prim;
  Problem why = Problem::Ok;
  const bool built = rc.kind == PrimitiveKind::Cylinder ? MakeCylinder(rc.frame, rc.radius, rc.height, &prim, &why)
                                                        : MakeCone(rc.frame, rc.radius, rc.radius2, rc.height, &prim, &why);
  return built && SameMeasuredShape(solid, prim);
}

/// Recognise \p s as exactly one right circular cylinder or cone, whatever made it.
///
/// Every edge a line or an arc; every arc about one axis (parallel normals, centres on one line) and
/// lying at one of the solid's two ends along that axis, one radius per end (an end with no arc is an
/// apex); every planar face perpendicular to the axis; every curved face — cylinder, cone or NURBS —
/// ON the cone between the two end circles, checked at sample points. Then the candidate primitive
/// must also have the solid's volume, area and bounds. A stepped shaft, a twisted loft, a bulging
/// barrel or a drilled solid fails one of those.
[[nodiscard]] bool ClassifyRightConical(const Solid& s, Conical* out) {
  if (s.vertices.empty() || s.faces.empty())
    return false;
  const Bounds bnd = ComputeBounds(s);
  if (!bnd.valid)
    return false;
  const double size = std::max({bnd.mx.x - bnd.mn.x, bnd.mx.y - bnd.mn.y, bnd.mx.z - bnd.mn.z, 1e-9});
  const double eps = 1e-6 * size;

  const Edge* firstArc = nullptr;
  for (const Edge& e : s.edges) {
    if (e.kind != CurveKind::Line && e.kind != CurveKind::Arc)
      return false;
    if (e.kind == CurveKind::Arc && !firstArc)
      firstArc = &e;
  }
  if (!firstArc)
    return false;
  const Vec3 axisPt = firstArc->frame.origin;
  const Vec3 axis = ray3d::Normalize(firstArc->frame.zAxis);
  const auto along = [&](const Vec3& p) { return ray3d::Dot(ray3d::Sub(p, axisPt), axis); };
  const auto offAxis = [&](const Vec3& p) {
    const Vec3 w = ray3d::Sub(p, axisPt);
    return ray3d::Length(ray3d::Sub(w, ray3d::Scale(axis, ray3d::Dot(w, axis))));
  };

  double tMin = 1e300, tMax = -1e300;
  for (const Vertex& v : s.vertices) {
    tMin = std::min(tMin, along(v.p));
    tMax = std::max(tMax, along(v.p));
  }
  if (!(tMax - tMin > eps))
    return false;

  double rMin = -1.0, rMax = -1.0;  // radius of the end circle at tMin / tMax; -1 = no arc there
  for (const Edge& e : s.edges) {
    if (e.kind != CurveKind::Arc)
      continue;
    if (ray3d::Length(ray3d::Cross(ray3d::Normalize(e.frame.zAxis), axis)) > 1e-6 || offAxis(e.frame.origin) > eps)
      return false;
    const double t = along(e.frame.origin);
    if (std::fabs(t - tMin) > eps && std::fabs(t - tMax) > eps)
      continue;  // a ring part-way along: judged against the cone below, so a step or groove fails
    double& slot = std::fabs(t - tMin) <= eps ? rMin : rMax;
    if (slot >= 0.0 && std::fabs(slot - e.radius) > eps)
      return false;
    slot = e.radius;
  }
  for (const Face& f : s.faces) {
    if (f.surface.kind == SurfaceKind::Plane) {
      if (ray3d::Length(ray3d::Cross(ray3d::Normalize(f.surface.frame.zAxis), axis)) > 1e-6)
        return false;
    } else if (f.surface.kind != SurfaceKind::Cylinder && f.surface.kind != SurfaceKind::Cone &&
               f.surface.kind != SurfaceKind::Nurbs) {
      return false;
    }
  }
  if (rMin < 0.0)
    rMin = 0.0;  // an apex at that end
  if (rMax < 0.0)
    rMax = 0.0;
  const auto onAxisAt = [&](double t) { return ray3d::Add(axisPt, ray3d::Scale(axis, t)); };
  Conical c;
  if (!ConicalBetween(onAxisAt(tMin), rMin, onAxisAt(tMax), rMax, &c))
    return false;

  // Every curved face lies on that cone: radius at axial position t is linear between the ends.
  const auto radiusAt = [&](double t) { return rMin + (rMax - rMin) * (t - tMin) / (tMax - tMin); };
  const auto onCone = [&](const Vec3& p) { return std::fabs(offAxis(p) - radiusAt(along(p))) <= eps; };
  // ...and so does every ring part-way along (a three-circle loft of one radius is still a cylinder;
  // a stepped shaft's middle ring is not on the line between its end circles).
  for (const Edge& e : s.edges)
    if (e.kind == CurveKind::Arc && std::fabs(e.radius - radiusAt(along(e.frame.origin))) > eps)
      return false;
  for (const Face& f : s.faces) {
    const Surface& sf = f.surface;
    if (sf.kind == SurfaceKind::Plane)
      continue;
    for (double fu : {0.0, 0.37, 0.71, 1.0}) {
      for (double fv : {0.0, 0.5, 1.0}) {
        Vec3 p;
        if (sf.kind == SurfaceKind::Nurbs) {
          p = nurbs::Evaluate(sf.patch, f.uStart + (f.uEnd - f.uStart) * fu, f.vStart + (f.vEnd - f.vStart) * fv);
        } else {
          const double u = f.uStart + (f.uEnd - f.uStart) * fu;
          p = ConicalPoint(sf, sf.radius, sf.kind == SurfaceKind::Cylinder ? sf.radius : sf.radius2, u,
                           sf.height * fv);
        }
        if (!onCone(p))
          return false;
      }
    }
  }

  Solid prim;
  if (!BuildConicalPrimitive(c, &prim) || !SameMeasuredShape(s, prim))
    return false;
  *out = c;
  return true;
}

}  // namespace

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
      // oblique through a cylinder or cone (an ellipse, B2b-1), or parallel to the axis (straight
      // seams and chords, #517).
      //
      // The recognisers below rebuild the pieces from the recipe, so they are handed a solid whose
      // recipe is TRUSTED (D-2026-09-17-a): its own, when that primitive has this solid's measured
      // shape; one recognised from the geometry, when the solid is exactly a cylinder or cone but was
      // saved before #515 or made by a Boolean or sweep; and none at all when a Cylinder / Cone recipe
      // does not describe the solid, so a damaged recipe is refused rather than cut at the wrong place.
      Solid described;
      const Solid* subject = &solid;
      if (!ConicalRecipeFitsSolid(solid)) {
        Conical recognised;
        if (ClassifyRightConical(solid, &recognised)) {
          described = solid;
          StampConicalRecipe(&described, recognised);
          subject = &described;
        } else if (solid.recipe.kind == PrimitiveKind::Cylinder || solid.recipe.kind == PrimitiveKind::Cone) {
          described = solid;
          described.recipe = Recipe{};
          subject = &described;
        }
      }
      const Vec3 upn = ray3d::Normalize(planeNormal);
      bool handled = false;
      bool ok = SliceCurvedPrimitive(*subject, planePoint, upn, keep, outAbove, outBelow, &handled, outWhy);
      if (handled)
        return ok;
      ok = SliceCylinderOblique(*subject, planePoint, upn, keep, outAbove, outBelow, &handled, outWhy);
      if (handled)
        return ok;
      ok = SliceConeOblique(*subject, planePoint, upn, keep, outAbove, outBelow, &handled, outWhy);
      if (handled)
        return ok;
      ok = SliceConeObliqueOpenNotch(*subject, planePoint, upn, keep, outAbove, outBelow, &handled, outWhy);
      if (handled)
        return ok;
      ok = SliceConicalAlongAxis(*subject, planePoint, upn, keep, outAbove, outBelow, &handled, outWhy);
      if (handled)
        return ok;
      // A sphere at any plane (a circle), and a torus cut square to its axis (a ring) — GitHub #520.
      ok = SliceSpherePrimitive(solid, planePoint, upn, keep, outAbove, outBelow, &handled, outWhy);
      if (handled)
        return ok;
      // A torus cut THROUGH its axis is two separate circles (#520 increment 3). Asked first because
      // the square-to-axis recogniser below answers for every OTHER torus plane, including with the
      // quartic refusal — so it would swallow this one.
      ok = SliceTorusThroughAxis(solid, planePoint, upn, keep, outAbove, outBelow, &handled, outWhy);
      if (handled)
        return ok;
      ok = SliceTorusSquareToAxis(solid, planePoint, upn, keep, outAbove, outBelow, &handled, outWhy);
      if (handled)
        return ok;
      // Nothing took the cut. For a cone, say which cut it was rather than "flat faces only", which
      // reads as though the user picked the wrong object (GitHub #516, REQ-201). Every cylinder cut
      // and every cut parallel to a cone's axis is handled above, so a cone reaches here only when
      // the cut is steeper than its side.
      if (subject->recipe.kind == PrimitiveKind::Cone)
        return Fail(Problem::SliceCutTooSteepForCone, outWhy);
      // Any other curved solid — a filleted box, a box with a drilled hole, a sphere — is cut when
      // the plane crosses only its flat faces, the curved ones carried whole (GitHub #518).
      return SliceCarryingCurvedFaces(solid, planePoint, upn, keep, outAbove, outBelow, outWhy);
    }
  }
  for (const Edge& e : solid.edges) {
    if (e.kind != CurveKind::Line)
      return SliceCarryingCurvedFaces(solid, planePoint, ray3d::Normalize(planeNormal), keep, outAbove,
                                      outBelow, outWhy);
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
      return Fail(Problem::SliceCutSeveralOutlines, outWhy);
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
      return Fail(Problem::SliceCutSeveralOutlines, outWhy);  // cross-section is more than one loop
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

} // namespace brep
