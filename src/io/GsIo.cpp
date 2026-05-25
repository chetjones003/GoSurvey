#include "GsIo.hpp"

#include "CadCommands.hpp"
#include "CadCoordinateFrame.hpp"
#include "SurveyPoints.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

constexpr int kGsFormatVersion = 1;

using nlohmann::json;

void EntityAttributesToJson(const EntityAttributes& e, json& o) {
  o["layer"] = e.layer;
  o["color"] = e.color;
  o["linetype"] = e.linetype;
  o["lineweightMm"] = e.lineweightMm;
  o["transparency"] = e.transparency;
}

EntityAttributes EntityAttributesFromJson(const json& o) {
  EntityAttributes e;
  if (o.contains("layer") && o["layer"].is_string())
    e.layer = o["layer"].get<std::string>();
  if (o.contains("color") && o["color"].is_string())
    e.color = o["color"].get<std::string>();
  if (o.contains("linetype") && o["linetype"].is_string())
    e.linetype = o["linetype"].get<std::string>();
  if (o.contains("lineweightMm") && o["lineweightMm"].is_number())
    e.lineweightMm = o["lineweightMm"].get<float>();
  if (o.contains("transparency") && o["transparency"].is_number())
    e.transparency = o["transparency"].get<float>();
  return e;
}

void CadLayerRowToJson(const CadLayerRow& r, json& o) {
  o["name"] = r.name;
  o["on"] = r.on;
  o["frozen"] = r.frozen;
  o["locked"] = r.locked;
  o["color"] = r.color;
  o["linetype"] = r.linetype;
  o["lineweightMm"] = r.lineweightMm;
  o["transparency"] = r.transparency;
}

CadLayerRow CadLayerRowFromJson(const json& o) {
  CadLayerRow r;
  if (o.contains("name") && o["name"].is_string())
    r.name = o["name"].get<std::string>();
  if (o.contains("on"))
    r.on = o["on"].get<bool>();
  if (o.contains("frozen"))
    r.frozen = o["frozen"].get<bool>();
  if (o.contains("locked"))
    r.locked = o["locked"].get<bool>();
  if (o.contains("color") && o["color"].is_string())
    r.color = o["color"].get<std::string>();
  if (o.contains("linetype") && o["linetype"].is_string())
    r.linetype = o["linetype"].get<std::string>();
  if (o.contains("lineweightMm") && o["lineweightMm"].is_number())
    r.lineweightMm = o["lineweightMm"].get<float>();
  if (o.contains("transparency") && o["transparency"].is_number())
    r.transparency = o["transparency"].get<float>();
  return r;
}

const char* AnnotationKindTag(CadAnnotation::Kind k) {
  switch (k) {
  case CadAnnotation::Kind::Text:
    return "text";
  case CadAnnotation::Kind::Mtext:
    return "mtext";
  case CadAnnotation::Kind::DimAligned:
    return "dim";
  case CadAnnotation::Kind::DimLinear:
    return "dimlinear";
  default:
    return "text";
  }
}

CadAnnotation::Kind AnnotationKindFromString(const std::string& s) {
  if (s == "mtext")
    return CadAnnotation::Kind::Mtext;
  if (s == "dim")
    return CadAnnotation::Kind::DimAligned;
  if (s == "dimlinear")
    return CadAnnotation::Kind::DimLinear;
  return CadAnnotation::Kind::Text;
}

void CadAnnotationToJson(const CadAnnotation& a, json& o) {
  o["kind"] = AnnotationKindTag(a.kind);
  o["insX"] = a.insX;
  o["insY"] = a.insY;
  o["plottedHeightInches"] = a.plottedHeightInches;
  o["rotationRad"] = a.rotationRad;
  o["text"] = a.text;
  o["boxMinX"] = a.boxMinX;
  o["boxMinY"] = a.boxMinY;
  o["boxMaxX"] = a.boxMaxX;
  o["boxMaxY"] = a.boxMaxY;
  o["dimExt1X"] = a.dimExt1X;
  o["dimExt1Y"] = a.dimExt1Y;
  o["dimExt2X"] = a.dimExt2X;
  o["dimExt2Y"] = a.dimExt2Y;
  o["dimSignedOffset"] = a.dimSignedOffset;
  if (a.kind == CadAnnotation::Kind::DimLinear)
    o["dimLinearVertical"] = a.dimLinearVertical;
  o["surveyPointLabelFor"] = a.surveyPointLabelFor;
}

