#include "util/cadpiperun.hpp"
#include "util/brep.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("NPS label parses to inches", "[issue486][piperun]") {
  double in = 0.0;
  REQUIRE(CadParsePipeNominalSizeInches("4in", &in));
  CHECK(in == Catch::Approx(4.0));
  REQUIRE(CadParsePipeNominalSizeInches("1.5in", &in));
  CHECK(in == Catch::Approx(1.5));
  REQUIRE(CadParsePipeNominalSizeInches("2.5 in", &in));
  CHECK(in == Catch::Approx(2.5));
  CHECK_FALSE(CadParsePipeNominalSizeInches("", &in));
  CHECK_FALSE(CadParsePipeNominalSizeInches("4ft", &in));
  CHECK_FALSE(CadParsePipeNominalSizeInches("in", &in));
  CHECK_FALSE(CadParsePipeNominalSizeInches("-4in", &in));
}

TEST_CASE("Known NPS sizes resolve to a feet outer diameter, unknown sizes refuse", "[issue486][piperun]") {
  double odFeet = 0.0;
  REQUIRE(CadPipeNominalOdFeet("4in", &odFeet));
  CHECK(odFeet == Catch::Approx(4.5 / 12.0));
  REQUIRE(CadPipeNominalOdFeet("0.5in", &odFeet));
  CHECK(odFeet == Catch::Approx(0.840 / 12.0));
  CHECK_FALSE(CadPipeNominalOdFeet("5in", &odFeet));   // not in the table
  CHECK_FALSE(CadPipeNominalOdFeet("bogus", &odFeet));
  CHECK_FALSE(CadPipeNominalOdFeet("", &odFeet));
}

TEST_CASE("A straight two-vertex run sweeps to one valid hollow pipe", "[issue486][piperun]") {
  // D-2026-09-23-a: the solid is a TUBE, not a rod. A run that states no wall is built at the
  // schedule-40 wall for its size, which is what makes a drawing saved before this open as pipe.
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  std::vector<CadSolidPtr> solids;
  REQUIRE(CadBuildPipeRunSolids(run, &solids));
  REQUIRE(solids.size() == 1);
  REQUIRE(solids[0] != nullptr);
  CHECK(brep::Validate(*solids[0]) == brep::Problem::Ok);
  const double outerRadius = (4.5 / 12.0) * 0.5;
  const double boreRadius = outerRadius - 0.237 / 12.0;  // schedule 40 for 4in
  const double expectedVolume =
      3.14159265358979 * (outerRadius * outerRadius - boreRadius * boreRadius) * 10.0;
  const brep::MassProperties mp = brep::ComputeMassProperties(*solids[0]);
  CHECK(mp.volume == Catch::Approx(expectedVolume).epsilon(0.01));
}

TEST_CASE("A run's stated wall thickness is what it is built at", "[issue486][piperun][wall]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  run.wallThicknessIn = 0.5;  // much heavier than schedule 40's 0.237in
  std::vector<CadSolidPtr> solids;
  REQUIRE(CadBuildPipeRunSolids(run, &solids));
  REQUIRE(solids.size() == 1);
  const double outerRadius = (4.5 / 12.0) * 0.5;
  const double boreRadius = outerRadius - 0.5 / 12.0;
  const double expectedVolume =
      3.14159265358979 * (outerRadius * outerRadius - boreRadius * boreRadius) * 10.0;
  CHECK(brep::ComputeMassProperties(*solids[0]).volume ==
        Catch::Approx(expectedVolume).epsilon(0.01));

  // A heavier wall is MORE metal: the same run at schedule 40 must weigh less.
  CadPipeRun standard = run;
  standard.wallThicknessIn = 0.0;
  std::vector<CadSolidPtr> stdSolids;
  REQUIRE(CadBuildPipeRunSolids(standard, &stdSolids));
  CHECK(brep::ComputeMassProperties(*stdSolids[0]).volume <
        brep::ComputeMassProperties(*solids[0]).volume);
}

TEST_CASE("Schedule-40 walls resolve per size, and refuse an unknown one",
          "[issue486][piperun][wall]") {
  double wallIn = 0.0;
  REQUIRE(CadPipeStandardWallThicknessInches("4in", &wallIn));
  CHECK(wallIn == Catch::Approx(0.237));
  REQUIRE(CadPipeStandardWallThicknessInches("0.5in", &wallIn));
  CHECK(wallIn == Catch::Approx(0.109));
  REQUIRE(CadPipeStandardWallThicknessInches("12in", &wallIn));
  CHECK(wallIn == Catch::Approx(0.406));
  CHECK_FALSE(CadPipeStandardWallThicknessInches("5in", &wallIn));
  CHECK_FALSE(CadPipeStandardWallThicknessInches("bogus", &wallIn));

  // The resolved wall a run is actually built at: its own, else the standard one.
  CadPipeRun run;
  run.nominalSize = "4in";
  double wallFeet = 0.0;
  REQUIRE(CadPipeRunWallThicknessFeet(run, &wallFeet));
  CHECK(wallFeet == Catch::Approx(0.237 / 12.0));
  run.wallThicknessIn = 0.25;
  REQUIRE(CadPipeRunWallThicknessFeet(run, &wallFeet));
  CHECK(wallFeet == Catch::Approx(0.25 / 12.0));
}

