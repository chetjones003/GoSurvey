#include "CredentialStore.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

namespace {

// A single stable target name identifies this credential across app restarts. Namespaced with
// the app name so it doesn't collide with anything else Credential Manager holds for this user.
constexpr const wchar_t* kCredentialTargetName = L"GoSurvey/Auth0RefreshToken";

}  // namespace

bool StoreRefreshToken(const std::string& refreshToken, std::string& errorOut) {
  errorOut.clear();

  CREDENTIALW cred{};
  cred.Type               = CRED_TYPE_GENERIC;
  cred.TargetName         = const_cast<LPWSTR>(kCredentialTargetName);
  cred.CredentialBlobSize = static_cast<DWORD>(refreshToken.size());
  cred.CredentialBlob     = reinterpret_cast<LPBYTE>(const_cast<char*>(refreshToken.data()));
  cred.Persist            = CRED_PERSIST_LOCAL_MACHINE;
  cred.Comment            = const_cast<LPWSTR>(L"GoSurvey Auth0 refresh token");

  if (!::CredWriteW(&cred, 0)) {
    errorOut = "CredWriteW failed (GetLastError=" + std::to_string(::GetLastError()) + ")";
    return false;
  }
  return true;
}

bool LoadRefreshToken(std::string& refreshTokenOut) {
  refreshTokenOut.clear();

  PCREDENTIALW cred = nullptr;
  if (!::CredReadW(kCredentialTargetName, CRED_TYPE_GENERIC, 0, &cred)) {
    // ERROR_NOT_FOUND is the ordinary "never signed in" state, not a failure to report.
    return false;
  }

  refreshTokenOut.assign(reinterpret_cast<const char*>(cred->CredentialBlob),
                        cred->CredentialBlobSize);
  ::CredFree(cred);
  return true;
}

bool DeleteRefreshToken() {
  // CredDeleteW returning false because the credential never existed is not a failure the caller
  // needs to see — the post-condition ("nothing stored") holds either way.
  ::CredDeleteW(kCredentialTargetName, CRED_TYPE_GENERIC, 0);
  return true;
}
