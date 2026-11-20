#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "CadCommands.hpp"

namespace {
void SubmitText(AppCommandState& state, const char* text, std::vector<std::string>& log) {
  char buffer[64]{};
  std::snprintf(buffer, sizeof(buffer), "%s", text);
  ProcessCommandLineSubmit(buffer, static_cast<int>(sizeof(buffer)), state, log);
}

AppCommandState MviewState() {
  AppCommandState state;
  PaperLayout layout;
  layout.portraitWidthIn = 11.f;
  layout.portraitHeightIn = 17.f;
  layout.landscape = true;
  state.paperLayouts.push_back(layout);
  state.activeSpaceIndex = 0;
  return state;
}
}  // namespace

TEST_CASE("MVIEW Fit and fixed options create the specified sheet rectangles (REQ-490)", "[req490][mview]") {
  struct Expected { const char* option; std::vector<std::array<float, 4>> rects; };
  const std::vector<Expected> cases = {
      {"Fit", {{0.f, 0.f, 17.f, 11.f}}},
      {"2", {{0.f, 0.f, 8.5f, 11.f}, {8.5f, 0.f, 17.f, 11.f}}},
      {"3", {{0.f, 0.f, 8.5f, 11.f}, {8.5f, 5.5f, 17.f, 11.f}, {8.5f, 0.f, 17.f, 5.5f}}},
      {"4", {{0.f, 0.f, 8.5f, 5.5f}, {8.5f, 0.f, 17.f, 5.5f},
             {0.f, 5.5f, 8.5f, 11.f}, {8.5f, 5.5f, 17.f, 11.f}}},
  };
  for (const Expected& expected : cases) {
    AppCommandState state = MviewState();
    std::vector<std::string> log;
    SubmitText(state, "MVIEW", log);
    REQUIRE(state.active == AppCommandState::Kind::PaperRectViewport);
    SubmitText(state, expected.option, log);
    REQUIRE(state.paperLayouts[0].viewports.size() == expected.rects.size());
    for (size_t i = 0; i < expected.rects.size(); ++i) {
      const Viewport& vp = state.paperLayouts[0].viewports[i];
      CHECK(vp.paperXIn == Catch::Approx(expected.rects[i][0]));
      CHECK(vp.paperYIn == Catch::Approx(expected.rects[i][1]));
      CHECK(vp.paperXIn + vp.paperWIn == Catch::Approx(expected.rects[i][2]));
      CHECK(vp.paperYIn + vp.paperHIn == Catch::Approx(expected.rects[i][3]));
      CHECK(vp.clipBoundary.empty());
    }
    CHECK(state.active == AppCommandState::Kind::None);
  }
}

TEST_CASE("MVIEW cancellation leaves the active layout unchanged (REQ-490)", "[req490][mview]") {
  AppCommandState state = MviewState();
  std::vector<std::string> log;
  SubmitText(state, "MVIEW", log);
  SubmitText(state, "Object", log);
  REQUIRE(state.active == AppCommandState::Kind::PaperRectViewport);
  CancelActiveCommand(state, log);
  CHECK(state.active == AppCommandState::Kind::None);
  CHECK(state.paperLayouts[0].viewports.empty());
  CHECK(state.paperVpMode == 0);
}

TEST_CASE("MVIEW retains its original two-corner rectangle workflow (REQ-490)", "[req490][mview]") {
  AppCommandState state = MviewState();
  std::vector<std::string> log;
  SubmitText(state, "MVIEW", log);
  SubmitViewportPick(state, 2.0, 3.0, log);
  CHECK(state.paperLayouts[0].viewports.empty());
  CHECK(state.paperVpPhase == 1);
  SubmitViewportPick(state, 8.0, 9.0, log);
  REQUIRE(state.paperLayouts[0].viewports.size() == 1);
  const Viewport& viewport = state.paperLayouts[0].viewports[0];
  CHECK(viewport.paperXIn == Catch::Approx(2.f));
  CHECK(viewport.paperYIn == Catch::Approx(3.f));
  CHECK(viewport.paperWIn == Catch::Approx(6.f));
  CHECK(viewport.paperHIn == Catch::Approx(6.f));
  CHECK(state.active == AppCommandState::Kind::None);
}

TEST_CASE("MVIEW Object copies the closed boundary and undo/redo restores the viewport (REQ-490)",
          "[req490][mview]") {
  AppCommandState state = MviewState();
  PaperLayout& layout = state.paperLayouts[0];
  layout.paperPolyOffsets = {0, 3};
  layout.paperPolyClosed = {1};
  layout.paperPolyVerts = {0.f, 0.f, 0.f, 4.f, 0.f, 0.f, 2.f, 3.f, 0.f};
  layout.paperPolyVertsBulge = {0.5f, 0.f, 0.f};
  const std::vector<float> source = layout.paperPolyVerts;
  std::vector<std::string> log;
  SubmitText(state, "MVIEW", log);
  SubmitText(state, "Object", log);
  SubmitViewportPick(state, 2.0, 1.0, log);
  REQUIRE(state.paperLayouts[0].viewports.size() == 1);
  CHECK(state.paperLayouts[0].viewports[0].clipBoundary.size() == 3);
  CHECK(state.paperLayouts[0].viewports[0].clipBoundary[0].bulge == Catch::Approx(0.5f));
  CHECK(state.paperLayouts[0].paperPolyVerts == source);
  CHECK(state.active == AppCommandState::Kind::None);

  SubmitText(state, "UNDO", log);
  CHECK(state.paperLayouts[0].viewports.empty());
  SubmitText(state, "REDO", log);
  REQUIRE(state.paperLayouts[0].viewports.size() == 1);
  CHECK(state.paperLayouts[0].viewports[0].clipBoundary.size() == 3);
  CHECK(state.paperLayouts[0].paperPolyVerts == source);
}

TEST_CASE("MVIEW Object refuses open polylines without changing geometry or undo state (REQ-490)",
          "[req490][mview]") {
  AppCommandState state = MviewState();
  PaperLayout& layout = state.paperLayouts[0];
  layout.paperPolyOffsets = {0, 3};
  layout.paperPolyClosed = {0};
  layout.paperPolyVerts = {0.f, 0.f, 0.f, 4.f, 0.f, 0.f, 2.f, 3.f, 0.f};
  const auto source = layout.paperPolyVerts;
  std::vector<std::string> log;
  SubmitText(state, "MVIEW", log);
  SubmitText(state, "Object", log);
  SubmitViewportPick(state, 2.0, 0.0, log);
  CHECK(state.paperLayouts[0].viewports.empty());
  CHECK(state.paperLayouts[0].paperPolyVerts == source);
  CHECK(state.documents[static_cast<size_t>(state.activeDrawingIdx)].undoStack.empty());
  CHECK(state.active == AppCommandState::Kind::PaperRectViewport);
}

TEST_CASE("MVIEW Object refuses other paper entities without changing layout state (REQ-490)",
          "[req490][mview]") {
  AppCommandState state = MviewState();
  PaperLayout& layout = state.paperLayouts[0];
  layout.paperLines = {0.f, 0.f, 0.f, 4.f, 0.f, 0.f};
  const auto source = layout.paperLines;
  std::vector<std::string> log;
  SubmitText(state, "MVIEW", log);
  SubmitText(state, "Object", log);
  SubmitViewportPick(state, 2.0, 0.0, log);
  CHECK(state.paperLayouts[0].viewports.empty());
  CHECK(state.paperLayouts[0].paperLines == source);
  CHECK(state.documents[static_cast<size_t>(state.activeDrawingIdx)].undoStack.empty());
  CHECK(state.active == AppCommandState::Kind::PaperRectViewport);
  CHECK_FALSE(log.empty());
}

