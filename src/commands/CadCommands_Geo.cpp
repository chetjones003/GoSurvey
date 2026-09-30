// CadCommands_Geo.cpp — the Geolocation ribbon tab's commands (REQ-359, GitHub issue #582 increment 3):
// Remove Location, the geographic marker (Reorient Marker / Edit Geographic Marker), and Mark
// Position, which places a Position Marker (D-2026-09-29-e).
//
// Same TU conventions as CadCommands_Align.cpp: entry points are declared in CadCommands.hpp; the
// shared command-layer helpers come from CadCommandsInternal.hpp. Coordinates follow REQ-101 / the
// local-storage invariant: world = local + worldDocumentOrigin, always computed in double.

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "geo/LocalGridTransform.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

/// One DMS component: 30°17'10.51249"N. Seconds carry into minutes / degrees when they round up.
std::string FormatDms(double deg, char pos, char neg) {
  const char hemi = deg < 0.0 ? neg : pos;
  constexpr double kSecScale = 100000.0;  // 5 decimals of a second, the NGS datasheet's precision
  const long long total = std::llround(std::fabs(deg) * 3600.0 * kSecScale);
  const long long d = total / (3600LL * static_cast<long long>(kSecScale));
  const long long rem = total % (3600LL * static_cast<long long>(kSecScale));
  const long long m = rem / (60LL * static_cast<long long>(kSecScale));
  const double sec = static_cast<double>(rem % (60LL * static_cast<long long>(kSecScale))) / kSecScale;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%lld\xC2\xB0%02lld'%08.5f\"%c", d, m, sec, hemi);
  return buf;
}

bool IsGeoCommand(AppCommandState::Kind k) {
  using K = AppCommandState::Kind;
  return k == K::GeoMarkPoint || k == K::GeoMarkLatLong || k == K::GeoReorientMarker ||
         k == K::DrawingSettingsPick;
}

/// Local-space box of a marker's circle (radius \p r) and label.
void MarkerBox(const CadPositionMarker& m, float r, float* mnX, float* mnY, float* mxX, float* mxY) {
  const float cx = static_cast<float>(m.x);
  const float cy = static_cast<float>(m.y);
  *mnX = std::min(cx - r, m.label.boxMinX);
  *mnY = std::min(cy - r, m.label.boxMinY);
  *mxX = std::max(cx + r, m.label.boxMaxX);
  *mxY = std::max(cy + r, m.label.boxMaxY);
}

}  // namespace

// ---------------------------------------------------------------------------
// Coordinates
// ---------------------------------------------------------------------------

namespace {

/// Meters in one drawing unit under the drawing's own foot (REQ-357); 0 = Unitless.
double MetersPerDrawingUnit(const DrawingSettings& s, int drawingInsUnits) {
  const double inchesPerMeter = DrawingInchesPerMeter(s.footDefinition);
  switch (drawingInsUnits) {
    case 1: return 1.0 / inchesPerMeter;
    case 2: return 12.0 / inchesPerMeter;
    case 4: return 0.001;
    case 6: return 1.0;
    default: return 0.0;
  }
}

/// The name the Transformation tab gives a CS-MAP length unit ("Zone units are in …").
std::string ZoneUnitName(const std::string& unit) {
  if (unit == "METER")
    return "Meters";
  if (unit == "FOOT")
    return "US Survey Feet";
  if (unit == "IFOOT")
    return "International Feet";
  return unit;
}

geo::GeoResult Fail(std::string why) {
  geo::GeoResult r;
  r.error = std::move(why);
  return r;
}

/// The pure transform of \p t in zone units, from its resolved factors \p f.
geo::LocalGridTransform TransformOf(const DrawingSettings::Transform& t, const DrawingTransformFactors& f) {
  geo::LocalGridTransform g;
  g.refLocalX = t.refLocalX * f.zoneUnitsPerDrawingUnit;
  g.refLocalY = t.refLocalY * f.zoneUnitsPerDrawingUnit;
  g.refGridX = t.refGridE;
  g.refGridY = t.refGridN;
  g.scale = f.combined;
  g.rotationRad = f.rotationRad;
  return g;
}

}  // namespace

