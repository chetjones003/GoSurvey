#include "curveintersect.hpp"

#include <algorithm>
#include <cmath>

namespace curveisect {
namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kEps = 1.e-12;

double Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
Vec2 Sub(const Vec2& a, const Vec2& b) { return Vec2{a.x - b.x, a.y - b.y}; }

/// Wrap to [0, 2π) so a solved angle can be range-tested against a sweep.
double Wrap2Pi(double t) {
  double w = std::fmod(t, kTwoPi);
  if (w < 0.0)
    w += kTwoPi;
  return w;
}

/// Solve `α·cos t + β·sin t = γ` for every root in [0, 2π).
///
/// Rewritten as R·cos(t − φ) = γ with R = hypot(α, β). |γ| > R means the line misses the conic;
/// |γ| == R is a tangency, reported once rather than as two coincident roots — a doubled root would
/// put two identical snap candidates on top of each other.
int SolveHarmonic(double alpha, double beta, double gamma, double* out) {
  const double R = std::hypot(alpha, beta);
  if (R < kEps)
    return 0;  // degenerate conic in this direction
  const double phi = std::atan2(beta, alpha);
  double c = gamma / R;
  if (c > 1.0 + 1.e-9 || c < -1.0 - 1.e-9)
    return 0;
  c = std::clamp(c, -1.0, 1.0);
  const double d = std::acos(c);
  if (d < 1.e-9) {  // tangent: one root
    out[0] = Wrap2Pi(phi);
    return 1;
  }
  out[0] = Wrap2Pi(phi + d);
  out[1] = Wrap2Pi(phi - d);
  return 2;
}

bool NearlyEqual(const Vec2& a, const Vec2& b, double tol) {
  return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol;
}

/// Drop duplicates so a tangency found by two different brackets is reported once.
void PushUnique(std::vector<Hit2>* out, const Hit2& h, double tol) {
  for (const Hit2& e : *out) {
    if (NearlyEqual(e.p, h.p, tol))
      return;
  }
  out->push_back(h);
}

/// True when a conic is a true circle: perpendicular axes of equal length. Only then is the
/// analytic circle×circle path valid — a projected circle usually fails this and must go numeric.
bool IsCircle(const Conic& k, double* radius) {
  const double lu = std::hypot(k.u.x, k.u.y);
  const double lv = std::hypot(k.v.x, k.v.y);
  if (lu < kEps || lv < kEps)
    return false;
  if (std::fabs(lu - lv) > 1.e-9 * std::max(1.0, lu))
    return false;
  if (std::fabs(Cross(k.u, k.v)) < (1.0 - 1.e-12) * lu * lv)
    return false;  // not perpendicular
  *radius = lu;
  return true;
}

/// Angle of point \p p on conic \p k, by projecting onto the (u, v) basis. Handles the non-
/// orthogonal case (a projected circle) via the inverse of the 2×2 [u v] matrix.
double AngleOf(const Conic& k, const Vec2& p) {
  const Vec2 d = Sub(p, k.c);
  const double det = Cross(k.u, k.v);
  if (std::fabs(det) < kEps)
    return 0.0;
  const double cs = (d.x * k.v.y - d.y * k.v.x) / det;   // cos t
  const double sn = (k.u.x * d.y - k.u.y * d.x) / det;   // sin t
  return Wrap2Pi(std::atan2(sn, cs));
}

} // namespace

bool Conic::isFullTurn() const { return std::fabs(tSweep) >= kTwoPi - 1.e-9; }

bool Conic::containsAngle(double t) const {
  if (isFullTurn())
    return true;
  const double rel = Wrap2Pi((tSweep >= 0.0 ? t - tStart : tStart - t));
  return rel <= std::fabs(tSweep) + 1.e-9;
}

Vec2 Conic::point(double t) const {
  const double c = std::cos(t);
  const double s = std::sin(t);
  return Vec2{this->c.x + u.x * c + v.x * s, this->c.y + u.y * c + v.y * s};
}

Vec2 Conic::tangent(double t) const {
  const double c = std::cos(t);
  const double s = std::sin(t);
  return Vec2{-u.x * s + v.x * c, -u.y * s + v.y * c};
}

Conic MakeCircle(double cx, double cy, double r) {
  Conic k;
  k.c = {cx, cy};
  k.u = {r, 0.0};
  k.v = {0.0, r};
  return k;
}

Conic MakeArc(double cx, double cy, double r, double startRad, double sweepRad) {
  Conic k = MakeCircle(cx, cy, r);
  k.tStart = startRad;
  k.tSweep = sweepRad;
  return k;
}

Conic MakeEllipse(double cx, double cy, double majVx, double majVy, double ratio) {
  Conic k;
  k.c = {cx, cy};
  k.u = {majVx, majVy};
  k.v = {-majVy * ratio, majVx * ratio};  // perpendicular, scaled by the minor/major ratio
  return k;
}

