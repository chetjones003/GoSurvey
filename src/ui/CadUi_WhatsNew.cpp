#include "CadUi.hpp"

#include "AppIcon.hpp"
#include "AppPaths.hpp"
#include "FontRegistry.hpp"
#include "CadCommands.hpp"
#include "MarkdownImGui.hpp"
#include "UserPrefs.hpp"
#include "Version.hpp"
#include "HttpFetch.hpp"
#include "WhatsNewContent.hpp"
#include "WhatsNewLogic.hpp"

#include <imgui.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

namespace {

constexpr const char* kReleasesUrl = "https://github.com/chetjones003/GoSurvey/releases";
// Dark slate tint (not white): matches SplashScreen backdrop so launch → What's New reads as one product.
constexpr ImVec4 kBackdropTint {0.12f, 0.16f, 0.26f, 0.88f};

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}

float EaseOutCubic(const float t) {
  const float u = 1.f - t;
  return 1.f - u * u * u;
}

// Ball-on-floor settle — value approaches 1 with diminishing bounces (CSS ease-out-bounce).
float EaseOutBounce(const float t) {
  constexpr float kN1 = 7.5625f;
  constexpr float kD1 = 2.75f;

  if (t < 1.f / kD1)
    return kN1 * t * t;
  if (t < 2.f / kD1) {
    const float u = t - 1.5f / kD1;
    return kN1 * u * u + 0.75f;
  }
  if (t < 2.5f / kD1) {
    const float u = t - 2.25f / kD1;
    return kN1 * u * u + 0.9375f;
  }
  const float u = t - 2.625f / kD1;
  return kN1 * u * u + 0.984375f;
}

float LerpFloat(const float a, const float b, const float t) { return a + (b - a) * t; }
ImVec4 Accent()   { return ImVec4(0.26f, 0.56f, 0.86f, 1.f); }
ImVec4 AccentHi() { return ImVec4(0.34f, 0.64f, 0.95f, 1.f); }
ImVec4 AccentLo() { return ImVec4(0.20f, 0.45f, 0.72f, 1.f); }
bool IsDark() { return ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x < 0.35f; }

void OpenReleasesPage() {
#ifdef _WIN32
  ::ShellExecuteA(nullptr, "open", kReleasesUrl, nullptr, nullptr, SW_SHOWNORMAL);
#endif
}

void ApplyDismissOnClose(AppCommandState& cmd) {
  const std::string running = GOSURVEY_VERSION_FULL;
  if (cmd.whatsNewDontShowChecked) {
    cmd.whatsNewDismissedVersion = running;
  } else if (cmd.whatsNewDismissedVersion == running) {
    cmd.whatsNewDismissedVersion.clear();
  }
  (void)SaveUserStartupPrefs(cmd);
}

bool AccentButton(const char* label, bool primary) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.f, 8.f));
  if (primary) {
    ImGui::PushStyleColor(ImGuiCol_Button, Accent());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentHi());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, AccentLo());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
  } else {
    const ImVec4 face = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    ImGui::PushStyleColor(ImGuiCol_Button, face);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Lerp(face, Accent(), 0.22f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Lerp(face, Accent(), 0.38f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
  }
  const bool clicked = ImGui::Button(label);
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
  return clicked;
}

// Crisp GS badge — solid rounded blue only. Do not use AddRectFilledMultiColor here: that API
// always fills a sharp quad, so a top sheen would paint whitish square corners outside the
// rounded blue body (exactly the artifact on the What's New logo).
void DrawGsBadge(ImDrawList* dl, ImVec2 c, float sz) {
  const ImVec2 a(c.x - sz * 0.5f, c.y - sz * 0.5f);
  const ImVec2 b(c.x + sz * 0.5f, c.y + sz * 0.5f);
  const float rnd = sz * 0.24f;
  dl->AddRectFilled(ImVec2(a.x + 2.f, a.y + 3.f), ImVec2(b.x + 3.f, b.y + 4.f),
                    ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.35f)), rnd,
                    ImDrawFlags_RoundCornersAll);
  dl->AddRectFilled(a, b, ImGui::GetColorU32(Accent()), rnd, ImDrawFlags_RoundCornersAll);
  const float fs = sz / ImGui::GetFontSize() * 0.52f;
  ImGui::SetWindowFontScale(fs);
  const ImVec2 t = ImGui::CalcTextSize("GS");
  dl->AddText(ImVec2(c.x - t.x * 0.5f, c.y - t.y * 0.5f), IM_COL32_WHITE, "GS");
  ImGui::SetWindowFontScale(1.f);
}

