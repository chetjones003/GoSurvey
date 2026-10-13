#include "TraverseLeastSquares.hpp"
#include "TraverseCalc.hpp"
#include "../geo/EcefTransform.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

constexpr double kPi      = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Sec = 180.0 * 3600.0 / kPi;
constexpr double kMetersToFeet = 1.0 / 0.3048;

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
bool SolveCholesky(std::vector<double>& N, std::vector<double>& t, int u) {
    std::vector<double> L(static_cast<size_t>(u) * u, 0.0);
    for (int i = 0; i < u; ++i) {
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
double LegDistSigma(const TraverseLeg& leg, const LsaWeights& w, double obsDist) {
    const double distConst = leg.hasTsWeights ? leg.tsWeights.sigmaDistConstFt : w.sigmaDistConstFt;
    const double distPpm   = leg.hasTsWeights ? leg.tsWeights.sigmaDistPpm    : w.sigmaDistPpm;
    const double base = distConst + distPpm * 1e-6 * obsDist;
    const double prismErr = leg.hasPrismError ? leg.prismError : 0.0;
    const double poleErr  = leg.hasPoleError  ? leg.poleError  : 0.0;
    return std::sqrt(base * base + prismErr * prismErr + poleErr * poleErr);
}

// Per-leg angle standard error (radians): the leg's own equipment spec if given,
// else the traverse-wide weight.
double LegAngleSigmaRad(const TraverseLeg& leg, const LsaWeights& w, double deg2rad) {
    const double angleSec = leg.hasTsWeights ? leg.tsWeights.sigmaAngleSec : w.sigmaAngleSec;
    return (angleSec / 3600.0) * deg2rad;
}

// A sparse coefficient term for one component of one observation.
struct Term { int col; double a; };

struct Coord { double e = 0.0, n = 0.0, up = 0.0; };

} // namespace


LsaResult ComputeTraverseLeastSquares(const TraverseData& td, const LsaWeights& w) {
    LsaResult r;

    // ---- Preconditions (REQ-017: surface, never absorb) -------------------
    if (!td.isClosedLoop) {
        r.message = "Least squares requires a closed loop.";
        return r;
    }
    if (!td.hasStartBearing) {
        r.message = "Least squares requires a start reference bearing.";
        return r;
    }
    const int m = static_cast<int>(td.legs.size());
    if (m < 3) {
        r.message = "Closed loop needs at least 3 legs to adjust.";
        return r;
    }
    for (int k = 0; k < m; ++k) {
        if (!td.legs[k].computed) {
            r.message = "All legs must have a horizontal angle and distance "
                        "(leg " + std::to_string(k + 1) + " is unresolved).";
            return r;
        }
    }

    // ---- Topology -----------------------------------------------------
    // Station id at loop position 0..m: position 0 and m are the (fixed) start
    // station (the closing foresight is the start monument, possibly
    // re-observed under a suffixed name but aliased to the same id on import).
    std::vector<int> posId(m + 1);
    posId[0] = td.startStationId;
    posId[m] = td.startStationId;
    for (int k = 1; k < m; ++k) posId[k] = td.legs[k - 1].stationId;

    // REQ-417: a usable GPS vector is one ComputeTraverse resolved AND that
    // carries a real a-priori covariance (StarNet COVARIANCE mode). Vectors
    // without covariance cannot be weighted without guessing an error model, so
    // they are excluded and counted, not silently dropped (REQ-201/REQ-017 style).
    std::vector<int> usableGps;
    for (int gi = 0; gi < static_cast<int>(td.gpsVectors.size()); ++gi) {
        const auto& gv = td.gpsVectors[gi];
        if (gv.computed && gv.hasCovariance) usableGps.push_back(gi);
        else if (gv.computed || gv.hasCovariance || !gv.errorMsg.empty()) ++r.gpsVectorsSkipped;
    }
    const bool hasGps = !usableGps.empty();
    const int dim = hasGps ? 3 : 2;  // E,N[,Up] per free station.
    r.usedGpsVectors = hasGps;

    // Approximate coordinates, keyed by station id. Loop stations come from
    // ComputeTraverse's leg chain; a GPS-only station (tied in by a vector but
    // not part of the loop) comes from ComputeTraverse's GPS resolution pass.
    std::unordered_map<int, Coord> coord;
    coord[td.startStationId] = {td.startEasting, td.startNorthing, td.startElevation};
    for (int k = 1; k < m; ++k) {
        if (coord.find(posId[k]) == coord.end()) {
            const auto& leg = td.legs[k - 1];
            coord[posId[k]] = {leg.computedEasting, leg.computedNorthing, leg.computedElevation};
        }
    }
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
        if (colOfId.find(id) != colOfId.end()) return;
        colOfId[id] = dim * static_cast<int>(freeIds.size());
        freeIds.push_back(id);
    };
    for (int k = 1; k < m; ++k) addFree(posId[k]);
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
    const int nObsConventional = 2 * m + vertObsCount;  // dist + angle per leg, + vert where measured.

    // GPS vector weight blocks (3x3, 1/ft²) and observed ENU delta (ft), fixed
    // before iteration: a direct rotation of the raw ECEF baseline (REQ-417).
    struct GpsEdge {
        int gi;
        int fromId, toId;
        double obs[3];     // observed East/North/Up delta, ft.
        double W[3][3];    // inverse rotated covariance, 1/ft².
    };
    std::vector<GpsEdge> gpsEdges;
    for (int gi : usableGps) {
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
        obsDist[k] = td.legs[k].computedHorizDist;
        obsAng[k]  = td.legs[k].computedHorizAngleDeg * kDeg2Rad;
        obsVert[k] = td.legs[k].computedDeltaZ;
        hasVert[k] = td.legs[k].hasVertAngle;
    }
    const double startBrgRad = td.startBearingDeg * kDeg2Rad;

    // ---- Gauss-Newton iterations -----------------------------------------
    const int kMaxIter = 12;
    int iter = 0;
    for (; iter < kMaxIter; ++iter) {
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
                const double sigmaRad = LegAngleSigmaRad(td.legs[k], w, kDeg2Rad);
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
        if (degenerate) return r;

        // --- GPS vector observations (REQ-417) ---
        for (const auto& e : gpsEdges) {
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

        std::vector<double> delta = t;             // SolveCholesky writes solution here.
        if (!SolveCholesky(N, delta, u)) {
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
        if (maxStep < 1e-9)
            break;
    }
    r.iterations = iter + 1;

    // ---- Residuals from converged coordinates (adjusted - observed) -------
    double vtWv = 0.0;
    r.residuals.resize(m);
    for (int k = 0; k < m; ++k) {
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
        const double sigmaA = LegAngleSigmaRad(td.legs[k], w, kDeg2Rad);
        vtWv += (vAng * vAng) / (sigmaA * sigmaA);

        LsaResidual& res = r.residuals[k];
        res.legIndex      = k;
        res.fromStationId = Oid;
        res.toStationId   = Fid;
        res.distResidualFt   = vDist;
        res.angleResidualSec = vAng * kRad2Sec;

        if (dim == 3 && hasVert[k]) {
            const double vVert = (F.up - O.up) - obsVert[k];
            const double sigmaV = w.sigmaVertConstFt + w.sigmaVertPpm * 1e-6 * obsDist[k];
            vtWv += (vVert * vVert) / (sigmaV * sigmaV);
            res.vertResidualFt = vVert;
        }
    }

    // GPS vector residuals.
    for (const auto& e : gpsEdges) {
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
    }

    r.refStdDev = (r.redundancy > 0) ? std::sqrt(vtWv / r.redundancy) : 0.0;

    // ---- Adjusted free-station coordinates -------------------------------
    for (int id : freeIds) {
        const Coord& c = coord[id];
        r.stationIds.push_back(id);
        r.adjEasting.push_back(c.e);
        r.adjNorthing.push_back(c.n);
        r.adjElevation.push_back(dim == 3 ? c.up : 0.0);
    }

    // ---- Closure recomputed from the adjusted observations ----------------
    // Walk the loop with adjusted angles/distances; a correct adjustment lands
    // back on the fixed start (REQ-015).
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
        r.adjClosureE = curE - td.startEasting;
        r.adjClosureN = curN - td.startNorthing;
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
