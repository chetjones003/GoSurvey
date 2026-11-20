#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// The Object Layers tab's rows (REQ-361): each kind of object that lands on its own layer when it
/// is created. Stored by index — append only.
enum class ObjectLayerKind : std::uint8_t {
  SurveyPoint = 0,
  SurveyPointLabel,
  Surface,      ///< Every TIN surface store entry (TIN, grid, volume, corridor).
  FeatureLine,
  PipeRun,
  PipeFitting,  ///< A pipe-catalogue part placed off any run (D-2026-09-29-f).
  Solid,
  Table,
};
inline constexpr int kObjectLayerKindCount = 8;

/// One Object Layers row: the base layer, the name modifier and whether a creation dialog may change it.
struct ObjectLayerRow {
  enum class Modifier : std::uint8_t { None = 0, Prefix = 1, Suffix = 2 };
  std::string layer;
  Modifier    modifier = Modifier::None;
  std::string value;   ///< Each `*` becomes the new object's name.
  bool        locked = false;
  bool operator==(const ObjectLayerRow& o) const {
    return layer == o.layer && modifier == o.modifier && value == o.value && locked == o.locked;
  }
};

/// REQ-361 item 1's NCS defaults, in \ref ObjectLayerKind order.
[[nodiscard]] inline std::array<ObjectLayerRow, kObjectLayerKindCount> DefaultObjectLayers() {
  std::array<ObjectLayerRow, kObjectLayerKindCount> rows;
  const char* names[kObjectLayerKindCount] = {"V-NODE", "V-NODE-TEXT", "C-TOPO", "C-TOPO-FEAT",
                                              "C-PIPE", "C-PIPE-FITT", "C-SOLID", "C-ANNO-TABL"};
  for (int i = 0; i < kObjectLayerKindCount; ++i)
    rows[static_cast<size_t>(i)].layer = names[i];
  return rows;
}

/// REQ-375 / D-2026-10-05-e: the settings a project supplies as defaults that a project drawing may
/// override. The zone and the drawing unit are enforced instead; the transformation, geographic
/// marker and online map are always the drawing's own. The names are the trailer's.
enum class ProjectDefaultKey : std::uint8_t {
  AngularUnits = 0,
  FootDefinition,
  ScaleInsertedObjects,
  SetDrawingVariables,
  PlotScale,
  ObjectLayers,
};
inline constexpr unsigned kProjectDefaultKeyCount = 6;
inline constexpr const char* kProjectDefaultKeyNames[kProjectDefaultKeyCount] = {
    "angularUnits", "footDefinition", "scaleInsertedObjects", "setDrawingVariables", "plotScale", "objectLayers"};

/// The drawing's own settings that the Drawing Settings window edits (REQ-357). The drawing unit and
/// the scale are NOT here: they are `DrawingContent::drawingInsUnits` and
/// `modelUnitsPerPlottedInch`, one value each (REQ-022, D-2026-09-29-c).
struct DrawingSettings {
  /// REQ-375: bit per \ref ProjectDefaultKey the drawing overrides. Meaningful only for a project
  /// drawing; a standalone drawing keeps it 0.
  unsigned overridden = 0;
  [[nodiscard]] bool IsOverridden(ProjectDefaultKey k) const { return (overridden >> static_cast<unsigned>(k)) & 1u; }
  void SetOverridden(ProjectDefaultKey k, bool on) {
    const unsigned bit = 1u << static_cast<unsigned>(k);
    overridden = on ? (overridden | bit) : (overridden & ~bit);
  }

  enum class AngularUnits : std::uint8_t { Degrees = 0, Radians = 1, Grads = 2 };
  enum class FootDefinition : std::uint8_t { UsSurvey = 0, International = 1 };
  AngularUnits   angularUnits = AngularUnits::Degrees;
  /// Imperial to Metric conversion: every feet↔meters conversion in the drawing uses this.
  FootDefinition footDefinition = FootDefinition::UsSurvey;
  /// Off → INSERT's unit scale factor is 1 whatever the block's unit.
  bool scaleInsertedObjects = true;
  /// On → DWG/DXF save writes LUNITS and AUNITS from these settings, beside INSUNITS.
  bool setDrawingVariables = true;
  /// The drawing's coordinate system (zone), a CS-MAP code such as "HARN/TX.TX-C" (REQ-358).
  /// Empty = No Datum, No Projection. A code the installed dictionary does not know is kept.
  std::string zoneCode;
  /// The geographic marker (REQ-359 item 4): the drawing's geolocation reference — a design point and
  /// a north direction, stored with the zone and read from GEODATA's design point / north by
  /// REQ-362. The point is WORLD (WCS) coordinates, the frame GEODATA uses, so it does not move when
  /// the local-storage origin does. Default: the drawing origin (WCS 0,0) with grid north.
  double markerX = 0.0;
  double markerY = 0.0;
  /// North direction, degrees counter-clockwise from +X; 90 = grid north (+Y).
  double markerNorthDeg = 90.0;