struct BackdropTex {
  unsigned int tex = 0;
  int w = 0;
  int h = 0;
  bool tried = false;
};

BackdropTex& WhatsNewBackdrop() {
  static BackdropTex slot;
  if (!slot.tried) {
    slot.tried = true;
    const std::filesystem::path path =
        ResolveBundledAssetPath(std::filesystem::path("resources") / "whats-new-bg.png");
    if (!path.empty())
      slot.tex = LoadIconTextureRgba(path, &slot.w, &slot.h);
  }
  return slot;
}

struct LaunchSpinnerLayout {
  float  barW = 0.f;
  float  barH = 0.f;
  float  blockH = 0.f;
  ImVec2 barMin {};
};

LaunchSpinnerLayout ComputeLaunchSpinnerLayout(const ImGuiViewport* vp, const char* statusLine) {
  assert(vp != nullptr);
  assert(statusLine != nullptr);

  LaunchSpinnerLayout layout {};
  // Reference height 720p — scales bar thickness, gaps, and rounding on larger monitors.
  const float layoutScale = (std::max)(1.f, vp->WorkSize.y / 720.f);
  layout.barW = (std::clamp)(vp->WorkSize.x * 0.34f, 280.f, 520.f);
  layout.barH = 10.f * layoutScale;

  const ImVec2 ts = ImGui::CalcTextSize(statusLine);
  const float  textGap = 12.f * layoutScale;
  layout.blockH        = layout.barH + textGap + ts.y;

  const ImVec2 center = vp->GetWorkCenter();
  const float  blockTop = center.y - layout.blockH * 0.5f;
  layout.barMin         = ImVec2(center.x - layout.barW * 0.5f, blockTop);
  return layout;
}

void DrawLaunchSpinnerForeground(ImGuiViewport* vp, const char* statusLine) {
  assert(vp != nullptr);
  assert(statusLine != nullptr);

  ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
  const ImVec2 p0 = vp->WorkPos;
  const ImVec2 p1(p0.x + vp->WorkSize.x, p0.y + vp->WorkSize.y);
  dl->AddRectFilled(p0, p1, IM_COL32(5, 13, 26, 240));

  const LaunchSpinnerLayout layout = ComputeLaunchSpinnerLayout(vp, statusLine);
  const float               layoutScale = (std::max)(1.f, vp->WorkSize.y / 720.f);
  const float               rounding    = 6.f * layoutScale;
  const ImVec2              barMax(layout.barMin.x + layout.barW, layout.barMin.y + layout.barH);

  dl->AddRectFilled(layout.barMin, barMax, IM_COL32(255, 255, 255, 26), rounding);

  // Match ImGui::ProgressBar(-time): indeterminate segment sweeps the track.
  const float t     = static_cast<float>(ImGui::GetTime());
  float       fill0 = std::fmod(-t, 1.0f);
  if (fill0 < 0.f)
    fill0 += 1.f;
  const float fill1  = (std::min)(1.f, fill0 + 0.35f);
  const float segMin = layout.barMin.x + fill0 * layout.barW;
  const float segMax = layout.barMin.x + fill1 * layout.barW;
  if (segMax > segMin)
    dl->AddRectFilled(ImVec2(segMin, layout.barMin.y), ImVec2(segMax, barMax.y),
                      ImGui::ColorConvertFloat4ToU32(AccentHi()), rounding);

  const ImVec2 ts = ImGui::CalcTextSize(statusLine);
  const ImVec2 center = vp->GetWorkCenter();
  dl->AddText(ImVec2(center.x - ts.x * 0.5f, barMax.y + 12.f * layoutScale),
              IM_COL32(224, 235, 247, 255), statusLine);
}

