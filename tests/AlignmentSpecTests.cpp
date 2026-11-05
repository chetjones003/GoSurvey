// Issue #121 - acceptance criteria of REQ-480..503 that the per-feature test files did not assert yet. Expected values
// are closed-form, as in AlignmentTests.cpp (REQ-503).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "alignment/Alignment.hpp"

using alignment::Alignment;
using alignment::Point2;
using Catch::Approx;

namespace {

constexpr double kDeg = alignment::kPi / 180.0;

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  std::vector<char> buf(text.begin(), text.end());
  buf.resize(std::max<size_t>(buf.size() + 1, 512), '\0');
  ProcessCommandLineSubmit(buf.data(), buf.size(), st, log);
}

bool LogHas(const std::vector<std::string>& log, const std::string& needle) {
  for (const std::string& line : log)
    if (line.find(needle) != std::string::npos) return true;
  return false;
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  return st;
}

/// 100 000 entities: 10-unit tangents and gentle curves alternating left and right, so the road stays near its line.
Alignment LongRoad() {
  Alignment a;
  REQUIRE(a.AppendTangent({0.0, 0.0}, 90.0 * kDeg, 10.0));
  for (int i = 1; i < 100000; ++i) {
    if (i % 2 == 0) REQUIRE(a.AppendTangent(10.0));
    else REQUIRE(a.AppendCurve(1000.0, 5.0, (i / 2) % 2 == 0));
  }
  return a;
}

}  // namespace

// --- REQ-484 / REQ-499 / REQ-503: performance ---

TEST_CASE("Station lookup on 100 000 entities is a binary search", "[req484][req499][req503][alignment]") {
  const Alignment a = LongRoad();
  REQUIRE(a.Entities().size() == 100000);
  int worst = 0;
  for (int k = 0; k <= 1000; ++k) {
    const double s = a.Length() * k / 1000.0;
    int steps = 0;
    const int i = a.EntityAtStationCounted(s, &steps);
    CHECK(i == a.GetEntityAtStation(s));
    worst = std::max(worst, steps);
  }
  CHECK(worst <= 17);  // ceil(log2(100 000)); a walk would look at up to 100 000
}

TEST_CASE("GetPointAtStation on 100 000 entities takes at most 20 microseconds", "[req484][req499][req503][alignment]") {
  const Alignment a = LongRoad();
  std::vector<double> micros;
  micros.reserve(2001);
  double sink = 0.0;
  for (int k = 0; k <= 2000; ++k) {
    const double s = a.Length() * ((k * 7919) % 2001) / 2000.0;  // spread over the road, not in order
    const auto t0 = std::chrono::steady_clock::now();
    const Point2 p = a.GetPointAtStation(s);
    const auto t1 = std::chrono::steady_clock::now();
    sink += p.x;
    micros.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
  }
  std::nth_element(micros.begin(), micros.begin() + 1000, micros.end());
  INFO("median " << micros[1000] << " us");
  CHECK(std::isfinite(sink));
#ifdef NDEBUG
  CHECK(micros[1000] <= 20.0);  // REQ-484 AC4 is a release-build criterion
#endif
}

// --- REQ-484: query API ---

TEST_CASE("GetStation and GetOffset are the station and offset of GetStationOffset", "[req484][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(a.AppendCurveByDelta(200.0, 30.0 * kDeg, true));
  for (const Point2 p : {Point2{5.0, 40.0}, Point2{-12.0, 90.0}, Point2{60.0, 150.0}}) {
    const alignment::StationOffset so = a.GetStationOffset(p.x, p.y);
    CHECK(a.GetStation(p.x, p.y) == so.internalStation);
    CHECK(a.GetOffset(p.x, p.y) == so.offset);
  }
}

TEST_CASE("Points beyond the ends extrapolate only when asked", "[req484][alignment]") {
  Alignment a;
  a.startingStation = 100.0;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));  // north, stations 1+00 .. 2+00
  const alignment::StationOffset clamped = a.GetStationOffset(2.0, 103.0);
  CHECK(clamped.outOfRange);
  CHECK(clamped.internalStation == Approx(200.0));
  const alignment::StationOffset past = a.GetStationOffset(2.0, 103.0, true);
  CHECK(past.outOfRange);
  CHECK(past.internalStation == Approx(203.0).margin(1e-9));
  CHECK(past.offset == Approx(2.0).margin(1e-9));
  const alignment::StationOffset before = a.GetStationOffset(-1.0, -4.0, true);
  CHECK(before.internalStation == Approx(96.0).margin(1e-9));
  CHECK(before.offset == Approx(-1.0).margin(1e-9));
  const alignment::StationOffset inside = a.GetStationOffset(3.0, 50.0, true);  // in range: unchanged
  CHECK_FALSE(inside.outOfRange);
  CHECK(inside.internalStation == Approx(150.0).margin(1e-9));
}

