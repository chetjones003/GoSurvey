#pragma once

#include "UpdateCheck.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

/// REQ-077 / REQ-078 orchestration: the background check, the download, and the handoff to the
/// installer. Follows the one-shot worker pattern in architecture §8 — inputs copied, task state
/// heap-allocated, a release store to `done` polled once per frame, cooperative cancellation.
///
/// This owns no drawing state and never touches `AppCommandState` from a worker.
namespace update {

/// Where the flow currently is. The UI reads this each frame; nothing else drives the dialog.
enum class Phase {
  Idle,          ///< nothing running, nothing to show
  Checking,      ///< manifest fetch in flight (silent — the user is not told this is happening)
  UpdateReady,   ///< a newer version exists; the REQ-078 dialog is showing
  /// User chose to install; the installer is coming down AND being hashed. Hashing is not its
  /// own phase because it happens in the same worker and takes milliseconds on a ~5 MB file —
  /// a separate state would only ever flicker.
  Downloading,
  ReadyToLaunch, ///< downloaded and hash-verified; awaiting the unsaved-changes check
  Failed,        ///< user-initiated step failed; the reason IS shown (REQ-201)
};

/// Async state for one check-or-download. Heap-allocated so its atomics don't make the owner
/// non-copyable (architecture §8, rule 2).
struct UpdateTask {
  std::thread       worker;
  std::atomic<bool> done{false};
  std::atomic<bool> cancel{false};
  std::atomic<long long> bytesReceived{0};
  std::atomic<long long> bytesTotal{0};

  // Written by the worker before its release store to `done`; read by the UI thread after.
  bool        ok = false;
  std::string error;
  Manifest    manifest;
  std::string downloadedPath;

  ~UpdateTask()
  {
    cancel.store(true, std::memory_order_relaxed);
    if (worker.joinable())
      worker.join();
  }
};

/// The whole updater's UI-thread-owned state.
struct UpdateState {
  Phase                       phase = Phase::Idle;
  UpdatePrefs                 prefs;
  Manifest                    available;     ///< valid from UpdateReady onward
  std::string                 runningVersion;
  std::string                 lastError;     ///< shown only in Phase::Failed
  std::unique_ptr<UpdateTask> task;

  /// Set when the user confirms the update but the drawing has unsaved changes, so the
  /// unsaved-changes modal must run before the application may exit (REQ-078).
  bool awaitingUnsavedCheck = false;
};

/// Starts the REQ-077 background check if it is due: enabled, not already running, and outside
/// the 24-hour throttle window. Returns immediately, always. Does nothing at all when disabled.
///
/// \p nowUnix is passed in rather than read here so the throttle is decided by the caller's
/// clock and can be reasoned about in one place.
void BeginStartupCheck(UpdateState& st, const std::string& ownerRepo, long long nowUnix);

/// Polls the in-flight task and advances `phase`. Call once per frame from the UI thread.
///
/// A failed CHECK is silent (ADR-029 (h)): it returns to Idle and logs. A failed DOWNLOAD or
/// hash is not — it was user-initiated, so it lands in Phase::Failed with a reason.
void PollUpdateTask(UpdateState& st);

/// User pressed "Update Now": starts the download + verify worker.
void BeginDownload(UpdateState& st);

/// User pressed "Skip this version": suppresses this exact version permanently.
void SkipAvailableVersion(UpdateState& st);

/// Hands the verified installer to Inno Setup and returns true if it started, in which case the
/// caller must exit the application so the files can be replaced (ADR-029 (f)).
///
/// The caller is responsible for having cleared unsaved work first — this function does not and
/// cannot know about drawings.
bool LaunchInstallerAndExit(UpdateState& st);

/// Directory the installer is downloaded into: %LOCALAPPDATA%\GoSurvey\updates.
std::string UpdateDownloadDir();

}  // namespace update
