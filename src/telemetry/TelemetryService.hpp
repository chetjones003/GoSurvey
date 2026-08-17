#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace telemetry {

struct TelemetryTask {
  std::thread       worker;
  std::atomic<bool> done{false};
  std::atomic<bool> cancel{false};

  // Written by worker before release store to `done`; read by UI thread after.
  bool        ok = false;
  std::string error;

  ~TelemetryTask() {
    cancel.store(true, std::memory_order_relaxed);
    if (worker.joinable())
      worker.join();
  }
};

/// Starts a fire-and-forget telemetry ping in a background worker thread (REQ-080, ADR-032).
/// Returns immediately. The ping checks for updates to installId and lastActivePingDate,
/// builds the JSON payload, and POSTs it to the configured endpoint.
///
/// Any network error is logged at trace level and swallowed (REQ-201 exception, like REQ-077).
/// This must never fail the application or add latency to the session.
///
/// Safe to call from the UI thread; the worker touches no drawing state.
std::unique_ptr<TelemetryTask> BeginTelemetryPing(
    const std::string& version,
    const std::string& channel  // "stable" or "beta"
);

/// Polls the in-flight ping task and cleans it up when done. Call once per frame from UI thread.
/// This is mostly for symmetry with UpdateService; the telemetry ping never blocks or needs
/// user interaction, so it's a simpler pattern than the update check.
void PollTelemetryTask(std::unique_ptr<TelemetryTask>& task);

}  // namespace telemetry
