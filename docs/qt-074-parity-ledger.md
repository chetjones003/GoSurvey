# GoSurvey 0.7.4 Qt parity ledger

Reference: release notes in [`RELEASE_NOTES.md`](../RELEASE_NOTES.md#074),
checked against accepted requirements and the current Qt source on branch
`test/win32UI`. This ledger covers the user-visible 0.7.4 release items and
their Qt routes, dialogs, and complete workflows. The broader release ribbon
inventory remains in [the Qt migration status](qt-ui-migration.md#version-072-ribbon-inventory).

## Status meanings

- **Verified** — a focused Qt workflow test exercises the behavior, including
  relevant failure, cancellation, undo, or read-only behavior.
- **Implemented** — the native Qt route and UI exist, but focused workflow
  coverage does not yet establish the complete behavior.
- **Missing** — a Qt workflow is absent, disabled, or materially incomplete.

A shared command dispatcher, catalog entry, or source reference by itself is
not evidence of a usable Qt workflow.

## Release feature workflows

| 0.7.4 feature / authority | Qt route and surface | Status | Evidence / gap and implementation issue |
|---|---|---|---|
| Traverse ground-to-grid scale; preserve default-off behavior (REQ-401, [#828](https://github.com/chetjones003/GoSurvey/issues/828)) | `TRAVERSE` / `LSA` → Adjustment window; command text accepts `#SCALE`, but no active-drawing projection resolver is connected | **Missing** | The editor does not expose the legacy scale option or resolve the active drawing's combined/grid factor before computing. This gap is tracked by [#911](https://github.com/chetjones003/GoSurvey/issues/911). |
| Traverse save/reload `.gstrav`, including malformed-file behavior and project tracking (REQ-403, [#834](https://github.com/chetjones003/GoSurvey/issues/834)) | Adjustment → Open/Import, Save, Save As | **Implemented** | `rawObservationsSurviveStructuredSessionSave` and `malformedOpenAndSavePreserveExistingSession` cover raw-data round-trip and failure preservation. Qt save code tracks files inside writable projects, but that project workflow lacks focused coverage; verify under [#912](https://github.com/chetjones003/GoSurvey/issues/912). |
| LSA network adjustment, GPS vectors and elevation acceptance (REQ-416–417, [#846](https://github.com/chetjones003/GoSurvey/issues/846), [#847](https://github.com/chetjones003/GoSurvey/issues/847), [#857](https://github.com/chetjones003/GoSurvey/issues/857)) | `TRAVERSE` / `LSA` → command editor, Calculate, Accept, Commit to Drawing | **Implemented** | `AdjustmentWindow` exposes command text and solve/accept/commit. Existing Qt tests verify solve/commit/undo and elevation acceptance, but do not exercise imported GPS vectors end to end; track completion under [#914](https://github.com/chetjones003/GoSurvey/issues/914). |
| Measurement residuals, standardized marks, chi-square and report content (REQ-418, REQ-423) | Adjustment → results table/output; Export report | **Implemented** | Output and report export are native. Current focused Qt coverage does not assert residual-table semantics or exported report contents against a known adjustment. Add this to [#915](https://github.com/chetjones003/GoSurvey/issues/915). |
| Network commands, StarNet `.dat` and field-code/comment preservation (REQ-419–421; [#846](https://github.com/chetjones003/GoSurvey/issues/846)) | Adjustment → Import observations (`.dat`) or command editor | **Implemented** | Importer and command editor routes exist; the complete import → inspect/edit → solve → save/reopen workflow and malformed/cancel behavior lack focused Qt coverage. Track under [#914](https://github.com/chetjones003/GoSurvey/issues/914). |
| JobXML, RW5 and FBK import (0.7.4 release notes; REQ-419–421) | Adjustment → Import observations | **Implemented** | Native importer routes exist for `.jxl`/`.xml`, `.rw5`, and `.fbk`, but the Qt workflow lacks focused format, validation, and cancellation coverage. Track with [#913](https://github.com/chetjones003/GoSurvey/issues/913). |
| Merge multiple raw-data imports into one continuous traverse (0.7.4 release notes; [#846](https://github.com/chetjones003/GoSurvey/issues/846)) | Adjustment → Import observations | **Missing** | The picker accepts one file, and successful import replaces the working data. The release workflow requires merging multiple files; implement and verify this under [#913](https://github.com/chetjones003/GoSurvey/issues/913). |
| Project adjustment settings, curvature/refraction, and persistent options (REQ-422, #857) | Adjustment → Instrument / Adjustment Settings dialog | **Implemented** | Dialog exposes settings and they feed the shared solver. Qt coverage does not yet establish validation, Cancel preservation, persisted settings after reopen, and unit behavior as one workflow. Track with [#915](https://github.com/chetjones003/GoSurvey/issues/915). |
| Point-cloud E57/LAS/LAZ/PTS/PTX attach/import and export (REQ-172; release notes) | Insert → Point Cloud → Attach; contextual Point Cloud tools | **Missing** | The ribbon Attach item is explicitly disabled (“not implemented yet”). Contextual display and centerline controls only operate on an already-loaded cloud and do not replace attach/import. This gap is tracked by [#916](https://github.com/chetjones003/GoSurvey/issues/916). |
| Sign-in choice at startup and later sign-in from Settings (REQ-091; release notes) | Startup gate; logo menu → View → Settings → Account | **Implemented** | `savedOptOutResolvesQtLaunchWithoutAuthAttempt`, `startupGateUsesApplicationLogoAndRequiresAChoice`, and `settingsAccountSupportsLaterSignInAndReadOnlyStatus` cover the opted-out path, required startup choice, actual bundled app logo, and Account actions. Startup design B and Account design C were selected and applied for [#880](https://github.com/chetjones003/GoSurvey/issues/880); screenshots are in `build/qt-880-startup-final.png` and `build/qt-880-account-final.png`. Silent refresh, interactive success/failure, and decline persistence failure still need end-to-end coverage. |
| Recent drawings/projects and version-based What's New (REQ-308, REQ-336) | Start screen New/Open Project, recent project cards and thumbnails, recent drawing grid/list cards; startup What's New and Help → What's new/About | **Implemented** | Qt records successfully opened/saved drawings and successfully opened projects in the existing MRU stores; cards show last-opened dates, project tags and paths; right-click menus provide Open/Remove and drawing file actions. Project cards use a recent project drawing thumbnail when available and an illustrated cover otherwise. Failed recent opens offer removal. Version-gated markdown notes render bundled images, show a release link/fallback, and respect per-version dismissal. `qtStartScreenHasRecentDrawingSurfaceAndVersionGatedWhatsNew` covers Start surfaces, view switching, signed-in Connect, and the version gate. Start screen option B and What's New option B were selected and applied for [#881](https://github.com/chetjones003/GoSurvey/issues/881). |
| Software update check, update dialog and Updates settings (REQ-077, REQ-078; [#882](https://github.com/chetjones003/GoSurvey/issues/882)) | Startup check (before the sign-in gate) → modal Software Update dialog; logo menu → View → Settings → Updates | **Verified** | Shared `update::` service, so the decision logic (version ordering, skip, hash check) is the 0.7.4 code. Qt tests cover: offer shows both versions and cannot be dismissed without a button; forward-only / breaking / undeclared compatibility notices, shown above the notes; Remind Me Later writes nothing; Skip persists only that version; download failure is shown with its reason; progress and Cancel return to the offer; Install and Restart goes through the unsaved-changes prompt, Cancel cancels the update, and an installer that cannot start keeps the app open; a check that finds nothing, fails, or is disabled shows nothing and makes no request; the offer appears before the sign-in gate and the launch waits for the check's answer; a declined elevation prompt offers Install again rather than a second download; Settings → Updates persists, reverts on a failed save, and clears a skip. Selected design: dialog C (navy banner), settings A (one page). **Not covered by automation:** a live manifest fetch, a successful download+hash and the real elevated installer launch, which need a published release and a UAC prompt. On a declined or failed installer launch Qt stays open and shows the dialog again (the ImGui build exits). |
| Usage ping, Settings privacy notice, crash and startup-failure reports (REQ-080, REQ-424; [#883](https://github.com/chetjones003/GoSurvey/issues/883)) | Ping after the startup sign-in gate; logo menu → View → Settings → "What GoSurvey sends"; crash → relaunched `GoSurveyQt.exe --gosurvey-crash-report=` report window; launch-time OpenGL check → "GoSurvey could not start" report window | **Verified** | Shared `telemetry::`, `crashReporter::`, `startupFailure::` and `failureReportUi::` code, so payloads, logs, endpoints and the GitHub link are the 0.7.4 code. Qt tests cover: the ping waits for the gate, fires once, carries the email only when signed in, goes through the real opt-out launch path, and is never sent by a window the entry point did not enable; the Settings notice says a ping each launch (REQ-080 as amended, not 0.7.4's stale "once a day"), follows sign-in state, has no toggle (ADR-032), and discloses crash/startup reports (REQ-424 §6, D-2026-10-17-a); the report window is read-only, Send This Report copies the report, opens the GitHub new-issue link with title and body, reports each outcome and stays open, Close sends nothing more; long reports are cut to fit the link; a crash bundle reads back and is deleted, a malformed one is rejected and deleted. The launch graphics check passes on a working driver. Selected design: report window B (navy banner), Settings B ("What GoSurvey sends" card). **Not covered by automation:** a real crash relaunch (`GoSurveyQt.exe --gosurvey-test-crash`, checked by hand), a real graphics-driver failure, and the live POSTs. |

## Cross-cutting behavior gates

| Behavior | Current Qt evidence | Status |
|---|---|---|
| Cancel without changing existing traverse/session state | Malformed load preserves current session; there is no focused chooser-cancel or multi-import cancellation test | **Implemented** — import cancellation/merge follow-up: [#913](https://github.com/chetjones003/GoSurvey/issues/913) |
| Validation errors remain visible and do not replace valid state | Load errors are surfaced by a warning dialog; malformed-load test checks session preservation | **Verified** for `.gstrav` load; **Implemented** for importers/settings |
| Failed solve or rejected adjustment does not mutate drawing | `invalidAdjustmentDoesNotMutateDrawing`; asynchronous solve/accept/commit test | **Verified** |
| Undo after committing accepted adjustment | `asynchronousSolveAcceptanceCommitAndUndo` | **Verified** |
| Read-only project behavior for adjustment workflows | No focused Qt adjustment test | **Implemented** — verify with [#912](https://github.com/chetjones003/GoSurvey/issues/912) |
| Close with unsaved traverse changes offers cancellation | `unsavedAdjustmentCanCancelClosing` | **Verified** |
| Save, reopen, project tracking, and report export as one user workflow | Session save/load is covered; project tracking and report export are not | **Implemented** — complete under [#912](https://github.com/chetjones003/GoSurvey/issues/912) and [#915](https://github.com/chetjones003/GoSurvey/issues/915) |

## Command and dialog route audit

| User entry point | Qt route | Surface and current state |
|---|---|---|
| Traverse | Home and Survey ribbon → `TRAVERSE` | Opens `AdjustmentWindow`; not just a shared legacy route. |
| Least-squares adjustment | Command entry → `LSA` / `TRAVERSE` | Opens the same window. Calculate, Accept, Commit, Save, Import, settings, and report actions are present. |
| Observation-file import | Adjustment toolbar/File → Open / Import | Native picker accepts `.fbk`, `.dat`, `.rw5`, `.jxl`/`.xml`, `.gsdat`, and `.gstrav`; merge and cancellation are not verified. |
| Point-cloud attach | Insert → Point Cloud → Attach | Disabled placeholder; no Qt dialog or route. |
| Account/sign-in | Startup gate and logo menu → View → Settings → Account | Native startup choice, busy/error display, saved opt-out, account identity and sign-out adapter; focused UI coverage exists. |

## Implementation order

1. Add a native point-cloud attach/import workflow with format selection,
   validation, cancellation, and failure preservation (REQ-172).
2. Add the active-drawing scale resolver and user control required by REQ-401.
5. Add Qt workflow tests for settings validation/cancel/persistence, report
   export, project tracking, read-only handling, and imported GPS/network
   solve/commit.

Issue links above identify the implementation work items in the parent tracker
for each remaining gap.
