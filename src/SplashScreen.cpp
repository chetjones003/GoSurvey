#include "SplashScreen.hpp"

#include "AppIcon.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>

#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

void GlfwApplySplashStageWindowHints() {
  glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  // Per-pixel alpha so area outside the splash card can show the desktop (where the OS supports it).
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
}

void GlfwApplyMainStageWindowChrome(GLFWwindow* window) {
  if (!window)
    return;
  if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED))
    glfwRestoreWindow(window);
  glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
  glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_TRUE);
  glfwSetWindowTitle(window, "GoSurvey — CAD");
  glfwMaximizeWindow(window);
  glfwFocusWindow(window);
}

void RunStartupSplash(GLFWwindow* window, double durationSec) {
  if (!window || durationSec <= 0.0)
    return;

  namespace fs = std::filesystem;
  fs::path logoPath;
  for (const char* name : {"main_logo.png", "logo.png", "white_logo2.png"}) {
    logoPath = ResolveBundledAssetPath(fs::path("icons") / name);
    if (!logoPath.empty())
      break;
  }
  AppLogoGpu splashTex{};
  const bool haveLogo = !logoPath.empty() && LoadAppTextureFromPngFile(logoPath, &splashTex, true);

  const bool outerTransparent = glfwGetWindowAttrib(window, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE;

  const double t0 = glfwGetTime();
  while (!glfwWindowShouldClose(window)) {
    const double now = glfwGetTime();
    const double elapsed = now - t0;
    if (elapsed >= durationSec)
      break;

    glfwPollEvents();

    const float raw = static_cast<float>(std::min(1.0, elapsed / durationSec));
    const float smooth = raw * raw * (3.f - 2.f * raw);
    float bar = std::pow(smooth, 0.88f);
    if (elapsed >= durationSec - 0.04)
      bar = 1.f;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImGuiStyle& stSnap = ImGui::GetStyle();
    const ImVec4 themeWinBg = stSnap.Colors[ImGuiCol_WindowBg];
    const ImVec4 themeMenuBg = stSnap.Colors[ImGuiCol_MenuBarBg];
    const ImVec4 themeDockBg = stSnap.Colors[ImGuiCol_DockingEmptyBg];
    const ImVec4 themeBorder = stSnap.Colors[ImGuiCol_Border];
    const ImVec4 themeAccent = stSnap.Colors[ImGuiCol_CheckMark];

    // Root viewport fill must be transparent so the framebuffer alpha stays 0 outside the card.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0.f, 0.f, 0.f, 0.f));

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const ImVec2 vp0 = vp->Pos;
    const ImVec2 vp1 = vp->Pos + vp->Size;
    const ImVec2 workPos = vp->WorkPos;
    const ImVec2 work = vp->WorkSize;

    ImVec2 panel(work.x * (1.f / 3.f), work.y * (1.f / 3.f));
    panel.x = fmaxf(panel.x, 360.f);
    panel.y = fmaxf(panel.y, 280.f);
    panel.x = fminf(panel.x, work.x * 0.92f);
    panel.y = fminf(panel.y, work.y * 0.92f);
    const ImVec2 card0(workPos.x + (work.x - panel.x) * 0.5f, workPos.y + (work.y - panel.y) * 0.5f);
    const ImVec2 card1(card0.x + panel.x, card0.y + panel.y);

    if (!outerTransparent) {
      ImVec4 dim = themeDockBg;
      dim.x *= 0.92f;
      dim.y *= 0.92f;
      dim.z *= 0.96f;
      bg->AddRectFilled(vp0, vp1, ImGui::ColorConvertFloat4ToU32(dim));
    }

    const ImVec4 topL = themeWinBg;
    const ImVec4 topR = ImVec4(themeMenuBg.x * 0.65f + themeWinBg.x * 0.35f, themeMenuBg.y * 0.65f + themeWinBg.y * 0.35f,
                               themeMenuBg.z * 0.65f + themeWinBg.z * 0.35f, 1.f);
    const ImVec4 botR = ImVec4(themeDockBg.x * 0.55f + themeMenuBg.x * 0.45f, themeDockBg.y * 0.55f + themeMenuBg.y * 0.45f,
                               themeDockBg.z * 0.55f + themeMenuBg.z * 0.45f, 1.f);
    const ImVec4 botL = ImVec4(themeDockBg.x * 0.75f + themeWinBg.x * 0.25f, themeDockBg.y * 0.75f + themeWinBg.y * 0.25f,
                               themeDockBg.z * 0.75f + themeWinBg.z * 0.25f, 1.f);
    bg->AddRectFilledMultiColor(card0, card1, ImGui::ColorConvertFloat4ToU32(topL), ImGui::ColorConvertFloat4ToU32(topR),
                                ImGui::ColorConvertFloat4ToU32(botR), ImGui::ColorConvertFloat4ToU32(botL));

    const ImU32 rim = ImGui::ColorConvertFloat4ToU32(themeBorder);
    bg->AddRect(card0, card1, rim, 8.f, ImDrawFlags_RoundCornersAll, 2.f);

    ImGui::SetNextWindowPos(card0);
    ImGui::SetNextWindowSize(panel);
    ImGui::SetNextWindowViewport(vp->ID);
    const ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                ImGuiWindowFlags_NoBackground;
    ImGui::Begin("##Splash", nullptr, wf);

    const ImVec2 ws = ImGui::GetWindowSize();
    const float intro = static_cast<float>(std::min(1.0, elapsed / 0.35));
    ImGui::Dummy(ImVec2(1, ws.y * 0.06f));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f * intro, 0.93f * intro, 0.96f * intro, intro));
    ImGui::SetWindowFontScale(1.55f);
    const char* title = "GoSurvey";
    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((ws.x - tw) * 0.5f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.62f, 0.72f, 0.85f * intro));
    const char* subtitle = "Survey-grade CAD";
    tw = ImGui::CalcTextSize(subtitle).x;
    ImGui::SetCursorPosX((ws.x - tw) * 0.5f);
    ImGui::TextUnformatted(subtitle);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(1, ws.y * 0.03f));

    if (haveLogo && splashTex.texture) {
      const float maxSide = std::min(ws.x, ws.y) * 0.42f;
      float lw = static_cast<float>(splashTex.width);
      float lh = static_cast<float>(splashTex.height);
      const float scale = maxSide / std::max(lw, lh);
      lw *= scale;
      lh *= scale;
      // Fade logo in quickly; solid tint (no pulsing).
      const float logoFade = std::min(1.f, static_cast<float>(elapsed / 0.12));
      ImGui::SetCursorPosX((ws.x - lw) * 0.5f);
      const ImTextureRef logoRef((ImTextureID)(intptr_t)(uintptr_t)splashTex.texture);
      ImGui::ImageWithBg(logoRef, ImVec2(lw, lh), ImVec2(0.f, 1.f), ImVec2(1.f, 0.f), ImVec4(0.f, 0.f, 0.f, 0.f),
                         ImVec4(logoFade, logoFade, logoFade, 1.f));

      const ImVec2 imgMin = ImGui::GetItemRectMin();
      const ImVec2 imgMax = ImGui::GetItemRectMax();
      const ImVec4 accent = themeAccent;
      ImVec4 frameCol(accent.x, accent.y, accent.z, 0.28f);
      ImGui::GetForegroundDrawList()->AddRect(imgMin - ImVec2(5, 5), imgMax + ImVec2(5, 5),
                                             ImGui::ColorConvertFloat4ToU32(frameCol), 8.f, 0, 1.75f);
    }

    ImGui::Dummy(ImVec2(1, ws.y * 0.04f));

    const char* phases[] = {"Bootstrapping interface…", "Preparing drawing engine…", "Almost ready…"};
    const int phaseIdx = std::min(2, static_cast<int>(raw * 3.f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.55f, 0.64f, 0.92f * intro));
    tw = ImGui::CalcTextSize(phases[phaseIdx]).x;
    ImGui::SetCursorPosX((ws.x - tw) * 0.5f);
    ImGui::TextUnformatted(phases[phaseIdx]);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(1, 18.f));
    const float barW = std::min(400.f, ws.x * 0.82f);
    ImGui::SetCursorPosX((ws.x - barW) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
    ImGui::ProgressBar(bar, ImVec2(barW, 11.f), "");
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::Render();
    int dw = 0;
    int dh = 0;
    glfwGetFramebufferSize(window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    if (outerTransparent)
      glClearColor(0.f, 0.f, 0.f, 0.f);
    else {
      const ImVec4& c = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
      glClearColor(c.x, c.y, c.z, 1.f);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  DestroyAppLogoGpu(&splashTex);
}
