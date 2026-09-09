// REQ-060 rotate and scale handles (TASK-232, GitHub issue #148 acceptance 4, slice 3 of 3).
//
// REQ-060's second acceptance bullet is the whole design: *"a gizmo drag and the equivalent typed
// command produce coordinates agreeing within REQ-101"*. These cases assert that agreement
// DIRECTLY — the same drawing rotated by a gizmo drag and by the function typed ROTATE calls, then
// compared coordinate for coordinate — rather than describing it.
//
// That is also why the commit goes through `ApplyRotationAboutUcsZ` and `ApplyUniformScaleAboutBase`
// rather than their inner halves. Typed ROTATE does not call one function, it CHOOSES between two on
// `CadWorkPlaneIsWorldXy`, and typed SCALE runs a second Z pass under a tilted UCS. A gizmo wired to
// the inner function would agree in plan view and diverge under a tilted UCS — a half-agreement no
// tolerance-based check would ever catch.
//
// The handle COUNTS carry the other half of the argument, and they look like under-delivery until
// the reason is stated: rotate has one ring because typed ROTATE is UCS-Z-only (REQ-329: "a full
// ROTATE3D is a separate future issue"), and scale has one handle because typed SCALE and
// `brep::Scale` are uniform (REQ-332 item 7). A handle with no equivalent typed command could not
// satisfy the bullet above, so it is not drawn.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;

AppCommandState WithSelectedLine(float x0, float y0, float z0, float x1, float y1, float z1) {
  AppCommandState st;
  st.userLinesFlat = {x0, y0, z0, x1, y1, z1};
  st.userLineAttrs.push_back(EntityAttributes{});
  SelectedEntity e;
  e.type = SelectedEntity::Type::LineSeg;
  e.index = 0;
  st.selection.push_back(e);
  st.uiViewportWidthPx = 1200.f;
  st.uiViewportHeightPx = 700.f;
  return st;
}

ray3d::Ray RayThrough(const ray3d::Vec3& through, const ray3d::Vec3& dir) {
  ray3d::Ray r;
  r.dir = ray3d::Normalize(dir);
  r.origin = ray3d::Sub(through, ray3d::Scale(r.dir, 100.0));
  return r;
}

/// A point on the rotation ring at parameter \p theta, in the SAME frame `CadAxisDragAngle` builds
/// (from the axis alone, never the camera) — so a test can aim at a known angle rather than at a
/// pixel and hope.
ray3d::Vec3 OnRing(const ray3d::Vec3& anchor, const ray3d::Vec3& n, double r, double theta) {
  ray3d::Vec3 seed{0.0, 0.0, 1.0};
  if (std::fabs(ray3d::Dot(n, seed)) > 0.9)
    seed = ray3d::Vec3{1.0, 0.0, 0.0};
  const ray3d::Vec3 e0 = ray3d::Normalize(ray3d::Cross(seed, n));
  const ray3d::Vec3 e1 = ray3d::Cross(n, e0);
  return ray3d::Add(anchor, ray3d::Add(ray3d::Scale(e0, std::cos(theta) * r),
                                       ray3d::Scale(e1, std::sin(theta) * r)));
}

}  // namespace

TEST_CASE("Gizmo: the drag angle is measured in the rotation plane", "[gizmo][req060][rotate]") {
  const ray3d::Vec3 anchor{5.0, 0.0, 0.0};
  const ray3d::Vec3 zAxis{0.0, 0.0, 1.0};

  // Aim straight down at a ring point built at a known parameter and the solve must report that
  // parameter back. Four quadrants, so a sign or an axis swap in the frame cannot pass.
  for (const double want : {0.0, 0.5 * kPi, kPi * 0.75, -0.5 * kPi}) {
    const ray3d::Vec3 p = OnRing(anchor, zAxis, 12.0, want);
    double got = 0.0;
    REQUIRE(CadAxisDragAngle(anchor, zAxis, RayThrough(p, {0.0, 0.0, -1.0}), &got));
    CHECK(got == Approx(want).margin(1e-9));
  }

  // The RADIUS does not matter — only the direction. A drag that wanders off the ring still names
  // the same angle, which is what makes the gesture forgiving in the hand.
  double a1 = 0.0, a2 = 0.0;
  REQUIRE(CadAxisDragAngle(anchor, zAxis, RayThrough(OnRing(anchor, zAxis, 3.0, 0.9), {0, 0, -1}), &a1));
  REQUIRE(CadAxisDragAngle(anchor, zAxis, RayThrough(OnRing(anchor, zAxis, 40.0, 0.9), {0, 0, -1}), &a2));
  CHECK(a1 == Approx(a2));
}

