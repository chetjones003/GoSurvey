// AutoCAD-style Options dialog — all DrawSettings* functions.
// Static helpers and tab drawers are file-local; only DrawSettingsPanel is public.

#include "CadUi.hpp"
#include "CadUiHelpers.hpp"
#include "SurveyPoints.hpp"
#include "UserPrefs.hpp"
#include "WinFileDialogs.hpp"

#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

static void BoxBegin(const char* label, float height = 0.f) {
  ImGui::SeparatorText(label);
  if (height > 0.f)
    ImGui::BeginChild((std::string("##box_") + label).c_str(), ImVec2(0, height), true, ImGuiWindowFlags_NoScrollbar);
  else
    ImGui::BeginChild((std::string("##box_") + label).c_str(), ImVec2(0, 0), true);
}

static void BoxEnd() { ImGui::EndChild(); }

static void DrawSettingsHeader(const AppCommandState& cmd) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.f));
  ImGui::Text("Current profile:   <<GoSurvey>>");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
  const char* drawingLabel = cmd.activeUiLayoutNameUtf8[0] ? cmd.activeUiLayoutNameUtf8 : "Untitled";
  ImGui::Text("Current drawing:   %s", drawingLabel);
  ImGui::PopStyleColor();
  ImGui::Separator();
}

static void DrawDisplayWindowElements(AppCommandState& cmd) {
  const char* themes[] = {"Dark", "Light"};
  ImGui::SetNextItemWidth(150.f);
  if (ImGui::Combo("Color theme:", &cmd.displayColorThemeIdx, themes, IM_ARRAYSIZE(themes))) {
    cmd.displayColorThemeIdx = std::clamp(cmd.displayColorThemeIdx, 0, 1);
    if (cmd.displayColorThemeIdx == 0)
      ApplyCadDarkTheme();
    else
      ApplyCadLightTheme();
  }
  ImGui::Spacing();
  ImGui::Checkbox("Display scroll bars in drawing window", &cmd.displayScrollbars);
  ImGui::Checkbox("Use large buttons for Toolbars", &cmd.displayLargeToolbarButtons);
  ImGui::Checkbox("Resize ribbon icons to standard sizes", &cmd.displayResizeRibbonIcons);
  ImGui::Checkbox("Show ToolTips", &cmd.displayShowTooltips);
  if (cmd.displayShowTooltips) {
    ImGui::Indent();
    ImGui::SetNextItemWidth(80.f);
    ImGui::DragFloat("Number of seconds before display##tooltipDelay", &cmd.displayTooltipDelaySec, 0.05f, 0.f, 5.f, "%.3f");
    ImGui::Checkbox("Show shortcut keys in ToolTips", &cmd.displayShowShortcutKeysInTooltips);
    ImGui::Checkbox("Show extended ToolTips", &cmd.displayShowExtendedTooltips);
    if (cmd.displayShowExtendedTooltips) {
      ImGui::SetNextItemWidth(80.f);
      ImGui::DragFloat("Number of seconds to delay##extDelay", &cmd.displayExtendedTooltipDelaySec, 0.05f, 0.f, 5.f, "%.3f");
    }
    ImGui::Unindent();
  }
  ImGui::Checkbox("Show rollover ToolTips", &cmd.displayShowRolloverTooltips);
  ImGui::Checkbox("Display File Tabs", &cmd.displayShowFileTabs);
}

static void DrawDisplayLayoutElements(AppCommandState& cmd) {
  ImGui::Checkbox("Display Layout and Model tabs", &cmd.displayLayoutAndModelTabs);
  ImGui::Checkbox("Display printable area", &cmd.displayPrintableArea);
  ImGui::Checkbox("Display paper background", &cmd.displayPaperBackground);
  if (cmd.displayPaperBackground) {
    ImGui::Indent();
    ImGui::Checkbox("Display paper shadow", &cmd.displayPaperShadow);
    ImGui::Unindent();
  }
  ImGui::Checkbox("Show Page Setup Manager for new layouts", &cmd.displayPageSetupOnNewLayouts);
  ImGui::Checkbox("Create viewport in new layouts", &cmd.displayCreateViewportInNewLayouts);
}