CadAnnotation CadAnnotationFromJson(const json& o) {
  CadAnnotation a;
  if (o.contains("kind") && o["kind"].is_string())
    a.kind = AnnotationKindFromString(o["kind"].get<std::string>());
  if (o.contains("insX"))
    a.insX = o["insX"].get<float>();
  if (o.contains("insY"))
    a.insY = o["insY"].get<float>();
  if (o.contains("plottedHeightInches"))
    a.plottedHeightInches = o["plottedHeightInches"].get<float>();
  if (o.contains("rotationRad"))
    a.rotationRad = o["rotationRad"].get<float>();
  if (o.contains("text") && o["text"].is_string())
    a.text = o["text"].get<std::string>();
  if (o.contains("boxMinX"))
    a.boxMinX = o["boxMinX"].get<float>();
  if (o.contains("boxMinY"))
    a.boxMinY = o["boxMinY"].get<float>();
  if (o.contains("boxMaxX"))
    a.boxMaxX = o["boxMaxX"].get<float>();
  if (o.contains("boxMaxY"))
    a.boxMaxY = o["boxMaxY"].get<float>();
  if (o.contains("dimExt1X"))
    a.dimExt1X = o["dimExt1X"].get<float>();
  if (o.contains("dimExt1Y"))
    a.dimExt1Y = o["dimExt1Y"].get<float>();
  if (o.contains("dimExt2X"))
    a.dimExt2X = o["dimExt2X"].get<float>();
  if (o.contains("dimExt2Y"))
    a.dimExt2Y = o["dimExt2Y"].get<float>();
  if (o.contains("dimSignedOffset"))
    a.dimSignedOffset = o["dimSignedOffset"].get<float>();
  if (a.kind == CadAnnotation::Kind::DimLinear && o.contains("dimLinearVertical") && o["dimLinearVertical"].is_boolean())
    a.dimLinearVertical = o["dimLinearVertical"].get<bool>();
  if (o.contains("surveyPointLabelFor"))
    a.surveyPointLabelFor = o["surveyPointLabelFor"].get<int>();
  return a;
}

void CadArcToJson(const CadArc& a, json& o) {
  o["cx"] = a.cx;
  o["cy"] = a.cy;
  o["r"] = a.r;
  o["startRad"] = a.startRad;
  o["sweepRad"] = a.sweepRad;
}

CadArc CadArcFromJson(const json& o) {
  CadArc a;
  if (o.contains("cx"))
    a.cx = o["cx"].get<float>();
  if (o.contains("cy"))
    a.cy = o["cy"].get<float>();
  if (o.contains("r"))
    a.r = o["r"].get<float>();
  if (o.contains("startRad"))
    a.startRad = o["startRad"].get<float>();
  if (o.contains("sweepRad"))
    a.sweepRad = o["sweepRad"].get<float>();
  return a;
}

void CadEllipseToJson(const CadEllipse& e, json& o) {
  o["cx"] = e.cx;
  o["cy"] = e.cy;
  o["majVx"] = e.majVx;
  o["majVy"] = e.majVy;
  o["ratio"] = e.ratio;
}

CadEllipse CadEllipseFromJson(const json& o) {
  CadEllipse e;
  if (o.contains("cx"))
    e.cx = o["cx"].get<float>();
  if (o.contains("cy"))
    e.cy = o["cy"].get<float>();
  if (o.contains("majVx"))
    e.majVx = o["majVx"].get<float>();
  if (o.contains("majVy"))
    e.majVy = o["majVy"].get<float>();
  if (o.contains("ratio"))
    e.ratio = o["ratio"].get<float>();
  return e;
}

void CreatePointsOptionsToJson(const CreatePointsOptions& c, json& o) {
  o["startNumber"] = c.startNumber;
  o["sequentialNumbering"] = c.sequentialNumbering;
  o["pointNumberOffset"] = c.pointNumberOffset;
  o["sequenceNumbersFrom"] = c.sequenceNumbersFrom;
  o["layer"] = c.layer;
  o["defaultDescription"] = c.defaultDescription;
  o["defaultElevation"] = c.defaultElevation;
  o["duplicatePolicy"] = static_cast<int>(c.duplicatePolicy);
}

