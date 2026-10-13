#include "Rw5Import.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Carlson SurvCE/SurvPC .rw5 importer (GitHub issue #846, Phase B). See
// Rw5Import.hpp for the exact supported grammar and the documented
// defined-gap/ignored-by-design boundaries. Grounded in Carlson's official
// "SurvCE Raw Data File Format (*.rw5) — Version 3.03" reference.
// ---------------------------------------------------------------------------

namespace {

std::vector<std::string> SplitComma(const std::string& line) {
    std::vector<std::string> toks;
    std::string cur;
    for (char c : line) {
        if (c == ',') { toks.push_back(cur); cur.clear(); }
        else cur += c;
    }
    toks.push_back(cur);
    return toks;
}

bool ParseDoubleTok(const std::string& s, double* out) {
    char* ep = nullptr;
    const double v = std::strtod(s.c_str(), &ep);
    if (ep == s.c_str() || *ep != '\0') return false;
    *out = v;
    return true;
}

// Finds a field matching `header` among `fields` (the record's tokens after
// the record-type token) and returns the value text after it. A single-
// letter header (only "N"/"E" in this format) must be followed by a literal
// space before the value, per the spec's own field-header table.
bool FindFieldStr(const std::vector<std::string>& fields, const char* header, std::string* valueOut) {
    const std::string h = header;
    for (const auto& tok : fields) {
        if (tok.size() >= 2 && tok[0] == '-' && tok[1] == '-') continue;  // a note field.
        if (h.size() == 1) {
            if (tok.size() > 2 && tok[0] == h[0] && tok[1] == ' ') {
                *valueOut = tok.substr(2);
                return true;
            }
        } else if (tok.size() > h.size() && tok.compare(0, h.size(), h) == 0) {
            *valueOut = tok.substr(h.size());
            return true;
        }
    }
    return false;
}

bool FindFieldNum(const std::vector<std::string>& fields, const char* header, double* valueOut) {
    std::string s;
    return FindFieldStr(fields, header, &s) && ParseDoubleTok(s, valueOut);
}

struct KnownPt { double n = 0.0, e = 0.0, z = 0.0; bool has = false, hasZ = false; };

}  // namespace

