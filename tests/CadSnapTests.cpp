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

TEST_CASE("FindBest snaps a feature-line endpoint", "[CadSnap][req158]") {
  AppCommandState st;
  st.objectSnapEndpoint = true;
  st.featureLineOffsets = {0, 2};
  st.featureLineVerts = {0.f, 0.f, 10.f, 100.f, 0.f, 12.f};
  st.featureLineClosed = {0};
  st.featureLineElevPt = {0, 0};
  st.featureLineBulge = {0.f, 0.f};
  st.featureLineRelOffset = {0.f, 0.f};
  st.featureLineInfo.push_back({});
  st.featureLineAttrs.push_back({});

  const CadSnap::Hit hit = CadSnap::FindBest(0.0, 0.0, st, /*commandActive=*/true, kTol);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Endpoint);
  CHECK(hit.x == Catch::Approx(0.f));
  CHECK(hit.y == Catch::Approx(0.f));
  CHECK(hit.z == Catch::Approx(10.f));
}

TEST_CASE("FindBest nearest snap on a feature-line segment", "[CadSnap][req158]") {
  AppCommandState st;
  st.objectSnapEndpoint = false;
  st.objectSnapMidpoint = false;
  st.objectSnapNearest = true;
  st.featureLineOffsets = {0, 2};
  st.featureLineVerts = {0.f, 0.f, 10.f, 100.f, 0.f, 12.f};
  st.featureLineClosed = {0};
  st.featureLineElevPt = {0, 0};
  st.featureLineBulge = {0.f, 0.f};
  st.featureLineRelOffset = {0.f, 0.f};
  st.featureLineInfo.push_back({});
  st.featureLineAttrs.push_back({});

  const CadSnap::Hit hit = CadSnap::FindBest(40.0, 1.0, st, /*commandActive=*/true, kTol);
  REQUIRE(hit.valid);
  CHECK(hit.kind == Kind::Nearest);
  CHECK(hit.x == Catch::Approx(40.f).margin(0.2f));
  CHECK(hit.y == Catch::Approx(0.f).margin(0.2f));
  CHECK(hit.z == Catch::Approx(10.8f).margin(0.2f));
}
