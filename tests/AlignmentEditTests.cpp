// REQ-486..489 / GitHub issue #121 Phase 2 - constraint editing, transforms, offsets, intersections, and the commands
// that drive them. Expected values are worked out by hand from the geometry.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "alignment/Alignment.hpp"
#include "alignment/AlignmentDesign.hpp"
#include "util/NumFormat.hpp"

using alignment::Alignment;
using alignment::Entity;
using alignment::EditParam;
using alignment::Point2;
using Catch::Approx;

namespace {
constexpr double kDeg = alignment::kPi / 180.0;
constexpr double kEps = 1e-6;

/// Tangent N45E 500 (Fixed) -> right curve R500 delta 15 (FloatOnPrev) -> tangent 200 (FloatOnPrev).
Alignment Chain() {
  Alignment a;
  a.name = "MAIN";
  REQUIRE(a.AppendTangent({1000.0, 2000.0}, 45.0 * kDeg, 500.0));
  REQUIRE(a.AppendCurveByDelta(500.0, 15.0 * kDeg, true));
  REQUIRE(a.AppendTangent(200.0));
  return a;
}

void CheckTangentContinuous(const Alignment& a) {
  for (const alignment::ContinuityIssue& c : a.CheckContinuity()) {
    CHECK(c.c0);
    CHECK(c.c1);
  }
}

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

AppCommandState Drawing() {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  return st;
}

/// The id of the grip of this kind on this entity, or -1 when the alignment offers none.
int GripWhich(const Alignment& a, alignment::GripKind kind, int entity) {
  for (const alignment::Grip& g : alignment::GripPoints(a))
    if (g.kind == kind && g.entity == entity) return g.which;
  return -1;
}
}  // namespace

TEST_CASE("Continuing entities float on what precedes them", "[req486][alignment]") {
  const Alignment a = Chain();
  CHECK(a.Entities()[0].constraint == Entity::Constraint::Fixed);
  CHECK(a.Entities()[1].constraint == Entity::Constraint::FloatOnPrev);
  CHECK(a.Entities()[2].constraint == Entity::Constraint::FloatOnPrev);
}

TEST_CASE("A radius-and-delta curve keeps its delta on a radius edit; later stations move (REQ-486 AC2, D-2026-11-04-f)",
          "[req486][req503][alignment]") {
  Alignment a = Chain();
  REQUIRE_FALSE(a.Entities()[1].definedByLength);
  const double len = a.Length();
  const double lastStart = a.EntityStartStation(2);
  std::string why;
  REQUIRE(a.EditEntity(1, EditParam::CurveRadius, 400.0, &why));
  CHECK(a.Entities()[1].radius == Approx(400.0));
  CHECK(a.Entities()[1].length == Approx(400.0 * 15.0 * kDeg).margin(1e-9));  // delta kept at 15 degrees
  CHECK(a.Length() == Approx(len - 500.0 * 15.0 * kDeg + 400.0 * 15.0 * kDeg).margin(1e-9));
  CHECK(a.EntityStartStation(2) < lastStart - 20.0);  // later stations moved back by the 26.18' the curve lost
  CHECK(a.EndAzimuth() == Approx(60.0 * kDeg).margin(1e-9));  // the turn is unchanged
  CheckTangentContinuous(a);
  alignment::CurveData cd;
  REQUIRE(a.CurveDataFor(1, &cd));
  CHECK(a.Entities()[2].start.x == Approx(cd.pt.x).margin(kEps));
  CHECK(a.Entities()[2].start.y == Approx(cd.pt.y).margin(kEps));
}

TEST_CASE("A radius-and-length curve keeps its length on a radius edit (D-2026-11-04-f)", "[req486][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({1000.0, 2000.0}, 45.0 * kDeg, 500.0));
  REQUIRE(a.AppendCurve(500.0, 130.8996938995747, true));  // CURVELEN: radius and length
  REQUIRE(a.AppendTangent(200.0));
  REQUIRE(a.Entities()[1].definedByLength);
  const double len = a.Length();
  REQUIRE(a.EditEntity(1, EditParam::CurveRadius, 400.0));
  CHECK(a.Length() == Approx(len).margin(1e-9));  // stations after it are unchanged
  CHECK(a.EndAzimuth() == Approx(45.0 * kDeg + 130.8996938995747 / 400.0).margin(1e-9));  // the sweep grew
  CheckTangentContinuous(a);
}

TEST_CASE("How a curve was defined is saved; older files read as radius-and-delta (D-2026-11-04-f)", "[req486][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT D", log);
  Submit(st, "ALIGNMENTADD D, TANGENT, 0, 0, 0, 200", log);
  Submit(st, "ALIGNMENTADD D, CURVELEN, 300, 100, RIGHT", log);
  Submit(st, "ALIGNMENTADD D, TANGENT, 100", log);
  Submit(st, "ALIGNMENTADD D, CURVE, 300, 20, LEFT", log);
  REQUIRE(st.alignments.size() == 1);
  REQUIRE(st.alignments[0].Entities().size() == 4);
  CHECK(st.alignments[0].Entities()[1].definedByLength);
  CHECK_FALSE(st.alignments[0].Entities()[3].definedByLength);
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState loaded = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(loaded, json, loadLog));
  REQUIRE(loaded.alignments.size() == 1);
  CHECK(loaded.alignments[0].Entities()[1].definedByLength);
  CHECK_FALSE(loaded.alignments[0].Entities()[3].definedByLength);
  std::string older = json;
  for (size_t at = older.find("\"dl\""); at != std::string::npos; at = older.find("\"dl\"")) older.replace(at, 4, "\"zz\"");
  AppCommandState loadedOlder = Drawing();
  REQUIRE(LoadGoSurveyFromJsonUtf8(loadedOlder, older, loadLog));
  CHECK_FALSE(loadedOlder.alignments[0].Entities()[1].definedByLength);
}

TEST_CASE("Keeping the delta of a length-locked curve is refused (D-2026-11-04-f)", "[req486][alignment]") {
  Alignment a = Chain();
  const double len = a.Length();
  std::string why;
  REQUIRE(a.EditEntity(1, EditParam::Locks, Entity::kLockLength));
  CHECK_FALSE(a.EditEntity(1, EditParam::CurveRadius, 400.0, &why));
  CHECK(why.find("entity 2") != std::string::npos);
  CHECK(why.find("fixed length") != std::string::npos);
  CHECK(a.Entities()[1].radius == Approx(500.0));
  CHECK(a.Length() == Approx(len).margin(1e-12));
}

