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
    if (td.hasEndCoordinates)
        out << "ENDPOINT " << Fmt(td.endEasting) << " " << Fmt(td.endNorthing)
            << " " << Fmt(td.endElevation) << "\n";

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
        if (leg.hasPrismConstant || leg.hasPrismError)
            out << "PRISM " << at << " " << to
                << " " << Fmt(leg.hasPrismConstant ? leg.prismConstant : 0.0, 4)
                << " " << Fmt(leg.hasPrismError ? leg.prismError : 0.0, 4) << "\n";
        if (leg.hasPoleError)
            out << "POLE " << at << " " << to << " " << Fmt(leg.poleError, 4) << "\n";
        if (leg.hasTsWeights)
            out << "TSERR " << at << " " << to
                << " " << Fmt(leg.tsWeights.sigmaAngleSec, 3)
                << " " << Fmt(leg.tsWeights.sigmaDistConstFt, 4)
                << " " << Fmt(leg.tsWeights.sigmaDistPpm, 3) << "\n";
    }

    for (const TraverseSideShot& ss : td.sideShots) {
        out << "\nSTN " << ss.atStationId << " " << ss.stationId << " "
            << (ss.isCheckShot ? "CHECK" : "SIDE");
        if (ss.isCheckShot && ss.hasKnownCoords)
            out << " " << Fmt(ss.knownEasting) << " " << Fmt(ss.knownNorthing);
        out << "\n";

        if (ss.hasHorizAngle)
            out << "HANG REF " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.horizAngleDeg) << "\n";
        if (ss.hasHorizDist)
            out << "HDIST " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.horizDist) << "\n";
        if (ss.hasSlopeDist)
            out << "SDIST " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.slopeDist) << "\n";
        if (ss.hasVertAngle)
            out << "VANG " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.vertAngleDeg)
                << " " << (ss.isZenithAngle ? "ZENITH" : "ELEV") << "\n";
        if (!ss.description.empty())
            out << "DESC " << ss.atStationId << " " << ss.stationId << " " << ss.description << "\n";
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
    std::vector<TraverseSideShot> newSideShots;
    std::unordered_map<int, size_t> sideIndex;  // TO (the shot's own point) -> index in newSideShots

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
        } else if (directive == "ENDPOINT") {
            double e, n, z;
            if (toks.size() < 4 || !ParseDoubleTok(toks[1], &e) || !ParseDoubleTok(toks[2], &n) ||
                !ParseDoubleTok(toks[3], &z))
                return fail("expected ENDPOINT <e> <n> <z>");
            newTd.endEasting = e;
            newTd.endNorthing = n;
            newTd.endElevation = z;
            newTd.hasEndCoordinates = true;
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
            if (toIndex.count(to) || sideIndex.count(to))
                return fail("station " + std::to_string(to) + " already declared with STN");

            const std::string kind = (toks.size() >= 4) ? ToUpper(toks[3]) : "";
            if (kind == "SIDE" || kind == "CHECK") {
                bool atIsOccupied = (at == newTd.startStationId);
                for (const TraverseLeg& l : newLegs)
                    if (l.stationId == at) { atIsOccupied = true; break; }
                if (!atIsOccupied)
                    return fail("AT " + std::to_string(at) + " is not an already-occupied station");

                TraverseSideShot ss;
                ss.atStationId = at;
                ss.stationId = to;
                ss.isZenithAngle = true;
                ss.isCheckShot = (kind == "CHECK");
                if (ss.isCheckShot && toks.size() >= 6) {
                    double ke, kn;
                    if (!ParseDoubleTok(toks[4], &ke) || !ParseDoubleTok(toks[5], &kn))
                        return fail("expected STN <at> <to> CHECK [known-e known-n]");
                    ss.knownEasting = ke;
                    ss.knownNorthing = kn;
                    ss.hasKnownCoords = true;
                }
                newSideShots.push_back(std::move(ss));
                sideIndex[to] = newSideShots.size() - 1;
            } else if (!kind.empty()) {
                return fail("STN: unknown modifier \"" + toks[3] + "\" (expected SIDE or CHECK)");
            } else {
                const int expected = expectedAt(newLegs.size());
                if (at != expected)
                    return fail("AT " + std::to_string(at) + " doesn't continue the traverse chain (expected " +
                                std::to_string(expected) + ")");
                TraverseLeg leg;
                leg.stationId = to;
                leg.isZenithAngle = true;
                newLegs.push_back(std::move(leg));
                toIndex[to] = newLegs.size() - 1;
            }
        } else if (directive == "HANG" || directive == "HDIST" || directive == "SDIST" ||
                   directive == "VANG" || directive == "DESC" || directive == "PRISM" ||
                   directive == "POLE" || directive == "TSERR") {
            // All of these share the same <at> <to> addressing, at a different token
            // offset for HANG (which carries <from> first).
            const size_t atPos = (directive == "HANG") ? 2 : 1;
            const size_t toPos = atPos + 1;
            if (toks.size() <= toPos)
                return fail("expected " + directive + " ... <at> <to> ...");
            int at, to;
            if (!ParseIntTok(toks[atPos], &at) || !ParseIntTok(toks[toPos], &to))
                return fail(directive + ": <at>/<to> must be integers");

            TraverseLeg* legPtr = nullptr;
            TraverseSideShot* sidePtr = nullptr;
            size_t legIdx = 0;
            const auto itLeg = toIndex.find(to);
            if (itLeg != toIndex.end()) {
                legIdx = itLeg->second;
                legPtr = &newLegs[legIdx];
                if (at != expectedAt(legIdx))
                    return fail("AT " + std::to_string(at) + " doesn't match station " +
                                std::to_string(expectedAt(legIdx)) + " occupied for the leg to station " +
                                std::to_string(to));
            } else {
                const auto itSide = sideIndex.find(to);
                if (itSide == sideIndex.end())
                    return fail("station " + std::to_string(to) + " not declared (use STN " +
                                std::to_string(at) + " " + std::to_string(to) + " first)");
                sidePtr = &newSideShots[itSide->second];
                if (at != sidePtr->atStationId)
                    return fail("AT " + std::to_string(at) + " doesn't match the station (" +
                                std::to_string(sidePtr->atStationId) + ") this side/check shot was declared at");
            }

            if (directive == "PRISM" || directive == "POLE" || directive == "TSERR") {
                if (sidePtr)
                    return fail(directive + " does not apply to a side/check shot (no least-squares adjustment uses it)");
            }

            if (directive == "HANG") {
                // toks[1] is FROM: unconstrained for a side/check shot (no "previous leg")
                // and for the first leg (no modeled backsight station — the literal REF is
                // round-tripped there); otherwise must equal the station occupied by the
                // previous leg.
                if (legPtr && legIdx > 0) {
                    int from;
                    if (!ParseIntTok(toks[1], &from))
                        return fail("HANG: <from> must be an integer after the first leg");
                    if (from != expectedAt(legIdx - 1))
                        return fail("FROM " + std::to_string(from) + " doesn't match station " +
                                    std::to_string(expectedAt(legIdx - 1)) + " occupied for the previous leg");
                }
                double ang;
                if (toks.size() < 5 || !ParseDoubleTok(toks[4], &ang))
                    return fail("expected HANG <from> <at> <to> <angle-deg>");
                if (legPtr) {
                    legPtr->horizAngleDeg = ang;
                    legPtr->hasHorizAngle = true;
                    legPtr->horizAngleBuf = Fmt(ang);
                } else {
                    sidePtr->horizAngleDeg = ang;
                    sidePtr->hasHorizAngle = true;
                    sidePtr->horizAngleBuf = Fmt(ang);
                }
            } else if (directive == "HDIST") {
                double dist;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &dist))
                    return fail("expected HDIST <at> <to> <dist>");
                if (legPtr) { legPtr->horizDist = dist; legPtr->hasHorizDist = (dist > 0.0); }
                else { sidePtr->horizDist = dist; sidePtr->hasHorizDist = (dist > 0.0); }
            } else if (directive == "SDIST") {
                double dist;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &dist))
                    return fail("expected SDIST <at> <to> <dist>");
                if (legPtr) { legPtr->slopeDist = dist; legPtr->hasSlopeDist = (dist > 0.0); }
                else { sidePtr->slopeDist = dist; sidePtr->hasSlopeDist = (dist > 0.0); }
            } else if (directive == "VANG") {
                double ang;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &ang))
                    return fail("expected VANG <at> <to> <angle-deg> [ZENITH|ELEV]");
                const bool zenith = (toks.size() < 5) || (ToUpper(toks[4]) != "ELEV");
                if (legPtr) {
                    legPtr->vertAngleDeg = ang;
                    legPtr->hasVertAngle = true;
                    legPtr->vertAngleBuf = Fmt(ang);
                    legPtr->isZenithAngle = zenith;
                } else {
                    sidePtr->vertAngleDeg = ang;
                    sidePtr->hasVertAngle = true;
                    sidePtr->vertAngleBuf = Fmt(ang);
                    sidePtr->isZenithAngle = zenith;
                }
            } else if (directive == "DESC") {  // everything after the <to> token, verbatim.
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
                const std::string desc = (pos < line.size()) ? line.substr(pos) : "";
                if (legPtr) legPtr->description = desc;
                else sidePtr->description = desc;
            } else if (directive == "PRISM") {
                double constant, error;
                if (toks.size() < 5 || !ParseDoubleTok(toks[3], &constant) || !ParseDoubleTok(toks[4], &error))
                    return fail("expected PRISM <at> <to> <constant> <error>");
                legPtr->prismConstant = constant;
                legPtr->hasPrismConstant = (constant != 0.0);
                legPtr->prismError = error;
                legPtr->hasPrismError = (error != 0.0);
            } else if (directive == "POLE") {
                double error;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &error))
                    return fail("expected POLE <at> <to> <error>");
                legPtr->poleError = error;
                legPtr->hasPoleError = (error != 0.0);
            } else {  // TSERR
                double a, d, p;
                if (toks.size() < 6 || !ParseDoubleTok(toks[3], &a) || !ParseDoubleTok(toks[4], &d) ||
                    !ParseDoubleTok(toks[5], &p))
                    return fail("expected TSERR <at> <to> <angle-arcsec> <dist-const> <dist-ppm>");
                legPtr->tsWeights.sigmaAngleSec = a;
                legPtr->tsWeights.sigmaDistConstFt = d;
                legPtr->tsWeights.sigmaDistPpm = p;
                legPtr->hasTsWeights = true;
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
    newTd.sideShots = std::move(newSideShots);
    td = std::move(newTd);
    weights = newWeights;
    errorMsg.clear();
    return true;
}

}  // namespace travcmd
