#include "PdfAlign.hpp"

#include <algorithm>
#include <cmath>

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

void TintImage(const pdfview::Bitmap& base, const pdfview::Bitmap& rev, pdfview::Bitmap& out) {
  out.w = base.w;
  out.h = base.h;
  out.bgra.assign(base.bgra.size(), 255);
  const size_t n = std::min(base.bgra.size(), rev.bgra.size()) / 4u;
  for (size_t i = 0; i < n; ++i)
    TintBgra(Classify(IsInk(&base.bgra[i * 4]), IsInk(&rev.bgra[i * 4])), &out.bgra[i * 4]);
}

} // namespace pdfalign
