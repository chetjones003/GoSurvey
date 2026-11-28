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

namespace {

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
} // namespace

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

namespace {

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
/// numerically. `internal document payload` stays v3.
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
/// `zTop > zP`). `internal document payload` stays v3.
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
/// left there. Every sphere and cylinder face integrates numerically. `internal document payload` stays v3.
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
} // namespace

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

} // namespace brep
