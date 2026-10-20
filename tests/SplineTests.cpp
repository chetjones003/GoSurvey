// REQ-440 (GitHub issue #803) - the SPLINE mathematics: fit and control-vertex construction, evaluation,
// tessellation within REQ-101's tolerance, closest point, and the affine maps MOVE/ROTATE/SCALE/MIRROR
// apply. Pure util/spline - no window, no GPU. The command-line and file halves live in
// tests/headless/transcripts/req440-spline.txt and the DXF/DWG tests.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "util/spline.hpp"

using Catch::Approx;
using spline::Spline;
using spline::Vec3;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kReq101 = 0.002;  // REQ-101: +-0.002 ft

double Dist(const Vec3& a, const Vec3& b) { return ray3d::Length(ray3d::Sub(a, b)); }

double DistToSegment(const Vec3& q, const Vec3& a, const Vec3& b) {
  const Vec3 ab = ray3d::Sub(b, a);
  const double len2 = ray3d::Dot(ab, ab);
  double t = len2 > 0.0 ? ray3d::Dot(ray3d::Sub(q, a), ab) / len2 : 0.0;
  t = std::clamp(t, 0.0, 1.0);
  return Dist(q, ray3d::Add(a, ray3d::Scale(ab, t)));
}

double DistToPolyline(const Vec3& q, const std::vector<Vec3>& pl) {
  double best = 1e300;
  for (std::size_t i = 0; i + 1 < pl.size(); ++i)
    best = std::min(best, DistToSegment(q, pl[i], pl[i + 1]));
  return best;
}

// A curve through these has real bends and a little height, so a flat or a straight result shows.
const std::vector<Vec3> kFit4 = {{0, 0, 0}, {3, 4, 1}, {7, 3, 2}, {10, 8, 0}};

}  // namespace

TEST_CASE("an open fit spline passes through every point it was given", "[spline][req440]") {
  Spline s;
  std::string why;
  REQUIRE(spline::MakeFit(kFit4, false, &s, &why));
  CHECK(s.degree == 3);
  CHECK_FALSE(s.periodic);
  CHECK(s.isFit());
  CHECK(spline::Validate(s).empty());
  for (const Vec3& p : kFit4) {
    double t = 0, d = 0;
    Vec3 q;
    spline::ClosestPoint(s, p, &t, &q, &d);
    CHECK(d < 1e-7);
  }
  // The curve starts and ends exactly on the first and last point.
  CHECK(Dist(spline::Evaluate(s, spline::DomainMin(s)), kFit4.front()) < 1e-9);
  CHECK(Dist(spline::Evaluate(s, spline::DomainMax(s)), kFit4.back()) < 1e-9);
}

TEST_CASE("an open fit spline is a natural cubic: no curvature at either end", "[spline][req440]") {
  Spline s;
  REQUIRE(spline::MakeFit(kFit4, false, &s, nullptr));
  const double lo = spline::DomainMin(s), hi = spline::DomainMax(s);
  const double h = 1e-3 * (hi - lo);
  // The second difference over step k estimates S'' at distance k from the end, so 2 D(h) - D(2h)
  // (Richardson) cancels the first-order term and leaves S'' AT the end.
  const auto second = [&](double at, double dir) {
    const auto d = [&](double k) {
      const Vec3 a = spline::Evaluate(s, at), b = spline::Evaluate(s, at + dir * k), c = spline::Evaluate(s, at + dir * 2 * k);
      return ray3d::Scale(ray3d::Sub(ray3d::Add(a, c), ray3d::Scale(b, 2.0)), 1.0 / (k * k));
    };
    return ray3d::Length(ray3d::Sub(ray3d::Scale(d(h), 2.0), d(2 * h)));
  };
  // The curvature in the middle is of order 10 here.
  CHECK(second(lo, +1) < 1e-3);
  CHECK(second(hi, -1) < 1e-3);
  // And it is not just a straight line: the middle bends.
  const Vec3 m0 = spline::Evaluate(s, 0.5 * (lo + hi)), m1 = spline::Evaluate(s, 0.5 * (lo + hi) + h), m2 = spline::Evaluate(s, 0.5 * (lo + hi) + 2 * h);
  CHECK(ray3d::Length(ray3d::Sub(ray3d::Add(m0, m2), ray3d::Scale(m1, 2.0))) / (h * h) > 1.0);
}