  /// The Transformation tab (REQ-360): local (drawing) ↔ grid,
  /// `grid = G_ref + k·R(θ)·(L − L_ref)` with k = k_grid · k_sea. Angles are stored in degrees
  /// whatever the Angular units (the window converts). Belongs to the zone: reset with it.
  struct Transform {
    enum class Computation : std::uint8_t { ReferencePoint = 0, UserDefined = 1 };
    /// Rotation point: θ from a second point's grid vs local bearing. ToNorth / Azimuth are the two
    /// kinds of "Specify grid rotation angle".
    enum class Rotation : std::uint8_t { RotationPoint = 0, ToNorth = 1, Azimuth = 2 };
    bool        apply = false;
    bool        applySeaLevel = false;
    double      elevation = 0.0;        ///< h, drawing unit
    double      spheroidRadiusM = 0.0;  ///< R, meters; 0 = the zone ellipsoid's semi-major axis
    Computation computation = Computation::ReferencePoint;
    double      userScaleFactor = 1.0;  ///< k_grid when User Defined
    // Reference point: L_ref (WORLD, drawing unit), G_ref (zone unit), survey point number (0 = none).
    double      refLocalX = 0.0, refLocalY = 0.0;
    double      refGridE = 0.0, refGridN = 0.0;
    int         refPointNumber = 0;
    Rotation    rotation = Rotation::ToNorth;
    // Rotation point, the same four values and a point number.
    double      rotLocalX = 0.0, rotLocalY = 0.0;
    double      rotGridE = 0.0, rotGridN = 0.0;
    int         rotPointNumber = 0;
    double      toNorthDeg = 0.0;       ///< local north → grid north, clockwise
    double      localAzimuthDeg = 0.0;  ///< Azimuth: this local azimuth (clockwise from north)…
    double      gridAzimuthDeg = 0.0;   ///< …becomes this grid azimuth
    bool operator==(const Transform& o) const {
      return apply == o.apply && applySeaLevel == o.applySeaLevel && elevation == o.elevation &&
             spheroidRadiusM == o.spheroidRadiusM && computation == o.computation &&
             userScaleFactor == o.userScaleFactor && refLocalX == o.refLocalX && refLocalY == o.refLocalY &&
             refGridE == o.refGridE && refGridN == o.refGridN && refPointNumber == o.refPointNumber &&
             rotation == o.rotation && rotLocalX == o.rotLocalX && rotLocalY == o.rotLocalY &&
             rotGridE == o.rotGridE && rotGridN == o.rotGridN && rotPointNumber == o.rotPointNumber &&
             toNorthDeg == o.toNorthDeg && localAzimuthDeg == o.localAzimuthDeg &&
             gridAzimuthDeg == o.gridAzimuthDeg;
    }
    bool operator!=(const Transform& o) const { return !(*this == o); }
  };
  Transform transform;

  /// The Object Layers tab (REQ-361): the layer each kind of new object is created on. A drawing
  /// saved without it opens with the defaults.
  std::array<ObjectLayerRow, kObjectLayerKindCount> objectLayers = DefaultObjectLayers();
  [[nodiscard]] const ObjectLayerRow& ObjectLayer(ObjectLayerKind k) const {
    return objectLayers[static_cast<size_t>(k)];
  }

  /// The Geolocation tab's online map (REQ-363, D-2026-09-30-b). Map Off draws nothing and fetches
  /// nothing. Stored values are the trailer's; append, never renumber.
  enum class OnlineMap : std::uint8_t { Off = 0, UsgsImagery = 1, UsgsImageryTopo = 2, UsgsTopo = 3 };
  OnlineMap onlineMap = OnlineMap::Off;

