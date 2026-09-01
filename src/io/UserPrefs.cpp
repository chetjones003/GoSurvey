#include "UserPrefs.hpp"

#include "AppIcon.hpp"
#include "CadCommands.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace {

std::filesystem::path UserPrefsJsonPath() {
  const std::filesystem::path dir = UserDataDirectory();
  if (!dir.empty())
    return dir / "gosurvey-user.json";
  const std::filesystem::path exeDir = AppExecutableDirectory();
  if (!exeDir.empty())
    return exeDir / "gosurvey-user.json";
  return std::filesystem::path("gosurvey-user.json");
}

// Applies the "settings" sub-object from user.json to cmd. All fields are optional with safe clamping.
void ApplyUserPrefsSettings(AppCommandState& st, const nlohmann::json& s) {
  if (!s.is_object())
    return;

  auto num = [&](const char* k, float* out, float lo, float hi) {
    if (s.contains(k) && s[k].is_number())
      *out = std::clamp(s[k].get<float>(), lo, hi);
  };
  auto b = [&](const char* k, bool* out) {
    if (s.contains(k) && s[k].is_boolean())
      *out = s[k].get<bool>();
  };
  auto str = [&](const nlohmann::json& obj, const char* k, std::string* out) {
    if (obj.contains(k) && obj[k].is_string())
      *out = obj[k].get<std::string>();
  };

  // --- Display / System settings ---
  if (s.contains("displayColorThemeIdx") && s["displayColorThemeIdx"].is_number_integer())
    st.displayColorThemeIdx = std::clamp(s["displayColorThemeIdx"].get<int>(), 0, 1);
  if (s.contains("settingsActiveTabIdx") && s["settingsActiveTabIdx"].is_number_integer())
    st.settingsActiveTabIdx = std::clamp(s["settingsActiveTabIdx"].get<int>(), 0, 10);
  if (s.contains("displayArcCircleSmoothness") && s["displayArcCircleSmoothness"].is_number_integer())
    st.displayArcCircleSmoothness = std::clamp(s["displayArcCircleSmoothness"].get<int>(), 8, 20000);
  if (s.contains("displayCrosshairSizePct") && s["displayCrosshairSizePct"].is_number_integer()) {
    st.displayCrosshairSizePct = std::clamp(s["displayCrosshairSizePct"].get<int>(), 1, 100);
    const float f = static_cast<float>(st.displayCrosshairSizePct) * 0.01f;
    st.viewportCrosshairArmFracX = std::clamp(f * 0.6f, 0.002f, 0.5f);
    st.viewportCrosshairArmFracY = std::clamp(f, 0.002f, 0.5f);
  }
  if (s.contains("displayWheelZoomFactor") && s["displayWheelZoomFactor"].is_number())
    st.displayWheelZoomFactor = std::clamp(s["displayWheelZoomFactor"].get<float>(), 1.01f, 3.0f);
  if (s.contains("displayFadeXref") && s["displayFadeXref"].is_number_integer())
    st.displayFadeXref = std::clamp(s["displayFadeXref"].get<int>(), 0, 90);
  if (s.contains("displayFadeInPlace") && s["displayFadeInPlace"].is_number_integer())
    st.displayFadeInPlace = std::clamp(s["displayFadeInPlace"].get<int>(), 0, 90);
  if (s.contains("displayLinearPrecision") && s["displayLinearPrecision"].is_number_integer())
    st.displayLinearPrecision = std::clamp(s["displayLinearPrecision"].get<int>(), 0, 12);
  if (s.contains("angleDisplayType") && s["angleDisplayType"].is_number_integer())
    st.angleDisplayType = std::clamp(s["angleDisplayType"].get<int>(), 0, 2);
  if (s.contains("angleDisplayPrecision") && s["angleDisplayPrecision"].is_number_integer())
    st.angleDisplayPrecision = std::clamp(s["angleDisplayPrecision"].get<int>(), 0, 6);
  if (s.contains("angleDisplayClockwise") && s["angleDisplayClockwise"].is_boolean())
    st.angleDisplayClockwise = s["angleDisplayClockwise"].get<bool>();
  if (s.contains("angleDisplayBaseDeg") && s["angleDisplayBaseDeg"].is_number())
    st.angleDisplayBaseDeg = s["angleDisplayBaseDeg"].get<double>();
  if (s.contains("systemHardwareAcceleration") && s["systemHardwareAcceleration"].is_boolean())
    st.systemHardwareAcceleration = s["systemHardwareAcceleration"].get<bool>();
  if (s.contains("systemPreferIntegratedGpu") && s["systemPreferIntegratedGpu"].is_boolean())
    st.systemPreferIntegratedGpu = s["systemPreferIntegratedGpu"].get<bool>();
  if (s.contains("gfxSmoothLineDisplay") && s["gfxSmoothLineDisplay"].is_boolean())
    st.gfxSmoothLineDisplay = s["gfxSmoothLineDisplay"].get<bool>();
  if (s.contains("gfxAcceleratedFontDisplay") && s["gfxAcceleratedFontDisplay"].is_boolean())
    st.gfxAcceleratedFontDisplay = s["gfxAcceleratedFontDisplay"].get<bool>();
  if (s.contains("gfxVideoMemoryCachingLevel") && s["gfxVideoMemoryCachingLevel"].is_number_integer())
    st.gfxVideoMemoryCachingLevel = std::clamp(s["gfxVideoMemoryCachingLevel"].get<int>(), 1, 5);

  // --- Crosshair appearance (User Preferences → Crosshair details) ---
  num("viewportCrosshairR",      &st.viewportCrosshairR,      0.f, 1.f);
  num("viewportCrosshairG",      &st.viewportCrosshairG,      0.f, 1.f);
  num("viewportCrosshairB",      &st.viewportCrosshairB,      0.f, 1.f);
  num("viewportCrosshairHairPx", &st.viewportCrosshairHairPx, 0.75f, 4.f);

  // --- Viewport background (Display → Window Elements) ---
  num("viewportBgR", &st.viewportBgR, 0.f, 1.f);
  num("viewportBgG", &st.viewportBgG, 0.f, 1.f);
  num("viewportBgB", &st.viewportBgB, 0.f, 1.f);

  // --- Survey point settings (User Preferences tab) ---
  num("surveyPointCrossSpanPlottedInches",    &st.surveyPointCrossSpanPlottedInches,    0.02f, 2.f);
  b  ("surveyPointShowIdInViewport",          &st.surveyPointShowIdInViewport);
  if (s.contains("surveyPointDisplayPrecision") && s["surveyPointDisplayPrecision"].is_number_integer())
    st.surveyPointDisplayPrecision = std::clamp(s["surveyPointDisplayPrecision"].get<int>(), 0, 12);
  num("surveyPointLabelPlottedHeightInches",  &st.surveyPointLabelPlottedHeightInches,  0.04f, 0.5f);
  num("surveyLabelOffsetEastPlottedIn",       &st.surveyLabelOffsetEastPlottedIn,       -2.f, 4.f);
  num("surveyLabelOffsetNorthPlottedIn",      &st.surveyLabelOffsetNorthPlottedIn,      -2.f, 4.f);
  if (s.contains("surveyLabelTemplates") && s["surveyLabelTemplates"].is_object()) {
    const auto& t = s["surveyLabelTemplates"];
    str(t, "numberDesc",    &st.surveyLabelTemplates.numberDesc);
    str(t, "numberOnly",    &st.surveyLabelTemplates.numberOnly);
    str(t, "descOnly",      &st.surveyLabelTemplates.descOnly);
    str(t, "numberElev",    &st.surveyLabelTemplates.numberElev);
    str(t, "numberElevDesc",     &st.surveyLabelTemplates.numberElevDesc);
    str(t, "numberNorthEast",   &st.surveyLabelTemplates.numberNorthEast);
    str(t, "northEast",         &st.surveyLabelTemplates.northEast);
    str(t, "numberNorthEastElev", &st.surveyLabelTemplates.numberNorthEastElev);
  }
  num("surveyLabelLeaderArrowPx", &st.surveyLabelLeaderArrowPx, 2.f, 30.f);

  // --- Text / MTEXT screen sizes (User Preferences tab) ---
  num("viewportTextMinPx",  &st.viewportTextMinPx,  4.f,  48.f);
  num("viewportTextMaxPx",  &st.viewportTextMaxPx,  24.f, 320.f);
  num("viewportMtextMinPx", &st.viewportMtextMinPx, 4.f,  48.f);
  num("viewportMtextMaxPx", &st.viewportMtextMaxPx, 24.f, 320.f);

  // --- Dimension settings (User Preferences tab) ---
  num("viewportDimExtLinePx",  &st.viewportDimExtLinePx,  0.25f, 8.f);
  num("viewportDimDimLinePx",  &st.viewportDimDimLinePx,  0.25f, 8.f);
  num("viewportDimArrowScale", &st.viewportDimArrowScale,  0.2f, 4.f);
  num("viewportDimTextMinPx",  &st.viewportDimTextMinPx,  4.f,  48.f);
  num("viewportDimTextMaxPx",  &st.viewportDimTextMaxPx,  24.f, 320.f);

  // --- Panel focus restoration ---
  if (s.contains("focusedSidePanel") && s["focusedSidePanel"].is_string()) {
    if (s["focusedSidePanel"].get<std::string>() == "Properties")
      st.pendingPropertiesFocus = true;
  }

  // --- Right-click behavior (Drafting tab legacy + User Preferences) ---
  b  ("rightClickRepeatLastCommand", &st.rightClickRepeatLastCommand);
  auto u8clamped = [&](const char* k, uint8_t* out, uint8_t hi) {
    if (s.contains(k) && s[k].is_number_unsigned())
      *out = static_cast<uint8_t>(std::min(s[k].get<unsigned>(), static_cast<unsigned>(hi)));
  };
  uint8_t tmp = 0;
  tmp = static_cast<uint8_t>(st.rightClickDefaultMode);
  u8clamped("rightClickDefaultMode",  &tmp, 1); st.rightClickDefaultMode  = static_cast<AppCommandState::RightClickDefaultMode>(tmp);
  tmp = static_cast<uint8_t>(st.rightClickEditMode);
  u8clamped("rightClickEditMode",     &tmp, 1); st.rightClickEditMode     = static_cast<AppCommandState::RightClickEditMode>(tmp);
  // Schema 1 corrects a shipped default: Edit Mode was RepeatLastCommand, which made the selection
  // shortcut menu (and with it Select similar) unreachable. Profiles written before schema 1 carry the old
  // default as if it were a choice, so the migration re-applies the compiled default once. Later profiles
  // keep whatever the user picked in Settings.
  if (!(s.contains("prefsSchemaVersion") && s["prefsSchemaVersion"].is_number_integer() &&
        s["prefsSchemaVersion"].get<int>() >= 1))
    st.rightClickEditMode = AppCommandState::RightClickEditMode::ShortcutMenu;
  tmp = static_cast<uint8_t>(st.rightClickCommandMode);
  u8clamped("rightClickCommandMode",  &tmp, 2); st.rightClickCommandMode  = static_cast<AppCommandState::RightClickCommandMode>(tmp);

  // Time-sensitive right-click (REQ-084). A profile written before this requirement has neither
  // key, so both keep the compiled defaults — which is exactly the "off by default, behaviour
  // unchanged on upgrade" the requirement asks for. No schema bump is needed for that reason.
  b("rightClickTimeSensitive", &st.rightClickTimeSensitive);
  if (s.contains("rightClickLongerClickMs") && s["rightClickLongerClickMs"].is_number_integer())
    st.rightClickLongerClickMs = std::clamp(s["rightClickLongerClickMs"].get<int>(),
                                            AppCommandState::kRightClickMinMs,
                                            AppCommandState::kRightClickMaxMs);

  // --- TRIMSTATE (REQ-056): 0 = draw a line to trim, 1 = pick cutting edges ---
  if (s.contains("trimState") && s["trimState"].is_number_integer())
    st.trimState = std::clamp(s["trimState"].get<int>(), 0, 1);

  // --- Active ribbon tab (REQ-302): intentionally NOT restored. The Home tab is
  // always focused on launch regardless of which tab was active at exit.
  st.activeRibbonTab = kRibbonTabHome;

  // --- FILLET/CHAMFER (REQ-103 step 6): app-level, not per-drawing — same shape as trimState above
  // (D-2026-08-24-g: a generalized "registry" was considered and explicitly declined) ---
  if (s.contains("filletRadius") && s["filletRadius"].is_number())
    st.filletRadius = std::max(0.f, s["filletRadius"].get<float>());
  if (s.contains("cornerTrimMode") && s["cornerTrimMode"].is_number_integer())
    st.cornerTrimMode = std::clamp(s["cornerTrimMode"].get<int>(), 0, 1);
  if (s.contains("chamferDist1") && s["chamferDist1"].is_number())
    st.chamferDist1 = std::max(0.f, s["chamferDist1"].get<float>());
  if (s.contains("chamferDist2") && s["chamferDist2"].is_number())
    st.chamferDist2 = std::max(0.f, s["chamferDist2"].get<float>());
  if (s.contains("chamferAngle") && s["chamferAngle"].is_number())
    st.chamferAngle = std::clamp(s["chamferAngle"].get<float>(), 0.01f, 179.99f);
  if (s.contains("chamferMode") && s["chamferMode"].is_number_integer())
    st.chamferMode = std::clamp(s["chamferMode"].get<int>(), 0, 1);

  // --- Update check (REQ-077) ---
  if (s.contains("updateCheckEnabled") && s["updateCheckEnabled"].is_boolean())
    st.updatePrefs.enabled = s["updateCheckEnabled"].get<bool>();
  if (s.contains("updateUseBetaChannel") && s["updateUseBetaChannel"].is_boolean())
    st.updatePrefs.useBetaChannel = s["updateUseBetaChannel"].get<bool>();
  if (s.contains("updateSkippedVersion") && s["updateSkippedVersion"].is_string())
    st.updatePrefs.skippedVersion = s["updateSkippedVersion"].get<std::string>();
  // `updateLastCheckUnix` was the 24-hour throttle anchor and is no longer read or written
  // (REQ-077 amended: every launch checks). A key left behind in an existing prefs file is
  // simply ignored, so no migration is needed.

  // --- Undo/Redo ---
  if (s.contains("undoHistoryMaxSize") && s["undoHistoryMaxSize"].is_number_integer())
    st.undoHistoryMaxSize = std::clamp(s["undoHistoryMaxSize"].get<int>(), 1, 200);

  // --- Floating command bar (REQ-040) ---
  b  ("cmdLineClassicDock", &st.cmdLineClassicDock);
  b  ("cmdBarVisible",      &st.cmdBarVisible);
  b  ("cmdBarAnchorValid",  &st.cmdBarAnchorValid);
  num("cmdBarAnchorX",      &st.cmdBarAnchorX, -100000.f, 100000.f);
  num("cmdBarAnchorY",      &st.cmdBarAnchorY, -100000.f, 100000.f);
  num("cmdBarWidth",        &st.cmdBarWidth, 0.f, 100000.f);
  num("cmdConsoleHeight",   &st.cmdConsoleHeight, 0.f, 100000.f);
  num("cmdBarFadeDelaySec", &st.cmdBarFadeDelaySec, 0.5f, 60.f);
  num("cmdBarOpacity",      &st.cmdBarOpacity, 0.3f, 1.f);
  if (s.contains("cmdBarHistoryLines") && s["cmdBarHistoryLines"].is_number_integer())
    st.cmdBarHistoryLines = std::clamp(s["cmdBarHistoryLines"].get<int>(), 1, 20);

  // --- MTEXT "Text Formatting" panel (REQ-051) ---
  b  ("mtextPanelAnchorValid",  &st.mtextPanelAnchorValid);
  num("mtextPanelAnchorX",      &st.mtextPanelAnchorX, -100000.f, 100000.f);
  num("mtextPanelAnchorY",      &st.mtextPanelAnchorY, -100000.f, 100000.f);
  b  ("mtextPanelRulerVisible", &st.mtextPanelRulerVisible);
  b  ("mtextPanelRow2Visible",  &st.mtextPanelRow2Visible);
  b  ("mtextEditAutocorrectCapsLock", &st.mtextEditAutocorrectCapsLock);
  if (s.contains("mtextPanelRunColor") && s["mtextPanelRunColor"].is_number_integer())
    st.mtextPanelRunColor = s["mtextPanelRunColor"].get<unsigned int>() & 0xFFFFFFu;

  // --- Visual style (REQ-064) --- range-checked, so a bad value falls back to the default style.
  if (s.contains("viewportVisualStyle") && s["viewportVisualStyle"].is_number_integer()) {
    const int vsRaw = s["viewportVisualStyle"].get<int>();
    st.viewportVisualStyle = (vsRaw >= 0 && vsRaw <= static_cast<int>(VisualStyle::Shaded))
                                 ? static_cast<VisualStyle>(vsRaw)
                                 : VisualStyle::Wireframe2D;
  }

  // --- Object snap (Drafting tab) ---
  b  ("objectSnapEnabled",         &st.objectSnapEnabled);
  b  ("objectSnapEndpoint",        &st.objectSnapEndpoint);
  b  ("objectSnapMidpoint",        &st.objectSnapMidpoint);
  b  ("objectSnapCenter",          &st.objectSnapCenter);
  b  ("objectSnapPerpendicular",   &st.objectSnapPerpendicular);
  b  ("objectSnapSurveyPoint",     &st.objectSnapSurveyPoint);
  b  ("objectSnapGeometricCenter", &st.objectSnapGeometricCenter);
  b  ("objectSnapIntersection", &st.objectSnapIntersection);
  b  ("objectSnapApparentIntersection", &st.objectSnapApparentIntersection);
  b  ("objectSnapSurface",             &st.objectSnapSurface);
  b  ("objectSnapSolid",               &st.objectSnapSolid);
  num("objectSnapAperturePx",      &st.objectSnapAperturePx,  4.f, 64.f);
  num("objectSnapGlyphHalfPx",     &st.objectSnapGlyphHalfPx, 3.f, 48.f);
  num("gripSizePx",                &st.gripSizePx,            2.f, 20.f);
}

} // namespace

