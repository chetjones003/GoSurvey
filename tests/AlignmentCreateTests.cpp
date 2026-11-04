// REQ-485 / GitHub issue #121: create alignments by layout (PIs, curves, spirals) and from objects (picking, Reverse,
// erase, refusals). Expected values are hand-computed from closed forms, and the layout case repeats what Civil 3D
// 2026 built for the same PIs (build/artifacts/panel-485, task TASK-alignment-create-issue121).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "alignment/Alignment.hpp"

using alignment::Alignment;
using alignment::Entity;
using alignment::Point2;
using Catch::Approx;

namespace {

constexpr double kDeg = alignment::kPi / 180.0;

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

bool LogHas(const std::vector<std::string>& log, const char* needle) {
  for (const std::string& line : log) {
    if (line.find(needle) != std::string::npos) return true;
  }
  return false;
}

std::string Joined(const std::vector<std::string>& log) {
  std::string out;
  for (const std::string& l : log) out += l + " | ";
  return out;
}

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  return st;
}

void PushLine(AppCommandState& st, double x0, double y0, double z0, double x1, double y1, double z1) {
  for (double v : {x0, y0, z0, x1, y1, z1}) st.userLinesFlat.push_back(v);
  st.userLineAttrs.push_back(EntityAttributes{});
}

void Select(AppCommandState& st, SelectedEntity::Type type, int index) {
  SelectedEntity se;
  se.type = type;
  se.index = index;
  st.selection.push_back(se);
}

/// A polyline: (0,0) -> (100,0) -> (200,0), a CCW quarter circle R100 to (300,100), then (300,300). Three line
/// segments and one bulge segment (REQ-485 acceptance 3).
void PushPolyline(AppCommandState& st, double z = 0.0) {
  const double b = std::tan(90.0 * kDeg / 4.0);
  st.userPolylineOffsets = {0, 5};
  st.userPolylineVerts = {0, 0, z, 100, 0, z, 200, 0, z, 300, 100, z, 300, 300, z};
  st.userPolylineVertsBulge = {0.f, 0.f, static_cast<float>(b), 0.f, 0.f};
  st.userPolylineVertsNormal.clear();
  for (int i = 0; i < 5; ++i) {
    for (float v : {0.f, 0.f, 1.f}) st.userPolylineVertsNormal.push_back(v);
  }
  st.userPolylineClosed = {0};
  st.userPolylineAttrs = {EntityAttributes{}};
}

void CheckSmooth(const Alignment& a) {
  for (const alignment::ContinuityIssue& c : a.CheckContinuity()) {
    INFO("after entity " << c.afterEntity);
    CHECK(c.c0);
    CHECK(c.c1);
  }
}

}  // namespace

// ------------------------------------------------------------------------------------------------ by layout (domain)

TEST_CASE("Layout without curves: one Fixed tangent per PI-to-PI leg", "[req485][alignment]") {
  alignment::CurveSettings s;
  s.addCurves = false;
  Alignment a;
  std::string err;
  REQUIRE(alignment::BuildFromPis({{0, 0}, {0, 500}, {400, 800}, {900, 800}}, s, &a, &err));
  REQUIRE(a.Entities().size() == 3);
  CHECK(a.Entities()[0].length == Approx(500.0));
  CHECK(a.Entities()[1].length == Approx(500.0));
  CHECK(a.Entities()[2].length == Approx(500.0));
  for (const Entity& e : a.Entities()) CHECK(e.constraint == Entity::Constraint::Fixed);
  CHECK(a.EndPoint().x == Approx(900.0));
}

