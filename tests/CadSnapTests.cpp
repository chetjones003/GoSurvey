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

TEST_CASE("A tilted polyline curve segment's midpoint follows the true arc, not its chord",
         "[CadSnap][req325]") {
  // REQ-325 / ADR-053 increment 3. A single-segment open polyline standing on the wall y = 0, same
  // shape family as WallArc: vertex A = (0,0,0), vertex B = (20,0,0), bulge = 1 (a half circle),
  // normal (0,-1,0). The plane FromNormal builds from A as origin gives xAxis=(1,0,0),
  // yAxis=(0,0,1), so local (10,0) is the arc's centre, world (10,0,0); the half circle's apex at
  // local (10,-10) is world (10,0,-10) -- NOT the chord midpoint (10,0,0) the pre-fix code always
  // returned regardless of bulge (a real gap: even a FLAT bulge segment's Midpoint was chord-only).
  AppCommandState st;
  st.objectSnapEndpoint = false;
  st.objectSnapMidpoint = true;
  st.objectSnapCenter = false;
  st.userPolylineVerts = {0.f, 0.f, 0.f, 20.f, 0.f, 0.f};
  st.userPolylineOffsets = {0, 2};
  st.userPolylineClosed = {0};
  st.userPolylineAttrs.emplace_back();
  st.userPolylineVertsBulge = {1.f, 0.f};
  st.userPolylineVertsNormal = {0.f, -1.f, 0.f, 0.f, 0.f, 1.f};

  const ray3d::Ray ray = RayAt(10.0, 0.0, -10.0);
  const CadSnap::Hit hit = CadSnap::FindBest(10.0, 0.0, st, /*commandActive=*/false, kTol,
                                             /*exclude=*/{}, &ray);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Midpoint);
  CHECK(hit.y == Approx(0.f).margin(1e-4));  // on the wall — the old chord path never left y = 0 either
  CHECK(hit.z < -9.f);                       // near the true apex (-10), nowhere near the chord's 0
  // Genuinely on the circle: distance 10 from (10, 0, 0), the world centre this plane implies.
  const float d = std::hypot(hit.x - 10.f, hit.z);
  CHECK(d == Approx(10.f).margin(0.1f));  // 0.1 covers the 24-chord sagitta at this radius
}

TEST_CASE("A flat polyline curve segment's midpoint follows the true arc too", "[CadSnap][req325]") {
  // Same fix, the FLAT case: bulge already existed (REQ-316), but Midpoint on it was still the
  // straight chord — never actually curve-aware even for the simple case.
  AppCommandState st;
  st.objectSnapEndpoint = false;
  st.objectSnapMidpoint = true;
  st.objectSnapCenter = false;
  st.userPolylineVerts = {0.f, 0.f, 0.f, 20.f, 0.f, 0.f};
  st.userPolylineOffsets = {0, 2};
  st.userPolylineClosed = {0};
  st.userPolylineAttrs.emplace_back();
  st.userPolylineVertsBulge = {1.f, 0.f};
  // No normal array at all — the pre-existing default, world +Z, flat.

  const CadSnap::Hit hit =
      CadSnap::FindBest(10.0, -10.0, st, /*commandActive=*/false, kTol, /*exclude=*/{}, nullptr);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Midpoint);
  // The flat apex is (10, -10, 0) — a half circle of radius 10 centred at (10, 0, 0) in the XY
  // plane (BulgeArc's own sign convention) — genuinely different from the chord midpoint
  // (10, 0, 0) the old code always returned.
  CHECK(hit.y < -9.f);
  CHECK(hit.z == Approx(0.f).margin(1e-4));
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

