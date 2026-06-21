#include "GsIo.hpp"

#include "CadCommands.hpp"
#include "TextStyle.hpp"
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
  e.layer        = o.value("layer",        e.layer);
  e.color        = o.value("color",        e.color);
  e.linetype     = o.value("linetype",     e.linetype);
  e.lineweightMm = o.value("lineweightMm", e.lineweightMm);
  e.transparency = o.value("transparency", e.transparency);
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
  o["plottable"] = r.plottable;
}

CadLayerRow CadLayerRowFromJson(const json& o) {
  CadLayerRow r;
  r.name         = o.value("name",         r.name);
  r.on           = o.value("on",           r.on);
  r.frozen       = o.value("frozen",       r.frozen);
  r.locked       = o.value("locked",       r.locked);
  r.color        = o.value("color",        r.color);
  r.linetype     = o.value("linetype",     r.linetype);
  r.lineweightMm = o.value("lineweightMm", r.lineweightMm);
  r.transparency = o.value("transparency", r.transparency);
  r.plottable    = o.value("plottable",    r.plottable);
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
  if (a.kind == CadAnnotation::Kind::Mtext && a.mtextAttach != 1)
    o["mtextAttach"] = a.mtextAttach;
  if (!a.fontFamily.empty()) o["fontFamily"] = a.fontFamily;
  if (a.bold)      o["bold"] = true;
  if (a.italic)    o["italic"] = true;
  if (a.underline) o["underline"] = true;
  // Text style reference + overrides (REQ-044). Written only when set so older readers/files are unaffected.
  if (!a.styleName.empty()) o["styleName"] = a.styleName;
  if (a.obliqueDeg != 0.f)  o["obliqueDeg"] = a.obliqueDeg;
  if (a.ovFont)    o["ovFont"] = true;
  if (a.ovHeight)  o["ovHeight"] = true;
  if (a.ovOblique) o["ovOblique"] = true;
  if (a.ovBold)    o["ovBold"] = true;
  if (a.ovItalic)  o["ovItalic"] = true;
  o["dimExt1X"] = a.dimExt1X;
  o["dimExt1Y"] = a.dimExt1Y;
  o["dimExt2X"] = a.dimExt2X;
  o["dimExt2Y"] = a.dimExt2Y;
  o["dimSignedOffset"] = a.dimSignedOffset;
  if (a.kind == CadAnnotation::Kind::DimLinear)
    o["dimLinearVertical"] = a.dimLinearVertical;
  o["surveyPointLabelFor"] = a.surveyPointLabelFor;
  if (a.surveyLabelHasUserOffset) {
    o["surveyLabelHasUserOffset"] = true;
    o["surveyLabelUserOffsetEast"] = a.surveyLabelUserOffsetEast;
    o["surveyLabelUserOffsetNorth"] = a.surveyLabelUserOffsetNorth;
  }
}

