#include "StarNetImport.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// MicroSurvey STAR*NET .dat importer (GitHub issue #846, Phase B). See
// StarNetImport.hpp for the exact supported grammar and the documented
// defined-gap boundary. Grounded in the STAR*NET v10 Reference Manual,
// Chapter 5 "Preparing Input Data" — not guessed.
// ---------------------------------------------------------------------------

namespace {

constexpr double kPi = 3.14159265358979323846;

// Strip a '#' end-of-line comment (STAR*NET allows it anywhere on a line) and
// a trailing descriptor token introduced by a leading "'" or a left-quote
// glyph (both appear in the manual's own examples) — descriptors play no part
// in GoSurvey's TraverseData, so they are simply dropped, not reported.
std::string StripCommentAndDescriptor(const std::string& line) {
    std::string s = line;
    const size_t hash = s.find('#');
    if (hash != std::string::npos) s.resize(hash);
    for (char q : {'\'', '`'}) {
        const size_t pos = s.find(q);
        if (pos != std::string::npos) { s.resize(pos); break; }
    }
    return s;
}

std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> toks;
    std::istringstream ss(line);
    std::string t;
    while (ss >> t) toks.push_back(t);
    return toks;
}

std::string ToUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool ParseDoubleTok(const std::string& s, double* out) {
    char* ep = nullptr;
    const double v = std::strtod(s.c_str(), &ep);
    if (ep == s.c_str() || *ep != '\0') return false;
    *out = v;
    return true;
}

// Unpacks STAR*NET's packed-DMS angle (DDD.mmssss…, identical convention to
// Autodesk FBK's packed angle) to decimal degrees.
double UnpackDmsAngle(double packed) {
    const bool neg = (packed < 0.0);
    const double absVal = std::abs(packed);
    const double deg = std::floor(absVal);
    const double mmFull = (absVal - deg) * 100.0;
    const double mm = std::floor(mmFull + 1e-9);
    const double ss = (mmFull - mm) * 100.0;
    const double result = deg + mm / 60.0 + ss / 3600.0;
    return neg ? -result : result;
}

// Parses a STAR*NET angle token: dash D-M-S ("123-44-55.66", exactly two
// dashes after an optional leading sign) or packed DDD.mmss otherwise — "When
// STAR*NET reads an angle, if two '-' separators are present, it is decoded
// as a D-M-S angle. Otherwise it is decoded as a packed angle."
bool ParseStarNetAngle(const std::string& tok, double* degOut) {
    bool neg = false;
    size_t start = 0;
    if (!tok.empty() && (tok[0] == '-' || tok[0] == '+')) {
        neg = (tok[0] == '-');
        start = 1;
    }
    const std::string body = tok.substr(start);
    const size_t dashCount = static_cast<size_t>(std::count(body.begin(), body.end(), '-'));
    if (dashCount == 2) {
        const size_t d1 = body.find('-');
        const size_t d2 = body.find('-', d1 + 1);
        double d, m, s;
        if (!ParseDoubleTok(body.substr(0, d1), &d) ||
            !ParseDoubleTok(body.substr(d1 + 1, d2 - d1 - 1), &m) ||
            !ParseDoubleTok(body.substr(d2 + 1), &s))
            return false;
        const double v = d + m / 60.0 + s / 3600.0;
        *degOut = neg ? -v : v;
        return true;
    }
    double packed;
    if (!ParseDoubleTok(tok, &packed)) return false;
    *degOut = UnpackDmsAngle(packed);
    return true;
}

// Parses a STAR*NET bearing/azimuth token: "N45-00-00W" style bearing,
// "B="-prefixed packed/GONS azimuth, or a plain dash/packed azimuth. Returns
// false if the token cannot be interpreted as a bearing/azimuth at all (the
// caller then tries it as a station name instead, per the manual's own
// disambiguation rule).
bool ParseStarNetBearingOrAzimuth(std::string tok, double* degOut) {
    bool forcedBearing = false;
    if (tok.size() > 2 && (tok[0] == 'B' || tok[0] == 'b') && tok[1] == '=') {
        forcedBearing = true;
        tok = tok.substr(2);
    }
    if (tok.size() >= 3) {
        const char ns = static_cast<char>(std::toupper(static_cast<unsigned char>(tok.front())));
        const char ew = static_cast<char>(std::toupper(static_cast<unsigned char>(tok.back())));
        if ((ns == 'N' || ns == 'S') && (ew == 'E' || ew == 'W')) {
            double ang;
            if (!ParseStarNetAngle(tok.substr(1, tok.size() - 2), &ang)) return false;
            double az = 0.0;
            if (ns == 'N' && ew == 'E') az = ang;
            else if (ns == 'S' && ew == 'E') az = 180.0 - ang;
            else if (ns == 'S' && ew == 'W') az = 180.0 + ang;
            else az = 360.0 - ang;  // N..W
            *degOut = TraverseNormBearing(az);
            return true;
        }
    }
    // A plain azimuth: only trusted as such without the "B=" prefix when it
    // contains no dashes a station name wouldn't also plausibly have, per the
    // manual: "If the item contains dashes and can also be interpreted as a
    // valid bearing or azimuth, then it is used as such. Otherwise, it will be
    // used as a station name." A plain packed/decimal number with no dashes
    // is ambiguous with a numeric station name, so only "B="-prefixed or
    // two-dash D-M-S forms are accepted here.
    const size_t dashCount = static_cast<size_t>(std::count(tok.begin(), tok.end(), '-'));
    if (forcedBearing || dashCount == 2) {
        double ang;
        if (ParseStarNetAngle(tok, &ang)) { *degOut = TraverseNormBearing(ang); return true; }
    }
    return false;
}

