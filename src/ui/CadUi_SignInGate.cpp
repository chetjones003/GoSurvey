// REQ-091 (amended 2026-08-23) — the launch-time sign-in gate. Every launch, until the user is
// signed in or there is no internet at all to sign in with, this modal blocks the rest of the
// application.

#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "FontRegistry.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace {

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, const float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}

float EaseOutCubic(const float t) {
  const float u = 1.f - t;
  return 1.f - u * u * u;
}

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

ImVec4 Accent() { return ImVec4(0.26f, 0.56f, 0.86f, 1.f); }
ImVec4 AccentHi() { return ImVec4(0.34f, 0.64f, 0.95f, 1.f); }
ImVec4 AccentLo() { return ImVec4(0.20f, 0.45f, 0.72f, 1.f); }

bool AccentButton(const char* label, const bool primary, const ImVec2& size = ImVec2(0.f, 0.f)) {
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
  const bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);
  return clicked;
}

void DrawGsBadge(ImDrawList* dl, const ImVec2 c, const float sz) {
  const ImVec2 a(c.x - sz * 0.5f, c.y - sz * 0.5f);
  const ImVec2 b(c.x + sz * 0.5f, c.y + sz * 0.5f);
  const float  rnd = sz * 0.24f;
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

void DrawSignInWindowGradient(ImDrawList* dl, const ImVec2 a, const ImVec2 b, const float rounding) {
  // Full-window dark navy gradient — subtle accent at the top, near-black at the bottom.
  const ImVec4 topL = Lerp(ImVec4(0.06f, 0.11f, 0.20f, 1.f), Accent(), 0.14f);
  const ImVec4 topR = Lerp(ImVec4(0.05f, 0.09f, 0.17f, 1.f), Accent(), 0.10f);
  const ImVec4 botR = ImVec4(0.015f, 0.025f, 0.055f, 1.f);
  const ImVec4 botL = ImVec4(0.02f, 0.035f, 0.065f, 1.f);
  dl->AddRectFilledMultiColor(a, b, ImGui::ColorConvertFloat4ToU32(topL),
                              ImGui::ColorConvertFloat4ToU32(topR),
                              ImGui::ColorConvertFloat4ToU32(botR),
                              ImGui::ColorConvertFloat4ToU32(botL));
  if (rounding > 0.f)
    dl->AddRect(a, b, ImGui::GetColorU32(Lerp(AccentLo(), ImVec4(0.f, 0.f, 0.f, 1.f), 0.72f)),
                rounding, 0, 1.f);
}

}  // namespace

void DrawSignInGateBody(AppCommandState& cmd) {
  const float contentW = ImGui::GetContentRegionAvail().x;
  constexpr float kSidePad = 28.f;
  constexpr float kBtnW    = 200.f;

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.92f, 0.97f, 1.f));
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kSidePad);
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentW - kSidePad * 2.f);
  ImGui::TextWrapped(
      "Sign in with Google, Microsoft, or an email and password to continue.");
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();

  ImGui::Spacing();
  ImGui::Spacing();

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (contentW - kBtnW) * 0.5f);
  ImGui::BeginDisabled(cmd.authBusy);
  if (AccentButton(cmd.authInteractiveBusy ? "Waiting for browser…" : "Sign In", true,
                   ImVec2(kBtnW, 0.f)))
    cmd.authSignInRequested = true;
  ImGui::EndDisabled();

  if (!cmd.authError.empty()) {
    ImGui::Spacing();
    const ImVec2 errSz = ImGui::CalcTextSize(cmd.authError.c_str(), nullptr, false, contentW - kSidePad * 2.f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (contentW - errSz.x) * 0.5f);
    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.f), "%s", cmd.authError.c_str());
  }
}

