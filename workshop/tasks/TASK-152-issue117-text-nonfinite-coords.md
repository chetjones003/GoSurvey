# TASK-152 — Reject non-finite / extent-overflowing TEXT so no `.gs` stores inf

- Type:    bug
- Status:  self-verify
- Opened:  2026-08-31
- Owner:   chetjones003

## 1. Authority
- Goal:         GOAL — robust, corruption-free document state
- Requirements: REQ-204 (finite-coords invariant), REQ-201 (no silent failure), REQ-101
- Constraints:  CON-07 (artifacts only under build/)
- Acceptance (REQ-204): after every driven step the finite-coords invariant holds — no coordinate
  is NaN or infinite; a failing run arrives as a minimized reproducer that runs standalone under
  the REQ-203 driver. (REQ-201): the refusal is reported, not swallowed.
- Owning subsystem: Commands — `ProcessCommandLineSubmit` TEXT commit path / annotation insert.

## 2. Scope
- In scope: the model- and paper-space TEXT commit path in `ProcessCommandLineSubmit`.
- Out of scope: MTEXT, dimensions, the in-place text editor, DXF/DWG import paths, and the
  separate LINE `1e38` + OFFSET NaN hit (issue comment — filed separately).
- Smallest change: guard the committed annotation the way `CommitCircle` guards a derived radius.

## 3. Architectural boundary check
- [x] No — a local validation guard in the owning subsystem, mirroring the existing CIRCLE guard
  (issue #59). No new abstraction, dependency, or data-format change.

## 5. Assumptions
```
ASSUMPTION-1: A TEXT whose world extent (CadAnnotationRoughBounds) is non-finite is invalid input,
  not a case to clamp.
- Because: the issue's "Expected" says TEXT must refuse a bad height the way LINE/CIRCLE refuse bad
  coords; there is no spec'd clamp behaviour for annotation size.
- Risk if wrong: a user typing an enormous-but-representable height is refused instead of clamped.
- Validate by: issue #117 acceptance ("TEXT refuses non-finite insertion / height / rotation").
```

## 6. Plan
- Approach: in the `TextCmdPhase::WaitString` commit branch, compute the divided plotted height,
  elevation, and `CadAnnotationRoughBounds` for the candidate annotation; if the insertion,
  elevation, rotation, plotted height, or any rough-bound corner is non-finite (or the height is
  <= 0), log `TEXT rejected - ...`, reset the command, and commit nothing — before `PushUndoSnapshot`.
  Also reject a non-finite height / angle at their own entry prompts for a cleaner message.
- Files touched: `src/commands/CadCommands.cpp`;
  `tests/headless/transcripts/regression-117-text-nonfinite-coords.txt` (new).
- Test approach: happy path = existing TEXT transcripts stay green; failure mode = the fuzzer's
  minimized seed-437896 reproducer places nothing, logs the refusal, and a save/open round trip
  stays finite with the origin un-rebased.
- Steps:
  - [x] guard the commit path + entry prompts
  - [x] add regression transcript (fuzzer reproducer, verbatim)
  - [x] build + full ctest (849/849)

## 7. Notes
- Root cause: height `1e+38` is finite and passed every guard; `plottedHeightInches * mupi *
  0.55 * len` in `CadAnnotationRoughBounds` overflowed float to inf; `MaybeRebaseLargeCoordinates`
  on load read that inf extent and rebased `worldDocumentOrigin` to (-inf, inf), spreading inf to
  every coordinate. Checking the *derived* extent (not just the typed input) is the fix, exactly as
  `CommitCircle` checks the derived radius for issue #59.

## COMPLETION REPORT — TASK-152 — 2026-08-31
- Requirements satisfied:  REQ-204 (Acceptance met: yes), REQ-201, REQ-101
- Summary:                 TEXT commit refuses a non-finite or extent-overflowing insertion /
                           elevation / rotation / height; nothing is stored and the refusal is logged.
- Tests:                   headless.regression-117-text-nonfinite-coords (refusal + clean round trip);
                           full corpus + unit suite 849/849 green
- Verification verdict:    PASS (self-run build-project + testing; no blocking findings)
- Assumptions:             ASSUMPTION-1 (validated against issue #117 acceptance)
- Architectural decisions: none made by Workshop
- Dependencies:            none
- Technical debt noted:    none for this path. A finite-but-huge height that stays under FLT_MAX is
                           still accepted (produces a valid, if large, extent) — consistent with the
                           issue's non-finite scope.
- Build:                   reproducible, clean (MSVC/Ninja release)
- Docs updated:            none required