static void DrawDisplayResolution(AppCommandState& cmd) {
  ImGui::SetNextItemWidth(80.f);
  if (ImGui::DragInt("Arc and circle smoothness", &cmd.displayArcCircleSmoothness, 5.f, 8, 20000)) {
    cmd.displayArcCircleSmoothness = std::clamp(cmd.displayArcCircleSmoothness, 8, 20000);
    BumpCadGpuCache(cmd);
  }
  ItemHelpTooltip("AutoCAD VIEWRES analog. Caps the chord count when tessellating circles/arcs at the current zoom.\nHigher = smoother curves (more GPU work). 1000 matches AutoCAD's default.");
  ImGui::SetNextItemWidth(80.f);
  ImGui::DragInt("Segments in a polyline curve", &cmd.displayPolylineCurveSegments, 0.25f, 4, 32);
  ItemHelpTooltip("Hint for spline-fit polylines (not currently consumed; reserved).");
  ImGui::SetNextItemWidth(80.f);
  ImGui::DragFloat("Rendered object smoothness", &cmd.displayRenderedObjectSmoothness, 0.01f, 0.01f, 10.f, "%.2f");
  ItemHelpTooltip("Reserved for 3D pipeline.");
  ImGui::SetNextItemWidth(80.f);
  ImGui::DragInt("Contour lines per surface", &cmd.displayContourLinesPerSurface, 0.25f, 0, 32);
  ItemHelpTooltip("Reserved for 3D pipeline.");
}

static void DrawDisplayPerformance(AppCommandState& cmd) {
  ImGui::Checkbox("Pan and zoom with raster && OLE", &cmd.displayPanZoomWithRaster);
  ImGui::Checkbox("Highlight raster image frame only", &cmd.displayHighlightRasterFrameOnly);
  ImGui::Checkbox("Apply solid fill", &cmd.displayApplySolidFill);
  ImGui::Checkbox("Show text boundary frame only", &cmd.displayShowTextBoundaryFrameOnly);
  ImGui::Checkbox("Draw true silhouettes for solids and surfaces", &cmd.displayDrawTrueSilhouettes);
  ImGui::TextDisabled("(GoSurvey is 2D-only; raster/OLE/3D options are placeholders.)");
}

static void DrawDisplayCrosshair(AppCommandState& cmd) {
  ImGui::SetNextItemWidth(60.f);
  if (ImGui::DragInt("Crosshair size##xhairSize", &cmd.displayCrosshairSizePct, 1.f, 1, 100, "%d")) {
    cmd.displayCrosshairSizePct = std::clamp(cmd.displayCrosshairSizePct, 1, 100);
    const float f = static_cast<float>(cmd.displayCrosshairSizePct) * 0.01f;
    cmd.viewportCrosshairArmFracX = std::clamp(f * 0.6f, 0.002f, 0.5f);
    cmd.viewportCrosshairArmFracY = std::clamp(f, 0.002f, 0.5f);
  }
  ItemHelpTooltip("Percent of the viewport. 100 makes the crosshair span the full window (AutoCAD CURSORSIZE).");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1.f);
  int slider = cmd.displayCrosshairSizePct;
  if (ImGui::SliderInt("##xhairSlider", &slider, 1, 100, "")) {
    cmd.displayCrosshairSizePct = std::clamp(slider, 1, 100);
    const float f = static_cast<float>(cmd.displayCrosshairSizePct) * 0.01f;
    cmd.viewportCrosshairArmFracX = std::clamp(f * 0.6f, 0.002f, 0.5f);
    cmd.viewportCrosshairArmFracY = std::clamp(f, 0.002f, 0.5f);
  }
  ImGui::Spacing();
  if (ImGui::TreeNode("Crosshair details##xhairDetail")) {
    float xc[3] = {cmd.viewportCrosshairR, cmd.viewportCrosshairG, cmd.viewportCrosshairB};
    if (ImGui::ColorEdit3("Color##xhair", xc)) {
      cmd.viewportCrosshairR = xc[0]; cmd.viewportCrosshairG = xc[1]; cmd.viewportCrosshairB = xc[2];
    }
    ImGui::DragFloat("Line thickness (px)##xhairThick", &cmd.viewportCrosshairHairPx, 0.05f, 0.75f, 4.f, "%.2f");
    ImGui::TreePop();
  }
}

