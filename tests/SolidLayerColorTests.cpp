// REQ-352 (D-2026-09-28-g, GitHub issue #564 section 6) — a solid and a pipe run take layer and
// colour edits like any other entity.
//
// The Properties panel and the ribbon Layers combo both call the command-layer functions exercised
// here, so these cases pin what the user sees: the stored attribute changes, the ASSEMBLED display
// colour follows it (the viewport reads `solidDisplayGeometry`, not the attribute), a ByLayer solid
// follows its layer, the edit is one undo step, and it survives a save and reload.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"

namespace {

void Submit(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", text.c_str());
  ProcessCommandLineSubmit(buf, sizeof(buf), st, log);
}

SelectedEntity Sel(SelectedEntity::Type t, int index) {
  SelectedEntity e;
  e.type = t;
  e.index = index;
  return e;
}

void AddLayer(AppCommandState& st, const std::string& name, const std::string& color) {
  CadLayerRow row;
  row.name = name;
  row.color = color;
  st.drawingLayerTable.push_back(row);
}

CadLayerRow& Layer(AppCommandState& st, const std::string& name) {
  for (CadLayerRow& r : st.drawingLayerTable)
    if (r.name == name)
      return r;
  FAIL("no layer " << name);
  return st.drawingLayerTable.front();
}

/// A drawing with one 20x10x8 box solid (index 0) and one 4in pipe run (index 0).
AppCommandState WithSolidAndRun() {
  AppCommandState st;
  std::vector<std::string> log;
  Submit(st, "BOX 0,0 20 10 8", log);
  REQUIRE(st.cadSolids.size() == 1);
  CadPipeRun run;
  run.name = "L-101";
  run.vertsXyz = {0.0, 30.0, 0.0, 10.0, 30.0, 0.0};
  run.nominalSize = "4in";
  run.wallThicknessIn = 0.237;
  st.cadPipeRuns.push_back(run);
  st.cadPipeRunAttrs.push_back(EntityAttributes{});
  EnsureAttrCounts(st);
  return st;
}

/// The colours the viewport is handed for solids this frame, one per coalesced batch.
std::vector<std::vector<float>> DisplayedColours(AppCommandState& st) {
  RefreshSolidDisplayGeometry(st);
  std::vector<std::vector<float>> out;
  for (const auto& b : st.solidDisplayGeometry.solids)
    out.push_back({b.rgba[0], b.rgba[1], b.rgba[2]});
  return out;
}

bool Displays(AppCommandState& st, float r, float g, float b) {
  for (const auto& c : DisplayedColours(st))
    if (std::fabs(c[0] - r) < 1e-3f && std::fabs(c[1] - g) < 1e-3f && std::fabs(c[2] - b) < 1e-3f)
      return true;
  return false;
}

}  // namespace

TEST_CASE("Changing a solid's colour changes what the viewport draws, as one undo step (REQ-352)",
          "[req352][solid]") {
  AppCommandState st = WithSolidAndRun();
  std::vector<std::string> log;
  st.selection = {Sel(SelectedEntity::Type::Solid, 0)};
  const std::string before = st.cadSolidAttrs[0].color;
  const size_t undoBefore = CadActiveUndoStackSize(st);

  CHECK(CadApplyColorToSelection(st, "#FF0000") == 1);
  CHECK(st.cadSolidAttrs[0].color == "#FF0000");
  CHECK(CadActiveUndoStackSize(st) == undoBefore + 1);
  CHECK(Displays(st, 1.f, 0.f, 0.f));

  REQUIRE(DoUndo(st, log));
  CHECK(st.cadSolidAttrs[0].color == before);
  CHECK_FALSE(Displays(st, 1.f, 0.f, 0.f));
  REQUIRE(DoRedo(st, log));
  CHECK(st.cadSolidAttrs[0].color == "#FF0000");
}

