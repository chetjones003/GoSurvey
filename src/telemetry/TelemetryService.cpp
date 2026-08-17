#include "TelemetryService.hpp"

#include "TelemetryPing.hpp"
#include "UserPrefs.hpp"
#include "HttpFetch.hpp"

#include <iostream>

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
  std::string event = DecideEventToSend(ids.installId, ids.lastActivePingDate);
  if (event.empty()) {
    // No event to send (24h throttle still active for the active event)
    task->ok = true;
    task->done.store(true, std::memory_order_release);
    return;
  }

  if (ids.installId.empty()) {
    ids.installId = GenerateInstallId();
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

  // POST to the telemetry endpoint. Network errors are logged and swallowed (REQ-201 exception).
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
    // Persist the id together with the date, and only once the ping is acknowledged. Writing the
    // id here rather than before the POST is what makes it stable: an id was previously stored
    // only on the "install" path, so an "active" ping left nothing behind and the next launch
    // minted a fresh identity — every run would have counted as a new machine.
    //
    // Storing on success (not on attempt) also lets a first ping that never landed be retried as
    // "install" next launch instead of being silently downgraded to "active" forever. The cost is
    // a duplicate install row in the narrow case where the row is written but the reply is lost.
    std::string today = GetTodayDateString();
    UpdateTelemetryIds(ids.installId, today);
    task->ok = true;
  } else {
    // Log the error but don't fail — this is a background call with no user recourse.
    // A network timeout on a job site with no signal is normal, not an error the user should see.
    std::cerr << "[telemetry] ping failed: " << errorOut << "\n";
    task->error = errorOut;
    // Don't set task->ok = false; we treat network errors as non-fatal
    task->ok = true;
  }

  task->done.store(true, std::memory_order_release);
}

std::unique_ptr<TelemetryTask> BeginTelemetryPing(const std::string& version,
                                                   const std::string& channel) {
  auto task = std::make_unique<TelemetryTask>();
  TelemetryTask* taskPtr = task.get();

  task->worker = std::thread([taskPtr, version, channel]() {
    TelemetryWorker(taskPtr, version, channel);
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
