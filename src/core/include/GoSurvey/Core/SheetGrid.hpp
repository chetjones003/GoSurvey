#pragma once

#include <string>
#include <variant>
#include <vector>

namespace SheetGrid {

/// Format-neutral tabular values shared by domain reports and IO adapters.
using Cell = std::variant<std::monostate, std::string, double>;
using Row = std::vector<Cell>;
using Grid = std::vector<Row>;

}  // namespace SheetGrid