TEST_CASE("Two equally close places report the lower station and the ambiguity", "[req484][alignment]") {
  Alignment a;  // a hairpin: up x = 0, round R 10, down x = 20
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(a.AppendCurveByDelta(10.0, 180.0 * kDeg, true));
  REQUIRE(a.AppendTangent(100.0));
  const alignment::StationOffset so = a.GetStationOffset(10.0, 50.0);
  CHECK(so.ambiguous);
  CHECK(so.internalStation == Approx(50.0).margin(1e-6));
  CHECK(so.offset == Approx(10.0).margin(1e-6));
  CHECK_FALSE(a.GetStationOffset(3.0, 50.0).ambiguous);
}

TEST_CASE("A point on a curve's radial extension measures to the curve in both directions", "[req484][alignment]") {
  for (const bool right : {true, false}) {
    Alignment a;
    REQUIRE(a.AppendTangent({0, 0}, 0.0, 10.0));
    REQUIRE(a.AppendCurveByDelta(100.0, 90.0 * kDeg, right));
    const double s = 10.0 + 100.0 * 30.0 * kDeg;
    const Point2 foot = a.GetPointAtStation(s);
    const double az = a.GetBearingAtStation(s);
    const double outward = right ? -1.0 : 1.0;  // away from the centre: left of a right curve, right of a left one
    const Point2 p{foot.x + outward * 25.0 * std::cos(az), foot.y - outward * 25.0 * std::sin(az)};
    const alignment::StationOffset so = a.GetStationOffset(p.x, p.y);
    INFO((right ? "right" : "left") << " curve");
    CHECK(so.internalStation == Approx(s).margin(1e-6));
    CHECK(so.offset == Approx(outward * 25.0).margin(1e-6));
    CHECK(so.entityIndex == 1);
  }
}

TEST_CASE("GETSTATIONOFFSET answers the issue's example point in the displayed station format", "[req484][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT Main, , 10+00", log);
  Submit(st, "ALIGNMENTADD Main, TANGENT, 900, 2010, 90, 500", log);  // east from (900, 2010), stations 10+00 ..
  log.clear();
  Submit(st, "GETSTATIONOFFSET Main, 1000, 2000", log);  // the issue's X = 1000, Y = 2000: 100 along, 10 right
  CHECK(LogHas(log, "Station = 11+00.00"));
  CHECK(LogHas(log, "(right)"));
}

TEST_CASE("GETSTATIONOFFSET reads the ground elevation from a chosen surface", "[req484][req497][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT G1, , 0+00", log);
  Submit(st, "ALIGNMENTADD G1, TANGENT, 0, 5, 90, 100", log);
  std::string z;
  for (int j = 0; j < 2; ++j)
    for (int i = 0; i < 11; ++i) {
      char buf[32];
      std::snprintf(buf, sizeof buf, ", %g", 0.1 * 10.0 * i + 50.0);  // Z = 0.1 X + 50
      z += buf;
    }
  Submit(st, "SURFACECREATEGRID PLANE, 0, 0, 10, 10, 11, 2" + z, log);
  REQUIRE(st.cadSurfaces.size() == 1);
  log.clear();
  Submit(st, "GETSTATIONOFFSET G1, 40, 7, SURFACE=PLANE", log);
  CHECK(LogHas(log, "Elevation = 54"));
  log.clear();
  Submit(st, "GETSTATIONOFFSET G1, 40, 70, SURFACE=PLANE", log);  // 65 left of the road, off the surface
  CHECK(LogHas(log, "off the surface \"PLANE\""));
  log.clear();
  Submit(st, "GETSTATIONOFFSET G1, 40, 7, SURFACE=NOSUCH", log);
  CHECK(LogHas(log, "no surface named \"NOSUCH\""));
}

// --- REQ-497 AC2: a station the surface does not cover is no-data, with a message ---

