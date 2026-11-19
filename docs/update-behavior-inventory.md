# Current update behavior inventory

This records what the checked-in Qt/C++ application currently does for software updates. It is a
source inventory, not evidence that a production upgrade has been exercised. The accepted product
requirements are [REQ-077](../spec/requirements.md) (startup check) and
[REQ-078](../spec/requirements.md) (user-approved update).

## Startup check

- On a normal launch, after loading user preferences, the application starts the update check
  before showing the editor. The splash remains visible while the check is running, and the editor
  does not open until the check finishes. Smoke-test launches skip this path.
- If update checking is disabled, no request is started. If Windows reports no Internet route, the
  check also exits immediately and writes a diagnostic message; it does not wait for a timeout.
- The selected channel chooses one of two HTTPS manifest URLs. Stable uses GitHub's
  `releases/latest/download/latest.json`; beta uses the `channel-beta` release tag's
  `latest.json` asset.
- The manifest fetch runs on a worker thread. It uses WinHTTP with 3,000 ms values for its
  resolve, connect, send, and receive timeouts. A fetched body is capped at 1 MiB.
- A malformed manifest, HTTP/network failure, timeout, current or older version, or skipped
  version produces no offer. The reason is sent to the Windows debug output; startup continues.
- The version parser accepts `major.minor.patch` with an optional `-label.number` prerelease.
  Numeric components cannot have leading zeroes and are capped at 1,000,000. Prerelease labels
  compare lexically and their numeric suffixes compare numerically; a release outranks its own
  prereleases.
- A manifest needs non-empty string values for `version`, `installerUrl`, and `sha256`. It may
  also provide `notes`, channel, size, `mandatory`, drawing-format compatibility fields, and an
  explicit breaking-drawings warning. `mandatory` is parsed but does not change the user choices or
  install flow.

## Offer and preferences

- A strictly newer version that is not the exact version previously skipped becomes an offer.
  The offer is a modal **Software Update** dialog shown before the user starts working.
- The dialog shows the offered and running versions, optional release notes, and a drawing-format
  warning when the manifest says the offered version writes a newer format or explicitly declares
  that existing drawings will break. A missing compatibility field causes no warning.
- When the current executable is under Program Files, the offer also explains the move to
  `%LOCALAPPDATA%\Programs\GoSurvey`, the one-time Windows approval, preservation of settings and
  data, and future no-elevation updates.
- The offer has three choices: **Update Now**, **Remind Me Later**, and **Skip This Version**.
  Remind-later clears the in-memory offer without saving a suppression value. Skip saves that
  version in user preferences; if saving fails, the old value is restored and a warning is shown.
  Settings can clear the skipped version.
- Update settings expose the startup check toggle, beta-channel toggle, and skipped-version
  display/clear control. Settings stage these edits until Apply/OK. Preferences store the values as
  `updateCheckEnabled`, `updateUseBetaChannel`, and `updateSkippedVersion`.

## Download, verification, and install

- Only clicking **Update Now** starts a download. The installer is written under
  `%LOCALAPPDATA%\GoSurvey\updates` with the offered version in its filename. The download has
  progress reporting, a 120,000 ms WinHTTP timeout setting, and a Cancel action.
- Cancellation and download failures delete partial files. When the server supplies a content
  length, a short response is reported as truncated and deleted.
- After download, the application computes SHA-256 and compares it with the manifest. A mismatch
  deletes the installer and is shown as an error. The hash detects corruption; because the manifest
  and installer share a host, it does not authenticate the publisher.
- A verified download presents **Install and Restart** and **Not Now**. Install goes through the
  normal window-close/unsaved-drawing prompts; cancelling a save prompt cancels the update and
  keeps the offer available. A successful close hands off to the installer and exits GoSurvey.
- A per-user install starts the downloaded Inno installer silently, closes GoSurvey, requests
  relaunch, and skips the shared VC++ runtime installer. It does not request elevation.
- If the currently running executable is under Program Files, the handoff instead starts the
  PowerShell bootstrap script used for installation, marked as an in-app update. That path performs
  the per-user migration and its one-time Windows approval.
- Not Now clears the in-memory offer. The downloaded file remains on disk, but a later offer's
  Update Now action starts the download flow again; there is no code path that reuses the prior
  file automatically.
- User-started download, hash, and installer-launch failures appear in the update dialog. The
  startup check's failures are deliberately silent, as described by ADR-029.

## Current implementation notes for the port

- REQ-077 describes a modal “Checking for updates” dialog with a progress indicator. The current
  code instead holds the splash screen while the worker runs; it does not show a dedicated update
  checking dialog or request progress from the manifest fetch. The editor is still gated until the
  check resolves.
- The startup request uses the four WinHTTP timeout settings noted above. The source does not add
  a separate wall-clock deadline around the whole fetch sequence.
- The update dialog's **Not Now** path leaves the verified installer file in the updates folder,
  but the next Update Now path downloads again. The source comment says it reuses the file, while
  the implementation starts `BeginDownload` and overwrites the destination.
- The pure version/manifest/decision logic has unit tests in `tests/UpdateCheckTests.cpp`. The
  network calls and installer handoff are Windows API wrappers and are explicitly not unit-tested
  in `src/platform/HttpFetch.hpp`; the task history also records that a real production update has
  not been performed. Treat download, hash, migration, and relaunch behavior as code-described
  until exercised end to end.

## Source map

- Startup sequencing: `src/qt/main.cpp`, `src/qt/EditorWindow.cpp`
- Decision rules and manifest parsing: `src/update/UpdateCheck.hpp`, `src/update/UpdateCheck.cpp`
- Async check/download state and installer handoff: `src/update/UpdateService.hpp`,
  `src/update/UpdateService.cpp`
- Dialog and user actions: `src/qt/EditorUpdate.cpp`
- Settings and persistence: `src/qt/EditorDialogs.cpp`, `src/io/UserPrefs.cpp`
- HTTPS, file download, and SHA-256: `src/platform/HttpFetch.hpp`, `src/platform/HttpFetch.cpp`
- Accepted behavior: `spec/requirements.md` REQ-077 and REQ-078; `spec/architecture.md` ADR-029