TEST_CASE("A wall that leaves no bore refuses the run rather than building a rod",
          "[issue486][piperun][wall]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";  // OD 4.5in, so anything from 2.25in is not a wall
  double wallFeet = 0.0;

  run.wallThicknessIn = 2.25;
  CHECK_FALSE(CadPipeRunWallThicknessFeet(run, &wallFeet));
  std::vector<CadSolidPtr> solids;
  CHECK_FALSE(CadBuildPipeRunSolids(run, &solids));
  CHECK(solids.empty());

  run.wallThicknessIn = 3.0;
  CHECK_FALSE(CadPipeRunWallThicknessFeet(run, &wallFeet));
  CHECK_FALSE(CadBuildPipeRunSolids(run, &solids));
  CHECK(solids.empty());

  // Only a POSITIVE value states a wall: 0 (every drawing written before pipes were hollow) and a
  // nonsense negative both take the standard one, rather than one of them refusing the run.
  run.wallThicknessIn = -0.1;
  CHECK(CadPipeRunWallThicknessFeet(run, &wallFeet));
  CHECK(wallFeet == Catch::Approx(0.237 / 12.0));
}

TEST_CASE("A run with an unresolvable nominal size builds no solids", "[issue486][piperun]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "9in";  // not in the table
  std::vector<CadSolidPtr> solids;
  CHECK_FALSE(CadBuildPipeRunSolids(run, &solids));
  CHECK(solids.empty());
}

TEST_CASE("A run with fewer than two vertices builds no solids", "[issue486][piperun]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0};
  run.nominalSize = "4in";
  std::vector<CadSolidPtr> solids;
  CHECK_FALSE(CadBuildPipeRunSolids(run, &solids));
  CHECK(solids.empty());
}

TEST_CASE("A run with a coincident-vertex segment refuses the whole run", "[issue486][piperun]") {
  // Increment B1 skipped a degenerate segment and kept building the rest; the auto-fillet sweep
  // (a single solid for the whole path, not one cylinder per segment) cannot do that — a
  // degenerate leg has no direction to march the bend geometry from, so the whole run refuses
  // (REQ-201) rather than silently building a partial/wrong pipe.
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "3in";
  std::vector<CadSolidPtr> solids;
  CHECK_FALSE(CadBuildPipeRunSolids(run, &solids));
  CHECK(solids.empty());
}

TEST_CASE("Fillet angle snaps to the nearest standard fitting angle", "[issue486][piperun][fillet]") {
  constexpr double kDeg = 3.14159265358979323846 / 180.0;
  CHECK(CadPipeSnapFilletAngleRad(89.0 * kDeg) == Catch::Approx(90.0 * kDeg));
  CHECK(CadPipeSnapFilletAngleRad(91.0 * kDeg) == Catch::Approx(90.0 * kDeg));
  CHECK(CadPipeSnapFilletAngleRad(40.0 * kDeg) == Catch::Approx(45.0 * kDeg));
  CHECK(CadPipeSnapFilletAngleRad(20.0 * kDeg) == Catch::Approx(22.5 * kDeg));
  CHECK(CadPipeSnapFilletAngleRad(10.0 * kDeg) == Catch::Approx(11.25 * kDeg));
  CHECK(CadPipeSnapFilletAngleRad(70.0 * kDeg) == Catch::Approx(60.0 * kDeg));
}

TEST_CASE("Fillet radius is 1.5x the nominal pipe size (long-radius elbow takeoff)",
          "[issue486][piperun][fillet]") {
  CHECK(CadPipeFilletRadiusFeet(4.0) == Catch::Approx(1.5 * 4.0 / 12.0));
  CHECK(CadPipeFilletRadiusFeet(2.0) == Catch::Approx(1.5 * 2.0 / 12.0));
}

TEST_CASE("A 90-degree bend sweeps to one valid, closed pipe solid", "[issue486][piperun][fillet]") {
  CadPipeRun run;
  // A true right-angle turn, legs long enough that the fillet fits comfortably.
  run.vertsXyz = {0.0, 0.0, 0.0, 20.0, 0.0, 0.0, 20.0, 20.0, 0.0};
  run.nominalSize = "4in";
  std::vector<CadSolidPtr> solids;
  REQUIRE(CadBuildPipeRunSolids(run, &solids));
  REQUIRE(solids.size() == 1);
  REQUIRE(solids[0] != nullptr);
  CHECK(brep::Validate(*solids[0]) == brep::Problem::Ok);
  const brep::MassProperties mp = brep::ComputeMassProperties(*solids[0]);
  CHECK(mp.volume > 0.0);
  // Roughly two 20 ft legs of 4in pipe, minus a small amount trimmed for the bend — well short of
  // twice that (which would mean the bend added length instead of replacing straight pipe with it).
  // The pipe is hollow (D-2026-09-23-a), so the comparison is against the WALL's volume: the same
  // two legs of rod would be five times this.
  const double outerRadius = (4.5 / 12.0) * 0.5;
  const double boreRadius = outerRadius - 0.237 / 12.0;  // schedule 40 for 4in
  const double twoStraightLegs =
      3.14159265358979 * (outerRadius * outerRadius - boreRadius * boreRadius) * 40.0;
  CHECK(mp.volume < twoStraightLegs);
  CHECK(mp.volume > twoStraightLegs * 0.9);
}

