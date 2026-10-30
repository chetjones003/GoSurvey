#pragma once

#include <string>
#include <variant>
#include <vector>

// A plain table of cells shared by the spreadsheet readers and writers and by DATAEXTRACTION
// (REQ-467, REQ-468, issue #804). Pure value types: no file, UI or spreadsheet library dependency.
namespace SheetGrid {

using Cell = std::variant<std::monostate, std::string, double>;  // empty, text, number
using Row = std::vector<Cell>;
using Grid = std::vector<Row>;  // grid[row][column]; rows may be ragged

}  // namespace SheetGrid