void CopyUtf8PathCapped(char* dest, size_t cap, const char* utf8) {
  if (!dest || cap == 0)
    return;
  dest[0] = '\0';
  if (!utf8)
    return;
  const size_t maxCopy = cap - 1;
  const size_t n = strnlen(utf8, maxCopy + 1);
  const size_t len = std::min(n, maxCopy);
  if (len > 0)
    std::memcpy(dest, utf8, len);
  dest[len] = '\0';
}

void LoadUserStartupPrefs(AppCommandState& st) {
  st.defaultWorkspaceTemplatePathUtf8[0] = '\0';
  CopyUtf8PathCapped(st.activeUiLayoutNameUtf8, sizeof(st.activeUiLayoutNameUtf8), "default");
  const auto path = UserPrefsJsonPath();
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return;
  try {
    nlohmann::json j;
    f >> j;
    if (!j.is_object())
      return;
    if (j.contains("defaultWorkspaceTemplatePath") && j["defaultWorkspaceTemplatePath"].is_string()) {
      const std::string s = j["defaultWorkspaceTemplatePath"].get<std::string>();
      CopyUtf8PathCapped(st.defaultWorkspaceTemplatePathUtf8, sizeof(st.defaultWorkspaceTemplatePathUtf8), s.c_str());
    }
    if (j.contains("activeUiLayout") && j["activeUiLayout"].is_string()) {
      const std::string s = j["activeUiLayout"].get<std::string>();
      CopyUtf8PathCapped(st.activeUiLayoutNameUtf8, sizeof(st.activeUiLayoutNameUtf8), s.c_str());
    }
    if (j.contains("settings") && j["settings"].is_object())
      ApplyUserPrefsSettings(st, j["settings"]);
  } catch (...) {
  }
}