std::string ValidateDrawingTransform(const DrawingSettings& s) {
  const DrawingSettings::Transform& t = s.transform;
  if (!t.apply)
    return {};
  for (double v : {t.elevation, t.spheroidRadiusM, t.userScaleFactor, t.refLocalX, t.refLocalY, t.refGridE,
                   t.refGridN, t.rotLocalX, t.rotLocalY, t.rotGridE, t.rotGridN, t.toNorthDeg, t.localAzimuthDeg,
                   t.gridAzimuthDeg})
    if (!std::isfinite(v))
      return "A transformation value is not a number.";
  if (t.computation == DrawingSettings::Transform::Computation::UserDefined && !(t.userScaleFactor > 0.0))
    return "The grid scale factor must be a number greater than 0.";
  if (t.spheroidRadiusM < 0.0)  // 0 = the zone ellipsoid's
    return "The spheroid radius must be a number greater than 0.";
  if (t.rotation == DrawingSettings::Transform::Rotation::RotationPoint) {
    double theta = 0.0;
    if (!geo::RotationFromPoints(t.refLocalX, t.refLocalY, t.rotLocalX, t.rotLocalY, t.refGridE, t.refGridN,
                                 t.rotGridE, t.rotGridN, &theta))
      return "The rotation point coincides with the reference point; choose a different rotation point.";
  }
  return {};
}

DrawingTransformFactors ResolveDrawingTransform(const DrawingSettings& s, int drawingInsUnits,
                                                bool withTransform) {
  DrawingTransformFactors f;
  if (!s.Geolocated()) {
    f.error = "The drawing has no coordinate system (No Datum, No Projection).";
    return f;
  }
  if (!geo::DictionariesLoaded()) {
    f.error = geo::DictionaryError();
    return f;
  }
  const std::optional<geo::CoordinateSystemInfo> zone = geo::FindCoordinateSystem(s.zoneCode);
  if (!zone) {
    f.error = s.zoneCode + " is unknown in this coordinate-system dictionary.";
    return f;
  }
  if (zone->geographic) {
    f.error = zone->code + " is a latitude/longitude system; a drawing point has no grid coordinate in it.";
    return f;
  }
  if (zone->metersPerUnit <= 0.0) {
    f.error = zone->code + " has a unit (" + zone->unit + ") that is not a length.";
    return f;
  }
  // A Unitless drawing is taken to be in the zone's unit.
  const double metersPerDrawingUnit = MetersPerDrawingUnit(s, drawingInsUnits);
  f.zoneUnitsPerDrawingUnit = metersPerDrawingUnit > 0.0 ? metersPerDrawingUnit / zone->metersPerUnit : 1.0;
  f.zoneUnitName = ZoneUnitName(zone->unit);
  f.ok = true;
  if (!withTransform)
    return f;

  const DrawingSettings::Transform& t = s.transform;
  DrawingSettings checked = s;
  checked.transform.apply = true;  // the tab's readouts want the reason even while the transform is off
  if (const std::string bad = ValidateDrawingTransform(checked); !bad.empty()) {
    f.transformError = bad;
    return f;
  }
  if (t.computation == DrawingSettings::Transform::Computation::UserDefined) {
    f.gridFactor = t.userScaleFactor;
  } else {
    const geo::GeoResult k = geo::GridScaleFactor(s.zoneCode, t.refGridE, t.refGridN);
    if (!k.ok) {
      f.transformError = "Grid scale factor at the reference point: " + k.error;
      return f;
    }
    f.gridFactor = k.x;
  }
  f.spheroidRadiusM = t.spheroidRadiusM > 0.0 ? t.spheroidRadiusM
                                              : geo::EllipsoidSemiMajorMeters(s.zoneCode).value_or(0.0);
  if (t.applySeaLevel) {
    // h in meters: the drawing unit, or the zone's unit for a Unitless drawing.
    const double metersPerElevationUnit = metersPerDrawingUnit > 0.0 ? metersPerDrawingUnit : zone->metersPerUnit;
    f.seaFactor = geo::SeaLevelScaleFactor(f.spheroidRadiusM, t.elevation * metersPerElevationUnit);
    if (!(f.spheroidRadiusM > 0.0) || !(f.seaFactor > 0.0)) {
      f.transformError = "The elevation is below the centre of the spheroid; there is no sea level factor.";
      return f;
    }
  }
  f.combined = f.gridFactor * f.seaFactor;
  switch (t.rotation) {
    case DrawingSettings::Transform::Rotation::RotationPoint:
      (void)geo::RotationFromPoints(t.refLocalX, t.refLocalY, t.rotLocalX, t.rotLocalY, t.refGridE, t.refGridN,
                                    t.rotGridE, t.rotGridN, &f.rotationRad);  // validated above
      break;
    case DrawingSettings::Transform::Rotation::ToNorth:
      f.rotationRad = geo::RotationFromToNorthDeg(t.toNorthDeg);
      break;
    case DrawingSettings::Transform::Rotation::Azimuth:
      f.rotationRad = geo::RotationFromAzimuthsDeg(t.localAzimuthDeg, t.gridAzimuthDeg);
      break;
  }
  f.transformOk = true;
  return f;
}

