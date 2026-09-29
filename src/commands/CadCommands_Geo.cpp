// CadCommands_Geo.cpp — the Geolocation ribbon tab's commands (REQ-359, GitHub issue #582 increment 3):
// Remove Location, the geographic marker (Reorient Marker / Edit Geographic Marker), and Mark
// Position, which places a Position Marker (D-2026-09-29-e).
//
// Same TU conventions as CadCommands_Align.cpp: entry points are declared in CadCommands.hpp; the
// shared command-layer helpers come from CadCommandsInternal.hpp. Coordinates follow REQ-101 / the
// local-storage invariant: world = local + worldDocumentOrigin, always computed in double.

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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
  return k == K::GeoMarkPoint || k == K::GeoMarkLatLong || k == K::GeoReorientMarker;
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

geo::GeoResult GridToDrawingPoint(const AppCommandState& st, double easting, double northing) {
  geo::GeoResult r;
  // DrawingPointToGrid is linear (scale k, then the world origin), so its inverse is exact: take the
  // scale it applies to a unit offset and undo it. That keeps ONE statement of the unit rules.
  const geo::GeoResult g0 = DrawingPointToGrid(st, -st.worldDocumentOriginX, -st.worldDocumentOriginY);
  if (!g0.ok)
    return g0;
  const geo::GeoResult g1 = DrawingPointToGrid(st, 1.0 - st.worldDocumentOriginX, -st.worldDocumentOriginY);
  if (!g1.ok)
    return g1;
  const double k = g1.x - g0.x;  // zone units per drawing unit (g0 is the world origin → 0,0)
  if (!(k > 0.0) || !std::isfinite(k)) {
    r.error = "The drawing unit has no size in the zone's unit.";
    return r;
  }
  r.ok = true;
  r.x = easting / k - st.worldDocumentOriginX;
  r.y = northing / k - st.worldDocumentOriginY;
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

static bool StartGeoCommand(AppCommandState& st, AppCommandState::Kind k, const char* name,
                            std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::None) {
    log.push_back(std::string(name) + " — finish or cancel the active command first.");
    return false;
  }
  if (!st.drawingSettings.Geolocated()) {
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

bool SubmitGeoCommandPoint(AppCommandState& st, double localX, double localY, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using GP = AppCommandState::GeoCmdPhase;
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
    default:                   return nullptr;
  }
}