static void DrawDisplayZoomFactor(AppCommandState& cmd) {
  ImGui::SetNextItemWidth(80.f);
  if (ImGui::DragFloat("##zoomFactorNum", &cmd.displayWheelZoomFactor, 0.01f, 1.01f, 3.0f, "%.2f"))
    cmd.displayWheelZoomFactor = std::clamp(cmd.displayWheelZoomFactor, 1.01f, 3.0f);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1.f);
  if (ImGui::SliderFloat("Wheel zoom factor##zoomFactorSlider", &cmd.displayWheelZoomFactor, 1.01f, 3.0f, "%.2fx"))
    cmd.displayWheelZoomFactor = std::clamp(cmd.displayWheelZoomFactor, 1.01f, 3.0f);
  ItemHelpTooltip("AutoCAD ZOOMFACTOR analog. Multiplier applied per mouse-wheel notch.\n1.10 = 10% per notch (slow, precise); 2.00 = 2x per notch (fast).");
  ImGui::TextDisabled("Current zoom: %.4g x", static_cast<double>(cmd.viewportZoom));
  ImGui::TextDisabled("Pan: (%.3f, %.3f)", cmd.viewportPanX, cmd.viewportPanY);
}

static void DrawDisplayFadeControl(AppCommandState& cmd) {
  ImGui::SetNextItemWidth(60.f); ImGui::DragInt("##xrefFadeNum", &cmd.displayFadeXref, 0.5f, 0, 90, "%d");
  ImGui::SameLine(); ImGui::SetNextItemWidth(-1.f);
  ImGui::SliderInt("Xref display##xrefFadeSlider", &cmd.displayFadeXref, 0, 90, "");
  cmd.displayFadeXref = std::clamp(cmd.displayFadeXref, 0, 90);
  ImGui::SetNextItemWidth(60.f); ImGui::DragInt("##inPlaceFadeNum", &cmd.displayFadeInPlace, 0.5f, 0, 90, "%d");
  ImGui::SameLine(); ImGui::SetNextItemWidth(-1.f);
  ImGui::SliderInt("In-place edit and annotative representations##inPlaceFadeSlider", &cmd.displayFadeInPlace, 0, 90, "");
  cmd.displayFadeInPlace = std::clamp(cmd.displayFadeInPlace, 0, 90);
  ImGui::TextDisabled("(Reserved: fade is a placeholder; no fade pass is applied yet.)");
}

