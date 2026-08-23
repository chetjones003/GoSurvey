#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace auth {

struct AuthTask {
  std::thread       worker;
  std::atomic<bool> done{false};
  std::atomic<bool> cancel{false};  // present for symmetry with TelemetryTask; not polled inside
                                    // the blocking listener wait, same as telemetry's own cancel
                                    // field today — see ~AuthTask.

  // Written by worker before release store to `done`; read by UI thread after.
  bool        ok = false;
  std::string email;   // populated on success, decoded from the ID token for display only
  std::string error;   // populated on failure; unlike telemetry this IS shown to the user, since
                       // sign-in is an interactive action with a UI waiting on the result
  bool        skippedNoNetwork = false;  // BeginSilentRefresh only: set when there was no route
                                        // to the internet at all, so the attempt was skipped
                                        // rather than tried and failed — REQ-091's launch gate
                                        // treats this the same as success (see DrawSignInGate)
  std::string tier;    // REQ-092: best-effort license-tier lookup after a successful sign-in.
                       // Empty if the lookup failed or hasn't been made — this never affects
                       // `ok`, since REQ-092 is a separate, deferred concern from REQ-091's
                       // identity (no feature reads this yet).

  ~AuthTask() {
    cancel.store(true, std::memory_order_relaxed);
    if (worker.joinable())
      worker.join();
  }
};

/// Starts the interactive sign-in flow (REQ-091, ADR-037 (b)): opens the system browser to Auth0
/// Universal Login via the native-app PKCE + loopback-redirect flow, waits for the browser
/// redirect, exchanges the code for tokens, and stores the refresh token via CredentialStore.
/// Returns immediately; poll with PollAuthTask.
std::unique_ptr<AuthTask> BeginInteractiveSignIn();

/// Attempts to renew the session from a previously stored refresh token, with no browser popup.
/// Returns immediately; poll with PollAuthTask. On failure (no stored token, or it is
/// expired/revoked), the caller falls back to BeginInteractiveSignIn per REQ-091's acceptance
/// condition.
std::unique_ptr<AuthTask> BeginSilentRefresh();

/// Polls the in-flight task and cleans it up when done. Call once per frame from the UI thread.
void PollAuthTask(std::unique_ptr<AuthTask>& task);

/// Deletes the stored refresh token — the sign-out action. Interactive sign-in starts fresh next
/// time; a subsequent silent-refresh attempt correctly fails with no token stored.
void SignOut();

}  // namespace auth
