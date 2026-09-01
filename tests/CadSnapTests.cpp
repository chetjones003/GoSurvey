// GitHub issue #103 — object snapping must select the CLOSEST valid snap point, and the
// Shift+right-click one-shot override menu must be able to reach every snap type regardless of
// what is toggled on as a running OSNAP.
//
// Before this fix, ConsiderSnap ranked candidates by the ACCEPTANCE metric (`pickDistSq`), which
// for Center/GeometricCenter/SurveyCenter is a heuristic distance — e.g. a circle's distance to its
// RIM, not to its actual center point, so that hovering anywhere near a large circle can still
// offer its center. That heuristic reading near-zero let a circle's Center candidate beat a line
// Endpoint that was genuinely closer to the cursor, which is exactly the "circle with a line
// endpoint on its circumference" scenario the issue describes.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <memory>

#include "CadCommands.hpp"
#include "CadSnap.hpp"

using Kind = CadSnap::Kind;
using Catch::Approx;

namespace {

// A generous tolerance so nothing here is gated by aperture — these tests are about ranking, not
// acceptance.
constexpr float kTol = 5.f;

} // namespace

TEST_CASE("FindBest picks the line endpoint when it is closer than a large circle's center", "[CadSnap]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.objectSnapCenter = true;

  // A large circle centered far from the cursor...
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 100.f}; // cx, cy, z, r

  // ...whose rim passes almost exactly through the cursor at (100, 0), where a line endpoint sits
  // 0.05 away — genuinely closer to the cursor than the circle's actual center (100 away).
  st.userLinesFlat = {100.05f, 0.f, 0.f, 200.f, 50.f, 0.f}; // x0,y0,z0, x1,y1,z1

  const CadSnap::Hit hit = CadSnap::FindBest(100.0, 0.0, st, /*commandActive=*/true, kTol);

  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Endpoint);
  CHECK(hit.x == Catch::Approx(100.05f));
  CHECK(hit.y == Catch::Approx(0.f));
}

TEST_CASE("FindBest picks the circle center when it is closer than a nearby line endpoint", "[CadSnap]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.objectSnapCenter = true;

  // A small circle whose center sits right under the cursor.
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 1.f};

  // A line endpoint further from the cursor than the circle's center, but still within tolerance
  // and within the circle's rim-distance acceptance test.
  st.userLinesFlat = {0.9f, 0.9f, 0.f, 5.f, 5.f, 0.f};

  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/true, kTol);

  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Center);
  CHECK(hit.x == Catch::Approx(0.f));
  CHECK(hit.y == Catch::Approx(0.f));
}

TEST_CASE("FindBest respects the snap tolerance: nothing outside it is ever returned", "[CadSnap]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.userLinesFlat = {50.f, 50.f, 0.f, 60.f, 60.f, 0.f}; // far outside a small tolerance

  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/true, /*tolWorld=*/0.5f);

  CHECK_FALSE(hit.valid);
}

TEST_CASE("FindBest breaks an exact-distance tie by kind priority (Endpoint over Midpoint)", "[CadSnap]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.objectSnapMidpoint = true;

  // One line whose endpoint AND another line whose midpoint land at exactly the same point.
  st.userLinesFlat = {
      1.f, 0.f, 0.f, 5.f, 5.f, 0.f, // endpoint candidate at (1,0)
      0.f, 0.f, 0.f, 2.f, 0.f, 0.f, // midpoint candidate also at (1,0)
  };

  const CadSnap::Hit hit = CadSnap::FindBest(1.0, 0.0, st, /*commandActive=*/true, kTol);

  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Endpoint); // Priority(Endpoint)=3 > Priority(Midpoint)=1
}

TEST_CASE("CommandHasPerpendicularSnapReference: ignoreToggle reaches a reference the running OSNAP hides",
          "[CadSnap]") {
  AppCommandState st;
  st.active = AppCommandState::Kind::Line;
  st.linePhase = AppCommandState::LinePhase::NeedNextPoint;
  st.anchorX = 3.f;
  st.anchorY = 4.f;

  // Perpendicular is NOT a running OSNAP right now — the scenario the Shift+right-click override
  // menu exists for (issue #103): reaching a snap type the user does not keep enabled generally.
  st.objectSnapPerpendicular = false;

  CHECK_FALSE(CadSnap::CommandHasPerpendicularSnapReference(st, /*commandActive=*/true));
  CHECK(CadSnap::CommandHasPerpendicularSnapReference(st, /*commandActive=*/true, /*ignoreToggle=*/true));
}

