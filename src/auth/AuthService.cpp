#include "AuthService.hpp"

#include "AuthConfig.hpp"
#include "AuthPing.hpp"
#include "../platform/CredentialStore.hpp"
#include "../platform/HttpFetch.hpp"
#include "../platform/OAuthListener.hpp"

#include <nlohmann/json.hpp>

#include <iostream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

namespace auth {

namespace {

std::string TokenEndpointUrl() {
  return std::string("https://") + kAuth0Domain + "/oauth/token";
}

bool PostToTokenEndpoint(const nlohmann::json& body, nlohmann::json& tokenResponseOut,
                         std::string& errorOut) {
  std::string response;
  // HttpPostJson already reports "HTTP <status>" for a non-2xx; Auth0 also returns a JSON error
  // body on failure, which is more useful than the bare status, so the caller reads `response`
  // regardless of the boolean result rather than only on success.
  const bool posted = HttpPostJson(TokenEndpointUrl(), body.dump(), 10000, errorOut, &response);
  if (!response.empty()) {
    try {
      tokenResponseOut = nlohmann::json::parse(response);
      if (!posted && tokenResponseOut.contains("error_description") &&
          tokenResponseOut["error_description"].is_string()) {
        errorOut = tokenResponseOut["error_description"].get<std::string>();
      }
    } catch (const std::exception&) {
      if (posted) {
        errorOut = "token endpoint returned malformed JSON";
      }
      return false;
    }
  }
  return posted;
}

std::string ExtractEmailFromIdToken(const std::string& idToken) {
  const size_t firstDot = idToken.find('.');
  if (firstDot == std::string::npos) return "";
  const size_t secondDot = idToken.find('.', firstDot + 1);
  if (secondDot == std::string::npos) return "";

  const std::vector<unsigned char> payloadBytes =
      Base64UrlDecode(idToken.substr(firstDot + 1, secondDot - firstDot - 1));
  if (payloadBytes.empty()) return "";

  try {
    const nlohmann::json payload =
        nlohmann::json::parse(std::string(payloadBytes.begin(), payloadBytes.end()));
    if (payload.contains("email") && payload["email"].is_string()) {
      return payload["email"].get<std::string>();
    }
  } catch (const std::exception&) {
    // Malformed/unexpected id_token payload — email stays empty. This is a display nicety, not
    // the security boundary (the accounts-worker verifies the JWT server-side, REQ-092), so it
    // does not fail sign-in.
  }
  return "";
}

/// Applies a successful token-exchange result: persists the refresh token (if the response
/// carried one — Auth0 may rotate it on refresh) and records the display email. This is the only
/// place a refresh token is written to CredentialStore, whether from interactive sign-in or a
/// rotating refresh.
///
/// Requires `access_token` to be present before reporting success. A 2xx with no usable token
/// data would otherwise report `task->ok = true` with nothing actually obtained — a silent
/// failure disguised as a success (REQ-201) — so this is checked explicitly rather than assumed
/// from the HTTP status alone, the same reasoning `TelemetryService` applies to `"ok":true`.
/// REQ-092: calls the accounts-worker's license lookup with the just-obtained access token.
/// Best-effort — any failure (network, non-200, malformed body) returns an empty tier rather than
/// propagating an error, since a signed-in user with no known tier is a valid, expected state
/// (the endpoint may be unreachable, or briefly down) and nothing currently depends on this value.
///
/// \p email, when non-empty, is passed as a query parameter so the Worker can keep the `users`
/// row's email current. This is the app's own already-authenticated user restating their own
/// email for display purposes — not a new trust boundary: `auth0_sub` (from the verified JWT)
/// remains the only thing the Worker keys or authorizes anything by; email is stored, never
/// checked.
std::string FetchLicenseTier(const std::string& accessToken, const std::string& email) {
  std::string body, error;
  std::string url = std::string(kAccountsApiBaseUrl) + "/v1/license";
  if (!email.empty())
    url += "?email=" + UrlEncodeQueryComponent(email);
  if (!HttpGetString(url, 5000, body, error, accessToken)) {
    std::cerr << "[auth] license lookup failed: " << error << "\n";
    return "";
  }

  try {
    const nlohmann::json parsed = nlohmann::json::parse(body);
    if (parsed.contains("tier") && parsed["tier"].is_string())
      return parsed["tier"].get<std::string>();
  } catch (const std::exception&) {
    std::cerr << "[auth] license lookup returned malformed JSON\n";
  }
  return "";
}

void ApplyTokenResponse(const nlohmann::json& tokenResponse, AuthTask* task) {
  if (!tokenResponse.contains("access_token") || !tokenResponse["access_token"].is_string()) {
    task->error = "token endpoint response did not contain an access token";
    return;
  }

  task->email = (tokenResponse.contains("id_token") && tokenResponse["id_token"].is_string())
                    ? ExtractEmailFromIdToken(tokenResponse["id_token"].get<std::string>())
                    : "";

  if (tokenResponse.contains("refresh_token") && tokenResponse["refresh_token"].is_string()) {
    std::string storeError;
    if (!StoreRefreshToken(tokenResponse["refresh_token"].get<std::string>(), storeError)) {
      task->error = "signed in, but could not store the session securely: " + storeError;
      return;
    }
  }
  task->ok = true;

  // REQ-092: best-effort license-tier lookup. Deliberately does not affect `ok` — a failed
  // lookup (network hiccup, Worker briefly down) must not undo a sign-in that already succeeded;
  // REQ-091's identity and REQ-092's tier are separate concerns by design (ADR-037).
  task->tier = FetchLicenseTier(tokenResponse["access_token"].get<std::string>(), task->email);
}

}  // namespace

std::unique_ptr<AuthTask> BeginInteractiveSignIn() {
  auto      task    = std::make_unique<AuthTask>();
  AuthTask* taskPtr = task.get();

  task->worker = std::thread([taskPtr]() {
    // Auth0 requires each callback URL registered exactly, port included (ADR-037 (b), amended
    // 2026-08-23) — an ephemeral port cannot be used, so a short fixed list is tried in order
    // until one is free on this machine.
    OAuthListener listener;
    std::string   listenerError;
    bool          bound = false;
    for (const int candidatePort : kOAuthCallbackPorts) {
      if (listener.Start(candidatePort, listenerError)) {
        bound = true;
        break;
      }
    }
    if (!bound) {
      taskPtr->error = "could not start the local sign-in listener on any candidate port: " +
                       listenerError;
      taskPtr->done.store(true, std::memory_order_release);
      return;
    }

    const std::string           verifier = GeneratePkceCodeVerifier();
    std::vector<unsigned char>  digest;
    std::string                 shaError;
    if (!ComputeSha256Bytes(std::vector<unsigned char>(verifier.begin(), verifier.end()), digest,
                            shaError)) {
      taskPtr->error = "could not prepare the sign-in request: " + shaError;
      taskPtr->done.store(true, std::memory_order_release);
      return;
    }
    const std::string codeChallenge = Base64UrlEncode(digest);
    const std::string state         = GenerateOAuthState();
    const std::string redirectUri =
        "http://127.0.0.1:" + std::to_string(listener.Port()) + "/callback";
    const std::string authorizeUrl = BuildAuthorizeUrl(
        kAuth0Domain, kAuth0ClientId, redirectUri, codeChallenge, state, kAuth0Audience);

    ::ShellExecuteA(nullptr, "open", authorizeUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    // Two minutes: long enough for a real sign-in (including a fresh Google/Microsoft login),
    // short enough that an abandoned browser tab doesn't leave this worker thread parked
    // forever.
    constexpr int kCallbackTimeoutMs = 120000;
    std::string   code, returnedState, errorParam, listenError;
    if (!listener.AwaitCallback(kCallbackTimeoutMs, code, returnedState, errorParam,
                                listenError)) {
      taskPtr->error = listenError;
      taskPtr->done.store(true, std::memory_order_release);
      return;
    }
    if (returnedState != state) {
      taskPtr->error = "sign-in response did not match the request (possible tampering); please retry";
      taskPtr->done.store(true, std::memory_order_release);
      return;
    }
    if (!errorParam.empty()) {
      taskPtr->error = "sign-in was not completed: " + errorParam;
      taskPtr->done.store(true, std::memory_order_release);
      return;
    }
    if (code.empty()) {
      taskPtr->error = "no authorization code was received";
      taskPtr->done.store(true, std::memory_order_release);
      return;
    }

    nlohmann::json body;
    body["grant_type"]    = "authorization_code";
    body["client_id"]     = kAuth0ClientId;
    body["code"]          = code;
    body["redirect_uri"]  = redirectUri;
    body["code_verifier"] = verifier;

    nlohmann::json tokenResponse;
    std::string    exchangeError;
    if (!PostToTokenEndpoint(body, tokenResponse, exchangeError)) {
      taskPtr->error = "token exchange failed: " + exchangeError;
      taskPtr->done.store(true, std::memory_order_release);
      return;
    }

    ApplyTokenResponse(tokenResponse, taskPtr);
    taskPtr->done.store(true, std::memory_order_release);
  });

  return task;
}

std::unique_ptr<AuthTask> BeginSilentRefresh() {
  auto      task    = std::make_unique<AuthTask>();
  AuthTask* taskPtr = task.get();

  // REQ-091's launch gate (DrawSignInGate) must not block a surveyor with no signal — same
  // reasoning and same check as REQ-077's update gate (UpdateService::BeginStartupCheck). Checked
  // here, before spawning a thread, because HasInternetConnectivity() is a local, non-blocking
  // query (its own doc comment: "safe to call on the UI thread").
  if (!HasInternetConnectivity()) {
    taskPtr->skippedNoNetwork = true;
    taskPtr->done.store(true, std::memory_order_release);
    return task;
  }

  task->worker = std::thread([taskPtr]() {
    std::string storedRefreshToken;
    if (!LoadRefreshToken(storedRefreshToken)) {
      taskPtr->error = "no stored session";
      taskPtr->done.store(true, std::memory_order_release);
      return;
    }

    nlohmann::json body;
    body["grant_type"]    = "refresh_token";
    body["client_id"]     = kAuth0ClientId;
    body["refresh_token"] = storedRefreshToken;

    nlohmann::json tokenResponse;
    std::string    refreshError;
    if (!PostToTokenEndpoint(body, tokenResponse, refreshError)) {
      // An expired or revoked refresh token lands here as a rejected grant from Auth0 — REQ-091's
      // "forces interactive sign-in" acceptance condition, not a bug to retry.
      taskPtr->error = "session expired: " + refreshError;
      taskPtr->done.store(true, std::memory_order_release);
      return;
    }

    ApplyTokenResponse(tokenResponse, taskPtr);
    taskPtr->done.store(true, std::memory_order_release);
  });

  return task;
}

void PollAuthTask(std::unique_ptr<AuthTask>& task) {
  if (!task)
    return;
  if (task->done.load(std::memory_order_acquire)) {
    task.reset();
  }
}

void SignOut() {
  DeleteRefreshToken();
}

}  // namespace auth
