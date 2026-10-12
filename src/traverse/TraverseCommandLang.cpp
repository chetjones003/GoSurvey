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

// Quotes a value if it contains whitespace; backslash-escapes embedded quotes.
std::string QuoteIfNeeded(const std::string& s) {
    bool needsQuote = s.empty();
    if (!needsQuote)
        for (char c : s)
            if (std::isspace(static_cast<unsigned char>(c))) { needsQuote = true; break; }
    if (!needsQuote)
        return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// Splits a line into whitespace-separated tokens, treating a double-quoted run
// (with \" / \\ escapes) as a single token with the quotes stripped.
std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> toks;
    std::string cur;
    bool inQuotes = false;
    bool any = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes && c == '\\' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) {
            cur += line[i + 1];
            ++i;
            continue;
        }
        if (c == '"') {
            inQuotes = !inQuotes;
            any = true;
            continue;
        }
        if (!inQuotes && std::isspace(static_cast<unsigned char>(c))) {
            if (any) { toks.push_back(cur); cur.clear(); any = false; }
            continue;
        }
        cur += c;
        any = true;
    }
    if (any) toks.push_back(cur);
    return toks;
}

std::string ToUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

using KvMap = std::unordered_map<std::string, std::string>;

bool GetNum(const KvMap& kv, const char* key, double* out) {
    const auto it = kv.find(key);
    if (it == kv.end() || it->second.empty()) return false;
    char* ep = nullptr;
    const double v = std::strtod(it->second.c_str(), &ep);
    if (ep == it->second.c_str()) return false;
    *out = v;
    return true;
}

bool GetBool(const KvMap& kv, const char* key, bool def) {
    const auto it = kv.find(key);
    if (it == kv.end()) return def;
    const std::string v = ToLower(it->second);
    return (v == "on" || v == "yes" || v == "true" || v == "1");
}

std::string GetStr(const KvMap& kv, const char* key, const std::string& def = {}) {
    const auto it = kv.find(key);
    return (it == kv.end()) ? def : it->second;
}

}  // namespace

std::string Serialize(const TraverseData& td, const LsaWeights& weights) {
    std::ostringstream out;
    out << "# GoSurvey Traverse Command (GSTC) \xe2\x80\x94 edit and re-apply.\n";

    out << "STATION id=" << td.startStationId
        << " e=" << Fmt(td.startEasting)
        << " n=" << Fmt(td.startNorthing)
        << " z=" << Fmt(td.startElevation);
    if (td.hasStartBearing)
        out << " bearing=" << Fmt(td.startBearingDeg);
    out << "\n";

    out << "LOOP closed=" << (td.isClosedLoop ? "on" : "off") << "\n";

    out << "SCALE apply=" << (td.applyScaleFactor ? "on" : "off")
        << " kind=" << (td.scaleFactorKind == TraverseData::ScaleFactorKind::GridOnly ? "grid" : "combined")
        << "\n";

    out << "ERRORS angle=" << Fmt(weights.sigmaAngleSec, 3)
        << " dist=" << Fmt(weights.sigmaDistConstFt, 4)
        << " ppm=" << Fmt(weights.sigmaDistPpm, 3)
        << "\n\n";

    for (const auto& leg : td.legs) {
        out << "LEG stn=" << leg.stationId;
        if (!leg.description.empty())
            out << " desc=" << QuoteIfNeeded(leg.description);
        if (leg.hasHorizAngle)
            out << " angle=" << Fmt(leg.horizAngleDeg);
        if (leg.hasHorizDist)
            out << " hdist=" << Fmt(leg.horizDist);
        if (leg.hasSlopeDist)
            out << " sdist=" << Fmt(leg.slopeDist);
        if (leg.hasVertAngle)
            out << " vangle=" << Fmt(leg.vertAngleDeg);
        out << " zenith=" << (leg.isZenithAngle ? "on" : "off");
        out << "\n";
    }
    return out.str();
}

bool Parse(const std::string& text, TraverseData& td, LsaWeights& weights, std::string& errorMsg) {
    TraverseData newTd;
    newTd.hasStartBearing = false;
    LsaWeights newWeights;
    bool sawStation = false;
    bool sawAnyLeg = false;

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

        KvMap kv;
        for (size_t i = 1; i < toks.size(); ++i) {
            const size_t eq = toks[i].find('=');
            if (eq == std::string::npos) {
                errorMsg = "line " + std::to_string(lineNo) + ": expected key=value, got \"" + toks[i] + "\"";
                return false;
            }
            kv[ToLower(toks[i].substr(0, eq))] = toks[i].substr(eq + 1);
        }

        double num = 0.0;
        if (directive == "STATION") {
            if (GetNum(kv, "id", &num)) newTd.startStationId = static_cast<int>(num);
            if (GetNum(kv, "e", &num)) newTd.startEasting = num;
            if (GetNum(kv, "n", &num)) newTd.startNorthing = num;
            if (GetNum(kv, "z", &num)) newTd.startElevation = num;
            if (GetNum(kv, "bearing", &num)) {
                newTd.startBearingDeg = num;
                newTd.hasStartBearing = true;
                newTd.startBearingBuf = TraverseFormatBearing(num);
            }
            sawStation = true;
        } else if (directive == "LOOP") {
            newTd.isClosedLoop = GetBool(kv, "closed", newTd.isClosedLoop);
        } else if (directive == "SCALE") {
            newTd.applyScaleFactor = GetBool(kv, "apply", newTd.applyScaleFactor);
            const std::string kind = ToLower(GetStr(kv, "kind", "combined"));
            newTd.scaleFactorKind = (kind == "grid") ? TraverseData::ScaleFactorKind::GridOnly
                                                      : TraverseData::ScaleFactorKind::Combined;
        } else if (directive == "ERRORS") {
            if (GetNum(kv, "angle", &num)) newWeights.sigmaAngleSec = num;
            if (GetNum(kv, "dist", &num)) newWeights.sigmaDistConstFt = num;
            if (GetNum(kv, "ppm", &num)) newWeights.sigmaDistPpm = num;
        } else if (directive == "LEG") {
            TraverseLeg leg;
            if (GetNum(kv, "stn", &num)) leg.stationId = static_cast<int>(num);
            else {
                errorMsg = "line " + std::to_string(lineNo) + ": LEG requires stn=<id>";
                return false;
            }
            leg.description = GetStr(kv, "desc");
            leg.isZenithAngle = GetBool(kv, "zenith", true);
            if (GetNum(kv, "angle", &num)) {
                leg.horizAngleDeg = num;
                leg.hasHorizAngle = true;
                leg.horizAngleBuf = Fmt(num);
            }
            if (GetNum(kv, "hdist", &num)) {
                leg.horizDist = num;
                leg.hasHorizDist = (num > 0.0);
            }
            if (GetNum(kv, "sdist", &num)) {
                leg.slopeDist = num;
                leg.hasSlopeDist = (num > 0.0);
            }
            if (GetNum(kv, "vangle", &num)) {
                leg.vertAngleDeg = num;
                leg.hasVertAngle = true;
                leg.vertAngleBuf = Fmt(num);
            }
            newTd.legs.push_back(std::move(leg));
            sawAnyLeg = true;
        } else {
            errorMsg = "line " + std::to_string(lineNo) + ": unknown directive \"" + toks[0] + "\"";
            return false;
        }
    }

    if (!sawStation) {
        errorMsg = "missing STATION directive";
        return false;
    }
    (void)sawAnyLeg;  // a traverse with zero legs is valid (just the start station).

    td = std::move(newTd);
    weights = newWeights;
    errorMsg.clear();
    return true;
}

}  // namespace travcmd
