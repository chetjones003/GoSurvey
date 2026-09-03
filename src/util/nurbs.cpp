#include "nurbs.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace nurbs {

using ray3d::Add;
using ray3d::Cross;
using ray3d::Dot;
using ray3d::Length;
using ray3d::Normalize;
using ray3d::Scale;
using ray3d::Sub;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] bool Finite(double x) { return std::isfinite(x); }
[[nodiscard]] bool Finite(const Vec3& v) { return Finite(v.x) && Finite(v.y) && Finite(v.z); }

/// The knot span index containing \p u for a clamped knot vector \p U with `n + 1` control points of
/// degree \p p (Piegl & Tiller, The NURBS Book, algorithm A2.1).
[[nodiscard]] int FindSpan(int n, int p, double u, const std::vector<double>& U) {
  if (u >= U[n + 1]) return n;
  if (u <= U[p]) return p;
  int low = p;
  int high = n + 1;
  int mid = (low + high) / 2;
  while (u < U[mid] || u >= U[mid + 1]) {
    if (u < U[mid])
      high = mid;
    else
      low = mid;
    mid = (low + high) / 2;
  }
  return mid;
}

/// Non-zero basis functions and (row 1) their first derivatives at \p u in span \p span, degree
/// \p p (algorithm A2.3, restricted to the first derivative). `out[0][r]` and `out[1][r]` are the
/// value and slope of the `(span - p + r)`-th basis function, `r = 0..p`.
void BasisDers(int span, double u, int p, const std::vector<double>& U,
               std::array<std::array<double, kMaxDegree + 1>, 2>* out) {
  std::array<std::array<double, kMaxDegree + 1>, kMaxDegree + 1> ndu{};
  std::array<double, kMaxDegree + 1> left{};
  std::array<double, kMaxDegree + 1> right{};

  ndu[0][0] = 1.0;
  for (int j = 1; j <= p; ++j) {
    left[j] = u - U[span + 1 - j];
    right[j] = U[span + j] - u;
    double saved = 0.0;
    for (int r = 0; r < j; ++r) {
      ndu[j][r] = right[r + 1] + left[j - r];  // lower triangle: knot differences
      const double temp = ndu[r][j - 1] / ndu[j][r];
      ndu[r][j] = saved + right[r + 1] * temp;  // upper triangle: basis functions
      saved = left[j - r] * temp;
    }
    ndu[j][j] = saved;
  }

  for (int j = 0; j <= p; ++j) (*out)[0][j] = ndu[j][p];

  // First derivatives.
  std::array<std::array<double, kMaxDegree + 1>, 2> a{};
  for (int r = 0; r <= p; ++r) {
    int s1 = 0;
    int s2 = 1;
    a[0][0] = 1.0;
    const int k = 1;
    double d = 0.0;
    const int rk = r - k;
    const int pk = p - k;
    if (r >= k) {
      a[s2][0] = a[s1][0] / ndu[pk + 1][rk];
      d = a[s2][0] * ndu[rk][pk];
    }
    const int j1 = (rk >= -1) ? 1 : -rk;
    const int j2 = (r - 1 <= pk) ? k - 1 : p - r;
    for (int j = j1; j <= j2; ++j) {
      a[s2][j] = (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][rk + j];
      d += a[s2][j] * ndu[rk + j][pk];
    }
    if (r <= pk) {
      a[s2][k] = -a[s1][k - 1] / ndu[pk + 1][r];
      d += a[s2][k] * ndu[r][pk];
    }
    (*out)[1][r] = d * static_cast<double>(p);
    (void)s2;
  }
}

/// Basis-function values only (no derivatives) — algorithm A2.2.
void BasisFuns(int span, double u, int p, const std::vector<double>& U,
               std::array<double, kMaxDegree + 1>* out) {
  std::array<double, kMaxDegree + 1> left{};
  std::array<double, kMaxDegree + 1> right{};
  (*out)[0] = 1.0;
  for (int j = 1; j <= p; ++j) {
    left[j] = u - U[span + 1 - j];
    right[j] = U[span + j] - u;
    double saved = 0.0;
    for (int r = 0; r < j; ++r) {
      const double temp = (*out)[r] / (right[r + 1] + left[j - r]);
      (*out)[r] = saved + right[r + 1] * temp;
      saved = left[j - r] * temp;
    }
    (*out)[j] = saved;
  }
}