static void DrawSettingsDisplayTab(AppCommandState& cmd) {
  if (ImGui::BeginTable("##disp_layout", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    BoxBegin("Window Elements", 260.f); DrawDisplayWindowElements(cmd); BoxEnd();
    BoxBegin("Layout elements", 170.f); DrawDisplayLayoutElements(cmd); BoxEnd();
    ImGui::TableSetColumnIndex(1);
    BoxBegin("Display resolution", 150.f); DrawDisplayResolution(cmd); BoxEnd();
    BoxBegin("Display performance", 150.f); DrawDisplayPerformance(cmd); BoxEnd();
    BoxBegin("Crosshair size", 130.f); DrawDisplayCrosshair(cmd); BoxEnd();
    BoxBegin("Zoom", 130.f); DrawDisplayZoomFactor(cmd); BoxEnd();
    BoxBegin("Fade control", 130.f); DrawDisplayFadeControl(cmd); BoxEnd();
    ImGui::EndTable();
  }
}

static void DrawSettingsFilesTab(AppCommandState& cmd, std::vector<std::string>* log) {
  ImGui::TextWrapped(
      "Search paths, file locations, and startup template. GoSurvey loads a workspace .gs at startup; an empty "
      "Custom path uses the bundled resources/default-template.gs next to the executable. Preferences are saved "
      "in gosurvey-user.json beside the executable.");
  ImGui::Separator();
  BoxBegin("Startup template (.gs)", 140.f);
  ImGui::InputText("Custom .gs path (UTF-8)##startup_gs", cmd.defaultWorkspaceTemplatePathUtf8,
                   IM_ARRAYSIZE(cmd.defaultWorkspaceTemplatePathUtf8));
  ImGui::SameLine();
#if defined(_WIN32)
  if (ImGui::Button("Browse##startup_gs")) {
    if (BrowseOpenFileGsUtf8(cmd.defaultWorkspaceTemplatePathUtf8, sizeof(cmd.defaultWorkspaceTemplatePathUtf8)) && log)
      log->push_back("Startup template path set from file dialog.");
  }
#else
  ImGui::BeginDisabled(); ImGui::Button("Browse##startup_gs"); ImGui::EndDisabled();
  ItemHelpTooltip("File browse for startup template is only implemented on Windows in this build.");
#endif
  const std::filesystem::path bundled = ResolveDefaultWorkspaceTemplateGsPath();
  if (!bundled.empty())
    ImGui::TextDisabled("Bundled template resolved to: %s", bundled.u8string().c_str());
  else
    ImGui::TextDisabled("Bundled template not found (expected resources/default-template.gs beside exe or cwd).");
  if (ImGui::Button("Save startup preferences##startup_save")) {
    SaveUserStartupPrefs(cmd);
    if (log) log->push_back("Saved startup preferences (gosurvey-user.json).");
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear path (use bundled)##startup_clear")) {
    cmd.defaultWorkspaceTemplatePathUtf8[0] = '\0';
    SaveUserStartupPrefs(cmd);
    if (log) log->push_back("Cleared custom startup path; bundled template will be used on next launch.");
  }
  BoxEnd();
  BoxBegin("Support file search path", 90.f);
  ImGui::TextDisabled("(Reserved.) GoSurvey resolves resources/ relative to the executable.");
  BoxEnd();
}

static void DrawGraphicsPerformanceDialog(AppCommandState& cmd, std::vector<std::string>* log) {
  if (!cmd.showGraphicsPerformanceDialog) return;
  ImGui::SetNextWindowSize(ImVec2(560, 640), ImGuiCond_FirstUseEver);
  bool open = cmd.showGraphicsPerformanceDialog;
  if (!ImGui::Begin("Graphics Performance", &open, ImGuiWindowFlags_NoCollapse)) {
    cmd.showGraphicsPerformanceDialog = open; ImGui::End(); return;
  }
  cmd.showGraphicsPerformanceDialog = open;
  ImGui::TextDisabled("Video Card:       OpenGL (driver-reported)");
  ImGui::TextDisabled("Driver Version:   reported by GLFW / driver");
  ImGui::TextDisabled("Virtual Device:   OpenGL %d.x", 3);
  ImGui::Separator();
  ImGui::TextUnformatted("Hardware Acceleration");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.f);
  if (ImGui::Checkbox("##hwaccel", &cmd.systemHardwareAcceleration) && log)
    log->push_back(std::string("Hardware acceleration: ") + (cmd.systemHardwareAcceleration ? "ON" : "OFF") + " (MSAA + line smoothing).");
  ImGui::TextWrapped("Disable hardware acceleration only if you are experiencing graphics issues or have an incompatible video card.");
  ImGui::Separator();
  ImGui::TextUnformatted("2D Display Settings");
  ImGui::Spacing();
  if (ImGui::Checkbox("Smooth line display", &cmd.gfxSmoothLineDisplay) && log)
    log->push_back(std::string("Smooth line display: ") + (cmd.gfxSmoothLineDisplay ? "ON" : "OFF") + ".");
  ImGui::TextDisabled("Removes the jagged effect on the display of diagonal lines and curved edges in 2D wireframe.");
  ImGui::Checkbox("Accelerated font display", &cmd.gfxAcceleratedFontDisplay);
  ImGui::TextDisabled("Improves the display of TrueType fonts using GPU acceleration.");
  ImGui::Spacing();
  ImGui::TextUnformatted("Video Memory Caching Level");
  ImGui::SameLine(); ImGui::SetNextItemWidth(-1.f);
  ImGui::SliderInt("##vmcache", &cmd.gfxVideoMemoryCachingLevel, 1, 5, "%d");
  ImGui::TextDisabled("Higher = more video memory used for graphics cache.");
  ImGui::Separator();
  ImGui::TextUnformatted("3D Display Settings");
  ImGui::Spacing();
  ImGui::Checkbox("Fast shaded mode", &cmd.gfx3dFastShadedMode);
  ImGui::Checkbox("Advanced material effects", &cmd.gfx3dAdvancedMaterialEffects);
  ImGui::Checkbox("Full shadow display", &cmd.gfx3dFullShadowDisplay);
  ImGui::Checkbox("Per-pixel lighting (Phong)", &cmd.gfx3dPerPixelLighting);
  ImGui::TextDisabled("(GoSurvey is 2D-only; 3D options are placeholders for future surface viewing.)");
  ImGui::Separator();
  if (ImGui::Button("Restore Defaults##gpx")) {
    cmd.systemHardwareAcceleration = true; cmd.gfxSmoothLineDisplay = true;
    cmd.gfxAcceleratedFontDisplay = true; cmd.gfxVideoMemoryCachingLevel = 5;
    cmd.gfx3dFastShadedMode = true; cmd.gfx3dAdvancedMaterialEffects = true;
    cmd.gfx3dFullShadowDisplay = true; cmd.gfx3dPerPixelLighting = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("OK##gpx")) cmd.showGraphicsPerformanceDialog = false;
  ImGui::SameLine();
  if (ImGui::Button("Cancel##gpx")) cmd.showGraphicsPerformanceDialog = false;
  ImGui::End();
}