TEST_CASE("MVIEW Object accepts a closed two-vertex bulge outline (REQ-490)", "[req490][mview]") {
  AppCommandState state = MviewState();
  PaperLayout& layout = state.paperLayouts[0];
  layout.paperPolyOffsets = {0, 2};
  layout.paperPolyClosed = {1};
  layout.paperPolyVerts = {2.f, 2.f, 0.f, 6.f, 2.f, 0.f};
  layout.paperPolyVertsBulge = {1.f, 1.f};
  std::vector<std::string> log;
  SubmitText(state, "MVIEW", log);
  SubmitText(state, "Object", log);
  SubmitViewportPick(state, 2.0, 2.0, log);
  REQUIRE(state.paperLayouts[0].viewports.size() == 1);
  REQUIRE(state.paperLayouts[0].viewports[0].clipBoundary.size() == 2);
  CHECK(state.paperLayouts[0].viewports[0].clipBoundary[0].bulge == Catch::Approx(1.f));
  CHECK(state.paperLayouts[0].viewports[0].paperWIn > 0.f);
  CHECK(state.paperLayouts[0].viewports[0].paperHIn > 0.f);
}

TEST_CASE("MVIEW refuses model space before starting its prompt (REQ-490)", "[req490][mview]") {
  AppCommandState state;
  std::vector<std::string> log;
  SubmitText(state, "MVIEW", log);
  CHECK(state.active == AppCommandState::Kind::None);
  CHECK(state.paperLayouts.empty());
  REQUIRE_FALSE(log.empty());
}

TEST_CASE("CopyPaperLayout duplicates complete value and preserves active layout through undo redo (REQ-489)",
          "[req489][layout]") {
  AppCommandState state;
  PaperLayout source;
  source.name = "Site Plan";
  source.portraitWidthIn = 18.f;
  source.portraitHeightIn = 24.f;
  source.landscape = true;
  source.presetIdx = -1;
  source.pageSetupName = "Issued Set";
  source.fitToPaper = true;
  source.offsetXIn = 0.25f;
  source.centerPlot = true;
  source.paperLines = {1.f, 2.f, 0.f, 8.f, 2.f, 0.f};
  source.paperLineAttrs.resize(1);
  source.paperTexts.push_back(CadAnnotation{});
  source.paperTexts.back().text = "SHEET A";
  source.paperTextAttrs.resize(1);
  Viewport viewport;
  viewport.modelCenterX = 42.0;
  viewport.scaleModelPerPaperIn = 100.f;
  viewport.locked = true;
  viewport.frozenLayers = {"Survey"};
  viewport.clipBoundary = {{1.f, 1.f, 0.f}, {6.f, 1.f, 0.f}, {3.f, 5.f, 0.f}};
  source.viewports.push_back(viewport);
  state.paperLayouts.push_back(source);
  PaperLayout collision;
  collision.name = "Site Plan (2)";
  state.paperLayouts.push_back(collision);
  PaperLayout active;
  active.name = "Current Sheet";
  state.paperLayouts.push_back(active);
  SetActiveSpace(state, 2);
  state.lastPaperLayoutIndex = 2;
  state.selectedViewportLayout = 2;
  state.pageSetupLayoutIdx = 2;
  state.surveyImportCsvLayoutIdx = 2;
  state.surveyExportCsvLayoutIdx = 2;

  std::vector<std::string> log;
  const int copyIndex = CopyPaperLayout(state, 0, {}, log);
  REQUIRE(copyIndex == 3);
  REQUIRE(state.paperLayouts.size() == 4);
  CHECK(state.paperLayouts[3].name == "Site Plan (3)");
  const PaperLayout& copy = state.paperLayouts[3];
  CHECK(copy.sheetWidthIn() == Catch::Approx(source.sheetWidthIn()));
  CHECK(copy.sheetHeightIn() == Catch::Approx(source.sheetHeightIn()));
  CHECK(copy.presetIdx == -1);
  CHECK(copy.pageSetupName == "Issued Set");
  CHECK(copy.fitToPaper);
  CHECK(copy.offsetXIn == Catch::Approx(0.25f));
  CHECK(copy.centerPlot);
  CHECK(copy.paperLines == source.paperLines);
  REQUIRE(copy.paperTexts.size() == 1);
  CHECK(copy.paperTexts[0].text == "SHEET A");
  REQUIRE(copy.viewports.size() == 1);
  CHECK(copy.viewports[0].modelCenterX == Catch::Approx(42.0));
  CHECK(copy.viewports[0].scaleModelPerPaperIn == Catch::Approx(100.f));
  CHECK(copy.viewports[0].locked);
  CHECK(copy.viewports[0].frozenLayers == std::vector<std::string>{"Survey"});
  CHECK(copy.viewports[0].clipBoundary.size() == 3);
  CHECK(state.paperLayouts[0].name == source.name);
  CHECK(state.paperLayouts[0].paperLines == source.paperLines);
  CHECK(state.paperLayouts[0].paperTexts[0].text == "SHEET A");
  CHECK(state.paperLayouts[1].name == "Site Plan (2)");
  CHECK(state.paperLayouts[2].name == "Current Sheet");
  CHECK(state.activeSpaceIndex == 2);
  CHECK(state.paperLayouts[static_cast<size_t>(state.activeSpaceIndex)].name == "Current Sheet");
  CHECK(state.lastPaperLayoutIndex == 2);
  CHECK(state.selectedViewportLayout == 2);
  CHECK(state.pageSetupLayoutIdx == 2);
  CHECK(state.surveyImportCsvLayoutIdx == 2);
  CHECK(state.surveyExportCsvLayoutIdx == 2);

  REQUIRE(DoUndo(state, log));
  CHECK(state.paperLayouts.size() == 3);
  CHECK(state.activeSpaceIndex == 2);
  REQUIRE(DoRedo(state, log));
  CHECK(state.paperLayouts.size() == 4);
  CHECK(state.activeSpaceIndex == 2);
  CHECK(state.paperLayouts[3].name == "Site Plan (3)");
}

TEST_CASE("LAYOUT COPY supports active and named sources and rejects collisions without undo mutation (REQ-489)",
          "[req489][layout]") {
  AppCommandState state;
  PaperLayout first;
  first.name = "Main Plan";
  state.paperLayouts.push_back(first);
  PaperLayout current;
  current.name = "Current Sheet";
  state.paperLayouts.push_back(current);
  SetActiveSpace(state, 1);
  std::vector<std::string> log;

  HandleLayoutCommand("COPY", state, log);
  REQUIRE(state.paperLayouts.size() == 3);
  CHECK(state.paperLayouts[2].name == "Current Sheet (2)");
  CHECK(state.activeSpaceIndex == 1);

  HandleLayoutCommand("COPY Main Plan Alternate Plan", state, log);
  REQUIRE(state.paperLayouts.size() == 4);
  CHECK(state.paperLayouts[3].name == "Alternate Plan");
  CHECK(state.paperLayouts[3].paperLines == state.paperLayouts[0].paperLines);
  CHECK(state.activeSpaceIndex == 1);

  const auto before = state.paperLayouts;
  const size_t undoDepth = state.documents[static_cast<size_t>(state.activeDrawingIdx)].undoStack.size();
  HandleLayoutCommand("COPY Main Plan alternate plan", state, log);
  CHECK(state.paperLayouts.size() == before.size());
  CHECK(state.paperLayouts[0].name == before[0].name);
  CHECK(state.paperLayouts[1].name == before[1].name);
  CHECK(state.activeSpaceIndex == 1);
  CHECK(state.documents[static_cast<size_t>(state.activeDrawingIdx)].undoStack.size() == undoDepth);
  REQUIRE(!log.empty());
  CHECK(log.back().find("already exists") != std::string::npos);
}

