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
void SetupMainDockLayout(ImGuiID dockspace_id, const ImVec2& dock_host_size, bool reserveCommandDock = true);

void DrawMainMenuBar(AppCommandState& cmd, std::vector<std::string>& log);
/// Ribbon under the menu bar: sectioned icon toolbars (Draw, Modify, View, …) plus a fixed-width layer strip; hover for tooltips.
void DrawRibbonBar(float height, AppCommandState& cmd, std::vector<std::string>& log);
/// Drop shadow + lit top edge on every floating window and popup, so dialogs lift
/// off the shell (REQ-081). Call once per frame AFTER all windows are submitted
/// and BEFORE ImGui::Render(); it appends to each window's own draw list, which
/// is what keeps each shadow at its own window's depth. No-op in a theme that
/// sets no window shadow.
void DrawFloatingWindowChrome();

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

/// QUICKSELECT (QS) filter window — builds a selection by entity type and property criteria.
void DrawQuickSelectWindow(AppCommandState& cmd, std::vector<std::string>& log);

void DrawViewPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawSettingsPanel(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// Drawing Units dialog (UNITS command). REQ-020. Owns displayLinearPrecision.
void DrawUnitsDialog(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

void DrawImportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawExportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawSurveyReportsPanel(AppCommandState& cmd);

void DrawLayerManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// Text style manager (STYLE, REQ-044): create / rename / delete / edit named text styles. Editing a
/// style re-bakes its referencing, non-overridden text (live reference); "Standard" cannot be deleted.
void DrawTextStyleManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);
/// Point Group manager (REQ-067): create/rename/delete groups and edit their rules, with the
/// resolved member count shown live so an empty or non-matching rule is visible immediately.
void DrawPointGroupManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);
/// Surfaces panel (REQ-068): create a TIN surface from point groups, rebuild, rename, delete.
void DrawSurfaceManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// Modal after COPY when survey points were selected — duplicate ID policy for new survey rows.
void DrawCopySurveyDuplicateModal(AppCommandState& cmd, std::vector<std::string>& log);

/// Modal after a DXF import whose embedded survey points have IDs that collide with existing points —
/// lets the user overwrite the existing rows or offset the imported IDs.
void DrawDxfPointConflictModal(AppCommandState& cmd, std::vector<std::string>& log);

// Paper-space layout dialogs (right-click a layout tab).
void DrawViewportsWindow(AppCommandState& cmd, std::vector<std::string>& log);
void DrawMoveCopyLayoutDialog(AppCommandState& cmd, std::vector<std::string>& log);
void DrawPageSetupManager(AppCommandState& cmd, std::vector<std::string>& log);
void DrawNewPageSetupDialog(AppCommandState& cmd, std::vector<std::string>& log);
void DrawPageSetupEditor(AppCommandState& cmd, std::vector<std::string>& log);
/// Batch-plot dialog: tick layouts → write a multi-page PDF (REQ-030).
void DrawBatchPlotDialog(AppCommandState& cmd, std::vector<std::string>& log);
/// Plot the active layout to a PDF (file dialog). REQ-029.
void PlotActiveLayout(AppCommandState& cmd, std::vector<std::string>& log);

/// PDFATTACH configuration dialog + pick-phase hint overlay.
bool DrawPdfAttachDialog(AppCommandState& cmd, std::vector<std::string>& log);

/// ALIGN results window: editable pair list, live Helmert solution, Apply button, report generation.
void DrawAlignResultsWindow(AppCommandState& cmd, std::vector<std::string>& log);

/// Modal shown when the user tries to close the application with unsaved drawings.
/// Sets cmd.closeConfirmed = true when the user accepts close (with or without saving).
void DrawCloseConfirmModal(AppCommandState& cmd, std::vector<std::string>& log);

namespace update { struct UpdateState; }
/// REQ-078: presents an available update and waits for an explicit choice. Draws nothing while
/// the background check is running — the check itself is never shown to the user.
void DrawUpdateDialog(AppCommandState& cmd, update::UpdateState& upd);

/// Confirms a DWG export before anything is written, stating what the Phase 1 converter route
/// drops (REQ-052). Writes to \c cmd.dwgPendingExportPath only when the user accepts.
void DrawDwgLossyExportModal(AppCommandState& cmd, std::vector<std::string>& log);

void DrawTraverseEditorPanel(AppCommandState& cmd, std::vector<std::string>& log);
