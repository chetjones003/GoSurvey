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

/// File ▸ Save, as a function rather than a menu-item body, so the menu and the Ctrl+S accelerator
/// in `main.cpp` cannot drift apart. Saves straight to the drawing's own path when it has one and
/// otherwise browses for a destination (adopting it, so the next save is silent).
void SaveActiveDocument(AppCommandState& cmd, std::vector<std::string>& log);

/// Append a fresh empty drawing after the Start tab and focus it (File ▸ New, tab-bar "+",
/// Start screen New). REQ-055 / REQ-308.
void NewDrawingInTab(AppCommandState& cmd, std::vector<std::string>& log);
/// Open \p dwgPathUtf8 (or browse when null) into a new focused tab; a path that fails to open is
/// dropped from the recent list. REQ-055 / REQ-308.
void OpenDrawingInNewTab(AppCommandState& cmd, std::vector<std::string>& log, const char* dwgPathUtf8);
/// REQ-308 — drop a drawing from the recent-drawings store (used when a recent tile fails to open).
void RemoveRecentDrawing(const std::string& absDrawingPath);
void ClearRecentDrawings();

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

/// REQ-142 Civil 3D-shaped drawing explorer (Prospector + Settings).
void DrawToolspaceWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

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

/// Frame-time diagnostic overlay (issue #166 investigation). No-op unless the PERFHUD command has
/// toggled it on. Call once per frame, after DrawDrawingViewport.
void DrawPerfHud(const AppCommandState& cmd);

/// REQ-308 — the Start screen shown for drawingTabs[0]: GoSurvey/version, Open/New, the project
/// website link, the Recent-drawings grid/list, and the sign-in / Welcome column. Drawn in place of
/// the GL viewport by DrawDrawingViewport when the Start tab is active.
void DrawStartScreen(AppCommandState& cmd, std::vector<std::string>& log);

/// REQ-308 — record \p absDrawingPath at the front of the recent-drawings store and queue a
/// thumbnail capture for the active tab. Call after a successful open or save.
void RecordRecentDrawing(AppCommandState& cmd, const std::string& absDrawingPath);

class ViewportRenderer;
/// REQ-308 — if a thumbnail capture is pending for \p renderer's tab, grab it now (called from the
/// main loop right after RenderScene) and attach it to the recent-drawings entry.
void ServicePendingThumbnail(AppCommandState& cmd, const ViewportRenderer& renderer);

void DrawCreatePointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

/// Floating panel listing all currently selected entities; each entry has a checkbox to deselect it.
void DrawSelectionCyclingPanel(AppCommandState& cmd);

/// QUICKSELECT (QS) filter window — builds a selection by entity type and property criteria.
void DrawQuickSelectWindow(AppCommandState& cmd, std::vector<std::string>& log);

void DrawViewPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawSettingsPanel(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// REQ-091 amendment (GitHub issue #182) — read-only "Account Details" placeholder window opened
/// from the menu-bar account dropdown. Shows the signed-in email and a "more coming soon" note.
void DrawAccountDetailsWindow(AppCommandState& cmd);

/// Drawing Units dialog (UNITS command). REQ-020. Owns displayLinearPrecision.
void DrawUnitsDialog(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// Right-Click Customization dialog (Options → User Preferences). REQ-084 (a). Sole owner of the
/// three context modes and the time-sensitive preference; Cancel reverts to the values it opened with.
void DrawRightClickCustomizationDialog(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

void DrawImportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawExportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawSurveyReportsPanel(AppCommandState& cmd);

void DrawLayerManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);
/// View Manager (REQ-106) — list, restore, update and delete named views, plus the New View prompt.
void DrawViewManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// Text style manager (STYLE, REQ-044): create / rename / delete / edit named text styles. Editing a
/// style re-bakes its referencing, non-overridden text (live reference); "Standard" cannot be deleted.
void DrawTextStyleManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);
/// Point Group manager (REQ-067): create/rename/delete groups and edit their rules, with the
/// resolved member count shown live so an empty or non-matching rule is visible immediately.
void DrawPointGroupManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);
/// Surface Style editor (SURFSTYLE, REQ-070 / ADR-036 (i)): the named table of how a surface is
/// DRAWN — contours, border, triangles, points. Editing a style changes every surface using it, and
/// touches no surface definition, so nothing here can re-triangulate anything.
void DrawSurfaceStyleWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);
void DrawDimStyleWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);
/// Surfaces panel (REQ-075): leftover definition explorer. Style/analysis is DrawSurfaceStyleWindow.
void DrawSurfaceManagerWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);
/// Surface Properties (Information / Definition / Analysis / Statistics) for one named surface.
void DrawSurfacePropertiesWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

void DrawVolumeDashboardWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);
void DrawQuickProfileWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

/// Feature line elevation editor (REQ-088). Every edit routes through the FLELEV command line, so
/// the panel and the REQ-203 driver exercise the same code.
void DrawFeatureLineElevationWindow(AppCommandState& cmd, std::vector<std::string>* log = nullptr);

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

/// INSERT configuration dialog + pick-phase hint overlay (GitHub issue #124).
void DrawInsertBlockDialog(AppCommandState& cmd, std::vector<std::string>& log);
/// Civil 3D-style Edit Block Definition picker (BEDIT with no name).
void DrawEditBlockDefinitionDialog(AppCommandState& cmd, std::vector<std::string>& log);

/// BEDIT Block Authoring Palettes (Parameters / Actions / Parameter Sets / Constraints).
void DrawBlockAuthoringPalettes(AppCommandState& cmd, std::vector<std::string>& log);

/// ALIGN results window: editable pair list, live Helmert solution, Apply button, report generation.
void DrawAlignResultsWindow(AppCommandState& cmd, std::vector<std::string>& log);

/// Modal shown when the user tries to close the application with unsaved drawings.
/// Sets cmd.closeConfirmed = true when the user accepts close (with or without saving).
void DrawCloseConfirmModal(AppCommandState& cmd, std::vector<std::string>& log);

namespace update { struct UpdateState; }
/// REQ-078: presents an available update and waits for an explicit choice. Draws nothing while
/// the background check is running — the check itself is never shown to the user.
void DrawUpdateDialog(AppCommandState& cmd, update::UpdateState& upd);

/// REQ-091 (amended): blocks the session every launch until \c cmd.authGateResolved — signed in,
/// or no internet connectivity at all (REQ-077's same offline exception). Draws nothing once
/// resolved; never re-opens later in the same session (e.g. after a manual sign-out).
void DrawSignInGate(AppCommandState& cmd);

/// Confirms a DWG export before anything is written, stating LibreDWG R2000 encode limits
/// (REQ-170). Writes to \c cmd.dwgPendingExportPath only when the user accepts.
void DrawDwgLossyExportModal(AppCommandState& cmd, std::vector<std::string>& log);

void DrawTraverseEditorPanel(AppCommandState& cmd, std::vector<std::string>& log);
