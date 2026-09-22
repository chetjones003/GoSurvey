#include "CadBlocks.hpp"
#include "CadCommands.hpp"

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

TEST_CASE("PIPERUN accepts a known size and class, then advances to the first-point prompt",
          "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in CS150", st, log));
  CHECK(st.pipeRunNominalSize == "4in");
  CHECK(st.pipeRunPressureClassTag == "CS150");
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitFirstPoint);
}

TEST_CASE("A click-to-add PIPERUN commits a CadPipeRun on END", "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
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
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 5.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("", st, log));
  CHECK(st.active == AppCommandState::Kind::None);
  REQUIRE(st.cadPipeRuns.size() == 1);
}

TEST_CASE("PIPERUN END with only a start point refuses — a run needs two vertices",
          "[issue486][piperun][command]") {
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
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
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 1.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));

  StartPipeRunCommand(st, log);
  CHECK(st.pipeRunPhase == AppCommandState::PipeRunPhase::WaitNominalSize);
  REQUIRE(HandlePipeRunTextInput("", st, log));  // keep the remembered size
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

TEST_CASE("Existing axis-aligned PIPERUN picks are unaffected by the default-on compass",
          "[issue486][piperun][compass]") {
  // Pins that REQ-346 does not regress the original increment-B2 tests: picks already exactly on a
  // preset ray commit at the exact picked coordinates, not a slightly-perturbed snap result.
  AppCommandState st;
  std::vector<std::string> log;
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
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
