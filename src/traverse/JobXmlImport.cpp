#include "JobXmlImport.hpp"

#include <pugixml.hpp>

#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Trimble JobXML importer (GitHub issue #846, Phase B; generalized for real
// Trimble Access field exports — GitHub issue #848 follow-up). See
// JobXmlImport.hpp for the exact supported grammar and the documented
// defined-gap boundary. Grounded in Trimble's official JobXMLSchema-5.72.xsd.
// ---------------------------------------------------------------------------

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMetersToFeet = 1.0 / 0.3048;  // exact international foot.

// Text of a child element, or empty if the element is absent or itself empty
// (JobXML's "OrNull" types are represented as an empty element — see the
// schema's distOrNullType/angleOrNullType/coordOrNullType definitions).
std::string ChildText(const pugi::xml_node& node, const char* name) {
    return node.child(name).text().as_string();
}

bool ChildDouble(const pugi::xml_node& node, const char* name, double* out) {
    const std::string s = ChildText(node, name);
    if (s.empty()) return false;
    char* ep = nullptr;
    *out = std::strtod(s.c_str(), &ep);
    return ep != s.c_str();
}

struct KnownPt { double n = 0.0, e = 0.0, z = 0.0; bool has = false, hasZ = false; };

bool ResolveAzimuth(const std::unordered_map<std::string, KnownPt>& knownPts,
                    const std::string& from, const std::string& to, double* azOut) {
    const auto itFrom = knownPts.find(from);
    const auto itTo = knownPts.find(to);
    if (itFrom == knownPts.end() || !itFrom->second.has) return false;
    if (itTo == knownPts.end() || !itTo->second.has) return false;
    const double dE = itTo->second.e - itFrom->second.e;
    const double dN = itTo->second.n - itFrom->second.n;
    double az = std::atan2(dE, dN) * (180.0 / kPi);
    if (az < 0.0) az += 360.0;
    *azOut = az;
    return true;
}

// Record types this importer explicitly recognizes as "not a terrestrial leg
// observation, and not supported" when present on a PointRecord alongside (or
// instead of) a plain Circle/MTA — GPS/ECEF/resection/offset/etc. methods.
// MTA (Mean Turned Angle) used to be in this list; it's now a first-class
// supported reading (see JobXmlImport.hpp) — Trimble's own pre-averaged
// multi-round observation, read directly instead of being rejected.
constexpr const char* kUnsupportedPointElements[] = {
    "WGS84", "ECEF", "RTXECEF", "Local", "Polar", "EnteredData", "AngleOffset",
    "DistanceOffset", "DualPrismOffset", "CircularObject", "RemoteObject",
    "LevelData", "ReducedLevelData", "ReducedLevelObsReferences",
    "StationAndOffset", "PhotogrammetryObs", "PointOnPlane",
};

struct BackBearingRec { std::string backSight; double face1Hz = 0.0; bool hasFace1 = false; };

// One already-classified terrestrial observation (a Circle face reading, or an
// MTA already-reduced reading), before grouping. Multiple ShotObs can share
// the same (stationIdAttr, name) — e.g. a Face1 + Face2 + MTA trio for one
// physical sight — and are merged by ShotGroup below.
struct ShotObs {
    std::string name, stationIdAttr, backBearingIdAttr, face;
    double hz = 0.0, va = 90.0, sd = 0.0;
    bool hasVa = false, isMta = false;
};

// Every ShotObs sharing one (stationIdAttr, name): a physical sight, possibly
// observed more than once (Face1/Face2, one or more rounds). Reduced to a
// single turned-angle/zenith/distance observation by ReduceGroup below —
// preferring Trimble's own MTA average when the controller produced one
// (StartRoundsRecord/EndRoundsRecord jobs always do), otherwise face-averaging
// the raw Circle readings exactly as FbkImport reduces F1/F2 sets.
struct ShotGroup {
    std::string stationIdAttr, name, backBearingIdAttr;
    bool hasMta = false;
    double mtaHz = 0.0, mtaVa = 90.0, mtaSd = 0.0;
    bool mtaHasVa = false;
    std::vector<TraverseMeasSet> rawSets;
};

