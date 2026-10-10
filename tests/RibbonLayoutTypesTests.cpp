#include <catch2/catch_test_macros.hpp>

#include "ui/RibbonLayoutTypes.hpp"

using namespace ribbonlayout;

TEST_CASE("RibbonSectionSpec round-trips a nested button/group/section structure", "[ribbonlayout]") {
  RibbonButtonSpec line;
  line.id = "line";
  line.label = "Line";
  line.iconName = "c3d_line";
  line.sizePolicy = RibbonSizePolicy::AutoFit;

  RibbonButtonSpec circle;
  circle.id = "circle";
  circle.label = "Circle";
  circle.iconName = "c3d_circle";
  circle.sizePolicy = RibbonSizePolicy::Fixed;
  circle.fixedSize = 32.f;

  RibbonGroupSpec drawGroup;
  drawGroup.title = "Draw";
  drawGroup.buttons = {line, circle};
  drawGroup.sizePolicy = RibbonSizePolicy::AutoFit;

  RibbonButtonSpec undo;
  undo.id = "undo";
  undo.label = "Undo";

  RibbonGroupSpec editGroup;
  editGroup.title = "Edit";
  editGroup.buttons = {undo};

  RibbonSectionSpec home;
  home.title = "Home";
  home.groups = {drawGroup, editGroup};

  REQUIRE(home.title == "Home");
  REQUIRE(home.groups.size() == 2);

  const RibbonGroupSpec& g0 = home.groups[0];
  REQUIRE(g0.title == "Draw");
  REQUIRE(g0.buttons.size() == 2);
  REQUIRE(g0.groups.empty());
  REQUIRE(g0.buttons[0].id == "line");
  REQUIRE(g0.buttons[0].label == "Line");
  REQUIRE(g0.buttons[0].iconName == "c3d_line");
  REQUIRE(g0.buttons[0].sizePolicy == RibbonSizePolicy::AutoFit);
  REQUIRE(g0.buttons[1].id == "circle");
  REQUIRE(g0.buttons[1].sizePolicy == RibbonSizePolicy::Fixed);
  REQUIRE(g0.buttons[1].fixedSize == 32.f);

  const RibbonGroupSpec& g1 = home.groups[1];
  REQUIRE(g1.title == "Edit");
  REQUIRE(g1.buttons.size() == 1);
  REQUIRE(g1.buttons[0].id == "undo");
}

TEST_CASE("RibbonGroupSpec can nest sub-groups instead of buttons", "[ribbonlayout]") {
  RibbonButtonSpec a;
  a.id = "a";
  RibbonButtonSpec b;
  b.id = "b";

  RibbonGroupSpec row1;
  row1.buttons = {a};
  RibbonGroupSpec row2;
  row2.buttons = {b};

  RibbonGroupSpec grid;
  grid.title = "Grid";
  grid.groups = {row1, row2};

  REQUIRE(grid.buttons.empty());
  REQUIRE(grid.groups.size() == 2);
  REQUIRE(grid.groups[0].buttons[0].id == "a");
  REQUIRE(grid.groups[1].buttons[0].id == "b");
}

TEST_CASE("Default-constructed specs have sane defaults", "[ribbonlayout]") {
  RibbonButtonSpec btn;
  REQUIRE(btn.id.empty());
  REQUIRE(btn.sizePolicy == RibbonSizePolicy::AutoFit);
  REQUIRE(btn.fixedSize == 0.f);

  RibbonSectionSpec sec;
  REQUIRE(sec.title.empty());
  REQUIRE(sec.groups.empty());
}
