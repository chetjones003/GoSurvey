#include "PdfAlign.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pdfalign {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kInkBelow = 170; // luminance under this is ink
} // namespace

double Transform::Scale() const { return std::hypot(a, b); }
double Transform::RotationDeg() const { return std::atan2(b, a) * 180.0 / kPi; }

Transform Transform::Inverse() const {
  const double d = a * a + b * b;
  Transform t;
  t.a = a / d;
  t.b = -b / d;
  t.tx = -(t.a * tx - t.b * ty);
  t.ty = -(t.b * tx + t.a * ty);
  return t;
}

Transform FromOnePoint(Pt rev, Pt base) {
  Transform t;
  t.tx = base.x - rev.x;
  t.ty = base.y - rev.y;
  return t;
}

bool FromTwoPoints(Pt rev1, Pt rev2, Pt base1, Pt base2, Transform& out, std::string& why) {
  const double rx = rev2.x - rev1.x, ry = rev2.y - rev1.y, bx = base2.x - base1.x, by = base2.y - base1.y;
  const double rr = rx * rx + ry * ry, bb = bx * bx + by * by;
  if (rr < 1e-9) {
    why = "the two points picked on the revision are the same point";
    return false;
  }
  if (bb < 1e-9) {
    why = "the two points picked on the base are the same point";
    return false;
  }
  // The complex ratio (base2 - base1) / (rev2 - rev1) is the scale and rotation together.
  Transform t;
  t.a = (bx * rx + by * ry) / rr;
  t.b = (by * rx - bx * ry) / rr;
  t.tx = base1.x - (t.a * rev1.x - t.b * rev1.y);
  t.ty = base1.y - (t.b * rev1.x + t.a * rev1.y);
  out = t;
  return true;
}

bool IsInk(const uint8_t* p) { return (p[0] + p[1] * 2 + p[2]) / 4 < kInkBelow; }

Ink Classify(bool baseInk, bool revInk) {
  if (baseInk && revInk)
    return Ink::Both;
  return baseInk ? Ink::BaseOnly : revInk ? Ink::RevOnly : Ink::None;
}

void TintBgra(Ink k, uint8_t o[4]) {
  // B, G, R, A
  switch (k) {
  case Ink::BaseOnly: o[0] = 40; o[1] = 40; o[2] = 220; break;
  case Ink::RevOnly: o[0] = 230; o[1] = 90; o[2] = 30; break;
  case Ink::Both: o[0] = o[1] = o[2] = 70; break;
  default: o[0] = o[1] = o[2] = 255; break;
  }
  o[3] = 255;
}

