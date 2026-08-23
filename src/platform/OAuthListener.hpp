#pragma once

#include <string>

/// A one-shot loopback HTTP listener for the native-app OAuth redirect (REQ-091, ADR-037 (b),
/// amended 2026-08-23).
///
/// RFC 8252: a native app cannot receive a browser redirect directly, so it binds a port on
/// 127.0.0.1, sends that as its redirect_uri, and catches the single resulting request itself.
/// RFC 8252 recommends an OS-assigned ephemeral port, but **Auth0's Allowed Callback URLs field
/// rejects a wildcard in the port position** — its placeholder support is documented as
/// "subdomain or domain name only" — so an ephemeral port cannot be pre-registered there at all.
/// This binds to a caller-supplied FIXED port instead; `AuthService` tries a short list of
/// candidates (`kOAuthCallbackPorts` in `AuthConfig.hpp`) until one is free, and every candidate
/// must be registered as its own exact Allowed Callback URL in the Auth0 dashboard.
///
/// This is not a general-purpose server — one instance serves exactly one sign-in attempt,
/// blocking on a worker thread (never the UI thread), same contract as the rest of `platform/`.
class OAuthListener {
public:
  OAuthListener();
  ~OAuthListener();

  OAuthListener(const OAuthListener&) = delete;
  OAuthListener& operator=(const OAuthListener&) = delete;

  /// Binds \p port on 127.0.0.1 and starts listening. Returns false (typically because the port
  /// is already in use by something else) with a reason on any socket error — the caller is
  /// expected to try the next candidate port in that case, not to treat it as fatal.
  bool Start(int port, std::string& errorOut);

  /// The port bound by Start(). Only valid after Start() returns true.
  int Port() const { return port_; }

  /// Blocks up to \p timeoutMs for the single browser redirect, parses `code`, `state`, and
  /// `error` off the callback's query string, writes a static "you can close this tab" HTML
  /// response, and closes the connection. Returns false on timeout or any socket/parse error, with
  /// a reason in \p errorOut; \p errorQueryParam carries Auth0's `error` value when present (e.g.
  /// the user cancelled sign-in), distinct from a transport failure.
  bool AwaitCallback(int          timeoutMs,
                     std::string& codeOut,
                     std::string& stateOut,
                     std::string& errorQueryParam,
                     std::string& errorOut);

private:
  void* listenSocket_;  // SOCKET, opaque here so this header stays winsock2.h-free
  int   port_;
  bool  wsaInitialized_;
};
