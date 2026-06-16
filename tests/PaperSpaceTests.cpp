#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "commands/PaperSpace.hpp"

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
