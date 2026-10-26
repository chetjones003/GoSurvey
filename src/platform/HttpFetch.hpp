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
/// All functions are blocking and are called from a worker thread, never the UI thread. The online
/// map's tile worker (REQ-363, MapTileService) is one of them.

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
///
/// \p httpStatusOut, when non-null, receives the HTTP status (0 when no response arrived) — used by
/// the online map (REQ-363) to tell "no tile here" (404) from "could not reach the server".
bool HttpGetString(const std::string& url,
                   int               timeoutMs,
                   std::string&      out,
                   std::string&      errorOut,
                   int*              httpStatusOut = nullptr);

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

/// POSTs a JSON body to \p url and returns true on 2xx, false on any other status or network error.
///
/// When \p responseOut is non-null the response body is stored there (capped at 64 KiB, which no
/// acknowledgement should approach). A 2xx is NOT proof the far end did the work: Google Apps
/// Script answers 200 even when its handler throws, reporting the failure only in the body. So
/// the caller that cares must read the body, which is why this parameter exists.
///
/// Used by REQ-080 telemetry and REQ-425 diagnostics. Callers decide how to handle failures;
/// telemetry discards them silently.
bool HttpPostJson(const std::string& url,
                  const std::string& jsonBody,
                  int                timeoutMs,
                  std::string&       errorOut,
                  std::string*       responseOut = nullptr);

/// POSTs an arbitrary byte body to an HTTPS endpoint using the supplied content type.
/// Used by REQ-425 multipart uploads; callers bound the body before calling.
bool HttpPostBody(const std::string& url,
                  const std::string& contentType,
                  const std::string& body,
                  int                timeoutMs,
                  std::string&       errorOut,
                  std::string*       responseOut = nullptr);
