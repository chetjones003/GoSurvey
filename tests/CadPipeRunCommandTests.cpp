#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "util/cadpiperun.hpp"
#include "CadRubberPreview.hpp"
#include "util/brep.hpp"
#include <chrono>
#include "util/ucs.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE("PIPERUN starts at the nominal-size prompt", "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  CHECK(st.active == AppCommandState::Kind::PipeRun);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
}

TEST_CASE("PIPERUN refuses an unknown nominal size and stays at the prompt", "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("9in", st, log));
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
  CHECK(st.pipeRunNominalSize.empty());
}

TEST_CASE("PIPERUN refuses an unknown pressure class and stays at the prompt", "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in CS999", st, log));
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
  CHECK(st.pipeRunPressureClassTag.empty());
}

TEST_CASE("PIPERUN accepts a known size and class, then asks for the wall",
          "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in CS150", st, log));
  CHECK(st.pipeRunNominalSize == "4in");
  CHECK(st.pipeRunPressureClassTag == "CS150");
  // The wall is asked AFTER the size (D-2026-09-23-a), because the default offered — and the
  // maximum accepted — both depend on which size was just chosen.
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitWallThickness);
  REQUIRE(HandlePipeRunTextInput("", st, log));
  CHECK(st.pipeRunWallThicknessIn == Catch::Approx(0.237));  // schedule 40 for 4in
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitFirstPoint);
}

TEST_CASE("PIPERUN takes a typed wall, and refuses one that leaves no bore",
          "[issue486][piperun][command][wall]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));  // OD 4.5in

  REQUIRE(HandlePipeRunTextInput("bogus", st, log));
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitWallThickness);  // prompt stands
  REQUIRE(HandlePipeRunTextInput("-0.2", st, log));
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitWallThickness);
  REQUIRE(HandlePipeRunTextInput("2.25", st, log));  // exactly half the OD: no bore left
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitWallThickness);
  CHECK(st.pipeRunWallThicknessIn == 0.0);

  // A click cannot skip the prompt either.
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitWallThickness);
  CHECK(st.pipeRunDraftVerts.empty());

  REQUIRE(HandlePipeRunTextInput("0.5", st, log));
  CHECK(st.pipeRunWallThicknessIn == Catch::Approx(0.5));
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitFirstPoint);
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRuns[0].wallThicknessIn == Catch::Approx(0.5));  // the run carries what was typed
}

TEST_CASE("The offered wall follows the size, rather than the last run's",
          "[issue486][piperun][command][wall]") {
  // A one-off heavy wall on a 4in run must not silently become the default on the next run, which
  // may well be a different size — the prompt offers that size's own standard wall each time.
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("0.75", st, log));
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("8in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));
  CHECK(st.pipeRunWallThicknessIn == Catch::Approx(0.322));  // schedule 40 for 8in, not 0.75
}

TEST_CASE("A click-to-add PIPERUN commits a CadPipeRun on END", "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNextPoint);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  CHECK(st.active == AppCommandState::Kind::None);
  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRuns[0].vertsXyz.size() == 9);  // 3 vertices
  CHECK(st.cadPipeRuns[0].nominalSize == "4in");
  REQUIRE(st.cadPipeRunAttrs.size() == 1);
}

TEST_CASE("Blank Enter finishes an open PIPERUN the same way END does", "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("2in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 5.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("", st, log));
  CHECK(st.active == AppCommandState::Kind::None);
  REQUIRE(st.cadPipeRuns.size() == 1);
}

// ASCII name deliberately: a non-ASCII character in a TEST_CASE name breaks ctest's own discovery
// on Windows, so the case passes when the exe is run directly and fails under `ctest` with "no test
// cases matched". Pre-existing here; fixed while this file was open.
TEST_CASE("PIPERUN END with only a start point refuses - a run needs two vertices",
          "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  CHECK(st.active == AppCommandState::Kind::PipeRun);  // still open — nothing committed
  CHECK(st.cadPipeRuns.empty());
}

TEST_CASE("PIPERUN U undoes the last vertex but not the start point", "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(st.pipeRunDraftVerts.size() == 9);
  REQUIRE(HandlePipeRunTextInput("u", st, log));
  CHECK(st.pipeRunDraftVerts.size() == 6);
  REQUIRE(HandlePipeRunTextInput("u", st, log));
  CHECK(st.pipeRunDraftVerts.size() == 3);  // back to the start point
  // A further U refuses rather than removing the start point.
  REQUIRE(HandlePipeRunTextInput("u", st, log));
  CHECK(st.pipeRunDraftVerts.size() == 3);
}

TEST_CASE("Esc-equivalent cancel clears the draft but remembers the nominal size",
          "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in CS300", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  CancelPipeRunCommand(st);
  CHECK(st.pipeRunDraftVerts.empty());
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
  CHECK(st.pipeRunNominalSize == "4in");  // remembered, like POLYSOLID's width/height/justify
  CHECK(st.pipeRunPressureClassTag == "CS300");
}

TEST_CASE("A second PIPERUN reuses the remembered size with a blank Enter", "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("6in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 1.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  StartPipeRunCommand(st, log);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
  REQUIRE(HandlePipeRunTextInput("", st, log));  // keep the remembered size
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitWallThickness);
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitFirstPoint);
  CHECK(st.pipeRunNominalSize == "6in");
}

TEST_CASE("A click while the size is still unset is refused, not silently accepted",
          "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
  CHECK(st.pipeRunDraftVerts.empty());
}

TEST_CASE("BEDIT hides the main drawing's pipe runs, and restores them on close",
          "[issue486][piperun][bedit]") {
  AppCommandState st;
  std::vector<std::string> log;

  // A pipe run committed in the MAIN drawing before BEDIT ever opens.
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);

  CadBlocksEnterNamedEditor(st, "TestFlange", log);
  REQUIRE(st.blockEditActive);
  // The block being edited has no pipe runs of its own — the main drawing's run must not leak in.
  CHECK(st.cadPipeRuns.empty());
  CHECK(st.cadPipeRunAttrs.empty());

  // BCLOSE's own restore path (CadBlocks.cpp) calls exactly this.
  CadRestoreGeometrySnapshot(st, st.blockEditModelStash);
  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRuns[0].nominalSize == "4in");
}

TEST_CASE("A pipe run is pickable as a whole SelectedEntity of Type::PipeRun", "[issue486][piperun][select]") {
  AppCommandState st;
  st.viewportVisualStyle = VisualStyle::Shaded;  // faces pickable — Wireframe2D draws none (D-2026-09-16-b)
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  st.cadPipeRuns.push_back(run);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});
  RefreshSolidDisplayGeometry(st);
  REQUIRE(st.pipeRunWorldSolids.size() == 1);

  // A ray straight down through the pipe's middle (5,0,0), well within its cross-section.
  const ray3d::Ray ray{ray3d::Vec3{5.0, 0.0, 5.0}, ray3d::Vec3{0.0, 0.0, -1.0}};
  SelectedEntity hit{};
  REQUIRE(PickClosestSolidEntity(st, ray, 0.01f, &hit));
  CHECK(hit.type == SelectedEntity::Type::PipeRun);
  CHECK(hit.index == 0);
}

TEST_CASE("DELETE removes a selected pipe run", "[issue486][piperun][select]") {
  AppCommandState st;
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  st.cadPipeRuns.push_back(run);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});

  SelectedEntity sel{};
  sel.type = SelectedEntity::Type::PipeRun;
  sel.index = 0;
  st.selection.push_back(sel);

  std::vector<std::string> log;
  ExecuteDeleteSelection(st, log);
  CHECK(st.cadPipeRuns.empty());
  CHECK(st.cadPipeRunAttrs.empty());
}

// --- Piping ortho/polar compass (REQ-346, D-2026-09-17-a) --------------------------------------

TEST_CASE("PIPERUN compass is on by default and only engages from a start point",
          "[issue486][piperun][compass]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  CHECK(st.pipeRunCompassOn);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  // Off-angle first point: nothing to measure the angle from yet, so it is placed as picked.
  SubmitPipeRunViewportPick(st, 3.f, 7.f, log);
  REQUIRE(st.pipeRunDraftVerts.size() == 3);
  CHECK(st.pipeRunDraftVerts[0] == 3.0);
  CHECK(st.pipeRunDraftVerts[1] == 7.0);
}

TEST_CASE("PIPERUN compass snaps an off-angle pick to the nearest preset ray from the last vertex",
          "[issue486][piperun][compass]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);  // start point
  REQUIRE(st.polarIncrementDeg == 90.0);          // default REQ-108 increment
  // (1, 9) is close to but not exactly the +Y ray (90 degrees) from the start point.
  SubmitPipeRunViewportPick(st, 1.f, 9.f, log);
  REQUIRE(st.pipeRunDraftVerts.size() == 6);
  CHECK(std::fabs(st.pipeRunDraftVerts[3] - 0.0) < 1e-4);  // X pulled back onto the +Y ray
  CHECK(st.pipeRunDraftVerts[4] > 8.9);                    // distance along the ray is preserved
}

TEST_CASE("PIPERUN COMPASS toggles off and the raw off-angle pick is then placed unsnapped",
          "[issue486][piperun][compass]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);  // start point
  REQUIRE(HandlePipeRunTextInput("compass", st, log));
  CHECK_FALSE(st.pipeRunCompassOn);
  SubmitPipeRunViewportPick(st, 1.f, 9.f, log);
  REQUIRE(st.pipeRunDraftVerts.size() == 6);
  CHECK(st.pipeRunDraftVerts[3] == 1.0);  // unsnapped — exactly as picked
  CHECK(st.pipeRunDraftVerts[4] == 9.0);

  // Toggling back on restores the compass's own default gate (does not touch st.polarMode, which
  // is left at its own default of off — the two toggles are independent, D-2026-09-17-a).
  REQUIRE(HandlePipeRunTextInput("compass", st, log));
  CHECK(st.pipeRunCompassOn);
  CHECK_FALSE(st.polarMode);
}

TEST_CASE("PIPERUN typed distance commits an exact-length segment along the compass-snapped "
          "cursor direction",
          "[issue486][piperun][compass]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);  // start point
  // Cursor hovering near the +Y ray (90 degrees) — as if the compass preview is showing that ray.
  st.uiCursorWorldX = 0.5f;
  st.uiCursorWorldY = 20.f;
  REQUIRE(HandlePipeRunTextInput("7.5", st, log));
  REQUIRE(st.pipeRunDraftVerts.size() == 6);
  CHECK(std::fabs(st.pipeRunDraftVerts[3] - 0.0) < 1e-4);
  CHECK(std::fabs(st.pipeRunDraftVerts[4] - 7.5) < 1e-4);
}

