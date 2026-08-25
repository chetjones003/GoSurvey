// REQ-091 (amended 2026-08-23) — the launch-time sign-in gate. Every launch, until the user is
// signed in or there is no internet at all to sign in with, this modal blocks the rest of the
// application.
//
// This is a real reversal of REQ-091's original acceptance condition ("no application feature is
// gated by sign-in state") — recorded as a decision (spec/project.md), not slipped in quietly.
// The offline exception mirrors REQ-077's update-check gate exactly, for the same reason: a
// surveyor opening GoSurvey on a job site with no signal must not be locked out of a program that
// has nothing to check in with.

#include "CadUi.hpp"

#include "CadCommands.hpp"

#include <imgui.h>

void DrawSignInGate(AppCommandState& cmd)
{
  if (cmd.authGateResolved)
    return;

  const char* kTitle = "Sign In Required";
  if (!ImGui::IsPopupOpen(kTitle))
    ImGui::OpenPopup(kTitle);

  ImGui::SetNextWindowSize(ImVec2(420.f, 0.f), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));

  // No close button, no click-away dismissal, NoResize — same reasoning as DrawUpdateDialog:
  // every exit is the explicit Sign In button succeeding, never an accidental dismissal.
  if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoResize))
    return;

  ImGui::TextWrapped(
      "Sign in with Google, Microsoft, or an email and password to continue.");
  ImGui::Spacing();

  ImGui::BeginDisabled(cmd.authBusy);
  if (ImGui::Button(cmd.authInteractiveBusy ? "Waiting for browser..." : "Sign In",
                    ImVec2(-FLT_MIN, 0.f))) {
    cmd.authSignInRequested = true;
  }
  ImGui::EndDisabled();

  if (!cmd.authError.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.f), "%s", cmd.authError.c_str());
  }

  ImGui::EndPopup();
}
