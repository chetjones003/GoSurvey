// AutoCAD-style Options dialog — all DrawSettings* functions.
// Static helpers and tab drawers are file-local; only DrawSettingsPanel is public.

#include "CadUi.hpp"
#include "CadUiHelpers.hpp"
#include "AppIcon.hpp"
#include "MtextRichFormat.hpp"
#include "NumFormat.hpp"
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
    ImGui::BeginChild((std::string("##box_") + label).c_str(), ImVec2(0, height), true);
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
  {
    float bg[3] = {cmd.viewportBgR, cmd.viewportBgG, cmd.viewportBgB};
    ImGui::SetNextItemWidth(150.f);
    if (ImGui::ColorEdit3("Viewport background", bg)) {
      cmd.viewportBgR = bg[0]; cmd.viewportBgG = bg[1]; cmd.viewportBgB = bg[2];
    }
    ItemHelpTooltip("Model-space background (clear) color for the drawing viewport.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##bgReset")) {
      cmd.viewportBgR = 0.1215686f; cmd.viewportBgG = 0.1215686f; cmd.viewportBgB = 0.1647059f;
    }
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
    if (SaveUserStartupPrefs(cmd)) {
      if (log) log->push_back("Saved startup preferences (gosurvey-user.json).");
    } else {
      if (log) log->push_back("Error: failed to write gosurvey-user.json (check directory permissions).");
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear path (use bundled)##startup_clear")) {
    cmd.defaultWorkspaceTemplatePathUtf8[0] = '\0';
    if (SaveUserStartupPrefs(cmd)) {
      if (log) log->push_back("Cleared custom startup path; bundled template will be used on next launch.");
    } else {
      if (log) log->push_back("Error: failed to write gosurvey-user.json (check directory permissions).");
    }
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
  ImGui::SetNextItemWidth(80.f);
  if (ImGui::DragInt("Coordinate display precision (decimals)", &cmd.surveyPointDisplayPrecision, 0.1f, 0, 12, "%d")) {
    cmd.surveyPointDisplayPrecision = std::clamp(cmd.surveyPointDisplayPrecision, 0, 12);
    for (size_t i = 0; i < cmd.surveyPoints.size(); ++i)
      EnsureSurveyPointLabelMtext(cmd, i, nullptr);
  }
  ItemHelpTooltip("Decimal places shown for survey-point northing/easting/elevation in labels and the survey points table. Display only; stored values keep full precision.");
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
  ImGui::Separator();
  ImGui::TextUnformatted("Leader arrow (shown when label is dragged away from its point)");
  if (ImGui::DragFloat("Arrow half-width (px)", &cmd.surveyLabelLeaderArrowPx, 0.1f, 2.f, 30.f, "%.1f"))
    cmd.surveyLabelLeaderArrowPx = std::clamp(cmd.surveyLabelLeaderArrowPx, 2.f, 30.f);
  ItemHelpTooltip("Controls the size of the filled arrowhead on survey label leader lines. Length is 2.36x the half-width.");
}

// State for the template editor popup (one at a time).
static std::string* gTplEditorTarget = nullptr;
static const char*  gTplEditorTitle  = nullptr;
static float        gTplEditorColor[3] = {1.f, 1.f, 1.f};
static std::string  gTplEditorInsert;  // text to inject at cursor next callback tick

static int TplEditorCallback(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways && !gTplEditorInsert.empty()) {
    data->InsertChars(data->CursorPos, gTplEditorInsert.c_str());
    gTplEditorInsert.clear();
  }
  return 0;
}

static void OpenTplEditorPopup(std::string* target, const char* title) {
  gTplEditorTarget = target;
  gTplEditorTitle  = title;
  ImGui::OpenPopup("##tpl_editor_modal");
}