TEST_CASE("PIPERUN typed distance commits correctly under a Front UCS, where the snapped ray runs "
          "along world Z",
          "[issue486][piperun][compass]") {
  // Regression: the typed-distance branch used to compute its direction/length from world X/Y ONLY.
  // Under a Front UCS (issue #371's own example), a vertical (UCS +Y) ray runs along world Z, not
  // X or Y, so that length came out ~0 and fell through to the point parser, producing "could not
  // read the base point" for what should have been an ordinary typed distance.
  AppCommandState st;
  st.activeUcs = ucs::RotatedAboutX(ucs::Ucs{}, 90.0);
  st.polarIncrementDeg = 90.0;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);  // start point, world (0,0,0)
  // Cursor hovering near straight up the UCS +Y axis, which is world +Z here.
  st.uiCursorWorldX = 0.1f;
  st.uiCursorWorldY = 0.f;
  st.uiCursorWorldZ = 20.f;
  st.resolvedPointZValid = true;
  st.resolvedPointZ = 20.f;
  REQUIRE(HandlePipeRunTextInput("7.5", st, log));
  REQUIRE(st.pipeRunDraftVerts.size() == 6);
  CHECK(std::fabs(st.pipeRunDraftVerts[3] - 0.0) < 1e-3);  // world X: on the ray
  CHECK(std::fabs(st.pipeRunDraftVerts[4] - 0.0) < 1e-3);  // world Y: on the anchor's own plane
  CHECK(std::fabs(st.pipeRunDraftVerts[5] - 7.5) < 1e-3);  // world Z: the typed distance, not ~0
}

TEST_CASE("PIPERUN compass flattens an off-plane object-snap hit onto the anchor's own UCS plane",
          "[issue486][piperun][compass]") {
  // A real 3D feature (a flange face, an existing pipe's own wall) can sit off the anchor's own
  // UCS plane. SnapToPolarRay's own documented contract preserves that out-of-plane offset — right
  // for ordinary POLAR, whose target always already lies on the work plane, but wrong for PIPERUN's
  // compass: the ring drawn on screen promises a flat disc through the anchor, and silently
  // carrying a face's stray depth through would land the pipe off that disc.
  AppCommandState st;
  st.activeUcs = ucs::RotatedAboutX(ucs::Ucs{}, 90.0);  // Front: world Y is out-of-plane
  st.polarIncrementDeg = 90.0;
  const float anchorX = 0.f, anchorY = 0.f, anchorZ = 0.f;
  // World (0.2, 4, 10): close to straight up the UCS +Y (world +Z) axis, but carrying a 4-unit
  // out-of-plane (world Y) offset the way a real face snap would.
  float wx = 0.2f, wy = 4.f;
  const float targetZ = 10.f;
  float wz = 0.f;
  ApplyPipeRunCompassFromAnchor(st, anchorX, anchorY, &wx, &wy, /*compass=*/true, anchorZ, targetZ, &wz);
  CHECK(std::fabs(wy - 0.0) < 1e-3);  // world Y forced back onto the anchor's own plane
  CHECK(std::fabs(wx - 0.0) < 1e-3);  // world X locked onto the vertical (UCS +Y) ray
  CHECK(wz > 9.9);                    // world Z preserved, up the snapped ray
}

// --- PIPESYS piping networks (issue #486 increment B3, REQ-345) --------------------------------

namespace {
AppCommandState MakeStateWithTwoPipeRuns() {
  AppCommandState st;
  CadPipeRun a; a.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0}; a.nominalSize = "4in";
  CadPipeRun b; b.vertsXyz = {0.0, 0.0, 0.0, 0.0, 10.0, 0.0}; b.nominalSize = "2in";
  st.cadPipeRuns.push_back(a);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});
  st.cadPipeRuns.push_back(b);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});
  return st;
}
} // namespace

TEST_CASE("PIPESYS NEW creates a named, empty network", "[issue486][pipesys]") {
  AppCommandState st;
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Cooling Loop 1", st, log);
  REQUIRE(st.cadPipingSystems.size() == 1);
  CHECK(st.cadPipingSystems[0].name == "Cooling Loop 1");
  CHECK(st.cadPipingSystems[0].pipeRunIndices.empty());
}

TEST_CASE("PIPESYS NEW refuses a blank name and a duplicate name", "[issue486][pipesys]") {
  AppCommandState st;
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW", st, log);
  CHECK(st.cadPipingSystems.empty());
  HandlePipingSystemCommand("NEW Loop A", st, log);
  REQUIRE(st.cadPipingSystems.size() == 1);
  HandlePipingSystemCommand("NEW Loop A", st, log);
  CHECK(st.cadPipingSystems.size() == 1);  // refused, not duplicated
}

TEST_CASE("PIPESYS ADD moves the selected pipe run into the named network", "[issue486][pipesys]") {
  AppCommandState st = MakeStateWithTwoPipeRuns();
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Loop A", st, log);
  SelectedEntity e{}; e.type = SelectedEntity::Type::PipeRun; e.index = 0;
  st.selection.push_back(e);
  HandlePipingSystemCommand("ADD Loop A", st, log);
  REQUIRE(st.cadPipingSystems[0].pipeRunIndices.size() == 1);
  CHECK(st.cadPipingSystems[0].pipeRunIndices[0] == 0);
}

TEST_CASE("PIPESYS ADD refuses with no pipe run selected", "[issue486][pipesys]") {
  AppCommandState st = MakeStateWithTwoPipeRuns();
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Loop A", st, log);
  HandlePipingSystemCommand("ADD Loop A", st, log);
  CHECK(st.cadPipingSystems[0].pipeRunIndices.empty());
}

TEST_CASE("PIPESYS ADD refuses an unknown network name", "[issue486][pipesys]") {
  AppCommandState st = MakeStateWithTwoPipeRuns();
  std::vector<std::string> log;
  SelectedEntity e{}; e.type = SelectedEntity::Type::PipeRun; e.index = 0;
  st.selection.push_back(e);
  HandlePipingSystemCommand("ADD Nonexistent", st, log);
  CHECK(st.cadPipingSystems.empty());
}

TEST_CASE("A run added to a second network is moved out of the first", "[issue486][pipesys]") {
  AppCommandState st = MakeStateWithTwoPipeRuns();
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Loop A", st, log);
  HandlePipingSystemCommand("NEW Loop B", st, log);
  SelectedEntity e{}; e.type = SelectedEntity::Type::PipeRun; e.index = 0;
  st.selection.push_back(e);
  HandlePipingSystemCommand("ADD Loop A", st, log);
  REQUIRE(st.cadPipingSystems[0].pipeRunIndices.size() == 1);
  HandlePipingSystemCommand("ADD Loop B", st, log);
  CHECK(st.cadPipingSystems[0].pipeRunIndices.empty());  // dropped from Loop A
  REQUIRE(st.cadPipingSystems[1].pipeRunIndices.size() == 1);
  CHECK(st.cadPipingSystems[1].pipeRunIndices[0] == 0);
}

TEST_CASE("PIPESYS REMOVE takes the selected run back out of a network", "[issue486][pipesys]") {
  AppCommandState st = MakeStateWithTwoPipeRuns();
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Loop A", st, log);
  SelectedEntity e{}; e.type = SelectedEntity::Type::PipeRun; e.index = 0;
  st.selection.push_back(e);
  HandlePipingSystemCommand("ADD Loop A", st, log);
  REQUIRE(st.cadPipingSystems[0].pipeRunIndices.size() == 1);
  HandlePipingSystemCommand("REMOVE Loop A", st, log);
  CHECK(st.cadPipingSystems[0].pipeRunIndices.empty());
}

TEST_CASE("PIPESYS RENAME changes the network's name without touching its runs", "[issue486][pipesys]") {
  AppCommandState st = MakeStateWithTwoPipeRuns();
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Loop A", st, log);
  SelectedEntity e{}; e.type = SelectedEntity::Type::PipeRun; e.index = 0;
  st.selection.push_back(e);
  HandlePipingSystemCommand("ADD Loop A", st, log);
  HandlePipingSystemCommand("RENAME Loop A Cooling Loop", st, log);
  REQUIRE(st.cadPipingSystems.size() == 1);
  CHECK(st.cadPipingSystems[0].name == "Cooling Loop");
  CHECK(st.cadPipingSystems[0].pipeRunIndices.size() == 1);
}

TEST_CASE("PIPESYS RENAME refuses colliding with an existing name", "[issue486][pipesys]") {
  AppCommandState st;
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Loop A", st, log);
  HandlePipingSystemCommand("NEW Loop B", st, log);
  HandlePipingSystemCommand("RENAME Loop A Loop B", st, log);
  CHECK(st.cadPipingSystems[0].name == "Loop A");  // unchanged
}

TEST_CASE("PIPESYS DELETE removes the network but leaves its pipe runs in place", "[issue486][pipesys]") {
  AppCommandState st = MakeStateWithTwoPipeRuns();
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Loop A", st, log);
  SelectedEntity e{}; e.type = SelectedEntity::Type::PipeRun; e.index = 0;
  st.selection.push_back(e);
  HandlePipingSystemCommand("ADD Loop A", st, log);
  HandlePipingSystemCommand("DELETE Loop A", st, log);
  CHECK(st.cadPipingSystems.empty());
  CHECK(st.cadPipeRuns.size() == 2);  // both runs still exist
}

TEST_CASE("Deleting a pipe run drops it from its network and reindexes the rest", "[issue486][pipesys]") {
  AppCommandState st = MakeStateWithTwoPipeRuns();
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Loop A", st, log);
  SelectedEntity e0{}; e0.type = SelectedEntity::Type::PipeRun; e0.index = 0;
  SelectedEntity e1{}; e1.type = SelectedEntity::Type::PipeRun; e1.index = 1;
  st.selection = {e0, e1};
  HandlePipingSystemCommand("ADD Loop A", st, log);
  REQUIRE(st.cadPipingSystems[0].pipeRunIndices == std::vector<int>({0, 1}));

  // Now select just run 0 and delete it — run 1 should reindex down to 0 in the network.
  st.selection = {e0};
  ExecuteDeleteSelection(st, log);
  REQUIRE(st.cadPipeRuns.size() == 1);
  REQUIRE(st.cadPipingSystems[0].pipeRunIndices.size() == 1);
  CHECK(st.cadPipingSystems[0].pipeRunIndices[0] == 0);
}

TEST_CASE("Bare PIPESYS lists existing networks without erroring on none", "[issue486][pipesys]") {
  AppCommandState st;
  std::vector<std::string> log;
  HandlePipingSystemCommand("", st, log);
  REQUIRE_FALSE(log.empty());
  HandlePipingSystemCommand("NEW Loop A", st, log);
  log.clear();
  HandlePipingSystemCommand("LIST", st, log);
  REQUIRE(log.size() == 1);
  CHECK(log[0].find("Loop A") != std::string::npos);
}

