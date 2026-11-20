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
/// If the file does not exist or does not contain this field, returns an empty string.
///
/// REQ-080 (amended 2026-08-23, D-2026-08-23-f): previously also tracked `lastActivePingDate` to
/// throttle "active" pings to once per rolling 24 hours. That throttle is gone by explicit user
/// decision — a ping fires every launch — so there is nothing left to persist but the identity.
struct TelemetryIds {
  std::string installId;
};
TelemetryIds GetTelemetryIds();

/// Persists the install ID in gosurvey-user.json (typically on first run). Returns true on
/// success, false if the file could not be written.
bool UpdateTelemetryIds(const std::string& installId);