[[nodiscard]] bool KnotsClamped(const std::vector<double>& U, int n, int p) {
  for (int i = 1; i <= p; ++i) {
    if (U[i] != U[0]) return false;
    if (U[n + i] != U[n + p]) return false;
  }
  return true;
}

}  // namespace

const char* PatchProblemText(PatchProblem p) {
  switch (p) {
  case PatchProblem::Ok: return "the patch is valid";
  case PatchProblem::DegreeOutOfRange: return "a NURBS patch degree is below 1 or above 3";
  case PatchProblem::TooFewControlPoints:
    return "a NURBS patch has fewer control points than its degree needs";
  case PatchProblem::ControlCountMismatch:
    return "a NURBS patch's control-point or weight count is not rows x columns";
  case PatchProblem::KnotVectorWrongLength:
    return "a NURBS patch knot vector's length does not match its control count and degree";
  case PatchProblem::KnotsNotNondecreasing: return "a NURBS patch knot vector steps backward";
  case PatchProblem::KnotVectorNotClamped: return "a NURBS patch knot vector is not clamped at its ends";
  case PatchProblem::DegenerateKnotDomain: return "a NURBS patch has no parameter span in one direction";
  case PatchProblem::NonFiniteControlPoint: return "a NURBS patch control point is not a finite number";
  case PatchProblem::NonPositiveWeight: return "a NURBS patch weight is zero, negative, or not finite";
  }
  return "unknown NURBS patch problem";
}

PatchProblem ValidatePatch(const Patch& patch) {
  const int pu = patch.degU;
  const int pv = patch.degV;
  if (pu < 1 || pu > kMaxDegree || pv < 1 || pv > kMaxDegree) return PatchProblem::DegreeOutOfRange;
  if (patch.nu < pu + 1 || patch.nv < pv + 1) return PatchProblem::TooFewControlPoints;

  const std::size_t cells = static_cast<std::size_t>(patch.nu) * static_cast<std::size_t>(patch.nv);
  if (patch.ctrl.size() != cells || patch.wts.size() != cells) return PatchProblem::ControlCountMismatch;

  if (patch.knotsU.size() != static_cast<std::size_t>(patch.nu + pu + 1) ||
      patch.knotsV.size() != static_cast<std::size_t>(patch.nv + pv + 1))
    return PatchProblem::KnotVectorWrongLength;

  for (const std::vector<double>* U : {&patch.knotsU, &patch.knotsV}) {
    for (std::size_t i = 0; i + 1 < U->size(); ++i) {
      if (!Finite((*U)[i]) || (*U)[i + 1] < (*U)[i]) return PatchProblem::KnotsNotNondecreasing;
    }
    if (!Finite(U->back())) return PatchProblem::KnotsNotNondecreasing;
  }
  if (!KnotsClamped(patch.knotsU, patch.nu, pu) || !KnotsClamped(patch.knotsV, patch.nv, pv))
    return PatchProblem::KnotVectorNotClamped;

  if (UMax(patch) - UMin(patch) <= 0.0 || VMax(patch) - VMin(patch) <= 0.0)
    return PatchProblem::DegenerateKnotDomain;

  for (const Vec3& c : patch.ctrl)
    if (!Finite(c)) return PatchProblem::NonFiniteControlPoint;
  for (double w : patch.wts)
    if (!Finite(w) || w <= 0.0) return PatchProblem::NonPositiveWeight;

  return PatchProblem::Ok;
}

double UMin(const Patch& patch) { return patch.knotsU[patch.degU]; }
double UMax(const Patch& patch) { return patch.knotsU[patch.nu]; }
double VMin(const Patch& patch) { return patch.knotsV[patch.degV]; }
double VMax(const Patch& patch) { return patch.knotsV[patch.nv]; }