// GitHub #185: a selective ALIGN filtered the circle store (`userCirclesCxCyZR`, stride 4) with
// `sCircles.count(i / 3)`. `floor(4k / 3) == k` only for k <= 2, so a drawing with four or more
// circles transformed the wrong ones. This case must use FIVE circles and select index 4 — a
// three-circle case passes against the unfixed `i / 3`, which is how the bug shipped.
TEST_CASE("ALIGN on a selection transforms only the selected circle (#185)", "[CadSnap][regression]") {
  AppCommandState st;
  // Five circles on the x axis, centres 100 apart, radius 5, in world XY.
  for (int k = 0; k < 5; ++k) {
    st.userCirclesCxCyZR.insert(st.userCirclesCxCyZR.end(),
                                {100.f * static_cast<float>(k), 0.f, 0.f, 5.f});
    st.userCircleNormals.insert(st.userCircleNormals.end(), {0.f, 0.f, 1.f});
  }

  // Select only circle 4 (centre (400, 0)).
  st.selection = {SelectedEntity{SelectedEntity::Type::Circle, 4}};
  st.alignSelectionSnapshot = st.selection;
  st.alignHasSelection      = true;

  // Two control pairs solving to a pure translation of (+100, +50): no rotation, no scale.
  st.alignControlPts = {{0.f, 0.f, 100.f, 50.f}, {10.f, 0.f, 110.f, 50.f}};
  RecalcAlignResult(st);
  REQUIRE(st.alignLastResult.valid);

  std::vector<std::string> log;
  ApplyAlignCommand(st, log, /*applyScale=*/true);

  REQUIRE(st.userCirclesCxCyZR.size() == 20);
  // Circle 4 moved by (+100, +50); every other circle stayed exactly where it was.
  for (int k = 0; k < 5; ++k) {
    const float expX = 100.f * static_cast<float>(k) + (k == 4 ? 100.f : 0.f);
    const float expY = (k == 4 ? 50.f : 0.f);
    CHECK(st.userCirclesCxCyZR[static_cast<size_t>(k) * 4 + 0] == Approx(expX).margin(1e-3));
    CHECK(st.userCirclesCxCyZR[static_cast<size_t>(k) * 4 + 1] == Approx(expY).margin(1e-3));
    CHECK(st.userCirclesCxCyZR[static_cast<size_t>(k) * 4 + 3] == Approx(5.f).margin(1e-3));
  }
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
  // REQ-101 is 0.002 ft; these are hand-computed, not read back from the same code under test.
  CHECK(hit.x == Approx(140.0).margin(0.002));
  CHECK(hit.y == Approx(90.0).margin(0.002));
  CHECK(hit.z == Approx(25.0).margin(0.002));
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
  CHECK(perspHit.x == Approx(orthoHit.x).margin(0.002));
  CHECK(perspHit.y == Approx(orthoHit.y).margin(0.002));
  CHECK(perspHit.z == Approx(orthoHit.z).margin(0.002));
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
  st.objectSnap3dEnabled = true;
  st.objectSnap3dVertex = true;
  st.objectSnap3dMidpointEdge = false;
  st.objectSnap3dNearestFace = true;

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
  st.objectSnap3dEnabled = true;
  st.objectSnap3dVertex = false;
  st.objectSnap3dMidpointEdge = false;
  st.objectSnap3dNearestFace = true;

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
  st.objectSnap3dEnabled = true;
  st.objectSnap3dVertex = true;

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

// ---------------------------------------------------------------------------------------------
// GitHub issue #395 / REQ-325 — the 3D Object Snap tab: Vertex, Midpoint on edge, Center of face
// (every face type including NURBS), Nearest to face, Perpendicular, Knot, and the F4 master gate
// being independent of 2D Object Snap (F3).
// ---------------------------------------------------------------------------------------------

TEST_CASE("3D Object Snap Vertex fires under its own flag and is gated by the F4 master", "[CadSnap][issue395]") {
  AppCommandState st;
  st.objectSnapEndpoint = false;  // 2D Endpoint OFF — proves solid Vertex no longer rides on it.
  st.objectSnap3dVertex = true;

  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 20.0, 20.0, &box, &why));
  InstallSolid(st, std::move(box));

  const ray3d::Ray ray = RayAt(-10.0, -10.0, 20.0);

  SECTION("F4 on: vertex snaps") {
    st.objectSnap3dEnabled = true;
    const CadSnap::Hit hit = CadSnap::FindBest(-10.0, -10.0, st, false, kTol, {}, &ray);
    REQUIRE(hit.valid);
    CHECK(hit.kind == Kind::Endpoint);
    CHECK(hit.x == Approx(-10.f).margin(1e-4));
    CHECK(hit.y == Approx(-10.f).margin(1e-4));
    CHECK(hit.z == Approx(20.f).margin(1e-4));
  }

  SECTION("F4 off: no solid-derived kind fires even though every per-mode flag is true") {
    st.objectSnap3dEnabled = false;
    st.objectSnap3dVertex = true;
    st.objectSnap3dMidpointEdge = true;
    st.objectSnap3dNearestFace = true;
    st.objectSnap3dCenterFace = true;
    const CadSnap::Hit hit = CadSnap::FindBest(-10.0, -10.0, st, false, kTol, {}, &ray);
    CHECK_FALSE(hit.valid);
  }
}

TEST_CASE("3D Object Snap Midpoint-on-edge", "[CadSnap][issue395]") {
  AppCommandState st;
  st.objectSnapMidpoint = false;  // 2D Midpoint OFF — proves independence from F3.
  st.objectSnap3dEnabled = true;
  st.objectSnap3dMidpointEdge = true;

  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 20.0, 20.0, &box, &why));
  InstallSolid(st, std::move(box));

  const ray3d::Ray ray = RayAt(-10.0, -10.0, 10.0);
  const CadSnap::Hit hit = CadSnap::FindBest(-10.0, -10.0, st, false, /*tolWorld=*/2.f, {}, &ray);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Midpoint);
  CHECK(hit.x == Approx(-10.f).margin(1e-6));
  CHECK(hit.y == Approx(-10.f).margin(1e-6));
  CHECK(hit.z == Approx(10.f).margin(1e-6));
}

