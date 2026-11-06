// REQ-487 / GitHub issue #121: alignment grips placed and behaving like Civil 3D's (PI, PC/PT, pass-through midpoint,
// centre, tangent midpoint), reverse carrying labels, speeds and offsets, and the readout during a grip drag. Expected
// values are closed-form; the PI case repeats a drag made in Civil 3D 2026 (task TASK-alignment-grips-issue121).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "alignment/Alignment.hpp"

using alignment::Alignment;
using alignment::Entity;
using alignment::GripKind;
using alignment::Point2;
using Catch::Approx;

namespace {

constexpr double kDeg = alignment::kPi / 180.0;
constexpr double kEps = 1e-6;

/// Civil 3D's Tangent-Tangent (With curves) at R 200 through (0,0), (0,500), (400,800), (900,800).
Alignment Road(bool curves = true) {
  alignment::CurveSettings s;
  s.addCurves = curves;
  s.radius = 200.0;
  Alignment a;
  a.name = "Main Road";
  std::string err;
  REQUIRE(alignment::BuildFromPis({{0, 0}, {0, 500}, {400, 800}, {900, 800}}, s, &a, &err));
  return a;
}

int GripWhich(const Alignment& a, GripKind kind, int entity) {
  for (const alignment::Grip& g : alignment::GripPoints(a))
    if (g.kind == kind && g.entity == entity) return g.which;
  return -1;
}

Point2 GripAt(const Alignment& a, GripKind kind, int entity) {
  for (const alignment::Grip& g : alignment::GripPoints(a))
    if (g.kind == kind && g.entity == entity) return g.point;
  FAIL("no such grip");
  return {};
}

void CheckSmooth(const Alignment& a) {
  for (const alignment::ContinuityIssue& c : a.CheckContinuity()) {
    INFO("after entity " << c.afterEntity);
    CHECK(c.c0);
    CHECK(c.c1);
  }
}

/// Tangent length T = R tan(delta / 2) of the curve between legs a->b and b->c.
double TangentDistance(Point2 a, Point2 b, Point2 c, double r) {
  const double az1 = std::atan2(b.x - a.x, b.y - a.y), az2 = std::atan2(c.x - b.x, c.y - b.y);
  return r * std::tan(std::fabs(std::remainder(az2 - az1, 2.0 * alignment::kPi)) * 0.5);
}

double Leg(Point2 a, Point2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

}  // namespace

TEST_CASE("Grips sit where Civil 3D puts them on a tangent / curve / tangent layout", "[req487][alignment]") {
  const Alignment a = Road();
  const auto grips = alignment::GripPoints(a);
  // Start; per Fixed tangent a midpoint; a PI at each corner; per Free curve PC, PT, midpoint and centre; the end.
  CHECK(grips.size() == 15);
  CHECK(GripAt(a, GripKind::Pi, 0).x == Approx(0.0).margin(kEps));
  CHECK(GripAt(a, GripKind::Pi, 0).y == Approx(500.0).margin(kEps));
  CHECK(GripAt(a, GripKind::Pi, 2).x == Approx(400.0).margin(kEps));
  CHECK(GripAt(a, GripKind::CurvePc, 1).y == Approx(400.0).margin(kEps));  // T = 100 before the PI
  CHECK(GripAt(a, GripKind::TangentMid, 0).y == Approx(200.0).margin(kEps));
  CHECK(GripAt(a, GripKind::TangentEnd, 4).x == Approx(900.0).margin(kEps));
  CHECK(GripWhich(a, GripKind::TangentEnd, 0) < 0);  // the first tangent ends at the PC, which has its own grip

  const Alignment corners = Road(false);  // no curves: a PI grip at each corner instead of the tangent end
  CHECK(GripWhich(corners, GripKind::Pi, 0) > 0);
  CHECK(GripWhich(corners, GripKind::TangentEnd, 0) < 0);
}

TEST_CASE("Dragging a PI moves both tangents; the curves keep R 200 and stay tangent (REQ-487 AC3)", "[req487][alignment]") {
  // Civil 3D 2026: the same road with PI 1 dragged west kept both radii at 200 and re-fitted the curves.
  const Alignment a = Road();
  Alignment out;
  REQUIRE(alignment::DragGrip(a, GripWhich(a, GripKind::Pi, 0), {-100.0, 500.0}, &out));
  REQUIRE(out.Entities().size() == 5);
  const Point2 p0{0, 0}, p1{-100, 500}, p2{400, 800}, p3{900, 800};
  const double t1 = TangentDistance(p0, p1, p2, 200.0), t2 = TangentDistance(p1, p2, p3, 200.0);
  CHECK(out.Entities()[0].length == Approx(Leg(p0, p1) - t1).margin(1e-6));
  CHECK(out.Entities()[2].length == Approx(Leg(p1, p2) - t1 - t2).margin(1e-6));
  CHECK(out.Entities()[4].length == Approx(Leg(p2, p3) - t2).margin(1e-6));
  CHECK(out.Entities()[1].radius == Approx(200.0));
  CHECK(out.Entities()[3].radius == Approx(200.0));
  CHECK(out.StartPoint().x == Approx(0.0).margin(kEps));
  CHECK(out.EndPoint().x == Approx(900.0).margin(kEps));
  CHECK(GripAt(out, GripKind::Pi, 0).x == Approx(-100.0).margin(kEps));
  CheckSmooth(out);

  const Alignment corners = Road(false);  // a plain corner moves the same way
  REQUIRE(alignment::DragGrip(corners, GripWhich(corners, GripKind::Pi, 0), {-100.0, 500.0}, &out));
  CHECK(out.Entities()[0].length == Approx(Leg(p0, p1)).margin(1e-6));
  CHECK(out.Entities()[1].length == Approx(Leg(p1, p2)).margin(1e-6));

  Alignment locked = a;  // a fixed bearing refuses: the drag leaves the shape as it was
  REQUIRE(locked.EditEntity(0, alignment::EditParam::Locks, Entity::kLockBearing));
  REQUIRE(alignment::DragGrip(locked, GripWhich(locked, GripKind::Pi, 0), {-100.0, 500.0}, &out));
  CHECK(out.Entities()[0].length == Approx(400.0).margin(kEps));
}

TEST_CASE("PC, PT, midpoint and centre grips set a Free curve's radius and keep it tangent", "[req487][alignment]") {
  const Alignment a = Road();
  alignment::CurveData cd;
  REQUIRE(a.CurveDataFor(1, &cd));
  const double phi = cd.delta * 0.5;
  Alignment out;
  // PC slid 150 back from the PI: T = 150 -> R = 150 / tan(delta / 2) = 300 (delta / 2 has tan 0.5).
  REQUIRE(alignment::DragGrip(a, GripWhich(a, GripKind::CurvePc, 1), {3.0, 350.0}, &out));
  CHECK(out.Entities()[1].radius == Approx(300.0).margin(1e-6));
  CheckSmooth(out);
  // PT slid 150 along the ahead tangent (bearing 0.8, 0.6) from the PI: the same curve.
  REQUIRE(alignment::DragGrip(a, GripWhich(a, GripKind::CurvePt, 1), {120.0, 590.0}, &out));
  CHECK(out.Entities()[1].radius == Approx(300.0).margin(1e-6));
  CHECK(GripAt(out, GripKind::Pi, 0).y == Approx(500.0).margin(kEps));  // the PI does not move

  const double bx = (cd.center.x - cd.pi.x), by = (cd.center.y - cd.pi.y), bl = std::hypot(bx, by);
  // Midpoint on the bisector where an R 250 curve would pass: external distance R (sec - 1).
  const double e = 250.0 * (1.0 / std::cos(phi) - 1.0);
  REQUIRE(alignment::DragGrip(a, GripWhich(a, GripKind::CurveMid, 1), {cd.pi.x + bx / bl * e, cd.pi.y + by / bl * e}, &out));
  CHECK(out.Entities()[1].radius == Approx(250.0).margin(1e-6));
  // Off the bisector: the curve passes through the point.
  const Point2 through{60.0, 470.0};
  REQUIRE(alignment::DragGrip(a, GripWhich(a, GripKind::CurveMid, 1), through, &out));
  CHECK(std::fabs(out.GetStationOffset(through.x, through.y).offset) < 1e-6);
  CheckSmooth(out);
  // Centre along the bisector at R / cos(delta / 2) from the PI.
  const double c = 320.0 / std::cos(phi);
  REQUIRE(alignment::DragGrip(a, GripWhich(a, GripKind::CurveCentre, 1), {cd.pi.x + bx / bl * c + 5.0, cd.pi.y + by / bl * c}, &out));
  CHECK(out.Entities()[1].radius == Approx(320.0 + 5.0 * (bx / bl) * std::cos(phi)).margin(1e-6));
}

TEST_CASE("The tangent midpoint grip moves a tangent across, keeping its bearing", "[req487][alignment]") {
  const Alignment a = Road();
  const Point2 mid = GripAt(a, GripKind::TangentMid, 2);
  Alignment out;
  // 50 to the right of the middle tangent (bearing 0.8, 0.6; right is 0.6, -0.8), plus a slide along it that is ignored.
  REQUIRE(alignment::DragGrip(a, GripWhich(a, GripKind::TangentMid, 2), {mid.x + 30.0 + 40.0, mid.y - 40.0 + 30.0}, &out));
  CHECK(out.Entities()[2].startAzimuth == Approx(a.Entities()[2].startAzimuth).margin(1e-12));
  const Point2 s = out.Entities()[2].start;
  CHECK(((s.x - a.Entities()[2].start.x) * 0.6 - (s.y - a.Entities()[2].start.y) * 0.8) == Approx(50.0).margin(1e-6));
  CHECK(out.Entities()[1].radius == Approx(200.0));
  CHECK(out.StartPoint().x == Approx(0.0).margin(kEps));
  CHECK(out.EndPoint().x == Approx(900.0).margin(kEps));
  CheckSmooth(out);

  const Alignment corners = Road(false);  // with plain corners the PIs slide along the neighbouring tangents
  const Point2 cm = GripAt(corners, GripKind::TangentMid, 1);
  REQUIRE(alignment::DragGrip(corners, GripWhich(corners, GripKind::TangentMid, 1), {cm.x - 60.0, cm.y + 80.0}, &out));
  CHECK(out.Entities()[1].start.x == Approx(0.0).margin(1e-6));  // still on the first tangent (x = 0)
  CHECK(out.Entities()[2].start.y == Approx(800.0).margin(1e-6));  // and on the last (y = 800)
  for (const alignment::ContinuityIssue& c : out.CheckContinuity()) CHECK(c.c0);
}

TEST_CASE("Reversing carries labels, design speeds and offsets; twice gives the original (REQ-487.2)", "[req487][alignment]") {
  Alignment a = Road();
  a.startingStation = 1000.0;
  alignment::StationOffsetLabel label;
  label.internalStation = 1100.0;
  label.offset = 15.0;
  a.stationLabels = {label};
  a.designSpeeds = {{1000.0, 30.0}, {1400.0, 45.0}};
  const Alignment before = a;
  const Point2 labelAt = a.GetPointFromStationOffset(1100.0, 15.0);
  REQUIRE(a.Reverse());
  const Point2 labelNow = a.GetPointFromStationOffset(a.stationLabels[0].internalStation, a.stationLabels[0].offset);
  CHECK(labelNow.x == Approx(labelAt.x).margin(1e-6));  // the label stays on the ground
  CHECK(labelNow.y == Approx(labelAt.y).margin(1e-6));
  REQUIRE(a.designSpeeds.size() == 2);
  CHECK(a.designSpeeds[0].internalStation == Approx(1000.0));
  CHECK(a.designSpeeds[0].speed == Approx(45.0));  // the far end, where 45 applied, comes first
  CHECK(a.designSpeeds[1].internalStation == Approx(1000.0 + a.Length() - 400.0).margin(1e-6));
  REQUIRE(a.Reverse());
  CHECK(a.stationLabels[0].internalStation == Approx(1100.0).margin(1e-9));
  CHECK(a.stationLabels[0].offset == Approx(15.0));
  CHECK(a.designSpeeds[1].internalStation == Approx(1400.0).margin(1e-9));
  for (size_t i = 0; i < a.Entities().size(); ++i) {
    CHECK(a.Entities()[i].start.x == Approx(before.Entities()[i].start.x).margin(1e-9));
    CHECK(a.Entities()[i].length == Approx(before.Entities()[i].length).margin(1e-9));
  }

  AppCommandState st;  // a linked offset keeps its side on the ground
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT R1", log);
  Submit(st, "ALIGNMENTADD R1, TANGENT, 0, 0, 90, 500", log);
  Submit(st, "ALIGNMENTADD R1, CURVE, 300, 30, LEFT", log);
  Submit(st, "OFFSETALIGNMENT R1, R12", log);
  REQUIRE(st.alignments.size() == 2);
  const Point2 childStart = st.alignments[1].StartPoint();
  Submit(st, "REVERSEALIGNMENT R1", log);
  CHECK(st.alignments[1].offsetDistance == Approx(-12.0));
  CHECK(st.alignments[1].EndPoint().x == Approx(childStart.x).margin(1e-6));
  CHECK(st.alignments[1].EndPoint().y == Approx(childStart.y).margin(1e-6));
}

TEST_CASE("While a grip drags an alignment the readout reports station, offset and bearing on it", "[req487][req501][alignment]") {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT G1", log);
  Submit(st, "ALIGNMENTADD G1, TANGENT, 0, 0, 90, 500", log);
  st.selection = {{SelectedEntity::Type::Alignment, 0}};
  REQUIRE(TryBeginEntityGripAtLocal(st, 250.0f, 0.0f, 2.0f));  // the tangent's midpoint
  ApplyEntityGripPoint(st, 250.0f, -900.0f, 0.f);  // far away: an idle readout would give up at this distance
  const std::string readout = AlignmentCursorReadout(st, 250.0, -900.0, 1.0);
  CHECK(readout.find("G1") != std::string::npos);
  CHECK(readout.find("STA 2+50.00") != std::string::npos);
  CHECK(readout.find("OFF 900") != std::string::npos);
  CHECK(st.alignments[0].StartPoint().y == Approx(-900.0).margin(1e-4));  // the tangent moved with the grip
}
