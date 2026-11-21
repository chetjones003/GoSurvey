#pragma once

#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <map>

#include "CadEntities.hpp"
#include "GoSurvey/Document/styleproperty.hpp"
#include <GoSurvey/Document/DimensionStyle.hpp>

// Centralized dimension style (issue #99) — one style object used by DIMALIGNED, DIMLINEAR and
// DIMANGULAR.  Pure value type, dependency-free so it is unit-testable and can be stored on
// AppCommandState without pulling in UI/GL.  Mirrors TextStyle/SurfaceStyle in shape but is a
// SINGLE active style (not a named table) for the first implementation — the requirement's
// "preferred behavior" allows new dims to use the active style and existing dims to update
// automatically, which is exactly what a single active style gives.  Per-dimension snapshots are
// a follow-up and would be added as a DimensionStyle field on CadAnnotation.

namespace DimensionStyles {

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
