// GitHub issue #399 increment 4 — smart TRIM (TRIMSTATE 0: two clicks draw a line across the
// drawing) was flat world-XY throughout (ExecuteDrawnSegmentTrimOnce and its helpers drop the Z of
// both drawn points and of every candidate edge). Under an orbited camera or a non-world UCS the
// drawn stroke landed on the ground plane, far from the cursor and geometry, and the trim picked the
// wrong target / a bogus XY-projected crossing. When the drawn points carry a real elevation (a
// valid pick ray), SubmitTrimViewportPick now resolves the operation in true 3D via
// Try3DDrawnLineTrim, the same way the classic "click the piece to remove" path (increments 1-3,
// Trim3DLineLineTests.cpp) already does. Plan view (no pick ray) still runs the byte-identical 2D
// path — the last test here is the regression guard for that.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"
#include "util/ray3d.hpp"

#include <string>
#include <vector>

using Catch::Approx;

namespace {

// Arms TRIM on the drawn-cut-line first-point step, as if the user had started TRIM on a fresh
// profile (TRIMSTATE 0) and is about to click the line's two points.
void ArmDrawnLineTrim(AppCommandState& st) {
  st.active = AppCommandState::Kind::Trim;
  st.trimPhase = AppCommandState::TrimPhase::CuttingLine_WaitP1;
}

// A valid pick ray is all Try3DDrawnLineTrim needs from the camera — it gates the 3D path in
// SubmitTrimViewportPick. Looking down -Y, the orbited equivalent of an elevation-view click.
const ray3d::Ray kOrbitRay{ray3d::Vec3{30.0, 100.0, 0.0}, ray3d::Vec3{0.0, -1.0, 0.0}};

// Feeds one drawn-line point: sets the committed cursor elevation, then submits (wx, wy) = world XY.
bool ClickPoint(AppCommandState& st, float wx, float wy, float wz, std::vector<std::string>& log) {
  st.uiCursorWorldZ = wz;
  return SubmitTrimViewportPick(st, wx, wy, 1.f, log, &kOrbitRay);
}

} // namespace

TEST_CASE("3D drawn-line TRIM cuts a line at a coplanar crossing in the world X-Z plane",
          "[trim][issue399]") {
  AppCommandState st;
  // Both lines live in the world X-Z plane (y=0) — a "Front UCS" drawing. Target (0,0,0)-(100,0,0);
  // cutter (50,0,-20)-(50,0,20), crossing at (50,0,0). A flat XY projection would drop z and find a
  // different crossing entirely.
  st.userLinesFlat = {
      0.f,  0.f, 0.f,   100.f, 0.f, 0.f,
      50.f, 0.f, -20.f, 50.f,  0.f, 20.f,
  };
  ArmDrawnLineTrim(st);

  std::vector<std::string> log;
  // Draw a short vertical stroke across the target at x=30 (world X-Z plane).
  REQUIRE(ClickPoint(st, 30.f, 0.f, -5.f, log));
  REQUIRE(ClickPoint(st, 30.f, 0.f, 5.f, log));

  REQUIRE(st.userLinesFlat.size() == 12);          // both entities remain
  CHECK(st.userLinesFlat[0] == Approx(50.f));      // near end moved to the true 3D crossing
  CHECK(st.userLinesFlat[1] == Approx(0.f));
  CHECK(st.userLinesFlat[2] == Approx(0.f));
  CHECK(st.userLinesFlat[3] == Approx(100.f));     // far end untouched
  CHECK(st.active == AppCommandState::Kind::None); // command ends after one drawn trim
}

TEST_CASE("3D drawn-line TRIM refuses a crossing that only exists in the XY projection",
          "[trim][issue399]") {
  AppCommandState st;
  // Target on z=0; cutter lifted 5 units in Z so the two lines pass each other well outside the
  // FILLET-style tolerance (drawing extent ~100 -> tol ~0.01). In a flat XY projection they still
  // "cross" at x=50; in true 3D they do not.
  st.userLinesFlat = {
      0.f,  0.f,  0.f, 100.f, 0.f,  0.f,
      50.f, -5.f, 5.f, 50.f,  5.f,  5.f,
  };
  ArmDrawnLineTrim(st);

  std::vector<std::string> log;
  REQUIRE(ClickPoint(st, 30.f, 0.f, -5.f, log));
  REQUIRE(ClickPoint(st, 30.f, 0.f, 5.f, log));

  REQUIRE(st.userLinesFlat.size() == 12);
  CHECK(st.userLinesFlat[0] == Approx(0.f));   // untouched
  CHECK(st.userLinesFlat[3] == Approx(100.f));
  bool sawRefusal = false;
  for (const std::string& line : log)
    if (line.find("nothing crosses") != std::string::npos)
      sawRefusal = true;
  CHECK(sawRefusal);
}

