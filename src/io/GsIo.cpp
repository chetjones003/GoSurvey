#include "GsIo.hpp"
#include "GsAnnotationJson.hpp"

#include "CadCommands.hpp"
#include "GsMigrate.hpp"
#include "TextStyle.hpp"
#include "DimensionStyle.hpp"
#include "CadCoordinateFrame.hpp"
#include "SurveyPoints.hpp"
#include "util/meshgeom.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace {

// kGsFormatVersion now lives in GsMigrate.hpp — it is the migration target, and the updater needs
// to read it without pulling in this translation unit.

using nlohmann::json;

void EntityAttributesToJson(const EntityAttributes& e, json& o) {
  o["id"] = e.id;  // REQ-076 stable identity; additive, no format-version bump (ADR-020 (d))
  o["layer"] = e.layer;
  o["color"] = e.color;
  o["linetype"] = e.linetype;
  o["lineweightMm"] = e.lineweightMm;
  o["transparency"] = e.transparency;
}

EntityAttributes EntityAttributesFromJson(const json& o) {
  EntityAttributes e;
  // A file written before REQ-076 has no "id", so every entity loads as 0 and EnsureEntityIds
  // assigns in its fixed array order — which is why loading the same legacy file twice yields the
  // same ids, as REQ-076's acceptance requires.
  e.id           = o.value("id",           static_cast<std::uint64_t>(0));
  e.layer        = o.value("layer",        e.layer);
  e.color        = o.value("color",        e.color);
  e.linetype     = o.value("linetype",     e.linetype);
  e.lineweightMm = o.value("lineweightMm", e.lineweightMm);
  e.transparency = o.value("transparency", e.transparency);
  return e;
}

void CadBlockXformToJson(const CadBlockXform& xf, json& o) {
  o["x"] = xf.x;
  o["y"] = xf.y;
  o["z"] = xf.z;
  o["sx"] = xf.sx;
  o["sy"] = xf.sy;
  o["sz"] = xf.sz;
  o["rotX"] = xf.rotX;
  o["rotY"] = xf.rotY;
  o["rotZ"] = xf.rotZ;
}

CadBlockXform CadBlockXformFromJson(const json& o) {
  CadBlockXform xf;
  xf.x = o.value("x", xf.x);
  xf.y = o.value("y", xf.y);
  xf.z = o.value("z", xf.z);
  xf.sx = o.value("sx", xf.sx);
  xf.sy = o.value("sy", xf.sy);
  xf.sz = o.value("sz", xf.sz);
  xf.rotX = o.value("rotX", xf.rotX);
  xf.rotY = o.value("rotY", xf.rotY);
  xf.rotZ = o.value("rotZ", xf.rotZ);
  return xf;
}

// Defined further down (with the main-drawing entity IO); forward-declared so block
// content can round-trip the same geometry kinds.
void CadArcToJson(const CadArc& a, json& o);
CadArc CadArcFromJson(const json& o);
void CadEllipseToJson(const CadEllipse& e, json& o);
CadEllipse CadEllipseFromJson(const json& o);

json CadMeshToJson(const CadMesh& m) {
  json o;
  o["verts"] = m.vertsXyz;
  o["normals"] = m.normalsXyz;
  o["indices"] = m.indices;
  if (!m.sourceName.empty())
    o["source"] = m.sourceName;
  json parts = json::array();
  for (const CadMeshPart& p : m.parts) {
    json jp;
    jp["name"] = p.name;
    jp["begin"] = p.indexBegin;
    jp["count"] = p.indexCount;
    jp["rgb"] = json::array({p.r, p.g, p.b});
    parts.push_back(std::move(jp));
  }
  o["parts"] = std::move(parts);
  return o;
}

std::shared_ptr<const CadMesh> CadMeshFromJson(const json& o) {
  if (!o.is_object())
    return nullptr;
  auto m = std::make_shared<CadMesh>();
  if (o.contains("verts"))
    m->vertsXyz = o["verts"].get<std::vector<float>>();
  if (o.contains("normals"))
    m->normalsXyz = o["normals"].get<std::vector<float>>();
  if (o.contains("indices"))
    m->indices = o["indices"].get<std::vector<std::uint32_t>>();
  if (o.contains("source"))
    m->sourceName = o["source"].get<std::string>();
  if (o.contains("parts") && o["parts"].is_array()) {
    for (const auto& jp : o["parts"]) {
      CadMeshPart p;
      if (jp.contains("name"))
        p.name = jp["name"].get<std::string>();
      if (jp.contains("begin"))
        p.indexBegin = jp["begin"].get<int>();
      if (jp.contains("count"))
        p.indexCount = jp["count"].get<int>();
      if (jp.contains("rgb") && jp["rgb"].is_array() && jp["rgb"].size() == 3) {
        p.r = jp["rgb"][0].get<float>();
        p.g = jp["rgb"][1].get<float>();
        p.b = jp["rgb"][2].get<float>();
      }
      m->parts.push_back(std::move(p));
    }
  }
  if (m->parts.empty() && !m->indices.empty()) {
    CadMeshPart p;
    p.indexBegin = 0;
    p.indexCount = static_cast<int>(m->indices.size());
    m->parts.push_back(std::move(p));
  }
  return m;
}

void EntityAttrArrayToJson(const std::vector<EntityAttributes>& v, json& out) {
  out = json::array();
  for (const auto& a : v) {
    json e;
    EntityAttributesToJson(a, e);
    out.push_back(std::move(e));
  }
}

void EntityAttrArrayFromJson(const json& o, const char* key, std::vector<EntityAttributes>& v) {
  if (o.contains(key) && o[key].is_array())
    for (const auto& e : o[key])
      v.push_back(EntityAttributesFromJson(e));
}

json CadBlockContentToJson(const CadBlockContent& c) {
  json o;
  o["lines"] = c.lines;
  json la = json::array();
  for (const auto& a : c.lineAttrs) {
    json e;
    EntityAttributesToJson(a, e);
    la.push_back(std::move(e));
  }
  o["lineAttrs"] = std::move(la);
  o["lineVis"] = c.lineVis;
  o["circles"] = c.circles;
  json ca = json::array();
  for (const auto& a : c.circleAttrs) {
    json e;
    EntityAttributesToJson(a, e);
    ca.push_back(std::move(e));
  }
  o["circleAttrs"] = std::move(ca);
  o["circleVis"] = c.circleVis;

  json arcs = json::array();
  for (const CadArc& a : c.arcs) {
    json aj;
    CadArcToJson(a, aj);
    arcs.push_back(std::move(aj));
  }
  o["arcs"] = std::move(arcs);
  json arcAttrs;
  EntityAttrArrayToJson(c.arcAttrs, arcAttrs);
  o["arcAttrs"] = std::move(arcAttrs);

  json ellipses = json::array();
  for (const CadEllipse& e : c.ellipses) {
    json ej;
    CadEllipseToJson(e, ej);
    ellipses.push_back(std::move(ej));
  }
  o["ellipses"] = std::move(ellipses);
  json ellAttrs;
  EntityAttrArrayToJson(c.ellAttrs, ellAttrs);
  o["ellAttrs"] = std::move(ellAttrs);

  o["polyOffsets"] = c.polyOffsets;
  o["polyVerts"] = c.polyVerts;
  json polyClosed = json::array();
  for (std::uint8_t v : c.polyClosed)
    polyClosed.push_back(static_cast<int>(v));
  o["polyClosed"] = std::move(polyClosed);
  json polyAttrs;
  EntityAttrArrayToJson(c.polyAttrs, polyAttrs);
  o["polyAttrs"] = std::move(polyAttrs);

  json meshes = json::array();
  for (const auto& mp : c.meshes) {
    if (mp)
      meshes.push_back(CadMeshToJson(*mp));
  }
  o["meshes"] = std::move(meshes);
  json meshAttrs;
  EntityAttrArrayToJson(c.meshAttrs, meshAttrs);
  o["meshAttrs"] = std::move(meshAttrs);

  json nested = json::array();
  for (const CadBlockNested& n : c.nested) {
    json nj;
    nj["defName"] = n.defName;
    json xf;
    CadBlockXformToJson(n.xf, xf);
    nj["xf"] = std::move(xf);
    nj["visState"] = n.visState;
    nested.push_back(std::move(nj));
  }
  o["nested"] = std::move(nested);
  json texts = json::array();
  for (const CadAnnotation& t : c.texts) {
    json tj;
    CadAnnotationToJson(t, tj);
    texts.push_back(std::move(tj));
  }
  o["texts"] = std::move(texts);
  json textAttrs;
  EntityAttrArrayToJson(c.textAttrs, textAttrs);
  o["textAttrs"] = std::move(textAttrs);
  return o;
}

CadBlockContent CadBlockContentFromJson(const json& o) {
  CadBlockContent c;
  if (o.contains("lines") && o["lines"].is_array())
    c.lines = o["lines"].get<std::vector<float>>();
  if (o.contains("lineAttrs") && o["lineAttrs"].is_array()) {
    for (const auto& e : o["lineAttrs"])
      c.lineAttrs.push_back(EntityAttributesFromJson(e));
  }
  if (o.contains("lineVis") && o["lineVis"].is_array())
    c.lineVis = o["lineVis"].get<std::vector<std::string>>();
  if (o.contains("circles") && o["circles"].is_array())
    c.circles = o["circles"].get<std::vector<float>>();
  EntityAttrArrayFromJson(o, "circleAttrs", c.circleAttrs);
  if (o.contains("circleVis") && o["circleVis"].is_array())
    c.circleVis = o["circleVis"].get<std::vector<std::string>>();

  if (o.contains("arcs") && o["arcs"].is_array())
    for (const auto& aj : o["arcs"])
      c.arcs.push_back(CadArcFromJson(aj));
  EntityAttrArrayFromJson(o, "arcAttrs", c.arcAttrs);

  if (o.contains("ellipses") && o["ellipses"].is_array())
    for (const auto& ej : o["ellipses"])
      c.ellipses.push_back(CadEllipseFromJson(ej));
  EntityAttrArrayFromJson(o, "ellAttrs", c.ellAttrs);

  if (o.contains("polyOffsets") && o["polyOffsets"].is_array())
    c.polyOffsets = o["polyOffsets"].get<std::vector<int>>();
  if (o.contains("polyVerts") && o["polyVerts"].is_array())
    c.polyVerts = o["polyVerts"].get<std::vector<float>>();
  if (o.contains("polyClosed") && o["polyClosed"].is_array())
    for (const auto& v : o["polyClosed"])
      c.polyClosed.push_back(static_cast<std::uint8_t>(std::clamp(v.get<int>(), 0, 1)));
  EntityAttrArrayFromJson(o, "polyAttrs", c.polyAttrs);

  if (o.contains("meshes") && o["meshes"].is_array())
    for (const auto& mj : o["meshes"]) {
      auto m = CadMeshFromJson(mj);
      if (m)
        c.meshes.push_back(std::move(m));
    }
  EntityAttrArrayFromJson(o, "meshAttrs", c.meshAttrs);

  if (o.contains("nested") && o["nested"].is_array()) {
    for (const auto& nj : o["nested"]) {
      CadBlockNested n;
      n.defName = nj.value("defName", "");
      if (nj.contains("xf"))
        n.xf = CadBlockXformFromJson(nj["xf"]);
      n.visState = nj.value("visState", "");
      c.nested.push_back(std::move(n));
    }
  }
  if (o.contains("texts") && o["texts"].is_array()) {
    for (const auto& t : o["texts"])
      c.texts.push_back(CadAnnotationFromJson(t));
  }
  EntityAttrArrayFromJson(o, "textAttrs", c.textAttrs);
  return c;
}