TEST_CASE("REQ-482 locks viewport scale edits and refuses floating edits in an off viewport", "[req482][viewport]") {
  AppCommandState state;
  PaperLayout layout;
  Viewport viewport;
  viewport.locked = true;
  viewport.on = false;
  layout.viewports.push_back(viewport);
  state.paperLayouts.push_back(layout);
  SetActiveSpace(state, 0);
  state.selectedViewportIndex = 0;
  state.selectedViewports = {0};
  std::vector<std::string> log;

  const size_t undoDepth = state.documents[static_cast<size_t>(state.activeDrawingIdx)].undoStack.size();
  HandleVpscaleCommand("25", state, log);
  CHECK(state.paperLayouts[0].viewports[0].scaleModelPerPaperIn == Catch::Approx(50.f));
  CHECK(state.documents[static_cast<size_t>(state.activeDrawingIdx)].undoStack.size() == undoDepth);
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("locked") != std::string::npos);

  EnterFloatingModelSpace(state, 0, 0, log);
  CHECK_FALSE(InFloatingModelSpace(state));
  CHECK(log.back().find("off") != std::string::npos);
  CHECK(state.paperLayouts[0].viewports[0].on == false);
}

TEST_CASE("REQ-484 applies one named drawing scale to selected paper viewports atomically", "[req484][viewport]") {
  AppCommandState state;
  state.annotationScales = {{"1:50", 1.f, 50.f}, {"1:100", 1.f, 100.f}};
  PaperLayout layout;
  Viewport first; first.modelCenterX = 11.; first.modelCenterY = -8.; first.paperXIn = 1.; first.paperYIn = 2.;
  first.paperWIn = 4.; first.paperHIn = 3.; first.scaleModelPerPaperIn = 50.f;
  Viewport second; second.modelCenterX = -21.; second.modelCenterY = 31.; second.paperXIn = 7.; second.paperYIn = 5.;
  second.paperWIn = 6.; second.paperHIn = 2.; second.scaleModelPerPaperIn = 25.f;
  layout.viewports = {first, second};
  state.paperLayouts.push_back(layout);
  SetActiveSpace(state, 0);
  state.selectedViewports = {0, 1};
  state.selectedViewportIndex = 1;
  std::vector<std::string> log;

  HandleVpMatchScaleCommand("1:100", state, log);
  CHECK(state.paperLayouts[0].viewports[0].scaleModelPerPaperIn == Catch::Approx(100.f));
  CHECK(state.paperLayouts[0].viewports[1].scaleModelPerPaperIn == Catch::Approx(100.f));
  CHECK(state.paperLayouts[0].viewports[0].modelCenterX == Catch::Approx(11.));
  CHECK(state.paperLayouts[0].viewports[0].modelCenterY == Catch::Approx(-8.));
  CHECK(state.paperLayouts[0].viewports[0].paperXIn == Catch::Approx(1.));
  CHECK(state.paperLayouts[0].viewports[0].paperYIn == Catch::Approx(2.));
  CHECK(state.paperLayouts[0].viewports[0].paperWIn == Catch::Approx(4.));
  CHECK(state.paperLayouts[0].viewports[0].paperHIn == Catch::Approx(3.));
  CHECK(state.paperLayouts[0].viewports[1].modelCenterX == Catch::Approx(-21.));
  CHECK(state.paperLayouts[0].viewports[1].modelCenterY == Catch::Approx(31.));
  CHECK(state.paperLayouts[0].viewports[1].paperXIn == Catch::Approx(7.));
  CHECK(state.paperLayouts[0].viewports[1].paperYIn == Catch::Approx(5.));
  CHECK(state.paperLayouts[0].viewports[1].paperWIn == Catch::Approx(6.));
  CHECK(state.paperLayouts[0].viewports[1].paperHIn == Catch::Approx(2.));
  REQUIRE(DoUndo(state, log));
  CHECK(state.paperLayouts[0].viewports[0].scaleModelPerPaperIn == Catch::Approx(50.f));
  CHECK(state.paperLayouts[0].viewports[1].scaleModelPerPaperIn == Catch::Approx(25.f));
  REQUIRE(DoRedo(state, log));
  CHECK(state.paperLayouts[0].viewports[0].scaleModelPerPaperIn == Catch::Approx(100.f));
  CHECK(state.paperLayouts[0].viewports[1].scaleModelPerPaperIn == Catch::Approx(100.f));

  const auto depth = state.documents[static_cast<size_t>(state.activeDrawingIdx)].undoStack.size();
  HandleVpMatchScaleCommand("missing scale", state, log);
  CHECK(state.documents[static_cast<size_t>(state.activeDrawingIdx)].undoStack.size() == depth);
  CHECK(state.paperLayouts[0].viewports[0].scaleModelPerPaperIn == Catch::Approx(100.f));
}

// REQ-026: a layout's orientation maps its portrait dimensions onto the sheet.
TEST_CASE("PaperLayout orientation maps portrait dims to the sheet", "[paperspace]") {
  PaperLayout L;
  L.portraitWidthIn = 24.f;
  L.portraitHeightIn = 36.f;

  L.landscape = false;
  REQUIRE(L.sheetWidthIn() == 24.f);
  REQUIRE(L.sheetHeightIn() == 36.f);

  L.landscape = true;
  REQUIRE(L.sheetWidthIn() == 36.f);
  REQUIRE(L.sheetHeightIn() == 24.f);
}

// REQ-026: the preset table is well-formed (portrait orientation: width <= height) and the
// default index is in range.
TEST_CASE("Paper size presets are well-formed", "[paperspace]") {
  REQUIRE(kPaperSizePresetCount > 0);
  for (int i = 0; i < kPaperSizePresetCount; ++i) {
    REQUIRE(kPaperSizePresets[i].name != nullptr);
    REQUIRE(kPaperSizePresets[i].widthIn > 0.f);
    REQUIRE(kPaperSizePresets[i].heightIn >= kPaperSizePresets[i].widthIn);
  }
  REQUIRE(kDefaultPaperPresetIdx >= 0);
  REQUIRE(kDefaultPaperPresetIdx < kPaperSizePresetCount);
}

// REQ-027: the model→paper transform maps the viewport's model center to the rect center, and a
// model point one scale-unit away maps one paper inch away.
TEST_CASE("Viewport model->paper transform", "[paperspace]") {
  Viewport vp;
  vp.paperXIn = 2.f;
  vp.paperYIn = 1.f;
  vp.paperWIn = 10.f;
  vp.paperHIn = 6.f;
  vp.modelCenterX = 1000.0;
  vp.modelCenterY = 500.0;
  vp.scaleModelPerPaperIn = 50.f;  // 50 model units per paper inch

  float px = 0.f, py = 0.f;
  ModelToPaperIn(vp, 1000.0, 500.0, &px, &py);  // model center → rect center
  REQUIRE(px == 7.f);   // 2 + 10/2
  REQUIRE(py == 4.f);   // 1 + 6/2

  ModelToPaperIn(vp, 1050.0, 500.0, &px, &py);  // +50 model units (= +1 paper inch) in X
  REQUIRE(px == 8.f);
  REQUIRE(py == 4.f);
}

// Zero/invalid scale must not divide-by-zero; safeScale clamps it.
TEST_CASE("Viewport safeScale clamps non-positive scale", "[paperspace]") {
  Viewport vp;
  vp.scaleModelPerPaperIn = 0.f;
  REQUIRE(vp.safeScale() > 0.f);
  float px = 0.f, py = 0.f;
  ModelToPaperIn(vp, 5.0, 5.0, &px, &py);  // must be finite (no div-by-zero)
  REQUIRE(std::isfinite(px));
  REQUIRE(std::isfinite(py));
}

