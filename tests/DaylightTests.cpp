// REQ-371 — side slope grading: the daylight line.
//
// Every offset asserted here is computed BY HAND in the comment above it, from the two equations
// that cross. That is deliberate: a daylight offset that is wrong by a slope factor is entirely
// plausible on screen, and comparing against another approximation of the same thing would hide a
// systematic error in both. The arithmetic is the oracle.
//
// Slopes are run:rise throughout — 3:1 is three horizontal per one vertical — matching REQ-074's
// wording, which REQ-105 was amended on 2026-09-09 to keep single across the program.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

#include "util/daylight.hpp"

using Catch::Approx;

namespace {

/// REQ-101 is +/-0.002 ft. Asserting at exactly the requirement, not at a tolerance chosen to pass.
constexpr double kReq101Ft = 0.002;

/// Ground as an exact plane over a square footprint, stated as `z = zAtRefX + slope * (x - refX)`.
///
/// The reference x is an explicit parameter rather than being folded into an intercept, because the
/// first version of this fixture folded it in against the footprint's own corner and silently put
/// the plane 200 ft above where every comment said it was. Keeping the plane exact is what lets the
/// expected answers be arithmetic; naming the point it passes through is what keeps them readable.
class PlaneGround final : public ISurfaceQuery {
public:
  PlaneGround(double x0, double y0, double size, double refX, double zAtRefX, double slopePerX)
      : x0_(x0), y0_(y0), size_(size), refX_(refX), zAtRef_(zAtRefX), slope_(slopePerX) {}

  [[nodiscard]] bool elevationAt(double x, double y, double* outZ) const override {
    if (x < x0_ || x > x0_ + size_ || y < y0_ || y > y0_ + size_)
      return false;  // off the surface — REQ-074's "outside", which must never be extrapolated
    if (outZ)
      *outZ = zAtRef_ + slope_ * (x - refX_);
    return true;
  }
  [[nodiscard]] bool slopePercentAt(double, double, double* o) const override {
    if (o) *o = std::fabs(slope_) * 100.0;
    return true;
  }
  [[nodiscard]] bool slopeAngleDegAt(double, double, double* o) const override {
    if (o) *o = std::atan(std::fabs(slope_)) * 57.29577951308232;
    return true;
  }
  [[nodiscard]] bool aspectDegAt(double, double, double* o) const override {
    if (o) *o = 90.0;
    return true;
  }

private:
  double x0_, y0_, size_, refX_, zAtRef_, slope_;
};

DaylightSearch Search(double maxOffset = 1000.0, double step = 1.0) {
  DaylightSearch s;
  s.maxOffsetFt = maxOffset;
  s.stepFt = step;
  return s;
}

}  // namespace

TEST_CASE("A fill slope daylights at the hand-computed offset", "[daylight][req371][grading]") {
  // Flat ground at 100 over a generous footprint; design 10 ft above it at 3:1.
  //   slope: 110 - t/3,  ground: 100   ->  t = 30
  const PlaneGround g(-500.0, -500.0, 1000.0, 0.0, 100.0, 0.0);
  SideSlopes s;
  s.cutRun = 2.0;
  s.fillRun = 3.0;

  const DaylightPoint d = SolveDaylightPoint(g, 0.0, 0.0, 110.0, 1.0, 0.0, s, Search());

  REQUIRE(d.daylighted);
  CHECK(d.why == DaylightFailure::None);
  CHECK_FALSE(d.inCut);
  CHECK(d.offset == Approx(30.0).margin(kReq101Ft));
  CHECK(d.x == Approx(30.0).margin(kReq101Ft));
  CHECK(d.z == Approx(100.0).margin(kReq101Ft));  // the point lies ON the surface
}

TEST_CASE("A cut slope daylights at its own hand-computed offset", "[daylight][req371][grading]") {
  // Same flat ground; design 5 ft BELOW it at 2:1.
  //   slope: 95 + t/2,  ground: 100   ->  t = 10
  const PlaneGround g(-500.0, -500.0, 1000.0, 0.0, 100.0, 0.0);
  SideSlopes s;
  s.cutRun = 2.0;
  s.fillRun = 3.0;

  const DaylightPoint d = SolveDaylightPoint(g, 0.0, 0.0, 95.0, 1.0, 0.0, s, Search());

  REQUIRE(d.daylighted);
  CHECK(d.inCut);
  CHECK(d.offset == Approx(10.0).margin(kReq101Ft));
  CHECK(d.z == Approx(100.0).margin(kReq101Ft));
}