struct KnownPt { double n = 0.0, e = 0.0, z = 0.0; bool has = false, hasZ = false; };

}  // namespace

bool StarNetImport(const char* path, TraverseData& td, std::string& errorMsg) {
    std::ifstream f(path);
    if (!f.is_open()) {
        errorMsg = "Cannot open file: ";
        errorMsg += path;
        return false;
    }

    std::unordered_map<std::string, KnownPt> knownPts;
    bool orderEN = false;  // STAR*NET default coordinate order is N-then-E.

    bool inTraverse = false;
    bool sawAnyTraverse = false;
    std::string backsightTok;       // TB's raw token, resolved once the traverse closes/ends.
    bool haveBacksightTok = false;
    std::string firstOccupied;      // name given by the first T line.
    bool havePrevOccupied = false;

    struct PendingLeg {
        std::string atName;
        double angleDeg = 0.0;
        double distFt = 0.0;
        bool hasTsErr = false;
        double tsAngleSec = 0.0;
        double tsDistConst = 0.0;
    };
    std::vector<PendingLeg> pending;  // one per T line, foresight resolved afterward.

    std::vector<TraverseLeg> legs;
    std::string endStation;
    bool haveEndStation = false;

    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string stripped = StripCommentAndDescriptor(line);
        const auto toks = Tokenize(stripped);
        if (toks.empty()) continue;

        auto fail = [&](const std::string& msg) -> bool {
            errorMsg = "line " + std::to_string(lineNo) + ": " + msg;
            return false;
        };

        const std::string code = ToUpper(toks[0]);

        if (!code.empty() && code[0] == '.') {
            if (code != ".ORDER")
                return fail("unsupported STAR*NET inline option \"" + toks[0] +
                            "\" (only .ORDER is supported)");
            for (size_t i = 1; i < toks.size(); ++i) {
                const std::string a = ToUpper(toks[i]);
                if (a == "EN") orderEN = true;
                else if (a == "NE") orderEN = false;
                // ATFROMTO/FROMATTO affect only A/M angle-station order, which this
                // importer doesn't parse (T lines have a single station name).
            }
            continue;
        }

        if (code == "C") {
            if (toks.size() < 4)
                return fail("expected C <name> <N> <E> [elev] ...");
            double a, b;
            if (!ParseDoubleTok(toks[2], &a) || !ParseDoubleTok(toks[3], &b))
                return fail("C: coordinate values must be numeric");
            KnownPt kp;
            kp.n = orderEN ? b : a;
            kp.e = orderEN ? a : b;
            kp.has = true;
            // A 3rd numeric token is an elevation (3D-looking C line) — captured for
            // TraverseData::startElevation even though this importer's traverse legs
            // stay 2D; a 4th-plus token is a std error or fixity symbol, out of scope
            // here (see StarNetImport.hpp), but doesn't itself make the line unreadable.
            double z;
            if (toks.size() > 4 && ParseDoubleTok(toks[4], &z)) {
                kp.z = z;
                kp.hasZ = true;
            }
            knownPts[toks[1]] = kp;
            continue;
        }

        if (code == "TB") {
            if (sawAnyTraverse)
                return fail("a second traverse (TB) was found — multiple/interconnected "
                            "traverses in one file are not supported");
            if (toks.size() < 2) return fail("expected TB <backsight-name-or-bearing>");
            backsightTok = toks[1];
            haveBacksightTok = true;
            inTraverse = true;
            sawAnyTraverse = true;
            continue;
        }

        if (code == "T") {
            if (!inTraverse) return fail("T line found outside a TB...TE traverse");
            if (toks.size() < 4)
                return fail("expected T <name> <angle> <dist> [angle-stderr dist-stderr]");
            double angle, dist;
            if (!ParseStarNetAngle(toks[2], &angle))
                return fail("T: unrecognized angle \"" + toks[2] + "\"");
            if (!ParseDoubleTok(toks[3], &dist))
                return fail("T: unrecognized distance \"" + toks[3] + "\"");
            if (toks.size() > 6)
                return fail("T: a 3D-looking line (extra tokens beyond angle/dist and "
                            "two std errors) is not supported — 2D traverse data only");

            PendingLeg pl;
            pl.atName = toks[1];
            pl.angleDeg = angle;
            pl.distFt = dist;
            if (toks.size() == 6) {
                double se1, se2;
                if (ParseDoubleTok(toks[4], &se1) && ParseDoubleTok(toks[5], &se2)) {
                    pl.hasTsErr = true;
                    pl.tsAngleSec = se1;
                    pl.tsDistConst = se2;
                }
                // Non-numeric (&, !, *) std-error entries fall back to GoSurvey's
                // traverse-wide defaults — a documented simplification (GoSurvey
                // doesn't model StarNet's per-observation fixed/free fixity).
            } else if (toks.size() == 5) {
                return fail("T: a single trailing token is ambiguous here (STAR*NET uses it "
                            "for an HI/HT entry, which 2D traverse data doesn't support)");
            }

            if (!havePrevOccupied) {
                firstOccupied = pl.atName;
            } else {
                // The PREVIOUS T line's angle/distance were TO this line's station —
                // resolve that pending leg now that its foresight is named.
                PendingLeg& prev = pending.back();
                TraverseLeg leg;
                leg.stationId = 0;  // resolved to a sequential id pass below.
                leg.description = pl.atName;
                leg.hasHorizAngle = true;
                leg.horizAngleDeg = prev.angleDeg;
                leg.hasHorizDist = true;
                leg.horizDist = prev.distFt;
                leg.isZenithAngle = true;
                if (prev.hasTsErr) {
                    leg.hasTsWeights = true;
                    leg.tsWeights.sigmaAngleSec = prev.tsAngleSec;
                    leg.tsWeights.sigmaDistConstFt = prev.tsDistConst;
                }
                legs.push_back(std::move(leg));
            }
            pending.push_back(pl);
            havePrevOccupied = true;
            continue;
        }

        if (code == "TE") {
            if (!inTraverse) return fail("TE line found without a preceding TB");
            if (toks.size() < 2) return fail("expected TE <name>");
            if (toks.size() > 2)
                return fail("TE: a closing angle is not supported (end the traverse with "
                            "just TE <name>)");
            if (pending.empty())
                return fail("TE with no T lines in between — nothing to close");

            PendingLeg& prev = pending.back();
            TraverseLeg leg;
            leg.description = toks[1];
            leg.hasHorizAngle = true;
            leg.horizAngleDeg = prev.angleDeg;
            leg.hasHorizDist = true;
            leg.horizDist = prev.distFt;
            leg.isZenithAngle = true;
            if (prev.hasTsErr) {
                leg.hasTsWeights = true;
                leg.tsWeights.sigmaAngleSec = prev.tsAngleSec;
                leg.tsWeights.sigmaDistConstFt = prev.tsDistConst;
            }
            legs.push_back(std::move(leg));

            endStation = toks[1];
            haveEndStation = true;
            inTraverse = false;
            continue;
        }

        return fail("unsupported STAR*NET record type \"" + toks[0] +
                    "\" (this importer handles only C, TB, T, TE — conventional 2D traverse "
                    "data, per issue #845's scope decision)");
    }

    if (!sawAnyTraverse || !haveEndStation) {
        errorMsg = "no complete TB...TE traverse found";
        return false;
    }
    if (legs.empty()) {
        errorMsg = "traverse has no legs";
        return false;
    }

    // ---------- sequential integer id map (preserves encounter order) --------
    std::unordered_map<std::string, int> ptIdMap;
    int nextId = 1;
    auto GetId = [&](const std::string& name) -> int {
        auto it = ptIdMap.find(name);
        if (it != ptIdMap.end()) return it->second;
        const int id = nextId++;
        ptIdMap[name] = id;
        return id;
    };
    GetId(firstOccupied);
    for (auto& leg : legs) leg.stationId = GetId(leg.description);

    td = TraverseData{};
    td.startStationId = GetId(firstOccupied);
    {
        const auto it = knownPts.find(firstOccupied);
        if (it != knownPts.end() && it->second.has) {
            td.startNorthing = it->second.n;
            td.startEasting = it->second.e;
            if (it->second.hasZ) td.startElevation = it->second.z;
        }
    }

    // ---------- reference bearing from TB's backsight -------------------------
    if (haveBacksightTok) {
        double az;
        if (ParseStarNetBearingOrAzimuth(backsightTok, &az)) {
            td.startBearingDeg = az;
            td.hasStartBearing = true;
            td.startBearingBuf = TraverseFormatBearing(az);
        } else {
            const auto itStn = knownPts.find(firstOccupied);
            const auto itBs = knownPts.find(backsightTok);
            if (itStn != knownPts.end() && itStn->second.has && itBs != knownPts.end() && itBs->second.has) {
                const double dE = itBs->second.e - itStn->second.e;
                const double dN = itBs->second.n - itStn->second.n;
                double azc = std::atan2(dE, dN) * (180.0 / kPi);
                if (azc < 0.0) azc += 360.0;
                td.startBearingDeg = azc;
                td.hasStartBearing = true;
                td.startBearingBuf = TraverseFormatBearing(azc);
            }
            // Otherwise: no coordinates for the backsight station, so no bearing
            // can be resolved — td.hasStartBearing stays false, and the caller's
            // ComputeTraverse will surface "Starting bearing required" rather than
            // this importer guessing one.
        }
    }

    td.legs = std::move(legs);

    // ---------- closed-loop detection: TE's end station == the first occupied one.
    if (endStation == firstOccupied)
        td.isClosedLoop = true;

    return true;
}