// REQ-028: per-viewport frozen layers default empty and can be toggled.
TEST_CASE("Viewport frozen layers toggle", "[paperspace]") {
  Viewport vp;
  REQUIRE(vp.frozenLayers.empty());

  // Initially no layers frozen
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "0"));
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "Layer1"));

  // Toggle a layer on
  ToggleFrozenLayerInViewport(vp, "Layer1");
  REQUIRE(vp.frozenLayers.size() == 1);
  REQUIRE(IsLayerFrozenInViewport(vp, "Layer1"));
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "0"));

  // Add another layer
  ToggleFrozenLayerInViewport(vp, "Layer2");
  REQUIRE(vp.frozenLayers.size() == 2);
  REQUIRE(IsLayerFrozenInViewport(vp, "Layer1"));
  REQUIRE(IsLayerFrozenInViewport(vp, "Layer2"));

  // Toggle a frozen layer off
  ToggleFrozenLayerInViewport(vp, "Layer1");
  REQUIRE(vp.frozenLayers.size() == 1);
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "Layer1"));
  REQUIRE(IsLayerFrozenInViewport(vp, "Layer2"));

  // Toggle off the last layer
  ToggleFrozenLayerInViewport(vp, "Layer2");
  REQUIRE(vp.frozenLayers.empty());
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "Layer2"));
}

// REQ-028 acceptance: a layer frozen in one viewport is hidden ONLY in that viewport — it stays
// visible in other viewports (and in model space, which never consults a viewport's frozen set).
// This is the predicate both the on-screen viewport render (CadUi) and the PDF plot (PdfPlot) apply.
TEST_CASE("Frozen layer is hidden only in its own viewport (REQ-028)", "[paperspace]") {
  Viewport vpA;  // freeze "Utilities" here
  Viewport vpB;  // leave everything thawed here
  ToggleFrozenLayerInViewport(vpA, "Utilities");

  // Hidden in vpA, visible in vpB.
  REQUIRE(IsLayerFrozenInViewport(vpA, "Utilities"));
  REQUIRE_FALSE(IsLayerFrozenInViewport(vpB, "Utilities"));

  // A different layer is untouched in both viewports.
  REQUIRE_FALSE(IsLayerFrozenInViewport(vpA, "Boundary"));
  REQUIRE_FALSE(IsLayerFrozenInViewport(vpB, "Boundary"));

  // Freezing the same layer in vpB is independent of vpA (thawing one leaves the other frozen).
  ToggleFrozenLayerInViewport(vpB, "Utilities");
  REQUIRE(IsLayerFrozenInViewport(vpB, "Utilities"));
  ToggleFrozenLayerInViewport(vpA, "Utilities");  // thaw in A only
  REQUIRE_FALSE(IsLayerFrozenInViewport(vpA, "Utilities"));
  REQUIRE(IsLayerFrozenInViewport(vpB, "Utilities"));
}

// REQ-046: VPFREEZE freezes (idempotent add) and VPTHAW thaws (remove) a layer in a viewport.
TEST_CASE("VPFREEZE freezes and VPTHAW thaws a layer in a viewport (REQ-046)", "[paperspace]") {
  Viewport vp;
  FreezeLayerInViewport(vp, "Grid");
  REQUIRE(IsLayerFrozenInViewport(vp, "Grid"));
  REQUIRE(vp.frozenLayers.size() == 1);
  FreezeLayerInViewport(vp, "Grid");  // idempotent — no duplicate
  REQUIRE(vp.frozenLayers.size() == 1);
  ThawLayerInViewport(vp, "Grid");
  REQUIRE_FALSE(IsLayerFrozenInViewport(vp, "Grid"));
  ThawLayerInViewport(vp, "Grid");  // thawing an already-thawed layer is a no-op
  REQUIRE(vp.frozenLayers.empty());
}

// REQ-046: per-viewport layer COLOR override — set/replace, query, clear, keeping the arrays parallel.
TEST_CASE("Per-viewport layer color override set/replace/clear (REQ-046)", "[paperspace]") {
  Viewport vp;
  REQUIRE(ViewportLayerColorOverride(vp, "Roads") == nullptr);

  SetViewportLayerColor(vp, "Roads", "Red");
  const std::string* c = ViewportLayerColorOverride(vp, "Roads");
  REQUIRE(c != nullptr);
  REQUIRE(*c == "Red");
  REQUIRE(vp.vpColorLayers.size() == 1);
  REQUIRE(vp.vpColorValues.size() == 1);

  // Setting the same layer again replaces the color, not appends.
  SetViewportLayerColor(vp, "Roads", "Blue");
  REQUIRE(vp.vpColorLayers.size() == 1);
  REQUIRE(*ViewportLayerColorOverride(vp, "Roads") == "Blue");

  // A second layer is independent.
  SetViewportLayerColor(vp, "Water", "Cyan");
  REQUIRE(vp.vpColorLayers.size() == 2);
  REQUIRE(*ViewportLayerColorOverride(vp, "Water") == "Cyan");
  REQUIRE(*ViewportLayerColorOverride(vp, "Roads") == "Blue");

  // Clearing removes one entry and keeps the parallel arrays in step.
  ClearViewportLayerColor(vp, "Roads");
  REQUIRE(ViewportLayerColorOverride(vp, "Roads") == nullptr);
  REQUIRE(vp.vpColorLayers.size() == 1);
  REQUIRE(vp.vpColorValues.size() == 1);
  REQUIRE(*ViewportLayerColorOverride(vp, "Water") == "Cyan");
}

TEST_CASE("Per-viewport color override is independent per viewport (REQ-046)", "[paperspace]") {
  Viewport a, b;
  SetViewportLayerColor(a, "Contours", "Green");
  REQUIRE(*ViewportLayerColorOverride(a, "Contours") == "Green");
  REQUIRE(ViewportLayerColorOverride(b, "Contours") == nullptr);  // untouched in the other viewport
}

