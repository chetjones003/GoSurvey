#include "TraverseIo.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace traverseio {

namespace {

using nlohmann::json;

constexpr const char* kFormat = "gstrav";
constexpr int kVersion = 1;

json SetToJson(const TraverseMeasSet& s) {
  json j;
  j["setNo"] = s.setNo;
  j["f1HzDec"] = s.f1HzDec;
  j["f1VaDec"] = s.f1VaDec;
  j["f1Sd"] = s.f1Sd;
  j["hasF1"] = s.hasF1;
  j["f2HzDec"] = s.f2HzDec;
  j["f2VaDec"] = s.f2VaDec;
  j["f2Sd"] = s.f2Sd;
  j["hasF2"] = s.hasF2;
  return j;
}

TraverseMeasSet SetFromJson(const json& j) {
  TraverseMeasSet s;
  s.setNo = j.value("setNo", 0);
  s.f1HzDec = j.value("f1HzDec", 0.0);
  s.f1VaDec = j.value("f1VaDec", 90.0);
  s.f1Sd = j.value("f1Sd", 0.0);
  s.hasF1 = j.value("hasF1", false);
  s.f2HzDec = j.value("f2HzDec", 0.0);
  s.f2VaDec = j.value("f2VaDec", 90.0);
  s.f2Sd = j.value("f2Sd", 0.0);
  s.hasF2 = j.value("hasF2", false);
  return s;
}

json LegToJson(const TraverseLeg& leg) {
  json j;
  j["stationId"] = leg.stationId;
  j["sourceOrder"] = leg.sourceOrder;
  j["description"] = leg.description;
  j["horizAngleBuf"] = leg.horizAngleBuf;
  j["horizAngleDeg"] = leg.horizAngleDeg;
  j["hasHorizAngle"] = leg.hasHorizAngle;
  j["face1HorizBuf"] = leg.face1HorizBuf;
  j["face2HorizBuf"] = leg.face2HorizBuf;
  j["face1HorizDeg"] = leg.face1HorizDeg;
  j["face2HorizDeg"] = leg.face2HorizDeg;
  j["backsightCircleDeg"] = leg.backsightCircleDeg;
  j["hasBacksightCircle"] = leg.hasBacksightCircle;
  j["horizDist"] = leg.horizDist;
  j["hasHorizDist"] = leg.hasHorizDist;
  j["slopeDist"] = leg.slopeDist;
  j["hasSlopeDist"] = leg.hasSlopeDist;
  j["vertAngleBuf"] = leg.vertAngleBuf;
  j["vertAngleDeg"] = leg.vertAngleDeg;
  j["isZenithAngle"] = leg.isZenithAngle;
  j["hasVertAngle"] = leg.hasVertAngle;
  j["face1VertBuf"] = leg.face1VertBuf;
  j["face2VertBuf"] = leg.face2VertBuf;
  j["face1VertDeg"] = leg.face1VertDeg;
  j["face2VertDeg"] = leg.face2VertDeg;
  j["hasFace1"] = leg.hasFace1;
  j["hasFace2"] = leg.hasFace2;
  json sets = json::array();
  for (const TraverseMeasSet& s : leg.rawSets)
    sets.push_back(SetToJson(s));
  j["rawSets"] = std::move(sets);
  return j;
}

TraverseLeg LegFromJson(const json& j) {
  TraverseLeg leg;
  leg.stationId = j.value("stationId", 0);
  leg.sourceOrder = j.value("sourceOrder", 0);
  leg.description = j.value("description", std::string());
  leg.horizAngleBuf = j.value("horizAngleBuf", std::string());
  leg.horizAngleDeg = j.value("horizAngleDeg", 0.0);
  leg.hasHorizAngle = j.value("hasHorizAngle", false);
  leg.face1HorizBuf = j.value("face1HorizBuf", std::string());
  leg.face2HorizBuf = j.value("face2HorizBuf", std::string());
  leg.face1HorizDeg = j.value("face1HorizDeg", 0.0);
  leg.face2HorizDeg = j.value("face2HorizDeg", 0.0);
  leg.backsightCircleDeg = j.value("backsightCircleDeg", 0.0);
  leg.hasBacksightCircle = j.value("hasBacksightCircle", false);
  leg.horizDist = j.value("horizDist", 0.0);
  leg.hasHorizDist = j.value("hasHorizDist", false);
  leg.slopeDist = j.value("slopeDist", 0.0);
  leg.hasSlopeDist = j.value("hasSlopeDist", false);
  leg.vertAngleBuf = j.value("vertAngleBuf", std::string());
  leg.vertAngleDeg = j.value("vertAngleDeg", 0.0);
  leg.isZenithAngle = j.value("isZenithAngle", true);
  leg.hasVertAngle = j.value("hasVertAngle", false);
  leg.face1VertBuf = j.value("face1VertBuf", std::string());
  leg.face2VertBuf = j.value("face2VertBuf", std::string());
  leg.face1VertDeg = j.value("face1VertDeg", 0.0);
  leg.face2VertDeg = j.value("face2VertDeg", 0.0);
  leg.hasFace1 = j.value("hasFace1", false);
  leg.hasFace2 = j.value("hasFace2", false);
  if (j.contains("rawSets") && j["rawSets"].is_array())
    for (const json& sj : j["rawSets"])
      leg.rawSets.push_back(SetFromJson(sj));
  return leg;
}

json ComponentToJson(const NetworkComponent& component) {
  json j;
  j["value"] = component.value;
  j["fix"] = static_cast<int>(component.fix);
  j["stdError"] = component.stdError;
  j["present"] = component.present;
  return j;
}

NetworkComponent ComponentFromJson(const json& j) {
  NetworkComponent component;
  component.value = j.value("value", 0.0);
  component.fix = static_cast<NetworkFix>(j.value("fix", 0));
  component.stdError = j.value("stdError", 0.0);
  component.present = j.value("present", false);
  return component;
}

// Optional REQ-452 fields are strict: malformed project data must not silently
// become a different measurement or a default layout on reopening.
void Require(bool valid, const char* reason) {
  if (!valid) throw std::invalid_argument(reason);
}

bool OpaqueId(const std::string& id) {
  return !id.empty() && std::none_of(id.begin(), id.end(),
      [](unsigned char c) { return std::isspace(c) != 0; });
}

void FiniteField(const json& entry, const char* field) {
  const auto& value = entry.at(field);
  Require(value.is_number() && std::isfinite(value.get<double>()), "non-finite numeric field");
}

void IntField(const json& entry, const char* field, int minimum) {
  const auto& value = entry.at(field);
  Require(value.is_number_integer(), "expected integer field");
  if (value.is_number_unsigned())
    Require(value.get<uint64_t>() <= static_cast<uint64_t>(std::numeric_limits<int>::max()), "integer out of range");
  const auto number = value.get<int64_t>();
  Require(number >= minimum && number <= std::numeric_limits<int>::max(), "integer out of range");
}

void ValidateOptionalFields(const json& j) {
  if (j.contains("sideShots")) {
    Require(j.at("sideShots").is_array(), "sideShots must be an array");
    for (const auto& entry : j.at("sideShots")) {
      for (const auto* field : {"atStationId", "stationId", "sourceOrder"})
        IntField(entry, field, std::numeric_limits<int>::min());
      for (const auto* field : {"horizAngleDeg", "horizDist", "slopeDist", "vertAngleDeg",
                               "knownEasting", "knownNorthing"})
        FiniteField(entry, field);
      for (const auto* field : {"description", "code", "horizAngleBuf", "vertAngleBuf"})
        Require(entry.at(field).is_string(), "side shot text must be a string");
      for (const auto* field : {"hasHorizAngle", "hasHorizDist", "hasSlopeDist", "isZenithAngle",
                               "hasVertAngle", "isCheckShot", "hasKnownCoords"})
        Require(entry.at(field).is_boolean(), "side shot flag must be a boolean");
    }
  }
  if (j.contains("networkDirections")) {
    Require(j.at("networkDirections").is_array(), "networkDirections must be an array");
    for (const auto& entry : j.at("networkDirections")) {
      for (const auto* field : {"set", "at", "to", "sourceOrder"})
        IntField(entry, field, std::numeric_limits<int>::min());
      FiniteField(entry, "deg");
      FiniteField(entry, "std");
      Require(entry.at("code").is_string(), "direction code must be a string");
    }
  }
  std::unordered_set<std::string> sources;
  if (j.contains("rawSources")) {
    Require(j.at("rawSources").is_array(), "rawSources must be an array");
    for (const auto& entry : j.at("rawSources")) {
      const auto id = entry.at("id").get<std::string>();
      Require(OpaqueId(id) && sources.insert(id).second, "invalid or duplicate source ID");
      Require(entry.at("path").is_string(), "source path must be a string");
    }
  }
  if (j.contains("rawSets")) {
    Require(j.at("rawSets").is_array(), "rawSets must be an array");
    for (const auto& entry : j.at("rawSets")) {
      IntField(entry, "at", 1);
      IntField(entry, "to", 1);
      if (entry.contains("sourceOrder")) IntField(entry, "sourceOrder", 0);
      Require(sources.count(entry.at("sourceId").get<std::string>()) != 0, "unknown raw source ID");
      const auto& readings = entry.at("measurements");
      IntField(readings, "setNo", 1);
      const bool f1 = readings.value("hasF1", false);
      const bool f2 = readings.value("hasF2", false);
      Require(f1 || f2, "raw set needs at least one face");
      if (f1) {
        FiniteField(readings, "f1HzDec");
        FiniteField(readings, "f1VaDec");
        FiniteField(readings, "f1Sd");
      }
      if (f2) {
        FiniteField(readings, "f2HzDec");
        FiniteField(readings, "f2VaDec");
        FiniteField(readings, "f2Sd");
      }
    }
  }
  if (!j.contains("graphLayout")) return;
  const auto& layout = j.at("graphLayout");
  Require(layout.is_object(), "graphLayout must be an object");
  std::unordered_set<std::string> nodes;
  if (layout.contains("nodes")) {
    Require(layout.at("nodes").is_array(), "graph nodes must be an array");
    for (const auto& entry : layout.at("nodes")) {
      const auto id = entry.at("id").get<std::string>();
      Require(OpaqueId(id) && nodes.insert(id).second, "invalid or duplicate node ID");
      const auto command = entry.at("command").get<std::string>();
      Require(!command.empty() && command.find_first_of("\r\n") == std::string::npos, "invalid node command");
      const auto& order = entry.at("sourceOrder");
      Require(order.is_number_integer() && (order.is_number_unsigned() || order.get<int64_t>() >= 0),
              "invalid node source order");
      FiniteField(entry, "x");
      FiniteField(entry, "y");
      if (entry.contains("groupId")) Require(OpaqueId(entry.at("groupId").get<std::string>()), "invalid group ID");
      if (entry.contains("manualOrder")) IntField(entry, "manualOrder", std::numeric_limits<int>::min());
    }
  }
  std::unordered_set<std::string> groups;
  if (layout.contains("groups")) {
    Require(layout.at("groups").is_array(), "graph groups must be an array");
    for (const auto& entry : layout.at("groups")) {
      const auto id = entry.at("id").get<std::string>();
      Require(OpaqueId(id) && groups.insert(id).second, "invalid or duplicate group ID");
      FiniteField(entry, "x");
      FiniteField(entry, "y");
      FiniteField(entry, "width");
      FiniteField(entry, "height");
      Require(entry.at("width").get<double>() >= 0 && entry.at("height").get<double>() >= 0,
              "negative frame bounds");
      Require(entry.at("collapsed").is_boolean() && entry.at("label").is_string(), "invalid frame state");
    }
  }
}

}  // namespace

