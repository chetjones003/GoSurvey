#include "TraverseCalc.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

static constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

// ------------------------------------------------------------------ parsing --

bool TraverseParseAngle(const std::string& raw, double* degreesOut) {
    if (!degreesOut)
        return false;

    // Strip leading/trailing whitespace
    size_t s = 0, e = raw.size();
    while (s < e && std::isspace(static_cast<unsigned char>(raw[s]))) ++s;
    while (e > s && std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
    if (s >= e)
        return false;

    const std::string t = raw.substr(s, e - s);

    // Try decimal degrees first: plain floating-point number (may be negative)
    char* end = nullptr;
    double val = std::strtod(t.c_str(), &end);
    // Skip trailing whitespace after number
    while (end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (end && *end == '\0') {
        *degreesOut = val;
        return true;
    }

    // DMS formats: 45d30m10.5s  /  45D30M10S  /  45°30'10"  /  45d30m (no seconds)
    // Accept negative prefix.
    bool neg = false;
    size_t pos = 0;
    if (pos < t.size() && t[pos] == '-') { neg = true; ++pos; }
    else if (pos < t.size() && t[pos] == '+') { ++pos; }

    auto readNum = [&](double* out) -> bool {
        if (pos >= t.size())
            return false;
        char* ep = nullptr;
        double n = std::strtod(t.c_str() + pos, &ep);
        if (!ep || ep == t.c_str() + pos)
            return false;
        pos = static_cast<size_t>(ep - t.c_str());
        *out = n;
        return true;
    };
    auto skipDelim = [&](const char* delims) -> bool {
        if (pos >= t.size())
            return false;
        unsigned char c = static_cast<unsigned char>(t[pos]);
        while (delims && *delims) {
            if (c == static_cast<unsigned char>(*delims)) { ++pos; return true; }
            ++delims;
        }
        // Also accept degree symbol UTF-8 0xC2 0xB0 as degree delimiter
        if (c == 0xC2u && pos + 1 < t.size() &&
            static_cast<unsigned char>(t[pos + 1]) == 0xB0u) {
            pos += 2;
            return true;
        }
        return false;
    };

    double deg = 0.0, min = 0.0, sec = 0.0;
    if (!readNum(&deg))
        return false;
    if (!skipDelim("dD°\xc2"))
        return false;

    if (!readNum(&min))
        return false;
    skipDelim("mM'");

    // Seconds optional
    if (pos < t.size()) {
        readNum(&sec);
        skipDelim("sS\"");
    }

    // Skip remaining whitespace / junk
    while (pos < t.size() && std::isspace(static_cast<unsigned char>(t[pos]))) ++pos;
    if (pos != t.size())
        return false; // unexpected trailing characters

    double result = deg + min / 60.0 + sec / 3600.0;
    *degreesOut = neg ? -result : result;
    return true;
}


// ----------------------------------------------------------------- math helpers --

double TraverseNormBearing(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0)
        deg += 360.0;
    return deg;
}

double TraverseReduceToHoriz(double slopeDist, double angleDeg, bool isZenith) {
    if (isZenith)
        return slopeDist * std::sin(angleDeg * kDeg2Rad);
    return slopeDist * std::cos(angleDeg * kDeg2Rad);
}

double TraverseReduceToVert(double slopeDist, double angleDeg, bool isZenith) {
    if (isZenith)
        return slopeDist * std::cos(angleDeg * kDeg2Rad);
    return slopeDist * std::sin(angleDeg * kDeg2Rad);
}

double TraverseSlopeFromHoriz(double horizDist, double angleDeg, bool isZenith) {
    // horiz = slope * sin(zenith)  (or slope * cos(elevation))  ->  invert.
    const double factor = isZenith ? std::sin(angleDeg * kDeg2Rad)
                                   : std::cos(angleDeg * kDeg2Rad);
    if (std::abs(factor) < 1e-10)
        return 0.0;  // near-horizontal zenith / near-vertical elevation: undefined.
    return horizDist / factor;
}

StatSummary ComputeStats(const std::vector<double>& samples) {
    StatSummary s;
    s.n = static_cast<int>(samples.size());
    if (s.n == 0)
        return s;
    for (double x : samples)
        s.sum += x;
    s.mean = s.sum / s.n;
    if (s.n >= 2) {
        double sq = 0.0;
        for (double x : samples) {
            const double d = x - s.mean;
            sq += d * d;
        }
        s.stddev = std::sqrt(sq / (s.n - 1));
    }
    return s;
}

double TraverseAverageFaceHoriz(double face1Deg, double face2Deg) {
    // F2 should be ~ F1 ± 180°. Adjust F2 by +180° to bring it onto the same scale as F1.
    double adj2 = std::fmod(face2Deg + 180.0, 360.0);
    if (adj2 < 0.0)
        adj2 += 360.0;
    double diff = adj2 - face1Deg;
    // Normalize to (-180, +180]
    while (diff > 180.0)  diff -= 360.0;
    while (diff <= -180.0) diff += 360.0;
    return TraverseNormBearing(face1Deg + diff * 0.5);
}

double TraverseAverageFaceZenith(double face1Deg, double face2Deg) {
    // For zenith angles: F1 + F2 = 360° when there is no vertical index error.
    return (face1Deg + (360.0 - face2Deg)) * 0.5;
}

double TraverseAverageFaceElevation(double face1Deg, double face2Deg) {
    return (face1Deg + face2Deg) * 0.5;
}


// ------------------------------------------------------------------ formatting --

std::string TraverseFormatBearing(double bearingDeg) {
    double b = std::fmod(bearingDeg, 360.0);
    if (b < 0.0)
        b += 360.0;
    int d = static_cast<int>(b);
    double mf = (b - d) * 60.0;
    int m = static_cast<int>(mf);
    double sf = (mf - m) * 60.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%03d\xc2\xb0%02d'%04.1f\"", d, m, sf);
    return std::string(buf);
}

std::string TraverseFormatDist(double val) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", val);
    return std::string(buf);
}