TEST_CASE("Paper-space object snap finds endpoints, midpoints, text (REQ-037)", "[paperspace]") {
  PaperLayout L;
  // One paper line from (0,0) to (10,0): endpoints (0,0),(10,0) and midpoint (5,0).
  const float seg[6] = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f};
  L.paperLines.assign(seg, seg + 6);
  CadAnnotation t;
  t.insX = 3.f;
  t.insY = 4.f;
  t.text = "TB";
  L.paperTexts.push_back(t);

  float sx = 0.f, sy = 0.f;

  // Near an endpoint → snaps to it.
  REQUIRE(SnapPaperInchPoint(L, 0.1f, 0.05f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(0.f));
  REQUIRE(sy == Catch::Approx(0.f));

  // Near the midpoint → snaps to (5,0).
  REQUIRE(SnapPaperInchPoint(L, 4.9f, 0.2f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(5.f));
  REQUIRE(sy == Catch::Approx(0.f));

  // Near the text insertion point → snaps to (3,4).
  REQUIRE(SnapPaperInchPoint(L, 3.2f, 3.9f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(3.f));
  REQUIRE(sy == Catch::Approx(4.f));

  // Far from everything (tol too small) → no snap, outputs untouched.
  sx = -99.f;
  sy = -99.f;
  REQUIRE_FALSE(SnapPaperInchPoint(L, 100.f, 100.f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(-99.f));
  REQUIRE(sy == Catch::Approx(-99.f));
}

// REQ-038 / ADR-013: paper space now stores circles/arcs/ellipses/polylines; snapping finds their key points.
TEST_CASE("Paper-space object snap finds new primitive key points (REQ-038)", "[paperspace]") {
  PaperLayout L;
  // Circle center (20,20), r=5 → center + 4 quadrants.
  const float circ[3] = {20.f, 20.f, 5.f};
  L.paperCircles.assign(circ, circ + 3);
  // Arc center (0,0), r=10, start 0, sweep +90° → start point (10,0), end point (0,10), center (0,0).
  CadArc a;
  a.cx = 0.f; a.cy = 0.f; a.r = 10.f; a.startRad = 0.f; a.sweepRad = 1.57079633f;
  L.paperArcs.push_back(a);
  // Polyline with a vertex at (40,5).
  L.paperPolyOffsets = {0, 2};
  L.paperPolyVerts = {40.f, 5.f, 0.f, 45.f, 5.f, 0.f};
  L.paperPolyClosed = {0};

  float sx = 0.f, sy = 0.f;
  // Circle east quadrant (25,20).
  REQUIRE(SnapPaperInchPoint(L, 24.9f, 20.1f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(25.f));
  REQUIRE(sy == Catch::Approx(20.f));
  // Arc end point (0,10) — cos(90°) leaves a tiny float residue in x, so use a margin.
  REQUIRE(SnapPaperInchPoint(L, 0.1f, 9.9f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(0.f).margin(1e-4));
  REQUIRE(sy == Catch::Approx(10.f));
  // Polyline vertex (40,5).
  REQUIRE(SnapPaperInchPoint(L, 40.2f, 4.8f, 0.5f, &sx, &sy));
  REQUIRE(sx == Catch::Approx(40.f));
  REQUIRE(sy == Catch::Approx(5.f));
}

// REQ-039 (bug #2): the renderer anchors paper text at its insertion = TOP-LEFT, so the bounds occupy
// [insY - h, insY] in Y and [insX, insX + w] in X. Earlier (bottom-left) bounds put the glyphs above the
// insertion, mis-picking text by ~one line height.
TEST_CASE("Paper text bounds anchor at the top-left insertion (REQ-039)", "[paperspace]") {
  CadAnnotation t;
  t.insX = 3.f;
  t.insY = 4.f;
  t.plottedHeightInches = 0.5f;
  // Two glyphs → width = kCadTextAdvanceFactor * h * 2 = 0.55 * 0.5 * 2 = 0.55. Paper used its own
  // 0.6 until TASK-198; model and paper now share one factor, so a string of a given height reports
  // the same width on a sheet as it does in the model — REQ-039's parity, in the one place it was
  // measurably absent.
  t.text = "AB";
  float x0, y0, x1, y1;
  PaperTextBoundsIn(t, &x0, &y0, &x1, &y1);
  REQUIRE(x0 == Catch::Approx(3.f));
  REQUIRE(y1 == Catch::Approx(4.f));        // top edge at the insertion Y
  REQUIRE(y0 == Catch::Approx(4.f - 0.5f)); // descends one line height below it
  REQUIRE(x1 == Catch::Approx(3.f + 0.55f));
  REQUIRE(y0 < y1);
}

// REQ-039 (bug #1): box-select hits each paper object type. Window (L→R) needs the whole extent inside;
// crossing (R→L) needs only an overlap.
TEST_CASE("Paper box-select selects each type by window/crossing rules (REQ-039)", "[paperspace]") {
  PaperLayout L;
  // Line from (1,1) to (3,3) — extent [1,3]x[1,3].
  const float seg[6] = {1.f, 1.f, 0.f, 3.f, 3.f, 0.f};
  L.paperLines.assign(seg, seg + 6);
  // Text at top-left (5,6), h=0.5 → bounds [5,5.3]x[5.5,6].
  CadAnnotation t; t.insX = 5.f; t.insY = 6.f; t.plottedHeightInches = 0.5f; t.text = "T";
  L.paperTexts.push_back(t);
  // Circle center (10,10) r=2 → bbox [8,12]x[8,12].
  const float circ[3] = {10.f, 10.f, 2.f};
  L.paperCircles.assign(circ, circ + 3);
  // Arc center (20,20) r=3 → bbox [17,23]x[17,23].
  CadArc a; a.cx = 20.f; a.cy = 20.f; a.r = 3.f; a.startRad = 0.f; a.sweepRad = 1.f;
  L.paperArcs.push_back(a);
  // Ellipse center (30,30) major (4,0) → bbox [26,34]x[26,34].
  CadEllipse e; e.cx = 30.f; e.cy = 30.f; e.majVx = 4.f; e.majVy = 0.f; e.ratio = 0.5f;
  L.paperEllipses.push_back(e);
  // Polyline verts (40,40)-(44,42) → bbox [40,44]x[40,42].
  L.paperPolyOffsets = {0, 2};
  L.paperPolyVerts = {40.f, 40.f, 0.f, 44.f, 42.f, 0.f};
  L.paperPolyClosed = {0};

  auto countType = [](const std::vector<PaperEntityRef>& v, PaperEntityRef::Type t) {
    int n = 0;
    for (const auto& r : v) if (r.type == t) ++n;
    return n;
  };

  // A window box [0,0]-[50,50] fully contains every object → all six selected.
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 0.f, 0.f, 50.f, 50.f, /*windowMode=*/true, out);
    REQUIRE(out.size() == 6);
    REQUIRE(countType(out, PaperEntityRef::Type::Line) == 1);
    REQUIRE(countType(out, PaperEntityRef::Type::Text) == 1);
    REQUIRE(countType(out, PaperEntityRef::Type::Circle) == 1);
    REQUIRE(countType(out, PaperEntityRef::Type::Arc) == 1);
    REQUIRE(countType(out, PaperEntityRef::Type::Ellipse) == 1);
    REQUIRE(countType(out, PaperEntityRef::Type::Polyline) == 1);
  }

  // A window box [9,9]-[11,11] cuts through the circle but does not contain it → window selects nothing.
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 9.f, 9.f, 11.f, 11.f, /*windowMode=*/true, out);
    REQUIRE(out.empty());
  }

  // The same box as a crossing selection (R→L). It lies ENTIRELY INSIDE the circle — centre (10,10),
  // r=2, and the box's farthest corner is sqrt(2) = 1.41 away — so it touches no drawn geometry and
  // selects nothing. Until 2026-09-03 this required the circle instead: the box test was the circle's
  // bounding SQUARE, which is solid, so a fence in the hollow middle hit. See the "against actual
  // geometry" case below, and TASK-197.
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 9.f, 9.f, 11.f, 11.f, /*windowMode=*/false, out);
    REQUIRE(out.empty());
  }

  // A crossing box that genuinely straddles the rim — the same intent the case above used to carry.
  // Centre (10,10) r=2: (11,11) is 1.41 from the centre (inside) and (13,13) is 4.24 (outside).
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 11.f, 11.f, 13.f, 13.f, /*windowMode=*/false, out);
    REQUIRE(out.size() == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Circle);
    REQUIRE(out.front().index == 0);
  }

  // A box far from everything selects nothing in either mode.
  {
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 100.f, 100.f, 110.f, 110.f, /*windowMode=*/false, out);
    REQUIRE(out.empty());
    SelectPaperEntitiesInBox(L, 100.f, 100.f, 110.f, 110.f, /*windowMode=*/true, out);
    REQUIRE(out.empty());
  }
}