TEST_CASE("ALIGNMENTSURFACE reports a station off the surface as no ground", "[req497][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT G2, , 0+00", log);
  Submit(st, "ALIGNMENTADD G2, TANGENT, 0, 5, 90, 300", log);  // runs past the 0..100 surface
  std::string z;
  for (int j = 0; j < 2; ++j)
    for (int i = 0; i < 11; ++i) z += ", 50";
  Submit(st, "SURFACECREATEGRID FLAT, 0, 0, 10, 10, 11, 2" + z, log);
  log.clear();
  Submit(st, "ALIGNMENTSURFACE G2, FLAT, STATION=2+00", log);
  CHECK(LogHas(log, "no ground: the point is off the surface"));
  CHECK_FALSE(LogHas(log, "EG = 0"));
}

// --- REQ-481 / REQ-486: constraints and refusals ---

namespace {
/// Tangent / Free curve R 200 / tangent, from PIs (0,0) (0,500) (400,800).
Alignment FreeCurveRoad() {
  alignment::CurveSettings s{true, 200.0, 0.0, 0.0};
  Alignment a;
  std::string why;
  REQUIRE(alignment::BuildFromPis({{0, 0}, {0, 500}, {400, 800}}, s, &a, &why));
  REQUIRE(a.Entities()[1].constraint == alignment::Entity::Constraint::Free);
  return a;
}
}  // namespace

TEST_CASE("A Free curve's refusals name the curve and both tangents", "[req486][alignment]") {
  Alignment a = FreeCurveRoad();
  std::string why;
  CHECK_FALSE(a.EditEntity(1, alignment::EditParam::CurveLength, 50.0, &why));
  CHECK(why.find("entity 2 is a Free curve between entity 1 and entity 3") != std::string::npos);
  CHECK_FALSE(a.EditEntity(1, alignment::EditParam::CurveDelta, 0.5, &why));
  CHECK(why.find("between entity 1 and entity 3") != std::string::npos);
  CHECK_FALSE(a.EditEntity(1, alignment::EditParam::CurveDirection, 0.0, &why));
  CHECK(why.find("between entity 1 and entity 3") != std::string::npos);
}

TEST_CASE("Free and floating violations are refused with a named message", "[req481][alignment]") {
  const double free = static_cast<double>(alignment::Entity::Constraint::Free);
  std::string why;
  {  // a tangent cannot be Free
    Alignment a = FreeCurveRoad();
    CHECK_FALSE(a.EditEntity(0, alignment::EditParam::Constraint, free, &why));
    CHECK(why.find("entity 1") != std::string::npos);
  }
  {  // a Free curve needs a tangent on each side: one at the end has fewer than two neighbours
    Alignment a;
    REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));
    REQUIRE(a.AppendCurveByDelta(200.0, 20.0 * kDeg, true));
    CHECK_FALSE(a.EditEntity(1, alignment::EditParam::Constraint, free, &why));
    INFO(why);
    CHECK(why.find("entity 2") != std::string::npos);
  }
  {  // free attached to free: two Free curves in a row
    Alignment a;
    REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));
    REQUIRE(a.AppendCurveByDelta(200.0, 20.0 * kDeg, true));
    REQUIRE(a.AppendCurveByDelta(300.0, 20.0 * kDeg, true));
    REQUIRE(a.AppendTangent(100.0));
    CHECK_FALSE(a.EditEntity(1, alignment::EditParam::Constraint, free, &why));
    CHECK(why.find("entity 2") != std::string::npos);
  }
  {  // a tangent that follows the Free curve after it would float on a Free entity
    Alignment a = FreeCurveRoad();
    const double onNext = static_cast<double>(alignment::Entity::Constraint::FloatOnNext);
    CHECK_FALSE(a.EditEntity(0, alignment::EditParam::Constraint, onNext, &why));
    CHECK(why.find("entity 1") != std::string::npos);
    CHECK(why.find("entity 3") != std::string::npos);
  }
}

TEST_CASE("Changing a tangency constraint keeps the overall length", "[req486][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 250.0));
  REQUIRE(a.AppendCurve(500.0, 132.14, true));  // the issue's example
  REQUIRE(a.AppendTangent(300.0));
  const double length = a.Length();
  const Point2 end = a.EndPoint();
  using C = alignment::Entity::Constraint;
  for (const C c : {C::Fixed, C::FloatOnPrev}) {
    REQUIRE(a.EditEntity(2, alignment::EditParam::Constraint, static_cast<double>(c)));
    CHECK(a.Length() == Approx(length).margin(1e-9));
    CHECK(a.EndPoint().x == Approx(end.x).margin(1e-9));
    CHECK(a.EndPoint().y == Approx(end.y).margin(1e-9));
  }
}

