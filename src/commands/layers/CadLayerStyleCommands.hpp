void SyncDrawingLayerTableWithGeometry(AppCommandState& st);

bool CadAddDrawingLayer(AppCommandState& st, const std::string& name, std::string* err);

// --- Object Layers (REQ-361, GitHub issue #582 increment 5) --------------------------------------
/// The layer a new object of \p kind named \p objectName goes on: the row's layer, with the Value
/// (each `*` = the name; dropped when there is no name) before it (Prefix) or after it (Suffix). Pure.
[[nodiscard]] std::string ResolveObjectLayer(const DrawingSettings& settings, ObjectLayerKind kind,
                                             std::string_view objectName);
/// \ref ResolveObjectLayer, made real: a name that cannot be a layer falls back to the row's base
/// layer (then "0"); a layer the drawing does not have is added with default properties. Call it
/// AFTER the creating path's PushUndoSnapshot, so one UNDO removes the object and the new layer.
/// Returns the layer name as the drawing spells it.
std::string EnsureObjectLayer(AppCommandState& st, ObjectLayerKind kind, std::string_view objectName);
/// The drawing's layer named \p name (case-insensitive), added with default properties when missing.
/// Returns the name as the drawing spells it. No undo step of its own.
std::string EnsureDrawingLayer(AppCommandState& st, const std::string& name);
/// A sentence naming the first Object Layers row whose base layer is not a usable layer name, or "".
/// ApplyDrawingSettings refuses such settings.
[[nodiscard]] std::string ValidateObjectLayers(const DrawingSettings& settings);
/// The attributes of a newly created object of \p kind: \ref MakeNewEntityAttrs (current colour,
/// REQ-356) on \ref EnsureObjectLayer's layer. The one stamp every creation path uses (REQ-361 item 3).
EntityAttributes MakeNewObjectAttrs(AppCommandState& st, ObjectLayerKind kind, std::string_view objectName);
/// For stores whose creator pushes the object first: grows \p attrs to \p count, the last one (the
/// new object) from \ref MakeNewObjectAttrs.
void AppendNewObjectAttrs(AppCommandState& st, std::vector<EntityAttributes>& attrs, size_t count,
                          ObjectLayerKind kind, std::string_view objectName);

bool CadRenameDrawingLayer(AppCommandState& st, const std::string& oldName, const std::string& newName, std::string* err);

bool CadDeleteDrawingLayer(AppCommandState& st, const std::string& name, std::string* err);

// --- Drawing Settings (REQ-357, GitHub issue #582) --------------------------------------------

/// Set the drawing's plot scale (model units per plotted inch) and resize what depends on it — the
/// survey-point labels and their layout cache. No undo step: callers that edit push their own.
void SetDrawingPlotScale(AppCommandState& st, float modelUnitsPerPlottedInch);
/// Pick the annotation-scale list entry closest to the drawing plot scale (issue #622).
void SyncCurrentAnnotationScaleIndex(AppCommandState& st);
/// Set CANNOSCALE index for model-space annotative display; bumps GPU cache when changed.
void SetCurrentAnnotationScaleIndex(AppCommandState& st, int index);

/// Write the Drawing Settings window's values to the drawing as ONE undo step, pushed only when
/// something changes. A unit change is a relabel and moves no geometry (REQ-022). Refuses (false,
/// with a message) a plot scale that is not a positive finite number. Returns true when applied or
/// when nothing changed.
bool ApplyDrawingSettings(AppCommandState& st, int drawingInsUnits, float modelUnitsPerPlottedInch,
                          const DrawingSettings& settings, std::vector<std::string>& log);

/// REQ-358 item 4: the grid coordinate (easting, northing in the zone's own unit) of a drawing point
/// given in LOCAL coordinates: world = local + worldDocumentOrigin in double (REQ-101), then drawing
/// unit → meters under the drawing's Imperial to Metric conversion (REQ-357) → the zone's unit. A
/// Unitless drawing is taken to be in the zone's unit. Fails (with the reason) when the drawing is
/// not geolocated or its zone is unknown to the loaded dictionary. When the drawing's transform is
/// applied (REQ-360) the result goes through it. (CadCommands_Geo.cpp)
[[nodiscard]] geo::GeoResult DrawingPointToGrid(const AppCommandState& st, double localX, double localY);

/// REQ-360: what a drawing's settings resolve to — the unit factor, and the transformation's factors
/// and angle (the Transformation tab's readouts). \c ok = the zone is usable; \c transformOk = the
/// transform's own values are (a Reference Point factor needs CS-MAP at G_ref). Without
/// \p withTransform only the zone and the unit factor are resolved (transformOk stays false).
struct DrawingTransformFactors {
  bool        ok = false;
  std::string error;               ///< Why !ok.
  double      zoneUnitsPerDrawingUnit = 1.0;
  std::string zoneUnitName;        ///< e.g. "US Survey Feet", "Meters".
  bool        transformOk = false;
  std::string transformError;      ///< Why !transformOk.
  double      gridFactor = 1.0;    ///< k_grid
  double      seaFactor = 1.0;     ///< k_sea (1 when the sea level factor is off)
  double      combined = 1.0;      ///< k = k_grid · k_sea
  double      spheroidRadiusM = 0.0;  ///< R actually used (the ellipsoid's when the stored one is 0)
  double      rotationRad = 0.0;   ///< θ, counter-clockwise
};
[[nodiscard]] DrawingTransformFactors ResolveDrawingTransform(const DrawingSettings& s, int drawingInsUnits,
                                                              bool withTransform = true);
/// REQ-360: the transform's typed values that can never work, as a sentence, or "" — checked only
/// while Apply transform settings is on. Pure (no dictionary): a scale factor that is not > 0, a
/// spheroid radius ≤ 0, R + h ≤ 0, and a rotation point coincident with the reference point.
[[nodiscard]] std::string ValidateDrawingTransform(const DrawingSettings& s);
/// REQ-358 item 4 + REQ-360 on a WORLD point in drawing units: grid (zone unit) of it.
[[nodiscard]] geo::GeoResult DrawingWorldToGrid(const DrawingSettings& s, int drawingInsUnits, double worldX,
                                                double worldY);
/// The exact inverse of \ref DrawingWorldToGrid: the WORLD point (drawing units) of a grid coordinate.
[[nodiscard]] geo::GeoResult GridToDrawingWorld(const DrawingSettings& s, int drawingInsUnits, double easting,
                                                double northing);
/// REQ-358 item 4: latitude/longitude (degrees, x = longitude) of a LOCAL drawing point, in the
/// zone's own datum.
[[nodiscard]] geo::GeoResult DrawingPointToLatLong(const AppCommandState& st, double localX, double localY);
