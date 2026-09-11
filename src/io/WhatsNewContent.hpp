#pragma once

#include <string>

/// REQ-336 — load the shipped What's New Markdown body.

struct WhatsNewContent {
  bool ok = false;                 ///< false when the file is missing or unreadable
  std::string markdown;            ///< file body when ok; empty when not
  std::string fallbackMessage;     ///< human-readable reason when !ok
};

/// Reads `resources/whats-new.md` beside the executable (or cwd). Never throws.
[[nodiscard]] WhatsNewContent LoadWhatsNewContent();