// The Shift+Right-Click "Snap once" live override (issue #103): once a kind is armed, FindBest must
// return ONLY that kind — even when a different kind's candidate is genuinely closer to the cursor,
// and even when that OTHER kind is the only one whose persistent toggle happens to be on.

TEST_CASE("FindBest onlyKind: a closer Endpoint does not win over an armed Center override", "[CadSnap]") {
  AppCommandState st;
  // Only Endpoint is a running OSNAP — Center is off entirely.
  st.objectSnapEndpoint = true;
  st.objectSnapCenter = false;

  st.userCirclesCxCyZR = {10.f, 10.f, 0.f, 1.f};       // Center candidate, farther from the cursor
  st.userLinesFlat = {0.1f, 0.f, 0.f, 5.f, 5.f, 0.f};   // Endpoint candidate, right next to the cursor

  const Kind onlyKind = Kind::Center;
  const CadSnap::Hit hit = CadSnap::FindBest(10.0, 10.0, st, /*commandActive=*/true, kTol,
                                             /*exclude=*/{}, /*pickRay=*/nullptr, &onlyKind);

  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Center);
  CHECK(hit.x == Catch::Approx(10.f));
  CHECK(hit.y == Catch::Approx(10.f));
}

TEST_CASE("FindBest onlyKind: no candidate of the armed kind means no snap at all, even near others",
          "[CadSnap]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  // A line endpoint sits right at the cursor, but the override demands Center and there is no
  // circle anywhere in the drawing — the override must not fall back to Endpoint.
  st.userLinesFlat = {0.f, 0.f, 0.f, 5.f, 5.f, 0.f};

  const Kind onlyKind = Kind::Center;
  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/true, kTol,
                                             /*exclude=*/{}, /*pickRay=*/nullptr, &onlyKind);

  CHECK_FALSE(hit.valid);
}

TEST_CASE("Surface snap interpolates the covering TIN and misses outside", "[CadSnap][req127]") {
  AppCommandState st;
  st.objectSnapEndpoint = false;
  st.objectSnapMidpoint = false;
  st.objectSnapCenter = false;
  st.objectSnapPerpendicular = false;
  st.objectSnapSurveyPoint = false;
  st.objectSnapGeometricCenter = false;
  st.objectSnapIntersection = false;
  st.objectSnapApparentIntersection = false;
  st.objectSnapSurface = true;

  auto tin = std::make_shared<CadTin>();
  tin->vertsXyz = {0.f, 0.f, 100.f, 500.f, 0.f, 125.f, 500.f, 500.f, 135.f, 0.f, 500.f, 110.f};
  tin->indices = {0, 1, 2, 0, 2, 3};
  CadSurface surf;
  surf.name = "EG";
  surf.tin = tin;
  st.cadSurfaces.push_back(std::move(surf));
  EnsureAttrCounts(st);

  const CadSnap::Hit hit = CadSnap::FindBest(250.0, 250.0, st, /*commandActive=*/true, kTol);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Surface);
  CHECK(hit.x == Catch::Approx(250.f));
  CHECK(hit.y == Catch::Approx(250.f));
  CHECK(hit.z == Catch::Approx(117.5f).margin(0.01f));

  const CadSnap::Hit miss = CadSnap::FindBest(1000.0, 1000.0, st, /*commandActive=*/true, kTol);
  CHECK_FALSE(miss.valid);

  st.objectSnapSurface = false;
  const CadSnap::Hit off = CadSnap::FindBest(250.0, 250.0, st, /*commandActive=*/true, kTol);
  CHECK_FALSE(off.valid);
}