void ResampleAligned(const pdfview::Bitmap& rev, float revHPt, float revPxPerPt, const Transform& revToBase, int outW, int outH,
                     float basePxPerPt, float baseHPt, pdfview::Bitmap& out) {
  out.w = outW;
  out.h = outH;
  out.bgra.assign(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4u, 255);
  if (rev.w <= 0 || rev.h <= 0 || revToBase.Scale() < 1e-9)
    return;
  const Transform inv = revToBase.Inverse();
  for (int oy = 0; oy < outH; ++oy) {
    uint8_t* row = &out.bgra[static_cast<size_t>(oy) * static_cast<size_t>(outW) * 4u];
    for (int ox = 0; ox < outW; ++ox) {
      const Pt basePt{(ox + 0.5) / basePxPerPt, baseHPt - (oy + 0.5) / basePxPerPt};
      const Pt rp = inv.Apply(basePt);
      const double fx = rp.x * revPxPerPt - 0.5, fy = (revHPt - rp.y) * revPxPerPt - 0.5;
      const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
      if (x0 < -1 || y0 < -1 || x0 >= rev.w || y0 >= rev.h)
        continue; // off the revision's page: white
      const double tx = fx - x0, ty = fy - y0;
      for (int c = 0; c < 3; ++c) {
        const auto at = [&](int x, int y) -> double {
          if (x < 0 || y < 0 || x >= rev.w || y >= rev.h)
            return 255.0;
          return rev.bgra[(static_cast<size_t>(y) * static_cast<size_t>(rev.w) + static_cast<size_t>(x)) * 4u + static_cast<size_t>(c)];
        };
        const double v = (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
        row[ox * 4 + c] = static_cast<uint8_t>(std::clamp(v + 0.5, 0.0, 255.0));
      }
    }
  }
}

void MarkOnlyIn(const pdfview::Bitmap& sheet, const pdfview::Bitmap& other, const uint8_t bgr[3], pdfview::Bitmap& out) {
  const int w = sheet.w, h = sheet.h;
  out.w = w;
  out.h = h;
  out.bgra.assign(sheet.bgra.size(), 0);
  if (other.w != w || other.h != h)
    return;
  // The other sheet "has" a mark near here when anything on it is even faintly dark within two pixels: a mark drawn onto
  // the base's pixel grid through the alignment is blurred a little, and a faint grey is still the same mark.
  const auto faintAt = [](const pdfview::Bitmap& b, int x, int y) {
    if (x < 0 || y < 0 || x >= b.w || y >= b.h)
      return false;
    const uint8_t* p = &b.bgra[(static_cast<size_t>(y) * static_cast<size_t>(b.w) + static_cast<size_t>(x)) * 4u];
    return (p[0] + p[1] * 2 + p[2]) / 4 < 225;
  };
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t at = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
      if (!IsInk(&sheet.bgra[at]))
        continue;
      bool near = false;
      for (int dy = -2; dy <= 2 && !near; ++dy)
        for (int dx = -2; dx <= 2 && !near; ++dx)
          near = faintAt(other, x + dx, y + dy);
      if (near)
        continue;
      out.bgra[at] = bgr[0];
      out.bgra[at + 1] = bgr[1];
      out.bgra[at + 2] = bgr[2];
      out.bgra[at + 3] = 255;
    }
}

void TintImage(const pdfview::Bitmap& base, const pdfview::Bitmap& rev, pdfview::Bitmap& out) {
  out.w = base.w;
  out.h = base.h;
  out.bgra.assign(base.bgra.size(), 255);
  const size_t n = std::min(base.bgra.size(), rev.bgra.size()) / 4u;
  for (size_t i = 0; i < n; ++i)
    TintBgra(Classify(IsInk(&base.bgra[i * 4]), IsInk(&rev.bgra[i * 4])), &out.bgra[i * 4]);
}

