# TASK-885 — Qt drawing-template workflow

- Type: feature
- Status: complete
- Opened: 2026-10-17
- Owner: Codex

## 1. Authority

- Goal: Qt migration parity, GitHub issue #878.
- Requirements: REQ-424 (accepted 2026-10-17), REQ-055, REQ-175, REQ-308, REQ-374.
- Constraints: preserve the 0.7.4 drawing behavior; keep the startup workspace template separate; no new dependency; Qt remains additive.
- Acceptance: REQ-424 acceptance criteria, plus issue #885's requirement to show 2–3 integrated screenshot options for each major affected surface and apply the user's selection before completion.
- Owning subsystem: Qt UI (`src/qt/`); shared `.gst` IO (`src/io/GsIo`).

## 2. Scope

- In scope: discover bundled and per-user `.gst` drawing templates, present a usable Qt chooser, create a fresh drawing from a selected template or Blank Drawing, preserve cancellation and failure state, honor active-project defaults, add focused Qt workflow coverage, update the 0.7.4 parity ledger.
- Out of scope: managing/editing templates, changing `.gst` format, changing startup workspace loading, ImGui migration/removal, production-default switch.
- Smallest change: a focused chooser and a small discovery helper using the existing `LoadGoSurveyTemplateFile` path; no generic template framework.

## 3. Architectural boundary check

- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
  - [x] No — proceed within the existing Qt UI and `.gst` IO boundaries.
  - [ ] Yes → STOP.

## 4. Questions

| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Should the picker reuse startup `.gst` or use a separate library? | 2026-10-17 | Separate library. Bundled `resources/drawing-templates/` plus per-user `<UserDataDirectory>/drawing-templates/`; Blank Drawing remains available. |

## 5. Assumptions

None. REQ-424 defines the library locations, project behavior, cancellation, and failure semantics.

## 6. Plan

- Approach: make discovery deterministic and read-only; load a chosen template into a temporary fresh document state before adding/focusing a tab; use the existing project-default path when the active drawing belongs to a project.
- Files/functions to touch: `src/qt/EditorWindow.*`, focused `tests/QtEditorTests.cpp`, `docs/qt-074-parity-ledger.md`, and any narrowly required `src/platform/AppPaths.*` helper.
- Test approach: `[req424]` chooser discovery/order/duplicate names; valid template and byte-identical source; blank and cancel; malformed template failure preserving current state; active-project defaults and read-only library behavior.
- Steps:
  - [x] Verify the plan against REQ-424, REQ-374, and current Qt tab/document ownership. Qt's `NewDrawing` already creates an empty tab; it will be extended to inherit the active project's UID so `RefreshProjects` applies the established defaults. Open Project's explicit UID path remains intact.
  - [x] Implement discovery, chooser, and rollback-safe new-tab creation.
  - [x] Add focused Qt workflow coverage. Both focused workflow tests pass; the full `qt.editor` suite timed out at 280 seconds during an earlier software-update test before reaching these cases.
  - [x] Update parity ledger with the implemented behavior and focused evidence.
  - [x] Capture three integrated screenshot options for the chooser; user selected option B (tile cards). Apply the selected direction and capture final chooser, selected-template, and invalid-template evidence under `build/qt-885-template-*`.

## 7. Workflow-specific notes

- Feature: scope and source-location decision resolved; test the failure-preservation path before considering implementation complete.

## 8. Verification and completion report

- Build: `cmake --build --preset ninja-release --target GoSurveyQt GoSurveyQtTests` — PASS.
- Focused Qt tests: `qtDrawingTemplateWorkflowDiscoversCreatesCancelsAndPreservesFailure`, `qtNewDrawingInProjectUsesDefaultsWithoutTemplateChooser`, and `qtDrawingTemplateScreenshotEvidenceForIssue885` — PASS.
- Full `qt.editor` suite: timed out at the 280-second limit in the existing software-update test `updateDownloadShowsProgressAndCancelReturnsToTheOffer`; no failure in the new tests. The two workflow tests were separately run and passed.
- Diff hygiene: `git diff --check` — PASS.
- Visual review: approved option B; final screenshots captured and shown to the user.
- Assumptions: none beyond accepted REQ-424 and D-2026-10-17-a.

## 9. Technical debt

None anticipated.
