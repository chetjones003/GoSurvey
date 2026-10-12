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
        "# a leading comment\n"
        "\n"
        "STATION 5 100 200 10\n"
        "   # an indented comment\n"
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
