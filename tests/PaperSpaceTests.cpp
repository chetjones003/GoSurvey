#include <catch2/catch_test_macros.hpp>

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
