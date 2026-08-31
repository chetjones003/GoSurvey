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