geo::GeoResult DrawingWorldToGrid(const DrawingSettings& s, int drawingInsUnits, double worldX, double worldY) {
  const DrawingTransformFactors f = ResolveDrawingTransform(s, drawingInsUnits, s.transform.apply);
  if (!f.ok)
    return Fail(f.error);
  geo::XY g{worldX * f.zoneUnitsPerDrawingUnit, worldY * f.zoneUnitsPerDrawingUnit};
  if (s.transform.apply) {
    if (!f.transformOk)
      return Fail("Transformation: " + f.transformError);
    g = geo::LocalToGrid(TransformOf(s.transform, f), g.x, g.y);
  }
  geo::GeoResult r;
  r.ok = true;
  r.x = g.x;
  r.y = g.y;
  return r;
}

geo::GeoResult GridToDrawingWorld(const DrawingSettings& s, int drawingInsUnits, double easting, double northing) {
  const DrawingTransformFactors f = ResolveDrawingTransform(s, drawingInsUnits, s.transform.apply);
  if (!f.ok)
    return Fail(f.error);
  if (!(f.zoneUnitsPerDrawingUnit > 0.0) || !std::isfinite(f.zoneUnitsPerDrawingUnit))
    return Fail("The drawing unit has no size in the zone's unit.");
  geo::XY local{easting, northing};
  if (s.transform.apply) {
    if (!f.transformOk)
      return Fail("Transformation: " + f.transformError);
    local = geo::GridToLocal(TransformOf(s.transform, f), easting, northing);
  }
  geo::GeoResult r;
  r.ok = true;
  r.x = local.x / f.zoneUnitsPerDrawingUnit;
  r.y = local.y / f.zoneUnitsPerDrawingUnit;
  return r;
}

geo::GeoResult DrawingPointToGrid(const AppCommandState& st, double localX, double localY) {
  // world = local + origin, in double (REQ-101).
  return DrawingWorldToGrid(st.drawingSettings, st.drawingInsUnits, localX + st.worldDocumentOriginX,
                            localY + st.worldDocumentOriginY);
}

geo::GeoResult DrawingPointToLatLong(const AppCommandState& st, double localX, double localY) {
  const geo::GeoResult grid = DrawingPointToGrid(st, localX, localY);
  if (!grid.ok)
    return grid;
  return geo::GridToLatLong(st.drawingSettings.zoneCode, grid.x, grid.y);
}

geo::GeoResult GridToDrawingPoint(const AppCommandState& st, double easting, double northing) {
  geo::GeoResult r = GridToDrawingWorld(st.drawingSettings, st.drawingInsUnits, easting, northing);
  if (r.ok) {
    r.x -= st.worldDocumentOriginX;
    r.y -= st.worldDocumentOriginY;
  }
  return r;
}

std::string FormatLatLongLabel(double latitudeDeg, double longitudeDeg) {
  return "LAT " + FormatDms(latitudeDeg, 'N', 'S') + "\nLONG " + FormatDms(longitudeDeg, 'E', 'W');
}

bool ParseGeoAngleDegrees(const std::string& raw, bool latitude, double* outDeg) {
  // Decimal degrees ("30.28625", "-97.7394") or up to three fields "D M S" separated by spaces or
  // °'" (e.g. 30 17 10.51249 N), with an optional N/S/E/W hemisphere letter at either end.
  std::string s = raw;
  double sign = 1.0;
  bool hemi = false;
  auto takeHemi = [&](char c) {
    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (latitude ? (u == 'N' || u == 'S') : (u == 'E' || u == 'W')) {
      if (u == 'S' || u == 'W')
        sign = -1.0;
      hemi = true;
      return true;
    }
    return false;
  };
  auto trim = [](std::string& t) {
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
      t.pop_back();
    size_t i = 0;
    while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i])))
      ++i;
    t.erase(0, i);
  };
  trim(s);
  if (!s.empty() && takeHemi(s.back()))
    s.pop_back();
  else if (!s.empty() && takeHemi(s.front()))
    s.erase(0, 1);
  // Separators: the degree sign (UTF-8 C2 B0), ' and " become spaces.
  std::string flat;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xB0) {
      flat.push_back(' ');
      ++i;
    } else if (c == '\'' || c == '"' || c == 'd' || c == 'D') {
      flat.push_back(' ');
    } else {
      flat.push_back(static_cast<char>(c));
    }
  }
  double f[3] = {0.0, 0.0, 0.0};
  int n = 0;
  const char* p = flat.c_str();
  while (*p != '\0') {
    while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)))
      ++p;
    if (*p == '\0')
      break;
    if (n == 3)
      return false;
    char* end = nullptr;
    f[n] = std::strtod(p, &end);
    if (end == p || !std::isfinite(f[n]))
      return false;
    p = end;
    ++n;
  }
  if (n == 0)
    return false;
  if (n > 1 && (f[1] < 0.0 || f[1] >= 60.0 || (n == 3 && (f[2] < 0.0 || f[2] >= 60.0))))
    return false;
  if (hemi && f[0] < 0.0)
    return false;  // "-97W" is ambiguous: a sign OR a hemisphere letter, not both
  double deg = std::fabs(f[0]) + f[1] / 60.0 + f[2] / 3600.0;
  if (f[0] < 0.0)
    deg = -deg;
  deg *= sign;
  if (std::fabs(deg) > (latitude ? 90.0 : 180.0))
    return false;
  *outDeg = deg;
  return true;
}

