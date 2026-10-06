#pragma once

// REQ-387 / ADR-067 — the built-in PDF viewer window (ImGui). Every route that opens a PDF calls
// OpenPdfInViewer; main calls DrawPdfViewers once a frame.

#include <string>
#include <vector>

struct AppCommandState;

/// Open `utf8Path` in a viewer window (focusing the one already showing it). Never blocks: the file
/// opens on a worker and the window shows "Opening..." until the first page is ready.
void OpenPdfInViewer(const std::string& utf8Path);

/// Draw every open viewer, upload finished page images, and keep the render worker fed.
void DrawPdfViewers(AppCommandState& cmd, std::vector<std::string>& log);

/// Free every viewer's GPU textures and stop its worker. Call before the GL context is destroyed.
void ShutdownPdfViewers();
