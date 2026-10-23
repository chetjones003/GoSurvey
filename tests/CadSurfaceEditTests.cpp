// Surface and surface-style edit services (issue #893; REQ-068, REQ-069, REQ-070, REQ-072, REQ-075,
// REQ-361). The Qt surface dialogs call these, so the rules are asserted once here.
#include "CadCommands.hpp"
#include "CadSurfaceEdit.hpp"
#include "SurfaceStyle.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {

AppCommandState DrawingWithSurfaces(const std::vector<std::string>& names) {
  AppCommandState st;
  SurfaceStyles::EnsureStandard(st.surfaceStyles);
  std::vector<std::string> log;
  for (const std::string& name : names) REQUIRE(CreateSurfaceFromPointGroups(st, name, {}, log) >= 0);
  return st;
}

bool LogMentions(const std::vector<std::string>& log, const std::string& text) {
  for (const std::string& line : log)
    if (line.find(text) != std::string::npos) return true;
  return false;
}

}  // namespace

TEST_CASE("Create Surface refuses a taken name and creates nothing", "[issue893][surfaces]") {
  AppCommandState st = DrawingWithSurfaces({"EG"});
  std::vector<std::string> log;
  surfedit::CreateRequest request;
  request.name = "EG";
  const std::string why = surfedit::CreateSurface(st, request, log);
  CHECK(why == "A surface named \"EG\" already exists.");
  CHECK(st.cadSurfaces.size() == 1);
}

TEST_CASE("Create Surface carries the description and style and is one undo step", "[issue893][surfaces][undo]") {
  AppCommandState st = DrawingWithSurfaces({});
  std::vector<std::string> log;
  surfedit::CreateRequest request;
  request.name = "Pond";
  request.description = "Retention pond";
  request.styleName = SurfaceStyles::kStandardName;
  REQUIRE(surfedit::CreateSurface(st, request, log).empty());
  REQUIRE(st.cadSurfaces.size() == 1);
  CHECK(st.cadSurfaces[0].description == "Retention pond");
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadSurfaces.empty());
}

TEST_CASE("Create Surface volume kinds need two different surfaces", "[issue893][surfaces][volume]") {
  AppCommandState st = DrawingWithSurfaces({"A", "B"});
  std::vector<std::string> log;
  surfedit::CreateRequest request;
  request.kind = surfedit::CreateKind::TinVolume;
  request.name = "Vol";
  CHECK(surfedit::CreateSurface(st, request, log) == "Pick a base surface and a comparison surface.");
  request.volumeBase = "A";
  request.volumeComparison = "A";
  CHECK(surfedit::CreateSurface(st, request, log) == "Base and comparison must be different surfaces.");
  CHECK(st.cadSurfaces.size() == 2);
}

TEST_CASE("Grid spacing must be positive", "[issue893][surfaces][grid]") {
  AppCommandState st = DrawingWithSurfaces({});
  std::vector<std::string> log;
  surfedit::CreateRequest request;
  request.kind = surfedit::CreateKind::Grid;
  request.name = "Grid";
  request.gridSpacingX = 0.0;
  CHECK(surfedit::CreateSurface(st, request, log) == "Grid spacing must be positive.");
  CHECK(st.cadSurfaces.empty());
}

TEST_CASE("Renaming a surface refuses empty and taken names without an undo step", "[issue893][surfaces][undo]") {
  AppCommandState st = DrawingWithSurfaces({"A", "B"});
  std::vector<std::string> log;
  CHECK_FALSE(surfedit::RenameSurface(st, 0, "", log));
  CHECK_FALSE(surfedit::RenameSurface(st, 0, "B", log));
  CHECK(LogMentions(log, "already exists"));
  CHECK(st.cadSurfaces[0].name == "A");
  CHECK_FALSE(surfedit::ApplySurfaceInfo(st, 0, "B", "x", "Standard", log));
  CHECK(st.cadSurfaces[0].description.empty());
  const bool couldUndo = CanUndo(st);
  CHECK(surfedit::RenameSurface(st, 0, "Ground", log));
  CHECK(st.cadSurfaces[0].name == "Ground");
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadSurfaces[0].name == "A");
  CHECK(CanUndo(st) == couldUndo);
}

