#pragma once

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