TEST_CASE("Editing a curve length or delta moves later stations", "[req486][alignment]") {
  Alignment a = Chain();
  const double len = a.Length();
  REQUIRE(a.EditEntity(1, EditParam::CurveLength, 200.0));
  CHECK(a.Length() == Approx(len - 130.8996938995747 + 200.0).margin(1e-9));
  CheckTangentContinuous(a);
  REQUIRE(a.EditEntity(1, EditParam::CurveDelta, 30.0 * kDeg));  // radius kept
  CHECK(a.Entities()[1].length == Approx(500.0 * 30.0 * kDeg));
  CHECK(a.Entities()[1].radius == Approx(500.0));
  CheckTangentContinuous(a);
}

TEST_CASE("Editing the first bearing swings everything that floats", "[req486][alignment]") {
  Alignment a = Chain();
  const double len = a.Length();
  REQUIRE(a.EditEntity(0, EditParam::LineAzimuth, 90.0 * kDeg));
  CHECK(a.Length() == Approx(len).margin(1e-9));
  CHECK(a.EndAzimuth() == Approx(90.0 * kDeg + 15.0 * kDeg).margin(1e-9));
  CHECK(a.Entities()[1].start.x == Approx(1500.0).margin(kEps));  // due east 500
  CHECK(a.Entities()[1].start.y == Approx(2000.0).margin(kEps));
  CheckTangentContinuous(a);
}

TEST_CASE("A derived (floating) bearing cannot be edited directly", "[req486][alignment]") {
  Alignment a = Chain();
  std::string why;
  CHECK_FALSE(a.EditEntity(2, EditParam::LineAzimuth, 10.0 * kDeg, &why));
  CHECK(why.find("entity 3") != std::string::npos);
  CHECK(why.find("Fixed") != std::string::npos);
  // Making it Fixed first allows the edit, and it then stops following the curve (C1 breaks, reported).
  REQUIRE(a.EditEntity(2, EditParam::Constraint, 0.0));
  REQUIRE(a.EditEntity(2, EditParam::LineAzimuth, 10.0 * kDeg));
  bool kink = false;
  for (const alignment::ContinuityIssue& c : a.CheckContinuity()) kink = kink || !c.c1;
  CHECK(kink);
}

TEST_CASE("Locked parameters refuse an edit that would change them", "[req486][alignment]") {
  Alignment a = Chain();
  std::string why;
  REQUIRE(a.EditEntity(1, EditParam::Locks, Entity::kLockRadius));
  CHECK_FALSE(a.EditEntity(1, EditParam::CurveRadius, 400.0, &why));
  CHECK(why.find("fixed radius") != std::string::npos);
  CHECK(a.Entities()[1].radius == Approx(500.0));  // untouched
  REQUIRE(a.EditEntity(1, EditParam::CurveDelta, 20.0 * kDeg));  // delta keeps the radius, so it is allowed
  REQUIRE(a.EditEntity(1, EditParam::Locks, Entity::kLockRadius | Entity::kLockLength));
  CHECK_FALSE(a.EditEntity(1, EditParam::CurveDelta, 25.0 * kDeg, &why));
  CHECK(why.find("fixed length") != std::string::npos);
  REQUIRE(a.EditEntity(1, EditParam::Locks, 0));
  REQUIRE(a.EditEntity(1, EditParam::CurveRadius, 450.0));
}

TEST_CASE("Invalid edits are refused and change nothing", "[req486][alignment]") {
  Alignment a = Chain();
  const Alignment before = a;
  std::string why;
  CHECK_FALSE(a.EditEntity(1, EditParam::CurveRadius, -5.0, &why));
  CHECK_FALSE(a.EditEntity(1, EditParam::CurveDelta, 400.0 * kDeg, &why));
  CHECK_FALSE(a.EditEntity(0, EditParam::CurveRadius, 100.0, &why));  // a tangent has no radius
  CHECK_FALSE(a.EditEntity(9, EditParam::LineLength, 100.0, &why));
  CHECK_FALSE(a.EditEntity(0, EditParam::LineLength, std::nan(""), &why));
  CHECK_FALSE(a.EditEntity(0, EditParam::Constraint, 1.0, &why));  // the first entity cannot follow anything
  CHECK(a.Length() == Approx(before.Length()));
  CHECK(a.EndPoint().x == Approx(before.EndPoint().x).margin(1e-12));
}

TEST_CASE("A Free curve re-fits between two Fixed tangents", "[req486][req503][alignment]") {
  // North 200 to (0,200), then east 300: the corner PI is (0,200). The arc is a placeholder until it is made Free.
  Alignment a;
  Entity l1;
  l1.start = {0, 0};
  l1.startAzimuth = 0.0;
  l1.length = 200.0;
  REQUIRE(a.AppendRaw(l1));
  Entity arc;
  arc.kind = alignment::EntityKind::Arc;
  arc.start = {0, 150};
  arc.startAzimuth = 0.0;
  arc.radius = 50.0;
  arc.length = 50.0 * alignment::kPi / 2.0;
  arc.right = true;
  REQUIRE(a.AppendRaw(arc));
  Entity l2;
  l2.start = {50, 200};
  l2.startAzimuth = 90.0 * kDeg;
  l2.length = 250.0;
  REQUIRE(a.AppendRaw(l2));
  std::string why;
  REQUIRE(a.EditEntity(1, EditParam::Constraint, 3.0, &why));  // Free
  // Free solved: the tangents trim to the 50-unit tangent length either side of the PI (0,200).
  CHECK(a.Entities()[0].length == Approx(150.0).margin(kEps));
  CHECK(a.Entities()[2].start.x == Approx(50.0).margin(kEps));
  CHECK(a.Entities()[2].length == Approx(250.0).margin(kEps));
  CheckTangentContinuous(a);

  REQUIRE(a.EditEntity(1, EditParam::CurveRadius, 80.0, &why));  // T = 80
  CHECK(a.Entities()[0].length == Approx(120.0).margin(kEps));
  CHECK(a.Entities()[1].length == Approx(80.0 * alignment::kPi / 2.0).margin(kEps));
  CHECK(a.Entities()[2].start.x == Approx(80.0).margin(kEps));
  CHECK(a.Entities()[2].length == Approx(220.0).margin(kEps));
  CHECK(a.EndPoint().x == Approx(300.0).margin(kEps));  // the far end of the second tangent never moved
  CHECK(a.EndPoint().y == Approx(200.0).margin(kEps));
  CheckTangentContinuous(a);

  const Alignment before = a;
  CHECK_FALSE(a.EditEntity(1, EditParam::CurveRadius, 400.0, &why));  // T = 400 > 200 available
  CHECK(why.find("does not fit") != std::string::npos);
  CHECK(a.Entities()[1].radius == Approx(before.Entities()[1].radius));
  // Flipping the corner direction is derived, not chosen: the solver picks the side the tangents turn.
  REQUIRE(a.EditEntity(0, EditParam::LineAzimuth, 180.0 * kDeg, &why) == false);  // a south tangent cannot meet the east one at that PI
}

