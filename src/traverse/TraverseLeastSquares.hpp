#pragma once

#include <string>
#include <vector>

struct TraverseData;

/// A-priori observation standard errors used to weight the adjustment (REQ-015).
/// Defaults match the accepted spec: 5" angles, 0.02 ft + 2 ppm distances.
/// sigmaVert* (REQ-417) only applies when GPS vectors are present (see
/// ComputeTraverseLeastSquares) and defaults to the same constant+ppm model as
/// distance, since vertical total-station measurement error scales similarly
/// with sight length; there is no separate field-proven vertical model in the
/// existing spec to draw from.
struct LsaWeights {
    double sigmaAngleSec    = 5.0;    ///< Std error of a turned angle, arc-seconds.
    double sigmaDistConstFt = 0.02;   ///< Constant part of distance std error, feet.
    double sigmaDistPpm     = 2.0;    ///< Distance std error, parts-per-million of length.
    double sigmaVertConstFt = 0.02;   ///< Constant part of a leg's vertical std error, feet (REQ-417).
    double sigmaVertPpm     = 2.0;    ///< Leg vertical std error, parts-per-million of horiz. distance (REQ-417).
};

/// One leg observation's residual from the least-squares adjustment (REQ-016).
/// Residual convention: residual = adjusted − observed.
struct LsaResidual {
    int    legIndex      = 0;     ///< 0-based leg this observation belongs to.
    int    fromStationId = 0;     ///< Occupied (instrument) station.
    int    toStationId   = 0;     ///< Foresight station.
    double angleResidualSec = 0.0; ///< Turned-angle residual, arc-seconds.
    double distResidualFt   = 0.0; ///< Horizontal-distance residual, feet.
    /// Vertical (elevation-difference) residual, feet. Only non-zero when the
    /// adjustment solved elevation too (REQ-417 — GPS vectors present and this
    /// leg carried a real vertical angle); 0.0 otherwise.
    double vertResidualFt   = 0.0;
};

/// One GPS vector's residual from the least-squares adjustment (REQ-417).
/// Residual convention: residual = adjusted − observed, in the local ENU frame.
struct LsaGpsResidual {
    int    vectorIndex   = 0;     ///< Index into TraverseData::gpsVectors.
    int    fromStationId = 0;
    int    toStationId   = 0;
    double eastResidualFt  = 0.0;
    double northResidualFt = 0.0;
    double upResidualFt    = 0.0;
};

/// Result of a weighted least-squares adjustment of a closed-loop traverse,
/// optionally including GPS vectors (REQ-417).
struct LsaResult {
    bool        ok = false;       ///< True if an adjustment was produced.
    std::string message;          ///< Reason when !ok (REQ-017); short summary when ok.

    /// True when at least one usable GPS vector (resolved + a-priori covariance)
    /// was folded into this adjustment, in which case elevation was solved as a
    /// third unknown per free station and unknowns/observations reflect that.
    bool   usedGpsVectors = false;
    int    gpsVectorsUsed    = 0;  ///< GPS vectors actually included.
    int    gpsVectorsSkipped = 0;  ///< GPS vectors present but excluded (see message).

    int    unknowns     = 0;      ///< Solved coordinate unknowns (2 or 3 per free station).
    int    observations = 0;      ///< Angle + distance (+ vertical + GPS) observations.
    int    redundancy   = 0;      ///< observations − unknowns.
    int    iterations   = 0;      ///< Gauss-Newton iterations run.
    double refStdDev    = 0.0;    ///< Std dev of unit weight, sqrt(vᵀWv / r).

    // Adjusted coordinates of the free (non-fixed) stations, parallel arrays.
    // Includes loop stations and, when usedGpsVectors, any GPS-only station tied
    // in only by a GPS vector. adjElevation is only meaningful when usedGpsVectors.
    std::vector<int>    stationIds;
    std::vector<double> adjEasting;
    std::vector<double> adjNorthing;
    std::vector<double> adjElevation;

    // Loop closure recomputed from the adjusted observations (~0 when adjusted).
    double adjClosureE      = 0.0;
    double adjClosureN      = 0.0;
    double adjClosureLinear = 0.0;

    std::vector<LsaResidual>    residuals;     ///< One entry per leg.
    std::vector<LsaGpsResidual> gpsResiduals;  ///< One entry per used GPS vector.
};

/// Adjust a closed-loop traverse by weighted least squares, optionally folding
/// in GPS vectors (REQ-417, ADR-065).
///
/// Preconditions (each failure yields ok == false and a message — REQ-017):
///   - td.isClosedLoop is true (the closing foresight is the start monument,
///     possibly re-observed under a suffixed name — it is treated as the start),
///   - a start reference bearing is set,
///   - every leg has a resolved horizontal angle and horizontal distance
///     (run ComputeTraverse first),
///   - redundancy ≥ 1 and the normal-equation system is non-singular.
///
/// GPS vectors (td.gpsVectors, resolved by ComputeTraverse) are included when a
/// vector has both \c computed and \c hasCovariance true; its ECEF covariance is
/// rotated into the local ENU frame (geo::EcefCovarianceToEnu) and inverted to a
/// full correlated 3x3 weight block (REQ-417 — "Full 3D, fully correlated").
/// When no vector is usable, elevation is left untouched and the adjustment is
/// numerically identical to the pre-REQ-417 2D (easting/northing-only) behavior.
/// A usable vector may tie in a station that is not part of the traverse loop at
/// all (a GPS-only station), which becomes an additional free unknown.
///
/// Never throws and never emits NaN; on any degenerate input it returns a
/// well-formed failed result.
LsaResult ComputeTraverseLeastSquares(const TraverseData& td, const LsaWeights& weights);
