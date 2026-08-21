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
#include "CadCoordinateFrame.hpp"
#include "CadRubberPreview.hpp"
#include "TransformPreview.hpp"
#include "CadUi.hpp"
#include "PdfAttachDialog.hpp"
#include "ViewportRenderer.hpp"
#include "CadSnap.hpp"
#include "PdfAttach.hpp"
#include "SurveyPoints.hpp"
#include "AppIcon.hpp"
#include "GsIo.hpp"
#include "SplashScreen.hpp"
#include "UserPrefs.hpp"
#include "ImGuiLayout.hpp"
#include "UpdateService.hpp"
#include "TelemetryService.hpp"
#include "Version.hpp"

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
#include <string>
#include <vector>

namespace
{

  /// Returns the first command-line argument naming an existing file, as UTF-8, or empty.
  ///
  /// BUG-012: the installer registers `.gs` with `shell\open\command = "...GoSurvey.exe" "%1"`,
  /// so Explorer has always passed the path — and `main()` took no arguments, so it was silently
  /// dropped and double-clicking a drawing opened an empty session.
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
      if (!LoadGoSurveyFile(cmd, u8.c_str(), boot))
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
        if (LoadGoSurveyFile(cmd, u8.c_str(), boot))
        {
          appendLines(boot);
          return;
        }
        appendLines(boot);
        cmdLog.push_back("Startup: custom template failed to load; trying bundled default-template.gs.");
      }
      else
      {
        cmdLog.push_back("Startup: custom template path not found; trying bundled default-template.gs.");
      }
    }

    if (tryLoadPath(ResolveDefaultWorkspaceTemplateGsPath()))
      return;
    cmdLog.push_back("Startup: bundled default-template.gs not found; starting with an empty drawing.");
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

  GLFWwindow *window = glfwCreateWindow(1600, 900, "GoSurvey", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    return 1;
  }
  glfwDefaultWindowHints();
  glfwMaximizeWindow(window);

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

  // RunStartupSplash(window, 1.0);
  GlfwApplyMainStageWindowChrome(window);

  AppCommandState cmd;
  LoadUserStartupPrefs(cmd);

  // REQ-077: the update check. Only the persisted settings live in AppCommandState; the worker
  // state is owned here, so no drawing state is ever touched from a background thread.
  update::UpdateState updateState;
  updateState.prefs = cmd.updatePrefs;
  // Runs on EVERY launch (no throttle) and gates the session: the dialog stays modal until it
  // resolves. Does nothing at all when the user has switched the check off.
  update::BeginStartupCheck(updateState, "chetjones003/GoSurvey");
  /// Set once the user has confirmed an update and the app is exiting to hand over to the
  /// installer, so the normal quit path can tell the two cases apart.
  bool updateExitPending = false;

  // REQ-080: anonymous telemetry. Fire-and-forget: does not gate the session, no UI, logs and
  // swallows network errors. Runs independent of the update check in its own worker.
  std::unique_ptr<telemetry::TelemetryTask> telemetryTask =
      telemetry::BeginTelemetryPing(GOSURVEY_VERSION_FULL,
                                    cmd.updatePrefs.useBetaChannel ? "beta" : "stable");
  const bool haveSavedDockIni = ImGuiLayout_ConfigureIniPath(cmd);
  std::vector<std::string> cmdLog;
  cmdLog.push_back("GoSurvey CAD shell ready.");
  cmdLog.push_back("Regenerating model.");
  cmdLog.push_back("Drawing Created.");
  cmdLog.push_back("JSON database - ready...");
  // BUG-012: a drawing passed on the command line wins over the startup template. Someone who
  // double-clicked a .gs wants that drawing, not a blank sheet built from their template.
  bool openedFromCommandLine = false;
  {
    const std::string startupFile = FirstExistingFileArgumentUtf8();
    if (!startupFile.empty())
    {
      std::vector<std::string> boot;
      openedFromCommandLine = LoadGoSurveyFile(cmd, startupFile.c_str(), boot);
      for (auto &s : boot)
        cmdLog.push_back(std::move(s));
      // A file that will not open falls back to the normal startup path rather than leaving the
      // user staring at nothing, but it says so — REQ-201, and silence here would look identical
      // to the bug this fixes.
      if (!openedFromCommandLine)
        cmdLog.push_back("Could not open " + startupFile + "; starting from the usual template.");
    }
  }
  if (!openedFromCommandLine)
    TryLoadStartupWorkspaceTemplate(cmd, cmdLog);
  cmd.activeDocSavedRevision = cmd.cadGpuRevision; // loaded drawing/template counts as "clean"
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
  // 130 of tools + the 9px gutter DrawRibbonBar leaves under the panel titles,
  // so the buttons keep the size they had before the gutter was added.
  const float ribbonH = 139.f;
  bool orthoEnabled = false;  // REQ-047: ORTHO is off by default (AutoCAD convention) — free-angle drawing
  bool gridVisible = false;
  // prevDrawingIdx lives in cmd — no local needed.

  // REQ-100 bench state that belongs to the loop rather than to the command layer.
  bool benchVsyncOff = false;
  double benchPrevTime = 0.0;

  while (true)
  {
    glfwPollEvents();

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
          cmd.bench.frameMs.push_back((nowT - benchPrevTime) * 1000.0);
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
    if (ImGui::IsKeyPressed(ImGuiKey_F8, false))
      orthoEnabled = !orthoEnabled;
    cmd.orthoMode = orthoEnabled;

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

    // TIN surface triangle edges (REQ-068), rebuilt only when the geometry revision moves. At the
    // REQ-100 surface density this buffer is ~600k segments; regenerating it every frame would
    // spend the frame budget re-deriving something whose input has not changed.
    static std::vector<float> surfaceEdges;
    static std::uint32_t surfaceEdgesRevision = 0xFFFFFFFFu;
    static int surfaceEdgesTab = -1;
    if (surfaceEdgesRevision != cmd.cadGpuRevision || surfaceEdgesTab != cmd.activeDrawingIdx) {
      surfaceEdgesRevision = cmd.cadGpuRevision;
      surfaceEdgesTab = cmd.activeDrawingIdx;  // two tabs can sit at the same revision
      surfaceEdges.clear();
      AppendSurfaceEdgeLines(cmd, &surfaceEdges);
    }

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

    // Generated surface display geometry (ADR-036 (e)). After TickSurfaceRebuilds, so a
    // triangulation that landed this frame is drawn from this frame rather than the next — and
    // after EnsureEntityIds for the same reason it is: the cache is keyed on the stable id.
    // Gated on its own staleness key, not on cadGpuRevision, which is what stops an unrelated edit
    // from regenerating every surface's geometry (REQ-070: a style change must not retriangulate,
    // and a line drawn elsewhere must not re-contour).
    RefreshSurfaceDisplayGeometry(cmd);

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

    DrawPropertiesPanel(cmd, &cmdLog);

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
      SaveDocumentToSnapshot(cmd, cmd.prevDrawingIdx);
      RestoreDocumentFromSnapshot(cmd, cmd.activeDrawingIdx);
      cmd.prevDrawingIdx = cmd.activeDrawingIdx;
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
    DrawUnitsDialog(cmd, &cmdLog);
    DrawRightClickCustomizationDialog(cmd, &cmdLog);  // REQ-084 (a)
    ImGuiLayout_DrawLayoutPopups(cmd, cmdLog);
    DrawLayerManagerWindow(cmd, &cmdLog);
    DrawTextStyleManagerWindow(cmd, &cmdLog);
    DrawPointGroupManagerWindow(cmd, &cmdLog);
    DrawSurfaceManagerWindow(cmd, &cmdLog);
    DrawFeatureLineElevationWindow(cmd, &cmdLog);  // REQ-088
    DrawViewPointsPanel(cmd, cmdLog);
    DrawImportPointsPanel(cmd, cmdLog);
    DrawExportPointsPanel(cmd, cmdLog);
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
    DrawAlignResultsWindow(cmd, cmdLog);
    DrawCloseConfirmModal(cmd, cmdLog);
    DrawUpdateDialog(cmd, updateState);
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

    if (cmd.active == AppCommandState::Kind::Trim &&
        cmd.trimPhase == AppCommandState::TrimPhase::CuttingLine_WaitP2)
    {
      // TRIM's cutting-line points commit through commitX/commitY too (SubmitTrimViewportPick).
      float lx = static_cast<float>(commitCurX);
      float ly = static_cast<float>(commitCurY);
      ApplyOrthoConstrainFromAnchor(cmd.trimCutInfP1x, cmd.trimCutInfP1y, &lx, &ly, orthoEnabled);
      PushRubberSegViewRel(rubberLines, cmd.trimCutInfP1x, cmd.trimCutInfP1y, lx, ly, cmd.viewportPanX,
                           cmd.viewportPanY);
      const float midx = (cmd.trimCutInfP1x + lx) * 0.5f;
      const float midy = (cmd.trimCutInfP1y + ly) * 0.5f;
      CadTrimAppendCutLineRemovedPreview(cmd, cmd.trimCutInfP1x, cmd.trimCutInfP1y, lx, ly, midx, midy, &previewLines);
    }

    std::vector<float> highlightLines;
    std::vector<float> highlightCircles;
    BuildSelectionHighlight(cmd, &highlightLines, &highlightCircles);

    std::vector<float> hoverLines;
    std::vector<float> hoverCircles;
    if (cmd.activeSpaceIndex == kModelSpaceIndex) // no model-entity hover in paper space (incl. floating)
      BuildHoverHighlight(cmd, &hoverLines, &hoverCircles);

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
    ext.ellipses = &cmd.userEllipses;
    ext.ellAttrs = &cmd.userEllAttrs;
    ext.polylineVerts = &cmd.userPolylineVerts;
    ext.polylineOffsets = &cmd.userPolylineOffsets;
    ext.polylineClosed = &cmd.userPolylineClosed;
    ext.polylineAttrs = &cmd.userPolylineAttrs;
    ext.featureLineVerts = &cmd.featureLineVerts;      // REQ-087
    ext.featureLineOffsets = &cmd.featureLineOffsets;
    ext.featureLineClosed = &cmd.featureLineClosed;
    ext.featureLineAttrs = &cmd.featureLineAttrs;
    ext.drawingLayers = &cmd.drawingLayerTable;
    ext.hiddenEntityIds = &cmd.hiddenEntityIds;  // object isolation (REQ-084 (d) / ADR-034)

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

    // Paper space (REQ-025) renders a blank sheet this increment; model geometry shows through
    // viewports in a later increment. The sheet outline itself is drawn as a UI overlay.
    // Paper space (incl. floating-viewport editing) renders a blank model scene; the sheet, viewports,
    // and any in-place edit previews are drawn as UI overlays. All model-interaction visuals (hover,
    // highlight, preview, rubber, snap glyph, selection rect, survey markers, PDFs) are suppressed here so
    // leftover DXF geometry isn't hover-highlighted behind the sheet.
    const bool paperSpace = cmd.activeSpaceIndex != kModelSpaceIndex;
    static const std::vector<float> kEmptyVerts;
    const std::vector<float> &sceneLines = paperSpace ? kEmptyVerts : cmd.userLinesFlat;
    const std::vector<float> &sceneCircles = paperSpace ? kEmptyVerts : cmd.userCirclesCxCyZR;
    const std::vector<float> &sceneRubber = paperSpace ? kEmptyVerts : rubberLines;
    // The camera is derived from the canonical pan/zoom plus the two orientation angles, so it
    // cannot disagree with the view state (REQ-058 / ADR-025 (c)).
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
                               // TIN surface edges (REQ-068), model space only like every GL entity.
                               (paperSpace || surfaceEdges.empty()) ? nullptr : &surfaceEdges);

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

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  CadUiClearMenuBarLogo();
  DestroyAppLogoGpu(&appLogo);
  ImGui::DestroyContext();

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
  return 0;
}
