#include "TraverseCommandLang.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace travcmd {
namespace {

std::string Fmt(double v, int prec = 6) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", prec, v);
    return buf;
}

std::string ToUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> toks;
    std::istringstream ss(line);
    std::string t;
    while (ss >> t) toks.push_back(t);
    return toks;
}

bool ParseIntTok(const std::string& s, int* out) {
    char* ep = nullptr;
    const long v = std::strtol(s.c_str(), &ep, 10);
    if (ep == s.c_str()) return false;
    *out = static_cast<int>(v);
    return true;
}

bool ParseDoubleTok(const std::string& s, double* out) {
    char* ep = nullptr;
    const double v = std::strtod(s.c_str(), &ep);
    if (ep == s.c_str()) return false;
    *out = v;
    return true;
}

}  // namespace

std::string Serialize(const TraverseData& td, const LsaWeights& weights) {
    std::ostringstream out;
    out << "# GoSurvey Traverse Command (GSTC) \xe2\x80\x94 edit and re-apply.\n";

    out << "STATION " << td.startStationId
        << " " << Fmt(td.startEasting)
        << " " << Fmt(td.startNorthing)
        << " " << Fmt(td.startElevation) << "\n";
    if (td.hasStartBearing)
        out << "BEARING " << Fmt(td.startBearingDeg) << "\n";

    out << "LOOP " << (td.isClosedLoop ? "ON" : "OFF") << "\n";
    out << "SCALE " << (td.applyScaleFactor ? "ON" : "OFF")
        << " " << (td.scaleFactorKind == TraverseData::ScaleFactorKind::GridOnly ? "GRID" : "COMBINED")
        << "\n";
    out << "ERRORS " << Fmt(weights.sigmaAngleSec, 3)
        << " " << Fmt(weights.sigmaDistConstFt, 4)
        << " " << Fmt(weights.sigmaDistPpm, 3)
        << "\n";

    for (size_t i = 0; i < td.legs.size(); ++i) {
        const TraverseLeg& leg = td.legs[i];
        const int at = (i == 0) ? td.startStationId : td.legs[i - 1].stationId;
        const int to = leg.stationId;

        out << "\nSTN " << at << " " << to << "\n";

        if (leg.hasHorizAngle) {
            const std::string from = (i == 0) ? "REF"
                                               : std::to_string((i == 1) ? td.startStationId
                                                                          : td.legs[i - 2].stationId);
            out << "HANG " << from << " " << at << " " << to << " " << Fmt(leg.horizAngleDeg) << "\n";
        }
        if (leg.hasHorizDist)
            out << "HDIST " << at << " " << to << " " << Fmt(leg.horizDist) << "\n";
        if (leg.hasSlopeDist)
            out << "SDIST " << at << " " << to << " " << Fmt(leg.slopeDist) << "\n";
        if (leg.hasVertAngle)
            out << "VANG " << at << " " << to << " " << Fmt(leg.vertAngleDeg)
                << " " << (leg.isZenithAngle ? "ZENITH" : "ELEV") << "\n";
        if (!leg.description.empty())
            out << "DESC " << at << " " << to << " " << leg.description << "\n";
    }
    return out.str();
}

