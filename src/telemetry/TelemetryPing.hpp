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
/// The previous Google Apps Script backend is retired but its deployment still exists, so
/// rolling back is this line plus a rebuild. Its setup guide was deleted once Cloudflare
/// superseded it and lives in git history if ever needed.
constexpr const char* TelemetryEndpoint = "https://gosurvey-telemetry.gosurvey.workers.dev/v1/ping";

struct TelemetryPayload {
  std::string installId;
  std::string event;  // "install" or "active"
  std::string version;
  std::string channel;  // "stable" or "beta"
  std::string os;  // "windows" on all platforms we support
  /// REQ-080 (amended 2026-08-23, D-2026-08-23-e): the REQ-091 signed-in email at the moment
  /// this ping fires, or empty if signed out. This is the one deliberate, user-directed
  /// exception to "no PII" — everything else about this payload is unchanged and still fires
  /// regardless of sign-in state.
  std::string email;
};

/// Build the telemetry payload as a JSON string.
/// Returns: {"installId":"...","event":"install|active",...,"email":"..."}
std::string BuildTelemetryJson(const TelemetryPayload& payload);

/// Generate a random 128-bit hex install ID (32 hex characters).
std::string GenerateInstallId();

/// Decide which event to send. Returns "install" if no installId exists yet (first run ever);
/// "active" otherwise.
///
/// REQ-080 (amended 2026-08-23, D-2026-08-23-f): a ping fires on every launch, with no rolling
/// throttle — this used to also take a `lastActivePingDate` and return "active" only if 24h had
/// elapsed, or an empty string (send nothing) otherwise. That throttle is gone by explicit user
/// decision: a ping now always fires, once per launch, for as long as the application runs. The
/// server enforces no per-day dedup either (`ux_pings_active_daily` was dropped) — see
/// tools/telemetry-worker/schema.sql.
std::string DecideEventToSend(const std::string& installId);