CadAnnotation CadAnnotationFromJson(const json& o) {
  CadAnnotation a;
  if (o.contains("kind") && o["kind"].is_string())
    a.kind = AnnotationKindFromString(o["kind"].get<std::string>());
  a.insX               = o.value("insX",               a.insX);
  a.insY               = o.value("insY",               a.insY);
  a.plottedHeightInches = o.value("plottedHeightInches", a.plottedHeightInches);
  a.rotationRad        = o.value("rotationRad",        a.rotationRad);
  a.text               = o.value("text",               a.text);
  a.boxMinX            = o.value("boxMinX",            a.boxMinX);
  a.boxMinY            = o.value("boxMinY",            a.boxMinY);
  a.boxMaxX            = o.value("boxMaxX",            a.boxMaxX);
  a.boxMaxY            = o.value("boxMaxY",            a.boxMaxY);
  a.mtextAttach        = o.value("mtextAttach",        a.mtextAttach);
  a.fontFamily         = o.value("fontFamily",         a.fontFamily);
  a.bold               = o.value("bold",               a.bold);
  a.italic             = o.value("italic",             a.italic);
  a.underline          = o.value("underline",          a.underline);
  a.styleName          = o.value("styleName",          a.styleName);
  a.obliqueDeg         = o.value("obliqueDeg",         a.obliqueDeg);
  a.ovFont             = o.value("ovFont",             a.ovFont);
  a.ovHeight           = o.value("ovHeight",           a.ovHeight);
  a.ovOblique          = o.value("ovOblique",          a.ovOblique);
  a.ovBold             = o.value("ovBold",             a.ovBold);
  a.ovItalic           = o.value("ovItalic",           a.ovItalic);
  a.dimExt1X           = o.value("dimExt1X",           a.dimExt1X);
  a.dimExt1Y           = o.value("dimExt1Y",           a.dimExt1Y);
  a.dimExt2X           = o.value("dimExt2X",           a.dimExt2X);
  a.dimExt2Y           = o.value("dimExt2Y",           a.dimExt2Y);
  a.dimSignedOffset    = o.value("dimSignedOffset",    a.dimSignedOffset);
  if (a.kind == CadAnnotation::Kind::DimLinear)
    a.dimLinearVertical = o.value("dimLinearVertical", a.dimLinearVertical);
  a.surveyPointLabelFor = o.value("surveyPointLabelFor", a.surveyPointLabelFor);
  a.surveyLabelHasUserOffset    = o.value("surveyLabelHasUserOffset",    a.surveyLabelHasUserOffset);
  a.surveyLabelUserOffsetEast   = o.value("surveyLabelUserOffsetEast",   a.surveyLabelUserOffsetEast);
  a.surveyLabelUserOffsetNorth  = o.value("surveyLabelUserOffsetNorth",  a.surveyLabelUserOffsetNorth);
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
  a.cx       = o.value("cx",       a.cx);
  a.cy       = o.value("cy",       a.cy);
  a.r        = o.value("r",        a.r);
  a.startRad = o.value("startRad", a.startRad);
  a.sweepRad = o.value("sweepRad", a.sweepRad);
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
  e.cx    = o.value("cx",    e.cx);
  e.cy    = o.value("cy",    e.cy);
  e.majVx = o.value("majVx", e.majVx);
  e.majVy = o.value("majVy", e.majVy);
  e.ratio = o.value("ratio", e.ratio);
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
  c.startNumber        = o.value("startNumber",        c.startNumber);
  c.sequentialNumbering = o.value("sequentialNumbering", c.sequentialNumbering);
  c.pointNumberOffset  = o.value("pointNumberOffset",  c.pointNumberOffset);
  c.sequenceNumbersFrom = o.value("sequenceNumbersFrom", c.sequenceNumbersFrom);
  c.layer              = o.value("layer",              c.layer);
  c.defaultDescription = o.value("defaultDescription", c.defaultDescription);
  c.defaultElevation   = o.value("defaultElevation",   c.defaultElevation);
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
  o["numberNorthEast"] = t.numberNorthEast;
  o["northEast"] = t.northEast;
  o["numberNorthEastElev"] = t.numberNorthEastElev;
}