TEST_CASE("Deleting an entity that cannot be re-closed marks the gap", "[req486][req482][alignment]") {
  Alignment a;  // three Fixed tangents with corners: removing the middle leaves the ends apart
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(a.AppendTangent({0, 100}, 90.0 * kDeg, 100.0));
  REQUIRE(a.AppendTangent({100, 100}, 0.0, 100.0));
  std::string why;
  REQUIRE(a.DeleteEntity(1, &why));
  const std::vector<alignment::ContinuityIssue> issues = a.CheckContinuity();
  REQUIRE(issues.size() == 1);
  CHECK_FALSE(issues[0].c0);
  CHECK(issues[0].gap == Approx(100.0).margin(1e-9));
}

// --- REQ-481: closed forms ---

TEST_CASE("Curve values to 1e-6, and PC-centre-PT subtends delta", "[req481][alignment]") {
  const double r = 500.0, delta = 15.0 * kDeg;
  const alignment::CurveData d = alignment::ComputeCurveData({0.0, 0.0}, 0.0, r, r * delta, true);
  CHECK(d.tangentLength == Approx(r * std::tan(delta / 2.0)).margin(1e-6));
  CHECK(d.arcLength == Approx(r * delta).margin(1e-6));
  CHECK(d.chord == Approx(2.0 * r * std::sin(delta / 2.0)).margin(1e-6));
  CHECK(d.external == Approx(r * (1.0 / std::cos(delta / 2.0) - 1.0)).margin(1e-6));
  CHECK(d.midOrdinate == Approx(r * (1.0 - std::cos(delta / 2.0))).margin(1e-6));
  const double a0 = std::atan2(d.pc.x - d.center.x, d.pc.y - d.center.y);
  const double a1 = std::atan2(d.pt.x - d.center.x, d.pt.y - d.center.y);
  CHECK(std::fabs(std::remainder(a1 - a0, alignment::kTwoPi)) == Approx(delta).margin(1e-12));
  // PC and PT lie on the back and ahead tangents through the PI.
  CHECK(d.pi.x - d.pc.x == Approx(0.0).margin(1e-6));
  const double aheadAz = delta;
  CHECK((d.pt.x - d.pi.x) * std::cos(aheadAz) - (d.pt.y - d.pi.y) * std::sin(aheadAz) == Approx(0.0).margin(1e-6));
}

TEST_CASE("Clothoid tangent offset and shift match the Fresnel series", "[req481][alignment]") {
  const double r = 500.0, ls = 100.0, theta = ls / (2.0 * r);  // A^2 = 50 000, theta = 0.1
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 50.0));
  REQUIRE(a.AppendSpiral(ls, 0.0, r, true));
  alignment::SpiralData sd;
  REQUIRE(alignment::SpiralDataFor(1, a, &sd));
  const double t2 = theta * theta;
  const double x = ls * (1.0 - t2 / 10.0 + t2 * t2 / 216.0 - t2 * t2 * t2 / 9360.0 + t2 * t2 * t2 * t2 / 685440.0);
  const double y = ls * theta * (1.0 / 3.0 - t2 / 42.0 + t2 * t2 / 1320.0 - t2 * t2 * t2 / 75600.0);
  CHECK(sd.parameterA * sd.parameterA == Approx(50000.0).margin(1e-6));
  CHECK(sd.deflection == Approx(0.1).margin(1e-12));
  REQUIRE(sd.tangentEnd);
  CHECK(sd.totalX == Approx(x).margin(1e-8));
  CHECK(sd.totalY == Approx(y).margin(1e-8));  // tangent offset
  CHECK(sd.p == Approx(y - r * (1.0 - std::cos(theta))).margin(1e-8));  // shift
  CHECK(sd.k == Approx(x - r * std::sin(theta)).margin(1e-8));
  CHECK(sd.longTangent == Approx(x - y / std::tan(theta)).margin(1e-8));
  CHECK(sd.shortTangent == Approx(y / std::sin(theta)).margin(1e-8));
  CHECK(sd.spi.y == Approx(50.0 + sd.longTangent).margin(1e-8));
  CHECK(a.GetCurvatureAtStation(150.0) == Approx(1.0 / r).margin(1e-12));
}

