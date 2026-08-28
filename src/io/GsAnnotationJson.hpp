#pragma once

#include <cstdint>
#include <string>

#include "CadEntities.hpp"

#include <nlohmann/json.hpp>

// Annotation JSON used by Save/LoadGoSurveyFile. Header-only so the DIMANGULAR kind+vertex
// round-trip (issue #125) is unit-testable without linking GsIo.cpp (command layer).

[[nodiscard]] inline const char* AnnotationKindTag(CadAnnotation::Kind k) {
  switch (k) {
  case CadAnnotation::Kind::Text:
    return "text";
  case CadAnnotation::Kind::Mtext:
    return "mtext";
  case CadAnnotation::Kind::DimAligned:
    return "dim";
  case CadAnnotation::Kind::DimLinear:
    return "dimlinear";
  case CadAnnotation::Kind::DimAngular:
    return "dimangular";
  }
  return "text";
}

[[nodiscard]] inline CadAnnotation::Kind AnnotationKindFromString(const std::string& s) {
  if (s == "mtext")
    return CadAnnotation::Kind::Mtext;
  if (s == "dim")
    return CadAnnotation::Kind::DimAligned;
  if (s == "dimlinear")
    return CadAnnotation::Kind::DimLinear;
  if (s == "dimangular")
    return CadAnnotation::Kind::DimAngular;
  return CadAnnotation::Kind::Text;
}

inline void CadAnnotationToJson(const CadAnnotation& a, nlohmann::json& o) {
  o["kind"] = AnnotationKindTag(a.kind);
  o["insX"] = a.insX;
  o["insY"] = a.insY;
  if (a.insZ != 0.f)
    o["insZ"] = a.insZ;
  o["plottedHeightInches"] = a.plottedHeightInches;
  o["rotationRad"] = a.rotationRad;
  o["text"] = a.text;
  o["boxMinX"] = a.boxMinX;
  o["boxMinY"] = a.boxMinY;
  o["boxMaxX"] = a.boxMaxX;
  o["boxMaxY"] = a.boxMaxY;
  if (a.kind == CadAnnotation::Kind::Mtext && a.mtextAttach != 1)
    o["mtextAttach"] = a.mtextAttach;
  if (!a.fontFamily.empty())
    o["fontFamily"] = a.fontFamily;
  if (a.bold)
    o["bold"] = true;
  if (a.italic)
    o["italic"] = true;
  if (a.underline)
    o["underline"] = true;
  if (!a.styleName.empty())
    o["styleName"] = a.styleName;
  if (a.obliqueDeg != 0.f)
    o["obliqueDeg"] = a.obliqueDeg;
  if (a.ovFont)
    o["ovFont"] = true;
  if (a.ovHeight)
    o["ovHeight"] = true;
  if (a.ovOblique)
    o["ovOblique"] = true;
  if (a.ovBold)
    o["ovBold"] = true;
  if (a.ovItalic)
    o["ovItalic"] = true;
  o["dimExt1X"] = a.dimExt1X;
  o["dimExt1Y"] = a.dimExt1Y;
  o["dimExt2X"] = a.dimExt2X;
  o["dimExt2Y"] = a.dimExt2Y;
  o["dimAngVertexX"] = a.dimAngVertexX;
  o["dimAngVertexY"] = a.dimAngVertexY;
  o["dimSignedOffset"] = a.dimSignedOffset;
  if (a.kind == CadAnnotation::Kind::DimLinear)
    o["dimLinearVertical"] = a.dimLinearVertical;
  o["surveyPointLabelForId"] = a.surveyPointLabelForId;
  if (a.surveyLabelHasUserOffset) {
    o["surveyLabelHasUserOffset"] = true;
    o["surveyLabelUserOffsetEast"] = a.surveyLabelUserOffsetEast;
    o["surveyLabelUserOffsetNorth"] = a.surveyLabelUserOffsetNorth;
  }
}

inline CadAnnotation CadAnnotationFromJson(const nlohmann::json& o) {
  CadAnnotation a;
  if (o.contains("kind") && o["kind"].is_string())
    a.kind = AnnotationKindFromString(o["kind"].get<std::string>());
  a.insX = o.value("insX", a.insX);
  a.insY = o.value("insY", a.insY);
  a.insZ = o.value("insZ", a.insZ);
  a.plottedHeightInches = o.value("plottedHeightInches", a.plottedHeightInches);
  a.rotationRad = o.value("rotationRad", a.rotationRad);
  a.text = o.value("text", a.text);
  a.boxMinX = o.value("boxMinX", a.boxMinX);
  a.boxMinY = o.value("boxMinY", a.boxMinY);
  a.boxMaxX = o.value("boxMaxX", a.boxMaxX);
  a.boxMaxY = o.value("boxMaxY", a.boxMaxY);
  a.mtextAttach = o.value("mtextAttach", a.mtextAttach);
  a.fontFamily = o.value("fontFamily", a.fontFamily);
  a.bold = o.value("bold", a.bold);
  a.italic = o.value("italic", a.italic);
  a.underline = o.value("underline", a.underline);
  a.styleName = o.value("styleName", a.styleName);
  a.obliqueDeg = o.value("obliqueDeg", a.obliqueDeg);
  a.ovFont = o.value("ovFont", a.ovFont);
  a.ovHeight = o.value("ovHeight", a.ovHeight);
  a.ovOblique = o.value("ovOblique", a.ovOblique);
  a.ovBold = o.value("ovBold", a.ovBold);
  a.ovItalic = o.value("ovItalic", a.ovItalic);
  a.dimExt1X = o.value("dimExt1X", a.dimExt1X);
  a.dimExt1Y = o.value("dimExt1Y", a.dimExt1Y);
  a.dimExt2X = o.value("dimExt2X", a.dimExt2X);
  a.dimExt2Y = o.value("dimExt2Y", a.dimExt2Y);
  a.dimAngVertexX = o.value("dimAngVertexX", a.dimAngVertexX);
  a.dimAngVertexY = o.value("dimAngVertexY", a.dimAngVertexY);
  a.dimSignedOffset = o.value("dimSignedOffset", a.dimSignedOffset);
  if (a.kind == CadAnnotation::Kind::DimLinear)
    a.dimLinearVertical = o.value("dimLinearVertical", a.dimLinearVertical);
  a.surveyPointLabelForId = o.contains("surveyPointLabelForId")
                                ? o.value("surveyPointLabelForId", -1)
                                : o.value("surveyPointLabelFor", -1);
  a.surveyLabelHasUserOffset = o.value("surveyLabelHasUserOffset", a.surveyLabelHasUserOffset);
  a.surveyLabelUserOffsetEast = o.value("surveyLabelUserOffsetEast", a.surveyLabelUserOffsetEast);
  a.surveyLabelUserOffsetNorth = o.value("surveyLabelUserOffsetNorth", a.surveyLabelUserOffsetNorth);
  return a;
}
