// Domain regression tests for the Traverse Editor extension (REQ-011, REQ-012,
// REQ-015, REQ-016, REQ-017, REQ-018). Pure compute only — no UI/GL dependency
// (ADR-002).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "traverse/TraverseCalc.hpp"
#include "traverse/TraverseLeastSquares.hpp"
#include "traverse/FbkImport.hpp"
#include "traverse/TraverseIo.hpp"
#include "traverse/TraverseCommandLang.hpp"
#include "traverse/StarNetImport.hpp"
#include "traverse/Rw5Import.hpp"
#include "traverse/JobXmlImport.hpp"
#include "traverse/TraverseReport.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
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

TEST_CASE("Chi-square critical values match published tables", "[lsa][req418]") {
    CHECK(LsaChiSquareCritical(0.975, 1) == Approx(5.023886).epsilon(0.002));
    CHECK(LsaChiSquareCritical(0.025, 1) == Approx(0.0009820691).epsilon(0.05));
    CHECK(LsaChiSquareCritical(0.975, 2) == Approx(7.377758908).epsilon(0.002));
    CHECK(LsaChiSquareCritical(0.025, 2) == Approx(0.050635616).epsilon(0.02));
    CHECK(LsaChiSquareCritical(0.975, 10) == Approx(20.48317735).epsilon(0.002));
    CHECK(LsaChiSquareCritical(0.025, 10) == Approx(3.24697278).epsilon(0.002));
    CHECK(LsaChiSquareCritical(0.0, 10) == Approx(0.0));
    CHECK(LsaChiSquareCritical(0.975, 0) == Approx(0.0));
}

TEST_CASE("Each measurement is its own residual row", "[lsa][req418]") {
    const TraverseData td = MakeSquareLoop();
    const LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);

    int angles = 0;
    int distances = 0;
    for (const LsaMeasResidual& row : r.measResiduals) {
        if (row.stdError > 0.0)
            CHECK(row.stdResidual == Approx(row.residual / row.stdError).margin(1e-9));
        CHECK_FALSE(row.flagged);
        CHECK(std::abs(row.stdResidual) <= 3.0);
        if (row.kind == LsaMeasKind::Angle) ++angles;
        if (row.kind == LsaMeasKind::Distance) ++distances;
    }
    CHECK(angles == 4);
    CHECK(distances == 4);
    REQUIRE(!r.measResiduals.empty());
    CHECK(r.measResiduals.front().kind == LsaMeasKind::Angle);
    CHECK(r.measResiduals.front().fromStationId == -1);

    CHECK(r.chiSquare.evaluated);
    CHECK(r.chiSquare.errorFactor == Approx(r.refStdDev).margin(1e-12));
    CHECK_FALSE(r.chiSquare.passed);
    CHECK_FALSE(r.chiSquare.exceededUpper);
    for (const LsaKindSummary& summary : r.kindSummaries) {
        REQUIRE(summary.count > 0);
        CHECK(summary.errorFactor ==
              Approx(std::sqrt(summary.sumSqStdResidual / static_cast<double>(summary.count))).margin(1e-12));
    }
}

TEST_CASE("A network of distances and an angle adjusts", "[lsa][req419]") {
    const char* text =
        "CTRL 1 0 0 ! !\n"
        "CTRL 2 100 0 ! !\n"
        "CTRL 3 50 86.6025403784 * *\n"
        "DIST 1 2 100\n"
        "DIST 2 3 100\n"
        "DIST 1 3 100\n"
        "ANGLE 1 2 3 -60\n";
    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    CHECK(td.networkStations.size() == 3);
    CHECK(td.networkDists.size() == 3);
    CHECK(td.networkAngles.size() == 1);

    const std::string again = travcmd::Serialize(td, weights);
    TraverseData roundTrip;
    REQUIRE(travcmd::Parse(again, roundTrip, weights, err));
    CHECK(roundTrip.networkDists.size() == 3);
    CHECK(roundTrip.networkAngles.size() == 1);

    const LsaResult result = ComputeTraverseLeastSquares(td, weights);
    REQUIRE(result.ok);
    CHECK(result.redundancy >= 1);
    double east = 0.0, north = 0.0;
    bool found = false;
    for (size_t i = 0; i < result.stationIds.size(); ++i) {
        if (result.stationIds[i] != 3) continue;
        east = result.adjEasting[i];
        north = result.adjNorthing[i];
        found = true;
    }
    CHECK(found);
    CHECK(east == Approx(50.0).margin(1e-4));
    CHECK(north == Approx(86.6025403784).margin(1e-4));
    for (const LsaMeasResidual& row : result.measResiduals)
        CHECK(std::abs(row.residual) < 1e-3);
}

TEST_CASE("A network distance blunder is marked", "[lsa][req419]") {
    const char* text =
        "CTRL 1 0 0 ! !\n"
        "CTRL 2 100 0 ! !\n"
        "CTRL 3 50 86.6025403784 * *\n"
        "DIST 1 2 100\n"
        "DIST 2 3 101 0.01\n"
        "DIST 1 3 100\n"
        "ANGLE 1 2 3 -60\n";
    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    const LsaResult result = ComputeTraverseLeastSquares(td, weights);
    REQUIRE(result.ok);
    bool markedDistance = false;
    for (const LsaMeasResidual& row : result.measResiduals) {
        if (row.kind == LsaMeasKind::Distance && row.flagged && row.fromStationId == 2 && row.toStationId == 3)
            markedDistance = true;
    }
    CHECK(markedDistance);
}

TEST_CASE("A standardized residual over 3 is marked", "[lsa][req418]") {
    const TraverseData td = MakeSquareLoop(101.0);
    LsaWeights weights;
    weights.sigmaAngleSec = 60.0;
    weights.sigmaDistConstFt = 0.01;
    weights.sigmaDistPpm = 0.0;
    const LsaResult r = ComputeTraverseLeastSquares(td, weights);
    REQUIRE(r.ok);
    CHECK(r.adjClosureLinear < 1e-3);
    REQUIRE(r.residuals.size() == 4);

    bool marked = false;
    bool unmarked = false;
    for (const LsaMeasResidual& row : r.measResiduals) {
        if (row.stdError > 0.0)
            CHECK(row.stdResidual == Approx(row.residual / row.stdError).margin(1e-6));
        if (row.flagged) {
            marked = true;
            CHECK(std::abs(row.stdResidual) > 3.0);
        } else {
            unmarked = true;
            CHECK(std::abs(row.stdResidual) <= 3.0);
        }
    }
    CHECK(marked);
    CHECK(unmarked);
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

// ------------------------------------------------- open traverse (#848) --
// Same geometry as MakeSquareLoop but stopping one leg short of closing: an
// OPEN traverse tied to a known ENDPOINT instead of looping back to the
// start. Three legs of a 100x100 square from (0,0) heading north with a 90°
// turn each leg lands exactly on (E=0, N=100) — see MakeSquareLoop's own
// worked geometry in its comment-free math (start bearing 0, HA 90 each leg).
TraverseData MakeOpenSquareThreeLegs() {
    TraverseData td;
    td.startStationId  = 0;
    td.startEasting     = 0.0;
    td.startNorthing    = 0.0;
    td.startBearingDeg  = 0.0;
    td.hasStartBearing  = true;
    td.isClosedLoop     = false;
    td.endEasting       = 0.0;
    td.endNorthing       = 100.0;
    td.hasEndCoordinates = true;

    auto addLeg = [&](int id, double haDeg, double dist) {
        TraverseLeg leg;
        leg.stationId    = id;
        leg.hasHorizAngle = true; leg.horizAngleDeg = haDeg;
        leg.hasHorizDist  = true; leg.horizDist     = dist;
        td.legs.push_back(leg);
    };
    addLeg(1, 90.0, 100.0);
    addLeg(2, 90.0, 100.0);
    addLeg(3, 90.0, 100.0);  // lands on the known ENDPOINT, (0, 100).

    ComputeTraverse(td);
    return td;
}

TEST_CASE("Least squares: open traverse with a known ENDPOINT adjusts", "[lsa][REQ-017]") {
    TraverseData td = MakeOpenSquareThreeLegs();

    // ComputeTraverse's own (unadjusted) closure now compares against the
    // ENDPOINT, not the start, for an open traverse.
    REQUIRE(td.closureValid);
    CHECK(td.closureLinear == Approx(0.0).margin(1e-6));

    LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);
    CHECK(r.observations == 6);
    CHECK(r.unknowns == 4);     // 2 free interior stations (1, 2) x 2D -- same shape as a closed loop.
    CHECK(r.redundancy == 2);
    CHECK(r.adjClosureLinear == Approx(0.0).margin(1e-6));
    for (const LsaResidual& v : r.residuals) {
        CHECK(v.distResidualFt == Approx(0.0).margin(1e-6));
        CHECK(v.angleResidualSec == Approx(0.0).margin(1e-3));
    }
}

TEST_CASE("Least squares: open traverse adjustment drives ENDPOINT misclosure to zero",
         "[lsa][REQ-017]") {
    TraverseData td = MakeOpenSquareThreeLegs();
    td.legs[0].horizDist += 0.10;  // 0.10 ft long on leg 1, same perturbation as the closed-loop test.
    ComputeTraverse(td);

    REQUIRE(td.closureValid);
    CHECK(td.closureLinear > 0.05);  // genuinely fails to land on the known ENDPOINT.

    LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);
    CHECK(r.redundancy == 2);
    CHECK(r.adjClosureLinear < 1e-3);
    CHECK(r.refStdDev > 0.0);
    CHECK(std::isfinite(r.refStdDev));
}

TEST_CASE("GSTC round-trips ENDPOINT for an open traverse", "[travcmd][REQ-017]") {
    TraverseData td = MakeOpenSquareThreeLegs();
    const std::string text = travcmd::Serialize(td, LsaWeights{});
    CHECK(text.find("ENDPOINT 0.000000 100.000000 0.000000") != std::string::npos);
    CHECK(text.find("LOOP OFF") != std::string::npos);

    TraverseData parsed;
    LsaWeights w;
    std::string err;
    REQUIRE(travcmd::Parse(text, parsed, w, err));
    CHECK(parsed.hasEndCoordinates);
    CHECK(parsed.endEasting == Approx(0.0));
    CHECK(parsed.endNorthing == Approx(100.0));
    CHECK_FALSE(parsed.isClosedLoop);
}

