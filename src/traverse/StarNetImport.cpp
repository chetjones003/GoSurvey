#include "StarNetImport.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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

std::string TrimCopy(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    return text.substr(begin, end - begin);
}

// A STAR*NET '#' starts a comment. A leading '\'' or '`' starts a descriptor
// (the field code). Both are kept: the comment becomes a GoSurvey `//` line,
// and the descriptor becomes `CODE` on the measurement.
struct StarNetLineParts {
    std::string body;
    std::string descriptor;
    std::string trailingComment;
    bool fullLineComment = false;
    std::string fullComment;
};

StarNetLineParts SplitStarNetLine(const std::string& line) {
    StarNetLineParts parts;
    const std::string trimmed = TrimCopy(line);
    if (!trimmed.empty() && trimmed[0] == '#') {
        parts.fullLineComment = true;
        parts.fullComment = TrimCopy(trimmed.substr(1));
        return parts;
    }
    const size_t hash = line.find('#');
    std::string head = hash == std::string::npos ? line : line.substr(0, hash);
    if (hash != std::string::npos) parts.trailingComment = TrimCopy(line.substr(hash + 1));
    size_t quote = std::string::npos;
    for (const char q : {'\'', '`'}) {
        const size_t at = head.find(q);
        if (at != std::string::npos && (quote == std::string::npos || at < quote)) quote = at;
    }
    if (quote != std::string::npos) {
        parts.descriptor = TrimCopy(head.substr(quote + 1));
        head.resize(quote);
    }
    parts.body = head;
    return parts;
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

struct KnownPt {
    double n = 0.0, e = 0.0, z = 0.0;
    bool has = false, hasZ = false;
    NetworkFix eFix = NetworkFix::Free;
    NetworkFix nFix = NetworkFix::Free;
    NetworkFix zFix = NetworkFix::Free;
    double eSig = 0.0, nSig = 0.0, zSig = 0.0;
    std::string code;
    int sourceOrder = 0;
};

struct NamedAngle {
    std::string at, from, to;
    double deg = 0.0;
    double sigSec = 0.0;
    std::string code;
    int sourceOrder = 0;
};
struct NamedDist {
    std::string from, to;
    double dist = 0.0;
    double sig = 0.0;
    std::string code;
    int sourceOrder = 0;
};
struct NamedAzimuth {
    std::string from, to;
    double deg = 0.0;
    double sigSec = 0.0;
    std::string code;
    int sourceOrder = 0;
};
struct NamedZenith {
    std::string from, to;
    double deg = 0.0;
    double sigSec = 0.0;
    std::string code;
    int sourceOrder = 0;
};
struct NamedEdif {
    std::string from, to;
    double dz = 0.0;
    double sig = 0.0;
    bool isLevelRun = false;
    std::string code;
    int sourceOrder = 0;
};

std::vector<std::string> SplitStationList(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (c == '-') {
            if (current.empty()) return {};
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

std::vector<std::string> ExpandFixityTokens(const std::vector<std::string>& tokens) {
    std::vector<std::string> expanded;
    for (const std::string& tok : tokens) {
        bool fixityRun = !tok.empty();
        for (const char c : tok)
            if (c != '!' && c != '*' && c != '&') fixityRun = false;
        if (fixityRun && tok.size() > 1) {
            for (const char c : tok) expanded.emplace_back(1, c);
        } else {
            expanded.push_back(tok);
        }
    }
    return expanded;
}

bool ParseFixOrSigma(const std::string& tok, NetworkFix* fix, double* sigma) {
    if (tok == "!") { *fix = NetworkFix::Fixed; *sigma = 0.0; return true; }
    if (tok == "*" || tok == "&") { *fix = NetworkFix::Free; *sigma = 0.0; return true; }
    double value = 0.0;
    if (!ParseDoubleTok(tok, &value) || !(value > 0.0)) return false;
    *fix = NetworkFix::Weighted;
    *sigma = value;
    return true;
}

// A GPS vector ("G1" record) plus its optional covariance ("G2"/"G3"), still
// keyed by station NAME — resolved to GoSurvey's sequential integer ids
// alongside the traverse legs' own names, after the main parse loop.
struct RawGpsVector {
    std::string fromName, toName;
    double dx = 0.0, dy = 0.0, dz = 0.0;
    double varX = 0.0, varY = 0.0, varZ = 0.0;
    double covXY = 0.0, covXZ = 0.0, covYZ = 0.0;
    bool hasCovariance = false;
};

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
    bool fromAtTo = false; // default angle order is At-From-To.
    bool threeD = false;
    bool deltaOn = false;  // .DELTA ON: vertical values are elevation differences.
    std::vector<NamedAngle> rawAngles;
    std::vector<NamedDist> rawDists;
    std::vector<NamedAzimuth> rawAzimuths;
    std::vector<NamedZenith> rawZeniths;
    std::vector<NamedEdif> rawEdifs;
    std::vector<std::string> skippedInlineOptions;
    bool dataOn = true;
    double linearToFeet = 1.0;
    bool inDirectionSet = false;
    int currentSetId = -1;
    std::string directionAt;
    int nextSetId = 0;
    struct NamedDir {
        int setId = 0;
        std::string at;
        std::string to;
        double dirDeg = 0.0;
        double sigSec = 0.0;
        double horizFt = 0.0;
        double zenithDeg = 90.0;
        bool hasDist = false;
        bool hasZenith = false;
        std::string code;
        int sourceOrder = 0;
    };
    std::vector<NamedDir> rawDirs;

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
        std::string code;
        int sourceOrder = 0;
    };
    std::vector<PendingLeg> pending;  // one per T line, foresight resolved afterward.

    std::vector<TraverseLeg> legs;
    std::string endStation;
    bool haveEndStation = false;

    // --- GPS vectors (GitHub issue #847) ---
    // StarNet's GPS weighting mode is a file-wide inline option; it must be
    // declared (and must be COVARIANCE — the alternate "STDERRCORR" encoding's
    // exact field semantics could not be confirmed against an authoritative
    // source, see StarNetImport.hpp) before any G-records are read.
    enum class GpsWeightMode : std::uint8_t { kNone, kCovariance };
    GpsWeightMode gpsWeightMode = GpsWeightMode::kNone;
    std::vector<RawGpsVector> rawGpsVectors;
    bool havePendingG1 = false;
    RawGpsVector pendingGv;
    bool pendingHasG2 = false;

    std::string line;
    int lineNo = 0;
    int nextSourceOrder = 1;
    std::vector<TraverseData::SourceComment> sourceComments;
    std::string lineDescriptor;
    std::string lineTrailing;
    bool lineOrdered = false;
    int lineOrder = 0;
    auto orderThisLine = [&]() -> int {
        if (!lineOrdered) {
            lineOrdered = true;
            lineOrder = nextSourceOrder++;
            if (!lineTrailing.empty()) {
                TraverseData::SourceComment note;
                note.beforeOrder = nextSourceOrder;
                note.text = lineTrailing;
                sourceComments.push_back(note);
            }
        }
        return lineOrder;
    };
    auto tag = [&](auto& obs) {
        obs.code = lineDescriptor;
        obs.sourceOrder = orderThisLine();
    };
    while (std::getline(f, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (TrimCopy(line).empty()) {
            TraverseData::SourceComment note;
            note.beforeOrder = nextSourceOrder;
            note.blankLine = true;
            sourceComments.push_back(note);
            continue;
        }
        const StarNetLineParts parts = SplitStarNetLine(line);
        if (parts.fullLineComment) {
            if (!parts.fullComment.empty()) {
                TraverseData::SourceComment note;
                note.beforeOrder = nextSourceOrder;
                note.text = parts.fullComment;
                sourceComments.push_back(note);
            }
            continue;
        }
        lineDescriptor = parts.descriptor;
        lineTrailing = parts.trailingComment;
        lineOrdered = false;
        const std::string stripped = parts.body;
        const auto toks = Tokenize(stripped);
        if (toks.empty()) continue;

        auto fail = [&](const std::string& msg) -> bool {
            errorMsg = "line " + std::to_string(lineNo) + ": " + msg;
            return false;
        };

        const std::string code = ToUpper(toks[0]);

        auto flushPendingGv = [&]() {
            if (havePendingG1) {
                rawGpsVectors.push_back(pendingGv);
                havePendingG1 = false;
                pendingHasG2 = false;
                pendingGv = RawGpsVector{};
            }
        };

        if (!code.empty() && code[0] == '.') {
            if (code == ".GPS") {
                if (toks.size() < 3 || ToUpper(toks[1]) != "WEIGHT")
                    return fail("unsupported \".GPS\" inline option (only \".GPS WEIGHT "
                                "COVARIANCE\" is supported)");
                const std::string mode = ToUpper(toks[2]);
                if (mode == "COVARIANCE") {
                    gpsWeightMode = GpsWeightMode::kCovariance;
                } else if (mode == "STDERRCORR") {
                    return fail("\".GPS WEIGHT STDERRCORR\" is not supported — its exact G2/G3 "
                                "field semantics (standard errors + correlations vs. raw "
                                "covariance) could not be confirmed against an authoritative "
                                "source; re-export the GPS vectors in COVARIANCE mode");
                } else {
                    return fail("unrecognized \".GPS WEIGHT\" mode \"" + toks[2] + "\"");
                }
                continue;
            }
            if (code == ".DAT" || code == ".DATA") {
                const std::string arg = toks.size() >= 2 ? ToUpper(toks[1]) : "ON";
                dataOn = (arg != "OFF");
                continue;
            }
            if (code == ".UNIT" || code == ".UNITS") {
                if (toks.size() >= 2 && ToUpper(toks[1]).rfind("MET", 0) == 0)
                    linearToFeet = 1.0 / 0.3048;
                else if (toks.size() < 2 || ToUpper(toks[1]).rfind("FEET", 0) == 0 || ToUpper(toks[1]) == "FT")
                    linearToFeet = 1.0;
                else
                    skippedInlineOptions.push_back(stripped);
                continue;
            }
            if (code == ".2D") { threeD = false; continue; }
            if (code == ".3D") { threeD = true; continue; }
            if (code == ".DELTA") {
                deltaOn = toks.size() < 2 || ToUpper(toks[1]) != "OFF";
                continue;
            }
            if (code != ".ORDER") {
                // Not one of our settings. Keep the original line so the editor
                // can show it as a comment (D-2026-10-13-e). Do not fail the file.
                skippedInlineOptions.push_back(stripped);
                continue;
            }
            for (size_t i = 1; i < toks.size(); ++i) {
                const std::string a = ToUpper(toks[i]);
                if (a == "EN") orderEN = true;
                else if (a == "NE") orderEN = false;
                else if (a == "FROMATTO") fromAtTo = true;
                else if (a == "ATFROMTO") fromAtTo = false;
            }
            continue;
        }

        if (!dataOn) continue;

        if (code == "G0" || code == "G1" || code == "G2" || code == "G3" || code == "G4") {
            if (gpsWeightMode != GpsWeightMode::kCovariance)
                return fail("a G-record (GPS vector) was found before \".GPS WEIGHT "
                            "COVARIANCE\" was declared");

            if (code == "G0") {
                // Free-text/date header for the NEXT vector — not used for any
                // computation, so its exact field layout doesn't need to be known.
                flushPendingGv();
                continue;
            }
            if (code == "G4")
                return fail("\"G4\" GPS records are not supported — no authoritative "
                            "description of this record type could be found");

            if (code == "G1") {
                flushPendingGv();
                if (toks.size() < 5) return fail("expected G1 <from>-<to> <dX> <dY> <dZ> [...]");
                const size_t dash = toks[1].find('-', 1);  // skip a possible leading sign
                if (dash == std::string::npos)
                    return fail("G1: expected \"<from>-<to>\" station pair \"" + toks[1] + "\"");
                pendingGv.fromName = toks[1].substr(0, dash);
                pendingGv.toName = toks[1].substr(dash + 1);
                if (!ParseDoubleTok(toks[2], &pendingGv.dx) ||
                    !ParseDoubleTok(toks[3], &pendingGv.dy) ||
                    !ParseDoubleTok(toks[4], &pendingGv.dz))
                    return fail("G1: dX/dY/dZ must be numeric");
                // A trailing "a/b"-style token (observed in real STAR*NET output,
                // meaning unconfirmed — likely antenna heights) carries no field this
                // importer reads into any computation, so it is safely ignored rather
                // than rejected: nothing downstream depends on understanding it.
                havePendingG1 = true;
                continue;
            }
            if (code == "G2") {
                if (!havePendingG1) return fail("\"G2\" record with no preceding \"G1\"");
                if (toks.size() < 4) return fail("expected G2 <varX> <varY> <varZ>");
                if (!ParseDoubleTok(toks[1], &pendingGv.varX) ||
                    !ParseDoubleTok(toks[2], &pendingGv.varY) ||
                    !ParseDoubleTok(toks[3], &pendingGv.varZ))
                    return fail("G2: variance values must be numeric");
                pendingHasG2 = true;
                continue;
            }
            // code == "G3"
            if (!pendingHasG2) return fail("\"G3\" record with no preceding \"G2\"");
            if (toks.size() < 4) return fail("expected G3 <covXY> <covXZ> <covYZ>");
            if (!ParseDoubleTok(toks[1], &pendingGv.covXY) ||
                !ParseDoubleTok(toks[2], &pendingGv.covXZ) ||
                !ParseDoubleTok(toks[3], &pendingGv.covYZ))
                return fail("G3: covariance values must be numeric");
            pendingGv.hasCovariance = true;
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
            kp.n *= linearToFeet;
            kp.e *= linearToFeet;
            std::vector<std::string> rest;
            for (size_t i = 4; i < toks.size(); ++i) rest.push_back(toks[i]);
            rest = ExpandFixityTokens(rest);
            auto applyFix = [&](const std::string& tok, NetworkFix* fix, double* sig) {
                return ParseFixOrSigma(tok, fix, sig);
            };
            if (!rest.empty() && (rest[0] == "!" || rest[0] == "*" || rest[0] == "&")) {
                if (rest.size() < 2 || !applyFix(rest[0], &kp.nFix, &kp.nSig) ||
                    !applyFix(rest[1], &kp.eFix, &kp.eSig))
                    return fail("C: expected two fixity symbols after the coordinates");
                if (orderEN) std::swap(kp.eFix, kp.nFix), std::swap(kp.eSig, kp.nSig);
            } else if (!rest.empty()) {
                double z = 0.0;
                if (!ParseDoubleTok(rest[0], &z))
                    return fail("C: unrecognized token \"" + rest[0] + "\"");
                const bool looksLikeFixes = rest.size() >= 2 &&
                    (rest[1] == "!" || rest[1] == "*" || rest[1] == "&" || rest.size() >= 4);
                if (rest.size() == 1 || looksLikeFixes) {
                    kp.z = z;
                    kp.hasZ = true;
                    if (rest.size() >= 4) {
                        if (!applyFix(rest[1], &kp.nFix, &kp.nSig) || !applyFix(rest[2], &kp.eFix, &kp.eSig) ||
                            !applyFix(rest[3], &kp.zFix, &kp.zSig))
                            return fail("C: fixity after an elevation must be !, *, or a standard error");
                        if (orderEN) std::swap(kp.eFix, kp.nFix), std::swap(kp.eSig, kp.nSig);
                    }
                } else if (rest.size() >= 2) {
                    if (!applyFix(rest[0], &kp.nFix, &kp.nSig) || !applyFix(rest[1], &kp.eFix, &kp.eSig))
                        return fail("C: standard errors must be positive numbers");
                    if (orderEN) std::swap(kp.eFix, kp.nFix), std::swap(kp.eSig, kp.nSig);
                }
            }
            kp.z *= linearToFeet;
            kp.nSig *= linearToFeet;
            kp.eSig *= linearToFeet;
            kp.zSig *= linearToFeet;
            kp.code = lineDescriptor;
            kp.sourceOrder = orderThisLine();
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
            pl.distFt = dist * linearToFeet;
            pl.code = lineDescriptor;
            pl.sourceOrder = orderThisLine();
            if (toks.size() == 6) {
                double se1, se2;
                if (ParseDoubleTok(toks[4], &se1) && ParseDoubleTok(toks[5], &se2)) {
                    pl.hasTsErr = true;
                    pl.tsAngleSec = se1;
                    pl.tsDistConst = se2 * linearToFeet;
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
                leg.code = prev.code;
                leg.sourceOrder = prev.sourceOrder;
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
            leg.code = prev.code;
            leg.sourceOrder = prev.sourceOrder;
            legs.push_back(std::move(leg));

            endStation = toks[1];
            haveEndStation = true;
            inTraverse = false;
            continue;
        }

        auto observationSigma = [&](const std::string& tok, double* sigma) -> bool {
            if (tok == "&") { *sigma = 0.0; return true; }
            if (tok == "*") return false;
            if (tok == "!") { *sigma = 0.01; return true; }
            return ParseDoubleTok(tok, sigma) && *sigma > 0.0;
        };

        if (code == "DB" || code == "DE" || code == "DM" || code == "DN") {
            if (code == "DE") { inDirectionSet = false; continue; }
            if (code == "DB") {
                if (toks.size() < 2) return fail("expected DB <station>");
                directionAt = toks[1];
                currentSetId = nextSetId++;
                inDirectionSet = true;
                continue;
            }
            if (!inDirectionSet) return fail(code + " found outside a DB...DE direction set");
            if (toks.size() < 3) return fail(code + ": expected a station and a direction");
            double dirDeg = 0.0;
            if (!ParseStarNetAngle(toks[2], &dirDeg))
                return fail(code + ": unrecognized direction \"" + toks[2] + "\"");
            NamedDir shot;
            shot.setId = currentSetId;
            shot.at = directionAt;
            shot.to = toks[1];
            shot.dirDeg = dirDeg;
            if (code == "DM" && toks.size() >= 4) {
                double slope = 0.0;
                if (!ParseDoubleTok(toks[3], &slope))
                    return fail("DM: unrecognized distance \"" + toks[3] + "\"");
                double zenith = 90.0;
                bool haveZenith = false;
                double hi = 0.0, ht = 0.0;
                bool haveHiHt = false;
                if (toks.size() >= 5 && toks[4].find('/') != std::string::npos) {
                    const size_t slash = toks[4].find('/');
                    haveHiHt = ParseDoubleTok(toks[4].substr(0, slash), &hi) &&
                               ParseDoubleTok(toks[4].substr(slash + 1), &ht);
                } else if (toks.size() >= 5 && ParseStarNetAngle(toks[4], &zenith)) {
                    haveZenith = true;
                    if (toks.size() >= 6 && toks[5].find('/') != std::string::npos) {
                        const size_t slash = toks[5].find('/');
                        haveHiHt = ParseDoubleTok(toks[5].substr(0, slash), &hi) &&
                                   ParseDoubleTok(toks[5].substr(slash + 1), &ht);
                    }
                }
                const bool face2 = haveZenith && zenith > 180.0;
                if (face2) {
                    zenith = 360.0 - zenith;
                    shot.dirDeg += 180.0;
                    if (shot.dirDeg >= 360.0) shot.dirDeg -= 360.0;
                }
                const double zenRad = zenith * (kPi / 180.0);
                double horiz = slope * std::sin(zenRad);
                double dz = slope * std::cos(zenRad);
                if (haveHiHt) dz += (hi - ht);
                if (haveZenith && haveHiHt) {
                    zenith = std::atan2(horiz, dz) * (180.0 / kPi);
                    if (zenith < 0.0) zenith += 360.0;
                }
                shot.hasDist = true;
                shot.horizFt = horiz * linearToFeet;
                shot.hasZenith = haveZenith;
                shot.zenithDeg = zenith;
            }
            tag(shot);
            rawDirs.push_back(shot);
            continue;
        }

        if (code == "A" || code == "D" || code == "V" || code == "B" || code == "M" || code == "L" ||
            code == "BM" || code == "DV") {
            if (toks.size() < 3) return fail(code + ": missing station list or measurement");
            const std::vector<std::string> names = SplitStationList(toks[1]);
            if (code == "A" || code == "M" || code == "BM" || code == "DV") {
                const size_t need = (code == "A" || code == "M") ? 3 : 2;
                if (names.size() != need)
                    return fail(code + ": expected " + std::to_string(need) + " station names in \"" + toks[1] + "\"");
            } else if (names.size() != 2) {
                return fail(code + ": expected from-to station names in \"" + toks[1] + "\"");
            }

            if (code == "A") {
                double deg = 0.0;
                if (!ParseStarNetAngle(toks[2], &deg)) return fail("A: unrecognized angle \"" + toks[2] + "\"");
                NamedAngle angle;
                if (fromAtTo) { angle.from = names[0]; angle.at = names[1]; angle.to = names[2]; }
                else { angle.at = names[0]; angle.from = names[1]; angle.to = names[2]; }
                angle.deg = deg;
                if (toks.size() >= 4 && toks[3] == "*") continue;
                if (toks.size() >= 4 && !observationSigma(toks[3], &angle.sigSec))
                    return fail("A: unrecognized standard error \"" + toks[3] + "\"");
                tag(angle);
                rawAngles.push_back(angle);
                continue;
            }
            if (code == "D") {
                double dist = 0.0;
                if (!ParseDoubleTok(toks[2], &dist)) return fail("D: unrecognized distance \"" + toks[2] + "\"");
                NamedDist distObs;
                distObs.from = names[0];
                distObs.to = names[1];
                distObs.dist = dist * linearToFeet;
                if (toks.size() >= 4 && toks[3] == "*") continue;
                if (toks.size() >= 4 && !observationSigma(toks[3], &distObs.sig))
                    return fail("D: unrecognized standard error \"" + toks[3] + "\"");
                tag(distObs);
                rawDists.push_back(distObs);
                continue;
            }
            if (code == "B") {
                double deg = 0.0;
                if (!ParseStarNetBearingOrAzimuth(toks[2], &deg) && !ParseStarNetAngle(toks[2], &deg))
                    return fail("B: unrecognized bearing \"" + toks[2] + "\"");
                NamedAzimuth azimuth;
                azimuth.from = names[0];
                azimuth.to = names[1];
                azimuth.deg = deg;
                if (toks.size() >= 4 && toks[3] == "*") continue;
                if (toks.size() >= 4 && !observationSigma(toks[3], &azimuth.sigSec))
                    return fail("B: unrecognized standard error \"" + toks[3] + "\"");
                tag(azimuth);
                rawAzimuths.push_back(azimuth);
                continue;
            }
            if (code == "V" || code == "L") {
                double value = 0.0;
                const bool asAngle = (code == "V" && !deltaOn);
                if (asAngle) {
                    if (!ParseStarNetAngle(toks[2], &value))
                        return fail("V: unrecognized zenith \"" + toks[2] + "\"");
                } else if (!ParseDoubleTok(toks[2], &value)) {
                    return fail(code + ": unrecognized elevation difference \"" + toks[2] + "\"");
                }
                double sig = 0.0;
                if (code == "L") {
                    if (toks.size() >= 4 && (toks.back() == "!" || toks.back() == "*" || toks.back() == "&" ||
                                             (toks.size() >= 4 && toks[toks.size() - 1].find('-') == std::string::npos &&
                                              toks.size() > 4))) {
                        if (toks.back() == "*") continue;
                        if (toks.back() == "!" || toks.back() == "&" || ParseDoubleTok(toks.back(), &sig)) {
                            if (toks.back() == "!") sig = 0.01;
                            if (toks.back() == "&") sig = 0.0;
                        }
                    }
                } else if (toks.size() >= 4 && toks[3] != "*") {
                    if (!observationSigma(toks[3], &sig))
                        return fail("V: unrecognized standard error \"" + toks[3] + "\"");
                } else if (toks.size() >= 4 && toks[3] == "*") {
                    continue;
                }
                if (asAngle) {
                    NamedZenith zenith;
                    zenith.from = names[0];
                    zenith.to = names[1];
                    zenith.deg = value;
                    zenith.sigSec = sig;
                    tag(zenith);
                    rawZeniths.push_back(zenith);
                } else {
                    NamedEdif diff;
                    diff.from = names[0];
                    diff.to = names[1];
                    diff.dz = value * linearToFeet;
                    diff.sig = sig;
                    diff.isLevelRun = (code == "L");
                    tag(diff);
                    rawEdifs.push_back(diff);
                }
                continue;
            }
            if (code == "DV") {
                double dist = 0.0;
                if (!ParseDoubleTok(toks[2], &dist)) return fail("DV: unrecognized distance");
                NamedDist distObs;
                distObs.from = names[0];
                distObs.to = names[1];
                distObs.dist = dist * linearToFeet;
                tag(distObs);
                rawDists.push_back(distObs);
                if (toks.size() >= 4) {
                    double vertical = 0.0;
                    if (deltaOn) {
                        if (!ParseDoubleTok(toks[3], &vertical)) return fail("DV: unrecognized elevation difference");
                        NamedEdif diff;
                        diff.from = names[0];
                        diff.to = names[1];
                        diff.dz = vertical * linearToFeet;
                        tag(diff);
                        rawEdifs.push_back(diff);
                    } else if (ParseStarNetAngle(toks[3], &vertical)) {
                        NamedZenith zenith;
                        zenith.from = names[0];
                        zenith.to = names[1];
                        zenith.deg = vertical;
                        tag(zenith);
                        rawZeniths.push_back(zenith);
                    }
                }
                continue;
            }
            // M or BM
            double first = 0.0;
            double dist = 0.0;
            if (code == "M") {
                if (!ParseStarNetAngle(toks[2], &first)) return fail("M: unrecognized angle");
            } else if (!ParseStarNetBearingOrAzimuth(toks[2], &first) && !ParseStarNetAngle(toks[2], &first)) {
                return fail("BM: unrecognized bearing");
            }
            if (toks.size() < 4 || !ParseDoubleTok(toks[3], &dist))
                return fail(code + ": unrecognized distance");
            if (code == "M") {
                NamedAngle angle;
                if (fromAtTo) { angle.from = names[0]; angle.at = names[1]; angle.to = names[2]; }
                else { angle.at = names[0]; angle.from = names[1]; angle.to = names[2]; }
                angle.deg = first;
                tag(angle);
                rawAngles.push_back(angle);
                NamedDist distObs;
                distObs.from = angle.at;
                distObs.to = angle.to;
                distObs.dist = dist * linearToFeet;
                tag(distObs);
                rawDists.push_back(distObs);
            } else {
                NamedAzimuth azimuth;
                azimuth.from = names[0];
                azimuth.to = names[1];
                azimuth.deg = first;
                tag(azimuth);
                rawAzimuths.push_back(azimuth);
                NamedDist distObs;
                distObs.from = names[0];
                distObs.to = names[1];
                distObs.dist = dist * linearToFeet;
                tag(distObs);
                rawDists.push_back(distObs);
            }
            if (toks.size() >= 5) {
                double vertical = 0.0;
                const bool verticalIsAngle = !deltaOn && ParseStarNetAngle(toks[4], &vertical);
                if (verticalIsAngle || (deltaOn && ParseDoubleTok(toks[4], &vertical))) {
                    if (verticalIsAngle) {
                        NamedZenith zenith;
                        zenith.from = (code == "M") ? (fromAtTo ? names[1] : names[0]) : names[0];
                        zenith.to = (code == "M") ? names[2] : names[1];
                        if (code == "M" && !fromAtTo) zenith.from = names[0];
                        zenith.deg = vertical;
                        tag(zenith);
                        rawZeniths.push_back(zenith);
                    } else {
                        NamedEdif diff;
                        diff.from = (code == "BM") ? names[0] : (fromAtTo ? names[1] : names[0]);
                        diff.to = (code == "BM" || fromAtTo) ? names.back() : names[2];
                        diff.dz = vertical * linearToFeet;
                        tag(diff);
                        rawEdifs.push_back(diff);
                    }
                }
            }
            continue;
        }

        return fail("unsupported STAR*NET record type \"" + toks[0] +
                    "\" (supported records are C, A, D, V, B, M, BM, DV, L, TB, T, TE, and G0-G3)");
    }
    if (havePendingG1) rawGpsVectors.push_back(pendingGv);

    const bool hasNetworkRecords = !rawAngles.empty() || !rawDists.empty() || !rawAzimuths.empty() ||
                                   !rawZeniths.empty() || !rawEdifs.empty() || !rawDirs.empty();
    if (sawAnyTraverse && !haveEndStation) {
        errorMsg = "no complete TB...TE traverse found";
        return false;
    }
    if (sawAnyTraverse && legs.empty()) {
        errorMsg = "traverse has no legs";
        return false;
    }
    if (!sawAnyTraverse && !hasNetworkRecords) {
        errorMsg = "no complete TB...TE traverse found";
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
    if (sawAnyTraverse) GetId(firstOccupied);
    for (auto& leg : legs) leg.stationId = GetId(leg.description);
    // GPS vector endpoints share the same id space as traverse station names,
    // so a vector tied to a conventional station resolves against it directly.
    std::vector<TraverseGpsVector> gpsVectors;
    gpsVectors.reserve(rawGpsVectors.size());
    for (const auto& rgv : rawGpsVectors) {
        TraverseGpsVector gv;
        gv.fromName = rgv.fromName;
        gv.toName = rgv.toName;
        gv.fromStationId = GetId(rgv.fromName);
        gv.toStationId = GetId(rgv.toName);
        gv.deltaXEcefM = rgv.dx;
        gv.deltaYEcefM = rgv.dy;
        gv.deltaZEcefM = rgv.dz;
        gv.varXM2 = rgv.varX;
        gv.varYM2 = rgv.varY;
        gv.varZM2 = rgv.varZ;
        gv.covXYM2 = rgv.covXY;
        gv.covXZM2 = rgv.covXZ;
        gv.covYZM2 = rgv.covYZ;
        gv.hasCovariance = rgv.hasCovariance;
        gpsVectors.push_back(std::move(gv));
    }

    td = TraverseData{};
    if (sawAnyTraverse) td.startStationId = GetId(firstOccupied);
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
    td.gpsVectors = std::move(gpsVectors);
    td.importerSkippedInlineOptions = std::move(skippedInlineOptions);
    td.sourceComments = std::move(sourceComments);

    for (const auto& known : knownPts) {
        NetworkStation station;
        station.id = GetId(known.first);
        station.northing.value = known.second.n;
        station.northing.present = true;
        station.northing.fix = known.second.nFix;
        station.northing.stdError = known.second.nSig;
        station.easting.value = known.second.e;
        station.easting.present = true;
        station.easting.fix = known.second.eFix;
        station.easting.stdError = known.second.eSig;
        if (known.second.hasZ) {
            station.elevation.value = known.second.z;
            station.elevation.present = true;
            station.elevation.fix = known.second.zFix;
            station.elevation.stdError = known.second.zSig;
        }
        station.code = known.second.code;
        station.sourceOrder = known.second.sourceOrder;
        td.networkStations.push_back(station);
    }
    for (const NamedAngle& angle : rawAngles) {
        NetworkAngleObs obs;
        obs.atId = GetId(angle.at);
        obs.fromId = GetId(angle.from);
        obs.toId = GetId(angle.to);
        obs.angleDeg = angle.deg;
        obs.stdErrorSec = angle.sigSec;
        obs.code = angle.code;
        obs.sourceOrder = angle.sourceOrder;
        td.networkAngles.push_back(obs);
    }
    for (const NamedDist& dist : rawDists) {
        NetworkDistObs obs;
        obs.fromId = GetId(dist.from);
        obs.toId = GetId(dist.to);
        obs.distance = dist.dist;
        obs.stdError = dist.sig;
        obs.code = dist.code;
        obs.sourceOrder = dist.sourceOrder;
        td.networkDists.push_back(obs);
    }
    for (const NamedAzimuth& azimuth : rawAzimuths) {
        NetworkAzimuthObs obs;
        obs.fromId = GetId(azimuth.from);
        obs.toId = GetId(azimuth.to);
        obs.azimuthDeg = azimuth.deg;
        obs.stdErrorSec = azimuth.sigSec;
        obs.code = azimuth.code;
        obs.sourceOrder = azimuth.sourceOrder;
        td.networkAzimuths.push_back(obs);
    }
    for (const NamedZenith& zenith : rawZeniths) {
        NetworkZenithObs obs;
        obs.fromId = GetId(zenith.from);
        obs.toId = GetId(zenith.to);
        obs.zenithDeg = zenith.deg;
        obs.stdErrorSec = zenith.sigSec;
        obs.code = zenith.code;
        obs.sourceOrder = zenith.sourceOrder;
        td.networkZeniths.push_back(obs);
    }
    struct SeedPt { double e = 0.0, n = 0.0, up = 0.0; bool has = false; };
    std::unordered_map<std::string, SeedPt> seeds;
    for (const auto& known : knownPts) {
        SeedPt seed;
        seed.e = known.second.e;
        seed.n = known.second.n;
        seed.up = known.second.z;
        seed.has = true;
        seeds[known.first] = seed;
    }
    for (int pass = 0; pass < static_cast<int>(rawDirs.size()) + 2; ++pass) {
        bool grew = false;
        for (int set = 0; set < nextSetId; ++set) {
            const NamedDir* atShot = nullptr;
            for (const NamedDir& shot : rawDirs)
                if (shot.setId == set) { atShot = &shot; break; }
            if (atShot == nullptr || !seeds[atShot->at].has) continue;
            bool haveOrient = false;
            double orient = 0.0;
            for (const NamedAzimuth& azimuth : rawAzimuths) {
                if (azimuth.from != atShot->at) continue;
                for (const NamedDir& shot : rawDirs) {
                    if (shot.setId != set || shot.to != azimuth.to) continue;
                    orient = azimuth.deg * (kPi / 180.0) - shot.dirDeg * (kPi / 180.0);
                    haveOrient = true;
                    break;
                }
                if (haveOrient) break;
            }
            if (!haveOrient) {
                for (const NamedDir& shot : rawDirs) {
                    if (shot.setId != set || !seeds[shot.to].has) continue;
                    const SeedPt& from = seeds[atShot->at];
                    const SeedPt& to = seeds[shot.to];
                    const double az = std::atan2(to.e - from.e, to.n - from.n);
                    orient = az - shot.dirDeg * (kPi / 180.0);
                    haveOrient = true;
                    break;
                }
            }
            if (!haveOrient) continue;
            const SeedPt& from = seeds[atShot->at];
            for (const NamedDir& shot : rawDirs) {
                if (shot.setId != set || seeds[shot.to].has || !shot.hasDist) continue;
                const double az = orient + shot.dirDeg * (kPi / 180.0);
                SeedPt placed;
                placed.e = from.e + shot.horizFt * std::sin(az);
                placed.n = from.n + shot.horizFt * std::cos(az);
                const double zen = shot.zenithDeg * (kPi / 180.0);
                const double tanZen = std::tan(zen);
                placed.up = from.up + (std::abs(tanZen) > 1e-8 ? shot.horizFt / tanZen : 0.0);
                placed.has = true;
                seeds[shot.to] = placed;
                grew = true;
            }
        }
        if (!grew) break;
    }
    for (const auto& seed : seeds) {
        if (!seed.second.has || knownPts.count(seed.first)) continue;
        NetworkStation station;
        station.id = GetId(seed.first);
        station.easting.value = seed.second.e;
        station.easting.present = true;
        station.easting.fix = NetworkFix::Free;
        station.northing.value = seed.second.n;
        station.northing.present = true;
        station.northing.fix = NetworkFix::Free;
        station.elevation.value = seed.second.up;
        station.elevation.present = true;
        station.elevation.fix = NetworkFix::Free;
        station.sourceOrder = nextSourceOrder++;
        td.networkStations.push_back(station);
    }
    for (const NamedDir& shot : rawDirs) {
        NetworkDirectionObs dir;
        dir.setId = shot.setId;
        dir.atId = GetId(shot.at);
        dir.toId = GetId(shot.to);
        dir.directionDeg = shot.dirDeg;
        dir.stdErrorSec = shot.sigSec;
        dir.code = shot.code;
        dir.sourceOrder = shot.sourceOrder;
        td.networkDirections.push_back(dir);
        if (shot.hasDist) {
            NetworkDistObs dist;
            dist.fromId = dir.atId;
            dist.toId = dir.toId;
            dist.distance = shot.horizFt;
            dist.code = shot.code;
            dist.sourceOrder = shot.sourceOrder;
            td.networkDists.push_back(dist);
        }
        if (shot.hasZenith) {
            NetworkZenithObs zenith;
            zenith.fromId = dir.atId;
            zenith.toId = dir.toId;
            zenith.zenithDeg = shot.zenithDeg;
            zenith.code = shot.code;
            zenith.sourceOrder = shot.sourceOrder;
            td.networkZeniths.push_back(zenith);
        }
    }
    for (const NamedEdif& diff : rawEdifs) {
        NetworkElevDiffObs obs;
        obs.fromId = GetId(diff.from);
        obs.toId = GetId(diff.to);
        obs.deltaZ = diff.dz;
        obs.stdError = diff.sig;
        obs.isLevelRun = diff.isLevelRun;
        obs.code = diff.code;
        obs.sourceOrder = diff.sourceOrder;
        td.networkElevDiffs.push_back(obs);
    }
    // The GPS tangent-plane origin (td.gpsOriginLatDeg/LonDeg) is not something
    // this importer can derive from a conventional .dat file — it must be set
    // by the user before ComputeTraverse will resolve any G-record vectors.

    // ---------- closed-loop detection: TE's end station == the first occupied one.
    if (endStation == firstOccupied)
        td.isClosedLoop = true;

    return true;
}
