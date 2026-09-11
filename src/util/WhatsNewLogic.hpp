#pragma once

#include <string_view>

/// REQ-336 — pure helpers for the What's New billboard (no UI, no filesystem).

/// True when auto-open should run for this launch: the version has not been dismissed and
/// this launch has not already auto-opened the window.
[[nodiscard]] bool WhatsNewShouldAutoOpen(std::string_view runningVersion,
                                          std::string_view dismissedVersion,
                                          bool alreadyAutoOpenedThisLaunch);

/// True when the dismiss checkbox should appear checked for \p runningVersion.
[[nodiscard]] bool WhatsNewDismissCheckboxChecked(std::string_view runningVersion,
                                                  std::string_view dismissedVersion);