bool Save(const std::string& path, const TraverseData& td, const LsaWeights& weights, std::string& err) {
  try {
    json j;
    j["format"] = kFormat;
    j["version"] = kVersion;

    j["startStationId"] = td.startStationId;
    j["startEasting"] = td.startEasting;
    j["startNorthing"] = td.startNorthing;
    j["startElevation"] = td.startElevation;
    j["startBearingBuf"] = td.startBearingBuf;
    j["startBearingDeg"] = td.startBearingDeg;
    j["hasStartBearing"] = td.hasStartBearing;

    j["endEasting"] = td.endEasting;
    j["endNorthing"] = td.endNorthing;
    j["endElevation"] = td.endElevation;
    j["hasEndCoordinates"] = td.hasEndCoordinates;

    j["useFace1Face2"] = td.useFace1Face2;
    j["isClosedLoop"] = td.isClosedLoop;
    j["applyScaleFactor"] = td.applyScaleFactor;
    j["scaleFactorKind"] = static_cast<int>(td.scaleFactorKind);
    j["scaleFactor"] = td.scaleFactor;

    json legs = json::array();
    for (const TraverseLeg& leg : td.legs)
      legs.push_back(LegToJson(leg));
    j["legs"] = std::move(legs);

    if (!td.sideShots.empty()) {
      j["sideShots"] = json::array();
      for (const auto& shot : td.sideShots)
        j["sideShots"].push_back({
            {"atStationId", shot.atStationId}, {"stationId", shot.stationId},
            {"description", shot.description}, {"code", shot.code}, {"sourceOrder", shot.sourceOrder},
            {"horizAngleBuf", shot.horizAngleBuf}, {"horizAngleDeg", shot.horizAngleDeg},
            {"hasHorizAngle", shot.hasHorizAngle}, {"horizDist", shot.horizDist},
            {"hasHorizDist", shot.hasHorizDist}, {"slopeDist", shot.slopeDist},
            {"hasSlopeDist", shot.hasSlopeDist}, {"vertAngleBuf", shot.vertAngleBuf},
            {"vertAngleDeg", shot.vertAngleDeg}, {"isZenithAngle", shot.isZenithAngle},
            {"hasVertAngle", shot.hasVertAngle}, {"isCheckShot", shot.isCheckShot},
            {"hasKnownCoords", shot.hasKnownCoords}, {"knownEasting", shot.knownEasting},
            {"knownNorthing", shot.knownNorthing}});
    }

    json stations = json::array();
    for (const NetworkStation& station : td.networkStations) {
      json sj;
      sj["id"] = station.id;
      sj["sourceOrder"] = station.sourceOrder;
      sj["easting"] = ComponentToJson(station.easting);
      sj["northing"] = ComponentToJson(station.northing);
      sj["elevation"] = ComponentToJson(station.elevation);
      stations.push_back(std::move(sj));
    }
    j["networkStations"] = std::move(stations);
    json angles = json::array();
    for (const NetworkAngleObs& angle : td.networkAngles)
      angles.push_back(json{{"at", angle.atId}, {"from", angle.fromId}, {"to", angle.toId},
                            {"deg", angle.angleDeg}, {"std", angle.stdErrorSec}, {"sourceOrder", angle.sourceOrder}});
    j["networkAngles"] = std::move(angles);
    json dists = json::array();
    for (const NetworkDistObs& dist : td.networkDists)
      dists.push_back(json{{"from", dist.fromId}, {"to", dist.toId}, {"dist", dist.distance}, {"std", dist.stdError}, {"sourceOrder", dist.sourceOrder}});
    j["networkDists"] = std::move(dists);
    json azimuths = json::array();
    for (const NetworkAzimuthObs& azimuth : td.networkAzimuths)
      azimuths.push_back(json{{"from", azimuth.fromId}, {"to", azimuth.toId}, {"deg", azimuth.azimuthDeg},
                              {"std", azimuth.stdErrorSec}, {"sourceOrder", azimuth.sourceOrder}});
    j["networkAzimuths"] = std::move(azimuths);
    json diffs = json::array();
    for (const NetworkElevDiffObs& diff : td.networkElevDiffs)
      diffs.push_back(json{{"from", diff.fromId}, {"to", diff.toId}, {"dz", diff.deltaZ}, {"std", diff.stdError}, {"sourceOrder", diff.sourceOrder}});
    j["networkElevDiffs"] = std::move(diffs);
    json zeniths = json::array();
    for (const NetworkZenithObs& zenith : td.networkZeniths)
      zeniths.push_back(json{{"from", zenith.fromId}, {"to", zenith.toId}, {"deg", zenith.zenithDeg},
                             {"std", zenith.stdErrorSec}, {"sourceOrder", zenith.sourceOrder}});
    j["networkZeniths"] = std::move(zeniths);

    if (!td.networkDirections.empty()) {
      j["networkDirections"] = json::array();
      for (const auto& direction : td.networkDirections)
        j["networkDirections"].push_back({{"set", direction.setId}, {"at", direction.atId},
            {"to", direction.toId}, {"deg", direction.directionDeg}, {"std", direction.stdErrorSec},
            {"code", direction.code}, {"sourceOrder", direction.sourceOrder}});
    }

    json w;
    w["sigmaAngleSec"] = weights.sigmaAngleSec;
    w["sigmaDistConstFt"] = weights.sigmaDistConstFt;
    w["sigmaDistPpm"] = weights.sigmaDistPpm;
    w["sigmaVertConstFt"] = weights.sigmaVertConstFt;
    w["sigmaVertPpm"] = weights.sigmaVertPpm;
    // REQ-422 — project adjustment settings.
    w["sigmaAzimuthSec"] = weights.sigmaAzimuthSec;
    w["sigmaZenithSec"] = weights.sigmaZenithSec;
    w["sigmaLevelFt"] = weights.sigmaLevelFt;
    w["centeringInstrumentFt"] = weights.centeringInstrumentFt;
    w["centeringTargetFt"] = weights.centeringTargetFt;
    w["centeringVerticalFt"] = weights.centeringVerticalFt;
    w["adjustmentIs3D"] = weights.adjustmentIs3D;
    w["refractionCoefficient"] = weights.refractionCoefficient;
    w["convergenceLimit"] = weights.convergenceLimit;
    w["maxIterations"] = weights.maxIterations;
    j["weights"] = std::move(w);

    j["importedFiles"] = td.importedFiles;  // REQ-423.
    if (!td.rawSources.empty()) {
      j["rawSources"] = json::array();
      for (const auto& source : td.rawSources)
        j["rawSources"].push_back({{"id", source.id}, {"path", source.path}});
    }
    if (!td.rawSets.empty()) {
      j["rawSets"] = json::array();
      for (const auto& raw : td.rawSets)
        j["rawSets"].push_back({{"at", raw.atStationId}, {"to", raw.toStationId},
                                {"sourceId", raw.sourceId}, {"sourceOrder", raw.sourceOrder},
                                {"measurements", SetToJson(raw.measurements)}});
    }

    if (!td.graphNodes.empty() || !td.graphGroups.empty()) {
      json layout{{"nodes", json::array()}, {"groups", json::array()}};
      for (const auto& node : td.graphNodes) {
        json entry{{"id", node.id}, {"command", node.command}, {"sourceOrder", node.sourceOrder},
                   {"x", node.x}, {"y", node.y}};
        if (!node.groupId.empty()) entry["groupId"] = node.groupId;
        if (node.manualOrder) entry["manualOrder"] = *node.manualOrder;
        layout["nodes"].push_back(std::move(entry));
      }
      for (const auto& group : td.graphGroups)
        layout["groups"].push_back({{"id", group.id}, {"x", group.x}, {"y", group.y},
                                    {"width", group.width}, {"height", group.height},
                                    {"collapsed", group.collapsed}, {"label", group.label}});
      j["graphLayout"] = std::move(layout);
    }

    ValidateOptionalFields(j);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
      err = "Could not open \"" + path + "\" for writing.";
      return false;
    }
    f << j.dump(2);
    if (!f) {
      err = "Could not write \"" + path + "\".";
      return false;
    }
    err.clear();
    return true;
  } catch (const std::exception& failure) {
    err = "Could not save traverse: " + std::string(failure.what());
    return false;
  }
}