TEST_CASE("A Free curve between parallel tangents is refused by name", "[req486][alignment]") {
  Alignment a;
  Entity l1;
  l1.start = {0, 0};
  l1.length = 100.0;
  REQUIRE(a.AppendRaw(l1));
  Entity arc;
  arc.kind = alignment::EntityKind::Arc;
  arc.start = {0, 100};
  arc.radius = 50.0;
  arc.length = 10.0;
  REQUIRE(a.AppendRaw(arc));
  Entity l2;
  l2.start = {5, 105};
  l2.length = 100.0;
  REQUIRE(a.AppendRaw(l2));
  std::string why;
  CHECK_FALSE(a.EditEntity(1, EditParam::Constraint, 3.0, &why));
  CHECK(why.find("parallel") != std::string::npos);
  CHECK(a.Entities()[1].constraint == Entity::Constraint::Fixed);
}

TEST_CASE("FloatOnNext follows the entity after it", "[req486][alignment]") {
  Alignment a;
  Entity l1;
  l1.start = {0, 0};
  l1.length = 100.0;
  REQUIRE(a.AppendRaw(l1));
  Entity l2;
  l2.start = {0, 100};
  l2.length = 100.0;
  REQUIRE(a.AppendRaw(l2));
  REQUIRE(a.EditEntity(0, EditParam::Constraint, 2.0));  // entity 1 now follows entity 2
  REQUIRE(a.EditEntity(1, EditParam::LineAzimuth, 90.0 * kDeg));
  // Entity 1 keeps its length, ends where entity 2 starts and points the way entity 2 points (east).
  CHECK(a.Entities()[0].startAzimuth == Approx(90.0 * kDeg));
  CHECK(a.Entities()[0].start.x == Approx(-100.0).margin(kEps));
  CHECK(a.Entities()[0].start.y == Approx(100.0).margin(kEps));
  CheckTangentContinuous(a);
}

TEST_CASE("Insert and delete keep the chain solved", "[req486][alignment]") {
  Alignment a = Chain();
  const double len = a.Length();
  std::string why;
  REQUIRE(a.InsertTangentAfter(0, 50.0, &why));
  REQUIRE(a.Entities().size() == 4);
  CHECK(a.Length() == Approx(len + 50.0).margin(1e-9));
  CheckTangentContinuous(a);
  REQUIRE(a.DeleteEntity(1, &why));  // remove the inserted tangent again
  CHECK(a.Entities().size() == 3);
  CHECK(a.Length() == Approx(len).margin(1e-9));
  CHECK(a.EndPoint().x == Approx(Chain().EndPoint().x).margin(kEps));
  CheckTangentContinuous(a);

  REQUIRE(a.InsertCurveAfter(2, 300.0, 100.0, false, &why));
  CHECK(a.CurveCount() == 2);
  CheckTangentContinuous(a);
  REQUIRE(a.DeleteEntity(0, &why));  // the old first entity: the next one becomes the Fixed start
  CHECK(a.Entities().front().constraint == Entity::Constraint::Fixed);
  CHECK_FALSE(a.DeleteEntity(7, &why));
}

TEST_CASE("Orphaned station equations are dropped when geometry shortens", "[req483][req486][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({0, 0}, 0.0, 1000.0));
  REQUIRE(a.AddStationEquation({900.0, 950.0, true}));
  REQUIRE(a.EditEntity(0, EditParam::LineLength, 500.0));
  CHECK(a.equations.empty());
}

TEST_CASE("Whole-object transforms", "[req487][alignment]") {
  Alignment a;
  REQUIRE(a.AppendTangent({10, 0}, 90.0 * kDeg, 100.0));  // east from (10,0)
  REQUIRE(a.AppendCurveByDelta(50.0, 90.0 * kDeg, true));
  const Point2 endBefore = a.EndPoint();
  Alignment t = a;
  t.Translate(5.0, -3.0);
  CHECK(t.EndPoint().x == Approx(endBefore.x + 5.0).margin(kEps));
  CHECK(t.EndPoint().y == Approx(endBefore.y - 3.0).margin(kEps));

  Alignment r = a;
  r.Rotate(0.0, 0.0, 90.0 * kDeg);  // clockwise: east becomes south
  CHECK(r.StartPoint().x == Approx(0.0).margin(kEps));
  CHECK(r.StartPoint().y == Approx(-10.0).margin(kEps));
  CHECK(r.StartAzimuth() == Approx(180.0 * kDeg));
  CHECK(r.Length() == Approx(a.Length()));

  Alignment m = a;
  REQUIRE(m.Mirror(0.0, 0.0, 1.0, 0.0));  // across the x axis
  CHECK(m.StartPoint().y == Approx(0.0).margin(kEps));
  CHECK(m.Entities()[1].right == false);  // handedness flips
  CHECK(m.EndPoint().x == Approx(endBefore.x).margin(kEps));
  CHECK(m.EndPoint().y == Approx(-endBefore.y).margin(kEps));
  CHECK_FALSE(m.Mirror(2.0, 2.0, 2.0, 2.0));

  Alignment s = a;
  REQUIRE(s.Scale(0.0, 0.0, 2.0));
  CHECK(s.Length() == Approx(2.0 * a.Length()));
  CHECK(s.Entities()[1].radius == Approx(100.0));
  CHECK_FALSE(s.Scale(0.0, 0.0, 0.0));
}

