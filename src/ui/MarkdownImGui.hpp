#pragma once

#include <functional>
#include <string_view>

/// REQ-336 / ADR-056 — draw CommonMark into the current ImGui window via vendored md4c.
/// Links open with the system browser (ShellExecute on Windows) unless a hook overrides them.

struct ImFont;

struct MarkdownImGuiHooks {
  /// Return true when the href was handled (skip default browser open).
  std::function<bool(std::string_view href)> onLink;
  /// Resolve a bundled image path to an OpenGL texture id (0 when unavailable).
  std::function<unsigned int(std::string_view src, int* outW, int* outH)> resolveImage;
  /// Optional heading face (e.g. IBM Plex Sans Condensed for wiki pages).
  ImFont* headingFont = nullptr;
  /// Optional monospace face for fenced code blocks (e.g. IBM Plex Mono).
  ImFont* monoFont = nullptr;
  /// When set, scroll to the first ## heading that matches this command (lowercase primary).
  std::string_view scrollToCommandPrimary;
  bool* scrollApplied = nullptr;
};

/// Draws CommonMark (plus tables when present). \p hooks may be null.
void DrawMarkdownImGui(std::string_view markdown, const MarkdownImGuiHooks* hooks = nullptr);
