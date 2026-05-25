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
  const std::filesystem::path exeDir = AppExecutableDirectory();
  if (exeDir.empty())
    return std::filesystem::path("gosurvey-user.json");
  return exeDir / "gosurvey-user.json";
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
    // Display / System settings (AutoCAD Options analog). Each is wrapped in a type check + sensible clamp so a
    // corrupted file still loads safely.
    if (j.contains("settings") && j["settings"].is_object()) {
      const auto& s = j["settings"];
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
      if (s.contains("displayFadeXref") && s["displayFadeXref"].is_number_integer())
        st.displayFadeXref = std::clamp(s["displayFadeXref"].get<int>(), 0, 90);
      if (s.contains("displayFadeInPlace") && s["displayFadeInPlace"].is_number_integer())
        st.displayFadeInPlace = std::clamp(s["displayFadeInPlace"].get<int>(), 0, 90);
      if (s.contains("systemHardwareAcceleration") && s["systemHardwareAcceleration"].is_boolean())
        st.systemHardwareAcceleration = s["systemHardwareAcceleration"].get<bool>();
      if (s.contains("gfxSmoothLineDisplay") && s["gfxSmoothLineDisplay"].is_boolean())
        st.gfxSmoothLineDisplay = s["gfxSmoothLineDisplay"].get<bool>();
      if (s.contains("gfxAcceleratedFontDisplay") && s["gfxAcceleratedFontDisplay"].is_boolean())
        st.gfxAcceleratedFontDisplay = s["gfxAcceleratedFontDisplay"].get<bool>();
      if (s.contains("gfxVideoMemoryCachingLevel") && s["gfxVideoMemoryCachingLevel"].is_number_integer())
        st.gfxVideoMemoryCachingLevel = std::clamp(s["gfxVideoMemoryCachingLevel"].get<int>(), 1, 5);
    }
  } catch (...) {
  }
}

void SaveUserStartupPrefs(const AppCommandState& st) {
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
  s["settingsActiveTabIdx"] = st.settingsActiveTabIdx;
  s["displayArcCircleSmoothness"] = st.displayArcCircleSmoothness;
  s["displayCrosshairSizePct"] = st.displayCrosshairSizePct;
  s["displayFadeXref"] = st.displayFadeXref;
  s["displayFadeInPlace"] = st.displayFadeInPlace;
  s["systemHardwareAcceleration"] = st.systemHardwareAcceleration;
  s["gfxSmoothLineDisplay"] = st.gfxSmoothLineDisplay;
  s["gfxAcceleratedFontDisplay"] = st.gfxAcceleratedFontDisplay;
  s["gfxVideoMemoryCachingLevel"] = st.gfxVideoMemoryCachingLevel;
  j["settings"] = std::move(s);
  try {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
      return;
    f << j.dump(2);
  } catch (...) {
  }
}