WhatsNewContent& CachedWhatsNewContent() {
  static WhatsNewContent content;
  static bool loaded = false;
  if (!loaded) {
    content = LoadWhatsNewContent();
    loaded  = true;
  }
  return content;
}

void EnsureWhatsNewAssetsLoaded() {
  (void)CachedWhatsNewContent();
  (void)WhatsNewBackdrop();
}

void DrawWhatsNewWindowGradient(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding) {
  // Full-window dark navy gradient — subtle accent at the top, near-black at the base.
  // Replaces the flat grey chrome so header/footer match splash and sign-in.
  const ImVec4 topL = Lerp(ImVec4(0.03f, 0.05f, 0.10f, 1.f), Accent(), 0.07f);
  const ImVec4 topR = Lerp(ImVec4(0.025f, 0.04f, 0.08f, 1.f), Accent(), 0.05f);
  const ImVec4 botR = ImVec4(0.008f, 0.012f, 0.028f, 1.f);
  const ImVec4 botL = ImVec4(0.01f, 0.016f, 0.032f, 1.f);
  dl->AddRectFilledMultiColor(a, b, ImGui::ColorConvertFloat4ToU32(topL),
                              ImGui::ColorConvertFloat4ToU32(topR),
                              ImGui::ColorConvertFloat4ToU32(botR),
                              ImGui::ColorConvertFloat4ToU32(botL));
  if (rounding > 0.f)
    dl->AddRect(a, b, ImGui::GetColorU32(Lerp(AccentLo(), ImVec4(0.f, 0.f, 0.f, 1.f), 0.72f)),
                rounding, 0, 1.f);
}

ImVec4 WhatsNewBodyBase() {
  return IsDark() ? ImVec4(0.02f, 0.04f, 0.08f, 1.f) : ImVec4(0.90f, 0.93f, 0.98f, 1.f);
}

void DrawFaintBackdrop(ImDrawList* dl, ImVec2 a, ImVec2 b) {
  const BackdropTex& bg = WhatsNewBackdrop();
  if (bg.tex == 0 || bg.w <= 0 || bg.h <= 0)
    return;

  const float boxW = b.x - a.x;
  const float boxH = b.y - a.y;
  if (boxW <= 1.f || boxH <= 1.f)
    return;

  // Cover the child while preserving aspect (center-crop).
  const float texAspect = static_cast<float>(bg.w) / static_cast<float>(bg.h);
  const float boxAspect = boxW / boxH;
  ImVec2 uv0(0.f, 0.f);
  ImVec2 uv1(1.f, 1.f);
  if (texAspect > boxAspect) {
    const float visible = boxAspect / texAspect;
    uv0.x = 0.5f - visible * 0.5f;
    uv1.x = 0.5f + visible * 0.5f;
  } else {
    const float visible = texAspect / boxAspect;
    uv0.y = 0.5f - visible * 0.5f;
    uv1.y = 0.5f + visible * 0.5f;
  }

  const ImU32 tint = ImGui::GetColorU32(kBackdropTint);
  dl->AddImage(static_cast<ImTextureID>(static_cast<std::intptr_t>(bg.tex)), a, b, uv0, uv1, tint);
}

}  // namespace

// Minimum launch spinner before the sign-in modal (sign-in uses DrawSignInGate, not this overlay).
static constexpr double kLaunchSpinnerMinSec = 5.0;
static std::chrono::steady_clock::time_point s_launchAuthOverlayStart {};
static bool                                    s_launchAuthTimerActive = false;

static bool LaunchSequenceHoldForMinSpinner() {
  if (!s_launchAuthTimerActive)
    return false;
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                       s_launchAuthOverlayStart)
                             .count();
  return elapsed < kLaunchSpinnerMinSec;
}

static void ResetLaunchAuthOverlayTimer() {
  s_launchAuthTimerActive = false;
}

