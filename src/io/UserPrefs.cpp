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
  try {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
      return;
    f << j.dump(2);
  } catch (...) {
  }
}
