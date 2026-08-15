// REQ-078 — the update dialog. The application presents an available update and WAITS.
//
// There is deliberately no path through this file that downloads or installs anything without a
// click: GoSurvey holds unsaved drawings, and an unannounced restart destroys work.

#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "UpdateService.hpp"

#include <imgui.h>

#include <cstdio>

namespace {

/// "12.4 MB of 31.0 MB" — or just the received figure when the server sent no Content-Length.
std::string FormatProgress(long long received, long long total)
{
  auto mb = [](long long bytes) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return std::string(buf);
  };
  if (total > 0)
    return mb(received) + " of " + mb(total);
  return mb(received);
}

}  // namespace

void DrawUpdateDialog(AppCommandState& cmd, update::UpdateState& upd)
{
  using update::Phase;

  if (upd.phase == Phase::Idle)
    return;

  const char* kTitle = "Software Update";
  if (!ImGui::IsPopupOpen(kTitle))
    ImGui::OpenPopup(kTitle);

  // Width is PINNED every frame; only the height auto-fits (the 0.f component).
  //
  // It used to be ImGuiCond_Appearing + AlwaysAutoResize, which re-fitted the window to its
  // content on every frame. The explicit width therefore applied once and was then ignored, so
  // the moment the phase changed from UpdateReady (wide: release-notes child and three buttons)
  // to Downloading (narrow: one line of text and a Cancel button) the window visibly shrank
  // sideways mid-download. Reported from the first live update, and it looks like a glitch
  // rather than a layout decision.
  ImGui::SetNextWindowSize(ImVec2(560.f, 0.f), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));

  // No close button and no click-away dismissal: every exit from this dialog is one of the
  // explicit choices below, so "I closed the window" can never be mistaken for a decision.
  // NoResize because the width is ours to control now, not the user's to drag.
  if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoResize))
    return;

  switch (upd.phase)
  {
    case Phase::Checking:
    {
      // REQ-077 (amended): the check gates the session. This dialog is the gate — modal, so no
      // drawing can be started on a build that is about to ask to replace itself.
      //
      // A negative fraction makes ImGui animate an indeterminate bar. That animation is only
      // possible because the fetch is on a worker: a UI thread blocked on a socket cannot
      // repaint, so a literally-blocking check would show a frozen window and Windows would
      // paint it as "Not Responding".
      ImGui::TextUnformatted("Checking for updates...");
      ImGui::ProgressBar(-1.f * static_cast<float>(ImGui::GetTime()), ImVec2(-1.f, 0.f), "");
      ImGui::TextDisabled("GoSurvey %s - %s channel", upd.runningVersion.c_str(),
                          upd.prefs.useBetaChannel ? "beta" : "stable");
      break;
    }

    case Phase::UpdateReady:
    {
      ImGui::Text("GoSurvey %s is available.", upd.available.version.c_str());
      ImGui::TextDisabled("You are running %s.", upd.runningVersion.c_str());
      ImGui::Separator();

      if (!upd.available.notes.empty())
      {
        ImGui::TextUnformatted("What's new:");
        ImGui::BeginChild("##updatenotes", ImVec2(0.f, 160.f), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(upd.available.notes.c_str());
        ImGui::EndChild();
      }

      ImGui::TextDisabled(
          "GoSurvey will close and reopen automatically. You'll be asked to save any open work "
          "first.");
      ImGui::Separator();

      if (ImGui::Button("Update Now", ImVec2(130.f, 0.f)))
        update::BeginDownload(upd);

      ImGui::SameLine();
      if (ImGui::Button("Remind Me Later", ImVec2(150.f, 0.f)))
      {
        // No state written: the next launch checks again and offers this same version.
        upd.phase = Phase::Idle;
        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();
      if (ImGui::Button("Skip This Version", ImVec2(150.f, 0.f)))
      {
        update::SkipAvailableVersion(upd);
        cmd.updatePrefs = upd.prefs;   // persist the skip through UserPrefs
        ImGui::CloseCurrentPopup();
      }
      break;
    }

    case Phase::Downloading:
    {
      const long long received = upd.task ? upd.task->bytesReceived.load() : 0;
      const long long total    = upd.task ? upd.task->bytesTotal.load() : 0;

      ImGui::Text("Downloading GoSurvey %s...", upd.available.version.c_str());
      const float fraction =
          (total > 0) ? static_cast<float>(static_cast<double>(received) / static_cast<double>(total))
                      : -1.f;   // indeterminate: ImGui animates a marquee for a negative fraction
      ImGui::ProgressBar(fraction, ImVec2(-1.f, 0.f), FormatProgress(received, total).c_str());

      if (ImGui::Button("Cancel", ImVec2(130.f, 0.f)) && upd.task)
        upd.task->cancel.store(true, std::memory_order_relaxed);
      break;
    }

    case Phase::ReadyToLaunch:
    {
      ImGui::Text("GoSurvey %s is ready to install.", upd.available.version.c_str());
      ImGui::TextDisabled("The installer will close GoSurvey, update it, and start it again.");
      ImGui::Separator();

      if (ImGui::Button("Install and Restart", ImVec2(180.f, 0.f)))
      {
        // The unsaved-work guard is the application loop's job, not this dialog's: it owns the
        // drawing list and the existing unsaved-changes modal. Raising the flag hands over.
        upd.awaitingUnsavedCheck = true;
      }
      ImGui::SameLine();
      if (ImGui::Button("Not Now", ImVec2(130.f, 0.f)))
      {
        // The verified installer stays on disk; the next launch re-offers and re-uses it.
        upd.phase = Phase::Idle;
        ImGui::CloseCurrentPopup();
      }
      break;
    }

    case Phase::Failed:
      // REQ-201 in full force: this path was user-initiated, so unlike the silent startup check
      // its failure is stated plainly rather than swallowed.
      ImGui::TextUnformatted("The update could not be completed.");
      ImGui::Separator();
      ImGui::TextWrapped("%s", upd.lastError.c_str());
      ImGui::Separator();
      ImGui::TextDisabled("You can keep working; the update will be offered again next time.");
      if (ImGui::Button("Close", ImVec2(130.f, 0.f)))
      {
        upd.lastError.clear();
        upd.phase = Phase::Idle;
        ImGui::CloseCurrentPopup();
      }
      break;

    default:
      break;
  }

  ImGui::EndPopup();
}
