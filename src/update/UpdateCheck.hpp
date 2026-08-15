#pragma once

#include <string>

/// REQ-077 / REQ-078 — deciding whether a fetched manifest describes a newer build.
///
/// Everything here is pure (ADR-029 (g)): no network, no window, no GL, no filesystem. The
/// WinHTTP fetch and the installer launch live in `platform/`; this decides what to do with
/// what they return. That split exists because version ordering across the prerelease
/// boundary is the part most likely to be quietly wrong, and quietly wrong here means a user
/// is never offered an update — a failure with no symptom to notice.
namespace update {

enum class Channel {
  Stable,
  Beta,
};

/// The updater's persisted settings. Lives in `AppCommandState` and round-trips through
/// `UserPrefs`. Declared in this header rather than `UpdateService.hpp` on purpose: this one is
/// pure and cheap to include, so holding it costs the command layer no `<thread>`.
struct UpdatePrefs {
  bool        enabled        = true;   ///< REQ-077: the user can switch the check off entirely
  bool        useBetaChannel = false;
  std::string skippedVersion;          ///< "Skip this version" — suppresses that version only
  long long   lastCheckUnix  = 0;      ///< throttle anchor; 0 = never checked
};

/// A parsed version. `prereleaseLabel` is empty for a release build; a release always outranks
/// its own prereleases, which is why the label cannot simply be compared as a string.
struct Version {
  int         major = 0;
  int         minor = 0;
  int         patch = 0;
  std::string prereleaseLabel;   ///< "beta", "dev", or empty for a release
  int         prereleaseNumber = 0;

  bool isPrerelease() const { return !prereleaseLabel.empty(); }
};

/// Parses "0.5.0" or "0.5.0-beta.7". Returns false on anything else — including a partial
/// version like "0.5" and trailing garbage like "0.5.0-beta.7x", both of which must be
/// rejected rather than silently coerced.
bool ParseVersion(const std::string& text, Version& out);

/// Total order over versions: -1 if a < b, 0 if equal, +1 if a > b.
///
/// Ordering rules, in order of precedence:
///   1. major, then minor, then patch, numerically;
///   2. a release outranks any prerelease of the same core ("0.5.0" > "0.5.0-beta.9");
///   3. between two prereleases of the same core, label lexicographically, then number
///      NUMERICALLY — "beta.10" > "beta.2", which string comparison gets backwards.
int CompareVersions(const Version& a, const Version& b);

/// The update manifest published as a release asset by the CI pipeline (REQ-202).
struct Manifest {
  std::string version;
  std::string channel;
  std::string installerUrl;
  std::string sha256;
  long long   size = 0;
  std::string notes;
  /// Reserved by ADR-029 so that making an update mandatory later is a policy change rather
  /// than a protocol change. Parsed and carried; nothing acts on it yet.
  bool        mandatory = false;
};

/// Parses `latest.json`. Returns false and sets \p errorOut on malformed JSON or a missing or
/// wrong-typed required field. Required: version, installerUrl, sha256. A manifest missing any
/// of those cannot be acted on safely, so it is rejected rather than half-accepted.
bool ParseManifest(const std::string& json, Manifest& out, std::string& errorOut);

/// The permanent manifest URL for a channel (ADR-029 (d)).
///
/// Stable resolves through `releases/latest/download/...`, which GitHub defines to exclude
/// prereleases — so a stable install is structurally unable to see a beta rather than
/// filtering one out here. Beta reads the fixed `channel-beta` tag whose assets CI clobbers.
std::string ManifestUrlForChannel(Channel channel, const std::string& ownerRepo);

/// What the caller should do about a fetched manifest.
enum class Decision {
  NoUpdate,      ///< up to date, older, unparseable, or the user skipped this exact version
  Offer,         ///< newer than the running build — show the REQ-078 dialog
};

/// The whole REQ-077 decision in one pure call.
///
/// \p skippedVersion is the version the user pressed "Skip this version" on, or empty. Skipping
/// suppresses only that exact version: a later one still prompts, which is what stops "skip"
/// from silently becoming "never update again".
/// \p reasonOut always receives a short explanation, including on NoUpdate — this is the text
/// the silent-failure path logs (ADR-029 (h)), and it is the only way to tell "no update" from
/// "something was wrong with the manifest" after the fact.
Decision DecideUpdate(const std::string& runningVersion,
                      const Manifest&    manifest,
                      const std::string& skippedVersion,
                      std::string&       reasonOut);

}  // namespace update