// ------------------------------------------------------------------ reduction --

void ReduceLegFromSets(TraverseLeg& leg) {
    if (leg.rawSets.empty())
        return;  // manually entered leg — nothing to reduce.

    // Average the literal per-face circle readings / zenith angles / slope
    // distances over the sets that carry that face.
    auto meanOf = [](const std::vector<double>& v) -> double {
        if (v.empty()) return 0.0;
        double s = 0.0;
        for (double x : v) s += x;
        return s / static_cast<double>(v.size());
    };

    std::vector<double> f1Hz, f1Va, f1Sd, f2Hz, f2Va, f2Sd;
    for (const TraverseMeasSet& s : leg.rawSets) {
        if (s.hasF1) { f1Hz.push_back(s.f1HzDec); f1Va.push_back(s.f1VaDec); f1Sd.push_back(s.f1Sd); }
        if (s.hasF2) { f2Hz.push_back(s.f2HzDec); f2Va.push_back(s.f2VaDec); f2Sd.push_back(s.f2Sd); }
    }

    const bool hasF1 = !f1Hz.empty();
    const bool hasF2 = !f2Hz.empty();
    leg.hasFace1 = hasF1;
    leg.hasFace2 = hasF2;
    if (!hasF1 && !hasF2) {
        leg.hasHorizAngle = false;
        leg.hasVertAngle  = false;
        leg.hasSlopeDist  = false;
        return;
    }

    const double bs = leg.hasBacksightCircle ? leg.backsightCircleDeg : 0.0;

    // --- Horizontal: face-average circle readings, then subtract backsight. ---
    const double avgF1Hz = meanOf(f1Hz);
    const double avgF2Hz = meanOf(f2Hz);
    double faceHz;
    if (hasF1 && hasF2)
        faceHz = TraverseAverageFaceHoriz(avgF1Hz, avgF2Hz);
    else if (hasF1)
        faceHz = avgF1Hz;
    else
        faceHz = TraverseNormBearing(avgF2Hz + 180.0);  // F2 only: onto the F1 frame.

    leg.horizAngleDeg = TraverseNormBearing(faceHz - bs);
    leg.hasHorizAngle = true;
    leg.horizAngleBuf = TraverseFormatDist(leg.horizAngleDeg);

    if (hasF1) {
        leg.face1HorizDeg = TraverseNormBearing(avgF1Hz - bs);
        leg.face1HorizBuf = TraverseFormatDist(leg.face1HorizDeg);
    }
    if (hasF2) {
        leg.face2HorizDeg = TraverseNormBearing(avgF2Hz - bs);
        leg.face2HorizBuf = TraverseFormatDist(leg.face2HorizDeg);
    }

    // --- Zenith: face-average (FBK vertical circle readings are zenith). ---
    const double avgF1Va = meanOf(f1Va);
    const double avgF2Va = meanOf(f2Va);
    if (hasF1 && hasF2)
        leg.vertAngleDeg = TraverseAverageFaceZenith(avgF1Va, avgF2Va);
    else if (hasF1)
        leg.vertAngleDeg = avgF1Va;
    else
        leg.vertAngleDeg = avgF2Va;
    leg.isZenithAngle = true;
    leg.hasVertAngle  = true;
    leg.vertAngleBuf  = TraverseFormatDist(leg.vertAngleDeg);
    if (hasF1) { leg.face1VertDeg = avgF1Va; leg.face1VertBuf = TraverseFormatDist(avgF1Va); }
    if (hasF2) { leg.face2VertDeg = avgF2Va; leg.face2VertBuf = TraverseFormatDist(avgF2Va); }

    // --- Slope distance: mean across all faces' distances. ---
    std::vector<double> allSd = f1Sd;
    allSd.insert(allSd.end(), f2Sd.begin(), f2Sd.end());
    const double sd = meanOf(allSd);
    if (sd > 0.0) {
        leg.slopeDist    = sd;
        leg.hasSlopeDist = true;
    } else {
        leg.hasSlopeDist = false;
    }
}


