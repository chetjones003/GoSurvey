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

#include <cmath>
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
  // The distances line is now the SECOND-from-last: the grade rides on its own line after it
  // (REQ-105 as amended, D-2026-09-09-f).
  REQUIRE(log.size() >= 2);
  const std::string& result = log[log.size() - 2];
  INFO("DIST result line: " << result);
  CHECK(result.find("DIST") != std::string::npos);
  CHECK(result.find("3.0000") != std::string::npos);   // dX
  CHECK(result.find("4.0000") != std::string::npos);   // dY
  CHECK(result.find("5.8310") != std::string::npos);   // slope = sqrt(34) = 5.83095...
}

// --- REQ-105 as amended (D-2026-09-09-f, GitHub #149 acceptance 1) ------------------------------
//
// The four numbers a surveyor reads together. The first two were already covered above; these pin
// the two that were missing, and the two degenerate cases that must be REFUSALS rather than
// divisions by zero (REQ-201).

TEST_CASE("DIST reports horizontal distance and grade alongside the slope distance",
          "[commands][dist][req105]") {
  AppCommandState st;
  std::vector<std::string> log;

  Submit(st, "dist", log);
  st.resolvedPointZValid = true;
  st.resolvedPointZ = 5.0f;
  Submit(st, "0,0", log);
  st.resolvedPointZ = 8.0f;
  Submit(st, "3,4", log);

  // run = hypot(3,4) = 5 exactly; rise = 3. So grade = 3/5 = 60.00%, and run:rise = 5/3 = 1.67:1.
  // Hand-computed, and both are exact in decimal at this precision -- the point of choosing a 3-4-5.
  REQUIRE_FALSE(log.empty());
  const std::string& grade = log.back();
  INFO("DIST grade line: " << grade);
  CHECK(grade.find("grade 60.00%") != std::string::npos);
  CHECK(grade.find("slope 1.67:1") != std::string::npos);
  CHECK(grade.find("horiz 5.0000") != std::string::npos);
  CHECK(grade.find("vert 3.0000") != std::string::npos);

  // The wording is REQ-074's, verbatim, so SURFELEV and DIST cannot describe one slope two ways.
  // If either command's format string is reworded, this is the assertion that should stop it.
  CHECK(grade.find("grade ") != std::string::npos);
  CHECK(grade.find(":1") != std::string::npos);
}

TEST_CASE("DIST reports a level pair as level rather than stating a run:rise ratio",
          "[commands][dist][req105]") {
  AppCommandState st;
  std::vector<std::string> log;

  Submit(st, "dist", log);
  st.resolvedPointZValid = true;
  st.resolvedPointZ = 12.5f;
  Submit(st, "0,0", log);
  Submit(st, "30,40", log);  // same Z: rise is exactly 0

  REQUIRE_FALSE(log.empty());
  const std::string& grade = log.back();
  INFO("DIST grade line: " << grade);
  CHECK(grade.find("level (0.00%)") != std::string::npos);
  CHECK(grade.find("horiz 50.0000") != std::string::npos);
  // A ratio here would be a division by a zero rise. It must be absent, not infinite or "inf".
  CHECK(grade.find(":1") == std::string::npos);
  CHECK(grade.find("inf") == std::string::npos);
}