json CadBlockDefToJson(const CadBlockDefinition& d) {
  json o;
  o["id"] = d.id;
  o["name"] = d.name;
  o["description"] = d.description;
  o["baseX"] = d.baseX;
  o["baseY"] = d.baseY;
  o["baseZ"] = d.baseZ;
  o["units"] = d.units;
  o["metadata"] = d.metadata;
  o["content"] = CadBlockContentToJson(d.content);
  json ads = json::array();
  for (const CadBlockAttrDef& a : d.attrDefs) {
    json aj;
    aj["tag"] = a.tag;
    aj["prompt"] = a.prompt;
    aj["defaultValue"] = a.defaultValue;
    aj["localX"] = a.localX;
    aj["localY"] = a.localY;
    aj["localZ"] = a.localZ;
    aj["height"] = a.height;
    aj["rotationRad"] = a.rotationRad;
    ads.push_back(std::move(aj));
  }
  o["attrDefs"] = std::move(ads);
  json pars = json::array();
  for (const CadBlockParameter& p : d.parameters) {
    json pj;
    pj["name"] = p.name;
    pj["kind"] = static_cast<int>(p.kind);
    pj["value"] = p.value;
    pars.push_back(std::move(pj));
  }
  o["parameters"] = std::move(pars);
  json acts = json::array();
  for (const CadBlockAction& a : d.actions) {
    json aj;
    aj["kind"] = static_cast<int>(a.kind);
    aj["paramName"] = a.paramName;
    aj["originX"] = a.originX;
    aj["originY"] = a.originY;
    aj["dirX"] = a.dirX;
    aj["dirY"] = a.dirY;
    aj["threshold"] = a.threshold;
    aj["applyTo"] = a.applyTo;
    aj["labelGroup"] = a.labelGroup;
    acts.push_back(std::move(aj));
  }
  o["actions"] = std::move(acts);
  o["visibilityStates"] = d.visibilityStates;
  return o;
}

CadBlockDefinition CadBlockDefFromJson(const json& o) {
  CadBlockDefinition d;
  d.id = o.value("id", d.id);
  d.name = o.value("name", d.name);
  d.description = o.value("description", d.description);
  d.baseX = o.value("baseX", d.baseX);
  d.baseY = o.value("baseY", d.baseY);
  d.baseZ = o.value("baseZ", d.baseZ);
  d.units = o.value("units", d.units);
  d.metadata = o.value("metadata", d.metadata);
  if (o.contains("content"))
    d.content = CadBlockContentFromJson(o["content"]);
  if (o.contains("attrDefs") && o["attrDefs"].is_array()) {
    for (const auto& aj : o["attrDefs"]) {
      CadBlockAttrDef a;
      a.tag = aj.value("tag", "");
      a.prompt = aj.value("prompt", "");
      a.defaultValue = aj.value("defaultValue", "");
      a.localX = aj.value("localX", 0.f);
      a.localY = aj.value("localY", 0.f);
      a.localZ = aj.value("localZ", 0.f);
      a.height = aj.value("height", 0.125f);
      a.rotationRad = aj.value("rotationRad", 0.f);
      d.attrDefs.push_back(std::move(a));
    }
  }
  if (o.contains("parameters") && o["parameters"].is_array()) {
    for (const auto& pj : o["parameters"]) {
      CadBlockParameter p;
      p.name = pj.value("name", "");
      p.kind = static_cast<CadBlockParamKind>(pj.value("kind", 0));
      p.value = pj.value("value", 0.f);
      d.parameters.push_back(std::move(p));
    }
  }
  if (o.contains("actions") && o["actions"].is_array()) {
    for (const auto& aj : o["actions"]) {
      CadBlockAction a;
      a.kind = static_cast<CadBlockActionKind>(aj.value("kind", 0));
      a.paramName = aj.value("paramName", "");
      a.originX = aj.value("originX", 0.f);
      a.originY = aj.value("originY", 0.f);
      a.dirX = aj.value("dirX", 1.f);
      a.dirY = aj.value("dirY", 0.f);
      a.threshold = aj.value("threshold", 0.f);
      a.applyTo = aj.value("applyTo", "");
      a.labelGroup = aj.value("labelGroup", "");
      d.actions.push_back(std::move(a));
    }
  }
  if (o.contains("visibilityStates") && o["visibilityStates"].is_array())
    d.visibilityStates = o["visibilityStates"].get<std::vector<std::string>>();
  return d;
}

json CadBlockRefToJson(const CadBlockRef& r) {
  json o;
  o["defName"] = r.defName;
  json xf;
  CadBlockXformToJson(r.xf, xf);
  o["xf"] = std::move(xf);
  o["visState"] = r.visState;
  json av = json::array();
  for (const CadBlockAttrValue& v : r.attributes) {
    json e;
    e["tag"] = v.tag;
    e["value"] = v.value;
    av.push_back(std::move(e));
  }
  o["attributes"] = std::move(av);
  json ps = json::array();
  for (const CadBlockParameter& p : r.paramState) {
    json e;
    e["name"] = p.name;
    e["value"] = p.value;
    ps.push_back(std::move(e));
  }
  o["paramState"] = std::move(ps);
  return o;
}