  /// REQ-364: a piece of the online map kept inside the drawing (ADR-064 (e)): its tiles as they were
  /// served, at one level. Drawn through the same placement as live tiles, with the map on or off.
  struct CapturedTile {
    int x = 0;
    int y = 0;
    /// The JPEG / PNG exactly as served. Immutable and shared, so undo snapshots copy a pointer.
    std::shared_ptr<const std::string> image;
    bool operator==(const CapturedTile& o) const {
      return x == o.x && y == o.y && (image == o.image || (image && o.image && *image == *o.image));
    }
  };
  struct CapturedArea {
    OnlineMap                 map = OnlineMap::Off;
    int                       level = 0;
    std::vector<CapturedTile> tiles;
    bool operator==(const CapturedArea& o) const { return map == o.map && level == o.level && tiles == o.tiles; }
  };
  std::vector<CapturedArea> capturedAreas;

  /// Geolocated exactly when a zone is set (REQ-358 item 3).
  [[nodiscard]] bool Geolocated() const { return !zoneCode.empty(); }
  /// Back to the default geographic marker (drawing origin, grid north).
  void ResetGeographicMarker() {
    markerX = 0.0;
    markerY = 0.0;
    markerNorthDeg = 90.0;
  }

  bool operator==(const DrawingSettings& o) const {
    return overridden == o.overridden && angularUnits == o.angularUnits && footDefinition == o.footDefinition &&
           scaleInsertedObjects == o.scaleInsertedObjects && setDrawingVariables == o.setDrawingVariables &&
           zoneCode == o.zoneCode && markerX == o.markerX && markerY == o.markerY &&
           markerNorthDeg == o.markerNorthDeg && transform == o.transform &&
           objectLayers == o.objectLayers && onlineMap == o.onlineMap && capturedAreas == o.capturedAreas;
  }
  bool operator!=(const DrawingSettings& o) const { return !(*this == o); }
};

/// One entry of the Map dropdown (REQ-363 item 1), in dropdown order with Map Off last.
struct OnlineMapInfo {
  DrawingSettings::OnlineMap map;
  const char* label;        ///< Dropdown / button text.
  const char* storageName;  ///< The trailer's value.
  const char* service;      ///< USGS The National Map service name; nullptr for Map Off.
  const char* icon;         ///< Ribbon icon (resources/icons/<icon>.png): the item's thumbnail.
};
inline constexpr std::array<OnlineMapInfo, 4> kOnlineMaps = {{
    {DrawingSettings::OnlineMap::UsgsImagery, "USGS Imagery", "usgsImagery", "USGSImageryOnly", "map_usgs_imagery"},
    {DrawingSettings::OnlineMap::UsgsImageryTopo, "USGS Imagery Topo", "usgsImageryTopo", "USGSImageryTopo",
     "map_usgs_imagery_topo"},
    {DrawingSettings::OnlineMap::UsgsTopo, "USGS Topo", "usgsTopo", "USGSTopo", "map_usgs_topo"},
    {DrawingSettings::OnlineMap::Off, "Map Off", "off", nullptr, "map_off"},
}};
[[nodiscard]] inline const OnlineMapInfo& OnlineMapInfoOf(DrawingSettings::OnlineMap m) {
  for (const OnlineMapInfo& i : kOnlineMaps)
    if (i.map == m)
      return i;
  return kOnlineMaps.back();
}
[[nodiscard]] inline const char* OnlineMapStorageName(DrawingSettings::OnlineMap m) {
  return OnlineMapInfoOf(m).storageName;
}
/// Unknown or empty → Map Off.
[[nodiscard]] inline DrawingSettings::OnlineMap OnlineMapFromStorageName(const std::string& name) {
  for (const OnlineMapInfo& i : kOnlineMaps)
    if (name == i.storageName)
      return i.map;
  return DrawingSettings::OnlineMap::Off;
}

/// Inches per meter under \p f: exactly 39.37 (US survey foot) or 1/0.0254 (international foot).
[[nodiscard]] inline double DrawingInchesPerMeter(DrawingSettings::FootDefinition f) {
  return f == DrawingSettings::FootDefinition::UsSurvey ? 39.37 : 1.0 / 0.0254;
}

/// AutoCAD's AUNITS code for \p a (0 degrees, 2 grads, 3 radians).
[[nodiscard]] inline int DrawingAunitsCode(DrawingSettings::AngularUnits a) {
  switch (a) {
    case DrawingSettings::AngularUnits::Radians: return 3;
    case DrawingSettings::AngularUnits::Grads:   return 2;
    default:                                      return 0;
  }
}
