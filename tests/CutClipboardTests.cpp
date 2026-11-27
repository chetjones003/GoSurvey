// Issue #805: unsupported model entity types must make Cut fail without erasing anything.
// This exercises the domain command directly; it does not need to create the Qt editor window.
#include "CadCommands.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

TEST_CASE("Cut refuses unsupported model entity types without erasing", "[issue805][clipboard]") {
  for (const auto type : {SelectedEntity::Type::Surface, SelectedEntity::Type::FeatureLine, SelectedEntity::Type::Mesh,
                          SelectedEntity::Type::Solid, SelectedEntity::Type::PipeRun,
                          SelectedEntity::Type::PointCloud, SelectedEntity::Type::PdfUnderlay,
                          SelectedEntity::Type::Alignment}) {
    AppCommandState state;
    state.drawing.userLinesFlat = {100, 100, 0, 110, 100, 0};
    state.selection = {{SelectedEntity::Type::LineSeg, 0}, {type, 0}};
    std::vector<std::string> log;

    CutSelectionToClipboard(state, log);

    CHECK(state.drawing.userLinesFlat.size() == 6);
    CHECK(state.clipboard.empty());
    REQUIRE_FALSE(log.empty());
    CHECK(log.back().find("CUT — not done") != std::string::npos);
  }
}

// Issue #1557 phase 2: CadClipboard holds its entities as DrawingContent (ADR-0010), so a copy and
// a paste go through the same fields the drawing uses. A copy fills only the entity fields; the
// drawing metadata must never make it look non-empty or paste.

