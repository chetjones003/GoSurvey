#include "CadCommands.hpp"
#include "CadRubberPreview.hpp"
#include "TransformPreview.hpp"
#include "CadUi.hpp"
#include "ViewportRenderer.hpp"
#include "CadSnap.hpp"
#include "SurveyPoints.hpp"
#include "AppIcon.hpp"
#include "GsIo.hpp"
#include "SplashScreen.hpp"
#include "UserPrefs.hpp"
#include "ImGuiLayout.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

static void TryLoadStartupWorkspaceTemplate(AppCommandState& cmd, std::vector<std::string>& cmdLog) {
  namespace fs = std::filesystem;
  auto appendLines = [&](std::vector<std::string>& lines) {
    for (auto& s : lines)
      cmdLog.push_back(std::move(s));
  };
  auto tryLoadPath = [&](const fs::path& p) -> bool {
    if (p.empty() || !fs::exists(p))
      return false;
    std::vector<std::string> boot;
    const std::string u8 = p.u8string();
    if (!LoadGoSurveyFile(cmd, u8.c_str(), boot))
      return false;
    appendLines(boot);
    return true;
  };

  if (cmd.defaultWorkspaceTemplatePathUtf8[0] != '\0') {
    const fs::path custom(cmd.defaultWorkspaceTemplatePathUtf8);
    if (!custom.empty() && fs::exists(custom)) {
      std::vector<std::string> boot;
      const std::string u8 = custom.u8string();
      if (LoadGoSurveyFile(cmd, u8.c_str(), boot)) {
        appendLines(boot);
        return;
      }
      appendLines(boot);
      cmdLog.push_back("Startup: custom template failed to load; trying bundled default-template.gs.");
    } else {
      cmdLog.push_back("Startup: custom template path not found; trying bundled default-template.gs.");
    }
  }

  if (tryLoadPath(ResolveDefaultWorkspaceTemplateGsPath()))
    return;
  cmdLog.push_back("Startup: bundled default-template.gs not found; starting with an empty drawing.");
}

} // namespace