TEST_CASE("two fit points make a straight line", "[spline][req440]") {
  Spline s;
  REQUIRE(spline::MakeFit({{0, 0, 0}, {10, 5, 0}}, false, &s, nullptr));
  std::vector<Vec3> pl;
  spline::Tessellate(s, 1e-4, &pl);
  for (const Vec3& p : pl)
    CHECK(DistToSegment(p, {0, 0, 0}, {10, 5, 0}) < 1e-9);
  CHECK(spline::Length(s) == Approx(std::sqrt(125.0)).margin(1e-6));
}

TEST_CASE("a closed fit spline passes through its points and is smooth at the join", "[spline][req440]") {
  const std::vector<Vec3> pts = {{0, 0, 0}, {6, -1, 0}, {8, 5, 1}, {3, 8, 1}, {-2, 4, 0}};
  Spline s;
  REQUIRE(spline::MakeFit(pts, true, &s, nullptr));
  CHECK(s.periodic);
  CHECK(spline::Validate(s).empty());
  for (const Vec3& p : pts) {
    double t = 0, d = 0;
    Vec3 q;
    spline::ClosestPoint(s, p, &t, &q, &d);
    CHECK(d < 1e-7);
  }
  std::vector<Vec3> pl;
  spline::Tessellate(s, 1e-3, &pl);
  REQUIRE(pl.size() > 8);
  CHECK(Dist(pl.front(), pl.back()) == 0.0);  // one point, not two that merely agree
  // Tangent continuity across the join: the step leaving the start and the step arriving at the end
  // point the same way.
  const double lo = spline::DomainMin(s), hi = spline::DomainMax(s), h = 1e-4;
  const Vec3 out = ray3d::Normalize(ray3d::Sub(spline::Evaluate(s, lo + h), spline::Evaluate(s, lo)));
  const Vec3 in = ray3d::Normalize(ray3d::Sub(spline::Evaluate(s, hi), spline::Evaluate(s, hi - h)));
  CHECK(ray3d::Dot(out, in) > 0.9999);
}

TEST_CASE("a control-vertex spline is pulled toward its interior vertices but does not touch them", "[spline][req440]") {
  const std::vector<Vec3> cv = {{0, 0, 0}, {2, 8, 0}, {8, 8, 0}, {10, 0, 0}};
  Spline s;
  REQUIRE(spline::MakeControl(cv, false, &s, nullptr));
  CHECK(s.degree == 3);
  CHECK_FALSE(s.isFit());
  CHECK(Dist(spline::Evaluate(s, spline::DomainMin(s)), cv.front()) < 1e-9);
  CHECK(Dist(spline::Evaluate(s, spline::DomainMax(s)), cv.back()) < 1e-9);
  for (std::size_t i : {1u, 2u}) {
    double t = 0, d = 0;
    Vec3 q;
    spline::ClosestPoint(s, cv[i], &t, &q, &d);
    CHECK(d > 1.0);  // well away from the vertex it is pulled toward
  }
}

TEST_CASE("fewer than four control vertices lower the degree rather than refuse", "[spline][req440]") {
  Spline three, two;
  REQUIRE(spline::MakeControl({{0, 0, 0}, {5, 5, 0}, {10, 0, 0}}, false, &three, nullptr));
  REQUIRE(spline::MakeControl({{0, 0, 0}, {10, 0, 0}}, false, &two, nullptr));
  CHECK(three.degree == 2);
  CHECK(two.degree == 1);
  CHECK(spline::Length(two) == Approx(10.0));
}

