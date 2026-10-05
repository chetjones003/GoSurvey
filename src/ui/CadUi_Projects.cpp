// REQ-374 / REQ-382 (issue #696 P1) — project glue between the pure gsproj module and the app:
// the open-project registry, join-on-open, the lock prompts, the New Project dialog, and the
// Recent Projects store path. The file format, join detection and lock mechanics live in
// src/io/Project.cpp so they are unit-tested without a window.

#include "CadUi.hpp"

#include "AppIcon.hpp"  // UserDataDirectory
#include "ProjectPoints.hpp"
#include "ProjectSettings.hpp"
#include "RecentDrawings.hpp"
#include "WinFileDialogs.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <ctime>
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path RecentProjectsJsonPath() {
  const auto dir = UserDataDirectory();
  return dir.empty() ? fs::path("gosurvey-recent-projects.json") : dir / "gosurvey-recent-projects.json";
}

std::string EnvVar(const char* name) {
#if defined(_WIN32)
  char buf[256]{};
  const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
  return (n > 0 && n < sizeof(buf)) ? std::string(buf, n) : std::string();
#else
  const char* v = std::getenv(name);
  return v ? v : "";
#endif
}

gsproj::LockInfo MakeMe() {
  gsproj::LockInfo me;
  me.user = EnvVar("USERNAME");
  me.machine = EnvVar("COMPUTERNAME");
#if defined(_WIN32)
  me.pid = static_cast<std::uint32_t>(GetCurrentProcessId());
#endif
  me.sinceUnix = static_cast<std::int64_t>(std::time(nullptr));
  return me;
}

bool PidAlive(std::uint32_t pid) {
#if defined(_WIN32)
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h)
    return GetLastError() == ERROR_ACCESS_DENIED;  // exists, just not ours to open
  DWORD code = 0;
  const bool running = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
  CloseHandle(h);
  return running;
#else
  (void)pid;
  return true;
#endif
}

enum class LockMode { Normal, ReadOnly, TakeOver };

bool SamePath(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  return fs::equivalent(a, b, ec);
}

AppCommandState::ProjectSession* FindSession(AppCommandState& cmd, const fs::path& gsprojFile) {
  for (auto& s : cmd.openProjects)
    if (SamePath(s.project.file, gsprojFile))
      return &s;
  return nullptr;
}

std::string AbsUtf8(const fs::path& p) {
  std::error_code ec;
  const fs::path a = fs::absolute(p, ec);
  return (ec ? p : a).u8string();
}

/// Opens (or reuses) the session for \p gsprojFile. Returns its uid, or 0 when the open did not
/// finish: the lock is held (a prompt is queued for the user) or the file could not be read (logged).
std::uint32_t EnsureProjectOpen(AppCommandState& cmd, std::vector<std::string>& log, const fs::path& gsprojFile,
                                const std::string& dwgPath, LockMode mode) {
  if (auto* have = FindSession(cmd, gsprojFile))
    return have->uid;

  AppCommandState::ProjectSession s;
  std::string err;
  if (!gsproj::Load(gsprojFile, &s.project, &err)) {
    log.push_back("Project could not be opened (" + AbsUtf8(gsprojFile) + "): " + err);
    return 0;
  }
  s.settings = std::make_shared<ProjectSettings>();
  if (!ParseProjectSettings(s.project.settingsJson, s.settings.get(), &err))
    log.push_back("Project settings of " + s.project.name + " could not be read (" + err + "); none are enforced.");
  s.me = MakeMe();
  if (mode == LockMode::ReadOnly) {
    s.readOnly = true;
  } else if (gsproj::TryAcquire(s.project.file, s.me, nullptr) == gsproj::LockResult::Acquired) {
    // we are the editor
  } else if (mode == LockMode::TakeOver) {
    if (!gsproj::TakeOver(s.project.file, s.me)) {
      log.push_back("Could not take over the project lock for " + s.project.name);
      return 0;
    }
  } else {
    auto& pr = cmd.projectPrompt;
    pr = {};
    pr.kind = AppCommandState::ProjectPrompt::Kind::Locked;
    pr.gsprojPath = AbsUtf8(s.project.file);
    pr.dwgPath = dwgPath;
    // Re-reads the holder for the prompt. If the lock vanished in the meantime we just won it, so hand
    // it back and let the user's next click go through the normal path.
    if (gsproj::TryAcquire(s.project.file, s.me, &pr.holder) == gsproj::LockResult::Acquired)
      gsproj::Release(s.project.file, s.me);
    pr.stale = gsproj::IsStale(pr.holder, s.me, PidAlive);
    pr.openRequested = true;
    return 0;
  }
  s.uid = cmd.nextProjectUid++;
  const std::uint32_t uid = s.uid;
  OpenProjectPointDb(s, log);  // REQ-376: the project's one survey point database
  recent::Note(RecentProjectsJsonPath(), AbsUtf8(s.project.file), "", static_cast<std::int64_t>(std::time(nullptr)));
  cmd.openProjects.push_back(std::move(s));
  return uid;
}

