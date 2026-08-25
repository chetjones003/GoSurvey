#pragma once

#include <string>
#include <vector>

/// Pure sign-in logic for REQ-091 (ADR-037): PKCE construction, the Auth0 authorize URL, and the
/// silent-refresh-vs-interactive decision. No network, no window — same split as
/// `telemetry/TelemetryPing.hpp` (ADR-032 (b)). The BCrypt SHA-256 call PKCE needs lives in
/// `platform/HttpFetch` and is called by `AuthService`, not here, for the same reason
/// `TelemetryPing` never calls `HttpPostJson`: this module stays testable without touching an OS
/// API.

/// Generates a PKCE code verifier: a random string from RFC 7636's unreserved character set
/// (`A-Z a-z 0-9 - . _ ~`), 64 characters long (within the spec's 43-128 range).
std::string GeneratePkceCodeVerifier();

/// Generates an opaque random state value for CSRF protection on the authorize request.
/// 32 hex characters, same shape as `GenerateInstallId` (ADR-032), independently generated here
/// because two present-day call sites do not justify sharing a random-hex helper (REQ-301).
std::string GenerateOAuthState();

/// Base64url-encodes \p data with no padding, per RFC 7636's code_challenge encoding.
std::string Base64UrlEncode(const std::vector<unsigned char>& data);

/// Base64url-decodes \p encoded (padding optional, matching what real ID tokens contain — Auth0
/// omits it, but a decoder that requires it would break on emitters that don't). Returns an empty
/// vector on malformed input; there is no partial-decode result to salvage.
std::vector<unsigned char> Base64UrlDecode(const std::string& encoded);

/// Percent-encodes everything except RFC 3986 unreserved characters, for use as a single query
/// parameter VALUE (not a whole URL) — e.g. a `:` or `/` inside a redirect_uri or email address
/// gets encoded here. Used by both `BuildAuthorizeUrl` and `AuthService`'s license-lookup query
/// string (two present-day call sites, REQ-301).
std::string UrlEncodeQueryComponent(const std::string& value);

/// Builds the Auth0 `/authorize` URL for the native-app PKCE + loopback-redirect flow (ADR-037 (b)).
/// \p codeChallenge is the base64url(SHA256(verifier)) — computed by the caller (AuthService), not
/// here. \p redirectUri is the loopback URL for the port `OAuthListener` bound. \p audience must
/// match the accounts-worker's registered Auth0 API identifier (REQ-092) — without it Auth0 issues
/// an opaque access token instead of a verifiable JWT, and the accounts-worker cannot check it.
std::string BuildAuthorizeUrl(const std::string& auth0Domain,
                              const std::string& clientId,
                              const std::string& redirectUri,
                              const std::string& codeChallenge,
                              const std::string& state,
                              const std::string& audience);

/// Decides whether the next launch can renew the session silently or must show the browser again.
/// Returns "silent-refresh" when a refresh token is stored and not known-expired; "interactive"
/// when there is no stored token, or the stored one is expired/revoked.
std::string DecideAuthAction(bool hasRefreshToken, bool refreshTokenExpired);
