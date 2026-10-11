// REQ-375 (issue #696 P2) — a project's enforced coordinate system and unit, its inheritable
// defaults, and per-drawing overrides. Pure: settings structs and AppCommandState, no window.

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "ProjectSettings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

ProjectSettings MakeProject() {
  ProjectSettings p;
  p.insUnits = 6;  // Meters
  p.zoneCode = "HARN/TX.TX-C";
  p.hasDefaults = true;
  p.plotScale = 100.f;
  p.defaults.angularUnits = DrawingSettings::AngularUnits::Grads;
  p.defaults.footDefinition = DrawingSettings::FootDefinition::International;
  p.defaults.scaleInsertedObjects = false;
  return p;
}

/// Tab 1 (active) and tab 2 (stored in documents[2]) both belong to project uid 7.
void JoinTwoTabs(AppCommandState& st, const ProjectSettings& p) {
  AppCommandState::ProjectSession s;
  s.uid = 7;
  s.settings = std::make_shared<ProjectSettings>(p);
  st.openProjects.push_back(std::move(s));
  st.drawingTabs[1].projectUid = 7;
  st.drawingTabs.push_back({"Second", 99u, 7u});
  st.documents.emplace_back();
  st.activeDrawingIdx = 1;
}

}  // namespace

TEST_CASE("req375 an empty project enforces nothing and supplies no defaults", "[req375]") {
  ProjectSettings p;
  std::string err;
  REQUIRE(ParseProjectSettings("", &p, &err));
  REQUIRE(ParseProjectSettings("{}", &p, &err));
  CHECK(p.insUnits == -1);
  CHECK(p.zoneCode.empty());
  CHECK_FALSE(p.hasDefaults);

  int units = 2;
  float scale = 50.f;
  DrawingSettings ds;
  ds.zoneCode = "HARN/TX.TX-N";
  ds.angularUnits = DrawingSettings::AngularUnits::Radians;
  CHECK_FALSE(ApplyProjectToDrawing(p, &units, &scale, &ds).changed);
  CHECK(ds.zoneCode == "HARN/TX.TX-N");
  CHECK(ds.angularUnits == DrawingSettings::AngularUnits::Radians);

  CHECK_FALSE(ParseProjectSettings("[1,2]", &p, &err));
  CHECK_FALSE(err.empty());

  // A hand-edited .gsproj with a nonsense scale must not throw or poison the project.
  REQUIRE(ParseProjectSettings(R"({"defaults":{},"plotScale":"big","linearUnits":"x"})", &p, &err));
  CHECK(p.plotScale == 50.f);
  CHECK(p.insUnits == -1);
}

TEST_CASE("req375 project settings round trip and keep members they do not own", "[req375]") {
  ProjectSettings p = MakeProject();
  p.defaults.objectLayers[0].layer = "MY-NODE";
  const std::string text = WriteProjectSettings(R"({"futureThing":{"a":1}})", p);

  ProjectSettings back;
  std::string err;
  REQUIRE(ParseProjectSettings(text, &back, &err));
  CHECK(back.insUnits == 6);
  CHECK(back.zoneCode == "HARN/TX.TX-C");
  CHECK(back.hasDefaults);
  CHECK(back.plotScale == 100.f);
  CHECK(back.defaults.angularUnits == DrawingSettings::AngularUnits::Grads);
  CHECK(back.defaults.footDefinition == DrawingSettings::FootDefinition::International);
  CHECK_FALSE(back.defaults.scaleInsertedObjects);
  CHECK(back.defaults.objectLayers[0].layer == "MY-NODE");
  CHECK(text.find("futureThing") != std::string::npos);

  // Clearing the zone and the unit removes them rather than writing a value.
  p.zoneCode.clear();
  p.insUnits = -1;
  ParseProjectSettings(WriteProjectSettings(text, p), &back, &err);
  CHECK(back.insUnits == -1);
  CHECK(back.zoneCode.empty());
}

TEST_CASE("req375 a project drawing cannot keep its own zone or unit", "[req375]") {
  const ProjectSettings p = MakeProject();
  int units = 2;
  float scale = 50.f;
  DrawingSettings ds;
  ds.zoneCode = "HARN/TX.TX-N";
  ds.markerX = 123.0;  // belong to the zone
  ds.transform.apply = true;

  const ProjectApplyResult r = ApplyProjectToDrawing(p, &units, &scale, &ds);
  CHECK(r.changed);
  CHECK(r.zoneReplaced);
  CHECK(r.unitsReplaced);
  CHECK(r.oldZone == "HARN/TX.TX-N");
  CHECK(units == 6);
  CHECK(ds.zoneCode == "HARN/TX.TX-C");
  CHECK(ds.markerX == 0.0);
  CHECK_FALSE(ds.transform.apply);

  // Already in agreement: nothing to do, nothing reported.
  CHECK_FALSE(ApplyProjectToDrawing(p, &units, &scale, &ds).changed);
}

