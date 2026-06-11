#pragma once

#include <string>
#include <vector>

/// One raw face-1/face-2 measurement set to a foresight, as read from a raw data
/// file (e.g. Autodesk FBK). Angles are decimal degrees; distance is slope distance.
/// A set may carry only F1, only F2, or both. The editor shows these as detail rows;
/// the reduced per-leg values live in \ref TraverseLeg.
struct TraverseMeasSet {
    int setNo = 0;            ///< 1-based set number within the leg.
    double f1HzDec = 0.0;     ///< Face-1 horizontal circle reading.
    double f1VaDec = 90.0;    ///< Face-1 zenith angle.
    double f1Sd = 0.0;        ///< Face-1 slope distance.
    bool hasF1 = false;
    double f2HzDec = 0.0;     ///< Face-2 horizontal circle reading.
    double f2VaDec = 90.0;    ///< Face-2 zenith angle.
    double f2Sd = 0.0;        ///< Face-2 slope distance.
    bool hasF2 = false;
};

/// One leg of a survey traverse.
/// Records measurements made at the instrument (FROM) station pointing to the
/// foresight (TO) station. Computed fields are filled by \ref ComputeTraverse.
struct TraverseLeg {
    int stationId = 0;           ///< Foresight station point number.
    std::string description;     ///< Station description / field notes.

    // --- Horizontal angle (° CW from backsight direction to this foresight) ---
    std::string horizAngleBuf;   ///< User input — decimal or DdMmSs (e.g. "45d30m10s").
    double horizAngleDeg = 0.0;
    bool hasHorizAngle = false;

    // Face 1 / Face 2 horizontal circle readings
    std::string face1HorizBuf;
    std::string face2HorizBuf;
    double face1HorizDeg = 0.0;
    double face2HorizDeg = 0.0;

    // --- Horizontal distance (measured directly) ---
    double horizDist = 0.0;
    bool hasHorizDist = false;

    // --- Slope distance ---
    double slopeDist = 0.0;
    bool hasSlopeDist = false;

    // --- Vertical / zenith angle ---
    std::string vertAngleBuf;    ///< User input — decimal or DdMmSs.
    double vertAngleDeg = 0.0;
    bool isZenithAngle = true;   ///< true = zenith (90° level); false = elevation from horizontal.
    bool hasVertAngle = false;

    // Face 1 / Face 2 vertical circle readings
    std::string face1VertBuf;
    std::string face2VertBuf;
    double face1VertDeg = 0.0;
    double face2VertDeg = 0.0;

    // Which faces are actually present. A foresight may carry only F1 (e.g. the
    // F2 face disabled in the field), so face averaging must not assume both.
    bool hasFace1 = false;
    bool hasFace2 = false;

    // Raw per-set F1/F2 observations from a raw-data import (FBK, RW5, …).
    // Empty for manually entered legs; the editor can expand these as detail rows.
    std::vector<TraverseMeasSet> rawSets;

    // --- Computed outputs (set by ComputeTraverse) ---
    double computedBearingDeg = 0.0;
    double computedHorizAngleDeg = 0.0;   ///< Resolved turned angle actually used (F1/F2 avg or single).
    double computedHorizDist = 0.0;
    double computedDeltaE = 0.0;
    double computedDeltaN = 0.0;
    double computedDeltaZ = 0.0;
    double computedEasting = 0.0;
    double computedNorthing = 0.0;
    double computedElevation = 0.0;
    bool computed = false;
    bool hasSufficientData = false;
    std::string errorMsg;
};

struct TraverseData {
    // --- Starting station ---
    int startStationId = 1;
    double startEasting = 0.0;
    double startNorthing = 0.0;
    double startElevation = 0.0;

    /// Orientation bearing at the start station (° CW from N).
    /// This is the bearing of the reference direction (backsight or known azimuth).
    /// The forward bearing of the first leg = startBearingDeg + HA of first leg.
    /// If you know the first leg bearing directly, set startBearingDeg to it and
    /// leave the first row's H.Angle at 0°.
    std::string startBearingBuf;
    double startBearingDeg = 0.0;
    bool hasStartBearing = false;

    std::vector<TraverseLeg> legs;

    bool useFace1Face2 = false;   ///< When true, use F1/F2 averages instead of single observations.
    bool isClosedLoop = false;    ///< When true, compute closure back to starting station.

    // --- Closure results (set by ComputeTraverse when isClosedLoop) ---
    double closureDeltaE = 0.0;       ///< End easting − start easting.
    double closureDeltaN = 0.0;       ///< End northing − start northing.
    double closureLinear = 0.0;       ///< Linear closure error sqrt(dE²+dN²).
    double closurePerimeter = 0.0;    ///< Sum of all horizontal distances.
    double closurePrecision = 0.0;    ///< 1/(perimeter/linear) — larger is better; 0 if closed perfectly.
    bool closureValid = false;
};

/// Parse angle from user string (decimal degrees or DdMmSs). Returns false on failure.
bool TraverseParseAngle(const std::string& raw, double* degreesOut);

/// Normalize angle to [0, 360).
double TraverseNormBearing(double deg);

/// Reduce slope distance to horizontal using zenith or elevation angle.
double TraverseReduceToHoriz(double slopeDist, double angleDeg, bool isZenith);

/// Recover slope distance from a horizontal distance using zenith or elevation
/// angle (the inverse of \ref TraverseReduceToHoriz). Returns 0 if the angle is
/// degenerate (sin/cos ~ 0), so a near-horizontal sight does not divide by zero.
double TraverseSlopeFromHoriz(double horizDist, double angleDeg, bool isZenith);

/// Simple descriptive statistics over a set of repeated observations (REQ-011).
struct StatSummary {
    int    n      = 0;     ///< Number of samples.
    double sum    = 0.0;   ///< Σx.
    double mean   = 0.0;   ///< Σx / n  (0 if n == 0).
    double stddev = 0.0;   ///< Sample standard deviation from the mean (n−1); 0 if n < 2.
};

/// Compute count, sum, mean, and sample standard deviation from the mean.
StatSummary ComputeStats(const std::vector<double>& samples);

/// Compute signed vertical component from slope distance and zenith or elevation angle.
double TraverseReduceToVert(double slopeDist, double angleDeg, bool isZenith);

/// Average face 1 and face 2 horizontal angles. F2 should read ~F1 ± 180°.
double TraverseAverageFaceHoriz(double face1Deg, double face2Deg);

/// Average face 1 and face 2 zenith angles. F1 + F2 should ≈ 360°.
double TraverseAverageFaceZenith(double face1Deg, double face2Deg);

/// Average face 1 and face 2 elevation angles (simple mean).
double TraverseAverageFaceElevation(double face1Deg, double face2Deg);

/// Format a bearing as "DDD°MM'SS.S\"".
std::string TraverseFormatBearing(double bearingDeg);

/// Format a delta or distance value to 4 decimal places.
std::string TraverseFormatDist(double val);

/// Compute all legs in \p td: fills computed fields and closure info.
void ComputeTraverse(TraverseData& td);
