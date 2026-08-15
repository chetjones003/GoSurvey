#include "HttpFetch.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <bcrypt.h>
#include <winhttp.h>

#include <cstdio>
#include <filesystem>
#include <vector>

namespace {

/// The User-Agent GitHub sees. A recognisable agent makes traffic from this app identifiable in
/// logs, and some endpoints reject an empty one outright.
constexpr const wchar_t* kUserAgent = L"GoSurvey-Updater/1.0";

std::wstring Widen(const std::string& utf8)
{
  if (utf8.empty())
    return std::wstring();
  const int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                                         nullptr, 0);
  if (need <= 0)
    return std::wstring();
  std::wstring out(static_cast<size_t>(need), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), need);
  return out;
}

std::string LastErrorText(const char* what)
{
  return std::string(what) + " failed (GetLastError=" + std::to_string(::GetLastError()) + ")";
}

/// Owns the three WinHTTP handles so every early return closes them in the right order.
/// One visible owner per resource (architecture §11.5); the alternative here is a goto chain
/// that has to be correct at eight exit points.
struct WinHttpSession {
  HINTERNET session = nullptr;
  HINTERNET connect = nullptr;
  HINTERNET request = nullptr;

  ~WinHttpSession()
  {
    if (request) ::WinHttpCloseHandle(request);
    if (connect) ::WinHttpCloseHandle(connect);
    if (session) ::WinHttpCloseHandle(session);
  }

  WinHttpSession() = default;
  WinHttpSession(const WinHttpSession&) = delete;
  WinHttpSession& operator=(const WinHttpSession&) = delete;
};

/// Opens \p url and sends the request, leaving \p s.request ready to read from.
/// Returns false with a reason on any failure, including a non-200 status.
bool OpenRequest(const std::string& url, int timeoutMs, WinHttpSession& s, std::string& errorOut)
{
  const std::wstring wideUrl = Widen(url);
  if (wideUrl.empty())
  {
    errorOut = "url is empty or not valid UTF-8";
    return false;
  }

  URL_COMPONENTS parts{};
  parts.dwStructSize     = sizeof(parts);
  wchar_t hostName[256]  = {};
  wchar_t urlPath[2048]  = {};
  parts.lpszHostName     = hostName;
  parts.dwHostNameLength = static_cast<DWORD>(std::size(hostName));
  parts.lpszUrlPath      = urlPath;
  parts.dwUrlPathLength  = static_cast<DWORD>(std::size(urlPath));

  if (!::WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts))
  {
    errorOut = LastErrorText("WinHttpCrackUrl");
    return false;
  }
  if (parts.nScheme != INTERNET_SCHEME_HTTPS)
  {
    // Refused rather than downgraded: the manifest and the installer are the two things this
    // program will execute, and TLS is the only thing authenticating either of them.
    errorOut = "refusing a non-HTTPS url";
    return false;
  }

  s.session = ::WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s.session)
  {
    errorOut = LastErrorText("WinHttpOpen");
    return false;
  }

  // Every phase gets the same budget, so a stalled connect cannot outlast the whole timeout.
  ::WinHttpSetTimeouts(s.session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

  s.connect = ::WinHttpConnect(s.session, hostName, parts.nPort, 0);
  if (!s.connect)
  {
    errorOut = LastErrorText("WinHttpConnect");
    return false;
  }

  s.request = ::WinHttpOpenRequest(s.connect, L"GET", urlPath, nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!s.request)
  {
    errorOut = LastErrorText("WinHttpOpenRequest");
    return false;
  }

  // GitHub redirects a release asset URL to its CDN host. WinHTTP follows redirects by default
  // and refuses an https->http downgrade, which is the behaviour we want; it is set explicitly
  // so that a future change to the default cannot silently weaken it.
  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  ::WinHttpSetOption(s.request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                     sizeof(redirectPolicy));

  if (!::WinHttpSendRequest(s.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                            0, 0, 0))
  {
    errorOut = LastErrorText("WinHttpSendRequest");
    return false;
  }
  if (!::WinHttpReceiveResponse(s.request, nullptr))
  {
    errorOut = LastErrorText("WinHttpReceiveResponse");
    return false;
  }

  DWORD status     = 0;
  DWORD statusSize = sizeof(status);
  if (!::WinHttpQueryHeaders(s.request,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX))
  {
    errorOut = LastErrorText("WinHttpQueryHeaders");
    return false;
  }
  if (status != 200)
  {
    // 404 is the ordinary case for a channel that has never published, so it is a plain
    // reported failure rather than anything exceptional.
    errorOut = "server returned HTTP " + std::to_string(status);
    return false;
  }
  return true;
}