// --- Auto-fitting elbow insertion at bends (issue #486 increment B5, REQ-345) -------------------

namespace {
/// A minimal, geometrically-simplified elbow-90 fitting for testing the auto-fit splitter: both
/// ports sit at the fitting's local origin (physically unrealistic, but the splitter's own logic
/// only reads port normals for orientation and port positions for the placement offset, so a
/// zero-size body is a valid, fully exercising test double). Inlet normal -X, outlet normal +Y —
/// exactly 90 degrees apart, matching the tag.
CadBlockDefinition MakeElbow90Def(const std::string& name, float engagementLength = 0.f) {
  CadBlockDefinition def;
  def.name = name;
  def.partType = CadPipePartType::Elbow90;
  def.nominalSize = "4in";
  CadBlockConnection in;
  in.name = "IN";
  in.nx = -1.f; in.ny = 0.f; in.nz = 0.f;
  in.role = CadBlockConnectionRole::Inlet;
  in.engagementLength = engagementLength;
  CadBlockConnection out;
  out.name = "OUT";
  out.nx = 0.f; out.ny = 1.f; out.nz = 0.f;
  out.role = CadBlockConnectionRole::Outlet;
  def.connections = {in, out};
  return def;
}
} // namespace

TEST_CASE("PIPERUN auto-inserts a matching elbow-90 fitting at a 90-degree bend",
          "[issue486][piperun][autofit]") {
  AppCommandState st;
  st.blockDefs.push_back(MakeElbow90Def("ELBOW90-4IN"));

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);   // 90-degree corner here
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 2);
  REQUIRE(st.cadPipeRuns[0].vertsXyz.size() == 6);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(10.0));
  CHECK(st.cadPipeRuns[0].vertsXyz[4] == Catch::Approx(0.0));
  REQUIRE(st.cadPipeRuns[1].vertsXyz.size() == 6);
  CHECK(st.cadPipeRuns[1].vertsXyz[0] == Catch::Approx(10.0));
  CHECK(st.cadPipeRuns[1].vertsXyz[1] == Catch::Approx(0.0));
  CHECK(st.cadPipeRuns[1].vertsXyz[3] == Catch::Approx(10.0));
  CHECK(st.cadPipeRuns[1].vertsXyz[4] == Catch::Approx(10.0));

  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].defName == "ELBOW90-4IN");
  CHECK(st.cadBlockRefs[0].xf.x == Catch::Approx(10.0f));
  CHECK(st.cadBlockRefs[0].xf.y == Catch::Approx(0.0f));
}

TEST_CASE("A pipe run with no matching library part falls back to the original single smooth run",
          "[issue486][piperun][autofit]") {
  AppCommandState st;  // no blockDefs at all — the pre-B5 behavior every existing test also covers
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRuns[0].vertsXyz.size() == 9);
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("The elbow's engagement length shortens the incoming pipe segment before the bend",
          "[issue486][piperun][autofit]") {
  AppCommandState st;
  st.blockDefs.push_back(MakeElbow90Def("ELBOW90-4IN", /*engagementLength=*/2.f));

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 2);
  REQUIRE(st.cadPipeRuns[0].vertsXyz.size() == 6);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(8.0));  // 10 - 2ft engagement cutback
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.x == Catch::Approx(8.0f));
}

TEST_CASE("An elbow lacking exactly two connection ports falls back to a smooth bend",
          "[issue486][piperun][autofit]") {
  AppCommandState st;
  CadBlockDefinition badDef;
  badDef.name = "ELBOW90-ONEPORT";
  badDef.partType = CadPipePartType::Elbow90;
  badDef.nominalSize = "4in";
  CadBlockConnection only;
  only.nx = -1.f;
  badDef.connections = {only};  // exactly one port — cannot orient a bend
  st.blockDefs.push_back(badDef);

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 1);  // smooth fallback — not split
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("An engagement length too large for the leg falls back to a smooth bend",
          "[issue486][piperun][autofit]") {
  AppCommandState st;
  st.blockDefs.push_back(MakeElbow90Def("ELBOW90-4IN", /*engagementLength=*/100.f));  // >> leg length

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("The whole multi-piece auto-fit commit undoes as one step",
          "[issue486][piperun][autofit]") {
  AppCommandState st;
  st.blockDefs.push_back(MakeElbow90Def("ELBOW90-4IN"));

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRuns.size() == 2);
  REQUIRE(st.cadBlockRefs.size() == 1);

  REQUIRE(DoUndo(st, log));
  CHECK(st.cadPipeRuns.empty());
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("A 45-degree bend matches an elbow-45 catalog part instead of elbow-90",
          "[issue486][piperun][autofit]") {
  AppCommandState st;
  CadBlockDefinition def45;
  def45.name = "ELBOW45-4IN";
  def45.partType = CadPipePartType::Elbow45;
  def45.nominalSize = "4in";
  CadBlockConnection in;
  in.nx = -1.f; in.ny = 0.f; in.nz = 0.f;
  in.role = CadBlockConnectionRole::Inlet;
  CadBlockConnection out;
  const float s = 0.70710678f;
  out.nx = s; out.ny = s; out.nz = 0.f;
  out.role = CadBlockConnectionRole::Outlet;
  def45.connections = {in, out};
  st.blockDefs.push_back(def45);
  // Also register an elbow-90 to prove the 45-degree bend picks the 45 part, not the 90.
  st.blockDefs.push_back(MakeElbow90Def("ELBOW90-4IN"));

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("compass", st, log));  // off — the default-on compass would snap
                                                        // the exact 45-degree pick below toward 90
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);       // corner: 45-degree turn from here
  SubmitPipeRunViewportPick(st, 17.071f, 7.071f, log);  // +X then 45 degrees toward +Y
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 2);
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].defName == "ELBOW45-4IN");
}

// --- Auto-fitting tee insertion at branch nodes (issue #486 increment B6, REQ-345) --------------

namespace {
/// A minimal, geometrically-simplified tee for testing the branch-fit planner: all three ports sit
/// at the fitting's local origin (same test-double simplification `MakeElbow90Def` uses — only
/// port normals/roles and engagement length matter to the planner; the outlet/branch world
/// positions are trusted straight from the model, never independently checked, so a zero-size body
/// is a valid, fully exercising test double here too). Inlet -X, Outlet +X (the straight run
/// through the body), Branch +Y.
CadBlockDefinition MakeTeeDef(const std::string& name, float engagementLength = 0.f) {
  CadBlockDefinition def;
  def.name = name;
  def.partType = CadPipePartType::Tee;
  def.nominalSize = "4in";
  CadBlockConnection in;
  in.name = "IN"; in.nx = -1.f; in.ny = 0.f; in.nz = 0.f;
  in.role = CadBlockConnectionRole::Inlet;
  in.engagementLength = engagementLength;
  CadBlockConnection out;
  out.name = "OUT"; out.nx = 1.f; out.ny = 0.f; out.nz = 0.f;
  out.role = CadBlockConnectionRole::Outlet;
  out.engagementLength = engagementLength;
  CadBlockConnection branch;
  branch.name = "BRANCH"; branch.nx = 0.f; branch.ny = 1.f; branch.nz = 0.f;
  branch.role = CadBlockConnectionRole::Branch;
  branch.engagementLength = engagementLength;
  def.connections = {in, out, branch};
  return def;
}

/// Two existing straight pipe runs forming a through line along X, both ending exactly at the
/// origin — the two "other" legs a third run's endpoint can branch-tee into.
AppCommandState MakeStateWithThroughRunsAtOrigin() {
  AppCommandState st;
  CadPipeRun a;  // -X leg: runs from (-10,0,0) to (0,0,0), ending AT the origin
  a.vertsXyz = {-10.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  a.nominalSize = "4in";
  CadPipeRun b;  // +X leg: runs from (0,0,0) to (10,0,0), starting AT the origin
  b.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  b.nominalSize = "4in";
  st.cadPipeRuns = {a, b};
  st.cadPipeRunAttrs = {EntityAttributes{}, EntityAttributes{}};
  return st;
}
} // namespace

TEST_CASE("PIPERUN auto-inserts a tee where a new run's endpoint meets two existing runs' ends",
          "[issue486][piperun][branch]") {
  AppCommandState st = MakeStateWithThroughRunsAtOrigin();
  st.blockDefs.push_back(MakeTeeDef("TEE-4IN"));

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);   // start exactly at the existing runs' shared end
  SubmitPipeRunViewportPick(st, 0.f, 10.f, log);  // branch leg: +Y
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 3);
  // The two pre-existing runs are unaffected here (zero engagement length ⇒ no cutback), still
  // ending exactly at the origin.
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(0.0));
  CHECK(st.cadPipeRuns[1].vertsXyz[0] == Catch::Approx(0.0));
  REQUIRE(st.cadPipeRuns[2].vertsXyz.size() == 6);
  CHECK(st.cadPipeRuns[2].vertsXyz[0] == Catch::Approx(0.0));
  CHECK(st.cadPipeRuns[2].vertsXyz[1] == Catch::Approx(0.0));

  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].defName == "TEE-4IN");
  CHECK(st.cadBlockRefs[0].xf.x == Catch::Approx(0.0f));
  CHECK(st.cadBlockRefs[0].xf.y == Catch::Approx(0.0f));
}

TEST_CASE("The tee's engagement length shortens the pinned (inlet) leg at the branch node",
          "[issue486][piperun][branch]") {
  // Only the INLET side is independently pinned by cutA — the SAME "trust the model" reasoning
  // `MakeElbow90Def`'s own engagement-length test relies on for an elbow's far side applies here to
  // BOTH the outlet and branch: with every port at the fitting's local origin (this test double's
  // own simplification), their solved world positions come out identical to the pinned inlet point
  // rather than independently offset — a real, non-degenerate block would place them correctly.
  AppCommandState st = MakeStateWithThroughRunsAtOrigin();
  st.blockDefs.push_back(MakeTeeDef("TEE-4IN", /*engagementLength=*/2.f));

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 0.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 3);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(-2.0));  // -X leg (inlet) cut back 2ft
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].defName == "TEE-4IN");
}