// ---------------------------------------------------------------------------
// GEODATA read (REQ-362, D-2026-09-29-g)
// ---------------------------------------------------------------------------

std::string GeoDataCoordinateSystemCode(const std::string& definition) {
  size_t b = 0;
  while (b < definition.size() && std::isspace(static_cast<unsigned char>(definition[b])))
    ++b;
  size_t e = definition.size();
  while (e > b && std::isspace(static_cast<unsigned char>(definition[e - 1])))
    --e;
  const std::string trimmed = definition.substr(b, e - b);
  if (trimmed.empty())
    return {};
  if (trimmed.find('<') == std::string::npos) {  // a bare code: one token, nothing else
    for (char c : trimmed)
      if (std::isspace(static_cast<unsigned char>(c)))
        return {};
    return trimmed;
  }
  // An XML definition: the id of the first element whose name ends in "CoordinateSystem"
  // (ProjectedCoordinateSystem, GeographicCoordinateSystem, …).
  static const std::string kSuffix = "CoordinateSystem";
  for (size_t lt = trimmed.find('<'); lt != std::string::npos; lt = trimmed.find('<', lt + 1)) {
    size_t n = lt + 1;
    while (n < trimmed.size() && (std::isalnum(static_cast<unsigned char>(trimmed[n])) || trimmed[n] == ':' ||
                                  trimmed[n] == '_'))
      ++n;
    const std::string name = trimmed.substr(lt + 1, n - lt - 1);
    if (name.size() < kSuffix.size() || name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
      continue;
    const size_t gt = trimmed.find('>', n);
    const std::string tag = trimmed.substr(n, gt == std::string::npos ? std::string::npos : gt - n);
    for (size_t at = tag.find("id"); at != std::string::npos; at = tag.find("id", at + 2)) {
      if (at > 0 && !std::isspace(static_cast<unsigned char>(tag[at - 1])))
        continue;  // part of another attribute's name
      size_t q = at + 2;
      while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q])))
        ++q;
      if (q >= tag.size() || tag[q] != '=')
        continue;
      ++q;
      while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q])))
        ++q;
      if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\''))
        continue;
      const size_t close = tag.find(tag[q], q + 1);
      if (close == std::string::npos)
        return {};
      return tag.substr(q + 1, close - q - 1);
    }
    return {};
  }
  return {};
}

void ApplyDwgGeoData(AppCommandState& st, const DwgGeoData& g, std::vector<std::string>& log) {
  DrawingSettings& ds = st.drawingSettings;
  ds.markerX = g.designX;  // GEODATA's design point is WCS, as the marker is (REQ-359 item 4)
  ds.markerY = g.designY;
  double north = 90.0;  // grid north when the file gives no direction
  if (std::hypot(g.northX, g.northY) > 1e-12) {
    north = std::atan2(g.northY, g.northX) * 180.0 / kPi;
    if (north < 0.0)
      north += 360.0;
  }
  ds.markerNorthDeg = north;

  const std::string code = GeoDataCoordinateSystemCode(g.coordinateSystemDefinition);
  std::optional<geo::CoordinateSystemInfo> zone;
  if (!code.empty())
    zone = geo::FindCoordinateSystem(code);
  // Unknown: kept verbatim (REQ-358 item 5). No definition: no zone — an Import DWG into a
  // geolocated drawing replaces its location, as it replaces its geometry.
  ds.zoneCode = zone ? zone->code : code;

  // REQ-360's scale settings, left for the user to review: the transform stays off.
  DrawingSettings::Transform t;
  if (g.scaleEstimation == 2 && std::isfinite(g.userScaleFactor) && g.userScaleFactor > 0.0) {
    t.computation = DrawingSettings::Transform::Computation::UserDefined;
    t.userScaleFactor = g.userScaleFactor;
  }
  t.applySeaLevel = g.seaLevelCorrection;
  if (std::isfinite(g.seaLevelElevation))
    t.elevation = g.seaLevelElevation;
  if (std::isfinite(g.projectionRadius) && g.projectionRadius > 0.0)
    t.spheroidRadiusM = g.projectionRadius;
  t.refLocalX = g.designX;
  t.refLocalY = g.designY;
  if (zone && g.hasReference && !zone->geographic) {
    const geo::GeoResult grid = geo::LatLongToGrid(zone->code, g.refLongitude, g.refLatitude);
    if (grid.ok) {
      t.refGridE = grid.x;
      t.refGridN = grid.y;
    }
  }
  ds.transform = t;

  char buf[200];
  std::snprintf(buf, sizeof(buf), "GEODATA — geographic marker at %.3f, %.3f, north %.4f\xC2\xB0 from the X axis.",
                g.designX, g.designY, north);
  log.push_back(buf);
  if (code.empty())
    log.push_back("GEODATA — the file names no coordinate system (Civil 3D keeps its zone elsewhere); "
                  "set the zone in Drawing Settings.");
  else if (!zone)
    log.push_back("GEODATA — coordinate system " + code + " is unknown in this dictionary; kept as the zone.");
  else
    log.push_back("GEODATA — zone " + zone->code + " (" + zone->description + ").");
}