TEST_CASE("Surface Properties applies name, description and style as one undo step", "[issue893][surfaces][undo]") {
  AppCommandState st = DrawingWithSurfaces({"A"});
  std::vector<std::string> log;
  REQUIRE(surfedit::AddStyle(st, 0, "Roads", log));
  REQUIRE(surfedit::ApplySurfaceInfo(st, 0, "Ground", "Existing ground", "Roads", log));
  CHECK(st.cadSurfaces[0].styleName == "Roads");
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadSurfaces[0].name == "A");
  CHECK(st.cadSurfaces[0].description.empty());
}

TEST_CASE("A point group is added once and removable", "[issue893][definition]") {
  AppCommandState st = DrawingWithSurfaces({"A"});
  std::vector<std::string> log;
  CHECK(surfedit::AddPointGroup(st, 0, "Ground", log));
  CHECK_FALSE(surfedit::AddPointGroup(st, 0, "Ground", log));
  CHECK(st.cadSurfaces[0].sourcePointGroups.size() == 1);
  CHECK(surfedit::RemoveDefinitionItem(st, 0, surfedit::DefinitionKind::PointGroup, 0, log));
  CHECK(st.cadSurfaces[0].sourcePointGroups.empty());
  CHECK_FALSE(surfedit::RemoveDefinitionItem(st, 0, surfedit::DefinitionKind::PointGroup, 0, log));
  CHECK_FALSE(surfedit::RemoveDefinitionItem(st, 5, surfedit::DefinitionKind::PointGroup, 0, log));
}

TEST_CASE("Reordering boundaries is undoable and snapshots BEFORE the move", "[issue893][definition][undo]") {
  AppCommandState st = DrawingWithSurfaces({"A"});
  std::vector<std::string> log;
  CadSurface& s = st.cadSurfaces[0];
  s.boundaries.push_back({11, CadBoundaryKind::Outer, "first"});
  s.boundaries.push_back({12, CadBoundaryKind::Hide, "second"});
  CHECK_FALSE(surfedit::MoveDefinitionItem(st, 0, surfedit::DefinitionKind::Boundary, 0, -1, log));
  REQUIRE(surfedit::MoveDefinitionItem(st, 0, surfedit::DefinitionKind::Boundary, 0, +1, log));
  CHECK(st.cadSurfaces[0].boundaries[0].name == "second");
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadSurfaces[0].boundaries[0].name == "first");  // REQ-069: order is meaningful
}

TEST_CASE("Definition rows reorder only within their own kind", "[issue893][definition]") {
  AppCommandState st = DrawingWithSurfaces({"A"});
  CadSurface& s = st.cadSurfaces[0];
  s.sourcePointGroups = {"G1", "G2"};
  s.breaklines.push_back({21, "bl"});
  const auto rows = surfedit::DefinitionRows(s);
  REQUIRE(rows.size() == 3);
  CHECK(surfedit::CanMoveDefinitionRow(rows, 0, +1));
  CHECK_FALSE(surfedit::CanMoveDefinitionRow(rows, 1, +1));  // next row is a breakline
  CHECK_FALSE(surfedit::CanMoveDefinitionRow(rows, 0, -1));
  REQUIRE(surfedit::MoveDefinitionRow(st, 0, rows, 0, +1));
  CHECK(st.cadSurfaces[0].sourcePointGroups[0] == "G2");
  CHECK_FALSE(surfedit::MoveDefinitionRow(st, 0, rows, 1, +1));
}

TEST_CASE("Renaming a style re-points the surfaces that use it; Standard is protected", "[issue893][styles]") {
  AppCommandState st = DrawingWithSurfaces({"A"});
  std::vector<std::string> log;
  REQUIRE(surfedit::AddStyle(st, 0, "Roads", log));
  st.cadSurfaces[0].styleName = "Roads";
  CHECK_FALSE(surfedit::RenameStyle(st, 0, "Base", log));  // Standard
  CHECK_FALSE(surfedit::RenameStyle(st, 1, "Standard", log));
  CHECK_FALSE(surfedit::RenameStyle(st, 1, "   ", log));
  CHECK(surfedit::RenameStyle(st, 1, "  Paving ", log));
  CHECK(st.surfaceStyles[1].name == "Paving");
  CHECK(st.cadSurfaces[0].styleName == "Paving");
}