const AppCommandState::ProjectSession* SessionByUid(const AppCommandState& cmd, std::uint32_t uid) {
  for (const auto& s : cmd.openProjects)
    if (s.uid == uid)
      return &s;
  return nullptr;
}

/// An empty drawing tab that belongs to project \p uid (Open Project / New Project land here).
void NewProjectDrawingTab(AppCommandState& cmd, std::vector<std::string>& log, std::uint32_t uid) {
  NewDrawingInTab(cmd, log);
  cmd.drawingTabs.back().projectUid = uid;
  if (const auto* s = SessionByUid(cmd, uid))
    log.push_back("Opened project " + s->project.name + (s->readOnly ? " (read-only)" : ""));
}

void OpenProjectResolved(AppCommandState& cmd, std::vector<std::string>& log, const fs::path& gsprojFile,
                         const std::string& dwgPath, LockMode mode) {
  const std::uint32_t uid = EnsureProjectOpen(cmd, log, gsprojFile, dwgPath, mode);
  if (uid == 0)
    return;
  if (dwgPath.empty())
    NewProjectDrawingTab(cmd, log, uid);
  else
    OpenDrawingInNewTabAs(cmd, log, dwgPath.c_str(), {false, uid});
}

}  // namespace

// ------------------------------------------------------------------------------------------------

bool ResolveProjectJoin(AppCommandState& cmd, std::vector<std::string>& log, const std::string& dwgPath,
                        std::uint32_t* projectUidOut) {
  *projectUidOut = 0;
  const gsproj::FindResult f = gsproj::FindProjectFor(fs::u8path(dwgPath));
  if (f.state == gsproj::FindState::None)
    return true;  // standalone: behaves exactly as before projects existed
  if (f.state == gsproj::FindState::Damaged) {
    auto& pr = cmd.projectPrompt;
    pr = {};
    pr.kind = AppCommandState::ProjectPrompt::Kind::Damaged;
    pr.gsprojPath = AbsUtf8(f.file);
    pr.dwgPath = dwgPath;
    pr.message = f.message;
    pr.openRequested = true;
    return false;
  }
  const std::uint32_t uid = EnsureProjectOpen(cmd, log, f.file, dwgPath, LockMode::Normal);
  if (uid == 0)
    return false;
  *projectUidOut = uid;
  return true;
}

void NoteProjectJoin(const AppCommandState& cmd, std::vector<std::string>& log, std::uint32_t projectUid) {
  if (const auto* s = SessionByUid(cmd, projectUid))
    log.push_back("Opened in project " + s->project.name + (s->readOnly ? " (read-only)" : ""));
}

std::string ProjectNameForTab(const AppCommandState& cmd, int tabIdx) {
  if (tabIdx < 0 || tabIdx >= static_cast<int>(cmd.drawingTabs.size()))
    return {};
  if (const auto* s = SessionByUid(cmd, cmd.drawingTabs[static_cast<size_t>(tabIdx)].projectUid))
    return s->project.name;
  return {};
}

bool ProjectIsReadOnlyForTab(const AppCommandState& cmd, int tabIdx) {
  if (tabIdx < 0 || tabIdx >= static_cast<int>(cmd.drawingTabs.size()))
    return false;
  const auto* s = SessionByUid(cmd, cmd.drawingTabs[static_cast<size_t>(tabIdx)].projectUid);
  return s != nullptr && s->readOnly;
}

void OpenProjectFile(AppCommandState& cmd, std::vector<std::string>& log, const char* gsprojPathUtf8) {
  char browsed[4096]{};
  if (!gsprojPathUtf8) {
    if (!BrowseOpenFileGsprojUtf8(browsed, sizeof(browsed)))
      return;
    gsprojPathUtf8 = browsed;
  }
  const fs::path file = fs::u8path(gsprojPathUtf8);
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) {
    log.push_back("Project not found: " + std::string(gsprojPathUtf8));
    recent::Remove(RecentProjectsJsonPath(), AbsUtf8(file));
    return;
  }
  if (FindSession(cmd, file)) {
    // Already open: show its first tab instead of making a second session.
    for (size_t i = 0; i < cmd.drawingTabs.size(); ++i)
      if (SessionByUid(cmd, cmd.drawingTabs[i].projectUid) == FindSession(cmd, file)) {
        cmd.activeDrawingIdx = cmd.prevDrawingIdx = static_cast<int>(i);
        cmd.pendingDrawingTabSwitch = true;
        return;
      }
  }
  OpenProjectResolved(cmd, log, file, std::string(), LockMode::Normal);
}

