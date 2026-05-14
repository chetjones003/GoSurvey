#pragma once

#include <cstddef>

struct AppCommandState;

/// UTF-8 path to startup .gs template (see Settings → Startup). Stored in gosurvey-user.json beside the executable.
void LoadUserStartupPrefs(AppCommandState& st);

/// Writes gosurvey-user.json beside the executable (best-effort; may fail if the directory is not writable).
void SaveUserStartupPrefs(const AppCommandState& st);

/// Copies \p utf8 into \p dest with a trailing NUL; never writes past \p cap bytes (including NUL).
void CopyUtf8PathCapped(char* dest, size_t cap, const char* utf8);
