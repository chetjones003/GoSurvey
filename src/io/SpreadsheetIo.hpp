#pragma once

#include "SheetGrid.hpp"

#include <string>

// Spreadsheet files for DATAEXTRACTION and DATALINK (REQ-467, REQ-468, issue #804).
// CSV is written and read by hand (RFC 4180 quoting). .xlsx goes through the vendored OpenXLSX
// library (third_party/OpenXLSX, D-2026-10-30-a).
namespace SpreadsheetIo {

/// Overwrites a CSV file; numbers use their shortest exact text representation.
/// Returns false on an open or write failure and sets err if it is non-null.
bool WriteCsv(const std::string& path, const SheetGrid::Grid& grid, std::string* err);
/// Reads CSV fields as text, ignoring a leading UTF-8 BOM. out must be non-null.
/// Returns false on an open failure or unclosed quoted field and sets err if non-null.
/// On a parse failure, out can contain rows parsed before the error.
bool ReadCsv(const std::string& path, SheetGrid::Grid* out, std::string* err);

/// Overwrites an XLSX file with a Sheet1 worksheet. Empty cells and empty strings are omitted.
/// Returns false and sets err if non-null when a std::exception is caught.
bool WriteXlsx(const std::string& path, const SheetGrid::Grid& grid, std::string* err);
/// Reads the first XLSX worksheet into out, which must be non-null. Numbers become doubles;
/// booleans become "TRUE" or "FALSE" and error cells become "#ERROR".
/// Returns false if there is no worksheet or a std::exception is caught, setting err if non-null.
/// out is cleared before reading and may contain rows read before a failure.
bool ReadXlsx(const std::string& path, SheetGrid::Grid* out, std::string* err);

}  // namespace SpreadsheetIo