bool Load(const std::string& path, TraverseData& td, LsaWeights& weights, std::string& err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "Could not open \"" + path + "\".";
    return false;
  }
  try {
    json j = json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
      err = "\"" + path + "\" is not a valid .gstrav file (malformed JSON).";
      return false;
    }
    if (j.value("format", std::string()) != kFormat) {
      err = "\"" + path + "\" is not a GoSurvey traverse file.";
      return false;
    }
    if (!j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int>() > kVersion) {
      err = "\"" + path + "\" was saved by a newer version of GoSurvey and cannot be read.";
      return false;
    }

    ValidateOptionalFields(j);
    TraverseData out;
    out.startStationId = j.value("startStationId", 1);
    out.startEasting = j.value("startEasting", 0.0);
    out.startNorthing = j.value("startNorthing", 0.0);
    out.startElevation = j.value("startElevation", 0.0);
    out.startBearingBuf = j.value("startBearingBuf", std::string());
    out.startBearingDeg = j.value("startBearingDeg", 0.0);
    out.hasStartBearing = j.value("hasStartBearing", false);

    out.endEasting = j.value("endEasting", 0.0);
    out.endNorthing = j.value("endNorthing", 0.0);
    out.endElevation = j.value("endElevation", 0.0);
    out.hasEndCoordinates = j.value("hasEndCoordinates", false);

    out.useFace1Face2 = j.value("useFace1Face2", false);
    out.isClosedLoop = j.value("isClosedLoop", false);
    out.applyScaleFactor = j.value("applyScaleFactor", false);
    out.scaleFactorKind = static_cast<TraverseData::ScaleFactorKind>(j.value("scaleFactorKind", 0));
    out.scaleFactor = j.value("scaleFactor", 1.0);

    if (j.contains("legs") && j["legs"].is_array())
      for (const json& lj : j["legs"])
        out.legs.push_back(LegFromJson(lj));

    if (j.contains("sideShots")) {
      for (const auto& entry : j.at("sideShots")) {
        TraverseSideShot shot;
        shot.atStationId = entry.at("atStationId").get<int>();
        shot.stationId = entry.at("stationId").get<int>();
        shot.description = entry.at("description").get<std::string>();
        shot.code = entry.at("code").get<std::string>();
        shot.sourceOrder = entry.at("sourceOrder").get<int>();
        shot.horizAngleBuf = entry.at("horizAngleBuf").get<std::string>();
        shot.horizAngleDeg = entry.at("horizAngleDeg").get<double>();
        shot.hasHorizAngle = entry.at("hasHorizAngle").get<bool>();
        shot.horizDist = entry.at("horizDist").get<double>();
        shot.hasHorizDist = entry.at("hasHorizDist").get<bool>();
        shot.slopeDist = entry.at("slopeDist").get<double>();
        shot.hasSlopeDist = entry.at("hasSlopeDist").get<bool>();
        shot.vertAngleBuf = entry.at("vertAngleBuf").get<std::string>();
        shot.vertAngleDeg = entry.at("vertAngleDeg").get<double>();
        shot.isZenithAngle = entry.at("isZenithAngle").get<bool>();
        shot.hasVertAngle = entry.at("hasVertAngle").get<bool>();
        shot.isCheckShot = entry.at("isCheckShot").get<bool>();
        shot.hasKnownCoords = entry.at("hasKnownCoords").get<bool>();
        shot.knownEasting = entry.at("knownEasting").get<double>();
        shot.knownNorthing = entry.at("knownNorthing").get<double>();
        out.sideShots.push_back(std::move(shot));
      }
    }

    if (j.contains("networkStations") && j["networkStations"].is_array()) {
      for (const json& sj : j["networkStations"]) {
        NetworkStation station;
        station.id = sj.value("id", 0);
        station.sourceOrder = sj.value("sourceOrder", 0);
        if (sj.contains("easting")) station.easting = ComponentFromJson(sj["easting"]);
        if (sj.contains("northing")) station.northing = ComponentFromJson(sj["northing"]);
        if (sj.contains("elevation")) station.elevation = ComponentFromJson(sj["elevation"]);
        out.networkStations.push_back(station);
      }
    }
    if (j.contains("networkAngles") && j["networkAngles"].is_array()) {
      for (const json& aj : j["networkAngles"]) {
        NetworkAngleObs angle;
        angle.atId = aj.value("at", 0);
        angle.fromId = aj.value("from", 0);
        angle.toId = aj.value("to", 0);
        angle.angleDeg = aj.value("deg", 0.0);
        angle.stdErrorSec = aj.value("std", 0.0);
        angle.sourceOrder = aj.value("sourceOrder", 0);
        out.networkAngles.push_back(angle);
      }
    }
    if (j.contains("networkDists") && j["networkDists"].is_array()) {
      for (const json& dj : j["networkDists"]) {
        NetworkDistObs dist;
        dist.fromId = dj.value("from", 0);
        dist.toId = dj.value("to", 0);
        dist.distance = dj.value("dist", 0.0);
        dist.stdError = dj.value("std", 0.0);
        dist.sourceOrder = dj.value("sourceOrder", 0);
        out.networkDists.push_back(dist);
      }
    }
    if (j.contains("networkAzimuths") && j["networkAzimuths"].is_array()) {
      for (const json& aj : j["networkAzimuths"]) {
        NetworkAzimuthObs azimuth;
        azimuth.fromId = aj.value("from", 0);
        azimuth.toId = aj.value("to", 0);
        azimuth.azimuthDeg = aj.value("deg", 0.0);
        azimuth.stdErrorSec = aj.value("std", 0.0);
        azimuth.sourceOrder = aj.value("sourceOrder", 0);
        out.networkAzimuths.push_back(azimuth);
      }
    }
    if (j.contains("networkElevDiffs") && j["networkElevDiffs"].is_array()) {
      for (const json& dj : j["networkElevDiffs"]) {
        NetworkElevDiffObs diff;
        diff.fromId = dj.value("from", 0);
        diff.toId = dj.value("to", 0);
        diff.deltaZ = dj.value("dz", 0.0);
        diff.stdError = dj.value("std", 0.0);
        diff.sourceOrder = dj.value("sourceOrder", 0);
        out.networkElevDiffs.push_back(diff);
      }
    }
    if (j.contains("networkZeniths") && j["networkZeniths"].is_array()) {
      for (const json& zj : j["networkZeniths"]) {
        NetworkZenithObs zenith;
        zenith.fromId = zj.value("from", 0);
        zenith.toId = zj.value("to", 0);
        zenith.zenithDeg = zj.value("deg", 90.0);
        zenith.stdErrorSec = zj.value("std", 0.0);
        zenith.sourceOrder = zj.value("sourceOrder", 0);
        out.networkZeniths.push_back(zenith);
      }
    }

    if (j.contains("networkDirections")) {
      for (const auto& entry : j.at("networkDirections")) {
        NetworkDirectionObs direction;
        direction.setId = entry.at("set").get<int>();
        direction.atId = entry.at("at").get<int>();
        direction.toId = entry.at("to").get<int>();
        direction.directionDeg = entry.at("deg").get<double>();
        direction.stdErrorSec = entry.at("std").get<double>();
        direction.code = entry.at("code").get<std::string>();
        direction.sourceOrder = entry.at("sourceOrder").get<int>();
        out.networkDirections.push_back(std::move(direction));
      }
    }

    LsaWeights w;
    if (j.contains("weights") && j["weights"].is_object()) {
      const json& wj = j["weights"];
      w.sigmaAngleSec = wj.value("sigmaAngleSec", w.sigmaAngleSec);
      w.sigmaDistConstFt = wj.value("sigmaDistConstFt", w.sigmaDistConstFt);
      w.sigmaDistPpm = wj.value("sigmaDistPpm", w.sigmaDistPpm);
      w.sigmaVertConstFt = wj.value("sigmaVertConstFt", w.sigmaVertConstFt);
      w.sigmaVertPpm = wj.value("sigmaVertPpm", w.sigmaVertPpm);
      // REQ-422 — project adjustment settings.
      w.sigmaAzimuthSec = wj.value("sigmaAzimuthSec", w.sigmaAzimuthSec);
      w.sigmaZenithSec = wj.value("sigmaZenithSec", w.sigmaZenithSec);
      w.sigmaLevelFt = wj.value("sigmaLevelFt", w.sigmaLevelFt);
      w.centeringInstrumentFt = wj.value("centeringInstrumentFt", w.centeringInstrumentFt);
      w.centeringTargetFt = wj.value("centeringTargetFt", w.centeringTargetFt);
      w.centeringVerticalFt = wj.value("centeringVerticalFt", w.centeringVerticalFt);
      w.adjustmentIs3D = wj.value("adjustmentIs3D", w.adjustmentIs3D);
      w.refractionCoefficient = wj.value("refractionCoefficient", w.refractionCoefficient);
      w.convergenceLimit = wj.value("convergenceLimit", w.convergenceLimit);
      w.maxIterations = wj.value("maxIterations", w.maxIterations);
    }
    if (j.contains("importedFiles") && j["importedFiles"].is_array()) {
      out.importedFiles.clear();
      for (const auto& entry : j["importedFiles"])
        if (entry.is_string()) out.importedFiles.push_back(entry.get<std::string>());
    }

    if (j.contains("rawSources")) {
      for (const auto& entry : j.at("rawSources"))
        out.rawSources.push_back({entry.at("id").get<std::string>(), entry.at("path").get<std::string>()});
    }
    if (j.contains("rawSets")) {
      for (const auto& entry : j.at("rawSets")) {
        TraverseRawSet raw;
        raw.atStationId = entry.at("at").get<int>();
        raw.toStationId = entry.at("to").get<int>();
        raw.sourceId = entry.at("sourceId").get<std::string>();
        raw.sourceOrder = entry.value("sourceOrder", 0);
        raw.measurements = SetFromJson(entry.at("measurements"));
        out.rawSets.push_back(std::move(raw));
      }
    }

    if (j.contains("graphLayout")) {
      const auto& layout = j.at("graphLayout");
      if (layout.contains("nodes")) {
        for (const auto& entry : layout.at("nodes")) {
          TraverseGraphNode node;
          node.id = entry.at("id").get<std::string>();
          node.command = entry.at("command").get<std::string>();
          node.sourceOrder = entry.at("sourceOrder").get<size_t>();
          node.x = entry.at("x").get<double>();
          node.y = entry.at("y").get<double>();
          node.groupId = entry.value("groupId", std::string());
          if (entry.contains("manualOrder")) node.manualOrder = entry.at("manualOrder").get<int>();
          out.graphNodes.push_back(std::move(node));
        }
      }
      if (layout.contains("groups")) {
        for (const auto& entry : layout.at("groups")) {
          TraverseGraphGroup group;
          group.id = entry.at("id").get<std::string>();
          group.x = entry.at("x").get<double>();
          group.y = entry.at("y").get<double>();
          group.width = entry.at("width").get<double>();
          group.height = entry.at("height").get<double>();
          group.collapsed = entry.at("collapsed").get<bool>();
          group.label = entry.at("label").get<std::string>();
          out.graphGroups.push_back(std::move(group));
        }
      }
    }

    td = std::move(out);
    weights = w;
    err.clear();
    return true;
  } catch (const std::exception& failure) {
    err = "Invalid traverse file: " + std::string(failure.what());
    return false;
  }
}

}  // namespace traverseio
