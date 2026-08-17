#pragma once

#include <string>
#include <cstddef>

struct AppCommandState;

/// Loads gosurvey-user.json: sets startup template path, active layout name, and all user settings.
void LoadUserStartupPrefs(AppCommandState& st);

/// Re-applies only the settings section of gosurvey-user.json without touching path or layout fields.
/// Call this after loading a startup workspace template so user preferences win over template defaults.
void LoadUserStartupPrefSettings(AppCommandState& st);

/// Writes gosurvey-user.json beside the executable. Returns true on success, false if the file could not be written.
bool SaveUserStartupPrefs(const AppCommandState& st);

/// Copies \p utf8 into \p dest with a trailing NUL; never writes past \p cap bytes (including NUL).
void CopyUtf8PathCapped(char* dest, size_t cap, const char* utf8);

/// Retrieves telemetry tracking data from gosurvey-user.json.
/// If the file does not exist or does not contain these fields, returns empty strings and generates a new ID.
struct TelemetryIds {
  std::string installId;
  std::string lastActivePingDate;  // Format: YYYY-MM-DD, empty if no active ping sent yet
};
TelemetryIds GetTelemetryIds();

/// Updates the telemetry data in gosurvey-user.json.
/// If installId is non-empty, it is written (typically on first run).
/// If lastActivePingDate is non-empty, it is written (typically after an active ping fires).
/// Returns true on success, false if the file could not be written.
bool UpdateTelemetryIds(const std::string& installId, const std::string& lastActivePingDate);
