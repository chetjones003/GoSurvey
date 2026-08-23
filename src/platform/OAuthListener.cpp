#include "OAuthListener.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace {

constexpr int    kBacklog         = 1;   // one sign-in attempt per instance (class comment)
constexpr size_t kMaxRequestBytes = 8192; // a browser GET request line + headers, generous cap

int HexDigitValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string PercentDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      const int hi = HexDigitValue(in[i + 1]);
      const int lo = HexDigitValue(in[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(in[i] == '+' ? ' ' : in[i]);
  }
  return out;
}

void ParseQueryString(const std::string& query, std::string& code, std::string& state,
                      std::string& error) {
  size_t pos = 0;
  while (pos < query.size()) {
    const size_t amp  = query.find('&', pos);
    const size_t end  = (amp == std::string::npos) ? query.size() : amp;
    const std::string pair = query.substr(pos, end - pos);
    const size_t eq   = pair.find('=');
    if (eq != std::string::npos) {
      const std::string key   = pair.substr(0, eq);
      const std::string value = PercentDecode(pair.substr(eq + 1));
      if (key == "code")       code = value;
      else if (key == "state") state = value;
      else if (key == "error") error = value;
    }
    if (amp == std::string::npos) break;
    pos = amp + 1;
  }
}

/// Extracts the query string from an HTTP request line, e.g.
/// "GET /callback?code=abc&state=xyz HTTP/1.1". No query string is not itself an error — the
/// caller decides what an empty callback means.
bool ExtractQueryFromRequestLine(const std::string& requestLine, std::string& queryOut) {
  const size_t pathStart = requestLine.find(' ');
  if (pathStart == std::string::npos) return false;
  const size_t pathEnd = requestLine.find(' ', pathStart + 1);
  if (pathEnd == std::string::npos) return false;

  const std::string target = requestLine.substr(pathStart + 1, pathEnd - pathStart - 1);
  const size_t q = target.find('?');
  if (q == std::string::npos) {
    queryOut.clear();
    return true;
  }
  queryOut = target.substr(q + 1);
  return true;
}

constexpr const char* kResponseBody =
    "<html><body><p>Sign-in complete. You can close this tab and return to "
    "GoSurvey.</p></body></html>";

}  // namespace

OAuthListener::OAuthListener()
    : listenSocket_(reinterpret_cast<void*>(static_cast<UINT_PTR>(INVALID_SOCKET))),
      port_(0),
      wsaInitialized_(false) {}

OAuthListener::~OAuthListener() {
  const SOCKET sock = reinterpret_cast<SOCKET>(listenSocket_);
  if (sock != INVALID_SOCKET) {
    ::closesocket(sock);
  }
  if (wsaInitialized_) {
    ::WSACleanup();
  }
}

bool OAuthListener::Start(int port, std::string& errorOut) {
  errorOut.clear();

  WSADATA wsaData{};
  if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    errorOut = "WSAStartup failed";
    return false;
  }
  wsaInitialized_ = true;

  const SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) {
    errorOut = "socket() failed";
    return false;
  }
  listenSocket_ = reinterpret_cast<void*>(static_cast<UINT_PTR>(sock));

  sockaddr_in addr{};
  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
  addr.sin_port        = ::htons(static_cast<u_short>(port));

  if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    // The ordinary reason: this exact port is already in use by something else. AuthService
    // tries the next candidate port, so this is not logged as alarming here.
    errorOut = "bind() to 127.0.0.1:" + std::to_string(port) + " failed (port in use?)";
    return false;
  }
  if (::listen(sock, kBacklog) == SOCKET_ERROR) {
    errorOut = "listen() failed";
    return false;
  }

  port_ = port;
  return true;
}

bool OAuthListener::AwaitCallback(int          timeoutMs,
                                  std::string& codeOut,
                                  std::string& stateOut,
                                  std::string& errorQueryParam,
                                  std::string& errorOut) {
  codeOut.clear();
  stateOut.clear();
  errorQueryParam.clear();
  errorOut.clear();

  const SOCKET listenSock = reinterpret_cast<SOCKET>(listenSocket_);
  if (listenSock == INVALID_SOCKET) {
    errorOut = "listener was never started";
    return false;
  }

  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(listenSock, &readSet);
  timeval tv{};
  tv.tv_sec  = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;

  // First argument is ignored on Windows; passing 0 rather than listenSock+1 is the documented
  // Winsock convention (unlike POSIX select()).
  const int selectResult = ::select(0, &readSet, nullptr, nullptr, &tv);
  if (selectResult == 0) {
    errorOut = "timed out waiting for the browser redirect";
    return false;
  }
  if (selectResult == SOCKET_ERROR) {
    errorOut = "select() failed";
    return false;
  }

  const SOCKET client = ::accept(listenSock, nullptr, nullptr);
  if (client == INVALID_SOCKET) {
    errorOut = "accept() failed";
    return false;
  }

  std::string request;
  char        buffer[1024];
  while (request.find("\r\n") == std::string::npos && request.size() < kMaxRequestBytes) {
    const int received = ::recv(client, buffer, sizeof(buffer), 0);
    if (received <= 0) break;
    request.append(buffer, static_cast<size_t>(received));
  }

  std::string requestLine = request.substr(0, request.find("\r\n"));
  std::string query;
  const bool  parsed = ExtractQueryFromRequestLine(requestLine, query);

  const std::string body = kResponseBody;
  const std::string response =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Content-Length: " +
      std::to_string(body.size()) +
      "\r\n"
      "Connection: close\r\n"
      "\r\n" +
      body;
  ::send(client, response.data(), static_cast<int>(response.size()), 0);
  ::shutdown(client, SD_BOTH);
  ::closesocket(client);

  if (!parsed) {
    errorOut = "malformed HTTP request from the redirect";
    return false;
  }

  ParseQueryString(query, codeOut, stateOut, errorQueryParam);
  return true;
}