TEST_CASE("Gizmo: a rotation gesture with no meaning is refused, not answered", "[gizmo][req060][rotate]") {
  const ray3d::Vec3 anchor{0.0, 0.0, 0.0};
  const ray3d::Vec3 zAxis{0.0, 0.0, 1.0};
  double a = 123.0;

  // A ray PARALLEL to the rotation plane never reaches it, so the gesture names no point at all.
  // The counterpart of the translate gizmo's end-on refusal, and refused for the same reason: the
  // alternative is a number from a near-singular divide, which reads as the selection spinning.
  CHECK_FALSE(CadAxisDragAngle(anchor, zAxis, RayThrough({10.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), &a));

  // Dead centre there is no direction to take an angle OF.
  CHECK_FALSE(CadAxisDragAngle(anchor, zAxis, RayThrough({0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}), &a));

  CHECK(a == 123.0);  // nothing was written on either refusal
}

TEST_CASE("Gizmo: how many handles each operation has, and why", "[gizmo][req060]") {
  AppCommandState st = WithSelectedLine(10.f, 0.f, 0.f, 20.f, 0.f, 0.f);

  st.gizmoOp = CadGizmoOp::Translate;
  CHECK(CadGizmoAxisCountFor(st) == 3);  // the UCS X, Y and Z
  st.gizmoOp = CadGizmoOp::Rotate;
  CHECK(CadGizmoAxisCountFor(st) == 1);  // typed ROTATE is UCS-Z-only, so one ring
  st.gizmoOp = CadGizmoOp::Scale;
  CHECK(CadGizmoAxisCountFor(st) == 1);  // a uniform scale has no per-axis meaning

  // REQ-060's third acceptance bullet, under every operation: an empty selection has no gizmo.
  st.selection.clear();
  for (const CadGizmoOp op : {CadGizmoOp::Translate, CadGizmoOp::Rotate, CadGizmoOp::Scale}) {
    st.gizmoOp = op;
    CHECK(CadGizmoModeFor(st) == CadGizmoMode::None);
    CHECK_FALSE(CadGizmoVisible(st));
    CHECK(CadGizmoAxisCountFor(st) == 0);
  }
}

TEST_CASE("Gizmo: a solid FACE gets a push handle and nothing else", "[gizmo][req060][rotate]") {
  // A face can be PUSHED and that is all — no kernel operation rotates or scales one. So under
  // Rotate or Scale the face selection gets NO gizmo, the same answer an edge or a vertex gets,
  // rather than a handle that would refuse on drop.
  AppCommandState st;
  st.uiViewportWidthPx = 1200.f;
  st.uiViewportHeightPx = 700.f;
  brep::Solid box;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 10.0, 8.0, &box, &why));
  auto sp = std::make_shared<const brep::Solid>(std::move(box));
  st.cadSolids.push_back(sp);
  SelectedSubObject face;
  face.solidIndex = 0;
  face.kind = solidpick::Kind::Face;
  face.index = 0;
  face.owner = sp;
  st.subObjectSelection.push_back(face);

  st.gizmoOp = CadGizmoOp::Translate;
  CHECK(CadGizmoModeFor(st) == CadGizmoMode::SubObjectFace);
  CHECK(CadGizmoAxisCountFor(st) == 1);

  st.gizmoOp = CadGizmoOp::Rotate;
  CHECK(CadGizmoModeFor(st) == CadGizmoMode::None);
  CHECK(CadGizmoAxisCountFor(st) == 0);

  st.gizmoOp = CadGizmoOp::Scale;
  CHECK(CadGizmoModeFor(st) == CadGizmoMode::None);
  CHECK(CadGizmoAxisCountFor(st) == 0);
}