void LoadUserStartupPrefSettings(AppCommandState& st) {
  const auto path = UserPrefsJsonPath();
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return;
  try {
    nlohmann::json j;
    f >> j;
    if (!j.is_object())
      return;
    if (j.contains("settings") && j["settings"].is_object())
      ApplyUserPrefsSettings(st, j["settings"]);
  } catch (...) {
  }
}

bool SaveUserStartupPrefs(const AppCommandState& st) {
  const auto path = UserPrefsJsonPath();
  nlohmann::json j = nlohmann::json::object();
  try {
    if (std::ifstream inf(path, std::ios::binary); inf)
      inf >> j;
  } catch (...) {
    j = nlohmann::json::object();
  }
  if (!j.is_object())
    j = nlohmann::json::object();

  j["defaultWorkspaceTemplatePath"] = std::string(st.defaultWorkspaceTemplatePathUtf8);
  j["activeUiLayout"] = std::string(st.activeUiLayoutNameUtf8);

  nlohmann::json s = nlohmann::json::object();

  // Display / System settings
  s["displayColorThemeIdx"]        = st.displayColorThemeIdx;
  s["settingsActiveTabIdx"]        = st.settingsActiveTabIdx;
  s["displayArcCircleSmoothness"]  = st.displayArcCircleSmoothness;
  s["displayCrosshairSizePct"]     = st.displayCrosshairSizePct;
  s["displayWheelZoomFactor"]      = st.displayWheelZoomFactor;
  s["displayFadeXref"]             = st.displayFadeXref;
  s["displayFadeInPlace"]          = st.displayFadeInPlace;
  s["displayLinearPrecision"]      = st.displayLinearPrecision;
  s["angleDisplayType"]            = st.angleDisplayType;
  s["angleDisplayPrecision"]       = st.angleDisplayPrecision;
  s["angleDisplayClockwise"]       = st.angleDisplayClockwise;
  s["angleDisplayBaseDeg"]         = st.angleDisplayBaseDeg;
  s["systemHardwareAcceleration"]  = st.systemHardwareAcceleration;
  s["systemPreferIntegratedGpu"]   = st.systemPreferIntegratedGpu;
  s["gfxSmoothLineDisplay"]        = st.gfxSmoothLineDisplay;
  s["gfxAcceleratedFontDisplay"]   = st.gfxAcceleratedFontDisplay;
  s["gfxVideoMemoryCachingLevel"]  = st.gfxVideoMemoryCachingLevel;

  // Crosshair appearance
  s["viewportCrosshairR"]          = st.viewportCrosshairR;
  s["viewportCrosshairG"]          = st.viewportCrosshairG;
  s["viewportCrosshairB"]          = st.viewportCrosshairB;
  s["viewportCrosshairHairPx"]     = st.viewportCrosshairHairPx;

  // Viewport background
  s["viewportBgR"]                 = st.viewportBgR;
  s["viewportBgG"]                 = st.viewportBgG;
  s["viewportBgB"]                 = st.viewportBgB;

  // Survey point settings
  s["surveyPointCrossSpanPlottedInches"]   = st.surveyPointCrossSpanPlottedInches;
  s["surveyPointShowIdInViewport"]         = st.surveyPointShowIdInViewport;
  s["surveyPointDisplayPrecision"]         = st.surveyPointDisplayPrecision;
  s["surveyPointLabelPlottedHeightInches"] = st.surveyPointLabelPlottedHeightInches;
  s["surveyLabelOffsetEastPlottedIn"]      = st.surveyLabelOffsetEastPlottedIn;
  s["surveyLabelOffsetNorthPlottedIn"]     = st.surveyLabelOffsetNorthPlottedIn;
  nlohmann::json tpl;
  tpl["numberDesc"]     = st.surveyLabelTemplates.numberDesc;
  tpl["numberOnly"]     = st.surveyLabelTemplates.numberOnly;
  tpl["descOnly"]       = st.surveyLabelTemplates.descOnly;
  tpl["numberElev"]     = st.surveyLabelTemplates.numberElev;
  tpl["numberElevDesc"]      = st.surveyLabelTemplates.numberElevDesc;
  tpl["numberNorthEast"]    = st.surveyLabelTemplates.numberNorthEast;
  tpl["northEast"]          = st.surveyLabelTemplates.northEast;
  tpl["numberNorthEastElev"] = st.surveyLabelTemplates.numberNorthEastElev;
  s["surveyLabelTemplates"] = std::move(tpl);
  s["surveyLabelLeaderArrowPx"] = st.surveyLabelLeaderArrowPx;

  // Text / MTEXT / Dimension sizes
  s["viewportTextMinPx"]      = st.viewportTextMinPx;
  s["viewportTextMaxPx"]      = st.viewportTextMaxPx;
  s["viewportMtextMinPx"]     = st.viewportMtextMinPx;
  s["viewportMtextMaxPx"]     = st.viewportMtextMaxPx;
  s["viewportDimExtLinePx"]   = st.viewportDimExtLinePx;
  s["viewportDimDimLinePx"]   = st.viewportDimDimLinePx;
  s["viewportDimArrowScale"]  = st.viewportDimArrowScale;
  s["viewportDimTextMinPx"]   = st.viewportDimTextMinPx;
  s["viewportDimTextMaxPx"]   = st.viewportDimTextMaxPx;

  // Panel focus restoration
  s["focusedSidePanel"] = st.propertiesPanelActive ? "Properties" : "Reports";

  // Bumped when a shipped default is corrected and existing profiles must pick the new value up once
  // (see the load side). 1 = Edit Mode right-click opens the selection shortcut menu.
  s["prefsSchemaVersion"] = 1;

  s["trimState"] = st.trimState;
  {
    int tab = st.activeRibbonTab;
    if (tab == kRibbonTabSurfaceCtx)
      tab = st.ribbonTabBeforeSurfaceCtx;
    if (tab == kRibbonTabSurveyPointCtx)
      tab = st.ribbonTabBeforeSurveyPointCtx;
    s["activeRibbonTab"] = std::clamp(tab, 0, kRibbonTabCount - 1);
  }
  s["filletRadius"] = st.filletRadius;
  s["cornerTrimMode"] = st.cornerTrimMode;
  s["chamferDist1"] = st.chamferDist1;
  s["chamferDist2"] = st.chamferDist2;
  s["chamferAngle"] = st.chamferAngle;
  s["chamferMode"] = st.chamferMode;

  // Update check (REQ-077)
  s["updateCheckEnabled"]   = st.updatePrefs.enabled;
  s["updateUseBetaChannel"] = st.updatePrefs.useBetaChannel;
  s["updateSkippedVersion"] = st.updatePrefs.skippedVersion;

  // Right-click behavior
  s["rightClickRepeatLastCommand"] = st.rightClickRepeatLastCommand;
  s["rightClickDefaultMode"]       = static_cast<uint8_t>(st.rightClickDefaultMode);
  s["rightClickEditMode"]          = static_cast<uint8_t>(st.rightClickEditMode);
  s["rightClickCommandMode"]       = static_cast<uint8_t>(st.rightClickCommandMode);
  s["rightClickTimeSensitive"]     = st.rightClickTimeSensitive;   // REQ-084
  s["rightClickLongerClickMs"]     = st.rightClickLongerClickMs;   // REQ-084

  // Object snap
  // Undo/Redo
  s["undoHistoryMaxSize"]         = st.undoHistoryMaxSize;

  // Floating command bar (REQ-040)
  s["cmdLineClassicDock"] = st.cmdLineClassicDock;
  s["cmdBarVisible"]      = st.cmdBarVisible;
  s["cmdBarAnchorValid"]  = st.cmdBarAnchorValid;
  s["cmdBarAnchorX"]      = st.cmdBarAnchorX;
  s["cmdBarAnchorY"]      = st.cmdBarAnchorY;
  s["cmdBarWidth"]        = st.cmdBarWidth;
  s["cmdConsoleHeight"]   = st.cmdConsoleHeight;
  s["cmdBarFadeDelaySec"] = st.cmdBarFadeDelaySec;
  s["cmdBarOpacity"]      = st.cmdBarOpacity;
  s["cmdBarHistoryLines"] = st.cmdBarHistoryLines;

  // MTEXT "Text Formatting" panel (REQ-051)
  s["mtextPanelAnchorValid"]  = st.mtextPanelAnchorValid;
  s["mtextPanelAnchorX"]      = st.mtextPanelAnchorX;
  s["mtextPanelAnchorY"]      = st.mtextPanelAnchorY;
  s["mtextPanelRulerVisible"] = st.mtextPanelRulerVisible;
  s["mtextPanelRow2Visible"]  = st.mtextPanelRow2Visible;
  s["mtextEditAutocorrectCapsLock"] = st.mtextEditAutocorrectCapsLock;
  s["mtextPanelRunColor"]     = st.mtextPanelRunColor;

  s["objectSnapEnabled"]          = st.objectSnapEnabled;
  s["objectSnapEndpoint"]         = st.objectSnapEndpoint;
  s["objectSnapMidpoint"]         = st.objectSnapMidpoint;
  s["objectSnapCenter"]           = st.objectSnapCenter;
  s["objectSnapPerpendicular"]    = st.objectSnapPerpendicular;
  s["objectSnapSurveyPoint"]      = st.objectSnapSurveyPoint;
  s["objectSnapGeometricCenter"]  = st.objectSnapGeometricCenter;
  s["viewportVisualStyle"]        = static_cast<int>(st.viewportVisualStyle);
  s["objectSnapIntersection"]     = st.objectSnapIntersection;
  s["objectSnapApparentIntersection"] = st.objectSnapApparentIntersection;
  s["objectSnapSurface"]              = st.objectSnapSurface;
  s["objectSnapSolid"]                = st.objectSnapSolid;
  s["objectSnapAperturePx"]       = st.objectSnapAperturePx;
  s["objectSnapGlyphHalfPx"]      = st.objectSnapGlyphHalfPx;
  s["gripSizePx"]                 = st.gripSizePx;

  j["settings"] = std::move(s);
  try {
    if (const auto dir = path.parent_path(); !dir.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
    }
    std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f)
      return false;
    f << j.dump(2);
    return f.good();
  } catch (...) {
    return false;
  }
}

TelemetryIds GetTelemetryIds() {
  TelemetryIds result;
  const auto path = UserPrefsJsonPath();
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return result;

  try {
    nlohmann::json j;
    f >> j;
    if (!j.is_object())
      return result;

    if (j.contains("installId") && j["installId"].is_string())
      result.installId = j["installId"].get<std::string>();
  } catch (...) {
  }

  return result;
}

bool UpdateTelemetryIds(const std::string& installId) {
  const auto path = UserPrefsJsonPath();
  nlohmann::json j = nlohmann::json::object();

  try {
    if (std::ifstream inf(path, std::ios::binary); inf)
      inf >> j;
  } catch (...) {
    j = nlohmann::json::object();
  }

  if (!j.is_object())
    j = nlohmann::json::object();

  if (!installId.empty())
    j["installId"] = installId;

  try {
    if (const auto dir = path.parent_path(); !dir.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
    }
    std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f)
      return false;
    f << j.dump(2);
    return f.good();
  } catch (...) {
    return false;
  }
}