TEST_CASE("3D Object Snap Nearest-to-face still works under the renamed flag", "[CadSnap][issue395]") {
  AppCommandState st;
  st.objectSnap3dEnabled = true;
  st.objectSnap3dNearestFace = true;

  brep::Solid cyl;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeCylinder(ucs::Ucs{}, 10.0, 20.0, &cyl, &why));
  InstallSolid(st, std::move(cyl));

  ray3d::Ray ray;
  ray.origin = {-70.0, -103.0, 10.0};
  ray.dir = ray3d::Normalize(ray3d::Vec3{0.55, 0.835, 0.0});

  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, false, /*tolWorld=*/60.f, {}, &ray);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Face);
  const double r = std::sqrt(static_cast<double>(hit.x) * hit.x + static_cast<double>(hit.y) * hit.y);
  CHECK(r == Approx(10.0).margin(1e-6));
}

TEST_CASE("3D Object Snap Center-of-face on a box's planar face is the geometric center", "[CadSnap][issue395]") {
  AppCommandState st;
  st.objectSnap3dEnabled = true;
  st.objectSnap3dCenterFace = true;
  st.objectSnap3dNearestFace = false;  // isolate Center-of-face: on this ray they'd tie on distance.

  // 20 x 20 x 20 box: the top face (z = 20) spans x,y in [-10, 10], so its center is (0, 0, 20).
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 20.0, 20.0, &box, &why));
  InstallSolid(st, std::move(box));

  ray3d::Ray ray;
  ray.origin = {0.0, 0.0, 100.0};
  ray.dir = ray3d::Normalize(ray3d::Vec3{0.0, 0.0, -1.0});

  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, false, /*tolWorld=*/1.f, {}, &ray);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::CenterOfFace);
  CHECK(hit.x == Approx(0.f).margin(1e-4));
  CHECK(hit.y == Approx(0.f).margin(1e-4));
  CHECK(hit.z == Approx(20.f).margin(1e-4));
}

TEST_CASE("3D Object Snap Center-of-face on a cylinder end-cap lands on the axis, not a vertex average",
         "[CadSnap][issue395]") {
  // The end-cap's rim is ONE circular edge with two coincident-parameter topology vertices — if
  // Center-of-face naively averaged those two vertices instead of sampling the circle, it would land
  // on the rim rather than at the true centroid (the axis). This is exactly the case the issue's
  // acceptance criteria calls out.
  AppCommandState st;
  st.objectSnap3dEnabled = true;
  st.objectSnap3dCenterFace = true;
  st.objectSnap3dNearestFace = false;  // isolate Center-of-face: on this ray they'd tie on distance.

  brep::Solid cyl;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeCylinder(ucs::Ucs{}, 10.0, 20.0, &cyl, &why));
  InstallSolid(st, std::move(cyl));

  // Aim straight down at the top cap (z = 20), centered on the axis.
  ray3d::Ray ray;
  ray.origin = {0.0, 0.0, 100.0};
  ray.dir = ray3d::Normalize(ray3d::Vec3{0.0, 0.0, -1.0});

  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, false, /*tolWorld=*/1.f, {}, &ray);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::CenterOfFace);
  CHECK(hit.x == Approx(0.f).margin(1e-3));
  CHECK(hit.y == Approx(0.f).margin(1e-3));
  CHECK(hit.z == Approx(20.f).margin(1e-3));
}

TEST_CASE("3D Object Snap Perpendicular lands at the foot on a planar face", "[CadSnap][issue395]") {
  AppCommandState st;
  st.objectSnap3dEnabled = true;
  st.objectSnap3dPerpendicular = true;
  st.objectSnap3dNearestFace = false;  // isolate Perpendicular: on this ray they'd tie on distance.
  // A LINE in progress, previous point at (2, 2) — the perpendicular reference (REQ-325/#395 reuses
  // the same command-reference machinery the 2D Perpendicular toggle already has). On a horizontal
  // face the foot of the perpendicular from any (x,y,*) keeps that same X/Y and only moves in Z, so
  // the reference is placed directly under the ray for the foot to land where the cursor is aiming.
  st.active = AppCommandState::Kind::Line;
  st.linePhase = AppCommandState::LinePhase::NeedNextPoint;
  st.anchorX = 2.0;
  st.anchorY = 2.0;

  // 20 x 20 x 20 box: the top face (z = 20) is the plane z = 20 for any x,y in range.
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 20.0, 20.0, &box, &why));
  InstallSolid(st, std::move(box));

  ray3d::Ray ray;
  ray.origin = {2.0, 2.0, 100.0};
  ray.dir = ray3d::Normalize(ray3d::Vec3{0.0, 0.0, -1.0});

  const CadSnap::Hit hit = CadSnap::FindBest(2.0, 2.0, st, /*commandActive=*/true, /*tolWorld=*/1.f, {}, &ray);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Perpendicular);
  // The foot of the perpendicular from (2,2,*) onto the plane z=20 is (2, 2, 20) — a plane's foot
  // from any point off it does not move in X/Y, only in Z.
  CHECK(hit.x == Approx(2.f).margin(1e-3));
  CHECK(hit.y == Approx(2.f).margin(1e-3));
  CHECK(hit.z == Approx(20.f).margin(1e-3));
}