TEST_CASE("a closed control-vertex spline is periodic and closes on itself", "[spline][req440]") {
  const std::vector<Vec3> cv = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
  Spline s;
  REQUIRE(spline::MakeControl(cv, true, &s, nullptr));
  CHECK(s.periodic);
  CHECK(spline::UniqueControlCount(s) == 4);
  CHECK(s.ctrl.size() == 4u + 3u);
  std::vector<Vec3> pl;
  spline::Tessellate(s, 1e-3, &pl);
  CHECK(Dist(pl.front(), pl.back()) == 0.0);
  // It rounds the corners instead of reaching them.
  double t = 0, d = 0;
  Vec3 q;
  spline::ClosestPoint(s, cv[0], &t, &q, &d);
  CHECK(d > 0.5);
}

TEST_CASE("editing a fit point re-derives the curve through the new point", "[spline][req440]") {
  Spline s;
  REQUIRE(spline::MakeFit(kFit4, false, &s, nullptr));
  s.fit[1] = {3, 9, 1};
  std::string why;
  REQUIRE(spline::Rebuild(&s, &why));
  double t = 0, d = 0;
  Vec3 q;
  spline::ClosestPoint(s, s.fit[1], &t, &q, &d);
  CHECK(d < 1e-7);
  // Dragging a point onto its neighbour cannot define a curve and says so, leaving the spline usable.
  Spline bad = s;
  bad.fit[2] = bad.fit[1];
  CHECK_FALSE(spline::Rebuild(&bad, &why));
  CHECK(why.find("coincide") != std::string::npos);
}

TEST_CASE("the tessellation never strays from the curve by more than REQ-101's tolerance", "[spline][req440]") {
  Spline fit;
  REQUIRE(spline::MakeFit(kFit4, false, &fit, nullptr));
  Spline cv;
  REQUIRE(spline::MakeControl({{0, 0, 0}, {1, 30, 0}, {40, -20, 5}, {80, 25, 0}, {100, 0, 0}}, false, &cv, nullptr));
  for (const Spline* s : {&fit, &cv}) {
    std::vector<Vec3> pl;
    spline::Tessellate(*s, kReq101 / 2, &pl);
    REQUIRE(pl.size() > 4);
    const double lo = spline::DomainMin(*s), hi = spline::DomainMax(*s);
    double worst = 0.0;
    for (int i = 0; i <= 4000; ++i)
      worst = std::max(worst, DistToPolyline(spline::Evaluate(*s, lo + (hi - lo) * i / 4000.0), pl));
    CHECK(worst <= kReq101);
  }
}