TEST_CASE("A branch that lands on only one existing run's endpoint is not enough legs for a tee",
          "[issue486][piperun][branch]") {
  AppCommandState st;
  CadPipeRun a; a.vertsXyz = {-10.0, 0.0, 0.0, 0.0, 0.0, 0.0}; a.nominalSize = "4in";
  st.cadPipeRuns = {a};
  st.cadPipeRunAttrs = {EntityAttributes{}};
  st.blockDefs.push_back(MakeTeeDef("TEE-4IN"));

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 0.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 2);  // no tee — only one other leg present
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("Three runs meeting without a roughly-straight through pair get no tee",
          "[issue486][piperun][branch]") {
  AppCommandState st;
  // Both existing legs point the SAME way (+X) — no pair is anywhere near opposite.
  CadPipeRun a; a.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0}; a.nominalSize = "4in";
  CadPipeRun b; b.vertsXyz = {0.0, 0.0, 0.0, 8.0, 2.0, 0.0}; b.nominalSize = "4in";
  st.cadPipeRuns = {a, b};
  st.cadPipeRunAttrs = {EntityAttributes{}, EntityAttributes{}};
  st.blockDefs.push_back(MakeTeeDef("TEE-4IN"));

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 0.f, -10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 3);  // all three still committed, just no tee/cutback
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(10.0));
  CHECK(st.cadPipeRuns[1].vertsXyz[3] == Catch::Approx(8.0));
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("An engagement length too large for one of the three legs falls back to no tee",
          "[issue486][piperun][branch]") {
  AppCommandState st = MakeStateWithThroughRunsAtOrigin();
  st.blockDefs.push_back(MakeTeeDef("TEE-4IN", /*engagementLength=*/100.f));  // >> any leg length

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 0.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 3);
  CHECK(st.cadBlockRefs.empty());
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(0.0));  // untouched — refusal is local
}

TEST_CASE("The tee commit (new run + two cutback existing runs + block) undoes as one step",
          "[issue486][piperun][branch]") {
  AppCommandState st = MakeStateWithThroughRunsAtOrigin();
  st.blockDefs.push_back(MakeTeeDef("TEE-4IN", /*engagementLength=*/2.f));

  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 0.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRuns.size() == 3);
  REQUIRE(st.cadBlockRefs.size() == 1);

  REQUIRE(DoUndo(st, log));
  REQUIRE(st.cadPipeRuns.size() == 2);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(0.0));  // existing runs' cutback undone too
  CHECK(st.cadPipeRuns[1].vertsXyz[0] == Catch::Approx(0.0));
  CHECK(st.cadBlockRefs.empty());
}

// --- Manual fitting placement on a run (issue #486 increment B7, REQ-345) -----------------------

namespace {
/// A minimal, geometrically-simplified inline 2-port fitting (a valve) — same test-double
/// simplification `MakeElbow90Def`/`MakeTeeDef` use: both ports at the local origin. Inlet -X,
/// Outlet +X — the fitting sits ACROSS the pipe direction, same convention as a tee's through pair.
CadBlockDefinition MakeValveDef(const std::string& name, float engagementLength = 0.f) {
  CadBlockDefinition def;
  def.name = name;
  def.partType = CadPipePartType::Valve;
  def.nominalSize = "4in";
  CadBlockConnection in;
  in.name = "IN"; in.nx = -1.f; in.ny = 0.f; in.nz = 0.f;
  in.role = CadBlockConnectionRole::Inlet;
  in.engagementLength = engagementLength;
  CadBlockConnection out;
  out.name = "OUT"; out.nx = 1.f; out.ny = 0.f; out.nz = 0.f;
  out.role = CadBlockConnectionRole::Outlet;
  out.engagementLength = engagementLength;
  def.connections = {in, out};
  return def;
}

AppCommandState MakeStateWithOneStraightRun() {
  AppCommandState st;
  CadPipeRun a;
  a.vertsXyz = {0.0, 0.0, 0.0, 20.0, 0.0, 0.0};
  a.nominalSize = "4in";
  st.cadPipeRuns = {a};
  st.cadPipeRunAttrs = {EntityAttributes{}};
  return st;
}

void SelectPipeRun(AppCommandState& st, int index) {
  SelectedEntity e{};
  e.type = SelectedEntity::Type::PipeRun;
  e.index = index;
  st.selection = {e};
}
} // namespace

TEST_CASE("PIPEFIT refuses without exactly one pipe run selected", "[issue486][pipefit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  std::vector<std::string> log;
  StartPipeFitCommand(st, "valve", log);
  CHECK(st.active == AppCommandState::Kind::None);
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("select exactly one") != std::string::npos);
}

TEST_CASE("PIPEFIT refuses an unknown part type", "[issue486][pipefit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  SelectPipeRun(st, 0);
  std::vector<std::string> log;
  StartPipeFitCommand(st, "not-a-part-type", log);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(log.back().find("unknown part type") != std::string::npos);
}

TEST_CASE("PIPEFIT splices a matching valve at the picked station, splitting the run in two",
          "[issue486][pipefit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  st.blockDefs.push_back(MakeValveDef("VALVE-4IN"));
  SelectPipeRun(st, 0);

  std::vector<std::string> log;
  StartPipeFitCommand(st, "valve", log);
  REQUIRE(st.active == AppCommandState::Kind::PipeFit);
  SubmitPipeFitViewportPick(st, 10.f, 0.f, log);
  CHECK(st.active == AppCommandState::Kind::None);

  REQUIRE(st.cadPipeRuns.size() == 2);
  REQUIRE(st.cadPipeRuns[0].vertsXyz.size() == 6);
  CHECK(st.cadPipeRuns[0].vertsXyz[0] == Catch::Approx(0.0));
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(10.0));  // no engagement ⇒ cut at the pick
  REQUIRE(st.cadPipeRuns[1].vertsXyz.size() == 6);
  CHECK(st.cadPipeRuns[1].vertsXyz[3] == Catch::Approx(20.0));  // tail of the run preserved

  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].defName == "VALVE-4IN");
  CHECK(st.cadBlockRefs[0].xf.x == Catch::Approx(10.0f));
}

TEST_CASE("The valve's inlet engagement length cuts the near side back from the pick",
          "[issue486][pipefit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  st.blockDefs.push_back(MakeValveDef("VALVE-4IN", /*engagementLength=*/1.5f));
  SelectPipeRun(st, 0);

  std::vector<std::string> log;
  StartPipeFitCommand(st, "valve", log);
  SubmitPipeFitViewportPick(st, 10.f, 0.f, log);

  REQUIRE(st.cadPipeRuns.size() == 2);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(8.5));  // 10 - 1.5ft engagement
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.x == Catch::Approx(8.5f));
}