static bool LaunchSequenceAwaitingSignInGate(const AppCommandState& cmd) {
  return HasInternetConnectivity() && !cmd.authGateResolved && !cmd.authBusy &&
         !cmd.authSignedIn;
}

void BeginLaunchAuthOverlayTimer() {
  if (!s_launchAuthTimerActive) {
    s_launchAuthOverlayStart = std::chrono::steady_clock::now();
    s_launchAuthTimerActive  = true;
  }
}

void RequestWhatsNewWindow(AppCommandState& cmd) {
  cmd.showWhatsNewWindow = true;
  cmd.whatsNewDontShowChecked =
      WhatsNewDismissCheckboxChecked(GOSURVEY_VERSION_FULL, cmd.whatsNewDismissedVersion);
}

void MaybeAutoOpenWhatsNew(AppCommandState& cmd) {
  if (cmd.activeDrawingIdx != 0)
    return;
  if (!WhatsNewShouldAutoOpen(GOSURVEY_VERSION_FULL, cmd.whatsNewDismissedVersion,
                              cmd.whatsNewAutoOpenedThisLaunch))
    return;
  // REQ-336 + REQ-091: auto-open waits for sign-in when online — offline uses the same skip as
  // the auth gate (nothing to sign in with).
  if (!cmd.authGateResolved && HasInternetConnectivity())
    return;
  if (LaunchSequenceHoldForMinSpinner())
    return;
  // Interactive sign-in completes in the browser while GoSurvey is in the background — defer
  // auto-open until the user returns so the opening animation is visible.
  if (!cmd.appWindowFocused)
    return;
  cmd.whatsNewAutoOpenedThisLaunch = true;
  RequestWhatsNewWindow(cmd);
}

bool LaunchSequenceOverlayActive(const AppCommandState& cmd, const bool updateOfferBlocks) {
  if (updateOfferBlocks)
    return false;
  if (cmd.whatsNewModalVisible)
    return false;

  const bool online = HasInternetConnectivity();

  // Hand off to DrawSignInGate once the min spinner elapsed and the silent refresh finished.
  if (LaunchSequenceAwaitingSignInGate(cmd) && !LaunchSequenceHoldForMinSpinner())
    return false;

  // Auto-open path: stay up from splash until What's New is on screen.
  if (cmd.whatsNewOpeningPending || cmd.showWhatsNewWindow)
    return true;

  // REQ-091: launch spinner while auth is in flight or during the minimum display window.
  if (online && !cmd.authGateResolved &&
      (cmd.authBusy || LaunchSequenceHoldForMinSpinner()))
    return true;

  return false;
}

void DrawLaunchSequenceOverlay(AppCommandState& cmd, const bool updateOfferBlocks) {
  if (!LaunchSequenceOverlayActive(cmd, updateOfferBlocks)) {
    if (cmd.authGateResolved)
      ResetLaunchAuthOverlayTimer();
    return;
  }

  const bool online = HasInternetConnectivity();

  const bool checkingAuth =
      online && !cmd.authGateResolved && (cmd.authBusy || LaunchSequenceHoldForMinSpinner());

  const char* spinnerStatus =
      checkingAuth ? (cmd.authInteractiveBusy ? "Waiting for browser…" : "Checking sign-in…")
                   : "Opening release notes…";

  ImGuiViewport* vp = ImGui::GetMainViewport();

  // Input-blocking shell (transparent). Spinner paints on the foreground draw list below.
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowViewport(vp->ID);
  ImGui::SetNextWindowFocus();

  const ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoInputs;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
  ImGui::Begin("##LaunchSequence336", nullptr, wf);
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);

  // Foreground draw list: above docked shell chrome; sign-in uses DrawSignInGate instead.
  DrawLaunchSpinnerForeground(vp, spinnerStatus);
}

