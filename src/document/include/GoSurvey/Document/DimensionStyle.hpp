#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

enum class DimTextAlign : uint8_t { Center = 0, Above = 1, Beside = 2 };
enum class DimArrowType : uint8_t { ClosedFilled = 0, ClosedBlank = 1, Tick = 2, Dot = 3, Open = 4, None = 5 };
enum class DimUnitFormat : uint8_t { Decimal = 0, Architectural = 1, Engineering = 2, Fractional = 3 };

inline const char* DimTextAlignName(DimTextAlign v) {
  switch (v) {
    case DimTextAlign::Center: return "Center";
    case DimTextAlign::Above:  return "Above";
    case DimTextAlign::Beside: return "Beside";
  }
  return "Center";
}
inline const char* DimArrowTypeName(DimArrowType v) {
  switch (v) {
    case DimArrowType::ClosedFilled: return "Closed Filled";
    case DimArrowType::ClosedBlank:  return "Closed Blank";
    case DimArrowType::Tick:         return "Architectural Tick";
    case DimArrowType::Dot:          return "Dot";
    case DimArrowType::Open:         return "Open";
    case DimArrowType::None:         return "None";
  }
  return "Closed Filled";
}
inline const char* DimUnitFormatName(DimUnitFormat v) {
  switch (v) {
    case DimUnitFormat::Decimal:       return "Decimal";
    case DimUnitFormat::Architectural: return "Architectural";
    case DimUnitFormat::Engineering:   return "Engineering";
    case DimUnitFormat::Fractional:    return "Fractional";
  }
  return "Decimal";
}

struct DimensionStyle {
  std::string name = "Standard";

  // Text
  float textSizeInches = 0.10f;
  std::string textFont;                  // empty = app default
  std::string textColor = "ByLayer";     // ByLayer / #RRGGBB / named
  DimTextAlign textAlign = DimTextAlign::Center;

  // Dimension line
  std::string dimLineColor = "ByLayer";
  std::string dimLineType = "Continuous";

  // Extension lines
  std::string extLineColor = "ByLayer";
  std::string extLineType = "Continuous";

  // Arrows
  float arrowSizeInches = 0.10f;
  DimArrowType arrowType = DimArrowType::ClosedFilled;
  std::string arrowColor = "ByLayer";

  // Units
  DimUnitFormat unitFormat = DimUnitFormat::Decimal;
  int unitPrecision = 2;                 // 0..4  -> 0 / 0.0 / 0.00 / 0.000 / 0.0000
  double unitScale = 1.0;                // display value = measured * scale
  std::string unitSuffix;                // DWG DIMPOST (often a trailing foot mark after <>)

  /// Every other Dimension Style setting the Modify Dimension Style window edits (line weights, suppress flags,
  /// arrowhead choices, centre marks, fit, alternate units, tolerances, ...), keyed by a short name.
  std::map<std::string, std::string> props;

  bool operator==(const DimensionStyle& o) const {
    return name == o.name && textSizeInches == o.textSizeInches && textFont == o.textFont &&
           textColor == o.textColor && textAlign == o.textAlign &&
           dimLineColor == o.dimLineColor && dimLineType == o.dimLineType &&
           extLineColor == o.extLineColor && extLineType == o.extLineType &&
           arrowSizeInches == o.arrowSizeInches && arrowType == o.arrowType && arrowColor == o.arrowColor &&
           unitFormat == o.unitFormat && unitPrecision == o.unitPrecision && unitScale == o.unitScale &&
           unitSuffix == o.unitSuffix && props == o.props;
  }
  bool operator!=(const DimensionStyle& o) const { return !(*this == o); }
};

namespace DimensionStyles {

// Default style — the value a new drawing starts with and a missing/corrupt internal document payload falls back to.
inline DimensionStyle Default() {
  DimensionStyle s;
  s.name = "Standard";
  return s;
}

inline std::vector<DimensionStyle> DefaultStyles() {
  return {Default()};
}

inline const DimensionStyle* Find(const std::vector<DimensionStyle>& styles, const std::string& name) {
  const auto it = std::find_if(styles.begin(), styles.end(), [&name](const DimensionStyle& style) {
    return style.name == name;
  });
  return it == styles.end() ? nullptr : &*it;
}

}  // namespace DimensionStyles