// ------------------------------------------------------------------ computation --

void ComputeTraverse(TraverseData& td) {
    td.closureValid = false;
    td.closureDeltaE = 0.0;
    td.closureDeltaN = 0.0;
    td.closureLinear = 0.0;
    td.closurePerimeter = 0.0;
    td.closurePrecision = 0.0;

    if (!td.hasStartBearing) {
        for (auto& leg : td.legs) {
            leg.computed = false;
            leg.hasSufficientData = false;
            leg.errorMsg = "Starting bearing required";
        }
        return;
    }

    double curE = td.startEasting;
    double curN = td.startNorthing;
    double curZ = td.startElevation;
    // prevFwdBearing is used to compute each leg's forward bearing.
    // For the first leg: fwd = startBearing + HA  (startBearing is the reference orientation)
    // This is equivalent to treating startBearing as the "previous forward bearing minus 180°"
    // so the formula (prevFwd + 180° + HA) becomes (startBearing - 180° + 180° + HA) = startBearing + HA.
    double prevFwdBearing = td.startBearingDeg - 180.0;

    for (auto& leg : td.legs) {
        leg.computed = false;
        leg.hasSufficientData = false;
        leg.errorMsg.clear();

        // --- Horizontal angle ---
        double ha = 0.0;
        bool hasHA = false;
        if (td.useFace1Face2 && (leg.hasFace1 || leg.hasFace2)) {
            if (leg.hasFace1 && leg.hasFace2)
                ha = TraverseAverageFaceHoriz(leg.face1HorizDeg, leg.face2HorizDeg);
            else if (leg.hasFace1)
                ha = leg.face1HorizDeg;
            else  // F2 only: bring onto the F1 frame (F2 reads ~F1 ± 180°).
                ha = TraverseNormBearing(leg.face2HorizDeg + 180.0);
            hasHA = true;
        } else if (leg.hasHorizAngle) {
            ha = leg.horizAngleDeg;
            hasHA = true;
        }

        if (!hasHA) {
            leg.errorMsg = "Horizontal angle required";
            // Propagation stops here; subsequent legs cannot be computed.
            break;
        }
        leg.computedHorizAngleDeg = ha;

        // --- Forward bearing ---
        // Back-bearing at the current instrument station = prevFwd + 180°
        // Forward bearing to foresight = back-bearing + HA
        const double fwdBearing = TraverseNormBearing(prevFwdBearing + 180.0 + ha);
        leg.computedBearingDeg = fwdBearing;

        // --- Horizontal and vertical distances ---
        double hd = 0.0;
        double dz = 0.0;
        bool hasHD = false;

        // Resolve the active vertical angle (F1/F2 or single)
        auto resolveVA = [&](double* vaOut) -> bool {
            if (td.useFace1Face2 && (leg.hasFace1 || leg.hasFace2)) {
                if (leg.hasFace1 && leg.hasFace2)
                    *vaOut = leg.isZenithAngle
                                 ? TraverseAverageFaceZenith(leg.face1VertDeg, leg.face2VertDeg)
                                 : TraverseAverageFaceElevation(leg.face1VertDeg, leg.face2VertDeg);
                else if (leg.hasFace1)
                    *vaOut = leg.face1VertDeg;
                else  // F2 only: a zenith F2 reads ~360° − F1; elevation is symmetric.
                    *vaOut = leg.isZenithAngle ? (360.0 - leg.face2VertDeg) : leg.face2VertDeg;
                return true;
            }
            if (leg.hasVertAngle) {
                *vaOut = leg.vertAngleDeg;
                return true;
            }
            return false;
        };

        if (leg.hasHorizDist) {
            hd = leg.horizDist;
            hasHD = true;
            // Compute dz from vertical angle if available
            double va = 0.0;
            if (resolveVA(&va)) {
                if (leg.isZenithAngle) {
                    const double sinZ = std::sin(va * kDeg2Rad);
                    dz = (std::abs(sinZ) > 1e-10) ? hd * std::cos(va * kDeg2Rad) / sinZ : 0.0;
                } else {
                    dz = hd * std::tan(va * kDeg2Rad);
                }
            }
        } else if (leg.hasSlopeDist) {
            double va = 0.0;
            if (!resolveVA(&va)) {
                leg.errorMsg = "Slope distance requires vertical angle";
                break;
            }
            hd = TraverseReduceToHoriz(leg.slopeDist, va, leg.isZenithAngle);
            dz = TraverseReduceToVert(leg.slopeDist, va, leg.isZenithAngle);
            hasHD = true;
        }

        if (!hasHD || hd < 0.0) {
            leg.errorMsg = "Horizontal or slope distance required";
            break;
        }

        // --- Coordinate increments ---
        const double bearingRad = fwdBearing * kDeg2Rad;
        leg.computedHorizDist = hd;
        leg.computedDeltaE    = hd * std::sin(bearingRad);
        leg.computedDeltaN    = hd * std::cos(bearingRad);
        leg.computedDeltaZ    = dz;
        leg.computedEasting   = curE + leg.computedDeltaE;
        leg.computedNorthing  = curN + leg.computedDeltaN;
        leg.computedElevation = curZ + dz;
        leg.computed          = true;
        leg.hasSufficientData = true;

        td.closurePerimeter += hd;

        curE = leg.computedEasting;
        curN = leg.computedNorthing;
        curZ = leg.computedElevation;
        prevFwdBearing = fwdBearing;
    }

    // --- Closure ---
    if (td.isClosedLoop && !td.legs.empty() && td.legs.back().computed) {
        td.closureDeltaE = curE - td.startEasting;
        td.closureDeltaN = curN - td.startNorthing;
        td.closureLinear = std::sqrt(td.closureDeltaE * td.closureDeltaE +
                                     td.closureDeltaN * td.closureDeltaN);
        if (td.closureLinear > 1e-10 && td.closurePerimeter > 1e-10)
            td.closurePrecision = td.closurePerimeter / td.closureLinear;
        td.closureValid = true;
    }
}
