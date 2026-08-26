#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <cstdio>

#include "CadEntities.hpp"

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

  bool operator==(const DimensionStyle& o) const {
    return name == o.name && textSizeInches == o.textSizeInches && textFont == o.textFont &&
           textColor == o.textColor && textAlign == o.textAlign &&
           dimLineColor == o.dimLineColor && dimLineType == o.dimLineType &&
           extLineColor == o.extLineColor && extLineType == o.extLineType &&
           arrowSizeInches == o.arrowSizeInches && arrowType == o.arrowType && arrowColor == o.arrowColor &&
           unitFormat == o.unitFormat && unitPrecision == o.unitPrecision && unitScale == o.unitScale;
  }
  bool operator!=(const DimensionStyle& o) const { return !(*this == o); }
};

namespace DimensionStyles {

// Default style — the value a new drawing starts with and a missing/corrupt .gs falls back to.
inline DimensionStyle Default() {
  DimensionStyle s;
  s.name = "Standard";
  return s;
}

// Format a linear measurement according to the style's units (scale + precision + format).
// Architectural/Engineering/Fractional fall back to Decimal for now — the format enum is stored
// and the UI exposes it, but the formatter is decimal-only until the measurement system carries
// feet-inch knowledge.  Keeping the enum now means .gs files already carry the choice.
inline std::string FormatLinearDim(double measured, const DimensionStyle& sty) {
  double scaled = measured * sty.unitScale;
  int prec = sty.unitPrecision;
  if (prec < 0) prec = 0;
  if (prec > 8) prec = 8;
  char buf[64];
  // Use fixed notation; precision is number of decimal places.
  std::snprintf(buf, sizeof(buf), "%.*f", prec, scaled);
  return std::string(buf);
}

} // namespace DimensionStyles
