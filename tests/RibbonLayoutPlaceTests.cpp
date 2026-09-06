#include <catch2/catch_test_macros.hpp>

#include "ui/RibbonLayoutPlace.hpp"

using namespace ribbonlayout;

namespace {

// Builds a RibbonMeasuredSection with one group holding buttons of the given widths (all height
// 20), without needing a live ImGui frame — PlaceRibbonSection only reads already-measured sizes.
RibbonMeasuredSection MakeMeasuredSection(RibbonSectionSpec& sectionStorage, RibbonGroupSpec& groupStorage,
                                           std::vector<RibbonButtonSpec>& buttonStorage,
                                           const std::vector<float>& widths) {
  buttonStorage.resize(widths.size());
  for (size_t i = 0; i < widths.size(); ++i) {
    buttonStorage[i].id = "btn" + std::to_string(i);
    buttonStorage[i].sizePolicy = RibbonSizePolicy::AutoFit;
  }
  groupStorage.buttons = buttonStorage;
  sectionStorage.groups = {groupStorage};

  RibbonMeasuredSection measured;
  measured.spec = &sectionStorage;
  RibbonMeasuredGroup mg;
  mg.spec = &sectionStorage.groups[0];
  for (size_t i = 0; i < widths.size(); ++i) {
    RibbonMeasuredButton mb;
    mb.spec = &sectionStorage.groups[0].buttons[i];
    mb.size = ImVec2(widths[i], 20.f);
    mg.buttons.push_back(mb);
    mg.size.x += widths[i];
    mg.size.y = std::max(mg.size.y, 20.f);
  }
  measured.groups.push_back(mg);
  measured.size = mg.size;
  return measured;
}

} // namespace

TEST_CASE("PlaceRibbonSection lays out AutoFit buttons left-to-right without overlap", "[ribbonlayout][place]") {
  RibbonSectionSpec sectionStorage;
  RibbonGroupSpec groupStorage;
  std::vector<RibbonButtonSpec> buttonStorage;
  const RibbonMeasuredSection measured = MakeMeasuredSection(sectionStorage, groupStorage, buttonStorage,
                                                               {30.f, 40.f, 25.f});

  const RibbonPlacedSection placed = PlaceRibbonSection(measured, 1000.f);

  REQUIRE(placed.items.size() == 3);
  REQUIRE(placed.items[0].pos.x == 0.f);
  REQUIRE(placed.items[1].pos.x == 30.f);
  REQUIRE(placed.items[2].pos.x == 70.f);
  for (const RibbonPlacedItem& item : placed.items)
    REQUIRE_FALSE(item.overflow);
}

TEST_CASE("PlaceRibbonSection marks trailing items that don't fit as overflow", "[ribbonlayout][place]") {
  RibbonSectionSpec sectionStorage;
  RibbonGroupSpec groupStorage;
  std::vector<RibbonButtonSpec> buttonStorage;
  // Widths 30, 40, 25: with only 60px available, the 3rd button (starting at x=70) doesn't fit;
  // the 2nd (starting at x=30, ending at x=70) doesn't fit either since 70 > 60.
  const RibbonMeasuredSection measured = MakeMeasuredSection(sectionStorage, groupStorage, buttonStorage,
                                                               {30.f, 40.f, 25.f});

  const RibbonPlacedSection placed = PlaceRibbonSection(measured, 60.f);

  REQUIRE(placed.items.size() == 3);
  REQUIRE_FALSE(placed.items[0].overflow);  // 0 + 30 = 30 <= 60
  REQUIRE(placed.items[1].overflow);        // 30 + 40 = 70 > 60
  REQUIRE(placed.items[2].overflow);        // 70 + 25 = 95 > 60
}

TEST_CASE("PlaceRibbonSection distributes remaining width equally among Fill items", "[ribbonlayout][place]") {
  RibbonSectionSpec sectionStorage;
  RibbonButtonSpec fixedBtn;
  fixedBtn.id = "fixed";
  fixedBtn.sizePolicy = RibbonSizePolicy::AutoFit;
  RibbonButtonSpec fill1;
  fill1.id = "fill1";
  fill1.sizePolicy = RibbonSizePolicy::Fill;
  RibbonButtonSpec fill2;
  fill2.id = "fill2";
  fill2.sizePolicy = RibbonSizePolicy::Fill;

  RibbonGroupSpec groupStorage;
  groupStorage.buttons = {fixedBtn, fill1, fill2};
  sectionStorage.groups = {groupStorage};

  RibbonMeasuredSection measured;
  measured.spec = &sectionStorage;
  RibbonMeasuredGroup mg;
  mg.spec = &sectionStorage.groups[0];

  RibbonMeasuredButton mbFixed;
  mbFixed.spec = &sectionStorage.groups[0].buttons[0];
  mbFixed.size = ImVec2(100.f, 20.f);
  mg.buttons.push_back(mbFixed);

  RibbonMeasuredButton mbFill1;
  mbFill1.spec = &sectionStorage.groups[0].buttons[1];
  mbFill1.size = ImVec2(0.f, 0.f);
  mg.buttons.push_back(mbFill1);

  RibbonMeasuredButton mbFill2;
  mbFill2.spec = &sectionStorage.groups[0].buttons[2];
  mbFill2.size = ImVec2(0.f, 0.f);
  mg.buttons.push_back(mbFill2);

  measured.groups.push_back(mg);

  const RibbonPlacedSection placed = PlaceRibbonSection(measured, 500.f);

  REQUIRE(placed.items.size() == 3);
  REQUIRE(placed.items[0].size.x == 100.f);
  // Remaining 400px split equally between the two Fill items -> 200 each.
  REQUIRE(placed.items[1].size.x == 200.f);
  REQUIRE(placed.items[2].size.x == 200.f);
  REQUIRE(placed.items[1].pos.x == 100.f);
  REQUIRE(placed.items[2].pos.x == 300.f);
  REQUIRE_FALSE(placed.items[0].overflow);
  REQUIRE_FALSE(placed.items[1].overflow);
  REQUIRE_FALSE(placed.items[2].overflow);
}

TEST_CASE("PlaceRibbonSection with no Fill items and no overflow places everything exactly", "[ribbonlayout][place]") {
  RibbonSectionSpec sectionStorage;
  RibbonGroupSpec groupStorage;
  std::vector<RibbonButtonSpec> buttonStorage;
  const RibbonMeasuredSection measured = MakeMeasuredSection(sectionStorage, groupStorage, buttonStorage,
                                                               {50.f, 50.f});

  const RibbonPlacedSection placed = PlaceRibbonSection(measured, 100.f);

  REQUIRE(placed.items.size() == 2);
  float totalWidth = 0.f;
  for (const RibbonPlacedItem& item : placed.items)
    totalWidth += item.size.x;
  REQUIRE(totalWidth <= 100.f);
  REQUIRE_FALSE(placed.items[0].overflow);
  REQUIRE_FALSE(placed.items[1].overflow);
}
