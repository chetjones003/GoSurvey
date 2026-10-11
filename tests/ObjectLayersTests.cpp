// REQ-361 (GitHub issue #582 increment 5): the Drawing Settings Object Layers tab. Every path that
// creates one of the eight kinds of object puts it on its row's layer (NCS defaults), creating the
// layer inside the creating step's undo; the name modifier; the current colour still applies; a
// fitting on a run keeps its run's layer (D-2026-09-29-f); the table is saved and kept per tab.

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "GsIo.hpp"
#include "SurveyCsv.hpp"
#include "SurveyPoints.hpp"

#include <imgui.h>

namespace {

// Survey-point labels are measured through ImGui::GetFont() while a point is placed or imported
// (EnsureSurveyPointLabelMtext) — the same fixture as LibreDwgCadTests.cpp (ADR-031 (c')).
struct HeadlessImGuiScope {
  HeadlessImGuiScope() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920.f, 1080.f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui::NewFrame();
  }
  ~HeadlessImGuiScope() {
    ImGui::EndFrame();
    ImGui::DestroyContext();
  }
};

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

bool HasLayer(const AppCommandState& st, const std::string& name) {
  for (const CadLayerRow& r : st.drawingLayerTable)
    if (r.name == name)
      return true;
  return false;
}

ObjectLayerRow& Row(AppCommandState& st, ObjectLayerKind k) {
  return st.drawingSettings.objectLayers[static_cast<size_t>(k)];
}

/// A 4in pipe run from (0,0) to (10,0), committed.
void RoutePipeRun(AppCommandState& st, std::vector<std::string>& log) {
  StartPipeRunCommand(st, log);
  REQUIRE(HandlePipeRunTextInput("4in", st, log));
  REQUIRE(HandlePipeRunTextInput("", st, log));  // schedule-40 wall
  SubmitPipeRunViewportPick(st, 0.f, 0.f, log);
  SubmitPipeRunViewportPick(st, 10.f, 0.f, log);
  REQUIRE(HandlePipeRunTextInput("end", st, log));
}

}  // namespace

TEST_CASE("The layer name: row layer, Prefix / Suffix, the object's name for * (REQ-361)", "[req361]") {
  DrawingSettings s;
  CHECK(ResolveObjectLayer(s, ObjectLayerKind::SurveyPoint, {}) == "V-NODE");
  CHECK(ResolveObjectLayer(s, ObjectLayerKind::Table, "ignored") == "C-ANNO-TABL");
  ObjectLayerRow& surf = s.objectLayers[static_cast<size_t>(ObjectLayerKind::Surface)];
  surf.modifier = ObjectLayerRow::Modifier::Suffix;
  surf.value = "-*";
  CHECK(ResolveObjectLayer(s, ObjectLayerKind::Surface, "EG") == "C-TOPO-EG");
  CHECK(ResolveObjectLayer(s, ObjectLayerKind::Surface, {}) == "C-TOPO-");  // no name: the * is dropped
  surf.modifier = ObjectLayerRow::Modifier::Prefix;
  surf.value = "*-";
  CHECK(ResolveObjectLayer(s, ObjectLayerKind::Surface, "FG") == "FG-C-TOPO");
  surf.modifier = ObjectLayerRow::Modifier::None;  // the value only counts with a modifier
  CHECK(ResolveObjectLayer(s, ObjectLayerKind::Surface, "FG") == "C-TOPO");

  CHECK(ValidateObjectLayers(s).empty());
  surf.layer = "  ";
  CHECK_FALSE(ValidateObjectLayers(s).empty());
  AppCommandState st;
  std::vector<std::string> log;
  CHECK_FALSE(ApplyDrawingSettings(st, st.drawingInsUnits, st.modelUnitsPerPlottedInch, s, log));
  CHECK(st.drawingSettings.objectLayers == DefaultObjectLayers());  // nothing changed
}

