// AutoCAD-style Options dialog — all DrawSettings* functions.
// Static helpers and tab drawers are file-local; only DrawSettingsPanel is public.

#include "CadUi.hpp"
#include "CadUiHelpers.hpp"
#include "AppIcon.hpp"
#include "AppPaths.hpp"
#include "GpuPreference.hpp"
#include "MtextRichFormat.hpp"
#include "NumFormat.hpp"
#include "SurveyPoints.hpp"
#include "UserPrefs.hpp"
#include "Version.hpp"
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

// A bare description paragraph at the top of a tab used to sit directly on the
// dialog body — fine while that body was one flat colour, but once dialogs
// gained their own gradient fill (REQ-081 rev 7 / issue #183) unboxed text
// read as "just laid over" the background instead of belonging to the dialog.
// This gives it the same bordered, recessed treatment every other section in
// this dialog already has (see BoxBegin), sized to fit: the box's own width
// drives the wrap (so it always matches the current window width, never a
// stale one), and its height is computed from that wrapped text every frame,
// so the paragraph is never clipped and never leaves dead space either.
static void DrawSettingsNote(const char* text) {
  const float wrapW = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().WindowPadding.x * 2.f;
  const ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, false, wrapW);
  const float boxH = textSize.y + ImGui::GetStyle().WindowPadding.y * 2.f;
  ImGui::BeginChild("##settings_note", ImVec2(0.f, boxH), true);
  ImGui::TextWrapped("%s", text);
  ImGui::EndChild();
  ImGui::Spacing();
}

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
      cmd.viewportBgR = 0.1f; cmd.viewportBgG = 0.1f; cmd.viewportBgB = 0.1f;
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
  // REQ-310. Sits with the crosshair settings rather than under the 3D placeholders below, because
  // it is a real, working property of the cursor and those are not.
  ImGui::Checkbox("3D crosshair (show UCS axes)##xhair3d", &cmd.viewportCrosshair3d);
  ItemHelpTooltip("Draw the cursor as the active UCS's X (red), Y (green) and Z (blue) axes instead "
                  "of two screen-aligned arms, so it shows which way the drawing plane runs under "
                  "an orbited view. Model space only. Command bar: CROSSHAIR3D ON | OFF.");
  ImGui::Spacing();
  if (ImGui::TreeNode("Crosshair details##xhairDetail")) {
    float xc[3] = {cmd.viewportCrosshairR, cmd.viewportCrosshairG, cmd.viewportCrosshairB};
    if (ImGui::ColorEdit3("Color##xhair", xc)) {
      cmd.viewportCrosshairR = xc[0]; cmd.viewportCrosshairG = xc[1]; cmd.viewportCrosshairB = xc[2];
    }
    ItemHelpTooltip("The 2D crosshair's colour. The 3D crosshair uses fixed per-axis colours "
                    "matching the UCS icon, so the two always agree about which axis is which.");
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
  DrawSettingsNote(
      "Search paths, file locations, and startup template. GoSurvey loads a workspace template .gst at startup; an empty "
      "Custom path uses the bundled resources/default-template.gst next to the executable. Preferences are saved "
      "in gosurvey-user.json beside the executable.");
  BoxBegin("Startup template (.gst)", 140.f);
  ImGui::InputText("Custom .gst path (UTF-8)##startup_gst", cmd.defaultWorkspaceTemplatePathUtf8,
                   IM_ARRAYSIZE(cmd.defaultWorkspaceTemplatePathUtf8));
  ImGui::SameLine();
#if defined(_WIN32)
  if (ImGui::Button("Browse##startup_gst")) {
    if (BrowseOpenFileGstUtf8(cmd.defaultWorkspaceTemplatePathUtf8, sizeof(cmd.defaultWorkspaceTemplatePathUtf8)) && log)
      log->push_back("Startup template path set from file dialog.");
  }
#else
  ImGui::BeginDisabled(); ImGui::Button("Browse##startup_gst"); ImGui::EndDisabled();
  ItemHelpTooltip("File browse for startup template is only implemented on Windows in this build.");
#endif
  const std::filesystem::path bundled = ResolveDefaultWorkspaceTemplateGstPath();
  if (!bundled.empty())
    ImGui::TextDisabled("Bundled template resolved to: %s", bundled.u8string().c_str());
  else
    ImGui::TextDisabled("Bundled template not found (expected resources/default-template.gst beside exe or cwd).");
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

  // BUG-013. GoSurvey asks for the discrete GPU by default (the exported driver symbols in
  // main.cpp); this hands the choice back for a laptop running on battery. The write goes to
  // Windows' own per-application preference, which is why it cannot apply until the next launch —
  // the GPU is bound when the process starts, so anything claiming otherwise would be a lie.
  if (ImGui::Checkbox("Prefer the integrated GPU (saves battery, slower)", &cmd.systemPreferIntegratedGpu)) {
    std::string err;
    const platform::GpuPreference pref = cmd.systemPreferIntegratedGpu ? platform::GpuPreference::PowerSaving
                                                                       : platform::GpuPreference::HighPerformance;
    if (platform::SetGpuPreferenceForThisExe(pref, &err))
      cmd.systemGpuPreferenceMessage = "Saved. Takes effect the next time GoSurvey starts.";
    else
      cmd.systemGpuPreferenceMessage = "Could not save: " + err;  // REQ-201: never silently
  }
  if (!cmd.systemGpuPreferenceMessage.empty())
    ImGui::TextDisabled("%s", cmd.systemGpuPreferenceMessage.c_str());
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

/// REQ-077: the update check is switchable, and switching it off means no network request is
/// ever made — not a suppressed prompt. The version is shown here too, so a user reporting a
/// problem has somewhere to read it off.
static void DrawSystemUpdates(AppCommandState& cmd) {
  ImGui::TextDisabled("Version %s", GOSURVEY_VERSION_FULL);
  ImGui::Checkbox("Check for updates on startup", &cmd.updatePrefs.enabled);
  ImGui::BeginDisabled(!cmd.updatePrefs.enabled);
  ImGui::Checkbox("Include beta releases", &cmd.updatePrefs.useBetaChannel);
  ImGui::EndDisabled();
  if (!cmd.updatePrefs.skippedVersion.empty()) {
    ImGui::TextDisabled("Skipping %s", cmd.updatePrefs.skippedVersion.c_str());
    ImGui::SameLine();
    // Without this the only way out of a skip is editing the prefs file by hand.
    if (ImGui::SmallButton("Clear##unskip")) cmd.updatePrefs.skippedVersion.clear();
  }
}

/// REQ-080's last acceptance condition: the usage ping is disclosed in the settings panel, in
/// plain language, saying what leaves the machine and what does not.
///
/// It sits next to Updates on purpose — they are the two things that talk to the network, and a
/// user checking on one is asking about the other. It is text, not a checkbox: the ping has no
/// opt-out by decision (spec/project.md, 2026-08-16, D3). Offering a toggle that did nothing, or
/// implying consent that is not asked for, would be worse than saying so plainly.
///
/// Amended 2026-08-23 (D-2026-08-23-e): this used to promise the ping was unconditionally
/// anonymous ("never sends your name, email..."). That promise was reversed by explicit user
/// decision — when signed in, the ping now includes the signed-in email — and leaving the old
/// text in place would make it a lie the moment that shipped, not a stale detail. Kept honest
/// rather than kept simple.
static void DrawSystemUsageData(AppCommandState& cmd) {
  ImGui::TextWrapped(
      "GoSurvey reports usage so development can be aimed at what people actually run. Every "
      "report includes a random ID that identifies this installation, the version, whether you "
      "are on stable or beta, and that you are on Windows — once when installed, and at most "
      "once a day after that.");
  ImGui::Spacing();
  if (cmd.authSignedIn) {
    ImGui::TextWrapped(
        "Because you are signed in (see Account below), your email is included too. Sign out "
        "and reports go back to fully anonymous.");
  } else {
    ImGui::TextWrapped(
        "It never sends your name, company, computer name, file names, drawings, survey data, "
        "or location. Your email is included only when you are signed in — you are currently "
        "signed out, so reports stay fully anonymous.");
  }
}

/// REQ-091: sign-in entry point. Lives beside Usage Data for the same reason that box
/// sits beside Updates — this is the other thing that leaves the machine, and a user reading one
/// disclosure is reading about the other. Unlike the telemetry ping, this one has a name attached
/// once the user opts in, and the box makes that plain: what identity provider, what's shown, and
/// that nothing in the app is gated by it yet (REQ-091 is identity only, not enforcement).
static void DrawAccountsSignIn(AppCommandState& cmd) {
  if (cmd.authSignedIn) {
    ImGui::TextWrapped("Signed in as %s",
                       cmd.authEmail.empty() ? "(no email on file)" : cmd.authEmail.c_str());
    ImGui::Spacing();
    ImGui::BeginDisabled(cmd.authBusy);
    if (ImGui::Button("Sign Out", ImVec2(-FLT_MIN, 0.f))) {
      cmd.authSignOutRequested = true;
    }
    ImGui::EndDisabled();
    return;
  }

  ImGui::TextWrapped(
      "Sign in with Google, Microsoft, or an email and password to identify this account. "
      "Nothing in GoSurvey is currently gated on signing in.");
  ImGui::Spacing();
  ImGui::BeginDisabled(cmd.authBusy);
  if (ImGui::Button(cmd.authInteractiveBusy ? "Waiting for browser..." : "Sign In",
                    ImVec2(-FLT_MIN, 0.f))) {
    cmd.authSignInRequested = true;
  }
  ImGui::EndDisabled();
  if (!cmd.authError.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.f), "%s", cmd.authError.c_str());
  }
}