CadBlockRef CadBlockRefFromJson(const json& o) {
  CadBlockRef r;
  r.defName = o.value("defName", "");
  if (o.contains("xf"))
    r.xf = CadBlockXformFromJson(o["xf"]);
  r.visState = o.value("visState", "");
  if (o.contains("attributes") && o["attributes"].is_array()) {
    for (const auto& e : o["attributes"])
      r.attributes.push_back(CadBlockAttrValue{e.value("tag", ""), e.value("value", "")});
  }
  if (o.contains("paramState") && o["paramState"].is_array()) {
    for (const auto& e : o["paramState"]) {
      CadBlockParameter p;
      p.name = e.value("name", "");
      p.value = e.value("value", 0.f);
      r.paramState.push_back(std::move(p));
    }
  }
  return r;
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

void CadArcToJson(const CadArc& a, json& o) {
  o["cx"] = a.cx;
  o["cy"] = a.cy;
  o["r"] = a.r;
  o["startRad"] = a.startRad;
  o["sweepRad"] = a.sweepRad;
  // Additive, and omitted when flat (REQ-057 / ADR-025): a drawing with no elevations still
  // serializes byte-identically to a pre-3D one, and older builds ignore the key.
  if (a.z != 0.f)
    o["z"] = a.z;
}

CadArc CadArcFromJson(const json& o) {
  CadArc a;
  a.cx       = o.value("cx",       a.cx);
  a.cy       = o.value("cy",       a.cy);
  a.r        = o.value("r",        a.r);
  a.startRad = o.value("startRad", a.startRad);
  a.sweepRad = o.value("sweepRad", a.sweepRad);
  a.z        = o.value("z",        a.z);  // absent → 0: legacy arcs load flat (REQ-057)
  return a;
}

void CadEllipseToJson(const CadEllipse& e, json& o) {
  o["cx"] = e.cx;
  o["cy"] = e.cy;
  o["majVx"] = e.majVx;
  o["majVy"] = e.majVy;
  o["ratio"] = e.ratio;
  if (e.z != 0.f)  // additive, omitted when flat — see CadArcToJson
    o["z"] = e.z;
}

CadEllipse CadEllipseFromJson(const json& o) {
  CadEllipse e;
  e.cx    = o.value("cx",    e.cx);
  e.cy    = o.value("cy",    e.cy);
  e.majVx = o.value("majVx", e.majVx);
  e.majVy = o.value("majVy", e.majVy);
  e.ratio = o.value("ratio", e.ratio);
  e.z     = o.value("z",     e.z);  // absent → 0: legacy ellipses load flat (REQ-057)
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
  // REQ-076: the id counter is saved so ids are not reused across a save/load, which is what makes a
  // stored reference safe over a file's whole life rather than only within one session.
  doc["nextEntityId"] = st.nextEntityId;
  // Named point groups (REQ-067). Additive section — a reader that does not know it ignores it, and
  // a file without it loads with no groups. Rules only: membership is resolved from the current
  // points on demand, so persisting a member list would only let it go stale.
  {
    json groups = json::array();
    for (const PointGroup& g : st.pointGroups) {
      json o;
      o["name"] = g.name;
      o["idRanges"] = g.rule.idRangesText;
      o["descriptionMatch"] = g.rule.descriptionMatch;
      o["rawDescriptionMatch"] = g.rule.rawDescriptionMatch;
      o["explicitIds"] = g.rule.explicitIds;
      groups.push_back(std::move(o));
    }
    doc["pointGroups"] = std::move(groups);
  }
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
  // Named surface styles (REQ-070 / ADR-036 (d)). Additive, no kGsFormatVersion bump — ADR-020 (d),
  // the same rule the mesh and surface sections follow.
  //
  // **Written only when the table holds more than the untouched default**, so a drawing with no
  // surfaces — or one whose surfaces all use an unedited "Standard" — still serialises byte for byte
  // as it did before this section existed. Resave idempotence is what BUG-015 and BUG-019 were both
  // made of, and a section that appears on every file the moment it is opened is exactly that defect.
  {
    const bool onlyUntouchedStandard =
        st.surfaceStyles.size() == 1 && st.surfaceStyles[0] == SurfaceStyles::StandardSurfaceStyle();
    if (!st.surfaceStyles.empty() && !onlyUntouchedStandard) {
      const auto componentToJson = [](const SurfaceComponentStyle& c) {
        json o;
        o["visible"] = c.visible;
        o["color"] = c.color;
        o["linetype"] = c.linetype;
        o["lineweightMm"] = c.lineweightMm;
        return o;
      };
      const auto bandsToJson = [](const std::vector<SurfaceBand>& bands) {
        json a = json::array();
        for (const SurfaceBand& b : bands) {
          json o;
          o["upperBound"] = b.upperBound;
          o["color"] = b.color;
          a.push_back(std::move(o));
        }
        return a;
      };
      json styles = json::array();
      for (const SurfaceStyle& s : st.surfaceStyles) {
        json o;
        o["name"] = s.name;
        o["triangles"] = componentToJson(s.triangles);
        o["border"] = componentToJson(s.border);
        o["majorContour"] = componentToJson(s.majorContour);
        o["minorContour"] = componentToJson(s.minorContour);
        o["points"] = componentToJson(s.points);
        o["minorIntervalFt"] = s.minorIntervalFt;
        o["majorIntervalFt"] = s.majorIntervalFt;
        // REQ-072 analysis (ADR-036 (g)). Written only when the style actually carries some, so a
        // drawing saved before REQ-072 existed — and any style that never opens the Analysis tab —
        // still resaves byte for byte. The same rule the section around it follows, applied per
        // style: a key that appears on every file the moment it is opened is what BUG-015 and
        // BUG-019 were both made of.
        if (s.analysisMode != SurfaceAnalysisMode::None || s.slopeArrowsOn || !s.bands.empty() ||
            !s.arrowBands.empty() || !s.userContourFt.empty() || s.contourSmoothPasses != 0 ||
            s.contourLabelSpacingFt != 0.0) {
          o["analysisMode"] = static_cast<int>(s.analysisMode);
          o["slopeArrowsOn"] = s.slopeArrowsOn;
          o["bands"] = bandsToJson(s.bands);
          o["arrowBands"] = bandsToJson(s.arrowBands);
          if (!s.userContourFt.empty())
            o["userContourFt"] = s.userContourFt;
          if (s.contourSmoothPasses != 0)
            o["contourSmoothPasses"] = s.contourSmoothPasses;
          if (s.contourLabelSpacingFt != 0.0)
            o["contourLabelSpacingFt"] = s.contourLabelSpacingFt;
        }
        styles.push_back(std::move(o));
      }
      doc["surfaceStyles"] = std::move(styles);
    }
  }
  // Dimension style (issue #99). Single active style, additive, no version bump.
  {
    const DimensionStyle& ds = st.activeDimensionStyle;
    const DimensionStyle def = DimensionStyles::Default();
    const bool isDefault = (ds == def);
    if (!isDefault) {
      json o;
      o["textSizeInches"] = ds.textSizeInches;
      if (!ds.textFont.empty()) o["textFont"] = ds.textFont;
      o["textColor"] = ds.textColor;
      o["textAlign"] = static_cast<int>(ds.textAlign);
      o["dimLineColor"] = ds.dimLineColor;
      o["dimLineType"] = ds.dimLineType;
      o["extLineColor"] = ds.extLineColor;
      o["extLineType"] = ds.extLineType;
      o["arrowSizeInches"] = ds.arrowSizeInches;
      o["arrowType"] = static_cast<int>(ds.arrowType);
      o["arrowColor"] = ds.arrowColor;
      o["unitFormat"] = static_cast<int>(ds.unitFormat);
      o["unitPrecision"] = ds.unitPrecision;
      o["unitScale"] = ds.unitScale;
      doc["dimensionStyle"] = std::move(o);
    }
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
        vo["vpColorLayers"] = v.vpColorLayers;    // REQ-046: per-viewport layer color override (parallel arrays)
        vo["vpColorValues"] = v.vpColorValues;
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
          // Paper fills are 2D by definition (ADR-025 (g)) — write the XY pairs the schema has
          // always held and no Z sidecar, so these entries stay byte-identical to older files.
          json pv = json::array();
          for (size_t i = 0; i + 2 < fr.vertsXyz.size(); i += 3) {
            pv.push_back(fr.vertsXyz[i + 0]);
            pv.push_back(fr.vertsXyz[i + 1]);
          }
          fo["verts"] = std::move(pv);
          fo["loops"] = fr.loopStart;
          pfills.push_back(std::move(fo));
        }
        o["paperFilledRegions"] = std::move(pfills);
        o["paperFilledRegionAttrs"] = attrsToJson(l.paperFilledRegionAttrs);
        json pbr = json::array();
        for (const CadBlockRef& r : l.paperBlockRefs)
          pbr.push_back(CadBlockRefToJson(r));
        o["paperBlockRefs"] = std::move(pbr);
        o["paperBlockRefAttrs"] = attrsToJson(l.paperBlockRefAttrs);
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
  // In memory circles are cx,cy,z,r (stride 4). On disk the long-standing "circles" key stays
  // cx,cy,r triples and Z rides in an additive "circlesZ" (one per circle), omitted when all zero
  // — same tolerant-key pattern as filled regions (ADR-020 (d)), no kGsFormatVersion bump. An
  // older build therefore still reads correct flat circles, and unchanged drawings still save
  // byte-identically.
  {
    json cxyr = json::array();
    json cz = json::array();
    bool anyZ = false;
    for (size_t i = 0; i + 3 < st.userCirclesCxCyZR.size(); i += 4) {
      cxyr.push_back(st.userCirclesCxCyZR[i + 0]);
      cxyr.push_back(st.userCirclesCxCyZR[i + 1]);
      cxyr.push_back(st.userCirclesCxCyZR[i + 3]);  // radius
      cz.push_back(st.userCirclesCxCyZR[i + 2]);    // z
      anyZ = anyZ || st.userCirclesCxCyZR[i + 2] != 0.f;
    }
    doc["circles"] = std::move(cxyr);
    if (anyZ)
      doc["circlesZ"] = std::move(cz);
  }
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

  // Feature lines (REQ-087). Same shape as polylines, plus the per-vertex elevation-point flag and
  // the per-line name. All six arrays are additive — a drawing written before REQ-087 has none of
  // them, and the reader guards on that.
  doc["featureLineOffsets"] = st.featureLineOffsets;
  doc["featureLineVerts"] = st.featureLineVerts;
  json flClosed = json::array();
  for (uint8_t c : st.featureLineClosed)
    flClosed.push_back(static_cast<int>(c));
  doc["featureLineClosed"] = std::move(flClosed);
  json flElev = json::array();
  for (uint8_t c : st.featureLineElevPt)
    flElev.push_back(static_cast<int>(c));
  doc["featureLineElevPt"] = std::move(flElev);
  json flInfo = json::array();
  for (const CadFeatureLineInfo& i : st.featureLineInfo) {
    json o;
    o["name"] = i.name;
    o["description"] = i.description;
    flInfo.push_back(std::move(o));
  }
  doc["featureLineInfo"] = std::move(flInfo);
  json flAttrs = json::array();
  for (const auto& a : st.featureLineAttrs) {
    json o;
    EntityAttributesToJson(a, o);
    flAttrs.push_back(std::move(o));
  }
  doc["featureLineAttrs"] = std::move(flAttrs);

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

  json tables = json::array();
  for (const auto& t : st.cadTables) {
    json o;
    CadTableToJson(t, o);
    tables.push_back(std::move(o));
  }
  if (!tables.empty())
    doc["tables"] = std::move(tables);
  json tableAttrs = json::array();
  for (const auto& a : st.cadTableAttrs) {
    json o;
    EntityAttributesToJson(a, o);
    tableAttrs.push_back(std::move(o));
  }
  if (!tableAttrs.empty())
    doc["tableAttrs"] = std::move(tableAttrs);

  doc["drawingInsUnits"] = st.drawingInsUnits;
  json blockDefs = json::array();
  for (const CadBlockDefinition& d : st.blockDefs)
    blockDefs.push_back(CadBlockDefToJson(d));
  if (!blockDefs.empty())
    doc["blockDefs"] = std::move(blockDefs);
  json blockRefs = json::array();
  for (const CadBlockRef& r : st.cadBlockRefs)
    blockRefs.push_back(CadBlockRefToJson(r));
  if (!blockRefs.empty())
    doc["blockRefs"] = std::move(blockRefs);
  json blockRefAttrs = json::array();
  for (const auto& a : st.cadBlockRefAttrs) {
    json o;
    EntityAttributesToJson(a, o);
    blockRefAttrs.push_back(std::move(o));
  }
  if (!blockRefAttrs.empty())
    doc["blockRefAttrs"] = std::move(blockRefAttrs);
  if (!st.blockFavorites.empty())
    doc["blockFavorites"] = st.blockFavorites;
  if (!st.blockRecent.empty())
    doc["blockRecent"] = st.blockRecent;

  // Filled regions (ADR-011): each is {verts:[x,y,…], loops:[startPairIdx,…]} + a parallel attribute object.
  json fills = json::array();
  for (const auto& fr : st.cadFilledRegions) {
    json o;
    // The in-memory store is interleaved XYZ (ADR-025 (a)), but the ON-DISK schema keeps the
    // long-standing XY "verts" array and adds Z as an additive "vertsZ" sidecar (the ADR-020 (d)
    // tolerant-key pattern, no kGsFormatVersion bump). That is deliberate and is NOT a §11.8
    // violation — §11.8 governs in-memory geometry stores, not the wire format. Splitting here
    // buys both directions: an older build still reads "verts" and gets correct flat geometry,
    // and a newer build reads the Z back. "vertsZ" is omitted entirely when every Z is 0, so
    // existing drawings continue to serialize byte-identically.
    json xy = json::array();
    json zs = json::array();
    bool anyZ = false;
    for (size_t i = 0; i + 2 < fr.vertsXyz.size(); i += 3) {
      xy.push_back(fr.vertsXyz[i + 0]);
      xy.push_back(fr.vertsXyz[i + 1]);
      zs.push_back(fr.vertsXyz[i + 2]);
      anyZ = anyZ || fr.vertsXyz[i + 2] != 0.f;
    }
    o["verts"] = std::move(xy);
    if (anyZ)
      o["vertsZ"] = std::move(zs);
    o["loops"] = fr.loopStart;
    if (!fr.patternName.empty()) {  // omit for solid fills so legacy files stay byte-identical (ADR-018)
      o["pattern"] = fr.patternName;
      o["patAngle"] = fr.patternAngleDeg;
      o["patScale"] = fr.patternScale;
    }
    fills.push_back(std::move(o));
  }
  doc["filledRegions"] = std::move(fills);

  // Imported meshes (REQ-063). Additive section — omitted entirely when there are none, so every
  // pre-REQ-063 drawing still serializes byte-identically and no kGsFormatVersion bump is needed
  // (the ADR-020 (d) tolerant-key precedent).
  //
  // Positions and normals are written as flat float arrays and indices as flat integers: the JSON
  // is large for a big model, but it is exact. Vertex positions must reload bit-identically
  // (REQ-063 acceptance), which is why nothing here is rounded or reformatted on the way out.
  if (!st.cadMeshes.empty()) {
    json meshes = json::array();
    for (const auto& mp : st.cadMeshes) {
      if (!mp)
        continue;
      json m;
      m["verts"] = mp->vertsXyz;
      m["normals"] = mp->normalsXyz;
      m["indices"] = mp->indices;
      if (!mp->sourceName.empty())
        m["source"] = mp->sourceName;
      json parts = json::array();
      for (const CadMeshPart& p : mp->parts) {
        json jp;
        jp["name"] = p.name;
        jp["begin"] = p.indexBegin;
        jp["count"] = p.indexCount;
        jp["rgb"] = json::array({p.r, p.g, p.b});
        parts.push_back(std::move(jp));
      }
      m["parts"] = std::move(parts);
      meshes.push_back(std::move(m));
    }
    doc["meshes"] = std::move(meshes);
    json meshAttrs = json::array();
    for (const auto& a : st.cadMeshAttrs) {
      json o;
      EntityAttributesToJson(a, o);
      meshAttrs.push_back(std::move(o));
    }
    doc["meshAttrs"] = std::move(meshAttrs);
  }

  // TIN surfaces (REQ-068). Additive and omitted when there are none, so a pre-REQ-068 drawing still
  // serializes byte-identically — the same ADR-020 (d) precedent the mesh section above follows.
  //
  // The triangulation is written out rather than rebuilt on load, for two reasons: rebuilding would
  // make opening a drawing depend on its point groups still resolving the same way, and REQ-068's
  // acceptance requires vertex positions to reload **bit-identically**, which only storing them can
  // guarantee. Nothing here is rounded or reformatted on the way out.
  if (!st.cadSurfaces.empty()) {
    json surfaces = json::array();
    for (const CadSurface& s : st.cadSurfaces) {
      json o;
      o["name"] = s.name;
      // REQ-070: the style is a reference by name, and it is omitted when empty so a surface that
      // has never been given one writes exactly the bytes it wrote before styles existed.
      if (!s.styleName.empty())
        o["styleName"] = s.styleName;
      o["sourcePointGroups"] = s.sourcePointGroups;
      // REQ-086: linked point files travel with their layout, because a point file does not describe
      // its own column order and a link that re-guessed would swap northing for easting on reload.
      json pointFiles = json::array();
      for (const CadSurfacePointFile& pf : s.sourcePointFiles) {
        json pfo;
        pfo["path"] = pf.path;
        pfo["layoutIndex"] = pf.layoutIndex;
        pfo["skipFirstRow"] = pf.skipFirstRow;
        pointFiles.push_back(std::move(pfo));
      }
      o["sourcePointFiles"] = std::move(pointFiles);
      // REQ-069: breaklines/boundaries are stored by stable entity id (REQ-076), never index —
      // the same rule every other cross-object reference in this file follows.
      //
      // REQ-075 gave both a descriptive string, so breaklines became objects like boundaries already
      // were. The old `breaklineIds` array of bare numbers is still READ below; it is no longer
      // written, so a file makes exactly one trip through the change.
      json breaklines = json::array();
      for (const CadSurfaceBreakline& bl : s.breaklines) {
        json blo;
        blo["entityId"] = bl.entityId;
        blo["description"] = bl.description;
        breaklines.push_back(std::move(blo));
      }
      o["breaklines"] = std::move(breaklines);
      json contours = json::array();
      for (const CadSurfaceBreakline& c : s.contourSources) {
        json co;
        co["entityId"] = c.entityId;
        co["description"] = c.description;
        contours.push_back(std::move(co));
      }
      o["contourSources"] = std::move(contours);
      json boundaries = json::array();
      for (const CadSurfaceBoundary& b : s.boundaries) {
        json bo;
        bo["entityId"] = b.entityId;
        bo["kind"] = b.kind == CadBoundaryKind::Outer ? "outer"
                    : b.kind == CadBoundaryKind::Hide  ? "hide"
                    : b.kind == CadBoundaryKind::Show  ? "show"
                    : b.kind == CadBoundaryKind::Mask  ? "mask"
                                                       : "clip";
        bo["name"] = b.name;
        boundaries.push_back(std::move(bo));
      }
      o["boundaries"] = std::move(boundaries);
      if (s.kind != SurfaceKind::Tin)
        o["kind"] = s.kind == SurfaceKind::Grid         ? "grid"
                    : s.kind == SurfaceKind::TinVolume  ? "tinVolume"
                    : s.kind == SurfaceKind::GridVolume ? "gridVolume"
                                                        : "corridor";
      if (!s.description.empty())
        o["description"] = s.description;
      if (s.kind == SurfaceKind::Grid || s.kind == SurfaceKind::GridVolume) {
        o["gridOriginX"] = s.gridOriginX;
        o["gridOriginY"] = s.gridOriginY;
        o["gridSpacingX"] = s.gridSpacingX;
        o["gridSpacingY"] = s.gridSpacingY;
        o["gridCols"] = s.gridCols;
        o["gridRows"] = s.gridRows;
        o["gridZ"] = s.gridZ;
      }
      if (!s.swappedEdgePicks.empty()) {
        json swaps = json::array();
        for (const auto& p : s.swappedEdgePicks) {
          json sp;
          sp["x"] = p.first;
          sp["y"] = p.second;
          swaps.push_back(std::move(sp));
        }
        o["swappedEdgePicks"] = std::move(swaps);
      }
      if (!s.deletedEdgePicks.empty()) {
        json delsE = json::array();
        for (const auto& p : s.deletedEdgePicks) {
          json sp;
          sp["x"] = p.first;
          sp["y"] = p.second;
          delsE.push_back(std::move(sp));
        }
        o["deletedEdgePicks"] = std::move(delsE);
      }
      if (!s.addedPointXyz.empty())
        o["addedPointXyz"] = s.addedPointXyz;
      if (!s.deletedPointPicks.empty()) {
        json dels = json::array();
        for (const auto& p : s.deletedPointPicks) {
          json dp;
          dp["x"] = p.first;
          dp["y"] = p.second;
          dels.push_back(std::move(dp));
        }
        o["deletedPointPicks"] = std::move(dels);
      }
      if (!s.movedPoints.empty()) {
        json moves = json::array();
        for (const auto& m : s.movedPoints) {
          json mo;
          mo["fromX"] = m.fromX;
          mo["fromY"] = m.fromY;
          mo["toX"] = m.toX;
          mo["toY"] = m.toY;
          mo["toZ"] = m.toZ;
          moves.push_back(std::move(mo));
        }
        o["movedPoints"] = std::move(moves);
      }
      json corr = json::array();
      for (const CadSurfaceBreakline& fl : s.corridorFeatureLines) {
        json flo;
        flo["entityId"] = fl.entityId;
        flo["description"] = fl.description;
        corr.push_back(std::move(flo));
      }
      if (!corr.empty())
        o["corridorFeatureLines"] = std::move(corr);
      if (!s.volumeBaseName.empty())
        o["volumeBaseName"] = s.volumeBaseName;
      if (!s.volumeComparisonName.empty())
        o["volumeComparisonName"] = s.volumeComparisonName;
      if (s.tin) {
        o["verts"] = s.tin->vertsXyz;
        o["indices"] = s.tin->indices;
      }
      surfaces.push_back(std::move(o));
    }
    doc["surfaces"] = std::move(surfaces);
    json surfAttrs = json::array();
    for (const auto& a : st.cadSurfaceAttrs) {
      json o;
      EntityAttributesToJson(a, o);
      surfAttrs.push_back(std::move(o));
    }
    doc["surfaceAttrs"] = std::move(surfAttrs);
  }
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
    o["rawDescription"] = p.rawDescription;  // REQ-066; additive, no format-version bump
    o["layer"] = p.layer;
    o["labelStyle"] = static_cast<int>(p.labelStyle);
    o["labelMtextAnnId"] = p.labelMtextAnnId;  // annotation entity id since REQ-076, not an index
    survey.push_back(std::move(o));
  }
  doc["surveyPoints"] = std::move(survey);
  doc["createPointsNextId"] = st.createPointsNextId;
  json cpo;
  CreatePointsOptionsToJson(st.createPointsOpts, cpo);
  doc["createPointsOptions"] = std::move(cpo);

  // Saved view (REQ-055): reopen the drawing looking at what the user left on screen.
  // The pan is the view CENTRE in local storage space, but it is written in WORLD coordinates —
  // loading may rebase the document origin (MaybeRebaseLargeCoordinates), which would silently move a
  // local pan somewhere else in the drawing. World coordinates are invariant under that rebase.
  json view;
  view["panWorldX"] = st.viewportPanX + st.worldDocumentOriginX;
  view["panWorldY"] = st.viewportPanY + st.worldDocumentOriginY;
  view["zoom"] = st.viewportZoom;
  // Camera orientation and work plane (REQ-058), additive and omitted at their defaults so a
  // drawing that was never orbited still serializes exactly as before.
  if (st.viewportPanZ != 0.0)
    view["panZ"] = st.viewportPanZ;
  if (st.viewportAzimuthDeg != 0.f)
    view["azimuthDeg"] = st.viewportAzimuthDeg;
  if (st.viewportElevationDeg != 90.f)
    view["elevationDeg"] = st.viewportElevationDeg;
  if (st.viewportRollDeg != 0.f)  // #153: screen roll under a tilted-UCS PLAN
    view["rollDeg"] = st.viewportRollDeg;
  // The UCS (REQ-154), additive and omitted at its default for the same reason as the camera keys
  // above: a drawing that never used UCS still serializes byte-for-byte as before.
  //
  // `ucsElevation` is still written whenever the frame is a plain elevation change, so a drawing
  // saved by this build still opens correctly in one that predates the full UCS. Newer builds
  // prefer the `ucs` object and only fall back to `ucsElevation` when it is absent.
  auto writeUcs = [](const ucs::Ucs& u) {
    json j;
    j["origin"] = {u.origin.x, u.origin.y, u.origin.z};
    j["xAxis"] = {u.xAxis.x, u.xAxis.y, u.xAxis.z};
    j["yAxis"] = {u.yAxis.x, u.yAxis.y, u.yAxis.z};
    j["zAxis"] = {u.zAxis.x, u.zAxis.y, u.zAxis.z};
    return j;
  };
  if (!ucs::IsWorld(st.activeUcs)) {
    view["ucs"] = writeUcs(st.activeUcs);
    // A pure elevation change — world axes, origin only in Z — is exactly what the old key meant,
    // so that (and only that) case stays readable by an older build.
    const bool elevationOnly = ucs::IsWorld(ucs::WithOrigin(st.activeUcs, {0.0, 0.0, 0.0})) &&
                               st.activeUcs.origin.x == 0.0 && st.activeUcs.origin.y == 0.0;
    if (elevationOnly)
      view["ucsElevation"] = st.activeUcs.origin.z;
  }
  if (st.ucsFollow)
    view["ucsFollow"] = true;
  if (!st.ucsNamed.empty()) {
    json named = json::array();
    for (const NamedUcs& n : st.ucsNamed) {
      json entry = writeUcs(n.frame);
      entry["name"] = n.name;
      named.push_back(std::move(entry));
    }
    view["namedUcs"] = std::move(named);
  }
  // Named views (REQ-106), additive and omitted when empty for the same reason as the keys above: a
  // drawing that never saved a view serializes byte-for-byte as it did before this existed, so no
  // format-version bump is needed (the ADR-020 (d) / ADR-025 (g) precedent).
  if (!st.namedViews.empty()) {
    json views = json::array();
    for (const NamedView& v : st.namedViews) {
      json e;
      e["name"] = v.name;
      e["panX"] = v.panX;
      e["panY"] = v.panY;
      if (v.panZ != 0.0)
        e["panZ"] = v.panZ;
      e["zoom"] = v.zoom;
      e["azimuthDeg"] = v.azimuthDeg;
      e["elevationDeg"] = v.elevationDeg;
      if (v.rollDeg != 0.f)  // #153
        e["rollDeg"] = v.rollDeg;
      // The frame rides with the view (REQ-106). Omitted at World so the common case stays compact.
      if (!ucs::IsWorld(v.ucs))
        e["ucs"] = writeUcs(v.ucs);
      views.push_back(std::move(e));
    }
    view["namedViews"] = std::move(views);
  }
  if (!st.activeViewName.empty())
    view["activeViewName"] = st.activeViewName;
  doc["view"] = std::move(view);

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
  settings["viewportVisualStyle"] = static_cast<int>(st.viewportVisualStyle);
  settings["objectSnapIntersection"] = st.objectSnapIntersection;
  settings["objectSnapApparentIntersection"] = st.objectSnapApparentIntersection;
  settings["objectSnapSurface"] = st.objectSnapSurface;
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

  // Visual style (REQ-064). Read through a range check rather than a raw cast: an out-of-range or
  // hand-edited value must land on the default style, not on an enum value that does not exist.
  if (s.contains("viewportVisualStyle") && s["viewportVisualStyle"].is_number_integer()) {
    const int vsRaw = s["viewportVisualStyle"].get<int>();
    st.viewportVisualStyle = (vsRaw >= 0 && vsRaw <= static_cast<int>(VisualStyle::Shaded))
                                 ? static_cast<VisualStyle>(vsRaw)
                                 : VisualStyle::Wireframe2D;
  }

  b(s, "objectSnapEnabled", &st.objectSnapEnabled);
  b(s, "objectSnapEndpoint", &st.objectSnapEndpoint);
  b(s, "objectSnapMidpoint", &st.objectSnapMidpoint);
  b(s, "objectSnapCenter", &st.objectSnapCenter);
  b(s, "objectSnapPerpendicular", &st.objectSnapPerpendicular);
  b(s, "objectSnapSurveyPoint", &st.objectSnapSurveyPoint);
  b(s, "objectSnapGeometricCenter", &st.objectSnapGeometricCenter);
  b(s, "objectSnapIntersection", &st.objectSnapIntersection);
  b(s, "objectSnapApparentIntersection", &st.objectSnapApparentIntersection);
  b(s, "objectSnapSurface", &st.objectSnapSurface);
  num(s, "objectSnapAperturePx", &st.objectSnapAperturePx);
  num(s, "objectSnapGlyphHalfPx", &st.objectSnapGlyphHalfPx);
  st.objectSnapAperturePx = std::clamp(st.objectSnapAperturePx, 4.f, 64.f);
  st.objectSnapGlyphHalfPx = std::clamp(st.objectSnapGlyphHalfPx, 3.f, 48.f);
}