TEST_CASE("Cut and fill slopes are two separate values, not one used twice",
          "[daylight][req371][grading]") {
  // REQ-371's own acceptance condition. The SAME vertical distance of 6 ft, once above the ground
  // and once below it, must travel different distances because the two runs differ:
  //   fill at 4:1  ->  6 * 4 = 24 ft
  //   cut  at 2:1  ->  6 * 2 = 12 ft
  // A solver that reached for one slope would return 24 (or 12) for both, and either answer looks
  // perfectly reasonable in isolation.
  const PlaneGround g(-500.0, -500.0, 1000.0, 0.0, 100.0, 0.0);
  SideSlopes s;
  s.cutRun = 2.0;
  s.fillRun = 4.0;

  const DaylightPoint fill = SolveDaylightPoint(g, 0.0, 0.0, 106.0, 1.0, 0.0, s, Search());
  const DaylightPoint cut = SolveDaylightPoint(g, 0.0, 0.0, 94.0, 1.0, 0.0, s, Search());

  REQUIRE(fill.daylighted);
  REQUIRE(cut.daylighted);
  CHECK(fill.offset == Approx(24.0).margin(kReq101Ft));
  CHECK(cut.offset == Approx(12.0).margin(kReq101Ft));
  CHECK(fill.offset != Approx(cut.offset).margin(0.1));
}

TEST_CASE("Sloping ground moves while the slope walks out", "[daylight][req371][grading]") {
  // The case that catches a solver which samples the ground once and then forgets it. Ground rises
  // 20% (z = 100 + 0.2x); design 110 at x = 0, so 10 ft of fill at 3:1.
  //
  //   UP the ramp:    110 - t/3 = 100 + 0.2t   ->  10 = t(1/3 + 0.2) = t(0.533333)  ->  t = 18.75
  //   DOWN the ramp:  110 - t/3 = 100 - 0.2t   ->  10 = t(1/3 - 0.2) = t(0.133333)  ->  t = 75
  //
  // Both are a long way from the 30 ft the flat case gives, in opposite directions, so a sampler
  // that froze the ground elevation cannot accidentally pass either.
  const PlaneGround g(-500.0, -500.0, 1000.0, 0.0, 100.0, 0.2);  // z = 100 + 0.2x
  SideSlopes s;
  s.cutRun = 2.0;
  s.fillRun = 3.0;

  SECTION("uphill reaches ground sooner") {
    const DaylightPoint d = SolveDaylightPoint(g, 0.0, 0.0, 110.0, 1.0, 0.0, s, Search());
    REQUIRE(d.daylighted);
    CHECK(d.offset == Approx(18.75).margin(kReq101Ft));
    CHECK(d.z == Approx(100.0 + 0.2 * 18.75).margin(kReq101Ft));
  }
  SECTION("downhill reaches much further") {
    const DaylightPoint d = SolveDaylightPoint(g, 0.0, 0.0, 110.0, -1.0, 0.0, s, Search());
    REQUIRE(d.daylighted);
    CHECK(d.offset == Approx(75.0).margin(kReq101Ft));
  }
  SECTION("a cut uphill at 2:1") {
    // 95 + t/2 = 100 + 0.2t  ->  5 = t(0.5 - 0.2) = t(0.3)  ->  t = 16.666667
    const DaylightPoint d = SolveDaylightPoint(g, 0.0, 0.0, 95.0, 1.0, 0.0, s, Search());
    REQUIRE(d.daylighted);
    CHECK(d.offset == Approx(16.666667).margin(kReq101Ft));
  }
}

TEST_CASE("A baseline already on the ground daylights where it stands",
          "[daylight][req371][grading]") {
  // Offset zero, and NOT some further crossing found by marching — there is no side slope here.
  const PlaneGround g(-500.0, -500.0, 1000.0, 0.0, 100.0, 0.0);
  const DaylightPoint d = SolveDaylightPoint(g, 7.0, 7.0, 100.0, 1.0, 0.0, SideSlopes{}, Search());

  REQUIRE(d.daylighted);
  CHECK(d.offset == Approx(0.0).margin(kReq101Ft));
  CHECK(d.x == Approx(7.0).margin(kReq101Ft));
  CHECK(d.z == Approx(100.0).margin(kReq101Ft));
}

