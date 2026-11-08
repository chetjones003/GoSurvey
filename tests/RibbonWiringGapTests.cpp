// GitHub issue #762 — keep the supported Qt ribbon actions wired to their command handlers.
#include <catch2/catch_test_macros.hpp>

#include "../src/qt/RibbonCatalog.hpp"

#include <string>
#include <utility>

namespace {
const qtribbon::Button* FindButton(const char* id) {
  for (const auto& button : qtribbon::kButtons)
    if (std::string(button.id) == id)
      return &button;
  return nullptr;
}
}  // namespace

TEST_CASE("Issue #762 Qt ribbon actions remain connected", "[ribbon][issue762]") {
  const auto* array = FindButton("##RibbonArray");
  REQUIRE(array != nullptr);
  CHECK(std::string(array->command) == "ARRAY");
  CHECK_FALSE(array->placeholder);

  const auto* properties = FindButton("##PalProps");
  REQUIRE(properties != nullptr);
  CHECK(std::string(properties->command) == "@properties");
  CHECK_FALSE(properties->placeholder);

  const auto* points = FindButton("##RibbonInsPointsFile");
  REQUIRE(points != nullptr);
  CHECK(std::string(points->command) == "IMPORTPOINTS");
  CHECK_FALSE(points->placeholder);
}

TEST_CASE("Issue #1002 Export to LandXML is wired to its command", "[ribbon][issue1002]") {
  const auto* landXml = FindButton("##RibbonExpLandXml");
  REQUIRE(landXml != nullptr);
  CHECK(std::string(landXml->command) == "EXPORTLANDXML");
  CHECK_FALSE(landXml->placeholder);
}

TEST_CASE("Issue #762 Qt ribbon does not create an unsupported Manage panel", "[ribbon][issue762]") {
  for (const auto& button : qtribbon::kButtons)
    CHECK(std::string(button.tab) != "Manage");
}

TEST_CASE("Issue #1003 Import LandXML is wired to its command", "[ribbon][issue1003]") {
  const auto* landXml = FindButton("##RibbonInsLandXml");
  REQUIRE(landXml != nullptr);
  CHECK(std::string(landXml->command) == "IMPORTLANDXML");
  CHECK_FALSE(landXml->placeholder);
}

TEST_CASE("Issue #1000 Create Sheets is wired to its command", "[ribbon][issue1000][req518]") {
  const auto* sheets = FindButton("##OutCreateSheets");
  REQUIRE(sheets != nullptr);
  CHECK(std::string(sheets->command) == "CREATESHEETS");
  CHECK_FALSE(sheets->placeholder);
}

TEST_CASE("Issue #802 decided-out Civil 3D placeholders are removed from the Qt ribbon", "[ribbon][issue802]") {
  for (const char* id : {"##CdParcel", "##CdIntersections", "##CdAssembly", "##CdPond", "##CdUgStorage", "##CdChannel"})
    CHECK(FindButton(id) == nullptr);
}

TEST_CASE("Issue #807 implemented surface controls are wired", "[ribbon][issue807]") {
  for (const auto& [id, command] : {std::pair{"##TsLodLow", "@surfaceLodLow"},
                                    std::pair{"##TsLodHigh", "@surfaceLodHigh"},
                                    std::pair{"##TsMoveTo", "@moveBlocksToSurface"},
                                    std::pair{"##TsVisCheck", "@surfaceVisibilityMenu"},
                                    std::pair{"##TsXBreak", "@surfaceCrossingsMenu"},
                                    std::pair{"##TsAddLabels", "@surfaceLabelsMenu"},
                                    std::pair{"##TsAddLegend", "@surfaceLegendMenu"}}) {
    const auto* button = FindButton(id);
    REQUIRE(button != nullptr);
    CHECK(std::string(button->command) == command);
    CHECK_FALSE(button->placeholder);
  }
}