TEST_CASE("Offsets are concentric curves and parallel tangents", "[req488][req503][alignment]") {
  const Alignment parent = Chain();
  Alignment off;
  off.name = "MAIN R12";
  std::string why;
  REQUIRE(alignment::MakeOffset(parent, 12.0, &off, &why));  // 12 to the right
  CHECK(off.type == alignment::AlignmentType::Offset);
  REQUIRE(off.Entities().size() == 3);
  CHECK(off.Entities()[1].radius == Approx(488.0).margin(1e-9));  // right curve: toward the centre
  CHECK(off.Entities()[1].length == Approx(488.0 * 15.0 * kDeg).margin(1e-9));
  alignment::CurveData pc, oc;
  REQUIRE(parent.CurveDataFor(1, &pc));
  REQUIRE(off.CurveDataFor(1, &oc));
  CHECK(oc.center.x == Approx(pc.center.x).margin(kEps));  // concentric
  CHECK(oc.center.y == Approx(pc.center.y).margin(kEps));
  CheckTangentContinuous(off);
  // Every point of the offset is 12 to the right of the parent.
  for (double s : {0.0, 250.0, 500.0, 560.0}) {
    const Point2 p = off.GetPointAtStation(s);
    const alignment::StationOffset so = parent.GetStationOffset(p.x, p.y);
    CHECK(so.offset == Approx(12.0).margin(1e-6));
  }
  Alignment left;
  REQUIRE(alignment::MakeOffset(parent, -12.0, &left, &why));
  CHECK(left.Entities()[1].radius == Approx(512.0).margin(1e-9));
  for (double s : {0.0, 250.0, 520.0}) {
    const Point2 p = left.GetPointAtStation(s);
    CHECK(parent.GetStationOffset(p.x, p.y).offset == Approx(-12.0).margin(1e-6));
  }
}

TEST_CASE("Offsets are refused where they would not be real geometry", "[req488][alignment]") {
  const Alignment parent = Chain();
  Alignment off;
  std::string why;
  CHECK_FALSE(alignment::MakeOffset(parent, 600.0, &off, &why));  // the R500 right curve would collapse
  CHECK(why.find("collapse") != std::string::npos);
  CHECK_FALSE(alignment::MakeOffset(parent, 0.0, &off, &why));

  Alignment kinked;
  REQUIRE(kinked.AppendTangent({0, 0}, 0.0, 100.0));
  Entity e;
  e.start = {0, 100};
  e.startAzimuth = 90.0 * kDeg;
  e.length = 100.0;
  REQUIRE(kinked.AppendRaw(e));
  CHECK_FALSE(alignment::MakeOffset(kinked, 5.0, &off, &why));
  CHECK(why.find("not tangent") != std::string::npos);
  CHECK_FALSE(alignment::MakeOffset(Alignment{}, 5.0, &off, &why));
}

TEST_CASE("Offsets refuse parent alignments that contain spirals", "[req488][alignment]") {
  Alignment parent;
  REQUIRE(parent.AppendTangent({0, 0}, 0.0, 100.0));
  REQUIRE(parent.AppendSpiral(80.0, 0.0, 300.0, true));
  Alignment off;
  std::string why;
  CHECK_FALSE(alignment::MakeOffset(parent, 10.0, &off, &why));
  CHECK(why.find("spiral") != std::string::npos);
}

TEST_CASE("Intersections of tangents, curves and touching circles", "[req489][alignment]") {
  Alignment east, north;
  REQUIRE(east.AppendTangent({0, 0}, 90.0 * kDeg, 100.0));
  REQUIRE(north.AppendTangent({40, -50}, 0.0, 100.0));
  auto hits = alignment::IntersectAlignments(east, north);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].point.x == Approx(40.0).margin(kEps));
  CHECK(hits[0].point.y == Approx(0.0).margin(kEps));
  CHECK(hits[0].stationA == Approx(40.0).margin(kEps));
  CHECK(hits[0].stationB == Approx(50.0).margin(kEps));
  CHECK(hits[0].angle == Approx(90.0 * kDeg).margin(1e-9));

  // A tangent through the centre of an R50 curve meets it twice (only the swept part counts).
  Alignment curve;
  REQUIRE(curve.AppendTangent({0, 0}, 0.0, 10.0));
  REQUIRE(curve.AppendCurveByDelta(50.0, 180.0 * kDeg, true));  // centre (50, 10), exactly a half circle
  const Alignment through = alignment::MakeLineObject({-20, 10}, {120, 10});
  hits = alignment::IntersectAlignments(curve, through);
  CHECK(hits.size() == 2);  // the curve start (0,10), shared with the tangent, and the far end (100,10)

  // Two circles that touch from outside: one tangential hit.
  const Alignment c1 = alignment::MakeCircleObject({0, 0}, 10.0, 0.0, alignment::kPi);
  const Alignment c2 = alignment::MakeCircleObject({20, 0}, 10.0, alignment::kPi, alignment::kPi);
  hits = alignment::IntersectAlignments(c1, c2);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].tangential);
  CHECK(hits[0].point.x == Approx(10.0).margin(1e-6));

  // Overlapping collinear tangents are a range, not points.
  Alignment l1, l2;
  REQUIRE(l1.AppendTangent({0, 0}, 90.0 * kDeg, 100.0));
  REQUIRE(l2.AppendTangent({60, 0}, 90.0 * kDeg, 100.0));
  std::vector<alignment::IntersectionOverlap> overlaps;
  hits = alignment::IntersectAlignments(l1, l2, &overlaps);
  REQUIRE(overlaps.size() == 1);
  CHECK(overlaps[0].fromStationA == Approx(60.0));
  CHECK(overlaps[0].toStationA == Approx(100.0));

  // Parallel but separate, and a miss, give nothing.
  Alignment far;
  REQUIRE(far.AppendTangent({0, 30}, 90.0 * kDeg, 100.0));
  CHECK(alignment::IntersectAlignments(l1, far).empty());
  Alignment shortNorth;
  REQUIRE(shortNorth.AppendTangent({40, 5}, 0.0, 10.0));
  CHECK(alignment::IntersectAlignments(east, shortNorth).empty());
}

