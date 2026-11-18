#include "PlotStyleTable.hpp"

#include "CadColor.hpp"
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace plotstyle {
namespace {

std::string Trim(std::string value) {
  while (!value.empty() && value.back() == '\0') value.pop_back();
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

bool Integer(const std::string& text, int* out) {
  if (out == nullptr) return false;
  try {
    size_t used = 0;
    const int value = std::stoi(text, &used);
    if (used == 0) return false;
    *out = value;
    return true;
  } catch (...) { return false; }
}

bool Number(const std::string& text, double* out) {
  if (out == nullptr) return false;
  try {
    size_t used = 0;
    const double value = std::stod(text, &used);
    if (used == 0 || !std::isfinite(value)) return false;
    *out = value;
    return true;
  } catch (...) { return false; }
}

bool Parse(std::string_view text, Table* table) {
  std::istringstream lines{std::string(text)};
  std::vector<std::string> stack;
  std::map<std::string, std::map<std::string, std::string>, std::less<>> styles;
  std::map<int, std::string> lineweights;
  int displayUnits = 0;
  std::string line;
  while (std::getline(lines, line)) {
    line = Trim(line);
    if (line.empty()) continue;
    if (line == "}") { if (!stack.empty()) stack.pop_back(); continue; }
    const size_t brace = line.find('{');
    if (brace != std::string::npos) {
      const std::string key = Trim(line.substr(0, brace));
      if (key.empty()) return false;
      stack.push_back(key);
      continue;
    }
    const size_t equals = line.find('=');
    if (equals == std::string::npos) continue;
    const std::string key = Trim(line.substr(0, equals));
    std::string value = Trim(line.substr(equals + 1));
    if (!value.empty() && value.front() == '"') value.erase(value.begin());
    if (stack.empty() && key == "custom_lineweight_display_units") {
      Integer(value, &displayUnits);
      continue;
    }
    if (stack.empty()) continue;
    if (stack.front() == "plot_style" && stack.size() >= 2)
      styles[stack[1]][key] = value;
    else if (stack.front() == "custom_lineweight_table") {
      int index = 0;
      if (Integer(key, &index)) lineweights[index] = value;
    }
  }
  if (styles.empty()) return false;

  const double toMm = displayUnits == 1 ? 25.4 : 1.0;
  for (const auto& [key, value] : lineweights) {
    double mm = 0.0;
    if (Number(value, &mm)) table->lineweightsMm[key] = mm * toMm;
  }
  for (const auto& [key, fields] : styles) {
    Style style;
    const auto get = [&fields](const char* name, std::string fallback = {}) {
      auto it = fields.find(name);
      return it == fields.end() ? fallback : it->second;
    };
    style.name = get("name");
    if (style.name.empty()) style.name = get("localized_name");
    int policy = 0;
    Integer(get("color_policy", "0"), &policy);
    style.explicitColor = (policy & 4) != 0;
    if (!Integer(get("color", get("mode_color", "-1")), &style.color)) style.color = -1;
    if (!Integer(get("lineweight", "0"), &style.lineweightIndex)) style.lineweightIndex = 0;
    if (!Integer(get("screen", "100"), &style.screening)) style.screening = 100;
    const std::string gray = get("color_policy", "0");
    style.grayscale = false;
    int screenPolicy = 0;
    Integer(gray, &screenPolicy);
    style.grayscale = (screenPolicy & 2) != 0;
    if (table->colorDependent) {
      int index = -1;
      if (!Integer(key, &index) || index < 0 || index > 254) return false;
      table->byAci[index + 1] = style;
    } else {
      if (style.name.empty()) return false;
      table->byName[style.name] = style;
    }
  }
  return true;
}

} // namespace

bool Load(const std::filesystem::path& path, Table* out, std::string* error) {
  if (out == nullptr) return false;
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) { if (error) *error = "cannot open plot style table '" + path.string() + "'"; return false; }
  const auto size = input.tellg();
  if (size <= 0 || size > 16 * 1024 * 1024) { if (error) *error = "plot style table has an invalid size"; return false; }
  std::vector<char> bytes(static_cast<size_t>(size));
  input.seekg(0);
  if (!input.read(bytes.data(), size)) { if (error) *error = "cannot read plot style table"; return false; }
  const std::string header(bytes.data(), std::min<size_t>(bytes.size(), 128));
  Table parsed;
  if (header.find("CTBVER1") != std::string::npos) parsed.colorDependent = true;
  else if (header.find("STBVER1") == std::string::npos) { if (error) *error = "unsupported plot style table format"; return false; }
  std::string text;
  if (header.find("compress") == std::string::npos) text.assign(bytes.begin(), bytes.end());
  else {
    for (size_t i = 0; i + 1 < bytes.size() && text.empty(); ++i) {
      const auto a = static_cast<unsigned char>(bytes[i]);
      const auto b = static_cast<unsigned char>(bytes[i + 1]);
      if (a != 0x78 || (b != 0x01 && b != 0x5e && b != 0x9c && b != 0xda)) continue;
      int decodedLength = 0;
      char* decoded = stbi_zlib_decode_malloc_guesssize_headerflag(bytes.data() + i,
        static_cast<int>(bytes.size() - i), 65536, &decodedLength, 1);
      if (decoded != nullptr) { text.assign(decoded, static_cast<size_t>(decodedLength)); stbi_image_free(decoded); }
    }
  }
  if (text.empty() || !Parse(text, &parsed)) { if (error) *error = "plot style table contents are malformed or unsupported"; return false; }
  *out = std::move(parsed);
  return true;
}

const Style* Find(const Table& table, int aci, const std::string& namedStyle) {
  if (table.colorDependent) {
    const auto it = table.byAci.find(aci);
    return it == table.byAci.end() ? nullptr : &it->second;
  }
  auto it = table.byName.find(namedStyle);
  if (it == table.byName.end()) it = table.byName.find("Normal");
  return it == table.byName.end() ? nullptr : &it->second;
}

uint32_t ApplyColor(const Table&, const Style* style, uint32_t objectRgb) {
  uint32_t rgb = objectRgb & 0xffffffu;
  if (style != nullptr && style->explicitColor) {
    if (style->color >= 1 && style->color <= 255) {
      float f[3]{};
      const std::string storage = CadColorStorageFromAci(style->color);
      CadColorResolveRgb(storage, 0.f, 0.f, 0.f, f);
      rgb = (static_cast<uint32_t>(f[0] * 255.f + .5f) << 16) |
            (static_cast<uint32_t>(f[1] * 255.f + .5f) << 8) | static_cast<uint32_t>(f[2] * 255.f + .5f);
    } else if (style->color < -1) rgb = static_cast<uint32_t>(style->color) & 0xffffffu;
  }
  if (style != nullptr) {
    if (style->grayscale) {
      const uint32_t gray = (static_cast<uint32_t>((((rgb >> 16) & 255u) * 30u + ((rgb >> 8) & 255u) * 59u + (rgb & 255u) * 11u) / 100u));
      rgb = (gray << 16) | (gray << 8) | gray;
    }
    const uint32_t p = static_cast<uint32_t>(std::clamp(style->screening, 0, 100));
    const uint32_t r = ((rgb >> 16) & 255u) * p / 100u + 255u * (100u - p) / 100u;
    const uint32_t g = ((rgb >> 8) & 255u) * p / 100u + 255u * (100u - p) / 100u;
    const uint32_t b = (rgb & 255u) * p / 100u + 255u * (100u - p) / 100u;
    rgb = (r << 16) | (g << 8) | b;
  }
  return rgb;
}

double LineweightMm(const Table& table, const Style* style, double objectMm) {
  if (style == nullptr || style->lineweightIndex == 0) return objectMm;
  const auto it = table.lineweightsMm.find(style->lineweightIndex);
  return it == table.lineweightsMm.end() ? objectMm : it->second;
}

} // namespace plotstyle