Vec3 Evaluate(const Patch& patch, double u, double v) {
  u = std::clamp(u, UMin(patch), UMax(patch));
  v = std::clamp(v, VMin(patch), VMax(patch));

  const int pu = patch.degU;
  const int pv = patch.degV;
  const int su = FindSpan(patch.nu - 1, pu, u, patch.knotsU);
  const int sv = FindSpan(patch.nv - 1, pv, v, patch.knotsV);

  std::array<double, kMaxDegree + 1> Nu{};
  std::array<double, kMaxDegree + 1> Nv{};
  BasisFuns(su, u, pu, patch.knotsU, &Nu);
  BasisFuns(sv, v, pv, patch.knotsV, &Nv);

  Vec3 numer{0.0, 0.0, 0.0};
  double denom = 0.0;
  for (int b = 0; b <= pv; ++b) {
    const int j = sv - pv + b;
    for (int a = 0; a <= pu; ++a) {
      const int i = su - pu + a;
      const int idx = j * patch.nu + i;
      const double c = Nu[a] * Nv[b] * patch.wts[idx];
      numer = Add(numer, Scale(patch.ctrl[idx], c));
      denom += c;
    }
  }
  return Scale(numer, 1.0 / denom);
}

SurfacePoint EvaluateWithDerivs(const Patch& patch, double u, double v) {
  u = std::clamp(u, UMin(patch), UMax(patch));
  v = std::clamp(v, VMin(patch), VMax(patch));

  const int pu = patch.degU;
  const int pv = patch.degV;
  const int su = FindSpan(patch.nu - 1, pu, u, patch.knotsU);
  const int sv = FindSpan(patch.nv - 1, pv, v, patch.knotsV);

  std::array<std::array<double, kMaxDegree + 1>, 2> Nu{};
  std::array<std::array<double, kMaxDegree + 1>, 2> Nv{};
  BasisDers(su, u, pu, patch.knotsU, &Nu);
  BasisDers(sv, v, pv, patch.knotsV, &Nv);

  // Homogeneous point Aw = sum N_i N_j w_ij P_ij, scalar w = sum N_i N_j w_ij, and the u/v
  // derivatives of both, then the rational quotient rule for S and its partials.
  Vec3 Aw{0.0, 0.0, 0.0};
  Vec3 Awu{0.0, 0.0, 0.0};
  Vec3 Awv{0.0, 0.0, 0.0};
  double w = 0.0;
  double wu = 0.0;
  double wv = 0.0;
  for (int b = 0; b <= pv; ++b) {
    const int j = sv - pv + b;
    for (int a = 0; a <= pu; ++a) {
      const int i = su - pu + a;
      const int idx = j * patch.nu + i;
      const double wij = patch.wts[idx];
      const Vec3& P = patch.ctrl[idx];

      const double nu0 = Nu[0][a];
      const double nu1 = Nu[1][a];
      const double nv0 = Nv[0][b];
      const double nv1 = Nv[1][b];

      const double c = nu0 * nv0 * wij;
      const double cu = nu1 * nv0 * wij;
      const double cv = nu0 * nv1 * wij;

      Aw = Add(Aw, Scale(P, c));
      Awu = Add(Awu, Scale(P, cu));
      Awv = Add(Awv, Scale(P, cv));
      w += c;
      wu += cu;
      wv += cv;
    }
  }

  SurfacePoint out;
  const double invW = 1.0 / w;
  out.p = Scale(Aw, invW);
  out.du = Scale(Sub(Awu, Scale(out.p, wu)), invW);
  out.dv = Scale(Sub(Awv, Scale(out.p, wv)), invW);
  const Vec3 n = Cross(out.du, out.dv);
  out.normal = (Dot(n, n) > 1e-24) ? Normalize(n) : Vec3{0.0, 0.0, 0.0};
  return out;
}

Patch Translate(const Patch& patch, const Vec3& delta) {
  Patch out = patch;
  for (Vec3& c : out.ctrl) c = Add(c, delta);
  return out;
}

// ---------------------------------------------------------------------------------------------
// Builders.
// ---------------------------------------------------------------------------------------------

