#include "TraverseLeastSquares.hpp"
#include "TraverseCalc.hpp"
#include "../geo/EcefTransform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace {

constexpr double kPi      = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Sec = 180.0 * 3600.0 / kPi;
constexpr double kMetersToFeet = 1.0 / 0.3048;

bool IsCancelled(const std::atomic<bool>* cancelRequested) {
    return cancelRequested && cancelRequested->load(std::memory_order_acquire);
}

LsaResult CancelledResult() {
    LsaResult result;
    result.cancelled = true;
    result.message = "Adjustment cancelled.";
    return result;
}

// Azimuth (radians, CW from north) of the direction from (oe,on) to (pe,pn).
double Azimuth(double oe, double on, double pe, double pn) {
    return std::atan2(pe - oe, pn - on);
}

// Wrap an angle difference into (-pi, pi].
double WrapPi(double a) {
    while (a >  kPi) a -= 2.0 * kPi;
    while (a <= -kPi) a += 2.0 * kPi;
    return a;
}

// Solve the symmetric positive-definite system N x = t in place by Cholesky
// (N is u*u row-major). Returns false if N is not positive-definite (singular).
bool SolveCholesky(std::vector<double>& N, std::vector<double>& t, int u,
                   const std::atomic<bool>* cancelRequested = nullptr) {
    std::vector<double> L(static_cast<size_t>(u) * u, 0.0);
    for (int i = 0; i < u; ++i) {
        if (IsCancelled(cancelRequested)) return false;
        for (int j = 0; j <= i; ++j) {
            double sum = N[static_cast<size_t>(i) * u + j];
            for (int k = 0; k < j; ++k)
                sum -= L[static_cast<size_t>(i) * u + k] * L[static_cast<size_t>(j) * u + k];
            if (i == j) {
                if (sum <= 1e-12)
                    return false;  // not positive-definite -> singular geometry.
                L[static_cast<size_t>(i) * u + j] = std::sqrt(sum);
            } else {
                L[static_cast<size_t>(i) * u + j] = sum / L[static_cast<size_t>(j) * u + j];
            }
        }
    }
    // Forward solve L y = t.
    std::vector<double> y(u, 0.0);
    for (int i = 0; i < u; ++i) {
        double sum = t[i];
        for (int k = 0; k < i; ++k)
            sum -= L[static_cast<size_t>(i) * u + k] * y[k];
        y[i] = sum / L[static_cast<size_t>(i) * u + i];
    }
    // Back solve L^T x = y (store into t).
    for (int i = u - 1; i >= 0; --i) {
        double sum = y[i];
        for (int k = i + 1; k < u; ++k)
            sum -= L[static_cast<size_t>(k) * u + i] * t[k];
        t[i] = sum / L[static_cast<size_t>(i) * u + i];
    }
    return true;
}

// Invert a symmetric 3x3 matrix. Returns false if singular (determinant ~0).
bool Invert3x3(const double m[3][3], double outInv[3][3]) {
    const double a = m[0][0], b = m[0][1], c = m[0][2];
    const double d = m[1][0], e = m[1][1], f = m[1][2];
    const double g = m[2][0], h = m[2][1], i = m[2][2];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::abs(det) < 1e-18) return false;
    const double invDet = 1.0 / det;
    outInv[0][0] =  (e * i - f * h) * invDet;
    outInv[0][1] = -(b * i - c * h) * invDet;
    outInv[0][2] =  (b * f - c * e) * invDet;
    outInv[1][0] = -(d * i - f * g) * invDet;
    outInv[1][1] =  (a * i - c * g) * invDet;
    outInv[1][2] = -(a * f - c * d) * invDet;
    outInv[2][0] =  (d * h - e * g) * invDet;
    outInv[2][1] = -(a * h - b * g) * invDet;
    outInv[2][2] =  (a * e - b * d) * invDet;
    return true;
}

// Per-leg distance standard error (GitHub issue #845 follow-up): the leg's own
// equipment spec (TSERR) if given, else the traverse-wide weights, with the
// prism/pole a-priori error contributions combined in quadrature — there is no
// deterministic correction for a prism's rated accuracy or a pole's centering
// error, so they only widen the uncertainty, matching how a real geodetic
// adjustment package (StarNet, SALSA) treats them.
// Angular standard-error contribution (arc-seconds) of a centering error over a sight of
// horizDistFt feet: centeringFt / horizDistFt, in radians, converted to arc-seconds
// (REQ-422). Zero when the sight has no length or the centering error is zero/unset.
double CenteringSec(double centeringFt, double horizDistFt) {
    if (!(horizDistFt > 1e-6) || !(centeringFt > 0.0)) return 0.0;
    return (centeringFt / horizDistFt) * kRad2Sec;
}

double LegDistSigma(const TraverseLeg& leg, const LsaWeights& w, double obsDist) {
    const double distConst = leg.hasTsWeights ? leg.tsWeights.sigmaDistConstFt : w.sigmaDistConstFt;
    const double distPpm   = leg.hasTsWeights ? leg.tsWeights.sigmaDistPpm    : w.sigmaDistPpm;
    const double base = distConst + distPpm * 1e-6 * obsDist;
    // A leg's own prism/pole error, when given, is its own centering-error figure and
    // replaces the project-default instrument/target centering (REQ-422) rather than
    // combining with it — the leg is the more specific setting.
    const double prismErr = leg.hasPrismError ? leg.prismError : w.centeringTargetFt;
    const double poleErr  = leg.hasPoleError  ? leg.poleError  : w.centeringInstrumentFt;
    return std::sqrt(base * base + prismErr * prismErr + poleErr * poleErr);
}

// Per-leg angle standard error (radians): the leg's own equipment spec if given,
// else the traverse-wide weight, plus the project-default centering error's angular
// contribution over this leg's sight distance (REQ-422).
double LegAngleSigmaRad(const TraverseLeg& leg, const LsaWeights& w, double deg2rad, double obsDist) {
    const double angleSec = leg.hasTsWeights ? leg.tsWeights.sigmaAngleSec : w.sigmaAngleSec;
    const double inst = CenteringSec(w.centeringInstrumentFt, obsDist);
    const double tgt  = CenteringSec(w.centeringTargetFt, obsDist);
    const double total = std::sqrt(angleSec * angleSec + inst * inst + tgt * tgt);
    return (total / 3600.0) * deg2rad;
}

// Lower regularized gamma P(s, x). Series when x is not large; continued
// fraction for the upper tail otherwise (Numerical Recipes gser/gcf).
double RegularizedGammaP(double shape, double x) {
    if (x <= 0.0) return 0.0;
    if (shape <= 0.0) return 0.0;
    const double logPrefix = -x + shape * std::log(x) - std::lgamma(shape);
    if (x < shape + 1.0) {
        double sum = 1.0 / shape;
        double term = sum;
        for (int n = 1; n < 400; ++n) {
            term *= x / (shape + static_cast<double>(n));
            sum += term;
            if (std::abs(term) < 1e-14 * std::abs(sum)) break;
        }
        return std::exp(logPrefix) * sum;
    }
    const double kTiny = 1e-30;
    double b = x + 1.0 - shape;
    double c = 1.0 / kTiny;
    double d = 1.0 / b;
    double h = d;
    for (int i = 1; i <= 400; ++i) {
        const double an = -static_cast<double>(i) * (static_cast<double>(i) - shape);
        b += 2.0;
        d = an * d + b;
        if (std::abs(d) < kTiny) d = kTiny;
        c = b + an / c;
        if (std::abs(c) < kTiny) c = kTiny;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::abs(del - 1.0) < 1e-12) break;
    }
    const double upper = std::exp(logPrefix) * h;
    return 1.0 - upper;
}

void PushMeas(std::vector<LsaMeasResidual>& rows, LsaMeasResidual row) {
    if (row.stdError > 0.0)
        row.stdResidual = row.residual / row.stdError;
    row.flagged = std::abs(row.stdResidual) > 3.0;
    rows.push_back(row);
}

void SummarizeMeasResiduals(LsaResult& result) {
    const std::array<LsaMeasKind, 11> kinds = {
        LsaMeasKind::Angle, LsaMeasKind::Distance, LsaMeasKind::Vertical,
        LsaMeasKind::GpsEast, LsaMeasKind::GpsNorth, LsaMeasKind::GpsUp,
        LsaMeasKind::CoordEast, LsaMeasKind::CoordNorth, LsaMeasKind::CoordUp,
        LsaMeasKind::Zenith, LsaMeasKind::Direction};
    for (const LsaMeasKind kind : kinds) {
        LsaKindSummary summary;
        summary.kind = kind;
        for (const LsaMeasResidual& row : result.measResiduals) {
            if (row.kind != kind) continue;
            summary.count += 1;
            summary.sumSqStdResidual += row.stdResidual * row.stdResidual;
        }
        if (summary.count < 1) continue;
        summary.errorFactor = std::sqrt(summary.sumSqStdResidual / static_cast<double>(summary.count));
        result.kindSummaries.push_back(summary);
    }
}