TEST_CASE("ALIGNMENTEDIT, LOCK, INSERT and REMOVE drive the solver and undo in one step", "[req486][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT E1", log);
  Submit(st, "ALIGNMENTADD E1, TANGENT, 0, 0, 45, 500", log);
  Submit(st, "ALIGNMENTADD E1, CURVE, 500, 15, RIGHT", log);
  Submit(st, "ALIGNMENTADD E1, TANGENT, 200", log);
  REQUIRE(st.alignments[0].Entities().size() == 3);

  log.clear();
  Submit(st, "ALIGNMENTEDIT E1, 2, RADIUS, 400", log);
  CHECK(LogHas(log, "updated"));
  CHECK(st.alignments[0].Entities()[1].radius == Approx(400.0));

  log.clear();
  Submit(st, "ALIGNMENTEDIT E1, 3, BEARING, 10", log);  // derived bearing
  CHECK(LogHas(log, "refused"));
  CHECK(LogHas(log, "Nothing changed"));

  Submit(st, "ALIGNMENTLOCK E1, 2, RADIUS, ON", log);
  log.clear();
  Submit(st, "ALIGNMENTEDIT E1, 2, RADIUS, 300", log);
  CHECK(LogHas(log, "fixed radius"));
  CHECK(st.alignments[0].Entities()[1].radius == Approx(400.0));

  log.clear();
  Submit(st, "ALIGNMENTEDIT E1, 9, LENGTH, 5", log);
  CHECK(LogHas(log, "not an entity number"));

  const double lenBefore = st.alignments[0].Length();
  log.clear();
  Submit(st, "ALIGNMENTINSERT E1, 1, TANGENT, 50", log);
  CHECK(st.alignments[0].Entities().size() == 4);
  CHECK(st.alignments[0].Length() == Approx(lenBefore + 50.0));
  Submit(st, "ALIGNMENTREMOVE E1, 2", log);
  CHECK(st.alignments[0].Entities().size() == 3);
  Submit(st, "UNDO", log);
  CHECK(st.alignments[0].Entities().size() == 4);
  Submit(st, "UNDO", log);
  CHECK(st.alignments[0].Entities().size() == 3);
}

TEST_CASE("REVERSEALIGNMENT and ALIGNMENTSTATION", "[req483][req487][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT R1, , 10+00", log);
  Submit(st, "ALIGNMENTADD R1, TANGENT, 0, 0, 90, 1000", log);
  Submit(st, "ALIGNMENTADD R1, CURVE, 200, 90, LEFT", log);
  const alignment::Point2 start = st.alignments[0].StartPoint(), end = st.alignments[0].EndPoint();
  Submit(st, "REVERSEALIGNMENT R1", log);
  CHECK(st.alignments[0].StartPoint().x == Approx(end.x).margin(kEps));
  CHECK(st.alignments[0].EndPoint().y == Approx(start.y).margin(kEps));
  CHECK(st.alignments[0].Entities().front().kind == alignment::EntityKind::Arc);
  Submit(st, "REVERSEALIGNDIRECTION R1", log);
  CHECK(st.alignments[0].StartPoint().x == Approx(start.x).margin(kEps));

  log.clear();
  Submit(st, "ALIGNMENTSTATION R1, EQ, ADD, 20+00, 25+00, INC", log);
  CHECK(LogHas(log, "equation added"));
  Submit(st, "ALIGNMENTSTATION R1, EQ, LIST", log);
  CHECK(LogHas(log, "increasing"));
  log.clear();
  Submit(st, "ALIGNMENTSTATION R1, START, 0+00", log);  // would drop the equation: needs CLEAR
  CHECK(LogHas(log, "CLEAR"));
  CHECK(st.alignments[0].startingStation == Approx(1000.0));
  Submit(st, "ALIGNMENTSTATION R1, START, 0+00, CLEAR", log);
  CHECK(st.alignments[0].startingStation == Approx(0.0));
  CHECK(st.alignments[0].equations.empty());
  log.clear();
  Submit(st, "ALIGNMENTSTATION R1, EQ, ADD, 99+00, 99+50, INC", log);  // outside the alignment
  CHECK(LogHas(log, "refused"));
}

TEST_CASE("OFFSETALIGNMENT creates linked offsets that follow edits and detach", "[req488][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT CL", log);
  Submit(st, "ALIGNMENTADD CL, TANGENT, 0, 0, 90, 500", log);
  Submit(st, "ALIGNMENTADD CL, CURVE, 500, 15, RIGHT", log);
  Submit(st, "ALIGNMENTADD CL, TANGENT, 200", log);
  Submit(st, "OFFSETALIGNMENT CL, R12, L12", log);
  REQUIRE(st.alignments.size() == 3);
  CHECK(st.alignments[1].type == alignment::AlignmentType::Offset);
  CHECK(st.alignments[1].parentId == st.alignments[0].id);
  CHECK(st.alignments[1].Entities()[1].radius == Approx(488.0).margin(1e-9));
  CHECK(st.alignments[2].Entities()[1].radius == Approx(512.0).margin(1e-9));

  log.clear();
  Submit(st, "ALIGNMENTEDIT CL, 2, RADIUS, 600", log);
  CHECK(st.alignments[1].Entities()[1].radius == Approx(588.0).margin(1e-9));  // followed the parent
  CHECK(st.alignments[2].Entities()[1].radius == Approx(612.0).margin(1e-9));

  log.clear();
  Submit(st, "ALIGNMENTEDIT CL - R12, 1, LENGTH, 5", log);  // linked offsets are not directly editable
  CHECK(LogHas(log, "linked"));
  Submit(st, "ALIGNMENTDETACH CL - R12", log);
  CHECK(st.alignments[1].parentId == 0);
  Submit(st, "ALIGNMENTEDIT CL - R12, 1, LENGTH, 400", log);
  CHECK(st.alignments[1].Entities()[0].length == Approx(400.0));

  // The remaining linked offset survives a save and reload with its link.
  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back = Drawing();
  std::vector<std::string> loadLog;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, loadLog));
  REQUIRE(back.alignments.size() == 3);
  CHECK(back.alignments[2].parentId == back.alignments[0].id);
  CHECK(back.alignments[2].offsetDistance == Approx(-12.0));
  CHECK(back.alignments[1].parentId == 0);

  log.clear();
  Submit(st, "OFFSETALIGNMENT CL, 5000", log);  // collapses the curve: nothing is created
  CHECK(LogHas(log, "Nothing was created"));
  CHECK(st.alignments.size() == 3);
  Submit(st, "UNDO", log);  // undoes the edit made after detach... then earlier steps keep unwinding one at a time
}