void IntersectSegSeg(const Seg& a, const Seg& b, std::vector<Hit2>* out) {
  if (!out)
    return;
  const Vec2 r = Sub(a.b, a.a);
  const Vec2 s = Sub(b.b, b.a);
  const double denom = Cross(r, s);
  if (std::fabs(denom) < kEps)
    return;  // parallel or collinear — see the header note
  const Vec2 d = Sub(b.a, a.a);
  const double t = Cross(d, s) / denom;
  const double u = Cross(d, r) / denom;
  if (t < -1.e-9 || t > 1.0 + 1.e-9 || u < -1.e-9 || u > 1.0 + 1.e-9)
    return;  // crossing lies off one of the segments
  Hit2 h;
  h.tA = std::clamp(t, 0.0, 1.0);
  h.tB = std::clamp(u, 0.0, 1.0);
  h.p = Vec2{a.a.x + r.x * h.tA, a.a.y + r.y * h.tA};
  out->push_back(h);
}

void IntersectSegConic(const Seg& s, const Conic& k, std::vector<Hit2>* out) {
  if (!out)
    return;
  const Vec2 d = Sub(s.b, s.a);
  const double len2 = d.x * d.x + d.y * d.y;
  if (len2 < kEps)
    return;
  // Every point of the segment satisfies cross(d, P - s.a) == 0. Substituting P = c + u·cos t +
  // v·sin t turns that into a harmonic equation in t alone — the segment parameter drops out and
  // comes back below. This is why ellipses need no special case.
  const Vec2 w = Sub(k.c, s.a);
  const double alpha = Cross(d, k.u);
  const double beta = Cross(d, k.v);
  const double gamma = -Cross(d, w);
  double roots[2];
  const int n = SolveHarmonic(alpha, beta, gamma, roots);
  for (int i = 0; i < n; ++i) {
    const double t = roots[i];
    if (!k.containsAngle(t))
      continue;
    const Vec2 p = k.point(t);
    const double sp = ((p.x - s.a.x) * d.x + (p.y - s.a.y) * d.y) / len2;
    if (sp < -1.e-9 || sp > 1.0 + 1.e-9)
      continue;
    Hit2 h;
    h.p = p;
    h.tA = std::clamp(sp, 0.0, 1.0);
    h.tB = t;
    PushUnique(out, h, 1.e-9);
  }
}

namespace {

/// One 2-D Newton step set: solve P(u) − Q(v) = 0 from a bracketed guess.
///
/// The Jacobian is [P'(u), −Q'(v)]; it goes singular exactly where the curves are tangent, which is
/// also where a snap is least useful, so a singular step abandons the root rather than nudging it
/// somewhere arbitrary.
bool RefineConicConic(const Conic& a, const Conic& b, double* ua, double* vb, double tol) {
  for (int iter = 0; iter < 40; ++iter) {
    const Vec2 pa = a.point(*ua);
    const Vec2 pb = b.point(*vb);
    const double fx = pa.x - pb.x;
    const double fy = pa.y - pb.y;
    if (std::fabs(fx) <= tol && std::fabs(fy) <= tol)
      return true;
    // Solve J·[du dv]ᵀ = −F with J = [ P'(u)  −Q'(v) ] as columns, by Cramer's rule.
    const Vec2 da = a.tangent(*ua);
    const Vec2 db = b.tangent(*vb);
    const double det = -da.x * db.y + da.y * db.x;
    if (std::fabs(det) < 1.e-14)
      return false;  // tangent curves: no isolated root to converge on
    const double du = (fx * db.y - fy * db.x) / det;
    const double dv = (fx * da.y - fy * da.x) / det;
    *ua += du;
    *vb += dv;
  }
  return false;
}

/// Segment × segment returning parameters only — no container, so it can sit in the bracketing
/// loop below without allocating on every one of its thousands of iterations.
bool SegSegParams(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1, double* t, double* u) {
  const Vec2 r = Sub(a1, a0);
  const Vec2 s = Sub(b1, b0);
  const double denom = Cross(r, s);
  if (std::fabs(denom) < kEps)
    return false;
  const Vec2 d = Sub(b0, a0);
  *t = Cross(d, s) / denom;
  *u = Cross(d, r) / denom;
  return *t >= -1.e-9 && *t <= 1.0 + 1.e-9 && *u >= -1.e-9 && *u <= 1.0 + 1.e-9;
}

} // namespace

