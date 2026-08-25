#include <catch2/catch_test_macros.hpp>

#include "../src/auth/AuthPing.hpp"

TEST_CASE("AuthPing: GeneratePkceCodeVerifier is 64 chars from the unreserved set", "[auth]") {
  const std::string verifier = GeneratePkceCodeVerifier();
  REQUIRE(verifier.length() == 64);
  for (char c : verifier) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
    REQUIRE(unreserved);
  }
}

TEST_CASE("AuthPing: Unique code verifiers are different", "[auth]") {
  REQUIRE(GeneratePkceCodeVerifier() != GeneratePkceCodeVerifier());
}

TEST_CASE("AuthPing: GenerateOAuthState produces 32 hex characters", "[auth]") {
  const std::string state = GenerateOAuthState();
  REQUIRE(state.length() == 32);
  for (char c : state) {
    REQUIRE(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
  }
}

TEST_CASE("AuthPing: Base64UrlEncode matches known RFC 4648 test vectors, unpadded", "[auth]") {
  // "f" -> "Zg", "fo" -> "Zm8", "foo" -> "Zm9v" (standard base64 has no '+' '/' here, so the url
  // alphabet and the standard one agree on these particular vectors; the padding removal is the
  // part actually under test).
  REQUIRE(Base64UrlEncode({'f'}) == "Zg");
  REQUIRE(Base64UrlEncode({'f', 'o'}) == "Zm8");
  REQUIRE(Base64UrlEncode({'f', 'o', 'o'}) == "Zm9v");
  REQUIRE(Base64UrlEncode({}) == "");
}

TEST_CASE("AuthPing: Base64UrlEncode uses '-' and '_' in place of '+' and '/'", "[auth]") {
  // Bytes 0xFB 0xFF 0xBF standard-base64-encode to "+/+/" family; confirm the url-safe substitution.
  const std::vector<unsigned char> bytes = {0xFB, 0xFF, 0xBF};
  const std::string encoded = Base64UrlEncode(bytes);
  REQUIRE(encoded.find('+') == std::string::npos);
  REQUIRE(encoded.find('/') == std::string::npos);
  REQUIRE(encoded.find('=') == std::string::npos);
}

TEST_CASE("AuthPing: Base64UrlDecode round-trips known vectors", "[auth]") {
  const std::vector<unsigned char> a = {'f'};
  const std::vector<unsigned char> b = {'f', 'o'};
  const std::vector<unsigned char> c = {'f', 'o', 'o'};
  REQUIRE(Base64UrlDecode("Zg") == a);
  REQUIRE(Base64UrlDecode("Zm8") == b);
  REQUIRE(Base64UrlDecode("Zm9v") == c);
  REQUIRE(Base64UrlDecode("").empty());
}

TEST_CASE("AuthPing: Base64UrlDecode round-trips through Base64UrlEncode", "[auth]") {
  const std::vector<unsigned char> original = {0xFB, 0xFF, 0xBF, 0x00, 0x10, 0x7A};
  const std::string encoded = Base64UrlEncode(original);
  REQUIRE(Base64UrlDecode(encoded) == original);
}

TEST_CASE("AuthPing: Base64UrlDecode rejects invalid characters", "[auth]") {
  REQUIRE(Base64UrlDecode("not valid base64!!").empty());
}

TEST_CASE("AuthPing: BuildAuthorizeUrl contains every required PKCE parameter", "[auth]") {
  const std::string url = BuildAuthorizeUrl("gosurvey.us.auth0.com", "abc123",
                                            "http://127.0.0.1:54321/callback", "chal-lenge_1",
                                            "state-abc", "https://gosurvey-accounts/");
  REQUIRE(url.find("https://gosurvey.us.auth0.com/authorize") == 0);
  REQUIRE(url.find("response_type=code") != std::string::npos);
  REQUIRE(url.find("client_id=abc123") != std::string::npos);
  REQUIRE(url.find("code_challenge=chal-lenge_1") != std::string::npos);
  REQUIRE(url.find("code_challenge_method=S256") != std::string::npos);
  REQUIRE(url.find("state=state-abc") != std::string::npos);
  REQUIRE(url.find("audience=https%3A%2F%2Fgosurvey-accounts%2F") != std::string::npos);
  // The redirect_uri's ':' and '/' must be percent-encoded as a query VALUE.
  REQUIRE(url.find("redirect_uri=http%3A%2F%2F127.0.0.1%3A54321%2Fcallback") != std::string::npos);
}

TEST_CASE("AuthPing: DecideAuthAction picks silent-refresh only with a valid stored token", "[auth]") {
  REQUIRE(DecideAuthAction(true, false) == "silent-refresh");
  REQUIRE(DecideAuthAction(true, true) == "interactive");
  REQUIRE(DecideAuthAction(false, false) == "interactive");
  REQUIRE(DecideAuthAction(false, true) == "interactive");
}