TEST_CASE("ALIGNMENTINTERSECT reports stations and refuses nonsense", "[req489][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT X1", log);
  Submit(st, "ALIGNMENTADD X1, TANGENT, 0, 0, 90, 100", log);
  Submit(st, "ALIGNMENT X2", log);
  Submit(st, "ALIGNMENTADD X2, TANGENT, 40, -50, 0, 100", log);
  log.clear();
  Submit(st, "ALIGNMENTINTERSECT X1, X2", log);
  CHECK(LogHas(log, "1 intersection"));
  CHECK(LogHas(log, "Station = 0+40.00"));
  log.clear();
  Submit(st, "ALIGNMENTINTERSECT X1, LINE, 70, -10, 70, 10", log);
  CHECK(LogHas(log, "X = 70.00"));
  log.clear();
  Submit(st, "ALIGNMENTINTERSECT X1, CIRCLE, 50, 0, 20", log);
  CHECK(LogHas(log, "2 intersection"));
  log.clear();
  Submit(st, "ALIGNMENTINTERSECT X1, CIRCLE, 50, 0, -1", log);
  CHECK(LogHas(log, "usage"));
}

TEST_CASE("MOVE and ERASE act on alignments and keep linked offsets consistent", "[req487][req488][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT CL", log);
  Submit(st, "ALIGNMENTADD CL, TANGENT, 0, 0, 90, 500", log);
  Submit(st, "ALIGNMENTADD CL, CURVE, 500, 15, RIGHT", log);
  Submit(st, "OFFSETALIGNMENT CL, R12", log);
  REQUIRE(st.alignments.size() == 2);
  const Point2 start = st.alignments[0].StartPoint();

  st.selection = {{SelectedEntity::Type::Alignment, 0}};
  ApplyTranslationToSelection(st, 10.f, 5.f, 0.f, log);
  CHECK(st.alignments[0].StartPoint().x == Approx(start.x + 10.0).margin(kEps));
  CHECK(st.alignments[0].StartPoint().y == Approx(start.y + 5.0).margin(kEps));
  // The linked offset was rebuilt from the moved parent: every point of it is still 12 to the right.
  const Point2 p = st.alignments[1].GetPointAtStation(250.0);
  CHECK(st.alignments[0].GetStationOffset(p.x, p.y).offset == Approx(12.0).margin(1e-6));

  ExecuteDeleteSelection(st, log);
  REQUIRE(st.alignments.size() == 1);
  CHECK(st.alignments[0].parentId == 0);  // the orphaned offset is independent now
  Submit(st, "UNDO", log);
  CHECK(st.alignments.size() == 2);
  CHECK(st.alignments[1].parentId == st.alignments[0].id);
}

TEST_CASE("Grips: start moves the alignment, tangent ends set bearing and length, curve grips set the radius", "[req487][alignment]") {
  const Alignment a = Chain();
  const auto grips = alignment::GripPoints(a);
  using K = alignment::GripKind;
  // Start; the Fixed tangent's end and midpoint; the floating curve's PT, midpoint and centre; the last tangent's end.
  REQUIRE(grips.size() == 7);
  CHECK(grips[0].which == 0);
  const int tangentEnd = GripWhich(a, K::TangentEnd, 0);
  const int curveMid = GripWhich(a, K::CurveMid, 1);
  const int lastEnd = GripWhich(a, K::TangentEnd, 2);
  REQUIRE(tangentEnd > 0);
  REQUIRE(curveMid > 0);
  REQUIRE(lastEnd > 0);
  CHECK(GripWhich(a, K::TangentMid, 2) < 0);  // a floating tangent has no move grip
  CHECK(GripWhich(a, K::CurvePc, 1) < 0);     // nor a floating curve a PC grip

  Alignment out;
  // Start grip: the whole alignment follows.
  REQUIRE(alignment::DragGrip(a, 0, {1010.0, 2005.0}, &out));
  CHECK(out.StartPoint().x == Approx(1010.0).margin(kEps));
  CHECK(out.EndPoint().y == Approx(a.EndPoint().y + 5.0).margin(kEps));

  // First (Fixed) tangent end: bearing AND length follow the cursor; the curve and last tangent float along.
  REQUIRE(alignment::DragGrip(a, tangentEnd, {1000.0, 2400.0}, &out));  // due north, 400 long
  CHECK(out.Entities()[0].startAzimuth == Approx(0.0).margin(1e-9));
  CHECK(out.Entities()[0].length == Approx(400.0).margin(1e-9));
  CHECK(out.Entities()[1].start.x == Approx(1000.0).margin(kEps));
  CHECK(out.Entities()[1].start.y == Approx(2400.0).margin(kEps));
  CheckTangentContinuous(out);

  // A floating tangent keeps its bearing and slides its length along it.
  const Point2 pt = a.GetPointAtStation(a.EntityStartStation(2) + 100.0);
  REQUIRE(alignment::DragGrip(a, lastEnd, {pt.x + 0.0, pt.y + 0.0}, &out));
  CHECK(out.Entities()[2].length == Approx(100.0).margin(1e-6));

  // The curve's mid grip sets the radius from the distance to its centre.
  alignment::CurveData cd;
  REQUIRE(a.CurveDataFor(1, &cd));
  REQUIRE(alignment::DragGrip(a, curveMid, {cd.center.x + 350.0, cd.center.y}, &out));
  CHECK(out.Entities()[1].radius == Approx(350.0).margin(1e-6));
  CheckTangentContinuous(out);

  // A locked bearing slides along the existing bearing instead of turning.
  Alignment locked = a;
  REQUIRE(locked.EditEntity(0, EditParam::Locks, Entity::kLockBearing));
  REQUIRE(alignment::DragGrip(locked, tangentEnd, {1000.0, 2400.0}, &out));
  CHECK(out.Entities()[0].startAzimuth == Approx(45.0 * kDeg).margin(1e-9));
  CHECK_FALSE(alignment::DragGrip(a, 99, {0, 0}, &out));
  CHECK(out.Entities().size() == a.Entities().size());  // always a valid alignment, even when nothing applies
}

