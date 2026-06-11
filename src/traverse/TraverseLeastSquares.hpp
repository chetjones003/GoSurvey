#pragma once

#include <string>
#include <vector>

struct TraverseData;

/// A-priori observation standard errors used to weight the adjustment (REQ-015).
/// Defaults match the accepted spec: 5" angles, 0.02 ft + 2 ppm distances.
struct LsaWeights {
    double sigmaAngleSec    = 5.0;    ///< Std error of a turned angle, arc-seconds.
    double sigmaDistConstFt = 0.02;   ///< Constant part of distance std error, feet.
    double sigmaDistPpm     = 2.0;    ///< Distance std error, parts-per-million of length.
};

/// One observation's residual from the least-squares adjustment (REQ-016).
/// Residual convention: adjusted = observed + residual.
struct LsaResidual {
    int    legIndex      = 0;     ///< 0-based leg this observation belongs to.
    int    fromStationId = 0;     ///< Occupied (instrument) station.
    int    toStationId   = 0;     ///< Foresight station.
    double angleResidualSec = 0.0; ///< Turned-angle residual, arc-seconds.
    double distResidualFt   = 0.0; ///< Horizontal-distance residual, feet.
};

/// Result of a weighted least-squares adjustment of a closed-loop traverse.
struct LsaResult {
    bool        ok = false;       ///< True if an adjustment was produced.
    std::string message;          ///< Reason when !ok (REQ-017); short summary when ok.

    int    unknowns     = 0;      ///< Solved coordinate unknowns (2 per free station).
    int    observations = 0;      ///< Angle + distance observations.
    int    redundancy   = 0;      ///< observations − unknowns.
    int    iterations   = 0;      ///< Gauss-Newton iterations run.
    double refStdDev    = 0.0;    ///< Std dev of unit weight, sqrt(vᵀWv / r).

    // Adjusted coordinates of the free (non-fixed) stations, parallel arrays.
    std::vector<int>    stationIds;
    std::vector<double> adjEasting;
    std::vector<double> adjNorthing;

    // Loop closure recomputed from the adjusted observations (~0 when adjusted).
    double adjClosureE      = 0.0;
    double adjClosureN      = 0.0;
    double adjClosureLinear = 0.0;

    std::vector<LsaResidual> residuals;  ///< One entry per leg.
};

/// Adjust a closed-loop traverse by weighted least squares.
///
/// Preconditions (each failure yields ok == false and a message — REQ-017):
///   - td.isClosedLoop is true (the closing foresight is the start monument,
///     possibly re-observed under a suffixed name — it is treated as the start),
///   - a start reference bearing is set,
///   - every leg has a resolved horizontal angle and horizontal distance
///     (run ComputeTraverse first),
///   - redundancy ≥ 1 and the normal-equation system is non-singular.
///
/// Never throws and never emits NaN; on any degenerate input it returns a
/// well-formed failed result.
LsaResult ComputeTraverseLeastSquares(const TraverseData& td, const LsaWeights& weights);
