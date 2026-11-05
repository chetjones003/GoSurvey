#include "alignment/Alignment.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

// REQ-503: every expected value below is computed by hand from closed-form geometry, not by
// running the code under test a second way, because an alignment that is wrong by a sign or a
// half-angle still looks plausible on screen.

using alignment::Alignment;
using alignment::Entity;
using alignment::EntityKind;
using alignment::Point2;
using alignment::StationEquation;
using Catch::Approx;

namespace {
constexpr double kDeg = alignment::kPi / 180.0;
constexpr double kEps = 1e-6;  // REQ-499 position tolerance

/// Tangent N45E 500 -> right curve R500 / 15 deg -> tangent 200, starting at a given origin.
Alignment SampleAlignment(Point2 origin = {1000.0, 2000.0}) {
  Alignment a;
  a.name = "MAIN";
  REQUIRE(a.AppendTangent(origin, 45.0 * kDeg, 500.0));
  REQUIRE(a.AppendCurveByDelta(500.0, 15.0 * kDeg, true));
  REQUIRE(a.AppendTangent(200.0));
  return a;
}
}  // namespace

TEST_CASE("Tangent end point, bearing and length follow from the azimuth", "[req481][req503][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({1000.0, 2000.0}, 45.0 * kDeg, 500.0));
  const Point2 e = a.EndPoint();
  CHECK(e.x == Approx(1353.5533905932738).margin(kEps));
  CHECK(e.y == Approx(2353.5533905932738).margin(kEps));
  CHECK(a.Length() == Approx(500.0));
  CHECK(a.EndAzimuth() == Approx(45.0 * kDeg));
  CHECK(a.EndStation() == Approx(500.0));
}

TEST_CASE("Circular curve closed-form values", "[req481][req503][alignment]") {
  // R = 500, delta = 15 deg. Hand values: tan(7.5deg)=0.1316525 => T=65.82625; L=130.89969; C=1000*sin(7.5deg)=130.52619
  const alignment::CurveData d = alignment::ComputeCurveData({0.0, 0.0}, 0.0, 500.0, 500.0 * 15.0 * kDeg, true);
  CHECK(d.delta == Approx(15.0 * kDeg));
  CHECK(d.tangentLength == Approx(65.82625).margin(1e-4));
  CHECK(d.arcLength == Approx(130.89969).margin(1e-4));
  CHECK(d.chord == Approx(130.52619).margin(1e-4));
  CHECK(d.external == Approx(4.31448).margin(1e-4));
  CHECK(d.midOrdinate == Approx(4.27757).margin(1e-4));
  // Heading north, curving right: PC at origin, PI straight ahead, centre 500 to the east.
  CHECK(d.pi.x == Approx(0.0).margin(kEps));
  CHECK(d.pi.y == Approx(d.tangentLength).margin(kEps));
  CHECK(d.center.x == Approx(500.0).margin(kEps));
  CHECK(d.center.y == Approx(0.0).margin(kEps));
  // PC, PT both lie on the circle and subtend delta.
  CHECK(std::hypot(d.pt.x - d.center.x, d.pt.y - d.center.y) == Approx(500.0).margin(kEps));
  CHECK(std::hypot(d.pi.x - d.pt.x, d.pi.y - d.pt.y) == Approx(d.tangentLength).margin(kEps));
  CHECK(d.degreeOfCurveArc == Approx(11.4591559).margin(1e-6));
}