TEST_CASE("Left and right curves have mirrored centres", "[req481][alignment]") {
  Alignment r, l;
  REQUIRE(r.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(r.AppendCurveByDelta(300.0, 40.0 * kDeg, true));
  REQUIRE(l.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(l.AppendCurveByDelta(300.0, 40.0 * kDeg, false));
  alignment::CurveData cr, cl;
  REQUIRE(r.CurveDataFor(1, &cr));
  REQUIRE(l.CurveDataFor(1, &cl));
  CHECK(cr.center.x == Approx(300.0).margin(1e-9));
  CHECK(cl.center.x == Approx(-300.0).margin(1e-9));
  CHECK(cr.center.y == Approx(100.0).margin(1e-9));
  CHECK(cl.center.y == Approx(100.0).margin(1e-9));
}

// --- REQ-482: continuity ---

TEST_CASE("Continuity: tangent to curve is not a C2 finding; gaps name their size; reverse curves are noted",
          "[req482][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(a.AppendCurveByDelta(200.0, 20.0 * kDeg, true));
  REQUIRE(a.AppendCurveByDelta(300.0, 20.0 * kDeg, false));  // reverse
  for (const alignment::ContinuityIssue& c : a.CheckContinuity())
    CHECK_FALSE(c.FailsRequirement(alignment::ContinuityRequirement::C2));  // no spiral at either junction
  const std::vector<alignment::ContinuityIssue> issues = a.CheckContinuity();
  CHECK_FALSE(issues[0].curvatureSignChange);
  CHECK(issues[1].curvatureSignChange);

  AppCommandState st = Drawing();
  st.alignments.push_back(a);
  st.alignments.back().name = "REV";
  Alignment gapped;
  REQUIRE(gapped.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(gapped.AppendTangent({0.001, 100.0}, 0.0, 100.0));  // 0.001 to the side of the end
  gapped.name = "GAP";
  st.alignments.push_back(gapped);
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTLIST REV", log);
  CHECK(LogHas(log, "NOTE: reverse curve after entity 2"));
  CHECK_FALSE(LogHas(log, "WARNING"));
  log.clear();
  Submit(st, "ALIGNMENTLIST GAP", log);
  CHECK(LogHas(log, "WARNING: gap of 0.00"));
  CHECK(LogHas(log, "(C0)"));
}

// --- REQ-483 AC4: equation boundaries 1e-9 apart ---

TEST_CASE("Three equations are exact immediately before, at and after each", "[req483][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 4000.0));
  REQUIRE(a.AddStationEquation({1000.0, 1200.0, true}));   // +200
  REQUIRE(a.AddStationEquation({2000.0, 2100.0, false}));  // back 2200, ahead 2100: -100
  REQUIRE(a.AddStationEquation({3000.0, 3500.0, true}));   // back 3100, ahead 3500: +400
  constexpr double kStep = 1e-9;
  struct Expect { double raw, before, back, ahead, after; };
  for (const Expect e : {Expect{1000.0, 1000.0, 1000.0, 1200.0, 1200.0}, Expect{2000.0, 2200.0, 2200.0, 2100.0, 2100.0},
                         Expect{3000.0, 3100.0, 3100.0, 3500.0, 3500.0}}) {
    INFO("equation at " << e.raw);
    CHECK(a.DisplayedStation(e.raw - kStep) == Approx(e.before - kStep).margin(1e-10));
    CHECK(a.DisplayedStation(e.raw, false) == Approx(e.back).margin(1e-10));
    CHECK(a.DisplayedStation(e.raw, true) == Approx(e.ahead).margin(1e-10));
    CHECK(a.DisplayedStation(e.raw + kStep) == Approx(e.after + kStep).margin(1e-10));
  }
}

// --- REQ-499: precision ---

TEST_CASE("Precision at 1e7 coordinates, near-tangent curves and micro offsets", "[req499][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({1e7, 1e7}, 30.0 * kDeg, 1000.0));
  REQUIRE(a.AppendCurveByDelta(1000.0, 1e-9, true));  // |delta| = 1e-9 rad
  REQUIRE(a.AppendTangent(1e6 - 1000.0));               // the alignment runs to 1e6 units
  CHECK(a.Length() == Approx(1e6).margin(1e-6));
  for (const double sta : {0.0, 500.0, 1000.0 + 5e-7, 250000.0, 999999.0}) {
    for (const double off : {-1e-6, 0.0, 1e-6, 3.5}) {
      const Point2 p = a.GetPointFromStationOffset(sta, off);
      const alignment::StationOffset so = a.GetStationOffset(p.x, p.y);
      INFO("sta " << sta << " off " << off);
      CHECK(std::fabs(so.internalStation - sta) <= 1e-6);
      CHECK(std::fabs(so.offset - off) <= 1e-6);
    }
  }
}

TEST_CASE("Degenerate entities are refused by name", "[req499][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT D", log);
  Submit(st, "ALIGNMENTADD D, TANGENT, 0, 0, 0, 100", log);
  REQUIRE(st.alignments.size() == 1);
  const size_t entities = st.alignments[0].Entities().size();
  for (const char* bad : {"ALIGNMENTADD D, TANGENT, 0", "ALIGNMENTADD D, CURVE, 0, 20, RIGHT",
                          "ALIGNMENTADD D, CURVE, 100, 360, RIGHT"}) {
    log.clear();
    Submit(st, bad, log);
    INFO(bad);
    CHECK(st.alignments[0].Entities().size() == entities);
    CHECK((LogHas(log, "length") || LogHas(log, "radius") || LogHas(log, "delta")));
  }
}

// --- REQ-485 AC1: custom metadata, shown by LIST ---

TEST_CASE("A new alignment keeps a custom name, description, layer, style and starting station", "[req485][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENTSTYLE NEW, Haul", log);
  Submit(st, "ALIGNMENT Haul Road, DESCRIPTION=North pit, LAYER=C-HAUL, STYLE=Haul, START=5+00", log);
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].style == "Haul");
  log.clear();
  Submit(st, "ALIGNMENTLIST Haul Road", log);
  CHECK(LogHas(log, "ALIGNMENT: Haul Road"));
  CHECK(LogHas(log, "layer C-HAUL, style Haul"));
  CHECK(LogHas(log, "Description:   North pit"));
  CHECK(LogHas(log, "Start station: 5+00.00"));
}

