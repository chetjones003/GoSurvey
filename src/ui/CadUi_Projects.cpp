// REQ-374 / REQ-382 (issue #696 P1) — project glue between the pure gsproj module and the app:
// the open-project registry, join-on-open, the lock prompts, the New Project dialog, and the
// Recent Projects store path. The file format, join detection and lock mechanics live in
// src/io/Project.cpp so they are unit-tested without a window.

#include "CadUi.hpp"

#include "AppIcon.hpp"  // UserDataDirectory
#include "PdfAttachDialog.hpp"
#include "ProjectAddFlow.hpp"
#include "ProjectFiles.hpp"
#include "ProjectPoints.hpp"
#include "ProjectSettings.hpp"
#include "RecentDrawings.hpp"
#include "WinFileDialogs.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
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

// REQ-380 clause 3: unpack into an empty folder, then open the project.
void OpenPackedProject(AppCommandState& cmd, std::vector<std::string>& log, const char* packPathUtf8,
                       const char* destFolderUtf8) {
  char packBuf[4096]{};
  char destBuf[4096]{};
  if (!packPathUtf8) {
    if (!BrowseOpenFileGspackUtf8(packBuf, sizeof(packBuf)))
      return;
    packPathUtf8 = packBuf;
  }
  if (!destFolderUtf8) {
    log.push_back("Open Packed Project - choose an empty folder to unpack into.");
    if (!BrowseFolderUtf8(destBuf, sizeof(destBuf))) {
      log.push_back("Open Packed Project cancelled.");
      return;
    }
    destFolderUtf8 = destBuf;
  }
  fs::path gsprojFile;
  gspack::Manifest manifest;
  std::string err;
  if (!gspack::ExtractPack(fs::u8path(packPathUtf8), fs::u8path(destFolderUtf8), &gsprojFile, &manifest, &err)) {
    log.push_back("Open Packed Project - " + err);
    return;
  }
  log.push_back("Open Packed Project - unpacked '" + manifest.projectName + "' into " + std::string(destFolderUtf8) + ".");
  if (!manifest.excluded.empty())
    log.push_back("Open Packed Project - " + std::to_string(manifest.excluded.size()) +
                  " file(s) were left out of this pack (point clouds); they show as unavailable.");
  OpenProjectFile(cmd, log, gsprojFile.u8string().c_str());
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

// REQ-378 (#696 P5): Add Drawing to Project. Asks for the drawing, shows the preview, and writes nothing
// until Add is pressed. Cancel drops the private copy, so the project and the original are unchanged.
void DrawAddDrawingModal(AppCommandState& cmd, std::vector<std::string>& log) {
  static bool openNext = false;
  if (cmd.addDrawingToProjectUid != 0) {
    const std::uint32_t uid = cmd.addDrawingToProjectUid;
    cmd.addDrawingToProjectUid = 0;
    char picked[4096]{};
    if (BrowseOpenFileDwgUtf8(picked, sizeof(picked))) {
      auto plan = std::make_shared<AddDrawingPlan>();
      PrepareAddDrawing(cmd, uid, picked, plan.get(), log);
      if (!plan->error.empty()) {
        log.push_back(plan->error);
      } else {
        cmd.addDrawingPlan = std::move(plan);
        openNext = true;
      }
    }
  }
  if (openNext) {
    openNext = false;
    ImGui::OpenPopup("Add Drawing to Project##adddrawing");
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  PushProductDialogAccent();
  if (!ImGui::BeginPopupModal("Add Drawing to Project##adddrawing", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    PopProductDialogAccent();
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();

  if (!cmd.addDrawingPlan) {
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    PopProductDialogAccent();
    return;
  }
  const std::shared_ptr<AddDrawingPlan> keep = cmd.addDrawingPlan;  // outlives a reset below
  AddDrawingPlan& plan = *keep;
  const projadd::Summary& sm = plan.summary;
  const ImVec4 warn(1.f, 0.75f, 0.3f, 1.f);
  const ImVec4 bad(1.f, 0.5f, 0.5f, 1.f);
  bool close = false;

  ImGui::TextWrapped("Drawing: %s", plan.sourcePath.c_str());
  ImGui::TextWrapped("Project: %s. The drawing is copied into the project's Drawings folder; the original is not changed.",
                     plan.projectName.c_str());
  ImGui::Separator();

  if (plan.conversion.Needed()) {
    if (plan.converted) {
      ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.f),
                         "Converted to the project's coordinate system and units (leftover error up to %.4f m).",
                         plan.conversion.residualMeters);
    } else {
      ImGui::TextColored(bad, "This drawing does not match the project:");
      if (plan.conversion.unitsDiffer)
        ImGui::BulletText("Its drawing unit differs from the project's.");
      if (plan.conversion.zoneDiffers)
        ImGui::BulletText("Its coordinate system differs from the project's.");
      ImGui::TextWrapped("It cannot be added as it is. Convert moves and scales everything in the copy into the "
                         "project's coordinate system and units; the original is not touched.");
      if (!plan.blockers.empty()) {
        ImGui::TextColored(bad, "Convert is not possible: this drawing holds objects that cannot be converted:");
        for (const std::string& b : plan.blockers)
          ImGui::BulletText("%s", b.c_str());
      } else if (!plan.conversion.ok) {
        ImGui::TextColored(bad, "Convert is not possible: %s", plan.conversion.error.c_str());
      } else if (plan.conversion.zoneDiffers) {
        ImGui::TextDisabled("Convert would leave an error of up to %.4f m at the edges of the drawing.",
                            plan.conversion.residualMeters);
      }
      ImGui::BeginDisabled(!plan.CanConvert());
      if (ImGui::Button("Convert drawing to the project"))
        ChooseAddConvert(cmd, &plan, log);
      ImGui::EndDisabled();
    }
    ImGui::Separator();
  }

  ImGui::Text("%zu point(s) found.", sm.total);
  ImGui::BulletText("%zu are new to the project.", sm.fresh);
  ImGui::BulletText("%zu numbers already exist (%zu identical, %zu differ).", sm.Existing(), sm.identical,
                    sm.differing);
  if (sm.duplicates > 0)
    ImGui::TextColored(warn, "%zu point number(s) appear twice in the drawing; only the first is used.", sm.duplicates);
  if (sm.identical > 0)
    ImGui::TextDisabled("Identical points are shared, not duplicated.");

  if (!sm.conflicts.empty()) {
    ImGui::Spacing();
    ImGui::TextWrapped("For each point number that exists with different data, choose what happens:");
    auto setAll = [&](projadd::Choice c) {
      for (const projadd::Conflict& k : sm.conflicts)
        plan.choices[k.id] = c;
    };
    if (ImGui::SmallButton("All: skip"))
      setAll(projadd::Choice::Skip);
    ImGui::SameLine();
    if (ImGui::SmallButton("All: overwrite"))
      setAll(projadd::Choice::Overwrite);
    ImGui::SameLine();
    if (ImGui::SmallButton("All: renumber"))
      setAll(projadd::Choice::Renumber);
    ImGui::BeginChild("##adddrawingconflicts", ImVec2(560.f, 180.f), true);
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(sm.conflicts.size()));
    while (clip.Step()) {
      for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
        const projadd::Conflict& k = sm.conflicts[static_cast<size_t>(i)];
        ImGui::PushID(k.id);
        ImGui::Text("#%d  project: %.3f, %.3f, %.3f %s   drawing: %.3f, %.3f, %.3f %s", k.id, k.existing.easting,
                    k.existing.northing, k.existing.elevation, k.existing.description.c_str(), k.incoming.easting,
                    k.incoming.northing, k.incoming.elevation, k.incoming.description.c_str());
        int sel = static_cast<int>(plan.choices.count(k.id) ? plan.choices[k.id] : projadd::Choice::Skip);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.f);
        if (ImGui::Combo("##choice", &sel, "Skip\0Overwrite\0Renumber\0"))
          plan.choices[k.id] = static_cast<projadd::Choice>(sel);
        ImGui::PopID();
      }
    }
    ImGui::EndChild();
  }

  if (!plan.overrides.empty()) {
    ImGui::Spacing();
    std::string list;
    for (const std::string& o : plan.overrides)
      list += (list.empty() ? "" : ", ") + o;
    ImGui::TextWrapped("These settings differ from the project's defaults and become this drawing's own overrides: %s.",
                       list.c_str());
  }
  if (sm.total > 0)
    ImGui::TextDisabled("The drawing will show exactly the points it brought.");
  ImGui::Spacing();

  ImGui::BeginDisabled(plan.Blocked());
  if (ImGui::Button("Add Drawing")) {
    AddDrawingResult res;
    if (CommitAddDrawing(cmd, plan, ImGui::GetTime(), &res, log)) {
      close = true;
      const std::string dest = res.destPath;
      const std::uint32_t uid = plan.projectUid;
      cmd.addDrawingPlan.reset();
      ImGui::CloseCurrentPopup();
      OpenDrawingInNewTabAs(cmd, log, dest.c_str(), {false, uid});
    } else if (!plan.error.empty()) {
      close = true;  // the failed write spoiled the private copy; start over
    }
  }
  ImGui::EndDisabled();
  if (plan.Blocked() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("The drawing's coordinate system or units differ from the project's.");
  ImGui::SameLine();
  if (!close && ImGui::Button("Cancel"))
    close = true;

  if (close && cmd.addDrawingPlan) {
    cmd.addDrawingPlan.reset();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  PopProductDialogAccent();
}

std::string SizeText(std::uint64_t bytes) {
  char buf[48];
  if (bytes >= 1024ull * 1024 * 1024)
    std::snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
  else if (bytes >= 1024ull * 1024)
    std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  else
    std::snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
  return buf;
}

// REQ-379 clause 1: the Copy / Link / Cancel question for a point cloud or PDF from outside the project.
void DrawProjectAttachModal(AppCommandState& cmd, std::vector<std::string>& log) {
  auto& pa = cmd.projectAttachPrompt;
  using Kind = AppCommandState::ProjectAttachPrompt::Kind;
  if (pa.openRequested) {
    ImGui::OpenPopup("Attach to project##projattach");
    pa.openRequested = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  PushProductDialogAccent();
  if (!ImGui::BeginPopupModal("Attach to project##projattach", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    PopProductDialogAccent();
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();

  const AppCommandState::ProjectSession* s = SessionByUid(cmd, pa.projectUid);
  const std::string name = fs::u8path(pa.sourcePath).filename().u8string();
  ImGui::TextWrapped("Attach \"%s\" (%s) to project %s.", name.c_str(), SizeText(pa.sizeBytes).c_str(),
                     s ? s->project.name.c_str() : "?");
  ImGui::Spacing();
  ImGui::TextWrapped("Copy puts the file in the project (%s) so it travels with the project when it is packed or emailed.",
                     pa.destRel.c_str());
  if (pa.reuse)
    ImGui::TextWrapped("An identical copy is already there; it will be used.");
  if (pa.sizeBytes >= projfiles::kLargeFileBytes)
    ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.1f, 1.f),
                       "This is a large file: copying it makes the project folder, and any pack of it, that much bigger.");
  ImGui::TextWrapped("Link leaves the file where it is. A linked file will NOT travel with the project.");
  ImGui::Spacing();

  int answer = 0;  // 1 copy, 2 link, 3 cancel
  if (ImGui::Button("Copy into project"))
    answer = 1;
  ImGui::SameLine();
  if (ImGui::Button("Link"))
    answer = 2;
  ImGui::SameLine();
  if (ImGui::Button("Cancel"))
    answer = 3;
  if (answer != 0) {
    const Kind kind = pa.kind;
    ImGui::CloseCurrentPopup();
    std::string finalPath;
    if (answer == 3) {
      pa = {};
      log.push_back("Attach cancelled.");
    } else if (ResolveProjectAttach(cmd, answer == 1, log, &finalPath)) {
      if (kind == Kind::PointCloud) {
        StartPointCloudImportAsync(cmd, finalPath, log);
      } else if (finalPath.size() < sizeof(cmd.pdfAttachFilePath)) {
        std::strcpy(cmd.pdfAttachFilePath, finalPath.c_str());
        StartPdfAttachBuild(cmd, log);
      } else {
        log.push_back("PDFATTACH - the file's path is too long; nothing was attached.");
      }
    }
  }
  ImGui::EndPopup();
  PopProductDialogAccent();
}

// REQ-379 clause 4: Project Health.
void DrawProjectHealthModal(AppCommandState& cmd, std::vector<std::string>& log) {
  if (cmd.projectHealthUid != 0 && !ImGui::IsPopupOpen("Project Health##projhealth"))
    ImGui::OpenPopup("Project Health##projhealth");
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  PushProductDialogAccent();
  if (!ImGui::BeginPopupModal("Project Health##projhealth", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    PopProductDialogAccent();
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();

  const AppCommandState::ProjectSession* s = SessionByUid(cmd, cmd.projectHealthUid);
  if (s == nullptr) {
    cmd.projectHealthUid = 0;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    PopProductDialogAccent();
    return;
  }
  const projfiles::Health h = ProjectHealthFor(cmd, cmd.projectHealthUid);
  ImGui::TextWrapped("Project %s", s->project.name.c_str());
  ImGui::Spacing();
  auto list = [](const char* title, const std::vector<std::string>& items) {
    if (items.empty())
      return;
    ImGui::TextUnformatted(title);
    for (const std::string& i : items)
      ImGui::BulletText("%s", i.c_str());
    ImGui::Spacing();
  };
  list("Linked files (will NOT travel with the project):", h.linked);
  list("Missing files:", h.missing);
  list("Files this version cannot reach:", h.unavailable);
  list("Unavailable (left out of the pack this project was opened from):", h.omitted);
  list("Drawings with unsaved changes:", h.unsaved);
  if (h.Clean())
    ImGui::TextWrapped("No problems found: every tracked file is in the project and every drawing is saved.");

  ImGui::BeginDisabled(h.linked.empty() || s->readOnly);
  if (ImGui::Button("Copy links into the project")) {
    size_t converted = 0;
    CopyProjectLinksIn(cmd, cmd.projectHealthUid, log, &converted);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Close")) {
    cmd.projectHealthUid = 0;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  PopProductDialogAccent();
}

// REQ-380 clauses 1-2: Pack Project — Health first, then the size, then the file.
void DrawProjectPackModal(AppCommandState& cmd, std::vector<std::string>& log) {
  auto& pp = cmd.projectPackPrompt;
  if (pp.projectUid != 0 && !ImGui::IsPopupOpen("Pack Project##projpack"))
    ImGui::OpenPopup("Pack Project##projpack");
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  PushProductDialogAccent();
  if (!ImGui::BeginPopupModal("Pack Project##projpack", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    PopProductDialogAccent();
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();

  AppCommandState::ProjectSession* s = nullptr;
  for (auto& open : cmd.openProjects)
    if (open.uid == pp.projectUid)
      s = &open;
  if (s == nullptr) {
    pp = {};
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    PopProductDialogAccent();
    return;
  }
  if (!pp.planned && !pp.planTried) {
    pp.planTried = true;
    FlushProjectPointDb(*s, log);  // the pack must hold the points as the drawings show them
    pp.planError.clear();
    pp.planned = gspack::PlanPack(s->project, fs::path(), &pp.plan, &pp.planError);
    if (!pp.planned && pp.planError.empty())
      pp.planError = "The project could not be listed.";
  }
  const projfiles::Health h = ProjectHealthFor(cmd, pp.projectUid);

  ImGui::TextWrapped("Pack project %s into one .gspack file you can email or share.", s->project.name.c_str());
  ImGui::Spacing();
  auto list = [](const char* title, const std::vector<std::string>& items) {
    if (items.empty())
      return;
    ImGui::TextUnformatted(title);
    for (const std::string& i : items)
      ImGui::BulletText("%s", i.c_str());
    ImGui::Spacing();
  };
  list("Linked files (will NOT be in the pack):", h.linked);
  list("Missing files (will NOT be in the pack):", h.missing);
  list("Files this version cannot reach:", h.unavailable);
  list("Drawings with unsaved changes (the pack holds the saved version):", h.unsaved);
  if (h.Clean())
    ImGui::TextWrapped("Project Health: no problems found.");
  else
    ImGui::Checkbox("Pack anyway, with the problems above", &pp.packAnyway);

  ImGui::BeginDisabled(h.linked.empty() || s->readOnly);
  if (ImGui::Button("Copy links into the project")) {
    size_t converted = 0;
    CopyProjectLinksIn(cmd, pp.projectUid, log, &converted);
    pp.planned = pp.planTried = false;  // re-plan
  }
  ImGui::EndDisabled();
  ImGui::Separator();

  if (!pp.planned) {
    ImGui::TextColored(ImVec4(0.85f, 0.3f, 0.2f, 1.f), "%s", pp.planError.c_str());
  } else {
    const std::uintmax_t shown = pp.excludePointClouds ? pp.plan.totalBytes - pp.plan.pointCloudBytes : pp.plan.totalBytes;
    ImGui::Text("%zu files, %s before compression.", pp.plan.files.size(), SizeText(shown).c_str());
    if (pp.plan.pointCloudBytes > 0) {
      ImGui::Text("Point clouds are %s of that.", SizeText(pp.plan.pointCloudBytes).c_str());
      ImGui::Checkbox("Leave point clouds out of the pack", &pp.excludePointClouds);
      if (pp.excludePointClouds)
        ImGui::TextWrapped("They will show as unavailable (not as errors) when the pack is opened.");
    }
    if (shown >= gspack::kEmailWarnBytes)
      ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.1f, 1.f),
                         "This is large for an email attachment (25 MB or more). Leaving point clouds out helps.");
  }
  ImGui::Spacing();

  ImGui::BeginDisabled(!pp.planned || (!h.Clean() && !pp.packAnyway));
  if (ImGui::Button("Pack...")) {
    char out[4096]{};
    const std::string defName = s->project.name + gspack::kExtension;
    if (BrowseSaveFileGspackUtf8(out, sizeof(out), defName.c_str())) {
      gspack::PackPlan plan;
      std::string err;
      gspack::PackOptions opt;
      opt.excludePointClouds = pp.excludePointClouds;
      opt.nowUnix = static_cast<std::int64_t>(std::time(nullptr));
      if (!gspack::PlanPack(s->project, fs::u8path(out), &plan, &err) ||
          !gspack::WritePack(s->project, plan, opt, fs::u8path(out), &err)) {
        log.push_back("Pack Project - " + err);
      } else {
        std::error_code ec;
        const std::uintmax_t packed = fs::file_size(fs::u8path(out), ec);
        log.push_back("Pack Project - wrote " + std::string(out) + " (" + SizeText(ec ? 0 : packed) + ").");
        pp = {};
        ImGui::CloseCurrentPopup();
      }
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    pp = {};
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  PopProductDialogAccent();
}

}  // namespace

void DrawProjectDialogs(AppCommandState& cmd, std::vector<std::string>& log) {
  ServiceProjects(cmd, log);
  SyncProjectPoints(cmd, log, ImGui::GetTime());  // REQ-376: project drawings <-> the shared point database
  EnforceProjectSettings(cmd, log);  // REQ-375: enforced zone/unit and inherited defaults, every frame
  DrawAddDrawingModal(cmd, log);
  DrawNewProjectModal(cmd, log);
  DrawProjectPromptModal(cmd, log);
  DrawProjectAttachModal(cmd, log);  // REQ-379
  DrawProjectHealthModal(cmd, log);
  DrawProjectPackModal(cmd, log);  // REQ-380
  if (cmd.openPackRequested) {
    cmd.openPackRequested = false;
    OpenPackedProject(cmd, log, nullptr, nullptr);
  }
}