void DrawSignInGate(AppCommandState& cmd) {
  if (cmd.authGateResolved)
    return;

  const char* kPopupId = "Sign In Required##GoSurvey091";

  static double signInOpenAnimStart = -1.0;
  constexpr double kSignInOpenAnimSec = 0.82;

  if (!ImGui::IsPopupOpen(kPopupId))
    ImGui::OpenPopup(kPopupId);

  if (signInOpenAnimStart < 0.0)
    signInOpenAnimStart = ImGui::GetTime();

  constexpr ImVec2 kSignInSize(480.f, 0.f);
  const float      rawAnimT = static_cast<float>(
      (ImGui::GetTime() - signInOpenAnimStart) / kSignInOpenAnimSec);
  const float animT = (std::min)(1.f, (std::max)(0.f, rawAnimT));

  constexpr float kScaleEndT    = 0.54f;
  constexpr float kFadeEndT     = 0.58f;
  constexpr float kDropStartT   = 0.52f;
  constexpr float kDropFromY    = -48.f;

  const float scalePhase = (std::min)(1.f, animT / kScaleEndT);
  const float fadePhase  = (std::min)(1.f, animT / kFadeEndT);
  const float dropPhase =
      (std::min)(1.f, (std::max)(0.f, (animT - kDropStartT) / (1.f - kDropStartT)));

  const float scaleEase = EaseOutCubic(scalePhase);
  const float alphaT    = EaseOutCubic(fadePhase);
  const float scale     = LerpFloat(0.84f, 1.f, scaleEase);
  const float yLift     = LerpFloat(kDropFromY, 0.f, EaseOutBounce(dropPhase));

  const ImVec2 animSize(kSignInSize.x * scale, 380.f * scale);
  const ImVec2 center   = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowSize(animSize, ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(center.x, center.y + yLift), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alphaT);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, LerpFloat(0.5f, 2.5f, scaleEase));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.f, 16.f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
  ImGui::PushStyleColor(ImGuiCol_Border, AccentLo());
  ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.f, 0.f, 0.f, 0.f));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.f, 0.f, 0.f, 0.f));
  ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.f, 0.f, 0.f, 0.f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.95f, 0.99f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.20f, 0.38f, 0.58f, 0.45f));

  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoSavedSettings;

  if (!ImGui::BeginPopupModal(kPopupId, nullptr, flags)) {
    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(4);
    return;
  }

  {
    ImDrawList*  bg  = ImGui::GetBackgroundDrawList();
    const ImVec2 a   = ImGui::GetWindowPos();
    const ImVec2 b(a.x + ImGui::GetWindowSize().x, a.y + ImGui::GetWindowSize().y);
    const float  rnd = ImGui::GetStyle().WindowRounding;
    DrawSignInWindowGradient(bg, a, b, rnd);
    ImVec4 accentGlow = AccentHi();
    accentGlow.w *= alphaT * scaleEase * 0.85f;
    bg->AddRectFilled(ImVec2(a.x + 8.f, a.y + 10.f), ImVec2(b.x + 8.f, b.y + 10.f),
                      ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.40f * alphaT)), rnd);
    bg->AddRect(ImVec2(a.x - 1.f, a.y - 1.f), ImVec2(b.x + 1.f, b.y + 1.f),
                ImGui::GetColorU32(accentGlow), rnd + 1.f, 0, LerpFloat(0.5f, 2.f, scaleEase));
  }

  ImGui::PushFont(FontReg::Billboard());

  {
    ImDrawList*  dl       = ImGui::GetWindowDrawList();
    const ImVec2 logoRow  = ImGui::GetCursorScreenPos();
    const float  contentW = ImGui::GetContentRegionAvail().x;
    constexpr float kLogo = 64.f;
    constexpr float kLogoPadTop = 16.f;
    constexpr float kLogoPadBot = 8.f;
    ImGui::Dummy(ImVec2(0.f, kLogoPadTop + kLogo + kLogoPadBot));
    const ImVec2 logoCenter(logoRow.x + contentW * 0.5f, logoRow.y + kLogoPadTop + kLogo * 0.5f);
    DrawGsBadge(dl, logoCenter, kLogo);
  }

  DrawSignInGateBody(cmd);

  ImGui::PopFont();
  ImGui::EndPopup();
  ImGui::PopStyleColor(8);
  ImGui::PopStyleVar(4);

  if (cmd.authGateResolved)
    signInOpenAnimStart = -1.0;
}