// TASK-197 — REQ-039 acceptance (1): a crossing box selects a paper object it TOUCHES, and a window
// box one it ENCLOSES. Every type used to be tested by its bounding box, which answers a different
// question for anything that is not a filled rectangle. Each miss below failed before the fix.
//
// Every MISS is paired with a HIT on the same entity, because a box test that selected nothing would
// pass every miss on its own.
TEST_CASE("Paper box-select tests the object, not a box round it (REQ-039, TASK-197)", "[paperspace]") {
  auto n = [](const std::vector<PaperEntityRef>& v) { return static_cast<int>(v.size()); };

  SECTION("line: a diagonal is not selected from the corner of its bounding square") {
    PaperLayout L;
    const float seg[6] = {0.f, 0.f, 0.f, 10.f, 10.f, 0.f};  // (0,0)-(10,10)
    L.paperLines.assign(seg, seg + 6);
    std::vector<PaperEntityRef> out;
    // Inside the bbox [0,10]^2, ~5 in clear of the line y = x.
    SelectPaperEntitiesInBox(L, 8.f, 1.f, 9.f, 2.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Straddling the line itself.
    SelectPaperEntitiesInBox(L, 4.f, 5.f, 6.f, 7.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Line);
    // Window still needs the whole segment inside - unchanged by this fix, asserted so it stays so.
    out.clear();
    SelectPaperEntitiesInBox(L, 4.f, 5.f, 6.f, 7.f, /*windowMode=*/true, out);
    REQUIRE(n(out) == 0);
    SelectPaperEntitiesInBox(L, -1.f, -1.f, 11.f, 11.f, /*windowMode=*/true, out);
    REQUIRE(n(out) == 1);
  }

  SECTION("circle: not from the enclosing square's corner, not from the hollow middle") {
    PaperLayout L;
    const float circ[3] = {0.f, 0.f, 10.f};
    L.paperCircles.assign(circ, circ + 3);
    std::vector<PaperEntityRef> out;
    // |(7.5,7.5)| = 10.6 > 10 - the whole box is outside the circle.
    SelectPaperEntitiesInBox(L, 7.5f, 7.5f, 8.5f, 8.5f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Floating in the middle: a circle is a curve, not a disc.
    SelectPaperEntitiesInBox(L, -1.f, -1.f, 1.f, 1.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Straddling the rim at (10,0).
    SelectPaperEntitiesInBox(L, 9.f, -1.f, 11.f, 1.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Circle);
  }

  SECTION("arc: the sweep decides, not the full circle's square") {
    PaperLayout L;
    CadArc a;  // quarter circle, centre (0,0) r=10, 0 to 90 deg - the upper-RIGHT quadrant only
    a.cx = 0.f; a.cy = 0.f; a.r = 10.f; a.startRad = 0.f; a.sweepRad = 1.5707963f;
    L.paperArcs.push_back(a);
    std::vector<PaperEntityRef> out;
    // On the full circle, three quadrants away from this arc.
    SelectPaperEntitiesInBox(L, -8.f, -8.f, -6.f, -6.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Under the arc's own span, in the empty region its bounding box covers.
    SelectPaperEntitiesInBox(L, 1.f, 1.f, 3.f, 3.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Straddling the crown at (0,10).
    SelectPaperEntitiesInBox(L, -1.f, 9.f, 1.f, 11.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Arc);
    // A window box round the ARC - not round the whole circle - now selects it. The old test wanted
    // the full circle's square inside, so a box this size selected nothing.
    out.clear();
    SelectPaperEntitiesInBox(L, -1.f, -1.f, 11.f, 11.f, /*windowMode=*/true, out);
    REQUIRE(n(out) == 1);
  }

  SECTION("ellipse: the ratio counts") {
    PaperLayout L;
    CadEllipse e;  // semi-major 10 along X, ratio 0.1 -> semi-minor 1. Real extent [-10,10]x[-1,1].
    e.cx = 0.f; e.cy = 0.f; e.majVx = 10.f; e.majVy = 0.f; e.ratio = 0.1f;
    L.paperEllipses.push_back(e);
    std::vector<PaperEntityRef> out;
    // Five times the ellipse's own half-height above it - inside the square the old test used.
    SelectPaperEntitiesInBox(L, -1.f, 5.f, 1.f, 6.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // The hollow middle.
    SelectPaperEntitiesInBox(L, -1.f, -0.2f, 1.f, 0.2f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Straddling the minor-axis end at (0,1).
    SelectPaperEntitiesInBox(L, -1.f, 0.5f, 1.f, 1.5f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Ellipse);
    // A window box round the true extent selects it; a square of side 2*major does not fit here.
    out.clear();
    SelectPaperEntitiesInBox(L, -11.f, -2.f, 11.f, 2.f, /*windowMode=*/true, out);
    REQUIRE(n(out) == 1);
  }

  SECTION("polyline: the empty inside of an L is empty") {
    PaperLayout L;
    // (0,0) -> (10,0) -> (10,10): an L. Its bbox is [0,10]^2; the whole upper-left is empty.
    L.paperPolyOffsets = {0, 3};
    L.paperPolyVerts = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 10.f, 0.f};
    L.paperPolyClosed = {0};
    std::vector<PaperEntityRef> out;
    SelectPaperEntitiesInBox(L, 1.f, 8.f, 3.f, 9.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    // Straddling the horizontal leg.
    SelectPaperEntitiesInBox(L, 4.f, -1.f, 6.f, 1.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Polyline);
  }

  SECTION("polyline: a CLOSED chain's closing segment selects too") {
    PaperLayout L;
    // A triangle (0,0)-(10,0)-(10,10), closed: the closing leg runs (10,10) back to (0,0).
    L.paperPolyOffsets = {0, 3};
    L.paperPolyVerts = {0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 10.f, 10.f, 0.f};
    L.paperPolyClosed = {1};
    std::vector<PaperEntityRef> out;
    // Straddling the hypotenuse at its midpoint (5,5) - a segment that exists only because it closes.
    SelectPaperEntitiesInBox(L, 4.f, 4.f, 6.f, 6.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Polyline);
    // The open version of the same chain is NOT selected there - proof the closing leg is what hit.
    L.paperPolyClosed = {0};
    out.clear();
    SelectPaperEntitiesInBox(L, 4.f, 4.f, 6.f, 6.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
  }

  SECTION("block: its real geometry, not just the insertion point") {
    std::vector<CadBlockDefinition> defs;
    CadBlockDefinition d;
    d.name = "B";
    // One segment from (2,2) to (6,6), well away from the insertion point at the origin.
    d.content.lines = {2.f, 2.f, 0.f, 6.f, 6.f, 0.f};
    d.content.lineAttrs.resize(1);
    defs.push_back(d);

    PaperLayout L;
    CadBlockRef br;
    br.defName = "B";
    br.xf.x = 0.f; br.xf.y = 0.f;
    L.paperBlockRefs.push_back(br);

    std::vector<PaperEntityRef> out;
    // A crossing box over what the block DRAWS, nowhere near its insertion point. This missed before.
    SelectPaperEntitiesInBox(L, 3.f, 3.f, 5.f, 5.f, /*windowMode=*/false, out, &defs);
    REQUIRE(n(out) == 1);
    REQUIRE(out.front().type == PaperEntityRef::Type::Block);
    // Far from both the geometry and the insertion point: still nothing.
    out.clear();
    SelectPaperEntitiesInBox(L, 50.f, 50.f, 60.f, 60.f, /*windowMode=*/false, out, &defs);
    REQUIRE(n(out) == 0);
    // Window now needs the whole extent inside, insertion point included.
    SelectPaperEntitiesInBox(L, 3.f, 3.f, 5.f, 5.f, /*windowMode=*/true, out, &defs);
    REQUIRE(n(out) == 0);
    SelectPaperEntitiesInBox(L, -1.f, -1.f, 7.f, 7.f, /*windowMode=*/true, out, &defs);
    REQUIRE(n(out) == 1);
    // With no definitions there is nothing to measure, so the historical insertion-point test stands.
    out.clear();
    SelectPaperEntitiesInBox(L, 3.f, 3.f, 5.f, 5.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 0);
    SelectPaperEntitiesInBox(L, -1.f, -1.f, 1.f, 1.f, /*windowMode=*/false, out);
    REQUIRE(n(out) == 1);
  }
}

// TASK-198 — paper text is bounded by the rectangle it OCCUPIES. Paper space carried its own copy of
// the text-bounds rule and it differed from model space's in three ways, each of which put the pick
// and the fence somewhere the glyphs are not. Every case below failed before the fix.
TEST_CASE("Paper text bounds: the box it occupies, not a guess at the insertion point (TASK-198)",
          "[paperspace]") {
  SECTION("MTEXT is bounded by the box the user dragged, not by a one-line estimate") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Mtext;
    a.insX = 0.f;
    a.insY = 0.f;
    a.plottedHeightInches = 0.25f;
    a.text = "a paragraph of wrapped text";
    a.boxMinX = 10.f; a.boxMaxX = 14.f;   // the rectangle the renderer wraps, anchors and clips to
    a.boxMinY = 20.f; a.boxMaxY = 23.f;
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    REQUIRE(x0 == Catch::Approx(10.f));
    REQUIRE(y0 == Catch::Approx(20.f));
    REQUIRE(x1 == Catch::Approx(14.f));
    REQUIRE(y1 == Catch::Approx(23.f));
  }

  SECTION("a Table is bounded by its box too") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Table;
    a.plottedHeightInches = 0.25f;
    a.text = "cells";
    a.boxMinX = 1.f; a.boxMaxX = 6.f;
    a.boxMinY = 2.f; a.boxMaxY = 5.f;
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    REQUIRE(x0 == Catch::Approx(1.f));
    REQUIRE(y0 == Catch::Approx(2.f));
    REQUIRE(x1 == Catch::Approx(6.f));
    REQUIRE(y1 == Catch::Approx(5.f));
  }

  SECTION("a stored box is normalized, so a box dragged right-to-left is still a box") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Mtext;
    a.text = "x";
    a.boxMinX = 14.f; a.boxMaxX = 10.f;   // reversed corners
    a.boxMinY = 23.f; a.boxMaxY = 20.f;
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    REQUIRE(x0 == Catch::Approx(10.f));
    REQUIRE(y0 == Catch::Approx(20.f));
    REQUIRE(x1 == Catch::Approx(14.f));
    REQUIRE(y1 == Catch::Approx(23.f));
  }

  SECTION("rotation turns the box with the glyphs") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Text;
    a.insX = 0.f;
    a.insY = 0.f;
    a.plottedHeightInches = 1.f;
    a.text = "ABCD";                       // 4 glyphs -> w = 0.55 * 1 * 4 = 2.2
    a.rotationRad = 1.5707963267948966f;   // 90 deg CCW
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    // Unrotated the box is [0,2.2] x [-1,0]. Turned a quarter turn about the insertion point it
    // becomes [0,1] x [0,2.2] — the string now runs UP the sheet, and the box has to run up with it.
    REQUIRE(x0 == Catch::Approx(0.f).margin(1.e-6));  // margin: Approx(0) rejects a signed zero
    REQUIRE(x1 == Catch::Approx(1.f));
    REQUIRE(y0 == Catch::Approx(0.f).margin(1.e-6));
    REQUIRE(y1 == Catch::Approx(2.2f));
  }

  SECTION("width counts characters, not bytes") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Text;
    a.insX = 0.f;
    a.insY = 0.f;
    a.plottedHeightInches = 1.f;
    a.text = "\xC3\xA9\xC3\xA9\xC3\xA9";   // "eee" with acute accents: 3 characters, 6 BYTES
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    REQUIRE(x1 - x0 == Catch::Approx(0.55f * 3.f));   // not 6 characters' worth
    // The same three characters written in ASCII must measure the same.
    CadAnnotation b = a;
    b.text = "eee";
    float bx0, by0, bx1, by1;
    PaperTextBoundsIn(b, &bx0, &by0, &bx1, &by1);
    REQUIRE(x1 - x0 == Catch::Approx(bx1 - bx0));
  }

  SECTION("a short label reports the width it draws, with no minimum padding") {
    CadAnnotation a;
    a.kind = CadAnnotation::Kind::Text;
    a.insX = 0.f;
    a.insY = 0.f;
    a.plottedHeightInches = 1.f;
    a.text = "A";
    float x0, y0, x1, y1;
    PaperTextBoundsIn(a, &x0, &y0, &x1, &y1);
    // Was floored at 2*h in model space, which reported an extent three times the glyph. A pick
    // aperture belongs to the pick (PickPaperEntityAt already expands by tolIn), not to the extent —
    // applied here it also inflated the box FENCE, which is what TASK-197 spent its length removing.
    REQUIRE(x1 - x0 == Catch::Approx(0.55f));
  }
}

// The bounds above are what the box fence consumes, so the fix has to be visible through it too.
TEST_CASE("Paper box-select follows an MTEXT's real box (TASK-198)", "[paperspace]") {
  PaperLayout L;
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Mtext;
  a.insX = 0.f;                       // insertion point at the origin ...
  a.insY = 0.f;
  a.plottedHeightInches = 0.25f;
  a.text = "wrapped paragraph text";
  a.boxMinX = 10.f; a.boxMaxX = 14.f;  // ... the box it occupies is ten inches away
  a.boxMinY = 20.f; a.boxMaxY = 23.f;
  L.paperTexts.push_back(a);

  std::vector<PaperEntityRef> out;
  // A crossing box over the text as drawn selects it.
  SelectPaperEntitiesInBox(L, 11.f, 21.f, 13.f, 22.f, /*windowMode=*/false, out);
  REQUIRE(out.size() == 1);
  REQUIRE(out.front().type == PaperEntityRef::Type::Text);

  // A crossing box over the INSERTION POINT, where the old estimate put the rectangle, selects
  // nothing — there is no text drawn there.
  out.clear();
  SelectPaperEntitiesInBox(L, -1.f, -1.f, 1.f, 1.f, /*windowMode=*/false, out);
  REQUIRE(out.empty());

  // Window mode needs the real box inside, and is not satisfied by enclosing the insertion point.
  SelectPaperEntitiesInBox(L, -1.f, -1.f, 5.f, 5.f, /*windowMode=*/true, out);
  REQUIRE(out.empty());
  SelectPaperEntitiesInBox(L, 9.f, 19.f, 15.f, 24.f, /*windowMode=*/true, out);
  REQUIRE(out.size() == 1);
}

TEST_CASE("Annotative hatch pattern scale tracks viewport scale (issue #622)", "[paperspace][issue622]") {
  Viewport vp;
  vp.scaleModelPerPaperIn = 40.f;
  CadFilledRegion fr;
  fr.patternName = "ANSI31";
  fr.patternScale = 2.f;
  fr.annotative = true;
  const CadFilledRegion scaled = FilledRegionForAnnotativeDraw(fr, &vp, 80.f);
  REQUIRE(scaled.patternScale == Catch::Approx(1.f));
  fr.annotative = false;
  REQUIRE(FilledRegionForAnnotativeDraw(fr, &vp, 80.f).patternScale == Catch::Approx(2.f));
}

TEST_CASE("Annotation scale entry converts to model units per plotted inch (issue #622)", "[paperspace][issue622]") {
  CadAnnotationScale s;
  s.name = "1:50";
  s.paperUnits = 1.f;
  s.drawingUnits = 50.f;
  REQUIRE(CadAnnotationScaleModelUnitsPerPlottedInch(s) == Catch::Approx(50.f));
}

TEST_CASE("Annotative TEXT uses current annotation scale in model space (issue #622)", "[paperspace][issue622]") {
  std::vector<CadAnnotationScale> scales(1);
  scales[0].paperUnits = 1.f;
  scales[0].drawingUnits = 25.f;
  CadAnnotation t;
  t.annotative = true;
  REQUIRE(AnnotativeModelUnitsPerPlottedInch(t, nullptr, 80.f, &scales, 0) == Catch::Approx(25.f));
}

TEST_CASE("Annotation scale status label uses DWG name when present (issue #622)", "[paperspace][issue622]") {
  CadAnnotationScale s;
  s.name = "1/4\" = 1'-0\"";
  REQUIRE(CadAnnotationScaleStatusLabel(s) == "1/4\" = 1'-0\"");
}

TEST_CASE("Annotative block ref uses model annotation scale index (issue #622)", "[paperspace][issue622]") {
  std::vector<CadAnnotationScale> scales(2);
  scales[0].paperUnits = 1.f;
  scales[0].drawingUnits = 10.f;
  scales[1].paperUnits = 1.f;
  scales[1].drawingUnits = 20.f;
  CadBlockRef ref;
  ref.annotative = true;
  ref.xf.sx = 2.f;
  ref.xf.sy = 2.f;
  const CadBlockRef out =
      CadBlockRefForAnnotativeDisplay(ref, nullptr, 40.f, &scales, 1);
  REQUIRE(out.xf.sx == Catch::Approx(1.f));
  REQUIRE(out.xf.sy == Catch::Approx(1.f));
}

TEST_CASE("Annotative hatch pattern scales with model annotation scale (issue #622)", "[paperspace][issue622]") {
  std::vector<CadAnnotationScale> scales(1);
  scales[0].paperUnits = 1.f;
  scales[0].drawingUnits = 40.f;
  CadFilledRegion fr;
  fr.patternName = "ANSI31";
  fr.patternScale = 2.f;
  fr.annotative = true;
  const CadFilledRegion scaled = FilledRegionForAnnotativeDraw(fr, nullptr, 80.f, &scales, 0);
  REQUIRE(scaled.patternScale == Catch::Approx(1.f));
}

TEST_CASE("Annotative per-scale visibility gate (issue #622)", "[paperspace][issue622]") {
  std::vector<CadAnnotationScale> scales(2);
  scales[0].name = "1:10";
  scales[0].paperUnits = 1.f;
  scales[0].drawingUnits = 10.f;
  scales[1].name = "1:50";
  scales[1].paperUnits = 1.f;
  scales[1].drawingUnits = 50.f;
  std::vector<std::string> vis{"1:10"};
  REQUIRE(CadAnnotativeVisibleAtActiveScale(true, vis, &scales, 0, nullptr, 40.f));
  REQUIRE_FALSE(CadAnnotativeVisibleAtActiveScale(true, vis, &scales, 1, nullptr, 40.f));
  REQUIRE(CadAnnotativeVisibleAtActiveScale(true, {}, &scales, 1, nullptr, 40.f));
  Viewport vp;
  vp.scaleModelPerPaperIn = 50.f;
  REQUIRE_FALSE(CadAnnotativeVisibleAtActiveScale(true, vis, &scales, -1, &vp, 40.f));
  vp.scaleModelPerPaperIn = 10.f;
  REQUIRE(CadAnnotativeVisibleAtActiveScale(true, vis, &scales, -1, &vp, 40.f));
}

TEST_CASE("Paper ARRAY rectangular copies are isolated and undo as one operation (REQ-483)",
          "[req483][paperspace][array]") {
  AppCommandState state = MviewState();
  state.paperLayouts[0].paperLines = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
  state.paperLayouts[0].paperLineAttrs.resize(1);
  state.userLinesFlat = {10.0, 0.0, 0.0, 11.0, 0.0, 0.0};
  PaperLayout sibling;
  sibling.paperLines = {20.f, 0.f, 0.f, 21.f, 0.f, 0.f};
  state.paperLayouts.push_back(sibling);

  std::vector<std::string> log;
  SubmitText(state, "ARRAY", log);
  REQUIRE(state.arrayOnPaper);
  REQUIRE(state.active == AppCommandState::Kind::Array);
  PaperEntityRef paperHit{};
  REQUIRE(PickPaperEntityAt(state.paperLayouts[0], 0.5f, 0.f, 0.12f, &paperHit));
  SubmitViewportPick(state, 0.5, 0.0, log);
  CAPTURE(log.back());
  REQUIRE(state.selectedPaperEntities.size() == 1);
  SubmitText(state, "", log);
  SubmitText(state, "R", log);
  SubmitText(state, "2", log);
  SubmitText(state, "3", log);
  SubmitText(state, "1", log);
  SubmitText(state, "0", log);

  CHECK(state.active == AppCommandState::Kind::None);
  CHECK(log.back() == "ARRAY — 2 total instance(s) (2 x 1).");
  REQUIRE(state.paperLayouts[0].paperLines.size() == 12);
  CHECK(state.paperLayouts[0].paperLines[0] == Catch::Approx(0.f));
  CHECK(state.paperLayouts[0].paperLines[6] == Catch::Approx(3.f));
  CHECK(state.paperLayouts[1].paperLines == sibling.paperLines);
  CHECK(state.userLinesFlat == std::vector<double>{10.0, 0.0, 0.0, 11.0, 0.0, 0.0});
  REQUIRE(DoUndo(state, log));
  CHECK(state.paperLayouts[0].paperLines == std::vector<float>{0.f, 0.f, 0.f, 1.f, 0.f, 0.f});
  CHECK(state.paperLayouts[1].paperLines == sibling.paperLines);
  CHECK(state.userLinesFlat == std::vector<double>{10.0, 0.0, 0.0, 11.0, 0.0, 0.0});
}

TEST_CASE("Paper ARRAY polar copies selected geometry without changing the source (REQ-483)",
          "[req483][paperspace][array]") {
  AppCommandState state = MviewState();
  state.paperLayouts[0].paperLines = {1.f, 0.f, 0.f, 2.f, 0.f, 0.f};
  state.paperLayouts[0].paperLineAttrs.resize(1);
  state.selectedPaperEntities.push_back({PaperEntityRef::Type::Line, 0});
  std::vector<std::string> log;
  SubmitText(state, "ARRAY", log);
  SubmitText(state, "P", log);
  SubmitText(state, "0,0", log);
  SubmitText(state, "4", log);
  SubmitText(state, "360", log);
  SubmitText(state, "Y", log);

  REQUIRE(state.paperLayouts[0].paperLines.size() == 24);
  CHECK(state.paperLayouts[0].paperLines[0] == Catch::Approx(1.f));
  CHECK(state.paperLayouts[0].paperLines[6] == Catch::Approx(0.f).margin(1.e-5f));
  CHECK(state.paperLayouts[0].paperLines[7] == Catch::Approx(1.f));
  CHECK(state.paperLayouts[0].paperLines[9] == Catch::Approx(0.f).margin(1.e-5f));
  CHECK(state.paperLayouts[0].paperLines[10] == Catch::Approx(2.f));
  CHECK(state.paperLayouts[0].paperLines[12] == Catch::Approx(-1.f));
  CHECK(state.paperLayouts[0].paperLines[18] == Catch::Approx(0.f).margin(1.e-5f));
  CHECK(state.paperLayouts[0].paperLines[19] == Catch::Approx(-1.f));
  REQUIRE(DoUndo(state, log));
  CHECK(state.paperLayouts[0].paperLines == std::vector<float>{1.f, 0.f, 0.f, 2.f, 0.f, 0.f});
}

TEST_CASE("Paper OVERKILL cleans only the active layout and undo restores it (REQ-483)",
          "[req483][paperspace][overkill]") {
  AppCommandState state = MviewState();
  state.paperLayouts[0].paperLines = {
      0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
      1.f, 0.f, 0.f, 0.f, 0.f, 0.f,
      3.f, 3.f, 0.f, 3.f, 3.f, 0.f,
  };
  const std::vector<float> original = state.paperLayouts[0].paperLines;
  PaperLayout sibling;
  sibling.paperLines = original;
  state.paperLayouts.push_back(sibling);
  state.userLinesFlat = {10.0, 0.0, 0.0, 11.0, 0.0, 0.0};
  const auto modelGeometry = state.userLinesFlat;

  std::vector<std::string> log;
  ExecuteOverkill(state, log);
  REQUIRE(state.paperLayouts[0].paperLines.size() == 6);
  CHECK(state.paperLayouts[0].paperLines[0] == Catch::Approx(0.f));
  CHECK(state.paperLayouts[0].paperLines[3] == Catch::Approx(1.f));
  CHECK(state.paperLayouts[1].paperLines == original);
  CHECK(state.userLinesFlat == modelGeometry);
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("cleaned up") != std::string::npos);
  REQUIRE(DoUndo(state, log));
  CHECK(state.paperLayouts[0].paperLines == original);
  CHECK(state.paperLayouts[1].paperLines == original);
  CHECK(state.userLinesFlat == modelGeometry);
}