TEST_CASE("Left and right curves mirror each other", "[req481][alignment]") {
  Alignment r, l;
  REQUIRE(r.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(r.AppendCurveByDelta(300.0, 40.0 * kDeg, true));
  REQUIRE(l.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(l.AppendCurveByDelta(300.0, 40.0 * kDeg, false));
  CHECK(r.EndPoint().x == Approx(-l.EndPoint().x).margin(kEps));
  CHECK(r.EndPoint().y == Approx(l.EndPoint().y).margin(kEps));
  CHECK(r.EndAzimuth() == Approx(40.0 * kDeg));
  CHECK(l.EndAzimuth() == Approx(360.0 * kDeg - 40.0 * kDeg));
}

TEST_CASE("Compound and reverse curves are C0 and C1", "[req481][req482][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 50.0));
  REQUIRE(a.AppendCurveByDelta(200.0, 30.0 * kDeg, true));
  REQUIRE(a.AppendCurveByDelta(400.0, 20.0 * kDeg, true));   // compound
  REQUIRE(a.AppendCurveByDelta(300.0, 25.0 * kDeg, false));  // reverse
  for (const alignment::ContinuityIssue& c : a.CheckContinuity()) {
    CHECK(c.c0);
    CHECK(c.c1);
  }
  CHECK(a.EndAzimuth() == Approx((30.0 + 20.0 - 25.0) * kDeg));
  bool compound = false, reverse = false;
  for (const alignment::GeometryPoint& p : a.GeometryPoints()) {
    compound = compound || p.code == alignment::GeometryPointCode::CompCurveCurve;
    reverse = reverse || p.code == alignment::GeometryPointCode::RevCurveCurve;
  }
  CHECK(compound);
  CHECK(reverse);
}

TEST_CASE("Station and XY round trip on a tangent-curve-tangent", "[req484][req503][alignment]") {
  const Alignment a = SampleAlignment();
  for (double sta : {0.0, 123.4, 500.0, 520.0, 630.9, 700.0, 830.8997}) {
    for (double off : {-500.0, -15.42, -0.001, 0.0, 0.001, 12.0, 400.0}) {
      const Point2 p = a.GetPointFromStationOffset(sta, off);
      const alignment::StationOffset so = a.GetStationOffset(p.x, p.y);
      INFO("sta=" << sta << " off=" << off);
      CHECK(so.internalStation == Approx(sta).margin(kEps));
      CHECK(so.offset == Approx(off).margin(kEps));
      CHECK_FALSE(so.outOfRange);
    }
  }
}

TEST_CASE("Offsets are positive to the right of travel", "[req484][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));  // heading north
  const Point2 right = a.GetPointFromStationOffset(50.0, 10.0);
  CHECK(right.x == Approx(10.0));  // east is right of north
  const alignment::StationOffset so = a.GetStationOffset(-10.0, 50.0);
  CHECK(so.offset == Approx(-10.0));
  CHECK(so.internalStation == Approx(50.0));
}

TEST_CASE("Points beyond the ends clamp and are flagged", "[req484][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));
  const alignment::StationOffset past = a.GetStationOffset(0.0, 103.0);
  CHECK(past.outOfRange);
  CHECK(past.internalStation == Approx(100.0));
  CHECK(std::fabs(past.offset) == Approx(3.0).margin(kEps));  // distance to the end point, not an extension
  const alignment::StationOffset before = a.GetStationOffset(4.0, -3.0);
  CHECK(before.outOfRange);
  CHECK(before.internalStation == Approx(0.0));
  CHECK(before.offset == Approx(5.0).margin(kEps));  // 3-4-5 to the start point; right side is positive
}

TEST_CASE("Spiral station-offset projection converges at large offsets", "[req484][alignment][regression]") {
  Alignment a;
  Entity spiral;
  spiral.kind = EntityKind::Spiral;
  spiral.start = {0.0, 0.0};
  spiral.startAzimuth = 0.0;
  spiral.length = 100.0;
  spiral.startRadius = 0.0;
  spiral.radius = 100.0;
  spiral.right = true;
  REQUIRE(a.AppendRaw(spiral));

  constexpr double station = 50.03;
  constexpr double offset = -1000.0;
  const Point2 point = a.GetPointFromStationOffset(station, offset);
  const alignment::StationOffset projected = a.GetStationOffset(point.x, point.y);
  CHECK(projected.internalStation == Approx(station).margin(kEps));
  CHECK(projected.offset == Approx(offset).margin(kEps));
  CHECK_FALSE(projected.outOfRange);
}

TEST_CASE("Spiral endpoint perpendiculars are in range but tangent extensions are not",
          "[req484][alignment][regression]") {
  Alignment a;
  Entity spiral;
  spiral.kind = EntityKind::Spiral;
  spiral.start = {0.0, 0.0};
  spiral.startAzimuth = 0.0;
  spiral.length = 100.0;
  spiral.startRadius = 0.0;
  spiral.radius = 100.0;
  spiral.right = true;
  REQUIRE(a.AppendRaw(spiral));

  for (const auto [station, offset] : {std::pair{0.0, 0.0}, std::pair{0.0, 25.0}, std::pair{0.0, -25.0},
                                       std::pair{100.0, 0.0}, std::pair{100.0, 25.0}, std::pair{100.0, -25.0}}) {
    const Point2 point = a.GetPointFromStationOffset(station, offset);
    const alignment::StationOffset projected = a.GetStationOffset(point.x, point.y);
    CHECK(projected.internalStation == Approx(station).margin(kEps));
    CHECK(projected.offset == Approx(offset).margin(kEps));
    CHECK_FALSE(projected.outOfRange);
  }

  const alignment::StationOffset before = a.GetStationOffset(0.0, -3.0);
  CHECK(before.internalStation == Approx(a.StartStation()).margin(kEps));
  CHECK(before.outOfRange);
  const Point2 end = a.EndPoint();
  const double endAzimuth = a.EndAzimuth();
  const alignment::StationOffset after =
      a.GetStationOffset(end.x + 3.0 * std::sin(endAzimuth), end.y + 3.0 * std::cos(endAzimuth));
  CHECK(after.internalStation == Approx(a.EndStation()).margin(kEps));
  CHECK(after.outOfRange);
}

TEST_CASE("A point radially outside a curve measures to the nearest arc point", "[req484][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 10.0));
  REQUIRE(a.AppendCurveByDelta(100.0, 90.0 * kDeg, true));  // centre at (100, 10)
  // A point beyond the curve's centre line direction at 45 deg of sweep, 30 outside the arc (left side).
  const double ang = 45.0 * kDeg;  // sweep so far
  const alignment::Point2 foot = a.GetPointAtStation(10.0 + 100.0 * ang);
  const double az = a.GetBearingAtStation(10.0 + 100.0 * ang);
  const double ox = foot.x - 30.0 * std::cos(az);  // 30 to the LEFT = away from the centre
  const double oy = foot.y + 30.0 * std::sin(az);
  const alignment::StationOffset so = a.GetStationOffset(ox, oy);
  CHECK(so.internalStation == Approx(10.0 + 100.0 * ang).margin(kEps));
  CHECK(so.offset == Approx(-30.0).margin(kEps));
}

TEST_CASE("Entity lookup is indexed and the earlier entity wins at a junction", "[req484][alignment]") {
  const Alignment a = SampleAlignment();
  CHECK(a.GetEntityAtStation(0.0) == 0);
  CHECK(a.GetEntityAtStation(500.0) == 0);
  CHECK(a.GetEntityAtStation(500.0001) == 1);
  CHECK(a.GetEntityAtStation(1e9) == 2);
  CHECK(a.GetCurvatureAtStation(600.0) == Approx(1.0 / 500.0));
  CHECK(a.GetCurvatureAtStation(100.0) == Approx(0.0).margin(1e-15));
}

TEST_CASE("Start station is configurable and the end station is computed", "[req483][alignment]") {
  Alignment a;
  a.startingStation = 150.0;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 1000.0));
  CHECK(alignment::FormatStation(a.StartStation()) == "1+50.00");
  CHECK(alignment::FormatStation(a.EndStation()) == "11+50.00");
  CHECK(a.GetPointAtStation(650.0).y == Approx(500.0));
}