TEST_CASE("a rational spline evaluates its weights: a quadratic arc is a true quarter circle", "[spline][req440]") {
  Spline s;
  s.degree = 2;
  s.ctrl = {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  s.weights = {1.0, std::sqrt(0.5), 1.0};
  s.knots = {0, 0, 0, 1, 1, 1};
  REQUIRE(spline::Validate(s).empty());
  for (int i = 0; i <= 20; ++i) {
    const Vec3 p = spline::Evaluate(s, i / 20.0);
    CHECK(std::hypot(p.x, p.y) == Approx(1.0).margin(1e-12));
  }
  CHECK(spline::Length(s) == Approx(kPi / 2).margin(2e-5));
}

TEST_CASE("move, rotate, scale and mirror move the curve exactly as they move its points", "[spline][req440]") {
  Spline s;
  REQUIRE(spline::MakeFit(kFit4, false, &s, nullptr));
  const double len = spline::Length(s);
  const double t0 = 0.37 * (spline::DomainMax(s) - spline::DomainMin(s)) + spline::DomainMin(s);
  const Vec3 before = spline::Evaluate(s, t0);

  Spline moved = s;
  spline::MapPoints(&moved, [](const Vec3& v) { return Vec3{v.x + 5, v.y - 2, v.z + 1}; });
  const Vec3 m = spline::Evaluate(moved, t0);
  CHECK(Dist(m, {before.x + 5, before.y - 2, before.z + 1}) < 1e-12);

  Spline turned = s;
  const double c = std::cos(0.6), sn = std::sin(0.6);
  spline::MapPoints(&turned, [&](const Vec3& v) { return Vec3{c * v.x - sn * v.y, sn * v.x + c * v.y, v.z}; });
  CHECK(spline::Length(turned) == Approx(len).margin(1e-4));

  Spline grown = s;
  spline::MapPoints(&grown, [](const Vec3& v) { return Vec3{2 * v.x, 2 * v.y, 2 * v.z}; });
  CHECK(spline::Length(grown) == Approx(2 * len).margin(1e-4));

  Spline flipped = s;
  spline::MapPoints(&flipped, [](const Vec3& v) { return Vec3{-v.x, v.y, v.z}; });
  CHECK(spline::Length(flipped) == Approx(len).margin(1e-4));
  // A fit spline keeps its fit points in step, so editing it afterwards still works.
  REQUIRE(spline::Rebuild(&flipped, nullptr));
  CHECK(Dist(spline::Evaluate(flipped, t0), {-before.x, before.y, before.z}) < 1e-9);
}

TEST_CASE("the closest point on a straight spline is its projection", "[spline][req440]") {
  Spline s;
  REQUIRE(spline::MakeControl({{0, 0, 0}, {10, 0, 0}}, false, &s, nullptr));
  double t = 0, d = 0;
  Vec3 q;
  spline::ClosestPoint(s, {4, 3, 0}, &t, &q, &d);
  CHECK(q.x == Approx(4.0).margin(1e-6));
  CHECK(d == Approx(3.0).margin(1e-6));
  spline::ClosestPoint(s, {-5, 1, 0}, &t, &q, &d);  // beyond the end: clamps to it
  CHECK(q.x == Approx(0.0).margin(1e-6));
}

TEST_CASE("dragging one control point of a closed spline keeps its repeated tail in step", "[spline][req440]") {
  Spline s;
  REQUIRE(spline::MakeControl({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}}, true, &s, nullptr));
  const std::size_t m = spline::UniqueControlCount(s);
  spline::SetControlPoint(&s, 0, {-3, -3, 0});
  CHECK(s.ctrl[0].x == -3.0);
  CHECK(s.ctrl[m].x == -3.0);  // the tail copy moved too
  CHECK(spline::Validate(s).empty());
  spline::SetControlPoint(&s, m, {99, 99, 99});  // not a distinct point: ignored
  CHECK(s.ctrl[m].x == -3.0);
}

TEST_CASE("a spline that cannot be a curve is refused with a reason", "[spline][req440]") {
  Spline s;
  std::string why;
  CHECK_FALSE(spline::MakeFit({{0, 0, 0}}, false, &s, &why));
  CHECK(why.find("two") != std::string::npos);
  CHECK_FALSE(spline::MakeFit({{0, 0, 0}, {1, 0, 0}}, true, &s, &why));
  CHECK(why.find("three") != std::string::npos);
  CHECK_FALSE(spline::MakeFit({{0, 0, 0}, {0, 0, 0}, {5, 5, 0}}, false, &s, &why));
  CHECK(why.find("coincide") != std::string::npos);
  CHECK_FALSE(spline::MakeFit({{0, 0, 0}, {std::nan(""), 0, 0}}, false, &s, &why));

  Spline ok;
  REQUIRE(spline::MakeControl({{0, 0, 0}, {5, 5, 0}, {10, 0, 0}, {15, 5, 0}}, false, &ok, nullptr));
  Spline bad = ok;
  bad.knots.pop_back();
  CHECK(spline::Validate(bad).find("knot vector") != std::string::npos);
  bad = ok;
  std::swap(bad.knots[4], bad.knots[3]);
  bad.knots[3] = 9;
  CHECK(spline::Validate(bad).find("order") != std::string::npos);
  bad = ok;
  bad.weights = {1, 1, -1, 1};
  CHECK(spline::Validate(bad).find("weight") != std::string::npos);
  bad = ok;
  bad.weights = {1, 1};
  CHECK(spline::Validate(bad).find("one weight") != std::string::npos);
  bad = ok;
  bad.ctrl[1].x = std::nan("");
  CHECK_FALSE(spline::Validate(bad).empty());
  // A spline that is not valid tessellates to nothing rather than crashing.
  std::vector<Vec3> pl{{1, 1, 1}};
  spline::Tessellate(bad, 1e-3, &pl);
  CHECK(pl.empty());
}
