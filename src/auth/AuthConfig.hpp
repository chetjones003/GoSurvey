#pragma once

#include <array>

/// Compile-time Auth0 tenant configuration (REQ-091, ADR-037).
///
/// Same reasoning as `telemetry/TelemetryPing.hpp`'s `TelemetryEndpoint` (ADR-032 (f)): fixed at
/// build time so a settings reset or a user preference cannot redirect where sign-in sends
/// credentials. Moving tenants is a rebuild, not a runtime setting.
///
/// ASSUMPTION-1 (TASK-090) resolved 2026-08-23: tenant provisioned by the operator, per
/// docs/auth0-setup.md — a Native application ("GoSurvey"), Google + Microsoft social connections,
/// the default Database connection, and the three fixed loopback callback URLs from
/// `kOAuthCallbackPorts` below registered under Allowed Callback URLs.
///
/// No client secret constant exists here on purpose: this is a Native (public) client using PKCE,
/// which authenticates the user, not the app binary — a secret embedded in a shipped .exe could
/// be extracted and would not actually be secret, so Auth0 does not expect one for this flow.
constexpr const char* kAuth0Domain   = "dev-hqk2tndprbftps0k.us.auth0.com";
constexpr const char* kAuth0ClientId = "HnID6lI88LRM6c9UEObeG8DB11k9iXgX";

/// Must match the accounts-worker's AUTH0_AUDIENCE var (tools/accounts-worker/wrangler.toml) and
/// the identifier of the Auth0 API registered for it (docs/auth0-setup.md). Without this in the
/// /authorize request, Auth0 issues an opaque access token instead of a verifiable JWT, and
/// REQ-092's license lookup cannot check it.
constexpr const char* kAuth0Audience = "https://gosurvey-accounts/";

/// Fixed candidate ports for the loopback OAuth listener (ADR-037 (b), amended 2026-08-23).
///
/// RFC 8252 recommends an OS-assigned ephemeral port, but Auth0's Allowed Callback URLs field
/// only supports a wildcard in the subdomain/domain, never the port — an ephemeral port cannot be
/// pre-registered there. `AuthService::BeginInteractiveSignIn` tries these in order and uses the
/// first one it can bind, so a single port already in use by something else on the user's machine
/// does not block sign-in outright.
///
/// EVERY port here must be registered as its own exact entry in Auth0's Allowed Callback URLs
/// (docs/auth0-setup.md Step 1.3) — e.g. `http://127.0.0.1:53682/callback`,
/// `http://127.0.0.1:53683/callback`, `http://127.0.0.1:53684/callback`. Adding a port here
/// without registering it in Auth0 makes that candidate always fail at the browser (Auth0 refuses
/// the redirect_uri), not at bind() — the two lists must be kept in sync by hand.
constexpr std::array<int, 3> kOAuthCallbackPorts = {53682, 53683, 53684};

/// Base URL of the deployed accounts-worker (REQ-092, ADR-037 (e)). Compile-time, same reasoning
/// as TelemetryEndpoint/kAuth0Domain: a settings reset cannot redirect where the license lookup
/// goes. The path is `/v1/license` — see tools/accounts-worker/src/index.js.
constexpr const char* kAccountsApiBaseUrl = "https://gosurvey-accounts.gosurvey.workers.dev";
