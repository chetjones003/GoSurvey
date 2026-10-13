#include "JobXmlImport.hpp"

#include <pugixml.hpp>

#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Trimble JobXML importer (GitHub issue #846, Phase B). See JobXmlImport.hpp
// for the exact supported grammar and the documented defined-gap boundary.
// Grounded in Trimble's official JobXMLSchema-5.72.xsd.
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
// instead of) a plain Circle — GPS/ECEF/resection/offset/etc. methods.
constexpr const char* kUnsupportedPointElements[] = {
    "WGS84", "ECEF", "RTXECEF", "Local", "Polar", "EnteredData", "AngleOffset",
    "DistanceOffset", "DualPrismOffset", "CircularObject", "RemoteObject",
    "MTA", "LevelData", "ReducedLevelData", "ReducedLevelObsReferences",
    "StationAndOffset", "PhotogrammetryObs", "PointOnPlane",
};

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

    // ---------- known points, from <Reductions><Point> (for azimuth lookup) --
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

    // ---------- FieldBook walk ------------------------------------------------
    struct BackBearingRec { std::string backSight; double face1Hz = 0.0; bool hasFace1 = false; };
    std::unordered_map<std::string, std::string> stationNames;       // StationRecord ID -> name
    std::unordered_map<std::string, BackBearingRec> backBearings;    // BackBearingRecord ID -> rec

    std::string firstOccupied;
    bool haveFirstOccupied = false;
    std::vector<TraverseLeg> legs;
    double startBearingDeg = 0.0;
    bool hasStartBearing = false;

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
            const pugi::xml_node stationIdNode = child.child("StationID");
            const pugi::xml_node backBearingIdNode = child.child("BackBearingID");
            const bool anyTerrestrial = circleNode || stationIdNode || backBearingIdNode;
            if (!anyTerrestrial)
                continue;  // a keyed-in/Cogo/non-observation point — not a leg, not an error.
            if (!circleNode || !stationIdNode || !backBearingIdNode)
                return fail("a terrestrial-looking record is missing Circle/StationID/BackBearingID");

            for (const char* other : kUnsupportedPointElements) {
                if (child.child(other)) {
                    std::string msg = "has a <";
                    msg += other;
                    msg += "> element — only plain terrestrial Circle observations are supported";
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

            double hz, sd;
            if (!ChildDouble(circleNode, "HorizontalCircle", &hz))
                return fail("Circle has no HorizontalCircle reading");
            if (!ChildDouble(circleNode, "EDMDistance", &sd))
                return fail("Circle has no EDMDistance");

            const std::string& occupied = stIt->second;
            if (!haveFirstOccupied) {
                firstOccupied = occupied;
                haveFirstOccupied = true;
            }
            const std::string expectedAt = legs.empty() ? firstOccupied : legs.back().description;
            if (occupied != expectedAt) {
                std::string msg = "occupies ";
                msg += occupied;
                msg += ", which doesn't continue the traverse chain (expected ";
                msg += expectedAt;
                msg += ")";
                return fail(msg);
            }

            if (!legs.empty()) {
                const std::string expectedBack =
                    (legs.size() == 1) ? firstOccupied : legs[legs.size() - 2].description;
                if (bbIt->second.backSight != expectedBack) {
                    std::string msg = "its backsight (";
                    msg += bbIt->second.backSight;
                    msg += ") doesn't match the station the traverse chain expects (";
                    msg += expectedBack;
                    msg += ")";
                    return fail(msg);
                }
            }

            TraverseLeg leg;
            leg.description = name;
            leg.hasHorizAngle = true;
            leg.horizAngleDeg = TraverseNormBearing(hz - bbIt->second.face1Hz);
            leg.hasSlopeDist = true;
            leg.slopeDist = sd * kMetersToFeet;
            leg.isZenithAngle = true;
            double va;
            if (ChildDouble(circleNode, "VerticalCircle", &va)) {
                leg.hasVertAngle = true;
                leg.vertAngleDeg = va;
            }

            if (legs.empty()) {
                double az;
                if (ResolveAzimuth(knownPts, occupied, bbIt->second.backSight, &az)) {
                    startBearingDeg = az;
                    hasStartBearing = true;
                }
                // Otherwise: no coordinates for the backsight station, so no bearing can be
                // resolved — hasStartBearing stays false, matching StarNetImport/Rw5Import.
            }

            legs.push_back(std::move(leg));
            continue;
        }

        // Everything else (UnitsRecord, CoordinateSystemRecord, InstrumentRecord,
        // TargetRecord, LineRecord, NoteRecord, GPS-related records, ...) carries
        // no traverse-leg information this importer needs — informational only.
    }

    if (legs.empty()) {
        errorMsg = "no terrestrial traverse observations found";
        return false;
    }

    // ---------- sequential integer id map (preserves encounter order) --------
    std::unordered_map<std::string, int> ptIdMap;
    int nextId = 1;
    auto GetId = [&](const std::string& name) -> int {
        const auto it = ptIdMap.find(name);
        if (it != ptIdMap.end()) return it->second;
        const int id = nextId++;
        ptIdMap[name] = id;
        return id;
    };
    GetId(firstOccupied);
    for (auto& leg : legs) leg.stationId = GetId(leg.description);

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

    if (!newTd.legs.empty() && newTd.legs.back().description == firstOccupied)
        newTd.isClosedLoop = true;

    td = std::move(newTd);
    return true;
}