// REQ-107 (D-2026-08-29-i): the geometry of a placed block instance is an object-snap target —
// Endpoint on its segment ends and its insertion point, Midpoint on its segment midpoints,
// Center on its circles.
TEST_CASE("FindBest snaps to a placed block instance's geometry", "[CadSnap][issue124][block]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.objectSnapMidpoint = true;
  st.objectSnapCenter = true;

  CadBlockDefinition def;
  def.name = "SQ";
  // A unit square (0,0)->(2,0)->(2,2)->(0,2) plus a circle centred at (1,1) r=0.5.
  def.content.lines = {0.f, 0.f, 0.f, 2.f, 0.f, 0.f,
                       2.f, 0.f, 0.f, 2.f, 2.f, 0.f};
  def.content.circles = {1.f, 1.f, 0.f, 0.5f};
  st.blockDefs.push_back(def);

  CadBlockRef r;
  r.defName = "SQ";
  r.xf.x = 100.f;
  r.xf.y = 50.f;
  st.cadBlockRefs.push_back(r);

  // Corner (2,0) in local -> (102,50) world.
  const CadSnap::Hit corner = CadSnap::FindBest(102.02, 50.0, st, /*commandActive=*/true, kTol);
  REQUIRE(corner.valid);
  CHECK(corner.kind == Kind::Endpoint);
  CHECK(corner.x == Catch::Approx(102.f));
  CHECK(corner.y == Catch::Approx(50.f));

  // Insertion point itself.
  const CadSnap::Hit ins = CadSnap::FindBest(100.01, 50.01, st, /*commandActive=*/true, kTol);
  REQUIRE(ins.valid);
  CHECK(ins.kind == Kind::Endpoint);
  CHECK(ins.x == Catch::Approx(100.f));

  // Midpoint of the bottom edge: local (1,0) -> world (101,50).
  st.objectSnapEndpoint = false;
  const CadSnap::Hit mid = CadSnap::FindBest(101.0, 50.03, st, /*commandActive=*/true, kTol);
  REQUIRE(mid.valid);
  CHECK(mid.kind == Kind::Midpoint);
  CHECK(mid.x == Catch::Approx(101.f));

  // Circle centre: local (1,1) -> world (101,51).
  st.objectSnapMidpoint = false;
  const CadSnap::Hit ctr = CadSnap::FindBest(101.0, 51.0, st, /*commandActive=*/true, kTol);
  REQUIRE(ctr.valid);
  CHECK(ctr.kind == Kind::Center);
  CHECK(ctr.x == Catch::Approx(101.f));
  CHECK(ctr.y == Catch::Approx(51.f));
}

TEST_CASE("Block-instance snapping honours the snap toggles", "[CadSnap][issue124][block]") {
  AppCommandState st;
  st.objectSnapEndpoint = false;
  st.objectSnapMidpoint = false;
  st.objectSnapCenter = false;

  CadBlockDefinition def;
  def.name = "SQ";
  def.content.lines = {0.f, 0.f, 0.f, 2.f, 0.f, 0.f};
  st.blockDefs.push_back(def);
  CadBlockRef r;
  r.defName = "SQ";
  st.cadBlockRefs.push_back(r);

  const CadSnap::Hit off = CadSnap::FindBest(2.0, 0.0, st, /*commandActive=*/true, kTol);
  CHECK_FALSE(off.valid);
}

TEST_CASE("Block-instance snapping expands nested blocks", "[CadSnap][issue124][block]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;

  CadBlockDefinition leaf;
  leaf.name = "LEAF";
  leaf.content.lines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  st.blockDefs.push_back(leaf);

  CadBlockDefinition parent;
  parent.name = "PARENT";
  CadBlockNested n;
  n.defName = "LEAF";
  n.xf.x = 5.f;
  n.xf.y = 5.f;
  parent.content.nested.push_back(n);
  st.blockDefs.push_back(parent);

  CadBlockRef r;
  r.defName = "PARENT";
  r.xf.x = 10.f;
  r.xf.y = 20.f;
  st.cadBlockRefs.push_back(r);

  // Leaf endpoint (1,0) local -> (6,5) in parent -> (16,25) world.
  const CadSnap::Hit hit = CadSnap::FindBest(16.0, 25.0, st, /*commandActive=*/true, kTol);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Endpoint);
  CHECK(hit.x == Catch::Approx(16.f));
  CHECK(hit.y == Catch::Approx(25.f));
}

// ---------------------------------------------------------------------------------------------
// REQ-312 (GitHub issue #145) - object snapping on an ARBITRARY-PLANE curve, from an orbited view.
//
// A snap has one job: hand back a point that is on the object. A tilted arc walked in the XY
// projection fails that at the first step - every candidate it produces lies somewhere the curve
// does not go - and in plan view the error is invisible, because the projection is all the user can
// see. Orbit, and the glyph is plainly floating in space beside the arc it claims to have found.
//
// So these cases pass a pick RAY, which is the orbited path (REQ-058): candidates are then ranked by
// distance to the ray in 3D rather than by distance in plan, and a candidate at the wrong elevation
// is genuinely far away instead of coincidentally on top of the right one.
//
// The arc below stands on the wall y = 0: centre at the origin, radius 10, normal (0, -1, 0). Its
// own frame (`ucs::FromNormal`, the Arbitrary Axis Algorithm) is X = (1, 0, 0), Y = (0, 0, 1), so
// the point at angle t is (10 cos t, 0, 10 sin t) and the top of the arc is (0, 0, 10).
// ---------------------------------------------------------------------------------------------