bool ReduceGroup(const ShotGroup& g, const std::unordered_map<std::string, BackBearingRec>& backBearings,
                 double* outHz, double* outVa, bool* outHasVa, double* outSd, std::string* err) {
    const auto bbIt = backBearings.find(g.backBearingIdAttr);
    if (bbIt == backBearings.end()) {
        *err = "internal: no backsight context for the observation of " + g.name;
        return false;
    }
    const double bs = bbIt->second.face1Hz;
    if (g.hasMta) {
        // An MTA ("Mean Turned Angle") reading is NOT on the same basis as a raw
        // <Circle> face reading, even though both elements are named
        // "HorizontalCircle": empirically verified against a real multi-round
        // Trimble Access export (issue #848 follow-up) by cross-checking against
        // its own <Reductions> ComputedGrid coordinates — a raw Face1/Face2
        // circle reading is azimuth-calibrated (SetHorizontalCircleMode=
        // SetToAzimuth) and genuinely needs the backsight azimuth subtracted to
        // get a turned angle (the ReduceLegFromSets path below does this), but
        // Trimble's own MTA average is computed ACROSS MULTIPLE ROUNDS — each of
        // which may have its own backsight re-check — by first reducing every
        // round to a turned angle and THEN averaging, so the MTA's
        // HorizontalCircle already IS the turned angle. Subtracting the
        // backsight's azimuth from it a second time silently rotated every leg
        // after the first by that station's own backsight azimuth.
        *outHz = TraverseNormBearing(g.mtaHz);
        *outVa = g.mtaHasVa ? g.mtaVa : 90.0;
        *outHasVa = g.mtaHasVa;
        *outSd = g.mtaSd;
        return true;
    }
    if (g.rawSets.empty()) {
        *err = "internal: no readings recorded for " + g.name;
        return false;
    }
    // Reuse the exact same F1/F2 face-averaging TraverseCalc.cpp already
    // implements for FbkImport, so a JobXML leg with multiple rounds reduces
    // identically to the equivalent FBK leg (and to how the Form tab's own
    // observation editor re-reduces an edited leg).
    TraverseLeg tmp;
    tmp.hasBacksightCircle = true;
    tmp.backsightCircleDeg = bs;
    tmp.rawSets = g.rawSets;
    ReduceLegFromSets(tmp);
    *outHz = tmp.horizAngleDeg;
    *outVa = tmp.vertAngleDeg;
    *outHasVa = tmp.hasVertAngle;
    *outSd = tmp.hasSlopeDist ? tmp.slopeDist : 0.0;
    return true;
}

}  // namespace

