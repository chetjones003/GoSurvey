// --- Geolocation tab (REQ-359, GitHub issue #582 increment 3; CadCommands_Geo.cpp) ------------------
/// The contextual Geolocation tab is shown exactly while the active drawing is geolocated (item 1).
[[nodiscard]] inline bool GeolocationRibbonTabVisible(const AppCommandState& st) {
  return st.drawing.drawingSettings.Geolocated();
}
/// The inverse of \ref DrawingPointToGrid: the LOCAL drawing point (x, y) of a grid coordinate in the
/// zone's unit. Fails, with the reason, exactly when DrawingPointToGrid would.
[[nodiscard]] geo::GeoResult GridToDrawingPoint(const AppCommandState& st, double easting, double northing);

// --- Traverse Editor scale factor (REQ-401, GitHub issue #828; CadCommands_Geo.cpp) ---------------
/// Resolves the ground→grid multiplier the Traverse Editor applies when its "Apply scale factor"
/// option is on: Combined = the Transformation tab's k = k_grid · k_sea at its reference point;
/// GridOnly = k_grid alone. Reuses \ref ResolveDrawingTransform rather than a second computation.
/// \p factorOut is left at 1.0 and a non-empty reason is returned when the drawing has no
/// coordinate system (REQ-358) or the Transformation tab's reference point cannot be resolved
/// (REQ-360's own failure cases).
[[nodiscard]] std::string ResolveTraverseScaleFactor(const AppCommandState& st,
                                                     TraverseData::ScaleFactorKind kind,
                                                     double* factorOut);

/// WGS 84 latitude/longitude ↔ a LOCAL drawing point (REQ-363 item 3, ADR-064 (b)): the same chain as
/// \ref DrawingPointToGrid / \ref GridToDrawingPoint plus the datum path to WGS 84, with the zone, the
/// datum paths and the REQ-360 transformation resolved ONCE by \ref Open, so an online map can place
/// thousands of tile points. Captures the drawing's settings, unit and origin at Open; reopen when any
/// of them changes. UI thread only (CS-MAP, ADR-063 (c)).
class DrawingWgs84Frame {
 public:
  /// False, with the reason, when the drawing's zone or transformation cannot be resolved.
  bool Open(const AppCommandState& st, std::string* error);
  [[nodiscard]] bool IsOpen() const { return converter_.IsOpen(); }
  /// x = longitude, y = latitude (WGS 84) → local drawing point.
  [[nodiscard]] geo::GeoResult LocalFromWgs84(double longitude, double latitude) const;
  /// Local drawing point → WGS 84 (x = longitude).
  [[nodiscard]] geo::GeoResult Wgs84FromLocal(double localX, double localY) const;