namespace {

/// One arc standing on the wall y = 0, swept from \p startRad through \p sweepRad.
CadArc WallArc(float startRad, float sweepRad) {
  CadArc a;
  a.cx = 0.f;
  a.cy = 0.f;
  a.z = 0.f;
  a.r = 10.f;
  a.startRad = startRad;
  a.sweepRad = sweepRad;
  a.nx = 0.f;
  a.ny = -1.f;
  a.nz = 0.f;  // the wall y = 0, normal (0, -1, 0)
  return a;
}

/// A pick ray aimed along +Y at \p target - the shape an orbited camera looking at the wall gives.
ray3d::Ray RayAt(double tx, double ty, double tz) {
  ray3d::Ray r;
  r.origin = {tx, ty - 100.0, tz};
  r.dir = {0.0, 1.0, 0.0};
  return r;
}

constexpr float kPi = 3.14159265358979f;

} // namespace

TEST_CASE("A tilted arc's endpoint snap lands on the arc, not on its XY shadow", "[CadSnap][req312]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.objectSnapMidpoint = false;
  st.objectSnapCenter = false;
  // Swept from the top of the arc round to (-10, 0, 0), so the START point is (0, 0, 10) - a point
  // the XY parametrisation puts at (0, 10, 0) instead, ten feet away and in the wrong plane.
  st.userArcs.push_back(WallArc(kPi * 0.5f, kPi * 0.5f));

  const ray3d::Ray ray = RayAt(0.0, 0.0, 10.0);
  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/false, kTol,
                                             /*exclude=*/{}, &ray);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Endpoint);
  CHECK(hit.x == Approx(0.f).margin(1e-4));
  CHECK(hit.y == Approx(0.f).margin(1e-4));
  CHECK(hit.z == Approx(10.f).margin(1e-4));
}

TEST_CASE("A tilted arc's midpoint candidates rise with the arc", "[CadSnap][req312]") {
  AppCommandState st;
  st.objectSnapEndpoint = false;
  st.objectSnapMidpoint = true;
  st.objectSnapCenter = false;
  st.userArcs.push_back(WallArc(0.f, kPi));  // a half circle over the wall, apex at (0, 0, 10)

  // Aimed at the apex. The nearest chord midpoint sits a chord-sagitta below it - the walk uses 24
  // segments over the half circle - so this is close to 10 without being exactly 10.
  const ray3d::Ray ray = RayAt(0.0, 0.0, 10.0);
  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/false, kTol,
                                             /*exclude=*/{}, &ray);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Midpoint);
  CHECK(hit.y == Approx(0.f).margin(1e-4));  // on the wall, which the XY walk never is
  CHECK(hit.z > 9.5f);

  // And the point it returned is genuinely ON the circle: 10 from the centre, and in the plane.
  const float d = std::sqrt(hit.x * hit.x + hit.y * hit.y + hit.z * hit.z);
  CHECK(d == Approx(10.f).margin(0.05f));  // 0.05 covers the chord's own sagitta, nothing more
}

TEST_CASE("A tilted arc offers nothing where only its XY shadow would be", "[CadSnap][req312]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.objectSnapMidpoint = true;
  st.objectSnapCenter = false;
  st.userArcs.push_back(WallArc(kPi * 0.5f, kPi * 0.5f));

  // (0, 10, 0) is where the XY parametrisation puts this arc's start. Nothing is there, and the
  // snap must say so rather than offering the point it used to compute.
  const ray3d::Ray ray = RayAt(0.0, 10.0, 0.0);
  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 10.0, st, /*commandActive=*/false, kTol,
                                             /*exclude=*/{}, &ray);
  CHECK_FALSE(hit.valid);
}

