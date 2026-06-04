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

  PdfAttach_Init();

  // One ViewportRenderer (owns its own FBO + texture) per open drawing tab.
  std::vector<std::unique_ptr<ViewportRenderer>> viewportRenderers;
  viewportRenderers.push_back(std::make_unique<ViewportRenderer>());
  if (!viewportRenderers[0]->Init()) {
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
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigInputTextEnterKeepActive = false; // CAD shell: Enter submits without selecting-all next keystroke

  ApplyCadLightTheme();
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
  cmdLog.push_back( "Regenerating model.");
  cmdLog.push_back("Drawing Created.");
  cmdLog.push_back("JSON database - ready...");
  TryLoadStartupWorkspaceTemplate(cmd, cmdLog);
  cmd.activeDocSavedRevision = cmd.cadGpuRevision;  // loaded template counts as "clean"
  // Re-apply user preferences so they override any template defaults (crosshair, snap, survey, etc.).
  LoadUserStartupPrefSettings(cmd);
  // Re-apply theme now that displayColorThemeIdx is known from saved prefs.
  if (cmd.displayColorThemeIdx == 0)
    ApplyCadDarkTheme();
  else
    ApplyCadLightTheme();
  if (!cmd.surveyPoints.empty()) {
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
  const float ribbonH = 130.f;
  bool orthoEnabled = true;
  bool gridVisible = false;
  // prevDrawingIdx lives in cmd — no local needed.

  while (true) {
    glfwPollEvents();

    // Intercept window-close so we can prompt about unsaved drawings.
    if (glfwWindowShouldClose(window)) {
      glfwSetWindowShouldClose(window, GLFW_FALSE);
      bool anyDirty = (cmd.cadGpuRevision != cmd.activeDocSavedRevision);
      for (int i = 0; i < static_cast<int>(cmd.documents.size()) && !anyDirty; ++i) {
        if (i != cmd.activeDrawingIdx &&
            cmd.documents[i].cadGpuRevision != cmd.documents[i].savedRevision)
          anyDirty = true;
      }
      if (anyDirty)
        cmd.confirmCloseModal = true;
      else
        cmd.closeConfirmed = true;
    }

    if (cmd.closeConfirmed)
      break;

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
        const bool hadCreatePtsUi = cmd.showCreatePointsWindow;
        cmd.showCreatePointsWindow = false;
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

    // Keep documents vector in sync with the tab list.
    while (cmd.documents.size() < cmd.drawingTabs.size())
      cmd.documents.emplace_back();

    // Erase renderer for a closed tab (must happen before provisioning so indices stay aligned).
    if (cmd.pendingTabErase >= 0) {
      const auto eraseAt = static_cast<size_t>(cmd.pendingTabErase);
      if (eraseAt < viewportRenderers.size()) {
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
    while (viewportRenderers.size() <= static_cast<size_t>(cmd.activeDrawingIdx)) {
      viewportRenderers.push_back(std::make_unique<ViewportRenderer>());
      viewportRenderers.back()->Init();
    }

    // Detect tab switch: save old document, restore new one.
    // cmd.prevDrawingIdx is the authoritative last-active index (also written by menu New/Open handlers).
    if (cmd.activeDrawingIdx != cmd.prevDrawingIdx) {
      SaveDocumentToSnapshot(cmd, cmd.prevDrawingIdx);
      RestoreDocumentFromSnapshot(cmd, cmd.activeDrawingIdx);
      cmd.prevDrawingIdx = cmd.activeDrawingIdx;
    }

    const size_t activeRendIdx = static_cast<size_t>(cmd.activeDrawingIdx);
    ViewportRenderer& activeRenderer = *viewportRenderers[activeRendIdx];

    CadSnap::Hit snapHit{};
    DrawDrawingViewport(activeRenderer.ColorTexture(), cmd, cmdLog, cmdBuf, static_cast<int>(sizeof(cmdBuf)),
                        &cmd.viewportPanX, &cmd.viewportPanY, &cmd.viewportZoom, &curX, &curY, &curRawX, &curRawY,
                        &fbW, &fbH, &snapHit);
    cmd.uiCursorWorldX = CadCoord::WorldXFromLocal(cmd, static_cast<float>(curX));
    cmd.uiCursorWorldY = CadCoord::WorldYFromLocal(cmd, static_cast<float>(curY));
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
    DrawCadStatusBarStrip(cmd, CadCoord::WorldXFromLocal(cmd, static_cast<float>(curX)),
                          CadCoord::WorldYFromLocal(cmd, static_cast<float>(curY)), 0.f, &orthoEnabled,
                          &gridVisible);

    // LINE/POLYLINE AP: after two picks the bottom command InputText is hidden — Enter must still lock bearing.
    // Keyboard-only "A" then bearing: Enter with empty buffer cancels awaiting mode when no text field is focused.
    {
      ImGuiIO& ioEnter = ImGui::GetIO();
      const bool enterDown =
          ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
      if (enterDown && !ioEnter.WantTextInput && cmd.active != AppCommandState::Kind::None)
        ProcessCommandLineSubmit(cmdBuf, static_cast<int>(sizeof(cmdBuf)), cmd, cmdLog);
    }

    DrawQuickSelectWindow(cmd, cmdLog);
    DrawSelectionCyclingPanel(cmd);
    DrawCreatePointsPanel(cmd, cmdLog);
    DrawSettingsPanel(cmd, &cmdLog);
    ImGuiLayout_DrawLayoutPopups(cmd, cmdLog);
    DrawLayerManagerWindow(cmd, &cmdLog);
    DrawViewPointsPanel(cmd, cmdLog);
    DrawImportPointsPanel(cmd, cmdLog);
    DrawExportPointsPanel(cmd, cmdLog);
    DrawSurveyReportsPanel(cmd);
    // After all panels have called Begin() their DockNode is set, so SetWindowFocus
    // can correctly update the dock node's SelectedTabId for the next frame.
    if (cmd.pendingPropertiesFocus) {
      ImGui::SetWindowFocus("Properties");
      cmd.pendingPropertiesFocus = false;
    }
    DrawCopySurveyDuplicateModal(cmd, cmdLog);
    DrawPdfAttachDialog(cmd, cmdLog);
    DrawAlignResultsWindow(cmd, cmdLog);
    DrawCloseConfirmModal(cmd, cmdLog);

    std::vector<float> rubberLines;
    const float orthoHalfH = (1.f / std::max(cmd.viewportZoom, 1.e-9f)) * 50.f;
    AppendCadDraftRubberLines(cmd, curX, curY, orthoEnabled, cmd.viewportPanX, cmd.viewportPanY, orthoHalfH, fbH,
                              rubberLines);

    float selRectBuf[4]{};
    const float* selRectPtr = nullptr;
    if (cmd.selBoxWaitingSecond) {
      selRectBuf[0] = std::min(cmd.selBoxAnchorX, static_cast<float>(curRawX));
      selRectBuf[1] = std::max(cmd.selBoxAnchorX, static_cast<float>(curRawX));
      selRectBuf[2] = std::min(cmd.selBoxAnchorY, static_cast<float>(curRawY));
      selRectBuf[3] = std::max(cmd.selBoxAnchorY, static_cast<float>(curRawY));
      selRectPtr = selRectBuf;
    }

    std::vector<float> previewLines;
    std::vector<float> previewCircles;
    const float previewCx =
        cmd.active == AppCommandState::Kind::Offset ? static_cast<float>(curRawX) : static_cast<float>(curX);
    const float previewCy =
        cmd.active == AppCommandState::Kind::Offset ? static_cast<float>(curRawY) : static_cast<float>(curY);
    BuildTransformPreview(cmd, previewCx, previewCy, &previewLines, &previewCircles);

    if (cmd.active == AppCommandState::Kind::Trim &&
        cmd.trimPhase == AppCommandState::TrimPhase::CuttingLine_WaitP2) {
      float lx = static_cast<float>(curX);
      float ly = static_cast<float>(curY);
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
    BuildHoverHighlight(cmd, &hoverLines, &hoverCircles);

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

    activeRenderer.SetSize(fbW, fbH);
    RenderTuning tuning{};
    tuning.arcCircleSmoothnessCap = std::clamp(cmd.displayArcCircleSmoothness, 8, 20000);
    tuning.hardwareAcceleration = cmd.systemHardwareAcceleration;
    tuning.smoothLineDisplay = cmd.gfxSmoothLineDisplay;
    // Build PDF render list: committed attachments + cursor-follow preview when picking insert point.
    std::vector<PdfAttachment> pdfRenderList;
    if (!cmd.pdfAttachments.empty())
      pdfRenderList = cmd.pdfAttachments; // shallow copy of the vector (texIds stay valid)

    activeRenderer.RenderScene(cmd.viewportPanX, cmd.viewportPanY, cmd.viewportZoom, fbW, fbH, cmd.userLinesFlat,
                         cmd.userCirclesCxCyR, cmd.cadGpuRevision,
                         rubberLines, snapHit.valid ? &snapHit : nullptr,
                         std::clamp(cmd.objectSnapGlyphHalfPx, 3.f, 48.f), selRectPtr,
                         previewLines.empty() ? nullptr : &previewLines,
                         previewCircles.empty() ? nullptr : &previewCircles,
                         highlightLines.empty() ? nullptr : &highlightLines,
                         highlightCircles.empty() ? nullptr : &highlightCircles,
                         hoverLines.empty() ? nullptr : &hoverLines,
                         hoverCircles.empty() ? nullptr : &hoverCircles,
                         surveyMarkers.empty() ? nullptr : &surveyMarkers, &cmd.userLineAttrs,
                         &cmd.userCircleAttrs, &ext, gridVisible, &cmd.drawingLayerTable, tuning,
                         pdfRenderList.empty() ? nullptr : &pdfRenderList);

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

  // Silently persist settings, preferences, and the current dock layout.
  SaveUserStartupPrefs(cmd);
  {
    const ImGuiIO& ioSave = ImGui::GetIO();
    if (ioSave.IniFilename && ioSave.IniFilename[0])
      ImGui::SaveIniSettingsToDisk(ioSave.IniFilename);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  CadUiClearMenuBarLogo();
  DestroyAppLogoGpu(&appLogo);
  ImGui::DestroyContext();

  // Release PDF textures before GL context is destroyed.
  for (auto& att : cmd.pdfAttachments)
    PdfAttach_ReleaseTexture(att);
  if (cmd.pdfDraftCache) {
    PdfDraftCache_Free(cmd.pdfDraftCache);
    cmd.pdfDraftCache = nullptr;
  }

  for (auto& r : viewportRenderers)
    r->Shutdown();
  PdfAttach_Shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
