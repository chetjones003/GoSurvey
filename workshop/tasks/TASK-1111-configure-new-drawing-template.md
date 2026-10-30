# TASK-1111 — Configure the default new-drawing template

- Type: feature
- Status: self-verify
- Opened: 2026-10-30
- Owner: Codex

## 1. Authority
- Goal: Qt migration parity (REQ-424; no numbered GOAL is defined for this behavior).
- Requirements: REQ-424 (accepted, amended by D-2026-10-30-a), REQ-449, REQ-374, REQ-175.
- Constraints: preserve project drawing defaults; preserve template files; use existing `.gst` IO and `gosurvey-user.json`; no new dependency.
- Acceptance:
  - Options lets users configure, save, clear, and restore the default drawing-template path.
  - Standalone New Drawing uses the configured template, or bundled `resources/default-template.gst` when none is configured, without showing a chooser.
  - A configured template load failure reports the error and preserves the active drawing without adding a tab.
  - New project drawings retain REQ-374 defaults and do not use the standalone template.
- Owning subsystem: Qt UI (`src/qt/`), user preference persistence (`src/io/UserPrefs`), template loading (`src/io/GsIo`).

## 2. Scope
- In scope: replace the template chooser with direct template-based creation; add a default-template control to application Options; persist the setting; retain project behavior; update docs and parity ledger.
- Out of scope: startup workspace template behavior; editing/managing templates; changing `.gst`; changing project defaults.
- Smallest change: one persisted path preference, one Options control using the existing file picker and `.gst` loader, and direct New Drawing dispatch.

## 3. Architectural boundary check
- No new dependency, abstraction, layer, ownership model, public API, data format, or global state. Reuses REQ-449's Options persistence model and the existing `.gst` loader.

## 4. Questions
None. The user explicitly directed the behavior and D-2026-10-30-a records how it amends REQ-424.

## 5. Assumptions
None.

## 6. Plan
- Approach: persist a standalone default-template path separately from the startup template; present a browse/clear control in Options; route standalone New Drawing to the configured file or bundled default; keep project UID creation unchanged.
- Files/functions to touch: `src/commands/CadCommands.hpp`, `src/io/UserPrefs.cpp`, `src/qt/EditorDialogs.cpp`, `src/qt/EditorWindow.cpp`, `src/qt/EditorWindow.hpp`, focused `tests/QtEditorTests.cpp`, REQ-424 documentation/parity ledger and product docs.
- Test approach: verify path staging/persistence/reset, configured and bundled template creation, load failure rollback, project defaults, and absence of chooser.
- Steps:
  - [x] Amend REQ-424 and record user decision D-2026-10-30-a.
  - [x] Verify ownership and plan against REQ-424, REQ-449, REQ-374, current UI and preference code.
  - [x] Implement the preference, Options control, and direct creation path.
  - [x] Update parity ledger and affected in-app product docs; README.md and PRIVACY.md reviewed and no change needed. Updated the public GoSurvey Wiki pages `Files-and-Drawings.md` and `Settings-and-Options.md` (commit `eea40bb`).
  - [ ] Complete focused behavioral verification and the Qt visual comparison evidence.

## 7. Workflow-specific notes
- Feature: request fields are fully supplied by the user's instruction. Verification approves the plan against the amended accepted requirement above.

## 8. Implementation log
- 2026-10-30 — Spec amended before implementation; plan verified against the existing Qt UI, preferences, `.gst` loading, and project drawing path.
- 2026-10-30 — Implemented Options path persistence, direct standalone template creation, bundled fallback, and chooser removal. Updated in-app wiki and parity ledger.
- 2026-10-30 — Updated the public GoSurvey Wiki pages `Files-and-Drawings.md` and `Settings-and-Options.md`, commit `eea40bb`.
- 2026-10-30 — `build.bat` PASS (Release, all targets built). The first build attempt caught an `addHeading` declaration-order compile error; replaced that call with a local heading widget and rebuilt successfully. `git diff --check` PASS. Tests were not run. Required screenshots and the three-reviewer Civil 3D comparison remain pending.

## 9. Self-verification
- Build: `build.bat` Release PASS.
- Tests: not run.
- Qt screenshot comparison: pending; no comparison result claimed.
- Diff hygiene: `git diff --check` PASS.

## 10. Verification result
- Submitted: 2026-10-30
- Verdict: pending verification
- Findings: focused behavioral tests and required Qt visual comparison not completed.

## 11. Outcome
- Requirements satisfied: implementation follows amended REQ-424; verification pending.
- Tests: updated existing Qt template workflow and screenshot cases; not run.
- Refactors: none.
- Docs updated: `docs/qt-074-parity-ledger.md`, `resources/wiki/Files-and-Drawings.md`, `resources/wiki/Getting-Started.md`, `resources/wiki/User-Interface.md`.
- Done: pending verification.