void FillChiSquare(LsaResult& result, double weightedSumOfSquares) {
    result.refStdDev = (result.redundancy > 0)
                           ? std::sqrt(weightedSumOfSquares / static_cast<double>(result.redundancy))
                           : 0.0;
    if (result.redundancy < 1) return;
    result.chiSquare.evaluated = true;
    result.chiSquare.statistic = weightedSumOfSquares;
    result.chiSquare.errorFactor = result.refStdDev;
    const double lowerCrit = LsaChiSquareCritical(0.025, result.redundancy);
    const double upperCrit = LsaChiSquareCritical(0.975, result.redundancy);
    const double redundancy = static_cast<double>(result.redundancy);
    result.chiSquare.lowerFactor = std::sqrt(lowerCrit / redundancy);
    result.chiSquare.upperFactor = std::sqrt(upperCrit / redundancy);
    result.chiSquare.passed = result.chiSquare.errorFactor >= result.chiSquare.lowerFactor &&
                              result.chiSquare.errorFactor <= result.chiSquare.upperFactor;
    result.chiSquare.exceededUpper = result.chiSquare.errorFactor > result.chiSquare.upperFactor;
}

// A sparse coefficient term for one component of one observation.
struct Term { int col; double a; };

struct Coord { double e = 0.0, n = 0.0, up = 0.0; };

struct NetSt {
    double e = 0.0;
    double n = 0.0;
    double up = 0.0;
    bool eFixed = true;
    bool nFixed = true;
    bool upFixed = true;
};

bool HasNetworkObservations(const TraverseData& td) {
    return !td.networkAngles.empty() || !td.networkDists.empty() ||
           !td.networkAzimuths.empty() ||            !td.networkElevDiffs.empty() ||
           !td.networkZeniths.empty() || !td.networkDirections.empty();
}

void AddPartial(std::vector<Term>& row, const std::unordered_map<int, std::array<int, 3>>& colOf,
                int stationId, int axis, double coefficient) {
    const auto it = colOf.find(stationId);
    if (it == colOf.end()) return;
    const int col = it->second[static_cast<size_t>(axis)];
    if (col >= 0) row.push_back(Term{col, coefficient});
}

void AddAzimuthPartials(std::vector<Term>& row, const std::unordered_map<int, NetSt>& stations,
                        const std::unordered_map<int, std::array<int, 3>>& colOf,
                        int fromId, int toId, double sign) {
    const NetSt& from = stations.at(fromId);
    const NetSt& to = stations.at(toId);
    const double de = to.e - from.e;
    const double dn = to.n - from.n;
    const double dist2 = de * de + dn * dn;
    if (dist2 < 1e-12) return;
    const double inv = sign / dist2;
    AddPartial(row, colOf, toId, 0, dn * inv);
    AddPartial(row, colOf, toId, 1, -de * inv);
    AddPartial(row, colOf, fromId, 0, -dn * inv);
    AddPartial(row, colOf, fromId, 1, de * inv);
}

void AddZenithPartials(std::vector<Term>& row, const std::unordered_map<int, NetSt>& stations,
                       const std::unordered_map<int, std::array<int, 3>>& colOf, int fromId, int toId) {
    const NetSt& from = stations.at(fromId);
    const NetSt& to = stations.at(toId);
    const double de = to.e - from.e;
    const double dn = to.n - from.n;
    const double du = to.up - from.up;
    const double horiz = std::sqrt(de * de + dn * dn);
    const double slope2 = horiz * horiz + du * du;
    if (slope2 < 1e-12) return;
    const double dZenithdUp = -horiz / slope2;
    AddPartial(row, colOf, toId, 2, dZenithdUp);
    AddPartial(row, colOf, fromId, 2, -dZenithdUp);
    if (horiz < 1e-6) return;
    const double dZenithdHoriz = du / slope2;
    AddPartial(row, colOf, toId, 0, dZenithdHoriz * de / horiz);
    AddPartial(row, colOf, toId, 1, dZenithdHoriz * dn / horiz);
    AddPartial(row, colOf, fromId, 0, -dZenithdHoriz * de / horiz);
    AddPartial(row, colOf, fromId, 1, -dZenithdHoriz * dn / horiz);
}

double ComputedZenith(const NetSt& from, const NetSt& to) {
    const double de = to.e - from.e;
    const double dn = to.n - from.n;
    const double du = to.up - from.up;
    return std::atan2(std::sqrt(de * de + dn * dn), du);
}

double HorizDistFt(const NetSt& a, const NetSt& b) {
    const double de = b.e - a.e;
    const double dn = b.n - a.n;
    return std::sqrt(de * de + dn * dn);
}

// Mean earth radius, feet (used only by the curvature+refraction correction, REQ-422).
constexpr double kEarthRadiusFt = 20902231.0;

// Curvature+refraction correction (radians) added to a geometric (flat-ENU) zenith angle to
// get the zenith a real instrument would read over a level sight of horizDistFt, with
// coefficient of refraction k (REQ-422). Derived from the standard elevation formula
// deltaElev = S*cosZ + (1-k)*S^2*sin^2(Z)/(2R): for a near-level sight, S*sinZ ~= horizDist,
// so the correction simplifies to a term independent of Z: (1-k)*horizDist/(2R).
double CurvatureRefractionCorrectionRad(double horizDistFt, double refractionCoefficient) {
    return (1.0 - refractionCoefficient) * horizDistFt / (2.0 * kEarthRadiusFt);
}

double ComputedZenithRefracted(const NetSt& from, const NetSt& to, double refractionCoefficient) {
    return ComputedZenith(from, to) +
           CurvatureRefractionCorrectionRad(HorizDistFt(from, to), refractionCoefficient);
}