CreatePointsOptions CreatePointsOptionsFromJson(const json& o) {
  CreatePointsOptions c{};
  if (o.contains("startNumber"))
    c.startNumber = o["startNumber"].get<int>();
  if (o.contains("sequentialNumbering"))
    c.sequentialNumbering = o["sequentialNumbering"].get<bool>();
  if (o.contains("pointNumberOffset"))
    c.pointNumberOffset = o["pointNumberOffset"].get<int>();
  if (o.contains("sequenceNumbersFrom"))
    c.sequenceNumbersFrom = o["sequenceNumbersFrom"].get<int>();
  if (o.contains("layer") && o["layer"].is_string())
    c.layer = o["layer"].get<std::string>();
  if (o.contains("defaultDescription") && o["defaultDescription"].is_string())
    c.defaultDescription = o["defaultDescription"].get<std::string>();
  if (o.contains("defaultElevation"))
    c.defaultElevation = o["defaultElevation"].get<float>();
  if (o.contains("duplicatePolicy")) {
    const int p = o["duplicatePolicy"].get<int>();
    if (p >= 0 && p <= static_cast<int>(SurveyDuplicatePolicy::Overwrite))
      c.duplicatePolicy = static_cast<SurveyDuplicatePolicy>(p);
  }
  return c;
}

void SurveyLabelTemplatesToJson(const SurveyLabelStyleTemplates& t, json& o) {
  o["numberDesc"] = t.numberDesc;
  o["numberOnly"] = t.numberOnly;
  o["descOnly"] = t.descOnly;
  o["numberElev"] = t.numberElev;
  o["numberElevDesc"] = t.numberElevDesc;
}

SurveyLabelStyleTemplates SurveyLabelTemplatesFromJson(const json& o) {
  SurveyLabelStyleTemplates t;
  if (o.contains("numberDesc") && o["numberDesc"].is_string())
    t.numberDesc = o["numberDesc"].get<std::string>();
  if (o.contains("numberOnly") && o["numberOnly"].is_string())
    t.numberOnly = o["numberOnly"].get<std::string>();
  if (o.contains("descOnly") && o["descOnly"].is_string())
    t.descOnly = o["descOnly"].get<std::string>();
  if (o.contains("numberElev") && o["numberElev"].is_string())
    t.numberElev = o["numberElev"].get<std::string>();
  if (o.contains("numberElevDesc") && o["numberElevDesc"].is_string())
    t.numberElevDesc = o["numberElevDesc"].get<std::string>();
  return t;
}