 private:
  geo::Wgs84GridConverter converter_;
  bool                    applyTransform_ = false;
  geo::LocalGridTransform transform_{};
  double                  zoneUnitsPerDrawingUnit_ = 1.0;
  double                  originX_ = 0.0;
  double                  originY_ = 0.0;
};
/// "LAT 30°17'10.51249\"N\nLONG 97°44'21.71739\"W" — a Position Marker's default label.
[[nodiscard]] std::string FormatLatLongLabel(double latitudeDeg, double longitudeDeg);
/// Decimal degrees, or "D M S" (spaces or °'\" between) with an optional N/S (\p latitude) or E/W
/// letter; S / W / a leading minus are negative. False when it does not parse or is out of range.
[[nodiscard]] bool ParseGeoAngleDegrees(const std::string& raw, bool latitude, double* outDeg);
/// Remove Location (REQ-359 item 2): zone → No Datum, No Projection and the geographic marker back to
/// its default, as ONE undo step. False (logged) when the drawing is not geolocated.
bool RemoveGeoLocation(AppCommandState& st, std::vector<std::string>& log);
/// REQ-363 item 8: choose the drawing's online map, as one undo step. False (logged) when the drawing
/// is not geolocated and \p map is not Map Off; choosing the current map again changes nothing.
bool SetOnlineMap(AppCommandState& st, DrawingSettings::OnlineMap map, std::vector<std::string>& log);
/// REQ-364 item 1: Capture Area (\p pick false: the visible area) or Pick Area (\p pick true: two
/// corners). Refused (logged) with Map Off, no location, or another command running.
bool StartCaptureMapArea(AppCommandState& st, bool pick, std::vector<std::string>& log);
/// REQ-364 item 5: stores \p area as one undo step and ends the command. Called by the map controller.
void CommitMapCapture(AppCommandState& st, DrawingSettings::CapturedArea area, std::vector<std::string>& log);
/// REQ-364 item 2: ends the command storing nothing, saying why. Called by the map controller.
void FailMapCapture(AppCommandState& st, const std::string& why, std::vector<std::string>& log);
/// REQ-364 item 5: removes every captured area, as one undo step. False (logged) when there is none.
bool RemoveCapturedMapAreas(AppCommandState& st, std::vector<std::string>& log);
/// Sets the geographic marker (REQ-359 item 4) to the LOCAL point (stored as WORLD) and a north
/// direction in degrees CCW from +X. One undo step. False (logged) when not geolocated.
bool SetGeographicMarker(AppCommandState& st, double localX, double localY, double northDeg,
                         std::vector<std::string>& log);
/// A Position Marker's circle radius in drawing units: \ref kPositionMarkerPlottedRadiusIn × scale.
[[nodiscard]] float PositionMarkerRadiusWorld(const AppCommandState& st);
/// Moves a marker and its label together.
/// LOCAL box around marker \p i's circle and label (box select, zoom).
void CadPositionMarkerLocalBox(const AppCommandState& st, size_t i, float* mnX, float* mnY, float* mxX, float* mxY);
/// True when (x, y) LOCAL is within \p tolWorld of marker \p i's circle or on its label box.
[[nodiscard]] bool CadPositionMarkerHit(const AppCommandState& st, size_t i, double x, double y, float tolWorld,
                                        double* distSq);
/// Opens the MTEXT editor on marker \p markerIndex's label; \p justPlaced makes the commit join the
/// placement's undo step.
void OpenPositionMarkerLabelEditor(AppCommandState& st, int markerIndex, bool justPlaced);
/// Mark Position (REQ-359 item 3): places a Position Marker at a LOCAL point, labelled with its
/// latitude/longitude, as one undo step, and opens the MTEXT editor on the label. Returns its index,
/// or -1 (logged) when the point has no latitude/longitude (not geolocated, unknown zone, …).
int PlacePositionMarkerAtLocal(AppCommandState& st, double localX, double localY, std::vector<std::string>& log);
/// Mark Position ▸ Lat-Long: the same, at a latitude/longitude in the zone's datum.
int PlacePositionMarkerAtLatLong(AppCommandState& st, double latitudeDeg, double longitudeDeg,
                                 std::vector<std::string>& log);

void CadMultileaderTranslate(CadMultileader* ml, double dx, double dy, double dz);
// CadMultileaderLocalBox is Document's (AnnotationBounds.hpp).
[[nodiscard]] bool CadMultileaderHit(const CadMultileader& ml, double x, double y, float tolWorld,
                                     double* distSq);

void StartMleaderCommand(AppCommandState& st, std::vector<std::string>& log);
void StartMleaderAddLeaderCommand(AppCommandState& st, std::vector<std::string>& log);
void RemoveLeaderFromSelectedMultileader(AppCommandState& st, std::vector<std::string>& log);
void CommitMleaderAddLeaderAt(AppCommandState& st, float tipX, float tipY, std::vector<std::string>& log);
void ResetMleaderDraft(AppCommandState& st);
void CommitMleaderLandingAt(AppCommandState& st, float landX, float landY, std::vector<std::string>& log);
void FinishMleaderCommand(AppCommandState& st, std::vector<std::string>& log);
void AbandonJustPlacedMultileader(AppCommandState& st);
void StartGeoMarkPointCommand(AppCommandState& st, std::vector<std::string>& log);
void StartGeoMarkLatLongCommand(AppCommandState& st, std::vector<std::string>& log);
void StartGeoReorientMarkerCommand(AppCommandState& st, std::vector<std::string>& log);
/// REQ-360: start the Transformation tab's pick for \p target (the window hides until it ends).
/// False (logged) while another command runs or when \p target is None.
bool StartDrawingSettingsPick(AppCommandState& st, AppCommandState::DrawingSettingsPickState::Target target,
                              std::vector<std::string>& log);
