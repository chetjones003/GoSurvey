#include "ProjectSettings.hpp"

#include "GsIo.hpp"

#include <cmath>
#include <nlohmann/json.hpp>

using nlohmann::json;

bool ParseProjectSettings(const std::string& settingsJson, ProjectSettings* out, std::string* err) {
  *out = ProjectSettings{};
  if (settingsJson.empty())
    return true;
  const json o = json::parse(settingsJson, nullptr, false);
  if (!o.is_object()) {
    if (err)
      *err = "the project's settings are not a JSON object";
    return false;
  }
  if (o.contains("linearUnits") && o["linearUnits"].is_number_integer())
    out->insUnits = o["linearUnits"].get<int>();
  if (o.contains("zone") && o["zone"].is_string())
    out->zoneCode = o["zone"].get<std::string>();
  if (o.contains("defaults") && o["defaults"].is_object()) {
    out->hasDefaults = DrawingSettingsFromJsonText(o["defaults"].dump(), &out->defaults);
    out->defaults.overridden = 0;
    // value<> would throw on a hand-edited non-number; a bad scale falls back to the standard 50.
    const float v = o.contains("plotScale") && o["plotScale"].is_number() ? o["plotScale"].get<float>() : 50.f;
    out->plotScale = std::isfinite(v) && v > 0.f ? v : 50.f;
  }
  return true;
}

std::string WriteProjectSettings(const std::string& existingJson, const ProjectSettings& p) {
  json o = json::parse(existingJson.empty() ? "{}" : existingJson, nullptr, false);
  if (!o.is_object())
    o = json::object();
  if (p.insUnits >= 0)
    o["linearUnits"] = p.insUnits;
  else
    o.erase("linearUnits");
  if (!p.zoneCode.empty())
    o["zone"] = p.zoneCode;
  else
    o.erase("zone");
  if (p.hasDefaults) {
    DrawingSettings d = p.defaults;
    d.overridden = 0;
    o["defaults"] = json::parse(DrawingSettingsToJsonText(d));
    o["plotScale"] = p.plotScale;
  } else {
    o.erase("defaults");
    o.erase("plotScale");
  }
  return o.dump();
}

void CopyProjectDefault(const ProjectSettings& p, ProjectDefaultKey key, float* plotScale, DrawingSettings* ds) {
  switch (key) {
  case ProjectDefaultKey::AngularUnits: ds->angularUnits = p.defaults.angularUnits; break;
  case ProjectDefaultKey::FootDefinition: ds->footDefinition = p.defaults.footDefinition; break;
  case ProjectDefaultKey::ScaleInsertedObjects: ds->scaleInsertedObjects = p.defaults.scaleInsertedObjects; break;
  case ProjectDefaultKey::SetDrawingVariables: ds->setDrawingVariables = p.defaults.setDrawingVariables; break;
  case ProjectDefaultKey::PlotScale: *plotScale = p.plotScale; break;
  case ProjectDefaultKey::ObjectLayers: ds->objectLayers = p.defaults.objectLayers; break;
  }
}

bool DiffersFromProjectDefault(const ProjectSettings& p, ProjectDefaultKey key, float plotScale,
                               const DrawingSettings& ds) {
  // Field by field, not "copy and compare": this runs for every project tab every frame, and a
  // DrawingSettings copy carries its captured map tiles.
  switch (key) {
  case ProjectDefaultKey::AngularUnits: return ds.angularUnits != p.defaults.angularUnits;
  case ProjectDefaultKey::FootDefinition: return ds.footDefinition != p.defaults.footDefinition;
  case ProjectDefaultKey::ScaleInsertedObjects: return ds.scaleInsertedObjects != p.defaults.scaleInsertedObjects;
  case ProjectDefaultKey::SetDrawingVariables: return ds.setDrawingVariables != p.defaults.setDrawingVariables;
  case ProjectDefaultKey::PlotScale: return plotScale != p.plotScale;
  case ProjectDefaultKey::ObjectLayers: return ds.objectLayers != p.defaults.objectLayers;
  }
  return false;
}

