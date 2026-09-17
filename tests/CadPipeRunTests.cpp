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

TEST_CASE("A straight two-vertex run sweeps to one valid cylinder", "[issue486][piperun]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "4in";
  std::vector<CadSolidPtr> solids;
  REQUIRE(CadBuildPipeRunSolids(run, &solids));
  REQUIRE(solids.size() == 1);
  REQUIRE(solids[0] != nullptr);
  CHECK(brep::Validate(*solids[0]) == brep::Problem::Ok);
  const double expectedRadius = (4.5 / 12.0) * 0.5;
  const double expectedVolume = 3.14159265358979 * expectedRadius * expectedRadius * 10.0;
  const brep::MassProperties mp = brep::ComputeMassProperties(*solids[0]);
  CHECK(mp.volume == Catch::Approx(expectedVolume).epsilon(0.01));
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
  const double pipeRadius = (4.5 / 12.0) * 0.5;
  const double twoStraightLegs = 3.14159265358979 * pipeRadius * pipeRadius * 40.0;
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