TEST_CASE("Increasing station equation", "[req483][req503][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 2000.0));
  std::string why;
  REQUIRE(a.AddStationEquation({1000.0, 1200.0, true}, &why));
  CHECK(a.DisplayedStation(500.0) == Approx(500.0));
  CHECK(a.DisplayedStation(1000.0, false) == Approx(1000.0));  // back side
  CHECK(a.DisplayedStation(1000.0, true) == Approx(1200.0));   // ahead side
  CHECK(a.DisplayedStation(1500.0) == Approx(1700.0));
  CHECK(a.EndStationWithEquations() == Approx(2200.0));
  // Geometry did not move.
  CHECK(a.GetPointAtStation(1500.0).y == Approx(1500.0));
}

TEST_CASE("Decreasing equation can map one displayed station to two places", "[req483][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 2000.0));
  REQUIRE(a.AddStationEquation({1000.0, 950.0, false}));
  CHECK(a.DisplayedStation(1500.0) == Approx(1450.0));
  const std::vector<double> hits = a.RawStationsForDisplayed(975.0);
  REQUIRE(hits.size() == 2);
  CHECK(hits[0] == Approx(975.0));
  CHECK(hits[1] == Approx(1025.0));
  CHECK(a.RawStationsForDisplayed(1450.0).size() == 1);
}

TEST_CASE("Multiple equations compose and boundaries are exact", "[req483][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 3000.0));
  REQUIRE(a.AddStationEquation({1000.0, 1100.0, true}));  // +100
  REQUIRE(a.AddStationEquation({2000.0, 2050.0, false}));  // back = 2100, ahead 2050: -50
  const double eps = 1e-6;
  CHECK(a.DisplayedStation(1000.0 - eps) == Approx(1000.0 - eps).margin(1e-9));
  CHECK(a.DisplayedStation(1000.0, true) == Approx(1100.0));
  CHECK(a.DisplayedStation(1000.0 + eps) == Approx(1100.0 + eps).margin(1e-9));
  CHECK(a.DisplayedStation(2000.0, false) == Approx(2100.0));
  CHECK(a.DisplayedStation(2000.0, true) == Approx(2050.0));
  CHECK(a.DisplayedStation(2500.0) == Approx(2550.0));
  CHECK(a.EquationDifference(0) == Approx(100.0));
  CHECK(a.EquationDifference(1) == Approx(-50.0));
}

TEST_CASE("Invalid station equations are refused with a reason", "[req483][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 1000.0));
  std::string why;
  CHECK_FALSE(a.AddStationEquation({5000.0, 5000.0, true}, &why));
  CHECK_FALSE(why.empty());
  CHECK_FALSE(a.AddStationEquation({500.0, 400.0, true}, &why));   // increasing but goes down
  CHECK_FALSE(a.AddStationEquation({500.0, 600.0, false}, &why));  // decreasing but goes up
  REQUIRE(a.AddStationEquation({500.0, 600.0, true}));
  CHECK_FALSE(a.AddStationEquation({500.0, 700.0, true}, &why));   // duplicate location
}

