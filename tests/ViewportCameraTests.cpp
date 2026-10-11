// REQ-061 — per-viewport camera in paper space. Pure projection: `ModelToPaperInThroughCamera`
// must reproduce the historical `ModelToPaperIn` to the bit in plan view (the parity a legacy
// .gs depends on) and give the axonometric projection for a rotated camera.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "render/ViewportProjection.hpp"

using Catch::Approx;

namespace {
Viewport MakeVp() {
  Viewport vp;
  vp.paperXIn = 1.5f;
  vp.paperYIn = 2.0f;
  vp.paperWIn = 10.f;
  vp.paperHIn = 7.5f;
  vp.modelCenterX = 120.0;
  vp.modelCenterY = -40.0;
  vp.scaleModelPerPaperIn = 25.f;
  return vp;
}
}  // namespace

// A fresh viewport is a plan view — the default that keeps every legacy drawing unchanged.
TEST_CASE("Viewport camera defaults to plan view (REQ-061)", "[paperspace][req061]") {
  Viewport vp;
  REQUIRE(vp.cameraIsPlan());
  REQUIRE(vp.camElevationDeg == 90.f);
  REQUIRE(vp.camAzimuthDeg == 0.f);

  vp.camAzimuthDeg = 45.f;
  vp.camElevationDeg = 35.26439f;
  REQUIRE_FALSE(vp.cameraIsPlan());
}

// The load-bearing property: in plan view the camera projection IS ModelToPaperIn, bit for bit,
// over a grid of model points. This is what "a legacy .gs renders identically" reduces to.
TEST_CASE("Plan-view camera projection equals ModelToPaperIn exactly (REQ-061)", "[paperspace][req061]") {
  Viewport vp = MakeVp();
  REQUIRE(vp.cameraIsPlan());
  for (double mx = -500.0; mx <= 500.0; mx += 37.0) {
    for (double my = -500.0; my <= 500.0; my += 41.0) {
      float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
      ModelToPaperIn(vp, mx, my, &ax, &ay);
      ModelToPaperInThroughCamera(vp, mx, my, 123.0 /*Z ignored in plan*/, &bx, &by);
      REQUIRE(bx == ax);
      REQUIRE(by == ay);
    }
  }
}

// The model centre lands at the viewport rect centre for ANY camera orientation.
TEST_CASE("Model centre maps to the viewport rect centre under a rotated camera (REQ-061)",
          "[paperspace][req061]") {
  Viewport vp = MakeVp();
  vp.camAzimuthDeg = 45.f;
  vp.camElevationDeg = 35.26439f;
  float px = 0.f, py = 0.f;
  ModelToPaperInThroughCamera(vp, vp.modelCenterX, vp.modelCenterY, 0.0, &px, &py);
  REQUIRE(px == Approx(vp.paperXIn + vp.paperWIn * 0.5f).margin(1e-4));
  REQUIRE(py == Approx(vp.paperYIn + vp.paperHIn * 0.5f).margin(1e-4));
}

// A hand-computed SW-isometric projection (azimuth 45, elevation atan(1/sqrt2)), using the same
// Camera math the model viewport is signed off on. Square viewport at the origin, scale 1.
TEST_CASE("SW-isometric camera projects a known model point to the hand-computed sheet spot (REQ-061)",
          "[paperspace][req061]") {
  Viewport vp;
  vp.paperXIn = 0.f;
  vp.paperYIn = 0.f;
  vp.paperWIn = 10.f;
  vp.paperHIn = 10.f;
  vp.modelCenterX = 0.0;
  vp.modelCenterY = 0.0;
  vp.scaleModelPerPaperIn = 1.f;
  vp.camAzimuthDeg = 45.f;
  vp.camElevationDeg = 35.26439f;

  float px = 0.f, py = 0.f;
  ModelToPaperInThroughCamera(vp, 3.0, 0.0, 0.0, &px, &py);
  REQUIRE(px == Approx(7.1213203).margin(1e-3));
  REQUIRE(py == Approx(6.2247449).margin(1e-3));

  // Elevation matters now: a point lifted in +Z moves on the sheet (it would not in plan view).
  float pz0x = 0.f, pz0y = 0.f, pz1x = 0.f, pz1y = 0.f;
  ModelToPaperInThroughCamera(vp, 3.0, 0.0, 0.0, &pz0x, &pz0y);
  ModelToPaperInThroughCamera(vp, 3.0, 0.0, 10.0, &pz1x, &pz1y);
  REQUIRE(std::fabs(pz1y - pz0y) > 1e-2f);
}

// AC: changing one viewport's camera does not touch another viewport's projection.
TEST_CASE("A viewport's camera is independent of its siblings (REQ-061 / issue #155)",
          "[paperspace][req061]") {
  PaperLayout L;
  L.viewports.push_back(MakeVp());
  L.viewports.push_back(MakeVp());
  L.viewports[0].camAzimuthDeg = 45.f;
  L.viewports[0].camElevationDeg = 35.26439f;

  float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
  ModelToPaperIn(L.viewports[1], 200.0, 100.0, &ax, &ay);
  ModelToPaperInThroughCamera(L.viewports[1], 200.0, 100.0, 50.0, &bx, &by);
  REQUIRE(L.viewports[1].cameraIsPlan());
  REQUIRE(bx == ax);
  REQUIRE(by == ay);
}

TEST_CASE("Standard-view table is well-formed (REQ-061)", "[paperspace][req061]") {
  REQUIRE(kViewportStandardViewCount >= 6);
  bool sawPlan = false;
  for (int i = 0; i < kViewportStandardViewCount; ++i) {
    REQUIRE(kViewportStandardViews[i].name != nullptr);
    REQUIRE(kViewportStandardViews[i].elevationDeg >= -90.f);
    REQUIRE(kViewportStandardViews[i].elevationDeg <= 90.f);
    if (kViewportStandardViews[i].azimuthDeg == 0.f && kViewportStandardViews[i].elevationDeg == 90.f)
      sawPlan = true;
  }
  REQUIRE(sawPlan);

  Viewport vp;
  vp.camAzimuthDeg = kViewportStandardViews[0].azimuthDeg;
  vp.camElevationDeg = kViewportStandardViews[0].elevationDeg;
  REQUIRE(vp.cameraIsPlan());  // entry 0 is Plan
}
