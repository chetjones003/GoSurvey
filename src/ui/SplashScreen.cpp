#include "SplashScreen.hpp"

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
#include <string>

namespace {

// Same steel-blue accent the Start screen uses, so launch → landing reads as one product (REQ-308).
constexpr ImVec4 kAccent  {0.26f, 0.56f, 0.86f, 1.f};
constexpr ImVec4 kAccentHi{0.34f, 0.64f, 0.95f, 1.f};

ImVec4 MixV(const ImVec4& a, const ImVec4& b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.f};
}

// Crisp vector "GS" badge — matches DrawGsBadge on the Start screen. Vector, so sharp at any size.
void SplashGsBadge(ImDrawList* dl, ImVec2 c, float sz, float alpha) {
  const ImVec2 a(c.x - sz * 0.5f, c.y - sz * 0.5f);
  const ImVec2 b(c.x + sz * 0.5f, c.y + sz * 0.5f);
  const float rnd = sz * 0.24f;
  auto A = [&](ImVec4 v) { v.w *= alpha; return ImGui::ColorConvertFloat4ToU32(v); };
  dl->AddRectFilled(ImVec2(a.x + 2.f, a.y + 3.f), ImVec2(b.x + 3.f, b.y + 4.f), A({0.f, 0.f, 0.f, 0.40f}), rnd);
  dl->AddRectFilled(a, b, A({kAccent.x, kAccent.y, kAccent.z, 1.f}), rnd);
  // Smooth top-down sheen, clipped to the rounded body — no hard midline.
  dl->PushClipRect(a, b, true);
  dl->AddRectFilledMultiColor(a, b, A({1.f, 1.f, 1.f, 0.16f}), A({1.f, 1.f, 1.f, 0.16f}),
                              A({1.f, 1.f, 1.f, 0.f}), A({1.f, 1.f, 1.f, 0.f}));
  dl->PopClipRect();
  dl->AddRect(a, b, A({1.f, 1.f, 1.f, 0.30f}), rnd, 0, 1.5f);
  const float fs = sz / ImGui::GetFontSize() * 0.52f;
  ImGui::SetWindowFontScale(fs);
  const ImVec2 t = ImGui::CalcTextSize("GS");
  dl->AddText(ImVec2(c.x - t.x * 0.5f, c.y - t.y * 0.5f), A({1.f, 1.f, 1.f, 1.f}), "GS");
  ImGui::SetWindowFontScale(1.f);
}

}  // namespace

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
  const ImVec4 barBg     = ImVec4(0.10f, 0.10f, 0.10f, 1.f);  // neutral gray — matches the viewport background
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

    // Blue-tinted gradient matching the Start screen: accent glow top, darkening toward the base.
    (void)themeMenuBg;
    (void)themeDockBg;
    const ImVec4 topL = MixV(themeWinBg, kAccent, 0.20f);
    const ImVec4 topR = MixV(themeWinBg, kAccent, 0.11f);
    const ImVec4 botR = MixV(themeWinBg, ImVec4(0.f, 0.f, 0.f, 1.f), 0.30f);
    const ImVec4 botL = MixV(themeWinBg, ImVec4(0.f, 0.f, 0.f, 1.f), 0.22f);
    bg->AddRectFilledMultiColor(card0, card1, ImGui::ColorConvertFloat4ToU32(topL), ImGui::ColorConvertFloat4ToU32(topR),
                                ImGui::ColorConvertFloat4ToU32(botR), ImGui::ColorConvertFloat4ToU32(botL));

    // Bright accent bar across the top, plus a subtle theme rim.
    bg->AddRectFilled(card0, ImVec2(card1.x, card0.y + 4.f), ImGui::ColorConvertFloat4ToU32(kAccent),
                      8.f, ImDrawFlags_RoundCornersTop);
    bg->AddRect(card0, card1, ImGui::ColorConvertFloat4ToU32(themeBorder), 8.f, ImDrawFlags_RoundCornersAll, 2.f);

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

    // Crisp vector "GS" badge, centered — matches the Start-screen hero mark. Replaces the old
    // upscaled 32px app.png, which is what made the previous splash look pixelated.
    {
      const float badgeFade = std::min(1.f, static_cast<float>(elapsed / 0.14));
      const float badge = std::min(96.f, ws.y * 0.20f);
      SplashGsBadge(ImGui::GetWindowDrawList(),
                    ImVec2(ImGui::GetWindowPos().x + ws.x * 0.5f,
                           ImGui::GetWindowPos().y + ws.y * 0.24f),
                    badge, badgeFade);
    }

    // --- Title block, centered under the badge ---
    ImGui::SetCursorPosY(ws.y * 0.42f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f * intro, 0.93f * intro, 0.96f * intro, intro));
    ImGui::SetWindowFontScale(2.6f);
    const char* title = "GoSurvey";
    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((ws.x - tw) * 0.5f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.74f, 0.90f, 0.85f * intro));
    const char* subtitle = "Precision Survey CAD";
    tw = ImGui::CalcTextSize(subtitle).x;
    ImGui::SetCursorPosX((ws.x - tw) * 0.5f);
    ImGui::TextUnformatted(subtitle);
    ImGui::PopStyleColor();

    // Version pill, centered — same treatment as the Start-screen hero.
    {
      const std::string ver = std::string("v") + GOSURVEY_VERSION_FULL;
      const ImVec2 vts = ImGui::CalcTextSize(ver.c_str());
      ImGui::Dummy(ImVec2(1.f, 5.f));
      const ImVec2 pc = ImGui::GetCursorScreenPos();
      const float px0 = pc.x + (ws.x - vts.x - 16.f) * 0.5f;
      ImVec4 pf = kAccent; pf.w = 0.32f * intro;
      ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(px0, pc.y), ImVec2(px0 + vts.x + 16.f, pc.y + vts.y + 6.f),
                                                ImGui::ColorConvertFloat4ToU32(pf), 9.f);
      ImVec4 pt{0.85f, 0.90f, 0.97f, intro};
      ImGui::GetWindowDrawList()->AddText(ImVec2(px0 + 8.f, pc.y + 3.f), ImGui::ColorConvertFloat4ToU32(pt), ver.c_str());
    }

    // --- Phase text + progress bar, anchored to the bottom of the card so they never clip ---
    // REQ-093: cosmetic only — text cycles purely on elapsed-time fraction, not on any real load
    // step finishing. Linetypes have no data table to load and text styles are already resident in
    // memory the instant AppCommandState is constructed, so neither has real work to gate on; the
    // labels below name them anyway for the loading feel the splash is meant to give (D-2026-08-23-g).
    const char* phases[] = {"Loading user settings…",  "Loading linetypes…", "Loading text styles…",
                             "Preparing workspace…",    "Loading blocks…",    "Almost ready…"};
    constexpr int kPhaseCount = static_cast<int>(sizeof(phases) / sizeof(phases[0]));
    const int phaseIdx = std::min(kPhaseCount - 1, static_cast<int>(raw * static_cast<float>(kPhaseCount)));

    ImGui::SetCursorPosY(ws.y - 52.f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.63f, 0.74f, 0.92f * intro));
    tw = ImGui::CalcTextSize(phases[phaseIdx]).x;
    ImGui::SetCursorPosX((ws.x - tw) * 0.5f);
    ImGui::TextUnformatted(phases[phaseIdx]);
    ImGui::PopStyleColor();

    ImGui::SetCursorPosY(ws.y - 26.f);
    const float barW = std::min(400.f, ws.x * 0.82f);
    ImGui::SetCursorPosX((ws.x - barW) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.f, 1.f, 1.f, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccentHi);
    ImGui::ProgressBar(bar, ImVec2(barW, 8.f), "");
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
}
