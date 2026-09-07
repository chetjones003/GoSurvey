// REQ-105 (GitHub issue #382) — DIST: two-point 3D distance (delta X/Y/Z + slope distance).
//
// Exercised through the command-line typed-entry path (ProcessCommandLineSubmit), the same
// entry point INVERSE and ID are driven through elsewhere in the app. Z is varied between the
// two points via AppCommandState::resolvedPointZ (what CadCommitElevation reads), so the slope
// distance actually differs from the horizontal distance — the point of this command over
// INVERSE, which only ever reports 2D.
//
// Linked into GoSurveySnapTests: needs gosurvey_domain for ProcessCommandLineSubmit / CadCommands.cpp.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"

using Catch::Approx;

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

} // namespace

TEST_CASE("DIST reports dX/dY/dZ and slope distance between two typed points", "[commands][dist][req105]") {
  AppCommandState st;
  std::vector<std::string> log;

  Submit(st, "dist", log);
  REQUIRE(st.active == AppCommandState::Kind::Dist);
  REQUIRE(st.distPhase == AppCommandState::DistPhase::WaitFrom);

  st.resolvedPointZValid = true;
  st.resolvedPointZ = 5.0f;
  Submit(st, "0,0", log);
  REQUIRE(st.distPhase == AppCommandState::DistPhase::WaitTo);
  REQUIRE(st.distFromX == Approx(0.0));
  REQUIRE(st.distFromY == Approx(0.0));
  REQUIRE(st.distFromZ == Approx(5.0));

  st.resolvedPointZ = 8.0f;
  Submit(st, "3,4", log);

  // Command completes and returns to idle.
  REQUIRE(st.active == AppCommandState::Kind::None);
  REQUIRE(st.distPhase == AppCommandState::DistPhase::WaitFrom);

  // dX=3, dY=4, dZ=3 -> slope = sqrt(9+16+9) = sqrt(34).
  REQUIRE_FALSE(log.empty());
  const std::string& result = log.back();
  INFO("DIST result line: " << result);
  CHECK(result.find("DIST") != std::string::npos);
  CHECK(result.find("3.0000") != std::string::npos);   // dX
  CHECK(result.find("4.0000") != std::string::npos);   // dY
  CHECK(result.find("5.8310") != std::string::npos);   // slope = sqrt(34) = 5.83095...
}

TEST_CASE("DIST refuses a zero-length pick and stays ready for a different second point", "[commands][dist][req105]") {
  AppCommandState st;
  std::vector<std::string> log;

  Submit(st, "dist", log);
  Submit(st, "1,1", log);
  REQUIRE(st.distPhase == AppCommandState::DistPhase::WaitTo);

  Submit(st, "1,1", log);
  // Coincident points: DIST must refuse rather than report a zero/garbage slope, and must not
  // silently complete the command (matching INVERSE's zero-distance refusal).
  REQUIRE(st.active == AppCommandState::Kind::Dist);
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("zero") != std::string::npos);
}

TEST_CASE("Starting DIST fresh always resets its draft (matches ESC + restart)", "[commands][dist][req105]") {
  AppCommandState st;
  std::vector<std::string> log;

  Submit(st, "dist", log);
  Submit(st, "1,1", log);
  REQUIRE(st.distPhase == AppCommandState::DistPhase::WaitTo);

  // Simulate ESC (cancel back to idle) then restarting DIST: the half-collected first point must
  // not leak into the new run.
  st.active = AppCommandState::Kind::None;
  StartDistCommand(st, log);
  REQUIRE(st.active == AppCommandState::Kind::Dist);
  REQUIRE(st.distPhase == AppCommandState::DistPhase::WaitFrom);
  REQUIRE(st.distFromX == Approx(0.0));
  REQUIRE(st.distFromY == Approx(0.0));
  REQUIRE(st.distFromZ == Approx(0.0));
}
