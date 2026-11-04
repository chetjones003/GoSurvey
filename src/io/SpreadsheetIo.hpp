#pragma once

#include "SheetGrid.hpp"

#include <string>

// Spreadsheet files for DATAEXTRACTION and DATALINK (REQ-467, REQ-468, issue #804).
// CSV is written and read by hand (RFC 4180 quoting). .xlsx goes through the vendored OpenXLSX
// library (third_party/OpenXLSX, D-2026-10-30-a).
namespace SpreadsheetIo {

/// Writes CSV, replacing an existing file. Numbers use their shortest exact text representation.
/// Returns false on an open or write failure and sets err when it is non-null.
bool WriteCsv(const std::string& path, const SheetGrid::Grid& grid, std::string* err);
/// Reads CSV cells as text, ignoring a leading UTF-8 BOM. out must be non-null.
/// Returns false on an open failure or unclosed quoted field and sets err when non-null.
/// On a parse failure, out may contain completed rows; on an open failure, out is unchanged.
bool ReadCsv(const std::string& path, SheetGrid::Grid* out, std::string* err);

/// Replaces the file with an XLSX workbook containing the grid in Sheet1. Empty cells and empty
/// strings are omitted. Returns false and sets err when non-null if a standard exception is caught.
bool WriteXlsx(const std::string& path, const SheetGrid::Grid& grid, std::string* err);
/// Reads the first XLSX worksheet into out, which must be non-null. Numbers become doubles;
/// booleans become TRUE or FALSE text and error cells become #ERROR. out is cleared first and may
/// contain rows read before failure. Returns false if there is no worksheet or a standard
/// exception is caught, setting err when non-null.
bool ReadXlsx(const std::string& path, SheetGrid::Grid* out, std::string* err);

}  // namespace SpreadsheetIo
