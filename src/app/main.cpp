// NOMINMAX must precede EVERY header here, not just <windows.h> below: several of the
// includes that follow pull <windows.h> in themselves, and by the time this file's own
// include of it is reached the min/max macros are already defined and shadow std::max /
// std::min at their call sites. Same reason as PdfAttach.cpp and WinFrameControls.cpp.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "CadCommands.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include "CadRubberPreview.hpp"
#include "util/gizmooverlay.hpp"  // CadGizmoOverlay, constructed here (REQ-060)
#include "TransformPreview.hpp"
#include "CadUi.hpp"
#include "util/framewatch.hpp"
#include "PdfAttachDialog.hpp"
#include "ViewportRenderer.hpp"
#include "CadSnap.hpp"
#include "PdfAttach.hpp"
#include "SurveyPoints.hpp"
#include "AppIcon.hpp"
#include "AppPaths.hpp"
#include "GsIo.hpp"
#include "DwgIo.hpp"
#include "SplashScreen.hpp"
#ifdef GOSURVEY_DEVELOPER_SHELL
#include "DevShell.hpp"
#endif
#include "UserPrefs.hpp"
#include "ImGuiLayout.hpp"
#include "UpdateService.hpp"
#include "TelemetryService.hpp"
#include "AuthService.hpp"
#include "Version.hpp"

#include <chrono>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
// CommandLineToArgvW — see FirstExistingFileArgumentUtf8. Excluded by WIN32_LEAN_AND_MEAN.
#include <shellapi.h>
#endif

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

  /// Returns the first command-line argument naming an existing file, as UTF-8, or empty.
  ///
  /// BUG-012: the installer used to register `.gs` with `shell\open\command = "...GoSurvey.exe"
  /// "%1"` (removed by issue #264 — `.gs` is no longer an openable document), so Explorer passed
  /// the path — and `main()` took no arguments, so it was silently dropped and double-clicking a
  /// drawing opened an empty session. Still handling this so a path passed on the command line
  /// any other way (a shortcut, a shell verb someone adds later) keeps working.
  ///
  /// Reads the WIDE command line rather than adding `argc`/`argv` to `main`. `argv` is encoded in
  /// the process ANSI codepage, so a drawing under a path containing characters outside it would
  /// arrive mangled and fail to open — on a machine where the file plainly exists. The rest of the
  /// codebase is UTF-8 end to end, and this keeps that true from the first line of `main`.
  static std::string FirstExistingFileArgumentUtf8()
  {
#ifdef _WIN32
    int     argc  = 0;
    LPWSTR *argvW = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argvW)
      return std::string();

    std::string result;
    for (int i = 1; i < argc; ++i)   // [0] is our own exe path
    {
      const std::filesystem::path p(argvW[i]);
      std::error_code             ec;
      // is_regular_file, not exists: a directory argument is not something to open, and the
      // error_code overload keeps a permission failure from throwing during startup.
      if (!p.empty() && std::filesystem::is_regular_file(p, ec))
      {
        result = p.u8string();
        break;
      }
    }
    ::LocalFree(argvW);
    return result;
#else
    return std::string();
#endif
  }

  static void TryLoadStartupWorkspaceTemplate(AppCommandState &cmd, std::vector<std::string> &cmdLog)
  {
    namespace fs = std::filesystem;
    auto appendLines = [&](std::vector<std::string> &lines)
    {
      for (auto &s : lines)
        cmdLog.push_back(std::move(s));
    };
    auto tryLoadPath = [&](const fs::path &p) -> bool
    {
      if (p.empty() || !fs::exists(p))
        return false;
      std::vector<std::string> boot;
      const std::string u8 = p.u8string();
      if (!LoadGoSurveyTemplateFile(cmd, u8.c_str(), boot))
        return false;
      appendLines(boot);
      return true;
    };

    if (cmd.defaultWorkspaceTemplatePathUtf8[0] != '\0')
    {
      const fs::path custom(cmd.defaultWorkspaceTemplatePathUtf8);
      if (!custom.empty() && fs::exists(custom))
      {
        std::vector<std::string> boot;
        const std::string u8 = custom.u8string();
        if (LoadGoSurveyTemplateFile(cmd, u8.c_str(), boot))
        {
          appendLines(boot);
          LoadBundledBlockLibrary(cmd, cmdLog);
          return;
        }
        appendLines(boot);
        cmdLog.push_back("Startup: custom template failed to load; trying bundled default-template.gst.");
      }
      else
      {
        cmdLog.push_back("Startup: custom template path not found; trying bundled default-template.gst.");
      }
    }

    if (tryLoadPath(ResolveDefaultWorkspaceTemplateGstPath())) {
      LoadBundledBlockLibrary(cmd, cmdLog);
      return;
    }
    cmdLog.push_back("Startup: bundled default-template.gst not found; starting with an empty drawing.");
    LoadBundledBlockLibrary(cmd, cmdLog);
  }

} // namespace

static void GlfwErrorCallback(int error, const char *description)
{
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

#ifdef _WIN32
/// Publishes the mutex Inno Setup looks for (`AppMutex` in installer/GoSurvey.iss) so the
/// installer can detect a running GoSurvey and close it rather than failing to replace a
/// locked executable. This is what lets REQ-078 apply an update with no separate updater
/// binary (ADR-029 (f)).
///
/// It is deliberately NOT single-instance enforcement: a second instance finds the mutex
/// already present, ignores that, and runs normally. Both namespaces are published because
/// the installer runs elevated and may not share the local one; failing to create the
/// global mutex is not an error worth reporting, since the local one still serves.
/// Handles are intentionally never closed — the process holds them until it exits, which
/// is precisely the lifetime the installer needs to observe.
static void PublishInstallerDetectionMutex()
{
  ::CreateMutexW(nullptr, FALSE, L"GoSurveyAppMutex");
  ::CreateMutexW(nullptr, FALSE, L"Global\\GoSurveyAppMutex");
}
#endif

#ifdef _WIN32
// BUG-013 — ask for the discrete GPU on a hybrid laptop.
//
// These two exported symbols are the documented way an application tells NVIDIA's and AMD's drivers
// that it wants the high-performance part. They are read by the driver when the process starts, so
// they must be *exported* (the linker would otherwise drop two variables nothing references) and
// they cannot be changed once the application is running.
//
// Without them GoSurvey rendered on whatever Windows chose, which on the reference machine was the
// integrated GPU: 250,000 line segments took 9.27 ms per frame instead of 1.38 ms, and a
// 2,000,000-triangle shaded mesh missed the REQ-100 budget at 21.40 ms where the discrete GPU does
// it in 1.97 ms. Nothing reported this for as long as the project has existed, because the symptom
// is not an error — it is just slowness, on hardware bought to avoid exactly that.
//
// A user who wants the integrated GPU back (for battery life in the field) sets it in Settings,
// which records the preference with Windows; see platform/GpuPreference.hpp for why that mechanism
// rather than a flag of our own, and why it can only take effect at the next launch.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

// GitHub issue #168 — append one diagnostic line per stall episode to
// `%APPDATA%\GoSurvey\frame-watch.log`. Written to a file, not the command line, for the same reason
// BENCH is (CadCommands_Bench.cpp): a stall that forces a kill of the app takes the scrollback with
// it, and the numbers are what the investigation needs. Best-effort — a failed open is silently
// skipped rather than allowed to disturb the frame loop it is diagnosing.
static void AppendFrameWatchLog(const AppCommandState& cmd, const framewatch::Tick& tick)
{
  namespace fs = std::filesystem;
  const fs::path dir = UserDataDirectory();
  if (dir.empty())
    return;
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::ofstream f(dir / "frame-watch.log", std::ios::app);
  if (!f)
    return;

  const std::time_t t = std::time(nullptr);
  char timeBuf[32] = "0000-00-00 00:00:00";
  struct tm tmInfo{};
  if (localtime_s(&tmInfo, &t) == 0)
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmInfo);

  const char* phase = tick.event == framewatch::Event::StallBegan ? "STALL BEGAN" : "STALL ENDED";
  const SelectedEntity& hov = cmd.viewportHoverEntity;

  f << timeBuf << "  " << phase << "  frame=" << tick.frameMs << "ms"
    << "  episode=" << tick.stalledFrames << "frames/" << tick.stalledMs << "ms\n";
  f << "    perf: viewportUi=" << cmd.perfViewportUiMs << "  render=" << cmd.perfRenderMs
    << "  hoverPick=" << cmd.perfHoverPickMs << (cmd.perfHoverPickRan ? "(ran)" : "(cached)")
    << "  snap=" << cmd.perfSnapMs << "\n";
  f << "    cmd: active=" << AppCommandState::KindName(cmd.active)
    << "  last=" << AppCommandState::KindName(cmd.lastCommand)
    << "  selBoxDrag=" << (cmd.selBoxWaitingSecond ? 1 : 0)
    << "  selection=" << cmd.selection.size()
    << "  selSurveyPts=" << cmd.selectedSurveyPointIndices.size()
    << "  hover=" << (cmd.viewportHoverEntityValid ? static_cast<int>(hov.type) : -1) << "\n";
  f << "    drawing: tab=" << cmd.activeDrawingIdx << "  space=" << cmd.activeSpaceIndex
    << "  lines=" << cmd.userLineAttrs.size()
    << "  polylines=" << (cmd.userPolylineOffsets.empty() ? size_t{0} : cmd.userPolylineOffsets.size() - 1)
    << "  circles=" << cmd.userCircleAttrs.size() << "  arcs=" << cmd.userArcs.size()
    << "  ellipses=" << cmd.userEllipses.size() << "  points=" << cmd.surveyPoints.size()
    << "  annotations=" << cmd.cadAnnotations.size() << "  surfaces=" << cmd.cadSurfaces.size()
    << "  filledRegions=" << cmd.cadFilledRegions.size()
    << "  zoom=" << cmd.viewportZoom << "  pan=(" << cmd.viewportPanX << "," << cmd.viewportPanY << ")\n";
  f.flush();
}

