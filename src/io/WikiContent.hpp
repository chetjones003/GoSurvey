#pragma once

#include <string>
#include <unordered_map>
#include <vector>

/// Shipped user manual (GitHub wiki mirror under `resources/wiki/`).

struct WikiNavEntry {
  std::string label;  ///< sidebar text (section title or page title)
  std::string slug;   ///< empty for section headers
  bool        isSection = false;
};

struct WikiPage {
  std::string slug;
  std::string title;
  std::string markdown;  ///< preprocessed for in-app rendering
};

struct WikiBundle {
  bool        ok = false;
  std::string rootPath;          ///< resolved `resources/wiki` directory
  std::string fallbackMessage; ///< when !ok
  std::vector<WikiNavEntry>     nav;
  std::unordered_map<std::string, WikiPage> pagesBySlug;
};

/// Loads every `*.md` page (except `_Sidebar.md` / `_Footer.md`) and builds sidebar navigation.
/// Never throws.
[[nodiscard]] WikiBundle LoadWikiBundle();

/// Converts a wiki page title ("Getting Started") or slug ("Getting-Started") to a slug key.
[[nodiscard]] std::string WikiTitleToSlug(std::string_view titleOrSlug);