TEST_CASE("req375 an inherited setting follows the project and an overridden one does not", "[req375]") {
  ProjectSettings p = MakeProject();
  int units = 6;
  float scale = 50.f;
  DrawingSettings ds;
  ds.zoneCode = p.zoneCode;
  ds.SetOverridden(ProjectDefaultKey::AngularUnits, true);
  ds.angularUnits = DrawingSettings::AngularUnits::Radians;  // the drawing's own choice

  ApplyProjectToDrawing(p, &units, &scale, &ds);
  CHECK(ds.angularUnits == DrawingSettings::AngularUnits::Radians);  // overridden: kept
  CHECK(ds.footDefinition == DrawingSettings::FootDefinition::International);  // inherited
  CHECK_FALSE(ds.scaleInsertedObjects);
  CHECK(scale == 100.f);

  // The project changes: the inherited ones follow, the override stays.
  p.defaults.footDefinition = DrawingSettings::FootDefinition::UsSurvey;
  p.defaults.angularUnits = DrawingSettings::AngularUnits::Degrees;
  p.plotScale = 20.f;
  ApplyProjectToDrawing(p, &units, &scale, &ds);
  CHECK(ds.footDefinition == DrawingSettings::FootDefinition::UsSurvey);
  CHECK(scale == 20.f);
  CHECK(ds.angularUnits == DrawingSettings::AngularUnits::Radians);

  // Reset to the project's value: no longer an override, so it follows again.
  CopyProjectDefault(p, ProjectDefaultKey::AngularUnits, &scale, &ds);
  ds.SetOverridden(ProjectDefaultKey::AngularUnits, false);
  CHECK(ds.angularUnits == DrawingSettings::AngularUnits::Degrees);
  p.defaults.angularUnits = DrawingSettings::AngularUnits::Grads;
  ApplyProjectToDrawing(p, &units, &scale, &ds);
  CHECK(ds.angularUnits == DrawingSettings::AngularUnits::Grads);
}

TEST_CASE("req375 a project without defaults inherits nothing", "[req375]") {
  ProjectSettings p;
  p.insUnits = 2;  // a unit but no defaults: the drawing's own settings stay
  int units = 2;
  float scale = 12.f;
  DrawingSettings ds;
  ds.footDefinition = DrawingSettings::FootDefinition::International;
  CHECK_FALSE(ApplyProjectToDrawing(p, &units, &scale, &ds).changed);
  CHECK(scale == 12.f);
  CHECK(ds.footDefinition == DrawingSettings::FootDefinition::International);
}

TEST_CASE("req375 overrides are saved with the drawing", "[req375]") {
  DrawingSettings ds;
  ds.SetOverridden(ProjectDefaultKey::PlotScale, true);
  ds.SetOverridden(ProjectDefaultKey::ObjectLayers, true);
  DrawingSettings back;
  REQUIRE(DrawingSettingsFromJsonText(DrawingSettingsToJsonText(ds), &back));
  CHECK(back.IsOverridden(ProjectDefaultKey::PlotScale));
  CHECK(back.IsOverridden(ProjectDefaultKey::ObjectLayers));
  CHECK_FALSE(back.IsOverridden(ProjectDefaultKey::AngularUnits));

  // A drawing saved before P2 has none.
  DrawingSettings fresh;
  REQUIRE(DrawingSettingsFromJsonText(DrawingSettingsToJsonText(fresh), &back));
  CHECK(back.overridden == 0u);
}

TEST_CASE("req375 every project tab agrees with its project, standalone is untouched", "[req375]") {
  AppCommandState st;
  JoinTwoTabs(st, MakeProject());
  st.drawingSettings.zoneCode = "HARN/TX.TX-N";
  st.drawingInsUnits = 2;
  st.documents[2].drawingSettings.zoneCode = "HARN/TX.TX-S";
  st.documents[2].drawingInsUnits = 2;
  st.documents[2].drawingSettings.SetOverridden(ProjectDefaultKey::FootDefinition, true);
  st.documents[2].drawingSettings.footDefinition = DrawingSettings::FootDefinition::UsSurvey;

  std::vector<std::string> log;
  EnforceProjectSettings(st, log);

  CHECK(st.drawingSettings.zoneCode == "HARN/TX.TX-C");  // the active tab
  CHECK(st.drawingInsUnits == 6);
  CHECK(st.modelUnitsPerPlottedInch == 100.f);
  CHECK(st.documents[2].drawingSettings.zoneCode == "HARN/TX.TX-C");  // a tab that is not showing
  CHECK(st.documents[2].drawingInsUnits == 6);
  CHECK(st.documents[2].drawingSettings.footDefinition == DrawingSettings::FootDefinition::UsSurvey);  // override
  CHECK(log.size() == 4);  // each replaced zone and unit is reported, never silent

  log.clear();
  EnforceProjectSettings(st, log);
  CHECK(log.empty());  // settled: nothing more to say

  // A standalone drawing is never touched.
  AppCommandState alone;
  alone.drawingSettings.zoneCode = "HARN/TX.TX-N";
  alone.drawingInsUnits = 2;
  EnforceProjectSettings(alone, log);
  CHECK(alone.drawingSettings.zoneCode == "HARN/TX.TX-N");
  CHECK(alone.drawingInsUnits == 2);
  CHECK(ProjectSettingsForTab(alone, 1) == nullptr);
}

TEST_CASE("req375 changing the plot scale by hand overrides it only when it differs", "[req375]") {
  AppCommandState st;
  JoinTwoTabs(st, MakeProject());
  st.modelUnitsPerPlottedInch = 40.f;
  NoteUserPlotScale(st);
  CHECK(st.drawingSettings.IsOverridden(ProjectDefaultKey::PlotScale));
  st.modelUnitsPerPlottedInch = 100.f;  // the project's own value: inherited again
  NoteUserPlotScale(st);
  CHECK_FALSE(st.drawingSettings.IsOverridden(ProjectDefaultKey::PlotScale));

  AppCommandState alone;
  alone.modelUnitsPerPlottedInch = 40.f;
  NoteUserPlotScale(alone);  // standalone: no meaning, no flag
  CHECK(alone.drawingSettings.overridden == 0u);
}
