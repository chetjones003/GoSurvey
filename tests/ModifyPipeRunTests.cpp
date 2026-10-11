// REQ-351 (D-2026-09-28-f, GitHub issue #564 section 4) — the Modify commands move pipe runs, and
// COPY / MIRROR carry a solid's and a run's attributes to the copy.
//
// A pipe run owns only its path; the swept pipe is re-derived from it. So every case here asserts on
// the PATH and on what must NOT change with it — the nominal size and the wall — because a transform
// that resized the pipe would still move the path correctly. The headless transcripts cannot build a
// pipe run, which is why these live in Catch2 (the solid-side transcript is `req351-modify-solids`).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "util/brep.hpp"

using Catch::Approx;

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

/// A two-leg 4in run (0,0,0) -> (10,0,0) -> (10,10,0) on layer PIPE-STEEL, selected.
AppCommandState WithSelectedRun() {
  AppCommandState st;
  CadPipeRun run;
  run.name = "L-101";
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 10.0, 10.0, 0.0};
  run.nominalSize = "4in";
  run.wallThicknessIn = 0.237;
  st.cadPipeRuns.push_back(run);
  EntityAttributes a{};
  a.layer = "PIPE-STEEL";
  st.cadPipeRunAttrs.push_back(a);
  SelectedEntity e;
  e.type = SelectedEntity::Type::PipeRun;
  e.index = 0;
  st.selection.push_back(e);
  return st;
}

void RequireVert(const CadPipeRun& r, size_t i, double x, double y, double z) {
  REQUIRE(r.vertsXyz.size() > i * 3 + 2);
  // 1e-5: the rotate commands carry their angle as a float (~1e-7 relative).
  CHECK(r.vertsXyz[i * 3 + 0] == Approx(x).margin(1e-5));
  CHECK(r.vertsXyz[i * 3 + 1] == Approx(y).margin(1e-5));
  CHECK(r.vertsXyz[i * 3 + 2] == Approx(z).margin(1e-5));
}

bool LogHas(const std::vector<std::string>& log, const std::string& needle) {
  for (const std::string& line : log)
    if (line.find(needle) != std::string::npos)
      return true;
  return false;
}

}  // namespace

TEST_CASE("MOVE moves a pipe run's path in 3D and undo puts it back (REQ-351)", "[req351][piperun]") {
  AppCommandState st = WithSelectedRun();
  std::vector<std::string> log;
  Submit(st, "MOVE", log);
  Submit(st, "0,0,0", log);
  Submit(st, "5,3,2", log);
  REQUIRE(st.cadPipeRuns.size() == 1);
  RequireVert(st.cadPipeRuns[0], 0, 5, 3, 2);
  RequireVert(st.cadPipeRuns[0], 2, 15, 13, 2);
  CHECK(st.cadPipeRuns[0].nominalSize == "4in");

  REQUIRE(DoUndo(st, log));
  RequireVert(st.cadPipeRuns[0], 0, 0, 0, 0);
  RequireVert(st.cadPipeRuns[0], 2, 10, 10, 0);
}

TEST_CASE("ROTATE turns a pipe run's path about the base point (REQ-351)", "[req351][piperun]") {
  AppCommandState st = WithSelectedRun();
  std::vector<std::string> log;
  Submit(st, "ROTATE", log);
  Submit(st, "0,0", log);
  Submit(st, "90", log);
  // Typed ROTATE angles are clockwise-positive (the survey convention every entity type follows),
  // so 90 takes +X to -Y — the same way a line or a solid beside the run turns.
  RequireVert(st.cadPipeRuns[0], 1, 0, -10, 0);
  RequireVert(st.cadPipeRuns[0], 2, 10, -10, 0);
}

TEST_CASE("SCALE scales a pipe run's route but keeps its size and wall (REQ-351, D-2026-09-28-f)",
          "[req351][piperun]") {
  AppCommandState st = WithSelectedRun();
  st.cadPipeRuns[0].vertsXyz[8] = 4.0;  // a rise on the last leg, so the Z scale shows
  std::vector<std::string> log;
  Submit(st, "SCALE", log);
  Submit(st, "0,0", log);
  Submit(st, "2", log);
  RequireVert(st.cadPipeRuns[0], 1, 20, 0, 0);
  RequireVert(st.cadPipeRuns[0], 2, 20, 20, 8);
  CHECK(st.cadPipeRuns[0].nominalSize == "4in");
  CHECK(st.cadPipeRuns[0].wallThicknessIn == 0.237);
}

