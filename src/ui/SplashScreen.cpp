#include "SplashScreen.hpp"

#include "AppIcon.hpp"
#include "CadUi.hpp"
#include "Version.hpp"
#include "WinFrameControls.hpp"

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
  // REQ-093 (amended): no GLFW_TRANSPARENT_FRAMEBUFFER — per-pixel window transparency isn't
  // reliably honored by every compositor/driver, and where it silently isn't, clearing alpha 0
  // paints solid black instead of the desktop showing through. The window is sized to the card
  // itself (main.cpp), so nothing outside it ever needs to be transparent.
  // Stay hidden until centered (main.cpp positions it right after creation, before showing) — an
  // AutoCAD-style splash is a small window, not the full screen behind a dimmed overlay, so there
  // must be no frame where it is visible at its default, unpositioned spot.
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
}

void GlfwApplyMainStageWindowChrome(GLFWwindow* window) {
  if (!window)
    return;
  // REQ-077: the running build states its version. GOSURVEY_VERSION_FULL is generated from
  // the one CMake project version (ADR-029 (a)), so this cannot drift from the installer.
  static const std::string kWindowTitle =
      std::string("GoSurvey ") + GOSURVEY_VERSION_FULL + " — CAD";
#if defined(_WIN32)
  glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
  glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_TRUE);
  glfwSetWindowTitle(window, kWindowTitle.c_str());
  GlfwPlatformInstallBorderlessResize(window);
  glfwMaximizeWindow(window);
  glfwFocusWindow(window);
#else
  if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED))
    glfwRestoreWindow(window);
  glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
  glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_TRUE);
  glfwSetWindowTitle(window, "GoSurvey — CAD");
  glfwMaximizeWindow(window);
  glfwFocusWindow(window);
#endif
}

namespace {

#if defined(_WIN32)

static void TitleBarDrawMinIcon(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 col) {
  const ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
  const float half = (mx.x - mn.x) * 0.22f;
  dl->AddLine(ImVec2(c.x - half, c.y), ImVec2(c.x + half, c.y), col, 1.25f);
}

static void TitleBarDrawMaxIcon(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 col) {
  const float bw = mx.x - mn.x;
  const float bh = mx.y - mn.y;
  const float dim = std::min(bw, bh);
  const float side = dim * 0.34f;
  const float cx = (mn.x + mx.x) * 0.5f;
  const float cy = (mn.y + mx.y) * 0.5f;
  const float h = side * 0.5f;
  dl->AddRect(ImVec2(cx - h, cy - h), ImVec2(cx + h, cy + h), col, 1.f, 0, 1.f);
}

static void TitleBarDrawRestoreIcon(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 col) {
  const float bw = mx.x - mn.x;
  const float bh = mx.y - mn.y;
  const float dim = std::min(bw, bh);
  const float s = dim * 0.15f;
  const float cx = (mn.x + mx.x) * 0.5f;
  const float cy = (mn.y + mx.y) * 0.5f;
  const float ox = dim * 0.085f;
  const float oy = -dim * 0.085f;
  dl->AddRect(ImVec2(cx - s + ox, cy - s + oy), ImVec2(cx + s + ox, cy + s + oy), col, 1.f, 0, 1.f);
  dl->AddRect(ImVec2(cx - s - ox, cy - s - oy), ImVec2(cx + s - ox, cy + s - oy), col, 1.f, 0, 1.f);
}

static void TitleBarDrawCloseIcon(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 col) {
  const float cx = (mn.x + mx.x) * 0.5f;
  const float cy = (mn.y + mx.y) * 0.5f;
  const float half = (mx.x - mn.x) * 0.22f;
  dl->AddLine(ImVec2(cx - half, cy - half), ImVec2(cx + half, cy + half), col, 1.25f);
  dl->AddLine(ImVec2(cx - half, cy + half), ImVec2(cx + half, cy - half), col, 1.25f);
}

#endif

} // namespace