LsaResult AdjustNetwork(const TraverseData& td, const LsaWeights& w,
                        const std::atomic<bool>* cancelRequested) {
    LsaResult result;
    if (IsCancelled(cancelRequested)) return CancelledResult();
    bool solveUp = !td.networkElevDiffs.empty() || !td.networkZeniths.empty();
    for (const NetworkStation& station : td.networkStations)
        if (station.elevation.present) solveUp = true;

    std::unordered_map<int, NetSt> stations;
    for (const NetworkStation& station : td.networkStations) {
        NetSt& st = stations[station.id];
        if (station.easting.present) {
            st.e = station.easting.value;
            st.eFixed = station.easting.fix == NetworkFix::Fixed;
        }
        if (station.northing.present) {
            st.n = station.northing.value;
            st.nFixed = station.northing.fix == NetworkFix::Fixed;
        }
        if (station.elevation.present) {
            st.up = station.elevation.value;
            st.upFixed = station.elevation.fix == NetworkFix::Fixed;
        }
    }

    std::vector<NetworkAngleObs> angles = td.networkAngles;
    std::vector<NetworkDistObs> dists = td.networkDists;
    std::vector<NetworkAzimuthObs> azimuths = td.networkAzimuths;
    const std::vector<NetworkElevDiffObs>& elevDiffs = td.networkElevDiffs;
    const std::vector<NetworkZenithObs>& zeniths = td.networkZeniths;
    const std::vector<NetworkDirectionObs>& directions = td.networkDirections;

    if (!td.legs.empty()) {
        if (!td.hasStartBearing) {
            result.message = "A traverse chain folded into a network needs a start bearing.";
            return result;
        }
        const int legCount = static_cast<int>(td.legs.size());
        if (!stations.count(td.startStationId)) {
            NetSt start;
            start.e = td.startEasting;
            start.n = td.startNorthing;
            start.up = td.startElevation;
            stations[td.startStationId] = start;
        }
        for (int k = 0; k < legCount; ++k) {
            if (!td.legs[static_cast<size_t>(k)].computed) {
                result.message = "All legs must be computed before a network adjustment.";
                return result;
            }
            const int occupied = (k == 0) ? td.startStationId : td.legs[static_cast<size_t>(k - 1)].stationId;
            const int foresight = td.legs[static_cast<size_t>(k)].stationId;
            if (!stations.count(foresight)) {
                NetSt st;
                st.e = td.legs[static_cast<size_t>(k)].computedEasting;
                st.n = td.legs[static_cast<size_t>(k)].computedNorthing;
                st.up = td.legs[static_cast<size_t>(k)].computedElevation;
                st.eFixed = false;
                st.nFixed = false;
                st.upFixed = true;
                if (td.isClosedLoop && foresight == td.startStationId) {
                    st.e = td.startEasting;
                    st.n = td.startNorthing;
                    st.eFixed = true;
                    st.nFixed = true;
                }
                if (td.hasEndCoordinates && k == legCount - 1) {
                    st.e = td.endEasting;
                    st.n = td.endNorthing;
                    st.up = td.endElevation;
                    st.eFixed = true;
                    st.nFixed = true;
                    st.upFixed = true;
                }
                stations[foresight] = st;
            }
            NetworkDistObs dist;
            dist.fromId = occupied;
            dist.toId = foresight;
            dist.distance = td.legs[static_cast<size_t>(k)].computedHorizDist;
            dist.stdError = LegDistSigma(td.legs[static_cast<size_t>(k)], w, dist.distance);
            dists.push_back(dist);
            const double angleSec = td.legs[static_cast<size_t>(k)].hasTsWeights
                                        ? td.legs[static_cast<size_t>(k)].tsWeights.sigmaAngleSec
                                        : w.sigmaAngleSec;
            if (k == 0) {
                NetworkAzimuthObs azimuth;
                azimuth.fromId = occupied;
                azimuth.toId = foresight;
                azimuth.azimuthDeg = td.startBearingDeg + td.legs[static_cast<size_t>(k)].computedHorizAngleDeg;
                azimuth.stdErrorSec = angleSec;
                azimuths.push_back(azimuth);
            } else {
                NetworkAngleObs angle;
                angle.atId = occupied;
                angle.fromId = (k == 1) ? td.startStationId : td.legs[static_cast<size_t>(k - 2)].stationId;
                angle.toId = foresight;
                angle.angleDeg = td.legs[static_cast<size_t>(k)].computedHorizAngleDeg;
                angle.stdErrorSec = angleSec;
                angles.push_back(angle);
            }
        }
    }

    if (solveUp) {
        bool anyHeld = false;
        for (auto& entry : stations) {
            bool specified = false;
            for (const NetworkStation& station : td.networkStations)
                if (station.id == entry.first && station.elevation.present) specified = true;
            if (!specified) entry.second.upFixed = false;
            if (entry.second.upFixed) anyHeld = true;
        }
        if (!anyHeld && !stations.empty()) {
            int holdId = stations.begin()->first;
            for (const auto& entry : stations)
                if (entry.first < holdId) holdId = entry.first;
            stations[holdId].upFixed = true;
        }
    }

    std::vector<int> ids;
    ids.reserve(stations.size());
    for (const auto& entry : stations) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());

    std::vector<int> columnStation;
    std::vector<int> columnAxis;
    std::unordered_map<int, std::array<int, 3>> colOf;
    for (const int id : ids) {
        const NetSt& st = stations[id];
        std::array<int, 3> cols = {-1, -1, -1};
        const std::array<bool, 3> fixed = {st.eFixed, st.nFixed, solveUp ? st.upFixed : true};
        for (int axis = 0; axis < 3; ++axis) {
            if (fixed[static_cast<size_t>(axis)]) continue;
            cols[static_cast<size_t>(axis)] = static_cast<int>(columnStation.size());
            columnStation.push_back(id);
            columnAxis.push_back(axis);
        }
        colOf[id] = cols;
    }
    int maxSet = -1;
    for (const NetworkDirectionObs& dir : directions)
        if (dir.setId > maxSet) maxSet = dir.setId;
    std::vector<double> orient(static_cast<size_t>(maxSet + 1), 0.0);
    std::vector<char> orientSet(static_cast<size_t>(maxSet + 1), 0);
    std::unordered_map<int, int> orientCol;
    for (const NetworkDirectionObs& dir : directions) {
        if (dir.setId < 0 || stations.find(dir.atId) == stations.end() ||
            stations.find(dir.toId) == stations.end())
            continue;
        if (orientSet[static_cast<size_t>(dir.setId)]) continue;
        const NetSt& at = stations[dir.atId];
        const NetSt& to = stations[dir.toId];
        const double az = Azimuth(at.e, at.n, to.e, to.n);
        orient[static_cast<size_t>(dir.setId)] = WrapPi(az - dir.directionDeg * kDeg2Rad);
        orientSet[static_cast<size_t>(dir.setId)] = 1;
        orientCol[dir.setId] = static_cast<int>(columnStation.size());
        columnStation.push_back(dir.setId);
        columnAxis.push_back(3);
    }
    const int unknowns = static_cast<int>(columnStation.size());

    auto missing = [&](int id) { return stations.find(id) == stations.end(); };
    for (const NetworkAngleObs& angle : angles)
        if (missing(angle.atId) || missing(angle.fromId) || missing(angle.toId)) {
            result.message = "A network angle names a station with no coordinates.";
            return result;
        }
    for (const NetworkDistObs& dist : dists)
        if (missing(dist.fromId) || missing(dist.toId)) {
            result.message = "A network distance names a station with no coordinates.";
            return result;
        }
    for (const NetworkAzimuthObs& azimuth : azimuths)
        if (missing(azimuth.fromId) || missing(azimuth.toId)) {
            result.message = "A network azimuth names a station with no coordinates.";
            return result;
        }
    for (const NetworkElevDiffObs& diff : elevDiffs)
        if (missing(diff.fromId) || missing(diff.toId)) {
            result.message = "An elevation difference names a station with no coordinates.";
            return result;
        }
    for (const NetworkZenithObs& zenith : zeniths)
        if (missing(zenith.fromId) || missing(zenith.toId)) {
            result.message = "A zenith angle names a station with no coordinates.";
            return result;
        }
    for (const NetworkDirectionObs& dir : directions)
        if (missing(dir.atId) || missing(dir.toId)) {
            result.message = "A direction names a station with no coordinates.";
            return result;
        }

    int weightedControl = 0;
    for (const NetworkStation& station : td.networkStations) {
        if (station.easting.present && station.easting.fix == NetworkFix::Weighted) ++weightedControl;
        if (station.northing.present && station.northing.fix == NetworkFix::Weighted) ++weightedControl;
        if (solveUp && station.elevation.present && station.elevation.fix == NetworkFix::Weighted) ++weightedControl;
    }
    const int observations = static_cast<int>(angles.size() + dists.size() + azimuths.size() +
                                              elevDiffs.size() + zeniths.size() + directions.size()) +
                             weightedControl;
    result.unknowns = unknowns;
    result.observations = observations;
    result.redundancy = observations - unknowns;
    if (unknowns < 1 || result.redundancy < 1) {
        result.message = "The network does not have enough redundant measurements to adjust.";
        return result;
    }

    // Combined instrument+target centering error, as an angular standard-error contribution
    // (arc-seconds) over a sight of horizDistFt (REQ-422).
    auto centeringExtraSec = [&](double horizDistFt) {
        const double inst = CenteringSec(w.centeringInstrumentFt, horizDistFt);
        const double tgt  = CenteringSec(w.centeringTargetFt, horizDistFt);
        return std::sqrt(inst * inst + tgt * tgt);
    };
    auto angleSigmaRad = [&](double stdErrorSec, double defaultSec, double extraSec) {
        const double sec = stdErrorSec > 0.0 ? stdErrorSec : defaultSec;
        return (std::sqrt(sec * sec + extraSec * extraSec) / 3600.0) * kDeg2Rad;
    };
    auto distSigma = [&](const NetworkDistObs& dist) {
        const double base = dist.stdError > 0.0 ? dist.stdError
                                                  : w.sigmaDistConstFt + w.sigmaDistPpm * 1e-6 * dist.distance;
        const double inst = w.centeringInstrumentFt;
        const double tgt  = w.centeringTargetFt;
        return std::sqrt(base * base + inst * inst + tgt * tgt);
    };

    const int kMaxIter = w.maxIterations > 0 ? w.maxIterations : 12;
    int iter = 0;
    for (; iter < kMaxIter; ++iter) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        std::vector<double> normal(static_cast<size_t>(unknowns) * unknowns, 0.0);
        std::vector<double> rhs(static_cast<size_t>(unknowns), 0.0);
        auto addEq = [&](const std::vector<Term>& row, double sigma, double misclose) {
            if (!(sigma > 0.0)) return;
            const double weight = 1.0 / (sigma * sigma);
            for (const Term& a : row) {
                rhs[static_cast<size_t>(a.col)] += weight * a.a * misclose;
                for (const Term& b : row)
                    normal[static_cast<size_t>(a.col) * unknowns + b.col] += weight * a.a * b.a;
            }
        };

        for (const NetworkAngleObs& angle : angles) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            const double computed = WrapPi(Azimuth(stations[angle.atId].e, stations[angle.atId].n,
                                                   stations[angle.toId].e, stations[angle.toId].n) -
                                           Azimuth(stations[angle.atId].e, stations[angle.atId].n,
                                                   stations[angle.fromId].e, stations[angle.fromId].n));
            std::vector<Term> row;
            AddAzimuthPartials(row, stations, colOf, angle.atId, angle.toId, 1.0);
            AddAzimuthPartials(row, stations, colOf, angle.atId, angle.fromId, -1.0);
            const double extraTo   = centeringExtraSec(HorizDistFt(stations[angle.atId], stations[angle.toId]));
            const double extraFrom = centeringExtraSec(HorizDistFt(stations[angle.atId], stations[angle.fromId]));
            const double extra = std::sqrt(extraTo * extraTo + extraFrom * extraFrom);
            addEq(row, angleSigmaRad(angle.stdErrorSec, w.sigmaAngleSec, extra),
                  WrapPi(angle.angleDeg * kDeg2Rad - computed));
        }
        for (const NetworkDistObs& dist : dists) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            const double de = stations[dist.toId].e - stations[dist.fromId].e;
            const double dn = stations[dist.toId].n - stations[dist.fromId].n;
            const double computed = std::sqrt(de * de + dn * dn);
            if (computed < 1e-9) {
                result.message = "Two stations in a distance occupy the same position.";
                return result;
            }
            std::vector<Term> row;
            AddPartial(row, colOf, dist.toId, 0, de / computed);
            AddPartial(row, colOf, dist.toId, 1, dn / computed);
            AddPartial(row, colOf, dist.fromId, 0, -de / computed);
            AddPartial(row, colOf, dist.fromId, 1, -dn / computed);
            addEq(row, distSigma(dist), dist.distance - computed);
        }
        for (const NetworkAzimuthObs& azimuth : azimuths) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            const double computed = Azimuth(stations[azimuth.fromId].e, stations[azimuth.fromId].n,
                                            stations[azimuth.toId].e, stations[azimuth.toId].n);
            std::vector<Term> row;
            AddAzimuthPartials(row, stations, colOf, azimuth.fromId, azimuth.toId, 1.0);
            const double extra = centeringExtraSec(HorizDistFt(stations[azimuth.fromId], stations[azimuth.toId]));
            addEq(row, angleSigmaRad(azimuth.stdErrorSec, w.sigmaAzimuthSec, extra),
                  WrapPi(azimuth.azimuthDeg * kDeg2Rad - computed));
        }
        for (const NetworkElevDiffObs& diff : elevDiffs) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            const double computed = stations[diff.toId].up - stations[diff.fromId].up;
            std::vector<Term> row;
            AddPartial(row, colOf, diff.toId, 2, 1.0);
            AddPartial(row, colOf, diff.fromId, 2, -1.0);
            const double defaultSigma = diff.isLevelRun ? w.sigmaLevelFt : w.sigmaVertConstFt;
            const double sigma = diff.stdError > 0.0 ? diff.stdError : defaultSigma;
            addEq(row, sigma, diff.deltaZ - computed);
        }
        for (const NetworkZenithObs& zenith : zeniths) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            const double computed = ComputedZenithRefracted(stations[zenith.fromId], stations[zenith.toId],
                                                             w.refractionCoefficient);
            std::vector<Term> row;
            AddZenithPartials(row, stations, colOf, zenith.fromId, zenith.toId);
            const double horizDist = HorizDistFt(stations[zenith.fromId], stations[zenith.toId]);
            const double vert = CenteringSec(w.centeringVerticalFt, horizDist);
            const double extra = std::sqrt(centeringExtraSec(horizDist) * centeringExtraSec(horizDist) +
                                           vert * vert);
            addEq(row, angleSigmaRad(zenith.stdErrorSec, w.sigmaZenithSec, extra),
                  WrapPi(zenith.zenithDeg * kDeg2Rad - computed));
        }
        for (const NetworkDirectionObs& dir : directions) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            const double az = Azimuth(stations[dir.atId].e, stations[dir.atId].n,
                                      stations[dir.toId].e, stations[dir.toId].n);
            const double computed = WrapPi(az - orient[static_cast<size_t>(dir.setId)]);
            std::vector<Term> row;
            AddAzimuthPartials(row, stations, colOf, dir.atId, dir.toId, 1.0);
            const auto colIt = orientCol.find(dir.setId);
            if (colIt != orientCol.end()) row.push_back(Term{colIt->second, -1.0});
            const double extra = centeringExtraSec(HorizDistFt(stations[dir.atId], stations[dir.toId]));
            addEq(row, angleSigmaRad(dir.stdErrorSec, w.sigmaAngleSec, extra),
                  WrapPi(dir.directionDeg * kDeg2Rad - computed));
        }
        for (const NetworkStation& station : td.networkStations) {
            const NetSt& st = stations[station.id];
            auto addCoord = [&](const NetworkComponent& component, int axis, double current) {
                if (!component.present || component.fix != NetworkFix::Weighted) return;
                std::vector<Term> row;
                AddPartial(row, colOf, station.id, axis, 1.0);
                addEq(row, component.stdError, component.value - current);
            };
            addCoord(station.easting, 0, st.e);
            addCoord(station.northing, 1, st.n);
            if (solveUp) addCoord(station.elevation, 2, st.up);
        }

        std::vector<double> delta = rhs;
        if (!SolveCholesky(normal, delta, unknowns, cancelRequested)) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            result.message = "The network normal equations are singular.";
            return result;
        }
        double maxStep = 0.0;
        for (int col = 0; col < unknowns; ++col) {
            const int axis = columnAxis[static_cast<size_t>(col)];
            if (axis == 3) {
                orient[static_cast<size_t>(columnStation[static_cast<size_t>(col)])] +=
                    delta[static_cast<size_t>(col)];
            } else {
                NetSt& st = stations[columnStation[static_cast<size_t>(col)]];
                if (axis == 0) st.e += delta[static_cast<size_t>(col)];
                else if (axis == 1) st.n += delta[static_cast<size_t>(col)];
                else st.up += delta[static_cast<size_t>(col)];
            }
            maxStep = std::max(maxStep, std::abs(delta[static_cast<size_t>(col)]));
        }
        if (maxStep < w.convergenceLimit) break;
    }
    result.iterations = iter + 1;

    double weightedSum = 0.0;
    auto pushAngleLike = [&](LsaMeasKind kind, int at, int from, int to, double observedDeg,
                             double computedRad, double sigmaRad) {
        const double residualSec = WrapPi(computedRad - observedDeg * kDeg2Rad) * kRad2Sec;
        LsaMeasResidual row;
        row.kind = kind;
        row.atStationId = at;
        row.fromStationId = from;
        row.toStationId = to;
        row.residual = residualSec;
        row.stdError = sigmaRad * kRad2Sec;
        row.adjusted = observedDeg + residualSec / 3600.0;
        weightedSum += (row.stdError > 0.0) ? (residualSec / row.stdError) * (residualSec / row.stdError) : 0.0;
        PushMeas(result.measResiduals, row);
    };
    for (const NetworkAngleObs& angle : angles) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const double computed = WrapPi(Azimuth(stations[angle.atId].e, stations[angle.atId].n,
                                               stations[angle.toId].e, stations[angle.toId].n) -
                                       Azimuth(stations[angle.atId].e, stations[angle.atId].n,
                                               stations[angle.fromId].e, stations[angle.fromId].n));
        const double extraTo   = centeringExtraSec(HorizDistFt(stations[angle.atId], stations[angle.toId]));
        const double extraFrom = centeringExtraSec(HorizDistFt(stations[angle.atId], stations[angle.fromId]));
        pushAngleLike(LsaMeasKind::Angle, angle.atId, angle.fromId, angle.toId, angle.angleDeg, computed,
                      angleSigmaRad(angle.stdErrorSec, w.sigmaAngleSec,
                                    std::sqrt(extraTo * extraTo + extraFrom * extraFrom)));
    }
    for (const NetworkAzimuthObs& azimuth : azimuths) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const double computed = Azimuth(stations[azimuth.fromId].e, stations[azimuth.fromId].n,
                                        stations[azimuth.toId].e, stations[azimuth.toId].n);
        const double extra = centeringExtraSec(HorizDistFt(stations[azimuth.fromId], stations[azimuth.toId]));
        pushAngleLike(LsaMeasKind::Angle, -1, azimuth.fromId, azimuth.toId, azimuth.azimuthDeg, computed,
                      angleSigmaRad(azimuth.stdErrorSec, w.sigmaAzimuthSec, extra));
    }
    for (const NetworkDistObs& dist : dists) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const double de = stations[dist.toId].e - stations[dist.fromId].e;
        const double dn = stations[dist.toId].n - stations[dist.fromId].n;
        const double computed = std::sqrt(de * de + dn * dn);
        const double sigma = distSigma(dist);
        LsaMeasResidual row;
        row.kind = LsaMeasKind::Distance;
        row.fromStationId = dist.fromId;
        row.toStationId = dist.toId;
        row.residual = computed - dist.distance;
        row.stdError = sigma;
        row.adjusted = dist.distance + row.residual;
        if (sigma > 0.0) weightedSum += (row.residual / sigma) * (row.residual / sigma);
        PushMeas(result.measResiduals, row);
    }
    for (const NetworkElevDiffObs& diff : elevDiffs) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const double computed = stations[diff.toId].up - stations[diff.fromId].up;
        const double defaultSigma = diff.isLevelRun ? w.sigmaLevelFt : w.sigmaVertConstFt;
        const double sigma = diff.stdError > 0.0 ? diff.stdError : defaultSigma;
        LsaMeasResidual row;
        row.kind = LsaMeasKind::Vertical;
        row.fromStationId = diff.fromId;
        row.toStationId = diff.toId;
        row.residual = computed - diff.deltaZ;
        row.stdError = sigma;
        row.adjusted = diff.deltaZ + row.residual;
        if (sigma > 0.0) weightedSum += (row.residual / sigma) * (row.residual / sigma);
        PushMeas(result.measResiduals, row);
    }
    for (const NetworkZenithObs& zenith : zeniths) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const double computed = ComputedZenithRefracted(stations[zenith.fromId], stations[zenith.toId],
                                                         w.refractionCoefficient);
        const double horizDist = HorizDistFt(stations[zenith.fromId], stations[zenith.toId]);
        const double vert = CenteringSec(w.centeringVerticalFt, horizDist);
        const double baseCentering = centeringExtraSec(horizDist);
        pushAngleLike(LsaMeasKind::Zenith, -1, zenith.fromId, zenith.toId, zenith.zenithDeg, computed,
                      angleSigmaRad(zenith.stdErrorSec, w.sigmaZenithSec,
                                    std::sqrt(baseCentering * baseCentering + vert * vert)));
    }
    for (const NetworkDirectionObs& dir : directions) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const double az = Azimuth(stations[dir.atId].e, stations[dir.atId].n,
                                  stations[dir.toId].e, stations[dir.toId].n);
        const double computed = WrapPi(az - orient[static_cast<size_t>(dir.setId)]);
        const double extra = centeringExtraSec(HorizDistFt(stations[dir.atId], stations[dir.toId]));
        pushAngleLike(LsaMeasKind::Direction, dir.atId, -1, dir.toId, dir.directionDeg, computed,
                      angleSigmaRad(dir.stdErrorSec, w.sigmaAngleSec, extra));
    }
    for (const NetworkStation& station : td.networkStations) {
        const NetSt& st = stations[station.id];
        auto pushCoord = [&](LsaMeasKind kind, const NetworkComponent& component, double current) {
            if (!component.present || component.fix != NetworkFix::Weighted || !(component.stdError > 0.0)) return;
            LsaMeasResidual row;
            row.kind = kind;
            row.toStationId = station.id;
            row.residual = current - component.value;
            row.stdError = component.stdError;
            row.adjusted = current;
            weightedSum += (row.residual / row.stdError) * (row.residual / row.stdError);
            PushMeas(result.measResiduals, row);
        };
        pushCoord(LsaMeasKind::CoordEast, station.easting, st.e);
        pushCoord(LsaMeasKind::CoordNorth, station.northing, st.n);
        if (solveUp) pushCoord(LsaMeasKind::CoordUp, station.elevation, st.up);
    }

    for (const int id : ids) {
        const NetSt& st = stations[id];
        const std::array<int, 3>& cols = colOf[id];
        if (cols[0] < 0 && cols[1] < 0 && cols[2] < 0) continue;
        result.stationIds.push_back(id);
        result.adjEasting.push_back(st.e);
        result.adjNorthing.push_back(st.n);
        result.adjElevation.push_back(st.up);
    }
    SummarizeMeasResiduals(result);
    FillChiSquare(result, weightedSum);
    result.ok = true;
    result.message = "Adjusted network: " + std::to_string(result.observations) + " obs, " +
                     std::to_string(result.unknowns) + " unknowns.";
    return result;
}

} // namespace

