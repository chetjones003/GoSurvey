#pragma once

#include "SheetGrid.hpp"

#include <string>

// Spreadsheet files for DATAEXTRACTION and DATALINK (REQ-467, REQ-468, issue #804).
// CSV is written and read by hand (RFC 4180 quoting). .xlsx goes through the vendored OpenXLSX
// library (third_party/OpenXLSX, D-2026-10-30-a).
namespace SpreadsheetIo {

/// Writes CSV, replacing an existing file. Numbers use their shortest exact form.
/// Returns false on an open or write failure and sets err when it is non-null.
bool WriteCsv(const std::string& path, const SheetGrid::Grid& grid, std::string* err);
/// Reads CSV cells as text, ignoring a UTF-8 BOM. Returns false on an open failure or an
/// unclosed quoted field and sets err when it is non-null. On a parse failure, out can contain
/// completed rows; on an open failure, out is unchanged.
bool ReadCsv(const std::string& path, SheetGrid::Grid* out, std::string* err);

/// Replaces the file with an XLSX workbook containing the grid in Sheet1. Returns false and sets
/// err when non-null if a standard exception is caught while creating or writing the workbook.
bool WriteXlsx(const std::string& path, const SheetGrid::Grid& grid, std::string* err);
/// Reads the first XLSX worksheet; numbers become doubles, booleans become TRUE or FALSE text,
/// and error cells become #ERROR. Clears out first; a failure can leave it partially populated.
/// Returns false for a workbook without worksheets or a caught standard exception, setting err
/// when non-null.
bool ReadXlsx(const std::string& path, SheetGrid::Grid* out, std::string* err);

}  // namespace SpreadsheetIo