TEST_CASE("The honest refusals are reported, never extrapolated", "[daylight][req371][grading]") {
  SideSlopes s;
  s.cutRun = 2.0;
  s.fillRun = 3.0;

  SECTION("the slope runs off the surface edge") {
    // Ground stops at x = 10, but a 10 ft fill at 3:1 needs 30 ft. REQ-074 forbids inventing ground
    // past the edge, so this must come back as a refusal rather than a 30 ft answer.
    // The footprint is SQUARE — one `size` governs both axes — so the y range must be given around
    // the baseline too, or the baseline falls outside and this stops testing the edge at all.
    const PlaneGround g(0.0, -5.0, 10.0, 0.0, 100.0, 0.0);  // x in [0,10], y in [-5,5]
    const DaylightPoint d = SolveDaylightPoint(g, 0.0, 0.0, 110.0, 1.0, 0.0, s, Search());
    CHECK_FALSE(d.daylighted);
    CHECK(d.why == DaylightFailure::SlopeLeftSurface);
  }
  SECTION("the baseline itself is off the surface") {
    const PlaneGround g(1000.0, 1000.0, 10.0, 0.0, 100.0, 0.0);
    const DaylightPoint d = SolveDaylightPoint(g, 0.0, 0.0, 110.0, 1.0, 0.0, s, Search());
    CHECK_FALSE(d.daylighted);
    CHECK(d.why == DaylightFailure::BaselineOutsideSurface);
  }
  SECTION("ground falls away exactly as fast as the fill slope") {
    // z = 100 - x/3 against a 3:1 fill: parallel forever. The answer is "never meets", and the
    // search must terminate rather than march to its limit and then claim an edge failure.
    const PlaneGround g(-500.0, -500.0, 1000.0, 0.0, 100.0, -1.0 / 3.0);
    const DaylightPoint d = SolveDaylightPoint(g, 0.0, 0.0, 110.0, 1.0, 0.0, s, Search(400.0, 1.0));
    CHECK_FALSE(d.daylighted);
    CHECK(d.why == DaylightFailure::NeverMeets);
  }
  SECTION("a degenerate slope is refused rather than divided by") {
    const PlaneGround g(-500.0, -500.0, 1000.0, 0.0, 100.0, 0.0);
    SideSlopes bad;
    bad.fillRun = 0.0;
    const DaylightPoint d = SolveDaylightPoint(g, 0.0, 0.0, 110.0, 1.0, 0.0, bad, Search());
    CHECK_FALSE(d.daylighted);
    CHECK(d.why == DaylightFailure::DegenerateSlope);
  }
}

TEST_CASE("Slope percent comes from one place, in REQ-074's convention",
          "[daylight][req371][grading]") {
  // run:rise -> percent. 2:1 is 50%, 3:1 is 33.33%, 4:1 is 25%. REQ-105 was amended specifically so
  // two commands could not describe one slope two ways; this is the single source that prevents it.
  CHECK(SlopePercentFromRun(2.0) == Approx(50.0));
  CHECK(SlopePercentFromRun(3.0) == Approx(33.333333).epsilon(1e-6));
  CHECK(SlopePercentFromRun(4.0) == Approx(25.0));
  CHECK(SlopePercentFromRun(1.0) == Approx(100.0));
  CHECK(SlopePercentFromRun(0.0) == Approx(0.0));   // degenerate, reported as zero not infinity
  CHECK(SlopePercentFromRun(-3.0) == Approx(0.0));
}

TEST_CASE("A closed baseline grades away from its interior whichever way it was drawn",
          "[daylight][req371][grading]") {
  // REQ-371: outward is decided from the ring's signed area, not from vertex order, because the
  // order is an accident of how it was drawn and the enclosed side is not.
  const std::vector<double> ccw = {0.0, 0.0, 10.0, 0.0, 10.0, 10.0, 0.0, 10.0};
  std::vector<double> cw = {0.0, 0.0, 0.0, 10.0, 10.0, 10.0, 10.0, 0.0};

  std::vector<double> nCcw, nCw;
  REQUIRE(BaselineOutwardNormals(ccw, /*closed=*/true, /*leftSide=*/true, &nCcw));
  REQUIRE(BaselineOutwardNormals(cw, /*closed=*/true, /*leftSide=*/true, &nCw));
  REQUIRE(nCcw.size() == 8u);
  REQUIRE(nCw.size() == 8u);

  // Vertex 0 is the (0,0) corner of a square whose interior is toward +x,+y, so outward there points
  // away from the centre: both components negative, and the same for either winding.
  CHECK(nCcw[0] < 0.0);
  CHECK(nCcw[1] < 0.0);
  CHECK(nCw[0] < 0.0);
  CHECK(nCw[1] < 0.0);

  // Every normal must point away from the square's centre, which is the actual claim.
  for (std::size_t i = 0; i < 4; ++i) {
    const double vx = ccw[2 * i] - 5.0;
    const double vy = ccw[2 * i + 1] - 5.0;
    CHECK((nCcw[2 * i] * vx + nCcw[2 * i + 1] * vy) > 0.0);
    CHECK(std::hypot(nCcw[2 * i], nCcw[2 * i + 1]) == Approx(1.0).margin(1e-9));
  }
}

