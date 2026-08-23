#pragma once

#include <string>

/// Windows Credential Manager storage for the Auth0 refresh token (REQ-091, ADR-037 (c)).
///
/// A deliberate departure from the plaintext `gosurvey-user.json` pattern REQ-080's telemetry ids
/// use (`io/UserPrefs`): those are anonymous and low-value if read, while a refresh token is a
/// live, reusable credential and gets the OS-native protected store instead. Nothing in the
/// codebase does this today — this is new, small platform surface, not an extension of UserPrefs.

/// Stores \p refreshToken under a named generic credential, replacing any previously stored value.
/// Returns false with a reason on failure.
bool StoreRefreshToken(const std::string& refreshToken, std::string& errorOut);

/// Loads the previously stored refresh token. Returns false (with no error text) if none is
/// stored — that is the ordinary "never signed in" / "signed out" state, not a failure.
bool LoadRefreshToken(std::string& refreshTokenOut);

/// Deletes the stored refresh token, if any. Returns true whether or not one existed.
bool DeleteRefreshToken();