TEST_CASE("A part with only one connection port refuses rather than splicing in",
          "[issue486][pipefit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  CadBlockDefinition badDef;
  badDef.name = "CAP-4IN";
  badDef.partType = CadPipePartType::Valve;
  badDef.nominalSize = "4in";
  CadBlockConnection only;
  only.nx = -1.f;
  badDef.connections = {only};
  st.blockDefs.push_back(badDef);
  SelectPipeRun(st, 0);

  std::vector<std::string> log;
  StartPipeFitCommand(st, "valve", log);
  SubmitPipeFitViewportPick(st, 10.f, 0.f, log);

  REQUIRE(st.cadPipeRuns.size() == 1);  // unchanged — refusal is total, not partial
  CHECK(st.cadPipeRuns[0].vertsXyz.size() == 6);
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("An engagement length too large for either side refuses without splitting the run",
          "[issue486][pipefit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  st.blockDefs.push_back(MakeValveDef("VALVE-4IN", /*engagementLength=*/100.f));
  SelectPipeRun(st, 0);

  std::vector<std::string> log;
  StartPipeFitCommand(st, "valve", log);
  SubmitPipeFitViewportPick(st, 10.f, 0.f, log);

  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRuns[0].vertsXyz.size() == 6);
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("The PIPEFIT splice (split run + block) undoes as one step", "[issue486][pipefit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  st.blockDefs.push_back(MakeValveDef("VALVE-4IN"));
  SelectPipeRun(st, 0);

  std::vector<std::string> log;
  StartPipeFitCommand(st, "valve", log);
  SubmitPipeFitViewportPick(st, 10.f, 0.f, log);
  REQUIRE(st.cadPipeRuns.size() == 2);
  REQUIRE(st.cadBlockRefs.size() == 1);

  REQUIRE(DoUndo(st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(20.0));
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("The new piece from a splice joins the same network the original run belonged to",
          "[issue486][pipefit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  st.blockDefs.push_back(MakeValveDef("VALVE-4IN"));
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Loop A", st, log);
  SelectPipeRun(st, 0);
  HandlePipingSystemCommand("ADD Loop A", st, log);
  REQUIRE(st.cadPipingSystems[0].pipeRunIndices == std::vector<int>({0}));

  StartPipeFitCommand(st, "valve", log);
  SubmitPipeFitViewportPick(st, 10.f, 0.f, log);
  REQUIRE(st.cadPipeRuns.size() == 2);

  CHECK(st.cadPipingSystems[0].pipeRunIndices == std::vector<int>({0, 1}));
}

TEST_CASE("PIPEFIT projects an off-centerline pick onto the nearest point of the run",
          "[issue486][pipefit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  st.blockDefs.push_back(MakeValveDef("VALVE-4IN"));
  SelectPipeRun(st, 0);

  std::vector<std::string> log;
  StartPipeFitCommand(st, "valve", log);
  SubmitPipeFitViewportPick(st, 10.f, 3.f, log);  // 3ft off the centerline

  REQUIRE(st.cadPipeRuns.size() == 2);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(10.0));  // projected straight down onto X
  CHECK(st.cadPipeRuns[0].vertsXyz[4] == Catch::Approx(0.0));
}

// --- A flange must weld its PIPE-END port to the pipe (user report 2026-09-24) -------------------

namespace {
/// The bundled 2in weld-neck flange, reduced to what decides its orientation: TWO ports, BOTH
/// tagged with the Inlet role, each carrying a single mode flagged `isDefault` (what the authoring
/// UI produces for an only-mode port), and the gasket face defined FIRST. Neither the role test nor
/// definition order can tell these apart — only the mode's own target can, which is the whole point
/// of the regression.
CadBlockDefinition MakeWeldNeckFlangeDef(const std::string& name, float neckOffset = 0.2f) {
  CadBlockDefinition def;
  def.name = name;
  def.partType = CadPipePartType::Flange;
  def.nominalSize = "4in";

  CadBlockConnection gasket;
  gasket.name = "gasketFace";
  gasket.x = 0.f; gasket.y = 0.f; gasket.z = 0.f;
  gasket.nx = 0.f; gasket.ny = -1.f; gasket.nz = 0.f;
  gasket.role = CadBlockConnectionRole::Inlet;
  CadBlockConnectionMode gasketMode;
  gasketMode.name = "Mode 1";
  gasketMode.target = CadConnectionModeTarget::FlangeFace;
  gasketMode.role = CadBlockConnectionRole::Inlet;
  gasketMode.isDefault = true;
  gasket.modes = {gasketMode};

  CadBlockConnection neck;
  neck.name = "weldNeckFace";
  neck.x = 0.f; neck.y = neckOffset; neck.z = 0.f;
  neck.nx = 0.f; neck.ny = 1.f; neck.nz = 0.f;
  neck.role = CadBlockConnectionRole::Inlet;
  CadBlockConnectionMode neckMode;
  neckMode.name = "Mode 1";
  neckMode.target = CadConnectionModeTarget::PipeEnd;
  neckMode.role = CadBlockConnectionRole::Inlet;
  neckMode.isDefault = true;
  neck.modes = {neckMode};

  def.connections = {gasket, neck};  // gasket FIRST, as the bundled part defines it
  return def;
}

/// World position of \p def's connection \p connIndex under the placed reference's transform.
ray3d::Vec3 PlacedPortWorld(const AppCommandState& st, const CadBlockRef& ref, size_t connIndex) {
  const int di = CadBlockFindDef(st.blockDefs, ref.defName);
  REQUIRE(di >= 0);
  const CadBlockConnection& c = st.blockDefs[static_cast<size_t>(di)].connections[connIndex];
  float wx = 0.f, wy = 0.f, wz = 0.f;
  CadBlockXformPoint(ref.xf, c.x, c.y, c.z, &wx, &wy, &wz);
  return ray3d::Vec3{wx, wy, wz};
}
} // namespace

TEST_CASE("A flange welds its pipe-end port to the pipe, not its flange face",
          "[issue486][pipefit][flangeport]") {
  AppCommandState st = MakeStateWithOneStraightRun();  // (0,0,0) -> (20,0,0)
  st.blockDefs.push_back(MakeWeldNeckFlangeDef("FLANGE-4IN"));

  std::vector<std::string> log;
  const ray3d::Vec3 pick{10.0, 0.0, 0.0};
  REQUIRE(CadPipeFitNamedAtPick(st, 0, "FLANGE-4IN", pick, log));

  REQUIRE(st.cadBlockRefs.size() == 1);
  // Port 0 is the gasket face, port 1 the weld neck. The weld neck is what the upstream pipe ends
  // against; the gasket face is the free end, one flange length downstream. Both ports land at one
  // of these two points either way — WHICH port lands on the pipe is the entire bug.
  const ray3d::Vec3 gasket = PlacedPortWorld(st, st.cadBlockRefs[0], 0);
  const ray3d::Vec3 neck = PlacedPortWorld(st, st.cadBlockRefs[0], 1);
  CHECK(neck.x == Catch::Approx(10.0).margin(1e-4));
  CHECK(gasket.x == Catch::Approx(10.2).margin(1e-4));
}

TEST_CASE("The flange's pipe-end port outranks role and definition order for the cutback too",
          "[issue486][pipefit][flangeport]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  CadBlockDefinition def = MakeWeldNeckFlangeDef("FLANGE-4IN");
  // Only the weld neck engages a pipe; the gasket face never slides onto anything. With the ports
  // swapped this cutback would be read off the gasket face instead and the pipe would stop short.
  def.connections[1].modes[0].engagementLength = 1.5f;
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  const ray3d::Vec3 pick{10.0, 0.0, 0.0};
  REQUIRE(CadPipeFitNamedAtPick(st, 0, "FLANGE-4IN", pick, log));

  REQUIRE(st.cadPipeRuns.size() == 2);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(8.5));  // 10 - the weld neck's 1.5ft
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(PlacedPortWorld(st, st.cadBlockRefs[0], 1).x == Catch::Approx(8.5).margin(1e-4));
}

TEST_CASE("A fitting with a pipe end on BOTH sides keeps its Inlet/Outlet resolution",
          "[issue486][pipefit][flangeport]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  // An inline valve, both ports explicitly tagged for a pipe end — the rule that saves the flange
  // must not fire here, or a through-run fitting would be resolved by definition order instead of
  // by the roles it was authored with.
  CadBlockDefinition def = MakeValveDef("VALVE-4IN");
  for (CadBlockConnection& c : def.connections) {
    CadBlockConnectionMode m;
    m.name = "Mode 1";
    m.target = CadConnectionModeTarget::PipeEnd;
    m.role = c.role;
    m.isDefault = true;
    m.engagementLength = 0.f;
    c.modes = {m};
  }
  std::swap(def.connections[0], def.connections[1]);  // Outlet defined FIRST
  st.blockDefs.push_back(def);

  std::vector<std::string> log;
  const ray3d::Vec3 pick{10.0, 0.0, 0.0};
  REQUIRE(CadPipeFitNamedAtPick(st, 0, "VALVE-4IN", pick, log));
  REQUIRE(st.cadPipeRuns.size() == 2);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(10.0));
}
// --- PIPESPLIT / PIPEJOIN / PIPEPROP (issue #486 increment B8, REQ-345) ---------------------------

TEST_CASE("PIPESPLIT refuses without exactly one pipe run selected", "[issue486][pipesplit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  std::vector<std::string> log;
  StartPipeSplitCommand(st, log);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(log.back().find("select exactly one") != std::string::npos);
}

TEST_CASE("PIPESPLIT splits a run at the picked station with no fitting inserted",
          "[issue486][pipesplit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  SelectPipeRun(st, 0);
  std::vector<std::string> log;
  StartPipeSplitCommand(st, log);
  REQUIRE(st.active == AppCommandState::Kind::PipeSplit);
  SubmitPipeSplitViewportPick(st, 10.f, 0.f, log);
  CHECK(st.active == AppCommandState::Kind::None);

  REQUIRE(st.cadPipeRuns.size() == 2);
  REQUIRE(st.cadPipeRuns[0].vertsXyz.size() == 6);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(10.0));
  REQUIRE(st.cadPipeRuns[1].vertsXyz.size() == 6);
  CHECK(st.cadPipeRuns[1].vertsXyz[0] == Catch::Approx(10.0));  // no gap — exact split, no cutback
  CHECK(st.cadPipeRuns[1].vertsXyz[3] == Catch::Approx(20.0));
  CHECK(st.cadBlockRefs.empty());
}

TEST_CASE("PIPESPLIT undoes as one step", "[issue486][pipesplit]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  SelectPipeRun(st, 0);
  std::vector<std::string> log;
  StartPipeSplitCommand(st, log);
  SubmitPipeSplitViewportPick(st, 10.f, 0.f, log);
  REQUIRE(st.cadPipeRuns.size() == 2);

  REQUIRE(DoUndo(st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(20.0));
}

TEST_CASE("PIPEJOIN refuses without exactly two pipe runs selected", "[issue486][pipejoin]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  SelectPipeRun(st, 0);
  std::vector<std::string> log;
  HandlePipeJoinCommand(st, log);
  CHECK(st.cadPipeRuns.size() == 1);
  CHECK(log.back().find("select exactly two") != std::string::npos);
}

TEST_CASE("PIPEJOIN refuses runs with different size or pressure class", "[issue486][pipejoin]") {
  AppCommandState st;
  CadPipeRun a; a.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0}; a.nominalSize = "4in";
  CadPipeRun b; b.vertsXyz = {10.0, 0.0, 0.0, 20.0, 0.0, 0.0}; b.nominalSize = "2in";
  st.cadPipeRuns = {a, b};
  st.cadPipeRunAttrs = {EntityAttributes{}, EntityAttributes{}};
  SelectedEntity e0{}; e0.type = SelectedEntity::Type::PipeRun; e0.index = 0;
  SelectedEntity e1{}; e1.type = SelectedEntity::Type::PipeRun; e1.index = 1;
  st.selection = {e0, e1};
  std::vector<std::string> log;
  HandlePipeJoinCommand(st, log);
  CHECK(st.cadPipeRuns.size() == 2);
  CHECK(log.back().find("same nominal size") != std::string::npos);
}

TEST_CASE("PIPEJOIN refuses runs with no coincident endpoint", "[issue486][pipejoin]") {
  AppCommandState st;
  CadPipeRun a; a.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0}; a.nominalSize = "4in";
  CadPipeRun b; b.vertsXyz = {50.0, 0.0, 0.0, 60.0, 0.0, 0.0}; b.nominalSize = "4in";
  st.cadPipeRuns = {a, b};
  st.cadPipeRunAttrs = {EntityAttributes{}, EntityAttributes{}};
  SelectedEntity e0{}; e0.type = SelectedEntity::Type::PipeRun; e0.index = 0;
  SelectedEntity e1{}; e1.type = SelectedEntity::Type::PipeRun; e1.index = 1;
  st.selection = {e0, e1};
  std::vector<std::string> log;
  HandlePipeJoinCommand(st, log);
  CHECK(st.cadPipeRuns.size() == 2);
  CHECK(log.back().find("coincident endpoint") != std::string::npos);
}

TEST_CASE("PIPEJOIN merges two runs sharing an endpoint into one, in every end-pairing",
          "[issue486][pipejoin]") {
  struct Case { ray3d::Vec3 a0, a1, b0, b1; };
  const std::vector<Case> cases = {
      {{0, 0, 0}, {10, 0, 0}, {10, 0, 0}, {20, 0, 0}},  // A end == B start
      {{0, 0, 0}, {10, 0, 0}, {20, 0, 0}, {10, 0, 0}},  // A end == B end
      {{10, 0, 0}, {0, 0, 0}, {10, 0, 0}, {20, 0, 0}},  // A start == B start
      {{10, 0, 0}, {0, 0, 0}, {20, 0, 0}, {10, 0, 0}},  // A start == B end
  };
  for (const Case& c : cases) {
    AppCommandState st;
    CadPipeRun a; a.vertsXyz = {c.a0.x, c.a0.y, c.a0.z, c.a1.x, c.a1.y, c.a1.z}; a.nominalSize = "4in";
    CadPipeRun b; b.vertsXyz = {c.b0.x, c.b0.y, c.b0.z, c.b1.x, c.b1.y, c.b1.z}; b.nominalSize = "4in";
    st.cadPipeRuns = {a, b};
    st.cadPipeRunAttrs = {EntityAttributes{}, EntityAttributes{}};
    SelectedEntity e0{}; e0.type = SelectedEntity::Type::PipeRun; e0.index = 0;
    SelectedEntity e1{}; e1.type = SelectedEntity::Type::PipeRun; e1.index = 1;
    st.selection = {e0, e1};
    std::vector<std::string> log;
    HandlePipeJoinCommand(st, log);
    REQUIRE(st.cadPipeRuns.size() == 1);
    REQUIRE(st.cadPipeRuns[0].vertsXyz.size() == 9);  // 3 distinct vertices, 0..10..20 in some order
    CHECK(st.cadPipeRuns[0].vertsXyz[3] == Catch::Approx(10.0));
    CHECK(log.back().find("merged") != std::string::npos);
  }
}