void ApplyDocumentFromJson(AppCommandState& st, const json& doc, std::vector<std::string>& log) {
  st.worldDocumentOriginX = doc.value("worldDocumentOriginX", 0.0);
  st.worldDocumentOriginY = doc.value("worldDocumentOriginY", 0.0);
  // A legacy file has no counter; 1 is correct there because every entity in it also has no id, so
  // the post-load EnsureEntityIds numbers the whole drawing from 1. EnsureEntityIds independently
  // raises the counter above any id actually present, so a hand-edited or newer file cannot make it
  // hand out an id that is already in use.
  st.nextEntityId = doc.value("nextEntityId", static_cast<std::uint64_t>(1));

  // Point groups (REQ-067). Absent in every file written before them → no groups, which is the
  // "legacy `.gs` loads unchanged" acceptance condition.
  st.pointGroups.clear();
  if (doc.contains("pointGroups") && doc["pointGroups"].is_array()) {
    for (const auto& o : doc["pointGroups"]) {
      if (!o.is_object())
        continue;
      PointGroup g;
      g.name = o.value("name", std::string());
      if (g.name.empty())
        continue;  // an unnamed group cannot be referenced or edited; dropping it beats keeping it
      g.rule.idRangesText = o.value("idRanges", std::string());
      g.rule.descriptionMatch = o.value("descriptionMatch", std::string());
      g.rule.rawDescriptionMatch = o.value("rawDescriptionMatch", std::string());
      if (o.contains("explicitIds") && o["explicitIds"].is_array())
        for (const auto& v : o["explicitIds"])
          if (v.is_number_integer())
            g.rule.explicitIds.push_back(v.get<int>());
      st.pointGroups.push_back(std::move(g));
    }
  }
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
          // REQ-046: per-viewport layer color override (kept as equal-length parallel arrays).
          if (vo.contains("vpColorLayers") && vo["vpColorLayers"].is_array() && vo.contains("vpColorValues") &&
              vo["vpColorValues"].is_array()) {
            const auto& ls = vo["vpColorLayers"];
            const auto& cs = vo["vpColorValues"];
            const size_t n = std::min(ls.size(), cs.size());
            for (size_t k = 0; k < n; ++k) {
              if (ls[k].is_string() && cs[k].is_string()) {
                v.vpColorLayers.push_back(ls[k].get<std::string>());
                v.vpColorValues.push_back(cs[k].get<std::string>());
              }
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
              // "verts" is XY on disk; the store is interleaved XYZ. Paper fills are always
              // Z = 0 (ADR-025 (g)), so expand the pairs with a zero Z.
              if (el.contains("verts")) {
                const auto& pv = el["verts"];
                for (size_t i = 0; i + 1 < pv.size(); i += 2) {
                  fr.vertsXyz.push_back(pv[i + 0].get<float>());
                  fr.vertsXyz.push_back(pv[i + 1].get<float>());
                  fr.vertsXyz.push_back(0.f);
                }
              }
              if (el.contains("loops"))
                for (const auto& v : el["loops"])
                  fr.loopStart.push_back(v.get<int>());
              if (fr.loopStart.empty() && fr.vertsXyz.size() >= 9)  // >= 3 vertices × 3 floats
                fr.loopStart.push_back(0);
              l.paperFilledRegions.push_back(std::move(fr));
            }
        readAttrs("paperFilledRegionAttrs", l.paperFilledRegionAttrs);
        l.paperFilledRegionAttrs.resize(l.paperFilledRegions.size());
        if (o.contains("paperBlockRefs") && o["paperBlockRefs"].is_array())
          for (const auto& br : o["paperBlockRefs"])
            if (br.is_object())
              l.paperBlockRefs.push_back(CadBlockRefFromJson(br));
        readAttrs("paperBlockRefAttrs", l.paperBlockRefAttrs);
        l.paperBlockRefAttrs.resize(l.paperBlockRefs.size());
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

  // Named surface styles (REQ-070 / ADR-036 (d)). Read tolerantly, every field with a default: a file
  // with no section synthesizes "Standard" and every surface in it — whose styleName is empty —
  // resolves to it, which is REQ-070's "a legacy `.gs` loads unchanged".
  st.surfaceStyles.clear();
  if (doc.contains("surfaceStyles") && doc["surfaceStyles"].is_array()) {
    const auto componentFromJson = [](const json& o, SurfaceComponentStyle* c) {
      if (!o.is_object())
        return;
      c->visible = o.value("visible", c->visible);
      c->color = o.value("color", c->color);
      c->linetype = o.value("linetype", c->linetype);
      c->lineweightMm = o.value("lineweightMm", c->lineweightMm);
    };
    // Each band carries its own colour, so sorting repairs a hand-edited or corrupt table without
    // repainting anything: AssignBand requires strictly ascending bounds, and a descending pair would
    // otherwise put a value in a band that is not its own. Reordering is the honest fix here; drawing
    // from a table known to violate its own precondition is not.
    const auto bandsFromJson = [](const json& a, std::vector<SurfaceBand>* out) {
      out->clear();
      if (!a.is_array())
        return;
      for (const auto& e : a) {
        if (!e.is_object())
          continue;
        SurfaceBand b;
        b.upperBound = e.value("upperBound", 0.0);
        b.color = e.value("color", std::string("ByLayer"));
        out->push_back(std::move(b));
      }
      std::stable_sort(out->begin(), out->end(),
                       [](const SurfaceBand& x, const SurfaceBand& y) {
                         return x.upperBound < y.upperBound;
                       });
    };
    for (const auto& o : doc["surfaceStyles"]) {
      if (!o.is_object())
        continue;
      // Seeded from the built-in default rather than from a value-initialised SurfaceStyle, so a key
      // REQ-072 adds to this object later reads back as the default it was written against instead of
      // as a zero nobody chose. ADR-020 (d)'s additive read, applied forward as well as backward.
      SurfaceStyle s = SurfaceStyles::StandardSurfaceStyle();
      s.name = o.value("name", std::string());
      if (s.name.empty())
        continue;  // an unnamed style cannot be referenced
      componentFromJson(o.value("triangles", json::object()), &s.triangles);
      componentFromJson(o.value("border", json::object()), &s.border);
      componentFromJson(o.value("majorContour", json::object()), &s.majorContour);
      componentFromJson(o.value("minorContour", json::object()), &s.minorContour);
      componentFromJson(o.value("points", json::object()), &s.points);
      s.minorIntervalFt = o.value("minorIntervalFt", s.minorIntervalFt);
      s.majorIntervalFt = o.value("majorIntervalFt", s.majorIntervalFt);
      // REQ-072 analysis. Absent keys leave the seeded defaults alone — banding off — which is what
      // makes a pre-REQ-072 drawing open with its plain display rather than with a zeroed table.
      if (o.contains("analysisMode")) {
        // An unrecognised mode falls back to None rather than becoming an enum value no switch
        // handles: a file written by a later version must degrade to "not banded", not to undefined.
        const int mode = o.value("analysisMode", 0);
        s.analysisMode = mode == 1   ? SurfaceAnalysisMode::Elevation
                         : mode == 2 ? SurfaceAnalysisMode::Slope
                         : mode == 3 ? SurfaceAnalysisMode::Direction
                         : mode == 4 ? SurfaceAnalysisMode::SlopeAngle
                                     : SurfaceAnalysisMode::None;
      }
      s.slopeArrowsOn = o.value("slopeArrowsOn", s.slopeArrowsOn);
      if (o.contains("userContourFt") && o["userContourFt"].is_array())
        s.userContourFt = o["userContourFt"].get<std::vector<double>>();
      s.contourSmoothPasses = o.value("contourSmoothPasses", s.contourSmoothPasses);
      s.contourLabelSpacingFt = o.value("contourLabelSpacingFt", s.contourLabelSpacingFt);
      if (o.contains("bands"))
        bandsFromJson(o["bands"], &s.bands);
      if (o.contains("arrowBands"))
        bandsFromJson(o["arrowBands"], &s.arrowBands);
      st.surfaceStyles.push_back(std::move(s));
    }
  }
  SurfaceStyles::EnsureStandard(st.surfaceStyles);

  // Dimension style (issue #99). Missing section -> default.
  {
    DimensionStyle ds = DimensionStyles::Default();
    if (doc.contains("dimensionStyle") && doc["dimensionStyle"].is_object()) {
      const auto& o = doc["dimensionStyle"];
      ds.textSizeInches = o.value("textSizeInches", ds.textSizeInches);
      ds.textFont = o.value("textFont", ds.textFont);
      ds.textColor = o.value("textColor", ds.textColor);
      ds.textAlign = static_cast<DimTextAlign>(o.value("textAlign", static_cast<int>(ds.textAlign)));
      ds.dimLineColor = o.value("dimLineColor", ds.dimLineColor);
      ds.dimLineType = o.value("dimLineType", ds.dimLineType);
      ds.extLineColor = o.value("extLineColor", ds.extLineColor);
      ds.extLineType = o.value("extLineType", ds.extLineType);
      ds.arrowSizeInches = o.value("arrowSizeInches", ds.arrowSizeInches);
      ds.arrowType = static_cast<DimArrowType>(o.value("arrowType", static_cast<int>(ds.arrowType)));
      ds.arrowColor = o.value("arrowColor", ds.arrowColor);
      ds.unitFormat = static_cast<DimUnitFormat>(o.value("unitFormat", static_cast<int>(ds.unitFormat)));
      ds.unitPrecision = o.value("unitPrecision", ds.unitPrecision);
      ds.unitScale = o.value("unitScale", ds.unitScale);
    }
    st.activeDimensionStyle = ds;
    st.dimStyleDraft = ds;
  }

  st.userLinesFlat.clear();
  for (const auto& v : doc["lineVerts"])
    st.userLinesFlat.push_back(v.get<float>());
  st.userLineAttrs.clear();
  for (const auto& o : doc["lineAttrs"])
    st.userLineAttrs.push_back(EntityAttributesFromJson(o));

  // "circles" is cx,cy,r on disk; "circlesZ" is the additive per-circle Z (absent → all flat,
  // which is exactly how every pre-3D drawing loads).
  st.userCirclesCxCyZR.clear();
  {
    const auto& cxyr = doc["circles"];
    const bool hasZ = doc.contains("circlesZ");
    const auto& cz = hasZ ? doc["circlesZ"] : cxyr;  // cz unread unless hasZ
    size_t ci = 0;
    for (size_t i = 0; i + 2 < cxyr.size(); i += 3, ++ci) {
      st.userCirclesCxCyZR.push_back(cxyr[i + 0].get<float>());          // cx
      st.userCirclesCxCyZR.push_back(cxyr[i + 1].get<float>());          // cy
      st.userCirclesCxCyZR.push_back(hasZ && ci < cz.size() ? cz[ci].get<float>() : 0.f);
      st.userCirclesCxCyZR.push_back(cxyr[i + 2].get<float>());          // r
    }
  }
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

  // Feature lines (REQ-087). Every key is guarded, unlike the polyline block above, because these
  // arrays are NEW: a drawing written before REQ-087 has none of them and `doc["..."]` on a missing
  // key would throw rather than yield an empty surface.
  st.featureLineOffsets.clear();
  st.featureLineVerts.clear();
  st.featureLineClosed.clear();
  st.featureLineElevPt.clear();
  st.featureLineInfo.clear();
  st.featureLineAttrs.clear();
  if (doc.contains("featureLineOffsets") && doc["featureLineOffsets"].is_array())
    for (const auto& v : doc["featureLineOffsets"])
      st.featureLineOffsets.push_back(v.get<int>());
  if (doc.contains("featureLineVerts") && doc["featureLineVerts"].is_array())
    for (const auto& v : doc["featureLineVerts"])
      st.featureLineVerts.push_back(v.get<float>());
  if (doc.contains("featureLineClosed") && doc["featureLineClosed"].is_array())
    for (const auto& v : doc["featureLineClosed"])
      st.featureLineClosed.push_back(static_cast<uint8_t>(std::clamp(v.get<int>(), 0, 1)));
  if (doc.contains("featureLineElevPt") && doc["featureLineElevPt"].is_array())
    for (const auto& v : doc["featureLineElevPt"])
      st.featureLineElevPt.push_back(static_cast<uint8_t>(std::clamp(v.get<int>(), 0, 1)));
  if (doc.contains("featureLineInfo") && doc["featureLineInfo"].is_array())
    for (const auto& o : doc["featureLineInfo"]) {
      CadFeatureLineInfo i;
      if (o.is_object()) {
        i.name = o.value("name", std::string());
        i.description = o.value("description", std::string());
      }
      st.featureLineInfo.push_back(std::move(i));
    }
  if (doc.contains("featureLineAttrs") && doc["featureLineAttrs"].is_array())
    for (const auto& o : doc["featureLineAttrs"])
      st.featureLineAttrs.push_back(EntityAttributesFromJson(o));

  st.cadAnnotations.clear();
  for (const auto& o : doc["annotations"])
    st.cadAnnotations.push_back(CadAnnotationFromJson(o));
  st.cadAnnotationAttrs.clear();
  for (const auto& o : doc["annotationAttrs"])
    st.cadAnnotationAttrs.push_back(EntityAttributesFromJson(o));

  st.cadTables.clear();
  st.cadTableAttrs.clear();
  if (doc.contains("tables") && doc["tables"].is_array()) {
    for (const auto& o : doc["tables"])
      st.cadTables.push_back(CadTableFromJson(o));
  }
  if (doc.contains("tableAttrs") && doc["tableAttrs"].is_array()) {
    for (const auto& o : doc["tableAttrs"])
      st.cadTableAttrs.push_back(EntityAttributesFromJson(o));
  }
  st.cadTableAttrs.resize(st.cadTables.size());
  MigrateLegacyAnnotationTables(st);

  st.drawingInsUnits = doc.value("drawingInsUnits", st.drawingInsUnits);
  st.blockDefs.clear();
  st.cadBlockRefs.clear();
  st.cadBlockRefAttrs.clear();
  if (doc.contains("blockDefs") && doc["blockDefs"].is_array()) {
    for (const auto& o : doc["blockDefs"])
      st.blockDefs.push_back(CadBlockDefFromJson(o));
  }
  if (doc.contains("blockRefs") && doc["blockRefs"].is_array()) {
    for (const auto& o : doc["blockRefs"])
      st.cadBlockRefs.push_back(CadBlockRefFromJson(o));
  }
  if (doc.contains("blockRefAttrs") && doc["blockRefAttrs"].is_array()) {
    for (const auto& o : doc["blockRefAttrs"])
      st.cadBlockRefAttrs.push_back(EntityAttributesFromJson(o));
  }
  st.cadBlockRefAttrs.resize(st.cadBlockRefs.size());
  if (doc.contains("blockFavorites") && doc["blockFavorites"].is_array())
    st.blockFavorites = doc["blockFavorites"].get<std::vector<std::string>>();
  if (doc.contains("blockRecent") && doc["blockRecent"].is_array())
    st.blockRecent = doc["blockRecent"].get<std::vector<std::string>>();

  // Imported meshes (REQ-063). Guarded with contains(), so a pre-REQ-063 drawing simply has none —
  // the "legacy .gs loads unchanged" acceptance condition.
  //
  // Every mesh is VALIDATED before it is stored. A malformed or truncated file must be refused with
  // a specific reason rather than partly loaded (REQ-201): an index that overruns the vertex array
  // would otherwise reach the GPU, where it is an out-of-bounds read rather than an error message.
  st.cadMeshes.clear();
  st.cadMeshAttrs.clear();
  if (doc.contains("meshes") && doc["meshes"].is_array()) {
    int meshIdx = 0;
    for (const auto& el : doc["meshes"]) {
      ++meshIdx;
      if (!el.is_object())
        continue;
      auto m = std::make_shared<CadMesh>();
      if (el.contains("verts"))
        m->vertsXyz = el["verts"].get<std::vector<float>>();
      if (el.contains("normals"))
        m->normalsXyz = el["normals"].get<std::vector<float>>();
      if (el.contains("indices"))
        m->indices = el["indices"].get<std::vector<std::uint32_t>>();
      if (el.contains("source"))
        m->sourceName = el["source"].get<std::string>();
      std::vector<std::pair<int, int>> partRanges;
      if (el.contains("parts") && el["parts"].is_array()) {
        for (const auto& jp : el["parts"]) {
          CadMeshPart p;
          if (jp.contains("name"))
            p.name = jp["name"].get<std::string>();
          if (jp.contains("begin"))
            p.indexBegin = jp["begin"].get<int>();
          if (jp.contains("count"))
            p.indexCount = jp["count"].get<int>();
          if (jp.contains("rgb") && jp["rgb"].is_array() && jp["rgb"].size() == 3) {
            p.r = jp["rgb"][0].get<float>();
            p.g = jp["rgb"][1].get<float>();
            p.b = jp["rgb"][2].get<float>();
          }
          partRanges.emplace_back(p.indexBegin, p.indexCount);
          m->parts.push_back(std::move(p));
        }
      }
      const meshgeom::MeshProblem problem =
          meshgeom::ValidateMesh(m->vertsXyz, m->normalsXyz, m->indices, partRanges);
      if (problem != meshgeom::MeshProblem::Ok) {
        log.push_back(std::string("Mesh ") + std::to_string(meshIdx) + " skipped — " +
                      meshgeom::MeshProblemText(problem) + ".");
        continue;
      }
      if (m->normalsXyz.empty() && !m->indices.empty())
        meshgeom::ComputeVertexNormals(m->vertsXyz, m->indices, &m->normalsXyz);
      // One part covering everything, so a mesh saved without parts still draws.
      if (m->parts.empty() && !m->indices.empty()) {
        CadMeshPart p;
        p.indexBegin = 0;
        p.indexCount = static_cast<int>(m->indices.size());
        m->parts.push_back(std::move(p));
      }
      st.cadMeshes.push_back(std::move(m));
    }
  }
  if (doc.contains("meshAttrs") && doc["meshAttrs"].is_array())
    for (const auto& o : doc["meshAttrs"])
      st.cadMeshAttrs.push_back(EntityAttributesFromJson(o));
  st.cadMeshAttrs.resize(st.cadMeshes.size());  // keep the parallel arrays length-locked

  // TIN surfaces (REQ-068). Guarded, so a drawing written before them simply has none.
  st.cadSurfaces.clear();
  st.cadSurfaceAttrs.clear();
  if (doc.contains("surfaces") && doc["surfaces"].is_array()) {
    for (const auto& el : doc["surfaces"]) {
      if (!el.is_object())
        continue;
      CadSurface s;
      s.name = el.value("name", std::string());
      if (s.name.empty())
        continue;  // an unnamed surface cannot be referenced or managed
      // REQ-070. Absent in every file written before styles existed, and absent again in any file
      // whose surfaces never left "Standard" — both read as empty, which resolves to the default on
      // read (ADR-036 (d)). A name whose style was since deleted is NOT repaired here: it stays as
      // written and falls back at draw time, so re-pointing the surface at a style with that name
      // later restores it instead of finding the reference silently rewritten.
      s.styleName = el.value("styleName", std::string());
      {
        const std::string ks = el.value("kind", std::string());
        s.kind = ks == "grid"          ? SurfaceKind::Grid
                 : ks == "tinVolume"   ? SurfaceKind::TinVolume
                 : ks == "gridVolume"  ? SurfaceKind::GridVolume
                 : ks == "corridor"    ? SurfaceKind::Corridor
                                       : SurfaceKind::Tin;
      }
      s.description = el.value("description", std::string());
      s.gridOriginX = el.value("gridOriginX", 0.0);
      s.gridOriginY = el.value("gridOriginY", 0.0);
      s.gridSpacingX = el.value("gridSpacingX", 1.0);
      s.gridSpacingY = el.value("gridSpacingY", 1.0);
      s.gridCols = el.value("gridCols", 0);
      s.gridRows = el.value("gridRows", 0);
      if (el.contains("gridZ") && el["gridZ"].is_array())
        s.gridZ = el["gridZ"].get<std::vector<float>>();
      if (el.contains("swappedEdgePicks") && el["swappedEdgePicks"].is_array()) {
        for (const auto& sp : el["swappedEdgePicks"]) {
          if (!sp.is_object())
            continue;
          s.swappedEdgePicks.emplace_back(sp.value("x", 0.0), sp.value("y", 0.0));
        }
      }
      if (el.contains("deletedEdgePicks") && el["deletedEdgePicks"].is_array()) {
        for (const auto& sp : el["deletedEdgePicks"]) {
          if (!sp.is_object())
            continue;
          s.deletedEdgePicks.emplace_back(sp.value("x", 0.0), sp.value("y", 0.0));
        }
      }
      if (el.contains("addedPointXyz") && el["addedPointXyz"].is_array())
        s.addedPointXyz = el["addedPointXyz"].get<std::vector<float>>();
      if (el.contains("deletedPointPicks") && el["deletedPointPicks"].is_array()) {
        for (const auto& dp : el["deletedPointPicks"]) {
          if (!dp.is_object())
            continue;
          s.deletedPointPicks.emplace_back(dp.value("x", 0.0), dp.value("y", 0.0));
        }
      }
      if (el.contains("movedPoints") && el["movedPoints"].is_array()) {
        for (const auto& mo : el["movedPoints"]) {
          if (!mo.is_object())
            continue;
          CadSurface::MovedPoint m;
          m.fromX = mo.value("fromX", 0.0);
          m.fromY = mo.value("fromY", 0.0);
          m.toX = mo.value("toX", 0.f);
          m.toY = mo.value("toY", 0.f);
          m.toZ = mo.value("toZ", 0.f);
          s.movedPoints.push_back(m);
        }
      }
      if (el.contains("corridorFeatureLines") && el["corridorFeatureLines"].is_array()) {
        for (const auto& flo : el["corridorFeatureLines"]) {
          if (!flo.is_object() || !flo.contains("entityId"))
            continue;
          CadSurfaceBreakline fl;
          fl.entityId = flo["entityId"].get<std::uint64_t>();
          fl.description = flo.value("description", std::string());
          s.corridorFeatureLines.push_back(std::move(fl));
        }
      }
      s.volumeBaseName = el.value("volumeBaseName", std::string());
      s.volumeComparisonName = el.value("volumeComparisonName", std::string());
      if (el.contains("sourcePointGroups") && el["sourcePointGroups"].is_array())
        for (const auto& g : el["sourcePointGroups"])
          if (g.is_string())
            s.sourcePointGroups.push_back(g.get<std::string>());
      // REQ-069. Ids are resolved lazily against the current drawing the next time the surface
      // rebuilds (BuildSurfaceFromSources / ResolveSurfaceInputs) — the same lazy-resolution rule
      // every other stable-id reference in this codebase already follows (ADR-027). An id that no
      // longer resolves is silently absent here; it is reported and pruned on that next rebuild, not
      // treated as a load-time error — a legacy file predating REQ-069 simply has none of either.
      // REQ-086. Absent in any file written before it — a legacy drawing simply has no linked files.
      if (el.contains("sourcePointFiles") && el["sourcePointFiles"].is_array())
        for (const auto& pfo : el["sourcePointFiles"]) {
          if (!pfo.is_object() || !pfo.contains("path") || !pfo["path"].is_string())
            continue;
          CadSurfacePointFile pf;
          pf.path = pfo["path"].get<std::string>();
          pf.layoutIndex = pfo.value("layoutIndex", 0);
          pf.skipFirstRow = pfo.value("skipFirstRow", false);
          s.sourcePointFiles.push_back(std::move(pf));
        }
      //
      // Breaklines are read in BOTH forms. `breaklines` is the current one, objects carrying the
      // REQ-075 description; `breaklineIds` is what REQ-069 originally wrote, a bare id array. A
      // drawing saved before REQ-075 therefore keeps its breaklines instead of silently losing them,
      // and is written back in the new form — one migration, on first save, with no separate step.
      if (el.contains("breaklines") && el["breaklines"].is_array()) {
        for (const auto& blo : el["breaklines"]) {
          if (!blo.is_object() || !blo.contains("entityId") || !blo["entityId"].is_number_unsigned())
            continue;
          CadSurfaceBreakline bl;
          bl.entityId = blo["entityId"].get<std::uint64_t>();
          bl.description = blo.value("description", std::string());
          s.breaklines.push_back(std::move(bl));
        }
      } else if (el.contains("breaklineIds") && el["breaklineIds"].is_array()) {
        for (const auto& id : el["breaklineIds"])
          if (id.is_number_unsigned()) {
            CadSurfaceBreakline bl;
            bl.entityId = id.get<std::uint64_t>();
            s.breaklines.push_back(std::move(bl));  // legacy: no description existed to carry
          }
      }
      if (el.contains("contourSources") && el["contourSources"].is_array()) {
        for (const auto& co : el["contourSources"]) {
          if (!co.is_object() || !co.contains("entityId") || !co["entityId"].is_number_unsigned())
            continue;
          CadSurfaceBreakline c;
          c.entityId = co["entityId"].get<std::uint64_t>();
          c.description = co.value("description", std::string());
          s.contourSources.push_back(std::move(c));
        }
      }
      if (el.contains("boundaries") && el["boundaries"].is_array())
        for (const auto& bo : el["boundaries"]) {
          if (!bo.is_object() || !bo.contains("entityId") || !bo["entityId"].is_number_unsigned())
            continue;
          CadSurfaceBoundary b;
          b.entityId = bo["entityId"].get<std::uint64_t>();
          const std::string kindStr = bo.value("kind", std::string("outer"));
          b.kind = kindStr == "hide" ? CadBoundaryKind::Hide
                 : kindStr == "show" ? CadBoundaryKind::Show
                 : kindStr == "clip" ? CadBoundaryKind::Clip
                 : kindStr == "mask" ? CadBoundaryKind::Mask
                                     : CadBoundaryKind::Outer;
          b.name = bo.value("name", std::string());  // absent in a pre-REQ-075 file
          s.boundaries.push_back(std::move(b));
        }
      if (el.contains("verts") && el["verts"].is_array() && el.contains("indices") &&
          el["indices"].is_array()) {
        auto tin = std::make_shared<CadTin>();
        tin->vertsXyz = el["verts"].get<std::vector<float>>();
        tin->indices = el["indices"].get<std::vector<std::uint32_t>>();
        // A triangulation whose arrays do not agree is corrupt; drop it rather than let the renderer
        // index past the end of the vertex array (REQ-201 — refuse, do not absorb).
        const bool sane = (tin->vertsXyz.size() % 3 == 0) && (tin->indices.size() % 3 == 0);
        bool inRange = sane;
        const std::uint32_t nv = static_cast<std::uint32_t>(tin->vertsXyz.size() / 3);
        if (sane)
          for (std::uint32_t ix : tin->indices)
            if (ix >= nv) {
              inRange = false;
              break;
            }
        if (inRange)
          s.tin = std::move(tin);
        else
          log.push_back("Surface \"" + s.name + "\": stored triangulation is inconsistent — dropped.");
      }
      st.cadSurfaces.push_back(std::move(s));
    }
  }
  if (doc.contains("surfaceAttrs") && doc["surfaceAttrs"].is_array())
    for (const auto& o : doc["surfaceAttrs"])
      st.cadSurfaceAttrs.push_back(EntityAttributesFromJson(o));
  st.cadSurfaceAttrs.resize(st.cadSurfaces.size());  // length-locked, as with meshes
  if (!st.cadMeshes.empty()) {
    // REQ-063 requires the count to be REPORTED, not merely survived: a silent truncation and a
    // successful load are indistinguishable without it.
    long long tris = 0;
    long long parts = 0;
    for (const auto& mp : st.cadMeshes) {
      tris += mp->triangleCount();
      parts += static_cast<long long>(mp->parts.size());
    }
    log.push_back("Loaded " + std::to_string(st.cadMeshes.size()) + " mesh(es): " + std::to_string(tris) +
                  " triangles, " + std::to_string(parts) + " part(s).");
  }

  // Filled regions (ADR-011) — guarded with contains() so older .gs files load unchanged.
  st.cadFilledRegions.clear();
  if (doc.contains("filledRegions") && doc["filledRegions"].is_array()) {
    for (const auto& el : doc["filledRegions"]) {
      CadFilledRegion fr;
      // Current form: {verts, loops}. Legacy form (pre-multi-loop): a bare flat vertex array = one loop.
      if (el.is_object()) {
        // "verts" is XY on disk; "vertsZ" is the additive per-vertex Z sidecar (REQ-057 /
        // ADR-025 (a)). Absent or short → Z = 0, which is exactly how every pre-3D drawing
        // loads: flat, and rendering identically to before.
        if (el.contains("verts")) {
          const auto& pv = el["verts"];
          const bool hasZ = el.contains("vertsZ");
          const auto& pz = hasZ ? el["vertsZ"] : pv;  // pz unread unless hasZ
          size_t vi = 0;
          for (size_t i = 0; i + 1 < pv.size(); i += 2, ++vi) {
            fr.vertsXyz.push_back(pv[i + 0].get<float>());
            fr.vertsXyz.push_back(pv[i + 1].get<float>());
            fr.vertsXyz.push_back(hasZ && vi < pz.size() ? pz[vi].get<float>() : 0.f);
          }
        }
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
        // Legacy pre-multi-loop form: a bare flat XY array = one loop. Expand to XYZ at Z = 0.
        for (size_t i = 0; i + 1 < el.size(); i += 2) {
          fr.vertsXyz.push_back(el[i + 0].get<float>());
          fr.vertsXyz.push_back(el[i + 1].get<float>());
          fr.vertsXyz.push_back(0.f);
        }
      }
      if (fr.loopStart.empty() && fr.vertsXyz.size() >= 9)  // >= 3 vertices × 3 floats
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
      // Absent in every pre-REQ-066 file, which is exactly the "loads empty and falls back to
      // description" case the acceptance calls for — not an error, not a copy of the description.
      if (o.contains("rawDescription") && o["rawDescription"].is_string())
        p.rawDescription = o["rawDescription"].get<std::string>();
      if (o.contains("layer") && o["layer"].is_string())
        p.layer = o["layer"].get<std::string>();
      const int ls = o.value("labelStyle", static_cast<int>(SurveyPointLabelStyle::NumberDesc));
      if (ls >= 0 && ls <= static_cast<int>(SurveyPointLabelStyle::NumberNorthEastElev))
        p.labelStyle = static_cast<SurveyPointLabelStyle>(ls);
      // The pre-REQ-076 "labelMtextAnnIndex" is deliberately not read: a legacy file's links are
      // rebuilt from the annotation side alone (see ReconcileSurveyLabelLinks), which needs one
      // direction, not two that could disagree.
      p.labelMtextAnnId = o.value("labelMtextAnnId", static_cast<std::uint64_t>(0));
      st.surveyPoints.push_back(std::move(p));
    }
  }

  st.createPointsNextId = doc.value("createPointsNextId", 1);
  if (doc.contains("createPointsOptions") && doc["createPointsOptions"].is_object())
    st.createPointsOpts = CreatePointsOptionsFromJson(doc["createPointsOptions"]);
  else
    st.createPointsOpts = CreatePointsOptions{};

  CadCoord::MaybeRebaseLargeCoordinates(st, &log);

  // Saved view (REQ-055). Applied AFTER the rebase above, because that is what fixes
  // worldDocumentOrigin — converting the stored world pan to local any earlier would use an origin the
  // drawing no longer has. Files written before this key fall back to framing the drawing, which is
  // still better than the default view they get today; an empty drawing keeps the default.
  if (doc.contains("view") && doc["view"].is_object()) {
    const json& view = doc["view"];
    const double panWorldX = view.value("panWorldX", 0.0);
    const double panWorldY = view.value("panWorldY", 0.0);
    const float zoom = view.value("zoom", 1.f);
    st.viewportPanX = panWorldX - st.worldDocumentOriginX;
    st.viewportPanY = panWorldY - st.worldDocumentOriginY;
    // Clamp to the range the zoom controls themselves use, so a corrupt or hand-edited value cannot
    // leave the drawing on an unrecoverable view (REQ-201).
    st.viewportZoom = std::clamp(zoom, 1.e-9f, 1.e9f);
    // Absent keys give plan view at world elevation, which is how every pre-3D drawing loads.
    // Elevation is clamped for the same reason the zoom is: a hand-edited value must not leave the
    // camera somewhere it cannot be recovered from (REQ-201).
    st.viewportPanZ = view.value("panZ", 0.0);
    st.viewportAzimuthDeg = view.value("azimuthDeg", 0.f);
    st.viewportElevationDeg = std::clamp(view.value("elevationDeg", 90.f), -90.f, 90.f);
    st.viewportRollDeg = view.value("rollDeg", 0.f);  // #153
    if (!std::isfinite(st.viewportRollDeg))
      st.viewportRollDeg = 0.f;
    // The UCS (REQ-154). A full frame wins; `ucsElevation` alone is what every drawing saved before
    // the UCS command carries, and still loads as the elevated world-parallel plane it described.
    // A frame that does not survive its own validity check is DISCARDED rather than adopted: a
    // hand-edited or truncated basis would silently skew every coordinate the user then entered,
    // and falling back to the WCS is the one outcome that cannot be wrong (REQ-201).
    auto readVec = [](const json& j, const char* key, ray3d::Vec3 fallback) {
      const auto it = j.find(key);
      if (it == j.end() || !it->is_array() || it->size() != 3)
        return fallback;
      return ray3d::Vec3{(*it)[0].get<double>(), (*it)[1].get<double>(), (*it)[2].get<double>()};
    };
    auto readUcs = [&](const json& j, ucs::Ucs* out) {
      ucs::Ucs u;
      u.origin = readVec(j, "origin", {0.0, 0.0, 0.0});
      u.xAxis = readVec(j, "xAxis", {1.0, 0.0, 0.0});
      u.yAxis = readVec(j, "yAxis", {0.0, 1.0, 0.0});
      u.zAxis = readVec(j, "zAxis", {0.0, 0.0, 1.0});
      if (!std::isfinite(u.origin.x) || !std::isfinite(u.origin.y) || !std::isfinite(u.origin.z))
        return false;
      if (!ucs::IsRightHandedOrthonormal(u, 1e-6))
        return false;
      *out = u;
      return true;
    };

    st.activeUcs = ucs::Ucs{};
    const auto ucsIt = view.find("ucs");
    if (ucsIt != view.end() && ucsIt->is_object()) {
      if (!readUcs(*ucsIt, &st.activeUcs))
        st.activeUcs = ucs::Ucs{};
    } else {
      st.activeUcs.origin.z = view.value("ucsElevation", 0.0);
    }
    st.ucsFollow = view.value("ucsFollow", false);
    st.ucsNamed.clear();
    const auto namedIt = view.find("namedUcs");
    if (namedIt != view.end() && namedIt->is_array()) {
      for (const auto& entry : *namedIt) {
        if (!entry.is_object())
          continue;
        NamedUcs n;
        n.name = entry.value("name", std::string());
        if (n.name.empty() || !readUcs(entry, &n.frame))
          continue;
        st.ucsNamed.push_back(std::move(n));
      }
    }

    // Named views (REQ-106). A view whose numbers are not finite is DISCARDED rather than loaded:
    // restoring it would send the camera somewhere it cannot come back from, and losing one saved
    // bookmark is a far smaller harm than a drawing you cannot look at (REQ-201).
    st.namedViews.clear();
    const auto nvIt = view.find("namedViews");
    if (nvIt != view.end() && nvIt->is_array()) {
      for (const auto& entry : *nvIt) {
        if (!entry.is_object())
          continue;
        NamedView v;
        v.name = entry.value("name", std::string());
        if (v.name.empty())
          continue;
        v.panX = entry.value("panX", 0.0);
        v.panY = entry.value("panY", 0.0);
        v.panZ = entry.value("panZ", 0.0);
        v.zoom = entry.value("zoom", 1.f);
        v.azimuthDeg = entry.value("azimuthDeg", 0.f);
        v.elevationDeg = std::clamp(entry.value("elevationDeg", 90.f), -90.f, 90.f);
        v.rollDeg = entry.value("rollDeg", 0.f);  // #153
        if (!std::isfinite(v.panX) || !std::isfinite(v.panY) || !std::isfinite(v.panZ) ||
            !std::isfinite(v.zoom) || v.zoom <= 0.f || !std::isfinite(v.azimuthDeg) ||
            !std::isfinite(v.rollDeg))
          continue;
        const auto vu = entry.find("ucs");
        if (vu != entry.end() && vu->is_object() && !readUcs(*vu, &v.ucs))
          v.ucs = ucs::Ucs{};  // a broken frame falls back to World, as the active one does
        st.namedViews.push_back(std::move(v));
      }
    }
    st.activeViewName = view.value("activeViewName", std::string());
    // A name that no longer resolves is dropped rather than shown: the ribbon would otherwise claim
    // the camera is sitting in a view the drawing does not contain.
    if (!st.activeViewName.empty() && !FindNamedView(st, st.activeViewName))
      st.activeViewName.clear();
  } else {
    const int fbW = std::max(st.viewportLastFbW, 1);
    const int fbH = std::max(st.viewportLastFbH, 1);
    CadCoord::FitViewportToDrawing(st, static_cast<float>(fbW) / static_cast<float>(fbH), fbW, fbH);
  }

  // Survey-point ↔ label links (REQ-076). Ids must exist before either branch runs: the legacy path
  // writes an annotation's id into a point, and the current path validates against ids.
  EnsureAttrCounts(st);
  EnsureEntityIds(st);

  // A file with no document-level "nextEntityId" predates REQ-076, so every label link in it is an
  // array index. One flag for the whole file rather than a per-annotation marker, because the format
  // changed as a unit — a file cannot be half-migrated.
  const bool legacyIndexLinks = !doc.contains("nextEntityId");

  if (legacyIndexLinks) {
    // Rebuild both halves from the annotation side. That direction alone is sufficient, and using
    // only one source means the two cannot contradict each other — which the old index pair could,
    // and which is why the code below it existed at all.
    for (SurveyPoint& p : st.surveyPoints)
      p.labelMtextAnnId = 0;
    int migrated = 0;
    int skippedNotMtext = 0;
    int skippedBadIndex = 0;
    int skippedNoId = 0;
    for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
      CadAnnotation& a = st.cadAnnotations[ai];
      const int legacyPointIndex = a.surveyPointLabelForId;  // still an index on this path
      a.surveyPointLabelForId = -1;
      if (legacyPointIndex < 0)
        continue;  // not a label at all — an ordinary annotation
      if (a.kind != CadAnnotation::Kind::Mtext) {
        ++skippedNotMtext;
        continue;
      }
      if (static_cast<size_t>(legacyPointIndex) >= st.surveyPoints.size()) {
        ++skippedBadIndex;
        continue;
      }
      const std::uint64_t annId = st.cadAnnotationAttrs[ai].id;
      if (annId == 0) {
        // Would silently produce an unlinked label and a duplicate on the next step (REQ-201).
        ++skippedNoId;
        continue;
      }
      SurveyPoint& p = st.surveyPoints[static_cast<size_t>(legacyPointIndex)];
      a.surveyPointLabelForId = p.id;
      p.labelMtextAnnId = annId;
      ++migrated;
    }
    if (migrated > 0 || skippedNotMtext > 0 || skippedBadIndex > 0 || skippedNoId > 0) {
      std::string msg = "Migrated " + std::to_string(migrated) +
                        " survey-point label link(s) from the pre-REQ-076 format.";
      if (skippedNotMtext > 0)
        msg += " Skipped " + std::to_string(skippedNotMtext) + " (annotation is not MTEXT).";
      if (skippedBadIndex > 0)
        msg += " Skipped " + std::to_string(skippedBadIndex) + " (point index out of range).";
      if (skippedNoId > 0)
        msg += " Skipped " + std::to_string(skippedNoId) + " (annotation had no entity id).";
      log.push_back(msg);
    }
    return;
  }

  // Current files: drop any half-link. Both directions must agree, or neither is trusted — a label
  // pointing at a point that does not point back is an orphan, not a label.
  for (SurveyPoint& p : st.surveyPoints) {
    const int ai = FindSurveyLabelAnnIndex(st, p);
    if (ai < 0 || st.cadAnnotations[static_cast<size_t>(ai)].kind != CadAnnotation::Kind::Mtext ||
        st.cadAnnotations[static_cast<size_t>(ai)].surveyPointLabelForId != p.id)
      p.labelMtextAnnId = 0;
  }
  for (size_t ai = 0; ai < st.cadAnnotations.size(); ++ai) {
    CadAnnotation& a = st.cadAnnotations[ai];
    if (a.surveyPointLabelForId < 0)
      continue;
    const int pi = SurveyPointIndexForId(st, a.surveyPointLabelForId);
    if (pi < 0 || st.surveyPoints[static_cast<size_t>(pi)].labelMtextAnnId != st.cadAnnotationAttrs[ai].id)
      a.surveyPointLabelForId = -1;
  }
}

} // namespace

