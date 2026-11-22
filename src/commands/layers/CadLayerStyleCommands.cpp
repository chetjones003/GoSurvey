#include "CadCommandsInternal.hpp"
#include "layers/CadLayerInternal.hpp"
#include "CadColor.hpp"
#include "CadLinetype.hpp"
#include "TextStyle.hpp"
#include "ProjectSettings.hpp"
#include "GoSurvey/Document/PlotScales.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

void EnsureAttrCounts(AppCommandState& st) {
  bool grew = false;
  const size_t nl = st.drawing.lineCount();
  while (st.drawing.userLineAttrs.size() < nl) {
    st.drawing.userLineAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  const size_t nc = st.drawing.circleCount();
  while (st.drawing.userCircleAttrs.size() < nc) {
    st.drawing.userCircleAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  // The normal side-car is repaired the same way, but growing it is not a NEW attribute: a
  // missing normal is the flat default, not a fresh entity, so it does not set grew.
  EnsureCircleNormals(st.drawing.userCircleNormals, nc);
  while (st.drawing.userArcAttrs.size() < st.drawing.userArcs.size()) {
    st.drawing.userArcAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.drawing.userEllAttrs.size() < st.drawing.userEllipses.size()) {
    st.drawing.userEllAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  const size_t np = st.drawing.userPolylineOffsets.size() > 0 ? st.drawing.userPolylineOffsets.size() - 1 : 0;
  while (st.drawing.userPolylineAttrs.size() < np) {
    st.drawing.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  const size_t na = st.drawing.cadAnnotations.size();
  while (st.drawing.cadAnnotationAttrs.size() < na) {
    st.drawing.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.drawing.cadSurfaceAttrs.size() < st.drawing.cadSurfaces.size()) {  // REQ-068
    st.drawing.cadSurfaceAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.drawing.cadSolidAttrs.size() < st.drawing.cadSolids.size()) {  // REQ-313
    st.drawing.cadSolidAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.drawing.cadPipeRunAttrs.size() < st.drawing.cadPipeRuns.size()) {  // issue #486
    st.drawing.cadPipeRunAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.drawing.cadTableAttrs.size() < st.drawing.cadTables.size()) {
    st.drawing.cadTableAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.drawing.cadPositionMarkerAttrs.size() < st.drawing.cadPositionMarkers.size()) {  // REQ-359
    st.drawing.cadPositionMarkerAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.drawing.cadMultileaderAttrs.size() < st.drawing.cadMultileaders.size()) {  // REQ-367
    st.drawing.cadMultileaderAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.drawing.cadSplineAttrs.size() < st.drawing.cadSplines.size()) {  // REQ-443
    st.drawing.cadSplineAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.drawing.cadPointAttrs.size() < st.drawing.cadPoints.size()) {  // REQ-456
    st.drawing.cadPointAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  while (st.drawing.cadBlockRefAttrs.size() < st.drawing.cadBlockRefs.size()) {
    st.drawing.cadBlockRefAttrs.push_back(MakeNewEntityAttrs(st));
    grew = true;
  }
  if (grew)
    BumpCadGpuCache(st);
}

static void CollectLayersUsedInDrawing(const AppCommandState& st, std::set<std::string>* out) {
  out->insert("0");
  auto add = [&](const std::string& s) {
    if (!s.empty())
      out->insert(s);
  };
  for (const auto& a : st.drawing.userLineAttrs)
    add(a.layer);
  for (const auto& a : st.drawing.userCircleAttrs)
    add(a.layer);
  for (const auto& a : st.drawing.userArcAttrs)
    add(a.layer);
  for (const auto& a : st.drawing.userEllAttrs)
    add(a.layer);
  for (const auto& a : st.drawing.userPolylineAttrs)
    add(a.layer);
  for (const auto& a : st.drawing.cadAnnotationAttrs)
    add(a.layer);
  for (const auto& a : st.drawing.cadTableAttrs)
    add(a.layer);
  for (const auto& a : st.drawing.cadPositionMarkerAttrs)  // REQ-359
    add(a.layer);
  for (const auto& a : st.drawing.cadMultileaderAttrs)  // REQ-367
    add(a.layer);
  for (const auto& a : st.drawing.cadSplineAttrs)  // REQ-443
    add(a.layer);
  for (const auto& a : st.drawing.cadPointAttrs)  // REQ-456
    add(a.layer);
  for (const auto& a : st.drawing.cadBlockRefAttrs)
    add(a.layer);
  // REQ-352: every store a layer edit can write to, so a name typed for a solid alone still
  // becomes a row the Layer Manager can turn off.
  for (const std::vector<EntityAttributes>* v :
       {&st.drawing.cadSolidAttrs, &st.drawing.cadPipeRunAttrs, &st.drawing.cadFilledRegionAttrs, &st.drawing.cadMeshAttrs,
        &st.drawing.featureLineAttrs, &st.drawing.cadSurfaceAttrs, &st.drawing.cadPointCloudAttrs})
    for (const auto& a : *v)
      add(a.layer);
  for (const auto& p : st.drawing.surveyPoints)
    add(p.layer);
  add(st.drawing.currentLayer);
}

void SyncDrawingLayerTableWithGeometry(AppCommandState& st) {
  if (st.drawing.drawingLayerTable.empty()) {
    CadLayerRow z;
    z.name = "0";
    st.drawing.drawingLayerTable.push_back(z);
  }
  std::set<std::string> have;
  for (const auto& r : st.drawing.drawingLayerTable)
    have.insert(r.name);
  std::set<std::string> used;
  CollectLayersUsedInDrawing(st, &used);
  for (const std::string& name : used) {
    if (!have.count(name)) {
      CadLayerRow row;
      row.name = name;
      st.drawing.drawingLayerTable.push_back(row);
      have.insert(name);
    }
  }
  if (st.drawing.currentLayer.empty())
    st.drawing.currentLayer = "0";
  if (!have.count(st.drawing.currentLayer))
    st.drawing.currentLayer = "0";
  // Text styles (REQ-044): guarantee the baseline "Standard" style and a valid active style.
  TextStyles::EnsureStandard(st.drawing.textStyles);
  if (st.drawing.activeTextStyleName.empty() || !TextStyles::Find(st.drawing.textStyles, st.drawing.activeTextStyleName))
    st.drawing.activeTextStyleName = TextStyles::kStandardName;
  // Surface styles (REQ-070): the same guarantee, and it is what a legacy `internal document payload` relies on — a file
  // written before this table existed has no styles section at all, and every surface in it carries
  // an empty styleName that must resolve to something drawable (ADR-036 (d)). Left to the loader
  // alone it would hold for `internal document payload` and not for DXF import or a fresh document, so it lives in the one
  // normalisation pass every route to a drawing already goes through.
  SurfaceStyles::EnsureStandard(st.drawing.surfaceStyles);
}

const TextStyle* ActiveTextStyle(const AppCommandState& st) {
  const std::string name = st.drawing.activeTextStyleName.empty() ? std::string(TextStyles::kStandardName)
                                                          : st.drawing.activeTextStyleName;
  if (const TextStyle* s = TextStyles::Find(st.drawing.textStyles, name)) return s;
  return TextStyles::Find(st.drawing.textStyles, TextStyles::kStandardName);
}

void SetActiveTextStyle(AppCommandState& st, const std::string& name) {
  st.drawing.activeTextStyleName = name;
  if (const TextStyle* s = ActiveTextStyle(st))
    st.drawing.defaultPlottedTextHeightInches = std::max(s->heightInches, 1.e-6f);
}

// Stamp the active text style onto a freshly built user TEXT/MTEXT (REQ-044 create path). Font/oblique/
// bold/italic come from the style; height is left as the caller set it (it already equals the style height
// via the SetActiveTextStyle → defaultPlottedTextHeightInches sync), and any height typed at the TEXT
// prompt is preserved. Per-property overrides land in a later phase.
void StampActiveTextStyleOnNewText(AppCommandState& st, CadAnnotation& a) {
  const TextStyle* s = ActiveTextStyle(st);
  if (!s) return;
  a.styleName = s->name;
  a.ovFont = a.ovHeight = a.ovOblique = a.ovBold = a.ovItalic = false;
  a.fontFamily = s->fontFamily;
  a.obliqueDeg = s->obliqueDeg;
  a.bold = s->bold;
  a.italic = s->italic;
  // The style's Annotative setting (D-2026-11-06-a): new text made with an annotative style is annotative.
  // An object already annotative (survey and position marker labels) stays so.
  a.annotative = a.annotative || StyleFlag(s->props, "annotative");
}



static bool LayerNameExistsCi(const AppCommandState& st, const std::string& name) {
  const std::string nl = StringUtil::toLowerAsciiCopy(name);
  for (const auto& r : st.drawing.drawingLayerTable) {
    if (StringUtil::toLowerAsciiCopy(r.name) == nl)
      return true;
  }
  return false;
}

bool CadAddDrawingLayer(AppCommandState& st, const std::string& raw, std::string* err) {
  const std::string name = StringUtil::trimCopy(raw);
  if (!ValidNewLayerNameChars(name)) {
    if (err)
      *err = "Invalid layer name (empty, too long, or reserved characters).";
    return false;
  }
  SyncDrawingLayerTableWithGeometry(st);
  if (LayerNameExistsCi(st, name)) {
    if (err)
      *err = "Layer already exists.";
    return false;
  }
  PushUndoSnapshot(st, "Add layer");
  CadLayerRow row;
  row.name = name;
  st.drawing.drawingLayerTable.push_back(row);
  return true;
}

// ---------------------------------------------------------------------------
// Object Layers (REQ-361, GitHub issue #582 increment 5)
// ---------------------------------------------------------------------------

std::string ResolveObjectLayer(const DrawingSettings& settings, ObjectLayerKind kind, std::string_view objectName) {
  const ObjectLayerRow& row = settings.ObjectLayer(kind);
  if (row.modifier == ObjectLayerRow::Modifier::None)
    return row.layer;
  std::string value;  // each '*' is the object's name; an object with no name drops it
  for (char c : row.value) {
    if (c == '*')
      value.append(objectName);
    else
      value.push_back(c);
  }
  return row.modifier == ObjectLayerRow::Modifier::Prefix ? value + row.layer : row.layer + value;
}

std::string EnsureObjectLayer(AppCommandState& st, ObjectLayerKind kind, std::string_view objectName) {
  std::string name = StringUtil::trimCopy(ResolveObjectLayer(st.drawing.drawingSettings, kind, objectName));
  // A name that cannot be a layer (an object name holding '/', say) falls back to the row's own layer.
  if (!ValidNewLayerNameChars(name))
    name = StringUtil::trimCopy(st.drawing.drawingSettings.ObjectLayer(kind).layer);
  if (!ValidNewLayerNameChars(name))
    name = "0";
  return EnsureDrawingLayer(st, name);
}

std::string EnsureDrawingLayer(AppCommandState& st, const std::string& name) {
  const std::string lower = StringUtil::toLowerAsciiCopy(name);
  for (const CadLayerRow& r : st.drawing.drawingLayerTable)
    if (StringUtil::toLowerAsciiCopy(r.name) == lower)
      return r.name;  // the table's own spelling
  CadLayerRow row;  // created with default properties, inside the caller's undo step
  row.name = name;
  st.drawing.drawingLayerTable.push_back(row);
  return name;
}

std::string ValidateObjectLayers(const DrawingSettings& settings) {
  for (const ObjectLayerRow& row : settings.objectLayers) {
    const std::string name = StringUtil::trimCopy(row.layer);
    if (!ValidNewLayerNameChars(name))
      return "Object Layers: \"" + name +
             "\" is not a layer name (empty, too long, or a character such as / * : ? \" < > |).";
  }
  return {};
}

EntityAttributes MakeNewObjectAttrs(AppCommandState& st, ObjectLayerKind kind, std::string_view objectName) {
  EntityAttributes a = MakeNewEntityAttrs(st);  // the current colour still applies (REQ-356)
  a.layer = EnsureObjectLayer(st, kind, objectName);
  return a;
}

void AppendNewObjectAttrs(AppCommandState& st, std::vector<EntityAttributes>& attrs, size_t count,
                          ObjectLayerKind kind, std::string_view objectName) {
  while (attrs.size() + 1 < count)  // anything older that lacks attributes gets the ordinary ones
    attrs.push_back(MakeNewEntityAttrs(st));
  if (attrs.size() < count)
    attrs.push_back(MakeNewObjectAttrs(st, kind, objectName));
}


bool CadRenameDrawingLayer(AppCommandState& st, const std::string& oldNameRaw, const std::string& newNameRaw,
                           std::string* err) {
  const std::string oldN = StringUtil::trimCopy(oldNameRaw);
  const std::string newN = StringUtil::trimCopy(newNameRaw);
  if (oldN == "0") {
    if (err)
      *err = "Layer 0 cannot be renamed.";
    return false;
  }
  if (!ValidNewLayerNameChars(newN)) {
    if (err)
      *err = "Invalid new layer name.";
    return false;
  }
  if (newN == oldN)
    return true;
  if (LayerNameExistsCi(st, newN) && StringUtil::toLowerAsciiCopy(newN) != StringUtil::toLowerAsciiCopy(oldN)) {
    if (err)
      *err = "A layer with that name already exists.";
    return false;
  }
  auto it = std::find_if(st.drawing.drawingLayerTable.begin(), st.drawing.drawingLayerTable.end(),
                         [&](const CadLayerRow& r) { return r.name == oldN; });
  if (it == st.drawing.drawingLayerTable.end()) {
    if (err)
      *err = "Layer not found in table.";
    return false;
  }
  PushUndoSnapshot(st, "Rename layer");
  auto reassign = [&](std::string& L) {
    if (L == oldN)
      L = newN;
  };
  for (auto& a : st.drawing.userLineAttrs)
    reassign(a.layer);
  for (auto& a : st.drawing.userCircleAttrs)
    reassign(a.layer);
  for (auto& a : st.drawing.userArcAttrs)
    reassign(a.layer);
  for (auto& a : st.drawing.userEllAttrs)
    reassign(a.layer);
  for (auto& a : st.drawing.userPolylineAttrs)
    reassign(a.layer);
  for (auto& a : st.drawing.cadAnnotationAttrs)
    reassign(a.layer);
  for (auto& p : st.drawing.surveyPoints)
    reassign(p.layer);
  if (st.drawing.currentLayer == oldN)
    st.drawing.currentLayer = newN;
  it->name = newN;
  BumpCadGpuCache(st);
  return true;
}

bool CadDeleteDrawingLayer(AppCommandState& st, const std::string& nameRaw, std::string* err) {
  const std::string name = StringUtil::trimCopy(nameRaw);
  if (name == "0") {
    if (err)
      *err = "Layer 0 cannot be deleted.";
    return false;
  }
  const auto itRow = std::find_if(st.drawing.drawingLayerTable.begin(), st.drawing.drawingLayerTable.end(),
                                  [&](const CadLayerRow& r) { return r.name == name; });
  if (itRow == st.drawing.drawingLayerTable.end()) {
    if (err)
      *err = "Layer not found.";
    return false;
  }
  PushUndoSnapshot(st, "Delete layer");
  auto reassign = [&](std::string& L) {
    if (L == name)
      L = "0";
  };
  for (auto& a : st.drawing.userLineAttrs)
    reassign(a.layer);
  for (auto& a : st.drawing.userCircleAttrs)
    reassign(a.layer);
  for (auto& a : st.drawing.userArcAttrs)
    reassign(a.layer);
  for (auto& a : st.drawing.userEllAttrs)
    reassign(a.layer);
  for (auto& a : st.drawing.userPolylineAttrs)
    reassign(a.layer);
  for (auto& a : st.drawing.cadAnnotationAttrs)
    reassign(a.layer);
  for (auto& p : st.drawing.surveyPoints)
    reassign(p.layer);
  if (st.drawing.currentLayer == name)
    st.drawing.currentLayer = "0";
  st.drawing.drawingLayerTable.erase(itRow);
  BumpCadGpuCache(st);
  return true;
}


// ---------------------------------------------------------------------------
// Drawing Settings (REQ-357, GitHub issue #582)
// ---------------------------------------------------------------------------

void SetDrawingPlotScale(AppCommandState& st, float modelUnitsPerPlottedInch) {
  st.drawing.modelUnitsPerPlottedInch = modelUnitsPerPlottedInch;
  RepositionAllSurveyPointLabels(st);
  st.surveyLabelLayoutCacheHalfH = st.viewportLastSurveyLayoutOrthoHalfH;
  st.surveyLabelLayoutCacheVpHeightPx = st.viewportLastSurveyLayoutHeightPx;
  st.surveyLabelLayoutCacheMup = st.drawing.modelUnitsPerPlottedInch;
  BumpCadGpuCache(st);
}

void SyncCurrentAnnotationScaleIndex(AppCommandState& st) {
  st.currentAnnotationScaleIndex = -1;
  if (st.drawing.annotationScales.empty())
    return;
  int best = 0;
  float bestDiff = 1.e30f;
  for (int i = 0; i < static_cast<int>(st.drawing.annotationScales.size()); ++i) {
    const float m = CadAnnotationScaleModelUnitsPerPlottedInch(st.drawing.annotationScales[static_cast<size_t>(i)]);
    const float d = std::fabs(m - st.drawing.modelUnitsPerPlottedInch);
    if (d < bestDiff) {
      bestDiff = d;
      best = i;
    }
  }
  st.currentAnnotationScaleIndex = best;
}

void SetCurrentAnnotationScaleIndex(AppCommandState& st, int index) {
  if (st.drawing.annotationScales.empty()) {
    st.currentAnnotationScaleIndex = -1;
    return;
  }
  const int n = static_cast<int>(st.drawing.annotationScales.size());
  const int ix = std::clamp(index, 0, n - 1);
  if (ix == st.currentAnnotationScaleIndex)
    return;
  st.currentAnnotationScaleIndex = ix;
  BumpCadGpuCache(st);
}

bool ApplyDrawingSettings(AppCommandState& st, int drawingInsUnits, float modelUnitsPerPlottedInch,
                          const DrawingSettings& settings, std::vector<std::string>& log) {
  if (!std::isfinite(modelUnitsPerPlottedInch) || modelUnitsPerPlottedInch <= 0.f) {
    log.push_back("Drawing Settings — the scale must be a positive number; nothing was changed.");
    return false;
  }
  // REQ-359 item 4: the geographic marker belongs to the zone; without a zone it is the default.
  DrawingSettings next = settings;
  if (!next.Geolocated()) {
    next.ResetGeographicMarker();
    next.transform = DrawingSettings::Transform{};  // REQ-360: the transform belongs to the zone too
  }
  if (const std::string bad = ValidateObjectLayers(next); !bad.empty()) {  // REQ-361
    log.push_back("Drawing Settings — " + bad + " Nothing was changed.");
    return false;
  }
  if (const std::string bad = ValidateDrawingTransform(next); !bad.empty()) {
    log.push_back("Drawing Settings — " + bad + " Nothing was changed.");
    return false;
  }
  const bool unitsChanged = drawingInsUnits != st.drawing.drawingInsUnits;
  const bool scaleChanged = modelUnitsPerPlottedInch != st.drawing.modelUnitsPerPlottedInch;
  const bool settingsChanged = next != st.drawing.drawingSettings;
  if (!unitsChanged && !scaleChanged && !settingsChanged)
    return true;
  PushUndoSnapshot(st, "Drawing Settings");
  st.drawing.drawingInsUnits = drawingInsUnits;  // a relabel: no coordinate is touched (REQ-022)
  st.drawing.drawingSettings = next;
  if (scaleChanged)
    SetDrawingPlotScale(st, modelUnitsPerPlottedInch);
  BumpCadGpuCache(st);  // document property: marks the drawing modified
  log.push_back("Drawing Settings applied.");
  return true;
}


void ResolveStoredColorForViewport(const std::string& colorStorage, float transparency, float defaultR,
                                  float defaultG, float defaultB, float* outRgba) {
  const float tr = transparency < 0.f ? 0.f : std::clamp(transparency, 0.f, 1.f);
  const float alpha = 1.f - tr;
  const std::string& c = colorStorage;

  if (c.empty() || c == "ByLayer") {
    outRgba[0] = defaultR;
    outRgba[1] = defaultG;
    outRgba[2] = defaultB;
    outRgba[3] = alpha;
    return;
  }
  float rgb[3];
  CadColorResolveRgb(c, defaultR, defaultG, defaultB, rgb);
  outRgba[0] = rgb[0];
  outRgba[1] = rgb[1];
  outRgba[2] = rgb[2];
  outRgba[3] = alpha;
}

static bool LayerNamesEqCi(const std::string& a, const std::string& b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

const CadLayerRow* FindDrawingLayerRowCi(const AppCommandState& st, const std::string& layerName) {
  for (const auto& r : st.drawing.drawingLayerTable) {
    if (LayerNamesEqCi(r.name, layerName))
      return &r;
  }
  return nullptr;
}

float EffectiveEntityTransparency01(const EntityAttributes& e, const CadLayerRow* layer) {
  if (e.transparency >= 0.f)
    return std::clamp(e.transparency, 0.f, 1.f);
  if (layer)
    return std::clamp(layer->transparency, 0.f, 1.f);
  return 0.f;
}

float EffectiveEntityLineweightMm(const EntityAttributes& e, const CadLayerRow* layer) {
  if (e.lineweightMm >= 0.f)
    return e.lineweightMm;
  if (layer && layer->lineweightMm >= 0.f)
    return layer->lineweightMm;
  return 0.09f;
}

std::string EffectiveEntityLinetypeNameForViewport(const EntityAttributes& e, const CadLayerRow* layer) {
  const std::string c = CadCanonicalLinetypeNameForDxf(e.linetype);
  if (c.empty() || c == "ByLayer")
    return layer ? layer->linetype : std::string("Continuous");
  return e.linetype;
}

void ResolveEntityRgbaForViewport(const EntityAttributes& attr, const CadLayerRow* layer, float defaultR,
                                  float defaultG, float defaultB, float* outRgba) {
  const float tr = EffectiveEntityTransparency01(attr, layer);
  std::string col = attr.color;
  if (col.empty() || col == "ByLayer") {
    if (layer && !layer->color.empty() && layer->color != "ByLayer")
      col = layer->color;
  }
  ResolveStoredColorForViewport(col, tr, defaultR, defaultG, defaultB, outRgba);
}

void ApplyMaterialDiffuseForShaded(const EntityAttributes& attr, float rgba[4]) {
  assert(rgba != nullptr);
  if (!attr.materialDiffuseOverride)
    return;
  rgba[0] = attr.materialDiffuseR;
  rgba[1] = attr.materialDiffuseG;
  rgba[2] = attr.materialDiffuseB;
}

struct DxfLwPair {
  int code;
  float mm;
};

static const DxfLwPair kDxfLwTable[] = {
    {0, 0.f},     {5, 0.05f},   {9, 0.09f},   {13, 0.13f},  {15, 0.15f},  {18, 0.18f},  {20, 0.20f},
    {25, 0.25f},  {30, 0.30f},  {35, 0.35f},  {40, 0.40f},  {50, 0.50f},  {53, 0.53f},  {60, 0.60f},
    {70, 0.70f},  {80, 0.80f},  {90, 0.90f},  {100, 1.00f}, {106, 1.06f}, {120, 1.20f}, {140, 1.40f},
    {158, 1.58f}, {200, 2.00f}, {211, 2.11f},
};

int CadDxfLineweightEnum370FromMm(float mm) {
  if (mm < 0.f)
    return -1;
  int best = 18;
  float bestD = 1e9f;
  for (const auto& e : kDxfLwTable) {
    const float d = std::fabs(e.mm - mm);
    if (d < bestD) {
      bestD = d;
      best = e.code;
    }
  }
  return best;
}

float CadDxfLineweightMmFromEnum370(int code) {
  if (code < 0)
    return -1.f;
  for (const auto& e : kDxfLwTable) {
    if (e.code == code)
      return e.mm;
  }
  return -1.f;
}