TEST_CASE("Layout with curves matches Civil 3D's Tangent-Tangent (With curves) at R 200", "[req485][alignment]") {
  // Civil 3D 2026, same PIs, default radius 200: tangent 400, curve 185.46, tangent 333.33, curve 128.70,
  // tangent 433.33. By hand: T = R tan(delta/2) = 100 (delta 53.13 deg) and 66.67 (delta 36.87 deg).
  alignment::CurveSettings s;
  s.radius = 200.0;
  Alignment a;
  std::string err;
  std::vector<std::string> warn;
  REQUIRE(alignment::BuildFromPis({{0, 0}, {0, 500}, {400, 800}, {900, 800}}, s, &a, &err, &warn));
  REQUIRE(a.Entities().size() == 5);
  CHECK(warn.empty());
  const std::vector<Entity>& e = a.Entities();
  CHECK(e[0].length == Approx(400.0).margin(1e-6));
  CHECK(e[1].kind == alignment::EntityKind::Arc);
  CHECK(e[1].radius == Approx(200.0));
  CHECK(e[1].right);
  CHECK(e[1].length == Approx(200.0 * std::atan2(4.0, 3.0)).margin(1e-6));  // 185.459
  CHECK(e[2].length == Approx(1000.0 / 3.0).margin(1e-6));
  CHECK(e[3].length == Approx(200.0 * (90.0 * kDeg - std::atan2(4.0, 3.0))).margin(1e-6));  // 128.700
  CHECK(e[4].length == Approx(1300.0 / 3.0).margin(1e-6));
  CHECK(e[1].constraint == Entity::Constraint::Free);
  CHECK(e[0].constraint == Entity::Constraint::Fixed);
  CHECK(a.EndPoint().x == Approx(900.0).margin(1e-6));
  CHECK(a.EndPoint().y == Approx(800.0).margin(1e-6));
  CheckSmooth(a);
}

TEST_CASE("Layout with spirals builds spiral-curve-spiral that lands on the next tangent", "[req485][alignment]") {
  // R 300, Ls 100 in and out at a 90 deg corner. Clothoid series: p = Ls^2/(24R) - Ls^4/(2688R^3) = 1.3884,
  // k = Ls/2 - Ls^3/(240R^2) = 49.9537, T = (R + p) tan 45 + k = 351.342.
  alignment::CurveSettings s;
  s.radius = 300.0;
  s.spiralInLength = 100.0;
  s.spiralOutLength = 100.0;
  Alignment a;
  std::string err;
  REQUIRE(alignment::BuildFromPis({{0, 0}, {0, 1000}, {1000, 1000}}, s, &a, &err));
  REQUIRE(a.Entities().size() == 5);
  CHECK(a.Entities()[1].kind == alignment::EntityKind::Spiral);
  CHECK(a.Entities()[2].kind == alignment::EntityKind::Arc);
  CHECK(a.Entities()[3].kind == alignment::EntityKind::Spiral);
  CHECK(a.Entities()[0].length == Approx(1000.0 - 351.342).margin(0.01));
  CHECK(a.Entities()[2].length == Approx(300.0 * (90.0 * kDeg - 100.0 / 300.0)).margin(1e-6));
  CHECK(a.EndPoint().x == Approx(1000.0).margin(1e-6));
  CHECK(a.EndPoint().y == Approx(1000.0).margin(1e-6));
  CHECK(a.EndAzimuth() == Approx(90.0 * kDeg).margin(1e-9));
  CheckSmooth(a);

  s.spiralInLength = 60.0;  // unequal spirals: the curve group still ends exactly on the ahead tangent
  s.spiralOutLength = 120.0;
  REQUIRE(alignment::BuildFromPis({{0, 0}, {0, 1000}, {1000, 1000}}, s, &a, &err));
  CHECK(a.Entities()[3].length == Approx(120.0));
  const Entity& last = a.Entities().back();
  CHECK(last.start.y == Approx(1000.0).margin(1e-6));
  CHECK(a.EndPoint().x == Approx(1000.0).margin(1e-6));
  CheckSmooth(a);
}