TEST_CASE("MIRROR adds a mirrored copy of a pipe run with its attributes (REQ-351)", "[req351][piperun]") {
  AppCommandState st = WithSelectedRun();
  CadPipingSystem sys;
  sys.name = "Cooling";
  sys.pipeRunIndices = {0};
  st.cadPipingSystems.push_back(sys);
  std::vector<std::string> log;
  Submit(st, "MIRROR", log);
  Submit(st, "-5,-50", log);
  Submit(st, "-5,50", log);
  Submit(st, "", log);  // keep the source
  REQUIRE(st.cadPipeRuns.size() == 2);
  RequireVert(st.cadPipeRuns[0], 2, 10, 10, 0);  // the original is untouched
  RequireVert(st.cadPipeRuns[1], 0, -10, 0, 0);
  RequireVert(st.cadPipeRuns[1], 2, -20, 10, 0);
  CHECK(st.cadPipeRuns[1].nominalSize == "4in");
  REQUIRE(st.cadPipeRunAttrs.size() == 2);
  CHECK(st.cadPipeRunAttrs[1].layer == "PIPE-STEEL");
  // The copy is a new run: it joins no network (D-2026-09-28-f).
  CHECK(st.cadPipingSystems[0].pipeRunIndices == std::vector<int>{0});

  REQUIRE(DoUndo(st, log));
  CHECK(st.cadPipeRuns.size() == 1);
}

TEST_CASE("COPY duplicates a pipe run and a solid together, attributes carried (REQ-351)",
          "[req351][piperun]") {
  AppCommandState st = WithSelectedRun();
  brep::Solid box;
  brep::Problem why{};
  REQUIRE(brep::MakeBox(ucs::Ucs{}, 4.0, 4.0, 4.0, &box, &why));
  st.cadSolids.push_back(std::make_shared<const brep::Solid>(box));
  EntityAttributes sa{};
  sa.layer = "EQUIP";
  st.cadSolidAttrs.push_back(sa);
  SelectedEntity e;
  e.type = SelectedEntity::Type::Solid;
  e.index = 0;
  st.selection.push_back(e);

  std::vector<std::string> log;
  Submit(st, "COPY", log);
  Submit(st, "0,0", log);
  Submit(st, "0,50", log);
  REQUIRE(st.cadPipeRuns.size() == 2);
  REQUIRE(st.cadSolids.size() == 2);
  RequireVert(st.cadPipeRuns[1], 2, 10, 60, 0);
  CHECK(st.cadPipeRunAttrs[1].layer == "PIPE-STEEL");
  CHECK(st.cadSolidAttrs[1].layer == "EQUIP");
  CHECK(brep::ComputeMassProperties(*st.cadSolids[1]).volume == Approx(64.0));

  // One undo takes back the whole copy — both objects.
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadPipeRuns.size() == 1);
  CHECK(st.cadSolids.size() == 1);
}

TEST_CASE("Polar ARRAY arrays a pipe run (REQ-351)", "[req351][piperun]") {
  AppCommandState st = WithSelectedRun();
  std::vector<std::string> log;
  Submit(st, "ARRAY", log);
  Submit(st, "p", log);
  Submit(st, "0,0", log);
  Submit(st, "4", log);
  Submit(st, "360", log);
  Submit(st, "y", log);
  REQUIRE(st.cadPipeRuns.size() == 4);
  RequireVert(st.cadPipeRuns[1], 1, 0, 10, 0);  // the first copy, a quarter turn round
}

TEST_CASE("STRETCH refuses a pipe run by name and leaves it alone (REQ-351 / REQ-201)",
          "[req351][piperun][req201]") {
  AppCommandState st = WithSelectedRun();
  std::vector<std::string> log;
  ApplyStretchToSelection(st, 5.f, 0.f, 0.f, 5.f, 20.f, -5.f, 20.f, false, log);
  CHECK(LogHas(log, "STRETCH — 1 pipe run(s) excluded"));
  RequireVert(st.cadPipeRuns[0], 1, 10, 0, 0);
}

TEST_CASE("MIRROR with erase-source leaves only the mirrored pipe run (REQ-351)", "[req351][piperun]") {
  AppCommandState st = WithSelectedRun();
  std::vector<std::string> log;
  Submit(st, "MIRROR", log);
  Submit(st, "-5,-50", log);
  Submit(st, "-5,50", log);
  Submit(st, "y", log);
  REQUIRE(st.cadPipeRuns.size() == 1);
  REQUIRE(st.cadPipeRunAttrs.size() == 1);
  RequireVert(st.cadPipeRuns[0], 2, -20, 10, 0);
}

TEST_CASE("MIRROR with erase-source removes a mirrored fitting's original too (REQ-351)",
          "[req351][piperun]") {
  // A fitting is a block reference. The flat MIRROR always copied one; its erase-source never
  // removed the original, so a run and its fitting mirrored "in place" left the fitting behind.
  AppCommandState st = WithSelectedRun();
  CadBlockRef fitting;
  fitting.defName = "ELBOW-4IN";
  fitting.xf.x = 10.f;
  st.cadBlockRefs.push_back(fitting);
  st.cadBlockRefAttrs.push_back(EntityAttributes{});
  SelectedEntity e;
  e.type = SelectedEntity::Type::BlockRef;
  e.index = 0;
  st.selection.push_back(e);
  std::vector<std::string> log;
  Submit(st, "MIRROR", log);
  Submit(st, "-5,-50", log);
  Submit(st, "-5,50", log);
  Submit(st, "y", log);
  REQUIRE(st.cadPipeRuns.size() == 1);
  REQUIRE(st.cadBlockRefs.size() == 1);
  CHECK(st.cadBlockRefAttrs.size() == 1);
  CHECK(st.cadBlockRefs[0].xf.x == Approx(-20.0));
}