SurveyLabelStyleTemplates SurveyLabelTemplatesFromJson(const json& o) {
  SurveyLabelStyleTemplates t;
  t.numberDesc         = o.value("numberDesc",         t.numberDesc);
  t.numberOnly         = o.value("numberOnly",         t.numberOnly);
  t.descOnly           = o.value("descOnly",           t.descOnly);
  t.numberElev         = o.value("numberElev",         t.numberElev);
  t.numberElevDesc     = o.value("numberElevDesc",     t.numberElevDesc);
  t.numberNorthEast    = o.value("numberNorthEast",    t.numberNorthEast);
  t.northEast          = o.value("northEast",          t.northEast);
  t.numberNorthEastElev = o.value("numberNorthEastElev", t.numberNorthEastElev);
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
  doc["drawingInsUnits"] = st.drawingInsUnits;
  doc["defaultPlottedTextHeightInches"] = st.defaultPlottedTextHeightInches;
  doc["currentLayer"] = st.currentLayer;
  // Named text styles (REQ-044). Additive — older readers ignore it; no kGsFormatVersion bump.
  doc["activeTextStyleName"] = st.activeTextStyleName;
  {
    json styles = json::array();
    for (const TextStyle& s : st.textStyles) {
      json o;
      o["name"] = s.name;
      if (!s.fontFamily.empty()) o["fontFamily"] = s.fontFamily;
      o["heightInches"] = s.heightInches;
      if (s.obliqueDeg != 0.f) o["obliqueDeg"] = s.obliqueDeg;
      if (s.bold)   o["bold"] = true;
      if (s.italic) o["italic"] = true;
      styles.push_back(std::move(o));
    }
    doc["textStyles"] = std::move(styles);
  }
  // Paper space layouts (REQ-031). Viewports/frozen layers persist in a later increment.
  {
    json layouts = json::array();
    for (const PaperLayout& l : st.paperLayouts) {
      json o;
      o["name"] = l.name;
      o["portraitWidthIn"] = l.portraitWidthIn;
      o["portraitHeightIn"] = l.portraitHeightIn;
      o["landscape"] = l.landscape;
      o["presetIdx"] = l.presetIdx;
      o["pageSetupName"] = l.pageSetupName;
      o["fitToPaper"] = l.fitToPaper;
      o["scaleModelPerPaperIn"] = l.scaleModelPerPaperIn;
      o["plotArea"] = l.plotArea;
      o["offsetXIn"] = l.offsetXIn;
      o["offsetYIn"] = l.offsetYIn;
      o["centerPlot"] = l.centerPlot;
      o["viewPanX"] = l.viewPanX;
      o["viewPanY"] = l.viewPanY;
      o["viewZoom"] = l.viewZoom;
      o["viewInit"] = l.viewInit;
      json vps = json::array();
      for (const Viewport& v : l.viewports) {
        json vo;
        vo["paperXIn"] = v.paperXIn;
        vo["paperYIn"] = v.paperYIn;
        vo["paperWIn"] = v.paperWIn;
        vo["paperHIn"] = v.paperHIn;
        vo["modelCenterX"] = v.modelCenterX;
        vo["modelCenterY"] = v.modelCenterY;
        vo["scaleModelPerPaperIn"] = v.scaleModelPerPaperIn;
        vo["layer"] = v.layer;
        vo["frozenLayers"] = v.frozenLayers;
        vps.push_back(vo);
      }
      o["viewports"] = vps;
      // Native paper-space geometry (REQ-037): lines + text in paper inches, owned by the layout.
      o["paperLines"] = l.paperLines;  // flat x0,y0,z0,x1,y1,z1 per segment
      {
        json plAttrs = json::array();
        for (const EntityAttributes& a : l.paperLineAttrs) {
          json ao;
          EntityAttributesToJson(a, ao);
          plAttrs.push_back(ao);
        }
        o["paperLineAttrs"] = std::move(plAttrs);
        json pTexts = json::array();
        for (const CadAnnotation& a : l.paperTexts) {
          json ao;
          CadAnnotationToJson(a, ao);
          pTexts.push_back(ao);
        }
        o["paperTexts"] = std::move(pTexts);
        json ptAttrs = json::array();
        for (const EntityAttributes& a : l.paperTextAttrs) {
          json ao;
          EntityAttributesToJson(a, ao);
          ptAttrs.push_back(ao);
        }
        o["paperTextAttrs"] = std::move(ptAttrs);
      }
      // Full paper-space primitive store (REQ-038, ADR-013): circles/arcs/ellipses/polylines, paper inches.
      {
        auto attrsToJson = [](const std::vector<EntityAttributes>& src) {
          json arr = json::array();
          for (const EntityAttributes& a : src) {
            json ao;
            EntityAttributesToJson(a, ao);
            arr.push_back(ao);
          }
          return arr;
        };
        o["paperCircles"] = l.paperCircles;  // flat cx,cy,r triples
        o["paperCircleAttrs"] = attrsToJson(l.paperCircleAttrs);
        json arcs = json::array();
        for (const CadArc& a : l.paperArcs) {
          json ao;
          CadArcToJson(a, ao);
          arcs.push_back(ao);
        }
        o["paperArcs"] = std::move(arcs);
        o["paperArcAttrs"] = attrsToJson(l.paperArcAttrs);
        json ells = json::array();
        for (const CadEllipse& e : l.paperEllipses) {
          json eo;
          CadEllipseToJson(e, eo);
          ells.push_back(eo);
        }
        o["paperEllipses"] = std::move(ells);
        o["paperEllAttrs"] = attrsToJson(l.paperEllAttrs);
        o["paperPolyOffsets"] = l.paperPolyOffsets;
        o["paperPolyVerts"] = l.paperPolyVerts;
        json pc = json::array();
        for (uint8_t c : l.paperPolyClosed)
          pc.push_back(static_cast<int>(c));
        o["paperPolyClosed"] = std::move(pc);
        o["paperPolyAttrs"] = attrsToJson(l.paperPolyAttrs);
        json pfills = json::array();
        for (const CadFilledRegion& fr : l.paperFilledRegions) {
          json fo;
          fo["verts"] = fr.verts;
          fo["loops"] = fr.loopStart;
          pfills.push_back(std::move(fo));
        }
        o["paperFilledRegions"] = std::move(pfills);
        o["paperFilledRegionAttrs"] = attrsToJson(l.paperFilledRegionAttrs);
      }
      layouts.push_back(o);
    }
    doc["paperLayouts"] = layouts;
    doc["activeSpaceIndex"] = st.activeSpaceIndex;
    json setups = json::array();
    for (const PageSetup& ps : st.savedPageSetups) {
      json o;
      o["name"] = ps.name;
      o["presetIdx"] = ps.presetIdx;
      o["portraitWidthIn"] = ps.portraitWidthIn;
      o["portraitHeightIn"] = ps.portraitHeightIn;
      o["landscape"] = ps.landscape;
      o["fitToPaper"] = ps.fitToPaper;
      o["scaleModelPerPaperIn"] = ps.scaleModelPerPaperIn;
      o["plotArea"] = ps.plotArea;
      o["offsetXIn"] = ps.offsetXIn;
      o["offsetYIn"] = ps.offsetYIn;
      o["centerPlot"] = ps.centerPlot;
      setups.push_back(o);
    }
    doc["savedPageSetups"] = setups;
  }
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

  // Filled regions (ADR-011): each is {verts:[x,y,…], loops:[startPairIdx,…]} + a parallel attribute object.
  json fills = json::array();
  for (const auto& fr : st.cadFilledRegions) {
    json o;
    o["verts"] = fr.verts;
    o["loops"] = fr.loopStart;
    if (!fr.patternName.empty()) {  // omit for solid fills so legacy files stay byte-identical (ADR-018)
      o["pattern"] = fr.patternName;
      o["patAngle"] = fr.patternAngleDeg;
      o["patScale"] = fr.patternScale;
    }
    fills.push_back(std::move(o));
  }
  doc["filledRegions"] = std::move(fills);
  json fillAttrs = json::array();
  for (const auto& a : st.cadFilledRegionAttrs) {
    json o;
    EntityAttributesToJson(a, o);
    fillAttrs.push_back(std::move(o));
  }
  doc["filledRegionAttrs"] = std::move(fillAttrs);

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
  settings["surveyLabelLeaderArrowPx"] = st.surveyLabelLeaderArrowPx;
  json tpl;
  SurveyLabelTemplatesToJson(st.surveyLabelTemplates, tpl);
  settings["surveyLabelTemplates"] = std::move(tpl);

  settings["viewportCrosshairR"] = st.viewportCrosshairR;
  settings["viewportCrosshairG"] = st.viewportCrosshairG;
  settings["viewportCrosshairB"] = st.viewportCrosshairB;
  settings["viewportBgR"] = st.viewportBgR;
  settings["viewportBgG"] = st.viewportBgG;
  settings["viewportBgB"] = st.viewportBgB;
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
  num(s, "surveyLabelLeaderArrowPx", &st.surveyLabelLeaderArrowPx);
  if (s.contains("surveyLabelTemplates") && s["surveyLabelTemplates"].is_object())
    st.surveyLabelTemplates = SurveyLabelTemplatesFromJson(s["surveyLabelTemplates"]);

  num(s, "viewportCrosshairR", &st.viewportCrosshairR);
  num(s, "viewportCrosshairG", &st.viewportCrosshairG);
  num(s, "viewportCrosshairB", &st.viewportCrosshairB);
  num(s, "viewportBgR", &st.viewportBgR);
  num(s, "viewportBgG", &st.viewportBgG);
  num(s, "viewportBgB", &st.viewportBgB);
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
  st.drawingInsUnits = doc.value("drawingInsUnits", 2);
  // Paper space layouts (REQ-031). Missing/garbage → no layouts, model space (no crash).
  st.paperLayouts.clear();
  if (doc.contains("paperLayouts") && doc["paperLayouts"].is_array()) {
    for (const auto& o : doc["paperLayouts"]) {
      if (!o.is_object())
        continue;
      PaperLayout l;
      if (o.contains("name") && o["name"].is_string())
        l.name = o["name"].get<std::string>();
      l.portraitWidthIn = o.value("portraitWidthIn", l.portraitWidthIn);
      l.portraitHeightIn = o.value("portraitHeightIn", l.portraitHeightIn);
      l.landscape = o.value("landscape", l.landscape);
      l.presetIdx = o.value("presetIdx", l.presetIdx);
      if (o.contains("pageSetupName") && o["pageSetupName"].is_string())
        l.pageSetupName = o["pageSetupName"].get<std::string>();
      l.fitToPaper = o.value("fitToPaper", l.fitToPaper);
      l.scaleModelPerPaperIn = o.value("scaleModelPerPaperIn", l.scaleModelPerPaperIn);
      l.plotArea = o.value("plotArea", l.plotArea);
      l.offsetXIn = o.value("offsetXIn", l.offsetXIn);
      l.offsetYIn = o.value("offsetYIn", l.offsetYIn);
      l.centerPlot = o.value("centerPlot", l.centerPlot);
      l.viewPanX = o.value("viewPanX", l.viewPanX);
      l.viewPanY = o.value("viewPanY", l.viewPanY);
      l.viewZoom = o.value("viewZoom", l.viewZoom);
      l.viewInit = o.value("viewInit", l.viewInit);
      if (o.contains("viewports") && o["viewports"].is_array()) {
        for (const auto& vo : o["viewports"]) {
          if (!vo.is_object())
            continue;
          Viewport v;
          v.paperXIn = vo.value("paperXIn", v.paperXIn);
          v.paperYIn = vo.value("paperYIn", v.paperYIn);
          v.paperWIn = vo.value("paperWIn", v.paperWIn);
          v.paperHIn = vo.value("paperHIn", v.paperHIn);
          v.modelCenterX = vo.value("modelCenterX", v.modelCenterX);
          v.modelCenterY = vo.value("modelCenterY", v.modelCenterY);
          v.scaleModelPerPaperIn = vo.value("scaleModelPerPaperIn", v.scaleModelPerPaperIn);
          if (vo.contains("layer") && vo["layer"].is_string())
            v.layer = vo["layer"].get<std::string>();
          if (vo.contains("frozenLayers") && vo["frozenLayers"].is_array()) {
            for (const auto& fl : vo["frozenLayers"]) {
              if (fl.is_string())
                v.frozenLayers.push_back(fl.get<std::string>());
            }
          }
          l.viewports.push_back(v);
        }
      }
      // Native paper-space geometry (REQ-037). Missing/garbage → empty (no crash); attrs padded to match.
      if (o.contains("paperLines") && o["paperLines"].is_array()) {
        for (const auto& f : o["paperLines"]) {
          if (f.is_number())
            l.paperLines.push_back(f.get<float>());
        }
        if (l.paperLines.size() % 6 != 0)  // drop a trailing partial segment
          l.paperLines.resize(l.paperLines.size() - (l.paperLines.size() % 6));
      }
      if (o.contains("paperLineAttrs") && o["paperLineAttrs"].is_array()) {
        for (const auto& ao : o["paperLineAttrs"])
          if (ao.is_object())
            l.paperLineAttrs.push_back(EntityAttributesFromJson(ao));
      }
      l.paperLineAttrs.resize(l.paperLines.size() / 6);  // keep parallel (default-fill or trim)
      if (o.contains("paperTexts") && o["paperTexts"].is_array()) {
        for (const auto& ao : o["paperTexts"])
          if (ao.is_object())
            l.paperTexts.push_back(CadAnnotationFromJson(ao));
      }
      if (o.contains("paperTextAttrs") && o["paperTextAttrs"].is_array()) {
        for (const auto& ao : o["paperTextAttrs"])
          if (ao.is_object())
            l.paperTextAttrs.push_back(EntityAttributesFromJson(ao));
      }
      l.paperTextAttrs.resize(l.paperTexts.size());  // keep parallel
      // Full paper-space primitive store (REQ-038, ADR-013). Missing → empty; attrs padded to stay parallel.
      {
        auto readAttrs = [&](const char* key, std::vector<EntityAttributes>& dst) {
          if (o.contains(key) && o[key].is_array())
            for (const auto& ao : o[key])
              if (ao.is_object())
                dst.push_back(EntityAttributesFromJson(ao));
        };
        if (o.contains("paperCircles") && o["paperCircles"].is_array()) {
          for (const auto& f : o["paperCircles"])
            if (f.is_number())
              l.paperCircles.push_back(f.get<float>());
          if (l.paperCircles.size() % 3 != 0)
            l.paperCircles.resize(l.paperCircles.size() - (l.paperCircles.size() % 3));
        }
        readAttrs("paperCircleAttrs", l.paperCircleAttrs);
        l.paperCircleAttrs.resize(l.paperCircles.size() / 3);
        if (o.contains("paperArcs") && o["paperArcs"].is_array())
          for (const auto& ao : o["paperArcs"])
            if (ao.is_object())
              l.paperArcs.push_back(CadArcFromJson(ao));
        readAttrs("paperArcAttrs", l.paperArcAttrs);
        l.paperArcAttrs.resize(l.paperArcs.size());
        if (o.contains("paperEllipses") && o["paperEllipses"].is_array())
          for (const auto& eo : o["paperEllipses"])
            if (eo.is_object())
              l.paperEllipses.push_back(CadEllipseFromJson(eo));
        readAttrs("paperEllAttrs", l.paperEllAttrs);
        l.paperEllAttrs.resize(l.paperEllipses.size());
        if (o.contains("paperPolyOffsets") && o["paperPolyOffsets"].is_array())
          for (const auto& v : o["paperPolyOffsets"])
            if (v.is_number())
              l.paperPolyOffsets.push_back(v.get<int>());
        if (o.contains("paperPolyVerts") && o["paperPolyVerts"].is_array())
          for (const auto& v : o["paperPolyVerts"])
            if (v.is_number())
              l.paperPolyVerts.push_back(v.get<float>());
        if (o.contains("paperPolyClosed") && o["paperPolyClosed"].is_array())
          for (const auto& v : o["paperPolyClosed"])
            if (v.is_number())
              l.paperPolyClosed.push_back(static_cast<uint8_t>(std::clamp(v.get<int>(), 0, 1)));
        // Drop a malformed offset table (must start at 0 and be monotonic) rather than risk OOB on render.
        const int nPoly = static_cast<int>(l.paperPolyOffsets.size()) - 1;
        if (nPoly < 0 || (!l.paperPolyOffsets.empty() && l.paperPolyOffsets.front() != 0) ||
            (nPoly >= 0 && !l.paperPolyOffsets.empty() &&
             static_cast<size_t>(l.paperPolyOffsets.back()) * 3 != l.paperPolyVerts.size())) {
          l.paperPolyOffsets.clear();
          l.paperPolyVerts.clear();
          l.paperPolyClosed.clear();
          l.paperPolyAttrs.clear();
        } else {
          readAttrs("paperPolyAttrs", l.paperPolyAttrs);
          l.paperPolyClosed.resize(static_cast<size_t>(std::max(0, nPoly)));
          l.paperPolyAttrs.resize(static_cast<size_t>(std::max(0, nPoly)));
        }
        if (o.contains("paperFilledRegions") && o["paperFilledRegions"].is_array())
          for (const auto& el : o["paperFilledRegions"])
            if (el.is_object()) {
              CadFilledRegion fr;
              if (el.contains("verts"))
                for (const auto& v : el["verts"])
                  fr.verts.push_back(v.get<float>());
              if (el.contains("loops"))
                for (const auto& v : el["loops"])
                  fr.loopStart.push_back(v.get<int>());
              if (fr.loopStart.empty() && fr.verts.size() >= 6)
                fr.loopStart.push_back(0);
              l.paperFilledRegions.push_back(std::move(fr));
            }
        readAttrs("paperFilledRegionAttrs", l.paperFilledRegionAttrs);
        l.paperFilledRegionAttrs.resize(l.paperFilledRegions.size());
      }
      st.paperLayouts.push_back(l);
    }
  }
  {
    int asi = doc.value("activeSpaceIndex", kModelSpaceIndex);
    if (asi < 0 || asi >= static_cast<int>(st.paperLayouts.size()))
      asi = kModelSpaceIndex;
    st.activeSpaceIndex = asi;
    st.lastPaperLayoutIndex =
        st.paperLayouts.empty() ? 0 : std::max(0, asi < 0 ? 0 : asi);
  }
  st.savedPageSetups.clear();
  if (doc.contains("savedPageSetups") && doc["savedPageSetups"].is_array()) {
    for (const auto& o : doc["savedPageSetups"]) {
      if (!o.is_object())
        continue;
      PageSetup ps;
      if (o.contains("name") && o["name"].is_string())
        ps.name = o["name"].get<std::string>();
      ps.presetIdx = o.value("presetIdx", ps.presetIdx);
      ps.portraitWidthIn = o.value("portraitWidthIn", ps.portraitWidthIn);
      ps.portraitHeightIn = o.value("portraitHeightIn", ps.portraitHeightIn);
      ps.landscape = o.value("landscape", ps.landscape);
      ps.fitToPaper = o.value("fitToPaper", ps.fitToPaper);
      ps.scaleModelPerPaperIn = o.value("scaleModelPerPaperIn", ps.scaleModelPerPaperIn);
      ps.plotArea = o.value("plotArea", ps.plotArea);
      ps.offsetXIn = o.value("offsetXIn", ps.offsetXIn);
      ps.offsetYIn = o.value("offsetYIn", ps.offsetYIn);
      ps.centerPlot = o.value("centerPlot", ps.centerPlot);
      st.savedPageSetups.push_back(ps);
    }
  }
  st.defaultPlottedTextHeightInches = doc.value("defaultPlottedTextHeightInches", 0.125f);
  if (doc.contains("currentLayer") && doc["currentLayer"].is_string())
    st.currentLayer = doc["currentLayer"].get<std::string>();
  else
    st.currentLayer = "0";

  // Named text styles (REQ-044). Read tolerantly: a missing table (older .gs) synthesizes "Standard",
  // so existing text — which carries no styleName — renders from its own fields, unchanged.
  const bool hadTextStyles =
      doc.contains("textStyles") && doc["textStyles"].is_array() && !doc["textStyles"].empty();
  st.textStyles.clear();
  if (hadTextStyles) {
    for (const auto& o : doc["textStyles"]) {
      TextStyle s;
      s.name         = o.value("name", std::string());
      s.fontFamily   = o.value("fontFamily", std::string());
      s.heightInches = o.value("heightInches", 0.125f);
      s.obliqueDeg   = o.value("obliqueDeg", 0.f);
      s.bold         = o.value("bold", false);
      s.italic       = o.value("italic", false);
      if (!s.name.empty()) st.textStyles.push_back(std::move(s));
    }
  }
  TextStyles::EnsureStandard(st.textStyles);
  st.activeTextStyleName = doc.value("activeTextStyleName", std::string(TextStyles::kStandardName));
  if (!TextStyles::Find(st.textStyles, st.activeTextStyleName))
    st.activeTextStyleName = TextStyles::kStandardName;
  // Keep the active style's height and the new-text default height consistent. For an older file (no
  // table) the synthesized "Standard" inherits the file's default so new-text height is unchanged; for a
  // file that carries styles the active style drives the new-text height.
  if (!hadTextStyles) {
    if (TextStyle* standard = TextStyles::Find(st.textStyles, TextStyles::kStandardName))
      standard->heightInches = std::max(st.defaultPlottedTextHeightInches, 1.e-6f);
  } else if (const TextStyle* active = TextStyles::Find(st.textStyles, st.activeTextStyleName)) {
    st.defaultPlottedTextHeightInches = std::max(active->heightInches, 1.e-6f);
  }

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

  // Filled regions (ADR-011) — guarded with contains() so older .gs files load unchanged.
  st.cadFilledRegions.clear();
  if (doc.contains("filledRegions") && doc["filledRegions"].is_array()) {
    for (const auto& el : doc["filledRegions"]) {
      CadFilledRegion fr;
      // Current form: {verts, loops}. Legacy form (pre-multi-loop): a bare flat vertex array = one loop.
      if (el.is_object()) {
        if (el.contains("verts"))
          for (const auto& v : el["verts"])
            fr.verts.push_back(v.get<float>());
        if (el.contains("loops"))
          for (const auto& v : el["loops"])
            fr.loopStart.push_back(v.get<int>());
        if (el.contains("pattern"))  // absent → solid (ADR-018; legacy fills read back as SOLID)
          fr.patternName = el["pattern"].get<std::string>();
        if (el.contains("patAngle"))
          fr.patternAngleDeg = el["patAngle"].get<float>();
        if (el.contains("patScale"))
          fr.patternScale = el["patScale"].get<float>();
      } else if (el.is_array()) {
        for (const auto& v : el)
          fr.verts.push_back(v.get<float>());
      }
      if (fr.loopStart.empty() && fr.verts.size() >= 6)
        fr.loopStart.push_back(0);
      st.cadFilledRegions.push_back(std::move(fr));
    }
  }
  st.cadFilledRegionAttrs.clear();
  if (doc.contains("filledRegionAttrs") && doc["filledRegionAttrs"].is_array()) {
    for (const auto& o : doc["filledRegionAttrs"])
      st.cadFilledRegionAttrs.push_back(EntityAttributesFromJson(o));
  }

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
      if (ls >= 0 && ls <= static_cast<int>(SurveyPointLabelStyle::NumberNorthEastElev))
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
    st.textStyles.clear();

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