TEST_CASE("Gizmo ROTATE agrees with the typed command, coordinate for coordinate",
          "[gizmo][req060][rotate]") {
  // THE acceptance. Two identical drawings: one turned by a gizmo drag through the ring, the other
  // by `ApplyRotationAboutUcsZ` — the function typed ROTATE calls — with the same base and the same
  // angle. They must land on the same coordinates.
  std::vector<std::string> log;
  AppCommandState byGizmo = WithSelectedLine(10.f, 0.f, 0.f, 20.f, 0.f, 0.f);
  byGizmo.gizmoOp = CadGizmoOp::Rotate;

  ray3d::Vec3 anchor{};
  REQUIRE(CadGizmoAnchorWorld(byGizmo, &anchor));
  CHECK(anchor.x == Approx(15.0));  // the selection's bounding-box centre
  const double len = static_cast<double>(CadGizmoHandleLenWorld(byGizmo));
  REQUIRE(len > 0.0);
  const ray3d::Vec3 n{0.0, 0.0, 1.0};
  const double tol = len * 0.1;

  // Grab on the ring at parameter 0 and drop at a quarter turn round it.
  REQUIRE(SubmitGizmoClick(byGizmo, RayThrough(OnRing(anchor, n, len, 0.0), {0, 0, -1}), tol, log));
  REQUIRE(byGizmo.gizmoDragActive);
  UpdateGizmoDrag(byGizmo, RayThrough(OnRing(anchor, n, len, 0.5 * kPi), {0, 0, -1}));
  const double dragged = byGizmo.gizmoDragDistance;
  CHECK(dragged == Approx(0.5 * kPi).margin(1e-9));
  REQUIRE(CommitGizmoDrag(byGizmo, log));

  // A quarter turn CCW about (15, 0) takes (10,0) to (15,-5) and (20,0) to (15,5) — asserted so the
  // pair below cannot agree by both being wrong in the same way.
  CHECK(byGizmo.userLinesFlat[0] == Approx(15.0));
  CHECK(byGizmo.userLinesFlat[1] == Approx(-5.0));
  CHECK(byGizmo.userLinesFlat[3] == Approx(15.0));
  CHECK(byGizmo.userLinesFlat[4] == Approx(5.0));

  AppCommandState byCommand = WithSelectedLine(10.f, 0.f, 0.f, 20.f, 0.f, 0.f);
  ApplyRotationAboutUcsZ(byCommand, static_cast<float>(anchor.x), static_cast<float>(anchor.y),
                         static_cast<float>(anchor.z), static_cast<float>(dragged), log);
  REQUIRE(byGizmo.userLinesFlat.size() == byCommand.userLinesFlat.size());
  for (size_t i = 0; i < byCommand.userLinesFlat.size(); ++i)
    CHECK(byGizmo.userLinesFlat[i] == Approx(byCommand.userLinesFlat[i]).margin(1e-6));
}

