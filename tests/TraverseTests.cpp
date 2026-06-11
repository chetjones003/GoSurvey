// Domain regression tests for the Traverse Editor extension (REQ-011, REQ-012,
// REQ-015, REQ-016, REQ-017). Pure compute only — no UI/GL dependency (ADR-002).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "traverse/TraverseCalc.hpp"
#include "traverse/TraverseLeastSquares.hpp"
#include "traverse/FbkImport.hpp"

#include <cmath>
#include <string>

using Catch::Approx;

namespace {

// A closed square loop: start (0,0), reference bearing North, four 100 ft legs
// each turning 90°, returning to the start station (id 0).  leg1Dist lets a test
// inject a distance error to create misclosure.
TraverseData MakeSquareLoop(double leg1Dist = 100.0) {
    TraverseData td;
    td.startStationId  = 0;
    td.startEasting    = 0.0;
    td.startNorthing   = 0.0;
    td.startElevation  = 0.0;
    td.startBearingDeg = 0.0;
    td.hasStartBearing = true;
    td.isClosedLoop    = true;

    auto addLeg = [&](int id, double haDeg, double dist) {
        TraverseLeg leg;
        leg.stationId    = id;
        leg.hasHorizAngle = true; leg.horizAngleDeg = haDeg;
        leg.hasHorizDist  = true; leg.horizDist     = dist;
        td.legs.push_back(leg);
    };
    addLeg(1, 90.0, leg1Dist);
    addLeg(2, 90.0, 100.0);
    addLeg(3, 90.0, 100.0);
    addLeg(0, 90.0, 100.0);   // closes back onto the start station.

    ComputeTraverse(td);
    return td;
}

} // namespace


// ---------------------------------------------------------------- REQ-011 --
TEST_CASE("ComputeStats: mean, sum, sample std dev", "[stats][REQ-011]") {
    const StatSummary s = ComputeStats({10.0, 12.0, 14.0});
    CHECK(s.n == 3);
    CHECK(s.sum == Approx(36.0));
    CHECK(s.mean == Approx(12.0));
    CHECK(s.stddev == Approx(2.0));   // sqrt((4+0+4)/2)

    const StatSummary one = ComputeStats({7.5});
    CHECK(one.n == 1);
    CHECK(one.mean == Approx(7.5));
    CHECK(one.stddev == Approx(0.0)); // undefined for n<2 -> reported 0.

    const StatSummary none = ComputeStats({});
    CHECK(none.n == 0);
    CHECK(none.stddev == Approx(0.0));
}

// ---------------------------------------------------------------- REQ-012 --
TEST_CASE("Complementary distance round-trips slope<->horizontal", "[dist][REQ-012]") {
    const double slope = 100.0;
    const double zenith = 60.0;            // 60° from vertical.
    const double horiz = TraverseReduceToHoriz(slope, zenith, /*isZenith=*/true);
    CHECK(horiz == Approx(100.0 * std::sin(60.0 * 3.14159265358979323846 / 180.0)));

    const double back = TraverseSlopeFromHoriz(horiz, zenith, /*isZenith=*/true);
    CHECK(back == Approx(slope).margin(1e-9));

    // Degenerate near-horizontal zenith (~0° from vertical => sin~0 path is fine,
    // but a 90° "horizontal sight" in elevation convention divides by cos(90)=0).
    CHECK(TraverseSlopeFromHoriz(50.0, 90.0, /*isZenith=*/false) == Approx(0.0));
}

// ----------------------------------------------------------- REQ-015/016 --
TEST_CASE("Least squares: perfect loop yields zero residuals", "[lsa][REQ-016]") {
    TraverseData td = MakeSquareLoop();        // no error.
    LsaWeights w;                              // spec defaults.
    LsaResult r = ComputeTraverseLeastSquares(td, w);

    REQUIRE(r.ok);
    CHECK(r.observations == 8);
    CHECK(r.unknowns == 6);
    CHECK(r.redundancy == 2);
    CHECK(r.adjClosureLinear == Approx(0.0).margin(1e-6));
    for (const LsaResidual& v : r.residuals) {
        CHECK(v.distResidualFt == Approx(0.0).margin(1e-6));
        CHECK(v.angleResidualSec == Approx(0.0).margin(1e-3));
    }
}