// ---------------------------------------------------------------- REQ-017 --
TEST_CASE("Least squares: insufficient/invalid input is surfaced", "[lsa][REQ-017]") {
    SECTION("open traverse with no ENDPOINT is rejected") {
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

// ---- REQ-403 (#834): Save/Load round-trip ----

namespace {

std::filesystem::path TraverseIoTestDir() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gosurvey-traverseio";
    std::filesystem::create_directories(dir);
    return dir;
}

// A closed-loop traverse with raw observation sets and non-default weights, exercising every
// field TraverseIo::Save/Load must round-trip.
TraverseData MakeRoundTripFixture() {
    TraverseData td;
    td.startStationId = 5;
    td.startEasting = 1000.25;
    td.startNorthing = 2000.5;
    td.startElevation = 123.75;
    td.startBearingBuf = "45d30m10s";
    TraverseParseAngle(td.startBearingBuf, &td.startBearingDeg);
    td.hasStartBearing = true;
    td.useFace1Face2 = true;
    td.isClosedLoop = true;
    td.applyScaleFactor = true;
    td.scaleFactorKind = TraverseData::ScaleFactorKind::GridOnly;
    td.scaleFactor = 0.99987;

    TraverseLeg leg;
    leg.stationId = 6;
    leg.description = "FH set";
    leg.horizAngleBuf = "90d00m00s";
    leg.horizAngleDeg = 90.0;
    leg.hasHorizAngle = true;
    leg.backsightCircleDeg = 12.5;
    leg.hasBacksightCircle = true;
    leg.hasHorizDist = true;
    leg.horizDist = 150.0;
    leg.vertAngleBuf = "89d59m00s";
    leg.vertAngleDeg = 89.9833;
    leg.isZenithAngle = true;
    leg.hasVertAngle = true;
    leg.hasFace1 = true;
    leg.hasFace2 = true;

    TraverseMeasSet s1;
    s1.setNo = 1;
    s1.hasF1 = true; s1.f1HzDec = 90.0001; s1.f1VaDec = 89.98; s1.f1Sd = 150.01;
    s1.hasF2 = true; s1.f2HzDec = 270.0002; s1.f2VaDec = 270.02; s1.f2Sd = 150.02;
    leg.rawSets.push_back(s1);
    TraverseMeasSet s2;
    s2.setNo = 2;
    s2.hasF1 = true; s2.f1HzDec = 89.9998; s2.f1VaDec = 89.99; s2.f1Sd = 149.99;
    leg.rawSets.push_back(s2);

    td.legs.push_back(leg);
    ComputeTraverse(td);
    return td;
}

LsaWeights MakeRoundTripWeights() {
    LsaWeights w;
    w.sigmaAngleSec = 3.5;
    w.sigmaDistConstFt = 0.015;
    w.sigmaDistPpm = 4.0;
    return w;
}

}  // namespace

TEST_CASE("TraverseIo: save/load round-trips every input field", "[io][REQ-403]") {
    const TraverseData original = MakeRoundTripFixture();
    const LsaWeights originalWeights = MakeRoundTripWeights();
    const std::string path = (TraverseIoTestDir() / "roundtrip.gstrav").string();

    std::string saveErr;
    REQUIRE(traverseio::Save(path, original, originalWeights, saveErr));
    CHECK(saveErr.empty());

    TraverseData loaded;
    LsaWeights loadedWeights;
    std::string loadErr;
    REQUIRE(traverseio::Load(path, loaded, loadedWeights, loadErr));
    CHECK(loadErr.empty());

    CHECK(loaded.startStationId == original.startStationId);
    CHECK(loaded.startEasting == Approx(original.startEasting));
    CHECK(loaded.startNorthing == Approx(original.startNorthing));
    CHECK(loaded.startElevation == Approx(original.startElevation));
    CHECK(loaded.startBearingBuf == original.startBearingBuf);
    CHECK(loaded.startBearingDeg == Approx(original.startBearingDeg));
    CHECK(loaded.hasStartBearing == original.hasStartBearing);
    CHECK(loaded.useFace1Face2 == original.useFace1Face2);
    CHECK(loaded.isClosedLoop == original.isClosedLoop);
    CHECK(loaded.applyScaleFactor == original.applyScaleFactor);
    CHECK(loaded.scaleFactorKind == original.scaleFactorKind);
    CHECK(loaded.scaleFactor == Approx(original.scaleFactor));

    REQUIRE(loaded.legs.size() == original.legs.size());
    const TraverseLeg& a = original.legs[0];
    const TraverseLeg& b = loaded.legs[0];
    CHECK(b.stationId == a.stationId);
    CHECK(b.description == a.description);
    CHECK(b.horizAngleDeg == Approx(a.horizAngleDeg));
    CHECK(b.backsightCircleDeg == Approx(a.backsightCircleDeg));
    CHECK(b.hasBacksightCircle == a.hasBacksightCircle);
    CHECK(b.vertAngleDeg == Approx(a.vertAngleDeg));
    REQUIRE(b.rawSets.size() == a.rawSets.size());
    for (size_t i = 0; i < a.rawSets.size(); ++i) {
        CHECK(b.rawSets[i].setNo == a.rawSets[i].setNo);
        CHECK(b.rawSets[i].hasF1 == a.rawSets[i].hasF1);
        CHECK(b.rawSets[i].f1HzDec == Approx(a.rawSets[i].f1HzDec));
        CHECK(b.rawSets[i].hasF2 == a.rawSets[i].hasF2);
        CHECK(b.rawSets[i].f2HzDec == Approx(a.rawSets[i].f2HzDec));
    }

    CHECK(loadedWeights.sigmaAngleSec == Approx(originalWeights.sigmaAngleSec));
    CHECK(loadedWeights.sigmaDistConstFt == Approx(originalWeights.sigmaDistConstFt));
    CHECK(loadedWeights.sigmaDistPpm == Approx(originalWeights.sigmaDistPpm));
}

// REQ-403 clause 4: a reloaded traverse recomputes to the same closure it had when saved.
TEST_CASE("TraverseIo: reload reproduces the saved closure", "[io][REQ-403]") {
    TraverseData original = MakeRoundTripFixture();
    const double savedClosureLinear = original.closureLinear;
    const double savedPerimeter = original.closurePerimeter;
    const std::string path = (TraverseIoTestDir() / "closure.gstrav").string();

    std::string err;
    LsaWeights weights = MakeRoundTripWeights();
    REQUIRE(traverseio::Save(path, original, weights, err));

    TraverseData loaded;
    LsaWeights loadedWeights;
    REQUIRE(traverseio::Load(path, loaded, loadedWeights, err));
    ComputeTraverse(loaded);  // the UI always recomputes after a load (traverseDataDirty).

    CHECK(loaded.closureLinear == Approx(savedClosureLinear).margin(1e-9));
    CHECK(loaded.closurePerimeter == Approx(savedPerimeter).margin(1e-9));
}

TEST_CASE("TraverseIo: an open-loop traverse with no raw sets round-trips too", "[io][REQ-403]") {
    TraverseData td;
    td.startStationId = 1;
    td.startBearingDeg = 0.0;
    td.hasStartBearing = true;
    td.isClosedLoop = false;

    TraverseLeg leg;
    leg.stationId = 2;
    leg.hasHorizAngle = true; leg.horizAngleDeg = 90.0;
    leg.hasHorizDist = true;  leg.horizDist = 100.0;
    td.legs.push_back(leg);
    ComputeTraverse(td);

    const std::string path = (TraverseIoTestDir() / "openloop.gstrav").string();
    LsaWeights weights;
    std::string err;
    REQUIRE(traverseio::Save(path, td, weights, err));

    TraverseData loaded;
    LsaWeights loadedWeights;
    REQUIRE(traverseio::Load(path, loaded, loadedWeights, err));
    REQUIRE(loaded.legs.size() == 1);
    CHECK(loaded.legs[0].rawSets.empty());
    CHECK(loaded.isClosedLoop == false);
}

TEST_CASE("TraverseIo: a malformed file fails Load and leaves the traverse untouched", "[io][REQ-403]") {
    const std::filesystem::path path = TraverseIoTestDir() / "malformed.gstrav";
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << "{ this is not valid json";
    }

    TraverseData td = MakeRoundTripFixture();
    const TraverseData before = td;
    LsaWeights weights = MakeRoundTripWeights();
    const LsaWeights weightsBefore = weights;

    std::string err;
    CHECK_FALSE(traverseio::Load(path.string(), td, weights, err));
    CHECK_FALSE(err.empty());
    CHECK(td.legs.size() == before.legs.size());
    CHECK(td.startStationId == before.startStationId);
    CHECK(weights.sigmaAngleSec == Approx(weightsBefore.sigmaAngleSec));
}