static void GlfwErrorCallback(int error, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int main() {
  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit())
    return 1;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GlfwApplySplashStageWindowHints();

  GLFWwindow* window = glfwCreateWindow(1600, 900, "GoSurvey", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwDefaultWindowHints();
  glfwMaximizeWindow(window);

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  ViewportRenderer viewport;
  if (!viewport.Init()) {
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
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigInputTextEnterKeepActive = false; // CAD shell: Enter submits without selecting-all next keystroke

  ApplyCadDarkTheme();
  if (!LoadApplicationFont())
    std::fprintf(stderr, "Calibri not found; using ImGui default font.\n");
  io.FontGlobalScale = 1.05f;

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  //RunStartupSplash(window, 1.0);
  GlfwApplyMainStageWindowChrome(window);

  AppCommandState cmd;
  LoadUserStartupPrefs(cmd);
  const bool haveSavedDockIni = ImGuiLayout_ConfigureIniPath(cmd);
  std::vector<std::string> cmdLog;
  cmdLog.push_back("GoSurvey CAD shell ready.");
  cmdLog.push_back(
      "LINE/L … SURVEY: CREATEPOINTS (CRTPTS), VIEWPOINTS (VWPTS), IMPORTPOINTS (IMPPTS), EXPORTPOINTS (EXPPTS), "
      "INVERSE (INV), "
      "JSON database — idle: two-click select. MMB "
      "pan.");
  TryLoadStartupWorkspaceTemplate(cmd, cmdLog);
  char cmdBuf[4096]{};

  float panX = 0.f;
  float panY = 0.f;
  float zoom = 1.f;
  float curX = 0.f;
  float curY = 0.f;
  float curRawX = 0.f;
  float curRawY = 0.f;
  int fbW = 900;
  int fbH = 650;

  bool dockLayoutDone = haveSavedDockIni;
  const float ribbonH = 130.f;
  bool orthoEnabled = true;
  bool gridVisible = true;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& ioFrame = ImGui::GetIO();
    if (!ioFrame.WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
        cmd.objectSnapEnabled = !cmd.objectSnapEnabled;
      if (ImGui::IsKeyPressed(ImGuiKey_F8, false))
        orthoEnabled = !orthoEnabled;
    }
    cmd.orthoMode = orthoEnabled;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      if (cmd.copySurveyDupModalOpen) {
        ApplyCopySurveyDuplicateModalResult(cmd, false, cmdLog);
        cmdBuf[0] = '\0';
      } else if (cmd.mtextRichEditorOpen) {
        CancelMtextRichEditor(cmd, &cmdLog);
        cmdBuf[0] = '\0';
      } else if (cmd.mtextGripMoveActive) {
        AbortMtextGripInteraction(cmd);
        BumpCadGpuCache(cmd);
        cmdBuf[0] = '\0';
        cmdLog.push_back("MTEXT grip edit canceled.");
      } else if (cmd.entityGripMoveActive) {
        RestoreEntityGripOriginal(cmd);
        ClearEntityGripInteraction(cmd);
        BumpCadGpuCache(cmd);
        cmdBuf[0] = '\0';
        cmdLog.push_back("Grip edit canceled.");
      } else if (cmd.active != AppCommandState::Kind::None) {
        using SAP = AppCommandState::SegmentAnglePickPhase;
        if ((cmd.active == AppCommandState::Kind::Line || cmd.active == AppCommandState::Kind::Polyline) &&
            cmd.segmentAnglePickPhase != SAP::Idle)
          CancelSegmentAnglePick(cmd, &cmdLog);
        else
          CancelActiveCommand(cmd, cmdLog);
        cmdBuf[0] = '\0';
      } else {
        const bool hadCreatePtsUi = cmd.showCreatePointsWindow || cmd.createPointsPlacementActive;
        cmd.showCreatePointsWindow = false;
        cmd.createPointsPlacementActive = false;
        ClearSelection(cmd);
        cmd.pendingZoomExtents = false;
        cmd.pendingZoomWindow = false;
        cmdBuf[0] = '\0';
        cmdLog.push_back(hadCreatePtsUi ? "Selection cleared; CREATEPOINTS closed." : "Selection cleared.");
      }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput) {
      StartDeleteCommand(cmd, cmdLog);
    }

    const ImGuiViewport* mainVp = ImGui::GetMainViewport();
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
      if (ImGui::BeginMenuBar()) {
        DrawMainMenuBar(cmd, cmdLog);
        ImGui::EndMenuBar();
      }
      ImGui::EndChild();
    }
#else
    if (ImGui::BeginMenuBar()) {
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

    if (cmd.pendingBuiltinDockLayoutReset) {
      cmd.pendingBuiltinDockLayoutReset = false;
      SetupMainDockLayout(dockspaceId, dockHostSize);
      ImGuiIO& ioDock = ImGui::GetIO();
      if (ioDock.IniFilename && ioDock.IniFilename[0])
        ImGui::SaveIniSettingsToDisk(ioDock.IniFilename);
      dockLayoutDone = true;
      cmdLog.push_back("UI layout: reset to built-in dock split (saved to current layout .ini).");
    } else if (!dockLayoutDone) {
      dockLayoutDone = true;
      SetupMainDockLayout(dockspaceId, dockHostSize);
    }

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar(3);

    DrawPropertiesPanel(cmd, &cmdLog);

    CadSnap::Hit snapHit{};
    DrawDrawingViewport(viewport.ColorTexture(), cmd, cmdLog, cmdBuf, static_cast<int>(sizeof(cmdBuf)), &panX,
                        &panY, &zoom, &curX, &curY, &curRawX, &curRawY, &fbW, &fbH, &snapHit);
    cmd.uiCursorWorldX = curX;
    cmd.uiCursorWorldY = curY;
    {
      ImGuiIO& ioDim = ImGui::GetIO();
      if (!ioDim.WantTextInput && cmd.active == AppCommandState::Kind::DimLinear &&
          cmd.dimPhase == AppCommandState::DimPhase::WaitDimLinePt) {
        if (ImGui::IsKeyPressed(ImGuiKey_H, false))
          CadDimLinearApplyHVHotkey(cmd, false, cmdLog);
        else if (ImGui::IsKeyPressed(ImGuiKey_V, false))
          CadDimLinearApplyHVHotkey(cmd, true, cmdLog);
      }
    }
    DrawCommandLinePanel(cmdLog, cmdBuf, static_cast<int>(sizeof(cmdBuf)), cmd);
    DrawCadStatusBarStrip(cmd, curX, curY, 0.f, &orthoEnabled, &gridVisible);

    // LINE/POLYLINE AP: after two picks the bottom command InputText is hidden — Enter must still lock bearing.
    // Keyboard-only "A" then bearing: Enter with empty buffer cancels awaiting mode when no text field is focused.
    {
      ImGuiIO& ioEnter = ImGui::GetIO();
      const bool enterDown =
          ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
      if (enterDown && !ioEnter.WantTextInput) {
        using SAP = AppCommandState::SegmentAnglePickPhase;
        using LP = AppCommandState::LinePhase;
        using PP = AppCommandState::PolylinePhase;
        const bool lineNext =
            cmd.active == AppCommandState::Kind::Line && cmd.linePhase == LP::NeedNextPoint;
        const bool polyNext =
            cmd.active == AppCommandState::Kind::Polyline && cmd.polylinePhase == PP::NeedNextPoint;
        const bool apCommit =
            (lineNext || polyNext) && cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit;
        const bool kbAwaitBearing =
            (lineNext || polyNext) && cmd.segmentAngleKeyboardAwaitBearing;
        if (apCommit || kbAwaitBearing)
          ProcessCommandLineSubmit(cmdBuf, static_cast<int>(sizeof(cmdBuf)), cmd, cmdLog);
      }
    }

    DrawCreatePointsPanel(cmd, cmdLog);
    DrawSettingsPanel(cmd, &cmdLog);
    ImGuiLayout_DrawLayoutPopups(cmd, cmdLog);
    DrawLayerManagerWindow(cmd, &cmdLog);
    DrawViewPointsPanel(cmd, cmdLog);
    DrawImportPointsPanel(cmd, cmdLog);
    DrawExportPointsPanel(cmd, cmdLog);
    DrawSurveyReportsPanel(cmd);
    DrawCopySurveyDuplicateModal(cmd, cmdLog);

    ProcessPendingViewportZoom(cmd, &panX, &panY, &zoom, fbW, fbH, cmdLog);

    std::vector<float> rubberLines;
    AppendCadDraftRubberLines(cmd, curX, curY, orthoEnabled, rubberLines);

    float selRectBuf[4]{};
    const float* selRectPtr = nullptr;
    if (cmd.selBoxWaitingSecond) {
      selRectBuf[0] = std::min(cmd.selBoxAnchorX, curRawX);
      selRectBuf[1] = std::max(cmd.selBoxAnchorX, curRawX);
      selRectBuf[2] = std::min(cmd.selBoxAnchorY, curRawY);
      selRectBuf[3] = std::max(cmd.selBoxAnchorY, curRawY);
      selRectPtr = selRectBuf;
    }

    std::vector<float> previewLines;
    std::vector<float> previewCircles;
    const float previewCx = cmd.active == AppCommandState::Kind::Offset ? curRawX : curX;
    const float previewCy = cmd.active == AppCommandState::Kind::Offset ? curRawY : curY;
    BuildTransformPreview(cmd, previewCx, previewCy, &previewLines, &previewCircles);

    if (cmd.active == AppCommandState::Kind::Trim &&
        cmd.trimPhase == AppCommandState::TrimPhase::CuttingLine_WaitP2) {
      float lx = curX;
      float ly = curY;
      ApplyOrthoConstrainFromAnchor(cmd.trimCutInfP1x, cmd.trimCutInfP1y, &lx, &ly, orthoEnabled);
      PushRubberSeg(rubberLines, cmd.trimCutInfP1x, cmd.trimCutInfP1y, lx, ly);
      const float midx = (cmd.trimCutInfP1x + lx) * 0.5f;
      const float midy = (cmd.trimCutInfP1y + ly) * 0.5f;
      CadTrimAppendCutLineRemovedPreview(cmd, cmd.trimCutInfP1x, cmd.trimCutInfP1y, lx, ly, midx, midy, &previewLines);
    }

    std::vector<float> highlightLines;
    std::vector<float> highlightCircles;
    BuildSelectionHighlight(cmd, &highlightLines, &highlightCircles);

    std::vector<float> surveyMarkers;
    if (!cmd.surveyPoints.empty()) {
      const float surveyCrossHalf =
          SurveyPointCrossHalfWorldFromPaper(cmd.surveyPointCrossSpanPlottedInches, cmd.modelUnitsPerPlottedInch);
      AppendAllSurveyPointMarkers(surveyCrossHalf, cmd.surveyPoints, &surveyMarkers);
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
    ext.drawingLayers = &cmd.drawingLayerTable;

    viewport.SetSize(fbW, fbH);
    viewport.RenderScene(panX, panY, zoom, fbW, fbH, cmd.userLinesFlat, cmd.userCirclesCxCyR, cmd.cadGpuRevision,
                         rubberLines, snapHit.valid ? &snapHit : nullptr,
                         std::clamp(cmd.objectSnapGlyphHalfPx, 3.f, 48.f), selRectPtr,
                         previewLines.empty() ? nullptr : &previewLines,
                         previewCircles.empty() ? nullptr : &previewCircles,
                         highlightLines.empty() ? nullptr : &highlightLines,
                         highlightCircles.empty() ? nullptr : &highlightCircles,
                         surveyMarkers.empty() ? nullptr : &surveyMarkers, &cmd.userLineAttrs,
                         &cmd.userCircleAttrs, &ext, gridVisible, &cmd.drawingLayerTable);

    ImGuiLayout_CommitDeferredIniLoadIfNeeded();
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

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  CadUiClearMenuBarLogo();
  DestroyAppLogoGpu(&appLogo);
  ImGui::DestroyContext();

  viewport.Shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