bool SaveProjectSettings(AppCommandState& cmd, std::uint32_t projectUid, const ProjectSettings& ps,
                         std::vector<std::string>& log) {
  for (auto& s : cmd.openProjects) {
    if (s.uid != projectUid)
      continue;
    if (s.readOnly) {
      log.push_back("Project settings - " + s.project.name + " is open read-only; nothing was changed.");
      return false;
    }
    gsproj::Project next = s.project;
    next.settingsJson = WriteProjectSettings(next.settingsJson, ps);
    std::string err;
    if (!gsproj::Save(next, &err)) {
      log.push_back("Project settings could not be saved (" + err + "); nothing was changed.");
      return false;
    }
    s.project = std::move(next);
    *s.settings = ps;
    log.push_back("Project settings saved: " + s.project.name);
    return true;
  }
  log.push_back("Project settings - the project is no longer open; nothing was changed.");
  return false;
}

void RemoveRecentProject(const std::string& absGsprojPath) {
  recent::Remove(RecentProjectsJsonPath(), absGsprojPath);
}

std::vector<recent::Entry> LoadRecentProjects() {
  return recent::Load(RecentProjectsJsonPath());
}

void ServiceProjects(AppCommandState& cmd, std::vector<std::string>& log) {
  for (size_t i = 0; i < cmd.openProjects.size();) {
    const std::uint32_t uid = cmd.openProjects[i].uid;
    const bool used = std::any_of(cmd.drawingTabs.begin(), cmd.drawingTabs.end(),
                                  [uid](const auto& t) { return t.projectUid == uid; });
    if (used) {
      ++i;
      continue;
    }
    FlushProjectPointDb(cmd.openProjects[i], log);  // REQ-376 clause 7: no edit is lost with the last tab
    if (!cmd.openProjects[i].readOnly)
      gsproj::Release(cmd.openProjects[i].project.file, cmd.openProjects[i].me);
    cmd.openProjects.erase(cmd.openProjects.begin() + static_cast<std::ptrdiff_t>(i));
  }
}

void ReleaseAllProjects(AppCommandState& cmd, std::vector<std::string>& log) {
  for (auto& s : cmd.openProjects) {
    FlushProjectPointDb(s, log);  // REQ-376 clause 7
    if (!s.readOnly)
      gsproj::Release(s.project.file, s.me);
  }
  cmd.openProjects.clear();
}

// ------------------------------------------------------------------------------------------------
// Dialogs

