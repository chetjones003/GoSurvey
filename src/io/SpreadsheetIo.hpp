#pragma once

#include "SheetGrid.hpp"

#include <string>

// Spreadsheet files for DATAEXTRACTION and DATALINK (REQ-467, REQ-468, issue #804).
// CSV is written and read by hand (RFC 4180 quoting). .xlsx goes through the vendored OpenXLSX
// library (third_party/OpenXLSX, D-2026-10-30-a). Every function returns false with a reason in
// \p err; none of them throws.
namespace SpreadsheetIo {

// CSV has no types: numbers are written in their shortest exact form, and every cell reads back as text.
bool WriteCsv(const std::string& path, const SheetGrid::Grid& grid, std::string* err);
bool ReadCsv(const std::string& path, SheetGrid::Grid* out, std::string* err);

// .xlsx reads and writes the first worksheet. Numbers stay numbers; text stays text.
bool WriteXlsx(const std::string& path, const SheetGrid::Grid& grid, std::string* err);
bool ReadXlsx(const std::string& path, SheetGrid::Grid* out, std::string* err);

}  // namespace SpreadsheetIo