TEST_CASE("Station formatting and parsing round trip", "[req483][alignment]") {
  using alignment::FormatStation;
  using alignment::ParseStation;
  CHECK(FormatStation(0.0) == "0+00.00");
  CHECK(FormatStation(125.0) == "1+25.00");
  CHECK(FormatStation(1245.32) == "12+45.32");
  CHECK(FormatStation(99.999) == "1+00.00");
  alignment::StationFormat sta;
  sta.prefix = "STA ";
  CHECK(FormatStation(1245.32, sta) == "STA 12+45.32");
  alignment::StationFormat metric;
  metric.digitsRight = 3;
  metric.decimals = 3;
  CHECK(FormatStation(1245.32, metric) == "1+245.320");
  alignment::StationFormat nodelim;
  nodelim.delimiter = '\0';
  CHECK(FormatStation(1050.0, nodelim) == "1050.00");
  for (double v : {0.0, 125.0, 1245.32, 100000.5}) {
    double back = -1.0;
    REQUIRE(ParseStation(FormatStation(v), &back));
    CHECK(back == Approx(v).margin(0.005));
  }
  double v = 0.0;
  REQUIRE(ParseStation("STA 12+45.32", &v));
  CHECK(v == Approx(1245.32));
  REQUIRE(ParseStation("1245.32", &v));
  CHECK(v == Approx(1245.32));
  CHECK_FALSE(ParseStation("12+145", &v));  // minor part exceeds two digits
  CHECK_FALSE(ParseStation("abc", &v));
  CHECK_FALSE(ParseStation("", &v));
}

TEST_CASE("Continuity checks flag a gap and a kink", "[req482][alignment]") {
  Alignment a;
  Entity e1;
  e1.start = {0, 0};
  e1.startAzimuth = 0.0;
  e1.length = 100.0;
  REQUIRE(a.AppendRaw(e1));
  Entity e2 = e1;
  e2.start = {0.001, 100.0};  // 1 mm gap, same heading
  REQUIRE(a.AppendRaw(e2));
  Entity e3 = e1;
  e3.start = a.EndPoint();
  e3.startAzimuth = 5.0 * kDeg;  // kink
  REQUIRE(a.AppendRaw(e3));
  const auto issues = a.CheckContinuity();
  REQUIRE(issues.size() == 2);
  CHECK_FALSE(issues[0].c0);
  CHECK(issues[0].gap == Approx(0.001).margin(1e-9));
  CHECK(issues[0].c1);
  CHECK(issues[1].c0);
  CHECK_FALSE(issues[1].c1);
  CHECK(issues[1].angleDelta == Approx(5.0 * kDeg));
}

TEST_CASE("Large state-plane coordinates keep sub-micron precision", "[req499][req503][alignment]") {
  const Point2 origin{6'500'000.123456, 2'100'000.654321};  // state-plane scale
  const Alignment big = SampleAlignment(origin);
  const Alignment small = SampleAlignment({0.0, 0.0});
  for (double sta : {250.0, 540.0, 700.0}) {
    const Point2 pb = big.GetPointAtStation(sta);
    const Point2 ps = small.GetPointAtStation(sta);
    CHECK(pb.x - origin.x == Approx(ps.x).margin(1e-7));
    CHECK(pb.y - origin.y == Approx(ps.y).margin(1e-7));
    const alignment::StationOffset so = big.GetStationOffset(pb.x + 0.5, pb.y);
    CHECK(so.internalStation == Approx(sta).margin(0.5));  // sanity: near the same place
  }
  const Point2 p = big.GetPointFromStationOffset(540.0, 0.001);
  const alignment::StationOffset so = big.GetStationOffset(p.x, p.y);
  CHECK(so.offset == Approx(0.001).margin(1e-6));
}

TEST_CASE("Very small and very large radii and long alignments", "[req499][req503][alignment]") {
  Alignment tight;
  REQUIRE(tight.AppendTangent({0, 0}, 0.0, 1.0));
  REQUIRE(tight.AppendCurveByDelta(0.01, 90.0 * kDeg, true));
  CHECK(tight.EndAzimuth() == Approx(90.0 * kDeg));

  Alignment flat;  // R = 1e7, delta 1e-6 rad (near-tangent)
  REQUIRE(flat.AppendTangent({0, 0}, 0.0, 10.0));
  REQUIRE(flat.AppendCurve(1.0e7, 10.0, true));
  CHECK(flat.EndAzimuth() == Approx(1.0e-6).margin(1e-12));
  const Point2 e = flat.EndPoint();
  CHECK(e.y == Approx(20.0).margin(1e-6));
  CHECK(e.x == Approx(10.0 * 5.0e-7).margin(1e-9));

  Alignment longA;  // 1000 x 1000-unit tangents/curves alternating
  REQUIRE(longA.AppendTangent({0, 0}, 0.0, 1000.0));
  for (int i = 0; i < 1000; ++i) {
    REQUIRE(longA.AppendCurveByDelta(2000.0, 0.05, i % 2 == 0));
    REQUIRE(longA.AppendTangent(1000.0));
  }
  CHECK(longA.Length() > 1.0e6);
  const alignment::StationOffset so = longA.GetStationOffset(longA.GetPointAtStation(750000.0).x,
                                                              longA.GetPointAtStation(750000.0).y);
  CHECK(so.internalStation == Approx(750000.0).margin(1e-5));
}

TEST_CASE("Degenerate curves are refused by name", "[req499][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 10.0));
  CHECK_FALSE(a.AppendCurve(0.0, 5.0, true));
  CHECK_FALSE(a.AppendCurve(-5.0, 5.0, true));
  CHECK_FALSE(a.AppendCurve(100.0, 0.0, true));
  CHECK_FALSE(a.AppendCurveByDelta(100.0, 2.0 * alignment::kPi, true));
  CHECK_FALSE(a.AppendTangent(0.0));
  CHECK(a.Entities().size() == 1);
}