static void DrawTplEditorPopup(AppCommandState& cmd) {
  ImGui::SetNextWindowSize(ImVec2(720.f, 600.f), ImGuiCond_Always);
  if (!ImGui::BeginPopupModal("##tpl_editor_modal", nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    return;

  if (gTplEditorTarget == nullptr) {
    ImGui::EndPopup();
    return;
  }

  ImGui::TextUnformatted(gTplEditorTitle ? gTplEditorTitle : "Edit Template");
  ImGui::SameLine();
  ImGui::TextDisabled("  Tags: [[b]] [[i]] [[u]] [[color:RRGGBB]] [[/color]]");
  ImGui::Separator();

  // Two-column layout: left = text editor (stretches), right = fixed-width toolbox (no child, no scroll).
  const float toolW = 200.f;
  const float gap   = ImGui::GetStyle().ItemSpacing.x;
  const float editorW = ImGui::GetContentRegionAvail().x - toolW - gap;
  // Text editor fills available height minus header + footer.
  const float editorH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.f - 12.f;

  ImGui::BeginGroup();
  ImGui::InputTextMultiline("##tpl_text", gTplEditorTarget, ImVec2(editorW, editorH),
                             ImGuiInputTextFlags_CallbackAlways, TplEditorCallback);
  ImGui::EndGroup();

  ImGui::SameLine();

  // Right toolbox: plain group, auto-height — no scroll, no child window.
  ImGui::BeginGroup();

  // ---- Attribute insert ----
  ImGui::TextUnformatted("Insert attribute");
  ImGui::Separator();
  const struct { const char* label; const char* token; } kAttrs[] = {
    {"ID",          "{id}"},
    {"Northing",    "{north}"},
    {"Easting",     "{east}"},
    {"Elevation",   "{elev}"},
    {"Description", "{desc}"},
  };
  const float btnW = toolW - 4.f;
  for (const auto& a : kAttrs) {
    if (ImGui::Button(a.label, ImVec2(btnW, 0.f)))
      gTplEditorInsert = a.token;
  }

  ImGui::Spacing();
  ImGui::TextUnformatted("Formatting");
  ImGui::Separator();
  const float halfBtnW = (btnW - gap) * 0.5f;
  if (ImGui::Button("Bold on",       ImVec2(halfBtnW, 0.f))) gTplEditorInsert = "[[b]]";
  ImGui::SameLine(0.f, gap);
  if (ImGui::Button("Bold off",      ImVec2(halfBtnW, 0.f))) gTplEditorInsert = "[[/b]]";
  if (ImGui::Button("Italic on",     ImVec2(halfBtnW, 0.f))) gTplEditorInsert = "[[i]]";
  ImGui::SameLine(0.f, gap);
  if (ImGui::Button("Italic off",    ImVec2(halfBtnW, 0.f))) gTplEditorInsert = "[[/i]]";
  if (ImGui::Button("Underline on",  ImVec2(halfBtnW, 0.f))) gTplEditorInsert = "[[u]]";
  ImGui::SameLine(0.f, gap);
  if (ImGui::Button("Underline off", ImVec2(halfBtnW, 0.f))) gTplEditorInsert = "[[/u]]";

  ImGui::Spacing();
  ImGui::TextUnformatted("Color");
  ImGui::Separator();
  // Small swatch: clicking it opens ImGui's built-in color picker popup.
  ImGui::SetNextItemWidth(btnW);
  ImGui::ColorEdit3("##tpl_col", gTplEditorColor,
                    ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs);
  ImGui::Spacing();
  if (ImGui::Button("Insert color tag", ImVec2(btnW, 0.f))) {
    const auto cr = static_cast<uint8_t>(gTplEditorColor[0] * 255.f + 0.5f);
    const auto cg = static_cast<uint8_t>(gTplEditorColor[1] * 255.f + 0.5f);
    const auto cb = static_cast<uint8_t>(gTplEditorColor[2] * 255.f + 0.5f);
    gTplEditorInsert = MtextRichColorTag(cr, cg, cb);
  }
  if (ImGui::Button("End color tag", ImVec2(btnW, 0.f)))
    gTplEditorInsert = "[[/color]]";

  ImGui::EndGroup();

  ImGui::Separator();
  if (ImGui::Button("Apply to all points", ImVec2(160.f, 0.f))) {
    for (size_t i = 0; i < cmd.surveyPoints.size(); ++i)
      EnsureSurveyPointLabelMtext(cmd, i, nullptr);
    BumpCadGpuCache(cmd);
  }
  ImGui::SameLine();
  if (ImGui::Button("Close", ImVec2(80.f, 0.f))) {
    gTplEditorTarget = nullptr;
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}


static void DrawUserPrefsLabelTemplates(AppCommandState& cmd) {
  ImGui::TextWrapped(
      "Click a style to edit its template. Supports placeholders {id} {desc} {elev} {north} {east} "
      "and rich tags [[b]] [[i]] [[u]] [[color:RRGGBB]] [[/color]].");
  ImGui::Spacing();

  struct TplEntry { const char* label; std::string* tpl; };
  TplEntry entries[] = {
    {"Number + description",                   &cmd.surveyLabelTemplates.numberDesc},
    {"Number only",                            &cmd.surveyLabelTemplates.numberOnly},
    {"Description only",                       &cmd.surveyLabelTemplates.descOnly},
    {"Number + elevation",                     &cmd.surveyLabelTemplates.numberElev},
    {"Number + elevation + description",       &cmd.surveyLabelTemplates.numberElevDesc},
    {"Number + northing + easting",            &cmd.surveyLabelTemplates.numberNorthEast},
    {"Northing + easting",                     &cmd.surveyLabelTemplates.northEast},
    {"Number + northing + easting + elevation",&cmd.surveyLabelTemplates.numberNorthEastElev},
  };

  for (auto& e : entries) {
    if (ImGui::Button(e.label, ImVec2(240.f, 0.f)))
      OpenTplEditorPopup(e.tpl, e.label);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", e.tpl->c_str());
  }

  ImGui::Spacing();
  if (ImGui::Button("Apply all templates to survey points")) {
    for (size_t i = 0; i < cmd.surveyPoints.size(); ++i)
      EnsureSurveyPointLabelMtext(cmd, i, nullptr);
    BumpCadGpuCache(cmd);
  }

  DrawTplEditorPopup(cmd);
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

static void DrawUserPrefsRightClick(AppCommandState& cmd) {
  using DM = AppCommandState::RightClickDefaultMode;
  using EM = AppCommandState::RightClickEditMode;
  using CM = AppCommandState::RightClickCommandMode;

  ImGui::TextDisabled("Default Mode — if no objects are selected, right click means:");
  static const char* kDMItems[] = { "Repeat last command", "Shortcut menu" };
  int dmSel = static_cast<int>(cmd.rightClickDefaultMode);
  if (ImGui::Combo("##rmb_default", &dmSel, kDMItems, 2))
    cmd.rightClickDefaultMode = static_cast<DM>(dmSel);

  ImGui::Spacing();
  ImGui::TextDisabled("Edit Mode — if one or more objects are selected, right click means:");
  static const char* kEMItems[] = { "Repeat last command", "Shortcut menu" };
  int emSel = static_cast<int>(cmd.rightClickEditMode);
  if (ImGui::Combo("##rmb_edit", &emSel, kEMItems, 2))
    cmd.rightClickEditMode = static_cast<EM>(emSel);

  ImGui::Spacing();
  ImGui::TextDisabled("Command Mode — if a command is in progress, right click means:");
  static const char* kCMItems[] = { "ENTER", "Shortcut menu: always enabled", "Shortcut menu: enabled when options are present" };
  int cmSel = static_cast<int>(cmd.rightClickCommandMode);
  if (ImGui::Combo("##rmb_command", &cmSel, kCMItems, 3))
    cmd.rightClickCommandMode = static_cast<CM>(cmSel);
}

static void DrawSettingsUserPrefsTab(AppCommandState& cmd) {
  if (ImGui::CollapsingHeader("Right Click Options", ImGuiTreeNodeFlags_DefaultOpen))
    DrawUserPrefsRightClick(cmd);
  if (ImGui::CollapsingHeader("Survey points (markers + linked MTEXT)", ImGuiTreeNodeFlags_DefaultOpen))
    DrawUserPrefsSurveyPoints(cmd);
  if (ImGui::CollapsingHeader("Label templates"))
    DrawUserPrefsLabelTemplates(cmd);
  if (ImGui::CollapsingHeader("Text & MTEXT screen size"))
    DrawUserPrefsTextMtext(cmd);
  if (ImGui::CollapsingHeader("Dimensions"))
    DrawUserPrefsDimensions(cmd);
  if (ImGui::CollapsingHeader("Undo / Redo", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextWrapped("Undo history is per drawing tab and cleared when the tab is closed.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(160.f);
    ImGui::SliderInt("History size (steps)", &cmd.undoHistoryMaxSize, 1, 200);
    cmd.undoHistoryMaxSize = std::clamp(cmd.undoHistoryMaxSize, 1, 200);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted("Maximum number of undo steps to keep per drawing tab.\nOlder steps are discarded when the limit is reached.\nHistory log is written to %APPDATA%\\GoSurvey\\history.log.");
      ImGui::EndTooltip();
    }
  }
}

static void DrawSettingsSelectionTab(AppCommandState& cmd) {
  ImGui::TextUnformatted("Selection"); ImGui::Separator();
  if (ImGui::CollapsingHeader("Grips", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextWrapped("Grips appear as blue squares on selected entities and can be snapped to.");
    ImGui::Spacing();
    ImGui::SliderFloat("Grip size (px)", &cmd.gripSizePx, 2.f, 20.f, "%.1f");
    cmd.gripSizePx = std::clamp(cmd.gripSizePx, 2.f, 20.f);
  }
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

  // Leave room for the separator + button row so they remain visible when content scrolls.
  const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 4.f;
  if (ImGui::BeginChild("##settings_content", ImVec2(0.f, -footerH))) {
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
      if (ImGui::BeginTabItem("Selection"))      { cmd.settingsActiveTabIdx = 8; DrawSettingsSelectionTab(cmd);                                                       ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("Profiles"))       { cmd.settingsActiveTabIdx = 9; DrawSettingsPlaceholderTab("Profiles", "Saved option profiles. Current: <<GoSurvey>>.");ImGui::EndTabItem(); }
      if (ImGui::BeginTabItem("AEC Editor"))     { cmd.settingsActiveTabIdx = 10; DrawSettingsPlaceholderTab("AEC Editor", "Civil/AEC-specific editor preferences.");   ImGui::EndTabItem(); }
      ImGui::EndTabBar();
    }
  }
  ImGui::EndChild();

  ImGui::Separator();
  if (ImGui::Button("OK", ImVec2(90.f, 0.f)))     { if (SaveUserStartupPrefs(cmd)) { if (log) log->push_back("Settings saved (gosurvey-user.json)."); } else { if (log) log->push_back("Error: failed to write gosurvey-user.json (check directory permissions)."); } cmd.showSettingsWindow = false; }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(90.f, 0.f))) cmd.showSettingsWindow = false;
  ImGui::SameLine();
  if (ImGui::Button("Apply", ImVec2(90.f, 0.f)))  { if (SaveUserStartupPrefs(cmd)) { if (log) log->push_back("Settings applied (gosurvey-user.json)."); } else { if (log) log->push_back("Error: failed to write gosurvey-user.json (check directory permissions)."); } }
  ImGui::SameLine();
  ImGui::BeginDisabled(); ImGui::Button("Help", ImVec2(90.f, 0.f)); ImGui::EndDisabled();
  ImGui::End();

  DrawGraphicsPerformanceDialog(cmd, log);
}

// ---------------------------------------------------------------------------
// Drawing Units dialog (UNITS command) — REQ-020. Owns displayLinearPrecision.
// Phase 1: Length group (Decimal + precision) is functional and the single owner
// of the non-survey display precision; a live Sample Output reflects it. The
// Angle and Insertion-scale groups are shown disabled as placeholders for the
// REQ-021 / REQ-022 follow-up phases. Cancel/[X]/Esc revert to the precision the
// dialog opened with; OK persists (REQ-020 acceptance).
// ---------------------------------------------------------------------------
void DrawUnitsDialog(AppCommandState& cmd, std::vector<std::string>* log) {
  // Track the open edge so we can snapshot for Cancel-revert (REQ-020/021).
  static bool   gWasOpen = false;
  static int    gSnapPrecision = 4;
  static int    gSnapAngType = 1;
  static int    gSnapAngPrec = 1;
  static bool   gSnapAngCw = true;
  static double gSnapAngBase = 0.0;
  static int    gSnapInsUnits = 2;
  if (cmd.showUnitsWindow && !gWasOpen) {  // entered the dialog: remember
    gSnapPrecision = cmd.displayLinearPrecision;
    gSnapAngType   = cmd.angleDisplayType;
    gSnapAngPrec   = cmd.angleDisplayPrecision;
    gSnapAngCw     = cmd.angleDisplayClockwise;
    gSnapAngBase   = cmd.angleDisplayBaseDeg;
    gSnapInsUnits  = cmd.drawingInsUnits;
  }
  gWasOpen = cmd.showUnitsWindow;

  if (!cmd.showUnitsWindow)
    return;

  auto revertAndClose = [&]() {
    cmd.displayLinearPrecision   = std::clamp(gSnapPrecision, 0, 12);
    cmd.angleDisplayType         = std::clamp(gSnapAngType, 0, 2);
    cmd.angleDisplayPrecision    = std::clamp(gSnapAngPrec, 0, 6);
    cmd.angleDisplayClockwise    = gSnapAngCw;
    cmd.angleDisplayBaseDeg      = gSnapAngBase;
    cmd.drawingInsUnits          = gSnapInsUnits;
    cmd.showUnitsWindow = false;
  };

  ImGui::SetNextWindowSize(ImVec2(520, 560), ImGuiCond_FirstUseEver);
  bool open = cmd.showUnitsWindow;
  if (!ImGui::Begin("Drawing Units", &open, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    if (!open) revertAndClose();  // window collapsed/closed via [X]
    return;
  }
  if (!open) {  // [X] pressed: treat as Cancel
    ImGui::End();
    revertAndClose();
    return;
  }
  // Esc closes as Cancel while the window is focused.
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    ImGui::End();
    revertAndClose();
    return;
  }

  // Precision dropdown: "0", "0.0", … "0.00000000" (0..8 decimals), AutoCAD-style.
  static const char* kPrecLabels[] = {"0",         "0.0",       "0.00",      "0.000",
                                       "0.0000",    "0.00000",   "0.000000",  "0.0000000",
                                       "0.00000000"};
  constexpr int kPrecCount = IM_ARRAYSIZE(kPrecLabels);

  if (ImGui::BeginTable("##units_top", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();

    // ---- Length ----
    ImGui::TableSetColumnIndex(0);
    BoxBegin("Length", 150.f);
    {
      ImGui::TextUnformatted("Type:");
      const char* kLenTypes[] = {"Decimal"};
      int lenType = 0;
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::Combo("##len_type", &lenType, kLenTypes, IM_ARRAYSIZE(kLenTypes));
      ItemHelpTooltip("GoSurvey works in decimal units (survey/civil norm). Other length formats are reserved.");

      ImGui::Spacing();
      ImGui::TextUnformatted("Precision:");
      int precIdx = std::clamp(cmd.displayLinearPrecision, 0, kPrecCount - 1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::Combo("##len_prec", &precIdx, kPrecLabels, kPrecCount))
        cmd.displayLinearPrecision = std::clamp(precIdx, 0, kPrecCount - 1);
      ItemHelpTooltip("Decimal places shown for all non-survey coordinate/length readouts: status bar, ID, INVERSE, dimensions, and properties. Display only — stored values keep full precision.");
    }
    BoxEnd();

    // ---- Angle (REQ-021) ----
    ImGui::TableSetColumnIndex(1);
    BoxBegin("Angle", 200.f);
    {
      ImGui::TextUnformatted("Type:");
      const char* kAngTypes[] = {"Decimal Degrees", "Deg/Min/Sec", "Surveyor's Units"};
      int angType = std::clamp(cmd.angleDisplayType, 0, 2);
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::Combo("##ang_type", &angType, kAngTypes, IM_ARRAYSIZE(kAngTypes)))
        cmd.angleDisplayType = std::clamp(angType, 0, 2);

      ImGui::Spacing();
      ImGui::TextUnformatted("Precision (decimals on smallest unit):");
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::DragInt("##ang_prec", &cmd.angleDisplayPrecision, 0.1f, 0, 6, "%d"))
        cmd.angleDisplayPrecision = std::clamp(cmd.angleDisplayPrecision, 0, 6);

      // Direction (clockwise/CCW) + base angle. Surveyor's units always reference
      // the N-S meridian, so direction/base do not apply there.
      ImGui::BeginDisabled(cmd.angleDisplayType == 2);
      ImGui::Checkbox("Clockwise", &cmd.angleDisplayClockwise);
      ImGui::TextUnformatted("Base (0\xc2\xb0):");
      const char* kBaseNames[] = {"North", "East", "South", "West", "Custom"};
      const double kBaseDeg[]  = {0.0, 90.0, 180.0, 270.0};
      int baseSel = 4;  // Custom unless it matches a cardinal
      for (int i = 0; i < 4; ++i)
        if (std::abs(cmd.angleDisplayBaseDeg - kBaseDeg[i]) < 1e-6) baseSel = i;
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::Combo("##ang_base", &baseSel, kBaseNames, IM_ARRAYSIZE(kBaseNames))) {
        if (baseSel < 4) cmd.angleDisplayBaseDeg = kBaseDeg[baseSel];
      }
      if (baseSel == 4) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputDouble("##ang_base_custom", &cmd.angleDisplayBaseDeg, 0., 0., "%.4f\xc2\xb0 CW from north")) {
          cmd.angleDisplayBaseDeg = std::fmod(cmd.angleDisplayBaseDeg, 360.0);
          if (cmd.angleDisplayBaseDeg < 0.0) cmd.angleDisplayBaseDeg += 360.0;
        }
      }
      ImGui::EndDisabled();
    }
    BoxEnd();
    ImGui::EndTable();
  }

  // ---- Insertion scale: drawing unit (AutoCAD INSUNITS relabel, REQ-022) ----
  BoxBegin("Insertion scale", 95.f);
  {
    ImGui::TextUnformatted("Units to scale inserted content:");
    const char* kInsNames[] = {"Feet", "Meters", "Unitless"};
    const int   kInsCodes[] = {2, 6, 0};
    int insSel = 0;
    for (int i = 0; i < 3; ++i)
      if (cmd.drawingInsUnits == kInsCodes[i]) insSel = i;
    ImGui::SetNextItemWidth(220.f);
    if (ImGui::Combo("##ins_units", &insSel, kInsNames, IM_ARRAYSIZE(kInsNames))) {
      cmd.drawingInsUnits = kInsCodes[std::clamp(insSel, 0, 2)];
      BumpCadGpuCache(cmd);  // document property: flag the drawing as modified
    }
    ItemHelpTooltip("AutoCAD INSUNITS. A relabel only: it tells the drawing (and the DXF $INSUNITS header) what unit it is in. It never rescales or converts geometry.");
  }
  ImGui::TextDisabled("Relabel only — saved to the drawing (.gs) and DXF $INSUNITS; geometry unchanged.");
  BoxEnd();

  // ---- Sample Output (live) ----
  BoxBegin("Sample Output", 80.f);
  {
    const int p = cmd.displayLinearPrecision;
    const std::string sx = FormatLinear(1.5, p);
    const std::string sy = FormatLinear(2.0, p);
    const std::string sz = FormatLinear(0.0, p);
    ImGui::Text("%s, %s, %s", sx.c_str(), sy.c_str(), sz.c_str());
    // Angle preview uses the current (pre-REQ-021) bearing formatter.
    const std::string dist = FormatLinear(3.0, p);
    ImGui::Text("%s < %s", dist.c_str(), FormatBearing(45.0, CadAngleDisplaySettings(cmd)).c_str());
  }
  BoxEnd();

  ImGui::Separator();
  if (ImGui::Button("OK", ImVec2(90.f, 0.f))) {
    if (SaveUserStartupPrefs(cmd)) {
      if (log) log->push_back("Drawing units saved (gosurvey-user.json).");
    } else if (log) {
      log->push_back("Error: failed to write gosurvey-user.json (check directory permissions).");
    }
    cmd.showUnitsWindow = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(90.f, 0.f)))
    revertAndClose();

  ImGui::End();
}