long long QueryContentLength(HINTERNET request)
{
  DWORD length     = 0;
  DWORD lengthSize = sizeof(length);
  if (::WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &length, &lengthSize,
                            WINHTTP_NO_HEADER_INDEX))
    return static_cast<long long>(length);
  return 0;   // chunked or absent; callers treat 0 as "unknown", not "empty"
}

}  // namespace

bool HttpGetString(const std::string& url, int timeoutMs, std::string& out, std::string& errorOut)
{
  out.clear();
  errorOut.clear();

  WinHttpSession s;
  if (!OpenRequest(url, timeoutMs, s, errorOut))
    return false;

  // A manifest is a few hundred bytes. The cap stops a wrong or hostile URL from being read
  // into memory forever; it is generous enough that a legitimate manifest cannot reach it.
  constexpr size_t kMaxBytes = 1u << 20;   // 1 MiB

  std::vector<char> buffer(16384);
  for (;;)
  {
    DWORD available = 0;
    if (!::WinHttpQueryDataAvailable(s.request, &available))
    {
      errorOut = LastErrorText("WinHttpQueryDataAvailable");
      out.clear();
      return false;
    }
    if (available == 0)
      break;

    const DWORD want = static_cast<DWORD>(
        available < buffer.size() ? available : static_cast<DWORD>(buffer.size()));
    DWORD read = 0;
    if (!::WinHttpReadData(s.request, buffer.data(), want, &read))
    {
      errorOut = LastErrorText("WinHttpReadData");
      out.clear();
      return false;
    }
    if (read == 0)
      break;

    if (out.size() + read > kMaxBytes)
    {
      errorOut = "response exceeded the 1 MiB manifest cap";
      out.clear();
      return false;
    }
    out.append(buffer.data(), read);
  }
  return true;
}

bool HttpDownloadFile(const std::string&                              url,
                      const std::string&                              destPathUtf8,
                      int                                             timeoutMs,
                      const std::function<bool(long long, long long)>& onProgress,
                      std::string&                                    errorOut)
{
  errorOut.clear();

  WinHttpSession s;
  if (!OpenRequest(url, timeoutMs, s, errorOut))
    return false;

  const std::filesystem::path destPath = std::filesystem::u8path(destPathUtf8);
  std::error_code             ec;
  std::filesystem::create_directories(destPath.parent_path(), ec);

  // Any failure below must leave nothing behind that a later run could mistake for a complete
  // download (REQ-078). Closing before removing matters: Windows will not delete an open file.
  auto abandonPartialFile = [&destPath](std::FILE* f) {
    if (f)
      std::fclose(f);
    std::error_code removeEc;
    std::filesystem::remove(destPath, removeEc);
  };

  std::FILE* out = nullptr;
  if (::_wfopen_s(&out, destPath.wstring().c_str(), L"wb") != 0 || !out)
  {
    errorOut = "could not open " + destPathUtf8 + " for writing";
    return false;
  }

  const long long total    = QueryContentLength(s.request);
  long long       received = 0;

  std::vector<char> buffer(64 * 1024);
  for (;;)
  {
    DWORD available = 0;
    if (!::WinHttpQueryDataAvailable(s.request, &available))
    {
      errorOut = LastErrorText("WinHttpQueryDataAvailable");
      abandonPartialFile(out);
      return false;
    }
    if (available == 0)
      break;

    const DWORD want = static_cast<DWORD>(
        available < buffer.size() ? available : static_cast<DWORD>(buffer.size()));
    DWORD read = 0;
    if (!::WinHttpReadData(s.request, buffer.data(), want, &read))
    {
      errorOut = LastErrorText("WinHttpReadData");
      abandonPartialFile(out);
      return false;
    }
    if (read == 0)
      break;

    if (std::fwrite(buffer.data(), 1, read, out) != read)
    {
      errorOut = "write failed (disk full?)";
      abandonPartialFile(out);
      return false;
    }
    received += read;

    if (onProgress && !onProgress(received, total))
    {
      errorOut = "cancelled";
      abandonPartialFile(out);
      return false;
    }
  }

  if (std::fclose(out) != 0)
  {
    errorOut = "failed to flush " + destPathUtf8;
    std::error_code removeEc;
    std::filesystem::remove(destPath, removeEc);
    return false;
  }

  // A server that reported a length and then sent less produced a truncated file. The hash
  // check would catch it too, but naming it here gives the honest reason (REQ-201).
  if (total > 0 && received != total)
  {
    errorOut = "truncated download: expected " + std::to_string(total) + " bytes, got " +
               std::to_string(received);
    std::error_code removeEc;
    std::filesystem::remove(destPath, removeEc);
    return false;
  }
  return true;
}