int main()
{
#ifdef _WIN32
  PublishInstallerDetectionMutex();
#endif

  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit())
    return 1;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GlfwApplySplashStageWindowHints();

  // REQ-093: the splash stage window is small and centered (AutoCAD-style), not the eventual
  // maximized shell — it used to be created at the full 1600x900 shell size and immediately
  // maximized right here, which is why the splash card previously appeared to float over a giant
  // dimmed rectangle instead of the real desktop: the "rectangle" WAS the (opaque, since per-pixel
  // transparency isn't guaranteed by every compositor) maximized window. GlfwApplyMainStageWindowChrome
  // (called after RunStartupSplash below) is the only maximize call now, so the window makes one
  // clean size/decoration transition instead of two.
  constexpr int kSplashWinW = 440;
  constexpr int kSplashWinH = 320;
  GLFWwindow *window = glfwCreateWindow(kSplashWinW, kSplashWinH, "GoSurvey", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    return 1;
  }
  if (GLFWmonitor *primary = glfwGetPrimaryMonitor())
  {
    int mx = 0, my = 0, mw = 0, mh = 0;
    glfwGetMonitorWorkarea(primary, &mx, &my, &mw, &mh);
    glfwSetWindowPos(window, mx + (mw - kSplashWinW) / 2, my + (mh - kSplashWinH) / 2);
  }
  glfwDefaultWindowHints();

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  PdfAttach_Init();

  // One ViewportRenderer (owns its own FBO + texture) per open drawing tab.
  std::vector<std::unique_ptr<ViewportRenderer>> viewportRenderers;
  viewportRenderers.push_back(std::make_unique<ViewportRenderer>());
  if (!viewportRenderers[0]->Init())
  {
    PdfAttach_Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  AppLogoGpu appLogo{};
  {
    namespace fs = std::filesystem;
    const fs::path iconPath = ResolveAppLogoPngPath();
    if (!iconPath.empty() && LoadAppLogoFromPngFile(window, iconPath, &appLogo, true))
      CadUiSetMenuBarLogo((ImTextureID)(intptr_t)(uintptr_t)appLogo.texture, static_cast<float>(appLogo.width),
                          static_cast<float>(appLogo.height));
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
#ifdef GOSURVEY_DEVELOPER_SHELL
  ImGuiTestEngine* devEngine = nullptr;
  DevShell_Create(&devEngine);
  std::string devshellRunName;
  const bool devshellCli = DevShell_ParseRunFlag(&devshellRunName);
#endif
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigInputTextEnterKeepActive = false; // CAD shell: Enter submits without selecting-all next keystroke

  ApplyCadLightTheme();
  if (!LoadApplicationFont())
    std::fprintf(stderr, "Calibri not found; using ImGui default font.\n");
  io.FontGlobalScale = 1.35f;

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  // AppCommandState + the ini-path configuration MUST happen before the splash's first
  // ImGui::NewFrame(), not after it. Dear ImGui auto-loads io.IniFilename's saved dock/window
  // layout exactly once per process, on the very first NewFrame() call ever made — a one-shot
  // latch that does not retry. ImGuiLayout_ConfigureIniPath is what points io.IniFilename at the
  // real, per-layout .ini path (it starts out null); until this line ran here, the splash's own
  // frame loop was the first thing calling NewFrame(), with IniFilename still null, so ImGui's
  // auto-load fired once, found nothing, and never looked again — the saved dock layout was
  // silently discarded every launch, which is what "my layout isn't respected" actually was.
  AppCommandState cmd;
#ifdef GOSURVEY_DEVELOPER_SHELL
  if (devEngine)
    DevShell_RegisterTests(devEngine, &cmd);
#endif
  LoadUserStartupPrefs(cmd);
#ifdef GOSURVEY_DEVELOPER_SHELL
  if (devshellCli)
    cmd.authGateResolved = true;
#endif
  const bool haveSavedDockIni = ImGuiLayout_ConfigureIniPath(cmd);

  // Shown only now (GlfwApplySplashStageWindowHints set GLFW_VISIBLE false) so it never flashes
  // at an unpositioned spot before glfwSetWindowPos above took effect.
  glfwShowWindow(window);

  // REQ-093: hardcoded 5 s regardless of how fast the real preload below finishes — the app is
  // still low-resource enough that the actual load is imperceptible, so the splash's duration is
  // deliberately decoupled from it rather than trying to track real progress.
#ifdef GOSURVEY_DEVELOPER_SHELL
  RunStartupSplash(window, devshellCli ? 0.0 : 5.0);
#else
  RunStartupSplash(window, 5.0);
#endif
  // The splash ran in a small (~440x320) window. Maximizing it for the main stage leaves the stale
  // splash front buffer stretched fullscreen for the frames it takes DWM to catch up — the "glitchy
  // maximized splash". Fix: hide the window across the maximize and the entire pre-loop setup, and
  // only reveal it once the first REAL UI frame has been rendered and swapped (see mainWindowShown
  // below). A hidden window composites nothing, so there is no stale content to stretch — the user
  // sees the splash, then the finished app, with nothing in between.
  glfwHideWindow(window);
  GlfwApplyMainStageWindowChrome(window);
  glfwHideWindow(window);  // GlfwApplyMainStageWindowChrome may transiently show it while maximizing
  // glfwMaximizeWindow posts an async resize on Windows; pump events so the window's real size has
  // landed before the first docked-layout frame reads it.
  glfwPollEvents();
  glfwPollEvents();
  bool mainWindowShown = false;

  // REQ-077: the update check. Only the persisted settings live in AppCommandState; the worker
  // state is owned here, so no drawing state is ever touched from a background thread.
  update::UpdateState updateState;
  updateState.prefs = cmd.updatePrefs;
  // Runs on EVERY launch (no throttle) and gates the session: the dialog stays modal until it
  // resolves. Does nothing at all when the user has switched the check off.
#ifdef GOSURVEY_DEVELOPER_SHELL
  if (!devshellCli)
#endif
  update::BeginStartupCheck(updateState, "chetjones003/GoSurvey");
  /// Set once the user has confirmed an update and the app is exiting to hand over to the
  /// installer, so the normal quit path can tell the two cases apart.
  bool updateExitPending = false;

  // REQ-080: anonymous telemetry. Fire-and-forget: does not gate the session, no UI, logs and
  // swallows network errors. Runs independent of the update check in its own worker.
  //
  // (Amended 2026-08-23, D-2026-08-23-e): no longer fired here, at raw process start. The
  // payload now carries the REQ-091 signed-in email when known, and sign-in state is not yet
  // resolved at this exact instant — silent refresh below hasn't run yet. So firing is deferred
  // to the point the auth gate first resolves (see the poll loop), which costs no perceptible
  // delay since the gate already blocks all other interaction until then. `telemetryFired`
  // ensures this happens exactly once per launch, same as before.
  std::unique_ptr<telemetry::TelemetryTask> telemetryTask;
  bool                                      telemetryFired   = false;
  const std::string telemetryChannel = cmd.updatePrefs.useBetaChannel ? "beta" : "stable";

  // REQ-091: accounts sign-in. The heavier async state (the task, holding a thread) stays local
  // here, same split as updateState/telemetryTask; only display flags live on AppCommandState.
  // `authLastAttemptInteractive` distinguishes "the automatic startup silent-refresh found no
  // stored session" (ordinary logged-out state, no error shown) from "the user clicked Sign In
  // and it failed" (REQ-201: shown, not swallowed) — both complete through the same poll below.
  std::unique_ptr<auth::AuthTask> authTask;
  bool                            authLastAttemptInteractive = false;
  authTask                                                   = auth::BeginSilentRefresh();
  cmd.authBusy                                                = true;
  std::vector<std::string> cmdLog;
  cmdLog.push_back("GoSurvey CAD shell ready.");
  cmdLog.push_back("Regenerating model.");
  cmdLog.push_back("Drawing Created.");
  cmdLog.push_back("JSON database - ready...");
  // BUG-012: a drawing passed on the command line wins over the startup template. Someone who
  // opened a drawing this way wants that drawing, not a blank sheet built from their template.
  bool openedFromCommandLine = false;
  {
    const std::string startupFile = FirstExistingFileArgumentUtf8();
    if (!startupFile.empty())
    {
      std::vector<std::string> boot;
      openedFromCommandLine = OpenDrawingDocument(cmd, startupFile.c_str(), boot);
      for (auto &s : boot)
        cmdLog.push_back(std::move(s));
      // A file that will not open falls back to the normal startup path rather than leaving the
      // user staring at nothing, but it says so — REQ-201, and silence here would look identical
      // to the bug this fixes.
      if (!openedFromCommandLine)
        cmdLog.push_back("Could not open " + startupFile + "; starting from the usual template.");
      else
      {
        // BUG-027: loading a drawing is not the same as OWNING it. Until this ran, a drawing opened
        // this way was loaded but anonymous - the tab still read "Drawing 1", and the first Save
        // (menu or Ctrl+S) opened Save As and then asked permission to overwrite the user's own
        // drawing. File > Open adopts its path for exactly this reason; the startup argument never
        // did.
        //
        // Adopted HERE and not inside LoadGoSurveyTemplateFile: the startup TEMPLATE loads through
        // that same function, and a blank drawing that adopted default-template.gst as its save
        // target would overwrite the template on the next Ctrl+S. Only the caller knows whether the
        // file it just read is the document or the mould for one.
        std::error_code             absEc;
        const std::filesystem::path argPath(startupFile);
        // Absolute, because a relative argument would otherwise be re-resolved later against
        // whatever the working directory happens to be by then.
        const std::filesystem::path absPath = std::filesystem::absolute(argPath, absEc);
        cmd.activeDocFilePath = absEc ? startupFile : absPath.u8string();
        // u8string, not string: the tab label is drawn by ImGui, which reads UTF-8, and this path
        // may contain characters the process ANSI codepage cannot spell.
        const std::string tabName = argPath.stem().u8string();
        // REQ-308: the loaded drawing is the first real drawing tab, not the Start sentinel (0).
        if (!tabName.empty())
          cmd.drawingTabs[FirstDrawingTabIndex()].name = tabName;
      }
    }
  }
  if (!openedFromCommandLine)
    TryLoadStartupWorkspaceTemplate(cmd, cmdLog);
  cmd.activeDocSavedRevision = cmd.cadGpuRevision; // loaded drawing/template counts as "clean"
  // REQ-308: the template / command-line drawing was just loaded into the live cmd fields while the
  // Start tab (index 0) is active. Persist it into its own tab's slot so switching to it restores
  // the drawing rather than an empty document. A double-clicked drawing opens straight onto that
  // tab; a normal launch lands on the Start screen.
  {
    const int drawIdx = FirstDrawingTabIndex();
    SaveDocumentToSnapshot(cmd, drawIdx);
    cmd.documents[drawIdx].savedRevision = cmd.cadGpuRevision;
    if (openedFromCommandLine) {
      cmd.activeDrawingIdx        = drawIdx;
      cmd.prevDrawingIdx          = drawIdx;
      cmd.pendingDrawingTabSwitch = true;
      RecordRecentDrawing(cmd, cmd.activeDocFilePath);
    }
  }
  // Re-apply user preferences so they override any template defaults (crosshair, snap, survey, etc.).
  LoadUserStartupPrefSettings(cmd);
  // Re-apply theme now that displayColorThemeIdx is known from saved prefs.
  if (cmd.displayColorThemeIdx == 0)
    ApplyCadDarkTheme();
  else
    ApplyCadLightTheme();
  if (!cmd.surveyPoints.empty())
  {
    RepositionAllSurveyPointLabels(cmd);
    BumpCadGpuCache(cmd);
  }
  char cmdBuf[4096]{};

  double curX = 0.;
  double curY = 0.;
  double curRawX = 0.;
  double curRawY = 0.;
  int fbW = 900;
  int fbH = 650;

  bool dockLayoutDone = haveSavedDockIni;
  // 130 of tools + gutter under panel titles, plus the REQ-302 tab strip and gap
  // (kRibbonTabStripH + kRibbonTabStripGapY). Extra 10px keeps two-line captions
  // above the title strip after the Survey-tab label layout.
  const float ribbonH = 139.f + 28.f + 4.f + 10.f + 34.f;  // +34: taller rows → more readable icons
  bool orthoEnabled = false;  // REQ-047: ORTHO is off by default (AutoCAD convention) — free-angle drawing
  bool gridVisible = false;
  // prevDrawingIdx lives in cmd — no local needed.

  // REQ-100 bench state that belongs to the loop rather than to the command layer.
  bool benchVsyncOff = false;
  double benchPrevTime = 0.0;
#ifdef GOSURVEY_DEVELOPER_SHELL
  int devshellFrames = 0;
#endif

  auto perfPrevFrame = std::chrono::steady_clock::now();
  framewatch::FrameWatch frameWatch;
  while (true)
  {
    glfwPollEvents();

    // Frame-time HUD (issue #166 investigation, PERFHUD command). Frame-to-frame wall clock,
    // measured at the top so it is the whole cost — poll, UI, render, swap, vsync wait.
    {
      const auto now = std::chrono::steady_clock::now();
      cmd.perfFrameMs = std::chrono::duration<double, std::milli>(now - perfPrevFrame).count();
      perfPrevFrame = now;
    }

    // GitHub issue #168 — freeze/stall detector. The frame time just measured is the full cost of
    // the PREVIOUS iteration; on a slow frame `cmd`'s state is still that iteration's (the loop has
    // not run again yet), so the snapshot names the subsystem that stalled. One line per episode:
    // the first slow frame and the first healthy frame after it. See util/framewatch.hpp for why
    // this cannot catch a true never-returning infinite loop.
    {
      const framewatch::Tick fwTick = framewatch::FrameWatchTick(&frameWatch, cmd.perfFrameMs);
      if (fwTick.event == framewatch::Event::StallBegan || fwTick.event == framewatch::Event::StallEnded)
        AppendFrameWatchLog(cmd, fwTick);
    }

    // --- REQ-100 frame-budget benchmark ---------------------------------------------------------
    // Timed at the TOP of the iteration, so each sample is the full frame-to-frame cost the user
    // actually feels: poll, UI, snap/hover, render, swap.
    //
    // Vsync must be off for the whole run. With glfwSwapInterval(1) every frame is pinned to a
    // multiple of the refresh interval, so the measurement would report the monitor rather than the
    // renderer — ~16.7 ms whatever the scene costs, and a clean 16 ms "pass" that means nothing.
    if (cmd.bench.active)
    {
      if (!benchVsyncOff)
      {
        glfwSwapInterval(0);
        benchVsyncOff = true;
        benchPrevTime = glfwGetTime();
      }
      else
      {
        const double nowT = glfwGetTime();
        // Warm-up frames are discarded: the first frames of a run pay for shader compilation, the
        // 250k-segment VBO upload and the tessellation cache, none of which recur while orbiting.
        if (cmd.bench.frameIndex >= cmd.bench.warmupFrames)
        {
          cmd.bench.frameMs.push_back((nowT - benchPrevTime) * 1000.0);
          // ADR-036 (e)'s REQ-100 obligation: the surface display cache must HOLD across frames, not
          // merely regenerate quickly once. The baseline is taken at the first TIMED frame, after
          // warm-up has paid for the one legitimate generation, so what the run reports is purely
          // work that recurred while orbiting. It must stay 0.
          if (!cmd.bench.regenBaselineTaken)
          {
            // Both caches, summed: the surface profile grows surfaceDisplayRegenCount and the solid
            // profile grows solidDisplayRegenCount, only one profile runs at a time, and the other
            // term is constant across the run — so the delta is whichever cache the profile exercises.
            cmd.bench.regenAtStart      = cmd.surfaceDisplayRegenCount + cmd.solidDisplayRegenCount;
            cmd.bench.regenBaselineTaken = true;
            // Captured here, not at FinishFrameBudgetBench: by then the bench scene has already been
            // swapped back out for the user's drawing and the cache holds their surfaces, not this one.
            cmd.bench.surfaceContourSegs = SurfaceDisplayContourSegs(cmd);
          }
          cmd.bench.regenDuringRun =
              (cmd.surfaceDisplayRegenCount + cmd.solidDisplayRegenCount) - cmd.bench.regenAtStart;
        }
        benchPrevTime = nowT;
      }
      ++cmd.bench.frameIndex;
      // The scripted orbit. Azimuth only: it keeps every contour on screen for the whole run, so
      // no frame is cheap merely because the geometry left the viewport.
      cmd.viewportAzimuthDeg =
          static_cast<float>(std::fmod(cmd.viewportAzimuthDeg + cmd.bench.orbitDegPerFrame, 360.0));
      if (cmd.bench.frameIndex >= cmd.bench.framesTotal + cmd.bench.warmupFrames)
      {
        FinishFrameBudgetBench(cmd, cmdLog);
        glfwSwapInterval(1);
        benchVsyncOff = false;
      }
    }

    // REQ-077/078: advance the background check or download, then let a confirmed update exit
    // through the SAME unsaved-changes path a normal quit uses. Reusing it rather than repeating
    // the dirty check is the point — one code path decides whether work is at risk.
    update::PollUpdateTask(updateState);
    telemetry::PollTelemetryTask(telemetryTask);

    // REQ-091: sign-in requests from the Settings panel. Ignored while a task is already in
    // flight rather than queued — a second click while one attempt is running is a double-click,
    // not two sign-ins.
    if (cmd.authSignOutRequested)
    {
      cmd.authSignOutRequested = false;
      auth::SignOut();
      cmd.authSignedIn = false;
      cmd.authEmail.clear();
      cmd.authError.clear();
    }
    if (cmd.authSignInRequested)
    {
      cmd.authSignInRequested = false;
      if (!authTask)
      {
        authTask                     = auth::BeginInteractiveSignIn();
        authLastAttemptInteractive   = true;
        cmd.authBusy                 = true;
        cmd.authInteractiveBusy      = true;
        cmd.authError.clear();
      }
    }
    if (authTask && authTask->done.load(std::memory_order_acquire))
    {
      cmd.authBusy            = false;
      cmd.authInteractiveBusy = false;
      cmd.authSignedIn        = authTask->ok;
      cmd.authEmail    = authTask->email;
      // The silent startup refresh finding no stored session is the ordinary logged-out state,
      // not a reported error (REQ-091 acceptance: an expired/revoked token just falls back to
      // interactive sign-in). Only a user-initiated attempt's failure is shown.
      cmd.authError    = (!authTask->ok && authLastAttemptInteractive) ? authTask->error : "";
      // REQ-091 (amended): the launch gate closes on a successful sign-in, OR when there was no
      // internet at all to attempt one with — never on an ordinary "no stored session" failure
      // while online, which is what leaves the gate open to show the blocking prompt.
      if (authTask->ok || authTask->skippedNoNetwork)
        cmd.authGateResolved = true;
      authTask.reset();
    }
    // REQ-080 (amended, D-2026-08-23-e): fires exactly once per launch, right after the auth
    // gate first resolves — the earliest point sign-in state is actually known. `cmd.authEmail`
    // is empty for a signed-out user (offline-skip case included), so this is a no-op change in
    // payload shape for anyone not signed in.
    if (cmd.authGateResolved && !telemetryFired)
    {
      telemetryFired = true;
      telemetryTask   = telemetry::BeginTelemetryPing(
          GOSURVEY_VERSION_FULL, telemetryChannel, cmd.authSignedIn ? cmd.authEmail : std::string());
    }
    if (updateState.awaitingUnsavedCheck)
    {
      updateState.awaitingUnsavedCheck = false;
      updateExitPending                = true;
      glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // Intercept window-close so we can prompt about unsaved drawings.
    if (glfwWindowShouldClose(window))
    {
      glfwSetWindowShouldClose(window, GLFW_FALSE);
      bool anyDirty = (cmd.cadGpuRevision != cmd.activeDocSavedRevision);
      for (int i = 0; i < static_cast<int>(cmd.documents.size()) && !anyDirty; ++i)
      {
        if (i != cmd.activeDrawingIdx &&
            cmd.documents[i].cadGpuRevision != cmd.documents[i].savedRevision)
          anyDirty = true;
      }
      if (anyDirty)
        cmd.confirmCloseModal = true;
      else
        cmd.closeConfirmed = true;
    }

    // The user cancelled the save prompt, so the update is cancelled too — the installer is not
    // run, and the verified download stays on disk for the next offer.
    if (updateExitPending && !cmd.closeConfirmed && !cmd.confirmCloseModal &&
        !glfwWindowShouldClose(window))
    {
      updateExitPending  = false;
      updateState.phase  = update::Phase::ReadyToLaunch;
    }

    if (cmd.closeConfirmed)
    {
      // Work is saved or deliberately discarded by this point, so it is safe to hand over.
      // Inno closes this process via the AppMutex, replaces the files, and restarts us.
      if (updateExitPending)
        update::LaunchInstallerAndExit(updateState);
      break;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // REQ-047: F3 (object snap) and F8 (ortho) are MODE toggles — like AutoCAD they must work even while
    // the command bar has keyboard focus. They are not text characters, so handling them during
    // WantTextInput never interferes with typing a command.
    if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
      cmd.objectSnapEnabled = !cmd.objectSnapEnabled;
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false)) {
      orthoEnabled = !orthoEnabled;
      if (orthoEnabled)
        cmd.polarMode = false;  // ORTHO and POLAR are mutually exclusive (issue #154)
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) {
      cmd.polarMode = !cmd.polarMode;
      if (cmd.polarMode)
        orthoEnabled = false;
    }
    cmd.orthoMode = orthoEnabled;

    // Ctrl+S (BUG-026). The File menu has always ADVERTISED this shortcut, but ImGui's MenuItem
    // shortcut argument is a label it draws — it installs no binding — so the key did nothing.
    // Deliberately NOT gated on WantTextInput, unlike Ctrl+Z/C/V below: those have a meaning
    // inside a text field and must yield to it, whereas Ctrl+S has none. That is the same rule
    // F3/F8 follow above, and it is what lets a user save without first leaving the command bar.
    //
    // Ctrl+S ONLY: Shift and Alt are excluded because this handler does not own those chords.
    // Ctrl+Shift+S means "Save As" to most of the muscle memory that will meet this application,
    // and answering it with a silent overwrite of the current file is the wrong answer to give
    // unasked. AltGr is Ctrl+Alt on many keyboard layouts, so letting Alt through would turn
    // typing an accented character in the command bar into a save.
    {
      const ImGuiIO &ioSave = ImGui::GetIO();
      if (ioSave.KeyCtrl && !ioSave.KeyShift && !ioSave.KeyAlt &&
          ImGui::IsKeyPressed(ImGuiKey_S, false) && cmd.activeDrawingIdx != 0)  // REQ-308: not on Start
        SaveActiveDocument(cmd, cmdLog);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
      if (cmd.copySurveyDupModalOpen)
      {
        ApplyCopySurveyDuplicateModalResult(cmd, false, cmdLog);
        cmdBuf[0] = '\0';
      }
      else if (cmd.mtextRichEditorOpen)
      {
        CancelMtextRichEditor(cmd, &cmdLog);
        cmdBuf[0] = '\0';
      }
      else if (cmd.tableCellEditorOpen)
      {
        CancelTableCellEditor(cmd);
        cmdBuf[0] = '\0';
      }
      else if (cmd.gizmoDragActive)
      {
        // A TRUE cancel, not an undo: a live gizmo drag changes nothing in the store until it is
        // committed, so abandoning one costs an undo step nobody spent. Ahead of the other grips
        // for the same reason it is ahead of them on click — it is the gesture in progress.
        CancelGizmoDrag(cmd);
        BumpCadGpuCache(cmd);
        cmdLog.push_back("Gizmo drag cancelled.");
        cmdBuf[0] = '\0';
      }
      else if (cmd.mtextGripMoveActive)
      {
        AbortMtextGripInteraction(cmd);
        BumpCadGpuCache(cmd);
        cmdBuf[0] = '\0';
        cmdLog.push_back("MTEXT grip edit canceled.");
      }
      else if (cmd.entityGripMoveActive)
      {
        RestoreEntityGripOriginal(cmd);
        ClearEntityGripInteraction(cmd);
        BumpCadGpuCache(cmd);
        cmdBuf[0] = '\0';
        cmdLog.push_back("Grip edit canceled.");
      }
      else if (cmd.active != AppCommandState::Kind::None)
      {
        using SAP = AppCommandState::SegmentAnglePickPhase;
        if ((cmd.active == AppCommandState::Kind::Line || cmd.active == AppCommandState::Kind::Polyline) &&
            cmd.segmentAnglePickPhase != SAP::Idle)
          CancelSegmentAnglePick(cmd, &cmdLog);
        else
          CancelActiveCommand(cmd, cmdLog);
        cmdBuf[0] = '\0';
      }
      else
      {
        const bool hadCreatePtsUi = cmd.showCreatePointsWindow;
        cmd.showCreatePointsWindow = false;
        ClearSelection(cmd);
        cmd.pendingZoomExtents = false;
        cmd.pendingZoomWindow = false;
        cmdBuf[0] = '\0';
        cmdLog.push_back(hadCreatePtsUi ? "Selection cleared; CREATEPOINTS closed." : "Selection cleared.");
      }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput)
    {
      StartDeleteCommand(cmd, cmdLog);
    }

    const bool ctrlHeld = ImGui::GetIO().KeyCtrl;
    if (ctrlHeld && !ImGui::GetIO().WantTextInput)
    {
      if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
      {
        if (ImGui::GetIO().KeyShift)
          DoRedo(cmd, cmdLog);
        else
          DoUndo(cmd, cmdLog);
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
        DoRedo(cmd, cmdLog);
      if (ImGui::IsKeyPressed(ImGuiKey_C, false))
        CopySelectionToClipboard(cmd, cmdLog);
      if (ImGui::IsKeyPressed(ImGuiKey_V, false))
        StartPasteCommand(cmd, cmdLog);
    }

    // The function-local `static std::vector<float> surfaceEdges` that used to live here is gone
    // (ADR-036 (e)). It was per-PROCESS, not per-document, and carried a hand-rolled tab guard
    // because two tabs can sit at the same revision; the cache on AppCommandState that replaced it
    // deletes the guard along with the class of bug it patched. See RefreshSurfaceDisplayGeometry
    // below, which is now the one place surface display geometry is produced.

    // Stable entity ids (REQ-076 / ADR-027). Called once here, after the frame's input has been
    // handled and before any panel can save, reference or snapshot an entity — so nothing above
    // ever observes an id-less entity. It early-outs on an unchanged geometry revision, so a frame
    // that drew nothing pays one integer compare. Assigning at the ~127 sites that construct an
    // EntityAttributes was rejected: a missed site there is silent, not a compile error.
    EnsureEntityIds(cmd);

    // Surface dynamic rebuild (REQ-069). After EnsureEntityIds, so a breakline/boundary added this
    // frame already has an id to be resolved by. Also revision-gated, and (unlike EnsureEntityIds)
    // it does real work only for surfaces that are actually behind, dispatched to a background
    // thread — see TickSurfaceRebuilds' own comment for the full contract.
    TickSurfaceRebuilds(cmd, cmdLog);

    // Volume Dashboard live recompute (REQ-073 amendment, TASK-095). After TickSurfaceRebuilds, so a
    // dashboard-selected surface that finished rebuilding this frame is already current below.
    TickVolumeDashboard(cmd);

    // Generated surface display geometry (ADR-036 (e)). After TickSurfaceRebuilds, so a
    // triangulation that landed this frame is drawn from this frame rather than the next — and
    // after EnsureEntityIds for the same reason it is: the cache is keyed on the stable id.
    // Gated on its own staleness key, not on cadGpuRevision, which is what stops an unrelated edit
    // from regenerating every surface's geometry (REQ-070: a style change must not retriangulate,
    // and a line drawn elsewhere must not re-contour).
    RefreshSurfaceDisplayGeometry(cmd);
    // The solid tessellation cache (REQ-313). Same placement and the same reason as the surface
    // refresh above: keyed on its own staleness key, so an unrelated edit does not retessellate a
    // solid, and #120's "do not regenerate a solid's render mesh every frame" holds by construction.
    RefreshSolidDisplayGeometry(cmd);
    // A sub-object reference is an index PLUS the solid it came from, and it EXPIRES rather than
    // re-binding when that solid is replaced (REQ-318 item 10 / ADR-049). Swept here, once a frame,
    // rather than at each of the many places a solid can be erased, undone or replaced — one sweep
    // that cannot be forgotten beats a dozen call sites that can. Costs nothing when the selection
    // is empty, which is the overwhelming case.
    ExpireSubObjectSelection(cmd);

    const ImGuiViewport *mainVp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mainVp->WorkPos);
    ImGui::SetNextWindowSize(mainVp->WorkSize);
    ImGui::SetNextWindowViewport(mainVp->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
#if !defined(_WIN32)
    hostFlags |= ImGuiWindowFlags_MenuBar;
#endif

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("GoSurveyHost", nullptr, hostFlags);

    DrawMainWindowTitleBar(window);

#if defined(_WIN32)
    {
      const float menuStripH = ImGui::GetFrameHeight() + 6.f;
      ImGui::BeginChild("##GoSurveyMenuHost", ImVec2(0.f, menuStripH), false,
                        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      if (ImGui::BeginMenuBar())
      {
        DrawMainMenuBar(cmd, cmdLog);
        ImGui::EndMenuBar();
      }
      ImGui::EndChild();
    }
#else
    if (ImGui::BeginMenuBar())
    {
      DrawMainMenuBar(cmd, cmdLog);
      ImGui::EndMenuBar();
    }
#endif

    DrawRibbonBar(ribbonH, cmd, cmdLog);

    const float cadStatusH = CadStatusBarStripHeightPx();
    const float dockWrapH = std::max(1.f, ImGui::GetContentRegionAvail().y - cadStatusH);
    ImGui::BeginChild("##GoSurveyDockWrap", ImVec2(0.f, dockWrapH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 dockHostSize = ImGui::GetContentRegionAvail();
    ImGuiID dockspaceId = ImGui::GetID("GoSurveyDockSpace");
    ImGui::DockSpace(dockspaceId, dockHostSize, 0);

    if (cmd.pendingBuiltinDockLayoutReset)
    {
      cmd.pendingBuiltinDockLayoutReset = false;
      SetupMainDockLayout(dockspaceId, dockHostSize, cmd.cmdLineClassicDock);
      ImGuiIO &ioDock = ImGui::GetIO();
      if (ioDock.IniFilename && ioDock.IniFilename[0])
        ImGui::SaveIniSettingsToDisk(ioDock.IniFilename);
      dockLayoutDone = true;
      cmdLog.push_back("UI layout: reset to built-in dock split (saved to current layout .ini).");
    }
    else if (!dockLayoutDone)
    {
      dockLayoutDone = true;
      SetupMainDockLayout(dockspaceId, dockHostSize, cmd.cmdLineClassicDock);
    }

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar(3);

    // REQ-308: the Start tab is a full-bleed landing page — the docked Properties/Reports/Toolspace
    // panels are not submitted while it is active, so their dock nodes collapse and the Start screen
    // fills the work area. Switching to a drawing tab submits them again and ImGui re-docks each to
    // its remembered node.
    const bool showDrawingPanels = (cmd.activeDrawingIdx != 0);
    // Leaving the Start tab for a drawing re-submits the side panels; focus Properties (not
    // whichever tab ImGui last had selected in that dock node, e.g. Reports).
    static bool prevShowDrawingPanels = false;
    if (showDrawingPanels && !prevShowDrawingPanels)
      cmd.pendingPropertiesFocus = true;
    prevShowDrawingPanels = showDrawingPanels;
    if (showDrawingPanels) {
      DrawPropertiesPanel(cmd, &cmdLog);
      DrawToolspaceWindow(cmd, &cmdLog);
    }

    // Keep documents vector in sync with the tab list.
    while (cmd.documents.size() < cmd.drawingTabs.size())
      cmd.documents.emplace_back();

    // Erase renderer for a closed tab (must happen before provisioning so indices stay aligned).
    if (cmd.pendingTabErase >= 0)
    {
      const auto eraseAt = static_cast<size_t>(cmd.pendingTabErase);
      if (eraseAt < viewportRenderers.size())
      {
        viewportRenderers[eraseAt]->Shutdown();
        viewportRenderers.erase(viewportRenderers.begin() + static_cast<std::ptrdiff_t>(eraseAt));
      }
      cmd.pendingTabErase = -1;
    }

    // Clamp activeDrawingIdx defensively (close/erase can leave it transiently out of range).
    if (!cmd.drawingTabs.empty())
      cmd.activeDrawingIdx = std::max(0, std::min(cmd.activeDrawingIdx,
                                                  static_cast<int>(cmd.drawingTabs.size()) - 1));

    // Always provision renderers before any switch logic (New/Open menu items bypass switch detection).
    while (viewportRenderers.size() <= static_cast<size_t>(cmd.activeDrawingIdx))
    {
      viewportRenderers.push_back(std::make_unique<ViewportRenderer>());
      viewportRenderers.back()->Init();
    }

    // Detect tab switch: save old document, restore new one.
    // cmd.prevDrawingIdx is the authoritative last-active index (also written by menu New/Open handlers).
    if (cmd.activeDrawingIdx != cmd.prevDrawingIdx)
    {
      if (cmd.blockEditActive)
      {
        // ADR-043: a block-edit session holds the block's geometry in the model arrays; snapshotting
        // that as the tab's document would corrupt it. Refuse the switch until BCLOSE.
        cmd.activeDrawingIdx = cmd.prevDrawingIdx;
        cmdLog.push_back("Close the block editor (BCLOSE) before switching drawings.");
      }
      else
      {
        SaveDocumentToSnapshot(cmd, cmd.prevDrawingIdx);
        // REQ-308: the Start tab (index 0) backs no document — leave the live cmd fields holding
        // the drawing the user just left, so switching back to it needs no reload. Switching FROM
        // Start to a drawing restores that drawing normally.
        if (cmd.activeDrawingIdx != 0)
          RestoreDocumentFromSnapshot(cmd, cmd.activeDrawingIdx);
        cmd.prevDrawingIdx = cmd.activeDrawingIdx;
      }
    }

    const size_t activeRendIdx = static_cast<size_t>(cmd.activeDrawingIdx);
    ViewportRenderer &activeRenderer = *viewportRenderers[activeRendIdx];

    CadSnap::Hit snapHit{};
    DrawDrawingViewport(activeRenderer.ColorTexture(), cmd, cmdLog, cmdBuf, static_cast<int>(sizeof(cmdBuf)),
                        &cmd.viewportPanX, &cmd.viewportPanY, &cmd.viewportZoom, &curX, &curY, &curRawX, &curRawY,
                        &fbW, &fbH, &snapHit);
    cmd.uiCursorWorldX = CadCoord::WorldXFromLocal(cmd, static_cast<float>(curX));
    cmd.uiCursorWorldY = CadCoord::WorldYFromLocal(cmd, static_cast<float>(curY));
    {
      ImGuiIO &ioDim = ImGui::GetIO();
      if (!ioDim.WantTextInput && cmd.active == AppCommandState::Kind::DimLinear &&
          cmd.dimPhase == AppCommandState::DimPhase::WaitDimLinePt)
      {
        if (ImGui::IsKeyPressed(ImGuiKey_H, false))
          CadDimLinearApplyHVHotkey(cmd, false, cmdLog);
        else if (ImGui::IsKeyPressed(ImGuiKey_V, false))
          CadDimLinearApplyHVHotkey(cmd, true, cmdLog);
      }
    }
    if (showDrawingPanels)  // REQ-308: no command line on the Start tab
      DrawCommandLinePanel(cmdLog, cmdBuf, static_cast<int>(sizeof(cmdBuf)), cmd);
    // Z was hard-coded 0 while the readout claimed to show a coordinate — it now reports the
    // cursor's real elevation (the work plane, or the snapped point's own Z). Absolute, never
    // rebased: the document origin is X/Y-only (ADR-025 D2).
    DrawCadStatusBarStrip(cmd, CadCoord::WorldXFromLocal(cmd, static_cast<float>(curX)),
                          CadCoord::WorldYFromLocal(cmd, static_cast<float>(curY)), cmd.uiCursorWorldZ,
                          &orthoEnabled, &gridVisible);

    // LINE/POLYLINE AP: after two picks the bottom command InputText is hidden — Enter must still lock bearing.
    // Keyboard-only "A" then bearing: Enter with empty buffer cancels awaiting mode when no text field is focused.
    {
      ImGuiIO &ioEnter = ImGui::GetIO();
      const bool enterDown =
          ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
      if (enterDown && !ioEnter.WantTextInput && cmd.active != AppCommandState::Kind::None)
        ProcessCommandLineSubmit(cmdBuf, static_cast<int>(sizeof(cmdBuf)), cmd, cmdLog);
    }

    DrawQuickSelectWindow(cmd, cmdLog);
    DrawSelectionCyclingPanel(cmd);
    DrawCreatePointsPanel(cmd, cmdLog);
    DrawSettingsPanel(cmd, &cmdLog);
    DrawAccountDetailsWindow(cmd);
    DrawUnitsDialog(cmd, &cmdLog);
    DrawRightClickCustomizationDialog(cmd, &cmdLog);  // REQ-084 (a)
    ImGuiLayout_DrawLayoutPopups(cmd, cmdLog);
    DrawLayerManagerWindow(cmd, &cmdLog);
    DrawViewManagerWindow(cmd, &cmdLog);
    DrawTextStyleManagerWindow(cmd, &cmdLog);
    DrawDimStyleWindow(cmd, &cmdLog);
    DrawPointGroupManagerWindow(cmd, &cmdLog);
    DrawSurfaceManagerWindow(cmd, &cmdLog);
    DrawSurfaceStyleWindow(cmd, &cmdLog);
    DrawVolumeDashboardWindow(cmd, &cmdLog);  // REQ-073 amendment (TASK-095)
    DrawQuickProfileWindow(cmd, &cmdLog);     // REQ-145
    DrawFeatureLineElevationWindow(cmd, &cmdLog);  // REQ-088
#ifdef GOSURVEY_DEVELOPER_SHELL
    DevShell_Draw(cmd, cmdLog);
    ++devshellFrames;
    if (devshellCli && devshellFrames == 4 && devEngine)
      DevShell_BeginCliRun(devEngine, window, devshellRunName.c_str());
#endif
    DrawViewPointsPanel(cmd, cmdLog);
    DrawImportPointsPanel(cmd, cmdLog);
    DrawExportPointsPanel(cmd, cmdLog);
    if (showDrawingPanels)  // REQ-308: hidden on the Start tab, same as Properties/Toolspace above
      DrawSurveyReportsPanel(cmd);
    DrawTraverseEditorPanel(cmd, cmdLog);
    // After all panels have called Begin() their DockNode is set, so SetWindowFocus
    // can correctly update the dock node's SelectedTabId for the next frame.
    if (cmd.pendingPropertiesFocus)
    {
      ImGui::SetWindowFocus("Properties");
      cmd.pendingPropertiesFocus = false;
    }
    DrawCopySurveyDuplicateModal(cmd, cmdLog);
    DrawDxfPointConflictModal(cmd, cmdLog);
    DrawViewportsWindow(cmd, cmdLog);
    DrawMoveCopyLayoutDialog(cmd, cmdLog);
    DrawPageSetupManager(cmd, cmdLog);
    DrawNewPageSetupDialog(cmd, cmdLog);
    DrawPageSetupEditor(cmd, cmdLog);
    DrawBatchPlotDialog(cmd, cmdLog);
    DrawPdfAttachDialog(cmd, cmdLog);
    DrawInsertBlockDialog(cmd, cmdLog);
    DrawEditBlockDefinitionDialog(cmd, cmdLog);
    DrawBlockAuthoringPalettes(cmd, cmdLog);
    DrawAlignResultsWindow(cmd, cmdLog);
    DrawCloseConfirmModal(cmd, cmdLog);
    DrawUpdateDialog(cmd, updateState);
    // REQ-091 (amended): blocks every launch until authGateResolved — signed in, or no internet
    // at all to sign in with. Stacks beneath the update dialog's modal (both gate the session;
    // ImGui's modal stack lets whichever opened first take input priority).
    DrawSignInGate(cmd);
    // The dialog writes skip state into cmd.updatePrefs; the throttle anchor is written by the
    // check itself. Sync the rest back so SaveUserStartupPrefs persists both.
    cmd.updatePrefs.enabled        = updateState.prefs.enabled;
    cmd.updatePrefs.useBetaChannel = updateState.prefs.useBetaChannel;
    DrawDwgLossyExportModal(cmd, cmdLog);

    // The point a click would COMMIT at, which is NOT the cursor. When an object snap is acquired,
    // SubmitViewportPick commits at the snap point (CadUi: commitX/commitY), while curX/curY is only
    // eased TOWARD it by the magnet — 92% at most, and far less out toward the magnet's outer radius
    // of 2.75 apertures. Previewing at the cursor therefore draws a shape the commit will not
    // produce. Harmless-looking for a line, which lands a few pixels off; ruinous for a 3-point
    // circle, where nudging the third point toward collinear sends the circumcentre and radius
    // arbitrarily far and the committed circle is nowhere near the previewed one.
    const double commitCurX =
        cmd.viewportSnapPickValid ? static_cast<double>(cmd.viewportSnapPickLocalX) : curX;
    const double commitCurY =
        cmd.viewportSnapPickValid ? static_cast<double>(cmd.viewportSnapPickLocalY) : curY;

    std::vector<float> rubberLines;
    const float orthoHalfH = (1.f / std::max(cmd.viewportZoom, 1.e-9f)) * 50.f;
    AppendCadDraftRubberLines(cmd, commitCurX, commitCurY, orthoEnabled, cmd.viewportPanX, cmd.viewportPanY,
                              orthoHalfH, fbH, rubberLines);

    // The selection box is drawn by the CadUi overlay (TASK-047), screen-aligned from the same two
    // projected corners the hit test uses, so it no longer needs to reach the GL renderer at all.

    std::vector<float> previewLines;
    std::vector<float> previewCircles;
    // Same rule as the draft rubber above: preview where the commit will land. OFFSET keeps the raw
    // cursor — its side-of-the-line test is about which side the pointer is on, not a snapped point.
    const float previewCx =
        cmd.active == AppCommandState::Kind::Offset ? static_cast<float>(curRawX) : static_cast<float>(commitCurX);
    const float previewCy =
        cmd.active == AppCommandState::Kind::Offset ? static_cast<float>(curRawY) : static_cast<float>(commitCurY);
    BuildTransformPreview(cmd, previewCx, previewCy, &previewLines, &previewCircles, orthoHalfH, fbH);

    // REQ-103 BREAK (TASK-101): the material the pending break would remove, kept out of the
    // translucent transform batch above on purpose — this one is drawn ON TOP of the object it
    // describes, so it needs an opaque, heavier stroke to be legible at all. See
    // BuildBreakRemovalPreview.
    std::vector<float> removalLines;
    std::vector<float> removalMarkers;
    BuildBreakRemovalPreview(cmd, static_cast<float>(commitCurX), static_cast<float>(commitCurY), orthoHalfH,
                             &removalLines, &removalMarkers);

    if (cmd.active == AppCommandState::Kind::Trim &&
        cmd.trimPhase == AppCommandState::TrimPhase::CuttingLine_WaitP2)
    {
      // TRIM's cutting-line points commit through commitX/commitY too (SubmitTrimViewportPick).
      float lx = static_cast<float>(commitCurX);
      float ly = static_cast<float>(commitCurY);
      ApplyOrthoConstrainFromAnchor(cmd, cmd.trimCutInfP1x, cmd.trimCutInfP1y, &lx, &ly, orthoEnabled);
      PushRubberSegViewRel(rubberLines, cmd.trimCutInfP1x, cmd.trimCutInfP1y, lx, ly, cmd.viewportPanX,
                           cmd.viewportPanY);
      const float midx = (cmd.trimCutInfP1x + lx) * 0.5f;
      const float midy = (cmd.trimCutInfP1y + ly) * 0.5f;
      CadTrimAppendCutLineRemovedPreview(cmd, cmd.trimCutInfP1x, cmd.trimCutInfP1y, lx, ly, midx, midy, &previewLines);
    }
    {
      // Where an armed gizmo drag would put a solid FACE (issue #148 acceptance 4). The ghost
      // channel, with every other "this is what you would get" preview - the same reasoning that
      // put the transform ghost there rather than in the highlight.
      std::vector<float> faceGhost;
      BuildSubObjectFaceGhost(cmd, &faceGhost);
      previewLines.insert(previewLines.end(), faceGhost.begin(), faceGhost.end());
    }

    std::vector<float> highlightLines;
    std::vector<float> highlightCircles;
    BuildSelectionHighlight(cmd, &highlightLines, &highlightCircles);

    // The sub-object selection (REQ-318 item 11) and the pre-highlight of what a Ctrl click would
    // take (item 14). Their edge and vertex linework is APPENDED to the ordinary highlight and hover
    // channels — which is what gives each the never-occluded treatment and the colour it should
    // have, without a new channel for either. Only the face tints need a channel of their own,
    // because only they are depth-tested.
    CadSubObjectOverlay subObjectOverlay;
    {
      std::vector<float> subObjectLines;
      BuildSubObjectHighlight(cmd, &subObjectOverlay.selectedFaceTris, &subObjectOverlay.selectedFaceEdges,
                              &subObjectLines);
      highlightLines.insert(highlightLines.end(), subObjectLines.begin(), subObjectLines.end());
    }

    std::vector<float> hoverLines;
    std::vector<float> hoverCircles;
    if (cmd.activeSpaceIndex == kModelSpaceIndex) { // no model-entity hover in paper space (incl. floating)
      BuildHoverHighlight(cmd, &hoverLines, &hoverCircles);
      // The sub-object pre-highlight rides the same channel, so a hovered edge or vertex gets the
      // hover blue rather than the selection yellow with no second colour to define. `CadUi` has
      // already suppressed the entity hover while Ctrl is held, so the two cannot both be here.
      std::vector<float> subHoverLines;
      BuildSubObjectHoverHighlight(cmd, &subObjectOverlay.hoverFaceTris, &subObjectOverlay.hoverFaceEdges,
                                   &subHoverLines);
      hoverLines.insert(hoverLines.end(), subHoverLines.begin(), subHoverLines.end());
    }

    // The translate gizmo (REQ-060, GitHub issue #148 Phase 5 slice 4b), and the ghost of what an
    // armed drag is about to do. The ghost rides the ordinary PREVIEW channel — that channel already
    // means "what is about to happen" for MOVE, ROTATE and OFFSET, and a gizmo drag is the same
    // statement made with a handle instead of two typed points.
    CadGizmoOverlay gizmoOverlay;
    BuildGizmoOverlay(cmd, &gizmoOverlay);
    if (cmd.gizmoDragActive) {
      std::vector<float> ghostLines;
      std::vector<float> ghostCircles;
      BuildGizmoDragGhost(cmd, &ghostLines, &ghostCircles);
      previewLines.insert(previewLines.end(), ghostLines.begin(), ghostLines.end());
      previewCircles.insert(previewCircles.end(), ghostCircles.begin(), ghostCircles.end());
    }

    std::vector<float> surveyMarkers;
    if (!cmd.surveyPoints.empty())
    {
      const float surveyCrossHalf =
          SurveyPointCrossHalfWorldFromPaper(cmd.surveyPointCrossSpanPlottedInches, cmd.modelUnitsPerPlottedInch);
      // Screen-facing marker crosses (REQ-058 / GAP-2): a survey X is a marker, not geometry, so it
      // is built in the camera's plane. Plan view gives world +X/+Y and the pre-3D geometry back.
      const Camera markerCam = CadViewCamera(cmd);
      const ray3d::Vec3 mr = markerCam.RightWorld();
      const ray3d::Vec3 mu = markerCam.UpWorld();
      MarkerBillboardBasis markerBasis;
      markerBasis.rightX = static_cast<float>(mr.x);
      markerBasis.rightY = static_cast<float>(mr.y);
      markerBasis.rightZ = static_cast<float>(mr.z);
      markerBasis.upX = static_cast<float>(mu.x);
      markerBasis.upY = static_cast<float>(mu.y);
      markerBasis.upZ = static_cast<float>(mu.z);
      AppendAllSurveyPointMarkers(surveyCrossHalf, cmd.surveyPoints, &surveyMarkers, markerBasis);
    }

    CadExtendedGeometryInput ext{};
    ext.arcs = &cmd.userArcs;
    ext.arcAttrs = &cmd.userArcAttrs;
    ext.circleNormals = &cmd.userCircleNormals;  // REQ-312
    ext.ellipses = &cmd.userEllipses;
    ext.ellAttrs = &cmd.userEllAttrs;
    ext.polylineVerts = &cmd.userPolylineVerts;
    ext.polylineOffsets = &cmd.userPolylineOffsets;
    ext.polylineClosed = &cmd.userPolylineClosed;
    ext.polylineAttrs = &cmd.userPolylineAttrs;
    ext.polylineBulge = &cmd.userPolylineVertsBulge;  // REQ-316 / ADR-047
    ext.featureLineVerts = &cmd.featureLineVerts;      // REQ-087
    ext.featureLineOffsets = &cmd.featureLineOffsets;
    ext.featureLineClosed = &cmd.featureLineClosed;
    ext.featureLineAttrs = &cmd.featureLineAttrs;
    ext.drawingLayers = &cmd.drawingLayerTable;
    ext.hiddenEntityIds = &cmd.hiddenEntityIds;  // object isolation (REQ-084 (d) / ADR-034)
    ext.blockDefs = &cmd.blockDefs;
    ext.blockRefs = &cmd.cadBlockRefs;
    ext.blockRefAttrs = &cmd.cadBlockRefAttrs;

    activeRenderer.SetSize(fbW, fbH);
    RenderTuning tuning{};
    tuning.arcCircleSmoothnessCap = std::clamp(cmd.displayArcCircleSmoothness, 8, 20000);
    tuning.hardwareAcceleration = cmd.systemHardwareAcceleration;
    tuning.smoothLineDisplay = cmd.gfxSmoothLineDisplay;
    tuning.visualStyle = cmd.viewportVisualStyle;  // REQ-064
    tuning.bgR = std::clamp(cmd.viewportBgR, 0.f, 1.f);
    tuning.bgG = std::clamp(cmd.viewportBgG, 0.f, 1.f);
    tuning.bgB = std::clamp(cmd.viewportBgB, 0.f, 1.f);
    // Build PDF render list: committed attachments + cursor-follow preview when picking insert point.
    std::vector<PdfAttachment> pdfRenderList;
    if (!cmd.pdfAttachments.empty())
      pdfRenderList = cmd.pdfAttachments; // shallow copy of the vector (texIds stay valid)

    // Live preview for selected PDF underlays during MOVE/COPY/ROTATE/SCALE.
    {
      using K = AppCommandState::Kind;
      using MP = AppCommandState::ModifyPhase;
      const bool isMove = (cmd.active == K::Move || cmd.active == K::Copy) &&
                          cmd.modifyPhase == MP::NeedDestination;
      const bool isRotate = cmd.active == K::Rotate;
      const bool isScale = cmd.active == K::Scale && cmd.modifyPhase == MP::NeedDestination;

      float dx = 0.f, dy = 0.f, theta = 0.f, sc = 1.f;
      bool hasPdfPreview = false;
      if (isMove)
      {
        dx = previewCx - cmd.modifyBaseX;
        dy = previewCy - cmd.modifyBaseY;
        hasPdfPreview = true;
      }
      else if (isRotate)
      {
        hasPdfPreview = CadRotatePreviewTheta(cmd, previewCx, previewCy, &theta);
      }
      else if (isScale)
      {
        hasPdfPreview = CadScalePreviewFactor(cmd, previewCx, previewCy, &sc);
      }

      if (hasPdfPreview)
      {
        constexpr float kRadToDeg = 180.f / 3.14159265f;
        const float cosT = std::cos(theta);
        const float sinT = std::sin(theta);
        const float bx = isRotate ? cmd.rotateBaseX : cmd.modifyBaseX;
        const float by = isRotate ? cmd.rotateBaseY : cmd.modifyBaseY;
        for (const auto &e : cmd.selection)
        {
          if (e.type != SelectedEntity::Type::PdfUnderlay)
            continue;
          const size_t idx = static_cast<size_t>(e.index);
          if (e.index < 0 || idx >= pdfRenderList.size())
            continue;
          PdfAttachment prev = pdfRenderList[idx]; // copy — modified insertX/Y/rot/scale only
          if (isMove)
          {
            prev.insertX += dx;
            prev.insertY += dy;
          }
          else if (isRotate)
          {
            const float rpx = prev.insertX - bx;
            const float rpy = prev.insertY - by;
            prev.insertX = bx + cosT * rpx - sinT * rpy;
            prev.insertY = by + sinT * rpx + cosT * rpy;
            prev.rotationDeg += theta * kRadToDeg;
          }
          else
          { // scale
            prev.insertX = bx + sc * (prev.insertX - bx);
            prev.insertY = by + sc * (prev.insertY - by);
            prev.scale = std::max(prev.scale * sc, 1e-9f);
          }
          prev.fade *= 0.6f; // dim the preview to distinguish it from the original
          pdfRenderList.push_back(std::move(prev));
        }
      }
    }

    // Viewport picks land during DrawDrawingViewport, which is AFTER the refresh at the top of
    // the frame. WATERDROP/CATCHMENT write lastWaterDropPathXyz there; a second assemble pass
    // (cache early-out, cheap) is what puts the preview in this frame's GL batches (REQ-133).
    RefreshSurfaceDisplayGeometry(cmd);

    // Paper space (REQ-025) renders a blank sheet this increment; model geometry shows through
    // viewports in a later increment. The sheet outline itself is drawn as a UI overlay.
    // Paper space (incl. floating-viewport editing) renders a blank model scene; the sheet, viewports,
    // and any in-place edit previews are drawn as UI overlays. All model-interaction visuals (hover,
    // highlight, preview, rubber, snap glyph, selection rect, survey markers, PDFs) are suppressed here so
    // leftover DXF geometry isn't hover-highlighted behind the sheet.
    const bool paperSpace = cmd.activeSpaceIndex != kModelSpaceIndex;

    // REQ-073 amendment: the Volume Dashboard's cut/fill map (TASK-095 §6 step 5), model space only
    // like every other GL surface entity. Built fresh each frame from the dashboard's own landed
    // buffers — cheap (pointers + colours, never vertices) and safe to redo every frame the same way
    // RefreshSurfaceDisplayGeometry's own assembly pass is, since visibility (here: is the map even
    // wanted) can change with no new geometry landing.
    VolumeMapDisplayGeometry volumeMapGeom;
    if (cmd.volumeDashboard.open && cmd.volumeDashboard.showMap && cmd.volumeDashboard.resultHasMap) {
      if (!cmd.volumeDashboard.mapCutTrianglesXyz.empty()) {
        volumeMapGeom.cut.verts = &cmd.volumeDashboard.mapCutTrianglesXyz;
        // Distinct from REQ-072's band palette on purpose — a cut/fill comparison and an elevation/
        // slope band are different questions, and sharing a palette invites reading one as the other.
        volumeMapGeom.cut.rgba[0] = 0.85f; volumeMapGeom.cut.rgba[1] = 0.35f;
        volumeMapGeom.cut.rgba[2] = 0.15f; volumeMapGeom.cut.rgba[3] = 0.85f;  // cut: orange-red
      }
      if (!cmd.volumeDashboard.mapFillTrianglesXyz.empty()) {
        volumeMapGeom.fill.verts = &cmd.volumeDashboard.mapFillTrianglesXyz;
        volumeMapGeom.fill.rgba[0] = 0.15f; volumeMapGeom.fill.rgba[1] = 0.45f;
        volumeMapGeom.fill.rgba[2] = 0.90f; volumeMapGeom.fill.rgba[3] = 0.85f;  // fill: blue
      }
    }

    // REQ-308: the Start tab owns no drawing — nothing to render into its (phantom) FBO, and the
    // start screen is drawn by DrawDrawingViewport as ImGui. Skip the scene pass entirely.
    const bool startTab = (cmd.activeDrawingIdx == 0);

    static const std::vector<float> kEmptyVerts;
    const std::vector<float> &sceneLines = paperSpace ? kEmptyVerts : cmd.userLinesFlat;
    const std::vector<float> &sceneCircles = paperSpace ? kEmptyVerts : cmd.userCirclesCxCyZR;
    const std::vector<float> &sceneRubber = paperSpace ? kEmptyVerts : rubberLines;
    // The camera is derived from the canonical pan/zoom plus the two orientation angles, so it
    // cannot disagree with the view state (REQ-058 / ADR-025 (c)).
    if (!startTab) {
    // Held in a named local because RenderScene takes a pointer to it and the call outlives any
    // temporary: the grid frame must still be alive when the renderer reads it.
    const ucs::Ucs ucsGridFrame = CadActiveUcsStorage(cmd);
    const auto perfRenderT0 = std::chrono::steady_clock::now();
    activeRenderer.RenderScene(CadViewCamera(cmd), fbW, fbH, sceneLines,
                               sceneCircles, cmd.cadGpuRevision,
                               sceneRubber, (paperSpace || !snapHit.valid) ? nullptr : &snapHit,
                               std::clamp(cmd.objectSnapGlyphHalfPx, 3.f, 48.f),
                               (paperSpace || previewLines.empty()) ? nullptr : &previewLines,
                               (paperSpace || previewCircles.empty()) ? nullptr : &previewCircles,
                               (paperSpace || highlightLines.empty()) ? nullptr : &highlightLines,
                               (paperSpace || highlightCircles.empty()) ? nullptr : &highlightCircles,
                               (paperSpace || hoverLines.empty()) ? nullptr : &hoverLines,
                               (paperSpace || hoverCircles.empty()) ? nullptr : &hoverCircles,
                               (paperSpace || surveyMarkers.empty()) ? nullptr : &surveyMarkers, &cmd.userLineAttrs,
                               &cmd.userCircleAttrs, &ext, gridVisible, &cmd.drawingLayerTable, tuning,
                               paperSpace ? nullptr : (pdfRenderList.empty() ? nullptr : &pdfRenderList),
                               // Paper space: skip GL geometry; the ImGui overlay draws the sheet + viewports.
                               cmd.activeSpaceIndex,
                               (paperSpace || cmd.cadFilledRegions.empty()) ? nullptr : &cmd.cadFilledRegions,
                               (paperSpace || cmd.cadFilledRegionAttrs.empty()) ? nullptr : &cmd.cadFilledRegionAttrs,
                               // Meshes are model-space only, like every other GL entity (REQ-063).
                               (paperSpace || cmd.cadMeshes.empty()) ? nullptr : &cmd.cadMeshes,
                               (paperSpace || cmd.cadMeshAttrs.empty()) ? nullptr : &cmd.cadMeshAttrs,
                               // B-rep solids (REQ-313), model space only like every GL entity. The
                               // batches borrow buffers owned by `cmd`, which outlives this call.
                               (paperSpace || cmd.solidDisplayGeometry.empty())
                                   ? nullptr
                                   : &cmd.solidDisplayGeometry,
                               // Generated surface geometry (REQ-068/REQ-070), model space only like
                               // every GL entity. The batches borrow buffers owned by `cmd`, which
                               // outlives this call — and nothing between the refresh above and here
                               // regenerates them.
                               (paperSpace || cmd.surfaceDisplayGeometry.empty())
                                   ? nullptr
                                   : &cmd.surfaceDisplayGeometry,
                               (paperSpace || volumeMapGeom.empty()) ? nullptr : &volumeMapGeom,
                               (paperSpace || removalLines.empty()) ? nullptr : &removalLines,
                               (paperSpace || removalMarkers.empty()) ? nullptr : &removalMarkers,
                               // The grid follows the UCS (REQ-154). Storage space, like the camera
                               // and every vertex the renderer receives. Paper space keeps its own
                               // 2D sheet grid and is deliberately excluded.
                               paperSpace ? nullptr : &ucsGridFrame,
                               // The selected and hovered solid FACE tints (REQ-318 items 11 and
                               // 14). Model space only, like every other GL overlay here; the edges
                               // and vertices of both went into the highlight and hover line
                               // channels above and are drawn never-occluded, while these two are
                               // depth-tested — see the renderer's own note at the draw.
                               (paperSpace || subObjectOverlay.empty()) ? nullptr : &subObjectOverlay,
                               // The gizmo handles. Model space only for the same reason — a paper
                               // sheet is 2D (ADR-025 (g)) and has no third axis to offer.
                               (paperSpace || gizmoOverlay.empty()) ? nullptr : &gizmoOverlay);
    cmd.perfRenderMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perfRenderT0).count();

    // REQ-308: after a drawing is opened or saved, its first rendered frame is captured as the
    // Recent-list thumbnail. No-op unless a capture is pending for this exact tab.
    ServicePendingThumbnail(cmd, activeRenderer);
    }

    // Frame profiler overlay (PERFHUD) — after the render so its render-ms is this frame's, not
    // last frame's. No-op unless toggled on. Before DrawFloatingWindowChrome for the same reason
    // every other window is.
    DrawPerfHud(cmd);

    // Must be the last UI call of the frame: it walks the submitted windows and
    // appends to their draw lists, so anything begun after it would be missed.
    DrawFloatingWindowChrome();

    ImGui::Render();
    int displayW = 0;
    int displayH = 0;
    glfwGetFramebufferSize(window, &displayW, &displayH);
    glViewport(0, 0, displayW, displayH);
    glClearColor(0.06f, 0.06f, 0.07f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);

    // Reveal the main window only after its first real frame is on screen (see the hide in the
    // startup sequence) — this is what makes the splash -> app hand-off show no stretched splash.
    if (!mainWindowShown) {
      glfwShowWindow(window);
      glfwFocusWindow(window);
      mainWindowShown = true;
    }
#ifdef GOSURVEY_DEVELOPER_SHELL
    DevShell_PostSwap(devEngine);
#endif
  }

  // Join any in-flight surface rebuild (REQ-069) here rather than leaving it to `cmd`'s destructor.
  // The destructor is the actual safety net — it covers every path — but it runs after
  // glfwTerminate(), so a slow triangulation would be waited out with the window already gone,
  // which reads as a hang. Waiting here keeps the window up until the worker is done.
  cmd.surfaceRebuildAsync.clear();

  // Silently persist settings, preferences, and the current dock layout.
  SaveUserStartupPrefs(cmd);
  {
    const ImGuiIO &ioSave = ImGui::GetIO();
    if (ioSave.IniFilename && ioSave.IniFilename[0])
      ImGui::SaveIniSettingsToDisk(ioSave.IniFilename);
  }

#ifdef GOSURVEY_DEVELOPER_SHELL
  int devshellExit = 0;
  const bool devshellCliDone = DevShell_CliRunFinished(&devshellExit);
  DevShell_Stop(devEngine);
#endif
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  CadUiClearMenuBarLogo();
  DestroyAppLogoGpu(&appLogo);
  ImGui::DestroyContext();
#ifdef GOSURVEY_DEVELOPER_SHELL
  DevShell_DestroyContext(devEngine);
#endif

  // Release PDF textures before GL context is destroyed.
  for (auto &att : cmd.pdfAttachments)
    PdfAttach_ReleaseTexture(att);
  if (cmd.pdfDraftCache)
  {
    PdfDraftCache_Free(cmd.pdfDraftCache);
    cmd.pdfDraftCache = nullptr;
  }

  for (auto &r : viewportRenderers)
    r->Shutdown();
  PdfAttach_Shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
#ifdef GOSURVEY_DEVELOPER_SHELL
  if (devshellCliDone)
    return devshellExit;
#endif
  return 0;
}