void DrawMainWindowTitleBar(GLFWwindow* window) {
#if !defined(_WIN32)
  (void)window;
  return;
#else
  if (!window)
    return;

  const ImGuiStyle& st = ImGui::GetStyle();
  float rowH = ImGui::GetFrameHeight() + 8.f;
  // Title bar is always dark regardless of the active application theme.
  const ImVec4 barBg     = ImVec4(0.090f, 0.102f, 0.122f, 1.f);  // #171A1F dark secondary
  const ImU32  iconCol   = IM_COL32(199, 207, 219, 255);           // resting icon color
  const ImU32  iconColHov = IM_COL32(255, 255, 255, 255);          // brighter on hover / press

  const float btnW      = 44.f;
  const float btnStripW = btnW * 3.f + st.ItemInnerSpacing.x * 2.f;
  const float padY      = 4.f;
  const float leftPad   = 10.f;

  ImTextureID logoTex = (ImTextureID)0;
  ImVec2 logoDims(0.f, 0.f);
  const bool haveLogo = CadUiTitleBarLogoQuery(&logoTex, &logoDims);
  float logoW = 0.f;
  float logoH = 0.f;
  if (haveLogo) {
    logoH = std::max(18.f, std::min(rowH - padY * 2.f, rowH - 8.f));
    logoW = logoH * (logoDims.x / logoDims.y);
    rowH  = std::max(rowH, logoH + padY * 2.f);
  }

  // Supply title bar geometry to the WndProc so WM_NCHITTEST returns HTCAPTION for the drag
  // region and HTCLIENT for the button strip, making hit detection OS-level reliable.
  GlfwPlatformSetTitleBarMetrics(rowH, btnStripW);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, barBg);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
  ImGui::BeginChild("##GoSurveyTitleBar", ImVec2(0.f, rowH), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

  const ImVec2 wp          = ImGui::GetWindowPos();
  const float  rightEdge   = wp.x + ImGui::GetWindowSize().x;
  const float  stripLeftScr = rightEdge - btnStripW;

  // Logo + title text.  The drag area to the right of the text is handled natively by HTCAPTION
  // in BorderlessWndProc — no InvisibleButton needed here.
  ImGui::SetCursorPos(ImVec2(leftPad, padY));
  if (haveLogo && logoTex) {
    ImGui::SetCursorPosY(padY + std::max(0.f, (rowH - padY * 2.f - logoH) * 0.5f));
    ImGui::Image(logoTex, ImVec2(logoW, logoH), ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));
    ImGui::SameLine(0.f, st.ItemInnerSpacing.x);
  }
  ImGui::SetCursorPosY(padY + std::max(0.f, (rowH - padY * 2.f - ImGui::GetTextLineHeight()) * 0.5f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.82f, 0.88f, 1.f));
  ImGui::TextUnformatted("GoSurvey");
  ImGui::PopStyleColor();

  // Window control buttons — full rowH so there are no dead zones at the top or bottom edge.
  ImGui::SetCursorScreenPos(ImVec2(stripLeftScr, wp.y));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 btnSize(btnW, rowH);

  // Minimize
  if (ImGui::InvisibleButton("##TitleMin", btnSize))
    glfwIconifyWindow(window);
  {
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    if (act)
      dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 80));
    else if (hov)
      dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 50));
    TitleBarDrawMinIcon(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), (hov || act) ? iconColHov : iconCol);
  }
  ImGui::SameLine(0.f, st.ItemInnerSpacing.x);

  // Maximize / restore
  const bool maxed = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
  if (ImGui::InvisibleButton("##TitleMax", btnSize)) {
    if (maxed) glfwRestoreWindow(window);
    else       glfwMaximizeWindow(window);
  }
  {
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    if (act)
      dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 80));
    else if (hov)
      dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 255, 255, 50));
    if (maxed)
      TitleBarDrawRestoreIcon(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), (hov || act) ? iconColHov : iconCol);
    else
      TitleBarDrawMaxIcon(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), (hov || act) ? iconColHov : iconCol);
  }
  ImGui::SameLine(0.f, st.ItemInnerSpacing.x);

  // Close
  if (ImGui::InvisibleButton("##TitleClose", btnSize))
    glfwSetWindowShouldClose(window, GLFW_TRUE);
  {
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    if (act)
      dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(150, 30, 18, 220));
    else if (hov)
      dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(196, 43, 28, 190));
    TitleBarDrawCloseIcon(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), (hov || act) ? iconColHov : iconCol);
  }

  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
#endif
}