// --- REQ-487: whole-object commands, copies, equations ---

namespace {
AppCommandState RoadWithOneAlignment() {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT Road", log);
  Submit(st, "ALIGNMENTADD Road, TANGENT, 0, 0, 90, 100", log);  // east from the origin
  Submit(st, "ALIGNMENTADD Road, CURVE, 200, 30, LEFT", log);
  REQUIRE(st.alignments.size() == 1);
  st.selection = {{SelectedEntity::Type::Alignment, 0}};
  return st;
}
}  // namespace

TEST_CASE("COPY makes an independent alignment with its own id and a new name", "[req487][alignment]") {
  AppCommandState st = RoadWithOneAlignment();
  std::vector<std::string> log;
  for (const char* line : {"COPY", "0,0", "0,500", ""}) Submit(st, line, log);
  REQUIRE(st.alignments.size() == 2);
  CHECK(st.alignments[1].id != st.alignments[0].id);
  CHECK(st.alignments[1].name == "Road (2)");
  CHECK(st.alignments[1].StartPoint().y == Approx(500.0).margin(1e-9));
  CHECK(st.alignments[1].Length() == Approx(st.alignments[0].Length()).margin(1e-9));
  // Names are unique ignoring case, so a copy never sits beside one that differs only by case.
  st.alignments[1].name = "ROAD (3)";
  CHECK(UniqueAlignmentName(st, "road (3)") == "road (3) (2)");
  CHECK(UniqueAlignmentName(st, "Road") == "Road (2)");
}

TEST_CASE("ROTATE, SCALE and MIRROR act on a selected alignment", "[req487][alignment]") {
  {
    AppCommandState st = RoadWithOneAlignment();
    for (double v : {0.0, 0.0, 0.0, 100.0, 0.0, 0.0}) st.userLinesFlat.push_back(v);  // an east line beside it
    st.userLineAttrs.push_back(EntityAttributes{});
    st.selection.push_back({SelectedEntity::Type::LineSeg, 0});
    std::vector<std::string> log;
    for (const char* line : {"ROTATE", "0,0", "90"}) Submit(st, line, log);
    // The alignment turns exactly as a line does (the typed angle follows the drawing's angle settings).
    const double lineAz = std::atan2(st.userLinesFlat[3] - st.userLinesFlat[0], st.userLinesFlat[4] - st.userLinesFlat[1]);
    CHECK(std::fabs(std::remainder(st.alignments[0].StartAzimuth() - lineAz, alignment::kTwoPi)) <= 1e-6);
    CHECK(std::fabs(std::remainder(lineAz - 90.0 * kDeg, alignment::kTwoPi)) == Approx(90.0 * kDeg).margin(1e-6));
    CHECK(st.alignments[0].Entities()[0].length == Approx(100.0).margin(1e-9));
  }
  {
    AppCommandState st = RoadWithOneAlignment();
    std::vector<std::string> log;
    for (const char* line : {"SCALE", "0,0", "2"}) Submit(st, line, log);
    CHECK(st.alignments[0].Entities()[0].length == Approx(200.0).margin(1e-9));
    CHECK(st.alignments[0].Entities()[1].radius == Approx(400.0).margin(1e-9));
  }
  {
    AppCommandState st = RoadWithOneAlignment();
    std::vector<std::string> log;
    for (const char* line : {"MIRROR", "0,0", "100,0", "Y"}) Submit(st, line, log);  // across the X axis, erase source
    REQUIRE_FALSE(st.alignments.empty());
    const alignment::Alignment& m = st.alignments.back();
    CHECK(m.Entities()[1].right);  // the left curve mirrors to a right one
    CHECK(m.Entities()[1].radius == Approx(200.0).margin(1e-9));
  }
}