TEST_CASE("A flat arc's endpoint snap is unchanged", "[CadSnap][req312]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.objectSnapMidpoint = false;
  st.objectSnapCenter = false;
  CadArc a;  // world +Z normal by default: the arc every drawing that predates REQ-312 holds
  a.cx = 0.f;
  a.cy = 0.f;
  a.r = 10.f;
  a.startRad = 0.f;
  a.sweepRad = kPi * 0.5f;
  st.userArcs.push_back(a);

  const CadSnap::Hit hit = CadSnap::FindBest(10.0, 0.0, st, /*commandActive=*/false, kTol);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Endpoint);
  CHECK(hit.x == Approx(10.f));
  CHECK(hit.y == Approx(0.f).margin(1e-5));
  CHECK(hit.z == Approx(0.f).margin(1e-5));
}

// TASK-161: ALIGN (2D Helmert) must carry a tilted arc's plane AND re-anchor its start angle, the
// same pairing ROTATE and MIRROR already do. `startRad` lives in the arc's own `ucs::FromNormal`
// frame, which turns when the normal turns, so the `startRad += rad` the ALIGN path applied on its
// own left the arc in the right plane at the right centre but swept a quarter turn off inside it.
TEST_CASE("ALIGN re-anchors a tilted arc's start after rotating its plane", "[CadSnap][req312]") {
  AppCommandState st;
  // Quarter arc on the wall y = 0: frame X = (1,0,0), Y = (0,0,1); start (10,0,0), end (0,0,10).
  st.userArcs.push_back(WallArc(0.f, kPi * 0.5f));

  // Two control pairs that solve to a pure +90 deg rotation about the origin (no scale, no shift).
  st.alignHasSelection = false;
  st.alignControlPts = {{1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, -1.f, 0.f}};
  RecalcAlignResult(st);
  REQUIRE(st.alignLastResult.valid);

  std::vector<std::string> log;
  ApplyAlignCommand(st, log, /*applyScale=*/true);

  REQUIRE(st.userArcs.size() == 1);
  // Plane turned: normal (0,-1,0) -> (1,0,0), the plane x = 0.
  CHECK(st.userArcs[0].nx == Approx(1.f).margin(1e-5));
  CHECK(st.userArcs[0].ny == Approx(0.f).margin(1e-5));

  ray3d::Vec3 s{}, e{};
  CurveEndpointsWorld(st.userArcs[0], &s, &e);
  // Start (10,0,0) rotates to (0,10,0); end (0,0,10) is on the rotation axis and stays put.
  // Without the re-anchor the start lands at (0,0,10) and the end at (0,-10,0).
  CHECK(s.x == Approx(0.0).margin(1e-4));
  CHECK(s.y == Approx(10.0).margin(1e-4));
  CHECK(s.z == Approx(0.0).margin(1e-4));
  CHECK(e.x == Approx(0.0).margin(1e-4));
  CHECK(e.y == Approx(0.0).margin(1e-4));
  CHECK(e.z == Approx(10.0).margin(1e-4));
}

// ================================================================================================
// REQ-309 — object snapping under a PERSPECTIVE camera (GitHub #144, Phase 1 of #120).
//
// REQ-309's acceptance requires snapping to resolve to the correct world coordinates under
// perspective, verified against hand-computed values within REQ-101 (0.01 ft).
//
// This is the test that could not be written as a headless transcript: the transcript driver's
// CLICK verb takes WORLD coordinates, so it never exercises the screen-pixel -> world-ray step
// that perspective actually changes. Here the pick ray is built the way the viewport builds it —
// Camera::ScreenRay through a real pixel — so the perspective path is genuinely under test.
//
// Perspective is the interesting case because its rays DIVERGE from an eye point. Under
// orthographic every ray shares the view direction, so a snap that is correct at the centre of the
// screen is correct everywhere; under perspective it need not be, which is why the endpoint used
// here sits well off-axis rather than at the camera target.

