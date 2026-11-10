// Issue #805: unsupported model entity types must make Cut fail without erasing anything.
// This exercises the domain command directly; it does not need to create the Qt editor window.
#include "CadCommands.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Cut refuses unsupported model entity types without erasing", "[issue805][clipboard]") {
  for (const auto type : {SelectedEntity::Type::Surface, SelectedEntity::Type::FeatureLine, SelectedEntity::Type::Mesh,
                          SelectedEntity::Type::Solid, SelectedEntity::Type::PipeRun,
                          SelectedEntity::Type::PointCloud, SelectedEntity::Type::PdfUnderlay,
                          SelectedEntity::Type::Alignment}) {
    AppCommandState state;
    state.userLinesFlat = {100, 100, 0, 110, 100, 0};
    state.selection = {{SelectedEntity::Type::LineSeg, 0}, {type, 0}};
    std::vector<std::string> log;

    CutSelectionToClipboard(state, log);

    CHECK(state.userLinesFlat.size() == 6);
    CHECK(state.clipboard.empty());
    REQUIRE_FALSE(log.empty());
    CHECK(log.back().find("CUT — not done") != std::string::npos);
  }
}
