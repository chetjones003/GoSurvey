#pragma once

#include "WikiContent.hpp"

#include <string>
#include <string_view>

/// Resolved in-app manual destination for contextual F1 help.
struct WikiHelpTarget {
  std::string pageSlug;         ///< e.g. "Drawing-Tools"
  std::string commandPrimary;   ///< lowercase registry primary, for ## heading scroll; may be empty
};

/// Build command → page map from `resources/wiki/Commands/*.md` (call after LoadWikiBundle).
void WikiHelpBuildIndex(const WikiBundle& bundle);

/// Lookup page for a registry primary name (already lowercased). Empty page when unknown.
[[nodiscard]] WikiHelpTarget WikiHelpLookupCommand(std::string_view primaryLower);

/// Call at the start of each frame before UI draws (clears stale hover context).
void WikiHelpBeginFrame();

/// Ribbon / status-bar / menu tooltip hover — pass the same tooltip string shown to the user.
void WikiHelpNotifyUiHover(std::string_view tooltipText);

/// True when a wiki heading line documents \p commandPrimary (e.g. "line" matches "## LINE (`L`)").
[[nodiscard]] bool WikiHeadingMatchesCommand(std::string_view headingLine,
                                             std::string_view commandPrimaryLower);

/// Parse a UI tooltip into a wiki target (Command bar: token or known UI topic).
[[nodiscard]] WikiHelpTarget WikiHelpTargetFromUiTooltip(std::string_view tooltipText);

/// Returns true when a ribbon/status tooltip was hovered this frame.
bool WikiHelpConsumeUiHover(std::string* outTooltip = nullptr);