bool Parse(const std::string& text, TraverseData& td, LsaWeights& weights, std::string& errorMsg) {
    TraverseData newTd;
    newTd.hasStartBearing = false;
    LsaWeights newWeights;
    bool sawStation = false;

    std::vector<TraverseLeg> newLegs;
    std::unordered_map<int, size_t> toIndex;  // TO station -> index in newLegs

    // The station occupied for the leg at newLegs[idx] — the previous leg's TO,
    // or the start station for the first leg.
    auto expectedAt = [&](size_t idx) -> int {
        return (idx == 0) ? newTd.startStationId : newLegs[idx - 1].stationId;
    };

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const size_t firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace == std::string::npos) continue;  // blank
        if (line[firstNonSpace] == '#') continue;          // comment

        const auto toks = Tokenize(line);
        if (toks.empty()) continue;
        const std::string directive = ToUpper(toks[0]);

        auto fail = [&](const std::string& msg) -> bool {
            errorMsg = "line " + std::to_string(lineNo) + ": " + msg;
            return false;
        };

        if (directive == "STATION") {
            int id = 0;
            double e, n, z;
            if (toks.size() < 5 || !ParseIntTok(toks[1], &id) || !ParseDoubleTok(toks[2], &e) ||
                !ParseDoubleTok(toks[3], &n) || !ParseDoubleTok(toks[4], &z))
                return fail("expected STATION <id> <e> <n> <z>");
            newTd.startStationId = id;
            newTd.startEasting = e;
            newTd.startNorthing = n;
            newTd.startElevation = z;
            sawStation = true;
        } else if (directive == "BEARING") {
            double deg;
            if (toks.size() < 2 || !ParseDoubleTok(toks[1], &deg))
                return fail("expected BEARING <deg>");
            newTd.startBearingDeg = deg;
            newTd.hasStartBearing = true;
            newTd.startBearingBuf = TraverseFormatBearing(deg);
        } else if (directive == "LOOP") {
            if (toks.size() < 2) return fail("expected LOOP ON|OFF");
            newTd.isClosedLoop = (ToUpper(toks[1]) == "ON");
        } else if (directive == "SCALE") {
            if (toks.size() < 2) return fail("expected SCALE ON|OFF [COMBINED|GRID]");
            newTd.applyScaleFactor = (ToUpper(toks[1]) == "ON");
            const std::string kind = (toks.size() >= 3) ? ToUpper(toks[2]) : "COMBINED";
            newTd.scaleFactorKind = (kind == "GRID") ? TraverseData::ScaleFactorKind::GridOnly
                                                      : TraverseData::ScaleFactorKind::Combined;
        } else if (directive == "ERRORS") {
            double a, d, p;
            if (toks.size() < 4 || !ParseDoubleTok(toks[1], &a) || !ParseDoubleTok(toks[2], &d) ||
                !ParseDoubleTok(toks[3], &p))
                return fail("expected ERRORS <angle-arcsec> <dist-const> <dist-ppm>");
            newWeights.sigmaAngleSec = a;
            newWeights.sigmaDistConstFt = d;
            newWeights.sigmaDistPpm = p;
        } else if (directive == "STN") {
            int at, to;
            if (toks.size() < 3 || !ParseIntTok(toks[1], &at) || !ParseIntTok(toks[2], &to))
                return fail("expected STN <at> <to>");
            if (toIndex.count(to))
                return fail("station " + std::to_string(to) + " already declared with STN");
            const int expected = expectedAt(newLegs.size());
            if (at != expected)
                return fail("AT " + std::to_string(at) + " doesn't continue the traverse chain (expected " +
                            std::to_string(expected) + ")");
            TraverseLeg leg;
            leg.stationId = to;
            leg.isZenithAngle = true;
            newLegs.push_back(std::move(leg));
            toIndex[to] = newLegs.size() - 1;
        } else if (directive == "HANG" || directive == "HDIST" || directive == "SDIST" ||
                   directive == "VANG" || directive == "DESC") {
            // All five share the same <at> <to> addressing, at a different token offset
            // for HANG (which carries <from> first).
            const size_t atPos = (directive == "HANG") ? 2 : 1;
            const size_t toPos = atPos + 1;
            if (toks.size() <= toPos)
                return fail("expected " + directive + " ... <at> <to> ...");
            int at, to;
            if (!ParseIntTok(toks[atPos], &at) || !ParseIntTok(toks[toPos], &to))
                return fail(directive + ": <at>/<to> must be integers");

            const auto it = toIndex.find(to);
            if (it == toIndex.end())
                return fail("station " + std::to_string(to) + " not declared (use STN " +
                            std::to_string(at) + " " + std::to_string(to) + " first)");
            const size_t idx = it->second;
            if (at != expectedAt(idx))
                return fail("AT " + std::to_string(at) + " doesn't match station " +
                            std::to_string(expectedAt(idx)) + " occupied for the leg to station " +
                            std::to_string(to));

            TraverseLeg& leg = newLegs[idx];
            if (directive == "HANG") {
                // toks[1] is FROM: unconstrained for the first leg (no modeled backsight
                // station — the literal REF is round-tripped there), otherwise must equal
                // the station occupied by the previous leg.
                if (idx > 0) {
                    int from;
                    if (!ParseIntTok(toks[1], &from))
                        return fail("HANG: <from> must be an integer after the first leg");
                    if (from != expectedAt(idx - 1))
                        return fail("FROM " + std::to_string(from) + " doesn't match station " +
                                    std::to_string(expectedAt(idx - 1)) + " occupied for the previous leg");
                }
                double ang;
                if (toks.size() < 5 || !ParseDoubleTok(toks[4], &ang))
                    return fail("expected HANG <from> <at> <to> <angle-deg>");
                leg.horizAngleDeg = ang;
                leg.hasHorizAngle = true;
                leg.horizAngleBuf = Fmt(ang);
            } else if (directive == "HDIST") {
                double dist;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &dist))
                    return fail("expected HDIST <at> <to> <dist>");
                leg.horizDist = dist;
                leg.hasHorizDist = (dist > 0.0);
            } else if (directive == "SDIST") {
                double dist;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &dist))
                    return fail("expected SDIST <at> <to> <dist>");
                leg.slopeDist = dist;
                leg.hasSlopeDist = (dist > 0.0);
            } else if (directive == "VANG") {
                double ang;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &ang))
                    return fail("expected VANG <at> <to> <angle-deg> [ZENITH|ELEV]");
                leg.vertAngleDeg = ang;
                leg.hasVertAngle = true;
                leg.vertAngleBuf = Fmt(ang);
                leg.isZenithAngle = (toks.size() < 5) || (ToUpper(toks[4]) != "ELEV");
            } else {  // DESC — everything after the <to> token, verbatim.
                auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)); };
                auto skipToken = [&](size_t p) -> size_t {
                    while (p < line.size() && isSpace(line[p])) ++p;
                    while (p < line.size() && !isSpace(line[p])) ++p;
                    return p;
                };
                size_t pos = skipToken(0);       // past DESC
                pos = skipToken(pos);            // past <at>
                pos = skipToken(pos);            // past <to>
                while (pos < line.size() && isSpace(line[pos])) ++pos;
                leg.description = (pos < line.size()) ? line.substr(pos) : "";
            }
        } else {
            return fail("unknown directive \"" + toks[0] + "\"");
        }
    }

    if (!sawStation) {
        errorMsg = "missing STATION directive";
        return false;
    }

    newTd.legs = std::move(newLegs);
    td = std::move(newTd);
    weights = newWeights;
    errorMsg.clear();
    return true;
}

}  // namespace travcmd