TEST_CASE("A ByLayer solid takes its layer's colour and follows it (REQ-352)", "[req352][solid]") {
  AppCommandState st = WithSolidAndRun();
  AddLayer(st, "PIPE-STEEL", "#00FF00");
  st.selection = {Sel(SelectedEntity::Type::Solid, 0)};
  REQUIRE(st.cadSolidAttrs[0].color == "ByLayer");  // the default a new solid is stamped with

  CHECK(CadApplyLayerToSelection(st, "PIPE-STEEL") == 1);
  CHECK(st.cadSolidAttrs[0].layer == "PIPE-STEEL");
  CHECK(Displays(st, 0.f, 1.f, 0.f));

  Layer(st, "PIPE-STEEL").color = "#0000FF";
  CHECK(Displays(st, 0.f, 0.f, 1.f));
}

TEST_CASE("Layer Off and Freeze hide a solid and a pipe run moved onto the layer (REQ-352)",
          "[req352][solid][piperun]") {
  AppCommandState st = WithSolidAndRun();
  AddLayer(st, "HIDDEN", "#FFFFFF");
  st.selection = {Sel(SelectedEntity::Type::Solid, 0), Sel(SelectedEntity::Type::PipeRun, 0)};
  REQUIRE(CadApplyLayerToSelection(st, "HIDDEN") == 2);
  REQUIRE_FALSE(DisplayedColours(st).empty());

  Layer(st, "HIDDEN").on = false;
  CHECK(DisplayedColours(st).empty());
  CHECK_FALSE(SolidVisible(st, 0));

  Layer(st, "HIDDEN").on = true;
  Layer(st, "HIDDEN").frozen = true;
  CHECK(DisplayedColours(st).empty());

  Layer(st, "HIDDEN").frozen = false;
  CHECK_FALSE(DisplayedColours(st).empty());
}

TEST_CASE("A pipe run's colour edit reaches its already-built pipe solids (REQ-352)", "[req352][piperun]") {
  AppCommandState st = WithSolidAndRun();
  // Build the run's solids first, so the edit has to reach solids that already exist — their
  // geometry signature does not change with a colour, which is what left them stale.
  (void)DisplayedColours(st);
  REQUIRE_FALSE(st.pipeRunWorldSolids.empty());

  st.selection = {Sel(SelectedEntity::Type::PipeRun, 0)};
  CHECK(CadApplyColorToSelection(st, "#FF00FF") == 1);
  CHECK(st.cadPipeRunAttrs[0].color == "#FF00FF");
  CHECK(Displays(st, 1.f, 0.f, 1.f));
  for (const EntityAttributes& a : st.pipeRunWorldSolidAttrs)
    CHECK(a.color == "#FF00FF");
}

TEST_CASE("A mixed selection of a solid, a pipe run and a line takes one layer in one undo (REQ-352)",
          "[req352][solid][piperun]") {
  AppCommandState st = WithSolidAndRun();
  std::vector<std::string> log;
  Submit(st, "LINE", log);
  Submit(st, "0,-20", log);
  Submit(st, "10,-20", log);
  Submit(st, "", log);
  REQUIRE(st.userLineAttrs.size() == 1);
  st.selection = {Sel(SelectedEntity::Type::Solid, 0), Sel(SelectedEntity::Type::PipeRun, 0),
                  Sel(SelectedEntity::Type::LineSeg, 0)};
  const size_t undoBefore = CadActiveUndoStackSize(st);

  CHECK(CadApplyLayerToSelection(st, "NEW-LAYER") == 3);
  CHECK(st.cadSolidAttrs[0].layer == "NEW-LAYER");
  CHECK(st.cadPipeRunAttrs[0].layer == "NEW-LAYER");
  CHECK(st.userLineAttrs[0].layer == "NEW-LAYER");
  CHECK(CadActiveUndoStackSize(st) == undoBefore + 1);
  bool inTable = false;
  for (const CadLayerRow& r : st.drawingLayerTable)
    inTable = inTable || r.name == "NEW-LAYER";
  CHECK(inTable);  // a typed new name becomes a layer

  REQUIRE(DoUndo(st, log));
  CHECK(st.cadSolidAttrs[0].layer == "0");
  CHECK(st.cadPipeRunAttrs[0].layer != "NEW-LAYER");
  CHECK(st.userLineAttrs[0].layer == "0");
}