TEST_CASE("3D Object Snap Knot enumerates a NURBS patch's distinct knot values", "[CadSnap][issue395]") {
  // Constructing a full LOFT/SWEEP NURBS solid fixture is disproportionate for this unit test, so —
  // per the issue's own guidance — the knot-enumeration/evaluation logic is verified directly against
  // a hand-built `nurbs::Patch` with a known clamped knot vector, independent of the full solid pipeline.
  // Degree 2, 4 control points per direction: clamped knot vector [0,0,0,0.5,1,1,1] has ONE interior
  // distinct knot, 0.5, beyond the clamped ends.
  nurbs::Patch patch;
  patch.degU = 2;
  patch.degV = 2;
  patch.nu = 4;
  patch.nv = 4;
  patch.knotsU = {0.0, 0.0, 0.0, 0.5, 1.0, 1.0, 1.0};
  patch.knotsV = {0.0, 0.0, 0.0, 0.5, 1.0, 1.0, 1.0};
  patch.ctrl.assign(static_cast<size_t>(patch.nu * patch.nv), ray3d::Vec3{});
  patch.wts.assign(static_cast<size_t>(patch.nu * patch.nv), 1.0);
  for (int j = 0; j < patch.nv; ++j) {
    for (int i = 0; i < patch.nu; ++i) {
      patch.ctrl[static_cast<size_t>(j * patch.nu + i)] =
          ray3d::Vec3{static_cast<double>(i), static_cast<double>(j), 0.0};
    }
  }

  // Distinct in-range knot values (matching CadSnap.cpp's own dedup rule): 0.0, 0.5, 1.0 -> 3 values,
  // so the 2D grid is 3x3 = 9 points, and every one should lie exactly on the flat z=0 patch.
  std::vector<double> distinct;
  for (double k : patch.knotsU) {
    if (distinct.empty() || std::fabs(distinct.back() - k) > 1e-9)
      distinct.push_back(k);
  }
  REQUIRE(distinct.size() == 3);
  for (double u : distinct) {
    for (double v : distinct) {
      const ray3d::Vec3 p = nurbs::Evaluate(patch, u, v);
      CHECK(p.z == Approx(0.0).margin(1e-9));
    }
  }
}

// ---------------------------------------------------------------------------------------------
// GitHub issue #372 — CENTRE-family object snaps from an orbited (non-plan) 3D view.
//
// In plan view a circle / ellipse / closed-polyline / survey-point CENTRE is offered whenever the
// cursor is over the SHAPE, not only near the centroid: `CircleCenterPickDistSq` and friends return
// 0 for a cursor inside the footprint. The orbited path (issue #103 / REQ-058) re-measured every
// candidate as the distance from the cursor ray to the exact snap point, so that heuristic — where
// the point is a whole radius from the rim under the cursor — was lost and CENTRE could never be
// acquired under orbit. The fix evaluates the heuristic at the cursor ray's crossing of the shape's
// OWN plane (`RayXyAtPlaneZ`) and keeps it as the acceptance test, while ranking stays the true ray
// distance. Testing at the shape plane (not the work plane, which `wx/wy` already is) is what stops
// a ray that merely passes over the footprint from firing a phantom snap.
// ---------------------------------------------------------------------------------------------

namespace {

/// A pick ray from an orbited eye (above and to the side) aimed through world point \p p — the
/// shape an isometric-ish camera gives. Not edge-on to the world-XY plane, so shapes drawn in plan
/// still have a well-defined "the cursor is pointing here" crossing.
ray3d::Ray OrbitRayThrough(double px, double py, double pz) {
  ray3d::Ray r;
  r.origin = {px - 18.0, py - 26.0, pz + 30.0};
  r.dir = ray3d::Normalize(ray3d::Vec3{px - r.origin.x, py - r.origin.y, pz - r.origin.z});
  return r;
}

} // namespace

TEST_CASE("Orbited: a circle CENTRE is acquired with the cursor over the rim, not the centroid",
          "[CadSnap][issue372]") {
  AppCommandState st;
  st.objectSnapCenter = true;
  st.objectSnapEndpoint = true;

  // Circle radius 10 in the world-XY plane, centred at the origin.
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 10.f};

  // Cursor pointing at the rim at (7, 0, 0) — 7 ft from the true centre, outside kTol (5). Before
  // the fix the ray-to-centre distance failed the tolerance test and nothing was returned.
  const ray3d::Ray ray = OrbitRayThrough(7.0, 0.0, 0.0);
  const CadSnap::Hit hit = CadSnap::FindBest(7.0, 0.0, st, /*commandActive=*/true, kTol, {}, &ray);

  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Center);
  CHECK(hit.x == Approx(0.f).margin(1e-4));
  CHECK(hit.y == Approx(0.f).margin(1e-4));
  CHECK(hit.z == Approx(0.f).margin(1e-4));
}

TEST_CASE("Orbited: a genuinely closer endpoint still out-ranks the circle CENTRE heuristic",
          "[CadSnap][issue372]") {
  AppCommandState st;
  st.objectSnapCenter = true;
  st.objectSnapEndpoint = true;

  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 10.f};
  // A line whose endpoint sits exactly where the cursor points, on the circle's rim. The CENTRE
  // heuristic accepts here too, but ranking is the true ray distance, so the endpoint wins.
  st.userLinesFlat = {7.f, 0.f, 0.f, 40.f, 25.f, 0.f};

  const ray3d::Ray ray = OrbitRayThrough(7.0, 0.0, 0.0);
  const CadSnap::Hit hit = CadSnap::FindBest(7.0, 0.0, st, /*commandActive=*/true, kTol, {}, &ray);

  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Endpoint);
  CHECK(hit.x == Approx(7.f).margin(1e-4));
}

