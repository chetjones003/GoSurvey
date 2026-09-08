// GitHub issue #399 increment 1 — TRIM's target pick, cutting-edge selection and pick-side math
// were all flat world-XY, correct only in plan view under the world UCS. An orbited camera (a real
// pick RAY, not a flattened cursor position) now finds the true 3D crossing between two Line
// entities: exact when they are coplanar, closest-approach-within-tolerance when they are skew, and
// refused (no cut) when the gap exceeds that tolerance — the reused-from-FILLET (issue #373)
// tolerance formula the user approved for this SPEC GAP: max(1e-5, 1e-4 * largest drawing extent).
//
// Scope is deliberately Line-vs-Line only; any other entity type reached while a pick ray is active
// is refused by name rather than silently running the old, possibly-wrong flat math. Plan view
// (pickRay == nullptr) is untouched — SubmitTrimViewportPick falls through to the original code
// path bit-for-bit, which TrimLinePreviewTests.cpp already pins for the non-target-pick side of
// TRIM (the drawn cut-line preview); this file adds the plan-view REGRESSION guard for the
// target-pick side (AC #4) alongside the new 3D cases (AC #1-3).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CadCommands.hpp"
#include "util/ray3d.hpp"

#include <cmath>
#include <vector>

using Catch::Approx;

namespace {

AppCommandState::TrimPhase kTargetPhase = AppCommandState::TrimPhase::SelectTrimTargets;

// Arms TRIM as if StartTrimCommand had already run and the user had picked \p cutters, and is now
// on the final "click the piece to remove" step.
void ArmTrimWithCutters(AppCommandState& st, std::vector<SelectedEntity> cutters) {
  st.active = AppCommandState::Kind::Trim;
  st.trimPhase = kTargetPhase;
  st.trimCutters = std::move(cutters);
}

SelectedEntity LineEntity(int index) {
  SelectedEntity e{};
  e.type = SelectedEntity::Type::LineSeg;
  e.index = index;
  return e;
}

} // namespace

TEST_CASE("3D TRIM cuts two coplanar crossing lines from an orbited pick ray", "[trim][issue399]") {
  AppCommandState st;
  // Target: (0,0,0)->(100,0,0). Cutter: (50,-20,0)->(50,20,0). Both flat at z=0 — coplanar,
  // crossing at (50,0,0), same shape TrimLinePreviewTests.cpp's TwoCrossingLines() uses.
  st.userLinesFlat = {
      0.f,  0.f,   0.f, 100.f, 0.f,  0.f,
      50.f, -20.f, 0.f, 50.f,  20.f, 0.f,
  };
  ArmTrimWithCutters(st, {LineEntity(1)});

  // A ray straight down the +Z world axis, aimed at the left half of the target (x=20) — the
  // orbited-camera equivalent of clicking the segment between the two crossing edges.
  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 0.0, 100.0}, ray3d::Vec3{0.0, 0.0, -1.0}};
  std::vector<std::string> log;
  REQUIRE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  REQUIRE(st.userLinesFlat.size() == 12);  // both entities remain; only the target end moved
  CHECK(st.userLinesFlat[0] == Approx(50.f));  // trimmed end now sits at the crossing
  CHECK(st.userLinesFlat[1] == Approx(0.f));
  CHECK(st.userLinesFlat[2] == Approx(0.f));
  CHECK(st.userLinesFlat[3] == Approx(100.f));  // far end untouched
}

TEST_CASE("3D TRIM cuts a skew line pair within the FILLET-style tolerance", "[trim][issue399]") {
  AppCommandState st;
  // Target flat at z=0. Cutter offset a hair in Z at its crossing point (drawing extent ~100, so
  // tolerance = max(1e-5, 1e-4*100) = 0.01; this gap is 0.001 — well inside it).
  st.userLinesFlat = {
      0.f,  0.f,   0.f,     100.f, 0.f,   0.f,
      50.f, -20.f, 0.001f,  50.f,  20.f,  0.001f,
  };
  ArmTrimWithCutters(st, {LineEntity(1)});

  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 0.0, 100.0}, ray3d::Vec3{0.0, 0.0, -1.0}};
  std::vector<std::string> log;
  REQUIRE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  REQUIRE(st.userLinesFlat.size() == 12);
  CHECK(st.userLinesFlat[0] == Approx(50.f).margin(1e-3));
}

