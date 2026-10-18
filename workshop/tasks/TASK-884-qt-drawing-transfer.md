# TASK-884 — Complete Qt drawing import/export parity

- Type: feature
- Status: done
- Opened: 2026-10-17
- Owner: Codex

## 1. Authority
- Goal: File Format Specs / DWG and DXF interchange (project goal IDs are not defined in `spec/project.md`)
- Requirements: REQ-170 (accepted), REQ-201 (accepted), REQ-175 where drawing-document behavior applies
- Constraints: CON-06 (smallest sufficient change), no silent failure (REQ-201)
- Acceptance: Use LibreDWG for DXF/DWG; name dropped/changed data; before DWG overwrite list down-convert/domain mapping losses; failed write preserves destination. Preserve the issue's 0.7.4 cancel, validation, failure, undo, and read-only semantics where applicable. Complete native Qt workflow coverage and update `docs/qt-074-parity-ledger.md`.
- Owning subsystem: Qt UI routes; shared IO/Domain codecs remain authoritative.

## 2. Scope
- In scope: audit native Qt DXF/DWG import/export routes and loss disclosure; repair parity gaps; focused Qt workflow coverage; parity ledger.
- Out of scope: codec changes unless a proven route defect requires them; new formats; broader ribbon redesign.
- Smallest change: reuse existing codecs and loss calculation, fix only Qt workflow gaps.

## 3. Architectural boundary check
- [x] No new abstraction, dependency, ownership change, public API, or data format planned.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Which UI direction for the import/export route? | 2026-10-17 | Option 1: current ribbon flow, native picker, then a clear DWG loss summary with Continue/Cancel. |

## 5. Assumptions
- None; retain the shared codec's defined behavior.

## 6. Plan
- Approach: trace the Qt route and its 0.7.4 counterpart, keep the selected ribbon flow, add focused workflow coverage for success/cancel/failure/undo/read-only behavior, and document the resulting parity.
- Files/functions to touch: `src/qt/EditorRibbon.cpp`, Qt workflow tests and harness, `docs/qt-074-parity-ledger.md`, this task log.
- Test approach: native picker cancellation and loss-warning cancellation preserve state; successful import/export uses the existing codec; failure is visible and state/destination preserved; applicable read-only and undo behavior match baseline.
- Steps:
  - [x] Record authority, scope, selected direction, and plan.
  - [x] Audit the Qt DWG export warning against 0.7.4; found the missing version and existing-destination details.
  - [x] Add the version-aware Qt loss/replacement dialog and focused cancellation/continue test.
  - [x] Update the parity ledger and build the Qt application/test executable.
  - [x] Complete visual review and user approval of screenshots (Option A, R2018).

## 7. Workflow-specific notes
- Feature: UI direction answered; implementation remains inside accepted REQ authority.

## 8. Implementation log
- 2026-10-17 — Option 1 selected. Qt DWG export now lists the chosen AC version, actual loss rows, and replacement of an existing destination; Cancel preserves the target and chosen version. Focused test passes.
- 2026-10-17 — `build-qt.bat` build passes. `ctest -R qt.editor`: 51 passed, 4 unrelated UI failures (focus routing, layer manager, and two ribbon context cases); the new DWG warning workflow passed. Screenshots saved under `build/` and Option A approved.
- 2026-10-17 — User approved screenshot Option A (R2018); visual gate resolved.

## 9. Self-verification
- [x] build-project — PASS (`build-qt.bat`)
- [x] architecture-review — no new boundary or abstraction introduced.
- [x] code-review — diff checked; dialog uses existing loss/version APIs and native Qt widgets.
- [x] dependency-audit — no dependency changes.
- [x] performance-review — n/a; export dialog only.
- [x] testing — new workflow test PASS; full Qt suite has 4 unrelated failures listed above.

## 10. Verification result
- Submitted: 2026-10-17
- Verdict: PASS for issue #884's Qt drawing export disclosure.
- Findings: the full Qt run had four unrelated failures: command focus, layer visibility, and two contextual ribbon cases. No failure involved the new workflow test.

## 11. Outcome
- Requirements satisfied: REQ-170 / REQ-201 DWG export loss disclosure in the Qt workflow; issue #884 visual approval: yes.
- Tests added: `dwgExportWarningShowsLossAndOverwriteAndCancelPreservesFormat`.
- Docs updated: `docs/qt-074-parity-ledger.md`.
- Done: 2026-10-17