namespace {

std::string WhenText(std::int64_t unix) {
  if (unix <= 0)
    return "an unknown time";
  const std::time_t t = static_cast<std::time_t>(unix);
  std::tm tmv{};
#if defined(_WIN32)
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
  return buf;
}

void DrawProjectPromptModal(AppCommandState& cmd, std::vector<std::string>& log) {
  auto& pr = cmd.projectPrompt;
  using Kind = AppCommandState::ProjectPrompt::Kind;
  if (pr.openRequested) {
    ImGui::OpenPopup("Project##projprompt");
    pr.openRequested = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  PushProductDialogAccent();
  if (!ImGui::BeginPopupModal("Project##projprompt", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    PopProductDialogAccent();
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();

  const std::string gsproj = pr.gsprojPath;
  const std::string dwg = pr.dwgPath;
  bool close = false;
  if (pr.kind == Kind::Damaged) {
    ImGui::TextWrapped("The project file for this drawing is damaged or unreadable:");
    ImGui::TextWrapped("%s", gsproj.c_str());
    if (!pr.message.empty())
      ImGui::TextWrapped("%s", pr.message.c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("Open the drawing on its own (its survey points stay in the drawing), or cancel.");
    ImGui::Spacing();
    if (ImGui::Button("Open standalone")) {
      const std::string path = dwg;
      close = true;
      pr = {};
      ImGui::CloseCurrentPopup();
      OpenDrawingInNewTabAs(cmd, log, path.c_str(), {false, 0});
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      close = true;
  } else if (pr.kind == Kind::Locked) {
    const std::string who = pr.holder.user.empty() ? std::string("someone") : pr.holder.user;
    ImGui::TextWrapped("This project is being edited by %s%s%s since %s.", who.c_str(),
                       pr.holder.machine.empty() ? "" : " on ", pr.holder.machine.c_str(),
                       WhenText(pr.holder.sinceUnix).c_str());
    if (pr.stale)
      ImGui::TextWrapped("That program is no longer running, so the lock looks left over from a crash.");
    ImGui::Spacing();
    ImGui::TextWrapped("Read-only lets you view and print but not change the project.");
    ImGui::Spacing();
    LockMode choice = LockMode::Normal;
    bool chosen = false;
    if (ImGui::Button("Open read-only")) {
      choice = LockMode::ReadOnly;
      chosen = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(pr.stale ? "Take over (stale lock)" : "Take over anyway")) {
      choice = LockMode::TakeOver;
      chosen = true;
    }
    if (ImGui::IsItemHovered() && !pr.stale)
      ImGui::SetTooltip("Only do this if you are sure the other person is not editing.\n"
                        "Their unsaved work in this project could be overwritten.");
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      close = true;
    if (chosen) {
      close = true;
      pr = {};
      ImGui::CloseCurrentPopup();
      OpenProjectResolved(cmd, log, fs::u8path(gsproj), dwg, choice);
    }
  }
  if (close) {
    pr = {};
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  PopProductDialogAccent();
}

void DrawNewProjectModal(AppCommandState& cmd, std::vector<std::string>& log) {
  static std::string name;
  static std::string location;
  static std::string error;
  static int         unitSel = 2;  // index into kDrawingUnitNames; Feet
  if (cmd.showNewProjectDialog) {
    cmd.showNewProjectDialog = false;
    name.clear();
    for (int i = 0; i < kDrawingUnitCount; ++i)
      if (kDrawingUnitCodes[i] == 2)
        unitSel = i;
    error.clear();
    if (location.empty()) {
      const std::string home = EnvVar("USERPROFILE");
      if (!home.empty())
        location = (fs::u8path(home) / "Documents").u8string();
    }
    ImGui::OpenPopup("New Project##newproj");
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  PushProductDialogAccent();
  if (!ImGui::BeginPopupModal("New Project##newproj", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    PopProductDialogAccent();
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();

  ImGui::SetNextItemWidth(360.f);
  ImGui::InputText("Name", &name);
  ImGui::SetNextItemWidth(360.f);
  ImGui::InputText("Location", &location);
  ImGui::SameLine();
  if (ImGui::Button("Browse...")) {
    char dir[4096]{};
    if (BrowseFolderUtf8(dir, sizeof(dir)))
      location = dir;
  }
  ImGui::SetNextItemWidth(360.f);
  ImGui::Combo("Linear units", &unitSel, kDrawingUnitNames, kDrawingUnitCount);
  ImGui::TextDisabled("Every drawing in the project uses these units. The coordinate system is chosen next.");
  if (!name.empty() && !location.empty())
    ImGui::TextDisabled("Creates %s", (fs::u8path(location) / fs::u8path(name)).u8string().c_str());
  if (!error.empty())
    ImGui::TextColored(ImVec4(1.f, 0.5f, 0.5f, 1.f), "%s", error.c_str());
  ImGui::Spacing();

  ImGui::BeginDisabled(name.empty() || location.empty());
  if (ImGui::Button("Create")) {
    gsproj::Project p;
    if (!gsproj::Create(fs::u8path(location), name, &p, &error)) {
      log.push_back("New project failed: " + error);
    } else {
      // REQ-375: the new project fixes its unit and starts with the standard defaults.
      ProjectSettings ps;
      ps.insUnits = kDrawingUnitCodes[std::clamp(unitSel, 0, kDrawingUnitCount - 1)];
      ps.hasDefaults = true;
      p.settingsJson = WriteProjectSettings(p.settingsJson, ps);
      std::string saveErr;
      if (!gsproj::Save(p, &saveErr))
        log.push_back("New project: the unit could not be saved (" + saveErr + ").");
      ImGui::CloseCurrentPopup();
      OpenProjectResolved(cmd, log, p.file, std::string(), LockMode::Normal);
      if (const auto* made = FindSession(cmd, p.file)) {
        cmd.projectSettingsUid = made->uid;  // pick the coordinate system right away
        log.push_back("Choose the project's coordinate system in Project Settings (or leave it unset).");
      }
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel"))
    ImGui::CloseCurrentPopup();

  ImGui::EndPopup();
  PopProductDialogAccent();
}

}  // namespace

void DrawProjectDialogs(AppCommandState& cmd, std::vector<std::string>& log) {
  ServiceProjects(cmd, log);
  SyncProjectPoints(cmd, log, ImGui::GetTime());  // REQ-376: project drawings <-> the shared point database
  EnforceProjectSettings(cmd, log);  // REQ-375: enforced zone/unit and inherited defaults, every frame
  DrawNewProjectModal(cmd, log);
  DrawProjectPromptModal(cmd, log);
}