std::string SerializeGoSurveyJson(const AppCommandState& st) {
  return BuildRoot(st).dump(2);
}

bool SaveGoSurveyFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (!pathUtf8 || !pathUtf8[0]) {
    log.push_back("Save .gs: empty path.");
    return false;
  }
  try {
    std::ofstream f(std::filesystem::path(pathUtf8), std::ios::binary);
    if (!f) {
      log.push_back(std::string("Could not open for write: ") + pathUtf8);
      return false;
    }
    f << SerializeGoSurveyJson(st);
    log.push_back(std::string("Saved GoSurvey workspace (.gs): ") + pathUtf8);
    return true;
  } catch (const std::exception& e) {
    log.push_back(std::string("Save .gs failed: ") + e.what());
    return false;
  }
}

bool LoadGoSurveyFromJsonUtf8(AppCommandState& st, std::string_view jsonUtf8, std::vector<std::string>& log) {
  json root;
  try {
    root = json::parse(std::string(jsonUtf8));
  } catch (const std::exception& e) {
    log.push_back(std::string("Parse GoSurvey document failed: ") + e.what());
    return false;
  }
  if (!root.is_object() || !root.contains("format") || root["format"] != "gosurvey") {
    log.push_back(".gs: not a GoSurvey file (missing format \"gosurvey\").");
    return false;
  }
  // REQ-079 / ADR-030: accept any version at or below this build's, migrating older documents
  // forward. This used to be `!= kGsFormatVersion`, which meant bumping the version would have
  // made every existing drawing unopenable — the reason the field sat at 1 while eleven changes
  // were routed around it via the tolerant-key pattern.
  if (!root.contains("version") || !root["version"].is_number_integer()) {
    log.push_back(".gs: missing or malformed format version.");
    return false;
  }
  if (!root.contains("document") || !root["document"].is_object()) {
    log.push_back(".gs: missing document object.");
    return false;
  }
  {
    const int   fileVersion = root["version"].get<int>();
    std::string migrateErr;
    // Migration happens in memory, on the parsed tree, BEFORE the typed loader runs. The file on
    // disk is untouched until the user saves (ADR-030 (3)).
    if (!MigrateGsDocument(root["document"], fileVersion, kGsFormatVersion, log, migrateErr)) {
      log.push_back(".gs: " + migrateErr);
      return false;
    }
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
    int recreated = 0;
    for (size_t i = 0; i < st.surveyPoints.size(); ++i) {
      if (st.surveyPoints[i].labelStyle != SurveyPointLabelStyle::None &&
          FindSurveyLabelAnnIndex(st, st.surveyPoints[i]) < 0) {
        EnsureSurveyPointLabelMtext(st, i, &log);
        ++recreated;
      }
    }
    // Loud on purpose (REQ-201): a point whose label link did not survive the load gets a *new*
    // label, and if the old one is still in the drawing the user sees two. Silence here is what
    // makes that look like a rendering bug instead of a link that failed to resolve.
    if (recreated > 0)
      log.push_back("Recreated " + std::to_string(recreated) +
                    " survey-point label(s) whose link did not resolve on load.");
    RepositionAllSurveyPointLabels(st);
    BumpCadGpuCache(st);
    log.push_back("Opened GoSurvey document.");
    return true;
  } catch (const std::exception& e) {
    log.push_back(std::string("Load GoSurvey document failed: ") + e.what());
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
  const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (!LoadGoSurveyFromJsonUtf8(st, bytes, log))
    return false;
  log.push_back(std::string("Opened GoSurvey workspace (.gs): ") + pathUtf8);
  return true;
}
