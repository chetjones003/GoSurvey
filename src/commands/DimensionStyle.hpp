#pragma once

#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <map>

#include "CadEntities.hpp"
#include "util/styleproperty.hpp"

// Centralized dimension style (issue #99) — one style object used by DIMALIGNED, DIMLINEAR and
// DIMANGULAR.  Pure value type, dependency-free so it is unit-testable and can be stored on
// AppCommandState without pulling in UI/GL.  Mirrors TextStyle/SurfaceStyle in shape but is a
// SINGLE active style (not a named table) for the first implementation — the requirement's
// "preferred behavior" allows new dims to use the active style and existing dims to update
// automatically, which is exactly what a single active style gives.  Per-dimension snapshots are
// a follow-up and would be added as a DimensionStyle field on CadAnnotation.

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

/// Copy the active style's text face and plotted height onto a dimension annotation (DIMSTY Apply).
inline void BakeTextOntoDimension(CadAnnotation& a, const DimensionStyle& sty) {
  if (a.kind != CadAnnotation::Kind::DimAligned && a.kind != CadAnnotation::Kind::DimLinear &&
      a.kind != CadAnnotation::Kind::DimAngular)
    return;
  a.fontFamily = sty.textFont;
  a.plottedHeightInches = std::max(sty.textSizeInches, 1.e-6f);
}

// Format a linear measurement according to the style's units (scale + precision + format).
inline std::string FormatLinearDimBase(double measured, const DimensionStyle& sty) {
  const double scaled = measured * sty.unitScale;
  const int prec = std::clamp(sty.unitPrecision, 0, 8);
  char buf[96];
  std::string core;
  switch (sty.unitFormat) {
  case DimUnitFormat::Architectural:
  case DimUnitFormat::Engineering: {
    const int feet = static_cast<int>(std::floor(scaled + 1.e-9));
    const double inches = (scaled - static_cast<double>(feet)) * 12.0;
    if (feet == 0)
      std::snprintf(buf, sizeof(buf), "%.*f\"", prec, inches);
    else
      std::snprintf(buf, sizeof(buf), "%d'-%.*f\"", feet, prec, inches);
    core = buf;
    break;
  }
  case DimUnitFormat::Fractional:
  default:
    std::snprintf(buf, sizeof(buf), "%.*f", prec, scaled);
    core = buf;
    break;
  }
  if (sty.unitSuffix.empty())
    return core;
  const std::size_t placeholder = sty.unitSuffix.find("<>");
  if (placeholder != std::string::npos) {
    std::string out = sty.unitSuffix;
    out.replace(placeholder, 2, core);
    return out;
  }
  return core + sty.unitSuffix;
}

inline std::string FormatLinearDim(double measured, const DimensionStyle& sty) {
  DimensionStyle primary = sty;
  primary.unitSuffix.clear();
  const double roundOff = StyleNumber(sty.props, "roundOff", 0);
  if (roundOff > 0 && primary.unitScale != 0)
    measured = std::round(measured * primary.unitScale / roundOff) * roundOff / primary.unitScale;
  auto suppress = [&](std::string value, const std::string& prefix) {
    if (StyleFlag(sty.props, prefix + "Trailing") && value.find('.') != std::string::npos) {
      while (!value.empty() && value.back() == '0') value.pop_back();
      if (!value.empty() && value.back() == '.') value.pop_back();
    }
    if (StyleFlag(sty.props, prefix + "Leading")) {
      if (value.rfind("0.", 0) == 0) value.erase(0, 1);
      else if (value.rfind("-0.", 0) == 0) value.erase(1, 1);
    }
    return value;
  };
  std::string number = suppress(FormatLinearDimBase(measured, primary), "zero");
  const std::string separator = StyleProperty(sty.props, "decimalSeparator");
  if (separator.find("Comma") != std::string::npos) std::replace(number.begin(), number.end(), '.', ',');
  else if (separator.find("Space") != std::string::npos) std::replace(number.begin(), number.end(), '.', ' ');
  const std::string prefix = StyleProperty(sty.props, "prefix");
  std::string result = prefix + number;
  if (const auto at = sty.unitSuffix.find("<>"); at != std::string::npos) {
    result = sty.unitSuffix;
    result.replace(at, 2, prefix + number);
  } else result += sty.unitSuffix;
  if (StyleFlag(sty.props, "altEnabled")) {
    DimensionStyle alternate = primary;
    alternate.unitScale *= StyleNumber(sty.props, "altMultiplier", 25.4);
    const auto precision = StyleProperty(sty.props, "altPrecision", "0.00");
    const auto point = precision.find('.');
    alternate.unitPrecision = point == std::string::npos ? 0 : static_cast<int>(precision.size() - point - 1);
    const auto format = StyleProperty(sty.props, "altFormat", "Decimal");
    for (int i = 0; i <= static_cast<int>(DimUnitFormat::Fractional); ++i)
      if (format == DimUnitFormatName(static_cast<DimUnitFormat>(i))) alternate.unitFormat = static_cast<DimUnitFormat>(i);
    const double interval = StyleNumber(sty.props, "altRound", 0);
    const double alternateMeasured = interval > 0 && alternate.unitScale != 0 ?
        std::round(measured * alternate.unitScale / interval) * interval / alternate.unitScale : measured;
    const std::string value = suppress(FormatLinearDimBase(alternateMeasured, alternate), "altZero");
    result += (StyleProperty(sty.props, "altPlacement") == "Below primary value" ? "\n[" : " [") +
        StyleProperty(sty.props, "altPrefix") + value + StyleProperty(sty.props, "altSuffix") + "]";
  }
  const auto method = StyleProperty(sty.props, "tolMethod", "None");
  if (method == "Symmetrical" || method == "Deviation" || method == "Limits") {
    const auto precision = StyleProperty(sty.props, "tolPrecision", "0.0");
    const auto point = precision.find('.');
    const int decimals = point == std::string::npos ? 0 : std::clamp(static_cast<int>(precision.size() - point - 1), 0, 8);
    const double upper = StyleNumber(sty.props, "tolUpper", 0), lower = StyleNumber(sty.props, "tolLower", 0);
    char tolerance[128];
    if (method == "Symmetrical") std::snprintf(tolerance, sizeof(tolerance), " \xc2\xb1%.*f", decimals, upper);
    else if (method == "Deviation") std::snprintf(tolerance, sizeof(tolerance), " +%.*f/-%.*f", decimals, upper, decimals, lower);
    else std::snprintf(tolerance, sizeof(tolerance), "%.*f\n%.*f", decimals, measured * sty.unitScale + upper, decimals, measured * sty.unitScale - lower);
    if (method == "Limits") result = prefix + tolerance + sty.unitSuffix;
    else result += tolerance;
  }
  return result;
}

} // namespace DimensionStyles