ProjectApplyResult ApplyProjectToDrawing(const ProjectSettings& p, int* insUnits, float* plotScale,
                                         DrawingSettings* ds) {
  ProjectApplyResult r;
  if (p.insUnits >= 0 && *insUnits != p.insUnits) {
    r.unitsReplaced = true;
    r.oldInsUnits = *insUnits;
    *insUnits = p.insUnits;
  }
  if (!p.zoneCode.empty() && ds->zoneCode != p.zoneCode) {
    r.zoneReplaced = true;
    r.oldZone = ds->zoneCode;
    ds->zoneCode = p.zoneCode;
    ds->ResetGeographicMarker();  // both belong to the zone
    ds->transform = DrawingSettings::Transform{};
  }
  bool inherited = false;
  if (p.hasDefaults) {
    for (unsigned k = 0; k < kProjectDefaultKeyCount; ++k) {
      const auto key = static_cast<ProjectDefaultKey>(k);
      if (ds->IsOverridden(key) || !DiffersFromProjectDefault(p, key, *plotScale, *ds))
        continue;
      CopyProjectDefault(p, key, plotScale, ds);
      inherited = true;
    }
  }
  r.changed = r.unitsReplaced || r.zoneReplaced || inherited;
  return r;
}

const ProjectSettings* ProjectSettingsForTab(const AppCommandState& st, int tabIdx) {
  if (tabIdx < 1 || tabIdx >= static_cast<int>(st.drawingTabs.size()))
    return nullptr;
  const uint32_t uid = st.drawingTabs[static_cast<size_t>(tabIdx)].projectUid;
  if (uid == 0)
    return nullptr;
  for (const auto& s : st.openProjects)
    if (s.uid == uid)
      return s.settings.get();
  return nullptr;
}

void NoteUserPlotScale(AppCommandState& st) {
  const ProjectSettings* p = ProjectSettingsForTab(st, st.activeDrawingIdx);
  if (p && p->hasDefaults)
    st.drawingSettings.SetOverridden(ProjectDefaultKey::PlotScale,
                                     DiffersFromProjectDefault(*p, ProjectDefaultKey::PlotScale,
                                                               st.modelUnitsPerPlottedInch, st.drawingSettings));
}

void EnforceProjectSettings(AppCommandState& st, std::vector<std::string>& log) {
  if (st.openProjects.empty())
    return;
  for (int i = 1; i < static_cast<int>(st.drawingTabs.size()); ++i) {
    const ProjectSettings* p = ProjectSettingsForTab(st, i);
    if (!p)
      continue;
    const bool active = i == st.activeDrawingIdx;
    if (!active && i >= static_cast<int>(st.documents.size()))
      continue;
    int*             units = active ? &st.drawingInsUnits : &st.documents[static_cast<size_t>(i)].drawingInsUnits;
    float*           scale = active ? &st.modelUnitsPerPlottedInch
                                    : &st.documents[static_cast<size_t>(i)].modelUnitsPerPlottedInch;
    DrawingSettings* ds = active ? &st.drawingSettings : &st.documents[static_cast<size_t>(i)].drawingSettings;
    const float      scaleBefore = *scale;
    const ProjectApplyResult r = ApplyProjectToDrawing(*p, units, scale, ds);
    if (!r.changed)
      continue;
    if (active && *scale != scaleBefore) {  // resizes survey-point labels exactly as setting it does
      const float want = *scale;
      *scale = scaleBefore;
      SetDrawingPlotScale(st, want);
    }
    const std::string& tab = st.drawingTabs[static_cast<size_t>(i)].name;
    if (r.zoneReplaced)
      log.push_back("Project settings - \"" + tab + "\" used coordinate system " +
                    (r.oldZone.empty() ? std::string("(none)") : r.oldZone) + "; the project's " + p->zoneCode +
                    " is now used. Nothing was moved.");
    if (r.unitsReplaced)
      log.push_back("Project settings - \"" + tab + "\" used drawing unit code " + std::to_string(r.oldInsUnits) +
                    "; the project's (" + std::to_string(p->insUnits) + ") is now used. Nothing was scaled.");
  }
}