TEST_CASE("3D drawn-line TRIM removes the side the drawn line's midpoint falls on", "[trim][issue399]") {
  AppCommandState st;
  // Target (0,0,0)-(100,0,0) with two cutters at x=25 and x=75, all in the world X-Z plane.
  st.userLinesFlat = {
      0.f,  0.f, 0.f,   100.f, 0.f, 0.f,
      25.f, 0.f, -10.f, 25.f,  0.f, 10.f,
      75.f, 0.f, -10.f, 75.f,  0.f, 10.f,
  };
  ArmDrawnLineTrim(st);

  std::vector<std::string> log;
  // Stroke drawn across the RIGHT part of the middle span (x=60) — nearest crossing is x=75, and the
  // midpoint (x=60) sits on the [0,75] side, so that side's near end (A) moves to x=75.
  REQUIRE(ClickPoint(st, 60.f, 0.f, -3.f, log));
  REQUIRE(ClickPoint(st, 60.f, 0.f, 3.f, log));

  REQUIRE(st.userLinesFlat.size() == 18);
  CHECK(st.userLinesFlat[0] == Approx(75.f));
  CHECK(st.userLinesFlat[3] == Approx(100.f));
}

TEST_CASE("3D drawn-line TRIM cuts a line against a coplanar circle cutting edge", "[trim][issue399]") {
  AppCommandState st;
  // Target crosses a circle of radius 10 centred at (50,0,0), both in world XY (z=0); crossings at
  // x=40 and x=60. Orbited pick ray -> the 3D path; the drawn stroke picks the x=40 crossing.
  st.userLinesFlat = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f};
  st.userCirclesCxCyZR = {50.f, 0.f, 0.f, 10.f};
  ArmDrawnLineTrim(st);

  std::vector<std::string> log;
  REQUIRE(ClickPoint(st, 20.f, -3.f, 0.f, log));
  REQUIRE(ClickPoint(st, 20.f, 3.f, 0.f, log));

  REQUIRE(st.userLinesFlat.size() == 6);
  CHECK(st.userLinesFlat[0] == Approx(40.f));
  CHECK(st.userLinesFlat[3] == Approx(100.f));
}

TEST_CASE("3D drawn-line TRIM cuts a polyline target segment, other vertices untouched",
          "[trim][issue399]") {
  AppCommandState st;
  // Polyline (0,0,0)-(100,0,0)-(100,0,100) in the world X-Z plane; a cutter line crosses the first
  // chord at x=50.
  st.userPolylineOffsets = {0, 3};
  st.userPolylineVerts = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f, 100.f, 0.f, 100.f};
  st.userPolylineClosed = {false};
  st.userLinesFlat = {50.f, 0.f, -20.f, 50.f, 0.f, 20.f};
  ArmDrawnLineTrim(st);

  std::vector<std::string> log;
  REQUIRE(ClickPoint(st, 30.f, 0.f, -5.f, log));
  REQUIRE(ClickPoint(st, 30.f, 0.f, 5.f, log));

  REQUIRE(st.userPolylineVerts.size() == 9);
  CHECK(st.userPolylineVerts[0] == Approx(50.f));   // near vertex moved to the crossing
  CHECK(st.userPolylineVerts[2] == Approx(0.f));
  CHECK(st.userPolylineVerts[3] == Approx(100.f));  // untouched
  CHECK(st.userPolylineVerts[8] == Approx(100.f));  // untouched
}

TEST_CASE("Plan-view drawn-line TRIM is unchanged when no pick ray is supplied (regression guard)",
          "[trim][issue399]") {
  AppCommandState st;
  st.userLinesFlat = {
      0.f,  0.f,   0.f, 100.f, 0.f,  0.f,
      50.f, -20.f, 0.f, 50.f,  20.f, 0.f,
  };
  st.active = AppCommandState::Kind::Trim;
  st.trimPhase = AppCommandState::TrimPhase::CuttingLine_WaitP1;

  std::vector<std::string> log;
  // No pick ray at all — the exact plan-view call shape. Flat 2D path (ExecuteDrawnSegmentTrimOnce).
  REQUIRE(SubmitTrimViewportPick(st, 30.f, -5.f, 1.f, log));
  REQUIRE(SubmitTrimViewportPick(st, 30.f, 5.f, 1.f, log));

  REQUIRE(st.userLinesFlat.size() == 12);
  CHECK(st.userLinesFlat[0] == Approx(50.f));
  CHECK(st.userLinesFlat[1] == Approx(0.f));
  CHECK(st.userLinesFlat[2] == Approx(0.f));
  CHECK(st.userLinesFlat[3] == Approx(100.f));
}