bool Rw5Import(const char* path, TraverseData& td, std::string& errorMsg) {
    std::ifstream f(path);
    if (!f.is_open()) {
        errorMsg = "Cannot open file: ";
        errorMsg += path;
        return false;
    }

    std::unordered_map<std::string, KnownPt> knownPts;

    bool haveOccupied = false;
    std::string currentOccupied;

    bool haveBackForCurrent = false;
    std::string currentBackPoint;   // BK's BP for the current OC, if given.
    bool haveCurrentBackPoint = false;

    bool sawFirstBk = false;
    std::string firstBackPoint;
    bool haveFirstBackPoint = false;
    bool haveFirstBackAz = false;
    double firstBackAzDeg = 0.0;

    std::string firstOccupied;
    bool haveFirstOccupied = false;

    std::vector<TraverseLeg> legs;

    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const auto toks = SplitComma(line);
        if (toks.empty() || toks[0].empty()) continue;

        auto fail = [&](const std::string& msg) -> bool {
            errorMsg = "line " + std::to_string(lineNo) + ": " + msg;
            return false;
        };

        const std::string& code = toks[0];
        const std::vector<std::string> fields(toks.begin() + 1, toks.end());

        if (code.size() >= 2 && code[0] == '-' && code[1] == '-') continue;  // a note-only line.

        if (code == "JB") continue;  // job header; informational only.

        if (code == "MO") {
            std::string un;
            if (FindFieldStr(fields, "UN", &un) && un == "1")
                return fail("MO declares meters (UN1) — this importer assumes feet/US feet");
            continue;
        }

        if (code == "SP") {
            std::string pn;
            double n, e;
            if (!FindFieldStr(fields, "PN", &pn) || !FindFieldNum(fields, "N", &n) ||
                !FindFieldNum(fields, "E", &e))
                return fail("expected SP with PN, N, and E fields");
            KnownPt kp;
            kp.n = n;
            kp.e = e;
            kp.has = true;
            double el;
            if (FindFieldNum(fields, "EL", &el)) { kp.z = el; kp.hasZ = true; }
            knownPts[pn] = kp;
            continue;
        }

        if (code == "OC") {
            std::string op;
            if (!FindFieldStr(fields, "OP", &op)) return fail("expected OC with an OP field");
            currentOccupied = op;
            haveOccupied = true;
            haveBackForCurrent = false;
            haveCurrentBackPoint = false;
            if (!haveFirstOccupied) {
                firstOccupied = op;
                haveFirstOccupied = true;
            }
            double n, e;
            if (FindFieldNum(fields, "N", &n) && FindFieldNum(fields, "E", &e)) {
                KnownPt kp;
                kp.n = n;
                kp.e = e;
                kp.has = true;
                double el;
                if (FindFieldNum(fields, "EL", &el)) { kp.z = el; kp.hasZ = true; }
                knownPts[op] = kp;
            }
            continue;
        }

        if (code == "BK") {
            if (!haveOccupied) return fail("BK with no preceding OC");
            std::string op;
            if (!FindFieldStr(fields, "OP", &op)) return fail("expected BK with an OP field");
            if (op != currentOccupied)
                return fail("BK's OP (" + op + ") doesn't match the currently occupied station (" +
                            currentOccupied + ")");

            std::string bp;
            const bool haveBp = FindFieldStr(fields, "BP", &bp);
            double bs;
            const bool haveBs = FindFieldNum(fields, "BS", &bs);

            currentBackPoint = bp;
            haveCurrentBackPoint = haveBp;
            haveBackForCurrent = true;

            if (!sawFirstBk) {
                sawFirstBk = true;
                firstBackPoint = bp;
                haveFirstBackPoint = haveBp;
                firstBackAzDeg = bs;
                haveFirstBackAz = haveBs;
            }
            continue;
        }

        if (code == "TR") {
            if (!haveOccupied) return fail("TR with no preceding OC");
            if (!haveBackForCurrent)
                return fail("TR with no preceding BK for this setup — can't verify its backsight");

            std::string op, fp;
            if (!FindFieldStr(fields, "OP", &op) || !FindFieldStr(fields, "FP", &fp))
                return fail("expected TR with OP and FP fields");
            if (op != currentOccupied)
                return fail("TR's OP (" + op + ") doesn't match the currently occupied station (" +
                            currentOccupied + ")");

            // The chain: this leg's AT must be the previous leg's FP (or the start
            // station for the first leg), and its backsight (BK's BP) must be the
            // station occupied before THAT — exactly the FROM-validation
            // StarNetImport does, just driven by OC/BK state instead of inline
            // tokens. An unverifiable backsight (BS given but no BP) is a defined
            // gap rather than a silently-trusted angle once there's a prior leg to
            // check it against.
            const std::string expectedAt = legs.empty() ? firstOccupied : legs.back().description;
            if (op != expectedAt)
                return fail("TR's OP (" + op + ") doesn't continue the traverse chain (expected " +
                            expectedAt + ")");
            if (!legs.empty()) {
                const std::string expectedBack = (legs.size() == 1) ? firstOccupied : legs[legs.size() - 2].description;
                if (!haveCurrentBackPoint)
                    return fail("this setup's BK has no BP to verify against the traverse chain "
                                "(expected backsight " + expectedBack + ")");
                if (currentBackPoint != expectedBack)
                    return fail("this setup's backsight (" + currentBackPoint + ") doesn't match the "
                                "station the traverse chain expects (" + expectedBack + ")");
            }

            double ar, al;
            const bool haveAr = FindFieldNum(fields, "AR", &ar);
            const bool haveAl = (!haveAr) && FindFieldNum(fields, "AL", &al);
            if (!haveAr && !haveAl) {
                std::string dummy;
                if (FindFieldStr(fields, "AZ", &dummy) || FindFieldStr(fields, "BR", &dummy) ||
                    FindFieldStr(fields, "DR", &dummy) || FindFieldStr(fields, "DL", &dummy))
                    return fail("TR uses an AZ/BR/DR/DL angle field — only AR/AL are supported");
                return fail("expected a TR angle field (AR or AL)");
            }

            double ze, va;
            const bool haveZe = FindFieldNum(fields, "ZE", &ze);
            const bool haveVa = (!haveZe) && FindFieldNum(fields, "VA", &va);
            if (!haveZe && !haveVa) {
                std::string dummy;
                if (FindFieldStr(fields, "CE", &dummy))
                    return fail("TR uses a CE (change-elevation) field — not supported, no angle to reduce");
                return fail("expected a TR vertical field (ZE or VA)");
            }

            double sd, hd;
            const bool haveSd = FindFieldNum(fields, "SD", &sd);
            const bool haveHd = (!haveSd) && FindFieldNum(fields, "HD", &hd);
            if (!haveSd && !haveHd) return fail("expected a TR distance field (SD or HD)");

            TraverseLeg leg;
            leg.description = fp;
            leg.hasHorizAngle = true;
            leg.horizAngleDeg = haveAr ? ar : TraverseNormBearing(360.0 - al);
            leg.hasVertAngle = true;
            leg.isZenithAngle = haveZe;
            leg.vertAngleDeg = haveZe ? ze : va;
            if (haveSd) { leg.slopeDist = sd; leg.hasSlopeDist = (sd > 0.0); }
            else { leg.horizDist = hd; leg.hasHorizDist = (hd > 0.0); }

            legs.push_back(std::move(leg));
            continue;
        }

        if (code == "SS" || code == "BD" || code == "BR" || code == "FD" || code == "FR" || code == "OF")
            continue;  // ignored by design — see Rw5Import.hpp.

        if (code == "GPS" || code == "BP" || code == "G0" || code == "G1" || code == "G2" ||
            code == "G3" || code == "G4")
            return fail("unsupported RW5 GNSS record \"" + code + "\" (GPS vectors are out of scope, see #847)");

        return fail("unsupported RW5 record type \"" + code + "\"");
    }

    if (legs.empty()) {
        errorMsg = "no TR (traverse) records found";
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

    if (haveFirstBackAz) {
        td.startBearingDeg = firstBackAzDeg;
        td.hasStartBearing = true;
        td.startBearingBuf = TraverseFormatBearing(firstBackAzDeg);
    } else if (haveFirstBackPoint) {
        const auto itStn = knownPts.find(firstOccupied);
        const auto itBs = knownPts.find(firstBackPoint);
        if (itStn != knownPts.end() && itStn->second.has && itBs != knownPts.end() && itBs->second.has) {
            const double dE = itBs->second.e - itStn->second.e;
            const double dN = itBs->second.n - itStn->second.n;
            double az = std::atan2(dE, dN) * (180.0 / 3.14159265358979323846);
            if (az < 0.0) az += 360.0;
            td.startBearingDeg = az;
            td.hasStartBearing = true;
            td.startBearingBuf = TraverseFormatBearing(az);
        }
    }

    td.legs = std::move(legs);

    if (!td.legs.empty() && td.legs.back().description == firstOccupied)
        td.isClosedLoop = true;

    return true;
}
