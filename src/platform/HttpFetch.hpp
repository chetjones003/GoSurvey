#pragma once

#include <functional>
#include <string>

/// HTTPS fetch and file hashing for the REQ-077/REQ-078 update check (ADR-029 (e)).
///
/// WinHTTP and BCrypt both ship with Windows, so this adds no third-party dependency — the
/// reason REQ-300's three questions resolve against libcurl for a Windows-only product.
///
/// Nothing here is unit-tested, and that is deliberate (ADR-029 (g)): these functions are a
/// thin shell over OS calls, and the logic worth pinning — version ordering and manifest
/// parsing — lives in `update/UpdateCheck.*` where it can be tested with no network.
///
/// All functions are blocking and are called from a worker thread, never the UI thread.

/// True when Windows reports an actual route to the internet (IPv4 or IPv6).
///
/// Used to skip the REQ-077 check entirely when there is nothing to reach, so a user opening
/// GoSurvey on a job site with no signal sees no dialog at all rather than waiting out a timeout.
///
/// This is a local query against the Network List Manager — no network round trip, so it is safe
/// to call on the UI thread. It is a fast NEGATIVE, not a guarantee: a captive portal or a
/// LAN-only network still reports connectivity, which is why the fetch keeps its own timeout.
bool HasInternetConnectivity();

/// GETs \p url and appends the response body to \p out.
///
/// Returns false on any failure — unresolvable host, timeout, TLS failure, or any status other
/// than 200 — with a short reason in \p errorOut. For the REQ-077 startup check that reason is
/// logged and never shown (ADR-029 (h)).
bool HttpGetString(const std::string& url,
                   int               timeoutMs,
                   std::string&      out,
                   std::string&      errorOut);

/// Downloads \p url to \p destPathUtf8, overwriting it.
///
/// \p onProgress receives (bytesReceived, bytesTotal) as the body arrives; bytesTotal is 0 when
/// the server sends no Content-Length. Returning false from it cancels the download, which is
/// how the user aborts one in progress.
///
/// On failure — including cancellation — the partial file is deleted before returning, so a
/// killed or cancelled download can never be mistaken for a complete one by a later attempt.
bool HttpDownloadFile(const std::string&                              url,
                      const std::string&                              destPathUtf8,
                      int                                             timeoutMs,
                      const std::function<bool(long long, long long)>& onProgress,
                      std::string&                                    errorOut);

/// Computes the SHA-256 of a file as lowercase hex.
///
/// This is REQ-078's integrity check. It proves the download is not corrupt or truncated; it
/// does NOT prove authenticity, because the expected hash travels in the manifest from the same
/// host over the same connection. Authenticity needs Authenticode signing (recorded debt).
bool ComputeFileSha256(const std::string& pathUtf8,
                       std::string&       hexOut,
                       std::string&       errorOut);