// ---------------------------------------------------------------------------
// Remove Location / the geographic marker
// ---------------------------------------------------------------------------

bool RemoveGeoLocation(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.drawingSettings.Geolocated()) {
    log.push_back("Remove Location — the drawing has no location (No Datum, No Projection).");
    return false;
  }
  PushUndoSnapshot(st, "Remove Location");
  st.drawingSettings.zoneCode.clear();
  st.drawingSettings.ResetGeographicMarker();
  st.drawingSettings.transform = DrawingSettings::Transform{};  // REQ-360: belongs to the zone
  BumpCadGpuCache(st);  // document property: marks the drawing modified
  log.push_back("Remove Location — the drawing is no longer geolocated (No Datum, No Projection).");
  return true;
}

bool SetGeographicMarker(AppCommandState& st, double localX, double localY, double northDeg,
                         std::vector<std::string>& log) {
  if (!st.drawingSettings.Geolocated()) {
    log.push_back("Geographic marker — the drawing has no location; assign a zone in Drawing Settings first.");
    return false;
  }
  if (!std::isfinite(localX) || !std::isfinite(localY) || !std::isfinite(northDeg)) {
    log.push_back("Geographic marker — not a valid point or direction; nothing was changed.");
    return false;
  }
  double north = std::fmod(northDeg, 360.0);
  if (north < 0.0)
    north += 360.0;
  PushUndoSnapshot(st, "Reorient Marker");
  st.drawingSettings.markerX = localX + st.worldDocumentOriginX;
  st.drawingSettings.markerY = localY + st.worldDocumentOriginY;
  st.drawingSettings.markerNorthDeg = north;
  BumpCadGpuCache(st);
  char buf[160];
  std::snprintf(buf, sizeof(buf), "Geographic marker — design point set; north is %.4f\xC2\xB0 from the X axis.", north);
  log.push_back(buf);
  return true;
}

// ---------------------------------------------------------------------------
// Position Markers
// ---------------------------------------------------------------------------

float PositionMarkerRadiusWorld(const AppCommandState& st) {
  return kPositionMarkerPlottedRadiusIn * std::max(st.modelUnitsPerPlottedInch, 1.e-6f);
}

void CadPositionMarkerTranslate(CadPositionMarker* m, double dx, double dy, double dz) {
  m->x += dx;
  m->y += dy;
  m->z += static_cast<float>(dz);
  const float fx = static_cast<float>(dx);
  const float fy = static_cast<float>(dy);
  m->label.insX += fx;
  m->label.insY += fy;
  m->label.insZ += static_cast<float>(dz);
  m->label.boxMinX += fx;
  m->label.boxMaxX += fx;
  m->label.boxMinY += fy;
  m->label.boxMaxY += fy;
}

void CadPositionMarkerLocalBox(const AppCommandState& st, size_t i, float* mnX, float* mnY, float* mxX, float* mxY) {
  MarkerBox(st.cadPositionMarkers[i], PositionMarkerRadiusWorld(st), mnX, mnY, mxX, mxY);
}

bool CadPositionMarkerHit(const AppCommandState& st, size_t i, double x, double y, float tolWorld, double* distSq) {
  if (i >= st.cadPositionMarkers.size())
    return false;
  const CadPositionMarker& m = st.cadPositionMarkers[i];
  const double r = static_cast<double>(PositionMarkerRadiusWorld(st));
  const double dx = x - m.x;
  const double dy = y - m.y;
  const double d = std::sqrt(dx * dx + dy * dy);
  if (d <= r + tolWorld) {
    *distSq = std::max(0.0, d - r) * std::max(0.0, d - r);  // inside the circle is a direct hit
    return true;
  }
  const CadAnnotation& a = m.label;
  if (x >= a.boxMinX - tolWorld && x <= a.boxMaxX + tolWorld && y >= a.boxMinY - tolWorld &&
      y <= a.boxMaxY + tolWorld) {
    *distSq = 0.0;
    return true;
  }
  return false;
}