TEST_CASE("Layout leaves a corner too tight for the radius as a PI and says so", "[req485][alignment]") {
  alignment::CurveSettings s;
  s.radius = 200.0;  // T = 200 at 90 deg, but the tangents are 100 long
  Alignment a;
  std::string err;
  std::vector<std::string> warn;
  REQUIRE(alignment::BuildFromPis({{0, 0}, {0, 100}, {100, 100}}, s, &a, &err, &warn));
  CHECK(a.Entities().size() == 2);
  REQUIRE(warn.size() == 1);
  CHECK(warn[0].find("PI 2") != std::string::npos);
  CHECK(warn[0].find("too tight") != std::string::npos);

  s.spiralInLength = 700.0;  // theta = Ls / 2R = 1.75 rad uses more than the 90 deg deflection: no curve
  REQUIRE(alignment::BuildFromPis({{0, 0}, {0, 2000}, {2000, 2000}}, s, &a, &err, &warn));
  CHECK(a.CurveCount() == 0);

  CHECK_FALSE(alignment::BuildFromPis({{5, 5}, {5, 5}}, s, &a, &err));  // repeated points are one point
  CHECK(err.find("two distinct points") != std::string::npos);
  s.radius = -1.0;
  CHECK_FALSE(alignment::BuildFromPis({{0, 0}, {0, 10}, {10, 10}}, s, &a, &err));
  CHECK(a.Empty());
}

// ------------------------------------------------------------------------------------------------ from objects (domain)

TEST_CASE("Create from objects refuses self-overlapping and branching objects", "[req485][alignment]") {
  alignment::SourceSegment a, b;
  a.a = {0, 0};
  a.b = {100, 0};
  a.sourceIndex = 1;
  b.a = {50, 0};  // runs back over a
  b.b = {150, 0};
  b.sourceIndex = 2;
  Alignment out;
  std::string err;
  CHECK_FALSE(alignment::BuildFromSegments({a, b}, {}, &out, &err));
  CHECK(err.find("objects 1 and 2 overlap for 50") != std::string::npos);

  alignment::SourceSegment arc1, arc2;  // two arcs on one circle, 0..90 and 45..135 degrees
  arc1.isArc = arc2.isArc = true;
  arc1.center = arc2.center = {0, 0};
  arc1.radius = arc2.radius = 100.0;
  arc1.startAngle = 0.0;
  arc1.endAngle = 90.0 * kDeg;
  arc2.startAngle = 45.0 * kDeg;
  arc2.endAngle = 135.0 * kDeg;
  arc1.sourceIndex = 3;
  arc2.sourceIndex = 4;
  CHECK_FALSE(alignment::BuildFromSegments({arc1, arc2}, {}, &out, &err));
  CHECK(err.find("objects 3 and 4 overlap") != std::string::npos);

  alignment::SourceSegment c;  // a Y: three lines from one point
  c.a = {100, 0};
  c.b = {200, 100};
  c.sourceIndex = 5;
  b.a = {100, 0};
  b.b = {200, -100};
  CHECK_FALSE(alignment::BuildFromSegments({a, b, c}, {}, &out, &err));
  CHECK(err.find("branch") != std::string::npos);
}

TEST_CASE("Create from objects shrinks a fillet that is too big, as Civil 3D does", "[req485][alignment]") {
  // Civil 3D 2026 AddFromPolyline with curves at its 200 ft default (task log §3): tangents L1, L2 meeting at the given
  // deflection; T is capped at 40 % of the shorter tangent and the radius follows from T = R tan(delta/2).
  struct Case {
    double l1, l2, deflectionDeg, radius;
  };
  const std::array<Case, 7> cases{{{100, 300, 90, 40.0}, {100, 100, 60, 69.282}, {500, 500, 90, 200.0},
                                   {300, 300, 30, 200.0}, {300, 300, 90, 120.0}, {250, 600, 90, 100.0},
                                   {201, 1000, 90, 80.4}}};
  for (const Case& c : cases) {
    INFO(c.l1 << " / " << c.l2 << " at " << c.deflectionDeg);
    alignment::SourceSegment a, b;
    a.a = {0, 0};
    a.b = {0, c.l1};
    b.a = a.b;
    b.b = {c.l2 * std::sin(c.deflectionDeg * kDeg), c.l1 + c.l2 * std::cos(c.deflectionDeg * kDeg)};
    alignment::FromObjectsOptions opt;
    opt.addFillets = true;
    opt.filletRadius = 200.0;
    Alignment out;
    std::string err;
    std::vector<std::string> warn;
    REQUIRE(alignment::BuildFromSegments({a, b}, opt, &out, &err, &warn));
    REQUIRE(out.Entities().size() == 3);
    CHECK(out.Entities()[1].radius == Approx(c.radius).margin(1e-3));
    const double t = c.radius * std::tan(c.deflectionDeg * kDeg / 2.0);
    CHECK(out.Entities()[0].length == Approx(c.l1 - t).margin(1e-3));
    CHECK(out.Entities()[2].length == Approx(c.l2 - t).margin(1e-3));
    CHECK(warn.size() == (c.radius < 200.0 ? 1u : 0u));
    CheckSmooth(out);
  }
}