bool ComputeFileSha256(const std::string& pathUtf8, std::string& hexOut, std::string& errorOut)
{
  hexOut.clear();
  errorOut.clear();

  std::FILE* f = nullptr;
  if (::_wfopen_s(&f, std::filesystem::u8path(pathUtf8).wstring().c_str(), L"rb") != 0 || !f)
  {
    errorOut = "could not open " + pathUtf8 + " for hashing";
    return false;
  }

  BCRYPT_ALG_HANDLE  alg  = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::vector<UCHAR> hashObject;
  std::vector<UCHAR> digest;
  bool               ok = false;

  // One exit path for a function with five failure points and three resources to release.
  auto cleanup = [&]() {
    if (hash) ::BCryptDestroyHash(hash);
    if (alg)  ::BCryptCloseAlgorithmProvider(alg, 0);
    if (f)    std::fclose(f);
  };

  if (::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
  {
    errorOut = "BCryptOpenAlgorithmProvider failed";
    cleanup();
    return false;
  }

  DWORD objectSize = 0, digestSize = 0, copied = 0;
  if (::BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
                          sizeof(objectSize), &copied, 0) < 0 ||
      ::BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digestSize),
                          sizeof(digestSize), &copied, 0) < 0)
  {
    errorOut = "BCryptGetProperty failed";
    cleanup();
    return false;
  }

  hashObject.resize(objectSize);
  digest.resize(digestSize);

  if (::BCryptCreateHash(alg, &hash, hashObject.data(), objectSize, nullptr, 0, 0) < 0)
  {
    errorOut = "BCryptCreateHash failed";
    cleanup();
    return false;
  }

  std::vector<UCHAR> buffer(64 * 1024);
  for (;;)
  {
    const size_t read = std::fread(buffer.data(), 1, buffer.size(), f);
    if (read == 0)
      break;
    if (::BCryptHashData(hash, buffer.data(), static_cast<ULONG>(read), 0) < 0)
    {
      errorOut = "BCryptHashData failed";
      cleanup();
      return false;
    }
  }
  if (std::ferror(f))
  {
    errorOut = "read error while hashing " + pathUtf8;
    cleanup();
    return false;
  }

  if (::BCryptFinishHash(hash, digest.data(), digestSize, 0) < 0)
  {
    errorOut = "BCryptFinishHash failed";
    cleanup();
    return false;
  }

  static const char* kHex = "0123456789abcdef";
  hexOut.reserve(static_cast<size_t>(digestSize) * 2);
  for (DWORD i = 0; i < digestSize; ++i)
  {
    hexOut.push_back(kHex[digest[i] >> 4]);
    hexOut.push_back(kHex[digest[i] & 0x0F]);
  }
  ok = true;

  cleanup();
  return ok;
}