TEST_CASE("Orbited: no phantom CENTRE when the ray passes over the footprint but not the shape",
          "[CadSnap][issue372]") {
  // A shallow orbit over a large circle (radius 100) in the world-XY plane. The cursor ray passes
  // ABOVE the disc and crosses z = 0 at (0, 145) — 45 ft outside the rim — yet its closest approach
  // to the centre POINT is only ~29 ft. A reach test around the centre point would accept; testing
  // at the ray's crossing of the circle's own plane correctly rejects.
  AppCommandState st;
  st.objectSnapCenter = true;
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 100.f};

  ray3d::Ray ray;
  ray.origin = {0.0, -100.0, 50.0};
  ray.dir = ray3d::Normalize(ray3d::Vec3{0.0, 0.98, -0.2});
  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/true, kTol, {}, &ray);

  CHECK_FALSE(hit.valid);
}

TEST_CASE("Orbited edge-on: circle CENTRE falls back to ray-proximity when the shape plane has no crossing",
          "[CadSnap][issue372]") {
  AppCommandState st;
  st.objectSnapCenter = true;
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 10.f};

  // FRONT view: a horizontal ray lying in the circle's own plane. There is no ray/plane crossing,
  // so the "cursor is over the shape" heuristic cannot apply — but a ray aimed straight through the
  // centre still resolves CENTRE, exactly as it did before issue #372.
  ray3d::Ray onAxis;
  onAxis.origin = {0.0, -100.0, 0.0};
  onAxis.dir = {0.0, 1.0, 0.0};
  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/true, kTol, {}, &onAxis);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Center);

  // ...but NOT from over the rim in that same edge-on view — the heuristic that would allow it is
  // unavailable, and the exact centre is a full radius from the ray.
  ray3d::Ray offRim;
  offRim.origin = {7.0, -100.0, 0.0};
  offRim.dir = {0.0, 1.0, 0.0};
  CHECK_FALSE(CadSnap::FindBest(7.0, 0.0, st, /*commandActive=*/true, kTol, {}, &offRim).valid);
}

TEST_CASE("Orbited grazing: a small circle CENTRE still resolves when the ray points nearly at it",
          "[CadSnap][issue372]") {
  // A shape smaller than the aperture, seen at a shallow (but not edge-on) angle. The pick ray
  // passes ~3 ft from the centre in space — inside kTol (5) — but its crossing of z = 0 is 75 ft
  // away, far outside the 1-ft disc. "Over the shape" alone would reject; accepting on the smaller
  // of the shape heuristic and the true ray distance keeps the pre-#372 envelope.
  AppCommandState st;
  st.objectSnapCenter = true;
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 1.f};

  ray3d::Ray ray;
  ray.origin = {0.0, -50.0, 5.0};
  ray.dir = ray3d::Normalize(ray3d::Vec3{0.0, 50.0, -2.0});
  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/true, kTol, {}, &ray);

  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Center);
  CHECK(hit.x == Approx(0.f).margin(1e-4));
  CHECK(hit.y == Approx(0.f).margin(1e-4));
}

TEST_CASE("Orbited: an ellipse CENTRE is acquired with the cursor over the body", "[CadSnap][issue372]") {
  AppCommandState st;
  st.objectSnapCenter = true;
  st.objectSnapMidpoint = false;  // isolate the CENTRE candidate — perimeter midpoints are their own path

  CadEllipse el;
  el.cx = 0.f;
  el.cy = 0.f;
  el.z = 0.f;
  el.majVx = 12.f;  // major axis along world +X, half-length 12
  el.majVy = 0.f;
  el.ratio = 0.5f;  // minor half-length 6
  st.userEllipses.push_back(el);

  // Pointing at the body at (8, 0, 0) — inside the ellipse, 8 ft from the centre, outside kTol.
  const ray3d::Ray ray = OrbitRayThrough(8.0, 0.0, 0.0);
  const CadSnap::Hit hit = CadSnap::FindBest(8.0, 0.0, st, /*commandActive=*/true, kTol, {}, &ray);

  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Center);
  CHECK(hit.x == Approx(0.f).margin(1e-4));
  CHECK(hit.y == Approx(0.f).margin(1e-4));
}

TEST_CASE("Orbited: a survey point CENTRE is acquired from over its marker at a zoomed-out scale",
          "[CadSnap][issue372]") {
  AppCommandState st;
  st.objectSnapSurveyPoint = true;

  SurveyPoint sp;
  sp.easting = 0.f;
  sp.northing = 0.f;
  sp.elevation = 0.f;
  st.surveyPoints.push_back(sp);
  // Zoomed out: the X marker's half-span is many apertures wide in world units.
  st.surveyPointCrossSpanPlottedInches = 0.2f;
  st.modelUnitsPerPlottedInch = 60.f;  // -> marker half-span ~6 ft, well over kTol

  // Pointing at the marker arm ~4 ft off the point — inside the drawn X, outside kTol.
  const ray3d::Ray ray = OrbitRayThrough(4.0, 0.0, 0.0);
  const CadSnap::Hit hit = CadSnap::FindBest(4.0, 0.0, st, /*commandActive=*/true, kTol, {}, &ray);

  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::SurveyCenter);
  CHECK(hit.x == Approx(0.f).margin(1e-4));
  CHECK(hit.y == Approx(0.f).margin(1e-4));
}