TEST_CASE("A new style is a copy and a duplicate name is refused", "[issue893][styles]") {
  AppCommandState st = DrawingWithSurfaces({});
  std::vector<std::string> log;
  st.surfaceStyles[0].minorIntervalFt = 1.0;
  REQUIRE(surfedit::AddStyle(st, 0, "Copy", log));
  CHECK(st.surfaceStyles.back().minorIntervalFt == 1.0);
  CHECK_FALSE(surfedit::AddStyle(st, 0, "Copy", log));
  CHECK_FALSE(surfedit::AddStyle(st, 0, "", log));
  CHECK(st.surfaceStyles.size() == 2);
}

TEST_CASE("Deleting a used style keeps the name and says how many surfaces fall back", "[issue893][styles]") {
  AppCommandState st = DrawingWithSurfaces({"A", "B"});
  std::vector<std::string> log;
  REQUIRE(surfedit::AddStyle(st, 0, "Roads", log));
  st.cadSurfaces[0].styleName = "Roads";
  st.cadSurfaces[1].styleName = "Roads";
  CHECK_FALSE(surfedit::DeleteStyle(st, 0, log));  // Standard
  log.clear();
  REQUIRE(surfedit::DeleteStyle(st, 1, log));
  CHECK(st.cadSurfaces[0].styleName == "Roads");  // REQ-070: re-creating the style adopts them back
  CHECK(LogMentions(log, "2 surface(s) using it now draw with \"Standard\""));
}

TEST_CASE("Style edits made without undo recording push no snapshot", "[issue893][styles][undo]") {
  AppCommandState st = DrawingWithSurfaces({});
  std::vector<std::string> log;
  const bool couldUndo = CanUndo(st);
  REQUIRE(surfedit::AddStyle(st, 0, "Scratch", log, /*recordUndo=*/false));
  surfedit::SetStyleIntervals(st, 1, 1.0, 5.0, /*recordUndo=*/false);
  REQUIRE(surfedit::RenameStyle(st, 1, "Renamed", log, /*recordUndo=*/false));
  REQUIRE(surfedit::DeleteStyle(st, 1, log, /*recordUndo=*/false));
  CHECK(CanUndo(st) == couldUndo);
}

TEST_CASE("Setting the analysis mode copies a style that other surfaces share", "[issue893][analysis]") {
  AppCommandState st = DrawingWithSurfaces({"A", "B"});
  std::vector<std::string> log;
  surfedit::SetAnalysisMode(st, 0, SurfaceAnalysisMode::Slope, log);
  const SurfaceStyle* first = SurfaceStyles::Resolve(st.surfaceStyles, st.cadSurfaces[0].styleName);
  const SurfaceStyle* second = SurfaceStyles::Resolve(st.surfaceStyles, st.cadSurfaces[1].styleName);
  REQUIRE(first != nullptr);
  REQUIRE(second != nullptr);
  CHECK(first->analysisMode == SurfaceAnalysisMode::Slope);
  CHECK(second->analysisMode == SurfaceAnalysisMode::None);
  CHECK(first->name != second->name);
}

TEST_CASE("Deleting a surface is one undo step", "[issue893][surfaces][undo]") {
  AppCommandState st = DrawingWithSurfaces({"A", "B"});
  std::vector<std::string> log;
  surfedit::DeleteSurface(st, 0, log);
  REQUIRE(st.cadSurfaces.size() == 1);
  REQUIRE(DoUndo(st, log));
  CHECK(st.cadSurfaces.size() == 2);
  CHECK(st.cadSurfaces[0].name == "A");
}

TEST_CASE("Next surface name skips names that are taken", "[issue893][surfaces]") {
  AppCommandState st = DrawingWithSurfaces({});
  CHECK(surfedit::NextSurfaceName(st) == "Surface");
  std::vector<std::string> log;
  REQUIRE(CreateSurfaceFromPointGroups(st, "Surface", {}, log) >= 0);
  CHECK(surfedit::NextSurfaceName(st) == "Surface (2)");
}