TEST_CASE("TraverseIo: a file with the wrong format tag is refused", "[io][REQ-403]") {
    const std::filesystem::path path = TraverseIoTestDir() / "wrongformat.gstrav";
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << R"({"format":"gspack","version":1})";
    }

    TraverseData td;
    LsaWeights weights;
    std::string err;
    CHECK_FALSE(traverseio::Load(path.string(), td, weights, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("TraverseIo: a missing file fails Load with a reason", "[io][REQ-403]") {
    TraverseData td;
    LsaWeights weights;
    std::string err;
    CHECK_FALSE(traverseio::Load((TraverseIoTestDir() / "does-not-exist.gstrav").string(), td, weights, err));
    CHECK_FALSE(err.empty());
}

// ---------------------------------------------------------------------------
// GitHub issue #845 (Phase A) — GoSurvey's own traverse command language
// (travcmd::), the two-way-synced text view in the Traverse Editor's Command
// tab. Only fields the form panel can manually edit round-trip here; raw
// per-set F1/F2 import detail (rawSets) has no manual-entry counterpart
// either, so it is out of scope by design (see TraverseCommandLang.hpp).
// ---------------------------------------------------------------------------

namespace {

TraverseData MakeCommandLangFixture() {
    TraverseData td;
    td.startStationId = 100;
    td.startEasting = 5000.25;
    td.startNorthing = 10000.75;
    td.startElevation = 123.4;
    td.startBearingDeg = 45.5;
    td.hasStartBearing = true;
    td.startBearingBuf = TraverseFormatBearing(td.startBearingDeg);
    td.isClosedLoop = true;
    td.applyScaleFactor = true;
    td.scaleFactorKind = TraverseData::ScaleFactorKind::GridOnly;

    TraverseLeg leg1;
    leg1.stationId = 101;
    leg1.description = "MH 1";
    leg1.horizAngleDeg = 90.0;
    leg1.hasHorizAngle = true;
    leg1.horizDist = 150.25;
    leg1.hasHorizDist = true;
    leg1.vertAngleDeg = 90.0;
    leg1.hasVertAngle = true;
    leg1.isZenithAngle = true;
    td.legs.push_back(leg1);

    TraverseLeg leg2;
    leg2.stationId = 102;
    leg2.description = "CB2";
    leg2.horizAngleDeg = 270.5;
    leg2.hasHorizAngle = true;
    leg2.slopeDist = 200.1;
    leg2.hasSlopeDist = true;
    leg2.vertAngleDeg = 2.5;
    leg2.hasVertAngle = true;
    leg2.isZenithAngle = false;
    td.legs.push_back(leg2);

    return td;
}

}  // namespace

TEST_CASE("TraverseCommandLang: Serialize then Parse round-trips every editable field", "[traverse][REQ-404]") {
    const TraverseData original = MakeCommandLangFixture();
    const LsaWeights originalWeights = MakeRoundTripWeights();

    const std::string text = travcmd::Serialize(original, originalWeights);

    TraverseData parsed;
    LsaWeights parsedWeights;
    std::string err;
    REQUIRE(travcmd::Parse(text, parsed, parsedWeights, err));
    CHECK(err.empty());

    CHECK(parsed.startStationId == original.startStationId);
    CHECK(parsed.startEasting == Approx(original.startEasting));
    CHECK(parsed.startNorthing == Approx(original.startNorthing));
    CHECK(parsed.startElevation == Approx(original.startElevation));
    CHECK(parsed.hasStartBearing == original.hasStartBearing);
    CHECK(parsed.startBearingDeg == Approx(original.startBearingDeg));
    CHECK(parsed.isClosedLoop == original.isClosedLoop);
    CHECK(parsed.applyScaleFactor == original.applyScaleFactor);
    CHECK(parsed.scaleFactorKind == original.scaleFactorKind);

    REQUIRE(parsed.legs.size() == original.legs.size());
    for (size_t i = 0; i < original.legs.size(); ++i) {
        const TraverseLeg& a = original.legs[i];
        const TraverseLeg& b = parsed.legs[i];
        CHECK(b.stationId == a.stationId);
        CHECK(b.description == a.description);
        CHECK(b.hasHorizAngle == a.hasHorizAngle);
        CHECK(b.horizAngleDeg == Approx(a.horizAngleDeg));
        CHECK(b.hasHorizDist == a.hasHorizDist);
        if (a.hasHorizDist) CHECK(b.horizDist == Approx(a.horizDist));
        CHECK(b.hasSlopeDist == a.hasSlopeDist);
        if (a.hasSlopeDist) CHECK(b.slopeDist == Approx(a.slopeDist));
        CHECK(b.hasVertAngle == a.hasVertAngle);
        CHECK(b.vertAngleDeg == Approx(a.vertAngleDeg));
        CHECK(b.isZenithAngle == a.isZenithAngle);
    }

    CHECK(parsedWeights.sigmaAngleSec == Approx(originalWeights.sigmaAngleSec));
    CHECK(parsedWeights.sigmaDistConstFt == Approx(originalWeights.sigmaDistConstFt));
    CHECK(parsedWeights.sigmaDistPpm == Approx(originalWeights.sigmaDistPpm));
}

TEST_CASE("TraverseCommandLang: a traverse with no legs still round-trips", "[traverse][REQ-404]") {
    TraverseData td;
    td.startStationId = 1;
    td.startEasting = 1000.0;
    td.startNorthing = 2000.0;
    td.startElevation = 50.0;
    LsaWeights weights;

    const std::string text = travcmd::Serialize(td, weights);

    TraverseData parsed;
    LsaWeights parsedWeights;
    std::string err;
    REQUIRE(travcmd::Parse(text, parsed, parsedWeights, err));
    CHECK(parsed.legs.empty());
    CHECK(parsed.startStationId == 1);
}

TEST_CASE("TraverseCommandLang: comments and blank lines are ignored", "[traverse][REQ-404]") {
    const std::string text =
        "// a leading comment\n"
        "\n"
        "STATION 5 100 200 10\n"
        "   // an indented comment\n"
        "STN 5 6\n"
        "HANG REF 5 6 90\n";

    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    CHECK(td.startStationId == 5);
    REQUIRE(td.legs.size() == 1);
    CHECK(td.legs[0].stationId == 6);
}

TEST_CASE("TraverseCommandLang: column alignment does not grow the text", "[traverse][REQ-404]") {
    const std::string text =
        "STATION 1 0 0 0\n"
        "CTRL 10 1000.5 2000.25 100 ! ! *\n"
        "DIR 1 10 11 12.345\n"
        "DIR 1 10 12 90\n"
        "// IMPORT \"job.dat\"\n";
    const std::string once = travcmd::AlignColumns(text);
    const std::string twice = travcmd::AlignColumns(once);
    CHECK(twice == once);
    CHECK(std::count(once.begin(), once.end(), '\n') == std::count(text.begin(), text.end(), '\n'));
    CHECK(once.find("CTRL") != std::string::npos);
    CHECK(once.find("DIR") != std::string::npos);
    const std::string marked = "#IMPORT \"job folder\\traverse.dat\"\nCTRL 1 10 20\n";
    const std::string markedAligned = travcmd::AlignColumns(marked);
    CHECK(markedAligned.find("#IMPORT \"job folder\\traverse.dat\"") != std::string::npos);
    CHECK(std::count(markedAligned.begin(), markedAligned.end(), '\n') ==
          std::count(marked.begin(), marked.end(), '\n'));
    // A second format of an already-formatted document must not add a blank line.
    const std::string third = travcmd::AlignColumns(twice + "");
    CHECK(std::count(third.begin(), third.end(), '\n') == std::count(once.begin(), once.end(), '\n'));

    const std::string shots =
        "DIR 0 1 5 269.647606 'TH CN\n"
        "DIST 1 5 262.134139 'TH CN\n"
        "ZENITH 1 5 91.731956 'TH CN\n";
    const std::string shotsAligned = travcmd::AlignColumns(shots);
    CHECK(travcmd::AlignColumns(shotsAligned) == shotsAligned);
    auto quoteColumn = [](const std::string& text, const char* word) -> size_t {
        const size_t line = text.find(word);
        if (line == std::string::npos) return std::string::npos;
        const size_t end = text.find('\n', line);
        const size_t quote = text.find('\'', line);
        if (quote == std::string::npos || (end != std::string::npos && quote > end)) return std::string::npos;
        return quote - line;
    };
    const size_t dirQuote = quoteColumn(shotsAligned, "DIR");
    const size_t distQuote = quoteColumn(shotsAligned, "DIST");
    const size_t zenithQuote = quoteColumn(shotsAligned, "ZENITH");
    REQUIRE(dirQuote != std::string::npos);
    CHECK(distQuote == dirQuote);
    CHECK(zenithQuote == dirQuote);
    // The station before the measurement, and the measurement itself, share a column.
    auto tokenEnd = [](const std::string& text, const char* word, int fromRight) -> size_t {
        const size_t line = text.find(word);
        const size_t quote = text.find('\'', line);
        if (line == std::string::npos || quote == std::string::npos) return std::string::npos;
        size_t end = quote;
        while (end > line && text[end - 1] == ' ') --end;
        for (int n = 0; n < fromRight; ++n) {
            while (end > line && text[end - 1] != ' ') --end;
            while (end > line && text[end - 1] == ' ') --end;
        }
        return end - line;
    };
    CHECK(tokenEnd(shotsAligned, "DIST", 0) == tokenEnd(shotsAligned, "DIR", 0));
    CHECK(tokenEnd(shotsAligned, "ZENITH", 0) == tokenEnd(shotsAligned, "DIR", 0));
    CHECK(tokenEnd(shotsAligned, "DIST", 1) == tokenEnd(shotsAligned, "DIR", 1));
    CHECK(tokenEnd(shotsAligned, "ZENITH", 1) == tokenEnd(shotsAligned, "DIR", 1));

    // A control point's coordinates must not open a gap on the shot lines.
    const std::string mixed =
        "CTRL 1 1640.419948 3280.839895 328.083990 ! ! ! 'TH CN\n"
        "DIR 0 1 5 269.647606 'TH CN\n"
        "DIST 1 5 262.134139 'TH CN\n";
    const std::string mixedAligned = travcmd::AlignColumns(mixed);
    const size_t dirLine = mixedAligned.find("\nDIR");
    REQUIRE(dirLine != std::string::npos);
    const size_t dirStart = dirLine + 1;
    const size_t setId = mixedAligned.find("0", dirStart);
    REQUIRE(setId != std::string::npos);
    CHECK(setId - dirStart < 12);
}

TEST_CASE("TraverseCommandLang: #IMPORT is a note and keeps the measurements", "[traverse][REQ-404]") {
    const std::string text =
        "STATION 1 0 0 0\n"
        "#IMPORT \"C:\\\\Example Field Job\\260214 TRAVERSE.dat\"\n"
        "CTRL 10 1000 2000 100 ! ! *\n"
        "DIR 1 10 11 12.5\n";
    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    CHECK(err.empty());
    REQUIRE(td.networkStations.size() == 1);
    CHECK(td.networkStations[0].id == 10);
    REQUIRE(td.networkDirections.size() == 1);
    CHECK(td.networkDirections[0].toId == 11);
}

TEST_CASE("TraverseCommandLang: DESC takes the rest of the line verbatim", "[traverse][REQ-404]") {
    const std::string text =
        "STATION 1 0 0 0\n"
        "STN 1 2\n"
        "DESC 1 2 Manhole 1\n";

    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    REQUIRE(td.legs.size() == 1);
    CHECK(td.legs[0].description == "Manhole 1");
}

TEST_CASE("TraverseCommandLang: HANG's FROM for leg 2+ must match the previous leg's station", "[traverse][REQ-404]") {
    const std::string good =
        "STATION 1 0 0 0\n"
        "STN 1 2\n"
        "HANG REF 1 2 90\n"
        "STN 2 3\n"
        "HANG 1 2 3 180\n";
    TraverseData td;
    LsaWeights weights;
    std::string err;
    CHECK(travcmd::Parse(good, td, weights, err));

    const std::string bad =
        "STATION 1 0 0 0\n"
        "STN 1 2\n"
        "HANG REF 1 2 90\n"
        "STN 2 3\n"
        "HANG 99 2 3 180\n";  // wrong FROM — should have been 1
    CHECK_FALSE(travcmd::Parse(bad, td, weights, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("TraverseCommandLang: Parse rejects an observation line for an undeclared station", "[traverse][REQ-404]") {
    const std::string text =
        "STATION 1 0 0 0\n"
        "HDIST 1 2 50\n";  // no STN 1 2 first

    TraverseData td;
    LsaWeights weights;
    std::string err;
    CHECK_FALSE(travcmd::Parse(text, td, weights, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("TraverseCommandLang: Parse rejects a STN whose AT breaks the chain", "[traverse][REQ-404]") {
    const std::string text =
        "STATION 1 0 0 0\n"
        "STN 99 2\n";  // should be AT 1 (the start station) for the first leg

    TraverseData td;
    LsaWeights weights;
    std::string err;
    CHECK_FALSE(travcmd::Parse(text, td, weights, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("TraverseCommandLang: Parse rejects an unknown directive", "[traverse][REQ-404]") {
    const std::string text =
        "STATION 1 0 0 0\n"
        "BOGUS 1\n";

    TraverseData td;
    LsaWeights weights;
    std::string err;
    CHECK_FALSE(travcmd::Parse(text, td, weights, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("TraverseCommandLang: Parse rejects text with no STATION directive", "[traverse][REQ-404]") {
    const std::string text = "STN 1 2\n";

    TraverseData td;
    LsaWeights weights;
    std::string err;
    CHECK_FALSE(travcmd::Parse(text, td, weights, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("TraverseCommandLang: a malformed token is reported with its line number", "[traverse][REQ-404]") {
    const std::string text =
        "STATION 1 0 0 0\n"
        "STN 1 garbage\n";

    TraverseData td;
    LsaWeights weights;
    std::string err;
    CHECK_FALSE(travcmd::Parse(text, td, weights, err));
    CHECK(err.find("line 2") != std::string::npos);
}

TEST_CASE("TraverseCommandLang: no '=' anywhere in generated text", "[traverse][REQ-404]") {
    const TraverseData td = MakeCommandLangFixture();
    const LsaWeights weights = MakeRoundTripWeights();
    const std::string text = travcmd::Serialize(td, weights);
    CHECK(text.find('=') == std::string::npos);
}

// ---------------------------------------------------------------------------
// GitHub issue #845 follow-up — per-leg equipment corrections/errors: PRISM
// constant+error, POLE error, TSERR (per-leg a-priori weights).
// ---------------------------------------------------------------------------

TEST_CASE("TraverseCommandLang: PRISM/POLE/TSERR round-trip", "[traverse][REQ-405]") {
    const std::string text =
        "STATION 1 0 0 0\n"
        "STN 1 2\n"
        "HANG REF 1 2 90\n"
        "HDIST 1 2 50\n"
        "PRISM 1 2 -0.0100 0.0030\n"
        "POLE 1 2 0.0050\n"
        "TSERR 1 2 3.5 0.0100 1.5\n";

    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    REQUIRE(td.legs.size() == 1);
    const TraverseLeg& leg = td.legs[0];

    CHECK(leg.hasPrismConstant);
    CHECK(leg.prismConstant == Approx(-0.0100));
    CHECK(leg.hasPrismError);
    CHECK(leg.prismError == Approx(0.0030));
    CHECK(leg.hasPoleError);
    CHECK(leg.poleError == Approx(0.0050));
    CHECK(leg.hasTsWeights);
    CHECK(leg.tsWeights.sigmaAngleSec == Approx(3.5));
    CHECK(leg.tsWeights.sigmaDistConstFt == Approx(0.0100));
    CHECK(leg.tsWeights.sigmaDistPpm == Approx(1.5));

    // Round-trip through Serialize too.
    const std::string regenerated = travcmd::Serialize(td, weights);
    TraverseData reparsed;
    LsaWeights reparsedWeights;
    REQUIRE(travcmd::Parse(regenerated, reparsed, reparsedWeights, err));
    REQUIRE(reparsed.legs.size() == 1);
    CHECK(reparsed.legs[0].prismConstant == Approx(leg.prismConstant));
    CHECK(reparsed.legs[0].prismError == Approx(leg.prismError));
    CHECK(reparsed.legs[0].poleError == Approx(leg.poleError));
    CHECK(reparsed.legs[0].tsWeights.sigmaAngleSec == Approx(leg.tsWeights.sigmaAngleSec));
}

TEST_CASE("TraverseCommandLang: Parse rejects malformed PRISM/POLE/TSERR lines", "[traverse][REQ-405]") {
    TraverseData td;
    LsaWeights weights;
    std::string err;

    CHECK_FALSE(travcmd::Parse("STATION 1 0 0 0\nSTN 1 2\nPRISM 1 2 -0.01\n", td, weights, err));  // missing <error>
    CHECK_FALSE(travcmd::Parse("STATION 1 0 0 0\nSTN 1 2\nPOLE 1 2\n", td, weights, err));          // missing <error>
    CHECK_FALSE(travcmd::Parse("STATION 1 0 0 0\nSTN 1 2\nTSERR 1 2 3.5 0.01\n", td, weights, err)); // missing <ppm>
}

TEST_CASE("ComputeTraverse: PRISM constant corrects the slope distance before reduction", "[traverse][REQ-405]") {
    TraverseData base;
    base.startStationId = 1;
    base.hasStartBearing = true;
    base.startBearingDeg = 0.0;
    TraverseLeg leg;
    leg.stationId = 2;
    leg.hasHorizAngle = true;
    leg.horizAngleDeg = 0.0;
    leg.hasSlopeDist = true;
    leg.slopeDist = 100.0;
    leg.hasVertAngle = true;
    leg.vertAngleDeg = 85.0;
    leg.isZenithAngle = true;
    base.legs.push_back(leg);

    TraverseData withCorrection = base;
    withCorrection.legs[0].hasPrismConstant = true;
    withCorrection.legs[0].prismConstant = 0.05;  // +0.05 ft

    ComputeTraverse(base);
    ComputeTraverse(withCorrection);

    REQUIRE(base.legs[0].computed);
    REQUIRE(withCorrection.legs[0].computed);

    const double expectedUncorrected = TraverseReduceToHoriz(100.0, 85.0, true);
    const double expectedCorrected   = TraverseReduceToHoriz(100.05, 85.0, true);
    CHECK(base.legs[0].computedGroundHorizDist == Approx(expectedUncorrected));
    CHECK(withCorrection.legs[0].computedGroundHorizDist == Approx(expectedCorrected));
    CHECK(withCorrection.legs[0].computedGroundHorizDist > base.legs[0].computedGroundHorizDist);
}

TEST_CASE("ComputeTraverse: PRISM constant does not apply to a directly-entered HDIST", "[traverse][REQ-405]") {
    TraverseData td;
    td.startStationId = 1;
    td.hasStartBearing = true;
    td.startBearingDeg = 0.0;
    TraverseLeg leg;
    leg.stationId = 2;
    leg.hasHorizAngle = true;
    leg.horizAngleDeg = 0.0;
    leg.hasHorizDist = true;
    leg.horizDist = 100.0;
    leg.hasPrismConstant = true;
    leg.prismConstant = 0.5;  // should be ignored — no slope-distance step to correct.
    td.legs.push_back(leg);

    ComputeTraverse(td);
    REQUIRE(td.legs[0].computed);
    CHECK(td.legs[0].computedGroundHorizDist == Approx(100.0));
}

TEST_CASE("Least squares: a leg's TSERR tightens its own distance weight", "[lsa][REQ-405]") {
    TraverseData baseline = MakeSquareLoop(100.10);
    const LsaResult rBase = ComputeTraverseLeastSquares(baseline, LsaWeights{});
    REQUIRE(rBase.ok);

    TraverseData tightened = MakeSquareLoop(100.10);
    tightened.legs[0].hasTsWeights = true;
    tightened.legs[0].tsWeights = LsaWeights{5.0, 0.0001, 0.01};  // far tighter than the default.
    const LsaResult rTight = ComputeTraverseLeastSquares(tightened, LsaWeights{});
    REQUIRE(rTight.ok);

    // A leg the solver trusts much more should absorb LESS of the misclosure.
    CHECK(std::abs(rTight.residuals[0].distResidualFt) < std::abs(rBase.residuals[0].distResidualFt));
    CHECK(rTight.adjClosureLinear < 1e-3);  // still closes.
}

TEST_CASE("Least squares: PRISM/POLE error loosens a leg's distance weight", "[lsa][REQ-405]") {
    TraverseData baseline = MakeSquareLoop(100.10);
    const LsaResult rBase = ComputeTraverseLeastSquares(baseline, LsaWeights{});
    REQUIRE(rBase.ok);

    TraverseData loosened = MakeSquareLoop(100.10);
    loosened.legs[0].hasPrismError = true;
    loosened.legs[0].prismError = 1.0;  // much larger than the 0.02 ft default sigma.
    const LsaResult rLoose = ComputeTraverseLeastSquares(loosened, LsaWeights{});
    REQUIRE(rLoose.ok);

    // A leg the solver trusts much less should absorb MORE of the misclosure.
    CHECK(std::abs(rLoose.residuals[0].distResidualFt) > std::abs(rBase.residuals[0].distResidualFt));
    CHECK(rLoose.adjClosureLinear < 1e-3);  // still closes.
}

// ---------------------------------------------------------------------------
// GitHub issue #846 (Phase B) — MicroSurvey STAR*NET .dat import. Grammar
// verified against the STAR*NET v10 Reference Manual (Chapter 5) before
// implementing; see StarNetImport.hpp for exactly what's supported and what's
// a defined gap.
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path StarNetTestDir() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gosurvey-starnet";
    std::filesystem::create_directories(dir);
    return dir;
}

std::string WriteStarNetFixture(const std::string& name, const std::string& content) {
    const std::filesystem::path path = StarNetTestDir() / name;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
    f.close();
    return path.string();
}

}  // namespace

TEST_CASE("StarNetImport: closed-loop traverse from the sample fixture", "[starnet][REQ-406]") {
    const std::string path = std::string(GOSURVEY_TEST_DATA_DIR) + "/closed_loop_trav.dat";
    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    CHECK(err.empty());

    REQUIRE(td.legs.size() == 3);
    CHECK(td.isClosedLoop);
    CHECK(td.hasStartBearing);
    CHECK(td.startBearingDeg == Approx(270.0).margin(1e-6));  // azimuth 7->5, due west.
    CHECK(td.startEasting == Approx(5100.0));
    CHECK(td.startNorthing == Approx(5000.0));

    CHECK(td.legs[0].horizAngleDeg == Approx(100.0));
    CHECK(td.legs[0].horizDist == Approx(150.25));
    CHECK_FALSE(td.legs[0].hasTsWeights);

    CHECK(td.legs[1].horizAngleDeg == Approx(240.0));
    CHECK(td.legs[1].horizDist == Approx(200.10));

    CHECK(td.legs[2].horizAngleDeg == Approx(78.0 + 50.0 / 60.0).margin(1e-6));
    CHECK(td.legs[2].horizDist == Approx(175.0));
    REQUIRE(td.legs[2].hasTsWeights);
    CHECK(td.legs[2].tsWeights.sigmaAngleSec == Approx(3.5));
    CHECK(td.legs[2].tsWeights.sigmaDistConstFt == Approx(0.0100));

    // The closing leg's station id must equal the start station id (same monument).
    CHECK(td.legs[2].stationId == td.startStationId);

    ComputeTraverse(td);
    LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);
    CHECK(std::isfinite(r.refStdDev));
}

TEST_CASE("StarNetImport: a fixed bearing TB (N..W form) resolves without any C records", "[starnet][REQ-406]") {
    const std::string text =
        "TB N75-00-00W 'no coordinates needed\n"
        "T 1 90-00-00 100.00\n"
        "T 2 90-00-00 100.00\n"
        "TE 3\n";
    const std::string path = WriteStarNetFixture("bearing_tb.dat", text);

    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    CHECK(td.hasStartBearing);
    CHECK(td.startBearingDeg == Approx(360.0 - 75.0).margin(1e-6));
    REQUIRE(td.legs.size() == 2);
    CHECK_FALSE(td.isClosedLoop);  // TE's station ("3") != the first occupied ("1").
}

TEST_CASE("StarNetImport: a \"B=\" packed-azimuth TB is recognized", "[starnet][REQ-406]") {
    const std::string text =
        "TB B=44.1135\n"
        "T 1 90-00-00 100.00\n"
        "T 2 90-00-00 100.00\n"
        "TE 1\n";
    const std::string path = WriteStarNetFixture("packed_tb.dat", text);

    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    CHECK(td.hasStartBearing);
    CHECK(td.startBearingDeg == Approx(44.0 + 11.0 / 60.0 + 35.0 / 3600.0).margin(1e-6));
    CHECK(td.isClosedLoop);
}

TEST_CASE("StarNetImport: comments and blank lines are ignored", "[starnet][REQ-406]") {
    const std::string text =
        "# a comment\n"
        "\n"
        "TB 1\n"
        "T 1 90-00-00 100.00 # inline comment\n"
        "T 2 90-00-00 100.00\n"
        "TE 3\n";
    const std::string path = WriteStarNetFixture("comments.dat", text);

    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    REQUIRE(td.legs.size() == 2);
    const std::string shown = travcmd::Serialize(td, LsaWeights{});
    const size_t commentAt = shown.find("// a comment");
    const size_t inlineAt = shown.find("// inline comment");
    const size_t firstStn = shown.find("\nSTN ");
    const size_t secondStn = firstStn == std::string::npos ? std::string::npos : shown.find("\nSTN ", firstStn + 1);
    REQUIRE(commentAt != std::string::npos);
    REQUIRE(inlineAt != std::string::npos);
    REQUIRE(firstStn != std::string::npos);
    REQUIRE(secondStn != std::string::npos);
    CHECK(commentAt < firstStn);
    CHECK(firstStn < inlineAt);
    CHECK(inlineAt < secondStn);
}

TEST_CASE("StarNetImport: field codes and comments keep their place", "[starnet][req421]") {
    const std::string text =
        "# header note\n"
        "\n"
        "C 1 0 0 ! ! 'PIN\n"
        "# between\n"
        "C 2 0 100 * *\n"
        "D 1-2 100 'FENCE CORNER\n";
    const std::string path = WriteStarNetFixture("codes.dat", text);
    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    const std::string shown = travcmd::Serialize(td, LsaWeights{});
    const size_t headerAt = shown.find("// header note");
    const size_t betweenAt = shown.find("// between");
    const size_t pinAt = shown.find("'PIN");
    const size_t fenceAt = shown.find("'FENCE CORNER");
    REQUIRE(headerAt != std::string::npos);
    REQUIRE(betweenAt != std::string::npos);
    REQUIRE(pinAt != std::string::npos);
    REQUIRE(fenceAt != std::string::npos);
    CHECK(headerAt < pinAt);
    CHECK(pinAt < betweenAt);
    const size_t headerEnd = shown.find('\n', headerAt);
    REQUIRE(headerEnd != std::string::npos);
    CHECK(headerEnd + 1 < shown.size());
    CHECK(shown[headerEnd + 1] == '\n');
    CHECK(betweenAt < fenceAt);

    TraverseData roundTrip;
    LsaWeights weights;
    REQUIRE(travcmd::Parse(shown, roundTrip, weights, err));
    const std::string again = travcmd::Serialize(roundTrip, weights);
    CHECK(again.find("// header note") != std::string::npos);
    CHECK(again.find("'PIN") != std::string::npos);
    CHECK(again.find("'FENCE CORNER") != std::string::npos);
    CHECK(again.find("// header note") < again.find("'PIN"));
}

TEST_CASE("StarNetImport: an unsupported record type is reported, not dropped", "[starnet][REQ-406]") {
    const std::string text =
        "TB 1\n"
        "SS 1-2-3 45-00-00 10\n"  // a sideshot is still not part of the adjustment.
        "T 1 90-00-00 100.00\n"
        "TE 2\n";
    const std::string path = WriteStarNetFixture("unsupported_record.dat", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(StarNetImport(path.c_str(), td, err));
    CHECK(err.find("unsupported") != std::string::npos);
}

TEST_CASE("StarNetImport: a network of A and D lines adjusts", "[starnet][req419]") {
    const std::string text =
        "C 1 0 0 ! !\n"
        "C 2 0 100 ! !\n"
        "C 3 100 100 * *\n"
        "A 2-1-3 90-00-00\n"
        "D 1-2 100\n"
        "D 2-3 100\n"
        "D 1-3 141.421356\n";
    const std::string path = WriteStarNetFixture("network_ad.dat", text);
    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    CHECK(td.networkAngles.size() == 1);
    CHECK(td.networkDists.size() == 3);
    const LsaResult result = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(result.ok);
    int freeId = -1;
    for (const NetworkStation& station : td.networkStations)
        if (station.easting.fix == NetworkFix::Free) freeId = station.id;
    REQUIRE(freeId >= 0);
    bool found = false;
    for (size_t i = 0; i < result.stationIds.size(); ++i) {
        if (result.stationIds[i] != freeId) continue;
        CHECK(result.adjEasting[i] == Approx(100.0).margin(0.05));
        CHECK(result.adjNorthing[i] == Approx(100.0).margin(0.05));
        found = true;
    }
    CHECK(found);
}

TEST_CASE("A zenith angle holds a level sight", "[lsa][req419]") {
    const char* text =
        "CTRL 1 0 0 100 ! ! !\n"
        "CTRL 2 100 0 100 ! ! *\n"
        "DIST 1 2 100\n"
        "ZENITH 1 2 90\n";
    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    const LsaResult result = ComputeTraverseLeastSquares(td, weights);
    REQUIRE(result.ok);
    bool found = false;
    for (size_t i = 0; i < result.stationIds.size(); ++i) {
        if (result.stationIds[i] != 2) continue;
        CHECK(result.adjElevation[i] == Approx(100.0).margin(1e-3));
        found = true;
    }
    CHECK(found);
    bool sawZenith = false;
    for (const LsaMeasResidual& row : result.measResiduals)
        if (row.kind == LsaMeasKind::Zenith) sawZenith = true;
    CHECK(sawZenith);
}

TEST_CASE("StarNetImport: glued !!! fixes a 3D coordinate", "[starnet][req419]") {
    const std::string text =
        ".Units Meters\n"
        "C 100 1000 500 100 !!!\n"
        "B 100-103 0-00-00 !\n"
        ".dat off\n"
        "DM 999 10-00-00 5 90-00-00\n"
        ".dat on\n"
        "DB 100\n"
        "DM 103 0-00-00 10 90-00-00 1.5/1.5\n"
        "DE\n";
    const std::string path = WriteStarNetFixture("glued_fix_direction.dat", text);
    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    REQUIRE(td.networkDirections.size() == 1);
    CHECK(td.networkDirections[0].toId != 0);
    bool sawFixed = false;
    for (const NetworkStation& station : td.networkStations) {
        if (station.easting.fix == NetworkFix::Fixed && station.northing.fix == NetworkFix::Fixed &&
            station.elevation.present && station.elevation.fix == NetworkFix::Fixed)
            sawFixed = true;
    }
    CHECK(sawFixed);
    CHECK(td.networkDists.size() == 1);
    CHECK(td.networkDists[0].distance == Approx(10.0 / 0.3048).epsilon(1e-6));
    const std::string shown = travcmd::Serialize(td, LsaWeights{});
    CHECK(shown.find(".dat off") == std::string::npos);
    CHECK(shown.find("DIR ") != std::string::npos);
}

TEST_CASE("StarNetImport: field direction-set file loads", "[starnet][field]") {
    const char* path =
        "C:\\Users\\chetj\\OneDrive\\Desktop\\Example Field Job w Starnet\\Starnet Data\\260214 TRAVERSE.dat";
    if (!std::filesystem::exists(path)) return;
    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path, td, err));
    CHECK(td.networkDirections.size() > 10);
    bool sawMetersAsFeet = false;
    for (const NetworkStation& station : td.networkStations) {
        if (station.easting.fix == NetworkFix::Fixed && station.easting.value > 1000.0)
            sawMetersAsFeet = true;
    }
    CHECK(sawMetersAsFeet);
    const std::string shown = travcmd::Serialize(td, LsaWeights{});
    const std::string twice = travcmd::AlignColumns(shown);
    CHECK(twice == shown);
    CHECK(std::count(shown.begin(), shown.end(), '\n') == std::count(twice.begin(), twice.end(), '\n'));
    CHECK(shown.find("CTRL") != std::string::npos);
    CHECK(shown.find("DIR") != std::string::npos);
    CHECK(shown.find("'TH CN") != std::string::npos);
    CHECK(shown.find("// STAR*JOBXML") != std::string::npos);
    CHECK(shown.find("// ORIGINAL UNALTERED JXL") != std::string::npos);
    CHECK(shown.find("// STAR*JOBXML") < shown.find("CTRL "));
    TraverseData roundTrip;
    LsaWeights roundWeights;
    REQUIRE(travcmd::Parse(shown, roundTrip, roundWeights, err));
    CHECK(roundTrip.networkDirections.size() == td.networkDirections.size());
    CHECK(roundTrip.networkStations.size() == td.networkStations.size());
    const LsaResult result = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(result.ok);
}

TEST_CASE("StarNetImport: a skipped inline option becomes an editor comment", "[starnet][REQ-406]") {
    const std::string text =
        ".INCLUDE other.dat\n"
        "TB 1\n"
        "T 1 90-00-00 100.00\n"
        "TE 2\n";
    const std::string path = WriteStarNetFixture("unsupported_option.dat", text);

    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    REQUIRE(td.importerSkippedInlineOptions.size() == 1);
    CHECK(td.importerSkippedInlineOptions[0].find(".INCLUDE") != std::string::npos);
    const std::string shown = travcmd::Serialize(td, LsaWeights{});
    CHECK(shown.find("Inline option from STAR*NET (not applied): .INCLUDE other.dat") != std::string::npos);

    TraverseData roundTrip;
    LsaWeights weights;
    REQUIRE(travcmd::Parse(shown, roundTrip, weights, err));
    REQUIRE(roundTrip.importerSkippedInlineOptions.size() == 1);
    CHECK(roundTrip.importerSkippedInlineOptions[0] == ".INCLUDE other.dat");
}

TEST_CASE("StarNetImport: a second TB (interconnected traverses) is rejected", "[starnet][REQ-406]") {
    const std::string text =
        "TB 1\n"
        "T 1 90-00-00 100.00\n"
        "TE 2\n"
        "TB 2\n"
        "T 2 90-00-00 100.00\n"
        "TE 3\n";
    const std::string path = WriteStarNetFixture("second_tb.dat", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(StarNetImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("StarNetImport: a TE with a closing angle is rejected", "[starnet][REQ-406]") {
    const std::string text =
        "TB 1\n"
        "T 1 90-00-00 100.00\n"
        "TE 2 125-13-26 24\n";
    const std::string path = WriteStarNetFixture("te_closing_angle.dat", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(StarNetImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("StarNetImport: a 3D-looking T line is rejected", "[starnet][REQ-406]") {
    const std::string text =
        "TB 1\n"
        "T 1 90-00-00 100.00 85-00-00\n"  // angle, dist, zenith — 3D, out of scope.
        "TE 2\n";
    const std::string path = WriteStarNetFixture("threed_t.dat", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(StarNetImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("StarNetImport: no TB...TE traverse is reported", "[starnet][REQ-406]") {
    const std::string text = "C 1 1000 1000\n";
    const std::string path = WriteStarNetFixture("no_traverse.dat", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(StarNetImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("StarNetImport: a missing file fails with a reason", "[starnet][REQ-406]") {
    TraverseData td;
    std::string err;
    CHECK_FALSE(StarNetImport((StarNetTestDir() / "does-not-exist.dat").string().c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("StarNetImport: .ORDER EN changes coordinate interpretation", "[starnet][REQ-406]") {
    const std::string text =
        ".ORDER EN\n"
        "C 1 5100.0000 5000.0000\n"  // E then N under .ORDER EN.
        "C 2 5000.0000 5000.0000\n"
        "TB 2\n"
        "T 1 90-00-00 100.00\n"
        "TE 3\n";
    const std::string path = WriteStarNetFixture("order_en.dat", text);

    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    CHECK(td.startEasting == Approx(5100.0));
    CHECK(td.startNorthing == Approx(5000.0));
}

TEST_CASE("StarNetImport: a C line's elevation sets the start elevation", "[starnet][REQ-406]") {
    const std::string text =
        "C 1 5000.0000 5000.0000 123.4500\n"
        "C 2 5000.0000 5100.0000\n"
        "TB 2\n"
        "T 1 90-00-00 100.00\n"
        "TE 2\n";
    const std::string path = WriteStarNetFixture("elevation.dat", text);

    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    CHECK(td.startElevation == Approx(123.45));
}

// ---------------------------------------------------------------------------
// GitHub issue #847 — GPS vector ("G0"-"G3") records in a StarNet file, and
// their resolution in ComputeTraverse via a local ENU tangent plane. See
// StarNetImport.hpp for exactly what's supported (and why G4/STDERRCORR are
// defined gaps, not guesses).
// ---------------------------------------------------------------------------

TEST_CASE("StarNetImport: a G1/G2/G3 GPS vector tied to the start station parses", "[starnet][REQ-416]") {
    const std::string text =
        "C 1 5000.0000 5000.0000\n"
        "C 2 5000.0000 5100.0000\n"
        "TB 2\n"
        "T 1 90-00-00 100.00\n"
        "TE 2\n"
        ".GPS WEIGHT COVARIANCE\n"
        "G0 'V1 comment\n"
        "G1 1-9 17.279000 -2.809000 -10.433000 1.630/1.630\n"
        "G2 1.823843000000E-002 3.938653000000E-002 1.501757000000E-002\n"
        "G3 -1.432300000000E-004 1.041400000000E-004 -2.265200000000E-004\n";
    const std::string path = WriteStarNetFixture("gps_vector.dat", text);

    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    CHECK(err.empty());
    REQUIRE(td.gpsVectors.size() == 1);

    const auto& gv = td.gpsVectors[0];
    CHECK(gv.fromName == "1");
    CHECK(gv.toName == "9");
    CHECK(gv.fromStationId == td.startStationId);
    CHECK(gv.deltaXEcefM == Approx(17.279000));
    CHECK(gv.deltaYEcefM == Approx(-2.809000));
    CHECK(gv.deltaZEcefM == Approx(-10.433000));
    REQUIRE(gv.hasCovariance);
    CHECK(gv.varXM2 == Approx(1.823843e-002));
    CHECK(gv.varYM2 == Approx(3.938653e-002));
    CHECK(gv.varZM2 == Approx(1.501757e-002));
    CHECK(gv.covXYM2 == Approx(-1.4323e-004));
    CHECK(gv.covXZM2 == Approx(1.0414e-004));
    CHECK(gv.covYZM2 == Approx(-2.2652e-004));
}

TEST_CASE("StarNetImport: a G1 without a preceding .GPS WEIGHT COVARIANCE is rejected",
          "[starnet][REQ-416]") {
    const std::string text =
        "TB N75-00-00W\n"
        "T 1 90-00-00 100.00\n"
        "TE 2\n"
        "G1 1-9 17.279 -2.809 -10.433\n";
    const std::string path = WriteStarNetFixture("gps_no_mode.dat", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(StarNetImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("StarNetImport: \".GPS WEIGHT STDERRCORR\" is rejected", "[starnet][REQ-416]") {
    const std::string text =
        "TB N75-00-00W\n"
        "T 1 90-00-00 100.00\n"
        "TE 2\n"
        ".GPS WEIGHT STDERRCORR\n"
        "G1 1-9 17.279 -2.809 -10.433\n";
    const std::string path = WriteStarNetFixture("gps_stderrcorr.dat", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(StarNetImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("StarNetImport: a G4 record is rejected", "[starnet][REQ-416]") {
    const std::string text =
        "TB N75-00-00W\n"
        "T 1 90-00-00 100.00\n"
        "TE 2\n"
        ".GPS WEIGHT COVARIANCE\n"
        "G1 1-9 17.279 -2.809 -10.433\n"
        "G4 something\n";
    const std::string path = WriteStarNetFixture("gps_g4.dat", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(StarNetImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("StarNetImport: a G1 with no covariance still parses (no G2/G3)", "[starnet][REQ-416]") {
    const std::string text =
        "TB N75-00-00W\n"
        "T 1 90-00-00 100.00\n"
        "TE 2\n"
        ".GPS WEIGHT COVARIANCE\n"
        "G1 1-9 17.279 -2.809 -10.433\n";
    const std::string path = WriteStarNetFixture("gps_no_cov.dat", text);

    TraverseData td;
    std::string err;
    REQUIRE(StarNetImport(path.c_str(), td, err));
    REQUIRE(td.gpsVectors.size() == 1);
    CHECK_FALSE(td.gpsVectors[0].hasCovariance);
}

TEST_CASE("ComputeTraverse: a GPS vector tied to the start station resolves via local ENU",
          "[starnet][REQ-416]") {
    TraverseData td;
    td.startStationId = 1;
    td.startEasting = 0.0;
    td.startNorthing = 0.0;
    td.startElevation = 0.0;
    td.hasStartBearing = true;  // required to reach the GPS-vector pass at all.
    td.startBearingDeg = 0.0;
    td.hasGpsOrigin = true;
    td.gpsOriginLatDeg = 0.0;
    td.gpsOriginLonDeg = 0.0;  // ENU axes align with ECEF Y/Z/X here.

    TraverseGpsVector gv;
    gv.fromStationId = 1;
    gv.toStationId = 2;
    gv.deltaXEcefM = 10.0;   // -> Up
    gv.deltaYEcefM = 20.0;   // -> East
    gv.deltaZEcefM = 30.0;   // -> North
    td.gpsVectors.push_back(gv);

    ComputeTraverse(td);

    REQUIRE(td.gpsVectors.size() == 1);
    const auto& r = td.gpsVectors[0];
    CHECK(r.computed);
    CHECK(r.errorMsg.empty());
    constexpr double kMetersToFeet = 1.0 / 0.3048;
    CHECK(r.computedDeltaEastFt == Approx(20.0 * kMetersToFeet));
    CHECK(r.computedDeltaNorthFt == Approx(30.0 * kMetersToFeet));
    CHECK(r.computedDeltaUpFt == Approx(10.0 * kMetersToFeet));
    CHECK(r.computedToEasting == Approx(20.0 * kMetersToFeet));
    CHECK(r.computedToNorthing == Approx(30.0 * kMetersToFeet));
    CHECK(r.computedToElevation == Approx(10.0 * kMetersToFeet));
}

TEST_CASE("ComputeTraverse: a GPS vector chained off a resolved GPS station also resolves",
          "[starnet][REQ-416]") {
    TraverseData td;
    td.startStationId = 1;
    td.hasStartBearing = true;
    td.startBearingDeg = 0.0;
    td.hasGpsOrigin = true;
    td.gpsOriginLatDeg = 0.0;
    td.gpsOriginLonDeg = 0.0;

    TraverseGpsVector gv1;
    gv1.fromStationId = 1;
    gv1.toStationId = 2;
    gv1.deltaYEcefM = 20.0;  // East
    td.gpsVectors.push_back(gv1);

    TraverseGpsVector gv2;
    gv2.fromStationId = 2;
    gv2.toStationId = 3;
    gv2.deltaYEcefM = 5.0;
    td.gpsVectors.push_back(gv2);

    ComputeTraverse(td);

    REQUIRE(td.gpsVectors.size() == 2);
    CHECK(td.gpsVectors[0].computed);
    CHECK(td.gpsVectors[1].computed);
    constexpr double kMetersToFeet = 1.0 / 0.3048;
    CHECK(td.gpsVectors[1].computedToEasting == Approx(25.0 * kMetersToFeet));
}

TEST_CASE("ComputeTraverse: a GPS vector with no known origin station is left unresolved",
          "[starnet][REQ-416]") {
    TraverseData td;
    td.startStationId = 1;
    td.hasStartBearing = true;
    td.hasGpsOrigin = true;

    TraverseGpsVector gv;
    gv.fromStationId = 99;  // never resolved by anything.
    gv.toStationId = 100;
    td.gpsVectors.push_back(gv);

    ComputeTraverse(td);

    REQUIRE(td.gpsVectors.size() == 1);
    CHECK_FALSE(td.gpsVectors[0].computed);
    CHECK_FALSE(td.gpsVectors[0].errorMsg.empty());
}

TEST_CASE("ComputeTraverse: a GPS vector with no GPS origin set is left unresolved",
          "[starnet][REQ-416]") {
    TraverseData td;
    td.startStationId = 1;
    td.hasStartBearing = true;
    td.hasGpsOrigin = false;  // not supplied by the user.

    TraverseGpsVector gv;
    gv.fromStationId = 1;
    gv.toStationId = 2;
    td.gpsVectors.push_back(gv);

    ComputeTraverse(td);

    REQUIRE(td.gpsVectors.size() == 1);
    CHECK_FALSE(td.gpsVectors[0].computed);
    CHECK_FALSE(td.gpsVectors[0].errorMsg.empty());
}

// ---------------------------------------------------------------------------
// GitHub issue #857 (REQ-417) — GPS vectors folded into the least-squares
// adjustment. "Full 3D, fully correlated" per the recorded decision: a usable
// GPS vector's ECEF covariance is rotated into ENU and inverted into a full
// 3x3 weight block, elevation becomes a third unknown per free station, and a
// vector may tie in a station outside the traverse loop entirely. When no
// usable vector is present the adjustment is untouched (2D, no elevation).
// ---------------------------------------------------------------------------

TEST_CASE("Least squares: a GPS vector tying two loop stations solves elevation too",
          "[lsa][REQ-417]") {
    TraverseData td = MakeSquareLoop();  // perfect (flat) loop, station ids 1,2,3,0.
    // Simulate every leg having actually carried a (flat, dz=0) vertical angle
    // in the field, so elevation is well-determined around the loop — a GPS
    // vector alone cannot anchor every station's elevation by itself (it only
    // ties the two stations it spans), matching how a real job would also have
    // zenith/vertical angles on its total-station legs.
    for (TraverseLeg& leg : td.legs) leg.hasVertAngle = true;  // computedDeltaZ is already 0.
    td.hasGpsOrigin = true;
    td.gpsOriginLatDeg = 0.0;
    td.gpsOriginLonDeg = 0.0;  // ENU: East=ECEF Y, North=ECEF Z, Up=ECEF X.

    // Tie station 1 -> station 2 with a vector consistent with their own
    // traverse-derived separation (so it reinforces rather than fights the
    // existing geometry) and a tight covariance.
    const double trueEastFt  = td.legs[1].computedEasting  - td.legs[0].computedEasting;
    const double trueNorthFt = td.legs[1].computedNorthing - td.legs[0].computedNorthing;
    constexpr double kFeetToMeters = 0.3048;

    TraverseGpsVector gv;
    gv.fromStationId = 1;
    gv.toStationId = 2;
    gv.deltaYEcefM = trueEastFt * kFeetToMeters;
    gv.deltaZEcefM = trueNorthFt * kFeetToMeters;
    gv.deltaXEcefM = 0.0;
    gv.hasCovariance = true;
    gv.varXM2 = gv.varYM2 = gv.varZM2 = 0.0004;  // ~0.02 m sigma, diagonal.
    td.gpsVectors.push_back(gv);

    ComputeTraverse(td);
    REQUIRE(td.gpsVectors[0].computed);

    LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);
    CHECK(r.usedGpsVectors);
    CHECK(r.gpsVectorsUsed == 1);
    CHECK(r.gpsVectorsSkipped == 0);
    CHECK(r.unknowns == 9);         // 3 free stations (1,2,3) * 3 (E,N,Up) now.
    CHECK(r.observations == 12 + 3); // 4 legs * 3 (dist+angle+vert) + 1 GPS vector (3 comps).
    CHECK(r.redundancy == r.observations - r.unknowns);
    REQUIRE(r.adjElevation.size() == r.stationIds.size());
    REQUIRE(r.gpsResiduals.size() == 1);
    CHECK(std::isfinite(r.gpsResiduals[0].eastResidualFt));
    CHECK(std::isfinite(r.gpsResiduals[0].northResidualFt));
    CHECK(std::isfinite(r.gpsResiduals[0].upResidualFt));
    CHECK(r.adjClosureLinear < 1e-2);
}

TEST_CASE("Least squares: a GPS vector ties in a station outside the traverse loop",
          "[lsa][REQ-417]") {
    TraverseData td = MakeSquareLoop();
    for (TraverseLeg& leg : td.legs) leg.hasVertAngle = true;  // anchors 1/2/3's elevation to the start.
    td.hasGpsOrigin = true;
    td.gpsOriginLatDeg = 0.0;
    td.gpsOriginLonDeg = 0.0;

    // Station 50 is not part of the loop at all; it is reached only via a GPS
    // vector from station 1 (REQ-417: "allow new GPS-only stations").
    TraverseGpsVector gv;
    gv.fromStationId = 1;
    gv.toStationId = 50;
    gv.deltaYEcefM = 10.0;  // East, meters.
    gv.deltaZEcefM = 5.0;   // North, meters.
    gv.deltaXEcefM = 1.0;   // Up, meters.
    gv.hasCovariance = true;
    gv.varXM2 = gv.varYM2 = gv.varZM2 = 0.0004;
    td.gpsVectors.push_back(gv);

    ComputeTraverse(td);
    REQUIRE(td.gpsVectors[0].computed);

    LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);
    CHECK(r.usedGpsVectors);
    CHECK(r.unknowns == 12);  // 4 free stations (1,2,3,50) * 3.

    bool found50 = false;
    for (size_t s = 0; s < r.stationIds.size(); ++s) {
        if (r.stationIds[s] == 50) {
            found50 = true;
            CHECK(std::isfinite(r.adjEasting[s]));
            CHECK(std::isfinite(r.adjNorthing[s]));
            CHECK(std::isfinite(r.adjElevation[s]));
        }
    }
    CHECK(found50);
}

TEST_CASE("Least squares: a GPS vector without covariance is excluded, not guessed",
          "[lsa][REQ-417]") {
    TraverseData td = MakeSquareLoop();
    td.hasGpsOrigin = true;
    td.gpsOriginLatDeg = 0.0;
    td.gpsOriginLonDeg = 0.0;

    TraverseGpsVector gv;
    gv.fromStationId = 1;
    gv.toStationId = 2;
    gv.deltaYEcefM = 30.0;
    gv.hasCovariance = false;  // no G2/G3 — StarNet gave no error model.
    td.gpsVectors.push_back(gv);

    ComputeTraverse(td);
    REQUIRE(td.gpsVectors[0].computed);  // resolves geometrically...

    LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);
    CHECK_FALSE(r.usedGpsVectors);   // ...but is not usable for weighting.
    CHECK(r.gpsVectorsUsed == 0);
    CHECK(r.gpsVectorsSkipped == 1);
    CHECK(r.unknowns == 6);          // falls back to the original 2D (no elevation) behavior.
}

TEST_CASE("Least squares: with no GPS vectors, behavior is unchanged (2D, no elevation)",
          "[lsa][REQ-417]") {
    TraverseData td = MakeSquareLoop();
    LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);
    CHECK_FALSE(r.usedGpsVectors);
    CHECK(r.gpsResiduals.empty());
    CHECK(r.unknowns == 6);
    CHECK(r.observations == 8);
    for (double z : r.adjElevation) CHECK(z == Approx(0.0));
}

// ---------------------------------------------------------------------------
// GitHub issue #846 (Phase B) — Carlson SurvCE/SurvPC .rw5 import. Grounded
// in Carlson's official RW5 v3.03 reference; see Rw5Import.hpp for exactly
// what's supported, ignored by design, and a defined gap.
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path Rw5TestDir() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gosurvey-rw5";
    std::filesystem::create_directories(dir);
    return dir;
}

std::string WriteRw5Fixture(const std::string& name, const std::string& content) {
    const std::filesystem::path path = Rw5TestDir() / name;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
    f.close();
    return path.string();
}

}  // namespace

TEST_CASE("Rw5Import: closed-loop traverse from the sample fixture", "[rw5][REQ-407]") {
    const std::string path = std::string(GOSURVEY_TEST_DATA_DIR) + "/closed_loop_trav.rw5";
    TraverseData td;
    std::string err;
    REQUIRE(Rw5Import(path.c_str(), td, err));
    CHECK(err.empty());

    REQUIRE(td.legs.size() == 3);
    CHECK(td.isClosedLoop);
    CHECK(td.hasStartBearing);
    CHECK(td.startBearingDeg == Approx(270.0));
    CHECK(td.startEasting == Approx(5100.0));
    CHECK(td.startNorthing == Approx(5000.0));

    CHECK(td.legs[0].horizAngleDeg == Approx(100.0));
    CHECK(td.legs[0].slopeDist == Approx(150.25));
    CHECK(td.legs[0].isZenithAngle);
    CHECK(td.legs[0].vertAngleDeg == Approx(90.0));

    CHECK(td.legs[1].horizAngleDeg == Approx(240.0));
    CHECK(td.legs[2].horizAngleDeg == Approx(78.8333).margin(1e-3));
    CHECK(td.legs[2].stationId == td.startStationId);

    ComputeTraverse(td);
    LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);
    CHECK(std::isfinite(r.refStdDev));
}

TEST_CASE("Rw5Import: AL (angle-left) is negated onto the CW convention", "[rw5][REQ-407]") {
    const std::string text =
        "OC,OP1,N 1000.00000,E 1000.00000\n"
        "BK,OP1,BP0,BS0.0000\n"
        "TR,OP1,FP2,AL90.0000,ZE90.0000,SD100.000000\n";
    const std::string path = WriteRw5Fixture("al.rw5", text);

    TraverseData td;
    std::string err;
    REQUIRE(Rw5Import(path.c_str(), td, err));
    REQUIRE(td.legs.size() == 1);
    CHECK(td.legs[0].horizAngleDeg == Approx(270.0));  // 360 - 90.
}

TEST_CASE("Rw5Import: VA (elevation angle) sets isZenithAngle false", "[rw5][REQ-407]") {
    const std::string text =
        "OC,OP1,N 1000.00000,E 1000.00000\n"
        "BK,OP1,BP0,BS0.0000\n"
        "TR,OP1,FP2,AR90.0000,VA5.0000,HD100.000000\n";
    const std::string path = WriteRw5Fixture("va.rw5", text);

    TraverseData td;
    std::string err;
    REQUIRE(Rw5Import(path.c_str(), td, err));
    REQUIRE(td.legs.size() == 1);
    CHECK_FALSE(td.legs[0].isZenithAngle);
    CHECK(td.legs[0].vertAngleDeg == Approx(5.0));
    CHECK(td.legs[0].horizDist == Approx(100.0));
    CHECK_FALSE(td.legs[0].hasSlopeDist);
}

TEST_CASE("Rw5Import: SS/BD/BR/FD/FR/OF records are ignored by design", "[rw5][REQ-407]") {
    const std::string text =
        "OC,OP1,N 1000.00000,E 1000.00000\n"
        "BK,OP1,BP0,BS0.0000\n"
        "SS,OP1,FP0,AR0.0000,ZE90.0000,SD50.000000,--backsight check\n"
        "BD,OP1,FP3,AR0.0055,ZE86.0126,SD10.320000\n"
        "OF,AR90.3333,ZE90.0000,SD25.550000\n"
        "TR,OP1,FP2,AR90.0000,ZE90.0000,SD100.000000\n";
    const std::string path = WriteRw5Fixture("ignored.rw5", text);

    TraverseData td;
    std::string err;
    REQUIRE(Rw5Import(path.c_str(), td, err));
    REQUIRE(td.legs.size() == 1);
    CHECK(td.legs[0].description == "2");
}

TEST_CASE("Rw5Import: comments and a Job/Mode header are accepted", "[rw5][REQ-407]") {
    const std::string text =
        "JB,NMSAMPLE,DT06-27-2003,TM14:21:53\n"
        "MO,AD0,UN0,SF1.00000000,EC1,EO0.0,AU0\n"
        "--a free-standing note\n"
        "OC,OP1,N 1000.00000,E 1000.00000\n"
        "BK,OP1,BP0,BS0.0000\n"
        "TR,OP1,FP2,AR90.0000,ZE90.0000,SD100.000000\n";
    const std::string path = WriteRw5Fixture("header.rw5", text);

    TraverseData td;
    std::string err;
    REQUIRE(Rw5Import(path.c_str(), td, err));
    REQUIRE(td.legs.size() == 1);
}

TEST_CASE("Rw5Import: MO declaring meters (UN1) is a defined gap", "[rw5][REQ-407]") {
    const std::string text =
        "MO,AD0,UN1,SF1.00000000,EC1,EO0.0,AU0\n"
        "OC,OP1,N 1000.00000,E 1000.00000\n"
        "BK,OP1,BP0,BS0.0000\n"
        "TR,OP1,FP2,AR90.0000,ZE90.0000,SD100.000000\n";
    const std::string path = WriteRw5Fixture("meters.rw5", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(Rw5Import(path.c_str(), td, err));
    CHECK(err.find("meters") != std::string::npos);
}

TEST_CASE("Rw5Import: GPS/GNSS records are a defined gap", "[rw5][REQ-407]") {
    const std::string text =
        "OC,OP1,N 1000.00000,E 1000.00000\n"
        "BK,OP1,BP0,BS0.0000\n"
        "TR,OP1,FP2,AR90.0000,ZE90.0000,SD100.000000\n"
        "GPS,PN701,LA42.214630920,LN-71.081409184,EL-21.8459\n";
    const std::string path = WriteRw5Fixture("gps.rw5", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(Rw5Import(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("Rw5Import: an AZ angle field is a defined gap", "[rw5][REQ-407]") {
    const std::string text =
        "OC,OP1,N 1000.00000,E 1000.00000\n"
        "BK,OP1,BP0,BS0.0000\n"
        "TR,OP1,FP2,AZ90.0000,ZE90.0000,SD100.000000\n";
    const std::string path = WriteRw5Fixture("az.rw5", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(Rw5Import(path.c_str(), td, err));
    const bool mentionsAz = (err.find("AZ") != std::string::npos) || (err.find("AR/AL") != std::string::npos);
    CHECK(mentionsAz);
}

TEST_CASE("Rw5Import: a backsight that doesn't match the traverse chain is rejected", "[rw5][REQ-407]") {
    const std::string text =
        "OC,OP1,N 1000.00000,E 1000.00000\n"
        "BK,OP1,BP0,BS0.0000\n"
        "TR,OP1,FP2,AR90.0000,ZE90.0000,SD100.000000\n"
        "OC,OP2\n"
        "BK,OP2,BP99\n"  // should be BP1 (the previous leg's occupied station).
        "TR,OP2,FP3,AR90.0000,ZE90.0000,SD100.000000\n";
    const std::string path = WriteRw5Fixture("wrong_backsight.rw5", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(Rw5Import(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("Rw5Import: a TR whose OP doesn't match the current occupy is rejected", "[rw5][REQ-407]") {
    const std::string text =
        "OC,OP1,N 1000.00000,E 1000.00000\n"
        "BK,OP1,BP0,BS0.0000\n"
        "TR,OP99,FP2,AR90.0000,ZE90.0000,SD100.000000\n";
    const std::string path = WriteRw5Fixture("wrong_op.rw5", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(Rw5Import(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("Rw5Import: no TR records is reported", "[rw5][REQ-407]") {
    const std::string text = "OC,OP1,N 1000.00000,E 1000.00000\n";
    const std::string path = WriteRw5Fixture("no_tr.rw5", text);

    TraverseData td;
    std::string err;
    CHECK_FALSE(Rw5Import(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("Rw5Import: a missing file fails with a reason", "[rw5][REQ-407]") {
    TraverseData td;
    std::string err;
    CHECK_FALSE(Rw5Import((Rw5TestDir() / "does-not-exist.rw5").string().c_str(), td, err));
    CHECK_FALSE(err.empty());
}

// ---------------------------------------------------------------------------
// GitHub issue #853 — side shots and check shots: observations from an
// already-occupied traverse station that don't advance the chain.
// ---------------------------------------------------------------------------

TEST_CASE("ComputeTraverse: a side shot from the start station reduces correctly", "[traverse][REQ-409]") {
    TraverseData td;
    td.startStationId = 1;
    td.startEasting = 1000.0;
    td.startNorthing = 1000.0;
    td.startElevation = 50.0;
    td.hasStartBearing = true;
    td.startBearingDeg = 0.0;  // backsight due north.

    TraverseSideShot ss;
    ss.atStationId = 1;
    ss.stationId = 101;
    ss.hasHorizAngle = true;
    ss.horizAngleDeg = 90.0;   // due east of the backsight.
    ss.hasHorizDist = true;
    ss.horizDist = 50.0;
    td.sideShots.push_back(ss);

    ComputeTraverse(td);

    REQUIRE(td.sideShots.size() == 1);
    const TraverseSideShot& r = td.sideShots[0];
    CHECK(r.computed);
    CHECK(r.hasSufficientData);
    CHECK(r.computedBearingDeg == Approx(90.0));
    CHECK(r.computedEasting == Approx(1050.0));
    CHECK(r.computedNorthing == Approx(1000.0));
}

TEST_CASE("ComputeTraverse: a side shot from a later leg's occupied station", "[traverse][REQ-409]") {
    TraverseData td;
    td.startStationId = 1;
    td.startEasting = 0.0;
    td.startNorthing = 0.0;
    td.hasStartBearing = true;
    td.startBearingDeg = 0.0;

    TraverseLeg leg;
    leg.stationId = 2;
    leg.hasHorizAngle = true;
    leg.horizAngleDeg = 0.0;  // straight ahead (same as backsight reversed).
    leg.hasHorizDist = true;
    leg.horizDist = 100.0;
    td.legs.push_back(leg);

    TraverseSideShot ss;
    ss.atStationId = 2;  // occupied after the leg above.
    ss.stationId = 201;
    ss.hasHorizAngle = true;
    ss.horizAngleDeg = 90.0;
    ss.hasHorizDist = true;
    ss.horizDist = 25.0;
    td.sideShots.push_back(ss);

    ComputeTraverse(td);

    REQUIRE(td.legs[0].computed);
    REQUIRE(td.sideShots.size() == 1);
    const TraverseSideShot& r = td.sideShots[0];
    CHECK(r.computed);
    // Station 2 is at (0,100), backsight bearing there = leg's fwd bearing (0) + 180 = 180.
    // Side shot bearing = 180 + 90 = 270 -> points due west.
    CHECK(r.computedBearingDeg == Approx(270.0));
    CHECK(r.computedEasting == Approx(0.0 - 25.0));
    CHECK(r.computedNorthing == Approx(100.0));
}

TEST_CASE("ComputeTraverse: a check shot reports its discrepancy from the known point", "[traverse][REQ-409]") {
    TraverseData td;
    td.startStationId = 1;
    td.startEasting = 1000.0;
    td.startNorthing = 1000.0;
    td.hasStartBearing = true;
    td.startBearingDeg = 0.0;

    TraverseSideShot ss;
    ss.atStationId = 1;
    ss.stationId = 99;
    ss.isCheckShot = true;
    ss.hasHorizAngle = true;
    ss.horizAngleDeg = 90.0;
    ss.hasHorizDist = true;
    ss.horizDist = 50.0;
    ss.hasKnownCoords = true;
    ss.knownEasting = 1053.0;   // 3 ft off from the computed 1050.
    ss.knownNorthing = 1000.0;
    td.sideShots.push_back(ss);

    ComputeTraverse(td);

    REQUIRE(td.sideShots.size() == 1);
    const TraverseSideShot& r = td.sideShots[0];
    CHECK(r.computed);
    CHECK(r.checkDiscrepancy == Approx(3.0).margin(1e-6));
}

TEST_CASE("ComputeTraverse: a side shot from an unoccupied station reports an error", "[traverse][REQ-409]") {
    TraverseData td;
    td.startStationId = 1;
    td.hasStartBearing = true;
    td.startBearingDeg = 0.0;

    TraverseSideShot ss;
    ss.atStationId = 99;  // never occupied.
    ss.stationId = 100;
    ss.hasHorizAngle = true;
    ss.horizAngleDeg = 90.0;
    ss.hasHorizDist = true;
    ss.horizDist = 50.0;
    td.sideShots.push_back(ss);

    ComputeTraverse(td);

    REQUIRE(td.sideShots.size() == 1);
    CHECK_FALSE(td.sideShots[0].computed);
    CHECK_FALSE(td.sideShots[0].errorMsg.empty());
}

TEST_CASE("TraverseCommandLang: a SIDE shot round-trips", "[traverse][REQ-409]") {
    const std::string text =
        "STATION 1 1000 1000 50\n"
        "STN 1 101 SIDE\n"
        "HANG REF 1 101 90\n"
        "HDIST 1 101 50\n"
        "DESC 1 101 Fence corner\n";

    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    REQUIRE(td.sideShots.size() == 1);
    CHECK_FALSE(td.sideShots[0].isCheckShot);
    CHECK(td.sideShots[0].atStationId == 1);
    CHECK(td.sideShots[0].stationId == 101);
    CHECK(td.sideShots[0].horizAngleDeg == Approx(90.0));
    CHECK(td.sideShots[0].horizDist == Approx(50.0));
    CHECK(td.sideShots[0].description == "Fence corner");

    // Round-trip through Serialize too.
    const std::string regenerated = travcmd::Serialize(td, weights);
    TraverseData reparsed;
    LsaWeights reparsedWeights;
    REQUIRE(travcmd::Parse(regenerated, reparsed, reparsedWeights, err));
    REQUIRE(reparsed.sideShots.size() == 1);
    CHECK(reparsed.sideShots[0].horizAngleDeg == Approx(90.0));
    CHECK(reparsed.sideShots[0].description == "Fence corner");
}

TEST_CASE("TraverseCommandLang: a CHECK shot with known coords round-trips", "[traverse][REQ-409]") {
    const std::string text =
        "STATION 1 1000 1000 50\n"
        "STN 1 99 CHECK 1053 1000\n"
        "HANG REF 1 99 90\n"
        "HDIST 1 99 50\n";

    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    REQUIRE(td.sideShots.size() == 1);
    CHECK(td.sideShots[0].isCheckShot);
    REQUIRE(td.sideShots[0].hasKnownCoords);
    CHECK(td.sideShots[0].knownEasting == Approx(1053.0));
    CHECK(td.sideShots[0].knownNorthing == Approx(1000.0));

    const std::string regenerated = travcmd::Serialize(td, weights);
    TraverseData reparsed;
    LsaWeights reparsedWeights;
    REQUIRE(travcmd::Parse(regenerated, reparsed, reparsedWeights, err));
    REQUIRE(reparsed.sideShots.size() == 1);
    CHECK(reparsed.sideShots[0].isCheckShot);
    CHECK(reparsed.sideShots[0].knownEasting == Approx(1053.0));
}

TEST_CASE("TraverseCommandLang: SIDE's AT must already be an occupied station", "[traverse][REQ-409]") {
    const std::string text =
        "STATION 1 1000 1000 50\n"
        "STN 99 101 SIDE\n";  // 99 was never occupied.

    TraverseData td;
    LsaWeights weights;
    std::string err;
    CHECK_FALSE(travcmd::Parse(text, td, weights, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("TraverseCommandLang: a side shot's HANG accepts any FROM (no chain to validate)", "[traverse][REQ-409]") {
    const std::string text =
        "STATION 1 1000 1000 50\n"
        "STN 1 2\n"
        "HANG REF 1 2 90\n"
        "HDIST 1 2 100\n"
        "STN 1 101 SIDE\n"
        "HANG 999 1 101 45\n"  // 999 is not a real station -- must be accepted anyway.
        "HDIST 1 101 20\n";

    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    REQUIRE(td.sideShots.size() == 1);
    CHECK(td.sideShots[0].horizAngleDeg == Approx(45.0));
}

TEST_CASE("TraverseCommandLang: PRISM/POLE/TSERR are rejected on a side shot", "[traverse][REQ-409]") {
    const std::string text =
        "STATION 1 1000 1000 50\n"
        "STN 1 101 SIDE\n"
        "HANG REF 1 101 45\n"
        "HDIST 1 101 20\n"
        "PRISM 1 101 0.01 0.002\n";

    TraverseData td;
    LsaWeights weights;
    std::string err;
    CHECK_FALSE(travcmd::Parse(text, td, weights, err));
    CHECK_FALSE(err.empty());
}

// ---------------------------------------------------------------------------
// GitHub issue #846 (Phase B) -- Trimble JobXML import. Grounded in Trimble's
// official JobXMLSchema-5.72.xsd; see JobXmlImport.hpp for exactly what's
// supported and what's a defined gap. Uses pugixml (third_party/pugixml).
// ---------------------------------------------------------------------------

namespace {

constexpr double kJxlMetersToFeet = 1.0 / 0.3048;

std::filesystem::path JobXmlTestDir() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gosurvey-jobxml";
    std::filesystem::create_directories(dir);
    return dir;
}

std::string WriteJobXmlFixture(const std::string& name, const std::string& content) {
    const std::filesystem::path path = JobXmlTestDir() / name;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
    f.close();
    return path.string();
}

std::string WrapJobXml(const std::string& fieldBookXml, const std::string& reductionsXml = "") {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<JOBFile jobName=\"Test\" version=\"5.72\" product=\"Trimble Access\" productVersion=\"2024.00\">\n"
        << "<FieldBook>\n" << fieldBookXml << "</FieldBook>\n"
        << "<Reductions>\n" << reductionsXml << "</Reductions>\n"
        << "<Environment></Environment>\n</JOBFile>\n";
    return out.str();
}

}  // namespace

TEST_CASE("JobXmlImport: closed-loop traverse from the sample fixture", "[jobxml][REQ-410]") {
    const std::string path = std::string(GOSURVEY_TEST_DATA_DIR) + "/closed_loop_trav.jxl";
    TraverseData td;
    std::string err;
    REQUIRE(JobXmlImport(path.c_str(), td, err));
    CHECK(err.empty());

    REQUIRE(td.legs.size() == 3);
    CHECK(td.isClosedLoop);
    CHECK(td.hasStartBearing);
    CHECK(td.startBearingDeg == Approx(270.0));

    CHECK(td.legs[0].horizAngleDeg == Approx(100.0));
    CHECK(td.legs[0].slopeDist == Approx(45.8000 * kJxlMetersToFeet));
    CHECK(td.legs[0].isZenithAngle);
    CHECK(td.legs[0].vertAngleDeg == Approx(90.0));

    CHECK(td.legs[1].horizAngleDeg == Approx(240.0));
    CHECK(td.legs[1].slopeDist == Approx(61.0000 * kJxlMetersToFeet));

    CHECK(td.legs[2].horizAngleDeg == Approx(78.8333));
    CHECK(td.legs[2].slopeDist == Approx(53.3400 * kJxlMetersToFeet));
    CHECK(td.legs[2].stationId == td.startStationId);

    ComputeTraverse(td);
    LsaResult r = ComputeTraverseLeastSquares(td, LsaWeights{});
    REQUIRE(r.ok);
    CHECK(std::isfinite(r.refStdDev));
}

TEST_CASE("JobXmlImport: distances and coordinates convert metres to feet", "[jobxml][REQ-410]") {
    const std::string path = std::string(GOSURVEY_TEST_DATA_DIR) + "/closed_loop_trav.jxl";
    TraverseData td;
    std::string err;
    REQUIRE(JobXmlImport(path.c_str(), td, err));
    CHECK(td.startEasting == Approx(1600.0 * kJxlMetersToFeet));
    CHECK(td.startNorthing == Approx(1500.0 * kJxlMetersToFeet));
}

TEST_CASE("JobXmlImport: a non-terrestrial (keyed-in) PointRecord is skipped, not rejected", "[jobxml][REQ-410]") {
    // Two StationRecords (issue #848 follow-up: a leg is a sight from one
    // setup to the very next one, so at least two setups are needed for any
    // leg to exist at all) — the thing actually under test is that the
    // keyed-in CTRL1 record is skipped rather than rejected.
    const std::string fb =
        "<PointRecord ID=\"KI1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Name>CTRL1</Name><Grid><North>100</North><East>100</East><Elevation>0</Elevation></Grid>"
        "</PointRecord>"
        "<StationRecord ID=\"ST1\" TimeStamp=\"2026-01-01T08:00:00\"><StationName>1</StationName></StationRecord>"
        "<BackBearingRecord ID=\"BB1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Station>1</Station><BackSight>0</BackSight><Face1HorizontalCircle>0</Face1HorizontalCircle>"
        "</BackBearingRecord>"
        "<PointRecord ID=\"P1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Name>2</Name><StationID>ST1</StationID><BackBearingID>BB1</BackBearingID>"
        "<Circle><HorizontalCircle>90</HorizontalCircle><VerticalCircle>90</VerticalCircle>"
        "<EDMDistance>10</EDMDistance></Circle></PointRecord>"
        "<StationRecord ID=\"ST2\" TimeStamp=\"2026-01-01T08:00:00\"><StationName>2</StationName></StationRecord>";
    const std::string path = WriteJobXmlFixture("keyedin.jxl", WrapJobXml(fb));

    TraverseData td;
    std::string err;
    REQUIRE(JobXmlImport(path.c_str(), td, err));
    REQUIRE(td.legs.size() == 1);
}

TEST_CASE("JobXmlImport: a GPS/WGS84 PointRecord is a defined gap", "[jobxml][REQ-410]") {
    const std::string fb =
        "<StationRecord ID=\"ST1\" TimeStamp=\"2026-01-01T08:00:00\"><StationName>1</StationName></StationRecord>"
        "<BackBearingRecord ID=\"BB1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Station>1</Station><BackSight>0</BackSight><Face1HorizontalCircle>0</Face1HorizontalCircle>"
        "</BackBearingRecord>"
        "<PointRecord ID=\"P1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Name>2</Name><StationID>ST1</StationID><BackBearingID>BB1</BackBearingID>"
        "<Circle><HorizontalCircle>90</HorizontalCircle><VerticalCircle>90</VerticalCircle>"
        "<EDMDistance>10</EDMDistance></Circle>"
        "<WGS84><Latitude>0</Latitude><Longitude>0</Longitude><Height>0</Height></WGS84>"
        "</PointRecord>";
    const std::string path = WriteJobXmlFixture("gps.jxl", WrapJobXml(fb));

    TraverseData td;
    std::string err;
    CHECK_FALSE(JobXmlImport(path.c_str(), td, err));
    CHECK(err.find("WGS84") != std::string::npos);
}

TEST_CASE("JobXmlImport: a backsight that doesn't match the traverse chain is rejected", "[jobxml][REQ-410]") {
    // "3" must itself be promoted to a backbone station (a 3rd StationRecord)
    // for its backsight to be chain-checked at all — the setup SEQUENCE is what
    // defines the chain now (issue #848 follow-up: Trimble's own real exports
    // mix side/check shots into every setup, so "every PointRecord is a leg"
    // no longer holds; only a sight from one setup to the very next one is).
    const std::string fb =
        "<StationRecord ID=\"ST1\" TimeStamp=\"2026-01-01T08:00:00\"><StationName>1</StationName></StationRecord>"
        "<BackBearingRecord ID=\"BB1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Station>1</Station><BackSight>0</BackSight><Face1HorizontalCircle>0</Face1HorizontalCircle>"
        "</BackBearingRecord>"
        "<PointRecord ID=\"P1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Name>2</Name><StationID>ST1</StationID><BackBearingID>BB1</BackBearingID>"
        "<Circle><HorizontalCircle>90</HorizontalCircle><VerticalCircle>90</VerticalCircle>"
        "<EDMDistance>10</EDMDistance></Circle></PointRecord>"
        "<StationRecord ID=\"ST2\" TimeStamp=\"2026-01-01T08:00:00\"><StationName>2</StationName></StationRecord>"
        "<BackBearingRecord ID=\"BB2\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Station>2</Station><BackSight>99</BackSight><Face1HorizontalCircle>0</Face1HorizontalCircle>"
        "</BackBearingRecord>"
        "<PointRecord ID=\"P2\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Name>3</Name><StationID>ST2</StationID><BackBearingID>BB2</BackBearingID>"
        "<Circle><HorizontalCircle>90</HorizontalCircle><VerticalCircle>90</VerticalCircle>"
        "<EDMDistance>10</EDMDistance></Circle></PointRecord>"
        "<StationRecord ID=\"ST3\" TimeStamp=\"2026-01-01T08:00:00\"><StationName>3</StationName></StationRecord>";
    const std::string path = WriteJobXmlFixture("wrong_backsight.jxl", WrapJobXml(fb));

    TraverseData td;
    std::string err;
    CHECK_FALSE(JobXmlImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("JobXmlImport: a PointRecord occupying the wrong station is rejected", "[jobxml][REQ-410]") {
    // The first leg's occupied station is self-defining (nothing to check it
    // against yet, same as StarNetImport/Rw5Import) -- the chain only becomes
    // checkable from the SECOND leg on, when it must occupy the first leg's
    // own foresight ("2") rather than some other station ("99").
    const std::string fb =
        "<StationRecord ID=\"ST1\" TimeStamp=\"2026-01-01T08:00:00\"><StationName>1</StationName></StationRecord>"
        "<BackBearingRecord ID=\"BB1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Station>1</Station><BackSight>0</BackSight><Face1HorizontalCircle>0</Face1HorizontalCircle>"
        "</BackBearingRecord>"
        "<PointRecord ID=\"P1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Name>2</Name><StationID>ST1</StationID><BackBearingID>BB1</BackBearingID>"
        "<Circle><HorizontalCircle>90</HorizontalCircle><VerticalCircle>90</VerticalCircle>"
        "<EDMDistance>10</EDMDistance></Circle></PointRecord>"
        "<StationRecord ID=\"ST2\" TimeStamp=\"2026-01-01T08:00:00\"><StationName>99</StationName></StationRecord>"
        "<BackBearingRecord ID=\"BB2\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Station>99</Station><BackSight>1</BackSight><Face1HorizontalCircle>0</Face1HorizontalCircle>"
        "</BackBearingRecord>"
        "<PointRecord ID=\"P2\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Name>3</Name><StationID>ST2</StationID><BackBearingID>BB2</BackBearingID>"
        "<Circle><HorizontalCircle>90</HorizontalCircle><VerticalCircle>90</VerticalCircle>"
        "<EDMDistance>10</EDMDistance></Circle></PointRecord>";
    const std::string path = WriteJobXmlFixture("wrong_station.jxl", WrapJobXml(fb));

    TraverseData td;
    std::string err;
    CHECK_FALSE(JobXmlImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("JobXmlImport: no terrestrial observations is reported", "[jobxml][REQ-410]") {
    const std::string fb =
        "<PointRecord ID=\"KI1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Name>CTRL1</Name><Grid><North>100</North><East>100</East><Elevation>0</Elevation></Grid>"
        "</PointRecord>";
    const std::string path = WriteJobXmlFixture("no_obs.jxl", WrapJobXml(fb));

    TraverseData td;
    std::string err;
    CHECK_FALSE(JobXmlImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("JobXmlImport: not a JobXML file is reported", "[jobxml][REQ-410]") {
    const std::string path = WriteJobXmlFixture("notjobxml.jxl", "<?xml version=\"1.0\"?><NotAJob/>");

    TraverseData td;
    std::string err;
    CHECK_FALSE(JobXmlImport(path.c_str(), td, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("JobXmlImport: an MTA reading is already a turned angle, not an azimuth needing the "
         "backsight subtracted again", "[jobxml][REQ-410]") {
    // Regression for a real-world bug (issue #848 follow-up): a raw <Circle>
    // face reading in a SetHorizontalCircleMode=SetToAzimuth job IS azimuth-
    // calibrated and needs the backsight's Face1HorizontalCircle subtracted to
    // get a turned angle (every other test here has Face1HorizontalCircle=0,
    // which can't catch a bug in that subtraction) -- but Trimble's own
    // multi-round MTA average is computed by reducing each round to a turned
    // angle FIRST, so <MTA><HorizontalCircle> already IS the turned angle.
    // Subtracting a non-zero backsight circle from it a second time silently
    // rotated every leg after the first by that station's own backsight
    // azimuth. BB1's Face1HorizontalCircle=90 here (nonzero, unlike every
    // other fixture in this file) specifically exercises that path.
    const std::string fb =
        "<StationRecord ID=\"ST1\" TimeStamp=\"2026-01-01T08:00:00\"><StationName>1</StationName></StationRecord>"
        "<BackBearingRecord ID=\"BB1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Station>1</Station><BackSight>0</BackSight><Face1HorizontalCircle>90</Face1HorizontalCircle>"
        "</BackBearingRecord>"
        "<PointRecord ID=\"P1\" TimeStamp=\"2026-01-01T08:00:00\">"
        "<Name>2</Name><Code>TH</Code><Method>MeanTurnedAngle</Method><Classification>MTA</Classification>"
        "<StationID>ST1</StationID><BackBearingID>BB1</BackBearingID>"
        "<MTA><HorizontalCircle>50</HorizontalCircle><VerticalCircle>90</VerticalCircle>"
        "<EDMDistance>10</EDMDistance></MTA></PointRecord>"
        "<StationRecord ID=\"ST2\" TimeStamp=\"2026-01-01T08:00:00\"><StationName>2</StationName></StationRecord>";
    const std::string path = WriteJobXmlFixture("mta_turned_angle.jxl", WrapJobXml(fb));

    TraverseData td;
    std::string err;
    REQUIRE(JobXmlImport(path.c_str(), td, err));
    REQUIRE(td.legs.size() == 1);
    CHECK(td.legs[0].horizAngleDeg == Approx(50.0));
}

TEST_CASE("JobXmlImport: a missing file fails with a reason", "[jobxml][REQ-410]") {
    TraverseData td;
    std::string err;
    CHECK_FALSE(JobXmlImport((JobXmlTestDir() / "does-not-exist.jxl").string().c_str(), td, err));
    CHECK_FALSE(err.empty());
}

// ---- REQ-422: project adjustment settings (centering error, refraction, iterations) ----

TEST_CASE("Centering error widens a short sight's angle standard error more than a long one",
         "[lsa][req422]") {
    const char* text =
        "CTRL 1 0 0 ! !\n"
        "CTRL 2 10 0 ! !\n"   // short sight: 10 ft
        "CTRL 3 1000 0 ! !\n" // long sight: 1000 ft
        "CTRL 4 10 10 * *\n"
        "CTRL 5 1000 10 * *\n"
        "DIST 1 2 10\nDIST 2 4 10\nANGLE 1 2 4 90\n"
        "DIST 1 3 1000\nDIST 3 5 10\nANGLE 1 3 5 90\n";
    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    weights.centeringInstrumentFt = 0.01;
    weights.centeringTargetFt = 0.01;
    const LsaResult result = ComputeTraverseLeastSquares(td, weights);
    REQUIRE(result.ok);
    double shortAngleStdError = 0.0, longAngleStdError = 0.0;
    for (const LsaMeasResidual& row : result.measResiduals) {
        if (row.kind != LsaMeasKind::Angle) continue;
        if (row.atStationId == 1 && row.toStationId == 4) shortAngleStdError = row.stdError;
        if (row.atStationId == 1 && row.toStationId == 5) longAngleStdError = row.stdError;
    }
    REQUIRE(shortAngleStdError > 0.0);
    REQUIRE(longAngleStdError > 0.0);
    CHECK(shortAngleStdError > longAngleStdError);
}

TEST_CASE("Zenith refraction correction matches the curvature+refraction formula", "[lsa][req422]") {
    // Two long sights to a free-elevation station: the refraction coefficient should change
    // the adjusted elevation by the curvature+refraction correction, which this test computes
    // independently from the same formula the solver uses (REQ-422 statement 4).
    const char* text =
        "CTRL 1 0 0 0 ! ! !\n"
        "CTRL 2 0 5000 0 ! ! !\n"
        "CTRL 3 5000 0 5 ! ! *\n"  // easting/northing fixed, elevation free.
        "ZENITH 1 3 89\n"
        "ZENITH 2 3 89\n";
    auto adjustedElevation = [&](double k) -> double {
        TraverseData td;
        LsaWeights weights;
        std::string err;
        REQUIRE(travcmd::Parse(text, td, weights, err));
        weights.refractionCoefficient = k;
        const LsaResult result = ComputeTraverseLeastSquares(td, weights);
        INFO(result.message);
        REQUIRE(result.ok);
        for (size_t i = 0; i < result.stationIds.size(); ++i)
            if (result.stationIds[i] == 3) return result.adjElevation[i];
        FAIL("station 3 not in the free-station list");
        return 0.0;
    };
    const double elevNoRefraction = adjustedElevation(1.0);   // (1-k) == 0: no correction.
    const double elevWithRefraction = adjustedElevation(0.07);
    const double kEarthRadiusFt = 20902231.0;
    // Both sights contribute their own correction; the two zeniths pull the single free
    // elevation unknown roughly equally, so the net shift is on the order of one sight's
    // correction converted through dZ -> dElev at near-level geometry (dElev ~= -S*dZ).
    const double expectedOrderOfMagnitude = (1.0 - 0.07) * 5000.0 / (2.0 * kEarthRadiusFt) * 5000.0;
    const double actualShift = std::abs(elevWithRefraction - elevNoRefraction);
    CHECK(actualShift > 0.0);
    CHECK(actualShift < expectedOrderOfMagnitude * 2.0);
}

TEST_CASE("maxIterations and convergenceLimit change the adjustment's reported iteration count",
         "[lsa][req422]") {
    const char* text =
        "CTRL 1 0 0 ! !\nCTRL 2 100 0 ! !\nCTRL 3 50 86.6025403784 * *\n"
        "DIST 1 2 100\nDIST 2 3 100\nDIST 1 3 100\nANGLE 1 2 3 -60\n";
    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    // convergenceLimit of 0 is never satisfied (maxStep is never < 0), so the loop always
    // runs to maxIterations -- directly exercising the cap.
    weights.convergenceLimit = 0.0;
    weights.maxIterations = 1;
    const LsaResult capped = ComputeTraverseLeastSquares(td, weights);
    REQUIRE(capped.ok);
    weights.maxIterations = 5;
    const LsaResult lessCapped = ComputeTraverseLeastSquares(td, weights);
    REQUIRE(lessCapped.ok);
    CHECK(lessCapped.iterations > capped.iterations);
}

TEST_CASE("Saving and reloading a .gstrav file round-trips the new settings fields", "[req422]") {
    LsaWeights w;
    w.sigmaAzimuthSec = 3.5;
    w.sigmaZenithSec = 8.0;
    w.sigmaLevelFt = 0.02;
    w.centeringInstrumentFt = 0.004;
    w.centeringTargetFt = 0.003;
    w.centeringVerticalFt = 0.002;
    w.adjustmentIs3D = true;
    w.refractionCoefficient = 0.08;
    w.convergenceLimit = 1e-7;
    w.maxIterations = 20;

    TraverseData td;
    td.startStationId = 1;
    td.importedFiles.push_back("C:/jobs/job1.dat");
    const std::string path = (std::filesystem::temp_directory_path() / "req422_roundtrip.gstrav").string();
    std::string err;
    REQUIRE(traverseio::Save(path, td, w, err));

    TraverseData loaded;
    LsaWeights loadedW;
    REQUIRE(traverseio::Load(path, loaded, loadedW, err));
    CHECK(loadedW.sigmaAzimuthSec == Approx(3.5));
    CHECK(loadedW.sigmaZenithSec == Approx(8.0));
    CHECK(loadedW.sigmaLevelFt == Approx(0.02));
    CHECK(loadedW.centeringInstrumentFt == Approx(0.004));
    CHECK(loadedW.centeringTargetFt == Approx(0.003));
    CHECK(loadedW.centeringVerticalFt == Approx(0.002));
    CHECK(loadedW.adjustmentIs3D == true);
    CHECK(loadedW.refractionCoefficient == Approx(0.08));
    CHECK(loadedW.convergenceLimit == Approx(1e-7));
    CHECK(loadedW.maxIterations == 20);
    REQUIRE(loaded.importedFiles.size() == 1);
    CHECK(loaded.importedFiles[0] == "C:/jobs/job1.dat");
    std::filesystem::remove(path);
}

// ---- REQ-423: adjustment report (summary section + export) ----

TEST_CASE("Report text lists files, then settings, then the residual listing, in order",
         "[req423]") {
    const char* text =
        "CTRL 1 0 0 ! !\nCTRL 2 100 0 ! !\nCTRL 3 50 86.6025403784 * *\n"
        "DIST 1 2 100\nDIST 2 3 100\nDIST 1 3 100\nANGLE 1 2 3 -60\n";
    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    td.importedFiles.push_back("C:/jobs/job1.dat");
    const LsaResult result = ComputeTraverseLeastSquares(td, weights);
    REQUIRE(result.ok);

    const std::string report = traverserpt::BuildReportText(td, weights, result, "Test Project", "ft");
    const size_t filesPos = report.find("Data File List");
    const size_t settingsPos = report.find("Project Option Settings");
    const size_t instrumentPos = report.find("Instrument Standard Error Settings");
    const size_t residualsPos = report.find("Measurement Residuals");
    REQUIRE(filesPos != std::string::npos);
    REQUIRE(settingsPos != std::string::npos);
    REQUIRE(instrumentPos != std::string::npos);
    REQUIRE(residualsPos != std::string::npos);
    CHECK(filesPos < settingsPos);
    CHECK(settingsPos < instrumentPos);
    CHECK(instrumentPos < residualsPos);
    CHECK(report.find("job1.dat") != std::string::npos);
    CHECK(report.find("StarNet") == std::string::npos);
    CHECK(report.find("STAR*NET") == std::string::npos);
}

TEST_CASE("A case with no imported files still produces a report", "[req423]") {
    const char* text =
        "CTRL 1 0 0 ! !\nCTRL 2 100 0 ! !\nCTRL 3 50 86.6025403784 * *\n"
        "DIST 1 2 100\nDIST 2 3 100\nDIST 1 3 100\nANGLE 1 2 3 -60\n";
    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    const LsaResult result = ComputeTraverseLeastSquares(td, weights);
    REQUIRE(result.ok);
    const std::string report = traverserpt::BuildReportText(td, weights, result, "", "ft");
    CHECK(report.find("(none imported)") != std::string::npos);
    CHECK_FALSE(report.empty());
}

TEST_CASE("Each report export produces a valid, non-empty file", "[req423]") {
    const char* text =
        "CTRL 1 0 0 ! !\nCTRL 2 100 0 ! !\nCTRL 3 50 86.6025403784 * *\n"
        "DIST 1 2 100\nDIST 2 3 100\nDIST 1 3 100\nANGLE 1 2 3 -60\n";
    TraverseData td;
    LsaWeights weights;
    std::string err;
    REQUIRE(travcmd::Parse(text, td, weights, err));
    const LsaResult result = ComputeTraverseLeastSquares(td, weights);
    REQUIRE(result.ok);
    const std::string report = traverserpt::BuildReportText(td, weights, result, "Test", "ft");

    const auto dir = std::filesystem::temp_directory_path();
    const std::string txtPath = (dir / "req423_report.txt").string();
    const std::string rtfPath = (dir / "req423_report.rtf").string();
    const std::string pdfPath = (dir / "req423_report.pdf").string();

    REQUIRE(traverserpt::ExportReportTxt(txtPath, report, err));
    REQUIRE(traverserpt::ExportReportRtf(rtfPath, report, err));
    REQUIRE(traverserpt::ExportReportPdf(pdfPath, report, err));

    REQUIRE(std::filesystem::exists(txtPath));
    REQUIRE(std::filesystem::exists(rtfPath));
    REQUIRE(std::filesystem::exists(pdfPath));
    CHECK(std::filesystem::file_size(txtPath) > 0);
    CHECK(std::filesystem::file_size(rtfPath) > 0);
    CHECK(std::filesystem::file_size(pdfPath) > 0);

    {
        std::ifstream txtIn(txtPath, std::ios::binary);
        const std::string txtContent((std::istreambuf_iterator<char>(txtIn)), std::istreambuf_iterator<char>());
        CHECK(txtContent == report);
    }
    {
        std::ifstream pdfIn(pdfPath, std::ios::binary);
        char magic[5] = {};
        pdfIn.read(magic, 4);
        CHECK(std::string(magic) == "%PDF");
    }

    std::filesystem::remove(txtPath);
    std::filesystem::remove(rtfPath);
    std::filesystem::remove(pdfPath);
}