TEST_CASE("Reverse keeps the physical geometry and twice is the identity", "[req487][alignment]") {
  Alignment a = SampleAlignment();
  const Point2 s = a.StartPoint();
  const Point2 e = a.EndPoint();
  const double len = a.Length();
  const Point2 mid = a.GetPointAtStation(len * 0.4);
  REQUIRE(a.Reverse());
  CHECK(a.StartPoint().x == Approx(e.x).margin(kEps));
  CHECK(a.StartPoint().y == Approx(e.y).margin(kEps));
  CHECK(a.EndPoint().x == Approx(s.x).margin(kEps));
  CHECK(a.EndPoint().y == Approx(s.y).margin(kEps));
  const Point2 m2 = a.GetPointAtStation(len * 0.6);
  CHECK(m2.x == Approx(mid.x).margin(kEps));
  CHECK(m2.y == Approx(mid.y).margin(kEps));
  REQUIRE(a.Entities().size() == 3);
  CHECK(a.Entities()[1].kind == EntityKind::Arc);
  CHECK_FALSE(a.Entities()[1].right);  // right curve becomes left
  REQUIRE(a.Reverse());
  const Alignment orig = SampleAlignment();
  for (size_t i = 0; i < 3; ++i) {
    CHECK(a.Entities()[i].start.x == Approx(orig.Entities()[i].start.x).margin(1e-9));
    CHECK(a.Entities()[i].start.y == Approx(orig.Entities()[i].start.y).margin(1e-9));
    CHECK(a.Entities()[i].startAzimuth == Approx(orig.Entities()[i].startAzimuth).margin(1e-9));
    CHECK(a.Entities()[i].right == orig.Entities()[i].right);
    // D-2026-11-05-a: within 1e-9, with the same kinds, directions, constraints and locks.
    CHECK(a.Entities()[i].kind == orig.Entities()[i].kind);
    CHECK(a.Entities()[i].length == Approx(orig.Entities()[i].length).margin(1e-9));
    CHECK(a.Entities()[i].radius == Approx(orig.Entities()[i].radius).margin(1e-9));
    CHECK(a.Entities()[i].startRadius == Approx(orig.Entities()[i].startRadius).margin(1e-9));
    CHECK(a.Entities()[i].constraint == orig.Entities()[i].constraint);
    CHECK(a.Entities()[i].locks == orig.Entities()[i].locks);
  }
}

TEST_CASE("Reverse refuses a gapped alignment and notes dropped equations", "[req487][alignment]") {
  Alignment gap;
  Entity e;
  e.start = {0, 0};
  e.length = 10.0;
  REQUIRE(gap.AppendRaw(e));
  e.start = {0, 11.0};
  REQUIRE(gap.AppendRaw(e));
  CHECK_FALSE(gap.Reverse());

  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(a.AddStationEquation({50.0, 60.0, true}));
  std::string note;
  REQUIRE(a.Reverse(&note));
  CHECK(note.find("1 station equation") != std::string::npos);
  CHECK(a.equations.empty());
}

TEST_CASE("Create from objects: line, line, arc become tangent, tangent, curve", "[req485][alignment]") {
  using alignment::SourceSegment;
  std::vector<SourceSegment> src;
  SourceSegment l1;
  l1.a = {0, 0};
  l1.b = {0, 100};
  l1.sourceIndex = 1;
  SourceSegment l2;
  l2.a = {0, 100};
  l2.b = {50, 100};  // a sharp corner, not tangent: still representable
  l2.sourceIndex = 2;
  SourceSegment arc;  // CCW quarter arc, radius 40, centre (50, 140): from (50,100) at -90deg to (90,140) at 0deg
  arc.isArc = true;
  arc.center = {50, 140};
  arc.radius = 40.0;
  arc.startAngle = -90.0 * kDeg;
  arc.endAngle = 0.0;
  arc.sourceIndex = 3;
  src = {arc, l1, l2};  // deliberately out of order
  alignment::FromObjectsOptions pickStart;
  pickStart.hasPick = true;
  pickStart.pick = {0, 0};
  Alignment out;
  std::string err;
  REQUIRE(alignment::BuildFromSegments(src, pickStart, &out, &err));
  REQUIRE(out.Entities().size() == 3);
  CHECK(out.Entities()[0].kind == EntityKind::Line);
  CHECK(out.Entities()[1].kind == EntityKind::Line);
  CHECK(out.Entities()[2].kind == EntityKind::Arc);
  CHECK(out.Entities()[2].radius == Approx(40.0).margin(1e-9));
  CHECK_FALSE(out.Entities()[2].right);  // CCW source = left turn
  CHECK(out.Entities()[2].length == Approx(40.0 * alignment::kPi / 2.0).margin(1e-9));
  CHECK(out.EndPoint().x == Approx(90.0).margin(1e-9));
  CHECK(out.EndPoint().y == Approx(140.0).margin(1e-9));
  CHECK(out.CurveCount() == 1);
}

