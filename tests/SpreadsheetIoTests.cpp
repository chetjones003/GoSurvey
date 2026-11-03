#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "SpreadsheetIo.hpp"
#include "util/DataExtract.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

// REQ-467 / REQ-468 (issue #804): column parsing, grid building, and CSV and .xlsx round trips.

namespace {

std::filesystem::path ScratchDir(const char* name) {
  auto dir = std::filesystem::temp_directory_path() / ("gosurvey-sheet-" + std::string(name));
  std::filesystem::create_directories(dir);
  return dir;
}

const std::string& Str(const SheetGrid::Cell& c) { return std::get<std::string>(c); }

}  // namespace

TEST_CASE("DATAEXTRACTION column list: empty and ALL select every column in order", "[spreadsheet][issue804]") {
  std::vector<ExtractColumn> cols;
  std::string err;
  REQUIRE(ParseExtractColumns("", &cols, &err));
  REQUIRE(cols.size() == 6);
  REQUIRE(ParseExtractColumns("  all ", &cols, &err));
  REQUIRE(cols.size() == 6);
  REQUIRE(cols.front() == ExtractColumn::Type);
  REQUIRE(cols.back() == ExtractColumn::Z);
}

TEST_CASE("DATAEXTRACTION column list keeps the typed order and accepts any case", "[spreadsheet][issue804]") {
  std::vector<ExtractColumn> cols;
  std::string err;
  REQUIRE(ParseExtractColumns("x, layer ,text", &cols, &err));
  REQUIRE(cols.size() == 3);
  REQUIRE(cols[0] == ExtractColumn::X);
  REQUIRE(cols[1] == ExtractColumn::Layer);
  REQUIRE(cols[2] == ExtractColumn::Text);
}

TEST_CASE("DATAEXTRACTION column list refuses unknown and repeated names", "[spreadsheet][issue804]") {
  std::vector<ExtractColumn> cols;
  std::string err;
  REQUIRE_FALSE(ParseExtractColumns("TYPE,COLOR", &cols, &err));
  REQUIRE(err.find("COLOR") != std::string::npos);
  REQUIRE_FALSE(ParseExtractColumns("X,x", &cols, &err));
  REQUIRE(err.find("twice") != std::string::npos);
}

TEST_CASE("DATAEXTRACTION grid has a header row and blank cells where an object has no data", "[spreadsheet][issue804]") {
  std::vector<ExtractRecord> recs(2);
  recs[0].type = "LINE";
  recs[0].layer = "0";
  recs[0].hasPoint = true;
  recs[0].x = 1.5;
  recs[0].y = -2.0;
  recs[0].z = 0.0;
  recs[1].type = "TEXT";
  recs[1].layer = "Notes";
  recs[1].hasText = true;
  recs[1].text = "Corner, north";
  std::vector<ExtractColumn> cols = {ExtractColumn::Type, ExtractColumn::Text, ExtractColumn::X};
  const SheetGrid::Grid grid = BuildExtractGrid(cols, recs);
  REQUIRE(grid.size() == 3);
  REQUIRE(Str(grid[0][0]) == "TYPE");
  REQUIRE(Str(grid[0][2]) == "X");
  REQUIRE(Str(grid[1][0]) == "LINE");
  REQUIRE(std::holds_alternative<std::monostate>(grid[1][1]));  // no text on a line
  REQUIRE(std::get<double>(grid[1][2]) == Catch::Approx(1.5));
  REQUIRE(Str(grid[2][1]) == "Corner, north");
  REQUIRE(std::holds_alternative<std::monostate>(grid[2][2]));  // no point recorded
}

TEST_CASE("CSV writes RFC 4180 quoting and reads the same grid back", "[spreadsheet][issue804]") {
  const auto dir = ScratchDir("csv");
  const std::string path = (dir / "round.csv").string();
  SheetGrid::Grid grid;
  grid.push_back({SheetGrid::Cell(std::string("TYPE")), SheetGrid::Cell(std::string("TEXT"))});
  grid.push_back({SheetGrid::Cell(std::string("TEXT")), SheetGrid::Cell(std::string("say \"hi\", then\nleave"))});
  grid.push_back({SheetGrid::Cell(0.1), SheetGrid::Cell()});
  std::string err;
  REQUIRE(SpreadsheetIo::WriteCsv(path, grid, &err));
  SheetGrid::Grid back;
  REQUIRE(SpreadsheetIo::ReadCsv(path, &back, &err));
  REQUIRE(back.size() == 3);
  REQUIRE(Str(back[1][1]) == "say \"hi\", then\nleave");
  REQUIRE(Str(back[2][0]) == "0.1");  // numbers are written in shortest exact form, read back as text
  REQUIRE(std::holds_alternative<std::string>(back[2][1]));
  REQUIRE(std::get<std::string>(back[2][1]).empty());
}

TEST_CASE("CSV read skips a UTF-8 byte order mark and reports an unclosed quote", "[spreadsheet][issue804]") {
  const auto dir = ScratchDir("csv-bom");
  const std::string good = (dir / "bom.csv").string();
  {
    std::ofstream f(good, std::ios::binary);
    f << "\xEF\xBB\xBF" "A,B\r\n1,2\r\n";
  }
  SheetGrid::Grid g;
  std::string err;
  REQUIRE(SpreadsheetIo::ReadCsv(good, &g, &err));
  REQUIRE(Str(g[0][0]) == "A");
  const std::string bad = (dir / "bad.csv").string();
  {
    std::ofstream f(bad, std::ios::binary);
    f << "\"open,field\n";
  }
  REQUIRE_FALSE(SpreadsheetIo::ReadCsv(bad, &g, &err));
  REQUIRE(err.find("unclosed") != std::string::npos);
}

TEST_CASE("XLSX round trip keeps numbers as numbers and text as text", "[spreadsheet][issue804]") {
  const auto dir = ScratchDir("xlsx");
  const std::string path = (dir / "round.xlsx").string();
  SheetGrid::Grid grid;
  grid.push_back({SheetGrid::Cell(std::string("TYPE")), SheetGrid::Cell(std::string("X")),
                  SheetGrid::Cell(std::string("TEXT"))});
  grid.push_back({SheetGrid::Cell(std::string("TEXT")), SheetGrid::Cell(12.25),
                  SheetGrid::Cell(std::string("Corner, \"north\" & <b>"))});
  grid.push_back({SheetGrid::Cell(std::string("LINE")), SheetGrid::Cell(), SheetGrid::Cell()});
  std::string err;
  REQUIRE(SpreadsheetIo::WriteXlsx(path, grid, &err));
  SheetGrid::Grid back;
  REQUIRE(SpreadsheetIo::ReadXlsx(path, &back, &err));
  REQUIRE(back.size() == 3);
  REQUIRE(Str(back[0][0]) == "TYPE");
  REQUIRE(std::get<double>(back[1][1]) == Catch::Approx(12.25));
  REQUIRE(Str(back[1][2]) == "Corner, \"north\" & <b>");
  REQUIRE(Str(back[2][0]) == "LINE");
}

TEST_CASE("XLSX read reports a missing file instead of throwing", "[spreadsheet][issue804]") {
  const auto dir = ScratchDir("xlsx-missing");
  SheetGrid::Grid g;
  std::string err;
  REQUIRE_FALSE(SpreadsheetIo::ReadXlsx((dir / "absent.xlsx").string(), &g, &err));
  REQUIRE_FALSE(err.empty());
}