TEST_CASE("3D TRIM refuses a skew line pair outside the tolerance (no cut)", "[trim][issue399]") {
  AppCommandState st;
  // Same layout, but the cutter is 5 units away in Z at closest approach — far past the ~0.01
  // tolerance for a drawing this size.
  st.userLinesFlat = {
      0.f,  0.f,   0.f,   100.f, 0.f,   0.f,
      50.f, -20.f, 5.f,   50.f,  20.f,  5.f,
  };
  ArmTrimWithCutters(st, {LineEntity(1)});

  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 0.0, 100.0}, ray3d::Vec3{0.0, 0.0, -1.0}};
  std::vector<std::string> log;
  CHECK_FALSE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  // Nothing moved.
  REQUIRE(st.userLinesFlat.size() == 12);
  CHECK(st.userLinesFlat[0] == Approx(0.f));
  CHECK(st.userLinesFlat[3] == Approx(100.f));
  bool sawRefusal = false;
  for (const std::string& line : log)
    if (line.find("does not cross") != std::string::npos)
      sawRefusal = true;
  CHECK(sawRefusal);
}

TEST_CASE("3D TRIM finds a rotated-UCS crossing via the pick ray, independent of world XY", "[trim][issue399]") {
  AppCommandState st;
  // Both lines live in the world X-Z plane (y=0 throughout) — a crossing that a flat world-XY
  // projection would place at a completely different (x,y), since it drops z. Target (0,0,0)-(100,0,0);
  // cutter (50,0,-20)-(50,0,20), crossing at (50,0,0).
  st.userLinesFlat = {
      0.f,  0.f, 0.f,   100.f, 0.f, 0.f,
      50.f, 0.f, -20.f, 50.f,  0.f, 20.f,
  };
  ArmTrimWithCutters(st, {LineEntity(1)});

  // A ray looking along -Y (as if the camera orbited to look down the rotated UCS's normal),
  // aimed at the left half of the target.
  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 100.0, 0.0}, ray3d::Vec3{0.0, -1.0, 0.0}};
  std::vector<std::string> log;
  REQUIRE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  REQUIRE(st.userLinesFlat.size() == 12);
  CHECK(st.userLinesFlat[0] == Approx(50.f));
  CHECK(st.userLinesFlat[2] == Approx(0.f));
}

TEST_CASE("3D TRIM cuts a line target against a polyline cutting edge from an orbited view",
         "[trim][issue399]") {
  // issue #399 increment 3: a Polyline cutting edge is now walked as a chain of straight chords
  // (matching the 2D path's own treatment) instead of being refused by name.
  AppCommandState st;
  st.userLinesFlat = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f};
  st.userPolylineOffsets = {0, 2};
  st.userPolylineVerts = {50.f, -10.f, 0.f, 50.f, 10.f, 0.f};
  st.userPolylineClosed = {false};
  SelectedEntity polyCutter{};
  polyCutter.type = SelectedEntity::Type::Polyline;
  polyCutter.index = 0;
  ArmTrimWithCutters(st, {polyCutter});

  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 0.0, 100.0}, ray3d::Vec3{0.0, 0.0, -1.0}};
  std::vector<std::string> log;
  REQUIRE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  REQUIRE(st.userLinesFlat.size() == 6);
  CHECK(st.userLinesFlat[0] == Approx(50.f));
  CHECK(st.userLinesFlat[3] == Approx(100.f));
}

TEST_CASE("3D TRIM cuts a polyline target segment from an orbited view, other vertices untouched",
         "[trim][issue399]") {
  // issue #399 increment 3: the polyline itself can now be the trim TARGET. Three vertices
  // (0,0,0)-(100,0,0)-(100,100,0); a cutter line crosses the first straight chord at x=50.
  AppCommandState st;
  st.userPolylineOffsets = {0, 3};
  st.userPolylineVerts = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f, 100.f, 100.f, 0.f};
  st.userPolylineClosed = {false};
  st.userLinesFlat = {50.f, -20.f, 0.f, 50.f, 20.f, 0.f};
  SelectedEntity polyTarget{};
  polyTarget.type = SelectedEntity::Type::Polyline;
  polyTarget.index = 0;
  ArmTrimWithCutters(st, {LineEntity(0)});

  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 0.0, 100.0}, ray3d::Vec3{0.0, 0.0, -1.0}};
  std::vector<std::string> log;
  REQUIRE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  REQUIRE(st.userPolylineVerts.size() == 9);
  CHECK(st.userPolylineVerts[0] == Approx(50.f));  // trimmed vertex moved to the crossing
  CHECK(st.userPolylineVerts[1] == Approx(0.f));
  CHECK(st.userPolylineVerts[2] == Approx(0.f));
  // Untouched vertices.
  CHECK(st.userPolylineVerts[3] == Approx(100.f));
  CHECK(st.userPolylineVerts[4] == Approx(0.f));
  CHECK(st.userPolylineVerts[6] == Approx(100.f));
  CHECK(st.userPolylineVerts[7] == Approx(100.f));
}

