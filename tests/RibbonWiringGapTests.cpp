// GitHub issue #762 — keep the supported Qt ribbon actions wired to their command handlers.
#include <catch2/catch_test_macros.hpp>

#include "../src/qt/RibbonCatalog.hpp"

#include <string>

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

TEST_CASE("Issue #762 Qt ribbon does not create an unsupported Manage panel", "[ribbon][issue762]") {
  for (const auto& button : qtribbon::kButtons)
    CHECK(std::string(button.tab) != "Manage");
}
