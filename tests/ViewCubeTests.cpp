// ViewCube geometry, hit-testing and click targets (REQ-059, GitHub #1034).
//
// Pure: no Qt, no GL. The Qt viewport only paints viewcube::BuildScene and forwards presses to
// HitTest / TargetFor, so what the acceptance criteria promise is pinned here.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <string>

#include "render/Camera.hpp"
#include "viewport/ViewCube.hpp"

using Catch::Approx;
using namespace viewcube;

namespace {

Camera View(float az, float el) {
  Camera c;
  c.azimuthDeg = az;
  c.elevationDeg = el;
  return c;
}

Scene SceneFor(const Camera& c, float offset = 0.f) { return BuildScene(c, 100.f, 10.f, offset); }

Point Centre(const QuadFace& q) {
  return {(q.pts[0].x + q.pts[1].x + q.pts[2].x + q.pts[3].x) * 0.25f,
          (q.pts[0].y + q.pts[1].y + q.pts[2].y + q.pts[3].y) * 0.25f};
}

const QuadFace* Visible(const Scene& s, const char* label) {
  for (const QuadFace& q : s.quads)
    if (q.visible && std::string(kFaces[q.face].label) == label) return &q;
  return nullptr;
}

// A point on the arrow ring at math-oriented bearing `deg`.
Point OnArrowRing(const Scene& s, float deg) {
  const float a = deg * detail::kDeg;
  return {s.center.x + std::cos(a) * s.arrowRadius, s.center.y - std::sin(a) * s.arrowRadius};
}

Target Click(const Scene& s, const Camera& c, Point p, float offset = 0.f) {
  Target t;
  REQUIRE(TargetFor(HitTest(s, p.x, p.y), c, offset, &t));
  return t;
}

}  // namespace

TEST_CASE("Plan view shows a legible TOP face and all four compass letters", "[viewcube]") {
  const Camera cam = View(0.f, 90.f);
  const Scene s = SceneFor(cam);
  int visible = 0;
  for (const QuadFace& q : s.quads) visible += q.visible ? 1 : 0;
  REQUIRE(visible == 1);
  const QuadFace* top = Visible(s, "TOP");
  REQUIRE(top != nullptr);
  REQUIRE(top->labelled);
  REQUIRE(top->labelMaxWidth > 20.f);  // wide enough to read, the first implementation's failure
  int letters = 0;
  for (const CompassLabel& c : s.compass) letters += c.shown ? 1 : 0;
  REQUIRE(letters == 4);
}

TEST_CASE("Every standard view shows the face it looks at, labelled", "[viewcube]") {
  for (const Face& f : kFaces) {
    const Scene s = SceneFor(View(f.azimuthDeg, f.elevationDeg));
    const QuadFace* q = Visible(s, f.label);
    INFO(f.label);
    REQUIRE(q != nullptr);
    REQUIRE(q->labelled);
    REQUIRE(q->labelMaxWidth > 8.f);
  }
}

TEST_CASE("The home view shows three labelled faces including the long ones", "[viewcube]") {
  const Scene s = SceneFor(View(kHomeAzimuthDeg, kIsometricElevationDeg));
  for (const char* label : {"TOP", "FRONT", "LEFT"}) {
    const QuadFace* q = Visible(s, label);
    INFO(label);
    REQUIRE(q != nullptr);
    REQUIRE(q->labelled);
  }
}

TEST_CASE("The displayed cube follows the camera after any orbit", "[viewcube]") {
  // Orbit from plan to a back view: the BACK face must become visible and TOP must leave.
  const Scene back = SceneFor(View(180.f, 0.f));
  REQUIRE(Visible(back, "BACK") != nullptr);
  REQUIRE(Visible(back, "FRONT") == nullptr);
  REQUIRE(Visible(back, "TOP") == nullptr);
  const Scene under = SceneFor(View(0.f, -60.f));
  REQUIRE(Visible(under, "BOTTOM") != nullptr);
  REQUIRE(Visible(under, "TOP") == nullptr);
  REQUIRE(back.cameraAzimuthDeg == Approx(180.f));
}

TEST_CASE("Clicking a face asks for that face's standard view", "[viewcube]") {
  const Camera cam = View(kHomeAzimuthDeg, kIsometricElevationDeg);
  const Scene s = SceneFor(cam);
  const Target top = Click(s, cam, Centre(*Visible(s, "TOP")));
  REQUIRE(top.elevationDeg == Approx(90.f));
  const Target front = Click(s, cam, Centre(*Visible(s, "FRONT")));
  REQUIRE(front.azimuthDeg == Approx(0.f));
  REQUIRE(front.elevationDeg == Approx(0.f));
  const Target left = Click(s, cam, Centre(*Visible(s, "LEFT")));
  REQUIRE(left.azimuthDeg == Approx(90.f));
  REQUIRE(left.elevationDeg == Approx(0.f));
  const Camera east = View(270.f, kIsometricElevationDeg);
  const Scene se = SceneFor(east);
  const Target right = Click(se, east, Centre(*Visible(se, "RIGHT")));
  REQUIRE(right.azimuthDeg == Approx(270.f));
  REQUIRE(right.elevationDeg == Approx(0.f));
}