void IntersectConicConic(const Conic& a, const Conic& b, std::vector<Hit2>* out, double tol) {
  if (!out)
    return;

  double ra = 0.0;
  double rb = 0.0;
  if (IsCircle(a, &ra) && IsCircle(b, &rb)) {
    // Analytic: the radical line of two circles. Concentric pairs (d ≈ 0) have either no
    // intersection or infinitely many (identical circles) — neither is a snap point.
    const Vec2 delta = Sub(b.c, a.c);
    const double dist = std::hypot(delta.x, delta.y);
    if (dist < kEps || dist > ra + rb + 1.e-9 || dist < std::fabs(ra - rb) - 1.e-9)
      return;
    const double aa = (ra * ra - rb * rb + dist * dist) / (2.0 * dist);
    const double h2 = ra * ra - aa * aa;
    const double h = h2 > 0.0 ? std::sqrt(h2) : 0.0;
    const Vec2 mid{a.c.x + delta.x * aa / dist, a.c.y + delta.y * aa / dist};
    const Vec2 perp{-delta.y / dist, delta.x / dist};
    const int n = (h > 1.e-9) ? 2 : 1;  // tangent circles meet once
    for (int i = 0; i < n; ++i) {
      const double sgn = (i == 0) ? 1.0 : -1.0;
      Hit2 hit;
      hit.p = Vec2{mid.x + perp.x * h * sgn, mid.y + perp.y * h * sgn};
      hit.tA = AngleOf(a, hit.p);
      hit.tB = AngleOf(b, hit.p);
      if (a.containsAngle(hit.tA) && b.containsAngle(hit.tB))
        PushUnique(out, hit, 1.e-9);
    }
    return;
  }

  // General case — at least one ellipse, or a projected circle, so no closed form applies. Bracket
  // by walking both curves as chords and looking for chord crossings, then refine each bracket with
  // Newton. 72 chords per curve keeps the bracket fine enough that two conics of any realistic
  // aspect cannot cross twice within one cell, while staying cheap enough for the snap loop: the
  // points are computed once (2 × 73 trig pairs) and the inner test is an AABB reject followed by a
  // segment crossing with no allocation.
  constexpr int kSteps = 72;
  const double spanA = a.isFullTurn() ? kTwoPi : std::fabs(a.tSweep);
  const double baseA = a.isFullTurn() ? 0.0 : std::min(a.tStart, a.tStart + a.tSweep);
  const double spanB = b.isFullTurn() ? kTwoPi : std::fabs(b.tSweep);
  const double baseB = b.isFullTurn() ? 0.0 : std::min(b.tStart, b.tStart + b.tSweep);

  Vec2 pa[kSteps + 1];
  Vec2 pb[kSteps + 1];
  for (int i = 0; i <= kSteps; ++i) {
    pa[i] = a.point(baseA + spanA * static_cast<double>(i) / kSteps);
    pb[i] = b.point(baseB + spanB * static_cast<double>(i) / kSteps);
  }

  for (int i = 0; i < kSteps; ++i) {
    const double axMin = std::min(pa[i].x, pa[i + 1].x);
    const double axMax = std::max(pa[i].x, pa[i + 1].x);
    const double ayMin = std::min(pa[i].y, pa[i + 1].y);
    const double ayMax = std::max(pa[i].y, pa[i + 1].y);
    for (int j = 0; j < kSteps; ++j) {
      if (std::max(pb[j].x, pb[j + 1].x) < axMin || std::min(pb[j].x, pb[j + 1].x) > axMax ||
          std::max(pb[j].y, pb[j + 1].y) < ayMin || std::min(pb[j].y, pb[j + 1].y) > ayMax)
        continue;
      double ta = 0.0;
      double tb = 0.0;
      if (!SegSegParams(pa[i], pa[i + 1], pb[j], pb[j + 1], &ta, &tb))
        continue;
      double ua = baseA + spanA * (static_cast<double>(i) + ta) / kSteps;
      double vb = baseB + spanB * (static_cast<double>(j) + tb) / kSteps;
      if (!RefineConicConic(a, b, &ua, &vb, tol))
        continue;
      if (!a.containsAngle(ua) || !b.containsAngle(vb))
        continue;
      Hit2 hit;
      hit.p = a.point(ua);
      hit.tA = Wrap2Pi(ua);
      hit.tB = Wrap2Pi(vb);
      PushUnique(out, hit, 1.e-6);
    }
  }
}

Vec2 ProjectPoint(double wx, double wy, double wz, const double right[3], const double up[3]) {
  return Vec2{wx * right[0] + wy * right[1] + wz * right[2], wx * up[0] + wy * up[1] + wz * up[2]};
}

Seg ProjectSeg(double x0, double y0, double z0, double x1, double y1, double z1, const double right[3],
               const double up[3]) {
  Seg s;
  s.a = ProjectPoint(x0, y0, z0, right, up);
  s.b = ProjectPoint(x1, y1, z1, right, up);
  return s;
}

Conic ProjectConic(const Conic& k, double z, const double right[3], const double up[3]) {
  Conic o = k;
  o.c = ProjectPoint(k.c.x, k.c.y, z, right, up);
  // u and v are DIRECTIONS in the entity's plane, so they carry no elevation of their own — the
  // plane is horizontal, so their Z component is zero and only the map's XY rows apply. Projecting
  // them as if they were points would add the plane's elevation to each axis and shear the conic.
  o.u = ProjectPoint(k.u.x, k.u.y, 0.0, right, up);
  o.v = ProjectPoint(k.v.x, k.v.y, 0.0, right, up);
  return o;
}

} // namespace curveisect