TEST_CASE("Create from objects: pick selects the direction and R reverses it", "[req485][alignment]") {
  using alignment::SourceSegment;
  SourceSegment l1;
  l1.a = {0, 0};
  l1.b = {100, 0};
  SourceSegment l2;
  l2.a = {100, 0};
  l2.b = {100, 100};
  alignment::FromObjectsOptions opt;
  opt.hasPick = true;
  opt.pick = {100, 100};  // nearest end is the far end of l2
  Alignment out;
  std::string err;
  REQUIRE(alignment::BuildFromSegments({l1, l2}, opt, &out, &err));
  CHECK(out.StartPoint().x == Approx(100.0));
  CHECK(out.StartPoint().y == Approx(100.0));
  CHECK(out.EndPoint().x == Approx(0.0));

  opt.reverse = true;
  REQUIRE(alignment::BuildFromSegments({l1, l2}, opt, &out, &err));
  CHECK(out.StartPoint().x == Approx(0.0));
  CHECK(out.EndPoint().y == Approx(100.0));
}

TEST_CASE("Create from objects: a polyline bulge becomes a true curve", "[req485][alignment]") {
  using alignment::SegmentFromBulge;
  // Three line segments and one bulge segment (bulge = tan(90deg/4) is a quarter circle, CCW).
  const double b = std::tan(90.0 * kDeg / 4.0);
  std::vector<alignment::SourceSegment> src = {
      SegmentFromBulge({0, 0}, {100, 0}, 0.0, 1), SegmentFromBulge({100, 0}, {200, 0}, 0.0, 2),
      SegmentFromBulge({200, 0}, {300, 100}, b, 3), SegmentFromBulge({300, 100}, {300, 300}, 0.0, 4)};
  Alignment out;
  std::string err;
  REQUIRE(alignment::BuildFromSegments(src, {}, &out, &err));
  REQUIRE(out.Entities().size() == 4);
  int arcs = 0;
  for (const Entity& e : out.Entities()) arcs += e.kind == EntityKind::Arc ? 1 : 0;
  CHECK(arcs == 1);
  const Entity& arc = out.Entities()[2];
  CHECK(arc.radius == Approx(100.0).margin(1e-9));  // chord 141.42 over 90 deg => R = 100
  CHECK_FALSE(arc.right);
  CHECK(out.EndPoint().y == Approx(300.0).margin(1e-9));
}

TEST_CASE("Create from objects: a clockwise bulge is a right curve", "[req485][alignment]") {
  const double b = -std::tan(90.0 * kDeg / 4.0);
  const alignment::SourceSegment seg = alignment::SegmentFromBulge({0, 0}, {100, -100}, b, 1);
  alignment::SourceSegment l;
  l.a = {100, -100};
  l.b = {100, -300};
  Alignment out;
  std::string err;
  REQUIRE(alignment::BuildFromSegments({seg, l}, {}, &out, &err));
  REQUIRE(out.Entities().size() == 2);
  CHECK(out.Entities()[0].kind == EntityKind::Arc);
  CHECK(out.Entities()[0].right);
  CHECK(out.Entities()[0].radius == Approx(100.0).margin(1e-9));
  CHECK(out.EndPoint().y == Approx(-300.0).margin(1e-9));
}

TEST_CASE("Create from objects refuses gaps, naming the object and the gap", "[req485][alignment]") {
  alignment::SourceSegment l1;
  l1.a = {0, 0};
  l1.b = {100, 0};
  l1.sourceIndex = 7;
  alignment::SourceSegment l2;
  l2.a = {102, 0};  // 2 units away
  l2.b = {200, 0};
  l2.sourceIndex = 9;
  Alignment out;
  std::string err;
  CHECK_FALSE(alignment::BuildFromSegments({l1, l2}, {}, &out, &err));
  CHECK(out.Empty());
  CHECK(err.find("objects 7 and 9 are not connected: gap of 2 ") != std::string::npos);

  alignment::SourceSegment zero;
  zero.a = {5, 5};
  zero.b = {5, 5};
  zero.sourceIndex = 4;
  CHECK_FALSE(alignment::BuildFromSegments({zero}, {}, &out, &err));
  CHECK(err.find("4") != std::string::npos);
  CHECK_FALSE(alignment::BuildFromSegments({}, {}, &out, &err));
}