namespace {

struct Ink2 {
  std::vector<float> x, y; ///< ink pixels as page points (origin lower-left)
};

/// At most ~cap ink pixels of b, as points, evenly thinned.
Ink2 InkPoints(const pdfview::Bitmap& b, double hPt, double ppp, size_t cap) {
  size_t total = 0;
  for (size_t i = 0; i + 3 < b.bgra.size(); i += 4)
    total += IsInk(&b.bgra[i]) ? 1 : 0;
  const size_t stride = std::max<size_t>(1, total / cap);
  Ink2 out;
  size_t seen = 0;
  for (int py = 0; py < b.h; ++py)
    for (int px = 0; px < b.w; ++px)
      if (IsInk(&b.bgra[(static_cast<size_t>(py) * static_cast<size_t>(b.w) + static_cast<size_t>(px)) * 4u]) && (seen++ % stride) == 0) {
        out.x.push_back(static_cast<float>((px + 0.5) / ppp));
        out.y.push_back(static_cast<float>(hPt - (py + 0.5) / ppp));
      }
  return out;
}

/// For every pixel, the index of (about) the nearest ink pixel: a two-pass chamfer that carries the source along.
std::vector<int32_t> NearestInk(const pdfview::Bitmap& b) {
  const int w = b.w, h = b.h;
  std::vector<int32_t> nn(static_cast<size_t>(w) * static_cast<size_t>(h), -1);
  for (size_t i = 0; i < nn.size(); ++i)
    if (IsInk(&b.bgra[i * 4]))
      nn[i] = static_cast<int32_t>(i);
  const auto d2 = [w](int32_t src, int x, int y) {
    const double dx = src % w - x, dy = src / w - y;
    return dx * dx + dy * dy;
  };
  const auto relax = [&](int x, int y, int ox, int oy) {
    const int nx = x + ox, ny = y + oy;
    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
      return;
    const int32_t cand = nn[static_cast<size_t>(ny) * static_cast<size_t>(w) + static_cast<size_t>(nx)];
    if (cand < 0)
      return;
    int32_t& cur = nn[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
    if (cur < 0 || d2(cand, x, y) < d2(cur, x, y))
      cur = cand;
  };
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      relax(x, y, -1, 0);
      relax(x, y, -1, -1);
      relax(x, y, 0, -1);
      relax(x, y, 1, -1);
    }
  for (int y = h - 1; y >= 0; --y)
    for (int x = w - 1; x >= 0; --x) {
      relax(x, y, 1, 0);
      relax(x, y, 1, 1);
      relax(x, y, 0, 1);
      relax(x, y, -1, 1);
    }
  return nn;
}

/// The shift (in bins) that best lines rev up with base, by correlating mean-removed, lightly smoothed profiles.
int BestShift(const std::vector<double>& base, const std::vector<double>& rev) {
  const auto prep = [](const std::vector<double>& v) {
    std::vector<double> s(v.size(), 0.0);
    for (size_t i = 0; i < v.size(); ++i) {
      double sum = 0;
      int n = 0;
      for (int k = -2; k <= 2; ++k) {
        const long j = static_cast<long>(i) + k;
        if (j >= 0 && j < static_cast<long>(v.size())) {
          sum += v[static_cast<size_t>(j)];
          ++n;
        }
      }
      s[i] = sum / n;
    }
    double mean = 0;
    for (double d : s)
      mean += d;
    mean /= static_cast<double>(std::max<size_t>(1, s.size()));
    for (double& d : s)
      d -= mean;
    return s;
  };
  const std::vector<double> b = prep(base), r = prep(rev);
  const long nb = static_cast<long>(b.size()), nr = static_cast<long>(r.size());
  double best = -1e300;
  long bestShift = 0;
  for (long shift = -nr + 1; shift < nb; ++shift) {
    double sum = 0;
    const long lo = std::max(0L, -shift), hi = std::min(nr, nb - shift);
    for (long i = lo; i < hi; ++i)
      sum += b[static_cast<size_t>(i + shift)] * r[static_cast<size_t>(i)];
    if (hi > lo)
      sum /= std::sqrt(static_cast<double>(hi - lo)); // a longer overlap is not a better match by length alone
    if (sum > best) {
      best = sum;
      bestShift = shift;
    }
  }
  return static_cast<int>(bestShift);
}

struct Fit {
  Transform xf;
  double confidence = 0.0;
};

/// Iterative closest point from start: pair each revision ink point with the nearest base ink point inside a gate
/// that narrows each round, fit shift + scale + rotation to the pairs, repeat.
Fit Refine(const Ink2& pts, const std::vector<int32_t>& nn, const pdfview::Bitmap& base, double baseHPt, double ppp, Transform start,
           const std::function<bool()>& cancel) {
  const double kMaxRot = 5.5 * kPi / 180.0;
  const auto nearestFor = [&](const Transform& t, size_t i, double& nx, double& ny, double& dist) {
    const Pt q = t.Apply({pts.x[i], pts.y[i]});
    const int px = static_cast<int>(std::floor(q.x * ppp)), py = static_cast<int>(std::floor((baseHPt - q.y) * ppp));
    if (px < 0 || py < 0 || px >= base.w || py >= base.h)
      return false;
    const int32_t s = nn[static_cast<size_t>(py) * static_cast<size_t>(base.w) + static_cast<size_t>(px)];
    if (s < 0)
      return false;
    nx = ((s % base.w) + 0.5) / ppp;
    ny = baseHPt - ((s / base.w) + 0.5) / ppp;
    dist = std::hypot(q.x - nx, q.y - ny);
    return true;
  };
  Transform t = start;
  double gate = std::max(30.0, 0.02 * std::max(base.w, base.h) / ppp);
  const double finalGate = std::max(2.0, 2.0 / ppp);
  for (int it = 0; it < 60; ++it) {
    if (cancel && cancel())
      break;
    gate = std::max(finalGate, gate * 0.85);
    double sx = 0, sy = 0, tx = 0, ty = 0;
    long n = 0;
    std::vector<std::pair<size_t, std::pair<double, double>>> pairs;
    for (size_t i = 0; i < pts.x.size(); ++i) {
      double nx, ny, d;
      if (!nearestFor(t, i, nx, ny, d) || d > gate)
        continue;
      pairs.push_back({i, {nx, ny}});
      sx += pts.x[i];
      sy += pts.y[i];
      tx += nx;
      ty += ny;
      ++n;
    }
    if (n < 20)
      break;
    const double pcx = sx / static_cast<double>(n), pcy = sy / static_cast<double>(n), ncx = tx / static_cast<double>(n),
                 ncy = ty / static_cast<double>(n);
    double re = 0, im = 0, den = 0; // sum conj(p - pc) * (n - nc), and |p - pc|^2
    for (const auto& pr : pairs) {
      const double ax = pts.x[pr.first] - pcx, ay = pts.y[pr.first] - pcy, bx = pr.second.first - ncx, by = pr.second.second - ncy;
      re += ax * bx + ay * by;
      im += ax * by - ay * bx;
      den += ax * ax + ay * ay;
    }
    if (den < 1e-9)
      break;
    Transform nt;
    nt.a = re / den;
    nt.b = im / den;
    nt.tx = ncx - (nt.a * pcx - nt.b * pcy);
    nt.ty = ncy - (nt.b * pcx + nt.a * pcy);
    if (std::fabs(std::atan2(nt.b, nt.a)) > kMaxRot || nt.Scale() < 0.5 || nt.Scale() > 2.0)
      break;
    const double move = std::fabs(nt.a - t.a) + std::fabs(nt.b - t.b) + std::fabs(nt.tx - t.tx) / 1000.0 + std::fabs(nt.ty - t.ty) / 1000.0;
    t = nt;
    if (gate <= finalGate && move < 1e-7)
      break;
  }
  long hit = 0;
  for (size_t i = 0; i < pts.x.size(); ++i) {
    double nx, ny, d;
    if (nearestFor(t, i, nx, ny, d) && d <= finalGate)
      ++hit;
  }
  Fit f;
  f.xf = t;
  f.confidence = pts.x.empty() ? 0.0 : static_cast<double>(hit) / static_cast<double>(pts.x.size());
  return f;
}

std::vector<double> Profile(const pdfview::Bitmap& b, bool columns) {
  std::vector<double> p(static_cast<size_t>(columns ? b.w : b.h), 0.0);
  for (int y = 0; y < b.h; ++y)
    for (int x = 0; x < b.w; ++x)
      if (IsInk(&b.bgra[(static_cast<size_t>(y) * static_cast<size_t>(b.w) + static_cast<size_t>(x)) * 4u]))
        p[static_cast<size_t>(columns ? x : b.h - 1 - y)] += 1.0; // rows are counted from the bottom, like page y
  return p;
}

} // namespace