TEST_CASE("3D TRIM finds a rotated-UCS crossing between two polylines via the pick ray",
         "[trim][issue399]") {
  AppCommandState st;
  // Both polylines live in the world X-Z plane (y=0); target (0,0,0)-(100,0,0), cutter chord
  // (50,0,-20)-(50,0,20), crossing at (50,0,0) — a crossing a flat XY projection would miss.
  st.userPolylineOffsets = {0, 2, 4};
  st.userPolylineVerts = {
      0.f,  0.f, 0.f,   100.f, 0.f, 0.f,
      50.f, 0.f, -20.f, 50.f,  0.f, 20.f,
  };
  st.userPolylineClosed = {false, false};
  SelectedEntity polyCutter{};
  polyCutter.type = SelectedEntity::Type::Polyline;
  polyCutter.index = 1;
  ArmTrimWithCutters(st, {polyCutter});

  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 100.0, 0.0}, ray3d::Vec3{0.0, -1.0, 0.0}};
  std::vector<std::string> log;

  // Target is polyline 0's own single segment.
  SelectedEntity dummy{};
  (void)dummy;
  REQUIRE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  CHECK(st.userPolylineVerts[0] == Approx(50.f));
  CHECK(st.userPolylineVerts[2] == Approx(0.f));
}

TEST_CASE("3D TRIM skips a skew polyline cutting edge outside tolerance rather than guessing",
         "[trim][issue399]") {
  AppCommandState st;
  st.userLinesFlat = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f};
  st.userPolylineOffsets = {0, 2};
  // Cutter chord offset 5 units in Z at its closest approach — far past tolerance for this drawing size.
  st.userPolylineVerts = {50.f, -10.f, 5.f, 50.f, 10.f, 5.f};
  st.userPolylineClosed = {false};
  SelectedEntity polyCutter{};
  polyCutter.type = SelectedEntity::Type::Polyline;
  polyCutter.index = 0;
  ArmTrimWithCutters(st, {polyCutter});

  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 0.0, 100.0}, ray3d::Vec3{0.0, 0.0, -1.0}};
  std::vector<std::string> log;
  CHECK_FALSE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  REQUIRE(st.userLinesFlat.size() == 6);
  CHECK(st.userLinesFlat[0] == Approx(0.f));  // untouched
}

TEST_CASE("3D TRIM cuts a line against a coplanar circle cutting edge from an orbited view",
         "[trim][issue399]") {
  AppCommandState st;
  // Target crosses a circle of radius 10 centred at (50,0,0); both lie in world XY (z=0), so the
  // crossings are at x = 40 and x = 60.
  st.userLinesFlat = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f};
  st.userCirclesCxCyZR = {50.f, 0.f, 0.f, 10.f};
  SelectedEntity circleCutter{};
  circleCutter.type = SelectedEntity::Type::Circle;
  circleCutter.index = 0;
  ArmTrimWithCutters(st, {circleCutter});

  // Orbited pick ray looking straight down, aimed at the left half of the target (x=20).
  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 0.0, 100.0}, ray3d::Vec3{0.0, 0.0, -1.0}};
  std::vector<std::string> log;
  REQUIRE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  // The pick at x=20 lands on the removed side [0,40]; the surviving segment is [40,100].
  REQUIRE(st.userLinesFlat.size() == 6);
  CHECK(st.userLinesFlat[0] == Approx(40.f));
  CHECK(st.userLinesFlat[3] == Approx(100.f));
}

