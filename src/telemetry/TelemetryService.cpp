#include "TelemetryService.hpp"

#include "TelemetryPing.hpp"
#include "UserPrefs.hpp"
#include "HttpFetch.hpp"

namespace telemetry {

void TelemetryWorker(TelemetryTask*       task,
                     const std::string&   version,
                     const std::string&   channel) {
  if (!task)
    return;

  TelemetryIds ids = GetTelemetryIds();

  // Decide BEFORE minting an id. DecideEventToSend tells "install" from "active" by whether an id
  // already exists, so generating one first makes every first run look like a returning one: the
  // "install" event becomes unreachable and every launch reports as a brand-new machine.
  //
  // REQ-080 (amended, D-2026-08-23-f): a ping fires on every launch, without a daily throttle.
  std::string event = DecideEventToSend(ids.installId);

  if (ids.installId.empty()) {
    ids.installId = GenerateInstallId();
    // Preserve the install identity even when the service is unreachable. Telemetry failure must
    // not change the next run's event choice or mint a different ID for an offline installation.
    (void)UpdateTelemetryIds(ids.installId);
  }

  // Build and send the payload
  TelemetryPayload payload;
  payload.installId = ids.installId;
  payload.event = event;
  payload.version = version;
  payload.channel = channel;
  payload.os = "windows";

  std::string json = BuildTelemetryJson(payload);
  std::string errorOut;
  std::string response;

  // POST to the telemetry endpoint. Network errors are discarded silently (REQ-080).
  bool ok = HttpPostJson(TelemetryEndpoint, json, 5000, errorOut, &response);

  // A 2xx is necessary but not sufficient. The Cloudflare Worker does return honest status codes
  // (unlike the Apps Script backend before it, which answered 200 to every failure for a day
  // before anyone noticed — BUG-020), but a 200 can still come from something that is not our
  // endpoint at all: a captive portal, a corporate proxy, or a stale DNS answer. The
  // acknowledgement the Worker writes on a real insert is the only thing that proves otherwise.
  //
  // The Worker's success body is `{"ok":true,...}` and must stay formatted that way; the coupling
  // is documented at both ends (tools/telemetry-worker/README.md).
  if (ok && response.find("\"ok\":true") == std::string::npos) {
    ok       = false;
    errorOut = "endpoint accepted the POST but did not acknowledge it: " + response.substr(0, 200);
  }

  if (ok) {
    task->ok = true;
  } else {
    // A network failure is expected in offline environments and has no user action.
    task->ok = true;
  }

  task->done.store(true, std::memory_order_release);
}

std::unique_ptr<TelemetryTask> BeginTelemetryPing(const std::string& version,
                                                   const std::string& channel) {
  auto task = std::make_unique<TelemetryTask>();
  TelemetryTask* taskPtr = task.get();

  task->worker = std::thread([taskPtr, version, channel]() {
    try {
      TelemetryWorker(taskPtr, version, channel);
    } catch (...) {
      // Telemetry has no user-facing failure path, including unexpected worker errors.
      taskPtr->done.store(true, std::memory_order_release);
    }
  });

  return task;
}

void PollTelemetryTask(std::unique_ptr<TelemetryTask>& task) {
  if (!task)
    return;

  if (task->done.load(std::memory_order_acquire)) {
    task.reset();
  }
}

}  // namespace telemetry
