#include "UpdateService.hpp"

#include "HttpFetch.hpp"
#include "Version.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// shellapi.h is excluded by WIN32_LEAN_AND_MEAN and must be asked for explicitly; it is where
// ShellExecuteExW lives, which is how the installer is launched with elevation.
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>

namespace update {
namespace {

/// How long the check waits before giving up.
///
/// This is now the worst case a user waits at startup, not a background cost: REQ-077 (amended)
/// makes the check modal, so an unreachable network is time the surveyor spends staring at a
/// progress bar before they can work. Shortened from 5s to 3s when the check became blocking —
/// ample for a few hundred bytes of JSON on any working connection, and the shortest the offline
/// case can be made without giving up on slow ones.
constexpr int kCheckTimeoutMs = 3000;

/// The installer download is user-initiated and much larger, so it gets a real budget.
constexpr int kDownloadTimeoutMs = 120000;

/// The REQ-077 failure path. Logged, never surfaced — the one sanctioned silent failure in the
/// project (ADR-029 (h), decision log 2026-08-15). Note the narrowness: this is used by the
/// unattended check only. Every user-initiated failure goes to Phase::Failed and is shown.
void LogSilently(const std::string& what)
{
  ::OutputDebugStringA(("[update] " + what + "\n").c_str());
}

std::string LocalAppDataDir()
{
  PWSTR  raw = nullptr;
  std::string result;
  if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw)
  {
    result = std::filesystem::path(raw).u8string();
    ::CoTaskMemFree(raw);
  }
  return result;
}

}  // namespace

std::string UpdateDownloadDir()
{
  const std::string base = LocalAppDataDir();
  if (base.empty())
    return std::string();
  return (std::filesystem::u8path(base) / "GoSurvey" / "updates").u8string();
}

void BeginStartupCheck(UpdateState& st, const std::string& ownerRepo)
{
  st.runningVersion = GOSURVEY_VERSION_FULL;

  if (!st.prefs.enabled)
    return;                              // REQ-077: disabled means no network request, at all
  if (st.task || st.phase != Phase::Idle)
    return;                              // already running or already showing something

  // REQ-077: with no route to the internet there is nothing to check, so skip entirely rather
  // than show a modal that can only end in a timeout. This is what keeps the gate from punishing
  // a surveyor opening the program somewhere with no signal.
  if (!HasInternetConnectivity())
  {
    LogSilently("check skipped: no internet connectivity reported");
    return;
  }

  // Inputs are COPIED into the worker (architecture §8, rule 1) — it holds no pointer back into
  // UpdateState, let alone AppCommandState.
  const Channel     channel = st.prefs.useBetaChannel ? Channel::Beta : Channel::Stable;
  const std::string url     = ManifestUrlForChannel(channel, ownerRepo);
  const std::string running = st.runningVersion;
  const std::string skipped = st.prefs.skippedVersion;

  st.task   = std::make_unique<UpdateTask>();
  st.phase  = Phase::Checking;
  UpdateTask* task = st.task.get();

  task->worker = std::thread([task, url, running, skipped]() {
    std::string body, error;
    if (!HttpGetString(url, kCheckTimeoutMs, body, error))
    {
      task->ok    = false;
      task->error = error;
      task->done.store(true, std::memory_order_release);
      return;
    }

    Manifest manifest;
    if (!ParseManifest(body, manifest, error))
    {
      task->ok    = false;
      task->error = error;
      task->done.store(true, std::memory_order_release);
      return;
    }

    std::string reason;
    const Decision decision = DecideUpdate(running, manifest, skipped, reason);
    task->ok       = (decision == Decision::Offer);
    task->error    = reason;             // carries the "why not" on a NoUpdate, for the log
    task->manifest = manifest;
    task->done.store(true, std::memory_order_release);   // rule 3: the worker's last act
  });
}

void BeginDownload(UpdateState& st)
{
  if (st.phase != Phase::UpdateReady || st.task)
    return;

  const std::string dir = UpdateDownloadDir();
  if (dir.empty())
  {
    st.phase     = Phase::Failed;
    st.lastError = "could not locate the local application data folder";
    return;
  }

  // Name the file after the version so two attempts at different versions cannot collide.
  const std::string dest =
      (std::filesystem::u8path(dir) / ("GoSurvey-" + st.available.version + "-Installer.exe"))
          .u8string();

  const Manifest manifest = st.available;   // copied into the worker, not referenced

  st.task  = std::make_unique<UpdateTask>();
  st.phase = Phase::Downloading;
  UpdateTask* task = st.task.get();

  task->worker = std::thread([task, manifest, dest]() {
    std::string error;
    auto onProgress = [task](long long received, long long total) {
      task->bytesReceived.store(received, std::memory_order_relaxed);
      task->bytesTotal.store(total, std::memory_order_relaxed);
      return !task->cancel.load(std::memory_order_relaxed);   // rule 5: cooperative cancellation
    };

    if (!HttpDownloadFile(manifest.installerUrl, dest, kDownloadTimeoutMs, onProgress, error))
    {
      task->ok    = false;
      task->error = error;
      task->done.store(true, std::memory_order_release);
      return;
    }

    std::string actualHash;
    if (!ComputeFileSha256(dest, actualHash, error))
    {
      task->ok    = false;
      task->error = error;
      task->done.store(true, std::memory_order_release);
      return;
    }

    // REQ-078: a hash mismatch must not execute, and must not leave the file behind to be
    // retried or run by hand.
    if (actualHash != manifest.sha256)
    {
      std::error_code ec;
      std::filesystem::remove(std::filesystem::u8path(dest), ec);
      task->ok    = false;
      task->error = "the downloaded installer failed its integrity check and was discarded "
                    "(expected " + manifest.sha256 + ", got " + actualHash + ")";
      task->done.store(true, std::memory_order_release);
      return;
    }

    task->ok             = true;
    task->downloadedPath = dest;
    task->done.store(true, std::memory_order_release);
  });
}