static void DrawSystemHardwareAccel(AppCommandState& cmd) {
  if (ImGui::Button("Graphics Performance", ImVec2(-FLT_MIN, 0.f))) cmd.showGraphicsPerformanceDialog = true;
  ImGui::TextDisabled("Current: HW accel %s, smooth lines %s.",
                      cmd.systemHardwareAcceleration ? "ON" : "OFF", cmd.gfxSmoothLineDisplay ? "ON" : "OFF");
  ImGui::Checkbox("Automatically check for certification update", &cmd.systemAutoCheckCertificationUpdate);
}

static void DrawSystemLayoutRegen(AppCommandState& cmd) {
  ImGui::RadioButton("Regen when switching layouts", &cmd.systemLayoutRegenOption, 0);
  ImGui::RadioButton("Cache model tab and last layout", &cmd.systemLayoutRegenOption, 1);
  ImGui::RadioButton("Cache model tab and all layouts", &cmd.systemLayoutRegenOption, 2);
}

static void DrawSystemGeneralOptions(AppCommandState& cmd) {
  ImGui::BeginDisabled(); ImGui::Button("Hidden Messages Settings", ImVec2(-FLT_MIN, 0.f)); ImGui::EndDisabled();
  ImGui::Checkbox("Display OLE Text Size Dialog", &cmd.systemDisplayOLETextSizeDialog);
  ImGui::Checkbox("Beep on error in user input", &cmd.systemBeepOnError);
  ImGui::Checkbox("Allow long symbol names", &cmd.systemAllowLongSymbolNames);
}