TEST_CASE("Dragging an alignment grip edits it live, cancels back, and linked offsets follow", "[req487][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT G1", log);
  Submit(st, "ALIGNMENTADD G1, TANGENT, 0, 0, 90, 500", log);
  Submit(st, "ALIGNMENTADD G1, CURVE, 500, 15, RIGHT", log);
  Submit(st, "OFFSETALIGNMENT G1, R12", log);
  st.selection = {{SelectedEntity::Type::Alignment, 0}};
  REQUIRE(TryBeginEntityGripAtLocal(st, 500.0f, 0.0f, 2.0f));  // the end of the first tangent
  CHECK(st.entityGripType == SelectedEntity::Type::Alignment);
  ApplyEntityGripPoint(st, 400.0f, 0.0f, 0.f);  // shorten the tangent to 400
  CHECK(st.alignments[0].Entities()[0].length == Approx(400.0).margin(kEps));
  CHECK(st.alignments[1].Entities()[0].length == Approx(400.0).margin(kEps));  // the offset followed live
  RestoreEntityGripOriginal(st);  // Esc
  CHECK(st.alignments[0].Entities()[0].length == Approx(500.0).margin(kEps));
  CHECK(st.alignments[1].Entities()[0].length == Approx(500.0).margin(kEps));
}

namespace {

/// North 200 to the PI (0,200), a Free R50 curve, then east 250: the Free-curve fixture of REQ-486 AC4.
Alignment FreeCorner() {
  Alignment a;
  Entity l1;
  l1.start = {0, 0};
  l1.length = 200.0;
  REQUIRE(a.AppendRaw(l1));
  Entity arc;
  arc.kind = alignment::EntityKind::Arc;
  arc.start = {0, 150};
  arc.radius = 50.0;
  arc.length = 50.0 * alignment::kPi / 2.0;
  arc.right = true;
  REQUIRE(a.AppendRaw(arc));
  Entity l2;
  l2.start = {50, 200};
  l2.startAzimuth = 90.0 * kDeg;
  l2.length = 250.0;
  REQUIRE(a.AppendRaw(l2));
  REQUIRE(a.EditEntity(1, EditParam::Constraint, 3.0));  // Free
  return a;
}

}  // namespace

TEST_CASE("The issue's example is reproduced from entered parameters (REQ-486 AC1)", "[req486][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT EX", log);
  Submit(st, "ALIGNMENTADD EX, TANGENT, 0, 0, 0, 250", log);
  Submit(st, "ALIGNMENTADD EX, CURVELEN, 500, 132.14, RIGHT", log);
  Submit(st, "ALIGNMENTADD EX, TANGENT, 100", log);
  REQUIRE(st.alignments.size() == 1);
  const Alignment& a = st.alignments[0];
  REQUIRE(a.Entities().size() == 3);
  CHECK(FormatLinear(a.Entities()[0].length, 2) == "250.00");
  CHECK(FormatLinear(a.Entities()[1].radius, 2) == "500.00");
  CHECK(FormatLinear(a.Entities()[1].length, 2) == "132.14");
  CHECK(alignment::FormatStation(a.EntityStartStation(1)) == "2+50.00");
  CHECK(alignment::FormatStation(a.EntityEndStation(1)) == "3+82.14");
  CHECK(alignment::FormatStation(a.EndStation()) == "4+82.14");
  CHECK(std::string(alignment::ParameterConstraintName(a.Entities()[1])) == "Radius, length");
  CheckTangentContinuous(a);
}

TEST_CASE("Only defining parameters are editable in the layout table (REQ-486.1)", "[req486][alignment]") {
  const Alignment chain = Chain();
  const Entity& fixedLine = chain.Entities()[0];
  const Entity& curve = chain.Entities()[1];
  const Entity& floatingLine = chain.Entities()[2];
  CHECK(alignment::ParameterEditable(fixedLine, EditParam::LineAzimuth));
  CHECK_FALSE(alignment::ParameterEditable(floatingLine, EditParam::LineAzimuth));
  CHECK(alignment::ParameterEditable(floatingLine, EditParam::LineLength));
  CHECK(alignment::ParameterEditable(curve, EditParam::CurveRadius));
  CHECK(alignment::ParameterEditable(curve, EditParam::CurveDelta));
  CHECK_FALSE(alignment::ParameterEditable(curve, EditParam::CurveLength));  // radius-and-delta: the length is derived
  CHECK(std::string(alignment::ParameterConstraintName(curve)) == "Radius, delta");
  const Alignment corner = FreeCorner();
  const Entity& free = corner.Entities()[1];
  CHECK(alignment::ParameterEditable(free, EditParam::CurveRadius));
  CHECK_FALSE(alignment::ParameterEditable(free, EditParam::CurveDelta));
  CHECK_FALSE(alignment::ParameterEditable(free, EditParam::CurveLength));
  CHECK_FALSE(alignment::ParameterEditable(free, EditParam::CurveDirection));
  CHECK(std::string(alignment::ParameterConstraintName(free)) == "Radius (between tangents)");
}

TEST_CASE("A Free curve's derived values are refused; a length or delta edit redefines a curve (D-2026-11-04-g)",
          "[req486][alignment]") {
  Alignment corner = FreeCorner();
  const Alignment before = corner;
  std::string why;
  CHECK_FALSE(corner.EditEntity(1, EditParam::CurveLength, 100.0, &why));
  CHECK(why.find("Free curve") != std::string::npos);
  CHECK_FALSE(corner.EditEntity(1, EditParam::CurveDelta, 60.0 * kDeg, &why));
  CHECK_FALSE(corner.EditEntity(1, EditParam::CurveDirection, 0.0, &why));
  CHECK(corner.Length() == Approx(before.Length()).margin(1e-12));

  Alignment chain = Chain();
  REQUIRE(chain.EditEntity(1, EditParam::CurveLength, 150.0));
  CHECK(chain.Entities()[1].definedByLength);
  REQUIRE(chain.EditEntity(1, EditParam::CurveRadius, 300.0));
  CHECK(chain.Entities()[1].length == Approx(150.0).margin(1e-9));  // now defined by its length
  REQUIRE(chain.EditEntity(1, EditParam::CurveDelta, 20.0 * kDeg));
  CHECK_FALSE(chain.Entities()[1].definedByLength);
  REQUIRE(chain.EditEntity(1, EditParam::CurveRadius, 600.0));
  CHECK(chain.Entities()[1].length == Approx(600.0 * 20.0 * kDeg).margin(1e-9));  // and again by its delta
}