TEST_CASE("PIPEJOIN undoes as one step and reindexes the piping-network reference",
          "[issue486][pipejoin]") {
  AppCommandState st;
  CadPipeRun a; a.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0}; a.nominalSize = "4in";
  CadPipeRun b; b.vertsXyz = {10.0, 0.0, 0.0, 20.0, 0.0, 0.0}; b.nominalSize = "4in";
  st.cadPipeRuns = {a, b};
  st.cadPipeRunAttrs = {EntityAttributes{}, EntityAttributes{}};
  std::vector<std::string> log;
  HandlePipingSystemCommand("NEW Loop A", st, log);
  SelectedEntity e1{}; e1.type = SelectedEntity::Type::PipeRun; e1.index = 1;
  st.selection = {e1};
  HandlePipingSystemCommand("ADD Loop A", st, log);
  REQUIRE(st.cadPipingSystems[0].pipeRunIndices == std::vector<int>({1}));

  SelectedEntity e0{}; e0.type = SelectedEntity::Type::PipeRun; e0.index = 0;
  st.selection = {e0, e1};
  HandlePipeJoinCommand(st, log);
  REQUIRE(st.cadPipeRuns.size() == 1);
  // Run 1 was erased (index 0 survived as the merged run) — the network's reference to the erased
  // index 1 is simply dropped, same as an ordinary delete; nothing left to reindex above it.
  CHECK(st.cadPipingSystems[0].pipeRunIndices.empty());

  REQUIRE(DoUndo(st, log));
  REQUIRE(st.cadPipeRuns.size() == 2);
  CHECK(st.cadPipingSystems[0].pipeRunIndices == std::vector<int>({1}));
}

TEST_CASE("PIPEPROP refuses without a pipe run selected", "[issue486][pipeprop]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  std::vector<std::string> log;
  HandlePipePropCommand("6in", st, log);
  CHECK(log.back().find("select one or more") != std::string::npos);
}

TEST_CASE("PIPEPROP refuses an unknown nominal size", "[issue486][pipeprop]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  SelectPipeRun(st, 0);
  std::vector<std::string> log;
  HandlePipePropCommand("99in", st, log);
  CHECK(log.back().find("unknown nominal size") != std::string::npos);
  CHECK(st.cadPipeRuns[0].nominalSize == "4in");  // unchanged
}

TEST_CASE("PIPEPROP updates nominal size and pressure class on the selected run",
          "[issue486][pipeprop]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  SelectPipeRun(st, 0);
  std::vector<std::string> log;
  HandlePipePropCommand("6in CS300", st, log);
  REQUIRE(st.cadPipeRuns[0].nominalSize == "6in");
  CHECK(st.cadPipeRuns[0].pressureClassTag == "CS300");
  CHECK(log.back().find("1 run(s) updated") != std::string::npos);
}

TEST_CASE("PIPEPROP takes a wall thickness, and says so when a wall no longer fits",
          "[issue486][pipeprop][wall]") {
  // D-2026-09-23-a. A bare number after the size is the wall, so `PIPEPROP 4in CS150` keeps working
  // and `PIPEPROP 4in 0.5` reaches the wall.
  AppCommandState st = MakeStateWithOneStraightRun();
  SelectPipeRun(st, 0);
  std::vector<std::string> log;
  HandlePipePropCommand("4in CS150 0.5", st, log);
  CHECK(st.cadPipeRuns[0].pressureClassTag == "CS150");
  CHECK(st.cadPipeRuns[0].wallThicknessIn == Catch::Approx(0.5));
  CHECK(log.back().find("1 run(s) updated") != std::string::npos);

  // That 0.5in wall does not fit 0.5in pipe (OD 0.840in): refused, and told in terms of the WALL
  // rather than blamed on a fillet radius it has nothing to do with.
  log.clear();
  HandlePipePropCommand("0.5in", st, log);
  CHECK(st.cadPipeRuns[0].nominalSize == "4in");  // unchanged
  CHECK(log.back().find("no bore") != std::string::npos);

  // Stating a thinner wall in the same breath is what makes it fit.
  log.clear();
  HandlePipePropCommand("0.5in 0.109", st, log);
  CHECK(st.cadPipeRuns[0].nominalSize == "0.5in");
  CHECK(st.cadPipeRuns[0].wallThicknessIn == Catch::Approx(0.109));

  // A wall that is not a wall is refused before any run is touched.
  log.clear();
  HandlePipePropCommand("4in 3.0", st, log);
  CHECK(st.cadPipeRuns[0].nominalSize == "0.5in");
  CHECK(log.back().find("no bore") != std::string::npos);
}

TEST_CASE("PIPEPROP updates every selected run independently, batch-style",
          "[issue486][pipeprop]") {
  AppCommandState st;
  CadPipeRun a; a.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0}; a.nominalSize = "4in";
  CadPipeRun b; b.vertsXyz = {20.0, 0.0, 0.0, 30.0, 0.0, 0.0}; b.nominalSize = "4in";
  st.cadPipeRuns = {a, b};
  st.cadPipeRunAttrs = {EntityAttributes{}, EntityAttributes{}};
  SelectedEntity e0{}; e0.type = SelectedEntity::Type::PipeRun; e0.index = 0;
  SelectedEntity e1{}; e1.type = SelectedEntity::Type::PipeRun; e1.index = 1;
  st.selection = {e0, e1};
  std::vector<std::string> log;
  HandlePipePropCommand("2in", st, log);
  CHECK(st.cadPipeRuns[0].nominalSize == "2in");
  CHECK(st.cadPipeRuns[1].nominalSize == "2in");
  CHECK(log.back().find("2 run(s) updated") != std::string::npos);
}

TEST_CASE("PIPEPROP undoes as one step", "[issue486][pipeprop]") {
  AppCommandState st = MakeStateWithOneStraightRun();
  SelectPipeRun(st, 0);
  std::vector<std::string> log;
  HandlePipePropCommand("6in", st, log);
  REQUIRE(st.cadPipeRuns[0].nominalSize == "6in");
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadPipeRuns[0].nominalSize == "4in");
}

TEST_CASE("Existing axis-aligned PIPERUN picks are unaffected by the default-on compass",
          "[issue486][piperun][compass]") {
  // Pins that REQ-346 does not regress the original increment-B2 tests: picks already exactly on a
  // preset ray commit at the exact picked coordinates, not a slightly-perturbed snap result.
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);
  const std::vector<double>& v = st.cadPipeRuns[0].vertsXyz;
  REQUIRE(v.size() == 9);
  CHECK(v[3] == 10.0);
  CHECK(v[4] == 0.0);
  CHECK(v[6] == 10.0);
  CHECK(v[7] == 10.0);
}

TEST_CASE("A pipe run belongs to its own drawing tab", "[issue486][piperun][command]") {
  // Regression: cadPipeRuns/cadPipeRunAttrs/cadPipingSystems were absent from DrawingDocument, so a
  // tab switch (and File > New, which restores an empty document into the live state) left the
  // previous drawing's runs rendering, selectable and snappable in the drawing the user switched
  // to. Same defect the section clip had, same fix.
  AppCommandState st;
  st.documents.resize(3);  // [0] backs the Start sentinel tab; 1 and 2 are drawings
  std::vector<std::string> log;

  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);
  CadPipingSystem sys;
  sys.name = "CW";
  sys.pipeRunIndices.push_back(0);
  st.cadPipingSystems.push_back(sys);
  RefreshSolidDisplayGeometry(st);
  REQUIRE_FALSE(st.pipeRunWorldSolids.empty());
  SaveDocumentToSnapshot(st, 1);

  RestoreDocumentFromSnapshot(st, 2);  // a drawing that never had a pipe run
  CHECK(st.cadPipeRuns.empty());
  CHECK(st.cadPipeRunAttrs.empty());
  CHECK(st.cadPipingSystems.empty());
  CHECK(st.pipeRunWorldSolids.empty());
  RefreshSolidDisplayGeometry(st);  // and the derived solids do not come back on the next frame
  CHECK(st.pipeRunWorldSolids.empty());

  RestoreDocumentFromSnapshot(st, 1);  // back to the drawing that owns the run
  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRuns[0].nominalSize == "4in");
  CHECK(st.cadPipeRunAttrs.size() == 1);
  REQUIRE(st.cadPipingSystems.size() == 1);
  CHECK(st.cadPipingSystems[0].name == "CW");
  RefreshSolidDisplayGeometry(st);
  CHECK_FALSE(st.pipeRunWorldSolids.empty());
}

TEST_CASE("Clearing a drawing's CAD geometry clears its pipe runs", "[issue486][piperun][command]") {
  // ClearCadGeometry is what a DXF/DWG import calls to replace the drawing's CAD content; pipe runs
  // were not in it, so an import into a drawing that had runs kept them beside the imported model.
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // wall: take the schedule-40 default
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  RefreshSolidDisplayGeometry(st);
  REQUIRE(st.cadPipeRuns.size() == 1);
  REQUIRE_FALSE(st.pipeRunWorldSolids.empty());

  ClearCadGeometry(st);
  CHECK(st.cadPipeRuns.empty());
  CHECK(st.cadPipeRunAttrs.empty());
  CHECK(st.cadPipingSystems.empty());
  CHECK(st.pipeRunWorldSolids.empty());
  RefreshSolidDisplayGeometry(st);
  CHECK(st.pipeRunWorldSolids.empty());
}

// --- REQ-350 (f): placing a part armed from the Pipe Fittings palette ---------------------------
//
// One gesture, two outcomes, decided by where the second click lands: on a run it splices (the
// PIPEFIT path, with cutback and a run split in two); off every run it places an ordinary block
// reference. These pin both, plus the "armed" flag being strictly one-shot.

namespace {

/// A 4in run along +X from the origin, ten feet long.
void AddFourInchRun(AppCommandState& st) {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  st.cadPipeRuns.push_back(run);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});
}

/// An inline two-port fitting (inlet at its origin, outlet a foot along +X), which is the shape
/// `PickElbowPorts` needs to splice something into a straight run.
void AddInlineFitting(AppCommandState& st, const char* name) {
  CadBlockDefinition def;
  def.name = name;
  def.partType = CadPipePartType::Flange;
  def.nominalSize = "4in";
  CadBlockConnection inlet;
  inlet.name = "P1";
  inlet.role = CadBlockConnectionRole::Inlet;
  inlet.x = 0.f;
  inlet.y = 0.f;
  inlet.z = 0.f;
  inlet.nx = -1.f;
  inlet.ny = 0.f;
  inlet.nz = 0.f;
  CadBlockConnection outlet;
  outlet.name = "P2";
  outlet.role = CadBlockConnectionRole::Outlet;
  outlet.x = 1.f;
  outlet.y = 0.f;
  outlet.z = 0.f;
  outlet.nx = 1.f;
  outlet.ny = 0.f;
  outlet.nz = 0.f;
  def.connections.push_back(inlet);
  def.connections.push_back(outlet);
  st.blockDefs.push_back(def);
}