static void DrawSettingsSystemTab(AppCommandState& cmd) {
  if (ImGui::BeginTable("##sys_layout", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    // 160 rather than 110: the BUG-013 GPU checkbox and the line that reports whether its setting
    // was saved both live in this box, and at 110 the checkbox was clipped mid-word.
    BoxBegin("Hardware Acceleration", 160.f); DrawSystemHardwareAccel(cmd); BoxEnd();
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
    BoxBegin("Updates", 110.f); DrawSystemUpdates(cmd); BoxEnd();
    BoxBegin("Usage Data", 175.f); DrawSystemUsageData(cmd); BoxEnd();
    BoxBegin("Account", 150.f); DrawAccountsSignIn(cmd); BoxEnd();
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
  ImGui::Checkbox("Intersection (objects that actually meet)", &cmd.objectSnapIntersection);
  ItemHelpTooltip("Snaps where two objects genuinely cross in 3D — their paths meet AND their "
                  "elevations agree there.");
  ImGui::Checkbox("Apparent intersection (objects that only look like they meet)",
                  &cmd.objectSnapApparentIntersection);
  ItemHelpTooltip("Snaps where two objects cross as seen from the current view but need not touch — "
                  "e.g. a line passing over another at a different elevation. The point returned is on "
                  "whichever object is nearer the camera. Off by default: it fires on objects that do "
                  "not meet.");
  ImGui::Checkbox("Surface elevation (interpolated TIN at the cursor)", &cmd.objectSnapSurface);
  ImGui::Checkbox("Solid face and edge (REQ-313)", &cmd.objectSnapSolid);
  ItemHelpTooltip("Snaps to the covering visible surface's triangle plane at the cursor (REQ-127). "
                  "Weaker than endpoints so vertices still win. Off: no surface snap.");
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
  const bool le = ImGui::DragFloat("Label left edge east of point (plotted in)", &cmd.surveyLabelOffsetEastPlottedIn, 0.002f, -2.f, 4.f, "%.3f");
  const bool ln = ImGui::DragFloat("Label centre north of point (plotted in)", &cmd.surveyLabelOffsetNorthPlottedIn, 0.002f, -2.f, 4.f, "%.3f");
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

// REQ-084 (a): the three context modes moved OUT of this tab and into the Right-Click
// Customization dialog, which is their single owner. What is left here is the door to it plus a
// one-line summary of the current settings, so the tab still says what right-click does today
// without duplicating (and eventually contradicting) the dialog's controls.
static void DrawUserPrefsRightClick(AppCommandState& cmd) {
  using DM = AppCommandState::RightClickDefaultMode;
  using EM = AppCommandState::RightClickEditMode;
  using CM = AppCommandState::RightClickCommandMode;

  ImGui::TextWrapped("Windows standard behavior");
  ImGui::Spacing();
  if (ImGui::Button("Right-click Customization...", ImVec2(230.f, 0.f)))
    cmd.showRightClickDialog = true;
  ItemHelpTooltip("Choose what right-click does with no selection, with a selection, and during a "
                  "command — and whether a quick click means ENTER (time-sensitive right-click).");

  ImGui::Spacing();
  const char* dm = cmd.rightClickDefaultMode == DM::RepeatLastCommand ? "Repeat Last Command" : "Shortcut Menu";
  const char* em = cmd.rightClickEditMode    == EM::RepeatLastCommand ? "Repeat Last Command" : "Shortcut Menu";
  const char* cm = cmd.rightClickCommandMode == CM::Enter ? "ENTER"
                   : cmd.rightClickCommandMode == CM::ShortcutMenuAlways ? "Shortcut Menu (always)"
                                                                        : "Shortcut Menu (when options present)";
  if (cmd.rightClickTimeSensitive) {
    // Say what actually governs, not what the stored preference says: with the timer on, Default
    // and Command Mode do not decide anything, and printing them here would misinform (REQ-201).
    ImGui::TextDisabled("Time-sensitive: on (%d ms) — quick click = ENTER, longer click = Shortcut Menu",
                        cmd.rightClickLongerClickMs);
    ImGui::TextDisabled("Edit Mode: %s", em);
  } else {
    ImGui::TextDisabled("Default: %s   |   Edit: %s   |   Command: %s", dm, em, cm);
  }
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
  DrawSettingsNote(description);
  ImGui::BeginDisabled(); ImGui::TextDisabled("(No GoSurvey-specific controls in this section yet.)"); ImGui::EndDisabled();
}

void DrawSettingsPanel(AppCommandState& cmd, std::vector<std::string>* log) {
  if (!cmd.showSettingsWindow) return;

  ImGui::SetNextWindowSize(ImVec2(960, 720), ImGuiCond_FirstUseEver);
  bool open = cmd.showSettingsWindow;
  if (!ImGui::Begin("Options", &open, ImGuiWindowFlags_NoCollapse)) {
    cmd.showSettingsWindow = open; ImGui::End(); return;
  }
  BeginStyledDialog();
  cmd.showSettingsWindow = open;
  DrawSettingsHeader(cmd);

  // Leave room for the separator + button row so they remain visible when content scrolls.
  const float footerH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + 4.f;
  // This child is a layout container, not a box: it must NOT take the recessed
  // ChildBg, or the inset panels inside it would be the same tone as their own
  // background and the "boxes in a dialog" reading would collapse.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
  const bool contentVisible = ImGui::BeginChild("##settings_content", ImVec2(0.f, -footerH));
  ImGui::PopStyleColor();  // popped unconditionally — BeginChild consumes it as it paints
  if (contentVisible) {
    // Tab strip. ImGui paints no background behind a plain tab bar, so tabs float
    // on the dialog body and read as bare text. Filling the row first — with a
    // rule under it — puts the unselected tabs on a recessed strip and leaves the
    // selected one standing on the body it belongs to. The strip is exactly one
    // frame tall, which is a tab bar's height by construction.
    {
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const float w = ImGui::GetContentRegionAvail().x;
      const float h = ImGui::GetFrameHeight();
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(ImGuiCol_Tab));
      dl->AddLine(ImVec2(p0.x, p0.y + h - 0.5f), ImVec2(p0.x + w, p0.y + h - 0.5f),
                  ImGui::GetColorU32(ImGuiCol_Border), 1.f);
    }
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
  if (StyledButton("OK", ImVec2(90.f, 0.f), /*primary=*/true))     { if (SaveUserStartupPrefs(cmd)) { if (log) log->push_back("Settings saved (gosurvey-user.json)."); } else { if (log) log->push_back("Error: failed to write gosurvey-user.json (check directory permissions)."); } cmd.showSettingsWindow = false; }
  ImGui::SameLine();
  if (StyledButton("Cancel", ImVec2(90.f, 0.f))) cmd.showSettingsWindow = false;
  ImGui::SameLine();
  if (StyledButton("Apply", ImVec2(90.f, 0.f), /*primary=*/true))  { if (SaveUserStartupPrefs(cmd)) { if (log) log->push_back("Settings applied (gosurvey-user.json)."); } else { if (log) log->push_back("Error: failed to write gosurvey-user.json (check directory permissions)."); } }
  ImGui::SameLine();
  ImGui::BeginDisabled(); StyledButton("Help", ImVec2(90.f, 0.f)); ImGui::EndDisabled();
  ImGui::End();

  DrawGraphicsPerformanceDialog(cmd, log);
}

// ---------------------------------------------------------------------------
// Right-Click Customization dialog — REQ-084 (a).
//
// The single owner of the three context modes and of time-sensitive right-click. Laid out as
// AutoCAD's is: the checkbox and its duration on top, then three labelled groups of RADIO buttons
// (not combo boxes) each carrying the sentence that says when it applies.
//
// Cancel restores every value the dialog opened with — including the checkbox and the duration —
// which is why the open edge is snapshotted, exactly as DrawUnitsDialog does for precision.
// ---------------------------------------------------------------------------

/// A titled group box: the label sits on the frame, the controls sit inside it. AutoCAD's dialog
/// is read group-first, and three flat runs of radio buttons would not carry that reading.
static void RmbGroupBegin(const char* label, float height) {
  ImGui::Spacing();
  ImGui::TextUnformatted(label);
  ImGui::BeginChild((std::string("##rmbgrp_") + label).c_str(), ImVec2(0.f, height), true);
  ImGui::Spacing();
}
static void RmbGroupEnd() { ImGui::EndChild(); }

void DrawRightClickCustomizationDialog(AppCommandState& cmd, std::vector<std::string>* log) {
  using DM = AppCommandState::RightClickDefaultMode;
  using EM = AppCommandState::RightClickEditMode;
  using CM = AppCommandState::RightClickCommandMode;

  // Snapshot on the open edge so Cancel is a true revert (REQ-084 acceptance).
  static bool gWasOpen = false;
  static bool gSnapTimeSensitive = false;
  static int  gSnapMs = 250;
  static DM   gSnapDefault = DM::RepeatLastCommand;
  static EM   gSnapEdit = EM::ShortcutMenu;
  static CM   gSnapCommand = CM::Enter;
  if (cmd.showRightClickDialog && !gWasOpen) {
    gSnapTimeSensitive = cmd.rightClickTimeSensitive;
    gSnapMs            = cmd.rightClickLongerClickMs;
    gSnapDefault       = cmd.rightClickDefaultMode;
    gSnapEdit          = cmd.rightClickEditMode;
    gSnapCommand       = cmd.rightClickCommandMode;
  }
  gWasOpen = cmd.showRightClickDialog;
  if (!cmd.showRightClickDialog)
    return;

  auto revert = [&]() {
    cmd.rightClickTimeSensitive = gSnapTimeSensitive;
    cmd.rightClickLongerClickMs = gSnapMs;
    cmd.rightClickDefaultMode   = gSnapDefault;
    cmd.rightClickEditMode      = gSnapEdit;
    cmd.rightClickCommandMode   = gSnapCommand;
  };

  ImGui::SetNextWindowSize(ImVec2(560.f, 660.f), ImGuiCond_FirstUseEver);
  bool open = true;
  if (!ImGui::Begin("Right-Click Customization", &open, ImGuiWindowFlags_NoCollapse)) {
    if (!open) {  // title-bar [X] is a Cancel, like every other dialog in this file
      revert();
      cmd.showRightClickDialog = false;
    }
    ImGui::End();
    return;
  }
  if (!open) {
    revert();
    cmd.showRightClickDialog = false;
    ImGui::End();
    return;
  }

  // --- Time-sensitive right-click ---------------------------------------------------------
  ImGui::Checkbox("Turn on time-sensitive right-click:", &cmd.rightClickTimeSensitive);
  ImGui::Indent(24.f);
  ImGui::TextUnformatted("Quick click for ENTER");
  ImGui::TextUnformatted("Longer click to display Shortcut Menu");
  ImGui::Spacing();
  ImGui::BeginDisabled(!cmd.rightClickTimeSensitive);
  ImGui::TextUnformatted("Longer click duration:");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70.f);
  ImGui::InputInt("##rmb_ms", &cmd.rightClickLongerClickMs, 0, 0);
  ImGui::SameLine();
  ImGui::TextUnformatted("milliseconds");
  ImGui::EndDisabled();
  ImGui::Unindent(24.f);
  // Clamp unconditionally: the field is typed into, so a 0 or a 99999 must not reach the
  // classifier and make right-click either unusable or permanently an ENTER.
  cmd.rightClickLongerClickMs = std::clamp(cmd.rightClickLongerClickMs, AppCommandState::kRightClickMinMs,
                                           AppCommandState::kRightClickMaxMs);

  // With the timer on, the hold duration decides the Default and Command contexts, so their stored
  // preferences do not apply. Greying them says that; silently ignoring them would not (REQ-201).
  const bool modesGoverned = cmd.rightClickTimeSensitive;
  const float kGroupH = ImGui::GetFrameHeightWithSpacing() * 3.1f;

  // --- Default Mode -----------------------------------------------------------------------
  ImGui::BeginDisabled(modesGoverned);
  RmbGroupBegin("Default Mode", kGroupH);
  ImGui::TextUnformatted("If no objects are selected, right-click means");
  {
    int sel = static_cast<int>(cmd.rightClickDefaultMode);
    if (ImGui::RadioButton("Repeat Last Command##dm", &sel, 0)) cmd.rightClickDefaultMode = DM::RepeatLastCommand;
    if (ImGui::RadioButton("Shortcut Menu##dm", &sel, 1))       cmd.rightClickDefaultMode = DM::ShortcutMenu;
  }
  RmbGroupEnd();
  ImGui::EndDisabled();

  // --- Edit Mode --------------------------------------------------------------------------
  // Never disabled: a selection still chooses between repeating and the menu, whatever the timer
  // is doing (REQ-084 (a)).
  RmbGroupBegin("Edit Mode", kGroupH);
  ImGui::TextUnformatted("If one or more objects are selected, right-click means");
  {
    int sel = static_cast<int>(cmd.rightClickEditMode);
    if (ImGui::RadioButton("Repeat Last Command##em", &sel, 0)) cmd.rightClickEditMode = EM::RepeatLastCommand;
    if (ImGui::RadioButton("Shortcut Menu##em", &sel, 1))       cmd.rightClickEditMode = EM::ShortcutMenu;
  }
  RmbGroupEnd();

  // --- Command Mode -----------------------------------------------------------------------
  ImGui::BeginDisabled(modesGoverned);
  RmbGroupBegin("Command Mode", ImGui::GetFrameHeightWithSpacing() * 4.1f);
  ImGui::TextUnformatted("If a command is in progress, right-click means");
  {
    int sel = static_cast<int>(cmd.rightClickCommandMode);
    if (ImGui::RadioButton("ENTER##cm", &sel, 0))
      cmd.rightClickCommandMode = CM::Enter;
    if (ImGui::RadioButton("Shortcut Menu: always enabled##cm", &sel, 1))
      cmd.rightClickCommandMode = CM::ShortcutMenuAlways;
    if (ImGui::RadioButton("Shortcut Menu: enabled when command options are present##cm", &sel, 2))
      cmd.rightClickCommandMode = CM::ShortcutMenuWhenOptions;
  }
  RmbGroupEnd();
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button("Apply && Close", ImVec2(150.f, 0.f))) {
    if (SaveUserStartupPrefs(cmd)) {
      if (log) log->push_back("Right-click customization saved (gosurvey-user.json).");
    } else if (log) {
      log->push_back("Error: failed to write gosurvey-user.json (check directory permissions).");
    }
    cmd.showRightClickDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(110.f, 0.f))) {
    revert();
    cmd.showRightClickDialog = false;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled();
  ImGui::Button("Help", ImVec2(110.f, 0.f));
  ImGui::EndDisabled();

  ImGui::End();
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
