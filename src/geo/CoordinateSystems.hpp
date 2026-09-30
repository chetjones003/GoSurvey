#pragma once

// Coordinate systems (REQ-358, ADR-063, GitHub issue #582 increment 2): GoSurvey's one wrapper
// around CS-MAP. Nothing else in the tree includes a CS-MAP header, so replacing or updating CS-MAP
// touches this directory only. Pure: no UI, no GL, no drawing state. Every call returns a status
// instead of throwing, and a dictionary that cannot be loaded is reported, never a crash (REQ-201).
//
// CS-MAP keeps global state (the dictionary directory, its caches), so these functions are for one
// thread: the UI thread, or a test.

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace geo {

/// The category and details shown for "no coordinate system" (REQ-358 item 2).
inline constexpr const char* kNoZoneCategory = "No Datum, No Projection";

/// Point CS-MAP at the compiled dictionaries in \p directory. Repeatable: a later call re-points
/// it. False, with the reason in DictionaryError(), when the directory holds no usable dictionary.
bool LoadDictionaries(const std::string& directory);
/// True after a LoadDictionaries that succeeded.
[[nodiscard]] bool DictionariesLoaded();
/// Why the last LoadDictionaries failed; empty after a success.
[[nodiscard]] const std::string& DictionaryError();

/// Every category in the dictionary, in the dictionary's order (e.g. "USA, Texas"). Empty when no
/// dictionary is loaded.
[[nodiscard]] const std::vector<std::string>& Categories();
/// Every coordinate-system code in \p category, in the dictionary's order.
[[nodiscard]] std::vector<std::string> CoordinateSystemsIn(const std::string& category);
/// The first category (dictionary order) that lists \p code, or empty.
[[nodiscard]] std::string CategoryOf(const std::string& code);

struct CoordinateSystemInfo {
  std::string code;         ///< CS-MAP key name, e.g. "HARN/TX.TX-C".
  std::string description;  ///< e.g. "HARN (HPGN datum) Texas State Planes, Central Zone, Meter".
  std::string projection;   ///< CS-MAP projection key, e.g. "LM".
  std::string datum;        ///< Datum key, e.g. "HARN/TX"; the ellipsoid key when it has no datum.
  std::string unit;         ///< e.g. "METER", "FOOT" (US survey foot), "IFOOT".
  double metersPerUnit = 1.0;  ///< Size of one \p unit in meters (0 for angular units).
  bool geographic = false;  ///< A lat/long system (projection "LL").
};

/// The details of \p code, or nothing when the dictionary does not know it (or is not loaded).
[[nodiscard]] std::optional<CoordinateSystemInfo> FindCoordinateSystem(const std::string& code);

/// The semi-major axis (meters) of \p code's ellipsoid — the Transformation tab's default Spheroid
/// radius (REQ-360 item 3) — or nothing when the code is unknown (or no dictionary is loaded).
[[nodiscard]] std::optional<double> EllipsoidSemiMajorMeters(const std::string& code);

/// A converted coordinate. Lat/long are degrees, x = longitude (east positive), y = latitude.
struct GeoResult {
  bool        ok = false;
  double      x = 0.0;
  double      y = 0.0;
  std::string error;  ///< Why it failed, when !ok.
};

/// Grid (easting, northing in the system's own unit) → latitude/longitude in the system's datum.
[[nodiscard]] GeoResult GridToLatLong(const std::string& code, double easting, double northing);
/// Latitude/longitude in the system's datum → grid (easting, northing in the system's unit).
[[nodiscard]] GeoResult LatLongToGrid(const std::string& code, double longitude, double latitude);
/// CS-MAP's point (grid) scale factor k of the projection \p code at the grid point (easting,
/// northing in the system's unit), in x (REQ-360 item 4, Reference Point computation). Fails for a
/// lat/long system or a point outside the projection's domain.
[[nodiscard]] GeoResult GridScaleFactor(const std::string& code, double easting, double northing);
/// Latitude/longitude in \p fromCode's datum → latitude/longitude in \p toCode's datum, along
/// CS-MAP's datum path (including the shipped grid files, e.g. NADCON). \p fromCode / \p toCode
/// are coordinate-system codes (e.g. "LL27", "LL83", "LL84").
[[nodiscard]] GeoResult ConvertLatLong(const std::string& fromCode, const std::string& toCode,
                                       double longitude, double latitude);

/// WGS 84 latitude/longitude ↔ a projected zone's grid (REQ-363, ADR-064 (b)), with the zone, WGS 84
/// and both datum paths looked up ONCE: the online map places thousands of tile points, and the
/// one-shot functions above search the dictionary on every call. Same datum path and failure rules as
/// \ref ConvertLatLong.
class Wgs84GridConverter {
 public:
  Wgs84GridConverter();
  ~Wgs84GridConverter();
  Wgs84GridConverter(const Wgs84GridConverter&) = delete;
  Wgs84GridConverter& operator=(const Wgs84GridConverter&) = delete;

  /// Opens \p zoneCode (closing any previous zone). False, with the reason in \p error, when the
  /// dictionary is not loaded, the code is unknown or geographic, or no datum path exists.
  bool Open(const std::string& zoneCode, std::string* error);
  void Close();
  [[nodiscard]] bool IsOpen() const;
  /// WGS 84 (x = longitude) → zone grid (easting, northing in the zone's unit).
  [[nodiscard]] GeoResult ToGrid(double longitude, double latitude) const;
  /// Zone grid → WGS 84 (x = longitude).
  [[nodiscard]] GeoResult ToWgs84(double easting, double northing) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace geo
