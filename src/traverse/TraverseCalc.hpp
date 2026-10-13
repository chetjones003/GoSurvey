#pragma once

#include "TraverseLeastSquares.hpp"

#include <cstdint>
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

    // Backsight circle reading for this leg's setup (° CW). The reduced
    // horizontal angle is (foresight circle reading − backsight reading).
    // Filled by the importer; needed to re-reduce edited raw sets (REQ-018).
    double backsightCircleDeg = 0.0;
    bool hasBacksightCircle = false;

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

    // --- Per-leg equipment corrections/errors (GitHub issue #845 follow-up) ---
    // A prism constant is a real geometric correction (manufacturer's reflector
    // offset): added to the raw slope distance before it is reduced to horizontal/
    // vertical. It only applies when the leg's distance was entered as a slope
    // distance (SDIST) — a directly-entered horizontal distance has already been
    // reduced and has no slope-distance step to correct.
    double prismConstant = 0.0;
    bool   hasPrismConstant = false;
    // The remaining three are pure a-priori uncertainty contributions for this
    // leg's least-squares distance weight — there is no deterministic correction
    // for a prism's rated accuracy or a pole's centering/plumb error, matching
    // how a real geodetic adjustment package (e.g. MicroSurvey StarNet, SALSA)
    // treats them. They combine with the angle/distance a-priori sigma (below,
    // or the traverse-wide LsaWeights if this leg has none of its own) in
    // quadrature: effective_dist_const = sqrt(dist_const² + prismError² + poleError²).
    double prismError = 0.0;
    bool   hasPrismError = false;
    double poleError = 0.0;
    bool   hasPoleError = false;
    /// This leg's own equipment accuracy spec (angle σ, dist σ, dist ppm), overriding
    /// the traverse-wide LsaWeights for just this leg's observations when set.
    LsaWeights tsWeights;
    bool       hasTsWeights = false;

    // --- Computed outputs (set by ComputeTraverse) ---
    double computedBearingDeg = 0.0;
    double computedHorizAngleDeg = 0.0;   ///< Resolved turned angle actually used (F1/F2 avg or single).
    /// Reduced horizontal distance actually used for ΔE/ΔN (REQ-401): the ground distance below,
    /// scaled by TraverseData::scaleFactor when applyScaleFactor is set. Equal to
    /// computedGroundHorizDist when the option is off.
    double computedHorizDist = 0.0;
    /// Horizontal distance after slope/zenith reduction (REQ-012), before any REQ-401 scale factor.
    double computedGroundHorizDist = 0.0;
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

/// A shot taken from an already-occupied traverse station without advancing
/// the traverse chain (GitHub issue #853) — either a "side shot" (a radial
/// detail point, e.g. a fence corner) or a "check shot" (a repeat observation
/// meant to verify the setup, usually compared against an already-known
/// position). Reduced with exactly the same math as a TraverseLeg, but keyed
/// by the OCCUPIED station rather than by a foresight that continues the
/// chain, so it lives in its own flat list (\ref TraverseData::sideShots)
/// instead of nested inside a particular leg — that also makes a shot taken
/// from the very first/start station (before any leg exists) representable.
struct TraverseSideShot {
    double horizAngleDeg = 0.0;
    double horizDist = 0.0;
    double slopeDist = 0.0;
    double vertAngleDeg = 0.0;
    /// The known coordinates to check against, if given (e.g. the backsight's,
    /// or another control point's). Comparison is skipped when not supplied —
    /// the shot still computes and displays normally.
    double knownEasting = 0.0;
    double knownNorthing = 0.0;

    // --- Computed outputs (set by ComputeTraverse) ---
    double computedBearingDeg = 0.0;
    double computedHorizDist = 0.0;
    double computedDeltaE = 0.0;
    double computedDeltaN = 0.0;
    double computedDeltaZ = 0.0;
    double computedEasting = 0.0;
    double computedNorthing = 0.0;
    double computedElevation = 0.0;
    /// Linear discrepancy between computed and known coordinates — valid only
    /// when hasKnownCoords && computed.
    double checkDiscrepancy = 0.0;

    std::string description;
    std::string horizAngleBuf;
    std::string vertAngleBuf;
    std::string errorMsg;

    int atStationId = 0;         ///< The station occupied when this shot was taken.
    int stationId = 0;           ///< This shot's own point number.

    bool hasHorizAngle = false;
    bool hasHorizDist = false;
    bool hasSlopeDist = false;
    bool isZenithAngle = true;
    bool hasVertAngle = false;
    /// Marks this as a check shot rather than a new detail point.
    bool isCheckShot = false;
    bool hasKnownCoords = false;
    bool computed = false;
    bool hasSufficientData = false;
};

/// One GPS vector observation (GitHub issue #847) — the ECEF ΔX/ΔY/ΔZ baseline
/// between two stations, as read from a STAR*NET-style "G1" record, plus its
/// a-priori covariance ("G2"/"G3"). Resolved into GoSurvey's local easting/
/// northing/elevation system by \ref ComputeTraverse via a local East-North-Up
/// tangent-plane rotation anchored at \ref TraverseData::gpsOriginLatDeg /
/// \ref TraverseData::gpsOriginLonDeg (see geo::EcefDeltaToEnu) — NOT by a map
/// projection, since GoSurvey has no datum of its own; a further, optional
/// re-projection onto a map grid (UTM, State Plane, ...) is a separate step
/// (see TraverseGpsGrid.hpp), kept out of this core math so it stays free of
/// the CS-MAP dependency.
///
/// Only resolved when \ref fromName already has a known local position — the
/// traverse's start station, a computed leg station, or another already-
/// resolved GPS vector's end station. A vector hanging off a station with no
/// known position (e.g. a GPS-only sub-network with no tie to the traverse)
/// is a defined gap: left unresolved with \ref errorMsg set, never guessed at.
struct TraverseGpsVector {
    // --- ECEF baseline components (meters), fromName -> toName ---
    double deltaXEcefM = 0.0;
    double deltaYEcefM = 0.0;
    double deltaZEcefM = 0.0;

