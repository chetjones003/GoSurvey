#include "util/cadtable.hpp"

#include "GsAnnotationJson.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("A 2x2 TABLE lays out four cells inside its box", "[req148][table]") {
  std::vector<std::string> cells{"A", "B", "C", "D"};
  REQUIRE(CadTableRowCount(2, cells) == 2);
  std::vector<CadTableCellRect> rects;
  CadTableLayoutCells(0.f, 0.f, 10.f, 4.f, 2, cells, &rects);
  REQUIRE(rects.size() == 4);
  for (const CadTableCellRect& r : rects) {
    CHECK(r.x0 >= 0.f);
    CHECK(r.x1 <= 10.f);
    CHECK(r.y0 >= 0.f);
    CHECK(r.y1 <= 4.f);
    CHECK(r.x1 > r.x0);
    CHECK(r.y1 > r.y0);
  }
  CHECK(rects[0].y1 == 4.f);
  CHECK(rects[2].y0 == 0.f);
}

TEST_CASE("TABLE entity JSON round-trips cells and insertion", "[req148][table][gs]") {
  CadTable t;
  t.insX = 1.f;
  t.insY = 2.f;
  t.insZ = 3.f;
  t.width = 10.f;
  t.height = 4.f;
  t.rotationRad = 0.25f;
  t.cols = 2;
  t.cells = {"Item", "Value", "Cut", "1"};
  t.plottedHeightInches = 0.125f;
  nlohmann::json o;
  CadTableToJson(t, o);
  const CadTable b = CadTableFromJson(o);
  CHECK(b.insX == 1.f);
  CHECK(b.insY == 2.f);
  CHECK(b.cols == 2);
  REQUIRE(b.cells.size() == 4);
  CHECK(b.cells[2] == "Cut");
  CHECK(b.rotationRad == 0.25f);
}

TEST_CASE("MOVE of a TABLE changes its insertion", "[req148][table]") {
  CadTable t;
  t.insX = 10.f;
  t.insY = 20.f;
  CadTableTranslate(&t, 5.f, -3.f);
  CHECK(t.insX == 15.f);
  CHECK(t.insY == 17.f);
}

TEST_CASE("CadTableHitCell returns the row-major index of a point inside a cell", "[req148][table]") {
  CadTable t;
  t.insX = 0.f;
  t.insY = 4.f;
  t.width = 10.f;
  t.height = 4.f;
  t.cols = 2;
  t.cells = {"A", "B", "C", "D"};
  CHECK(CadTableHitCell(t, 2.f, 3.f) == 0);
  CHECK(CadTableHitCell(t, 8.f, 3.f) == 1);
  CHECK(CadTableHitCell(t, 2.f, 1.f) == 2);
  CHECK(CadTableHitCell(t, 8.f, 1.f) == 3);
  CHECK(CadTableHitCell(t, -1.f, 2.f) == -1);
}

TEST_CASE("CadTableFitToContent sizes the box to the longest cell and the row count", "[req148][table]") {
  CadTable t;
  t.cols = 2;
  t.cells = {"Item", "Value", "Cut", "2878.1423 yd3"};
  t.plottedHeightInches = 0.125f;
  t.width = 48.f;
  t.height = 8.f;
  const float mup = 80.f;
  const float hText = CadTableHeightWorld(t, mup);
  CadTableFitToContent(&t, mup);
  const float needInner = CadTableEstimatedTextWidth("2878.1423 yd3", hText);
  CHECK(t.width >= 2.f * (needInner + hText * kCadTableCellPadXEm * 2.f) - 1.e-3f);
  CHECK(t.height >= 2.f * hText * kCadTableRowToText - 1.e-3f);
  CHECK(t.width > 48.f);
}

TEST_CASE("CadTableFitToContent grows width when a cell string gets longer", "[req148][table]") {
  CadTable t;
  t.cols = 2;
  t.cells = {"A", "B"};
  t.plottedHeightInches = 0.125f;
  const float mup = 80.f;
  CadTableFitToContent(&t, mup);
  const float w0 = t.width;
  t.cells[1] = "a much longer value string than before";
  CadTableFitToContent(&t, mup);
  CHECK(t.width > w0);
}

TEST_CASE("legacy TABLE annotation JSON still round-trips cells", "[req148][table][gs]") {
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Table;
  a.tableCols = 2;
  a.tableCells = {"Item", "Value", "Cut", "1"};
  a.boxMaxX = 8.f;
  a.boxMaxY = 4.f;
  nlohmann::json o;
  CadAnnotationToJson(a, o);
  const CadAnnotation b = CadAnnotationFromJson(o);
  CHECK(b.kind == CadAnnotation::Kind::Table);
  CHECK(b.tableCols == 2);
  REQUIRE(b.tableCells.size() == 4);
  CHECK(b.tableCells[2] == "Cut");
}
