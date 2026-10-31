#pragma once

#include "SheetGrid.hpp"

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// DATAEXTRACTION column model and grid builder (REQ-467, issue #804). Pure: the command layer
// gathers ExtractRecords from the selection, this turns them into a table.

enum class ExtractColumn : std::uint8_t { Type, Layer, Text, X, Y, Z };

struct ExtractRecord {
  std::string type;   // object type name, for example LINE or TEXT
  std::string layer;
  std::string text;
  bool hasText = false;   // the object carries text; an empty Text cell is otherwise left blank
  bool hasPoint = false;  // the object has a defining point; an empty X/Y/Z cell is otherwise left blank
  double x = 0.0, y = 0.0, z = 0.0;
};

inline const char* ExtractColumnName(ExtractColumn c) {
  switch (c) {
    case ExtractColumn::Type:  return "TYPE";
    case ExtractColumn::Layer: return "LAYER";
    case ExtractColumn::Text:  return "TEXT";
    case ExtractColumn::X:     return "X";
    case ExtractColumn::Y:     return "Y";
    case ExtractColumn::Z:     return "Z";
  }
  return "";
}

inline std::string ExtractUpper(std::string s) {
  for (char& ch : s)
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}

// Parses the typed column list: empty or ALL selects every column; otherwise a comma-separated list of
// TYPE, LAYER, TEXT, X, Y, Z in any case. Output order follows the typed order. Returns false with a
// reason for an unknown or repeated name.
inline bool ParseExtractColumns(const std::string& typed, std::vector<ExtractColumn>* out, std::string* err) {
  out->clear();
  std::string t = typed;
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front())))
    t.erase(t.begin());
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
    t.pop_back();
  if (t.empty() || ExtractUpper(t) == "ALL") {
    out->assign({ExtractColumn::Type, ExtractColumn::Layer, ExtractColumn::Text, ExtractColumn::X, ExtractColumn::Y,
                 ExtractColumn::Z});
    return true;
  }
  const ExtractColumn all[] = {ExtractColumn::Type, ExtractColumn::Layer, ExtractColumn::Text,
                               ExtractColumn::X,    ExtractColumn::Y,     ExtractColumn::Z};
  size_t start = 0;
  while (start <= t.size()) {
    size_t comma = t.find(',', start);
    std::string part = t.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
      part.erase(part.begin());
    while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
      part.pop_back();
    const std::string name = ExtractUpper(part);
    bool found = false;
    for (ExtractColumn c : all) {
      if (name == ExtractColumnName(c)) {
        for (ExtractColumn seen : *out) {
          if (seen == c) {
            if (err) *err = "column " + name + " is listed twice";
            return false;
          }
        }
        out->push_back(c);
        found = true;
        break;
      }
    }
    if (!found) {
      if (err) *err = "unknown column '" + part + "' (use TYPE, LAYER, TEXT, X, Y, Z or ALL)";
      return false;
    }
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  return true;
}

// Builds a header row and one data row per record, in the order of \p cols.
inline SheetGrid::Grid BuildExtractGrid(const std::vector<ExtractColumn>& cols, const std::vector<ExtractRecord>& recs) {
  SheetGrid::Grid grid;
  SheetGrid::Row header;
  for (ExtractColumn c : cols)
    header.emplace_back(std::string(ExtractColumnName(c)));
  grid.push_back(std::move(header));
  for (const ExtractRecord& r : recs) {
    SheetGrid::Row row;
    for (ExtractColumn c : cols) {
      switch (c) {
        case ExtractColumn::Type:  row.emplace_back(r.type); break;
        case ExtractColumn::Layer: row.emplace_back(r.layer); break;
        case ExtractColumn::Text:
          row.push_back(r.hasText ? SheetGrid::Cell(r.text) : SheetGrid::Cell());
          break;
        case ExtractColumn::X:
          row.push_back(r.hasPoint ? SheetGrid::Cell(r.x) : SheetGrid::Cell());
          break;
        case ExtractColumn::Y:
          row.push_back(r.hasPoint ? SheetGrid::Cell(r.y) : SheetGrid::Cell());
          break;
        case ExtractColumn::Z:
          row.push_back(r.hasPoint ? SheetGrid::Cell(r.z) : SheetGrid::Cell());
          break;
      }
    }
    grid.push_back(std::move(row));
  }
  return grid;
}