CadBlockLibraryEntry EntryFor(const char* name) {
  CadBlockLibraryEntry e;
  e.name = name;
  e.imported = true;
  e.isFitting = true;
  e.partType = CadPipePartType::Flange;
  e.nominalSize = "4in";
  return e;
}

} // namespace

TEST_CASE("A palette part armed and clicked ON a run splices into it", "[issue486][req350][palette][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddFourInchRun(st);
  AddInlineFitting(st, "FLANGE4");

  REQUIRE(CadPipePaletteArmPart(st, EntryFor("FLANGE4"), log));
  CHECK(st.active == AppCommandState::Kind::InsertBlock);
  CHECK(st.insertBlockPipeSpliceArmed);
  CHECK(st.insertBlockPhase == AppCommandState::InsertBlockPhase::WaitInsertPoint);
  // Armed for a single click: no scale or rotation prompt stands between the pick and the placement.
  CHECK_FALSE(st.insertBlockSpecifyScale);
  CHECK_FALSE(st.insertBlockSpecifyRot);
  CHECK_FALSE(st.insertBlockSpecifyAlignFace);

  // Mid-span, right on the centreline.
  SubmitInsertBlockPick(st, 5.f, 0.f, 0.f, log);

  // The run is now two pieces with the fitting between them — PIPEFIT's own result.
  CHECK(st.cadPipeRuns.size() == 2);
  CHECK(st.cadPipeRunAttrs.size() == 2);
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].defName == "FLANGE4");
  // Both pieces keep the parent's size, so the palette keeps matching after the splice.
  CHECK(st.cadPipeRuns[0].nominalSize == "4in");
  CHECK(st.cadPipeRuns[1].nominalSize == "4in");
  // One-shot: the arming does not survive its own placement.
  CHECK_FALSE(st.insertBlockPipeSpliceArmed);
  CHECK(st.active == AppCommandState::Kind::None);
}

TEST_CASE("A palette part armed and clicked OFF every run is placed as a plain INSERT",
          "[issue486][req350][palette][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddFourInchRun(st);
  AddInlineFitting(st, "FLANGE4");

  REQUIRE(CadPipePaletteArmPart(st, EntryFor("FLANGE4"), log));
  // Well clear of the run, which lies along the X axis at y = 0.
  SubmitInsertBlockPick(st, 5.f, 40.f, 0.f, log);

  CHECK(st.cadPipeRuns.size() == 1);  // untouched — nothing was spliced
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefs[0].defName == "FLANGE4");
  CHECK(st.cadBlockRefs[0].xf.y == Catch::Approx(40.f));
  CHECK_FALSE(st.insertBlockPipeSpliceArmed);
}

TEST_CASE("Cancelling an armed palette placement leaves the drawing alone",
          "[issue486][req350][palette][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddFourInchRun(st);
  AddInlineFitting(st, "FLANGE4");

  REQUIRE(CadPipePaletteArmPart(st, EntryFor("FLANGE4"), log));
  CancelActiveCommand(st, log);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK_FALSE(st.insertBlockPipeSpliceArmed);
  CHECK(st.cadPipeRuns.size() == 1);
  CHECK(st.cadBlockRefs.empty());

  // And the flag being clear is load-bearing: an ordinary INSERT afterwards must NOT splice, even
  // when its pick lands squarely on a pipe run.
  StartInsertBlockCommand(st, log);
  std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "FLANGE4");
  st.insertBlockSpecifyScale = false;
  st.insertBlockSpecifyRot = false;
  st.insertBlockSpecifyAlignFace = false;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;
  SubmitInsertBlockPick(st, 5.f, 0.f, 0.f, log);
  CHECK(st.cadPipeRuns.size() == 1);  // still one run: a plain INSERT never splits anything
  CHECK(st.cadBlockRefs.size() == 1);
}

TEST_CASE("A run counts as under the pick only near its own centreline",
          "[issue486][req350][palette][command]") {
  AppCommandState st;
  AddFourInchRun(st);  // 4in OD = 0.375 ft, so a radius of 0.1875 ft

  int idx = -1;
  CHECK(CadPipeRunUnderPick(st, ray3d::Vec3{5.0, 0.0, 0.0}, &idx));
  CHECK(idx == 0);
  // Just inside the slack around the pipe wall...
  CHECK(CadPipeRunUnderPick(st, ray3d::Vec3{5.0, 0.2, 0.0}, &idx));
  // ...and well outside it.
  CHECK_FALSE(CadPipeRunUnderPick(st, ray3d::Vec3{5.0, 3.0, 0.0}, &idx));
  CHECK(idx == -1);
  // Past the end of the run is not on the run either (the nearest point clamps to the endpoint).
  CHECK_FALSE(CadPipeRunUnderPick(st, ray3d::Vec3{40.0, 0.0, 0.0}, &idx));

  // With two runs overlapping the pick, the nearer centreline wins.
  CadPipeRun second;
  second.vertsXyz = {0.0, 0.1, 0.0, 10.0, 0.1, 0.0};
  second.nominalSize = "4in";
  st.cadPipeRuns.push_back(second);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});
  CHECK(CadPipeRunUnderPick(st, ray3d::Vec3{5.0, 0.09, 0.0}, &idx));
  CHECK(idx == 1);
}

TEST_CASE("Arming a part that is not in the library is refused, not silently armed",
          "[issue486][req350][palette][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddFourInchRun(st);

  CHECK_FALSE(CadPipePaletteArmPart(st, EntryFor("NOT_A_REAL_PART"), log));
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK_FALSE(st.insertBlockPipeSpliceArmed);
  // An empty row name is refused the same way rather than arming an unnamed placement.
  CadBlockLibraryEntry nameless;
  nameless.isFitting = true;
  CHECK_FALSE(CadPipePaletteArmPart(st, nameless, log));
  CHECK(st.active == AppCommandState::Kind::None);
}

TEST_CASE("PIPERUN opens the fittings palette and finishing the run leaves it open",
          "[issue486][req350][palette][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  CHECK_FALSE(st.pipeFittingPaletteOpen);

  StartPipeRunCommand(st, log);
  CHECK(st.pipeFittingPaletteOpen);

  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // schedule-40 wall
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);
  // REQ-350 (a): the run is finished and the palette is still there, because a flange on the end just
  // routed is wanted NOW.
  CHECK(st.pipeFittingPaletteOpen);
  CHECK(st.active == AppCommandState::Kind::None);
}

// --- REQ-346: the compass locks a CLICKED segment to the UCS axes, elevation included ------------
//
// The compass resolves a full 3D point. Under a Front-style UCS its axes are world X and world Z, so
// a locked segment's whole displacement can live in Z — and `SubmitPipeRunViewportPick` used to drop
// the compass's resolved `wz` and re-read the raw cursor elevation instead, committing a vertex off
// the very ray the ghost had just drawn. The rubber preview and the typed-distance path both passed
// `&wz` already, so the preview looked locked and the click was not: the "preview must use the commit
// point" failure. These pin all three on the same answer.

TEST_CASE("The compass locks a clicked pipe segment to the UCS axes, elevation included",
          "[issue486][req346][piperun][compass][command]") {
  AppCommandState st;
  std::vector<std::string> log;

  // Front UCS: its X is world X, its Y is world Z. A segment locked to this frame's Y axis therefore
  // runs straight up in world Z.
  const ucs::Ucs front = ucs::OrthographicPresets()[2].frame;
  REQUIRE_FALSE(ucs::IsWorld(front));
  st.activeUcs = front;

  REQUIRE(st.pipeRunCompassOn);                 // on by default, as the prompt reports
  REQUIRE(st.polarIncrementDeg == 90.0);        // and locking to the axes is what 90 means

  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // schedule-40 wall

  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);

  // The second click carries a real ELEVATION, as an object-snap hit on a 3D feature does. That is
  // what makes this a regression pin rather than a tautology: the compass resolves the segment onto
  // this frame's X axis (world X, elevation 0), while the raw cursor elevation says 2. Committing the
  // raw one — the bug — yields a vertex 2 ft off the snapped ray, locked to nothing.
  st.viewportSnapPickValid = true;
  st.viewportSnapPickLocalZ = 2.f;
  REQUIRE(CadCommitElevation(st) == Catch::Approx(2.f));
  SubmitPipeRunViewportPick(st, 8.f, 0.f, log);
  st.viewportSnapPickValid = false;
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  REQUIRE(st.cadPipeRuns.size() == 1);
  const std::vector<double>& v = st.cadPipeRuns[0].vertsXyz;
  REQUIRE(v.size() == 6);

  const ray3d::Vec3 a{v[0], v[1], v[2]};
  const ray3d::Vec3 b{v[3], v[4], v[5]};
  const ray3d::Vec3 d = ray3d::Sub(b, a);
  REQUIRE(ray3d::Length(d) > 1e-6);

  // The segment must lie along ONE of the UCS's own axes — that is what "locked" means. Measured in
  // the UCS frame so the assertion says what the user sees, not what the world happens to call it.
  const double alongX = std::fabs(ray3d::Dot(d, front.xAxis));
  const double alongY = std::fabs(ray3d::Dot(d, front.yAxis));
  const double offPlane = std::fabs(ray3d::Dot(d, front.zAxis));
  const double len = ray3d::Length(d);
  CHECK(offPlane == Catch::Approx(0.0).margin(1e-6));            // never off the compass's own dial
  CHECK(std::max(alongX, alongY) == Catch::Approx(len).margin(1e-6));
  CHECK(std::min(alongX, alongY) == Catch::Approx(0.0).margin(1e-6));

  // And concretely: this one locks to the frame's X axis, which is world X at the anchor's own
  // elevation. Before the fix the committed vertex kept the raw cursor elevation (2 ft), so it came
  // out diagonal in world XZ — exactly "the segment will not lock to the UCS axes".
  CHECK(d.x == Catch::Approx(len).margin(1e-6));
  CHECK(d.z == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("A clicked pipe segment commits exactly where the compass preview put it",
          "[issue486][req346][piperun][compass][command]") {
  // The preview and the commit must agree by construction, so this asks the shared function what the
  // ghost would show and then checks the click landed there.
  AppCommandState st;
  std::vector<std::string> log;
  st.activeUcs = ucs::OrthographicPresets()[2].frame;  // Front

  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);

  st.viewportSnapPickValid = true;
  st.viewportSnapPickLocalZ = 2.f;
  const float pickX = 8.f;
  const float pickY = 0.f;
  float gx = pickX;
  float gy = pickY;
  float gz = static_cast<float>(CadCommitElevation(st));
  ApplyPipeRunCompassFromAnchor(st, 0.f, 0.f, &gx, &gy, /*compass=*/true, 0.f, gz, &gz);

  SubmitPipeRunViewportPick(st, pickX, pickY, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);
  const std::vector<double>& v = st.cadPipeRuns[0].vertsXyz;
  REQUIRE(v.size() == 6);
  CHECK(v[3] == Catch::Approx(static_cast<double>(gx)).margin(1e-6));
  CHECK(v[4] == Catch::Approx(static_cast<double>(gy)).margin(1e-6));
  CHECK(v[5] == Catch::Approx(static_cast<double>(gz)).margin(1e-6));
}