TEST_CASE("A new survey point and its label land on V-NODE / V-NODE-TEXT; one UNDO removes all (REQ-361)",
          "[req361]") {
  HeadlessImGuiScope imgui;
  AppCommandState st;
  std::vector<std::string> log;
  st.currentColor = "#FF0000";
  REQUIRE_FALSE(HasLayer(st, "V-NODE"));
  REQUIRE(TryPlaceSurveyPoint(st, 10.0, 20.0, 5.0, log));
  REQUIRE(st.surveyPoints.size() == 1);
  CHECK(st.surveyPoints[0].layer == "V-NODE");
  CHECK(HasLayer(st, "V-NODE"));
  CHECK(HasLayer(st, "V-NODE-TEXT"));
  bool labelOnLayer = false;
  for (size_t i = 0; i < st.cadAnnotations.size(); ++i)
    if (st.cadAnnotations[i].surveyPointLabelForId == st.surveyPoints[0].id)
      labelOnLayer = st.cadAnnotationAttrs[i].layer == "V-NODE-TEXT";
  CHECK(labelOnLayer);

  REQUIRE(DoUndo(st, log));
  CHECK(st.surveyPoints.empty());
  CHECK_FALSE(HasLayer(st, "V-NODE"));
  CHECK_FALSE(HasLayer(st, "V-NODE-TEXT"));

  // A layer the drawing already has is reused as the drawing spells it, not duplicated.
  CadLayerRow existing;
  existing.name = "v-node";
  st.drawingLayerTable.push_back(existing);
  REQUIRE(TryPlaceSurveyPoint(st, 1.0, 1.0, 0.0, log));
  CHECK(st.surveyPoints.back().layer == "v-node");
}

TEST_CASE("Create Points: a typed layer wins unless the row is Locked (REQ-361 item 4)", "[req361]") {
  HeadlessImGuiScope imgui;
  AppCommandState st;
  std::vector<std::string> log;
  st.createPointsOpts.layer = "MY-POINTS";
  REQUIRE(TryPlaceSurveyPoint(st, 0.0, 0.0, 0.0, log));
  CHECK(st.surveyPoints.back().layer == "MY-POINTS");
  CHECK(HasLayer(st, "MY-POINTS"));
  Row(st, ObjectLayerKind::SurveyPoint).locked = true;
  REQUIRE(TryPlaceSurveyPoint(st, 5.0, 0.0, 0.0, log));
  CHECK(st.surveyPoints.back().layer == "V-NODE");
}

TEST_CASE("CSV-imported survey points land on the Survey point row's layer (REQ-361)", "[req361]") {
  HeadlessImGuiScope imgui;
  const auto path = (std::filesystem::temp_directory_path() / "gosurvey-req361-points.csv").string();
  {
    std::ofstream f(path);
    f << "100.0,200.0,10.0\n110.0,210.0,11.0\n";
  }
  AppCommandState st;
  Row(st, ObjectLayerKind::SurveyPoint).layer = "V-NODE-TOPO";
  std::snprintf(st.surveyImportCsvPath, sizeof(st.surveyImportCsvPath), "%s", path.c_str());
  st.surveyImportCsvLayoutIdx = 3;  // ENZ
  st.surveyImportCsvSkipFirstRow = false;
  std::vector<std::string> log;
  REQUIRE(SurveyCsvImportFile(st, log));
  REQUIRE(st.surveyPoints.size() == 2);
  for (const SurveyPoint& p : st.surveyPoints)
    CHECK(p.layer == "V-NODE-TOPO");
  CHECK(HasLayer(st, "V-NODE-TOPO"));
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST_CASE("A TIN surface EG with Suffix -* lands on C-TOPO-EG; one UNDO removes both (REQ-361)", "[req361]") {
  AppCommandState st;
  std::vector<std::string> log;
  Row(st, ObjectLayerKind::Surface).modifier = ObjectLayerRow::Modifier::Suffix;
  Row(st, ObjectLayerKind::Surface).value = "-*";
  Submit(st, "SURFACECREATE EG", log);
  REQUIRE(st.cadSurfaces.size() == 1);
  REQUIRE(st.cadSurfaceAttrs.size() == 1);
  CHECK(st.cadSurfaceAttrs[0].layer == "C-TOPO-EG");
  CHECK(HasLayer(st, "C-TOPO-EG"));
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadSurfaces.empty());
  CHECK_FALSE(HasLayer(st, "C-TOPO-EG"));

  // A name that cannot be part of a layer name falls back to the row's own layer.
  Submit(st, "SURFACECREATE A/B", log);
  REQUIRE(st.cadSurfaces.size() == 1);
  CHECK(st.cadSurfaceAttrs[0].layer == "C-TOPO");
}