void OpenPositionMarkerLabelEditor(AppCommandState& st, int markerIndex, bool justPlaced) {
  if (markerIndex < 0 || static_cast<size_t>(markerIndex) >= st.cadPositionMarkers.size())
    return;
  CloseMtextRichEditorUi(st);
  st.mtextRichEditorPlacement = false;
  st.mtextRichEditorPaper = false;
  st.mtextRichEditorPlain = false;
  st.mtextRichEditorAnnIndex = -1;
  st.mtextRichEditorMarkerIndex = markerIndex;
  st.mtextRichEditorMarkerJustPlaced = justPlaced;
  st.mtextRichEditorBuf = st.cadPositionMarkers[static_cast<size_t>(markerIndex)].label.text;
  st.mtextRichEditorOpen = true;
  st.mtextRichEditorFocusRequest = true;
}

int PlacePositionMarkerAtLocal(AppCommandState& st, double localX, double localY, std::vector<std::string>& log) {
  const geo::GeoResult ll = DrawingPointToLatLong(st, localX, localY);
  if (!ll.ok) {
    log.push_back("Mark Position — " + ll.error);
    return -1;
  }
  PushUndoSnapshot(st, "Mark Position");
  CadPositionMarker m;
  m.x = localX;
  m.y = localY;
  m.z = CadCommitElevation(st);
  m.latitudeDeg = ll.y;
  m.longitudeDeg = ll.x;
  // The label: an MTEXT box beside the marker, up and to the right, sized in plotted inches so it
  // reads the same at every drawing scale (REQ-359 item 3).
  const float mup = std::max(st.modelUnitsPerPlottedInch, 1.e-6f);
  const float r = PositionMarkerRadiusWorld(st);
  const float h = std::max(st.defaultPlottedTextHeightInches, 0.01f) * mup;
  CadAnnotation& a = m.label;
  a.kind = CadAnnotation::Kind::Mtext;
  a.plottedHeightInches = st.defaultPlottedTextHeightInches;
  a.boxMinX = static_cast<float>(localX) + 1.5f * r;
  a.boxMaxX = a.boxMinX + 22.f * h;
  a.boxMaxY = static_cast<float>(localY) + 1.5f * r + 2.6f * h;
  a.boxMinY = a.boxMaxY - 2.6f * h;
  a.insX = a.boxMinX;
  a.insY = a.boxMinY;
  a.insZ = m.z;
  a.text = FormatLatLongLabel(m.latitudeDeg, m.longitudeDeg);
  StampActiveTextStyleOnNewText(st, a);
  st.cadPositionMarkers.push_back(std::move(m));
  st.cadPositionMarkerAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  const int ix = static_cast<int>(st.cadPositionMarkers.size()) - 1;
  log.push_back("Mark Position — Position Marker placed; edit its label (Esc keeps the latitude/longitude).");
  OpenPositionMarkerLabelEditor(st, ix, /*justPlaced=*/true);
  return ix;
}

int PlacePositionMarkerAtLatLong(AppCommandState& st, double latitudeDeg, double longitudeDeg,
                                 std::vector<std::string>& log) {
  if (!st.drawingSettings.Geolocated()) {
    log.push_back("Mark Position — the drawing has no location; assign a zone in Drawing Settings first.");
    return -1;
  }
  const geo::GeoResult grid = geo::LatLongToGrid(st.drawingSettings.zoneCode, longitudeDeg, latitudeDeg);
  if (!grid.ok) {
    log.push_back("Mark Position — " + grid.error);
    return -1;
  }
  const geo::GeoResult local = GridToDrawingPoint(st, grid.x, grid.y);
  if (!local.ok) {
    log.push_back("Mark Position — " + local.error);
    return -1;
  }
  const int ix = PlacePositionMarkerAtLocal(st, local.x, local.y, log);
  if (ix >= 0) {
    // Keep exactly what the user typed, not the inverse of the float-rounded grid (and the label).
    CadPositionMarker& m = st.cadPositionMarkers[static_cast<size_t>(ix)];
    m.latitudeDeg = latitudeDeg;
    m.longitudeDeg = longitudeDeg;
    m.label.text = FormatLatLongLabel(latitudeDeg, longitudeDeg);
    st.mtextRichEditorBuf = m.label.text;
  }
  return ix;
}