bool JobXmlImport(const char* path, TraverseData& td, std::string& errorMsg) {
    pugi::xml_document doc;
    const pugi::xml_parse_result result = doc.load_file(path);
    if (!result) {
        errorMsg = "Cannot open or parse file: ";
        errorMsg += path;
        errorMsg += " (";
        errorMsg += result.description();
        errorMsg += ")";
        return false;
    }

    const pugi::xml_node root = doc.child("JOBFile");
    if (!root) {
        errorMsg = "not a JobXML file (missing JOBFile root element)";
        return false;
    }
    const pugi::xml_node fieldBook = root.child("FieldBook");
    if (!fieldBook) {
        errorMsg = "missing FieldBook element";
        return false;
    }
    const pugi::xml_node reductions = root.child("Reductions");

    // ---------- known points, from <Reductions><Point> (for azimuth lookup and
    // demoting a repeat shot of a known point to a CHECK rather than a SIDE) ---
    std::unordered_map<std::string, KnownPt> knownPts;
    if (reductions) {
        for (const pugi::xml_node pt : reductions.children("Point")) {
            const std::string name = ChildText(pt, "Name");
            const pugi::xml_node grid = pt.child("Grid");
            if (name.empty() || !grid) continue;
            double n, e;
            if (!ChildDouble(grid, "North", &n) || !ChildDouble(grid, "East", &e)) continue;
            KnownPt kp;
            kp.n = n * kMetersToFeet;
            kp.e = e * kMetersToFeet;
            kp.has = true;
            double z;
            if (ChildDouble(grid, "Elevation", &z)) { kp.z = z * kMetersToFeet; kp.hasZ = true; }
            knownPts[name] = kp;
        }
    }

    // ---------- FieldBook walk: collect raw shots, one setup (StationRecord) at
    // a time, in document order (the order the crew actually worked) -----------
    std::unordered_map<std::string, std::string> stationNames;       // StationRecord ID -> name
    std::unordered_map<std::string, BackBearingRec> backBearings;    // BackBearingRecord ID -> rec
    std::vector<std::pair<std::string, std::string>> stationSeq;     // (StationRecord ID, name), in order.
    std::vector<ShotObs> shots;

    for (const pugi::xml_node child : fieldBook.children()) {
        const std::string tag = child.name();
        const std::string idAttr = child.attribute("ID").as_string();

        auto fail = [&](const std::string& msg) -> bool {
            errorMsg = tag;
            errorMsg += " (ID=";
            errorMsg += idAttr;
            errorMsg += "): ";
            errorMsg += msg;
            return false;
        };

        if (tag == "StationRecord") {
            const std::string name = ChildText(child, "StationName");
            if (name.empty()) return fail("missing StationName");
            stationNames[idAttr] = name;
            stationSeq.emplace_back(idAttr, name);
            continue;
        }

        if (tag == "BackBearingRecord") {
            BackBearingRec bb;
            bb.backSight = ChildText(child, "BackSight");
            bb.hasFace1 = ChildDouble(child, "Face1HorizontalCircle", &bb.face1Hz);
            backBearings[idAttr] = bb;
            continue;
        }

        if (tag == "PointRecord") {
            const pugi::xml_node circleNode = child.child("Circle");
            const pugi::xml_node mtaNode = child.child("MTA");
            const pugi::xml_node stationIdNode = child.child("StationID");
            const pugi::xml_node backBearingIdNode = child.child("BackBearingID");
            const bool anyTerrestrial = circleNode || mtaNode || stationIdNode || backBearingIdNode;
            if (!anyTerrestrial)
                continue;  // a keyed-in/Cogo/non-observation point — not a leg, not an error.
            if ((!circleNode && !mtaNode) || !stationIdNode || !backBearingIdNode)
                return fail("a terrestrial-looking record is missing Circle/MTA, StationID, or BackBearingID");

            for (const char* other : kUnsupportedPointElements) {
                if (child.child(other)) {
                    std::string msg = "has a <";
                    msg += other;
                    msg += "> element — only plain terrestrial Circle/MTA observations are supported";
                    return fail(msg);
                }
            }

            const std::string name = ChildText(child, "Name");
            const std::string stationId = stationIdNode.text().as_string();
            const std::string backBearingId = backBearingIdNode.text().as_string();
            if (name.empty()) return fail("missing Name");

            const auto stIt = stationNames.find(stationId);
            if (stIt == stationNames.end()) {
                std::string msg = "StationID ";
                msg += stationId;
                msg += " has no matching StationRecord";
                return fail(msg);
            }
            const auto bbIt = backBearings.find(backBearingId);
            if (bbIt == backBearings.end()) {
                std::string msg = "BackBearingID ";
                msg += backBearingId;
                msg += " has no matching BackBearingRecord";
                return fail(msg);
            }
            if (!bbIt->second.hasFace1)
                return fail("its BackBearingRecord has no Face1HorizontalCircle reading");

            // A Classification of "BackSight" (or a shot whose own Name equals
            // this setup's backsight) is a repeat verification of the already-
            // known backsight direction, not a traverse leg or a detail point —
            // skip it outright. Classification is absent in the simple
            // single-record grammar's own fixtures; absent is indistinguishable
            // from "Normal" here, which is exactly the behavior those need.
            const std::string classification = ChildText(child, "Classification");
            if (classification == "BackSight" || name == bbIt->second.backSight)
                continue;

            const bool isMta = (classification == "MTA") && mtaNode;
            const pugi::xml_node obsNode = isMta ? mtaNode : (circleNode ? circleNode : mtaNode);

            double hz, sd;
            if (!ChildDouble(obsNode, "HorizontalCircle", &hz)) return fail("has no HorizontalCircle reading");
            if (!ChildDouble(obsNode, "EDMDistance", &sd)) return fail("has no EDMDistance");

            ShotObs obs;
            obs.name = name;
            obs.stationIdAttr = stationId;
            obs.backBearingIdAttr = backBearingId;
            obs.hz = hz;
            obs.sd = sd;
            obs.va = 90.0;
            obs.hasVa = ChildDouble(obsNode, "VerticalCircle", &obs.va);
            obs.isMta = (obsNode == mtaNode);
            obs.face = ChildText(circleNode, "Face");  // empty (MTA, or a Face-less Circle) -> Face1.

            shots.push_back(std::move(obs));
            continue;
        }

        // Everything else (UnitsRecord, CoordinateSystemRecord, InstrumentRecord,
        // TargetRecord, AtmosphereRecord, StartRoundsRecord/StartRoundRecord/
        // EndRoundRecord/EndRoundsRecord, RoundsMeanTurnedAngleResiduals,
        // NoteRecord, GPS-related records, ...) carries no traverse-leg
        // information this importer needs — informational only.
    }

    if (stationSeq.empty()) {
        errorMsg = "no StationRecord found";
        return false;
    }

    // ---------- group raw shots by (StationID, Name): one physical sight -------
    std::vector<ShotGroup> groups;
    std::unordered_map<std::string, size_t> groupIndex;
    for (const ShotObs& s : shots) {
        const std::string key = s.stationIdAttr + "\x1f" + s.name;
        size_t idx;
        const auto it = groupIndex.find(key);
        if (it == groupIndex.end()) {
            ShotGroup g;
            g.stationIdAttr = s.stationIdAttr;
            g.name = s.name;
            g.backBearingIdAttr = s.backBearingIdAttr;  // first member's orientation context wins.
            groups.push_back(std::move(g));
            idx = groups.size() - 1;
            groupIndex[key] = idx;
        } else {
            idx = it->second;
        }
        ShotGroup& g = groups[idx];
        if (s.isMta) {
            if (!g.hasMta) {
                g.hasMta = true;
                g.mtaHz = s.hz; g.mtaVa = s.va; g.mtaSd = s.sd; g.mtaHasVa = s.hasVa;
            }
        } else {
            TraverseMeasSet ms;
            ms.setNo = static_cast<int>(g.rawSets.size()) + 1;
            if (s.face == "Face2") {
                ms.hasF2 = true; ms.f2HzDec = s.hz; ms.f2VaDec = s.hasVa ? s.va : 90.0; ms.f2Sd = s.sd;
            } else {
                ms.hasF1 = true; ms.f1HzDec = s.hz; ms.f1VaDec = s.hasVa ? s.va : 90.0; ms.f1Sd = s.sd;
            }
            g.rawSets.push_back(ms);
        }
    }
    auto findGroup = [&](const std::string& stationIdAttr, const std::string& name) -> const ShotGroup* {
        const auto it = groupIndex.find(stationIdAttr + "\x1f" + name);
        return it == groupIndex.end() ? nullptr : &groups[it->second];
    };

    // ---------- sequential integer id map: backbone station names first, so a
    // side shot's station id doesn't depend on processing order ---------------
    std::unordered_map<std::string, int> ptIdMap;
    int nextId = 1;
    auto GetId = [&](const std::string& name) -> int {
        const auto it = ptIdMap.find(name);
        if (it != ptIdMap.end()) return it->second;
        const int id = nextId++;
        ptIdMap[name] = id;
        return id;
    };
    for (const auto& st : stationSeq) GetId(st.second);

    // ---------- backbone traverse legs: one per consecutive StationRecord pair.
    // The surveyor's own setup sequence (not shot order) is the chain — every
    // qualifying shot that ISN'T "from this setup, to the very next setup's
    // point" is a side/check shot (REQ-401-style: every GSTC leg must continue
    // the chain; Trimble's own exports mix in topo/control side shots at every
    // setup, which the old single-Circle-record grammar had no way to tell
    // apart from a real leg). ----------------------------------------------
    std::vector<TraverseLeg> legs;
    std::vector<bool> consumed(groups.size(), false);
    double startBearingDeg = 0.0;
    bool hasStartBearing = false;
    const std::string& firstOccupied = stationSeq.front().second;

    for (size_t k = 0; k + 1 < stationSeq.size(); ++k) {
        const std::string& curName = stationSeq[k].second;
        const std::string& curId = stationSeq[k].first;
        const std::string& nextName = stationSeq[k + 1].second;

        const ShotGroup* g = findGroup(curId, nextName);
        if (!g) {
            errorMsg = "no observation found from station ";
            errorMsg += curName;
            errorMsg += " to station ";
            errorMsg += nextName;
            errorMsg += " (expected to continue the traverse chain)";
            return false;
        }
        const auto bbIt = backBearings.find(g->backBearingIdAttr);
        if (k > 0 && bbIt != backBearings.end() && bbIt->second.backSight != stationSeq[k - 1].second) {
            errorMsg = "the observation from station ";
            errorMsg += curName;
            errorMsg += " to station ";
            errorMsg += nextName;
            errorMsg += " backsights ";
            errorMsg += bbIt->second.backSight;
            errorMsg += ", which doesn't match the station (";
            errorMsg += stationSeq[k - 1].second;
            errorMsg += ") the traverse chain expects";
            return false;
        }

        double hz, va, sd;
        bool hasVa;
        std::string err;
        if (!ReduceGroup(*g, backBearings, &hz, &va, &hasVa, &sd, &err)) { errorMsg = err; return false; }

        TraverseLeg leg;
        leg.stationId = GetId(nextName);
        leg.description = nextName;
        leg.isZenithAngle = true;
        leg.hasHorizAngle = true;
        leg.horizAngleDeg = hz;
        leg.horizAngleBuf = TraverseFormatDist(hz);
        leg.hasSlopeDist = true;
        leg.slopeDist = sd * kMetersToFeet;
        if (hasVa) {
            leg.hasVertAngle = true;
            leg.vertAngleDeg = va;
            leg.vertAngleBuf = TraverseFormatDist(va);
        }

        if (legs.empty() && bbIt != backBearings.end()) {
            double az;
            if (ResolveAzimuth(knownPts, curName, bbIt->second.backSight, &az)) {
                startBearingDeg = az;
                hasStartBearing = true;
            }
            // Otherwise: no coordinates for the backsight station, so no bearing can be
            // resolved — hasStartBearing stays false, matching StarNetImport/Rw5Import.
        }

        legs.push_back(std::move(leg));
        consumed[static_cast<size_t>(g - groups.data())] = true;
    }

    // ---------- closing leg: the last setup shooting back to the start point.
    // Not a backbone StationRecord pair (the crew doesn't re-occupy the start
    // monument, just confirms closure from the last setup) — detected the same
    // way the single-record grammar always has (a shot named for the start). --
    bool isClosedLoop = false;
    {
        const std::string& lastId = stationSeq.back().first;
        if (const ShotGroup* g = findGroup(lastId, firstOccupied)) {
            const auto bbIt = backBearings.find(g->backBearingIdAttr);
            const bool backsightOk = stationSeq.size() < 2 || bbIt == backBearings.end() ||
                                     bbIt->second.backSight == stationSeq[stationSeq.size() - 2].second;
            if (backsightOk) {
                double hz, va, sd;
                bool hasVa;
                std::string err;
                if (ReduceGroup(*g, backBearings, &hz, &va, &hasVa, &sd, &err)) {
                    TraverseLeg leg;
                    leg.stationId = GetId(firstOccupied);
                    leg.description = firstOccupied;
                    leg.isZenithAngle = true;
                    leg.hasHorizAngle = true;
                    leg.horizAngleDeg = hz;
                    leg.horizAngleBuf = TraverseFormatDist(hz);
                    leg.hasSlopeDist = true;
                    leg.slopeDist = sd * kMetersToFeet;
                    if (hasVa) {
                        leg.hasVertAngle = true;
                        leg.vertAngleDeg = va;
                        leg.vertAngleBuf = TraverseFormatDist(va);
                    }
                    legs.push_back(std::move(leg));
                    isClosedLoop = true;
                    consumed[static_cast<size_t>(groupIndex[lastId + "\x1f" + firstOccupied])] = true;
                }
            }
        }
    }

    if (legs.empty()) {
        errorMsg = "no terrestrial traverse observations found";
        return false;
    }

    // ---------- every other shot is a side shot (REQ: issue #853) — a detail
    // point if its name has no known coordinates, a check shot if it does
    // (comparing against the already-known position). A name re-shot from more
    // than one setup gets a ".N" re-observation suffix for the 2nd/3rd/... time,
    // the same convention FbkImport uses for a repeated closing monument — GSTC
    // requires every STN's own point number to be unique. ----------------------
    std::vector<TraverseSideShot> sideShots;
    std::unordered_map<std::string, int> nameUseCount;
    for (const auto& st : stationSeq) nameUseCount[st.second] = 1;  // backbone names already "used".

    for (size_t gi = 0; gi < groups.size(); ++gi) {
        if (consumed[gi]) continue;
        const ShotGroup& g = groups[gi];

        double hz, va, sd;
        bool hasVa;
        std::string err;
        if (!ReduceGroup(g, backBearings, &hz, &va, &hasVa, &sd, &err)) { errorMsg = err; return false; }

        int& useCount = nameUseCount[g.name];
        const std::string displayName = (useCount == 0) ? g.name : (g.name + "." + std::to_string(useCount));
        ++useCount;

        TraverseSideShot ss;
        ss.atStationId = GetId(stationNames[g.stationIdAttr]);
        ss.stationId = GetId(displayName);
        ss.description = displayName;
        ss.isZenithAngle = true;
        ss.hasHorizAngle = true;
        ss.horizAngleDeg = hz;
        ss.horizAngleBuf = TraverseFormatDist(hz);
        ss.hasSlopeDist = true;
        ss.slopeDist = sd * kMetersToFeet;
        if (hasVa) {
            ss.hasVertAngle = true;
            ss.vertAngleDeg = va;
            ss.vertAngleBuf = TraverseFormatDist(va);
        }
        const auto kpIt = knownPts.find(g.name);
        if (kpIt != knownPts.end() && kpIt->second.has) {
            ss.isCheckShot = true;
            ss.hasKnownCoords = true;
            ss.knownEasting = kpIt->second.e;
            ss.knownNorthing = kpIt->second.n;
        }
        sideShots.push_back(std::move(ss));
    }

    // ---------- assemble ------------------------------------------------------
    TraverseData newTd;
    newTd.startStationId = GetId(firstOccupied);
    {
        const auto it = knownPts.find(firstOccupied);
        if (it != knownPts.end() && it->second.has) {
            newTd.startNorthing = it->second.n;
            newTd.startEasting = it->second.e;
            if (it->second.hasZ) newTd.startElevation = it->second.z;
        }
    }
    if (hasStartBearing) {
        newTd.startBearingDeg = startBearingDeg;
        newTd.hasStartBearing = true;
        newTd.startBearingBuf = TraverseFormatBearing(startBearingDeg);
    }

    newTd.legs = std::move(legs);
    newTd.sideShots = std::move(sideShots);
    newTd.isClosedLoop = isClosedLoop;

    td = std::move(newTd);
    return true;
}
