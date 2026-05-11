#pragma once

#include "CadCommands.hpp"
#include "CadSnap.hpp"

#include <imgui.h>

#include <string>
#include <vector>

void ApplyCadDarkTheme();

/// One-time layout: properties (left), toggles (right), command line (bottom), drawing (center).
void SetupMainDockLayout(ImGuiID dockspace_id);

void DrawMainMenuBar(AppCommandState& cmd, std::vector<std::string>& log);
/// Ribbon strip under the menu bar (tabs + tool groups).
void DrawRibbonBar(float height, AppCommandState& cmd, std::vector<std::string>& log);

void DrawPropertiesPanel(AppCommandState& cmd);
/// \param object_snap_enabled UI checkbox state for geometric object snaps (endpoint, mid, center, perp).
/// \param grid_visible when non-null, ties the Grid checkbox to persistent state (viewport minor grid).
void DrawHotTogglesPanel(bool* object_snap_enabled, bool* ortho_mode_enabled, AppCommandState* cmd_state,
                         bool* grid_visible);

void DrawCommandLinePanel(std::vector<std::string>& log, char* cmdBuf, int cmdBufSize, AppCommandState& cmd,
                          float cursorX, float cursorY, float cursorZ);

/// Central CAD viewport: renders OpenGL texture and handles pan / zoom / LINE picks.
/// Writes framebuffer pixel size and cursor world position. When object snap finds a hit, cursor and
/// \p out_snap reflect the snapped point; otherwise raw hover coordinates.
void DrawDrawingViewport(unsigned int viewportTextureId, AppCommandState& cmd, std::vector<std::string>& log,
                         float* panX, float* panY, float* zoom, float* outCursorX, float* outCursorY,
                         float* outCursorRawX, float* outCursorRawY, int* outFbW, int* outFbH,
                         bool object_snap_enabled, CadSnap::Hit* out_snap);

void DrawCreatePointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawViewPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawImportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawExportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log);

void DrawSurveyReportsPanel(AppCommandState& cmd);

/// Modal after COPY when survey points were selected — duplicate ID policy for new survey rows.
void DrawCopySurveyDuplicateModal(AppCommandState& cmd, std::vector<std::string>& log);
