#include "FbkImport.hpp"
#include "TraverseCalc.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// This parser handles the Autodesk/Leica FBK (Field Book) format used by
// Civil 3D and Leica total stations.  Key records:
//
//   NEZ "name" northing easting elev  — known point coordinates
//   STN "name" [hi]                   — instrument setup (occupy)
//   BS  "name" hz_packed_dms          — backsight Hz circle reading
//   F1 VA "name" hz sd va             — face-1 observation (packed DMS angles)
//   F2 VA "name" hz sd va             — face-2 observation
//
// Angles use packed DMS: e.g. 317.59551 = 317°59'55.1"
// Distances are plain decimal.
// Lines / partial lines beginning with ! or -- are comments.
// ---------------------------------------------------------------------------

namespace {

static constexpr double kPi = 3.14159265358979323846;

// Unpack Autodesk packed DMS (D.MMSS) to decimal degrees.
static double UnpackFbkAngle(double packed) {
    const bool neg = (packed < 0.0);
    const double absVal = std::abs(packed);
    const double deg = std::floor(absVal);
    const double mmFull = (absVal - deg) * 100.0;
    const double mm = std::floor(mmFull + 1e-9);
    const double ss = (mmFull - mm) * 100.0;
    const double result = deg + mm / 60.0 + ss / 3600.0;
    return neg ? -result : result;
}

static double SimpleAvg(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0.0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}

static double ComputeAzimuth(double fromN, double fromE, double toN, double toE) {
    const double dE = toE - fromE;
    const double dN = toN - fromN;
    double az = std::atan2(dE, dN) * (180.0 / kPi);
    if (az < 0.0) az += 360.0;
    return az;
}

// Parse a quoted name token: "KCP2" → KCP2
static bool ParseQuotedName(const std::string& tok, std::string* out) {
    if (tok.size() < 2 || tok.front() != '"' || tok.back() != '"')
        return false;
    *out = tok.substr(1, tok.size() - 2);
    return true;
}

static std::string FmtDec(double deg) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", deg);
    return std::string(buf);
}

// Tokenize a FBK line: strip inline "--" comment, split on whitespace,
// uppercase the first two tokens (record type and subtype code).
static std::vector<std::string> TokenizeFbkLine(const std::string& line) {
    std::string content = line;
    for (size_t i = 0; i + 1 < content.size(); ++i) {
        if (content[i] == '-' && content[i + 1] == '-') {
            if (i == 0 || std::isspace(static_cast<unsigned char>(content[i - 1]))) {
                content = content.substr(0, i);
                break;
            }
        }
    }
    std::vector<std::string> tokens;
    {
        std::istringstream ss(content);
        std::string tok;
        while (ss >> tok)
            tokens.push_back(tok);
    }
    // Uppercase only tokens[0] (record type). Quoted names must not be cased.
    if (!tokens.empty())
        for (char& c : tokens[0])
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return tokens;
}

// One foresight point's accumulated face observations within a setup.
struct PointObs {
    std::vector<double> f1Hz, f1Va, f1Sd;
    std::vector<double> f2Hz, f2Va, f2Sd;
};

// One instrument setup (STN → BS → observations).
struct SetupRec {
    std::string stnName;
    std::string bsName;
    double bsHzDec = 0.0;    // backsight Hz reading in decimal degrees
    bool hasBs = false;
    std::vector<std::string> obsOrder;     // unique point names in insertion order
    std::map<std::string, PointObs> obs;
};

} // namespace


