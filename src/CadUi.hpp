#pragma once

#include "CadCommands.hpp"
#include "CadSnap.hpp"

#include <imgui.h>

#include <string>
#include <vector>

void ApplyCadDarkTheme();

/// Optional app logo texture (from \p LoadAppLogoFromPngFile via \ref ResolveAppLogoPngPath). On Windows it appears in the custom title bar;
/// on other platforms, at the left of the main menu bar.
void CadUiSetMenuBarLogo(ImTextureID texture, float widthPx, float heightPx);
void CadUiClearMenuBarLogo();
/// Returns true when a logo was set with \ref CadUiSetMenuBarLogo and fills \p outTexture / \p outDimsPx.
bool CadUiTitleBarLogoQuery(ImTextureID* outTexture, ImVec2* outDimsPx);

/// One-time layout: properties (left), reports (right), command line (bottom, includes mode toggles), drawing (center).
void SetupMainDockLayout(ImGuiID dockspace_id);

void DrawMainMenuBar(AppCommandState& cmd, std::vector<std::string>& log);
/// Ribbon under the menu bar: sectioned tool grids (Draw, Modify, View, …) plus a fixed-width layer strip.
void DrawRibbonBar(float height, AppCommandState& cmd, std::vector<std::string>& log);

void DrawPropertiesPanel(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// Command log, input, hints, and a single-line status bar (toggles, annotation-scale combo, cursor). Default
/// plotted text height is under Properties → General.
void DrawCommandLinePanel(std::vector<std::string>& log, char* cmdBuf, int cmdBufSize, AppCommandState& cmd,
                          float cursorX, float cursorY, float cursorZ, bool* ortho_mode_enabled, bool* grid_visible);

/// Central CAD viewport: renders OpenGL texture and handles pan / zoom / LINE picks.
/// Writes framebuffer pixel size and cursor world position. When object snap finds a hit, cursor and
/// \p out_snap reflect the snapped point; otherwise raw hover coordinates.
void DrawDrawingViewport(unsigned int viewportTextureId, AppCommandState& cmd, std::vector<std::string>& log,
                         char* cmdBuf, int cmdBufSize, float* panX, float* panY, float* zoom, float* outCursorX,
                         float* outCursorY, float* outCursorRawX, float* outCursorRawY, int* outFbW, int* outFbH,
                         CadSnap::Hit* out_snap);

void DrawCreatePointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawViewPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawSettingsPanel(AppCommandState& cmd);

void DrawImportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawExportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawSurveyReportsPanel(AppCommandState& cmd);

void DrawLayerManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// Modal after COPY when survey points were selected — duplicate ID policy for new survey rows.
void DrawCopySurveyDuplicateModal(AppCommandState& cmd, std::vector<std::string>& log);