int DropPositionMarkersFromSelection(AppCommandState& st, const char* verb, std::vector<std::string>& log) {
  const size_t before = st.selection.size();
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [](const SelectedEntity& e) {
                                      return e.type == SelectedEntity::Type::PositionMarker;
                                    }),
                     st.selection.end());
  const int dropped = static_cast<int>(before - st.selection.size());
  if (dropped > 0)
    log.push_back(std::string(verb) + " — " + std::to_string(dropped) +
                  " Position Marker(s) left unchanged: a Position Marker can only be moved, copied or erased.");
  return dropped;
}

// ---------------------------------------------------------------------------
// GEOMARKPOINT / GEOMARKLATLONG / GEOREORIENTMARKER
// ---------------------------------------------------------------------------

/// \p needsZone: the command works on the drawing's zone. The Drawing Settings pick does not — the
/// window may hold a zone that is not applied yet.
static bool StartGeoCommand(AppCommandState& st, AppCommandState::Kind k, const char* name,
                            std::vector<std::string>& log, bool needsZone = true) {
  if (st.active != AppCommandState::Kind::None) {
    log.push_back(std::string(name) + " — finish or cancel the active command first.");
    return false;
  }
  if (needsZone && !st.drawingSettings.Geolocated()) {
    log.push_back(std::string(name) + " — the drawing has no location; assign a zone in Drawing Settings first.");
    return false;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = k;
  st.lastCommand = k;
  st.geoCmdPhase = AppCommandState::GeoCmdPhase::WaitFirst;
  return true;
}

void StartGeoMarkPointCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (StartGeoCommand(st, AppCommandState::Kind::GeoMarkPoint, "GEOMARKPOINT", log))
    log.push_back("GEOMARKPOINT — specify the position (click or type X,Y). ESC cancels.");
}

void StartGeoMarkLatLongCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (StartGeoCommand(st, AppCommandState::Kind::GeoMarkLatLong, "GEOMARKLATLONG", log))
    log.push_back("GEOMARKLATLONG — enter the latitude (e.g. 30.28625 or 30 17 10.5N). ESC cancels.");
}

void StartGeoReorientMarkerCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (StartGeoCommand(st, AppCommandState::Kind::GeoReorientMarker, "GEOREORIENTMARKER", log))
    log.push_back("GEOREORIENTMARKER — specify the design point (click or type X,Y). ESC cancels.");
}

bool StartDrawingSettingsPick(AppCommandState& st, AppCommandState::DrawingSettingsPickState::Target target,
                              std::vector<std::string>& log) {
  using T = AppCommandState::DrawingSettingsPickState::Target;
  const AppCommandState::Kind repeat = st.lastCommand;  // an internal pick is not a command to repeat
  if (target == T::None ||
      !StartGeoCommand(st, AppCommandState::Kind::DrawingSettingsPick, "DRAWINGSETTINGS", log, false))
    return false;
  st.lastCommand = repeat;
  st.drawingSettingsPick = {};
  st.drawingSettingsPick.target = target;
  const bool point = target == T::ReferencePoint || target == T::RotationPoint;
  log.push_back(point ? "DRAWINGSETTINGS — pick a point or a survey point (or type X,Y). ESC returns to the window."
                      : "DRAWINGSETTINGS — pick the first point of the direction. ESC returns to the window.");
  return true;
}

namespace {

/// The survey point at a picked LOCAL point (a Survey point snap lands on it), or 0. A typed point
/// arrives through float (ParseStoragePoint), so the match allows float rounding at that magnitude.
int SurveyPointNumberAt(const AppCommandState& st, double localX, double localY) {
  const double tol = std::max(1e-6, 4.0 * 1.1920929e-7 * std::max(std::fabs(localX), std::fabs(localY)));
  int best = 0;
  double bestD = tol;
  for (const SurveyPoint& p : st.surveyPoints) {
    const double d = std::hypot(p.easting - localX, p.northing - localY);
    if (d <= bestD) {
      bestD = d;
      best = p.id;
    }
  }
  return best;
}

bool SubmitDrawingSettingsPick(AppCommandState& st, double localX, double localY, std::vector<std::string>& log) {
  using T = AppCommandState::DrawingSettingsPickState::Target;
  using GP = AppCommandState::GeoCmdPhase;
  AppCommandState::DrawingSettingsPickState& pick = st.drawingSettingsPick;
  if (pick.target == T::ReferencePoint || pick.target == T::RotationPoint) {
    pick.worldX = localX + st.worldDocumentOriginX;
    pick.worldY = localY + st.worldDocumentOriginY;
    pick.pointNumber = SurveyPointNumberAt(st, localX, localY);
    pick.done = true;
    st.active = AppCommandState::Kind::None;
    return true;
  }
  if (st.geoCmdPhase == GP::WaitFirst) {
    st.geoCmdFirstA = localX;
    st.geoCmdFirstB = localY;
    st.geoCmdPhase = GP::WaitSecond;
    log.push_back("DRAWINGSETTINGS — pick the second point of the direction.");
    return true;
  }
  const double dx = localX - st.geoCmdFirstA;
  const double dy = localY - st.geoCmdFirstB;
  if (std::hypot(dx, dy) < 1.e-9) {
    log.push_back("DRAWINGSETTINGS — the two points must differ; pick again.");
    return false;
  }
  pick.azimuthDeg = geo::AzimuthDeg(dx, dy);
  pick.done = true;
  st.geoCmdPhase = GP::WaitFirst;
  st.active = AppCommandState::Kind::None;
  return true;
}

}  // namespace

