#include "SpreadsheetIo.hpp"

#include <OpenXLSX.hpp>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace SpreadsheetIo {
namespace {

std::string NumberText(double v) {
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof(buf), v);
  return std::string(buf, res.ptr);
}

std::string CsvField(const std::string& s) {
  const bool quote = s.find_first_of(",\"\r\n") != std::string::npos;
  if (!quote)
    return s;
  std::string out = "\"";
  for (char ch : s) {
    if (ch == '"')
      out += '"';
    out += ch;
  }
  out += '"';
  return out;
}

std::string CellAsCsvText(const SheetGrid::Cell& c) {
  if (const auto* s = std::get_if<std::string>(&c))
    return CsvField(*s);
  if (const auto* d = std::get_if<double>(&c))
    return NumberText(*d);
  return "";
}

// Splits CSV text into rows of fields, honouring quoted fields with embedded commas, quotes and newlines.
bool ParseCsv(const std::string& text, SheetGrid::Grid* out, std::string* err) {
  out->clear();
  SheetGrid::Row row;
  std::string field;
  bool inQuotes = false;
  bool fieldStarted = false;
  auto endField = [&]() {
    row.emplace_back(field);
    field.clear();
    fieldStarted = false;
  };
  auto endRow = [&]() {
    endField();
    out->push_back(std::move(row));
    row.clear();
  };
  for (size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (inQuotes) {
      if (ch == '"') {
        if (i + 1 < text.size() && text[i + 1] == '"') {
          field += '"';
          ++i;
        } else {
          inQuotes = false;
        }
      } else {
        field += ch;
      }
      continue;
    }
    if (ch == '"' && !fieldStarted) {
      inQuotes = true;
      fieldStarted = true;
    } else if (ch == ',') {
      endField();
      fieldStarted = false;
    } else if (ch == '\r' || ch == '\n') {
      if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
        ++i;
      endRow();
    } else {
      field += ch;
      fieldStarted = true;
    }
  }
  if (inQuotes) {
    if (err)
      *err = "CSV has an unclosed quoted field";
    return false;
  }
  if (fieldStarted || !row.empty())
    endRow();
  return true;
}

std::string CellText(const OpenXLSX::XLCellValue& v) {
  switch (v.type()) {
    case OpenXLSX::XLValueType::Boolean:
      return v.get<bool>() ? "TRUE" : "FALSE";
    case OpenXLSX::XLValueType::Error:
      return "#ERROR";
    default:
      return v.get<std::string>();
  }
}

}  // namespace

bool WriteCsv(const std::string& path, const SheetGrid::Grid& grid, std::string* err) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    if (err)
      *err = "cannot write " + path;
    return false;
  }
  for (const SheetGrid::Row& row : grid) {
    for (size_t c = 0; c < row.size(); ++c) {
      if (c > 0)
        f << ',';
      f << CellAsCsvText(row[c]);
    }
    f << "\r\n";
  }
  f.close();
  if (!f) {
    if (err)
      *err = "failed while writing " + path;
    return false;
  }
  return true;
}

bool ReadCsv(const std::string& path, SheetGrid::Grid* out, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (err)
      *err = "cannot open " + path;
    return false;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  std::string text = ss.str();
  // A UTF-8 byte order mark from Excel or a text editor is not part of the data.
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
    text.erase(0, 3);
  return ParseCsv(text, out, err);
}

bool WriteXlsx(const std::string& path, const SheetGrid::Grid& grid, std::string* err) {
  try {
    OpenXLSX::XLDocument doc;
    doc.create(path, true);
    OpenXLSX::XLWorksheet wks = doc.workbook().worksheet("Sheet1");
    for (size_t r = 0; r < grid.size(); ++r) {
      for (size_t c = 0; c < grid[r].size(); ++c) {
        const SheetGrid::Cell& cell = grid[r][c];
        if (const auto* s = std::get_if<std::string>(&cell)) {
          if (!s->empty())
            wks.cell(static_cast<uint32_t>(r + 1), static_cast<uint16_t>(c + 1)).value() = *s;
        } else if (const auto* d = std::get_if<double>(&cell)) {
          wks.cell(static_cast<uint32_t>(r + 1), static_cast<uint16_t>(c + 1)).value() = *d;
        }
      }
    }
    doc.save();
    doc.close();
    return true;
  } catch (const std::exception& e) {
    if (err)
      *err = std::string("cannot write ") + path + ": " + e.what();
    return false;
  }
}

bool ReadXlsx(const std::string& path, SheetGrid::Grid* out, std::string* err) {
  out->clear();
  try {
    OpenXLSX::XLDocument doc;
    doc.open(path);
    const std::vector<std::string> names = doc.workbook().worksheetNames();
    if (names.empty()) {
      if (err)
        *err = path + " has no worksheets";
      return false;
    }
    OpenXLSX::XLWorksheet wks = doc.workbook().worksheet(names.front());
    const uint32_t rows = wks.rowCount();
    const uint16_t cols = wks.columnCount();
    for (uint32_t r = 1; r <= rows; ++r) {
      SheetGrid::Row row;
      for (uint16_t c = 1; c <= cols; ++c) {
        const OpenXLSX::XLCellValue v = wks.cell(r, c).value();
        switch (v.type()) {
          case OpenXLSX::XLValueType::Empty:
            row.emplace_back();
            break;
          case OpenXLSX::XLValueType::Integer:
            row.emplace_back(static_cast<double>(v.get<int64_t>()));
            break;
          case OpenXLSX::XLValueType::Float:
            row.emplace_back(v.get<double>());
            break;
          default:
            row.emplace_back(CellText(v));
            break;
        }
      }
      out->push_back(std::move(row));
    }
    doc.close();
    return true;
  } catch (const std::exception& e) {
    if (err)
      *err = std::string("cannot read ") + path + ": " + e.what();
    return false;
  }
}

}  // namespace SpreadsheetIo