Patch RuledLinear(const std::vector<Vec3>& row0, const std::vector<Vec3>& row1) {
  Patch patch;
  const int n = static_cast<int>(row0.size());
  if (n < 2 || static_cast<int>(row1.size()) != n) return patch;  // invalid; ValidatePatch will say so

  patch.degU = 1;
  patch.degV = 1;
  patch.nu = n;
  patch.nv = 2;

  // Chord-length parameters along row0, normalised to [0, 1].
  std::vector<double> t(n, 0.0);
  for (int i = 1; i < n; ++i) t[i] = t[i - 1] + Length(Sub(row0[i], row0[i - 1]));
  const double total = t[n - 1];
  if (total > 0.0)
    for (double& tv : t) tv /= total;
  else
    for (int i = 0; i < n; ++i) t[i] = static_cast<double>(i) / (n - 1);

  patch.knotsU.reserve(n + 2);
  patch.knotsU.push_back(t[0]);
  for (double tv : t) patch.knotsU.push_back(tv);
  patch.knotsU.push_back(t[n - 1]);
  patch.knotsV = {0.0, 0.0, 1.0, 1.0};

  patch.ctrl.resize(static_cast<std::size_t>(n) * 2);
  patch.wts.assign(static_cast<std::size_t>(n) * 2, 1.0);
  for (int i = 0; i < n; ++i) {
    patch.ctrl[i] = row0[i];
    patch.ctrl[n + i] = row1[i];
  }
  return patch;
}

void RationalArc(const Vec3& centre, const Vec3& start, const Vec3& axis, double sweepRad,
                 std::vector<Vec3>* outPts, std::vector<double>* outWts) {
  outPts->clear();
  outWts->clear();

  const Vec3 e0 = Normalize(Sub(start, centre));
  const double r = Length(Sub(start, centre));
  const Vec3 ax = Normalize(axis);
  const Vec3 e1 = Normalize(Cross(ax, e0));  // +sweep rotates e0 toward e1

  const int segments = std::max(1, static_cast<int>(std::ceil(std::fabs(sweepRad) / (kPi / 2.0) - 1e-9)));
  const double segAngle = sweepRad / segments;
  const double half = segAngle / 2.0;
  const double wMid = std::cos(half);

  auto onCircle = [&](double ang) {
    return Add(centre, Add(Scale(e0, r * std::cos(ang)), Scale(e1, r * std::sin(ang))));
  };

  outPts->push_back(onCircle(0.0));
  outWts->push_back(1.0);
  for (int s = 0; s < segments; ++s) {
    const double a0 = s * segAngle;
    const double a1 = (s + 1) * segAngle;
    const double mid = (a0 + a1) / 2.0;
    // Tangent intersection = point on the circle at the mid-angle scaled out by 1/cos(half).
    const Vec3 midPt =
        Add(centre, Scale(Add(Scale(e0, std::cos(mid)), Scale(e1, std::sin(mid))), r / wMid));
    outPts->push_back(midPt);
    outWts->push_back(wMid);
    outPts->push_back(onCircle(a1));
    outWts->push_back(1.0);
  }
}

Patch ArcRibbon(const Vec3& centre0, const Vec3& start0, const Vec3& centre1, const Vec3& start1,
                const Vec3& axis, double sweepRad) {
  Patch patch;
  std::vector<Vec3> p0;
  std::vector<Vec3> p1;
  std::vector<double> w0;
  std::vector<double> w1;
  RationalArc(centre0, start0, axis, sweepRad, &p0, &w0);
  RationalArc(centre1, start1, axis, sweepRad, &p1, &w1);
  const int n = static_cast<int>(p0.size());
  if (n < 3 || static_cast<int>(p1.size()) != n) return patch;

  const int segments = (n - 1) / 2;
  patch.degU = 2;
  patch.degV = 1;
  patch.nu = n;
  patch.nv = 2;

  patch.knotsU.reserve(static_cast<std::size_t>(n + 3));
  patch.knotsU.insert(patch.knotsU.end(), {0.0, 0.0, 0.0});
  for (int s = 1; s < segments; ++s) {
    patch.knotsU.push_back(static_cast<double>(s));
    patch.knotsU.push_back(static_cast<double>(s));
  }
  patch.knotsU.insert(patch.knotsU.end(),
                      {static_cast<double>(segments), static_cast<double>(segments),
                       static_cast<double>(segments)});
  patch.knotsV = {0.0, 0.0, 1.0, 1.0};

  patch.ctrl.resize(static_cast<std::size_t>(n) * 2);
  patch.wts.resize(static_cast<std::size_t>(n) * 2);
  for (int i = 0; i < n; ++i) {
    patch.ctrl[i] = p0[i];
    patch.wts[i] = w0[i];
    patch.ctrl[n + i] = p1[i];
    patch.wts[n + i] = w1[i];
  }
  return patch;
}

}  // namespace nurbs
