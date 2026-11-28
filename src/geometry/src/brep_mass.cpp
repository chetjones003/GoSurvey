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

/// Signed area of one loop, measured in the face's plane and about the face's outward normal.
[[nodiscard]] double PlaneLoopSignedArea(const Solid& s, const Face& f, const Loop& lp) {
  const ucs::Ucs& fr = f.surface.frame;
  double acc = 0.0;
  for (const EdgeUse& u : lp.uses) {
    const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
    const int startV = u.reversed ? e.v1 : e.v0;
    const int endV = u.reversed ? e.v0 : e.v1;
    if (e.kind == CurveKind::Intersection) {
      // No closed-form bulge term exists for a marching curve (TASK-204 slice (b), GitHub issue
      // #283 — the first time an Intersection edge has bounded a PLANAR face rather than a curved
      // wall). Walk the marched polyline and shoelace over its consecutive points instead of just
      // the endpoints — reduces to the ordinary straight-chord term when the curve happens to be
      // straight, and picks up the real bulge otherwise.
      std::vector<Vec3> poly = MarchIntersectionCurve(s, e, 128);
      if (u.reversed)
        std::reverse(poly.begin(), poly.end());
      for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
        const ucs::Point2D pa = ucs::WorldToPlane(fr, poly[i]);
        const ucs::Point2D pb = ucs::WorldToPlane(fr, poly[i + 1]);
        acc += 0.5 * (pa.x * pb.y - pb.x * pa.y);
      }
      continue;
    }
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
} // namespace

[[nodiscard]] double PlaneFaceArea(const Solid& s, const Face& f) {
  double acc = 0.0;
  for (const Loop& lp : f.loops)
    acc += PlaneLoopSignedArea(s, f, lp);
  return acc;
}

namespace {

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

/// Walk one revolution-surface loop into a (u, v) polyline: `u` is continuously unwrapped `atan2`,
/// `v` is frame-local Z — the same convention as `LocalSurfaceDerivs` and ACIS `paramLoops`.
[[nodiscard]] std::vector<curveisect::Vec2> ProjectRevolutionLoopToParam(const Solid& s, const Loop& lp,
                                                                           const Surface& sf) {
  constexpr int kArcSamples = 8;
  std::vector<curveisect::Vec2> poly;
  bool haveRaw = false;
  double prevRaw = 0.0;
  double contU = 0.0;
  for (const EdgeUse& u : lp.uses) {
    const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
    int samples = 1;
    if (e.kind == CurveKind::Line)
      samples = 1;
    else if (e.kind == CurveKind::Ellipse)
      samples = 32;
    else if (e.kind == CurveKind::Arc)
      samples = std::max(kArcSamples,
                         static_cast<int>(std::ceil(32.0 * std::fabs(e.sweep) / kTwoPi)));
    else
      samples = kArcSamples;
    for (int k = 0; k < samples; ++k) {
      const double tTraverse = static_cast<double>(k) / static_cast<double>(samples);
      const double edgeT = u.reversed ? (1.0 - tTraverse) : tTraverse;
      const Vec3 local = ucs::WorldToUcs(sf.frame, EdgePointAt(s, e, edgeT));
      const double rawU = std::atan2(local.y, local.x);
      if (!haveRaw) {
        contU = rawU;
        haveRaw = true;
      } else {
        double delta = rawU - prevRaw;
        while (delta > kPi)
          delta -= 2.0 * kPi;
        while (delta <= -kPi)
          delta += 2.0 * kPi;
        contU += delta;
      }
      prevRaw = rawU;
      poly.push_back(curveisect::Vec2{contU, local.z});
    }
  }
  return poly;
}

[[nodiscard]] bool RevolutionFaceEdgeParamBbox(const Solid& s, const Face& f, double* uLo, double* uHi,
                                               double* vLo, double* vHi) {
  bool any = false;
  for (const Loop& lp : f.loops) {
    const std::vector<curveisect::Vec2> poly = ProjectRevolutionLoopToParam(s, lp, f.surface);
    for (const curveisect::Vec2& p : poly) {
      if (!any) {
        *uLo = *uHi = p.x;
        *vLo = *vHi = p.y;
        any = true;
      } else {
        *uLo = std::min(*uLo, p.x);
        *uHi = std::max(*uHi, p.x);
        *vLo = std::min(*vLo, p.y);
        *vHi = std::max(*vHi, p.y);
      }
    }
  }
  return any;
}

/// True when the face's stored rectangle / height over-counts material vs its real 3D boundary.
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
  // `st.hasClip` faces (a coaxial-stack wall band's own bite) are FULL height almost everywhere,
  // with the found `[a, b]` interval the REMOVED bite to subtract from that full span — unlike
  // every other `IsectStrip` user (`oneSided`, or a cutter-lining face via `other2`), where the
  // found interval already IS the face's own surviving material band, used directly.
  auto widthAt = [&](double u) {
    double a = 0.0, b = 0.0;
    if (IsectStripAt(st, u, &a, &b))
      return st.hasClip ? (st.fullHi - st.fullLo) - (b - a) : (b - a);
    return st.hasClip ? (st.fullHi - st.fullLo) : 0.0;
  };
  auto areaAt = [&](double u) { return r * widthAt(u); };
  auto volAt = [&](double u) {
    return r * (r - qL.x * std::cos(u) - qL.y * std::sin(u)) * widthAt(u);
  };
  // REQ-339 (issue #504): a shoulder-crossing face (a coaxial-stack wall band's own bite, `hasClip`,
  // or a cutter-lining face switching between the two walls, `other2`) has a real kink where the
  // notch pinches to nothing or the active wall switches — continuous, but not differentiable,
  // unlike every other `IsectStrip` user (a smooth lens curve throughout). Gauss quadrature
  // converges slowly across a kink no matter how many panels, so rather than throw panels at it,
  // locate it (bisecting on a signature that changes exactly there) and integrate each smooth side
  // separately.
  if (!st.hasClip && st.other2 == nullptr) {
    out.area = GradedGaussIntegrate(f.uStart, f.uEnd, 48, areaAt);
    out.volTerm = GradedGaussIntegrate(f.uStart, f.uEnd, 48, volAt);
    return out;
  }
  // A signature that is continuous everywhere EXCEPT exactly at a kink: whether a crossing exists
  // (the `hasClip` pinch), XORed with which side of `clipZ` this longitude's own transverse position
  // falls on (the `other2` wall switch) — either changing indicates a kink between two samples.
  auto signature = [&](double u) {
    double a = 0.0, b = 0.0;
    const bool has = IsectStripAt(st, u, &a, &b);
    bool hiSide = false;
    if (st.other2 != nullptr) {
      const Vec3 probe = ucs::UcsToWorld(st.sf->frame, Vec3{r * std::cos(u), r * std::sin(u), 0.0});
      hiSide = ucs::WorldToUcs(st.other2->frame, probe).z >= st.clipZ;
    }
    return has != hiSide;
  };
  std::vector<double> bounds{f.uStart};
  const int scan = 200;
  bool prevSig = signature(f.uStart);
  double prevU = f.uStart;
  for (int i = 1; i <= scan; ++i) {
    const double u = f.uStart + (f.uEnd - f.uStart) * i / scan;
    const bool sig = signature(u);
    if (sig != prevSig) {
      double lo = prevU, hi = u;
      for (int k = 0; k < 60; ++k) {
        const double m = 0.5 * (lo + hi);
        (signature(m) == prevSig ? lo : hi) = m;
      }
      bounds.push_back(0.5 * (lo + hi));
    }
    prevSig = sig;
    prevU = u;
  }
  bounds.push_back(f.uEnd);
  for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
    out.area += GradedGaussIntegrate(bounds[i], bounds[i + 1], 48, areaAt);
    out.volTerm += GradedGaussIntegrate(bounds[i], bounds[i + 1], 48, volAt);
  }
  return out;
}
} // namespace