TEST_CASE("A station equation can be edited, and a bad edit changes nothing", "[req487][req483][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT E", log);
  Submit(st, "ALIGNMENTADD E, TANGENT, 0, 0, 0, 1000", log);
  Submit(st, "ALIGNMENTSTATION E, EQ, ADD, 4+00, 5+00, INC", log);
  REQUIRE(st.alignments[0].equations.size() == 1);
  log.clear();
  Submit(st, "ALIGNMENTSTATION E, EQ, EDIT, 1, 6+00, 6+50, INC", log);
  CHECK(LogHas(log, "equation 1 changed"));
  REQUIRE(st.alignments[0].equations.size() == 1);
  CHECK(st.alignments[0].equations[0].rawBack == Approx(600.0));
  CHECK(st.alignments[0].equations[0].ahead == Approx(650.0));
  log.clear();
  Submit(st, "ALIGNMENTSTATION E, EQ, EDIT, 1, 6+00, 5+00, INC", log);  // increasing but goes down
  CHECK(LogHas(log, "refused"));
  CHECK(st.alignments[0].equations[0].ahead == Approx(650.0));
  Submit(st, "UNDO", log);
  REQUIRE(st.alignments[0].equations.size() == 1);
  CHECK(st.alignments[0].equations[0].rawBack == Approx(400.0));
}

TEST_CASE("Changing the starting station removes equations and design speeds, only when confirmed", "[req483][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT S", log);
  Submit(st, "ALIGNMENTADD S, TANGENT, 0, 0, 0, 1000", log);
  Submit(st, "ALIGNMENTSPEED S, 1+00, 50", log);
  REQUIRE(st.alignments[0].designSpeeds.size() == 1);
  log.clear();
  Submit(st, "ALIGNMENTSTATION S, START, 2+00", log);
  CHECK(LogHas(log, "1 design speed(s)"));
  CHECK(st.alignments[0].startingStation == 0.0);
  Submit(st, "ALIGNMENTSTATION S, START, 2+00, CLEAR", log);
  CHECK(st.alignments[0].startingStation == Approx(200.0));
  CHECK(st.alignments[0].designSpeeds.empty());
}

// --- REQ-488: offsets ---

TEST_CASE("OFFSETALIGNMENT takes counts and increments per side, a name template and a station range", "[req488][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT CL", log);
  Submit(st, "ALIGNMENTADD CL, TANGENT, 0, 0, 90, 500", log);
  Submit(st, "ALIGNMENTADD CL, CURVE, 500, 15, LEFT", log);
  Submit(st, "ALIGNMENTADD CL, TANGENT, 300", log);
  log.clear();
  Submit(st, "OFFSETALIGNMENT CL, LEFT=2x6, RIGHT=1x12, NAME={parent}-{side}-{n}", log);
  REQUIRE(st.alignments.size() == 4);
  CHECK(st.alignments[1].name == "CL-Left-1");
  CHECK(st.alignments[1].offsetDistance == Approx(-6.0));
  CHECK(st.alignments[2].name == "CL-Left-2");
  CHECK(st.alignments[2].offsetDistance == Approx(-12.0));
  CHECK(st.alignments[3].name == "CL-Right-1");
  CHECK(st.alignments[3].offsetDistance == Approx(12.0));
  log.clear();
  Submit(st, "OFFSETALIGNMENT CL, R20, FROM=1+00, TO=6+00", log);
  REQUIRE(st.alignments.size() == 5);
  const alignment::Alignment& part = st.alignments[4];
  CHECK(part.StartStation() == Approx(100.0).margin(1e-9));
  CHECK(part.Entities().size() == 2);  // the tangent from 1+00, and the curve up to 6+00
  const Point2 p = part.GetPointAtStation(part.StartStation());
  CHECK(st.alignments[0].GetStationOffset(p.x, p.y).internalStation == Approx(100.0).margin(1e-6));
  CHECK(st.alignments[0].GetStationOffset(p.x, p.y).offset == Approx(20.0).margin(1e-6));
  // The ranged offset follows its parent and keeps its range.
  Submit(st, "ALIGNMENTEDIT CL, 2, RADIUS, 600", log);
  CHECK(st.alignments[4].Entities()[1].radius == Approx(620.0).margin(1e-9));  // a left curve: right offset is outside
  CHECK(st.alignments[4].StartStation() == Approx(100.0).margin(1e-9));
}