json BuildRoot(const AppCommandState& st) {
  json root;
  root["format"] = "gosurvey";
  root["version"] = kGsFormatVersion;

  json doc;
  doc["worldDocumentOriginX"] = st.worldDocumentOriginX;
  doc["worldDocumentOriginY"] = st.worldDocumentOriginY;
  doc["modelUnitsPerPlottedInch"] = st.modelUnitsPerPlottedInch;
  doc["defaultPlottedTextHeightInches"] = st.defaultPlottedTextHeightInches;
  doc["currentLayer"] = st.currentLayer;
  doc["lineVerts"] = st.userLinesFlat;
  json lineAttrs = json::array();
  for (const auto& a : st.userLineAttrs) {
    json o;
    EntityAttributesToJson(a, o);
    lineAttrs.push_back(std::move(o));
  }
  doc["lineAttrs"] = std::move(lineAttrs);
  doc["circles"] = st.userCirclesCxCyR;
  json circleAttrs = json::array();
  for (const auto& a : st.userCircleAttrs) {
    json o;
    EntityAttributesToJson(a, o);
    circleAttrs.push_back(std::move(o));
  }
  doc["circleAttrs"] = std::move(circleAttrs);

  json arcs = json::array();
  for (const auto& a : st.userArcs) {
    json o;
    CadArcToJson(a, o);
    arcs.push_back(std::move(o));
  }
  doc["arcs"] = std::move(arcs);
  json arcAttrs = json::array();
  for (const auto& a : st.userArcAttrs) {
    json o;
    EntityAttributesToJson(a, o);
    arcAttrs.push_back(std::move(o));
  }
  doc["arcAttrs"] = std::move(arcAttrs);

  json ells = json::array();
  for (const auto& e : st.userEllipses) {
    json o;
    CadEllipseToJson(e, o);
    ells.push_back(std::move(o));
  }
  doc["ellipses"] = std::move(ells);
  json ellAttrs = json::array();
  for (const auto& a : st.userEllAttrs) {
    json o;
    EntityAttributesToJson(a, o);
    ellAttrs.push_back(std::move(o));
  }
  doc["ellAttrs"] = std::move(ellAttrs);

  doc["polylineOffsets"] = st.userPolylineOffsets;
  doc["polylineVerts"] = st.userPolylineVerts;
  json polyClosed = json::array();
  for (uint8_t c : st.userPolylineClosed)
    polyClosed.push_back(static_cast<int>(c));
  doc["polylineClosed"] = std::move(polyClosed);
  json polyAttrs = json::array();
  for (const auto& a : st.userPolylineAttrs) {
    json o;
    EntityAttributesToJson(a, o);
    polyAttrs.push_back(std::move(o));
  }
  doc["polylineAttrs"] = std::move(polyAttrs);

  json anns = json::array();
  for (const auto& a : st.cadAnnotations) {
    json o;
    CadAnnotationToJson(a, o);
    anns.push_back(std::move(o));
  }
  doc["annotations"] = std::move(anns);
  json annAttrs = json::array();
  for (const auto& a : st.cadAnnotationAttrs) {
    json o;
    EntityAttributesToJson(a, o);
    annAttrs.push_back(std::move(o));
  }
  doc["annotationAttrs"] = std::move(annAttrs);

  json layers = json::array();
  for (const auto& r : st.drawingLayerTable) {
    json o;
    CadLayerRowToJson(r, o);
    layers.push_back(std::move(o));
  }
  doc["layers"] = std::move(layers);

  json survey = json::array();
  for (const auto& p : st.surveyPoints) {
    json o;
    o["id"] = p.id;
    o["easting"] = p.easting;
    o["northing"] = p.northing;
    o["elevation"] = p.elevation;
    o["description"] = p.description;
    o["layer"] = p.layer;
    o["labelStyle"] = static_cast<int>(p.labelStyle);
    o["labelMtextAnnIndex"] = p.labelMtextAnnIndex;
    survey.push_back(std::move(o));
  }
  doc["surveyPoints"] = std::move(survey);
  doc["createPointsNextId"] = st.createPointsNextId;
  json cpo;
  CreatePointsOptionsToJson(st.createPointsOpts, cpo);
  doc["createPointsOptions"] = std::move(cpo);

  root["document"] = std::move(doc);

  json settings;
  settings["surveyPointCrossSpanPlottedInches"] = st.surveyPointCrossSpanPlottedInches;
  settings["surveyPointShowIdInViewport"] = st.surveyPointShowIdInViewport;
  settings["surveyPointLabelPlottedHeightInches"] = st.surveyPointLabelPlottedHeightInches;
  settings["surveyLabelOffsetEastPlottedIn"] = st.surveyLabelOffsetEastPlottedIn;
  settings["surveyLabelOffsetNorthPlottedIn"] = st.surveyLabelOffsetNorthPlottedIn;
  settings["surveyLabelBoxWidthPlottedIn"] = st.surveyLabelBoxWidthPlottedIn;
  settings["surveyLabelBoxHeightPlottedIn"] = st.surveyLabelBoxHeightPlottedIn;
  json tpl;
  SurveyLabelTemplatesToJson(st.surveyLabelTemplates, tpl);
  settings["surveyLabelTemplates"] = std::move(tpl);

  settings["viewportCrosshairR"] = st.viewportCrosshairR;
  settings["viewportCrosshairG"] = st.viewportCrosshairG;
  settings["viewportCrosshairB"] = st.viewportCrosshairB;
  settings["viewportCrosshairArmFracX"] = st.viewportCrosshairArmFracX;
  settings["viewportCrosshairArmFracY"] = st.viewportCrosshairArmFracY;
  settings["viewportCrosshairPickHalfPxX"] = st.viewportCrosshairPickHalfPxX;
  settings["viewportCrosshairPickHalfPxY"] = st.viewportCrosshairPickHalfPxY;
  settings["viewportCrosshairHairPx"] = st.viewportCrosshairHairPx;

  settings["viewportTextMinPx"] = st.viewportTextMinPx;
  settings["viewportTextMaxPx"] = st.viewportTextMaxPx;
  settings["viewportMtextMinPx"] = st.viewportMtextMinPx;
  settings["viewportMtextMaxPx"] = st.viewportMtextMaxPx;

  settings["viewportDimExtLinePx"] = st.viewportDimExtLinePx;
  settings["viewportDimDimLinePx"] = st.viewportDimDimLinePx;
  settings["viewportDimArrowScale"] = st.viewportDimArrowScale;
  settings["viewportDimTextMinPx"] = st.viewportDimTextMinPx;
  settings["viewportDimTextMaxPx"] = st.viewportDimTextMaxPx;

  settings["objectSnapEnabled"] = st.objectSnapEnabled;
  settings["objectSnapEndpoint"] = st.objectSnapEndpoint;
  settings["objectSnapMidpoint"] = st.objectSnapMidpoint;
  settings["objectSnapCenter"] = st.objectSnapCenter;
  settings["objectSnapPerpendicular"] = st.objectSnapPerpendicular;
  settings["objectSnapSurveyPoint"] = st.objectSnapSurveyPoint;
  settings["objectSnapGeometricCenter"] = st.objectSnapGeometricCenter;
  settings["objectSnapAperturePx"] = st.objectSnapAperturePx;
  settings["objectSnapGlyphHalfPx"] = st.objectSnapGlyphHalfPx;

  root["settings"] = std::move(settings);
  return root;
}