[[nodiscard]] bool RevolutionFaceNeedsBoundaryIntegral(const Solid& s, const Face& f) {
  if (f.surface.kind != SurfaceKind::Cylinder && f.surface.kind != SurfaceKind::Cone)
    return false;
  if (FaceLoopHasIntersectionEdge(s, f))
    return false;
  // Plant ASM lists a frustum wall as one loop / one full-turn rim edge; `IntegrateRevolutionTrimFaceNumeric`
  // collapses that to zero `(u,v)` area — use `ConicalFaceIntegrals` on the stretched patch instead.
  if (f.loops.size() == 1 && f.loops[0].uses.size() == 1) {
    const Edge& rim = s.edges[static_cast<std::size_t>(f.loops[0].uses[0].edge)];
    if (rim.kind == CurveKind::Arc && rim.v0 == rim.v1 && std::fabs(rim.sweep) + 0.05 >= kTwoPi)
      return false;
  }
  if (FaceLoopHasEllipseEdge(s, f)) {
    if (f.surface.kind == SurfaceKind::Cylinder) {
      CylinderCut cc;
      return !CylinderCutZExtent(s, f, &cc);
    }
    return !MakeConeCutStrip(s, f).valid();
  }
  double uLo = 0.0;
  double uHi = 0.0;
  double vLo = 0.0;
  double vHi = 0.0;
  if (!RevolutionFaceEdgeParamBbox(s, f, &uLo, &uHi, &vLo, &vHi))
    return false;
  const double uSpan = uHi - uLo;
  const double vSpan = vHi - vLo;
  const double curU = std::max(f.uEnd - f.uStart, 0.0);
  const double scale = std::fabs(f.surface.height) + std::fabs(vSpan) + f.surface.radius + 1.0;
  const double tol = 1e-5 * scale;
  // Require a positive walked axial span — a single full-circle rim has vSpan≈0 but may still use
  // the full `[0,height]` analytic patch after `FinalizeSingleRimConeFaces`.
  if (vSpan > tol && vSpan + tol < f.surface.height)
    return true;
  if (vSpan > tol && f.surface.height + tol < vSpan)
    return true;
  // Partial circumferential band only — not a full 2π wall (would break primitive cones).
  if (uSpan < 2.0 * kPi - 0.05 && uSpan + 0.05 < curU)
    return true;
  return false;
}

