#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "util/geom2d.hpp"

using Catch::Approx;

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// REQ-316 / ADR-047: a bulge of 0 is a straight segment — BulgeArc reports invalid and the segment
// length is the plain chord.
TEST_CASE("zero bulge is a straight segment", "[bulge]") {
  const BulgeArcSpan a = BulgeArc(0.0, 0.0, 3.0, 4.0, 0.0);
  REQUIRE_FALSE(a.valid);
  REQUIRE(BulgeSegmentLength(0.0, 0.0, 3.0, 4.0, 0.0) == Approx(5.0));
}

// REQ-316: a quarter circle. bulge = tan(theta/4) = tan(pi/8) for a 90 degree arc. A unit chord
// along +X with a positive (CCW) bulge has radius 1/(2 sin 45) = 1/sqrt(2), centre at the chord's
// perpendicular bisector, and arc length R * pi/2.
TEST_CASE("quarter-circle bulge: radius, sweep and arc length", "[bulge]") {
  const double b = std::tan(kPi / 8.0);
  const BulgeArcSpan a = BulgeArc(0.0, 0.0, 1.0, 0.0, b);
  REQUIRE(a.valid);
  const double R = 1.0 / (2.0 * std::sin(kPi / 4.0));
  REQUIRE(a.radius == Approx(R));
  REQUIRE(std::fabs(a.sweep) == Approx(kPi / 2.0));
  REQUIRE(a.sweep > 0.0);  // positive bulge -> CCW
  REQUIRE(BulgeSegmentLength(0.0, 0.0, 1.0, 0.0, b) == Approx(R * kPi / 2.0));
}

// REQ-316: the arc is tangent to its chord's rotation — for a symmetric arc the tangent at the
// start point makes angle (sweep/2) with the chord. Check the tangent direction we would hand the
// next POLYLINE segment: perpendicular to the centre->start radius, in the sweep direction.
TEST_CASE("arc tangent at the start point bisects toward the chord", "[bulge]") {
  const double b = std::tan(kPi / 8.0);  // 90 degree arc
  const BulgeArcSpan a = BulgeArc(0.0, 0.0, 1.0, 0.0, b);
  REQUIRE(a.valid);
  // Tangent at start = d/dt (centre + R (cos t, sin t)) at t = startAngle, sign of sweep.
  const double tx = -std::sin(a.startAngle) * (a.sweep > 0 ? 1.0 : -1.0);
  const double ty = std::cos(a.startAngle) * (a.sweep > 0 ? 1.0 : -1.0);
  // Chord direction is +X. A positive (CCW) bulge puts the arc on the left of P0->P1 travel, so
  // the heading starts 45 degrees to the RIGHT of the chord and rotates left to +45 by the end.
  const double ang = std::atan2(ty, tx);
  REQUIRE(ang == Approx(-kPi / 4.0).margin(1e-4));
}

// REQ-316: negative bulge sweeps the other way; magnitude is unchanged.
TEST_CASE("negative bulge mirrors the sweep", "[bulge]") {
  const double b = std::tan(kPi / 8.0);
  const BulgeArcSpan p = BulgeArc(0.0, 0.0, 1.0, 0.0, b);
  const BulgeArcSpan n = BulgeArc(0.0, 0.0, 1.0, 0.0, -b);
  REQUIRE(p.valid);
  REQUIRE(n.valid);
  REQUIRE(n.sweep == Approx(-p.sweep));
  REQUIRE(n.radius == Approx(p.radius));
}

// REQ-316: a degenerate chord (coincident endpoints) is not an arc.
TEST_CASE("degenerate chord is invalid", "[bulge]") {
  const BulgeArcSpan a = BulgeArc(5.0, 5.0, 5.0, 5.0, 0.5);
  REQUIRE_FALSE(a.valid);
}

