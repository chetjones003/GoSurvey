#include "CadBlocks.hpp"
#include "CadCommands.hpp"

#include <catch2/catch_test_macros.hpp>

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