TEST_CASE("Face targets are relative to the active coordinate system", "[viewcube]") {
  const Camera cam = View(kHomeAzimuthDeg, kIsometricElevationDeg);
  const Scene s = SceneFor(cam, -30.f);  // a UCS rotated 30 degrees counter-clockwise
  const Target right = Click(s, cam, Centre(*Visible(s, "LEFT")), -30.f);
  REQUIRE(right.azimuthDeg == Approx(60.f));  // LEFT is az 90, shifted by the UCS
  REQUIRE(right.elevationDeg == Approx(0.f));
}

TEST_CASE("Home reaches SW isometric from any starting orientation", "[viewcube]") {
  for (const Camera& cam : {View(0.f, 90.f), View(200.f, -40.f), View(359.f, 12.f)}) {
    const Scene s = SceneFor(cam);
    const Target t = Click(s, cam, s.home);
    REQUIRE(t.azimuthDeg == Approx(45.f));
    REQUIRE(t.elevationDeg == Approx(35.26438968f));
  }
  const Camera plan = View(0.f, 90.f);
  REQUIRE(Click(SceneFor(plan, -30.f), plan, SceneFor(plan, -30.f).home, -30.f).azimuthDeg == Approx(15.f));
}

TEST_CASE("Rotation arrows move to the next quarter turn even when already square", "[viewcube]") {
  const Camera square = View(0.f, 90.f);
  const Scene s = SceneFor(square);
  const Point cw = OnArrowRing(s, kArrowMidDeg - kArrowGapDeg - 10.f);
  const Point ccw = OnArrowRing(s, kArrowMidDeg + kArrowGapDeg + 10.f);
  REQUIRE(HitTest(s, cw.x, cw.y).part == Part::TurnCw);
  REQUIRE(HitTest(s, ccw.x, ccw.y).part == Part::TurnCcw);
  REQUIRE(Click(s, square, cw).azimuthDeg == Approx(90.f));
  REQUIRE(Click(s, square, ccw).azimuthDeg == Approx(270.f));
}

TEST_CASE("Rotation arrows square an off-angle view and preserve its elevation", "[viewcube]") {
  const Camera cam = View(100.f, 25.f);
  const Scene s = SceneFor(cam);
  const Point cw = OnArrowRing(s, kArrowMidDeg - kArrowGapDeg - 10.f);
  const Point ccw = OnArrowRing(s, kArrowMidDeg + kArrowGapDeg + 10.f);
  const Target next = Click(s, cam, cw);
  REQUIRE(next.azimuthDeg == Approx(180.f));
  REQUIRE(next.elevationDeg == Approx(25.f));
  const Target previous = Click(s, cam, ccw);
  REQUIRE(previous.azimuthDeg == Approx(90.f));
  REQUIRE(previous.elevationDeg == Approx(25.f));
}

TEST_CASE("Rotation arrows square to the UCS north, not the world's", "[viewcube]") {
  // The camera is square to the world (az 0) under a UCS rotated 30 degrees: the next UCS quarter
  // turn is 30 + 90 = 120, not 90.
  const Camera cam = View(0.f, 40.f);
  const Scene s = SceneFor(cam, 30.f);
  const Point cw = OnArrowRing(s, kArrowMidDeg - kArrowGapDeg - 10.f);
  REQUIRE(Click(s, cam, cw, 30.f).azimuthDeg == Approx(30.f));  // snaps up to the UCS-square view first
  const Camera squared = View(30.f, 40.f);
  REQUIRE(Click(SceneFor(squared, 30.f), squared, cw, 30.f).azimuthDeg == Approx(120.f));
}

TEST_CASE("A press anywhere in the widget is the widget's, outside it is not", "[viewcube]") {
  const Scene s = SceneFor(View(0.f, 90.f));
  REQUIRE(Contains(s, s.originX + 1.f, s.originY + 1.f));
  REQUIRE(Contains(s, s.originX + s.sizePx - 1.f, s.originY + s.sizePx - 1.f));
  REQUIRE_FALSE(Contains(s, s.originX - 1.f, s.originY + 50.f));
  REQUIRE_FALSE(Contains(s, s.originX + 50.f, s.originY + s.sizePx + 1.f));
  // The widget's corner is inside it but is no control.
  REQUIRE(HitTest(s, s.originX + s.sizePx - 2.f, s.originY + s.sizePx - 2.f).part == Part::None);
  REQUIRE(HitTest(s, s.originX - 5.f, s.originY).part == Part::None);
}

TEST_CASE("The widget sits in the top-right corner of the viewport", "[viewcube]") {
  const Point o = OriginFor(1000.f);
  REQUIRE(o.x + kSizePx + kPadPx == Approx(1000.f));
  REQUIRE(o.y == Approx(kPadPx));
}