void DrawWhatsNewWindow(AppCommandState& cmd) {
  const char* kPopupId = "What's New##GoSurvey336";

  static bool   wasOpen              = false;
  static int    openFrames           = 0;
  static double whatsNewOpenAnimStart = -1.0;
  constexpr double kWhatsNewOpenAnimSec = 0.82;

  if (!cmd.showWhatsNewWindow) {
    wasOpen               = false;
    openFrames            = 0;
    whatsNewOpenAnimStart = -1.0;
    return;
  }
  if (!wasOpen) {
    wasOpen    = true;
    openFrames = 0;
  }

  // Hold the real modal back so the launch overlay spinner paints for a couple of frames first.
  ++openFrames;
  if (openFrames <= 2) {
    if (openFrames == 2)
      EnsureWhatsNewAssetsLoaded();
    return;
  }

  const WhatsNewContent& cachedContent = CachedWhatsNewContent();

  if (!ImGui::IsPopupOpen(kPopupId))
    ImGui::OpenPopup(kPopupId);

  if (whatsNewOpenAnimStart < 0.0)
    whatsNewOpenAnimStart = ImGui::GetTime();

  constexpr ImVec2 kWhatsNewSize(1120.f, 960.f);
  const float      rawAnimT = static_cast<float>(
      (ImGui::GetTime() - whatsNewOpenAnimStart) / kWhatsNewOpenAnimSec);
  const float animT = (std::min)(1.f, (std::max)(0.f, rawAnimT));

  // Scale + fade: slower first half. Drop + bounce: second half, from above center.
  constexpr float kScaleEndT = 0.54f;
  constexpr float kFadeEndT  = 0.58f;
  constexpr float kDropStartT = 0.52f;
  constexpr float kDropFromY  = -78.f;

  const float scalePhase = (std::min)(1.f, animT / kScaleEndT);
  const float fadePhase  = (std::min)(1.f, animT / kFadeEndT);
  const float dropPhase =
      (std::min)(1.f, (std::max)(0.f, (animT - kDropStartT) / (1.f - kDropStartT)));

  const float scaleEase = EaseOutCubic(scalePhase);
  const float alphaT    = EaseOutCubic(fadePhase);
  const float scale     = LerpFloat(0.80f, 1.f, scaleEase);
  const float yLift     = LerpFloat(kDropFromY, 0.f, EaseOutBounce(dropPhase));

  const ImVec2 animSize(kWhatsNewSize.x * scale, kWhatsNewSize.y * scale);
  const ImVec2 center   = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowSize(animSize, ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(center.x, center.y + yLift), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

  const ImVec4 winBg = IsDark() ? ImVec4(0.03f, 0.05f, 0.10f, 1.f) : ImVec4(0.98f, 0.99f, 1.f, 1.f);
  const ImVec4 titleBg = Lerp(IsDark() ? ImVec4(0.025f, 0.04f, 0.08f, 1.f)
                                       : ImVec4(0.88f, 0.92f, 0.97f, 1.f),
                              Accent(), 0.55f);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alphaT);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, LerpFloat(0.5f, 2.5f, scaleEase));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, winBg);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, WhatsNewBodyBase());
  ImGui::PushStyleColor(ImGuiCol_Border, Accent());
  ImGui::PushStyleColor(ImGuiCol_TitleBg, titleBg);
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, AccentLo());
  ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, titleBg);
  ImGui::PushStyleColor(ImGuiCol_CheckMark, AccentHi());
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IsDark() ? ImVec4(0.04f, 0.07f, 0.12f, 1.f)
                                                   : ImVec4(0.90f, 0.93f, 0.97f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Lerp(winBg, Accent(), 0.20f));
  ImGui::PushStyleColor(ImGuiCol_Separator, Lerp(winBg, Accent(), 0.45f));

  // Modal: blocks every click/hover to windows behind until Close (same pattern as SignInGate /
  // UpdateDialog). No close button — exit is explicit Close or View release notes only.
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoSavedSettings;

  if (!ImGui::BeginPopupModal(kPopupId, nullptr, flags)) {
    ImGui::PopStyleColor(10);
    ImGui::PopStyleVar(4);
    return;
  }

  cmd.whatsNewModalVisible   = true;
  cmd.whatsNewOpeningPending = false;

  ImGui::PushFont(FontReg::Billboard());

  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2  a  = ImGui::GetWindowPos();
    const ImVec2  b(a.x + ImGui::GetWindowSize().x, a.y + ImGui::GetWindowSize().y);
    const float   rnd = ImGui::GetStyle().WindowRounding;
    DrawWhatsNewWindowGradient(dl, a, b, rnd);
  }

  {
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const ImVec2 a = ImGui::GetWindowPos();
    const ImVec2 b(a.x + ImGui::GetWindowSize().x, a.y + ImGui::GetWindowSize().y);
    const float rnd = ImGui::GetStyle().WindowRounding;
    const float glowA = alphaT * LerpFloat(0.f, 1.f, scaleEase);
    ImVec4 accentGlow = AccentHi();
    accentGlow.w *= glowA;
    bg->AddRectFilled(ImVec2(a.x + 8.f, a.y + 10.f), ImVec2(b.x + 8.f, b.y + 10.f),
                      ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.45f * alphaT)), rnd);
    bg->AddRect(ImVec2(a.x - 1.f, a.y - 1.f), ImVec2(b.x + 1.f, b.y + 1.f),
                ImGui::GetColorU32(accentGlow), rnd + 1.f, 0, LerpFloat(0.5f, 2.f, scaleEase));
  }

  // Centered app logo (crisp GS badge — same mark as the Start hero).
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 logoRow = ImGui::GetCursorScreenPos();
    const float contentW = ImGui::GetContentRegionAvail().x;
    const float logoSz = LerpFloat(52.f, 72.f, scaleEase);
    const float logoPadTop = LerpFloat(14.f, 22.f, scaleEase);
    const float logoPadBot = LerpFloat(10.f, 16.f, scaleEase);
    ImGui::Dummy(ImVec2(0.f, logoPadTop + logoSz + logoPadBot));
    const ImVec2 logoCenter(logoRow.x + contentW * 0.5f, logoRow.y + logoPadTop + logoSz * 0.5f);
    DrawGsBadge(dl, logoCenter, logoSz);
  }

  const ImVec2 bodySize(0.f, -ImGui::GetFrameHeightWithSpacing() * 2.6f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
  ImGui::PushStyleColor(ImGuiCol_Border, Lerp(winBg, Accent(), 0.35f));
  // Transparent child so the backdrop shows; we paint a solid fill + image ourselves.
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
  if (ImGui::BeginChild("##whatsNewBody", bodySize, true,
                        ImGuiWindowFlags_NoBackground)) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetWindowPos();
    const ImVec2 b(a.x + ImGui::GetWindowSize().x, a.y + ImGui::GetWindowSize().y);
    dl->AddRectFilled(a, b, ImGui::GetColorU32(WhatsNewBodyBase()), 6.f);
    DrawFaintBackdrop(dl, a, b);

    // Keep markdown above the backdrop in z-order by drawing text after the image.
    ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, ImGui::GetStyle().WindowPadding.y));
    if (cachedContent.ok) {
      DrawMarkdownImGui(cachedContent.markdown);
    } else {
      ImGui::TextWrapped("%s", cachedContent.fallbackMessage.c_str());
      ImGui::Spacing();
      ImGui::TextDisabled("Full release notes are still available on GitHub.");
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::Checkbox("Don't show this screen again for this version?", &cmd.whatsNewDontShowChecked);

  if (AccentButton("View release notes on GitHub", true))
    OpenReleasesPage();
  ImGui::SameLine();
  if (AccentButton("Close", false)) {
    ApplyDismissOnClose(cmd);
    cmd.showWhatsNewWindow     = false;
    cmd.whatsNewOpeningPending = false;
    cmd.whatsNewModalVisible   = false;
    wasOpen               = false;
    whatsNewOpenAnimStart = -1.0;
    ImGui::CloseCurrentPopup();
  }

  ImGui::PopFont();
  ImGui::EndPopup();
  ImGui::PopStyleColor(10);
  ImGui::PopStyleVar(4);
}
