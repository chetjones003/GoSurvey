#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "util/cadtablestyle.hpp"

// REQ-466, REQ-474, REQ-475, REQ-476 (GitHub issue #804, D-2026-10-30-b): table styles, point table cells, sizing,
// and cell hit-testing.

TEST_CASE("A new drawing has the three built-in table styles (REQ-475)", "[table][issue804]") {
  const std::vector<CadTableStyle> styles = CadTableBuiltinStyles();
  REQUIRE(styles.size() == 3);
  REQUIRE(styles[0].name == "Standard");
  REQUIRE_FALSE(styles[0].point);
  REQUIRE(styles[1].name == "NameNEZD");
  REQUIRE(styles[1].point);
  REQUIRE(styles[1].pointColumns.size() == 4);
  REQUIRE(styles[2].name == "NameNEZD with Comments");
  REQUIRE(styles[2].pointColumns.size() == 5);
  REQUIRE(styles[2].pointColumns.back() == CadPointTableColumn::Description);
}

TEST_CASE("Table style names match case-insensitively (REQ-475)", "[table][issue804]") {
  const std::vector<CadTableStyle> styles = CadTableBuiltinStyles();
  REQUIRE(CadTableFindStyle(styles, "namenezd") != nullptr);
  REQUIRE(CadTableFindStyle(styles, "NAMENEZD WITH COMMENTS") != nullptr);
  REQUIRE(CadTableFindStyle(styles, "NameNEZ") == nullptr);
}

TEST_CASE("A point table has a header row then one row per point, in order (REQ-474)", "[table][issue804]") {
  const std::vector<CadTableStyle> styles = CadTableBuiltinStyles();
  const CadTableStyle* nezd = CadTableFindStyle(styles, "NameNEZD");
  REQUIRE(nezd != nullptr);
  std::vector<CadTablePointRow> rows(2);
  rows[0] = {1, 5000.5, 1000.25, 100.0, "EG"};
  rows[1] = {5, 5001.0, 1002.0, 101.5, "CL"};
  const std::vector<std::string> cells = CadPointTableCells(*nezd, rows, 2);
  REQUIRE(cells.size() == 3 * 4);
  REQUIRE(cells[0] == "Point Name");
  REQUIRE(cells[3] == "Elevation");
  REQUIRE(cells[4] == "1");
  REQUIRE(cells[5] == "5000.50");
  REQUIRE(cells[6] == "1000.25");
  REQUIRE(cells[7] == "100.00");
  REQUIRE(cells[8] == "5");
}

TEST_CASE("NameNEZD with Comments adds the description column (REQ-474)", "[table][issue804]") {
  const std::vector<CadTableStyle> styles = CadTableBuiltinStyles();
  const CadTableStyle* withComments = CadTableFindStyle(styles, "NameNEZD with Comments");
  REQUIRE(withComments != nullptr);
  const std::vector<CadTablePointRow> rows = {{3, 1.0, 2.0, 3.0, "FENCE"}};
  const std::vector<std::string> cells = CadPointTableCells(*withComments, rows, 1);
  REQUIRE(cells.size() == 2 * 5);
  REQUIRE(cells[4] == "Description");
  REQUIRE(cells[9] == "FENCE");
}

TEST_CASE("A table takes its size and cell count from its style (REQ-475)", "[table][issue804]") {
  const CadTableStyle standard = CadTableBuiltinStyles()[0];
  CadTable t;
  CadTableApplyStyleSize(&t, standard, 2, 3, 2.0f);
  REQUIRE(t.cols == 3);
  REQUIRE(t.cells.size() == 6);
  REQUIRE(t.width == Catch::Approx(3.0f * standard.columnWidthInches * 2.0f));
  REQUIRE(t.height == Catch::Approx(2.0f * standard.rowHeightInches * 2.0f));
  REQUIRE(t.styleName == "Standard");
}

TEST_CASE("Cell hit-testing returns the row-major cell index (REQ-476)", "[table][issue804]") {
  CadTable t;
  t.insX = 10.f;
  t.insY = 10.f;
  t.cols = 3;
  t.width = 30.f;
  t.height = 20.f;
  t.cells.assign(6, std::string());
  // Local coordinates are downward-Y from the upper-left corner: cell (row 0, col 0) is near (10,10).
  REQUIRE(CadTableCellAtWorld(t, 11.f, 9.f) == 0);
  REQUIRE(CadTableCellAtWorld(t, 25.f, 9.f) == 1);
  REQUIRE(CadTableCellAtWorld(t, 39.f, 9.f) == 2);
  // The second row is below the insertion point: world Y goes negative.
  REQUIRE(CadTableCellAtWorld(t, 11.f, -5.f) == 3);
  REQUIRE(CadTableCellAtWorld(t, 39.f, -5.f) == 5);
}

TEST_CASE("Points outside a table are not in any cell (REQ-476)", "[table][issue804]") {
  CadTable t;
  t.insX = 0.f;
  t.insY = 0.f;
  t.cols = 2;
  t.width = 10.f;
  t.height = 10.f;
  t.cells.assign(4, std::string());
  REQUIRE(CadTableCellAtWorld(t, -1.f, -1.f) == -1);
  REQUIRE(CadTableCellAtWorld(t, 11.f, 5.f) == -1);
  REQUIRE(CadTableCellAtWorld(t, 5.f, 11.f) == -1);
}