TEST_CASE("REQ-309 endpoint snap resolves under a perspective camera", "[CadSnap][req309]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;

  // A line whose FAR endpoint is the hand-computed target: (140, 90, 25).
  st.userLinesFlat = {10.f, 5.f, 25.f, 140.f, 90.f, 25.f};

  constexpr float kW = 1200.f, kH = 800.f;
  Camera cam = Camera::Plan(60.0, 40.0, 120.f);
  cam.targetZ = 10.0;
  cam.azimuthDeg = 28.f;
  cam.elevationDeg = 52.f;
  cam.projection = Camera::Projection::Perspective;
  cam.fovDeg = 55.f;

  // Project the endpoint to the pixel a user would be pointing at, then pick from that pixel.
  float px = 0.f, py = 0.f;
  cam.WorldToScreen(140.0, 90.0, 25.0, kW, kH, &px, &py);
  REQUIRE(px > 0.f);
  REQUIRE(px < kW);
  REQUIRE(py > 0.f);
  REQUIRE(py < kH);

  const ray3d::Ray ray = cam.ScreenRay(px, py, kW, kH);

  // The XY the viewport would hand FindBest is where that ray meets the work plane; the ray itself
  // is what disambiguates in 3D. Use the endpoint's own XY as the cursor position, which is what
  // pointing at it means.
  const CadSnap::Hit hit =
      CadSnap::FindBest(140.0, 90.0, st, /*commandActive=*/true, kTol, {}, &ray);

  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Endpoint);
  // REQ-101 is 0.01 ft; these are hand-computed, not read back from the same code under test.
  CHECK(hit.x == Approx(140.0).margin(0.01));
  CHECK(hit.y == Approx(90.0).margin(0.01));
  CHECK(hit.z == Approx(25.0).margin(0.01));
}

TEST_CASE("REQ-309 perspective and orthographic snap to the same endpoint", "[CadSnap][req309]") {
  // Changing projection changes how the drawing is LOOKED AT, never what it is (REQ-309), so the
  // same endpoint must be returned either way — the snap result is a property of the geometry.
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.userLinesFlat = {0.f, 0.f, 0.f, 75.f, 45.f, 12.f};

  constexpr float kW = 1000.f, kH = 700.f;
  Camera cam = Camera::Plan(30.0, 20.0, 90.f);
  cam.azimuthDeg = 15.f;
  cam.elevationDeg = 65.f;

  float ox = 0.f, oy = 0.f;
  cam.WorldToScreen(75.0, 45.0, 12.0, kW, kH, &ox, &oy);
  const ray3d::Ray orthoRay = cam.ScreenRay(ox, oy, kW, kH);
  const CadSnap::Hit orthoHit =
      CadSnap::FindBest(75.0, 45.0, st, /*commandActive=*/true, kTol, {}, &orthoRay);

  cam.projection = Camera::Projection::Perspective;
  cam.fovDeg = 50.f;
  float ppx = 0.f, ppy = 0.f;
  cam.WorldToScreen(75.0, 45.0, 12.0, kW, kH, &ppx, &ppy);
  const ray3d::Ray perspRay = cam.ScreenRay(ppx, ppy, kW, kH);
  const CadSnap::Hit perspHit =
      CadSnap::FindBest(75.0, 45.0, st, /*commandActive=*/true, kTol, {}, &perspRay);

  REQUIRE(orthoHit.valid);
  REQUIRE(perspHit.valid);
  CHECK(orthoHit.kind == perspHit.kind);
  CHECK(perspHit.x == Approx(orthoHit.x).margin(0.01));
  CHECK(perspHit.y == Approx(orthoHit.y).margin(0.01));
  CHECK(perspHit.z == Approx(orthoHit.z).margin(0.01));
}

// ---------------------------------------------------------------------------------------------
// B-rep solid snapping (REQ-313 / ADR-045 addendum (g)).
//
// Driven through the real `FindBest` with a pick ray, the same way the REQ-312 arc cases above are.
// The failure that matters here is not "no snap": it is a snap that returns a point NEAR the solid
// instead of ON it. A face hit taken straight off the tessellation is short of a curved surface by
// the chord's sagitta — small, plausible, and smaller every time the user zooms in to check it.
// ---------------------------------------------------------------------------------------------

namespace {

/// Put one solid in the drawing and bring the display cache up to date, which is what the frame loop
/// does before any pick can happen. Face snapping reads that cache, so a test that skipped this
/// would be testing a solid that has not been drawn yet.
void InstallSolid(AppCommandState& st, brep::Solid s) {
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(s)));
  st.cadSolidAttrs.push_back(EntityAttributes{});
  RefreshSolidDisplayGeometry(st);
}

}  // namespace