static void DrawSettingsSystemTab(AppCommandState& cmd) {
  if (ImGui::BeginTable("##sys_layout", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    BoxBegin("Hardware Acceleration", 110.f); DrawSystemHardwareAccel(cmd); BoxEnd();
    BoxBegin("Current Pointing Device", 110.f);
    const char* devices[] = {"Current System Pointing Device"};
    int idx = 0; ImGui::SetNextItemWidth(-1.f); ImGui::Combo("##ptdev", &idx, devices, IM_ARRAYSIZE(devices));
    ImGui::TextDisabled("Accept input from:");
    ImGui::BeginDisabled(); ImGui::RadioButton("Digitizer only", false); ImGui::RadioButton("Digitizer and mouse", true); ImGui::EndDisabled();
    BoxEnd();
    BoxBegin("Layout Regen Options", 120.f); DrawSystemLayoutRegen(cmd); BoxEnd();
    ImGui::TableSetColumnIndex(1);
    BoxBegin("General Options", 140.f); DrawSystemGeneralOptions(cmd); BoxEnd();
    BoxBegin("Help", 70.f); ImGui::Checkbox("Access online content when available", &cmd.systemAccessOnlineContent); BoxEnd();
    BoxBegin("InfoCenter", 70.f);
    ImGui::BeginDisabled(); ImGui::Button("Balloon Notifications", ImVec2(-FLT_MIN, 0.f)); ImGui::EndDisabled();
    BoxEnd();
    BoxBegin("Security", 70.f);
    ImGui::BeginDisabled(); ImGui::Button("Security Options", ImVec2(-FLT_MIN, 0.f)); ImGui::EndDisabled();
    BoxEnd();
    BoxBegin("dbConnect Options", 80.f);
    ImGui::Checkbox("Store Links index in drawing file", &cmd.systemStoreLinksIndexInDrawing);
    ImGui::Checkbox("Open tables in read-only mode", &cmd.systemOpenTablesReadOnly);
    BoxEnd();
    ImGui::EndTable();
  }
}

static void DrawSettingsDraftingTab(AppCommandState& cmd) {
  BoxBegin("Right-click behavior", 90.f);
  ImGui::Checkbox("Right-click in drawing repeats last command when no command is active",
                  &cmd.rightClickRepeatLastCommand);
  ItemHelpTooltip("When enabled, right-clicking in the drawing viewport with no active command immediately "
                  "re-invokes the last used command (LINE, CIRCLE, COPY, etc.).\n"
                  "When disabled, right-click opens the context menu instead.");
  BoxEnd();
  BoxBegin("Object snap (AutoSnap)", 0.f);
  ImGui::TextUnformatted("Cursor snaps to drawing geometry when OSNAP is on (status bar or F3).");
  ImGui::Separator();
  ImGui::Checkbox("Enable object snap", &cmd.objectSnapEnabled);
  if (ImGui::DragFloat("Aperture (screen px)", &cmd.objectSnapAperturePx, 0.25f, 4.f, 64.f, "%.1f"))
    cmd.objectSnapAperturePx = std::clamp(cmd.objectSnapAperturePx, 4.f, 64.f);
  ItemHelpTooltip("Screen pick radius: larger catches snaps from farther away; smaller is stricter.");
  if (ImGui::DragFloat("Snap indicator half-size (px)", &cmd.objectSnapGlyphHalfPx, 0.15f, 3.f, 48.f, "%.1f"))
    cmd.objectSnapGlyphHalfPx = std::clamp(cmd.objectSnapGlyphHalfPx, 3.f, 48.f);
  ItemHelpTooltip("Green snap symbols (endpoint square, midpoint triangle, etc.): half-width on screen.");
  ImGui::Separator();
  ImGui::TextDisabled("Snap types (also: right-click OSNAP on the command line)");
  ImGui::Checkbox("Endpoint", &cmd.objectSnapEndpoint);
  ImGui::Checkbox("Midpoint", &cmd.objectSnapMidpoint);
  ImGui::Checkbox("Center (circle / ellipse center)", &cmd.objectSnapCenter);
  ImGui::Checkbox("Perpendicular (when a reference point applies)", &cmd.objectSnapPerpendicular);
  ImGui::Checkbox("Survey point", &cmd.objectSnapSurveyPoint);
  ImGui::Checkbox("Geometric center (closed polyline)", &cmd.objectSnapGeometricCenter);
  ImGui::Separator();
  ImGui::TextWrapped(
      "With a command active (LINE, CIRCLE, …), Shift+right-click anywhere on the drawing: choose a snap type, "
      "then pick one from every matching snap in the model (list is sorted by distance from that click). "
      "That choice applies to the next left-click only.");
  BoxEnd();
}

static void DrawUserPrefsSurveyPoints(AppCommandState& cmd) {
  ImGui::DragFloat("Cross span (plotted inches)", &cmd.surveyPointCrossSpanPlottedInches, 0.002f, 0.02f, 2.f, "%.3f");
  ItemHelpTooltip("Horizontal span of the X on paper: world size = span x model units per plotted inch.");
  ImGui::Checkbox("Show point ID in viewport", &cmd.surveyPointShowIdInViewport);
  if (ImGui::DragFloat("Survey label text height (plotted inches)", &cmd.surveyPointLabelPlottedHeightInches, 0.001f, 0.04f, 0.5f, "%.3f")) {
    for (size_t i = 0; i < cmd.surveyPoints.size(); ++i)
      EnsureSurveyPointLabelMtext(cmd, i, nullptr);
  }
  const bool le = ImGui::DragFloat("Label center east of point (plotted in)", &cmd.surveyLabelOffsetEastPlottedIn, 0.002f, -2.f, 4.f, "%.3f");
  const bool ln = ImGui::DragFloat("Label center north of point (plotted in)", &cmd.surveyLabelOffsetNorthPlottedIn, 0.002f, -2.f, 4.f, "%.3f");
  if (le || ln) {
    for (size_t i = 0; i < cmd.surveyPoints.size(); ++i)
      RepositionSurveyLabelMtextForPoint(cmd, i);
    BumpCadGpuCache(cmd);
  }
}

static void DrawUserPrefsLabelTemplates(AppCommandState& cmd) {
  ImGui::TextWrapped("Label style templates (apply to all points). Placeholders: {id} {desc} {elev}. Press Enter in a field or click Apply to refresh existing labels.");
  ImGui::InputTextMultiline("Number + description##svy_tpl_nd", &cmd.surveyLabelTemplates.numberDesc, ImVec2(-FLT_MIN, 52.f));
  ImGui::InputTextMultiline("Number only##svy_tpl_no", &cmd.surveyLabelTemplates.numberOnly, ImVec2(-FLT_MIN, 40.f));
  ImGui::InputTextMultiline("Description only##svy_tpl_do", &cmd.surveyLabelTemplates.descOnly, ImVec2(-FLT_MIN, 40.f));
  ImGui::InputTextMultiline("Number + elevation##svy_tpl_ne", &cmd.surveyLabelTemplates.numberElev, ImVec2(-FLT_MIN, 52.f));
  ImGui::InputTextMultiline("Number + elevation + description##svy_tpl_ned", &cmd.surveyLabelTemplates.numberElevDesc, ImVec2(-FLT_MIN, 60.f));
  if (ImGui::Button("Apply label templates to all survey points")) {
    for (size_t i = 0; i < cmd.surveyPoints.size(); ++i)
      EnsureSurveyPointLabelMtext(cmd, i, nullptr);
    BumpCadGpuCache(cmd);
  }
}

static void DrawUserPrefsTextMtext(AppCommandState& cmd) {
  ImGui::DragFloat("TEXT min px", &cmd.viewportTextMinPx, 0.25f, 4.f, 48.f, "%.1f");
  ImGui::DragFloat("TEXT max px", &cmd.viewportTextMaxPx, 0.5f, 24.f, 320.f, "%.1f");
  if (cmd.viewportTextMaxPx < cmd.viewportTextMinPx) cmd.viewportTextMaxPx = cmd.viewportTextMinPx;
  ImGui::DragFloat("MTEXT min px", &cmd.viewportMtextMinPx, 0.25f, 4.f, 48.f, "%.1f");
  ImGui::DragFloat("MTEXT max px", &cmd.viewportMtextMaxPx, 0.5f, 24.f, 320.f, "%.1f");
  if (cmd.viewportMtextMaxPx < cmd.viewportMtextMinPx) cmd.viewportMtextMaxPx = cmd.viewportMtextMinPx;
}

static void DrawUserPrefsDimensions(AppCommandState& cmd) {
  ImGui::DragFloat("Extension line px", &cmd.viewportDimExtLinePx, 0.05f, 0.25f, 8.f, "%.2f");
  ImGui::DragFloat("Dimension line px", &cmd.viewportDimDimLinePx, 0.05f, 0.25f, 8.f, "%.2f");
  ImGui::DragFloat("Arrow size scale", &cmd.viewportDimArrowScale, 0.02f, 0.2f, 4.f, "%.2f");
  ItemHelpTooltip("Multiplies arrow length derived from dimension text height (paper x plot scale).");
  ImGui::DragFloat("Value text min px", &cmd.viewportDimTextMinPx, 0.25f, 4.f, 48.f, "%.1f");
  ImGui::DragFloat("Value text max px", &cmd.viewportDimTextMaxPx, 0.5f, 24.f, 320.f, "%.1f");
  if (cmd.viewportDimTextMaxPx < cmd.viewportDimTextMinPx) cmd.viewportDimTextMaxPx = cmd.viewportDimTextMinPx;
}

static void DrawSettingsUserPrefsTab(AppCommandState& cmd) {
  if (ImGui::CollapsingHeader("Survey points (markers + linked MTEXT)", ImGuiTreeNodeFlags_DefaultOpen))
    DrawUserPrefsSurveyPoints(cmd);
  if (ImGui::CollapsingHeader("Label templates"))
    DrawUserPrefsLabelTemplates(cmd);
  if (ImGui::CollapsingHeader("Text & MTEXT screen size"))
    DrawUserPrefsTextMtext(cmd);
  if (ImGui::CollapsingHeader("Dimensions"))
    DrawUserPrefsDimensions(cmd);
}

static void DrawSettingsPlaceholderTab(const char* title, const char* description) {
  ImGui::TextUnformatted(title); ImGui::Separator();
  ImGui::TextWrapped("%s", description); ImGui::Spacing();
  ImGui::BeginDisabled(); ImGui::TextDisabled("(No GoSurvey-specific controls in this section yet.)"); ImGui::EndDisabled();
}

void DrawSettingsPanel(AppCommandState& cmd, std::vector<std::string>* log) {
  if (!cmd.showSettingsWindow) return;

  ImGui::SetNextWindowSize(ImVec2(960, 720), ImGuiCond_FirstUseEver);
  bool open = cmd.showSettingsWindow;
  if (!ImGui::Begin("Options", &open, ImGuiWindowFlags_NoCollapse)) {
    cmd.showSettingsWindow = open; ImGui::End(); return;
  }
  cmd.showSettingsWindow = open;
  DrawSettingsHeader(cmd);

  const ImGuiTabBarFlags tabFlags = ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_NoCloseWithMiddleMouseButton;
  if (ImGui::BeginTabBar("##optionsTabs", tabFlags)) {
    if (ImGui::BeginTabItem("Files"))          { cmd.settingsActiveTabIdx = 0; DrawSettingsFilesTab(cmd, log);                                                       ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Display"))        { cmd.settingsActiveTabIdx = 1; DrawSettingsDisplayTab(cmd);                                                          ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Open and Save"))  { cmd.settingsActiveTabIdx = 2; DrawSettingsPlaceholderTab("Open and Save", "File-format and recovery options.");      ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Plot and Publish")){ cmd.settingsActiveTabIdx = 3; DrawSettingsPlaceholderTab("Plot and Publish", "Default plot settings.");              ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("System"))         { cmd.settingsActiveTabIdx = 4; DrawSettingsSystemTab(cmd);                                                           ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("User Preferences")){ cmd.settingsActiveTabIdx = 5; DrawSettingsUserPrefsTab(cmd);                                                        ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Drafting"))       { cmd.settingsActiveTabIdx = 6; DrawSettingsDraftingTab(cmd);                                                         ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("3D Modeling"))    { cmd.settingsActiveTabIdx = 7; DrawSettingsPlaceholderTab("3D Modeling", "GoSurvey is 2D; 3D options are reserved."); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Selection"))      { cmd.settingsActiveTabIdx = 8; DrawSettingsPlaceholderTab("Selection", "Pickbox / grip size options.");               ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Profiles"))       { cmd.settingsActiveTabIdx = 9; DrawSettingsPlaceholderTab("Profiles", "Saved option profiles. Current: <<GoSurvey>>.");ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("AEC Editor"))     { cmd.settingsActiveTabIdx = 10; DrawSettingsPlaceholderTab("AEC Editor", "Civil/AEC-specific editor preferences.");   ImGui::EndTabItem(); }
    ImGui::EndTabBar();
  }

  ImGui::Separator();
  if (ImGui::Button("OK", ImVec2(90.f, 0.f)))     { SaveUserStartupPrefs(cmd); if (log) log->push_back("Settings saved (gosurvey-user.json)."); cmd.showSettingsWindow = false; }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(90.f, 0.f))) cmd.showSettingsWindow = false;
  ImGui::SameLine();
  if (ImGui::Button("Apply", ImVec2(90.f, 0.f)))  { SaveUserStartupPrefs(cmd); if (log) log->push_back("Settings applied (gosurvey-user.json)."); }
  ImGui::SameLine();
  ImGui::BeginDisabled(); ImGui::Button("Help", ImVec2(90.f, 0.f)); ImGui::EndDisabled();
  ImGui::End();

  DrawGraphicsPerformanceDialog(cmd, log);
}
