#include "ImGuiLayout.hpp"

#include "AppIcon.hpp"
#include "CadCommands.hpp"
#include "UserPrefs.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>

static std::string g_iniAbsPathUtf8;
static bool g_deferredIniLoadPending = false;

namespace {

std::filesystem::path LayoutsRootDirectory() {
  namespace fs = std::filesystem;
  const fs::path exeDir = AppExecutableDirectory();
  if (!exeDir.empty())
    return exeDir / "resources" / "layouts";
  return fs::current_path() / "resources" / "layouts";
}

void EnsureLayoutsRootExists() {
  std::error_code ec;
  std::filesystem::create_directories(LayoutsRootDirectory(), ec);
}

std::string SanitizeLayoutStem(const char* utf8) {
  if (!utf8)
    return {};
  std::string s;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8); *p; ++p) {
    const unsigned char c = *p;
    if (std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.')
      s.push_back(static_cast<char>(c));
    else if (c == ' ')
      s.push_back('_');
  }
  while (!s.empty() && (s.front() == '.' || s.front() == '_'))
    s.erase(s.begin());
  while (!s.empty() && (s.back() == '.' || s.back() == '_'))
    s.pop_back();
  if (s.size() > 48)
    s.resize(48);
  return s;
}

} // namespace

bool ImGuiLayout_ConfigureIniPath(AppCommandState& st) {
  EnsureLayoutsRootExists();
  std::string stem = SanitizeLayoutStem(st.activeUiLayoutNameUtf8);
  if (stem.empty())
    stem = "default";
  CopyUtf8PathCapped(st.activeUiLayoutNameUtf8, sizeof(st.activeUiLayoutNameUtf8), stem.c_str());

  namespace fs = std::filesystem;
  const fs::path iniPath = LayoutsRootDirectory() / (stem + ".ini");
  g_iniAbsPathUtf8 = iniPath.u8string();
  g_deferredIniLoadPending = false;

  ImGui::GetIO().IniFilename = g_iniAbsPathUtf8.c_str();

  if (!fs::exists(iniPath))
    return false;
  const auto sz = static_cast<std::uintmax_t>(fs::file_size(iniPath));
  return sz > 32u;
}

void ImGuiLayout_CommitDeferredIniLoadIfNeeded() {
  // No-op: the deferred load mechanism was removed. IniFilename is set directly in
  // ImGuiLayout_ConfigureIniPath so ImGui's built-in auto-load handles restoration.
  (void)g_deferredIniLoadPending;
}

void ImGuiLayout_ListLayoutStems(std::vector<std::string>* out_sorted_stems) {
  if (!out_sorted_stems)
    return;
  out_sorted_stems->clear();
  namespace fs = std::filesystem;
  const fs::path root = LayoutsRootDirectory();
  std::error_code ec;
  if (!fs::exists(root, ec))
    return;
  for (const fs::directory_entry& e : fs::directory_iterator(root, ec)) {
    if (!e.is_regular_file(ec))
      continue;
    const fs::path p = e.path();
    if (p.extension() != ".ini")
      continue;
    out_sorted_stems->push_back(p.stem().u8string());
  }
  std::sort(out_sorted_stems->begin(), out_sorted_stems->end());
  out_sorted_stems->erase(std::unique(out_sorted_stems->begin(), out_sorted_stems->end()), out_sorted_stems->end());
}

bool ImGuiLayout_SwitchToLayout(AppCommandState& st, const char* layout_stem_utf8, std::vector<std::string>& log) {
  std::string stem = SanitizeLayoutStem(layout_stem_utf8);
  if (stem.empty()) {
    log.push_back("Layout: empty name.");
    return false;
  }
  namespace fs = std::filesystem;
  const fs::path iniPath = LayoutsRootDirectory() / (stem + ".ini");
  if (!fs::exists(iniPath)) {
    log.push_back("Layout: file not found: " + iniPath.u8string());
    return false;
  }

  ImGuiIO& io = ImGui::GetIO();
  if (io.IniFilename && io.IniFilename[0])
    ImGui::SaveIniSettingsToDisk(io.IniFilename);

  g_deferredIniLoadPending = false;
  g_iniAbsPathUtf8 = iniPath.u8string();
  io.IniFilename = g_iniAbsPathUtf8.c_str();
  ImGui::LoadIniSettingsFromDisk(g_iniAbsPathUtf8.c_str());

  CopyUtf8PathCapped(st.activeUiLayoutNameUtf8, sizeof(st.activeUiLayoutNameUtf8), stem.c_str());
  SaveUserStartupPrefs(st);
  log.push_back("UI layout: switched to \"" + stem + "\" (" + g_iniAbsPathUtf8 + ").");
  return true;
}

bool ImGuiLayout_SaveCurrentLayoutAs(AppCommandState& st, const char* layout_stem_utf8, std::vector<std::string>& log) {
  std::string stem = SanitizeLayoutStem(layout_stem_utf8);
  if (stem.empty()) {
    log.push_back("Layout: enter a name (letters, digits, dash, underscore).");
    return false;
  }
  EnsureLayoutsRootExists();
  namespace fs = std::filesystem;
  const fs::path iniPath = LayoutsRootDirectory() / (stem + ".ini");

  ImGui::SaveIniSettingsToDisk(iniPath.u8string().c_str());

  g_deferredIniLoadPending = false;
  g_iniAbsPathUtf8 = iniPath.u8string();
  ImGui::GetIO().IniFilename = g_iniAbsPathUtf8.c_str();

  CopyUtf8PathCapped(st.activeUiLayoutNameUtf8, sizeof(st.activeUiLayoutNameUtf8), stem.c_str());
  SaveUserStartupPrefs(st);
  log.push_back("UI layout: saved as \"" + stem + "\" (" + g_iniAbsPathUtf8 + ").");
  return true;
}

void ImGuiLayout_DrawViewLayoutMenu(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!ImGui::BeginMenu("Layout"))
    return;
  if (ImGui::MenuItem("Save current as…")) {
    cmd.openSaveLayoutAsPopup = true;
    cmd.saveLayoutAsNameBufUtf8[0] = '\0';
  }
  ImGui::Separator();
  {
    std::vector<std::string> stems;
    ImGuiLayout_ListLayoutStems(&stems);
    if (stems.empty())
      ImGui::MenuItem("(no saved layouts yet)", nullptr, false, false);
    else {
      if (ImGui::BeginMenu("Switch to")) {
        for (const std::string& s : stems) {
          const bool active = std::string(cmd.activeUiLayoutNameUtf8) == s;
          if (ImGui::MenuItem(s.c_str(), nullptr, active) && !active)
            ImGuiLayout_SwitchToLayout(cmd, s.c_str(), log);
        }
        ImGui::EndMenu();
      }
    }
  }
  ImGui::EndMenu();
}

void ImGuiLayout_DrawLayoutPopups(AppCommandState& cmd, std::vector<std::string>& log) {
  if (cmd.openSaveLayoutAsPopup) {
    ImGui::OpenPopup("Save layout as##GoSurvey");
    cmd.openSaveLayoutAsPopup = false;
  }

  ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);
  if (ImGui::BeginPopupModal("Save layout as##GoSurvey", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Name is stored as <name>.ini under resources/layouts next to the executable.");
    ImGui::Spacing();
    ImGui::InputText("Layout name##save_layout", cmd.saveLayoutAsNameBufUtf8, sizeof(cmd.saveLayoutAsNameBufUtf8));
    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(120, 0))) {
      if (ImGuiLayout_SaveCurrentLayoutAs(cmd, cmd.saveLayoutAsNameBufUtf8, log))
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}
