# TASK-1106 — Explain the 0.7.5 install migration

- Type: feature
- Status: in review
- Opened: 2026-10-25
- Owner: Workshop

## 1. Authority
- Goal: M-Distribution
- Requirements: **REQ-078**, **REQ-454** (accepted), amended by **D-2026-10-25-f**
- Constraints: preserve REQ-078's explicit update choice and three outcomes; REQ-201 visible
  user-initiated errors; REQ-300 no unnecessary dependencies; no installer-path behavior changes.
- Acceptance: as stated in REQ-078 and REQ-454, including the 0.7.5 note and conditional
  Program Files migration notice above the update release notes.
- Owning subsystem: `src/update/` owns installation-path state; `src/qt/EditorUpdate.cpp` owns
  update-dialog presentation; `RELEASE_NOTES.md` supplies user-facing release and manifest notes.

## 2. Scope
- In scope: add the 0.7.5 migration note; show a prominent message before update notes whenever an
  update offer is shown from a legacy Program Files install; keep it hidden for per-user installs.
- Out of scope: changing the existing one-time migration bridge, UAC behavior, install destination,
  update consent, or installer/update execution.
- Smallest change: reuse the existing Program Files path detection and update dialog's pre-notes
  information area; pass one boolean on the existing `UpdateState` and add the release note.

## 3. Architectural boundary check
- New abstraction, dependency, layer, ownership change, global state, public API, or data format?
  - [x] No. Existing updater path detection remains authoritative; one derived UI state field is
    passed through the existing updater state object.

## 4. Questions
None. The user chose the 0.7.5 release notes plus a prominent update message, the one-time migration
bridge, and `%LOCALAPPDATA%` for all future installs.

## 5. Assumptions
None.

## 6. Plan
- Approach: expose the existing Program Files detection result on `UpdateState`; add a visibly
  distinct, word-wrapped migration message above the scrolling release notes when true; add a plain
  language 0.7.5 release note sourced by the existing release workflow.
- Files/functions to touch: `spec/project.md`, `spec/requirements.md`, this task, a plan review,
  `src/update/UpdateService.hpp/.cpp`, `src/qt/EditorUpdate.cpp`, updater/Qt tests,
  `resources/wiki/`, and `RELEASE_NOTES.md`.
- Test approach: reuse the same Program Files predicate used by the migration handoff; test that the
  update dialog shows the notice only for legacy installs and above the notes without changing its
  three choices; ensure release-note text is in the version section used by the release workflow.
  Capture the offer dialog's legacy and per-user states for approval.
- Steps:
  - [x] Record D-2026-10-25-f and amend REQ-078/REQ-454.
  - [x] Verify this plan against the accepted requirements and architecture.
  - [x] Implement the notice and 0.7.5 note.
  - [x] Build, run focused tests, and capture both UI states.
  - [x] Present screenshots for user approval before commit/PR.
  - [x] Review and update affected product documentation before PR.

## 7. Workflow-specific notes
- Feature request was confirmed in the conversation. Existing one-time migration and future per-user
  install behavior is already implemented under TASK-1044; this task covers the requested notice.
- Product documentation review for PR: README, PRIVACY.md, `resources/wiki/`, and the public manual
  wiki will be reviewed before opening the PR.

## 8. Implementation log
- 2026-10-25: Confirmed existing migration implementation and release-note pipeline; added the
  user-approved notice requirement before implementation.
- 2026-10-26: Added a conditional migration notice in the existing update offer and the 0.7.5
  release note. The notice uses the existing Program Files path predicate, appears before the
  release notes, and leaves the three update choices intact.
- 2026-10-26: `build.bat` passed. All 6 `qt.editor.shard-*` tests passed; the documented exact
  filter `^qt[.]editor$` matched no tests because this repository shards that suite, so the
  equivalent `^qt[.]editor` filter was used. `build.output-layout` passed; `git diff --check`
  passed. Captured the legacy and per-user offer states; the user approved the screenshots before
  PR preparation.
- 2026-10-26: Product documentation review before PR — README reviewed, no update needed because
  it already directs installers to the current `docs/install.md` guide; PRIVACY.md reviewed, no
  update needed because migration does not change data collection or transmission; in-app help
  wiki updated in FAQ, Getting Started, Files and Drawings, Settings and Options, and Troubleshooting to
  describe the per-user install path, migration, and actual `%APPDATA%` settings location; public
  GoSurvey wiki reviewed and the same affected guidance was updated and published in commit
  `945b4c54d3c423bce383c901ddbf746c7e222a08` and
  `d86ef2eb3b4fd00ee17038dcaaa20b3212697079`; `docs/install.md` reviewed and already describes the
  bootstrap and migration. No external documentation update remains pending.
- 2026-10-26: Opened [PR #1102](https://github.com/chetjones003/GoSurvey/pull/1102) against `beta`
  with `feature` and `needs review` labels. Required CI checks are running; merge and check review
  remain.

## 9. Self-verification
- [x] build-project — `build.bat` passed.
- [x] architecture-review — existing update-state ownership and Program Files predicate reused;
  no layer, dependency, or ownership changes.
- [x] code-review — self-review against the accepted notice and update-consent requirements found
  no correctness, standards, or scope findings. The implementation exposes only a boolean and
  renders static text; the action set is unchanged.
- [x] dependency-audit — n/a; no manifest or dependency changes.
- [x] performance-review — n/a; one path check during startup and no hot-path changes.
- [x] testing — all 6 Qt editor shards and the output-layout check passed.

## 10. Verification result
- Plan submitted: 2026-10-25
- Verdict: PASS — see `verification/reviews/TASK-1106-plan-review.md`.

## 11. Outcome
- Requirements satisfied: REQ-078 and REQ-454 migration-notice and 0.7.5 release-note criteria.
- Tests added: Qt editor assertions for legacy and per-user offers; assertions cover message,
  placement, visibility, and unchanged actions.
- Docs updated: `RELEASE_NOTES.md`, four in-app wiki pages, four public wiki pages; README,
  PRIVACY.md, and `docs/install.md` reviewed with no changes required.
- Done: implementation, verification, documentation review, screenshot approval, and PR creation
  complete; required CI and PR review remain.
