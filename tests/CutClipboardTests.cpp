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

// Issue #1557 phase 2: CadClipboard holds its entities as DrawingContent (ADR-0010), so a copy and
// a paste go through the same fields the drawing uses. A copy fills only the entity fields; the
// other drawing fields the clipboard now carries must never make it look non-empty or paste.

TEST_CASE("A copied line is held as drawing content and pastes at the cursor", "[issue1557][clipboard]") {
  AppCommandState state;
  state.userLinesFlat = {100, 100, 0, 110, 100, 0};
  state.userLineAttrs = {EntityAttributes{}};
  state.selection = {{SelectedEntity::Type::LineSeg, 0}};
  std::vector<std::string> log;

  CopySelectionToClipboard(state, log);

  REQUIRE_FALSE(state.clipboard.empty());
  REQUIRE(state.clipboard.content.userLinesFlat.size() == 6);
  REQUIRE(state.clipboard.content.userLineAttrs.size() == 1);

  CommitClipboardPasteAt(state, state.clipboard.basePtX + 50.0, state.clipboard.basePtY, log);

  REQUIRE(state.userLinesFlat.size() == 12);
  CHECK(state.userLinesFlat[6] == Catch::Approx(150.0));
  CHECK(state.userLinesFlat[7] == Catch::Approx(100.0));
  CHECK(state.userLinesFlat[9] == Catch::Approx(160.0));
}

TEST_CASE("Clipboard emptiness ignores drawing fields that a copy never fills", "[issue1557][clipboard]") {
  CadClipboard cb;
  CHECK(cb.empty());

  cb.content.currentLayer = "Survey";
  cb.content.activeTextStyleName = "Bold";
  cb.content.createPointsNextId = 42;
  CHECK(cb.empty());

  cb.content.userLinesFlat = {0, 0, 0, 1, 0, 0};
  CHECK_FALSE(cb.empty());
}