namespace {

/// Numerical area / volume term of a `SliceConeOblique` piece's lateral face — the cone analogue of
/// \ref IntegrateCylinderFaceNumeric. `z` is exact at each sampled `u` (no bisection needed, unlike a
/// marching `Intersection` curve), but the outer `u` integral has no closed form once the cone's
/// radius varies with height, so it goes through the same `GradedGaussIntegrate` machinery
/// (ADR-045 (b), extended per TASK-204's Q3: an *analytic* curve whose face integral still has no
/// tractable closed form).
[[nodiscard]] FaceIntegrals IntegrateConeCutFaceNumeric(const Solid& s, const Face& f, const Vec3& q) {
  const Vec3 qL = ucs::WorldToUcs(f.surface.frame, q);
  const double r0 = f.surface.radius;
  const double r1 = f.surface.radius2;
  const double h = f.surface.height;
  const double kk = (r1 - r0) / h;
  const double slant = std::sqrt(1.0 + kk * kk);
  const ConeCutStrip st = MakeConeCutStrip(s, f);
  FaceIntegrals out;
  if (!st.valid())
    return out;
  auto iRho = [&](double zLo, double zHi) {
    return r0 * (zHi - zLo) + kk * (zHi * zHi - zLo * zLo) * 0.5;
  };
  auto iRho2 = [&](double zLo, double zHi) {
    return r0 * r0 * (zHi - zLo) + r0 * kk * (zHi * zHi - zLo * zLo) +
           kk * kk * (zHi * zHi * zHi - zLo * zLo * zLo) / 3.0;
  };
  auto iRhoZ = [&](double zLo, double zHi) {
    return r0 * (zHi * zHi - zLo * zLo) * 0.5 + kk * (zHi * zHi * zHi - zLo * zLo * zLo) / 3.0;
  };
  out.area = GradedGaussIntegrate(f.uStart, f.uEnd, 64, [&](double u) {
    double zLo = 0.0, zHi = 0.0;
    return ConeCutStripAt(st, u, &zLo, &zHi) ? slant * iRho(zLo, zHi) : 0.0;
  });
  out.volTerm = GradedGaussIntegrate(f.uStart, f.uEnd, 64, [&](double u) {
    double zLo = 0.0, zHi = 0.0;
    if (!ConeCutStripAt(st, u, &zLo, &zHi))
      return 0.0;
    const double rhoI = iRho(zLo, zHi);
    return iRho2(zLo, zHi) - (qL.x * std::cos(u) + qL.y * std::sin(u)) * rhoI -
           kk * (iRhoZ(zLo, zHi) - qL.z * rhoI);
  });
  return out;
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

/// General-loop numeric mass-property integrator (ADR-052, issue #307) — forward-declared here,
/// defined below \ref ToroidalPoint since it needs those surface-point/derivative formulas.
[[nodiscard]] FaceIntegrals IntegrateGeneralLoopFace(const Face& f, const Vec3& qLocal);

/// Closure-probe integral of a cone/cylinder face from its walked 3D boundary (not `paramLoops`).
[[nodiscard]] FaceIntegrals IntegrateRevolutionTrimFaceNumeric(const Solid& s, const Face& f,
                                                               const Vec3& q);

/// \p q is the world-frame reference point; each branch transforms it into the surface's own frame.
///
/// \p honorGeneralLoops gates the `Face::paramLoops` branch (issue #307). `Validate`'s own internal
/// closure check (two `VolumeAbout` calls compared for point-invariance) passes false: that check
/// exists to catch a shell that does not actually seal, and `paramLoops` is documented (ADR-052 (c),
/// issue #306) as a classification aid that need not trace the face's real 3D boundary — so trusting
/// it there would make the closure probe fail on a face whose loop is simply a placeholder, not a
/// real leak. `ComputeMassProperties`, the user-facing report, passes true so the general-loop
/// numeric integral this file adds actually gets used once a real trim shape exists (issue #310).
[[nodiscard]] FaceIntegrals IntegrateFace(const Solid& s, const Face& f, const Vec3& q,
                                          bool honorGeneralLoops = true) {
  const Surface& sf = f.surface;
  const Vec3 qLocal = ucs::WorldToUcs(sf.frame, q);
  FaceIntegrals out;
  if (honorGeneralLoops && !f.paramLoops.empty()) {
    out = IntegrateGeneralLoopFace(f, qLocal);
    if (sf.inward)
      out.volTerm = -out.volTerm;
    return out;
  }
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
    if (RevolutionFaceNeedsBoundaryIntegral(s, f)) {
      out = IntegrateRevolutionTrimFaceNumeric(s, f, q);
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
    if (FaceLoopHasIntersectionEdge(s, f)) {
      out = IntegrateConeCutFaceNumeric(s, f, q);
    } else if (RevolutionFaceNeedsBoundaryIntegral(s, f)) {
      out = IntegrateRevolutionTrimFaceNumeric(s, f, q);
    } else if (FaceLoopHasEllipseEdge(s, f)) {
      out = IntegrateConeCutFaceNumeric(s, f, q);
    } else {
      const ConeIntegrals ci =
          ConicalFaceIntegrals(sf.radius, sf.radius2, sf.height, f.uStart, f.uEnd, qLocal);
      out.area = ci.area;
      out.volTerm = ci.volTerm;
    }
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
} // namespace

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
[[nodiscard]] double VolumeAbout(const Solid& s, const Vec3& q, bool* outFinite,
                                 bool honorGeneralLoops = true) {
  double volTerm = 0.0;
  bool finite = true;
  for (const Face& f : s.faces) {
    const FaceIntegrals fi = IntegrateFace(s, f, q, honorGeneralLoops);
    if (!std::isfinite(fi.area) || !std::isfinite(fi.volTerm))
      finite = false;
    volTerm += fi.volTerm;
  }
  if (outFinite)
    *outFinite = finite;
  return volTerm / 3.0;
}

namespace {

/// Numeric area/volume term of a face carrying a general trim loop (`Face::paramLoops`, ADR-052,
/// issue #306), for any `SurfaceKind` — the additive branch issue #307 asks for. Integrates
/// `|Su x Sv|` (area) and `(P - q) . (Su x Sv)` (the divergence-theorem volume term, shared with
/// every closed-form `*FaceIntegrals` function above) over the loop's actual parameter-space
/// interior, found per `v`-scanline via \ref GeneralLoopScanline rather than assumed to be the
/// `uStart/uEnd/vStart/vEnd` rectangle. `inward` is left for the shared flip in \ref IntegrateFace.
[[nodiscard]] FaceIntegrals IntegrateGeneralLoopFace(const Face& f, const Vec3& qLocal) {
  const Surface& sf = f.surface;
  FaceIntegrals out;
  // Break the v-range at every vertex's own v, not just at its overall min/max: the u-interval
  // returned by `GeneralLoopScanline` only varies smoothly BETWEEN vertices — at each vertex an edge
  // can enter or leave the scan, changing which interval a sample falls in by a jump. A polygon whose
  // straight edges are not all v-monotonic (e.g. a rectangular hole) is otherwise a step function of
  // v, which a fixed Gauss panel grid does not integrate exactly unless a panel boundary happens to
  // land exactly on the step — this way one always does.
  std::vector<double> vBreaks;
  for (const std::vector<curveisect::Vec2>& poly : f.paramLoops)
    for (const curveisect::Vec2& p : poly)
      vBreaks.push_back(p.y);
  if (vBreaks.size() < 2)
    return out;
  std::sort(vBreaks.begin(), vBreaks.end());
  vBreaks.erase(std::unique(vBreaks.begin(), vBreaks.end(),
                            [](double a, double b) { return std::fabs(a - b) < 1e-12 * (1.0 + std::fabs(a)); }),
                vBreaks.end());
  if (vBreaks.size() < 2)
    return out;
  constexpr int kVPanels = 8;
  constexpr int kUPanels = 12;
  auto integrand = [&](double u, double v, bool wantVol) {
    Vec3 p;
    Vec3 su;
    Vec3 sv;
    LocalSurfaceDerivs(sf, u, v, &p, &su, &sv);
    const Vec3 n = ray3d::Cross(su, sv);
    return wantVol ? ray3d::Dot(ray3d::Sub(p, qLocal), n) : ray3d::Length(n);
  };
  auto stripIntegral = [&](double v, bool wantVol) {
    const std::vector<double> xs = GeneralLoopScanline(f.paramLoops, v);
    double acc = 0.0;
    for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
      const double u0 = xs[i];
      const double u1 = xs[i + 1];
      if (u1 > u0)
        acc += GradedGaussIntegrate(u0, u1, kUPanels,
                                    [&](double u) { return integrand(u, v, wantVol); });
    }
    return acc;
  };
  for (std::size_t i = 0; i + 1 < vBreaks.size(); ++i) {
    const double v0 = vBreaks[i];
    const double v1 = vBreaks[i + 1];
    out.area += GradedGaussIntegrate(v0, v1, kVPanels, [&](double v) { return stripIntegral(v, false); });
    out.volTerm += GradedGaussIntegrate(v0, v1, kVPanels, [&](double v) { return stripIntegral(v, true); });
  }
  return out;
}

[[nodiscard]] FaceIntegrals IntegrateRevolutionTrimFaceNumeric(const Solid& s, const Face& f,
                                                               const Vec3& q) {
  const Vec3 qLocal = ucs::WorldToUcs(f.surface.frame, q);
  Face scratch;
  scratch.surface = f.surface;
  scratch.uStart = f.uStart;
  scratch.uEnd = f.uEnd;
  scratch.vStart = f.vStart;
  scratch.vEnd = f.vEnd;
  scratch.paramLoops.reserve(f.loops.size());
  std::vector<std::vector<curveisect::Vec2>> polys;
  polys.reserve(f.loops.size());
  for (const Loop& lp : f.loops) {
    std::vector<curveisect::Vec2> poly = ProjectRevolutionLoopToParam(s, lp, f.surface);
    if (poly.size() >= 3)
      polys.push_back(std::move(poly));
  }
  if (polys.empty())
    return {};
  auto signedArea = [](const std::vector<curveisect::Vec2>& poly) {
    double acc = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i) {
      const curveisect::Vec2& a = poly[i];
      const curveisect::Vec2& b = poly[(i + 1) % poly.size()];
      acc += a.x * b.y - b.x * a.y;
    }
    return 0.5 * acc;
  };
  size_t outerIdx = 0;
  double outerArea = -1.0;
  for (size_t i = 0; i < polys.size(); ++i) {
    const double a = std::fabs(signedArea(polys[i]));
    if (a > outerArea) {
      outerArea = a;
      outerIdx = i;
    }
  }
  for (size_t j = 0; j < polys.size(); ++j) {
    std::vector<curveisect::Vec2> poly = std::move(polys[j]);
    const bool wantCcw = (j == outerIdx);
    const double sa = signedArea(poly);
    if ((wantCcw && sa < 0.0) || (!wantCcw && sa > 0.0))
      std::reverse(poly.begin(), poly.end());
    scratch.paramLoops.push_back(std::move(poly));
  }
  return IntegrateGeneralLoopFace(scratch, qLocal);
}


// ---------------------------------------------------------------------------------------------
// First moments of volume — the centroid (REQ-334, ADR-055, GitHub #149 acceptance 4).
// ---------------------------------------------------------------------------------------------


/// The centroid needs `integral of r dV`, and by the divergence theorem its k-th component is
/// `1/2 * closed-surface-integral of r_k^2 n_k dA`. Two properties of that integrand shape
/// everything below, and both were established by measurement before any of this was written
/// (`Desktop\Notes for claude\issue149-analysis\probes\p4_centroid_quadrature.cpp`):
///
///  - **It is NOT frame-covariant.** `r_k^2 n_k` in one Cartesian frame is not the k-th component
///    of any vector that transforms into `r_k^2 n_k` in a rotated one. So the moment cannot be
///    accumulated in each face's own frame and rotated at the end the way a normal or a tangent
///    can. Every face must contribute in ONE shared frame, and that frame is world.
///    Measured cost of getting this wrong: a tilted box's centroid off by 3.2 ft, while an
///    axis-aligned box, sphere and torus all still came out exact — the error is invisible until
///    something is rotated.
///
///  - **The ORIGIN, not the axes, is what has to be local.** `r = p - q` with `q` on the solid
///    keeps every squared term at model scale even when the solid sits at easting 2.2e6. This is
///    the same rule \ref ComputeMassProperties already follows for the volume, and P2 measured the
///    alternative at 46 to 6,978 ft of error.
struct FaceMoment {
  bool ok = false;      ///< false when this face's shape is outside the covered set (see below).
  double area = 0.0;
  double volTerm = 0.0; ///< integral of (p-q).n dA
  Vec3 m{0, 0, 0};      ///< integral of 1/2 (r_x^2 n_x, r_y^2 n_y, r_z^2 n_z) dA, WORLD axes
};

/// 16-point Gauss-Legendre on [-1, 1], built once.
///
/// Quadrature rather than five new closed forms, and that is a deliberate choice recorded in
/// ADR-055 rather than a shortcut: it samples the **exact analytic surface**, never the display
/// mesh, so it is not the approximation GitHub #149's tessellation note rules out. It is also not
/// novel here — four of `IntegrateFace`'s own paths are already numeric, and ADR-048 authorises
/// "adaptive numerical quadrature" for the Nurbs kind's mass properties. A 16-point rule is exact
/// for degree 31, and every integrand below is a low-degree polynomial or low-order trigonometric
/// function of its parameter, so the residual is round-off and not truncation: the primitives come
/// out at 1e-12 ft or better, nine orders inside REQ-101.
struct GaussRule {
  double x[16];
  double w[16];
};

[[nodiscard]] const GaussRule& Gauss16() {
  static const GaussRule g = [] {
    GaussRule r{};
    for (int i = 0; i < 16; ++i) {
      double t = std::cos(kPi * (static_cast<double>(i) + 0.75) / 16.5);
      for (int it = 0; it < 100; ++it) {
        double p0 = 1.0, p1 = 0.0;
        for (int j = 0; j < 16; ++j) {
          const double p2 = p1;
          p1 = p0;
          p0 = ((2.0 * j + 1.0) * t * p1 - static_cast<double>(j) * p2) / (static_cast<double>(j) + 1.0);
        }
        const double dp = 16.0 * (t * p0 - p1) / (t * t - 1.0);
        const double dt = -p0 / dp;
        t += dt;
        if (std::fabs(dt) < 1e-16)
          break;
      }
      double p0 = 1.0, p1 = 0.0;
      for (int j = 0; j < 16; ++j) {
        const double p2 = p1;
        p1 = p0;
        p0 = ((2.0 * j + 1.0) * t * p1 - static_cast<double>(j) * p2) / (static_cast<double>(j) + 1.0);
      }
      const double dp = 16.0 * (t * p0 - p1) / (t * t - 1.0);
      r.x[i] = t;
      r.w[i] = 2.0 / ((1.0 - t * t) * dp * dp);
    }
    return r;
  }();
  return g;
}

/// A surface point and its two parametric derivatives, in the surface's own frame. The
/// parametrisations are the ones \ref Surface documents for each kind, so this cannot drift from
/// what the tessellator and the closest-point queries use.
struct PatchPoint {
  Vec3 p, du, dv;
};

[[nodiscard]] PatchPoint EvalSurfaceLocal(const Surface& sf, double u, double v) {
  PatchPoint o;
  const double cu = std::cos(u), su = std::sin(u);
  switch (sf.kind) {
  case SurfaceKind::Cylinder:
    o.p = {sf.radius * cu, sf.radius * su, v};
    o.du = {-sf.radius * su, sf.radius * cu, 0.0};
    o.dv = {0.0, 0.0, 1.0};
    break;
  case SurfaceKind::Cone: {
    const double t = (sf.height != 0.0) ? v / sf.height : 0.0;
    const double r = sf.radius + (sf.radius2 - sf.radius) * t;
    const double drdv = (sf.height != 0.0) ? (sf.radius2 - sf.radius) / sf.height : 0.0;
    o.p = {r * cu, r * su, v};
    o.du = {-r * su, r * cu, 0.0};
    o.dv = {drdv * cu, drdv * su, 1.0};
    break;
  }
  case SurfaceKind::Sphere: {
    const double cv = std::cos(v), sv = std::sin(v);
    o.p = {sf.radius * cv * cu, sf.radius * cv * su, sf.radius * sv};
    o.du = {-sf.radius * cv * su, sf.radius * cv * cu, 0.0};
    o.dv = {-sf.radius * sv * cu, -sf.radius * sv * su, sf.radius * cv};
    break;
  }
  case SurfaceKind::Torus: {
    const double cv = std::cos(v), sv = std::sin(v);
    const double rr = sf.radius + sf.radius2 * cv;
    o.p = {rr * cu, rr * su, sf.radius2 * sv};
    o.du = {-rr * su, rr * cu, 0.0};
    o.dv = {-sf.radius2 * sv * cu, -sf.radius2 * sv * su, sf.radius2 * cv};
    break;
  }
  default:
    break;
  }
  return o;
}

/// A boundary edge's point and tangent at parameter \p t in [0, 1], **analytically** — never by
/// finite difference. The probe used a difference quotient and lost seven digits to cancellation at
/// easting 2.2e6 (2.8e-5 ft of centroid error, still inside REQ-101 but needlessly); the closed
/// tangent restores it to round-off. Returns false for a curve kind this path does not cover.
[[nodiscard]] bool EdgePointAndTangent(const Solid& s, const Edge& e, double t, Vec3* p, Vec3* d) {
  switch (e.kind) {
  case CurveKind::Line: {
    const Vec3 a = s.vertices[static_cast<std::size_t>(e.v0)].p;
    const Vec3 b = s.vertices[static_cast<std::size_t>(e.v1)].p;
    *p = ray3d::Add(a, ray3d::Scale(ray3d::Sub(b, a), t));
    *d = ray3d::Sub(b, a);
    return true;
  }
  case CurveKind::Arc: {
    // Centre at frame.origin, X toward v0, sweeping `sweep` about Z — the one parametrisation
    // \ref EdgePointAt uses, so the boundary integral and the tessellator agree by construction.
    const double th = e.sweep * t;
    const double c = std::cos(th), sn = std::sin(th);
    *p = ray3d::Add(e.frame.origin, ray3d::Add(ray3d::Scale(e.frame.xAxis, e.radius * c),
                                               ray3d::Scale(e.frame.yAxis, e.radius * sn)));
    *d = ray3d::Add(ray3d::Scale(e.frame.xAxis, -e.radius * e.sweep * sn),
                    ray3d::Scale(e.frame.yAxis, e.radius * e.sweep * c));
    return true;
  }
  case CurveKind::Ellipse:
  case CurveKind::Intersection:
  default:
    return false;  // increment 2's work — see ADR-055 (d)
  }
}

/// A planar face's contribution, by Green's theorem evaluated along its boundary.
///
/// Quadrature along each edge rather than a polygon formula, because that is what makes ONE code
/// path cover a straight-edged face and an **arc-bounded** one. A cylinder's circular cap is a
/// `Plane` face whose loop is two arc edges, and a polygon formula silently inscribes a polygon in
/// it — the same 0.16% error the per-face area work (D-2026-09-09-g) measured on exactly that cap.
///
///   A   = contour integral of a db          Sa  = contour integral of (a^2/2) db
///   Sb  = -contour integral of (b^2/2) da   Saa = contour integral of (a^3/3) db
///   Sbb = -contour integral of (b^3/3) da   Sab = contour integral of (a^2 b/2) db
///
/// where (a, b) are the face frame's own axes. A point of the face is `p = O + a*X + b*Y`, so with
/// `c = O - q` each world component is `r_k = c_k + a*X_k + b*Y_k` and
///   `integral of r_k^2 dA = c_k^2*A + 2 c_k X_k Sa + 2 c_k Y_k Sb + X_k^2 Saa + 2 X_k Y_k Sab
///                           + Y_k^2 Sbb`.
/// Everything is referenced to `q` only through `c`, so no term is squared at survey magnitude.
[[nodiscard]] FaceMoment PlanarFaceMoment(const Solid& s, const Face& f, const Vec3& q) {
  FaceMoment out;
  if (f.loops.size() != 1)
    return out;  // a hole needs the loops summed with sign; increment 2 (ADR-055 (d))
  const Surface& sf = f.surface;
  const GaussRule& g = Gauss16();

  double A = 0.0, Sa = 0.0, Sb = 0.0, Saa = 0.0, Sab = 0.0, Sbb = 0.0;
  for (const EdgeUse& u : f.loops.front().uses) {
    const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
    for (int i = 0; i < 16; ++i) {
      const double tRaw = 0.5 * (g.x[i] + 1.0);
      const double t = u.reversed ? (1.0 - tRaw) : tRaw;
      // 0.5 folds in the [-1,1] -> [0,1] change of variable; the sign flips when the loop traverses
      // the stored edge backwards, which is what makes the contour close in one consistent sense.
      const double w = 0.5 * g.w[i] * (u.reversed ? -1.0 : 1.0);
      Vec3 pw{0, 0, 0}, dw{0, 0, 0};
      if (!EdgePointAndTangent(s, e, t, &pw, &dw))
        return out;  // not covered
      const Vec3 pl = ucs::WorldToUcs(sf.frame, pw);
      // The tangent is a DIRECTION: rotate it into the face frame without translating.
      const Vec3 dl{ray3d::Dot(dw, sf.frame.xAxis), ray3d::Dot(dw, sf.frame.yAxis),
                    ray3d::Dot(dw, sf.frame.zAxis)};
      const double a = pl.x, b = pl.y;
      const double da = dl.x * w, db = dl.y * w;
      A += a * db;
      Sa += 0.5 * a * a * db;
      Sb += -0.5 * b * b * da;
      Saa += (a * a * a / 3.0) * db;
      Sbb += -(b * b * b / 3.0) * da;
      Sab += 0.5 * a * a * b * db;
    }
  }

  const Vec3 c = ray3d::Sub(sf.frame.origin, q);
  const Vec3 X = sf.frame.xAxis, Y = sf.frame.yAxis;
  const Vec3 n = sf.inward ? ray3d::Scale(sf.frame.zAxis, -1.0) : sf.frame.zAxis;
  const double ck[3] = {c.x, c.y, c.z};
  const double Xk[3] = {X.x, X.y, X.z};
  const double Yk[3] = {Y.x, Y.y, Y.z};
  const double nk[3] = {n.x, n.y, n.z};
  double mk[3] = {0.0, 0.0, 0.0};
  for (int k = 0; k < 3; ++k) {
    const double i2 = ck[k] * ck[k] * A + 2.0 * ck[k] * Xk[k] * Sa + 2.0 * ck[k] * Yk[k] * Sb +
                      Xk[k] * Xk[k] * Saa + 2.0 * Xk[k] * Yk[k] * Sab + Yk[k] * Yk[k] * Sbb;
    mk[k] = 0.5 * i2 * nk[k];
  }
  out.ok = true;
  out.area = std::fabs(A);
  out.m = Vec3{mk[0], mk[1], mk[2]};
  // (p - q).n is constant over a plane face; `A` carries the loop's own orientation.
  out.volTerm = ray3d::Dot(c, n) * A;
  return out;
}

/// A curved face's contribution, by quadrature over its (u, v) rectangle.
[[nodiscard]] FaceMoment CurvedFaceMoment(const Face& f, const Vec3& q) {
  FaceMoment out;
  const Surface& sf = f.surface;
  double v0 = f.vStart, v1 = f.vEnd;
  if (sf.kind == SurfaceKind::Cylinder || sf.kind == SurfaceKind::Cone) {
    // `vStart`/`vEnd` on a Cylinder/Cone face is not the height range: the volume path
    // (`IntegrateFace`) never reads it either, deriving the true height instead from
    // `CylinderCutZExtent` or falling back to `sf.height`. The only face that height range differs
    // from `[0, sf.height]` for is one bounded by an `Ellipse` cut edge, which `IntegrateFaceMoment`
    // refuses by name before this is reached.
    v0 = 0.0;
    v1 = sf.height;
  }
  const double u0 = f.uStart, u1 = f.uEnd;
  if (!(std::fabs(u1 - u0) > 0.0) || !(std::fabs(v1 - v0) > 0.0))
    return out;

  const GaussRule& g = Gauss16();
  const double hu = 0.5 * (u1 - u0), mu = 0.5 * (u1 + u0);
  const double hv = 0.5 * (v1 - v0), mv = 0.5 * (v1 + v0);
  for (int i = 0; i < 16; ++i) {
    const double u = mu + hu * g.x[i];
    for (int j = 0; j < 16; ++j) {
      const double v = mv + hv * g.x[j];
      const PatchPoint pt = EvalSurfaceLocal(sf, u, v);
      Vec3 nl = ray3d::Cross(pt.du, pt.dv);
      const double jac = ray3d::Length(nl);
      if (!(jac > 0.0))
        continue;  // a pole: zero-measure, contributes nothing
      nl = ray3d::Scale(nl, 1.0 / jac);
      if (sf.inward)
        nl = ray3d::Scale(nl, -1.0);
      const double w = g.w[i] * g.w[j] * hu * hv * jac;

      const Vec3 pW = ucs::UcsToWorld(sf.frame, pt.p);
      const Vec3 nW = ray3d::Add(
          ray3d::Add(ray3d::Scale(sf.frame.xAxis, nl.x), ray3d::Scale(sf.frame.yAxis, nl.y)),
          ray3d::Scale(sf.frame.zAxis, nl.z));
      const Vec3 r = ray3d::Sub(pW, q);
      out.area += w;
      out.volTerm += ray3d::Dot(r, nW) * w;
      out.m.x += 0.5 * r.x * r.x * nW.x * w;
      out.m.y += 0.5 * r.y * r.y * nW.y * w;
      out.m.z += 0.5 * r.z * r.z * nW.z * w;
    }
  }
  out.ok = true;
  return out;
}

/// One face's moment, or `ok == false` when its shape is outside the covered set.
///
/// **What is deliberately NOT covered in this increment** (ADR-055 (d)), each refused rather than
/// approximated: a face carrying a general trim loop (`paramLoops`, ADR-052), a `Nurbs` surface
/// (REQ-315 lofts), a face with holes, and any boundary edge that is an `Ellipse` or an
/// `Intersection` curve — the procedural cases a Phase 4 boolean produces. Those are exactly the
/// faces whose *domain* is not a rectangle or a simple loop, and each needs the same treatment
/// `IntegrateFace` already gives them for the volume. Reporting a centroid computed as though they
/// were simple would be the plausible-wrong-number failure REQ-201 exists to prevent.
[[nodiscard]] FaceMoment IntegrateFaceMoment(const Solid& s, const Face& f, const Vec3& q) {
  if (!f.paramLoops.empty())
    return FaceMoment{};
  if (f.surface.kind == SurfaceKind::Plane)
    return PlanarFaceMoment(s, f, q);
  if (f.surface.kind == SurfaceKind::Nurbs)
    return FaceMoment{};
  if (f.loops.size() != 1)
    return FaceMoment{};  // a hole needs the loops summed with sign; increment 2 (ADR-055 (d))
  for (const Loop& lp : f.loops)
    for (const EdgeUse& u : lp.uses) {
      const CurveKind k = s.edges[static_cast<std::size_t>(u.edge)].kind;
      if (k == CurveKind::Intersection || k == CurveKind::Ellipse)
        return FaceMoment{};
    }
  return CurvedFaceMoment(f, q);
}

// ---------------------------------------------------------------------------------------------
// Second moments of volume — inertia (REQ-349, GitHub #460).
// ---------------------------------------------------------------------------------------------

/// Volume second moments about `q`: `Sxx = ∫ x'^2 dV`, `Sxy = ∫ x' y' dV`, etc. where `r' = p - q`.
/// From these `Ixx = Syy + Szz`, `Ixy = -Sxy`. Integrated via the divergence theorem in the same
/// world-axes, solid-local-origin discipline the centroid uses.
struct FaceSecondMoment {
  bool ok = false;
  double area = 0.0;
  double volTerm = 0.0;
  double Sxx = 0.0;
  double Syy = 0.0;
  double Szz = 0.0;
  double Sxy = 0.0;
  double Sxz = 0.0;
  double Syz = 0.0;
};

/// A planar face's second-moment contribution, by Green's theorem along its boundary.
///
/// For a plane `r·n = dot(c,n)` is constant, so each volume integral reduces to
/// `1/5 * dot(c,n) * ∫ f(r) dA` where `f` is quadratic. The region integrals over `(a,b)` are the
/// same six scalars `PlanarFaceMoment` already computes: `A, Sa, Sb, Saa, Sab, Sbb`.
[[nodiscard]] FaceSecondMoment PlanarFaceSecondMoment(const Solid& s, const Face& f, const Vec3& q) {
  FaceSecondMoment out;
  if (f.loops.size() != 1)
    return out;
  const Surface& sf = f.surface;
  const GaussRule& g = Gauss16();

  double A = 0.0, Sa = 0.0, Sb = 0.0, Saa = 0.0, Sab = 0.0, Sbb = 0.0;
  for (const EdgeUse& u : f.loops.front().uses) {
    const Edge& e = s.edges[static_cast<std::size_t>(u.edge)];
    for (int i = 0; i < 16; ++i) {
      const double tRaw = 0.5 * (g.x[i] + 1.0);
      const double t = u.reversed ? (1.0 - tRaw) : tRaw;
      const double w = 0.5 * g.w[i] * (u.reversed ? -1.0 : 1.0);
      Vec3 pw{0, 0, 0}, dw{0, 0, 0};
      if (!EdgePointAndTangent(s, e, t, &pw, &dw))
        return out;
      const Vec3 pl = ucs::WorldToUcs(sf.frame, pw);
      const Vec3 dl{ray3d::Dot(dw, sf.frame.xAxis), ray3d::Dot(dw, sf.frame.yAxis),
                    ray3d::Dot(dw, sf.frame.zAxis)};
      const double a = pl.x, b = pl.y;
      const double da = dl.x * w, db = dl.y * w;
      A += a * db;
      Sa += 0.5 * a * a * db;
      Sb += -0.5 * b * b * da;
      Saa += (a * a * a / 3.0) * db;
      Sbb += -(b * b * b / 3.0) * da;
      Sab += 0.5 * a * a * b * db;
    }
  }

  const Vec3 c = ray3d::Sub(sf.frame.origin, q);
  const Vec3 X = sf.frame.xAxis, Y = sf.frame.yAxis;
  Vec3 n = sf.frame.zAxis;
  if (sf.inward)
    n = ray3d::Scale(n, -1.0);
  const double nW[3] = {n.x, n.y, n.z};
  const double cW[3] = {c.x, c.y, c.z};
  const double Xw[3] = {X.x, X.y, X.z};
  const double Yw[3] = {Y.x, Y.y, Y.z};
  const double dotCN = ray3d::Dot(c, n);
  // If dot is near zero the face contributes nothing to volume and to second moments.
  if (std::fabs(dotCN) < 1e-18 && std::fabs(A) < 1e-18) {
    // Still a valid face, but zero volume contribution.
  }

  auto integ = [&](int px, int py, double coeff) -> double {
    // Generic helper not used; keep inline expansions below for clarity.
    (void)px;
    (void)py;
    (void)coeff;
    return 0.0;
  };
  (void)integ;
  (void)nW;

  // Region integrals of world monomials over the face.
  // x = c_x + X_x a + Y_x b, etc. Expand and integrate termwise.
  const double cx = cW[0], cy = cW[1], cz = cW[2];

  // Helpers to compute ∫ x^2 dA, ∫ y^2 dA, ∫ z^2 dA, ∫ xy dA, etc.
  auto quad = [&](double cx_, double cy_, double cz_, double Xx_, double Xy_, double Xz_, double Yx_,
                  double Yy_, double Yz_, double* outXX, double* outYY, double* outZZ, double* outXY,
                  double* outXZ, double* outYZ) {
    const double A_ = A;
    const double Sa_ = Sa, Sb_ = Sb, Saa_ = Saa, Sbb_ = Sbb, Sab_ = Sab;
    *outXX = cx_ * cx_ * A_ + 2 * cx_ * Xx_ * Sa_ + 2 * cx_ * Yx_ * Sb_ + Xx_ * Xx_ * Saa_ +
             2 * Xx_ * Yx_ * Sab_ + Yx_ * Yx_ * Sbb_;
    *outYY = cy_ * cy_ * A_ + 2 * cy_ * Xy_ * Sa_ + 2 * cy_ * Yy_ * Sb_ + Xy_ * Xy_ * Saa_ +
             2 * Xy_ * Yy_ * Sab_ + Yy_ * Yy_ * Sbb_;
    *outZZ = cz_ * cz_ * A_ + 2 * cz_ * Xz_ * Sa_ + 2 * cz_ * Yz_ * Sb_ + Xz_ * Xz_ * Saa_ +
             2 * Xz_ * Yz_ * Sab_ + Yz_ * Yz_ * Sbb_;
    *outXY = cx_ * cy_ * A_ + (cx_ * Xy_ + cy_ * Xx_) * Sa_ + (cx_ * Yy_ + cy_ * Yx_) * Sb_ +
             Xx_ * Xy_ * Saa_ + (Xx_ * Yy_ + Yx_ * Xy_) * Sab_ + Yx_ * Yy_ * Sbb_;
    *outXZ = cx_ * cz_ * A_ + (cx_ * Xz_ + cz_ * Xx_) * Sa_ + (cx_ * Yz_ + cz_ * Yx_) * Sb_ +
             Xx_ * Xz_ * Saa_ + (Xx_ * Yz_ + Yx_ * Xz_) * Sab_ + Yx_ * Yz_ * Sbb_;
    *outYZ = cy_ * cz_ * A_ + (cy_ * Xz_ + cz_ * Xy_) * Sa_ + (cy_ * Yz_ + cz_ * Yy_) * Sb_ +
             Xy_ * Xz_ * Saa_ + (Xy_ * Yz_ + Yy_ * Xz_) * Sab_ + Yy_ * Yz_ * Sbb_;
  };

  double intXX = 0, intYY = 0, intZZ = 0, intXY = 0, intXZ = 0, intYZ = 0;
  quad(cx, cy, cz, Xw[0], Xw[1], Xw[2], Yw[0], Yw[1], Yw[2], &intXX, &intYY, &intZZ, &intXY,
       &intXZ, &intYZ);

  const double scale = dotCN / 5.0;
  out.ok = true;
  out.area = std::fabs(A);
  out.volTerm = dotCN * A;
  out.Sxx = scale * intXX;
  out.Syy = scale * intYY;
  out.Szz = scale * intZZ;
  out.Sxy = scale * intXY;
  out.Sxz = scale * intXZ;
  out.Syz = scale * intYZ;
  return out;
}

/// A curved face's second-moment contribution, by quadrature over its (u,v) rectangle.
///
/// Uses the homogeneous form `∫ f dV = 1/5 ∫ f(r) (r·n) dA` sampled at each Gauss point, where
/// `r = pW - q` and `f` is `x^2, y^2, z^2, xy, xz, yz`.
[[nodiscard]] FaceSecondMoment CurvedFaceSecondMoment(const Face& f, const Vec3& q) {
  FaceSecondMoment out;
  const Surface& sf = f.surface;
  double v0 = f.vStart, v1 = f.vEnd;
  if (sf.kind == SurfaceKind::Cylinder || sf.kind == SurfaceKind::Cone) {
    v0 = 0.0;
    v1 = sf.height;
  }
  const double u0 = f.uStart, u1 = f.uEnd;
  if (!(std::fabs(u1 - u0) > 0.0) || !(std::fabs(v1 - v0) > 0.0))
    return out;

  const GaussRule& g = Gauss16();
  const double hu = 0.5 * (u1 - u0), mu = 0.5 * (u1 + u0);
  const double hv = 0.5 * (v1 - v0), mv = 0.5 * (v1 + v0);
  for (int i = 0; i < 16; ++i) {
    const double u = mu + hu * g.x[i];
    for (int j = 0; j < 16; ++j) {
      const double v = mv + hv * g.x[j];
      const PatchPoint pt = EvalSurfaceLocal(sf, u, v);
      Vec3 nl = ray3d::Cross(pt.du, pt.dv);
      const double jac = ray3d::Length(nl);
      if (!(jac > 0.0))
        continue;
      nl = ray3d::Scale(nl, 1.0 / jac);
      if (sf.inward)
        nl = ray3d::Scale(nl, -1.0);
      const double w = g.w[i] * g.w[j] * hu * hv * jac;

      const Vec3 pW = ucs::UcsToWorld(sf.frame, pt.p);
      const Vec3 nW = ray3d::Add(
          ray3d::Add(ray3d::Scale(sf.frame.xAxis, nl.x), ray3d::Scale(sf.frame.yAxis, nl.y)),
          ray3d::Scale(sf.frame.zAxis, nl.z));
      const Vec3 r = ray3d::Sub(pW, q);
      const double dot = ray3d::Dot(r, nW);
      const double dA = w;
      const double coeff = dot * dA / 5.0;
      out.area += dA;
      out.volTerm += dot * dA;
      out.Sxx += r.x * r.x * coeff;
      out.Syy += r.y * r.y * coeff;
      out.Szz += r.z * r.z * coeff;
      out.Sxy += r.x * r.y * coeff;
      out.Sxz += r.x * r.z * coeff;
      out.Syz += r.y * r.z * coeff;
    }
  }
  out.ok = true;
  return out;
}

[[nodiscard]] FaceSecondMoment IntegrateFaceSecondMoment(const Solid& s, const Face& f,
                                                        const Vec3& q) {
  if (!f.paramLoops.empty())
    return FaceSecondMoment{};
  if (f.surface.kind == SurfaceKind::Plane)
    return PlanarFaceSecondMoment(s, f, q);
  if (f.surface.kind == SurfaceKind::Nurbs)
    return FaceSecondMoment{};
  if (f.loops.size() != 1)
    return FaceSecondMoment{};
  for (const Loop& lp : f.loops)
    for (const EdgeUse& u : lp.uses) {
      const CurveKind k = s.edges[static_cast<std::size_t>(u.edge)].kind;
      if (k == CurveKind::Intersection || k == CurveKind::Ellipse)
        return FaceSecondMoment{};
    }
  return CurvedFaceSecondMoment(f, q);
}

} // namespace

bool FaceArea(const Solid& s, int faceIndex, double* outArea, Problem* outWhy) {
  const auto fail = [&](Problem p) {
    if (outWhy)
      *outWhy = p;
    return false;
  };
  if (!outArea)
    return fail(Problem::IndexOutOfRange);
  if (faceIndex < 0 || static_cast<size_t>(faceIndex) >= s.faces.size())
    return fail(Problem::IndexOutOfRange);
  // Topology first: a face's boundary is walked through `Solid::edges` and `Solid::vertices`, so a
  // shell that does not validate can send the integrator at indices that are not there. This is the
  // same gate `ComputeMassProperties` opens with, and for the same reason.
  const Problem why = Validate(s);
  if (why != Problem::Ok)
    return fail(why);

  // NOT gated on `SelfIntersects`, deliberately, and this is the one place this function's contract
  // differs from `ComputeMassProperties` — see REQ-313 as amended, D-2026-09-09-g.
  //
  // That gate exists because a self-passing shell encloses part of space twice, which makes its
  // VOLUME a number with no meaning. A single face's area is not that kind of quantity: the face is
  // one bounded patch of one surface, and its area is exactly as well defined whether or not some
  // other face crosses it. REQ-318 lets a user click that face, and answering "unavailable" for a
  // figure that is perfectly well defined would be the worse of the two wrong answers.
  //
  // The visible consequence, stated rather than hidden: on a self-intersecting solid the per-face
  // areas still sum to the true surface area while `ComputeMassProperties` reports `valid == false`
  // and zero. That asymmetry is intentional. On every solid that passes both gates the sum and the
  // total agree by construction, because this calls the same integrator with the same reference
  // point — which `BrepTests`' per-face area cases assert rather than assume.
  const Vec3 q = ReferencePoint(s);
  *outArea = std::fabs(IntegrateFace(s, s.faces[static_cast<size_t>(faceIndex)], q).area);
  return true;
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

  // The centroid (REQ-334 / ADR-055). Separate from the loop above because it is allowed to fail
  // where the volume is not: its integrator covers fewer face shapes in this increment, and a face
  // it does not cover makes the centroid unavailable while leaving the volume and area untouched.
  //
  // Referenced to the SAME `q` the volume is, which is what keeps every squared term at model scale
  // at survey magnitudes. `M` accumulates in WORLD axes because the integrand is not
  // frame-covariant — see \ref FaceMoment.
  Vec3 M{0.0, 0.0, 0.0};
  double volAbout = 0.0;
  bool covered = !s.faces.empty();
  for (const Face& f : s.faces) {
    const FaceMoment fm = IntegrateFaceMoment(s, f, q);
    if (!fm.ok) {
      covered = false;
      break;
    }
    M = ray3d::Add(M, fm.m);
    volAbout += fm.volTerm;
  }
  volAbout /= 3.0;
  // The volume this integrator re-derives must agree with the one already reported, or the two are
  // not describing the same solid and the centroid built on it means nothing. A relative 1e-9 is
  // far looser than the 1e-12 the primitives actually achieve and far tighter than anything that
  // could hide a real disagreement.
  if (covered && std::fabs(volAbout) > 1e-12 && std::isfinite(volAbout) &&
      std::fabs(volAbout - mp.volume) <= 1e-9 * std::fabs(mp.volume)) {
    const Vec3 cen = ray3d::Add(q, ray3d::Scale(M, 1.0 / volAbout));
    if (std::isfinite(cen.x) && std::isfinite(cen.y) && std::isfinite(cen.z)) {
      mp.centroid = cen;
      mp.centroidValid = true;
    }
  }

  // Second moments — inertia about the centroid (REQ-349).
  // Shares the same covered set and the same world-axes, solid-local-origin discipline.
  // Withheld when the centroid is unavailable or a face is outside the set, leaving volume/area/centroid untouched.
  if (mp.centroidValid) {
    double Sxx_q = 0.0, Syy_q = 0.0, Szz_q = 0.0, Sxy_q = 0.0, Sxz_q = 0.0, Syz_q = 0.0;
    double volAbout2 = 0.0;
    bool covered2 = !s.faces.empty();
    for (const Face& f : s.faces) {
      const FaceSecondMoment fm = IntegrateFaceSecondMoment(s, f, q);
      if (!fm.ok) {
        covered2 = false;
        break;
      }
      Sxx_q += fm.Sxx;
      Syy_q += fm.Syy;
      Szz_q += fm.Szz;
      Sxy_q += fm.Sxy;
      Sxz_q += fm.Sxz;
      Syz_q += fm.Syz;
      volAbout2 += fm.volTerm;
    }
    volAbout2 /= 3.0;
    if (covered2 && std::fabs(volAbout2) > 1e-12 && std::isfinite(volAbout2) &&
        std::fabs(volAbout2 - mp.volume) <= 1e-9 * std::fabs(mp.volume) &&
        std::isfinite(Sxx_q) && std::isfinite(Syy_q) && std::isfinite(Szz_q) &&
        std::isfinite(Sxy_q) && std::isfinite(Sxz_q) && std::isfinite(Syz_q)) {
      const Vec3 d = ray3d::Sub(mp.centroid, q);
      const double dx = d.x, dy = d.y, dz = d.z;
      const double V = mp.volume;
      // Transfer second moments from q to centroid: S_c = S_q - V * d_i d_j with correction for diagonal.
      const double Sxx_c = Sxx_q - V * dx * dx;
      const double Syy_c = Syy_q - V * dy * dy;
      const double Szz_c = Szz_q - V * dz * dz;
      const double Sxy_c = Sxy_q - V * dx * dy;
      const double Sxz_c = Sxz_q - V * dx * dz;
      const double Syz_c = Syz_q - V * dy * dz;

      const double Ixx_c = Syy_c + Szz_c;
      const double Iyy_c = Sxx_c + Szz_c;
      const double Izz_c = Sxx_c + Syy_c;
      const double Ixy_c = -Sxy_c;
      const double Ixz_c = -Sxz_c;
      const double Iyz_c = -Syz_c;

      if (std::isfinite(Ixx_c) && std::isfinite(Iyy_c) && std::isfinite(Izz_c) &&
          std::isfinite(Ixy_c) && std::isfinite(Ixz_c) && std::isfinite(Iyz_c) &&
          Ixx_c >= -1e-12 && Iyy_c >= -1e-12 && Izz_c >= -1e-12) {
        mp.Ixx = Ixx_c;
        mp.Iyy = Iyy_c;
        mp.Izz = Izz_c;
        mp.Ixy = Ixy_c;
        mp.Ixz = Ixz_c;
        mp.Iyz = Iyz_c;

        // Principal axes: eigendecomposition of the centroidal tensor.
        double A[3][3] = {
            {Ixx_c, Ixy_c, Ixz_c},
            {Ixy_c, Iyy_c, Iyz_c},
            {Ixz_c, Iyz_c, Izz_c},
        };
        double eig[3] = {0, 0, 0};
        double vec[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        JacobiEigenSymmetric3x3(A, eig, vec);
        // Sort descending but keep axes paired; Jacobi already sorts, but re-assert ordering for determinism.
        // The Jacobi routine returns sorted descending.
        mp.principalI1 = eig[0];
        mp.principalI2 = eig[1];
        mp.principalI3 = eig[2];
        mp.principalAxis1 = Vec3{vec[0][0], vec[1][0], vec[2][0]};
        mp.principalAxis2 = Vec3{vec[0][1], vec[1][1], vec[2][1]};
        mp.principalAxis3 = Vec3{vec[0][2], vec[1][2], vec[2][2]};
        mp.inertiaValid = true;
      }
    }
  }
  return mp;
}

InertiaTensor InertiaAboutPoint(const MassProperties& mp, const Vec3& p) {
  InertiaTensor out;
  if (!mp.inertiaValid)
    return out;
  const Vec3 d = ray3d::Sub(p, mp.centroid);
  const double dx = d.x, dy = d.y, dz = d.z;
  const double d2 = dx * dx + dy * dy + dz * dz;
  const double m = mp.volume;
  out.xx = mp.Ixx + m * (d2 - dx * dx);
  out.yy = mp.Iyy + m * (d2 - dy * dy);
  out.zz = mp.Izz + m * (d2 - dz * dz);
  out.xy = mp.Ixy - m * dx * dy;
  out.xz = mp.Ixz - m * dx * dz;
  out.yz = mp.Iyz - m * dy * dz;
  return out;
}

void JacobiEigenSymmetric3x3(const double A[3][3], double eig[3], double vec[3][3]) {
  double B[3][3];
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      B[i][j] = A[i][j];
  double V[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  const int maxIter = 50;
  const double eps = 1e-15;
  for (int iter = 0; iter < maxIter; ++iter) {
    int p = 0, q = 1;
    double maxOff = std::fabs(B[0][1]);
    if (std::fabs(B[0][2]) > maxOff) {
      maxOff = std::fabs(B[0][2]);
      p = 0;
      q = 2;
    }
    if (std::fabs(B[1][2]) > maxOff) {
      maxOff = std::fabs(B[1][2]);
      p = 1;
      q = 2;
    }
    if (maxOff < eps)
      break;
    const double app = B[p][p];
    const double aqq = B[q][q];
    const double apq = B[p][q];
    const double phi = 0.5 * std::atan2(2.0 * apq, aqq - app);
    const double c = std::cos(phi);
    const double s = std::sin(phi);
    // Rotate B
    for (int k = 0; k < 3; ++k) {
      if (k == p || k == q)
        continue;
      const double bpk = B[p][k];
      const double bqk = B[q][k];
      B[p][k] = c * bpk - s * bqk;
      B[k][p] = B[p][k];
      B[q][k] = s * bpk + c * bqk;
      B[k][q] = B[q][k];
    }
    B[p][p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
    B[q][q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
    B[p][q] = 0.0;
    B[q][p] = 0.0;
    // Rotate eigenvectors
    for (int k = 0; k < 3; ++k) {
      const double vkp = V[k][p];
      const double vkq = V[k][q];
      V[k][p] = c * vkp - s * vkq;
      V[k][q] = s * vkp + c * vkq;
    }
  }
  eig[0] = B[0][0];
  eig[1] = B[1][1];
  eig[2] = B[2][2];
  // Sort descending with corresponding vectors (stable for equal values)
  for (int i = 0; i < 3; ++i) {
    for (int j = i + 1; j < 3; ++j) {
      if (eig[j] > eig[i]) {
        std::swap(eig[i], eig[j]);
        for (int k = 0; k < 3; ++k)
          std::swap(V[k][i], V[k][j]);
      }
    }
  }
  // Canonicalize sign: make the largest-magnitude component of each eigenvector positive
  for (int j = 0; j < 3; ++j) {
    double maxAbs = 0.0;
    int idx = 0;
    for (int k = 0; k < 3; ++k) {
      const double ab = std::fabs(V[k][j]);
      if (ab > maxAbs) {
        maxAbs = ab;
        idx = k;
      }
    }
    if (V[idx][j] < 0.0) {
      for (int k = 0; k < 3; ++k)
        V[k][j] = -V[k][j];
    }
  }
  // Enforce right-handedness: det of V should be +1
  const double det = V[0][0] * (V[1][1] * V[2][2] - V[1][2] * V[2][1]) -
                     V[0][1] * (V[1][0] * V[2][2] - V[1][2] * V[2][0]) +
                     V[0][2] * (V[1][0] * V[2][1] - V[1][1] * V[2][0]);
  if (det < 0.0) {
    for (int k = 0; k < 3; ++k)
      V[k][2] = -V[k][2];
  }
  // Orthonormalize for safety (Jacobi maintains orthonormality up to round-off)
  for (int j = 0; j < 3; ++j) {
    double len = std::sqrt(V[0][j] * V[0][j] + V[1][j] * V[1][j] + V[2][j] * V[2][j]);
    if (len > 1e-12) {
      for (int k = 0; k < 3; ++k)
        V[k][j] /= len;
    }
  }
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      vec[i][j] = V[i][j];
}

namespace {

// ---------------------------------------------------------------------------------------------
// Bounds.
// ---------------------------------------------------------------------------------------------


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

} // namespace brep
