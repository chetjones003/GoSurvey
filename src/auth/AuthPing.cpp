#include "AuthPing.hpp"

#include <random>
#include <sstream>

namespace {

std::string RandomStringFromCharset(const std::string& charset, size_t length) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<size_t> dis(0, charset.size() - 1);

  std::string out;
  out.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    out.push_back(charset[dis(gen)]);
  }
  return out;
}

}  // namespace

std::string UrlEncodeQueryComponent(const std::string& value) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    const bool unreserved =
        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '.' || c == '_' || c == '~';
    if (unreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

std::string GeneratePkceCodeVerifier() {
  static const std::string kUnreserved =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
  return RandomStringFromCharset(kUnreserved, 64);
}

std::string GenerateOAuthState() {
  static const std::string kHexChars = "0123456789abcdef";
  return RandomStringFromCharset(kHexChars, 32);
}

std::string Base64UrlEncode(const std::vector<unsigned char>& data) {
  static const char* kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);

  size_t i = 0;
  while (i + 3 <= data.size()) {
    const unsigned int chunk =
        (static_cast<unsigned int>(data[i]) << 16) |
        (static_cast<unsigned int>(data[i + 1]) << 8) |
        static_cast<unsigned int>(data[i + 2]);
    out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
    out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
    out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
    out.push_back(kAlphabet[chunk & 0x3F]);
    i += 3;
  }

  const size_t remaining = data.size() - i;
  if (remaining == 1) {
    const unsigned int chunk = static_cast<unsigned int>(data[i]) << 16;
    out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
    out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
    // No padding characters: RFC 7636 code_challenge is unpadded base64url.
  } else if (remaining == 2) {
    const unsigned int chunk =
        (static_cast<unsigned int>(data[i]) << 16) | (static_cast<unsigned int>(data[i + 1]) << 8);
    out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
    out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
    out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
  }

  return out;
}

std::vector<unsigned char> Base64UrlDecode(const std::string& encoded) {
  auto decodeChar = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;  // padding ('=') or invalid — both end decoding of the current group
  };

  std::vector<unsigned char> out;
  out.reserve((encoded.size() * 3) / 4 + 3);

  int          buffer      = 0;
  int          bufferBits  = 0;
  for (char c : encoded) {
    const int value = decodeChar(c);
    if (value < 0) {
      if (c == '=') break;  // explicit padding: stop cleanly rather than treat it as an error
      return {};            // any other invalid character makes the whole input unusable
    }
    buffer = (buffer << 6) | value;
    bufferBits += 6;
    if (bufferBits >= 8) {
      bufferBits -= 8;
      out.push_back(static_cast<unsigned char>((buffer >> bufferBits) & 0xFF));
    }
  }
  return out;
}

std::string BuildAuthorizeUrl(const std::string& auth0Domain,
                              const std::string& clientId,
                              const std::string& redirectUri,
                              const std::string& codeChallenge,
                              const std::string& state,
                              const std::string& audience) {
  std::ostringstream oss;
  oss << "https://" << auth0Domain << "/authorize"
      << "?response_type=code"
      << "&client_id=" << UrlEncodeQueryComponent(clientId)
      << "&redirect_uri=" << UrlEncodeQueryComponent(redirectUri)
      << "&scope=" << UrlEncodeQueryComponent("openid profile email offline_access")
      << "&audience=" << UrlEncodeQueryComponent(audience)
      << "&code_challenge=" << UrlEncodeQueryComponent(codeChallenge)
      << "&code_challenge_method=S256"
      << "&state=" << UrlEncodeQueryComponent(state);
  return oss.str();
}

std::string DecideAuthAction(bool hasRefreshToken, bool refreshTokenExpired) {
  if (hasRefreshToken && !refreshTokenExpired) {
    return "silent-refresh";
  }
  return "interactive";
}