TEST_CASE("Create from objects: fillet a 90 degree corner", "[req485][alignment]") {
  alignment::SourceSegment l1;
  l1.a = {0, 0};
  l1.b = {0, 500};
  alignment::SourceSegment l2;
  l2.a = {0, 500};
  l2.b = {500, 500};
  alignment::FromObjectsOptions opt;
  opt.addFillets = true;
  opt.filletRadius = 100.0;
  Alignment out;
  std::string err;
  std::vector<std::string> warn;
  REQUIRE(alignment::BuildFromSegments({l1, l2}, opt, &out, &err, &warn));
  REQUIRE(out.Entities().size() == 3);
  CHECK(out.Entities()[0].length == Approx(400.0).margin(1e-9));  // shortened by T = 100
  CHECK(out.Entities()[1].kind == EntityKind::Arc);
  CHECK(out.Entities()[1].right);
  CHECK(out.Entities()[1].length == Approx(100.0 * alignment::kPi / 2.0).margin(1e-9));
  CHECK(out.Entities()[2].length == Approx(400.0).margin(1e-9));
  CHECK(out.EndPoint().x == Approx(500.0).margin(1e-9));
  CHECK(out.EndPoint().y == Approx(500.0).margin(1e-9));
  for (const alignment::ContinuityIssue& c : out.CheckContinuity()) {
    CHECK(c.c0);
    CHECK(c.c1);
  }

  opt.filletRadius = 900.0;  // too big: the radius shrinks so T is 40 % of the shorter tangent (D-2026-11-04-i)
  warn.clear();
  REQUIRE(alignment::BuildFromSegments({l1, l2}, opt, &out, &err, &warn));
  REQUIRE(out.Entities().size() == 3);
  CHECK(out.Entities()[1].radius == Approx(200.0).margin(1e-9));
  REQUIRE(warn.size() == 1);
  CHECK(warn[0].find("used radius 200") != std::string::npos);
}

TEST_CASE("Geometry points, bounds and display sampling", "[req490][req495][alignment]") {
  const Alignment a = SampleAlignment({0.0, 0.0});
  int pc = 0, pt = 0, pi = 0, centre = 0, begin = 0, end = 0;
  for (const alignment::GeometryPoint& p : a.GeometryPoints()) {
    using C = alignment::GeometryPointCode;
    pc += p.code == C::TanCurve;
    pt += p.code == C::CurveTan;
    pi += p.code == C::CurvePI;
    centre += p.code == C::CurveCenter;
    begin += p.code == C::Begin;
    end += p.code == C::End;
  }
  CHECK(pc == 1);
  CHECK(pt == 1);
  CHECK(pi == 1);
  CHECK(centre == 1);
  CHECK(begin == 1);
  CHECK(end == 1);

  // A half circle of radius 100 heading north then curving right: bounds include the east extreme.
  Alignment half;
  REQUIRE(half.AppendTangent({0, 0}, 0.0, 10.0));
  REQUIRE(half.AppendCurveByDelta(100.0, 180.0 * kDeg - 1e-6, true));
  Point2 mn, mx;
  REQUIRE(half.Bounds(&mn, &mx));
  CHECK(mx.x == Approx(200.0).margin(1e-6));  // centre (100, 10), radius 100: the east extreme is x = 200
}

TEST_CASE("Display sampling never exceeds the chord height", "[req495][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 10.0));
  REQUIRE(a.AppendCurveByDelta(5.0, 90.0 * kDeg, true));
  REQUIRE(a.AppendCurveByDelta(5000.0, 10.0 * kDeg, false));
  const double h = 0.01;
  const auto pts = a.SampleForDisplay(h);
  REQUIRE(pts.size() > 4);
  // Every sampled midpoint chord must be within h of the true curve: check against station-evaluated points.
  const alignment::Point2 first = pts.front();
  CHECK(first.x == Approx(0.0));
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const Point2 m{(pts[i].x + pts[i + 1].x) * 0.5, (pts[i].y + pts[i + 1].y) * 0.5};
    const alignment::StationOffset so = a.GetStationOffset(m.x, m.y);
    CHECK(std::fabs(so.offset) <= h * 1.0001);
  }
}

TEST_CASE("Clothoid spiral deflection and end curvature", "[req481][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 50.0));
  REQUIRE(a.AppendSpiral(100.0, 0.0, 500.0, true));  // tangent→spiral, R=500, Ls=100
  CHECK(a.SpiralCount() == 1);
  CHECK(a.EndAzimuth() == Approx(0.1).margin(1e-9));
  CHECK(a.GetCurvatureAtStation(a.EndStation()) == Approx(1.0 / 500.0).margin(1e-12));
  const Point2 end = a.EndPoint();
  CHECK(end.x == Approx(3.330959502573634).margin(1e-8));
  CHECK(end.y == Approx(50.0 + 99.900046285311).margin(1e-8));
  alignment::SpiralData sd;
  REQUIRE(alignment::SpiralDataFor(1, a, &sd));
  CHECK(sd.parameterA == Approx(std::sqrt(500.0 * 100.0)).margin(1e-6));
  CHECK(sd.deflection == Approx(0.1).margin(1e-9));
}

TEST_CASE("Spiral bounds include interior extrema", "[req481][alignment][regression]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 10.0));
  REQUIRE(a.AppendSpiral(400.0, 0.0, 100.0, true));

  Point2 mn{}, mx{};
  REQUIRE(a.Bounds(&mn, &mx));
  const auto samples = a.SampleForDisplay(0.01);
  REQUIRE(samples.size() > 2);
  double sampledMaxY = -std::numeric_limits<double>::infinity();
  for (const Point2 p : samples) sampledMaxY = std::max(sampledMaxY, p.y);

  const double endpointMaxY = std::max(a.StartPoint().y, a.EndPoint().y);
  CHECK(sampledMaxY > endpointMaxY + 1e-3);
  CHECK(mx.y >= sampledMaxY - 1e-6);
}