TEST_CASE("A tiny direction change well under 1 degree passes straight through, unfilleted",
          "[issue486][piperun][fillet]") {
  CadPipeRun run;
  // ~0.06 degree kink — collinear enough that no fitting belongs there.
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 20.0, 0.01, 0.0};
  run.nominalSize = "4in";
  std::vector<CadSolidPtr> solids;
  REQUIRE(CadBuildPipeRunSolids(run, &solids));
  REQUIRE(solids.size() == 1);
  CHECK(brep::Validate(*solids[0]) == brep::Problem::Ok);
}

TEST_CASE("A corner with legs shorter than the ideal takeoff still builds, clamped to a smaller "
          "fillet",
          "[issue486][piperun][fillet]") {
  CadPipeRun run;
  // A 90-degree turn on legs far shorter than a 4in pipe's 6in (0.5 ft) ideal long-radius takeoff —
  // the fillet radius is clamped down rather than the corner refused (CadBuildPipeRunSolids's own
  // doc comment: refusal is reserved for a corner with essentially no room at all).
  run.vertsXyz = {0.0, 0.0, 0.0, 0.05, 0.0, 0.0, 0.05, 0.05, 0.0};
  run.nominalSize = "4in";
  std::vector<CadSolidPtr> solids;
  REQUIRE(CadBuildPipeRunSolids(run, &solids));
  REQUIRE(solids.size() == 1);
  CHECK(brep::Validate(*solids[0]) == brep::Problem::Ok);
}

TEST_CASE("A corner with essentially no room at all refuses the whole run", "[issue486][piperun][fillet]") {
  CadPipeRun run;
  // Legs a few thousandths of a foot long — below the 1e-6 ft tangent-length floor even after
  // clamping to half the leg length.
  run.vertsXyz = {0.0, 0.0, 0.0, 1e-6, 0.0, 0.0, 1e-6, 1e-6, 0.0};
  run.nominalSize = "4in";
  std::vector<CadSolidPtr> solids;
  CHECK_FALSE(CadBuildPipeRunSolids(run, &solids));
  CHECK(solids.empty());
}

TEST_CASE("A multi-bend run (two right-angle turns) still builds one valid solid",
          "[issue486][piperun][fillet]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 20.0, 0.0, 0.0, 20.0, 20.0, 0.0, 20.0, 20.0, 20.0};
  run.nominalSize = "2in";
  std::vector<CadSolidPtr> solids;
  REQUIRE(CadBuildPipeRunSolids(run, &solids));
  REQUIRE(solids.size() == 1);
  CHECK(brep::Validate(*solids[0]) == brep::Problem::Ok);
}

TEST_CASE("A straight run's end ports sit at the clicked vertices with outward-facing normals",
          "[issue486][piperun][port]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  CadPipeRunEndPort start, end;
  REQUIRE(CadPipeRunEndPorts(run, &start, &end));
  CHECK(start.point.x == Catch::Approx(0.0));
  CHECK(start.point.y == Catch::Approx(0.0));
  CHECK(start.point.z == Catch::Approx(0.0));
  // Start normal points backward, away from the pipe (-X); end normal points forward (+X).
  CHECK(start.outwardNormal.x == Catch::Approx(-1.0));
  CHECK(end.point.x == Catch::Approx(10.0));
  CHECK(end.outwardNormal.x == Catch::Approx(1.0));
  // Both normals are unit length.
  CHECK(ray3d::Length(start.outwardNormal) == Catch::Approx(1.0));
  CHECK(ray3d::Length(end.outwardNormal) == Catch::Approx(1.0));
}

TEST_CASE("A bent run's end ports still read from the ACTUAL swept geometry, not the raw clicks",
          "[issue486][piperun][port]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 20.0, 0.0, 0.0, 20.0, 20.0, 0.0};
  run.nominalSize = "4in";
  CadPipeRunEndPort start, end;
  REQUIRE(CadPipeRunEndPorts(run, &start, &end));
  // The START end is untouched by the bend — exactly at the first clicked vertex, facing -X.
  CHECK(start.point.x == Catch::Approx(0.0));
  CHECK(start.point.y == Catch::Approx(0.0));
  CHECK(start.outwardNormal.x == Catch::Approx(-1.0));
  // The END end faces +Y (the pipe's final direction after the 90-degree bend).
  CHECK(end.outwardNormal.y == Catch::Approx(1.0).margin(1e-6));
  CHECK(end.outwardNormal.x == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("End ports refuse for the same reasons the swept solid itself would", "[issue486][piperun][port]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "9in";  // not in the NPS table
  CadPipeRunEndPort start, end;
  CHECK_FALSE(CadPipeRunEndPorts(run, &start, &end));
}
