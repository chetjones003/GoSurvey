# TASK-882 — Qt software update workflow

## Authority

- GitHub issue #882 (parent tracker #878); accepted **REQ-077** (startup update check, silent on
  failure) and **REQ-078** (update dialog, hash-verified install, unsaved-changes guard).
- Beta 0.7.4 behavior is authoritative; the Qt UI must not change what the shared `update::`
  service decides.

## Scope

- Run the REQ-077 startup check from the Qt entry point, reusing `update::BeginStartupCheck` /
  `PollUpdateTask`. Current, failed, disabled and offline checks stay silent (REQ-077 exception to
  REQ-201); only an offer shows UI.
- Qt Software Update dialog for the REQ-078 phases: offer (both versions, compatibility notice above
  the release notes, Update Now / Remind Me Later / Skip This Version), downloading with progress and
  Cancel, ready (Install and Restart / Not Now), failed (reason shown, Close). Not dismissable
  except by its buttons.
- Install and Restart goes through the window's existing unsaved-changes close path; cancelling
  the save prompt cancels the update. The installer starts only after that path completes.
- Settings → Updates: check on startup, include beta releases, skipped version with Clear;
  saved immediately, reverting the control if the write fails.
- Offer is shown before the sign-in gate (matches the ImGui launch sequence).

## Files and boundaries

`src/qt/EditorUpdate.cpp` (new), `src/qt/NonDismissableDialog.hpp` (new, shared with the sign-in
gate), `EditorWindow.*`, `EditorDialogs.cpp`, `main.cpp`, `cmake/GoSurveyQt.cmake` (link
`UpdateService.cpp`), `tests/QtEditorTests.cpp`, `docs/qt-074-parity-ledger.md`. No `spec/` change,
no new dependency, no change to `update::` or `platform/`.

## Decisions

- Interpretation of "current, available and failure states": 0.7.4 shows nothing for current or a
  failed check (REQ-077) and a dialog for available and for a failed download/verify. Parity keeps it
  that way; no manual "Check now" was added (it would be a new requirement).
- Deviation (judgement call): if the installer cannot start (UAC declined / launch error), Qt stays
  open and the dialog reports it. The ImGui loop exits regardless. The service itself documents a
  declined UAC prompt as "returns to the offer".
- UI direction selected by the user: dialog option C (navy banner), settings option A (one page).

## Verification

- Qt build and `GoSurveyQtTests` (all tests, including 11 new update/settings workflow tests).
- Not automated: live manifest fetch, successful download + hash, real elevated installer launch.
