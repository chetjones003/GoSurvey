#pragma once

#include <string>
#include <vector>

/// Named layer row for the layer manager (visibility / freeze / lock are stored for future viewport filtering).
struct CadLayerRow {
  std::string name;
  bool on = true;
  bool frozen = false;
  bool locked = false;
  /// Layer swatch color (same encoding as \ref EntityAttributes::color except not "ByLayer").
  std::string color = "White";
  /// Named STB plot style assigned to this layer; defaults to Normal.
  std::string plotStyleName = "Normal";
  std::string linetype = "Continuous";
  float lineweightMm = -1.f; ///< \c -1 = default (DXF layer 370 -3 on export).
  float transparency = 0.f;  ///< 0 opaque .. 1 fully transparent (layer-wide).
  bool plottable = true;     ///< when false, geometry/viewports on this layer are excluded from plots (REQ-029/030).
};

/// The layer table a brand-new drawing starts with — layer "0" and nothing else.
inline std::vector<CadLayerRow> DefaultDrawingLayerTable() {
  CadLayerRow zero;
  zero.name = "0";
  return {zero};
}
