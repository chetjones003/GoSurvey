// REQ-392: lining two revisions of a sheet up by one or two matching points, and telling what is drawn on the
// base, the revision, or both (the Tint mode).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pdf/PdfAlign.hpp"

#include <cmath>

using namespace pdfalign;
using Catch::Approx;

namespace {
pdfview::Bitmap White(int w, int h) {
  pdfview::Bitmap b;
  b.w = w;
  b.h = h;
  b.bgra.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 255);
  return b;
}
void Black(pdfview::Bitmap& b, int x, int y) {
  for (int c = 0; c < 3; ++c)
    b.bgra[(static_cast<size_t>(y) * static_cast<size_t>(b.w) + static_cast<size_t>(x)) * 4u + static_cast<size_t>(c)] = 0;
}
bool IsBlack(const pdfview::Bitmap& b, int x, int y) { return b.bgra[(static_cast<size_t>(y) * static_cast<size_t>(b.w) + static_cast<size_t>(x)) * 4u] < 50; }
} // namespace

TEST_CASE("one matching point shifts the revision exactly onto the base point", "[issue732][req392]") {
  const Transform t = FromOnePoint({100, 50}, {130, 20});
  const Pt p = t.Apply({100, 50});
  CHECK(p.x == Approx(130));
  CHECK(p.y == Approx(20));
  CHECK(t.Scale() == Approx(1.0));
  CHECK(t.RotationDeg() == Approx(0.0).margin(1e-12));
  CHECK(t.Apply({0, 0}).x == Approx(30));
}

TEST_CASE("two matching points give the shift, scale and rotation that carry both onto the base", "[issue732][req392]") {
  // A known transform: scale 1.5, rotation 2 degrees, shift (40, -12).
  const double s = 1.5, th = 2.0 * 3.14159265358979323846 / 180.0;
  Transform truth;
  truth.a = s * std::cos(th);
  truth.b = s * std::sin(th);
  truth.tx = 40;
  truth.ty = -12;
  const Pt r1{100, 100}, r2{500, 300};
  Transform got;
  std::string why;
  REQUIRE(FromTwoPoints(r1, r2, truth.Apply(r1), truth.Apply(r2), got, why));
  CHECK(got.Scale() == Approx(1.5).epsilon(1e-9));
  CHECK(got.RotationDeg() == Approx(2.0).epsilon(1e-9));
  for (const Pt q : {Pt{0, 0}, Pt{612, 792}, Pt{-30, 400}}) {
    CHECK(got.Apply(q).x == Approx(truth.Apply(q).x).margin(1e-6));
    CHECK(got.Apply(q).y == Approx(truth.Apply(q).y).margin(1e-6));
  }
}

TEST_CASE("two matching points that are one point are refused with a reason", "[issue732][req392]") {
  Transform t;
  std::string why;
  CHECK_FALSE(FromTwoPoints({10, 10}, {10, 10}, {0, 0}, {5, 5}, t, why));
  CHECK_FALSE(why.empty());
  why.clear();
  CHECK_FALSE(FromTwoPoints({10, 10}, {20, 20}, {3, 3}, {3, 3}, t, why));
  CHECK_FALSE(why.empty());
}

TEST_CASE("a transform followed by its inverse is the identity", "[issue732][req392]") {
  Transform t;
  t.a = 0.9;
  t.b = 0.2;
  t.tx = 12;
  t.ty = -7;
  const Pt p = t.Inverse().Apply(t.Apply({33, 44}));
  CHECK(p.x == Approx(33).margin(1e-9));
  CHECK(p.y == Approx(44).margin(1e-9));
}