TEST_CASE("An offset station maps to the parent's perpendicular foot", "[req488][alignment]") {
  Alignment parent;
  REQUIRE(parent.AppendTangent({0, 0}, 90.0 * kDeg, 500.0));
  REQUIRE(parent.AppendCurveByDelta(500.0, 15.0 * kDeg, false));  // left: a +12 offset lies outside, R 512
  REQUIRE(parent.AppendTangent(300.0));
  Alignment off;
  std::string why;
  REQUIRE(alignment::MakeOffset(parent, 12.0, &off, &why));
  // On the tangent the station difference is zero; on the curve it scales by R'/R.
  for (const double s : {50.0, 250.0, 499.0}) {
    const Point2 p = off.GetPointAtStation(s);
    CHECK(std::fabs(parent.GetStationOffset(p.x, p.y).internalStation - s) <= 1e-6);
  }
  const double arcStart = parent.EntityStartStation(1);
  for (const double along : {10.0, 60.0, 120.0}) {
    const Point2 p = off.GetPointAtStation(off.EntityStartStation(1) + along);
    const double footAlong = parent.GetStationOffset(p.x, p.y).internalStation - arcStart;
    CHECK(footAlong == Approx(along * 500.0 / 512.0).margin(1e-6));
  }
}

// --- REQ-489: intersections ---

TEST_CASE("ALIGNMENTINTERSECT gives stations on both alignments, reads arcs and places points", "[req489][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT A1", log);
  Submit(st, "ALIGNMENTADD A1, TANGENT, 0, 0, 90, 200", log);  // east along y = 0
  Submit(st, "ALIGNMENT B1, , 1+00", log);
  Submit(st, "ALIGNMENTADD B1, TANGENT, 50, -100, 0, 300", log);  // north along x = 50
  log.clear();
  Submit(st, "ALIGNMENTINTERSECT A1, B1, POINT", log);
  CHECK(LogHas(log, "Station = 0+50.00"));
  CHECK(LogHas(log, "Station on \"B1\" = 2+00.00"));
  CHECK(LogHas(log, "1 point(s) placed"));
  REQUIRE(st.cadPoints.size() == 1);
  CHECK(st.cadPoints[0].x == Approx(50.0).margin(1e-9));
  CHECK(st.cadPoints[0].y == Approx(0.0).margin(1e-9));
  log.clear();
  Submit(st, "ALIGNMENTINTERSECT A1, ARC, 100, 0, 30, 0, 180", log);  // the upper half circle: one crossing at each end
  CHECK(LogHas(log, "2 intersection(s)"));
  CHECK(LogHas(log, "Station = 0+70.00"));
  CHECK(LogHas(log, "Station = 1+30.00"));
  log.clear();
  Submit(st, "ALIGNMENTINTERSECT A1, ARC, 100, 0, 30, 20, 160", log);  // misses the road
  CHECK(LogHas(log, "0 intersection(s)"));
}

TEST_CASE("A tangent through a curve's centre meets it at its analytic stations", "[req489][alignment]") {
  Alignment a;  // tangent 100 north, then a right R 500 curve of 60 degrees; centre at (500, 100)
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(a.AppendCurveByDelta(500.0, 60.0 * kDeg, true));
  const Alignment line = alignment::MakeLineObject({500.0 - 600.0 * std::cos(30.0 * kDeg), 100.0 + 600.0 * std::sin(30.0 * kDeg)},
                                                   {500.0, 100.0});  // along the 30-degree radius into the centre
  const std::vector<alignment::Intersection> hits = alignment::IntersectAlignments(a, line);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].stationA == Approx(100.0 + 500.0 * 30.0 * kDeg).margin(1e-6));
  const Alignment through = alignment::MakeLineObject({500.0 - 700.0 * std::cos(30.0 * kDeg), 100.0 + 700.0 * std::sin(30.0 * kDeg)},
                                                      {500.0 + 700.0 * std::cos(30.0 * kDeg), 100.0 - 700.0 * std::sin(30.0 * kDeg)});
  CHECK(alignment::IntersectAlignments(a, through).size() == 1);  // the far side is past the curve's 60 degrees
}