// --- REQ-330 / GitHub issue #401: Quadrant object snap for circles and arcs -------------------
//
// The four "compass" points of a circle/arc: one radius from the centre along the active UCS X and
// Y axes, projected onto the curve's own plane. N/E/S/W in a TOP view with the world UCS.

namespace {
// Only the Quadrant toggle on, so nothing else can answer and the assertions are unambiguous.
AppCommandState QuadOnlyState() {
  AppCommandState st;
  st.objectSnapEndpoint = false;
  st.objectSnapMidpoint = false;
  st.objectSnapCenter = false;
  st.objectSnapPerpendicular = false;
  st.objectSnapSurveyPoint = false;
  st.objectSnapGeometricCenter = false;
  st.objectSnapIntersection = false;
  st.objectSnapQuadrant = true;
  return st;
}
} // namespace

TEST_CASE("Quadrant snap TOP view world UCS offers the circle's N/E/S/W points", "[CadSnap][issue401]") {
  AppCommandState st = QuadOnlyState();
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 10.f};  // centre origin, radius 10, flat

  const CadSnap::Hit east = CadSnap::FindBest(9.5, 0.0, st, /*commandActive=*/true, kTol);
  REQUIRE(east.valid);
  CHECK(east.kind == Kind::Quadrant);
  CHECK(east.x == Approx(10.f).margin(1e-3));
  CHECK(east.y == Approx(0.f).margin(1e-3));

  const CadSnap::Hit north = CadSnap::FindBest(0.0, 9.5, st, /*commandActive=*/true, kTol);
  REQUIRE(north.valid);
  CHECK(north.x == Approx(0.f).margin(1e-3));
  CHECK(north.y == Approx(10.f).margin(1e-3));

  const CadSnap::Hit west = CadSnap::FindBest(-9.5, 0.0, st, /*commandActive=*/true, kTol);
  REQUIRE(west.valid);
  CHECK(west.x == Approx(-10.f).margin(1e-3));

  const CadSnap::Hit south = CadSnap::FindBest(0.0, -9.5, st, /*commandActive=*/true, kTol);
  REQUIRE(south.valid);
  CHECK(south.y == Approx(-10.f).margin(1e-3));
}

TEST_CASE("Quadrant snap rotates to follow a UCS turned about Z", "[CadSnap][issue401]") {
  AppCommandState st = QuadOnlyState();
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 10.f};
  st.activeUcs = ucs::RotatedAboutZ(ucs::Ucs{}, 40.0);  // plan view, UCS spun 40 degrees

  // The +UCS-X quadrant is now 10 * (cos40, sin40) = (7.6604, 6.4279).
  const float qx = 10.f * std::cos(40.f * kPi / 180.f);
  const float qy = 10.f * std::sin(40.f * kPi / 180.f);
  const CadSnap::Hit hit = CadSnap::FindBest(qx, qy, st, /*commandActive=*/true, kTol);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Quadrant);
  CHECK(hit.x == Approx(qx).margin(1e-3));
  CHECK(hit.y == Approx(qy).margin(1e-3));

  // And the world-X point (10, 0) is no longer a quadrant of this circle.
  const CadSnap::Hit worldEast = CadSnap::FindBest(10.0, 0.0, st, /*commandActive=*/true, /*tolWorld=*/1.0f);
  CHECK_FALSE(worldEast.valid);
}

TEST_CASE("Quadrant snap keeps all four points on a tilted circle from an orbited camera",
         "[CadSnap][issue401]") {
  AppCommandState st = QuadOnlyState();
  // Circle tilted 45 degrees about world X: normal (0, -sin45, cos45).
  const float s = std::sqrt(0.5f);
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 10.f};
  st.userCircleNormals = {0.f, -s, s};
  // World UCS. +X projects onto the plane unchanged; +Y projects to (0, 0.7071, 0.7071) normalised,
  // so that quadrant point is 10 * (0, 0.7071, 0.7071) = (0, 7.071, 7.071).
  const ray3d::Ray ray = RayAt(0.0, 7.071, 7.071);
  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 7.071, st, /*commandActive=*/false, kTol,
                                             /*exclude=*/{}, &ray);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Quadrant);
  // On the circle: 10 from the centre.
  const float d = std::sqrt(hit.x * hit.x + hit.y * hit.y + hit.z * hit.z);
  CHECK(d == Approx(10.f).margin(1e-2));
  // On the circle's plane: (point - centre) . normal == 0.
  CHECK(hit.y * (-s) + hit.z * s == Approx(0.f).margin(1e-2));

  // The +X quadrant is still exactly (10, 0, 0).
  const ray3d::Ray rayX = RayAt(10.0, 0.0, 0.0);
  const CadSnap::Hit hx = CadSnap::FindBest(10.0, 0.0, st, /*commandActive=*/false, kTol,
                                            /*exclude=*/{}, &rayX);
  REQUIRE(hx.valid);
  CHECK(hx.x == Approx(10.f).margin(1e-2));
  CHECK(hx.y == Approx(0.f).margin(1e-2));
  CHECK(hx.z == Approx(0.f).margin(1e-2));
}