TEST_CASE("A new layer name typed for a solid alone becomes a layer the user can turn off (REQ-352)",
          "[req352][solid]") {
  AppCommandState st = WithSolidAndRun();
  st.selection = {Sel(SelectedEntity::Type::Solid, 0)};
  REQUIRE(CadApplyLayerToSelection(st, "SOLIDS-ONLY") == 1);
  bool inTable = false;
  for (const CadLayerRow& r : st.drawingLayerTable)
    inTable = inTable || r.name == "SOLIDS-ONLY";
  REQUIRE(inTable);
  Layer(st, "SOLIDS-ONLY").on = false;
  CHECK_FALSE(SolidVisible(st, 0));
}

TEST_CASE("An edit that changes nothing pushes no undo step (REQ-352)", "[req352][solid]") {
  AppCommandState st = WithSolidAndRun();
  st.selection = {Sel(SelectedEntity::Type::Solid, 0)};
  const size_t undoBefore = CadActiveUndoStackSize(st);
  CHECK(CadApplyLayerToSelection(st, st.cadSolidAttrs[0].layer) == 0);
  CHECK(CadApplyColorToSelection(st, "") == 0);
  CHECK(CadActiveUndoStackSize(st) == undoBefore);
}

TEST_CASE("The ribbon Layers combo moves a selection, and sets the current layer without one (REQ-352)",
          "[req352][ribbon]") {
  AppCommandState st = WithSolidAndRun();
  std::vector<std::string> log;
  AddLayer(st, "PIPE-STEEL", "#00FF00");
  const std::string current = st.currentLayer;

  st.selection = {Sel(SelectedEntity::Type::Solid, 0)};
  CHECK(CadSelectionLayer(st) == "0");
  CadRibbonPickLayer(st, "PIPE-STEEL", log);
  CHECK(st.cadSolidAttrs[0].layer == "PIPE-STEEL");
  CHECK(st.currentLayer == current);  // the current layer is left alone
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("1 object(s) moved to layer \"PIPE-STEEL\"") != std::string::npos);

  st.selection.push_back(Sel(SelectedEntity::Type::PipeRun, 0));
  CHECK(CadSelectionLayer(st) == kCadSelectionLayerVaries);

  st.selection.clear();
  CHECK(CadSelectionLayer(st).empty());
  CadRibbonPickLayer(st, "PIPE-STEEL", log);
  CHECK(st.currentLayer == "PIPE-STEEL");
  CHECK(st.cadPipeRunAttrs[0].layer != "PIPE-STEEL");
}

TEST_CASE("A solid's and a pipe run's layer and colour survive save and reload (REQ-352)",
          "[req352][solid][piperun][io]") {
  AppCommandState st = WithSolidAndRun();
  AddLayer(st, "PIPE-STEEL", "#00FF00");
  st.selection = {Sel(SelectedEntity::Type::Solid, 0), Sel(SelectedEntity::Type::PipeRun, 0)};
  REQUIRE(CadApplyLayerToSelection(st, "PIPE-STEEL") == 2);
  REQUIRE(CadApplyColorToSelection(st, "#123456") == 2);

  const std::string json = SerializeGoSurveyJson(st);
  AppCommandState back;
  std::vector<std::string> log;
  REQUIRE(LoadGoSurveyFromJsonUtf8(back, json, log));
  REQUIRE(back.cadSolidAttrs.size() == 1);
  REQUIRE(back.cadPipeRunAttrs.size() == 1);
  CHECK(back.cadSolidAttrs[0].layer == "PIPE-STEEL");
  CHECK(back.cadSolidAttrs[0].color == "#123456");
  CHECK(back.cadPipeRunAttrs[0].layer == "PIPE-STEEL");
  CHECK(back.cadPipeRunAttrs[0].color == "#123456");
}