bool ValidateDocumentJson(const json& doc, std::vector<std::string>& log) {
  if (!doc.contains("lineVerts") || !doc["lineVerts"].is_array()) {
    log.push_back(".gs: missing document.lineVerts array.");
    return false;
  }
  const auto& lv = doc["lineVerts"];
  if (lv.size() % 6 != 0) {
    log.push_back(".gs: lineVerts length must be a multiple of 6.");
    return false;
  }
  const size_t nLineSeg = lv.size() / 6;
  if (!doc.contains("lineAttrs") || !doc["lineAttrs"].is_array() || doc["lineAttrs"].size() != nLineSeg) {
    log.push_back(".gs: lineAttrs count must match line segment count.");
    return false;
  }
  if (!doc.contains("circles") || !doc["circles"].is_array() || doc["circles"].size() % 3 != 0) {
    log.push_back(".gs: circles array length must be a multiple of 3.");
    return false;
  }
  const size_t nCirc = doc["circles"].size() / 3;
  if (!doc.contains("circleAttrs") || !doc["circleAttrs"].is_array() || doc["circleAttrs"].size() != nCirc) {
    log.push_back(".gs: circleAttrs count must match circles.");
    return false;
  }
  if (!doc.contains("arcs") || !doc["arcs"].is_array()) {
    log.push_back(".gs: missing arcs array.");
    return false;
  }
  const size_t nArc = doc["arcs"].size();
  if (!doc.contains("arcAttrs") || !doc["arcAttrs"].is_array() || doc["arcAttrs"].size() != nArc) {
    log.push_back(".gs: arcAttrs count must match arcs.");
    return false;
  }
  if (!doc.contains("ellipses") || !doc["ellipses"].is_array()) {
    log.push_back(".gs: missing ellipses array.");
    return false;
  }
  const size_t nEll = doc["ellipses"].size();
  if (!doc.contains("ellAttrs") || !doc["ellAttrs"].is_array() || doc["ellAttrs"].size() != nEll) {
    log.push_back(".gs: ellAttrs count must match ellipses.");
    return false;
  }
  if (!doc.contains("polylineOffsets") || !doc["polylineOffsets"].is_array()) {
    log.push_back(".gs: missing polylineOffsets.");
    return false;
  }
  if (!doc.contains("polylineVerts") || !doc["polylineVerts"].is_array()) {
    log.push_back(".gs: missing polylineVerts.");
    return false;
  }
  const auto& po = doc["polylineOffsets"];
  const auto& pv = doc["polylineVerts"];
  if (pv.size() % 3 != 0) {
    log.push_back(".gs: polylineVerts length must be a multiple of 3.");
    return false;
  }
  // Zero polylines: empty offset table (matches save when there are no polylines).
  if (po.empty()) {
    if (!pv.empty()) {
      log.push_back(".gs: polylineVerts must be empty when polylineOffsets is empty.");
      return false;
    }
  } else if (po.size() == 1) {
    log.push_back(".gs: polylineOffsets invalid (expected empty or at least two entries).");
    return false;
  } else {
    for (size_t i = 0; i < po.size(); ++i) {
      if (!po[i].is_number_integer()) {
        log.push_back(".gs: polylineOffsets must be integers.");
        return false;
      }
    }
    for (size_t i = 1; i < po.size(); ++i) {
      if (po[i].get<int>() < po[i - 1].get<int>()) {
        log.push_back(".gs: polylineOffsets must be non-decreasing.");
        return false;
      }
    }
    const int lastOff = po.back().get<int>();
    if (lastOff < 0 || static_cast<size_t>(lastOff) * 3 > pv.size()) {
      log.push_back(".gs: polylineVerts too short for polylineOffsets.");
      return false;
    }
  }
  const size_t nPoly = po.size() >= 2 ? po.size() - 1 : 0;
  if (!doc.contains("polylineClosed") || !doc["polylineClosed"].is_array() ||
      doc["polylineClosed"].size() != nPoly) {
    log.push_back(".gs: polylineClosed length must match polyline count.");
    return false;
  }
  if (!doc.contains("polylineAttrs") || !doc["polylineAttrs"].is_array() ||
      doc["polylineAttrs"].size() != nPoly) {
    log.push_back(".gs: polylineAttrs length must match polyline count.");
    return false;
  }
  if (!doc.contains("annotations") || !doc["annotations"].is_array()) {
    log.push_back(".gs: missing annotations array.");
    return false;
  }
  const size_t nAnn = doc["annotations"].size();
  if (!doc.contains("annotationAttrs") || !doc["annotationAttrs"].is_array() ||
      doc["annotationAttrs"].size() != nAnn) {
    log.push_back(".gs: annotationAttrs count must match annotations.");
    return false;
  }
  return true;
}

