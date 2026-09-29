// REQ-357 (GitHub issue #582 increment 1): the Drawing Settings window's command-layer half — the
// DRAWINGSETTINGS command, the one-undo-step apply, units as a relabel, the drawing's foot
// definition in INSERT's unit factor, the plot-scale list shared with the status bar, trailer
// persistence, per-tab isolation and the DXF header variables.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "DxfIo.hpp"
#include "GsIo.hpp"
#include "util/PlotScales.hpp"

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

void OneLine(AppCommandState& st) {
  st.userLinesFlat = {1.25, 2.5, 0.0, 10.75, 20.5, 3.0};
  st.userLineAttrs = {EntityAttributes{}};
}

std::string ReadFile(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

/// The value line after a DXF header variable, or empty when the variable is absent.
std::string DxfHeaderValue(const std::string& dxf, const std::string& var) {
  const size_t at = dxf.find("\r\n" + var + "\r\n");
  if (at == std::string::npos)
    return {};
  size_t p = at + var.size() + 4;          // past "\r\n$VAR\r\n"
  p = dxf.find("\r\n", p);                 // past the group code line
  if (p == std::string::npos)
    return {};
  const size_t e = dxf.find("\r\n", p + 2);
  return dxf.substr(p + 2, e - (p + 2));
}

}  // namespace

TEST_CASE("DRAWINGSETTINGS and EDITDRAWINGSETTINGS open the window (REQ-357)", "[req357]") {
  std::vector<std::string> log;
  AppCommandState st;
  Submit(st, "DRAWINGSETTINGS", log);
  CHECK(st.showDrawingSettingsWindow);

  AppCommandState alias;
  Submit(alias, "editdrawingsettings", log);
  CHECK(alias.showDrawingSettingsWindow);
}

TEST_CASE("Applying Drawing Settings is one undo step and moves no geometry (REQ-357)", "[req357]") {
  std::vector<std::string> log;
  AppCommandState st;
  OneLine(st);
  const std::vector<double> before = st.userLinesFlat;
  const size_t undo0 = CadActiveUndoStackSize(st);

  // Nothing changed: no undo frame.
  REQUIRE(ApplyDrawingSettings(st, st.drawingInsUnits, st.modelUnitsPerPlottedInch, st.drawingSettings, log));
  CHECK(CadActiveUndoStackSize(st) == undo0);

  DrawingSettings s;
  s.angularUnits = DrawingSettings::AngularUnits::Grads;
  s.footDefinition = DrawingSettings::FootDefinition::International;
  s.scaleInsertedObjects = false;
  s.setDrawingVariables = false;
  REQUIRE(ApplyDrawingSettings(st, 6, 20.f, s, log));
  CHECK(CadActiveUndoStackSize(st) == undo0 + 1);
  CHECK(st.drawingInsUnits == 6);
  CHECK(st.modelUnitsPerPlottedInch == 20.f);
  CHECK(st.drawingSettings == s);
  CHECK(st.userLinesFlat == before);  // a relabel: every coordinate identical

  REQUIRE(DoUndo(st, log));
  CHECK(st.drawingInsUnits == 2);
  CHECK(st.modelUnitsPerPlottedInch == 50.f);
  CHECK(st.drawingSettings == DrawingSettings{});
  CHECK(st.userLinesFlat == before);

  REQUIRE(DoRedo(st, log));
  CHECK(st.drawingInsUnits == 6);
  CHECK(st.drawingSettings == s);
}

TEST_CASE("A scale that is not a positive number is refused (REQ-357)", "[req357]") {
  std::vector<std::string> log;
  AppCommandState st;
  const size_t undo0 = CadActiveUndoStackSize(st);
  CHECK_FALSE(ApplyDrawingSettings(st, 6, 0.f, st.drawingSettings, log));
  CHECK_FALSE(ApplyDrawingSettings(st, 6, -10.f, st.drawingSettings, log));
  CHECK_FALSE(ApplyDrawingSettings(st, 6, std::numeric_limits<float>::quiet_NaN(), st.drawingSettings, log));
  CHECK(st.drawingInsUnits == 2);  // nothing was changed
  CHECK(st.modelUnitsPerPlottedInch == 50.f);
  CHECK(CadActiveUndoStackSize(st) == undo0);
}

TEST_CASE("INSERT's unit factor uses the drawing's foot definition and can be switched off (REQ-357)",
          "[req357]") {
  AppCommandState st;
  st.drawingInsUnits = 2;  // feet
  CadBlockDefinition def;
  def.units = "meters";

  st.drawingSettings.footDefinition = DrawingSettings::FootDefinition::UsSurvey;
  CHECK(CadBlockInsertUnitsScale(st, def) == Catch::Approx(39.37 / 12.0).epsilon(1e-7));  // 3.2808333…
  st.drawingSettings.footDefinition = DrawingSettings::FootDefinition::International;
  CHECK(CadBlockInsertUnitsScale(st, def) == Catch::Approx(1.0 / 0.3048).epsilon(1e-7));  // 3.2808399…
  st.drawingSettings.scaleInsertedObjects = false;
  CHECK(CadBlockInsertUnitsScale(st, def) == 1.f);

  // Millimetres follow the same foot: a 1000 mm block is exactly 1 m.
  st.drawingSettings.scaleInsertedObjects = true;
  st.drawingSettings.footDefinition = DrawingSettings::FootDefinition::UsSurvey;
  def.units = "millimeters";
  CHECK(CadBlockInsertUnitsScale(st, def) == Catch::Approx(39.37 / 12.0 / 1000.0).epsilon(1e-7));
}