AutoResult AutoAlign(const pdfview::Bitmap& base, double baseHPt, const pdfview::Bitmap& rev, double revHPt, double pxPerPt,
                     const std::function<bool()>& cancel) {
  AutoResult res;
  if (base.w <= 0 || rev.w <= 0 || pxPerPt <= 0.0)
    return res;
  const Ink2 pts = InkPoints(rev, revHPt, pxPerPt, 60000);
  if (pts.x.size() < 50)
    return res;
  const std::vector<int32_t> nn = NearestInk(base);

  // Two starting points: the page corners as they are, and the shift that lines the ink profiles up.
  Transform shifted;
  shifted.tx = BestShift(Profile(base, true), Profile(rev, true)) / pxPerPt;
  shifted.ty = BestShift(Profile(base, false), Profile(rev, false)) / pxPerPt;
  Fit best = Refine(pts, nn, base, baseHPt, pxPerPt, Transform{}, cancel);
  if (!(cancel && cancel())) {
    const Fit other = Refine(pts, nn, base, baseHPt, pxPerPt, shifted, cancel);
    if (other.confidence > best.confidence)
      best = other;
  }
  res.confidence = best.confidence;
  res.matched = best.confidence >= kLowConfidence;
  if (res.matched)
    res.xf = best.xf; // otherwise the identity: no match is claimed
  return res;
}

} // namespace pdfalign