TEST_CASE("Create from objects joins gaps within the tolerance", "[req485][alignment]") {
  alignment::SourceSegment a, b;
  a.a = {0, 0};
  a.b = {100, 0};
  b.a = {100.0005, 0.0003};  // inside the default 0.001
  b.b = {200, 0};
  Alignment out;
  std::string err;
  REQUIRE(alignment::BuildFromSegments({a, b}, {}, &out, &err));
  for (const alignment::ContinuityIssue& c : out.CheckContinuity()) CHECK(c.c0);

  b.a = {100.5, 0};  // half a unit: refused at the default, joined with TOL 1.0
  CHECK_FALSE(alignment::BuildFromSegments({a, b}, {}, &out, &err));
  alignment::FromObjectsOptions opt;
  opt.gapTolerance = 1.0;
  REQUIRE(alignment::BuildFromSegments({a, b}, opt, &out, &err));
  CHECK(out.Entities()[1].start.x == Approx(100.0));
  CHECK(out.EndPoint().x == Approx(200.0));
}

// ------------------------------------------------------------------------------------------------ commands

TEST_CASE("ALIGNMENT takes the Create Alignment dialog's fields (REQ-485 acceptance 1)", "[req485][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT Main Road, Two lane road, 5+00, C-ROAD, Standard, TYPE=Rail, LABELSET=Standard, SPEED=40", log);
  INFO(Joined(log));
  REQUIRE(st.alignments.size() == 1);
  const alignment::Alignment& a = st.alignments[0];
  CHECK(a.type == alignment::AlignmentType::Rail);
  CHECK(a.layer == "C-ROAD");
  REQUIRE(a.designSpeeds.size() == 1);
  CHECK(a.designSpeeds[0].speed == Approx(40.0));
  log.clear();
  Submit(st, "ALIGNMENTLIST Main Road", log);
  CHECK(LogHas(log, "ALIGNMENT: Main Road  [Rail, layer C-ROAD, style Standard]"));
  CHECK(LogHas(log, "Description:   Two lane road"));

  log.clear();
  Submit(st, "ALIGNMENT Other, TYPE=Road", log);  // not a type: refused, nothing made
  CHECK(LogHas(log, "not an alignment type"));
  Submit(st, "ALIGNMENT Other, STYLE=Nope", log);
  CHECK(LogHas(log, "no alignment style named \"Nope\""));
  CHECK(st.alignments.size() == 1);

  st.createAlignmentDialogAvailable = true;  // the Qt editor: no arguments opens the dialog
  log.clear();
  Submit(st, "CREATEALIGNMENT", log);
  CHECK(st.createAlignmentDialogRequested);
  CHECK(NextAlignmentName(st) == "Alignment - (1)");
}