TEST_CASE("The plot-scale list is chosen by the drawing unit and shared with the status bar (REQ-357)",
          "[req357]") {
  const std::vector<PlotScaleChoice> feet = PlotScaleChoicesFor(2);
  REQUIRE(feet.size() == 16);
  CHECK(feet.front().label == "1\" = 1'");
  CHECK(feet.back().label == "1\" = 500'");
  CHECK(PlotScaleLabel(2, 50.f) == "1\" = 50'");
  CHECK(PlotScaleLabel(1, 600.f) == "1\" = 50'");  // an Inches drawing: 50 ft = 600 in
  CHECK(PlotScaleLabel(6, 12.7f) == "1:500");      // 0.0254 m × 500
  CHECK(PlotScaleLabel(4, 12700.f) == "1:500");    // 25.4 mm × 500
  CHECK(PlotScaleLabel(2, 37.f) == "1\" = 37' (custom)");
  CHECK(PlotScaleChoiceIndex(PlotScaleChoicesFor(6), 50.f) == -1);
}

TEST_CASE("PLOTSCALE and INSUNITS changes are undoable, and undo restores the scale (REQ-357)", "[req357]") {
  std::vector<std::string> log;
  AppCommandState st;
  Submit(st, "PLOTSCALE 20", log);
  CHECK(st.modelUnitsPerPlottedInch == 20.f);
  Submit(st, "INSUNITS meters", log);
  CHECK(st.drawingInsUnits == 6);
  REQUIRE(DoUndo(st, log));
  CHECK(st.drawingInsUnits == 2);
  CHECK(st.modelUnitsPerPlottedInch == 20.f);
  REQUIRE(DoUndo(st, log));
  CHECK(st.modelUnitsPerPlottedInch == 50.f);
}

TEST_CASE("Drawing Settings survive the drawing's save and reopen; old drawings get the defaults (REQ-357)",
          "[req357]") {
  std::vector<std::string> log;
  AppCommandState st;
  st.drawingInsUnits = 4;
  st.modelUnitsPerPlottedInch = 12700.f;
  st.drawingSettings.angularUnits = DrawingSettings::AngularUnits::Radians;
  st.drawingSettings.footDefinition = DrawingSettings::FootDefinition::International;
  st.drawingSettings.scaleInsertedObjects = false;
  st.drawingSettings.setDrawingVariables = false;

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, log));
  CHECK(back.drawingInsUnits == 4);
  CHECK(back.modelUnitsPerPlottedInch == 12700.f);
  CHECK(back.drawingSettings == st.drawingSettings);

  std::string legacy = json;
  const size_t at = legacy.find("\"drawingSettings\"");
  REQUIRE(at != std::string::npos);
  legacy.replace(at, std::string("\"drawingSettings\"").size(), "\"unusedSettings\"");
  AppCommandState old;
  old.drawingSettings.scaleInsertedObjects = false;  // must be reset, not kept from before the load
  REQUIRE(LoadGoSurveyFromJsonUtf8(old, legacy, log));
  CHECK(old.drawingSettings == DrawingSettings{});
}

TEST_CASE("Each drawing tab keeps its own units, scale and Drawing Settings (REQ-357)", "[req357]") {
  AppCommandState st;
  st.documents.resize(3);
  st.drawingInsUnits = 6;
  st.modelUnitsPerPlottedInch = 12.7f;
  st.drawingSettings.angularUnits = DrawingSettings::AngularUnits::Grads;
  SaveDocumentToSnapshot(st, 1);

  RestoreDocumentFromSnapshot(st, 2);  // a drawing that never changed them
  CHECK(st.drawingInsUnits == 2);
  CHECK(st.modelUnitsPerPlottedInch == 50.f);
  CHECK(st.drawingSettings == DrawingSettings{});

  RestoreDocumentFromSnapshot(st, 1);
  CHECK(st.drawingInsUnits == 6);
  CHECK(st.modelUnitsPerPlottedInch == 12.7f);
  CHECK(st.drawingSettings.angularUnits == DrawingSettings::AngularUnits::Grads);
}

TEST_CASE("DXF export writes LUNITS and AUNITS only with Set drawing variables on (REQ-357)", "[req357]") {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gosurvey-req357-dxf";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::filesystem::path p = dir / "units.dxf";
  std::vector<std::string> log;

  AppCommandState st;
  OneLine(st);
  st.drawingInsUnits = 4;
  st.drawingSettings.angularUnits = DrawingSettings::AngularUnits::Radians;
  REQUIRE(ExportDxfFile(st, p.string().c_str(), log));
  std::string dxf = ReadFile(p);
  CHECK(DxfHeaderValue(dxf, "$INSUNITS") == "4");
  CHECK(DxfHeaderValue(dxf, "$LUNITS") == "2");
  CHECK(DxfHeaderValue(dxf, "$AUNITS") == "3");

  AppCommandState in;  // a millimetre drawing's unit comes back on import
  REQUIRE(ImportDxfFile(in, p.string().c_str(), log));
  CHECK(in.drawingInsUnits == 4);

  st.drawingSettings.setDrawingVariables = false;
  REQUIRE(ExportDxfFile(st, p.string().c_str(), log));
  dxf = ReadFile(p);
  CHECK(DxfHeaderValue(dxf, "$INSUNITS") == "4");
  CHECK(DxfHeaderValue(dxf, "$AUNITS").empty());
  CHECK(DxfHeaderValue(dxf, "$LUNITS").empty());
  std::filesystem::remove_all(dir, ec);
}