void PollUpdateTask(UpdateState& st)
{
  if (!st.task)
    return;
  if (!st.task->done.load(std::memory_order_acquire))
    return;

  // Take ownership before touching the results, so the thread is joined exactly once and the
  // task cannot be polled twice.
  std::unique_ptr<UpdateTask> finished = std::move(st.task);
  if (finished->worker.joinable())
    finished->worker.join();

  switch (st.phase)
  {
    case Phase::Checking:
      // The unattended path. Neither outcome is shown to the user: an update becomes a dialog,
      // and everything else — no network, timeout, 404, malformed JSON, up-to-date — is a log
      // line (ADR-029 (h)).
      if (finished->ok)
      {
        st.available = finished->manifest;
        st.phase     = Phase::UpdateReady;
      }
      else
      {
        LogSilently("check: " + finished->error);
        st.phase = Phase::Idle;
      }
      break;

    case Phase::Downloading:
      // User-initiated, so REQ-201 applies normally and failures are reported.
      if (finished->ok)
      {
        st.available.installerUrl = finished->downloadedPath;   // now a local path
        st.phase                  = Phase::ReadyToLaunch;
      }
      else
      {
        st.lastError = finished->error;
        st.phase     = (finished->error == "cancelled") ? Phase::UpdateReady : Phase::Failed;
      }
      break;

    default:
      // A task finishing in any other phase means the state moved on beneath it (architecture
      // §8, rule 4): discard the result rather than apply it to a state it was not computed for.
      LogSilently("discarded a stale update task result");
      break;
  }
}

void SkipAvailableVersion(UpdateState& st)
{
  if (st.phase != Phase::UpdateReady)
    return;
  st.prefs.skippedVersion = st.available.version;
  st.phase                = Phase::Idle;
}

bool LaunchInstallerAndExit(UpdateState& st)
{
  if (st.phase != Phase::ReadyToLaunch)
    return false;

  const std::wstring exe = std::filesystem::u8path(st.available.installerUrl).wstring();

  // /SILENT shows only a progress window. /CLOSEAPPLICATIONS lets Inno shut down any GoSurvey
  // still running (via the AppMutex) rather than failing on a locked file.
  //
  // /RELAUNCH=1 is what actually brings the app back, and it is not interchangeable with
  // /RESTARTAPPLICATIONS. That flag only restarts applications Restart Manager itself closed,
  // and this process exits immediately below — long before Setup performs its Restart Manager
  // scan — so nothing is ever registered for it to restart. The first live update proved it:
  // the app closed, updated, and never reopened. The installer's [Run] entry keyed on this
  // parameter does the relaunch instead, as the original (non-elevated) user.
  const std::wstring args = L"/SILENT /CLOSEAPPLICATIONS /RELAUNCH=1";

  // ShellExecuteEx rather than CreateProcess: the installer needs elevation (the install lives
  // in Program Files), and "runas" is what raises the UAC prompt. CreateProcess would simply
  // fail with ERROR_ELEVATION_REQUIRED.
  SHELLEXECUTEINFOW info{};
  info.cbSize       = sizeof(info);
  info.fMask        = SEE_MASK_NOASYNC;
  info.lpVerb       = L"runas";
  info.lpFile       = exe.c_str();
  info.lpParameters = args.c_str();
  info.nShow        = SW_SHOWNORMAL;

  if (!::ShellExecuteExW(&info))
  {
    const DWORD err = ::GetLastError();
    // ERROR_CANCELLED is the user declining the UAC prompt — an ordinary choice, not a fault,
    // so it returns to the offer rather than to an error.
    if (err == ERROR_CANCELLED)
    {
      st.phase = Phase::UpdateReady;
      return false;
    }
    st.lastError = "could not start the installer (error " + std::to_string(err) + ")";
    st.phase     = Phase::Failed;
    return false;
  }
  return true;
}

}  // namespace update