void ApplySettingsFromJson(AppCommandState& st, const json& s) {
  if (!s.is_object())
    return;
  auto num = [](const json& j, const char* k, float* out) {
    if (j.contains(k) && j[k].is_number())
      *out = j[k].get<float>();
  };
  auto b = [](const json& j, const char* k, bool* out) {
    if (j.contains(k) && j[k].is_boolean())
      *out = j[k].get<bool>();
  };

  num(s, "surveyPointCrossSpanPlottedInches", &st.surveyPointCrossSpanPlottedInches);
  b(s, "surveyPointShowIdInViewport", &st.surveyPointShowIdInViewport);
  num(s, "surveyPointLabelPlottedHeightInches", &st.surveyPointLabelPlottedHeightInches);
  num(s, "surveyLabelOffsetEastPlottedIn", &st.surveyLabelOffsetEastPlottedIn);
  num(s, "surveyLabelOffsetNorthPlottedIn", &st.surveyLabelOffsetNorthPlottedIn);
  num(s, "surveyLabelBoxWidthPlottedIn", &st.surveyLabelBoxWidthPlottedIn);
  num(s, "surveyLabelBoxHeightPlottedIn", &st.surveyLabelBoxHeightPlottedIn);
  if (s.contains("surveyLabelTemplates") && s["surveyLabelTemplates"].is_object())
    st.surveyLabelTemplates = SurveyLabelTemplatesFromJson(s["surveyLabelTemplates"]);

  num(s, "viewportCrosshairR", &st.viewportCrosshairR);
  num(s, "viewportCrosshairG", &st.viewportCrosshairG);
  num(s, "viewportCrosshairB", &st.viewportCrosshairB);
  num(s, "viewportCrosshairArmFracX", &st.viewportCrosshairArmFracX);
  num(s, "viewportCrosshairArmFracY", &st.viewportCrosshairArmFracY);
  num(s, "viewportCrosshairPickHalfPxX", &st.viewportCrosshairPickHalfPxX);
  num(s, "viewportCrosshairPickHalfPxY", &st.viewportCrosshairPickHalfPxY);
  num(s, "viewportCrosshairHairPx", &st.viewportCrosshairHairPx);

  num(s, "viewportTextMinPx", &st.viewportTextMinPx);
  num(s, "viewportTextMaxPx", &st.viewportTextMaxPx);
  num(s, "viewportMtextMinPx", &st.viewportMtextMinPx);
  num(s, "viewportMtextMaxPx", &st.viewportMtextMaxPx);

  num(s, "viewportDimExtLinePx", &st.viewportDimExtLinePx);
  num(s, "viewportDimDimLinePx", &st.viewportDimDimLinePx);
  num(s, "viewportDimArrowScale", &st.viewportDimArrowScale);
  num(s, "viewportDimTextMinPx", &st.viewportDimTextMinPx);
  num(s, "viewportDimTextMaxPx", &st.viewportDimTextMaxPx);

  b(s, "objectSnapEnabled", &st.objectSnapEnabled);
  b(s, "objectSnapEndpoint", &st.objectSnapEndpoint);
  b(s, "objectSnapMidpoint", &st.objectSnapMidpoint);
  b(s, "objectSnapCenter", &st.objectSnapCenter);
  b(s, "objectSnapPerpendicular", &st.objectSnapPerpendicular);
  b(s, "objectSnapSurveyPoint", &st.objectSnapSurveyPoint);
  b(s, "objectSnapGeometricCenter", &st.objectSnapGeometricCenter);
  num(s, "objectSnapAperturePx", &st.objectSnapAperturePx);
  num(s, "objectSnapGlyphHalfPx", &st.objectSnapGlyphHalfPx);
  st.objectSnapAperturePx = std::clamp(st.objectSnapAperturePx, 4.f, 64.f);
  st.objectSnapGlyphHalfPx = std::clamp(st.objectSnapGlyphHalfPx, 3.f, 48.f);
}