TEST_CASE("Spiral A uses curvature change for finite to finite spirals", "[req481][alignment][regression]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 10.0));
  REQUIRE(a.AppendSpiral(100.0, 500.0, 250.0, true));

  alignment::SpiralData sd;
  REQUIRE(alignment::SpiralDataFor(1, a, &sd));
  CHECK(sd.parameterA == Approx(std::sqrt(100.0 / std::fabs(1.0 / 250.0 - 1.0 / 500.0))).margin(1e-9));
}

TEST_CASE("Spiral over half-turn omits undefined tangent intersection", "[req481][req490][alignment][regression]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 10.0));
  REQUIRE(a.AppendSpiral(1000.0, 0.0, 100.0, true));

  alignment::SpiralData sd;
  REQUIRE(alignment::SpiralDataFor(1, a, &sd));
  CHECK_FALSE(sd.tangentEnd);
  const auto geometry = a.GeometryPoints();
  CHECK(std::none_of(geometry.begin(), geometry.end(), [](const alignment::GeometryPoint& p) {
    return p.code == alignment::GeometryPointCode::SpiralPI;
  }));
}

TEST_CASE("Tangent-spiral-curve junction is C2 when radii match", "[req481][req482][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 100.0));
  REQUIRE(a.AppendSpiral(100.0, 0.0, 500.0, true));
  REQUIRE(a.AppendCurve(500.0, 50.0, true));
  const auto issues = a.CheckContinuity();
  REQUIRE(issues.size() == 2);
  CHECK(issues[0].c0);
  CHECK(issues[0].c1);
  CHECK(issues[0].c2);
  CHECK(issues[1].c0);
  CHECK(issues[1].c1);
  CHECK(issues[1].c2);
  a.continuityRequirement = alignment::ContinuityRequirement::C2;
  CHECK_FALSE(issues[1].FailsRequirement(a.continuityRequirement));
}

TEST_CASE("Spiral geometry points TS SC CS ST", "[req490][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 100.0));
  REQUIRE(a.AppendSpiral(100.0, 0.0, 500.0, true));
  REQUIRE(a.AppendCurve(500.0, 50.0, true));
  using C = alignment::GeometryPointCode;
  int ts = 0;
  int sc = 0;
  Point2 tsPt{};
  Point2 scPt{};
  for (const alignment::GeometryPoint& p : a.GeometryPoints()) {
    if (p.code == C::TanSpiral) {
      ++ts;
      tsPt = p.point;
    }
    if (p.code == C::SpiralCurve) {
      ++sc;
      scPt = p.point;
    }
  }
  CHECK(ts == 1);
  CHECK(sc == 1);
  CHECK(tsPt.x == Approx(0.0).margin(1e-9));
  CHECK(tsPt.y == Approx(100.0).margin(1e-9));
  const Point2 spiralEnd = a.GetPointAtStation(a.EntityEndStation(1));
  CHECK(scPt.x == Approx(spiralEnd.x).margin(1e-8));
  CHECK(scPt.y == Approx(spiralEnd.y).margin(1e-8));

  Alignment stAlign;
  REQUIRE(stAlign.AppendTangent({0.0, 0.0}, 0.0, 80.0));
  REQUIRE(stAlign.AppendSpiral(60.0, 0.0, 600.0, true));
  REQUIRE(stAlign.AppendTangent(40.0));
  int st = 0;
  for (const alignment::GeometryPoint& p : stAlign.GeometryPoints()) {
    if (p.code == C::SpiralTan)
      ++st;
  }
  CHECK(st == 1);

  Alignment ss;
  REQUIRE(ss.AppendTangent({0.0, 0.0}, 0.0, 20.0));
  REQUIRE(ss.AppendSpiral(50.0, 0.0, 800.0, true));
  REQUIRE(ss.AppendSpiral(50.0, 800.0, 400.0, true));
  int spiralSpiral = 0;
  for (const alignment::GeometryPoint& p : ss.GeometryPoints()) {
    if (p.code == C::SpiralSpiral)
      ++spiralSpiral;
  }
  CHECK(spiralSpiral == 1);
}

TEST_CASE("Spiral-curve C2 mismatch is flagged at C2 level", "[req482][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 0.0, 50.0));
  REQUIRE(a.AppendSpiral(100.0, 0.0, 500.0, true));
  REQUIRE(a.AppendCurve(400.0, 40.0, true));  // end radius 500 vs curve 400
  const alignment::ContinuityIssue& j = a.CheckContinuity().back();
  CHECK(j.c0);
  CHECK(j.c1);
  CHECK_FALSE(j.c2);
  CHECK(j.curvatureGap == Approx(0.0005).margin(1e-12));
  a.continuityRequirement = alignment::ContinuityRequirement::C1;
  CHECK_FALSE(j.FailsRequirement(a.continuityRequirement));
  a.continuityRequirement = alignment::ContinuityRequirement::C2;
  CHECK(j.FailsRequirement(a.continuityRequirement));
}
