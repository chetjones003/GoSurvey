#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

namespace plotstyle {

struct Style {
  std::string name;
  int color = -1;                 // -1 means preserve effective object color
  int lineweightIndex = 0;       // 0 means preserve object/layer lineweight
  int screening = 100;
  bool grayscale = false;
  bool explicitColor = false;
};

struct Table {
  bool colorDependent = false;
  std::unordered_map<int, Style> byAci;
  std::map<std::string, Style, std::less<>> byName;
  std::unordered_map<int, double> lineweightsMm;
};

// Reads an existing AutoCAD CTB/STB file. False includes a user-facing reason for corrupt,
// unsupported, or missing tables; callers must not silently ignore a configured table.
bool Load(const std::filesystem::path& path, Table* out, std::string* error);

const Style* Find(const Table& table, int aci, const std::string& namedStyle);
uint32_t ApplyColor(const Table& table, const Style* style, uint32_t objectRgb);
double LineweightMm(const Table& table, const Style* style, double objectMm);

} // namespace plotstyle