const char* LsaMeasKindName(LsaMeasKind kind) {
    switch (kind) {
    case LsaMeasKind::Angle: return "Angle";
    case LsaMeasKind::Distance: return "Distance";
    case LsaMeasKind::Vertical: return "Vertical";
    case LsaMeasKind::GpsEast: return "GPS E";
    case LsaMeasKind::GpsNorth: return "GPS N";
    case LsaMeasKind::GpsUp: return "GPS U";
    case LsaMeasKind::CoordEast: return "Easting";
    case LsaMeasKind::CoordNorth: return "Northing";
    case LsaMeasKind::CoordUp: return "Elev";
    case LsaMeasKind::Zenith: return "Zenith";
    case LsaMeasKind::Direction: return "Direction";
    }
    return "";
}

double LsaChiSquareCritical(double probability, int degreesOfFreedom) {
    if (degreesOfFreedom < 1 || !(probability > 0.0) || !(probability < 1.0))
        return 0.0;
    double lo = 0.0;
    double hi = static_cast<double>(degreesOfFreedom) +
                20.0 * std::sqrt(static_cast<double>(degreesOfFreedom)) + 10.0;
    for (int expand = 0; expand < 30; ++expand) {
        if (RegularizedGammaP(0.5 * degreesOfFreedom, 0.5 * hi) >= probability) break;
        hi *= 2.0;
    }
    for (int step = 0; step < 70; ++step) {
        const double mid = 0.5 * (lo + hi);
        if (RegularizedGammaP(0.5 * degreesOfFreedom, 0.5 * mid) < probability)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

LsaResult ComputeTraverseLeastSquares(const TraverseData& td, const LsaWeights& w,
                                      const std::atomic<bool>* cancelRequested) {
    if (IsCancelled(cancelRequested)) return CancelledResult();
    if (HasNetworkObservations(td))
        return AdjustNetwork(td, w, cancelRequested);
    LsaResult r;

    // ---- Preconditions (REQ-017: surface, never absorb) -------------------
    const int m = static_cast<int>(td.legs.size());
    if (m < 3) {
        r.message = "The traverse needs at least 3 legs to adjust.";
        return r;
    }
    if (!td.hasStartBearing) {
        r.message = "Least squares requires a start reference bearing.";
        return r;
    }
    for (int k = 0; k < m; ++k) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        if (!td.legs[k].computed) {
            r.message = "All legs must have a horizontal angle and distance "
                        "(leg " + std::to_string(k + 1) + " is unresolved).";
            return r;
        }
    }

    struct CheckControl {
        const TraverseSideShot* shot;
        int occupiedPosition;
    };
    std::vector<CheckControl> checkControls;
    if (!td.isClosedLoop) {
        for (const TraverseSideShot& shot : td.sideShots) {
            if (!shot.isCheckShot || !shot.hasKnownCoords || !shot.computed) continue;
            for (int position = 0; position < m; ++position) {
                if ((position == 0 ? td.startStationId : td.legs[static_cast<size_t>(position - 1)].stationId)
                    == shot.atStationId) {
                    checkControls.push_back({&shot, position});
                    break;
                }
            }
        }
    }
    // A plain open chain has no redundant observations. A complete known CHECK
    // contributes two weighted observations and fixes its target coordinates.
    if (!td.isClosedLoop && !td.hasEndCoordinates && checkControls.empty()) {
        r.message = "Least squares requires a closed loop, ENDPOINT coordinates, or a "
                    "complete CHECK shot with known coordinates for an open traverse.";
        return r;
    }

    // ---- Topology -----------------------------------------------------
    // Station id at loop position 0..m: position 0 is always the fixed start.
    // Position m is the last leg's foresight: the start for a closed loop, a
    // fixed ENDPOINT when supplied, or a free station for a CHECK-controlled
    // open traverse.
    std::vector<int> posId(m + 1);
    posId[0] = td.startStationId;
    for (int k = 1; k <= m; ++k) posId[k] = td.legs[k - 1].stationId;

    // REQ-417: a usable GPS vector is one ComputeTraverse resolved AND that
    // carries a real a-priori covariance (StarNet COVARIANCE mode). Vectors
    // without covariance cannot be weighted without guessing an error model, so
    // they are excluded and counted, not silently dropped (REQ-201/REQ-017 style).
    std::vector<int> usableGps;
    for (int gi = 0; gi < static_cast<int>(td.gpsVectors.size()); ++gi) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const auto& gv = td.gpsVectors[gi];
        if (gv.computed && gv.hasCovariance) usableGps.push_back(gi);
        else if (gv.computed || gv.hasCovariance || !gv.errorMsg.empty()) ++r.gpsVectorsSkipped;
    }
    const bool hasGps = !usableGps.empty();
    const int dim = hasGps ? 3 : 2;  // E,N[,Up] per free station.
    r.usedGpsVectors = hasGps;

    std::unordered_map<int, Coord> checkControlCoords;
    for (const CheckControl& control : checkControls) {
        const TraverseSideShot& shot = *control.shot;
        const auto [it, inserted] = checkControlCoords.emplace(
            shot.stationId, Coord{shot.knownEasting, shot.knownNorthing, 0.0});
        if (!inserted && (std::abs(it->second.e - shot.knownEasting) > 1e-6 ||
                          std::abs(it->second.n - shot.knownNorthing) > 1e-6)) {
            r.message = "CHECK shots give conflicting known coordinates for station " +
                        std::to_string(shot.stationId) + ".";
            return r;
        }
    }
    auto conflictsWithFixed = [&](int id, double easting, double northing) {
        const auto it = checkControlCoords.find(id);
        return it != checkControlCoords.end() &&
               (std::abs(it->second.e - easting) > 1e-6 || std::abs(it->second.n - northing) > 1e-6);
    };
    if (conflictsWithFixed(td.startStationId, td.startEasting, td.startNorthing)) {
        r.message = "CHECK control coordinates conflict with the start station.";
        return r;
    }
    if (td.hasEndCoordinates && conflictsWithFixed(td.legs.back().stationId, td.endEasting, td.endNorthing)) {
        r.message = "CHECK control coordinates conflict with ENDPOINT coordinates.";
        return r;
    }

    // Approximate coordinates, keyed by station id. Loop stations come from
    // ComputeTraverse's leg chain; a GPS-only station (tied in by a vector but
    // not part of the loop) comes from ComputeTraverse's GPS resolution pass.
    std::unordered_map<int, Coord> coord;
    coord[td.startStationId] = {td.startEasting, td.startNorthing, td.startElevation};
    if (td.hasEndCoordinates) coord[posId[m]] = {td.endEasting, td.endNorthing, td.endElevation};
    const int lastApproximatePosition = td.hasEndCoordinates ? m - 1 : m;
    for (int k = 1; k <= lastApproximatePosition; ++k) {
        if (coord.find(posId[k]) == coord.end()) {
            const auto& leg = td.legs[k - 1];
            coord[posId[k]] = {leg.computedEasting, leg.computedNorthing, leg.computedElevation};
        }
    }
    for (const auto& [id, known] : checkControlCoords) coord[id] = known;
    if (hasGps) {
        for (int gi : usableGps) {
            const auto& gv = td.gpsVectors[gi];
            if (coord.find(gv.toStationId) == coord.end())
                coord[gv.toStationId] = {gv.computedToEasting, gv.computedToNorthing, gv.computedToElevation};
        }
    }

    // Free (unknown) station ids: loop interior stations, plus — when GPS
    // vectors are in play — any GPS-tied station not already free and not the
    // fixed start (REQ-417: "allow new GPS-only stations").
    std::vector<int> freeIds;
    std::unordered_map<int, int> colOfId;  // station id -> base column (dim-wide block).
    auto addFree = [&](int id) {
        if (id == td.startStationId) return;
        if (td.hasEndCoordinates && id == posId[m]) return;  // the open traverse's fixed far end.
        if (checkControlCoords.find(id) != checkControlCoords.end()) return;
        if (colOfId.find(id) != colOfId.end()) return;
        colOfId[id] = dim * static_cast<int>(freeIds.size());
        freeIds.push_back(id);
    };
    // Position m is free only when an open traverse has no ENDPOINT. In that
    // case, CHECK controls provide the redundant observations needed to solve.
    for (int k = 1; k <= m; ++k) addFree(posId[k]);
    if (hasGps) {
        for (int gi : usableGps) {
            addFree(td.gpsVectors[gi].fromStationId);
            addFree(td.gpsVectors[gi].toStationId);
        }
    }
    auto colOf = [&](int id) -> int {
        const auto it = colOfId.find(id);
        return it == colOfId.end() ? -1 : it->second;
    };

    const int u = dim * static_cast<int>(freeIds.size());
    r.unknowns = u;
    int vertObsCount = 0;
    if (dim == 3)
        for (int k = 0; k < m; ++k)
            if (td.legs[k].hasVertAngle) ++vertObsCount;
    const int nObsConventional = 2 * m + vertObsCount + 2 * static_cast<int>(checkControls.size());

    // GPS vector weight blocks (3x3, 1/ft²) and observed ENU delta (ft), fixed
    // before iteration: a direct rotation of the raw ECEF baseline (REQ-417).
    struct GpsEdge {
        int gi;
        int fromId, toId;
        double obs[3];     // observed East/North/Up delta, ft.
        double W[3][3];    // inverse rotated covariance, 1/ft².
        double sigmaFt[3]; // marginal standard error, sqrt of the ENU variance diagonal.
    };
    std::vector<GpsEdge> gpsEdges;
    for (int gi : usableGps) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const auto& gv = td.gpsVectors[gi];
        double covEcef[3][3] = {
            {gv.varXM2,  gv.covXYM2, gv.covXZM2},
            {gv.covXYM2, gv.varYM2,  gv.covYZM2},
            {gv.covXZM2, gv.covYZM2, gv.varZM2},
        };
        double covEnuM2[3][3];
        geo::EcefCovarianceToEnu(covEcef, td.gpsOriginLatDeg, td.gpsOriginLonDeg, covEnuM2);
        double covEnuFt2[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                covEnuFt2[i][j] = covEnuM2[i][j] * kMetersToFeet * kMetersToFeet;
        GpsEdge e{};
        e.gi = gi;
        e.fromId = gv.fromStationId;
        e.toId   = gv.toStationId;
        e.obs[0] = gv.computedDeltaEastFt;
        e.obs[1] = gv.computedDeltaNorthFt;
        e.obs[2] = gv.computedDeltaUpFt;
        for (int axis = 0; axis < 3; ++axis)
            e.sigmaFt[axis] = std::sqrt(std::max(0.0, covEnuFt2[axis][axis]));
        if (!Invert3x3(covEnuFt2, e.W)) {
            ++r.gpsVectorsSkipped;
            continue;  // degenerate covariance; excluded rather than guessed.
        }
        gpsEdges.push_back(e);
    }
    r.gpsVectorsUsed = static_cast<int>(gpsEdges.size());
    r.observations = nObsConventional + 3 * r.gpsVectorsUsed;
    r.redundancy   = r.observations - u;
    if (r.redundancy < 1) {
        r.message = "Insufficient redundancy to adjust.";
        return r;
    }

    // Observations fixed before iteration (derived once from raw measurements).
    std::vector<double> obsDist(m), obsAng(m), obsVert(m);
    std::vector<bool> hasVert(m, false);
    for (int k = 0; k < m; ++k) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        obsDist[k] = td.legs[k].computedHorizDist;
        obsAng[k]  = td.legs[k].computedHorizAngleDeg * kDeg2Rad;
        obsVert[k] = td.legs[k].computedDeltaZ;
        hasVert[k] = td.legs[k].hasVertAngle;
    }
    const double startBrgRad = td.startBearingDeg * kDeg2Rad;

    // ---- Gauss-Newton iterations -----------------------------------------
    const int kMaxIter = w.maxIterations > 0 ? w.maxIterations : 12;
    int iter = 0;
    std::vector<double> Nfinal;  // the last iteration's normal matrix, kept for the
                                  // post-convergence covariance/error-ellipse inversion below.
    for (; iter < kMaxIter; ++iter) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        std::vector<double> N(static_cast<size_t>(u) * u, 0.0);
        std::vector<double> t(u, 0.0);

        auto addObs = [&](const std::vector<Term>& row, double weight, double l) {
            for (size_t a = 0; a < row.size(); ++a) {
                t[row[a].col] += weight * row[a].a * l;
                for (size_t b = 0; b < row.size(); ++b)
                    N[static_cast<size_t>(row[a].col) * u + row[b].col] +=
                        weight * row[a].a * row[b].a;
            }
        };

        // A correlated multi-component observation block: rows[i] is the
        // design row for component i, Wmat is its (symmetric) weight matrix,
        // l[i] is observed − computed for component i.
        auto addBlockObs = [&](const std::vector<std::vector<Term>>& rows,
                                const double Wmat[3][3], const double l[3]) {
            const int c = static_cast<int>(rows.size());
            for (int i = 0; i < c; ++i) {
                for (int j = 0; j < c; ++j) {
                    const double wij = Wmat[i][j];
                    if (wij == 0.0) continue;
                    for (const auto& ti : rows[i]) {
                        t[ti.col] += ti.a * wij * l[j];
                        for (const auto& tj : rows[j])
                            N[static_cast<size_t>(ti.col) * u + tj.col] += ti.a * wij * tj.a;
                    }
                }
            }
        };

        bool degenerate = false;
        for (int k = 0; k < m && !degenerate; ++k) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            const int Oid = posId[k], Fid = posId[k + 1];
            const Coord& O = coord[Oid];
            const Coord& F = coord[Fid];
            const double dEf = F.e - O.e, dNf = F.n - O.n;
            const double dof2 = dEf * dEf + dNf * dNf;
            const double D = std::sqrt(dof2);
            if (D < 1e-9) {
                r.message = "Degenerate leg geometry (zero-length sight).";
                degenerate = true;
                break;
            }

            // --- Distance observation ---
            {
                const double sigma = LegDistSigma(td.legs[k], w, obsDist[k]);
                const double weight = 1.0 / (sigma * sigma);
                std::vector<Term> row;
                if (colOf(Oid) >= 0) {
                    row.push_back({colOf(Oid) + 0, -dEf / D});
                    row.push_back({colOf(Oid) + 1, -dNf / D});
                }
                if (colOf(Fid) >= 0) {
                    row.push_back({colOf(Fid) + 0, dEf / D});
                    row.push_back({colOf(Fid) + 1, dNf / D});
                }
                const double l = obsDist[k] - D;   // observed - computed.
                addObs(row, weight, l);
            }

            // --- Angle observation ---
            {
                const double sigmaRad = LegAngleSigmaRad(td.legs[k], w, kDeg2Rad, obsDist[k]);
                const double weight = 1.0 / (sigmaRad * sigmaRad);

                const double azOF = Azimuth(O.e, O.n, F.e, F.n);
                double azOB;
                int Bid = -1;
                if (k == 0) {
                    azOB = startBrgRad;             // fixed reference orientation.
                } else {
                    Bid = posId[k - 1];
                    const Coord& B = coord[Bid];
                    azOB = Azimuth(O.e, O.n, B.e, B.n);
                }
                const double computedAng = azOF - azOB;
                const double l = WrapPi(obsAng[k] - computedAng);

                std::vector<Term> row;
                if (colOf(Fid) >= 0) {
                    row.push_back({colOf(Fid) + 0,  dNf / dof2});
                    row.push_back({colOf(Fid) + 1, -dEf / dof2});
                }
                if (k != 0) {
                    const Coord& B = coord[Bid];
                    const double dEb = B.e - O.e, dNb = B.n - O.n;
                    const double dob2 = dEb * dEb + dNb * dNb;
                    if (colOf(Bid) >= 0) {
                        row.push_back({colOf(Bid) + 0, -(dNb / dob2)});
                        row.push_back({colOf(Bid) + 1,  (dEb / dob2)});
                    }
                    if (colOf(Oid) >= 0) {
                        row.push_back({colOf(Oid) + 0, -dNf / dof2 + dNb / dob2});
                        row.push_back({colOf(Oid) + 1,  dEf / dof2 - dEb / dob2});
                    }
                } else if (colOf(Oid) >= 0) {
                    row.push_back({colOf(Oid) + 0, -dNf / dof2});
                    row.push_back({colOf(Oid) + 1,  dEf / dof2});
                }
                addObs(row, weight, l);
            }

            // --- Vertical (elevation-difference) observation (REQ-417) ---
            if (dim == 3 && hasVert[k]) {
                const double sigma = w.sigmaVertConstFt + w.sigmaVertPpm * 1e-6 * obsDist[k];
                const double weight = 1.0 / (sigma * sigma);
                const double computedDz = F.up - O.up;
                const double l = obsVert[k] - computedDz;
                std::vector<Term> row;
                if (colOf(Oid) >= 0) row.push_back({colOf(Oid) + 2, -1.0});
                if (colOf(Fid) >= 0) row.push_back({colOf(Fid) + 2,  1.0});
                addObs(row, weight, l);
            }
        }
        for (const CheckControl& control : checkControls) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            const TraverseSideShot& shot = *control.shot;
            const int occupiedId = shot.atStationId;
            const int controlId = shot.stationId;
            const Coord& occupied = coord[occupiedId];
            const Coord& known = coord[controlId];
            const double dE = known.e - occupied.e;
            const double dN = known.n - occupied.n;
            const double d2 = dE * dE + dN * dN;
            const double distance = std::sqrt(d2);
            if (distance < 1e-9) {
                r.message = "Degenerate CHECK control geometry (zero-length sight).";
                degenerate = true;
                break;
            }

            // The known check coordinates are fixed; each shot contributes a
            // weighted distance and angle observation from its occupied setup.
            const double observedDistance = shot.computedHorizDist;
            const double baseDistSigma = w.sigmaDistConstFt + w.sigmaDistPpm * 1e-6 * observedDistance;
            const double distSigma = std::sqrt(baseDistSigma * baseDistSigma +
                w.centeringInstrumentFt * w.centeringInstrumentFt +
                w.centeringTargetFt * w.centeringTargetFt);
            {
                std::vector<Term> row;
                if (colOf(occupiedId) >= 0) {
                    row.push_back({colOf(occupiedId) + 0, -dE / distance});
                    row.push_back({colOf(occupiedId) + 1, -dN / distance});
                }
                addObs(row, 1.0 / (distSigma * distSigma), observedDistance - distance);
            }

            const double observedAngle = shot.horizAngleDeg * kDeg2Rad;
            const double azimuthToControl = Azimuth(occupied.e, occupied.n, known.e, known.n);
            const int backsightId = control.occupiedPosition == 0 ? -1 : posId[control.occupiedPosition - 1];
            const double azimuthToBacksight = control.occupiedPosition == 0
                ? startBrgRad
                : Azimuth(occupied.e, occupied.n, coord[backsightId].e, coord[backsightId].n);
            const double computedAngle = azimuthToControl - azimuthToBacksight;
            const double lAngle = WrapPi(observedAngle - computedAngle);
            const double angleSigmaSec = std::sqrt(
                w.sigmaAngleSec * w.sigmaAngleSec +
                CenteringSec(w.centeringInstrumentFt, observedDistance) * CenteringSec(w.centeringInstrumentFt, observedDistance) +
                CenteringSec(w.centeringTargetFt, observedDistance) * CenteringSec(w.centeringTargetFt, observedDistance));
            const double angleSigmaRad = (angleSigmaSec / 3600.0) * kDeg2Rad;
            std::vector<Term> angleRow;
            if (control.occupiedPosition == 0) {
                if (colOf(occupiedId) >= 0) {
                    angleRow.push_back({colOf(occupiedId) + 0, -dN / d2});
                    angleRow.push_back({colOf(occupiedId) + 1,  dE / d2});
                }
            } else {
                const Coord& backsight = coord[backsightId];
                const double backE = backsight.e - occupied.e;
                const double backN = backsight.n - occupied.n;
                const double backD2 = backE * backE + backN * backN;
                if (backD2 <= 1e-12) {
                    r.message = "Least squares CHECK control has a zero-length backsight.";
                    return r;
                }
                if (colOf(occupiedId) >= 0) {
                    angleRow.push_back({colOf(occupiedId) + 0, -dN / d2 + backN / backD2});
                    angleRow.push_back({colOf(occupiedId) + 1,  dE / d2 - backE / backD2});
                }
                if (colOf(backsightId) >= 0) {
                    angleRow.push_back({colOf(backsightId) + 0, -backN / backD2});
                    angleRow.push_back({colOf(backsightId) + 1,  backE / backD2});
                }
            }
            addObs(angleRow, 1.0 / (angleSigmaRad * angleSigmaRad), lAngle);
        }
        if (degenerate) return r;

        // --- GPS vector observations (REQ-417) ---
        for (const auto& e : gpsEdges) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            const Coord& from = coord[e.fromId];
            const Coord& to   = coord[e.toId];
            const double comp[3] = {to.e - from.e, to.n - from.n, to.up - from.up};
            const double l[3] = {e.obs[0] - comp[0], e.obs[1] - comp[1], e.obs[2] - comp[2]};
            std::vector<std::vector<Term>> rows(3);
            for (int c = 0; c < 3; ++c) {
                if (colOf(e.fromId) >= 0) rows[c].push_back({colOf(e.fromId) + c, -1.0});
                if (colOf(e.toId)   >= 0) rows[c].push_back({colOf(e.toId)   + c,  1.0});
            }
            addBlockObs(rows, e.W, l);
        }

        Nfinal = N;  // SolveCholesky only reads N (via a local Cholesky factor), never mutates it.
        std::vector<double> delta = t;             // SolveCholesky writes solution here.
        if (!SolveCholesky(N, delta, u, cancelRequested)) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            r.message = "Normal equations are singular (check geometry/weights).";
            return r;
        }

        double maxStep = 0.0;
        for (int fi = 0; fi < static_cast<int>(freeIds.size()); ++fi) {
            Coord& c = coord[freeIds[fi]];
            c.e += delta[dim * fi + 0];
            c.n += delta[dim * fi + 1];
            maxStep = std::max(maxStep, std::abs(delta[dim * fi + 0]));
            maxStep = std::max(maxStep, std::abs(delta[dim * fi + 1]));
            if (dim == 3) {
                c.up += delta[dim * fi + 2];
                maxStep = std::max(maxStep, std::abs(delta[dim * fi + 2]));
            }
        }
        if (maxStep < w.convergenceLimit)
            break;
    }
    r.iterations = iter + 1;

    // ---- Residuals from converged coordinates (adjusted - observed) -------
    double vtWv = 0.0;
    r.residuals.resize(m);
    for (int k = 0; k < m; ++k) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const int Oid = posId[k], Fid = posId[k + 1];
        const Coord& O = coord[Oid];
        const Coord& F = coord[Fid];
        const double D = std::sqrt((F.e - O.e) * (F.e - O.e) + (F.n - O.n) * (F.n - O.n));

        const double vDist = D - obsDist[k];
        const double sigmaD = LegDistSigma(td.legs[k], w, obsDist[k]);
        vtWv += (vDist * vDist) / (sigmaD * sigmaD);

        const double azOF = Azimuth(O.e, O.n, F.e, F.n);
        const double azOB = (k == 0) ? startBrgRad
                                     : Azimuth(O.e, O.n, coord[posId[k - 1]].e, coord[posId[k - 1]].n);
        const double vAng = WrapPi((azOF - azOB) - obsAng[k]);  // adjusted - observed.
        const double sigmaA = LegAngleSigmaRad(td.legs[k], w, kDeg2Rad, obsDist[k]);
        vtWv += (vAng * vAng) / (sigmaA * sigmaA);

        LsaResidual& res = r.residuals[k];
        res.legIndex      = k;
        res.fromStationId = Oid;
        res.toStationId   = Fid;
        res.distResidualFt   = vDist;
        res.angleResidualSec = vAng * kRad2Sec;

        const int backsightId = (k == 0) ? -1 : posId[k - 1];
        LsaMeasResidual angleRow;
        angleRow.kind = LsaMeasKind::Angle;
        angleRow.atStationId = Oid;
        angleRow.fromStationId = backsightId;
        angleRow.toStationId = Fid;
        angleRow.adjusted = obsAng[k] / kDeg2Rad + (vAng * kRad2Sec) / 3600.0;
        angleRow.residual = vAng * kRad2Sec;
        angleRow.stdError = sigmaA * kRad2Sec;
        PushMeas(r.measResiduals, angleRow);

        LsaMeasResidual distRow;
        distRow.kind = LsaMeasKind::Distance;
        distRow.fromStationId = Oid;
        distRow.toStationId = Fid;
        distRow.adjusted = obsDist[k] + vDist;
        distRow.residual = vDist;
        distRow.stdError = sigmaD;
        PushMeas(r.measResiduals, distRow);

        if (dim == 3 && hasVert[k]) {
            const double vVert = (F.up - O.up) - obsVert[k];
            const double sigmaV = w.sigmaVertConstFt + w.sigmaVertPpm * 1e-6 * obsDist[k];
            vtWv += (vVert * vVert) / (sigmaV * sigmaV);
            res.vertResidualFt = vVert;

            LsaMeasResidual vertRow;
            vertRow.kind = LsaMeasKind::Vertical;
            vertRow.fromStationId = Oid;
            vertRow.toStationId = Fid;
            vertRow.adjusted = obsVert[k] + vVert;
            vertRow.residual = vVert;
            vertRow.stdError = sigmaV;
            PushMeas(r.measResiduals, vertRow);
        }
    }

    for (const CheckControl& control : checkControls) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const TraverseSideShot& shot = *control.shot;
        const Coord& occupied = coord[shot.atStationId];
        const Coord& known = coord[shot.stationId];
        const double dE = known.e - occupied.e;
        const double dN = known.n - occupied.n;
        const double distance = std::sqrt(dE * dE + dN * dN);
        const double observedDistance = shot.computedHorizDist;
        const double distResidual = distance - observedDistance;
        const double baseDistSigma = w.sigmaDistConstFt + w.sigmaDistPpm * 1e-6 * observedDistance;
        const double distSigma = std::sqrt(baseDistSigma * baseDistSigma +
            w.centeringInstrumentFt * w.centeringInstrumentFt +
            w.centeringTargetFt * w.centeringTargetFt);
        vtWv += (distResidual * distResidual) / (distSigma * distSigma);

        const int backsightId = control.occupiedPosition == 0 ? -1 : posId[control.occupiedPosition - 1];
        const double azimuthToBacksight = control.occupiedPosition == 0
            ? startBrgRad
            : Azimuth(occupied.e, occupied.n, coord[backsightId].e, coord[backsightId].n);
        const double computedAngle = Azimuth(occupied.e, occupied.n, known.e, known.n) - azimuthToBacksight;
        const double angleResidualSec = WrapPi(computedAngle - shot.horizAngleDeg * kDeg2Rad) * kRad2Sec;
        const double angleSigmaSec = std::sqrt(
            w.sigmaAngleSec * w.sigmaAngleSec +
            CenteringSec(w.centeringInstrumentFt, observedDistance) * CenteringSec(w.centeringInstrumentFt, observedDistance) +
            CenteringSec(w.centeringTargetFt, observedDistance) * CenteringSec(w.centeringTargetFt, observedDistance));
        vtWv += (angleResidualSec * angleResidualSec) / (angleSigmaSec * angleSigmaSec);

        LsaMeasResidual angleRow;
        angleRow.kind = LsaMeasKind::Angle;
        angleRow.atStationId = shot.atStationId;
        angleRow.fromStationId = backsightId;
        angleRow.toStationId = shot.stationId;
        angleRow.adjusted = shot.horizAngleDeg + angleResidualSec / 3600.0;
        angleRow.residual = angleResidualSec;
        angleRow.stdError = angleSigmaSec;
        PushMeas(r.measResiduals, angleRow);

        LsaMeasResidual distanceRow;
        distanceRow.kind = LsaMeasKind::Distance;
        distanceRow.fromStationId = shot.atStationId;
        distanceRow.toStationId = shot.stationId;
        distanceRow.adjusted = observedDistance + distResidual;
        distanceRow.residual = distResidual;
        distanceRow.stdError = distSigma;
        PushMeas(r.measResiduals, distanceRow);
    }

    // GPS vector residuals.
    for (const auto& e : gpsEdges) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const Coord& from = coord[e.fromId];
        const Coord& to   = coord[e.toId];
        const double v[3] = {(to.e - from.e) - e.obs[0], (to.n - from.n) - e.obs[1],
                              (to.up - from.up) - e.obs[2]};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                vtWv += v[i] * e.W[i][j] * v[j];

        LsaGpsResidual gr;
        gr.vectorIndex   = e.gi;
        gr.fromStationId = e.fromId;
        gr.toStationId   = e.toId;
        gr.eastResidualFt  = v[0];
        gr.northResidualFt = v[1];
        gr.upResidualFt    = v[2];
        r.gpsResiduals.push_back(gr);

        const std::array<LsaMeasKind, 3> gpsKinds = {
            LsaMeasKind::GpsEast, LsaMeasKind::GpsNorth, LsaMeasKind::GpsUp};
        for (int axis = 0; axis < 3; ++axis) {
            LsaMeasResidual gpsRow;
            gpsRow.kind = gpsKinds[axis];
            gpsRow.fromStationId = e.fromId;
            gpsRow.toStationId = e.toId;
            gpsRow.adjusted = e.obs[axis] + v[axis];
            gpsRow.residual = v[axis];
            gpsRow.stdError = e.sigmaFt[axis];
            PushMeas(r.measResiduals, gpsRow);
        }
    }

    SummarizeMeasResiduals(r);
    FillChiSquare(r, vtWv);

    // ---- Error ellipses (1-sigma): Qxx = N^-1, covariance = Qxx * sigma0^2 ----
    // N^-1 is built one column at a time by re-solving N x = e_i through the same
    // Cholesky routine used for the adjustment itself — O(u) solves of an O(u)
    // system each, fine at traverse scale (tens to low hundreds of unknowns).
    std::vector<double> Ninv(static_cast<size_t>(u) * u, 0.0);
    bool haveNinv = true;
    for (int col = 0; col < u && haveNinv; ++col) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        std::vector<double> Ncopy = Nfinal;
        std::vector<double> e(u, 0.0);
        e[col] = 1.0;
        if (!SolveCholesky(Ncopy, e, u, cancelRequested)) {
            if (IsCancelled(cancelRequested)) return CancelledResult();
            haveNinv = false;
            break;
        }
        for (int row = 0; row < u; ++row) Ninv[static_cast<size_t>(row) * u + col] = e[row];
    }

    // ---- Adjusted free-station coordinates + per-station error ellipse ----
    for (int fi = 0; fi < static_cast<int>(freeIds.size()); ++fi) {
        if (IsCancelled(cancelRequested)) return CancelledResult();
        const int id = freeIds[fi];
        const Coord& c = coord[id];
        r.stationIds.push_back(id);
        r.adjEasting.push_back(c.e);
        r.adjNorthing.push_back(c.n);
        r.adjElevation.push_back(dim == 3 ? c.up : 0.0);

        double semiMajor = 0.0, semiMinor = 0.0, azimuthDeg = 0.0;
        if (haveNinv) {
            const int ce = dim * fi + 0, cn = dim * fi + 1;
            const double sigma0Sq = r.refStdDev * r.refStdDev;
            const double cEE = Ninv[static_cast<size_t>(ce) * u + ce] * sigma0Sq;
            const double cNN = Ninv[static_cast<size_t>(cn) * u + cn] * sigma0Sq;
            const double cEN = Ninv[static_cast<size_t>(ce) * u + cn] * sigma0Sq;
            const double trace = cEE + cNN;
            const double det = cEE * cNN - cEN * cEN;
            const double disc = std::sqrt(std::max(0.0, (trace * trace) / 4.0 - det));
            const double lambda1 = trace / 2.0 + disc;  // major
            const double lambda2 = trace / 2.0 - disc;  // minor
            semiMajor = std::sqrt(std::max(0.0, lambda1));
            semiMinor = std::sqrt(std::max(0.0, lambda2));
            // Eigenvector angle of the major axis in the (E,N) plane, standard
            // closed form for a symmetric 2x2; converted from a math angle off
            // the E axis to an azimuth (degrees clockwise from north).
            const double angMath = (std::abs(cEN) < 1e-15 && std::abs(cEE - cNN) < 1e-15)
                                        ? 0.0
                                        : 0.5 * std::atan2(2.0 * cEN, cEE - cNN);
            const double dE = std::cos(angMath), dN = std::sin(angMath);
            azimuthDeg = std::atan2(dE, dN) * (180.0 / kPi);
            if (azimuthDeg < 0.0) azimuthDeg += 360.0;
        }
        r.stationSemiMajorFt.push_back(semiMajor);
        r.stationSemiMinorFt.push_back(semiMinor);
        r.stationEllipseAzimuthDeg.push_back(azimuthDeg);
    }

    // ---- Closure recomputed from the adjusted observations ----------------
    // Walk the chain with adjusted angles/distances; a correct adjustment
    // lands back on the fixed start for a closed loop (REQ-015), or on the
    // known ENDPOINT for an open traverse (issue #848 follow-up) — comparing
    // against the start wouldn't mean anything once the chain doesn't loop
    // back there.
    {
        double curE = td.startEasting, curN = td.startNorthing;
        double prevFwd = td.startBearingDeg - 180.0;
        for (int k = 0; k < m; ++k) {
            const double adjAngDeg = obsAng[k] / kDeg2Rad + r.residuals[k].angleResidualSec / 3600.0;
            const double adjDist   = obsDist[k] + r.residuals[k].distResidualFt;
            const double fwd = TraverseNormBearing(prevFwd + 180.0 + adjAngDeg);
            curE += adjDist * std::sin(fwd * kDeg2Rad);
            curN += adjDist * std::cos(fwd * kDeg2Rad);
            prevFwd = fwd;
        }
        const double closeToE = td.isClosedLoop ? td.startEasting : td.endEasting;
        const double closeToN = td.isClosedLoop ? td.startNorthing : td.endNorthing;
        r.adjClosureE = curE - closeToE;
        r.adjClosureN = curN - closeToN;
        r.adjClosureLinear = std::sqrt(r.adjClosureE * r.adjClosureE +
                                       r.adjClosureN * r.adjClosureN);
    }

    r.ok = true;
    r.message = "Adjusted: " + std::to_string(r.observations) + " obs, " +
                std::to_string(r.unknowns) + " unknowns, redundancy " +
                std::to_string(r.redundancy) + ".";
    if (r.usedGpsVectors)
        r.message += " (" + std::to_string(r.gpsVectorsUsed) + " GPS vector(s) folded in"
                   + (r.gpsVectorsSkipped > 0 ? ", " + std::to_string(r.gpsVectorsSkipped) + " skipped)" : ")");
    return r;
}