void ApplyDocumentFromJson(AppCommandState& st, const json& doc, std::vector<std::string>& log) {
  st.worldDocumentOriginX = doc.value("worldDocumentOriginX", 0.0);
  st.worldDocumentOriginY = doc.value("worldDocumentOriginY", 0.0);
  st.modelUnitsPerPlottedInch = doc.value("modelUnitsPerPlottedInch", 50.f);
  st.defaultPlottedTextHeightInches = doc.value("defaultPlottedTextHeightInches", 0.125f);
  if (doc.contains("currentLayer") && doc["currentLayer"].is_string())
    st.currentLayer = doc["currentLayer"].get<std::string>();
  else
    st.currentLayer = "0";

  st.userLinesFlat.clear();
  for (const auto& v : doc["lineVerts"])
    st.userLinesFlat.push_back(v.get<float>());
  st.userLineAttrs.clear();
  for (const auto& o : doc["lineAttrs"])
    st.userLineAttrs.push_back(EntityAttributesFromJson(o));

  st.userCirclesCxCyR.clear();
  for (const auto& v : doc["circles"])
    st.userCirclesCxCyR.push_back(v.get<float>());
  st.userCircleAttrs.clear();
  for (const auto& o : doc["circleAttrs"])
    st.userCircleAttrs.push_back(EntityAttributesFromJson(o));

  st.userArcs.clear();
  for (const auto& o : doc["arcs"])
    st.userArcs.push_back(CadArcFromJson(o));
  st.userArcAttrs.clear();
  for (const auto& o : doc["arcAttrs"])
    st.userArcAttrs.push_back(EntityAttributesFromJson(o));

  st.userEllipses.clear();
  for (const auto& o : doc["ellipses"])
    st.userEllipses.push_back(CadEllipseFromJson(o));
  st.userEllAttrs.clear();
  for (const auto& o : doc["ellAttrs"])
    st.userEllAttrs.push_back(EntityAttributesFromJson(o));

  st.userPolylineOffsets.clear();
  for (const auto& v : doc["polylineOffsets"])
    st.userPolylineOffsets.push_back(v.get<int>());
  st.userPolylineVerts.clear();
  for (const auto& v : doc["polylineVerts"])
    st.userPolylineVerts.push_back(v.get<float>());
  st.userPolylineClosed.clear();
  for (const auto& v : doc["polylineClosed"])
    st.userPolylineClosed.push_back(static_cast<uint8_t>(std::clamp(v.get<int>(), 0, 1)));
  st.userPolylineAttrs.clear();
  for (const auto& o : doc["polylineAttrs"])
    st.userPolylineAttrs.push_back(EntityAttributesFromJson(o));

  st.cadAnnotations.clear();
  for (const auto& o : doc["annotations"])
    st.cadAnnotations.push_back(CadAnnotationFromJson(o));
  st.cadAnnotationAttrs.clear();
  for (const auto& o : doc["annotationAttrs"])
    st.cadAnnotationAttrs.push_back(EntityAttributesFromJson(o));

  st.drawingLayerTable.clear();
  if (doc.contains("layers") && doc["layers"].is_array()) {
    for (const auto& o : doc["layers"])
      st.drawingLayerTable.push_back(CadLayerRowFromJson(o));
  }

  st.surveyPoints.clear();
  if (doc.contains("surveyPoints") && doc["surveyPoints"].is_array()) {
    for (const auto& o : doc["surveyPoints"]) {
      SurveyPoint p;
      p.id = o.value("id", 0);
      p.easting = o.value("easting", 0.f);
      p.northing = o.value("northing", 0.f);
      p.elevation = o.value("elevation", 0.f);
      if (o.contains("description") && o["description"].is_string())
        p.description = o["description"].get<std::string>();
      if (o.contains("layer") && o["layer"].is_string())
        p.layer = o["layer"].get<std::string>();
      const int ls = o.value("labelStyle", static_cast<int>(SurveyPointLabelStyle::NumberDesc));
      if (ls >= 0 && ls <= static_cast<int>(SurveyPointLabelStyle::NumberElevDesc))
        p.labelStyle = static_cast<SurveyPointLabelStyle>(ls);
      p.labelMtextAnnIndex = o.value("labelMtextAnnIndex", -1);
      st.surveyPoints.push_back(std::move(p));
    }
  }

  st.createPointsNextId = doc.value("createPointsNextId", 1);
  if (doc.contains("createPointsOptions") && doc["createPointsOptions"].is_object())
    st.createPointsOpts = CreatePointsOptionsFromJson(doc["createPointsOptions"]);
  else
    st.createPointsOpts = CreatePointsOptions{};

  CadCoord::MaybeRebaseLargeCoordinates(st, &log);

  for (size_t i = 0; i < st.surveyPoints.size(); ++i) {
    int& li = st.surveyPoints[i].labelMtextAnnIndex;
    if (li >= 0 && static_cast<size_t>(li) < st.cadAnnotations.size()) {
      CadAnnotation& a = st.cadAnnotations[static_cast<size_t>(li)];
      if (a.kind != CadAnnotation::Kind::Mtext || a.surveyPointLabelFor != static_cast<int>(i))
        li = -1;
    } else
      li = -1;
  }
  for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
    CadAnnotation& a = st.cadAnnotations[ai];
    const int sp = a.surveyPointLabelFor;
    if (sp >= 0 && static_cast<size_t>(sp) < st.surveyPoints.size()) {
      if (st.surveyPoints[static_cast<size_t>(sp)].labelMtextAnnIndex != static_cast<int>(ai))
        a.surveyPointLabelFor = -1;
    } else if (sp >= 0)
      a.surveyPointLabelFor = -1;
  }
}

} // namespace

