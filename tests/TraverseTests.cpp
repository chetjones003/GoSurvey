// Domain regression tests for the Traverse Editor extension (REQ-011, REQ-012,
// REQ-015, REQ-016, REQ-017, REQ-018). Pure compute only — no UI/GL dependency
// (ADR-002).

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

// ---------------------------------------------------------------- REQ-018 --
// ReduceLegFromSets re-derives a leg's reduced fields from its editable raw
// sets, subtracting the stored backsight reading (ADR-003). Editing/adding a
// set must change the reduced result.
TEST_CASE("ReduceLegFromSets re-derives leg", "[reduce][REQ-018]") {
    auto makeSet = [](int n, double f1Hz, double f2Hz, double sd) {
        TraverseMeasSet s;
        s.setNo = n;
        s.hasF1 = true; s.f1HzDec = f1Hz; s.f1VaDec = 90.0; s.f1Sd = sd;
        s.hasF2 = true; s.f2HzDec = f2Hz; s.f2VaDec = 270.0; s.f2Sd = sd;
        return s;
    };

    SECTION("face-averaged circle reading reduced against the backsight") {
        TraverseLeg leg;
        leg.backsightCircleDeg = 10.0;
        leg.hasBacksightCircle = true;
        leg.rawSets.push_back(makeSet(1, 90.0, 270.0, 100.0)); // F2 = F1 + 180.
        ReduceLegFromSets(leg);

        REQUIRE(leg.hasHorizAngle);
        CHECK(leg.hasFace1);
        CHECK(leg.hasFace2);
        CHECK(leg.horizAngleDeg == Approx(80.0).margin(1e-9));  // 90 - 10 backsight.
        CHECK(leg.vertAngleDeg  == Approx(90.0).margin(1e-9));  // (90 + (360-270))/2.
        CHECK(leg.slopeDist     == Approx(100.0).margin(1e-9));
    }

    SECTION("adding a second observation moves the mean") {
        TraverseLeg leg;                       // no backsight => reading is the angle.
        leg.rawSets.push_back(makeSet(1, 90.0, 270.0, 100.0));
        ReduceLegFromSets(leg);
        CHECK(leg.horizAngleDeg == Approx(90.0).margin(1e-9));

        leg.rawSets.push_back(makeSet(2, 92.0, 272.0, 100.0));
        ReduceLegFromSets(leg);
        CHECK(leg.horizAngleDeg == Approx(91.0).margin(1e-9));  // mean of 90 and 92.
    }

    SECTION("empty sets leave a manual leg untouched") {
        TraverseLeg leg;
        leg.hasHorizAngle = true; leg.horizAngleDeg = 45.0;
        ReduceLegFromSets(leg);                // no rawSets -> no-op.
        CHECK(leg.horizAngleDeg == Approx(45.0));
    }
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

// ------------------------------------------------------------------ #826 --
// Earth curvature + atmospheric refraction correction (issue #826).
// Expected values are hand-computed from C = (1 - k) * hd^2 / (2R),
// R = 6371000 m — independent of the engine under test.
namespace {

constexpr double k826Deg2Rad = 3.14159265358979323846 / 180.0;

// Single-leg traverse; returns the computed dZ. useSlope selects the
// slope-distance path (zenith convention); otherwise horizontal distance
// with an elevation angle.
double Run826Leg(double distM, double vertAngleDeg, bool isZenith, bool useSlope,
                 bool applyCorr, double k) {
    TraverseData td;
    td.startStationId = 1;
    td.startBearingDeg = 0.0;
    td.hasStartBearing = true;
    td.applyCurvRefCorr = applyCorr;
    td.refractionCoeff = k;
    TraverseLeg leg;
    leg.stationId = 2;
    leg.hasHorizAngle = true;
    leg.horizAngleDeg = 0.0;
    if (useSlope) {
        leg.hasSlopeDist = true;
        leg.slopeDist = distM;
    } else {
        leg.hasHorizDist = true;
        leg.horizDist = distM;
    }
    leg.hasVertAngle = true;
    leg.vertAngleDeg = vertAngleDeg;
    leg.isZenithAngle = isZenith;
    td.legs.push_back(leg);
    ComputeTraverse(td);
    REQUIRE(td.legs[0].computed);
    return td.legs[0].computedDeltaZ;
}

}  // namespace

TEST_CASE("Curvature+refraction: 1 km leg gains 0.0675 m at k=0.14", "[curvref][issue-826]") {
    const double dzCorr = Run826Leg(1000.0, 2.0, false, false, true, 0.14);
    const double dzPlain = Run826Leg(1000.0, 2.0, false, false, false, 0.14);
    // Hand-computed: (1 - 0.14) * 1000^2 / (2 * 6371000) = 0.067484 m.
    CHECK(dzCorr - dzPlain == Approx(0.0674933).margin(1e-6));
    // The trig part itself is untouched by the fix.
    CHECK(dzPlain == Approx(1000.0 * std::tan(2.0 * k826Deg2Rad)).margin(1e-9));
}

TEST_CASE("Curvature+refraction: k=0 gives pure curvature 0.0785 m/km", "[curvref][issue-826]") {
    const double dzCorr = Run826Leg(1000.0, 2.0, false, false, true, 0.0);
    const double dzPlain = Run826Leg(1000.0, 2.0, false, false, false, 0.0);
    // Hand-computed: 1000^2 / (2 * 6371000) = 0.078480 m.
    CHECK(dzCorr - dzPlain == Approx(0.078480).margin(1e-6));
}

TEST_CASE("Curvature+refraction: custom k=0.20 scales the correction", "[curvref][issue-826]") {
    const double dzCorr = Run826Leg(1000.0, 2.0, false, false, true, 0.20);
    const double dzPlain = Run826Leg(1000.0, 2.0, false, false, false, 0.20);
    // Hand-computed: (1 - 0.20) * 1000^2 / (2 * 6371000) = 0.062784 m.
    CHECK(dzCorr - dzPlain == Approx(0.062784).margin(1e-6));
}

TEST_CASE("Curvature+refraction: 300 m leg corrects 6.1 mm", "[curvref][issue-826]") {
    const double dzCorr = Run826Leg(300.0, 1.0, false, false, true, 0.14);
    const double dzPlain = Run826Leg(300.0, 1.0, false, false, false, 0.14);
    // Hand-computed: (1 - 0.14) * 300^2 / (2 * 6371000) = 0.0060736 m.
    CHECK(dzCorr - dzPlain == Approx(0.00607440).margin(1e-7));
}

TEST_CASE("Curvature+refraction: downhill sights get a positive correction", "[curvref][issue-826]") {
    // Curvature dominates regardless of sight direction: the correction is
    // always added, so a downhill leg becomes less negative.
    const double dzCorr = Run826Leg(1000.0, -2.0, false, false, true, 0.14);
    const double dzPlain = Run826Leg(1000.0, -2.0, false, false, false, 0.14);
    CHECK(dzCorr - dzPlain == Approx(0.0674933).margin(1e-6));
    CHECK(dzCorr > dzPlain);
}

TEST_CASE("Curvature+refraction: slope-distance/zenith path matches", "[curvref][issue-826]") {
    // 1000 m slope at zenith 88 deg: hd = 1000*sin(88) = 999.3908 m.
    // Hand-computed: (1 - 0.14) * 999.3908^2 / (2 * 6371000) = 0.067402 m.
    const double dzCorr = Run826Leg(1000.0, 88.0, true, true, true, 0.14);
    const double dzPlain = Run826Leg(1000.0, 88.0, true, true, false, 0.14);
    CHECK(dzCorr - dzPlain == Approx(0.0674111).margin(1e-6));
}

TEST_CASE("Curvature+refraction: legs with no vertical angle are untouched", "[curvref][issue-826]") {
    // No vertical observation -> legacy flat behavior, bit-identical.
    TraverseData td;
    td.startStationId = 1;
    td.startBearingDeg = 0.0;
    td.hasStartBearing = true;
    td.applyCurvRefCorr = true;
    TraverseLeg leg;
    leg.stationId = 2;
    leg.hasHorizAngle = true;
    leg.horizAngleDeg = 0.0;
    leg.hasHorizDist = true;
    leg.horizDist = 1000.0;
    td.legs.push_back(leg);
    ComputeTraverse(td);
    REQUIRE(td.legs[0].computed);
    CHECK(td.legs[0].computedDeltaZ == Approx(0.0).margin(1e-12));
}

// REQ-401: the scale-factor option is off by default and must not change anything when left off.
TEST_CASE("Scale factor off reproduces unscaled coordinates", "[engine][REQ-401]") {
    TraverseData td;
    td.startStationId = 1;
    td.startBearingDeg = 0.0;
    td.hasStartBearing = true;

    TraverseLeg leg;
    leg.stationId = 2;
    leg.hasHorizAngle = true; leg.horizAngleDeg = 90.0;
    leg.hasHorizDist = true;  leg.horizDist = 100.0;
    td.legs.push_back(leg);

    REQUIRE_FALSE(td.applyScaleFactor);  // default
    ComputeTraverse(td);
    REQUIRE(td.legs[0].computed);
    CHECK(td.legs[0].computedHorizDist == Approx(100.0).margin(1e-9));
    CHECK(td.legs[0].computedGroundHorizDist == Approx(100.0).margin(1e-9));
    CHECK(td.legs[0].computedEasting == Approx(100.0).margin(1e-9));
    CHECK(td.closurePerimeter == Approx(100.0).margin(1e-9));
}

// REQ-401: turning the option on scales the leg's ground distance into every downstream quantity.
TEST_CASE("Scale factor on scales distance, coordinates, and perimeter", "[engine][REQ-401]") {
    TraverseData td;
    td.startStationId = 1;
    td.startBearingDeg = 0.0;
    td.hasStartBearing = true;
    td.applyScaleFactor = true;
    td.scaleFactor = 0.9996;  // a typical UTM-like central-meridian grid factor

    TraverseLeg leg;
    leg.stationId = 2;
    leg.hasHorizAngle = true; leg.horizAngleDeg = 90.0;
    leg.hasHorizDist = true;  leg.horizDist = 1000.0;
    td.legs.push_back(leg);

    ComputeTraverse(td);
    REQUIRE(td.legs[0].computed);
    CHECK(td.legs[0].computedGroundHorizDist == Approx(1000.0).margin(1e-9));
    CHECK(td.legs[0].computedHorizDist == Approx(999.6).margin(1e-6));
    CHECK(td.legs[0].computedEasting == Approx(999.6).margin(1e-6));
    CHECK(td.closurePerimeter == Approx(999.6).margin(1e-6));
}

// REQ-401: Combined vs Grid-only is purely a UI-side choice of which number is fed into
// scaleFactor — ComputeTraverse itself only ever sees the resolved multiplier.
TEST_CASE("Scale factor kind defaults to Combined", "[engine][REQ-401]") {
    TraverseData td;
    CHECK(td.scaleFactorKind == TraverseData::ScaleFactorKind::Combined);
}
