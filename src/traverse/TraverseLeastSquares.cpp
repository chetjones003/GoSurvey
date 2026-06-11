#include "TraverseLeastSquares.hpp"
#include "TraverseCalc.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi      = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Sec = 180.0 * 3600.0 / kPi;

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
    // The closing foresight is the start monument re-observed; the loop is
    // declared closed by td.isClosedLoop (set on import or by the user), so we
    // treat the last node as the fixed start rather than matching station ids.
    for (int k = 0; k < m; ++k) {
        if (!td.legs[k].computed) {
            r.message = "All legs must have a horizontal angle and distance "
                        "(leg " + std::to_string(k + 1) + " is unresolved).";
            return r;
        }
    }

    // ---- Topology ---------------------------------------------------------
    // Nodes 0..m: node 0 and node m are the (fixed) start station.
    // Node k (1..m-1) is the foresight of leg k-1 and is a free unknown.
    const int u = 2 * (m - 1);          // 2 coordinates per free station.
    const int nObs = 2 * m;             // a distance + an angle per leg.
    r.unknowns     = u;
    r.observations = nObs;
    r.redundancy   = nObs - u;
    if (r.redundancy < 1) {
        r.message = "Insufficient redundancy to adjust.";
        return r;
    }

    auto colOf = [&](int node) -> int {           // -1 for the fixed start node.
        if (node == 0 || node == m) return -1;
        return 2 * (node - 1);
    };

    // Approximate coordinates per node (from ComputeTraverse).
    std::vector<double> ne(m + 1, 0.0), nn(m + 1, 0.0);
    ne[0] = td.startEasting;  nn[0] = td.startNorthing;
    ne[m] = td.startEasting;  nn[m] = td.startNorthing;
    for (int k = 1; k < m; ++k) {
        ne[k] = td.legs[k - 1].computedEasting;
        nn[k] = td.legs[k - 1].computedNorthing;
    }

    // Observations.
    std::vector<double> obsDist(m), obsAng(m);
    for (int k = 0; k < m; ++k) {
        obsDist[k] = td.legs[k].computedHorizDist;
        obsAng[k]  = td.legs[k].computedHorizAngleDeg * kDeg2Rad;
    }
    const double startBrgRad = td.startBearingDeg * kDeg2Rad;

    // A sparse coefficient term: (column, partial derivative).
    struct Term { int col; double a; };

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

        for (int k = 0; k < m; ++k) {
            const int O = k, F = k + 1;
            const double oe = ne[O], on = nn[O], fe = ne[F], fn = nn[F];
            const double dEf = fe - oe, dNf = fn - on;
            const double dof2 = dEf * dEf + dNf * dNf;
            const double D = std::sqrt(dof2);
            if (D < 1e-9) {
                r.message = "Degenerate leg geometry (zero-length sight).";
                return r;
            }

            // --- Distance observation ---
            {
                const double sigma = w.sigmaDistConstFt + w.sigmaDistPpm * 1e-6 * obsDist[k];
                const double weight = 1.0 / (sigma * sigma);
                std::vector<Term> row;
                if (colOf(O) >= 0) {
                    row.push_back({colOf(O) + 0, -dEf / D});
                    row.push_back({colOf(O) + 1, -dNf / D});
                }
                if (colOf(F) >= 0) {
                    row.push_back({colOf(F) + 0, dEf / D});
                    row.push_back({colOf(F) + 1, dNf / D});
                }
                const double l = obsDist[k] - D;   // observed - computed.
                addObs(row, weight, l);
            }

            // --- Angle observation ---
            {
                const double sigmaRad = (w.sigmaAngleSec / 3600.0) * kDeg2Rad;
                const double weight = 1.0 / (sigmaRad * sigmaRad);

                const double azOF = Azimuth(oe, on, fe, fn);
                double azOB;
                int B = -1;
                if (k == 0) {
                    azOB = startBrgRad;             // fixed reference orientation.
                } else {
                    B = k - 1;
                    azOB = Azimuth(oe, on, ne[B], nn[B]);
                }
                const double computedAng = azOF - azOB;
                const double l = WrapPi(obsAng[k] - computedAng);

                std::vector<Term> row;
                // d(azOF)/d* : to F uses (dNf/dof2, -dEf/dof2); to O negated.
                if (colOf(F) >= 0) {
                    row.push_back({colOf(F) + 0,  dNf / dof2});
                    row.push_back({colOf(F) + 1, -dEf / dof2});
                }
                if (k != 0) {
                    const double dEb = ne[B] - oe, dNb = nn[B] - on;
                    const double dob2 = dEb * dEb + dNb * dNb;
                    if (colOf(B) >= 0) {
                        // angle = azOF - azOB  ->  partials wrt B are -(azOB partials).
                        row.push_back({colOf(B) + 0, -(dNb / dob2)});
                        row.push_back({colOf(B) + 1,  (dEb / dob2)});
                    }
                    if (colOf(O) >= 0) {
                        row.push_back({colOf(O) + 0, -dNf / dof2 + dNb / dob2});
                        row.push_back({colOf(O) + 1,  dEf / dof2 - dEb / dob2});
                    }
                } else if (colOf(O) >= 0) {
                    // Fixed reference backsight: only azOF contributes at O.
                    row.push_back({colOf(O) + 0, -dNf / dof2});
                    row.push_back({colOf(O) + 1,  dEf / dof2});
                }
                addObs(row, weight, l);
            }
        }

        std::vector<double> delta = t;             // SolveCholesky writes solution here.
        if (!SolveCholesky(N, delta, u)) {
            r.message = "Normal equations are singular (check geometry/weights).";
            return r;
        }

        double maxStep = 0.0;
        for (int j = 0; j < m - 1; ++j) {
            ne[j + 1] += delta[2 * j + 0];
            nn[j + 1] += delta[2 * j + 1];
            maxStep = std::max(maxStep, std::abs(delta[2 * j + 0]));
            maxStep = std::max(maxStep, std::abs(delta[2 * j + 1]));
        }
        if (maxStep < 1e-9)
            break;
    }
    r.iterations = iter + 1;

    // ---- Residuals from converged coordinates (adjusted - observed) -------
    double vtWv = 0.0;
    r.residuals.resize(m);
    for (int k = 0; k < m; ++k) {
        const int O = k, F = k + 1;
        const double oe = ne[O], on = nn[O], fe = ne[F], fn = nn[F];
        const double D = std::sqrt((fe - oe) * (fe - oe) + (fn - on) * (fn - on));

        const double vDist = D - obsDist[k];
        const double sigmaD = w.sigmaDistConstFt + w.sigmaDistPpm * 1e-6 * obsDist[k];
        vtWv += (vDist * vDist) / (sigmaD * sigmaD);

        const double azOF = Azimuth(oe, on, fe, fn);
        const double azOB = (k == 0) ? startBrgRad
                                     : Azimuth(oe, on, ne[k - 1], nn[k - 1]);
        const double vAng = WrapPi((azOF - azOB) - obsAng[k]);  // adjusted - observed.
        const double sigmaA = (w.sigmaAngleSec / 3600.0) * kDeg2Rad;
        vtWv += (vAng * vAng) / (sigmaA * sigmaA);

        LsaResidual& res = r.residuals[k];
        res.legIndex      = k;
        res.fromStationId = (k == 0) ? td.startStationId : td.legs[k - 1].stationId;
        res.toStationId   = td.legs[k].stationId;
        res.distResidualFt   = vDist;
        res.angleResidualSec = vAng * kRad2Sec;
    }
    r.refStdDev = (r.redundancy > 0) ? std::sqrt(vtWv / r.redundancy) : 0.0;

    // ---- Adjusted free-station coordinates -------------------------------
    for (int k = 1; k < m; ++k) {
        r.stationIds.push_back(td.legs[k - 1].stationId);
        r.adjEasting.push_back(ne[k]);
        r.adjNorthing.push_back(nn[k]);
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
    return r;
}