bool SaveGoSurveyFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (!pathUtf8 || !pathUtf8[0]) {
    log.push_back("Save .gs: empty path.");
    return false;
  }
  try {
    const json root = BuildRoot(st);
    std::ofstream f(std::filesystem::path(pathUtf8), std::ios::binary);
    if (!f) {
      log.push_back(std::string("Could not open for write: ") + pathUtf8);
      return false;
    }
    f << root.dump(2);
    log.push_back(std::string("Saved GoSurvey workspace (.gs): ") + pathUtf8);
    return true;
  } catch (const std::exception& e) {
    log.push_back(std::string("Save .gs failed: ") + e.what());
    return false;
  }
}

bool LoadGoSurveyFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (!pathUtf8 || !pathUtf8[0]) {
    log.push_back("Open .gs: empty path.");
    return false;
  }
  std::ifstream f(std::filesystem::path(pathUtf8), std::ios::binary);
  if (!f) {
    log.push_back(std::string("Could not open: ") + pathUtf8);
    return false;
  }
  json root;
  try {
    f >> root;
  } catch (const std::exception& e) {
    log.push_back(std::string("Parse .gs failed: ") + e.what());
    return false;
  }
  if (!root.is_object() || !root.contains("format") || root["format"] != "gosurvey") {
    log.push_back(".gs: not a GoSurvey file (missing format \"gosurvey\").");
    return false;
  }
  if (!root.contains("version") || !root["version"].is_number_integer() ||
      root["version"].get<int>() != kGsFormatVersion) {
    log.push_back(".gs: unsupported version (expected " + std::to_string(kGsFormatVersion) + ").");
    return false;
  }
  if (!root.contains("document") || !root["document"].is_object()) {
    log.push_back(".gs: missing document object.");
    return false;
  }
  const json& doc = root["document"];
  if (!ValidateDocumentJson(doc, log))
    return false;

  try {
    ResetCadToolStateToIdle(st);
    CloseMtextRichEditorUi(st);
    st.mtextRichEditorBuf.clear();
    ClearCadGeometry(st);
    st.surveyPoints.clear();
    st.surveyPointIdBuffers.clear();
    st.selectedSurveyPointIndices.clear();
    st.drawingLayerTable.clear();

    ApplyDocumentFromJson(st, doc, log);

    if (root.contains("settings") && root["settings"].is_object())
      ApplySettingsFromJson(st, root["settings"]);

    EnsureAttrCounts(st);
    SyncDrawingLayerTableWithGeometry(st);
    RepositionAllSurveyPointLabels(st);
    for (size_t i = 0; i < st.surveyPoints.size(); ++i) {
      if (st.surveyPoints[i].labelStyle != SurveyPointLabelStyle::None &&
          st.surveyPoints[i].labelMtextAnnIndex < 0)
        EnsureSurveyPointLabelMtext(st, i, &log);
    }
    RepositionAllSurveyPointLabels(st);
    BumpCadGpuCache(st);
    log.push_back(std::string("Opened GoSurvey workspace (.gs): ") + pathUtf8);
    return true;
  } catch (const std::exception& e) {
    log.push_back(std::string("Load .gs failed: ") + e.what());
    return false;
  }
}