TEST_CASE("DIST reports a vertical pair as vertical rather than dividing by a zero run",
          "[commands][dist][req105]") {
  AppCommandState st;
  std::vector<std::string> log;

  Submit(st, "dist", log);
  st.resolvedPointZValid = true;
  st.resolvedPointZ = 100.0f;
  Submit(st, "7,7", log);
  st.resolvedPointZ = 118.0f;
  Submit(st, "7,7", log);  // same plan position, 18 ft higher

  // This is NOT the zero-length refusal: the pair has a real 18 ft slope distance, so the command
  // completes and reports it. What it cannot report is a grade.
  REQUIRE(st.active == AppCommandState::Kind::None);
  REQUIRE(log.size() >= 2);
  const std::string& dists = log[log.size() - 2];
  const std::string& grade = log.back();
  INFO("DIST lines: " << dists << " | " << grade);
  CHECK(dists.find("18.0000") != std::string::npos);  // slope distance is real and reported
  CHECK(grade.find("vertical") != std::string::npos);
  CHECK(grade.find("No grade") != std::string::npos);
  CHECK(grade.find(":1") == std::string::npos);
  CHECK(grade.find("inf") == std::string::npos);
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

// --- GitHub #149 acceptance 8: "Measurements remain accurate at survey coordinate magnitudes" ---
//
// The criterion covers every measurement in the phase, and the rest of it was already pinned —
// `BrepTests` builds and measures solids at E 2,196,000, and `SectionClipTests` places the clip
// plane there. DIST, which is acceptance 1's own command, had **no** survey-magnitude case at all:
// before these, no coordinate anywhere in this file exceeded about 83,000.
//
// That mattered more than a missing test usually does, because `AppCommandState::distFromX/Y/Z` are
// `float`. A float's resolution at 2.2e6 is about 0.25 ft — 125x REQ-101's +/-0.002 — so if those
// fields ever held a survey coordinate the command would be quietly, badly wrong.
//
// They do not, and the reason is `MaybeEstablishDocumentOriginFromTypedPoint`: the first typed point
// in a fresh drawing establishes the document origin, and everything after it is stored LOCAL to
// that origin (`local = world - origin`). The stored floats therefore hold small numbers however far
// out the survey is.
//
// So these cases assert the OUTCOME and the MECHANISM together. The outcome alone would keep passing
// if origin-at-entry were removed and the fields widened to double instead — fine, but a different
// design — and would go silently wrong if origin-at-entry were removed and the fields left as they
// are. Asserting both is what makes this a test of the invariant rather than of one arithmetic path.

TEST_CASE("DIST is exact at survey coordinate magnitudes", "[commands][dist][req105][req101][req149]") {
  AppCommandState st;
  std::vector<std::string> log;

  REQUIRE(st.worldDocumentOriginX == Approx(0.0));

  Submit(st, "dist", log);
  st.resolvedPointZValid = true;
  st.resolvedPointZ = 5.0f;
  Submit(st, "2196000,1400000", log);   // a real Kentucky North state-plane easting/northing
  st.resolvedPointZ = 8.0f;
  Submit(st, "2196003,1400004", log);

  // The same 3-4-5 as the origin case above, carried out to state-plane coordinates: dX 3, dY 4,
  // dZ 3, slope sqrt(34) = 5.83095...
  REQUIRE(log.size() >= 2);
  const std::string& result = log[log.size() - 2];
  INFO("DIST result line: " << result);
  CHECK(result.find("3.0000") != std::string::npos);
  CHECK(result.find("4.0000") != std::string::npos);
  CHECK(result.find("5.8310") != std::string::npos);

  // ...and the mechanism that makes it exact. The origin absorbed the magnitude on the first typed
  // point, so what the `float` fields hold is small.
  CHECK(st.worldDocumentOriginX != Approx(0.0));
  CHECK(std::fabs(static_cast<double>(st.distFromX)) < 1000.0);
  CHECK(std::fabs(static_cast<double>(st.distFromY)) < 1000.0);
}

TEST_CASE("DIST resolves a hundredth of a foot at survey magnitudes",
          "[commands][dist][req105][req101][req149]") {
  // REQ-101 is +/-0.002 ft, so 0.010 ft is the smallest round figure it must report faithfully —
  // and it is five times the tolerance, which makes a failure unambiguous rather than marginal.
  // Stored as a raw float at 2.2e6 this delta vanishes entirely: both endpoints quantize to the
  // same representable value and the command would report 0.0000.
  AppCommandState st;
  std::vector<std::string> log;

  Submit(st, "dist", log);
  st.resolvedPointZValid = true;
  st.resolvedPointZ = 0.0f;
  Submit(st, "2196000.000,1400000.000", log);
  st.resolvedPointZ = 0.0f;
  Submit(st, "2196000.010,1400000.000", log);

  REQUIRE(log.size() >= 2);
  const std::string& result = log[log.size() - 2];
  INFO("DIST result line: " << result);
  CHECK(result.find("dX = 0.0100") != std::string::npos);
  CHECK(result.find("dY = 0.0000") != std::string::npos);
  CHECK(result.find("slope dist = 0.0100") != std::string::npos);
}

TEST_CASE("DIST's horizontal distance and grade hold at survey magnitudes",
          "[commands][dist][req105][req101][req149]") {
  // Acceptance 1's other two numbers, at acceptance 8's magnitudes. Grade is a RATIO, so a
  // quantization that left the deltas merely close would show here as a wrong percentage rather
  // than as a small absolute error — which is the more visible failure to a surveyor reading it.
  AppCommandState st;
  std::vector<std::string> log;

  Submit(st, "dist", log);
  st.resolvedPointZValid = true;
  st.resolvedPointZ = 5.0f;
  Submit(st, "2196000,1400000", log);
  st.resolvedPointZ = 8.0f;
  Submit(st, "2196003,1400004", log);

  REQUIRE_FALSE(log.empty());
  const std::string& grade = log.back();
  INFO("DIST grade line: " << grade);
  CHECK(grade.find("horiz 5.0000") != std::string::npos);
  CHECK(grade.find("vert 3.0000") != std::string::npos);
  CHECK(grade.find("grade 60.00%") != std::string::npos);
  CHECK(grade.find("slope 1.67:1") != std::string::npos);
}

TEST_CASE("DIST stays exact after work near the origin moves out to a survey magnitude",
          "[commands][dist][req105][req101][req149]") {
  // The case that decides whether the `float` fields are ever exposed: a drawing that started at
  // small coordinates — so the origin is already established at zero — into which a survey-magnitude
  // pair is then measured. If the origin stayed put, the stored locals WOULD be the survey
  // coordinates, a float would quantize them to about 0.25 ft, and a hundredth-of-a-foot delta
  // would report as zero.
  //
  // It does not stay put: the origin re-establishes when a typed point lands far from it, so the
  // locals are small again and the deltas survive. Measured here rather than assumed, with a
  // FRACTIONAL delta — 2,196,000 and 2,196,003 are both under 2^24 and therefore exact as floats
  // even unabsorbed, so an integer delta would pass whatever the origin did and prove nothing.
  AppCommandState st;
  std::vector<std::string> log;

  Submit(st, "dist", log);
  st.resolvedPointZValid = true;
  st.resolvedPointZ = 0.0f;
  Submit(st, "0,0", log);
  Submit(st, "10,0", log);
  REQUIRE(st.worldDocumentOriginX == Approx(0.0));  // small work leaves the origin where it was

  Submit(st, "dist", log);
  st.resolvedPointZValid = true;
  st.resolvedPointZ = 0.0f;
  Submit(st, "2196000.000,1400000.000", log);
  st.resolvedPointZValid = true;
  st.resolvedPointZ = 0.0f;
  Submit(st, "2196000.010,1400000.000", log);

  REQUIRE(log.size() >= 2);
  const std::string& result = log[log.size() - 2];
  INFO("DIST result line: " << result);
  CHECK(result.find("dX = 0.0100") != std::string::npos);

  // The mechanism: the origin followed the work, so the float never held a survey coordinate.
  CHECK(st.worldDocumentOriginX != Approx(0.0));
  CHECK(std::fabs(static_cast<double>(st.distFromX)) < 1000.0);
}
