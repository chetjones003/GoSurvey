#pragma once

#include <string>

/// The telemetry endpoint: a Cloudflare Worker backed by D1.
///
/// Source and deployment live in `tools/telemetry-worker/`; operator setup is
/// `docs/cloudflare-telemetry-setup.md`. The path is `/v1/ping` — the Worker 404s anything else.
///
/// Compile-time by design (ADR-032 (f)): a settings reset cannot disable telemetry and a user
/// preference cannot redirect it. The corollary is that moving the endpoint — including putting
/// it behind a custom domain instead of `*.workers.dev` — is a rebuild and a re-release, so the
/// hostname wants settling before the first ship, not after.
///
/// The previous Google Apps Script backend is retired but intact; rolling back is this line plus
/// a rebuild (see `docs/google-sheets-setup.md`).
constexpr const char* TelemetryEndpoint = "https://gosurvey-telemetry.gosurvey.workers.dev/v1/ping";

struct TelemetryPayload {
  std::string installId;
  std::string event;  // "install" or "active"
  std::string version;
  std::string channel;  // "stable" or "beta"
  std::string os;  // "windows" on all platforms we support
};

/// Build the telemetry payload as a JSON string.
/// Returns: {"installId":"...","event":"install|active",...}
std::string BuildTelemetryJson(const TelemetryPayload& payload);

/// Generate a random 128-bit hex install ID (32 hex characters).
std::string GenerateInstallId();

/// Decide which event to send and whether to send at all.
/// Returns "install" if no installId exists; "active" if 24h have elapsed since lastActivePingDate;
/// empty string if neither condition is met (do not send).
std::string DecideEventToSend(const std::string& installId, const std::string& lastActivePingDate);

/// Get today's date as YYYY-MM-DD.
std::string GetTodayDateString();

/// Returns true if newDate is at least 24 hours after oldDate (both formatted as YYYY-MM-DD).
bool IsAfter24Hours(const std::string& oldDate, const std::string& newDate);