TEST_CASE("A solid's corner answers Endpoint and its edge answers Edge", "[CadSnap][req313]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.objectSnapMidpoint = false;
  st.objectSnapSolid = true;

  // A 20 x 20 x 20 box on the origin: x and y span +/-10, z runs 0 to 20.
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 20.0, 20.0, &box, &why));
  InstallSolid(st, std::move(box));

  SECTION("a corner is an Endpoint") {
    const ray3d::Ray ray = RayAt(-10.0, -10.0, 20.0);
    const CadSnap::Hit hit = CadSnap::FindBest(-10.0, -10.0, st, false, kTol, {}, &ray);
    REQUIRE(hit.valid);
    CHECK(hit.kind == Kind::Endpoint);
    CHECK(hit.x == Approx(-10.f).margin(1e-4));
    CHECK(hit.y == Approx(-10.f).margin(1e-4));
    CHECK(hit.z == Approx(20.f).margin(1e-4));
  }

  SECTION("mid-way up a vertical edge is an Edge, and Endpoint does not reach it") {
    // Half way up the corner post at (-10, -10): 10 feet from either end, so no vertex is anywhere
    // near the cursor and the only honest answer is a point along the edge.
    const ray3d::Ray ray = RayAt(-10.0, -10.0, 10.0);
    const CadSnap::Hit hit = CadSnap::FindBest(-10.0, -10.0, st, false, /*tolWorld=*/2.f, {}, &ray);
    REQUIRE(hit.valid);
    CHECK(hit.kind == Kind::Edge);
    CHECK(hit.x == Approx(-10.f).margin(1e-6));
    CHECK(hit.y == Approx(-10.f).margin(1e-6));
    CHECK(hit.z == Approx(10.f).margin(1e-6));
  }

  SECTION("a point in mid-air near nothing offers nothing") {
    const ray3d::Ray ray = RayAt(500.0, 500.0, 500.0);
    const CadSnap::Hit hit = CadSnap::FindBest(500.0, 500.0, st, false, kTol, {}, &ray);
    CHECK_FALSE(hit.valid);
  }
}

TEST_CASE("A face snap lands on the surface, not on the tessellator's chord", "[CadSnap][req313]") {
  AppCommandState st;
  st.objectSnapEndpoint = false;
  st.objectSnapMidpoint = false;
  st.objectSnapSolid = true;

  // A cylinder of radius 10 rising 20 from the origin.
  brep::Solid cyl;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeCylinder(ucs::Ucs{}, 10.0, 20.0, &cyl, &why));
  InstallSolid(st, std::move(cyl));

  // Aimed at the wall half way up, along a direction that is not one of the tessellation's own
  // vertex angles — so the ray meets a chord strictly inside the true surface, which is exactly the
  // case a raw triangle hit would get wrong.
  ray3d::Ray ray;
  ray.origin = {-70.0, -103.0, 10.0};
  ray.dir = ray3d::Normalize(ray3d::Vec3{0.55, 0.835, 0.0});

  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, false, /*tolWorld=*/60.f, {}, &ray);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Face);
  // ON the cylinder: exactly radius 10 from the axis. A point taken off the chord would be up to a
  // sagitta short of this — the chord tolerance is 0.01 ft, four orders of magnitude outside the
  // margin below, so this assertion can only pass if the analytic projection happened.
  const double r = std::sqrt(static_cast<double>(hit.x) * hit.x + static_cast<double>(hit.y) * hit.y);
  CHECK(r == Approx(10.0).margin(1e-6));
  // And the projection moves a point radially only — the height it was found at is kept.
  CHECK(hit.z == Approx(10.f).margin(1e-6));
}

TEST_CASE("A solid on an off layer offers no snap at all", "[CadSnap][req313]") {
  // The rule every kind is held to: invisible and unclickable must not be able to disagree
  // (REQ-084 (d)). Snapping is the third surface that rule has to hold on, after drawing and
  // picking, and it is the easiest one to forget.
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.objectSnapSolid = true;

  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 20.0, 20.0, &box, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(box)));
  EntityAttributes attr{};
  attr.layer = "HIDDEN-LAYER";
  st.cadSolidAttrs.push_back(attr);
  CadLayerRow row;
  row.name = "HIDDEN-LAYER";
  row.on = false;
  st.drawingLayerTable.push_back(row);
  RefreshSolidDisplayGeometry(st);

  const ray3d::Ray ray = RayAt(-10.0, -10.0, 20.0);
  const CadSnap::Hit hit = CadSnap::FindBest(-10.0, -10.0, st, false, kTol, {}, &ray);
  CHECK_FALSE(hit.valid);
}