bool SubmitGeoCommandPoint(AppCommandState& st, double localX, double localY, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using GP = AppCommandState::GeoCmdPhase;
  if (st.active == K::DrawingSettingsPick)
    return SubmitDrawingSettingsPick(st, localX, localY, log);
  if (st.active == K::GeoMarkPoint) {
    st.active = K::None;
    return PlacePositionMarkerAtLocal(st, localX, localY, log) >= 0;
  }
  if (st.active != K::GeoReorientMarker)
    return false;
  if (st.geoCmdPhase == GP::WaitFirst) {
    st.geoCmdFirstA = localX;
    st.geoCmdFirstB = localY;
    st.geoCmdPhase = GP::WaitSecond;
    log.push_back("GEOREORIENTMARKER — specify a point in the north direction.");
    return true;
  }
  const double dx = localX - st.geoCmdFirstA;
  const double dy = localY - st.geoCmdFirstB;
  if (std::hypot(dx, dy) < 1.e-9) {
    log.push_back("GEOREORIENTMARKER — the north point must differ from the design point; pick again.");
    return false;
  }
  st.active = K::None;
  st.geoCmdPhase = GP::WaitFirst;
  return SetGeographicMarker(st, st.geoCmdFirstA, st.geoCmdFirstB, std::atan2(dy, dx) * 180.0 / kPi, log);
}

bool HandleGeoCommandText(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using GP = AppCommandState::GeoCmdPhase;
  if (!IsGeoCommand(st.active))
    return false;
  if (st.active == K::GeoMarkLatLong) {
    if (st.geoCmdPhase == GP::WaitFirst) {
      // "lat,long" in one go is accepted too.
      const size_t comma = line.find(',');
      double lat = 0.0, lon = 0.0;
      if (comma != std::string::npos && ParseGeoAngleDegrees(line.substr(0, comma), true, &lat) &&
          ParseGeoAngleDegrees(line.substr(comma + 1), false, &lon)) {
        st.active = K::None;
        PlacePositionMarkerAtLatLong(st, lat, lon, log);
        return true;
      }
      if (!ParseGeoAngleDegrees(line, true, &lat)) {
        log.push_back("GEOMARKLATLONG — not a latitude: decimal degrees (-90..90) or D M S with N/S.");
        return true;
      }
      st.geoCmdFirstA = lat;
      st.geoCmdPhase = GP::WaitSecond;
      log.push_back("GEOMARKLATLONG — enter the longitude (west negative, e.g. -97.7394 or 97 44 21.7W).");
      return true;
    }
    double lon = 0.0;
    if (!ParseGeoAngleDegrees(line, false, &lon)) {
      log.push_back("GEOMARKLATLONG — not a longitude: decimal degrees (-180..180) or D M S with E/W.");
      return true;
    }
    st.active = K::None;
    st.geoCmdPhase = GP::WaitFirst;
    PlacePositionMarkerAtLatLong(st, st.geoCmdFirstA, lon, log);
    return true;
  }
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
    log.push_back(std::string(AppCommandState::KindName(st.active)) + " — pick in the viewport or type X,Y.");
    return true;
  }
  SubmitGeoCommandPoint(st, px, py, log);
  return true;
}

const char* GeoCommandPrompt(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  const bool first = st.geoCmdPhase == AppCommandState::GeoCmdPhase::WaitFirst;
  switch (st.active) {
    case K::GeoMarkPoint:      return "Specify position:";
    case K::GeoMarkLatLong:    return first ? "Enter latitude:" : "Enter longitude:";
    case K::GeoReorientMarker: return first ? "Specify design point:" : "Specify north direction:";
    case K::DrawingSettingsPick:
      if (st.drawingSettingsPick.target == AppCommandState::DrawingSettingsPickState::Target::ReferencePoint)
        return "Specify reference point:";
      if (st.drawingSettingsPick.target == AppCommandState::DrawingSettingsPickState::Target::RotationPoint)
        return "Specify rotation point:";
      return first ? "Specify first point of the direction:" : "Specify second point of the direction:";
    default:                   return nullptr;
  }
}