TEST_CASE("Gizmo ROTATE agrees with the typed command under a TILTED UCS too",
          "[gizmo][req060][rotate]") {
  // The case the World-UCS test above cannot see, and the reason `ApplyRotationAboutUcsZ` exists at
  // all. Typed ROTATE CHOOSES between two implementations on `CadWorkPlaneIsWorldXy`; a gizmo wired
  // to the plan-view one would pass every test in the World UCS and silently turn the selection
  // about the wrong axis the moment the UCS is tilted.
  //
  // UCS X 90: basis X = (1,0,0), Y = (0,0,1), Z = (0,-1,0) — so the ring's normal, and the axis of
  // the turn, is world -Y rather than world Z.
  std::vector<std::string> log;
  const auto tiltedUcs = [] {
    ucs::Ucs u;
    u.origin = {0.0, 0.0, 0.0};
    u.xAxis = {1.0, 0.0, 0.0};
    u.yAxis = {0.0, 0.0, 1.0};
    u.zAxis = {0.0, -1.0, 0.0};
    return u;
  }();

  AppCommandState byGizmo = WithSelectedLine(10.f, 0.f, 0.f, 20.f, 0.f, 0.f);
  byGizmo.gizmoOp = CadGizmoOp::Rotate;
  byGizmo.activeUcs = tiltedUcs;
  REQUIRE_FALSE(CadWorkPlaneIsWorldXy(byGizmo));  // the branch under test is genuinely taken

  ray3d::Vec3 anchor{};
  REQUIRE(CadGizmoAnchorWorld(byGizmo, &anchor));
  const double len = static_cast<double>(CadGizmoHandleLenWorld(byGizmo));
  const ray3d::Vec3 n = CadGizmoAxisWorld(byGizmo, 0);
  CHECK(n.y == Approx(-1.0));  // the ring turns about the UCS Z, not world Z

  // Aim along the ring's own normal so every ray meets its plane squarely.
  const ray3d::Vec3 look = ray3d::Scale(n, -1.0);
  REQUIRE(SubmitGizmoClick(byGizmo, RayThrough(OnRing(anchor, n, len, 0.0), look), len * 0.1, log));
  UpdateGizmoDrag(byGizmo, RayThrough(OnRing(anchor, n, len, 0.4), look));
  const double dragged = byGizmo.gizmoDragDistance;
  CHECK(dragged == Approx(0.4).margin(1e-9));
  REQUIRE(CommitGizmoDrag(byGizmo, log));

  AppCommandState byCommand = WithSelectedLine(10.f, 0.f, 0.f, 20.f, 0.f, 0.f);
  byCommand.activeUcs = tiltedUcs;
  ApplyRotationAboutUcsZ(byCommand, static_cast<float>(anchor.x), static_cast<float>(anchor.y),
                         static_cast<float>(anchor.z), static_cast<float>(dragged), log);
  REQUIRE(byGizmo.userLinesFlat.size() == byCommand.userLinesFlat.size());
  for (size_t i = 0; i < byCommand.userLinesFlat.size(); ++i)
    CHECK(byGizmo.userLinesFlat[i] == Approx(byCommand.userLinesFlat[i]).margin(1e-6));

  // ...and it really did leave the world XY plane, so "they agree" is not two no-ops agreeing.
  CHECK(std::fabs(byGizmo.userLinesFlat[2]) > 1e-6);
}

TEST_CASE("Gizmo SCALE agrees with the typed command, coordinate for coordinate",
          "[gizmo][req060][scale]") {
  std::vector<std::string> log;
  AppCommandState byGizmo = WithSelectedLine(10.f, 0.f, 0.f, 20.f, 0.f, 0.f);
  byGizmo.gizmoOp = CadGizmoOp::Scale;

  ray3d::Vec3 anchor{};
  REQUIRE(CadGizmoAnchorWorld(byGizmo, &anchor));
  const double len = static_cast<double>(CadGizmoHandleLenWorld(byGizmo));
  const ray3d::Vec3 x{1.0, 0.0, 0.0};
  const double tol = len * 0.1;

  // Grab the handle at its tip and drop at twice that distance from the anchor: the factor is the
  // RATIO of the two, so this is a scale of exactly 2.
  const ray3d::Vec3 grabAt = ray3d::Add(anchor, ray3d::Scale(x, len));
  const ray3d::Vec3 dropAt = ray3d::Add(anchor, ray3d::Scale(x, 2.0 * len));
  REQUIRE(SubmitGizmoClick(byGizmo, RayThrough(grabAt, {0, 0, -1}), tol, log));
  UpdateGizmoDrag(byGizmo, RayThrough(dropAt, {0, 0, -1}));
  const double factor = byGizmo.gizmoDragDistance;
  CHECK(factor == Approx(2.0));
  REQUIRE(CommitGizmoDrag(byGizmo, log));

  // Doubling about (15, 0): (10,0) -> (5,0) and (20,0) -> (25,0).
  CHECK(byGizmo.userLinesFlat[0] == Approx(5.0));
  CHECK(byGizmo.userLinesFlat[3] == Approx(25.0));

  AppCommandState byCommand = WithSelectedLine(10.f, 0.f, 0.f, 20.f, 0.f, 0.f);
  ApplyUniformScaleAboutBase(byCommand, static_cast<float>(anchor.x), static_cast<float>(anchor.y),
                             static_cast<float>(anchor.z), static_cast<float>(factor), log);
  for (size_t i = 0; i < byCommand.userLinesFlat.size(); ++i)
    CHECK(byGizmo.userLinesFlat[i] == Approx(byCommand.userLinesFlat[i]).margin(1e-6));
}