// --- D-2026-09-24-d: the route is real geometry while it is being drawn -------------------------

namespace {

/// PIPERUN taken to the point where clicks place vertices.
void StartRoutingFourInch(AppCommandState& st, std::vector<std::string>& log) {
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // schedule-40 wall
}

} // namespace

TEST_CASE("The pipe exists from the second click and keeps up with the route",
          "[issue486][req345][piperun][live][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartRoutingFourInch(st, log);

  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  // One point is not a pipe — nothing is drawn yet, and nothing is half-stored (REQ-201).
  CHECK(st.cadPipeRuns.empty());
  CHECK(st.pipeRunLiveIndex == -1);

  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(st.cadPipeRuns.size() == 1);
  REQUIRE(st.pipeRunLiveIndex == 0);
  CHECK(st.cadPipeRunAttrs.size() == 1);
  CHECK(st.cadPipeRuns[0].vertsXyz.size() == 6);          // two vertices
  CHECK(st.cadPipeRuns[0].nominalSize == "4in");
  CHECK(st.cadPipeRuns[0].wallThicknessIn > 0.0);         // the wall answered at the prompt
  CHECK(st.active == AppCommandState::Kind::PipeRun);     // and the command is still routing

  // A third point EXTENDS the same entity rather than adding a second one.
  SubmitPipeRunViewportPick(st, 10.f, 10.f, log);
  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.pipeRunLiveIndex == 0);
  CHECK(st.cadPipeRuns[0].vertsXyz.size() == 9);

  // U shortens the drawn pipe with the route.
  REQUIRE(HandlePipeRunTextInput("u", st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRuns[0].vertsXyz.size() == 6);
}

TEST_CASE("Cancelling a route takes its drawn pipe with it",
          "[issue486][req345][piperun][live][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartRoutingFourInch(st, log);
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(st.cadPipeRuns.size() == 1);

  CancelActiveCommand(st, log);
  CHECK(st.cadPipeRuns.empty());        // Esc means cancel, not "keep the half-routed pipe"
  CHECK(st.cadPipeRunAttrs.empty());
  CHECK(st.pipeRunLiveIndex == -1);
  CHECK(st.active == AppCommandState::Kind::None);
}

TEST_CASE("Finishing a route leaves exactly one run, not the provisional one as well",
          "[issue486][req345][piperun][live][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartRoutingFourInch(st, log);
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(st.cadPipeRuns.size() == 1);   // the provisional one

  REQUIRE(HandlePipeRunTextInput("end", st, log));
  // The provisional entity is retired and the finished route committed in its place — the drawing
  // must not end up with both.
  CHECK(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRunAttrs.size() == 1);
  CHECK(st.pipeRunLiveIndex == -1);
  CHECK(st.active == AppCommandState::Kind::None);
  CHECK(st.cadPipeRuns[0].vertsXyz.size() == 6);
}

TEST_CASE("Picking a fitting mid-route finishes the run instead of discarding it",
          "[issue486][req350][piperun][live][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddInlineFitting(st, "FLANGE4");
  StartRoutingFourInch(st, log);
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(st.cadPipeRuns.size() == 1);

  // The reported defect: this used to leave PIPERUN's route unbuilt, so the pipe vanished the
  // instant the palette took the command.
  REQUIRE(CadPipePaletteArmPart(st, EntryFor("FLANGE4"), log));
  CHECK(st.cadPipeRuns.size() == 1);                        // the pipe is still there...
  CHECK(st.cadPipeRuns[0].vertsXyz.size() == 6);
  CHECK(st.pipeRunLiveIndex == -1);                         // ...and it is a finished run now
  CHECK(st.active == AppCommandState::Kind::InsertBlock);   // with the part armed for placement

  // And it can be spliced into straight away, which is the point of the whole exercise.
  SubmitInsertBlockPick(st, 5.f, 0.f, 0.f, log);
  CHECK(st.cadPipeRuns.size() == 2);
  CHECK(st.cadBlockRefs.size() == 1);
}

TEST_CASE("A one-point route armed from the palette is dropped, not left half-open",
          "[issue486][req350][piperun][live][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  AddInlineFitting(st, "FLANGE4");
  StartRoutingFourInch(st, log);
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);  // a single point is not a run

  REQUIRE(CadPipePaletteArmPart(st, EntryFor("FLANGE4"), log));
  CHECK(st.cadPipeRuns.empty());
  CHECK(st.pipeRunLiveIndex == -1);
  CHECK(st.active == AppCommandState::Kind::InsertBlock);
}

// --- The routing preview must not grow with the route (perf regression pin) ---------------------
//
// Measured on the machine this was written on: a pipe run's swept tube costs ~5.2 ms at two points
// and ~11 ms more for every point after that (117 ms at twelve). The rubber preview rebuilt the
// WHOLE route's tube plus its edge tessellation every frame, so routing got slower with every click
// and eventually froze the application. Now that the clicked route is real geometry
// (D-2026-09-24-d), the ghost only has to show the PENDING segment.
//
// This pins the shape of that fix rather than a millisecond count — a timing assertion would be
// flaky and `project.md` keeps performance numbers on the reference machine with BENCH. If the ghost
// ever goes back to previewing the whole route, its output grows with the route and this fails.
TEST_CASE("The pipe run preview does not grow with the route",
          "[issue486][req345][piperun][live][preview]") {
  const auto ghostSizeFor = [](int clicks) {
    AppCommandState st;
    std::vector<std::string> log;
    st.pipeRunCompassOn = false;
    StartPipeRunCommand(st, log);
    REQUIRE(HandlePipeRunTextInput("4in", st, log));
    REQUIRE(HandlePipeRunTextInput("", st, log));
    for (int i = 0; i < clicks; ++i)
      SubmitPipeRunViewportPick(st, 20.f * static_cast<float>((i + 1) / 2),
                                20.f * static_cast<float>(i / 2), log);
    std::vector<float> rubber;
    AppendCadDraftRubberLines(st, 200.0, 200.0, /*orthoEnabled=*/false, 0.0, 0.0, 100.f, 1000, rubber);
    return rubber.size();
  };

  const std::size_t shortRoute = ghostSizeFor(2);
  const std::size_t longRoute = ghostSizeFor(10);
  CHECK(shortRoute > 0);                 // the pending segment IS previewed
  CHECK(longRoute == shortRoute);        // and only ever that one segment
}

// --- D-2026-09-24-e: the run being drawn is tessellated coarsely, the finished one is not --------
//
// A pipe tube's display tessellation is the single most expensive thing routing does — measured at
// ~1.6 s for a four-vertex run at the finished circular budget, paid again on EVERY click because
// each click rebuilds the provisional run. Drafting it at `brep::kDraftFullCircleSegments` brings a
// click to tens of milliseconds; finishing the command restores the full budget.
//
// This pins which budget is used when, not a millisecond count: timings belong on the reference
// machine with BENCH (project.md §7), but "the draft never gets promoted back" and "the finished run
// is left coarse" are both silent, permanent quality bugs that a test can catch.

TEST_CASE("The run being routed is tessellated in draft, the finished run at full quality",
          "[issue486][req345][piperun][live][tess]") {
  AppCommandState st;
  std::vector<std::string> log;
  st.pipeRunCompassOn = false;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 20.f, 0.f, log);
  REQUIRE(st.cadPipeRuns.size() == 1);
  REQUIRE(st.pipeRunLiveIndex == 0);

  const auto budgetOfOnlyPipeSolid = [&]() {
    RefreshSolidDisplayGeometry(st);
    REQUIRE(st.pipeRunWorldSolids.size() == 1);
    const CadSolidPtr& sp = st.pipeRunWorldSolids[0];
    for (const CadSolidTessellation& e : st.solidDisplayCache)
      if (e.key.lock() == sp)
        return e.fullCircleSegments;
    return -1;
  };

  // Mid-command: the geometry on screen is a draft and is allowed to be coarse.
  CHECK(budgetOfOnlyPipeSolid() == brep::kDraftFullCircleSegments);

  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.pipeRunLiveIndex == -1);
  CHECK(st.active == AppCommandState::Kind::None);

  // Finished: full quality, and nothing is left showing the draft.
  CHECK(budgetOfOnlyPipeSolid() == brep::kFullCircleSegments);
}

TEST_CASE("A pipe run drawn by another command is never drafted",
          "[issue486][req345][piperun][live][tess]") {
  // Only the run PIPERUN is currently drawing is provisional. An existing run in the drawing must
  // keep full quality even while a new one is being routed beside it — otherwise starting PIPERUN
  // would visibly coarsen everything already drawn.
  AppCommandState st;
  std::vector<std::string> log;
  st.pipeRunCompassOn = false;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 20.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
  REQUIRE(st.cadPipeRuns.size() == 1);

  // Now route a SECOND run while the first stands finished.
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("", st, log));  // keep 4in
  REQUIRE(HandlePipeRunTextInput("", st, log));  // schedule-40 wall
  SubmitPipeRunViewportPick(st, 0.f, 50.f, log);
  SubmitPipeRunViewportPick(st, 20.f, 50.f, log);
  REQUIRE(st.cadPipeRuns.size() == 2);
  REQUIRE(st.pipeRunLiveIndex == 1);

  RefreshSolidDisplayGeometry(st);
  REQUIRE(st.pipeRunWorldSolids.size() == 2);
  REQUIRE(st.pipeRunWorldSolidOwnerIndex.size() == 2);
  for (std::size_t i = 0; i < st.pipeRunWorldSolids.size(); ++i) {
    int budget = -1;
    for (const CadSolidTessellation& e : st.solidDisplayCache)
      if (e.key.lock() == st.pipeRunWorldSolids[i])
        budget = e.fullCircleSegments;
    const bool isTheDraft = st.pipeRunWorldSolidOwnerIndex[i] == st.pipeRunLiveIndex;
    INFO("solid " << i << " owner " << st.pipeRunWorldSolidOwnerIndex[i]);
    CHECK(budget == (isTheDraft ? brep::kDraftFullCircleSegments : brep::kFullCircleSegments));
  }
}