TEST_CASE("Editing the first bearing re-solves a Free curve and round-trips (REQ-486 AC4)", "[req486][alignment]") {
  Alignment a = FreeCorner();
  const Alignment original = a;
  std::string why;
  // Turn the first tangent 10 degrees right (N10E): the Free curve now turns 80 degrees, not 90.
  REQUIRE(a.EditEntity(0, EditParam::LineAzimuth, 10.0 * kDeg, &why));
  alignment::CurveData cd;
  REQUIRE(a.CurveDataFor(1, &cd));
  CHECK(cd.delta == Approx(80.0 * kDeg).margin(1e-9));
  CheckTangentContinuous(a);
  CHECK(a.EndPoint().x == Approx(original.EndPoint().x).margin(kEps));  // the second tangent keeps its end
  CHECK(a.EndPoint().y == Approx(original.EndPoint().y).margin(kEps));
  // Back to north and a radius out and back: the original geometry returns.
  REQUIRE(a.EditEntity(0, EditParam::LineAzimuth, 0.0, &why));
  REQUIRE(a.EditEntity(1, EditParam::CurveRadius, 80.0, &why));
  REQUIRE(a.EditEntity(1, EditParam::CurveRadius, 50.0, &why));
  for (size_t i = 0; i < a.Entities().size(); ++i) {
    CHECK(a.Entities()[i].start.x == Approx(original.Entities()[i].start.x).margin(1e-9));
    CHECK(a.Entities()[i].start.y == Approx(original.Entities()[i].start.y).margin(1e-9));
    CHECK(a.Entities()[i].length == Approx(original.Entities()[i].length).margin(1e-9));
  }
}

TEST_CASE("A fixed station refuses edits that would move it, naming every row (REQ-486.3)", "[req486][alignment]") {
  Alignment a = Chain();
  std::string why;
  REQUIRE(a.EditEntity(2, EditParam::Locks, Entity::kLockStation));
  const double lockedAt = a.EntityStartStation(2);
  CHECK_FALSE(a.EditEntity(1, EditParam::CurveRadius, 400.0, &why));  // the curve would shorten and move entity 3
  CHECK(why.find("entity 3 would move") != std::string::npos);
  CHECK(a.EntityStartStation(2) == Approx(lockedAt).margin(1e-12));
  CHECK_FALSE(a.EditEntity(0, EditParam::LineLength, 450.0, &why));
  CHECK_FALSE(a.InsertTangentAfter(0, 25.0, &why));
  CHECK(why.find("fixed station") != std::string::npos);
  CHECK_FALSE(a.DeleteEntity(2, &why));
  CHECK(why.find("cannot be removed") != std::string::npos);
  REQUIRE(a.EditEntity(2, EditParam::LineLength, 300.0));  // its own length does not move its start
  REQUIRE(a.EditEntity(0, EditParam::Locks, Entity::kLockStation));
  CHECK_FALSE(a.EditEntity(0, EditParam::LineLength, 450.0, &why));
  CHECK(why.find("entity 3") != std::string::npos);  // only entity 3 moves: entity 1 starts at the beginning
  CHECK(why.find("entity 1 would move") == std::string::npos);
}

TEST_CASE("ALIGNMENTLOCK STATION, refusal wording and one undo step per edit (REQ-486 AC3, AC5)", "[req486][alignment]") {
  AppCommandState st = Drawing();
  std::vector<std::string> log;
  Submit(st, "ALIGNMENT L", log);
  Submit(st, "ALIGNMENTADD L, TANGENT, 0, 0, 45, 500", log);
  Submit(st, "ALIGNMENTADD L, CURVE, 500, 15, RIGHT", log);
  Submit(st, "ALIGNMENTADD L, TANGENT, 200", log);
  Submit(st, "ALIGNMENTLOCK L, 3, STATION, ON", log);
  REQUIRE((st.alignments[0].Entities()[2].locks & Entity::kLockStation) != 0);
  log.clear();
  Submit(st, "ALIGNMENTEDIT L, 2, RADIUS, 400", log);
  CHECK(LogHas(log, "refused"));
  CHECK(LogHas(log, "entity 3 would move"));
  CHECK(st.alignments[0].Entities()[1].radius == Approx(500.0));
  Submit(st, "ALIGNMENTLOCK L, 3, STATION, OFF", log);
  Submit(st, "ALIGNMENTEDIT L, 2, RADIUS, 400", log);
  REQUIRE(st.alignments[0].Entities()[1].radius == Approx(400.0));
  Submit(st, "UNDO", log);
  CHECK(st.alignments[0].Entities()[1].radius == Approx(500.0));
  Submit(st, "REDO", log);
  CHECK(st.alignments[0].Entities()[1].radius == Approx(400.0));
}

TEST_CASE("The criteria column states what the check set requires (REQ-486.5)", "[req486][req502][alignment]") {
  Alignment a = Chain();
  a.designSpeeds.push_back({0.0, 50.0});
  alignment::AlignmentCheckSet set;
  set.radiusTable = {{40.0, 300.0}, {50.0, 450.0}};
  set.minLineLength = 100.0;
  CHECK(alignment::CriteriaRequirementText(a, set, 1, 2) == "R \xE2\x89\xA5 450.00'");
  CHECK(alignment::CriteriaRequirementText(a, set, 0, 2) == "L \xE2\x89\xA5 100.00'");
  set.minLineLength = 0.0;
  CHECK(alignment::CriteriaRequirementText(a, set, 0, 2).empty());
  CHECK(alignment::CriteriaRequirementText(a, set, 9, 2).empty());
}
