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

TEST_CASE("A bent three-vertex run sweeps to two cylinders", "[issue486][piperun]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 10.0, 10.0, 0.0};
  run.nominalSize = "2in";
  std::vector<CadSolidPtr> solids;
  REQUIRE(CadBuildPipeRunSolids(run, &solids));
  REQUIRE(solids.size() == 2);
  for (const CadSolidPtr& sp : solids) {
    REQUIRE(sp != nullptr);
    CHECK(brep::Validate(*sp) == brep::Problem::Ok);
  }
}

TEST_CASE("A degenerate segment (coincident vertices) contributes no solid for that segment",
          "[issue486][piperun]") {
  CadPipeRun run;
  run.vertsXyz = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0};
  run.nominalSize = "3in";
  std::vector<CadSolidPtr> solids;
  REQUIRE(CadBuildPipeRunSolids(run, &solids));
  REQUIRE(solids.size() == 1);  // only the real segment sweeps
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