TEST_CASE("Quadrant snap falls back to the circle's own axes when its plane is perpendicular to the UCS",
         "[CadSnap][issue401]") {
  AppCommandState st = QuadOnlyState();
  // The "wall" circle: normal (0, -1, 0), so its plane is perpendicular to the world UCS plane.
  // Projecting +UCS-Y onto it degenerates, so the snap uses the circle plane's local axes:
  // FromNormal((0,-1,0)) -> xAxis (1,0,0), yAxis (0,0,1). Four points: (10,0,0), (0,0,10),
  // (-10,0,0), (0,0,-10) -- all distinct, all on the circle.
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 10.f};
  st.userCircleNormals = {0.f, -1.f, 0.f};

  const ray3d::Ray rup = RayAt(0.0, 0.0, 10.0);
  const CadSnap::Hit top = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/false, kTol,
                                             /*exclude=*/{}, &rup);
  REQUIRE(top.valid);
  CHECK(top.kind == Kind::Quadrant);
  CHECK(top.x == Approx(0.f).margin(1e-3));
  CHECK(top.y == Approx(0.f).margin(1e-3));
  CHECK(top.z == Approx(10.f).margin(1e-3));

  const ray3d::Ray side = RayAt(10.0, 0.0, 0.0);
  const CadSnap::Hit east = CadSnap::FindBest(10.0, 0.0, st, /*commandActive=*/false, kTol,
                                              /*exclude=*/{}, &side);
  REQUIRE(east.valid);
  CHECK(east.x == Approx(10.f).margin(1e-3));
  CHECK(east.z == Approx(0.f).margin(1e-3));
}

TEST_CASE("Quadrant snap on an arc offers only the quadrant points inside the sweep", "[CadSnap][issue401]") {
  AppCommandState st = QuadOnlyState();
  CadArc a;  // flat arc, centre origin, radius 10, sweeping 0 to 90 degrees
  a.cx = 0.f;
  a.cy = 0.f;
  a.r = 10.f;
  a.startRad = 0.f;
  a.sweepRad = kPi * 0.5f;
  st.userArcs.push_back(a);

  // 0 degrees (10, 0) and 90 degrees (0, 10) are inside the sweep.
  const CadSnap::Hit q0 = CadSnap::FindBest(9.5, 0.0, st, /*commandActive=*/true, kTol);
  REQUIRE(q0.valid);
  CHECK(q0.kind == Kind::Quadrant);
  CHECK(q0.x == Approx(10.f).margin(1e-3));

  const CadSnap::Hit q90 = CadSnap::FindBest(0.0, 9.5, st, /*commandActive=*/true, kTol);
  REQUIRE(q90.valid);
  CHECK(q90.y == Approx(10.f).margin(1e-3));

  // 180 degrees (-10, 0) and 270 degrees (0, -10) are outside the sweep -- nothing there.
  const CadSnap::Hit q180 = CadSnap::FindBest(-9.5, 0.0, st, /*commandActive=*/true, kTol);
  CHECK_FALSE(q180.valid);
  const CadSnap::Hit q270 = CadSnap::FindBest(0.0, -9.5, st, /*commandActive=*/true, kTol);
  CHECK_FALSE(q270.valid);
}

TEST_CASE("Quadrant snap obeys its per-type toggle and the snap-once override", "[CadSnap][issue401]") {
  AppCommandState st = QuadOnlyState();
  st.userCirclesCxCyZR = {0.f, 0.f, 0.f, 10.f};

  // Toggle off: nothing offered even right on the quadrant point.
  st.objectSnapQuadrant = false;
  const CadSnap::Hit off = CadSnap::FindBest(10.0, 0.0, st, /*commandActive=*/true, kTol);
  CHECK_FALSE(off.valid);

  // Shift+right-click "snap once" override reaches it regardless of the toggle.
  const Kind only = Kind::Quadrant;
  const CadSnap::Hit forced = CadSnap::FindBest(9.5, 0.0, st, /*commandActive=*/true, kTol,
                                                /*exclude=*/{}, /*pickRay=*/nullptr, &only);
  REQUIRE(forced.valid);
  CHECK(forced.kind == Kind::Quadrant);
  CHECK(forced.x == Approx(10.f).margin(1e-3));
}