TEST_CASE("Least squares: adjustment drives misclosure to zero", "[lsa][REQ-015]") {
    TraverseData td = MakeSquareLoop(100.10); // 0.10 ft long on leg 1.

    // Sanity: the unadjusted loop genuinely fails to close.
    REQUIRE(td.closureValid);
    CHECK(td.closureLinear > 0.05);

    LsaWeights w;
    LsaResult r = ComputeTraverseLeastSquares(td, w);
    REQUIRE(r.ok);
    CHECK(r.redundancy == 2);

    // REQ-015: after adjustment the loop closes.
    CHECK(r.adjClosureLinear < 1e-3);
    CHECK(r.refStdDev > 0.0);
    CHECK(std::isfinite(r.refStdDev));

    // Every residual is finite, and the adjustment shortens the long leg.
    REQUIRE(r.residuals.size() == 4);
    for (const LsaResidual& v : r.residuals) {
        CHECK(std::isfinite(v.distResidualFt));
        CHECK(std::isfinite(v.angleResidualSec));
    }
    CHECK(r.residuals[0].distResidualFt < 0.0);   // adjusted = observed + v < observed.
}

// ---------------------------------------------------------------- REQ-017 --
TEST_CASE("Least squares: insufficient/invalid input is surfaced", "[lsa][REQ-017]") {
    SECTION("open traverse is rejected") {
        TraverseData td = MakeSquareLoop();
        td.isClosedLoop = false;
        LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.message.empty());
    }
    SECTION("too few legs is rejected") {
        TraverseData td;
        td.isClosedLoop = true;
        td.hasStartBearing = true;
        TraverseLeg leg; leg.computed = true;
        td.legs.push_back(leg);
        LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.message.empty());
    }
}

// ----------------------------------------------------- closed-loop FBK import --
// Regression: a real Leica loop closes on the start monument re-observed under
// a ".1" suffix (KCP2 -> KCP2.1). Import must auto-detect closure and the
// adjustment must run (REQ-014/015 with real data).
TEST_CASE("FBK import detects a re-observed closed loop", "[fbk][REQ-015]") {
    const std::string path = std::string(GOSURVEY_TEST_DATA_DIR) + "/closed_loop_trav.fbk";
    TraverseData td;
    std::string err;
    REQUIRE(FbkImport(path.c_str(), td, err));
    CHECK(err.empty());

    // KCP2 -> KCP3 -> KCP1.1 -> KCP2.1(=KCP2).
    REQUIRE(td.legs.size() == 3);
    CHECK(td.legs.back().description == "KCP2.1");
    CHECK(td.isClosedLoop);             // closure auto-detected from the suffix.
    CHECK(td.hasStartBearing);

    ComputeTraverse(td);
    LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);
    CHECK(r.redundancy == 2);           // simple loop fixed at the start.
    CHECK(r.adjClosureLinear < 1e-3);   // adjustment closes the loop.
    CHECK(std::isfinite(r.refStdDev));

    // Leg 1 (KCP3) has the F2 face disabled in the field: F1 only.
    REQUIRE(td.legs[0].hasFace1);
    CHECK_FALSE(td.legs[0].hasFace2);

    // Enabling Face 1/Face 2 mode must not break the single-face leg: it should
    // still compute (not error) and agree with the single-observation result.
    const double bearingSingle = td.legs[0].computedBearingDeg;
    td.useFace1Face2 = true;
    ComputeTraverse(td);
    CHECK(td.legs[0].computed);
    CHECK(td.legs[0].hasSufficientData);
    CHECK(td.legs[0].errorMsg.empty());
    CHECK(td.legs[0].computedBearingDeg == Approx(bearingSingle).margin(1e-6));
}

// Engine-level guard for the single-face case (independent of FBK parsing).
TEST_CASE("Face averaging tolerates a missing face", "[engine][REQ-011]") {
    TraverseData td;
    td.startStationId = 1;
    td.startBearingDeg = 0.0;
    td.hasStartBearing = true;
    td.useFace1Face2 = true;

    TraverseLeg leg;
    leg.stationId = 2;
    leg.hasFace1 = true;  leg.face1HorizDeg = 90.0; leg.face1VertDeg = 90.0; // zenith
    leg.hasFace2 = false;                                                    // F2 disabled
    leg.isZenithAngle = true;
    leg.slopeDist = 100.0; leg.hasSlopeDist = true;
    td.legs.push_back(leg);

    ComputeTraverse(td);
    REQUIRE(td.legs[0].computed);
    CHECK(td.legs[0].errorMsg.empty());
    // F1-only horizontal angle is used directly; bearing = startBearing + 90°.
    CHECK(td.legs[0].computedBearingDeg == Approx(90.0).margin(1e-6));
    // Zenith ~90° => horizontal distance ~ slope distance.
    CHECK(td.legs[0].computedHorizDist == Approx(100.0).margin(1e-3));
}