bool FbkImport(const char* path, TraverseData& td, std::string& errorMsg) {
    std::ifstream f(path);
    if (!f.is_open()) {
        errorMsg = "Cannot open file: ";
        errorMsg += path;
        return false;
    }

    struct KnownPt { double n = 0.0, e = 0.0, z = 0.0; };
    std::unordered_map<std::string, KnownPt> knownPts;

    std::vector<SetupRec> setups;
    SetupRec* cur = nullptr;

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        const auto tokens = TokenizeFbkLine(line);
        if (tokens.empty()) continue;

        const std::string& rec = tokens[0];
        // Skip comment lines (! at start or -- at start)
        if (rec[0] == '!' || (rec.size() >= 2 && rec[0] == '-' && rec[1] == '-'))
            continue;

        // ---- NEZ "name" northing easting elev ----
        if (rec == "NEZ" && tokens.size() >= 5) {
            std::string name;
            if (!ParseQuotedName(tokens[1], &name)) continue;
            KnownPt kp;
            kp.n = std::strtod(tokens[2].c_str(), nullptr);
            kp.e = std::strtod(tokens[3].c_str(), nullptr);
            kp.z = std::strtod(tokens[4].c_str(), nullptr);
            knownPts[name] = kp;
        }
        // ---- STN "name" [hi] ---- start a new setup
        else if (rec == "STN" && tokens.size() >= 2) {
            std::string name;
            if (!ParseQuotedName(tokens[1], &name)) continue;
            setups.emplace_back();
            setups.back().stnName = name;
            cur = &setups.back();
        }
        // ---- BS "name" hz_packed_dms ----
        else if (rec == "BS" && tokens.size() >= 3 && cur) {
            std::string name;
            if (!ParseQuotedName(tokens[1], &name)) continue;
            char* ep = nullptr;
            const double packed = std::strtod(tokens[2].c_str(), &ep);
            if (ep == tokens[2].c_str()) continue;
            cur->bsName   = name;
            cur->bsHzDec  = UnpackFbkAngle(packed);
            cur->hasBs    = true;
        }
        // ---- F1 VA "name" hz sd va  /  F2 VA "name" hz sd va ----
        else if ((rec == "F1" || rec == "F2") && tokens.size() >= 6 && cur) {
            // tokens[1] is the observation subtype; must be "VA" (case-insensitive)
            std::string subtype = tokens[1];
            for (char& c : subtype) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (subtype != "VA") continue;
            std::string name;
            if (!ParseQuotedName(tokens[2], &name)) continue;
            char* ep = nullptr;
            const double hz = UnpackFbkAngle(std::strtod(tokens[3].c_str(), &ep));
            if (ep == tokens[3].c_str()) continue;
            const double sd = std::strtod(tokens[4].c_str(), nullptr);
            const double va = UnpackFbkAngle(std::strtod(tokens[5].c_str(), nullptr));

            PointObs& po = cur->obs[name];
            if (std::find(cur->obsOrder.begin(), cur->obsOrder.end(), name)
                == cur->obsOrder.end())
                cur->obsOrder.push_back(name);

            if (rec == "F1") {
                po.f1Hz.push_back(hz);
                po.f1Va.push_back(va);
                po.f1Sd.push_back(sd);
            } else {
                po.f2Hz.push_back(hz);
                po.f2Va.push_back(va);
                po.f2Sd.push_back(sd);
            }
        }
        // All other records (JOB, UNITS, SF, PRISM, etc.) are ignored.
    }

    if (setups.empty()) {
        errorMsg = "No STN records found — is this an Autodesk FBK file?";
        return false;
    }

    // ---------- sequential integer ID map (preserves encounter order) --------
    std::unordered_map<std::string, int> ptIdMap;
    int nextId = 1;
    auto GetId = [&](const std::string& name) -> int {
        auto it = ptIdMap.find(name);
        if (it != ptIdMap.end()) return it->second;
        const int id = nextId++;
        ptIdMap[name] = id;
        return id;
    };
    for (const auto& s : setups) {
        GetId(s.stnName);
        for (const auto& n : s.obsOrder) GetId(n);
    }

    // ---------- populate TraverseData ----------------------------------------
    td = TraverseData{};
    td.useFace1Face2 = false; // face averaging done during import; pre-averaged values in horizAngleDeg/vertAngleDeg

    // Starting station from first setup
    const SetupRec& first = setups[0];
    td.startStationId = GetId(first.stnName);
    {
        const auto it = knownPts.find(first.stnName);
        if (it != knownPts.end()) {
            td.startNorthing  = it->second.n;
            td.startEasting   = it->second.e;
            td.startElevation = it->second.z;
        }
    }

    // Reference bearing = azimuth from first STN to first BS (from coordinates)
    if (first.hasBs) {
        const auto itStn = knownPts.find(first.stnName);
        const auto itBs  = knownPts.find(first.bsName);
        if (itStn != knownPts.end() && itBs != knownPts.end()) {
            const double az = ComputeAzimuth(itStn->second.n, itStn->second.e,
                                             itBs->second.n,  itBs->second.e);
            td.startBearingDeg = az;
            td.hasStartBearing = true;
            td.startBearingBuf = TraverseFormatBearing(az);
        }
    }

    // Build traverse legs from every setup's foresight observations
    for (const auto& setup : setups) {
        if (!setup.hasBs) continue;

        for (const auto& pname : setup.obsOrder) {
            if (pname == setup.bsName) continue;  // skip backsight check shots

            const auto& po = setup.obs.at(pname);
            const bool hasF1 = !po.f1Hz.empty();
            const bool hasF2 = !po.f2Hz.empty();
            if (!hasF1 && !hasF2) continue;

            // Face-averaged Hz circle reading for this foresight
            double faceHz = 0.0;
            if (hasF1 && hasF2)
                faceHz = TraverseAverageFaceHoriz(SimpleAvg(po.f1Hz), SimpleAvg(po.f2Hz));
            else if (hasF1)
                faceHz = SimpleAvg(po.f1Hz);
            else
                faceHz = TraverseNormBearing(SimpleAvg(po.f2Hz) + 180.0);

            // H.Angle = foresight circle reading − backsight circle reading (mod 360)
            const double ha = TraverseNormBearing(faceHz - setup.bsHzDec);

            // Face-averaged zenith angle
            const bool hasF1Va = !po.f1Va.empty();
            const bool hasF2Va = !po.f2Va.empty();
            double faceVa = 90.0;
            bool hasVa = false;
            if (hasF1Va && hasF2Va)
                faceVa = TraverseAverageFaceZenith(SimpleAvg(po.f1Va), SimpleAvg(po.f2Va));
            else if (hasF1Va)
                faceVa = SimpleAvg(po.f1Va);
            else if (hasF2Va)
                faceVa = SimpleAvg(po.f2Va);
            hasVa = hasF1Va || hasF2Va;

            // Average slope distance across both faces
            std::vector<double> allSd;
            allSd.insert(allSd.end(), po.f1Sd.begin(), po.f1Sd.end());
            allSd.insert(allSd.end(), po.f2Sd.begin(), po.f2Sd.end());
            const double sd = allSd.empty() ? 0.0 : SimpleAvg(allSd);

            TraverseLeg leg;
            leg.stationId     = GetId(pname);
            leg.description   = pname;
            leg.horizAngleDeg = ha;
            leg.hasHorizAngle = true;
            leg.horizAngleBuf = FmtDec(ha);
            leg.isZenithAngle = true;
            if (hasVa) {
                leg.vertAngleDeg = faceVa;
                leg.hasVertAngle = true;
                leg.vertAngleBuf = FmtDec(faceVa);
            }
            if (sd > 0.0) {
                leg.slopeDist    = sd;
                leg.hasSlopeDist = true;
            }

            // Populate face columns so Face1/Face2 mode works correctly if
            // the user enables it.  Store the reduced H.Angles (foresight
            // circle reading − BS circle reading) per face, and the raw
            // zenith readings per face.  The traverse engine will then call
            // TraverseAverageFaceHoriz / TraverseAverageFaceZenith on these.
            if (hasF1) {
                leg.face1HorizDeg = TraverseNormBearing(SimpleAvg(po.f1Hz) - setup.bsHzDec);
                leg.face1HorizBuf = FmtDec(leg.face1HorizDeg);
                if (hasF1Va) {
                    leg.face1VertDeg = SimpleAvg(po.f1Va);
                    leg.face1VertBuf = FmtDec(leg.face1VertDeg);
                }
            }
            if (hasF2) {
                // Store F2 reduced H.Angle; TraverseAverageFaceHoriz expects
                // F2 ≈ F1 + 180° in the same reference frame so pass the raw
                // reduced angle — the averaging function handles the wrap.
                leg.face2HorizDeg = TraverseNormBearing(SimpleAvg(po.f2Hz) - setup.bsHzDec);
                leg.face2HorizBuf = FmtDec(leg.face2HorizDeg);
                if (hasF2Va) {
                    leg.face2VertDeg = SimpleAvg(po.f2Va);
                    leg.face2VertBuf = FmtDec(leg.face2VertDeg);
                }
            }

            // Populate raw measurement sets so the editor can show per-set
            // F1/F2 detail rows.  Pair by index; unmatched singles carry forward.
            {
                const size_t nSets = std::max(po.f1Hz.size(), po.f2Hz.size());
                for (size_t s = 0; s < nSets; ++s) {
                    TraverseMeasSet ms;
                    ms.setNo = static_cast<int>(s) + 1;
                    if (s < po.f1Hz.size()) {
                        ms.f1HzDec = po.f1Hz[s];
                        ms.f1VaDec = (s < po.f1Va.size()) ? po.f1Va[s] : 90.0;
                        ms.f1Sd    = (s < po.f1Sd.size()) ? po.f1Sd[s] : 0.0;
                        ms.hasF1   = true;
                    }
                    if (s < po.f2Hz.size()) {
                        ms.f2HzDec = po.f2Hz[s];
                        ms.f2VaDec = (s < po.f2Va.size()) ? po.f2Va[s] : 90.0;
                        ms.f2Sd    = (s < po.f2Sd.size()) ? po.f2Sd[s] : 0.0;
                        ms.hasF2   = true;
                    }
                    leg.rawSets.push_back(ms);
                }
            }

            td.legs.push_back(leg);
        }
    }

    if (td.legs.empty()) {
        errorMsg = "FBK parsed but no traverse legs could be extracted";
        return false;
    }

    return true;
}