// --- REQ-335: snapping the SECOND point of a SECTION plane (user report, 2026-09-11) ------------
//
// "The 2nd selection point for sections is not wanting to snap to a midpoint right above the first
// point." Reproduced here rather than reasoned about, because five plausible explanations were
// each ruled out by reading (solid midpoints exist and are on by default; ConsiderSnap re-ranks in
// 3D whenever a ray is present, so points stacked in Z are separable; `commandActive` gates only
// perpendicular; the snap is suppressed only during an object-SELECTION step, which WaitP2 is not;
// and SECTION commits `CadCommitElevation`, which is the snapped Z). All five were true and none
// was the answer, which is the point of writing the case instead.
TEST_CASE("SECTION's second point snaps to the midpoint of the edge above the first",
          "[CadSnap][req335]") {
  AppCommandState st;
  st.objectSnapEnabled = true;
  st.objectSnap3dEnabled = true;
  st.objectSnap3dVertex = true;
  st.objectSnap3dMidpointEdge = true;

  // The box the report used, and the state SECTION is in at its second point.
  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 14.0, 12.0, &box, &why));
  InstallSolid(st, std::move(box));
  st.active = AppCommandState::Kind::Section;
  st.sectionPhase = AppCommandState::SectionPhase::WaitP2;
  // The solid is SELECTED — SECTION selected it a moment ago, and a selected object is the one
  // difference between this and the passing `issue395` midpoint case.
  SelectedEntity sel{};
  sel.type = SelectedEntity::Type::Solid;
  sel.index = 0;
  st.selection.push_back(sel);

  // A vertical edge of a 20 x 14 x 12 box centred on the origin runs x=-10, y=-7, z 0..12, so its
  // midpoint is (-10, -7, 6) — directly above the bottom corner, which is where the first point is.
  st.sectionP1 = ray3d::Vec3{-10.0, -7.0, 0.0};
  const ray3d::Ray ray = RayAt(-10.0, -7.0, 6.0);
  const CadSnap::Hit hit =
      CadSnap::FindBest(-10.0, -7.0, st, /*commandActive=*/true, /*tolWorld=*/2.f, {}, &ray);

  REQUIRE(hit.valid);
  INFO("kind=" << static_cast<int>(hit.kind) << " at (" << hit.x << ", " << hit.y << ", " << hit.z << ")");
  CHECK(hit.kind == Kind::Midpoint);
  CHECK(hit.z == Approx(6.f).margin(1e-6));
}

TEST_CASE("SECTION's second point snaps to a vertical edge's midpoint from an ORBITED camera",
          "[CadSnap][req335]") {
  // The faithful version of the case above. The previous one handed `FindBest` the midpoint's own
  // XY, which is not what the viewport does: it passes the XY where the cursor ray crosses the WORK
  // PLANE, and the ray is what disambiguates in 3D. For a point six feet up, those two XYs are
  // several feet apart in an orbited view — so a plan-distance acceptance test would reject the very
  // point the user is pointing at, and only the ray-distance override saves it.
  AppCommandState st;
  st.objectSnapEnabled = true;
  st.objectSnap3dEnabled = true;
  st.objectSnap3dVertex = true;
  st.objectSnap3dMidpointEdge = true;

  brep::Solid box;
  brep::Problem why = brep::Problem::Ok;
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 20.0, 14.0, 12.0, &box, &why));
  InstallSolid(st, std::move(box));
  st.active = AppCommandState::Kind::Section;
  st.sectionPhase = AppCommandState::SectionPhase::WaitP2;
  SelectedEntity sel{};
  sel.type = SelectedEntity::Type::Solid;
  sel.index = 0;
  st.selection.push_back(sel);
  st.sectionP1 = ray3d::Vec3{-10.0, -7.0, 0.0};

  constexpr float kW = 1280.f;
  constexpr float kH = 720.f;
  Camera cam = Camera::Plan(0.0, 0.0, 30.f);
  cam.azimuthDeg = 135.f;
  cam.elevationDeg = 22.f;

  // Point at the midpoint of the vertical edge above the first point: (-10, -7, 6).
  float px = 0.f, py = 0.f;
  cam.WorldToScreen(-10.0, -7.0, 6.0, kW, kH, &px, &py);
  const ray3d::Ray ray = cam.ScreenRay(px, py, kW, kH);

  // What the viewport hands FindBest: the ray's crossing of the work plane z = 0, NOT the target's
  // own XY. This is the line that makes the test faithful.
  REQUIRE(std::fabs(ray.dir.z) > 1e-9);
  const double t = (0.0 - ray.origin.z) / ray.dir.z;
  const double wpX = ray.origin.x + t * ray.dir.x;
  const double wpY = ray.origin.y + t * ray.dir.y;
  INFO("work-plane crossing (" << wpX << ", " << wpY << ") vs target XY (-10, -7)");

  const CadSnap::Hit hit =
      CadSnap::FindBest(wpX, wpY, st, /*commandActive=*/true, /*tolWorld=*/2.f, {}, &ray);
  REQUIRE(hit.valid);
  INFO("kind=" << static_cast<int>(hit.kind) << " at (" << hit.x << ", " << hit.y << ", " << hit.z << ")");
  CHECK(hit.kind == Kind::Midpoint);
  CHECK(hit.x == Approx(-10.f).margin(1e-4));
  CHECK(hit.y == Approx(-7.f).margin(1e-4));
  CHECK(hit.z == Approx(6.f).margin(1e-4));
}
