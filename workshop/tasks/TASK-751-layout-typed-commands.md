# TASK-751 — Typed command-line equivalents for layout management (REQ-401, issue #751/#753 V3)

- Type:    feature
- Status:  in progress
- Opened:  2026-10-09
- Owner:   workshop

## 1. Authority  (fill BEFORE planning — incomplete = not ready)
- Goal:         Paper-space parity with model-space command-line driving (issue #751 audit, tracked in #753)
- Requirements: REQ-401 (D-2026-10-09-b), REQ-025 (layout add/rename/delete via UI), REQ-027
  (viewport scale)
- Constraints:  CON-NN as applicable; no new behavior beyond what the existing UI already does;
  MVSETUP and VPCLIP are explicitly out of scope (REQ-401)
- Acceptance: see REQ-401's Acceptance clause
- Owning subsystem: Commands (`CadCommands.cpp`), UI (Page Setup dialog reuse)

## 2. Scope
- In scope:
  - `LAYOUT` command: `[New/Rename/Delete]` sub-keyword prompt, driving the existing
    add/rename/delete layout functions.
  - `PAGESETUP` command: opens the existing Page Setup dialog for the active layout; refuses in
    Model space.
  - `VPSCALE` command: prompts for a scale, applies it to the resolved current/selected viewport;
    refuses clearly if none.
  - Tests for each command's happy path and refusal path.
- Out of scope:
  - `MVSETUP`, `VPCLIP` (REQ-401 decision).
  - Any change to the tab-bar UI itself, the Page Setup dialog's fields, or viewport-scale
    resolution semantics beyond what the existing UI control already does.
  - Fixing L1-L3 (layout delete state bugs, #763) — this task must not regress them, but fixing
    them is separate.

## 3. Implementation approach
- Reuse the existing layout add/rename/delete functions and Page Setup dialog opener found during
  research; wire three new entries into the command registry following the pattern used for
  MVIEW/MSPACE/PSPACE/VPFREEZE/VPTHAW.
- `LAYOUT`'s sub-keyword prompt follows the same typed-keyword pattern as other multi-step commands
  (e.g. PEDIT's `[Close/Open/Width]`).
- `VPSCALE` reuses whatever "current viewport" resolution the existing scale-setting UI control
  already uses, so behavior matches exactly.

## 4. Test approach
- Headless/unit tests per REQ-401 acceptance: New/Rename/Delete each produce the same end state as
  the existing UI path; PAGESETUP opens prefilled and refuses in Model space; VPSCALE applies to
  the resolved viewport and refuses with no viewport.

## 5. Architectural-boundary check
- No new architectural surface: commands call into existing layout/dialog/viewport logic only.

## 6. Verification
- build-project, code-review; run new + existing paper-space tests.