TEST_CASE("ALIGNMENTLAYOUT lays out PIs with curves, undoes a PI and is one undo step", "[req485][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT Road", log);
  Submit(st, "ALIGNMENTCURVESETTINGS RADIUS=200", log);
  CHECK(EffectiveAlignmentCurveRadius(st) == Approx(200.0));
  Submit(st, "ALIGNMENTLAYOUT Road, CURVES", log);
  REQUIRE(st.active == AppCommandState::Kind::AlignmentLayout);
  Submit(st, "0,0", log);
  REQUIRE(SubmitAlignmentCommandPoint(st, 0.0, 500.0, log));  // a click
  Submit(st, "400,800", log);
  Submit(st, "@600,0", log);  // relative to the last PI
  INFO(Joined(log));
  CHECK(st.alignments[0].Entities().size() == 5);
  Submit(st, "U", log);
  CHECK(st.alignments[0].Entities().size() == 3);
  Submit(st, "900,800", log);
  Submit(st, "", log);  // Enter finishes
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(st.alignments[0].Entities().size() == 5);
  CHECK(st.alignments[0].Length() == Approx(400.0 + 1000.0 / 3.0 + 1300.0 / 3.0 + 200.0 * 90.0 * kDeg).margin(1e-6));
  CHECK(st.alignmentLayoutToolsName == "Road");

  Submit(st, "ALIGNMENTLAYOUT Road, NOCURVES", log);  // continues from the end
  Submit(st, "900,1200", log);
  Submit(st, "", log);
  CHECK(st.alignments[0].Entities().size() == 6);
  Submit(st, "UNDO", log);  // the second session
  CHECK(st.alignments[0].Entities().size() == 5);
  Submit(st, "UNDO", log);  // the first session
  CHECK(st.alignments[0].Empty());
}

TEST_CASE("ALIGNMENTLAYOUT keeps the starting direction from the dialog", "[req485][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT Ramp", log);
  Submit(st, "ALIGNMENTLAYOUT Ramp, NOCURVES, FROM=100 100, DIRECTION=90", log);
  REQUIRE(st.alignmentLayout.pis.size() == 1);
  Submit(st, "300,140", log);  // projected onto the eastward ray from (100, 100)
  Submit(st, "", log);
  REQUIRE(st.alignments[0].Entities().size() == 1);
  CHECK(st.alignments[0].EndPoint().x == Approx(300.0));
  CHECK(st.alignments[0].EndPoint().y == Approx(100.0));
  CHECK(st.alignments[0].StartAzimuth() == Approx(90.0 * kDeg));
}

TEST_CASE("CREATEALIGNMENTFROMOBJECTS: a polyline becomes 3 tangents and 1 curve, erased and undone together",
          "[req485][alignment]") {
  AppCommandState st = Drawing();
  PushPolyline(st);
  Select(st, SelectedEntity::Type::Polyline, 0);
  std::vector<std::string> log;
  Submit(st, "CREATEALIGNMENTFROMOBJECTS FromPoly, START=1+00, ERASE, TYPE=Miscellaneous", log);
  INFO(Joined(log));
  REQUIRE(st.alignments.size() == 1);
  const alignment::Alignment& a = st.alignments[0];
  REQUIRE(a.Entities().size() == 4);  // acceptance 3: 3 tangents + 1 curve, never a vertex approximation
  CHECK(a.CurveCount() == 1);
  CHECK(a.Entities()[2].radius == Approx(100.0).margin(1e-6));
  CHECK(a.type == alignment::AlignmentType::Miscellaneous);
  CHECK(st.userPolylineOffsets.size() <= 1);  // no polyline remains
  CHECK(LogHas(log, "1 source object(s) erased"));

  Submit(st, "UNDO", log);  // acceptance 6: one step removes the alignment and restores the source
  CHECK(st.alignments.empty());
  CHECK(st.userPolylineOffsets.size() == 2);
}

TEST_CASE("CREATEALIGNMENTFROMOBJECTS refuses a 2-unit gap and leaves the drawing unchanged (acceptance 5)",
          "[req485][alignment]") {
  AppCommandState st = Drawing();
  PushLine(st, 0, 0, 0, 0, 100, 0);
  PushLine(st, 0, 100, 0, 100, 100, 0);
  PushLine(st, 102, 100, 0, 200, 100, 0);
  for (int i = 0; i < 3; ++i) Select(st, SelectedEntity::Type::LineSeg, i);
  const size_t undoDepth = st.documents[1].undoStack.size();
  std::vector<std::string> log;
  Submit(st, "CREATEALIGNMENTFROMOBJECTS Gapped, ERASE", log);
  INFO(Joined(log));
  CHECK(st.alignments.empty());
  CHECK(LogHas(log, "objects 2 and 3 are not connected: gap of 2 "));
  CHECK(LogHas(log, "No alignment was created"));
  CHECK(st.userLinesFlat.size() == 18);
  CHECK(st.documents[1].undoStack.size() == undoDepth);
}