TEST_CASE("A copied line is held as drawing content and pastes at the cursor", "[issue1557][clipboard]") {
  AppCommandState state;
  state.drawing.userLinesFlat = {100, 100, 0, 110, 100, 0};
  state.drawing.userLineAttrs = {EntityAttributes{}};
  state.selection = {{SelectedEntity::Type::LineSeg, 0}};
  std::vector<std::string> log;

  CopySelectionToClipboard(state, log);

  REQUIRE_FALSE(state.clipboard.empty());
  REQUIRE(state.clipboard.content.userLinesFlat.size() == 6);
  REQUIRE(state.clipboard.content.userLineAttrs.size() == 1);

  CommitClipboardPasteAt(state, state.clipboard.basePtX + 50.0, state.clipboard.basePtY, log);

  REQUIRE(state.drawing.userLinesFlat.size() == 12);
  CHECK(state.drawing.userLinesFlat[6] == Catch::Approx(150.0));
  CHECK(state.drawing.userLinesFlat[7] == Catch::Approx(100.0));
  CHECK(state.drawing.userLinesFlat[9] == Catch::Approx(160.0));
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

TEST_CASE("PASTEBLOCK resolves copied geometry in another drawing", "[issue1665][clipboard]") {
  AppCommandState source;
  CadBlockDefinition parent;
  parent.name = "Assembly";
  parent.content.nested.push_back({"pIn"});
  parent.content.nested.push_back({"Pin_paste1"});
  CadBlockDefinition child;
  child.name = "Pin";
  child.content.userLinesFlat = {0, 0, 0, 3, 0, 0};
  CadBlockDefinition sibling;
  sibling.name = "Pin_paste1";
  sibling.content.userLinesFlat = {0, 0, 0, 0, 4, 0};
  CadBlockDefinition unrelated;
  unrelated.name = "Unrelated";
  source.drawing.blockDefs = {parent, child, sibling, unrelated};
  source.drawing.cadBlockRefs.push_back({"assembly"});
  source.selection = {{SelectedEntity::Type::BlockRef, 0}};
  std::vector<std::string> log;
  char copy[] = "COPYCLIP";
  ProcessCommandLineSubmit(copy, sizeof(copy), source, log);
  REQUIRE(source.clipboard.content.blockDefs.size() == 3);

  AppCommandState destination;
  destination.documents.resize(1);
  destination.activeDrawingIdx = 0;
  destination.clipboard = source.clipboard;
  std::string expectedParent = "Assembly";
  std::string expectedChild = "Pin";
  SECTION("Destination has no definitions") {}
  SECTION("Top-level and nested names collide case-insensitively") {
    CadBlockDefinition conflict;
    conflict.name = "ASSEMBLY";
    conflict.content.userLinesFlat = {0, 0, 0, 100, 0, 0};
    destination.drawing.blockDefs.push_back(conflict);
    conflict.name = "pin";
    destination.drawing.blockDefs.push_back(conflict);
    destination.drawing.cadBlockRefs.push_back({"ASSEMBLY"});
    destination.drawing.cadBlockRefAttrs.emplace_back();
    expectedParent = "Assembly_paste1";
    expectedChild = "Pin_paste2";
  }
  const size_t originalDefs = destination.drawing.blockDefs.size();
  const size_t originalRefs = destination.drawing.cadBlockRefs.size();
  char paste[] = "PASTEBLOCK 10,20";
  ProcessCommandLineSubmit(paste, sizeof(paste), destination, log);
  REQUIRE(destination.drawing.blockDefs.size() == originalDefs + 3);
  REQUIRE(destination.drawing.cadBlockRefs.size() == originalRefs + 1);
  const auto& ref = destination.drawing.cadBlockRefs.back();
  CHECK(ref.defName == expectedParent);
  CHECK(destination.drawing.blockDefs[originalDefs].content.nested.front().defName == expectedChild);
  CHECK(destination.clipboard.content.cadBlockRefs.front().defName == "assembly");
  std::vector<CadBlockWorldSeg> lines;
  CadBlockCollectWorldLines(destination.drawing.blockDefs, ref, {}, &lines);
  REQUIRE(lines.size() == 2);
  CHECK(std::any_of(lines.begin(), lines.end(), [](const CadBlockWorldSeg& line) {
    return line.x0 == 10 && line.y0 == 20 && line.x1 == 13 && line.y1 == 20;
  }));
  CHECK(std::any_of(lines.begin(), lines.end(), [](const CadBlockWorldSeg& line) {
    return line.x0 == 10 && line.y0 == 20 && line.x1 == 10 && line.y1 == 24;
  }));
  if (originalRefs != 0) {
    lines.clear();
    CadBlockCollectWorldLines(destination.drawing.blockDefs, destination.drawing.cadBlockRefs.front(), {}, &lines);
    REQUIRE(lines.size() == 1);
    CHECK(lines.front().x1 == 100);
  }

  REQUIRE(CanUndo(destination));
  char undo[] = "UNDO";
  ProcessCommandLineSubmit(undo, sizeof(undo), destination, log);
  CHECK(destination.drawing.blockDefs.size() == originalDefs);
  CHECK(destination.drawing.cadBlockRefs.size() == originalRefs);
  char redo[] = "REDO";
  ProcessCommandLineSubmit(redo, sizeof(redo), destination, log);
  CHECK(destination.drawing.blockDefs.size() == originalDefs + 3);
  REQUIRE(destination.drawing.cadBlockRefs.size() == originalRefs + 1);
  CHECK(destination.drawing.cadBlockRefs.back().defName == expectedParent);
}

TEST_CASE("PASTEBLOCK leaves the drawing alone when a copied dependency is missing", "[issue1665][clipboard]") {
  AppCommandState state;
  state.documents.resize(1);
  state.activeDrawingIdx = 0;
  state.clipboard.content.cadBlockRefs.push_back({"Assembly"});
  CadBlockDefinition parent;
  parent.name = "Assembly";
  parent.content.nested.push_back({"Missing"});
  state.clipboard.content.blockDefs.push_back(parent);
  CadBlockDefinition unrelated;
  unrelated.name = "Missing";
  state.drawing.blockDefs.push_back(unrelated);
  std::vector<std::string> log;
  char paste[] = "PASTEBLOCK";
  ProcessCommandLineSubmit(paste, sizeof(paste), state, log);
  CHECK(state.drawing.cadBlockRefs.empty());
  CHECK(state.drawing.blockDefs.size() == 1);
  CHECK_FALSE(CanUndo(state));
  REQUIRE_FALSE(log.empty());
  CHECK(log.back().find("missing clipboard definition \"Missing\"") != std::string::npos);
}