TEST_CASE("An open baseline takes the side it is given", "[daylight][req371][grading]") {
  // A straight run along +x. Left is +y, right is -y, and nothing about the geometry prefers one —
  // which is exactly why REQ-371 makes the caller choose.
  const std::vector<double> open = {0.0, 0.0, 10.0, 0.0, 20.0, 0.0};
  std::vector<double> left, right;
  REQUIRE(BaselineOutwardNormals(open, /*closed=*/false, /*leftSide=*/true, &left));
  REQUIRE(BaselineOutwardNormals(open, /*closed=*/false, /*leftSide=*/false, &right));

  // Walking along +x, the left-hand side is +y. The first version of this assertion had the two
  // backwards and agreed with a solver that also had them backwards, which is exactly the trap a
  // test written from the implementation falls into.
  CHECK(left[1] > 0.0);
  CHECK(right[1] < 0.0);
  for (std::size_t i = 0; i < 3; ++i)
    CHECK(left[2 * i + 1] == Approx(-right[2 * i + 1]).margin(1e-9));
}

TEST_CASE("A baseline too short to have a direction is refused", "[daylight][req371][grading]") {
  std::vector<double> out;
  CHECK_FALSE(BaselineOutwardNormals({}, true, true, &out));
  CHECK_FALSE(BaselineOutwardNormals({1.0, 2.0}, true, true, &out));
  CHECK_FALSE(BaselineOutwardNormals({1.0, 2.0, 1.0, 2.0}, false, true, &out));  // two identical
}

TEST_CASE("Daylight offsets hold at survey coordinate magnitudes", "[daylight][req371][grading]") {
  // REQ-371's last acceptance condition. At E 2,196,000 a float resolves about 0.25 ft — 125x
  // REQ-101 — so this is the case that fails outright if any coordinate narrows on the way through.
  // The expected answer is the SAME 18.75 ft as the local case, which is the point.
  const double e0 = 2196000.0;
  const double n0 = 1400000.0;
  const PlaneGround g(e0 - 500.0, n0 - 500.0, 1000.0, e0, 100.0, 0.2);
  SideSlopes s;
  s.cutRun = 2.0;
  s.fillRun = 3.0;

  const DaylightPoint d = SolveDaylightPoint(g, e0, n0, 110.0, 1.0, 0.0, s, Search());

  REQUIRE(d.daylighted);
  CHECK(d.offset == Approx(18.75).margin(kReq101Ft));
  CHECK(d.x == Approx(e0 + 18.75).margin(kReq101Ft));
  CHECK(d.z == Approx(100.0 + 0.2 * 18.75).margin(kReq101Ft));
}

TEST_CASE("A finer search step does not change the answer", "[daylight][req371][grading]") {
  // REQ-371 records the step as a known limit, so the limit is measured rather than asserted away:
  // on ground this smooth, 5 ft and 0.1 ft steps must agree well inside REQ-101, which is what says
  // the bisection — not the marching granularity — is setting the precision.
  const PlaneGround g(-500.0, -500.0, 1000.0, 0.0, 100.0, 0.2);
  SideSlopes s;
  s.cutRun = 2.0;
  s.fillRun = 3.0;

  const DaylightPoint coarse = SolveDaylightPoint(g, 0.0, 0.0, 110.0, 1.0, 0.0, s, Search(1000.0, 5.0));
  const DaylightPoint fine = SolveDaylightPoint(g, 0.0, 0.0, 110.0, 1.0, 0.0, s, Search(1000.0, 0.1));

  REQUIRE(coarse.daylighted);
  REQUIRE(fine.daylighted);
  CHECK(coarse.offset == Approx(fine.offset).margin(kReq101Ft));
  CHECK(fine.offset == Approx(18.75).margin(kReq101Ft));
}