TEST_CASE("Gizmo: a scale drag through the anchor is refused rather than mirrored",
          "[gizmo][req060][scale]") {
  // Dragging past the anchor and out the far side gives a NEGATIVE ratio, which is a mirror — its
  // own operation (REQ-332 item 7), and one no typed command offers for a solid. The drag holds its
  // last good factor instead of inventing one.
  std::vector<std::string> log;
  AppCommandState st = WithSelectedLine(10.f, 0.f, 0.f, 20.f, 0.f, 0.f);
  st.gizmoOp = CadGizmoOp::Scale;
  ray3d::Vec3 anchor{};
  REQUIRE(CadGizmoAnchorWorld(st, &anchor));
  const double len = static_cast<double>(CadGizmoHandleLenWorld(st));
  const ray3d::Vec3 x{1.0, 0.0, 0.0};

  REQUIRE(SubmitGizmoClick(st, RayThrough(ray3d::Add(anchor, ray3d::Scale(x, len)), {0, 0, -1}),
                           len * 0.1, log));
  UpdateGizmoDrag(st, RayThrough(ray3d::Add(anchor, ray3d::Scale(x, 1.5 * len)), {0, 0, -1}));
  CHECK(st.gizmoDragDistance == Approx(1.5));
  // Now well past the anchor on the far side — the ratio would be negative.
  UpdateGizmoDrag(st, RayThrough(ray3d::Add(anchor, ray3d::Scale(x, -2.0 * len)), {0, 0, -1}));
  CHECK(st.gizmoDragDistance == Approx(1.5));  // held, not flipped
}

TEST_CASE("Gizmo: a drag that changes nothing is a cancel, per operation", "[gizmo][req060]") {
  // The neutral value is the OPERATION's own: zero for a turn, but ONE for a scale — a scale of
  // zero is a collapse, not a no-op, so a shared "is it zero" test would commit an empty undo step
  // for rotate and a catastrophe for scale.
  std::vector<std::string> log;
  AppCommandState st = WithSelectedLine(10.f, 0.f, 0.f, 20.f, 0.f, 0.f);
  st.gizmoOp = CadGizmoOp::Scale;
  ray3d::Vec3 anchor{};
  REQUIRE(CadGizmoAnchorWorld(st, &anchor));
  const double len = static_cast<double>(CadGizmoHandleLenWorld(st));
  const ray3d::Vec3 grabAt = ray3d::Add(anchor, ray3d::Vec3{len, 0.0, 0.0});
  REQUIRE(SubmitGizmoClick(st, RayThrough(grabAt, {0, 0, -1}), len * 0.1, log));
  CHECK(st.gizmoDragDistance == Approx(1.0));  // armed at the scale's neutral, not at zero
  const auto before = st.userLinesFlat;  // `double` since ADR-054's storage migration
  CHECK_FALSE(CommitGizmoDrag(st, log));  // dropped where it was grabbed: nothing happened
  CHECK(st.userLinesFlat == before);
}
