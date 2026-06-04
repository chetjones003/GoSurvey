#pragma once

#include "CadCommands.hpp"
#include "CadSnap.hpp"

#include <imgui.h>

#include <string>
#include <vector>

void ApplyCadDarkTheme();
void ApplyCadLightTheme();

/// Optional app logo texture (from \p LoadAppLogoFromPngFile via \ref ResolveAppLogoPngPath). On Windows it appears in the custom title bar;
/// on other platforms, at the left of the main menu bar.
void CadUiSetMenuBarLogo(ImTextureID texture, float widthPx, float heightPx);
void CadUiClearMenuBarLogo();
/// Returns true when a logo was set with \ref CadUiSetMenuBarLogo and fills \p outTexture / \p outDimsPx.
bool CadUiTitleBarLogoQuery(ImTextureID* outTexture, ImVec2* outDimsPx);

/// One-time layout: properties (left), reports (right), command line (bottom), drawing (center). Status toggles
/// (OSNAP, ORTHO, …) are a separate fixed strip at the bottom of the main work area (see \ref DrawCadStatusBarStrip).
void SetupMainDockLayout(ImGuiID dockspace_id, const ImVec2& dock_host_size);

void DrawMainMenuBar(AppCommandState& cmd, std::vector<std::string>& log);
/// Ribbon under the menu bar: sectioned icon toolbars (Draw, Modify, View, …) plus a fixed-width layer strip; hover for tooltips.
void DrawRibbonBar(float height, AppCommandState& cmd, std::vector<std::string>& log);

void DrawPropertiesPanel(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// Command log, input, and hints. Default plotted text height is under Properties → General.
void DrawCommandLinePanel(std::vector<std::string>& log, char* cmdBuf, int cmdBufSize, AppCommandState& cmd);

/// Fixed-height strip: OSNAP, ORTHO, GRID, POLAR, plot scale, cursor readout. Laid out by \ref main.cpp across the
/// bottom of the main viewport (not docked, not movable).
float CadStatusBarStripHeightPx();
void DrawCadStatusBarStrip(AppCommandState& cmd, double cursorX, double cursorY, float cursorZ,
                           bool* ortho_mode_enabled, bool* grid_visible);

/// Central CAD viewport: renders OpenGL texture and handles pan / zoom / LINE picks.
/// Writes framebuffer pixel size and cursor world position. When object snap finds a hit, cursor and
/// \p out_snap reflect the snapped point; otherwise raw hover coordinates.
void DrawDrawingViewport(unsigned int viewportTextureId, AppCommandState& cmd, std::vector<std::string>& log,
                         char* cmdBuf, int cmdBufSize, double* panX, double* panY, float* zoom, double* outCursorX,
                         double* outCursorY, double* outCursorRawX, double* outCursorRawY, int* outFbW, int* outFbH,
                         CadSnap::Hit* out_snap);

void DrawCreatePointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

/// Floating panel listing all currently selected entities; each entry has a checkbox to deselect it.
void DrawSelectionCyclingPanel(AppCommandState& cmd);

void DrawViewPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawSettingsPanel(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

void DrawImportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawExportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawSurveyReportsPanel(AppCommandState& cmd);

void DrawLayerManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// Modal after COPY when survey points were selected — duplicate ID policy for new survey rows.
void DrawCopySurveyDuplicateModal(AppCommandState& cmd, std::vector<std::string>& log);

/// PDFATTACH configuration dialog + pick-phase hint overlay.
bool DrawPdfAttachDialog(AppCommandState& cmd, std::vector<std::string>& log);

/// ALIGN results window: editable pair list, live Helmert solution, Apply button, report generation.
void DrawAlignResultsWindow(AppCommandState& cmd, std::vector<std::string>& log);

/// Modal shown when the user tries to close the application with unsaved drawings.
/// Sets cmd.closeConfirmed = true when the user accepts close (with or without saving).
void DrawCloseConfirmModal(AppCommandState& cmd, std::vector<std::string>& log);