    // --- A-priori covariance of the ECEF baseline (meters²) ---
    // Required to be true covariance (StarNet's ".GPS WEIGHT COVARIANCE" mode);
    // the alternate standard-error/correlation encoding ("STDERRCORR") is a
    // defined gap — its exact field semantics could not be confirmed against
    // an authoritative source, so it is rejected by the importer rather than
    // guessed at (see StarNetImport.cpp). Not yet consumed by
    // ComputeTraverseLeastSquares (tracked separately).
    double varXM2 = 0.0, varYM2 = 0.0, varZM2 = 0.0;
    double covXYM2 = 0.0, covXZM2 = 0.0, covYZM2 = 0.0;
    bool hasCovariance = false;

    // --- Computed outputs (set by ComputeTraverse) ---
    double computedDeltaEastFt = 0.0;
    double computedDeltaNorthFt = 0.0;
    double computedDeltaUpFt = 0.0;
    double computedToEasting = 0.0;
    double computedToNorthing = 0.0;
    double computedToElevation = 0.0;
    bool computed = false;
    std::string errorMsg;

    std::string fromName;   ///< Station occupied by the GPS baseline's origin end.
    std::string toName;     ///< Station at the baseline's other end.
    int fromStationId = 0;  ///< Resolved against the same id space as TraverseLeg::stationId.
    int toStationId = 0;
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

    /// Known coordinates at the END of an OPEN traverse (the last leg's
    /// foresight station) — issue #848 follow-up. A plain open chain tied to
    /// only the start station has zero redundant observations (2 unknowns
    /// consumed per leg, nothing left over) and least squares has nothing to
    /// adjust; tying the far end to a second known control point is the
    /// standard way a real open traverse gets closure (STAR*NET's general
    /// network model lets any point carry known coordinates, not just the
    /// start). Ignored when \ref isClosedLoop is true.
    double endEasting = 0.0;
    double endNorthing = 0.0;
    double endElevation = 0.0;
    bool hasEndCoordinates = false;

    /// Side shots and check shots (GitHub issue #853) — observations from an
    /// already-occupied station that don't advance the traverse chain.
    std::vector<TraverseSideShot> sideShots;

    /// GPS vector observations (GitHub issue #847).
    std::vector<TraverseGpsVector> gpsVectors;
    /// The local tangent-plane origin used to resolve \ref gpsVectors into
    /// local easting/northing/elevation (geodetic WGS84 degrees). Must be
    /// supplied by the user — e.g. a receiver's known lat/long at one of the
    /// GPS stations — since GoSurvey has no datum/geoid model of its own to
    /// derive one from local coordinates alone.
    double gpsOriginLatDeg = 0.0;
    double gpsOriginLonDeg = 0.0;
    bool hasGpsOrigin = false;

    bool useFace1Face2 = false;   ///< When true, use F1/F2 averages instead of single observations.
    bool isClosedLoop = false;    ///< When true, compute closure back to starting station.
    bool applyCurvRefCorr = true; ///< Apply combined earth curvature + refraction correction to dZ (#826).
    double refractionCoeff = 0.14;///< Refraction coefficient k (typical range -2..+2); 0.14 = average conditions.

    /// REQ-401: which drawing-settings scale factor the UI resolves into \ref scaleFactor when
    /// \ref applyScaleFactor is on. Not interpreted by ComputeTraverse itself.
    enum class ScaleFactorKind : std::uint8_t { Combined = 0, GridOnly = 1 };
    /// REQ-401: off by default — leaves every distance/coordinate exactly as before this option
    /// existed. When on, each leg's ground horizontal distance is multiplied by \ref scaleFactor
    /// before it drives coordinates, closure, and the least-squares adjustment.
    bool applyScaleFactor = false;
    ScaleFactorKind scaleFactorKind = ScaleFactorKind::Combined;
    /// The ground→grid multiplier to apply when \ref applyScaleFactor is on. The UI resolves this
    /// from the drawing's coordinate system (CadCommands_Geo ResolveTraverseScaleFactor); 1.0 when
    /// unresolved (no coordinate system, or the Transformation tab's reference point fails).
    double scaleFactor = 1.0;

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

/// Re-derive a leg's reduced fields from its raw per-set F1/F2 observations
/// (REQ-018, ADR-003). Face-averages the literal circle readings, subtracts the
/// leg's backsight reading to get the reduced horizontal angle, and averages the
/// zenith angles and slope distances. Updates horizAngleDeg / vertAngleDeg /
/// slopeDist, the per-face reduced values, the presence flags, and the input
/// buffers. No-op when the leg has no raw sets (a manually entered leg).
/// The FBK importer and the editor both call this, so import and edit reduce
/// identically.
void ReduceLegFromSets(TraverseLeg& leg);

/// Compute all legs in \p td: fills computed fields and closure info.
void ComputeTraverse(TraverseData& td);