void RunStartupSplash(GLFWwindow* window, double durationSec) {
  if (!window || durationSec <= 0.0)
    return;

  namespace fs = std::filesystem;
  const fs::path logoPath = ResolveAppLogoPngPath();
  AppLogoGpu splashTex{};
  const bool haveLogo = !logoPath.empty() && LoadAppTextureFromPngFile(logoPath, &splashTex, true);

  // REQ-093 (amended): per-pixel window transparency is not reliable across compositors/drivers —
  // where it isn't actually honored, clearing alpha 0 paints solid black instead of showing the
  // desktop, which is worse than not attempting it. The splash window (main.cpp) is now sized to
  // the card itself, so there is no surrounding area that needs to be transparent or dimmed: the
  // card simply fills the window edge to edge, opaque, and the real desktop is whatever is outside
  // the (small) window bounds — genuinely, not via a transparency trick.
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

    // No-op today (the "##Splash" window below sets NoBackground and nothing else docks during the
    // splash), kept for symmetry with its PopStyleColor(2) below.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0.f, 0.f, 0.f, 0.f));

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const ImVec2 workPos = vp->WorkPos;
    const ImVec2 work = vp->WorkSize;

    // The window IS the card (main.cpp creates/sizes it for exactly this) — no margin to dim, no
    // backdrop rect, nothing surrounding it to fake-transparent. A 1px inset keeps the 2px-wide
    // border stroke below fully inside the window instead of getting clipped at its exact edge.
    const ImVec2 card0(workPos.x + 1.f, workPos.y + 1.f);
    const ImVec2 card1(workPos.x + work.x - 1.f, workPos.y + work.y - 1.f);

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
    ImGui::SetNextWindowSize(card1 - card0);
    ImGui::SetNextWindowViewport(vp->ID);
    const ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                ImGuiWindowFlags_NoBackground;
    ImGui::Begin("##Splash", nullptr, wf);

    const ImVec2 ws = ImGui::GetWindowSize();
    const float intro = static_cast<float>(std::min(1.0, elapsed / 0.35));

    // Small corner mark, drawn at its NATIVE pixel size — the bundled app.png is only 32x32, and
    // stretching a source that small to fill a fraction of the card (the previous design) is what
    // made it look pixelated. Un-scaled in a corner reads as a mark, not a hero image, and stays
    // crisp at any card size. "GoSurvey" is the dominant visual now instead (below).
    if (haveLogo && splashTex.texture) {
      const ImVec2 savedCursor = ImGui::GetCursorPos();
      const float pad = 14.f;
      const float logoFade = std::min(1.f, static_cast<float>(elapsed / 0.12));
      ImGui::SetCursorPos(ImVec2(pad, pad));
      const ImTextureRef logoRef((ImTextureID)(intptr_t)(uintptr_t)splashTex.texture);
      ImGui::ImageWithBg(logoRef, ImVec2(static_cast<float>(splashTex.width), static_cast<float>(splashTex.height)),
                         ImVec2(0.f, 1.f), ImVec2(1.f, 0.f), ImVec4(0.f, 0.f, 0.f, 0.f),
                         ImVec4(logoFade, logoFade, logoFade, 1.f));
      ImGui::SetCursorPos(savedCursor);
    }

    ImGui::Dummy(ImVec2(1, ws.y * 0.30f));

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f * intro, 0.93f * intro, 0.96f * intro, intro));
    ImGui::SetWindowFontScale(2.6f);
    const char* title = "GoSurvey";
    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((ws.x - tw) * 0.5f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.62f, 0.72f, 0.85f * intro));
    const char* subtitle = "Precision Survey CAD";
    tw = ImGui::CalcTextSize(subtitle).x;
    ImGui::SetCursorPosX((ws.x - tw) * 0.5f);
    ImGui::TextUnformatted(subtitle);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(1, ws.y * 0.10f));

    // REQ-093: cosmetic only — text cycles purely on elapsed-time fraction, not on any real load
    // step finishing. Linetypes have no data table to load and text styles are already resident in
    // memory the instant AppCommandState is constructed, so neither has real work to gate on; the
    // labels below name them anyway for the loading feel the splash is meant to give (D-2026-08-23-g).
    const char* phases[] = {"Loading user settings…",  "Loading linetypes…", "Loading text styles…",
                             "Preparing workspace…",    "Loading blocks…",    "Almost ready…"};
    constexpr int kPhaseCount = static_cast<int>(sizeof(phases) / sizeof(phases[0]));
    const int phaseIdx = std::min(kPhaseCount - 1, static_cast<int>(raw * static_cast<float>(kPhaseCount)));
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
    // Opaque: the window is exactly the card, so there is no surrounding area whose transparency
    // would matter, and relying on true per-pixel transparency instead — where the compositor
    // doesn't actually honor it — paints solid black rather than the desktop (see the comment
    // above this function).
    {
      const ImVec4& c = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
      glClearColor(c.x, c.y, c.z, 1.f);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  DestroyAppLogoGpu(&splashTex);
}
