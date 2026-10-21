#include "spline.hpp"

#include <algorithm>
#include <cmath>

namespace spline {

namespace {

constexpr double kMinChord = 1e-9;  ///< Two consecutive points closer than this are the same point.

[[nodiscard]] bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

[[nodiscard]] double Dist(const Vec3& a, const Vec3& b) { return ray3d::Length(ray3d::Sub(a, b)); }

/// Index of the knot span containing \p u (Piegl & Tiller A2.1), for a curve of \p count control points.
[[nodiscard]] int FindSpan(int count, int p, double u, const std::vector<double>& k) {
  const int n = count - 1;
  if (u >= k[static_cast<std::size_t>(n) + 1])
    return n;
  if (u <= k[static_cast<std::size_t>(p)])
    return p;
  int lo = p, hi = n + 1, mid = (lo + hi) / 2;
  while (u < k[static_cast<std::size_t>(mid)] || u >= k[static_cast<std::size_t>(mid) + 1]) {
    if (u < k[static_cast<std::size_t>(mid)])
      hi = mid;
    else
      lo = mid;
    mid = (lo + hi) / 2;
  }
  return mid;
}

/// The nonzero basis functions and their derivatives up to order \p nd at \p u (Piegl & Tiller A2.3).
/// `ders[d][j]` is the d'th derivative of basis function `span - p + j`.
void BasisDerivs(int span, double u, int p, int nd, const std::vector<double>& U,
                 std::vector<std::vector<double>>* ders) {
  const auto at = [&](int i) { return U[static_cast<std::size_t>(i)]; };
  std::vector<std::vector<double>> ndu(static_cast<std::size_t>(p) + 1, std::vector<double>(static_cast<std::size_t>(p) + 1, 0.0));
  std::vector<double> left(static_cast<std::size_t>(p) + 1), right(static_cast<std::size_t>(p) + 1);
  ndu[0][0] = 1.0;
  for (int j = 1; j <= p; ++j) {
    left[static_cast<std::size_t>(j)] = u - at(span + 1 - j);
    right[static_cast<std::size_t>(j)] = at(span + j) - u;
    double saved = 0.0;
    for (int r = 0; r < j; ++r) {
      ndu[static_cast<std::size_t>(j)][static_cast<std::size_t>(r)] = right[static_cast<std::size_t>(r) + 1] + left[static_cast<std::size_t>(j - r)];
      const double temp = ndu[static_cast<std::size_t>(r)][static_cast<std::size_t>(j) - 1] / ndu[static_cast<std::size_t>(j)][static_cast<std::size_t>(r)];
      ndu[static_cast<std::size_t>(r)][static_cast<std::size_t>(j)] = saved + right[static_cast<std::size_t>(r) + 1] * temp;
      saved = left[static_cast<std::size_t>(j - r)] * temp;
    }
    ndu[static_cast<std::size_t>(j)][static_cast<std::size_t>(j)] = saved;
  }
  ders->assign(static_cast<std::size_t>(nd) + 1, std::vector<double>(static_cast<std::size_t>(p) + 1, 0.0));
  for (int j = 0; j <= p; ++j)
    (*ders)[0][static_cast<std::size_t>(j)] = ndu[static_cast<std::size_t>(j)][static_cast<std::size_t>(p)];
  std::vector<std::vector<double>> a(2, std::vector<double>(static_cast<std::size_t>(p) + 1, 0.0));
  for (int r = 0; r <= p; ++r) {
    int s1 = 0, s2 = 1;
    a[0][0] = 1.0;
    for (int k = 1; k <= nd; ++k) {
      double d = 0.0;
      const int rk = r - k, pk = p - k;
      if (r >= k) {
        a[static_cast<std::size_t>(s2)][0] = a[static_cast<std::size_t>(s1)][0] / ndu[static_cast<std::size_t>(pk) + 1][static_cast<std::size_t>(rk)];
        d = a[static_cast<std::size_t>(s2)][0] * ndu[static_cast<std::size_t>(rk)][static_cast<std::size_t>(pk)];
      }
      const int j1 = rk >= -1 ? 1 : -rk;
      const int j2 = (r - 1 <= pk) ? k - 1 : p - r;
      for (int j = j1; j <= j2; ++j) {
        a[static_cast<std::size_t>(s2)][static_cast<std::size_t>(j)] =
            (a[static_cast<std::size_t>(s1)][static_cast<std::size_t>(j)] - a[static_cast<std::size_t>(s1)][static_cast<std::size_t>(j) - 1]) /
            ndu[static_cast<std::size_t>(pk) + 1][static_cast<std::size_t>(rk) + static_cast<std::size_t>(j)];
        d += a[static_cast<std::size_t>(s2)][static_cast<std::size_t>(j)] *
             ndu[static_cast<std::size_t>(rk) + static_cast<std::size_t>(j)][static_cast<std::size_t>(pk)];
      }
      if (r <= pk) {
        a[static_cast<std::size_t>(s2)][static_cast<std::size_t>(k)] = -a[static_cast<std::size_t>(s1)][static_cast<std::size_t>(k) - 1] / ndu[static_cast<std::size_t>(pk) + 1][static_cast<std::size_t>(r)];
        d += a[static_cast<std::size_t>(s2)][static_cast<std::size_t>(k)] * ndu[static_cast<std::size_t>(r)][static_cast<std::size_t>(pk)];
      }
      (*ders)[static_cast<std::size_t>(k)][static_cast<std::size_t>(r)] = d;
      std::swap(s1, s2);
    }
  }
  double r = static_cast<double>(p);
  for (int k = 1; k <= nd; ++k) {
    for (int j = 0; j <= p; ++j)
      (*ders)[static_cast<std::size_t>(k)][static_cast<std::size_t>(j)] *= r;
    r *= static_cast<double>(p - k);
  }
}

/// Solve `A x = b` for \p rhs.size() right-hand sides at once (Gaussian elimination, partial
/// pivoting). \p a is square, row-major `n x n`; \p b holds one Vec3 per row. False if singular.
[[nodiscard]] bool Solve(std::vector<double> a, std::vector<Vec3> b, std::size_t n, std::vector<Vec3>* x) {
  for (std::size_t col = 0; col < n; ++col) {
    std::size_t piv = col;
    for (std::size_t r = col + 1; r < n; ++r)
      if (std::fabs(a[r * n + col]) > std::fabs(a[piv * n + col]))
        piv = r;
    if (std::fabs(a[piv * n + col]) < 1e-12)
      return false;
    if (piv != col) {
      for (std::size_t c = 0; c < n; ++c)
        std::swap(a[piv * n + c], a[col * n + c]);
      std::swap(b[piv], b[col]);
    }
    for (std::size_t r = col + 1; r < n; ++r) {
      const double f = a[r * n + col] / a[col * n + col];
      if (f == 0.0)
        continue;
      for (std::size_t c = col; c < n; ++c)
        a[r * n + c] -= f * a[col * n + c];
      b[r] = ray3d::Sub(b[r], ray3d::Scale(b[col], f));
    }
  }
  x->assign(n, Vec3{});
  for (std::size_t i = n; i-- > 0;) {
    Vec3 acc = b[i];
    for (std::size_t c = i + 1; c < n; ++c)
      acc = ray3d::Sub(acc, ray3d::Scale((*x)[c], a[i * n + c]));
    (*x)[i] = ray3d::Scale(acc, 1.0 / a[i * n + i]);
  }
  return true;
}

/// A periodic cubic's expanded control points from its \p m distinct ones: the first three repeat at
/// the end, `E[j] = C[(j - 1) mod m]`, with uniform knots `0 .. m + 6` (domain `[3, m + 3]`).
void ExpandPeriodic(const std::vector<Vec3>& c, Spline* out) {
  const int m = static_cast<int>(c.size());
  out->degree = 3;
  out->periodic = true;
  out->weights.clear();
  out->ctrl.assign(static_cast<std::size_t>(m) + 3, Vec3{});
  for (int j = 0; j < m + 3; ++j)
    out->ctrl[static_cast<std::size_t>(j)] = c[static_cast<std::size_t>(((j - 1) % m + m) % m)];
  out->knots.assign(static_cast<std::size_t>(m) + 7, 0.0);
  for (int j = 0; j < m + 7; ++j)
    out->knots[static_cast<std::size_t>(j)] = static_cast<double>(j);
}

/// Appends points to \p pts / parameters to \p ts until every chord is within \p tol of the curve.
void Subdivide(const Spline& s, double a, const Vec3& pa, double b, const Vec3& pb, double tol, int depth,
               std::vector<double>* ts, std::vector<Vec3>* pts) {
  const auto segDist = [&](const Vec3& q) {
    const Vec3 ab = ray3d::Sub(pb, pa);
    const double len2 = ray3d::Dot(ab, ab);
    double t = len2 > 0.0 ? ray3d::Dot(ray3d::Sub(q, pa), ab) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return Dist(q, ray3d::Add(pa, ray3d::Scale(ab, t)));
  };
  const double m = 0.5 * (a + b);
  const Vec3 pm = Evaluate(s, m);
  bool split = depth < 2;
  if (!split && depth < 14) {
    const double dev = std::max({segDist(pm), segDist(Evaluate(s, 0.5 * (a + m))), segDist(Evaluate(s, 0.5 * (m + b)))});
    split = dev > tol;
  }
  if (!split) {
    ts->push_back(b);
    pts->push_back(pb);
    return;
  }
  Subdivide(s, a, pa, m, pm, tol, depth + 1, ts, pts);
  Subdivide(s, m, pm, b, pb, tol, depth + 1, ts, pts);
}

void TessellateWithParams(const Spline& s, double tol, std::vector<double>* ts, std::vector<Vec3>* pts) {
  ts->clear();
  pts->clear();
  if (!Validate(s).empty())
    return;
  const double lo = DomainMin(s), hi = DomainMax(s);
  std::vector<double> breaks;
  for (double k : s.knots)
    if (k >= lo && k <= hi && (breaks.empty() || k > breaks.back() + 1e-12))
      breaks.push_back(k);
  ts->push_back(lo);
  pts->push_back(Evaluate(s, lo));
  for (std::size_t i = 0; i + 1 < breaks.size(); ++i)
    Subdivide(s, breaks[i], Evaluate(s, breaks[i]), breaks[i + 1], Evaluate(s, breaks[i + 1]), tol, 0, ts, pts);
  if (s.periodic && pts->size() > 1)
    pts->back() = pts->front();  // the join is one point, not two that agree to rounding
}

}  // namespace

bool MakeFit(const std::vector<Vec3>& pts, bool closed, Spline* out, std::string* why) {
  const auto fail = [&](const char* msg) {
    if (why)
      *why = msg;
    return false;
  };
  const std::size_t n = pts.size();
  if (n < (closed ? 3u : 2u))
    return fail(closed ? "a closed spline needs at least three points" : "a spline needs at least two points");
  for (std::size_t i = 0; i < n; ++i) {
    if (!Finite(pts[i]))
      return fail("a point is not a finite number");
    const std::size_t j = (i + 1) % n;
    if ((closed || j != 0) && Dist(pts[i], pts[j]) < kMinChord)
      return fail("two neighbouring points coincide");
  }

  Spline s;
  s.fit = pts;
  if (closed) {
    // Periodic uniform cubic: S(3 + k) = (E[k] + 4 E[k+1] + E[k+2]) / 6 = P[k], i.e. a circulant
    // system for the distinct control points.
    std::vector<double> a(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
      a[i * n + i] += 4.0 / 6.0;
      a[i * n + (i + 1) % n] += 1.0 / 6.0;
      a[i * n + (i + n - 1) % n] += 1.0 / 6.0;
    }
    std::vector<Vec3> c;
    if (!Solve(a, pts, n, &c))
      return fail("those points cannot be joined into a smooth closed curve");
    ExpandPeriodic(c, &s);
    s.fit = pts;
    *out = std::move(s);
    return true;
  }

  // Open natural cubic: n + 2 control points, n interpolation conditions and zero second derivative
  // at both ends. Chord-length parameters; interior knots at the interior parameters.
  std::vector<double> u(n, 0.0);
  for (std::size_t i = 1; i < n; ++i)
    u[i] = u[i - 1] + Dist(pts[i], pts[i - 1]);
  for (std::size_t i = 1; i < n; ++i)
    u[i] /= u[n - 1];
  const std::size_t m = n + 2;
  s.degree = 3;
  s.knots.assign(4, 0.0);
  for (std::size_t i = 1; i + 1 < n; ++i)
    s.knots.push_back(u[i]);
  s.knots.insert(s.knots.end(), 4, 1.0);

  std::vector<double> a(m * m, 0.0);
  std::vector<Vec3> rhs(m, Vec3{});
  std::vector<std::vector<double>> ders;
  const auto fillRow = [&](std::size_t row, double at, int order) {
    const int span = FindSpan(static_cast<int>(m), 3, at, s.knots);
    BasisDerivs(span, at, 3, 2, s.knots, &ders);
    for (int j = 0; j <= 3; ++j)
      a[row * m + static_cast<std::size_t>(span - 3 + j)] = ders[static_cast<std::size_t>(order)][static_cast<std::size_t>(j)];
  };
  for (std::size_t k = 0; k < n; ++k) {
    fillRow(k, u[k], 0);
    rhs[k] = pts[k];
  }
  fillRow(n, 0.0, 2);
  fillRow(n + 1, 1.0, 2);
  if (!Solve(a, rhs, m, &s.ctrl))
    return fail("those points cannot be joined into a smooth curve");
  s.periodic = false;
  s.weights.clear();
  *out = std::move(s);
  return true;
}

bool MakeControl(const std::vector<Vec3>& ctrl, bool closed, Spline* out, std::string* why) {
  const auto fail = [&](const char* msg) {
    if (why)
      *why = msg;
    return false;
  };
  const std::size_t n = ctrl.size();
  if (n < (closed ? 3u : 4u))
    return fail(closed ? "a closed spline needs at least three control vertices"
                       : "an open control-vertex spline needs at least four points for degree 3");
  for (const Vec3& v : ctrl)
    if (!Finite(v))
      return fail("a control vertex is not a finite number");
  Spline s;
  if (closed) {
    ExpandPeriodic(ctrl, &s);
    *out = std::move(s);
    return true;
  }
  s.degree = 3;
  s.periodic = false;
  s.ctrl = ctrl;
  // Clamped uniform knots: p + 1 zeros, 1 .. n-p-1, p + 1 copies of n - p.
  const int p = s.degree;
  s.knots.assign(static_cast<std::size_t>(p) + 1, 0.0);
  for (int i = 1; i <= static_cast<int>(n) - p - 1; ++i)
    s.knots.push_back(static_cast<double>(i));
  s.knots.insert(s.knots.end(), static_cast<std::size_t>(p) + 1, static_cast<double>(static_cast<int>(n) - p));
  *out = std::move(s);
  return true;
}

bool Rebuild(Spline* s, std::string* why) {
  if (!s->isFit())
    return true;
  Spline fresh;
  if (!MakeFit(s->fit, s->periodic, &fresh, why))
    return false;
  *s = std::move(fresh);
  return true;
}

std::string Validate(const Spline& s) {
  if (s.degree < 1)
    return "the degree is below 1";
  if (s.ctrl.size() < static_cast<std::size_t>(s.degree) + 1)
    return "there are too few control points for the degree";
  if (s.knots.size() != s.ctrl.size() + static_cast<std::size_t>(s.degree) + 1)
    return "the knot vector has the wrong length";
  for (std::size_t i = 0; i < s.knots.size(); ++i) {
    if (!std::isfinite(s.knots[i]))
      return "a knot is not a finite number";
    if (i > 0 && s.knots[i] < s.knots[i - 1])
      return "the knots are not in order";
  }
  for (const Vec3& v : s.ctrl)
    if (!Finite(v))
      return "a control point is not a finite number";
  for (const Vec3& v : s.fit)
    if (!Finite(v))
      return "a fit point is not a finite number";
  if (!s.weights.empty()) {
    if (s.weights.size() != s.ctrl.size())
      return "there is not one weight per control point";
    for (double w : s.weights)
      if (!(w > 0.0) || !std::isfinite(w))
        return "a weight is not positive";
  }
  if (!(s.knots[s.ctrl.size()] > s.knots[static_cast<std::size_t>(s.degree)]))
    return "the curve has no parameter range";
  if (s.periodic && s.ctrl.size() < static_cast<std::size_t>(s.degree) + 3)
    return "a closed spline needs at least three distinct control points";
  return {};
}

double DomainMin(const Spline& s) { return s.knots[static_cast<std::size_t>(s.degree)]; }
double DomainMax(const Spline& s) { return s.knots[s.ctrl.size()]; }

Vec3 Evaluate(const Spline& s, double t) {
  t = std::clamp(t, DomainMin(s), DomainMax(s));
  const int p = s.degree;
  const int span = FindSpan(static_cast<int>(s.ctrl.size()), p, t, s.knots);
  std::vector<double> N(static_cast<std::size_t>(p) + 1), left(static_cast<std::size_t>(p) + 1), right(static_cast<std::size_t>(p) + 1);
  N[0] = 1.0;
  for (int j = 1; j <= p; ++j) {
    left[static_cast<std::size_t>(j)] = t - s.knots[static_cast<std::size_t>(span + 1 - j)];
    right[static_cast<std::size_t>(j)] = s.knots[static_cast<std::size_t>(span) + static_cast<std::size_t>(j)] - t;
    double saved = 0.0;
    for (int r = 0; r < j; ++r) {
      const double temp = N[static_cast<std::size_t>(r)] / (right[static_cast<std::size_t>(r) + 1] + left[static_cast<std::size_t>(j - r)]);
      N[static_cast<std::size_t>(r)] = saved + right[static_cast<std::size_t>(r) + 1] * temp;
      saved = left[static_cast<std::size_t>(j - r)] * temp;
    }
    N[static_cast<std::size_t>(j)] = saved;
  }
  Vec3 acc{};
  double wsum = 0.0;
  for (int j = 0; j <= p; ++j) {
    const std::size_t idx = static_cast<std::size_t>(span) - static_cast<std::size_t>(p) +
                            static_cast<std::size_t>(j);
    const double w = s.weights.empty() ? 1.0 : s.weights[idx];
    const double f = N[static_cast<std::size_t>(j)] * w;
    acc = ray3d::Add(acc, ray3d::Scale(s.ctrl[idx], f));
    wsum += f;
  }
  return wsum != 0.0 ? ray3d::Scale(acc, 1.0 / wsum) : acc;
}

void Tessellate(const Spline& s, double tolerance, std::vector<Vec3>* out) {
  std::vector<double> ts;
  TessellateWithParams(s, tolerance, &ts, out);
}

double Length(const Spline& s) {
  std::vector<Vec3> pts;
  Tessellate(s, 1e-5, &pts);
  double len = 0.0;
  for (std::size_t i = 1; i < pts.size(); ++i)
    len += Dist(pts[i], pts[i - 1]);
  return len;
}

void ClosestPoint(const Spline& s, const Vec3& p, double* tOut, Vec3* pointOut, double* distOut) {
  std::vector<double> ts;
  std::vector<Vec3> pts;
  TessellateWithParams(s, 1e-3, &ts, &pts);
  if (pts.empty()) {
    if (tOut) *tOut = 0.0;
    if (pointOut) *pointOut = Vec3{};
    if (distOut) *distOut = 1e300;
    return;
  }
  std::size_t best = 0;
  double bestD = 1e300;
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const double d = Dist(pts[i], p);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  // Refine inside the two chords that meet at the nearest sample: golden-section on the true curve.
  double a = ts[best > 0 ? best - 1 : 0];
  double b = ts[best + 1 < ts.size() ? best + 1 : best];
  const double kInvPhi = 0.6180339887498949;
  double c = b - (b - a) * kInvPhi, d = a + (b - a) * kInvPhi;
  double fc = Dist(Evaluate(s, c), p), fd = Dist(Evaluate(s, d), p);
  for (int it = 0; it < 48; ++it) {
    if (fc < fd) {
      b = d; d = c; fd = fc;
      c = b - (b - a) * kInvPhi;
      fc = Dist(Evaluate(s, c), p);
    } else {
      a = c; c = d; fc = fd;
      d = a + (b - a) * kInvPhi;
      fd = Dist(Evaluate(s, d), p);
    }
  }
  double t = 0.5 * (a + b);
  Vec3 q = Evaluate(s, t);
  double dist = Dist(q, p);
  if (dist > bestD) {  // the refinement must never be worse than the sample it started from
    t = ts[best];
    q = pts[best];
    dist = bestD;
  }
  if (tOut) *tOut = t;
  if (pointOut) *pointOut = q;
  if (distOut) *distOut = dist;
}

std::size_t UniqueControlCount(const Spline& s) {
  return s.periodic ? s.ctrl.size() - static_cast<std::size_t>(s.degree) : s.ctrl.size();
}

void SetControlPoint(Spline* s, std::size_t i, const Vec3& p) {
  const std::size_t m = UniqueControlCount(*s);
  if (i >= m)
    return;
  s->ctrl[i] = p;
  if (s->periodic && i < static_cast<std::size_t>(s->degree))
    s->ctrl[i + m] = p;
}

std::size_t EditablePointCount(const Spline& s) { return s.isFit() ? s.fit.size() : UniqueControlCount(s); }

Vec3 EditablePoint(const Spline& s, std::size_t i) {
  if (i >= EditablePointCount(s))
    return Vec3{};
  return s.isFit() ? s.fit[i] : s.ctrl[i];
}

bool SetEditablePoint(Spline* s, std::size_t i, const Vec3& p, std::string* why) {
  if (i >= EditablePointCount(*s)) {
    if (why)
      *why = "there is no such point";
    return false;
  }
  if (s->isFit()) {
    Spline t = *s;
    t.fit[i] = p;
    if (!Rebuild(&t, why))
      return false;
    *s = std::move(t);
    return true;
  }
  SetControlPoint(s, i, p);
  return true;
}

void MapPoints(Spline* s, const std::function<Vec3(const Vec3&)>& f) {
  for (Vec3& v : s->ctrl)
    v = f(v);
  for (Vec3& v : s->fit)
    v = f(v);
}

}  // namespace spline