/// REQ-362: what an AutoCAD / Civil 3D `GEODATA` object says. Read: copied out by the DWG importer
/// (IO) and applied by \ref ApplyDwgGeoData. Write (item 2): built by \ref BuildDwgGeoData and
/// encoded by the DWG exporter.
struct DwgGeoData {
  double      designX = 0.0;  ///< Design point, WCS drawing units.
  double      designY = 0.0;
  /// What GEODATA's reference point (the design point in the coordinate system) is, by its
  /// coordinate type: 3 geographic = longitude / latitude (degrees), 2 projected grid = easting /
  /// northing in the zone's unit; anything else (unknown, local grid) gives no grid reference.
  enum class Reference : std::uint8_t { None, Geographic, ProjectedGrid };
  Reference   reference = Reference::None;
  double      refX = 0.0;  ///< Longitude or easting.
  double      refY = 0.0;  ///< Latitude or northing.
  double      northX = 0.0;  ///< North direction in the drawing; (0, 0) when the file has none.
  double      northY = 0.0;
  int         scaleEstimation = 1;  ///< 1 none, 2 user specified, 3 grid scale at reference point, 4 prismoidal
  double      userScaleFactor = 1.0;
  bool        seaLevelCorrection = false;
  double      seaLevelElevation = 0.0;
  double      projectionRadius = 0.0;
  std::string coordinateSystemDefinition;  ///< A code, an XML definition, or empty (Civil 3D).
  int         horizontalUnits = 0;         ///< AutoCAD units value (the INSUNITS codes); written only.
  double      horizontalUnitScale = 1.0;   ///< Meters per drawing unit; written only.
};
/// REQ-362 item 2: the GEODATA a DWG save writes for this drawing — design point = the marker (WCS),
/// reference = its latitude / longitude from the zone, north, the drawing unit, the zone code and
/// REQ-360's scale settings. False, with \p why set, when there is no zone or the marker's
/// latitude / longitude cannot be computed (an unknown zone code, a point outside the zone): then
/// nothing is written, never a partial GEODATA.
[[nodiscard]] bool BuildDwgGeoData(const AppCommandState& st, DwgGeoData* out, std::string* why);
/// The coordinate-system code a GEODATA definition names: the definition itself when it is a bare
/// code, else the `id` of its first `…CoordinateSystem` element; "" when it names none.
[[nodiscard]] std::string GeoDataCoordinateSystemCode(const std::string& definition);
/// REQ-362 item 1: sets the geographic marker (design point, north), the zone when the definition
/// names one (a code the dictionary does not know is kept, REQ-358 item 5), and REQ-360's scale
/// settings with the transform left off. No undo step (it is part of opening the file); logs what
/// it set and, when there is no coordinate system, says so.
void ApplyDwgGeoData(AppCommandState& st, const DwgGeoData& g, std::vector<std::string>& log);
/// A viewport pick or typed point for GEOMARKPOINT / GEOREORIENTMARKER (LOCAL coordinates).
bool SubmitGeoCommandPoint(AppCommandState& st, double localX, double localY, std::vector<std::string>& log);
/// Typed input while a geolocation command is active; false when none is.
bool HandleGeoCommandText(AppCommandState& st, const std::string& line, std::vector<std::string>& log);
/// The dynamic-input / command-line prompt for the active geolocation command, or nullptr.
[[nodiscard]] const char* GeoCommandPrompt(const AppCommandState& st);
