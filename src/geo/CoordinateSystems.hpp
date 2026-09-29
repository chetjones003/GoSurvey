#pragma once

// Coordinate systems (REQ-358, ADR-063, GitHub issue #582 increment 2): GoSurvey's one wrapper
// around CS-MAP. Nothing else in the tree includes a CS-MAP header, so replacing or updating CS-MAP
// touches this directory only. Pure: no UI, no GL, no drawing state. Every call returns a status
// instead of throwing, and a dictionary that cannot be loaded is reported, never a crash (REQ-201).
//
// CS-MAP keeps global state (the dictionary directory, its caches), so these functions are for one
// thread: the UI thread, or a test.

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
/// Latitude/longitude in \p fromCode's datum → latitude/longitude in \p toCode's datum, along
/// CS-MAP's datum path (including the shipped grid files, e.g. NADCON). \p fromCode / \p toCode
/// are coordinate-system codes (e.g. "LL27", "LL83", "LL84").
[[nodiscard]] GeoResult ConvertLatLong(const std::string& fromCode, const std::string& toCode,
                                       double longitude, double latitude);

}  // namespace geo
