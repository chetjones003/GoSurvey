#include "CadUi.hpp"

#include "AppIcon.hpp"
#include "AppPaths.hpp"
#include "FontRegistry.hpp"
#include "CadCommands.hpp"
#include "MarkdownImGui.hpp"
#include "UserPrefs.hpp"
#include "Version.hpp"
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

#include <cstdint>
#include <string>

namespace {

constexpr const char* kReleasesUrl = "https://github.com/chetjones003/GoSurvey/releases";
// Dark slate tint (not white): matches SplashScreen backdrop so launch → What's New reads as one product.
constexpr ImVec4 kBackdropTint {0.25f, 0.30f, 0.40f, 0.80f};

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t);
}
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
  cmd.whatsNewAutoOpenedThisLaunch = true;
  RequestWhatsNewWindow(cmd);
}

void DrawWhatsNewWindow(AppCommandState& cmd) {
  const char* kPopupId = "What's New##GoSurvey336";

  static bool contentLoaded = false;
  static WhatsNewContent cachedContent{};
  static bool wasOpen = false;
  if (!cmd.showWhatsNewWindow) {
    wasOpen = false;
    return;
  }
  if (!wasOpen) {
    cachedContent = LoadWhatsNewContent();
    contentLoaded = true;
  }
  wasOpen = true;

  if (!ImGui::IsPopupOpen(kPopupId))
    ImGui::OpenPopup(kPopupId);

  constexpr ImVec2 kWhatsNewSize(1120.f, 960.f);
  ImGui::SetNextWindowSize(kWhatsNewSize, ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));

  const ImVec4 winBg = IsDark() ? ImVec4(0.14f, 0.16f, 0.19f, 1.f) : ImVec4(0.98f, 0.99f, 1.f, 1.f);
  const ImVec4 titleBg = Lerp(IsDark() ? ImVec4(0.10f, 0.12f, 0.15f, 1.f)
                                       : ImVec4(0.88f, 0.92f, 0.97f, 1.f),
                              Accent(), 0.55f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.5f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 12.f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, winBg);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IsDark() ? ImVec4(0.11f, 0.12f, 0.14f, 1.f)
                                                   : ImVec4(0.94f, 0.96f, 0.99f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Border, Accent());
  ImGui::PushStyleColor(ImGuiCol_TitleBg, titleBg);
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, AccentLo());
  ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, titleBg);
  ImGui::PushStyleColor(ImGuiCol_CheckMark, AccentHi());
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IsDark() ? ImVec4(0.18f, 0.20f, 0.24f, 1.f)
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
    ImGui::PopStyleVar(3);
    return;
  }

  ImGui::PushFont(FontReg::Billboard());

  {
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const ImVec2 a = ImGui::GetWindowPos();
    const ImVec2 b(a.x + ImGui::GetWindowSize().x, a.y + ImGui::GetWindowSize().y);
    const float rnd = ImGui::GetStyle().WindowRounding;
    bg->AddRectFilled(ImVec2(a.x + 8.f, a.y + 10.f), ImVec2(b.x + 8.f, b.y + 10.f),
                      ImGui::GetColorU32(ImVec4(0.f, 0.f, 0.f, 0.45f)), rnd);
    bg->AddRect(ImVec2(a.x - 1.f, a.y - 1.f), ImVec2(b.x + 1.f, b.y + 1.f),
                ImGui::GetColorU32(AccentHi()), rnd + 1.f, 0, 2.f);
  }

  // Centered app logo (crisp GS badge — same mark as the Start hero).
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 logoRow = ImGui::GetCursorScreenPos();
    const float contentW = ImGui::GetContentRegionAvail().x;
    constexpr float kLogo = 72.f;
    // Extra top pad so the badge sits clear of the title-bar clip (avoids squared top corners).
    constexpr float kLogoPadTop = 22.f;
    constexpr float kLogoPadBot = 16.f;
    ImGui::Dummy(ImVec2(0.f, kLogoPadTop + kLogo + kLogoPadBot));
    const ImVec2 logoCenter(logoRow.x + contentW * 0.5f, logoRow.y + kLogoPadTop + kLogo * 0.5f);
    DrawGsBadge(dl, logoCenter, kLogo);
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
    dl->AddRectFilled(a, b, ImGui::GetColorU32(IsDark() ? ImVec4(0.11f, 0.12f, 0.14f, 1.f)
                                                         : ImVec4(0.94f, 0.96f, 0.99f, 1.f)),
                      6.f);
    DrawFaintBackdrop(dl, a, b);

    // Keep markdown above the backdrop in z-order by drawing text after the image.
    ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, ImGui::GetStyle().WindowPadding.y));
    if (contentLoaded && cachedContent.ok) {
      DrawMarkdownImGui(cachedContent.markdown);
    } else if (contentLoaded) {
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
    cmd.showWhatsNewWindow = false;
    wasOpen = false;
    ImGui::CloseCurrentPopup();
  }

  ImGui::PopFont();
  ImGui::EndPopup();
  ImGui::PopStyleColor(10);
  ImGui::PopStyleVar(3);
}
