#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include "ui/RibbonLayoutMeasure.hpp"

using namespace ribbonlayout;

namespace {

// Same headless-font-atlas pattern as tests/GsMigrateLegacyBreaklineTests.cpp: CalcTextSize
// needs a live ImGui frame with a built font atlas.
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

} // namespace

TEST_CASE("AutoFit button size grows with its label", "[ribbonlayout][measure]") {
  const HeadlessImGuiScope imguiScope;

  RibbonButtonSpec shortLabel;
  shortLabel.label = "Go";
  shortLabel.sizePolicy = RibbonSizePolicy::AutoFit;

  RibbonButtonSpec longLabel;
  longLabel.label = "A much longer button label";
  longLabel.sizePolicy = RibbonSizePolicy::AutoFit;

  const ImVec2 shortSize = MeasureRibbonButton(shortLabel);
  const ImVec2 longSize = MeasureRibbonButton(longLabel);

  REQUIRE(shortSize.x > 0.f);
  REQUIRE(shortSize.y > 0.f);
  REQUIRE(longSize.x > shortSize.x);
  // Both are single-line labels, so the icon (not the text) sets the height.
  REQUIRE(longSize.y == shortSize.y);
}

TEST_CASE("AutoFit button with no label is just the icon square plus padding", "[ribbonlayout][measure]") {
  const HeadlessImGuiScope imguiScope;

  RibbonButtonSpec iconOnly;
  iconOnly.sizePolicy = RibbonSizePolicy::AutoFit;

  const ImVec2 size = MeasureRibbonButton(iconOnly);
  const float expectedSide = MeasureIconSide() + kMeasureIconPad * 2.f;

  REQUIRE(size.x == expectedSide);
  REQUIRE(size.y == expectedSide);
}

TEST_CASE("Fixed size passes through unchanged", "[ribbonlayout][measure]") {
  const HeadlessImGuiScope imguiScope;

  RibbonButtonSpec fixed;
  fixed.label = "Ignored for sizing";
  fixed.sizePolicy = RibbonSizePolicy::Fixed;
  fixed.fixedSize = 42.f;

  const ImVec2 size = MeasureRibbonButton(fixed);
  REQUIRE(size.x == 42.f);
  REQUIRE(size.y == 42.f);
}

TEST_CASE("Fill size is left unresolved at measure time", "[ribbonlayout][measure]") {
  const HeadlessImGuiScope imguiScope;

  RibbonButtonSpec fill;
  fill.label = "Whatever remains";
  fill.sizePolicy = RibbonSizePolicy::Fill;

  const ImVec2 size = MeasureRibbonButton(fill);
  REQUIRE(size.x == 0.f);
  REQUIRE(size.y == 0.f);
}

TEST_CASE("MeasureRibbonSection sums group widths and rolls buttons up into groups", "[ribbonlayout][measure]") {
  const HeadlessImGuiScope imguiScope;

  RibbonButtonSpec a;
  a.id = "a";
  a.label = "A";
  RibbonButtonSpec b;
  b.id = "b";
  b.label = "B";

  RibbonGroupSpec drawGroup;
  drawGroup.title = "Draw";
  drawGroup.buttons = {a, b};

  RibbonSectionSpec section;
  section.title = "Home";
  section.groups = {drawGroup};

  const RibbonMeasuredSection measured = MeasureRibbonSection(section);

  REQUIRE(measured.spec == &section);
  REQUIRE(measured.groups.size() == 1);
  REQUIRE(measured.groups[0].buttons.size() == 2);

  const float sumButtons = measured.groups[0].buttons[0].size.x + measured.groups[0].buttons[1].size.x;
  REQUIRE(measured.groups[0].size.x == sumButtons);
  REQUIRE(measured.size.x == measured.groups[0].size.x);
  REQUIRE(measured.size.y == measured.groups[0].size.y);
}

TEST_CASE("MeasureRibbonGroup rolls up nested sub-groups", "[ribbonlayout][measure]") {
  const HeadlessImGuiScope imguiScope;

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

  const RibbonMeasuredGroup measured = MeasureRibbonGroup(grid);

  REQUIRE(measured.buttons.empty());
  REQUIRE(measured.groups.size() == 2);
  const float sumSubGroups = measured.groups[0].size.x + measured.groups[1].size.x;
  REQUIRE(measured.size.x == sumSubGroups);
}
