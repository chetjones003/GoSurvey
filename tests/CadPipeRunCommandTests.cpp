#include "CadBlocks.hpp"
#include "CadCommands.hpp"

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