TEST_CASE("CREATEALIGNMENTFROMOBJECTS refuses vertical lines, 3D polylines and splines by name", "[req485][alignment]") {
  AppCommandState st = Drawing();
  PushLine(st, 5, 5, 0, 5, 5, 10);
  Select(st, SelectedEntity::Type::LineSeg, 0);
  std::vector<std::string> log;
  Submit(st, "CREATEALIGNMENTFROMOBJECTS Vertical", log);
  CHECK(LogHas(log, "object 1 is a vertical line"));

  st.selection.clear();
  PushPolyline(st);
  st.userPolylineVerts[5] = 7.0;  // one vertex raised: non-planar
  Select(st, SelectedEntity::Type::Polyline, 0);
  log.clear();
  Submit(st, "CREATEALIGNMENTFROMOBJECTS Raised", log);
  CHECK(LogHas(log, "object 1 is a 3D polyline"));

  st.selection.clear();
  Select(st, SelectedEntity::Type::Spline, 0);
  log.clear();
  Submit(st, "CREATEALIGNMENTFROMOBJECTS Curvy", log);
  CHECK(LogHas(log, "object 1 is a spline"));
  CHECK(st.alignments.empty());

  st.selection.clear();
  PushPolyline(st, 12.0);  // a flat polyline at an elevation: converted, and the ignored Z is reported
  Select(st, SelectedEntity::Type::Polyline, 0);
  log.clear();
  Submit(st, "CREATEALIGNMENTFROMOBJECTS Elevated", log);
  CHECK(st.alignments.size() == 1);
  CHECK(LogHas(log, "elevations were ignored"));
}

TEST_CASE("CREATEALIGNMENTFROMOBJECTS picks objects, then Reverse at the prompt (acceptance 6)", "[req485][alignment]") {
  AppCommandState st = Drawing();
  PushLine(st, 0, 0, 0, 0, 100, 0);
  PushLine(st, 0, 100, 0, 100, 100, 0);
  std::vector<std::string> log;
  Submit(st, "CREATEALIGNMENTFROMOBJECTS", log);
  REQUIRE(st.active == AppCommandState::Kind::AlignmentFromObjects);
  REQUIRE(SubmitAlignmentCommandPoint(st, 0.0, 10.0, log));  // the first line, near its (0,0) end
  REQUIRE(SubmitAlignmentCommandPoint(st, 50.0, 100.0, log));
  CHECK_FALSE(SubmitAlignmentCommandPoint(st, 50.0, 100.0, log));  // already selected
  Submit(st, "", log);  // done picking: the direction prompt
  INFO(Joined(log));
  CHECK(LogHas(log, "starts at 0.0000, 0.0000"));
  Submit(st, "R", log);
  CHECK(LogHas(log, "starts at 100.0000, 100.0000"));
  Submit(st, "", log);  // headless: created with the next default name
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].name == "Alignment - (1)");
  CHECK(st.alignments[0].StartPoint().x == Approx(100.0));
  CHECK(st.alignments[0].EndPoint().y == Approx(0.0));
  CHECK(st.userLinesFlat.size() == 12);  // not erased unless asked
  Submit(st, "UNDO", log);
  CHECK(st.alignments.empty());

  st.createAlignmentDialogAvailable = true;  // with the Qt editor the dialog takes over after the direction
  Submit(st, "CREATEALIGNMENTFROMOBJECTS", log);
  REQUIRE(SubmitAlignmentCommandPoint(st, 0.0, 90.0, log));
  Submit(st, "", log);
  Submit(st, "", log);
  CHECK(st.alignmentFromObjectsDialogRequested);
  CHECK(st.selection.size() == 1);
  CHECK(AlignmentFromObjectsPickArgs(st).find(", PICK=0.0000000000 90.0000000000") == 0);
}