TEST_CASE("3D TRIM cuts a line against a coplanar arc cutting edge in a tilted (X-Z) plane",
         "[trim][issue399]") {
  AppCommandState st;
  // Target and a tilted arc both live in the world X-Z plane (y=0). Arc: centre (50,0,0), radius
  // 10, normal +Y (its own plane is X-Z), half turn so it spans both crossings at x=40 and x=60.
  st.userLinesFlat = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f};
  CadArc arc{};
  arc.cx = 50.f;
  arc.cy = 0.f;
  arc.z = 0.f;
  arc.r = 10.f;
  arc.nx = 0.f;
  arc.ny = 1.f;
  arc.nz = 0.f;
  arc.startRad = 0.f;
  arc.sweepRad = 6.28318530717958647692f;
  st.userArcs.push_back(arc);
  SelectedEntity arcCutter{};
  arcCutter.type = SelectedEntity::Type::Arc;
  arcCutter.index = 0;
  ArmTrimWithCutters(st, {arcCutter});

  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 100.0, 0.0}, ray3d::Vec3{0.0, -1.0, 0.0}};
  std::vector<std::string> log;
  REQUIRE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  // The pick at x=20 lands on the removed side [0,40]; the surviving segment is [40,100].
  REQUIRE(st.userLinesFlat.size() == 6);
  CHECK(st.userLinesFlat[0] == Approx(40.f).margin(0.05));
  CHECK(st.userLinesFlat[3] == Approx(100.f));
}

TEST_CASE("3D TRIM skips a skew circle cutting edge rather than guessing a crossing",
         "[trim][issue399]") {
  AppCommandState st;
  // Target line is on world Z=0; the circle is centred on the target's line but tilted (normal +X,
  // its own plane is Y-Z) and offset in Z so it does not actually touch the target.
  st.userLinesFlat = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f};
  st.userCirclesCxCyZR = {50.f, 0.f, 5.f, 10.f};
  st.userCircleNormals = {1.f, 0.f, 0.f};
  SelectedEntity circleCutter{};
  circleCutter.type = SelectedEntity::Type::Circle;
  circleCutter.index = 0;
  ArmTrimWithCutters(st, {circleCutter});

  const ray3d::Ray pickRay{ray3d::Vec3{20.0, 0.0, 100.0}, ray3d::Vec3{0.0, 0.0, -1.0}};
  std::vector<std::string> log;
  CHECK_FALSE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log, &pickRay));

  REQUIRE(st.userLinesFlat.size() == 6);
  CHECK(st.userLinesFlat[0] == Approx(0.f));  // untouched: skew cutter contributed no crossing
  bool sawRefusal = false;
  for (const std::string& line : log)
    if (line.find("does not cross") != std::string::npos)
      sawRefusal = true;
  CHECK(sawRefusal);
}

TEST_CASE("Plan view TRIM polyline target/cutting-edge pick is unchanged (regression guard)",
         "[trim][issue399]") {
  // issue #399 increment 3 must not touch the plan-view (pickRay == nullptr) path at all — it falls
  // through to the original 2D TrimSegmentToCuttingEdges Poly branch, unaffected by this increment.
  AppCommandState st;
  st.userPolylineOffsets = {0, 2};
  st.userPolylineVerts = {0.f, 0.f, 0.f, 100.f, 0.f, 0.f};
  st.userPolylineClosed = {false};
  st.userLinesFlat = {50.f, -20.f, 0.f, 50.f, 20.f, 0.f};
  SelectedEntity polyTarget{};
  polyTarget.type = SelectedEntity::Type::Polyline;
  polyTarget.index = 0;
  ArmTrimWithCutters(st, {LineEntity(0)});

  std::vector<std::string> log;
  REQUIRE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log));

  REQUIRE(st.userPolylineVerts.size() == 6);
  CHECK(st.userPolylineVerts[0] == Approx(50.f));
  CHECK(st.userPolylineVerts[1] == Approx(0.f));
}

TEST_CASE("Plan view TRIM target pick is unchanged (regression guard, no pick ray)", "[trim][issue399]") {
  AppCommandState st;
  st.userLinesFlat = {
      0.f,  0.f,   0.f, 100.f, 0.f,  0.f,
      50.f, -20.f, 0.f, 50.f,  20.f, 0.f,
  };
  ArmTrimWithCutters(st, {LineEntity(1)});

  std::vector<std::string> log;
  // No pickRay argument at all — exactly how every existing plan-view caller invokes this.
  REQUIRE(SubmitTrimViewportPick(st, 20.f, 0.f, 1.f, log));

  REQUIRE(st.userLinesFlat.size() == 12);
  CHECK(st.userLinesFlat[0] == Approx(50.f));
  CHECK(st.userLinesFlat[1] == Approx(0.f));
}