TEST_CASE("A new pipe run lands on C-PIPE; its fittings keep the run's layer (REQ-361, D-2026-09-29-f)",
          "[req361]") {
  AppCommandState st;
  std::vector<std::string> log;
  RoutePipeRun(st, log);
  REQUIRE(st.cadPipeRuns.size() == 1);
  CHECK(st.cadPipeRunAttrs[0].layer == "C-PIPE");
  CHECK(HasLayer(st, "C-PIPE"));
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadPipeRuns.empty());
  CHECK_FALSE(HasLayer(st, "C-PIPE"));

  // A catalogue part: placed off any run it goes on C-PIPE-FITT (with its name for *); fitted to a
  // run it takes the run's layer.
  CadBlockDefinition def;
  def.name = "VALVE-4IN";
  def.partType = CadPipePartType::Valve;
  def.nominalSize = "4in";
  st.blockDefs.push_back(def);
  Row(st, ObjectLayerKind::PipeFitting).modifier = ObjectLayerRow::Modifier::Suffix;
  Row(st, ObjectLayerKind::PipeFitting).value = "-*";
  REQUIRE(CadBlockPlaceInsert(st, "VALVE-4IN", CadBlockXform{}, /*explode=*/false, log));
  CHECK(st.cadBlockRefAttrs.back().layer == "C-PIPE-FITT-VALVE-4IN");
  EntityAttributes run;
  run.layer = "MY-RUN";
  REQUIRE(CadBlockPlaceInsertNoUndo(st, "VALVE-4IN", CadBlockXform{}, log, &run));
  CHECK(st.cadBlockRefAttrs.back().layer == "MY-RUN");

  // An ordinary block is not a fitting: the current layer, as before.
  CadBlockDefinition plain;
  plain.name = "NORTH-ARROW";
  st.blockDefs.push_back(plain);
  st.currentLayer = "0";
  REQUIRE(CadBlockPlaceInsert(st, "NORTH-ARROW", CadBlockXform{}, false, log));
  CHECK(st.cadBlockRefAttrs.back().layer == "0");
}

TEST_CASE("Solids, feature lines and tables land on their rows; the current colour still applies (REQ-361)",
          "[req361]") {
  AppCommandState st;
  std::vector<std::string> log;
  st.currentColor = "#00FF00";
  st.currentLayer = "0";
  Submit(st, "BOX 0,0 20 10 8", log);
  REQUIRE(st.cadSolids.size() == 1);
  CHECK(st.cadSolidAttrs[0].layer == "C-SOLID");
  CHECK(st.cadSolidAttrs[0].color == "#00FF00");  // REQ-356
  REQUIRE(DoUndo(st, log));
  CHECK_FALSE(HasLayer(st, "C-SOLID"));

  Row(st, ObjectLayerKind::FeatureLine).modifier = ObjectLayerRow::Modifier::Suffix;
  Row(st, ObjectLayerKind::FeatureLine).value = "-*";
  StartFeatureLineCommand(st, "DITCH", log);
  REQUIRE(SubmitFeatureLineVertex(st, 0.f, 0.f, false, log));
  REQUIRE(SubmitFeatureLineVertex(st, 10.f, 0.f, false, log));
  CommitFeatureLineDraft(st, false, log);
  REQUIRE(st.featureLineAttrs.size() == 1);
  CHECK(st.featureLineAttrs[0].layer == "C-TOPO-FEAT-DITCH");

  st.lastVolumeReportText = "Net: 0 yd3";
  Submit(st, "VOLREPORT TABLE", log);
  REQUIRE(st.cadTables.size() == 1);
  REQUIRE(st.cadTableAttrs.size() == 1);
  CHECK(st.cadTableAttrs[0].layer == "C-ANNO-TABL");
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadTables.empty());
  CHECK_FALSE(HasLayer(st, "C-ANNO-TABL"));
}

TEST_CASE("The Object Layers table survives the trailer and is kept per drawing tab (REQ-361)", "[req361]") {
  AppCommandState src;
  ObjectLayerRow& r = src.drawingSettings.objectLayers[static_cast<size_t>(ObjectLayerKind::PipeRun)];
  r.layer = "P-WATR";
  r.modifier = ObjectLayerRow::Modifier::Prefix;
  r.value = "X-*";
  r.locked = true;
  std::vector<std::string> log;
  AppCommandState back;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, SerializeGoSurveyJson(src), log));
  CHECK(back.drawingSettings.objectLayers == src.drawingSettings.objectLayers);

  // A drawing saved before REQ-361 opens with the defaults.
  AppCommandState old;
  std::string json = SerializeGoSurveyJson(old);
  const size_t at = json.find("\"objectLayers\"");
  REQUIRE(at != std::string::npos);
  json.replace(at, 14, "\"objectLayersX\"");
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, log));
  CHECK(back.drawingSettings.objectLayers == DefaultObjectLayers());

  AppCommandState st;
  st.documents.resize(3);
  st.drawingSettings.objectLayers[0].layer = "TAB-ONE";
  SaveDocumentToSnapshot(st, 1);
  st.drawingSettings.objectLayers[0].layer = "TAB-TWO";
  SaveDocumentToSnapshot(st, 2);
  RestoreDocumentFromSnapshot(st, 1);
  CHECK(st.drawingSettings.objectLayers[0].layer == "TAB-ONE");
  RestoreDocumentFromSnapshot(st, 2);
  CHECK(st.drawingSettings.objectLayers[0].layer == "TAB-TWO");
}