TEST_CASE("Tint classes: base only, revision only, both, neither", "[issue732][req392]") {
  CHECK(Classify(true, false) == Ink::BaseOnly);
  CHECK(Classify(false, true) == Ink::RevOnly);
  CHECK(Classify(true, true) == Ink::Both);
  CHECK(Classify(false, false) == Ink::None);

  // A hand-built 2x2 pair: base ink at (0,0) and (1,0); revision ink at (1,0) and (0,1).
  pdfview::Bitmap base = White(2, 2), rev = White(2, 2);
  Black(base, 0, 0);
  Black(base, 1, 0);
  Black(rev, 1, 0);
  Black(rev, 0, 1);
  pdfview::Bitmap out;
  TintImage(base, rev, out);
  const auto cls = [&](int x, int y) {
    uint8_t want[4];
    for (Ink k : {Ink::None, Ink::BaseOnly, Ink::RevOnly, Ink::Both}) {
      TintBgra(k, want);
      if (std::equal(want, want + 4, &out.bgra[(static_cast<size_t>(y) * 2u + static_cast<size_t>(x)) * 4u]))
        return k;
    }
    return Ink::None;
  };
  CHECK(cls(0, 0) == Ink::BaseOnly);
  CHECK(cls(1, 0) == Ink::Both);
  CHECK(cls(0, 1) == Ink::RevOnly);
  CHECK(cls(1, 1) == Ink::None);
  // The three inked classes must look different from one another and from the page.
  uint8_t a[4], b[4], c[4], d[4];
  TintBgra(Ink::BaseOnly, a);
  TintBgra(Ink::RevOnly, b);
  TintBgra(Ink::Both, c);
  TintBgra(Ink::None, d);
  CHECK(a[2] > a[0]); // base is red
  CHECK(b[0] > b[2]); // revision is blue
  CHECK(c[0] == c[2]); // both is grey
  CHECK(d[0] == 255);
}

TEST_CASE("the Base and Revision views mark only the ink the other sheet lacks", "[issue732][req392]") {
  pdfview::Bitmap base = White(12, 12), rev = White(12, 12);
  Black(base, 2, 2); // in both
  Black(rev, 2, 2);
  Black(base, 8, 8); // only in the base
  Black(rev, 3, 8);  // only in the revision (2+ px from anything in the base)
  Black(rev, 3, 3);  // within a pixel of base ink: the same mark, not a change
  const uint8_t blue[3] = {235, 120, 40};
  pdfview::Bitmap out;
  MarkOnlyIn(base, rev, blue, out);
  const auto alpha = [&](int x, int y) { return out.bgra[(static_cast<size_t>(y) * 12u + static_cast<size_t>(x)) * 4u + 3u]; };
  CHECK(alpha(8, 8) == 255);
  CHECK(out.bgra[(8u * 12u + 8u) * 4u] == 235); // blue channel of the mark
  CHECK(alpha(2, 2) == 0);
  CHECK(alpha(0, 11) == 0);
  MarkOnlyIn(rev, base, blue, out);
  CHECK(alpha(3, 8) == 255);
  CHECK(alpha(3, 3) == 0);
  CHECK(alpha(2, 2) == 0);
}

TEST_CASE("a shifted revision is drawn where the transform says", "[issue732][req392]") {
  // Revision page 10 x 10 pt at 1 px/pt with ink at pixel (2, 7), which is the point (2.5, 2.5) in page space.
  pdfview::Bitmap rev = White(10, 10);
  Black(rev, 2, 7);
  const Transform shift = FromOnePoint({2.5, 2.5}, {6.5, 4.5}); // carry it to (6.5, 4.5): pixel (6, 5)
  pdfview::Bitmap out;
  ResampleAligned(rev, 10.f, 1.f, shift, 10, 10, 1.f, 10.f, out);
  REQUIRE(out.w == 10);
  CHECK(IsBlack(out, 6, 5));
  CHECK_FALSE(IsBlack(out, 2, 7));

  // Off the revision's page it is white, not stale ink.
  const Transform away = FromOnePoint({0, 0}, {50, 50});
  ResampleAligned(rev, 10.f, 1.f, away, 10, 10, 1.f, 10.f, out);
  for (int y = 0; y < 10; ++y)
    for (int x = 0; x < 10; ++x)
      CHECK_FALSE(IsBlack(out, x, y));
}
