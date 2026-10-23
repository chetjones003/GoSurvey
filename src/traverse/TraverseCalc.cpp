#include "TraverseCalc.hpp"

#include "../geo/EcefTransform.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <unordered_map>

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


void TraverseAppendImported(TraverseData& into, TraverseData&& imported) {
    if (into.legs.empty() && into.sideShots.empty() && into.gpsVectors.empty()) {
        into = std::move(imported);
        return;
    }

    int maxUsedId = into.startStationId;
    for (const TraverseLeg& leg : into.legs) maxUsedId = std::max(maxUsedId, leg.stationId);
    for (const TraverseSideShot& shot : into.sideShots)
        maxUsedId = std::max({maxUsedId, shot.atStationId, shot.stationId});
    for (const TraverseGpsVector& vector : into.gpsVectors)
        maxUsedId = std::max({maxUsedId, vector.fromStationId, vector.toStationId});

    const int chainEndId = into.legs.empty() ? into.startStationId : into.legs.back().stationId;
    std::unordered_map<int, int> remap;
    remap[imported.startStationId] = chainEndId;
    int nextId = maxUsedId + 1;
    auto mapId = [&](int oldId) {
        const auto found = remap.find(oldId);
        if (found != remap.end()) return found->second;
        const int newId = nextId++;
        remap.emplace(oldId, newId);
        return newId;
    };

    for (TraverseLeg& leg : imported.legs) leg.stationId = mapId(leg.stationId);
    for (TraverseSideShot& shot : imported.sideShots) {
        shot.atStationId = mapId(shot.atStationId);
        shot.stationId = mapId(shot.stationId);
    }
    for (TraverseGpsVector& vector : imported.gpsVectors) {
        vector.fromStationId = mapId(vector.fromStationId);
        vector.toStationId = mapId(vector.toStationId);
    }

    into.legs.insert(into.legs.end(), std::make_move_iterator(imported.legs.begin()),
                     std::make_move_iterator(imported.legs.end()));
    into.sideShots.insert(into.sideShots.end(), std::make_move_iterator(imported.sideShots.begin()),
                          std::make_move_iterator(imported.sideShots.end()));
    into.gpsVectors.insert(into.gpsVectors.end(), std::make_move_iterator(imported.gpsVectors.begin()),
                           std::make_move_iterator(imported.gpsVectors.end()));
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

    // Per-occupied-station position + effective backsight bearing, captured as
    // each leg resolves, for the side-shot/check-shot pass below (REQ-409).
    struct StationSetup { double e = 0.0, n = 0.0, z = 0.0, backBearingDeg = 0.0; bool valid = false; };
    std::unordered_map<int, StationSetup> setups;
    setups[td.startStationId] = {td.startEasting, td.startNorthing, td.startElevation, td.startBearingDeg, true};

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
        bool hasVA = false;  // a vertical angle was resolved for this leg

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
                hasVA = true;
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
            hasVA = true;  // slope distances always carry a vertical angle
            // Prism constant (GitHub issue #845 follow-up): a real EDM correction,
            // applied before reduction — only meaningful for a measured slope
            // distance, not one already entered as a reduced horizontal distance.
            const double correctedSd = leg.slopeDist + (leg.hasPrismConstant ? leg.prismConstant : 0.0);
            hd = TraverseReduceToHoriz(correctedSd, va, leg.isZenithAngle);
            dz = TraverseReduceToVert(correctedSd, va, leg.isZenithAngle);
            hasHD = true;
        }

        if (!hasHD || hd < 0.0) {
            leg.errorMsg = "Horizontal or slope distance required";
            break;
        }

        // --- Earth curvature + atmospheric refraction (issue #826) ---
        // Combined correction C = (1 - k) * hd^2 / (2R), added to the trig dZ.
        // hd is the horizontal distance resolved above, for both the entered-HD
        // and slope-distance paths. Applied only when a vertical angle was
        // resolved: legs with no vertical observation keep legacy flat behavior.
        if (td.applyCurvRefCorr && hasVA) {
            constexpr double kEarthRadiusM = 6371000.0;  // mean earth radius
            dz += (1.0 - td.refractionCoeff) * hd * hd / (2.0 * kEarthRadiusM);
        }

        // --- Coordinate increments ---
        const double bearingRad = fwdBearing * kDeg2Rad;
        // REQ-401: reduce ground to grid when requested; off (the default) reproduces hd exactly.
        const double gridHd = td.applyScaleFactor ? hd * td.scaleFactor : hd;
        leg.computedGroundHorizDist = hd;
        leg.computedHorizDist = gridHd;
        leg.computedDeltaE    = gridHd * std::sin(bearingRad);
        leg.computedDeltaN    = gridHd * std::cos(bearingRad);
        leg.computedDeltaZ    = dz;
        leg.computedEasting   = curE + leg.computedDeltaE;
        leg.computedNorthing  = curN + leg.computedDeltaN;
        leg.computedElevation = curZ + dz;
        leg.computed          = true;
        leg.hasSufficientData = true;

        td.closurePerimeter += gridHd;

        setups[leg.stationId] = {leg.computedEasting, leg.computedNorthing, leg.computedElevation,
                                  TraverseNormBearing(fwdBearing + 180.0), true};

        curE = leg.computedEasting;
        curN = leg.computedNorthing;
        curZ = leg.computedElevation;
        prevFwdBearing = fwdBearing;
    }

    // --- Side shots and check shots (REQ-409, GitHub issue #853) ---
    // Reduced with the exact same trig as a leg, using the occupied station's
    // resolved position and effective backsight bearing captured above — a
    // shot's forward bearing is simply (that station's back-bearing + its own
    // turned angle), matching the per-leg formula exactly (see `setups` above).
    for (auto& ss : td.sideShots) {
        ss.computed = false;
        ss.hasSufficientData = false;
        ss.errorMsg.clear();
        ss.checkDiscrepancy = 0.0;

        const auto it = setups.find(ss.atStationId);
        if (it == setups.end() || !it->second.valid) {
            ss.errorMsg = "Occupied station not found or not resolved";
            continue;
        }
        if (!ss.hasHorizAngle) {
            ss.errorMsg = "Horizontal angle required";
            continue;
        }

        double hd = 0.0, dz = 0.0;
        bool hasHD = false, hasVA = false;
        if (ss.hasHorizDist) {
            hd = ss.horizDist;
            hasHD = true;
            if (ss.hasVertAngle) {
                hasVA = true;
                if (ss.isZenithAngle) {
                    const double sinZ = std::sin(ss.vertAngleDeg * kDeg2Rad);
                    dz = (std::abs(sinZ) > 1e-10) ? hd * std::cos(ss.vertAngleDeg * kDeg2Rad) / sinZ : 0.0;
                } else {
                    dz = hd * std::tan(ss.vertAngleDeg * kDeg2Rad);
                }
            }
        } else if (ss.hasSlopeDist) {
            if (!ss.hasVertAngle) {
                ss.errorMsg = "Slope distance requires vertical angle";
                continue;
            }
            hasVA = true;
            hd = TraverseReduceToHoriz(ss.slopeDist, ss.vertAngleDeg, ss.isZenithAngle);
            dz = TraverseReduceToVert(ss.slopeDist, ss.vertAngleDeg, ss.isZenithAngle);
            hasHD = true;
        }

        if (!hasHD || hd < 0.0) {
            ss.errorMsg = "Horizontal or slope distance required";
            continue;
        }

        if (td.applyCurvRefCorr && hasVA) {
            constexpr double kEarthRadiusM = 6371000.0;
            dz += (1.0 - td.refractionCoeff) * hd * hd / (2.0 * kEarthRadiusM);
        }

        const double fwdBearing = TraverseNormBearing(it->second.backBearingDeg + ss.horizAngleDeg);
        const double bearingRad = fwdBearing * kDeg2Rad;
        const double gridHd = td.applyScaleFactor ? hd * td.scaleFactor : hd;

        ss.computedBearingDeg = fwdBearing;
        ss.computedHorizDist = gridHd;
        ss.computedDeltaE = gridHd * std::sin(bearingRad);
        ss.computedDeltaN = gridHd * std::cos(bearingRad);
        ss.computedDeltaZ = dz;
        ss.computedEasting = it->second.e + ss.computedDeltaE;
        ss.computedNorthing = it->second.n + ss.computedDeltaN;
        ss.computedElevation = it->second.z + dz;
        ss.computed = true;
        ss.hasSufficientData = true;

        if (ss.hasKnownCoords) {
            const double dE = ss.computedEasting - ss.knownEasting;
            const double dN = ss.computedNorthing - ss.knownNorthing;
            ss.checkDiscrepancy = std::sqrt(dE * dE + dN * dN);
        }
    }

    // --- GPS vectors (REQ-416, GitHub issue #847) ---
    // Resolved only when the "from" station already has a known local position
    // (start station, a computed leg station, or another already-resolved GPS
    // vector's end station, in file order) — a GPS-only sub-network with no tie
    // into the conventional traverse is a defined gap, not a guess.
    constexpr double kMetersToFeet = 1.0 / 0.3048;  // exact international foot.
    for (auto& gv : td.gpsVectors) {
        gv.computed = false;
        gv.errorMsg.clear();

        if (!td.hasGpsOrigin) {
            gv.errorMsg = "GPS origin latitude/longitude required to resolve vectors";
            continue;
        }
        const auto it = setups.find(gv.fromStationId);
        if (it == setups.end() || !it->second.valid) {
            gv.errorMsg = "Origin station \"" + gv.fromName +
                          "\" has no known position (not the start station, a computed "
                          "traverse leg, or an already-resolved GPS vector)";
            continue;
        }

        double eastM = 0.0, northM = 0.0, upM = 0.0;
        geo::EcefDeltaToEnu(gv.deltaXEcefM, gv.deltaYEcefM, gv.deltaZEcefM,
                            td.gpsOriginLatDeg, td.gpsOriginLonDeg, &eastM, &northM, &upM);

        gv.computedDeltaEastFt = eastM * kMetersToFeet;
        gv.computedDeltaNorthFt = northM * kMetersToFeet;
        gv.computedDeltaUpFt = upM * kMetersToFeet;
        gv.computedToEasting = it->second.e + gv.computedDeltaEastFt;
        gv.computedToNorthing = it->second.n + gv.computedDeltaNorthFt;
        gv.computedToElevation = it->second.z + gv.computedDeltaUpFt;
        gv.computed = true;

        // Make the resolved end station available to a later vector chained off it.
        setups[gv.toStationId] = {gv.computedToEasting, gv.computedToNorthing,
                                   gv.computedToElevation, 0.0, true};
    }

    // --- Closure --- (issue #848 follow-up: an open traverse with known end
    // coordinates closes against THAT point instead of the start — closing
    // against the start wouldn't mean anything once the chain doesn't loop
    // back there).
    if (!td.legs.empty() && td.legs.back().computed &&
        (td.isClosedLoop || td.hasEndCoordinates)) {
        const double closeToE = td.isClosedLoop ? td.startEasting : td.endEasting;
        const double closeToN = td.isClosedLoop ? td.startNorthing : td.endNorthing;
        td.closureDeltaE = curE - closeToE;
        td.closureDeltaN = curN - closeToN;
        td.closureLinear = std::sqrt(td.closureDeltaE * td.closureDeltaE +
                                     td.closureDeltaN * td.closureDeltaN);
        if (td.closureLinear > 1e-10 && td.closurePerimeter > 1e-10)
            td.closurePrecision = td.closurePerimeter / td.closureLinear;
        td.closureValid = true;
    }
}