// REQ-316 / ADR-047: ArcBulgeThrough — the bulge grip drags the midpoint. Round-trip: the apex of
// a known quarter-circle bulge must reproduce that bulge. A positive (CCW) bulge on the chord
// (0,0)->(1,0) bows the arc to -Y (see the tangency case above), so its apex sits below the chord.
TEST_CASE("ArcBulgeThrough round-trips a quarter-circle bulge from its apex", "[bulge]") {
  const double b0 = std::tan(kPi / 8.0);
  const BulgeArcSpan a = BulgeArc(0.0, 0.0, 1.0, 0.0, b0);
  REQUIRE(a.valid);
  const double mid = a.startAngle + a.sweep * 0.5;
  const double apexX = a.cx + a.radius * std::cos(mid);
  const double apexY = a.cy + a.radius * std::sin(mid);
  const double b = ArcBulgeThrough(0.0, 0.0, apexX, apexY, 1.0, 0.0);
  REQUIRE(b == Approx(b0).margin(1e-6));
}

TEST_CASE("ArcBulgeThrough: collinear points give a straight segment", "[bulge]") {
  REQUIRE(ArcBulgeThrough(0.0, 0.0, 5.0, 0.0, 10.0, 0.0) == Approx(0.0));
}

TEST_CASE("ArcBulgeThrough: apex on the other side flips the sign", "[bulge]") {
  const double up = ArcBulgeThrough(0.0, 0.0, 0.5, 0.2, 1.0, 0.0);
  const double dn = ArcBulgeThrough(0.0, 0.0, 0.5, -0.2, 1.0, 0.0);
  REQUIRE(dn == Approx(-up).margin(1e-6));
  REQUIRE(std::fabs(up) > 1e-3);
}

// REQ-316: exact distance to an arc, so a hover/pick aperture behaves the same on a curve as on a
// line (the sampled approximation made curves need a bigger aperture between samples).
TEST_CASE("PointArcDistanceSq: perpendicular foot on the arc", "[bulge]") {
  BulgeArcSpan s;
  s.valid = true;
  s.cx = 0.0; s.cy = 0.0; s.radius = 10.0;
  s.startAngle = 0.0; s.sweep = kPi;  // upper half circle
  // A point just outside the arc at angle 90 deg: radius 10.5 -> distance 0.5.
  REQUIRE(PointArcDistanceSq(0.0, 10.5, s) == Approx(0.25));
  // A point just inside: radius 9.7 -> distance 0.3.
  REQUIRE(PointArcDistanceSq(0.0, 9.7, s) == Approx(0.09).margin(1e-9));
  // Exactly on the arc: distance 0.
  REQUIRE(PointArcDistanceSq(10.0, 0.0, s) == Approx(0.0).margin(1e-9));
}

TEST_CASE("PointArcDistanceSq: outside the span falls back to an endpoint", "[bulge]") {
  BulgeArcSpan s;
  s.valid = true;
  s.cx = 0.0; s.cy = 0.0; s.radius = 10.0;
  s.startAngle = 0.0; s.sweep = kPi / 2.0;  // first quadrant only
  // A point at angle -90 deg (below), radius 10: not on the span; nearest endpoint is (10,0).
  const double got = PointArcDistanceSq(0.0, -10.0, s);
  REQUIRE(got == Approx(10.0 * 10.0 + 10.0 * 10.0));  // dist to (10,0) squared
}

// REQ-316 acceptance: a 3-4-5 straight leg plus a quarter circle of radius 10. Total length is
// 5 + (10 * pi/2).
TEST_CASE("polyline length: straight leg plus quarter circle R10", "[bulge]") {
  const double straight = BulgeSegmentLength(0.0, 0.0, 3.0, 4.0, 0.0);
  // A quarter-circle arc of radius 10: chord length = 2 R sin45 = 10 sqrt(2).
  const double chord = 10.0 * std::sqrt(2.0);
  const double b = std::tan(kPi / 8.0);
  const double arc = BulgeSegmentLength(3.0, 4.0, 3.0 + chord, 4.0, b);
  REQUIRE(straight == Approx(5.0));
  REQUIRE(arc == Approx(10.0 * kPi / 2.0));
  REQUIRE(straight + arc == Approx(5.0 + 5.0 * kPi));
}
