#pragma once

#include <string>

struct ImFont;

// Dynamic font registry (REQ/ADR-012): resolves a DXF font name — a TrueType family ("Arial",
// "Times New Roman", "arial.ttf") or an SHX stroke font ("romans.shx", "txt") — to a Windows
// TrueType file loaded on demand into the ImGui atlas. SHX fonts have no TrueType equivalent, so
// they are substituted with the closest installed TTF (as AutoCAD does when an SHX is missing).
namespace FontReg {

/// Lazily load + cache the font for \p fontNameOrShx at the requested style. Returns the app default
/// font when the file can't be resolved. \p outRealBold / \p outRealItalic report whether the returned
/// font actually carries that style, so callers can apply faux bold/italic for the missing variants.
ImFont* Resolve(const std::string& fontNameOrShx, bool bold, bool italic, bool* outRealBold = nullptr,
                bool* outRealItalic = nullptr);

/// Register the application's default/fallback font (used when a name can't be resolved).
void SetDefault(ImFont* f);

/// Toolspace tree face (Segoe UI, slightly larger). Falls back to the default UI font.
void SetToolspace(ImFont* f);
ImFont* Toolspace();

/// What's New / billboard body (Segoe UI Semilight). Falls back to Toolspace then default.
void SetBillboard(ImFont* f);
ImFont* Billboard();

/// In-app wiki reader body (IBM Plex Sans, bundled). Falls back to Billboard then default.
void SetWiki(ImFont* f);
ImFont* Wiki();

/// Wiki headings (IBM Plex Sans Condensed). Falls back to Wiki body font.
void SetWikiHeading(ImFont* f);
ImFont* WikiHeading();

/// Wiki code blocks (IBM Plex Mono). Falls back to Wiki body font.
void SetWikiMono(ImFont* f);
ImFont* WikiMono();

}  // namespace FontReg
