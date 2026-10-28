#include "TraverseIo.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

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

}  // namespace

bool Save(const std::string& path, const TraverseData& td, const LsaWeights& weights, std::string& err) {
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

  json sideShots = json::array();
  for (const TraverseSideShot& shot : td.sideShots) {
    sideShots.push_back(json{
        {"horizAngleDeg", shot.horizAngleDeg}, {"horizDist", shot.horizDist},
        {"slopeDist", shot.slopeDist}, {"vertAngleDeg", shot.vertAngleDeg},
        {"knownEasting", shot.knownEasting}, {"knownNorthing", shot.knownNorthing},
        {"description", shot.description}, {"code", shot.code}, {"sourceOrder", shot.sourceOrder},
        {"horizAngleBuf", shot.horizAngleBuf}, {"vertAngleBuf", shot.vertAngleBuf},
        {"atStationId", shot.atStationId}, {"stationId", shot.stationId},
        {"hasHorizAngle", shot.hasHorizAngle}, {"hasHorizDist", shot.hasHorizDist},
        {"hasSlopeDist", shot.hasSlopeDist}, {"isZenithAngle", shot.isZenithAngle},
        {"hasVertAngle", shot.hasVertAngle}, {"isCheckShot", shot.isCheckShot},
        {"hasKnownCoords", shot.hasKnownCoords}});
  }
  j["sideShots"] = std::move(sideShots);

  json legs = json::array();
  for (const TraverseLeg& leg : td.legs)
    legs.push_back(LegToJson(leg));
  j["legs"] = std::move(legs);

  json stations = json::array();
  for (const NetworkStation& station : td.networkStations) {
    json sj;
    sj["id"] = station.id;
    sj["easting"] = ComponentToJson(station.easting);
    sj["northing"] = ComponentToJson(station.northing);
    sj["elevation"] = ComponentToJson(station.elevation);
    stations.push_back(std::move(sj));
  }
  j["networkStations"] = std::move(stations);
  json angles = json::array();
  for (const NetworkAngleObs& angle : td.networkAngles)
    angles.push_back(json{{"at", angle.atId}, {"from", angle.fromId}, {"to", angle.toId},
                          {"deg", angle.angleDeg}, {"std", angle.stdErrorSec}});
  j["networkAngles"] = std::move(angles);
  json dists = json::array();
  for (const NetworkDistObs& dist : td.networkDists)
    dists.push_back(json{{"from", dist.fromId}, {"to", dist.toId}, {"dist", dist.distance}, {"std", dist.stdError}});
  j["networkDists"] = std::move(dists);
  json azimuths = json::array();
  for (const NetworkAzimuthObs& azimuth : td.networkAzimuths)
    azimuths.push_back(json{{"from", azimuth.fromId}, {"to", azimuth.toId}, {"deg", azimuth.azimuthDeg},
                            {"std", azimuth.stdErrorSec}});
  j["networkAzimuths"] = std::move(azimuths);
  json diffs = json::array();
  for (const NetworkElevDiffObs& diff : td.networkElevDiffs)
    diffs.push_back(json{{"from", diff.fromId}, {"to", diff.toId}, {"dz", diff.deltaZ}, {"std", diff.stdError}});
  j["networkElevDiffs"] = std::move(diffs);
  json zeniths = json::array();
  for (const NetworkZenithObs& zenith : td.networkZeniths)
    zeniths.push_back(json{{"from", zenith.fromId}, {"to", zenith.toId}, {"deg", zenith.zenithDeg},
                           {"std", zenith.stdErrorSec}});
  j["networkZeniths"] = std::move(zeniths);
  json directions = json::array();
  for (const NetworkDirectionObs& direction : td.networkDirections)
    directions.push_back(json{{"setId", direction.setId}, {"at", direction.atId}, {"to", direction.toId},
                              {"deg", direction.directionDeg}, {"std", direction.stdErrorSec},
                              {"code", direction.code}, {"sourceOrder", direction.sourceOrder}});
  j["networkDirections"] = std::move(directions);

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
  return true;
}

bool Load(const std::string& path, TraverseData& td, LsaWeights& weights, std::string& err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "Could not open \"" + path + "\".";
    return false;
  }
  json j = json::parse(f, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    err = "\"" + path + "\" is not a valid .gstrav file (malformed JSON).";
    return false;
  }
  try {
  if (j.value("format", std::string()) != kFormat) {
    err = "\"" + path + "\" is not a GoSurvey traverse file.";
    return false;
  }
  if (!j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int>() > kVersion) {
    err = "\"" + path + "\" was saved by a newer version of GoSurvey and cannot be read.";
    return false;
  }

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

  if (j.contains("sideShots") && j["sideShots"].is_array()) {
    for (const json& sj : j["sideShots"]) {
      TraverseSideShot shot;
      shot.horizAngleDeg = sj.value("horizAngleDeg", 0.0);
      shot.horizDist = sj.value("horizDist", 0.0);
      shot.slopeDist = sj.value("slopeDist", 0.0);
      shot.vertAngleDeg = sj.value("vertAngleDeg", 0.0);
      shot.knownEasting = sj.value("knownEasting", 0.0);
      shot.knownNorthing = sj.value("knownNorthing", 0.0);
      shot.description = sj.value("description", std::string());
      shot.code = sj.value("code", std::string());
      shot.sourceOrder = sj.value("sourceOrder", 0);
      shot.horizAngleBuf = sj.value("horizAngleBuf", std::string());
      shot.vertAngleBuf = sj.value("vertAngleBuf", std::string());
      shot.atStationId = sj.value("atStationId", 0);
      shot.stationId = sj.value("stationId", 0);
      shot.hasHorizAngle = sj.value("hasHorizAngle", false);
      shot.hasHorizDist = sj.value("hasHorizDist", false);
      shot.hasSlopeDist = sj.value("hasSlopeDist", false);
      shot.isZenithAngle = sj.value("isZenithAngle", true);
      shot.hasVertAngle = sj.value("hasVertAngle", false);
      shot.isCheckShot = sj.value("isCheckShot", false);
      shot.hasKnownCoords = sj.value("hasKnownCoords", false);
      out.sideShots.push_back(std::move(shot));
    }
  }

  if (j.contains("legs") && j["legs"].is_array())
    for (const json& lj : j["legs"])
      out.legs.push_back(LegFromJson(lj));

  if (j.contains("networkStations") && j["networkStations"].is_array()) {
    for (const json& sj : j["networkStations"]) {
      NetworkStation station;
      station.id = sj.value("id", 0);
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
      out.networkZeniths.push_back(zenith);
    }
  }
  if (j.contains("networkDirections") && j["networkDirections"].is_array()) {
    for (const json& dj : j["networkDirections"]) {
      NetworkDirectionObs direction;
      direction.setId = dj.value("setId", 0);
      direction.atId = dj.value("at", 0);
      direction.toId = dj.value("to", 0);
      direction.directionDeg = dj.value("deg", 0.0);
      direction.stdErrorSec = dj.value("std", 0.0);
      direction.code = dj.value("code", std::string());
      direction.sourceOrder = dj.value("sourceOrder", 0);
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

  td = std::move(out);
  weights = w;
  return true;
  } catch (const json::exception& ex) {
    err = "Could not read \"" + path + "\": invalid .gstrav data (" + ex.what() + ").";
    return false;
  }
}

}  // namespace traverseio
