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

/// Which measurement a listing row describes (REQ-418).
enum class LsaMeasKind {
    Angle,
    Distance,
    Vertical,
    GpsEast,
    GpsNorth,
    GpsUp,
    CoordEast,
    CoordNorth,
    CoordUp,
    Zenith,
    Direction,
};

/// One measurement's residual listing row (REQ-418).
/// Residual convention: residual = adjusted − observed.
/// An angle's adjusted value is degrees; its residual and standard error are
/// arc-seconds. Every other kind uses feet for all three.
/// fromStationId is the backsight for an angle, and -1 when that backsight is
/// the reference bearing rather than a station. atStationId is the occupied
/// station for an angle, and -1 for a distance, vertical, or GPS component.
struct LsaMeasResidual {
    LsaMeasKind kind = LsaMeasKind::Angle;
    int atStationId = -1;
    int fromStationId = -1;
    int toStationId = 0;
    double adjusted = 0.0;
    double residual = 0.0;
    double stdError = 0.0;
    double stdResidual = 0.0;
    bool flagged = false; ///< True when abs(stdResidual) > 3.
};

/// Count, sum of squared standardized residuals, and error factor for one
/// measurement kind (REQ-418). errorFactor = sqrt(sumSqStdResidual / count).
struct LsaKindSummary {
    LsaMeasKind kind = LsaMeasKind::Angle;
    int count = 0;
    double sumSqStdResidual = 0.0;
    double errorFactor = 0.0;
};

/// Two-tailed 5% chi-square test of the adjustment's weighted sum of squares
/// (REQ-418). errorFactor equals LsaResult::refStdDev. The bounds are in the
/// same error-factor units.
struct LsaChiSquare {
    bool evaluated = false;
    double statistic = 0.0;
    double errorFactor = 0.0;
    double lowerFactor = 0.0;
    double upperFactor = 0.0;
    bool passed = false;
    bool exceededUpper = false;
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

    // 1-sigma standard error ellipse per free station (parallel to stationIds),
    // from the inverted normal-equation matrix scaled by refStdDev^2 — the
    // standard "Qxx * sigma0^2" covariance propagation. azimuthDeg is the
    // semi-major axis's direction, degrees clockwise from north (0-360), matching
    // this file's bearing convention. Empty when the normal matrix couldn't be
    // inverted (same geometry that would make the adjustment itself singular).
    std::vector<double> stationSemiMajorFt;
    std::vector<double> stationSemiMinorFt;
    std::vector<double> stationEllipseAzimuthDeg;

    // Loop closure recomputed from the adjusted observations (~0 when adjusted).
    double adjClosureE      = 0.0;
    double adjClosureN      = 0.0;
    double adjClosureLinear = 0.0;

    std::vector<LsaResidual>    residuals;     ///< One entry per leg.
    std::vector<LsaGpsResidual> gpsResiduals;  ///< One entry per used GPS vector.

    /// One row per measurement (REQ-418), parallel in meaning to residuals and
    /// gpsResiduals but split so an angle and a distance are not one line.
    std::vector<LsaMeasResidual> measResiduals;
    std::vector<LsaKindSummary> kindSummaries;
    LsaChiSquare chiSquare;
};

/// Chi-square critical value at \p probability (for example 0.025 or 0.975)
/// with \p degreesOfFreedom. In-tree; no statistics library (ADR-001, REQ-300).
/// Returns 0 when the inputs cannot describe a critical value.
double LsaChiSquareCritical(double probability, int degreesOfFreedom);

/// Adjust a closed-loop OR open traverse by weighted least squares, optionally
/// folding in GPS vectors (REQ-417, ADR-065; open-traverse support — issue
/// #848 follow-up).
///
/// Preconditions (each failure yields ok == false and a message — REQ-017):
///   - td.isClosedLoop is true (the closing foresight is the start monument,
///     possibly re-observed under a suffixed name — it is treated as the start)
///     OR td.hasEndCoordinates is true (an open traverse tied to a second
///     known point — td.endEasting/endNorthing/endElevation — for the
///     redundancy a plain open chain tied to only the start doesn't have),
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
