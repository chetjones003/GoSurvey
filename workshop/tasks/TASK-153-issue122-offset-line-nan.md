# TASK-153 — LINE must refuse a finite-but-unstorable point so OFFSET cannot write NaN

- Type:    bug
- Status:  self-verify
- Opened:  2026-08-31
- Owner:   chetjones003

## 1. Authority
- Goal:         GOAL — robust, corruption-free document state
- Requirements: REQ-204 (`finite-coords` invariant), REQ-201 (no silent failure), REQ-101
- Constraints:  CON-07 (artifacts only under build/)
- Acceptance (REQ-204): after every driven step the `finite-coords` invariant holds — no coordinate
  is NaN or infinite; a failing run arrives as a minimized reproducer that runs standalone under the
  REQ-203 driver. (REQ-201): the refusal is reported, not swallowed.
- Owning subsystem: Commands — `SubmitLineVertex` (LINE commit) and `CommitOffsetLine` (OFFSET).

## 2. Scope
- In scope: the LINE vertex-commit path and the OFFSET line-copy commit path.
- Out of scope: POLYLINE / RECT / ARC / ELLIPSE commit guards (a finite-but-huge magnitude there did
  not reproduce — TASK-065 §2 — and the entry-time `@dx,dy` overflow is already guarded); the
  mid-command origin-rebase interaction (a symptom here, not reachable once LINE refuses the point).
- Smallest change: one commit-site magnitude guard on LINE, mirroring CIRCLE's non-finite refusal,
  plus a defensive non-finite check on the OFFSET line result.

## 3. Architectural boundary check
- [x] No — two local validation guards in the owning subsystem, mirroring the existing CIRCLE guard
  (issue #59). New named constant `kMaxStorableCoordinateMagnitude` in `CadCoordinateFrame.hpp`
  alongside the existing `kMaxEstablishableOriginMagnitude`; no new abstraction, dependency, or
  data-format change.

## 5. Assumptions
```
ASSUMPTION-1: A coordinate whose magnitude exceeds ~1e18 is invalid input, not a value to rebase.
- Because: `float` `x*x` overflows FLT_MAX once |x| > ~1.8e19, so no downstream geometry math
  (OFFSET side test, snap, length) can stay finite; a value that large is a typo, not an easting.
- Risk if wrong: a user with a genuine >1e18 coordinate is refused. No projected system approaches
  this; REQ-204's own hostile distribution tops out at 1e12, which stays allowed (and is rebased on
  load by REQ-079). 1e18 leaves 6 orders of headroom above that and 1 below the overflow point.
- Validate by: issue #122 acceptance (finite-coords holds); regression-61 (1e12 easting still
  normalizes, not refused) stays green.
```

## 6. Plan
- Approach: in `SubmitLineVertex`, before either phase stores the point, reject a vertex that is
  non-finite or whose |x|/|y| exceeds `kMaxStorableCoordinateMagnitude`; log `LINE rejected — ...`
  and return false (command stays active on its current phase). Independently, in `CommitOffsetLine`,
  after computing the four offset endpoints, reject the copy if any is non-finite — mirrors
  `CommitCircle`'s guard and directly satisfies "OFFSET must not write NaN into `userLinesFlat`".
- Files touched: `src/commands/CadCoordinateFrame.hpp` (constant),
  `src/commands/CadCommands.cpp` (two guards),
  `tests/headless/transcripts/regression-122-offset-line-nan.txt` (new).
- Test approach: failure mode = the fuzzer's minimized seed-595175 reproducer places nothing, logs
  the refusal, and a save/open round trip stays finite (`CHECK ALL`). Happy path = full corpus +
  unit suite stay green, in particular regression-61/61a (large-but-legitimate coordinate still
  rebased, not refused) and regression-59b (relative-overflow message unchanged).
- Steps:
  - [x] add `kMaxStorableCoordinateMagnitude` (1e18) with rationale
  - [x] guard `SubmitLineVertex`
  - [x] guard `CommitOffsetLine`
  - [x] add regression transcript (fuzzer reproducer, verbatim)
  - [x] build + full ctest (849/849)

## 7. Notes
- Root cause: `1e+38` is finite (< FLT_MAX) so LINE accepted it as the first point; it is above
  `kMaxEstablishableOriginMagnitude` so no origin frame formed around it, leaving it stored as a
  local coordinate near 1e38. The typed `1e+07` second point *did* clear the establishment band, so
  `MaybeEstablishDocumentOriginFromTypedPoint` rebased the document origin mid-LINE — the in-flight
  anchor is not shifted by `ShiftAllStorageBy`, so the committed segment kept its ~1e38 endpoint.
  OFFSET's signed-side projection then computed `dx*dx` (~1e76) → `inf` → `t = inf/inf` → NaN,
  written into `userLinesFlat[6]`. Refusing the unstorable point at the LINE commit (as `CommitCircle`
  refuses a non-finite derived radius) removes the whole chain; the OFFSET guard is defence in depth
  for the same class arriving from crafted geometry.

## COMPLETION REPORT — TASK-153 — 2026-08-31
- Requirements satisfied:  REQ-204 (Acceptance met: yes), REQ-201, REQ-101
- Summary:                 LINE refuses a non-finite or >1e18 vertex and reports it; OFFSET refuses a
                           non-finite line copy; neither stores anything on refusal.
- Tests:                   headless.regression-122-offset-line-nan (refusal + clean round trip);
                           full corpus + unit suite 849/849 green (regression-61/61a, 59b unaffected)
- Verification verdict:    PASS (self-run build-project + testing; no blocking findings)
- Assumptions:             ASSUMPTION-1 (validated against issue #122 acceptance and regression-61)
- Architectural decisions: none made by Workshop
- Dependencies:            none
- Technical debt noted:    POLYLINE/RECT/ARC/ELLIPSE commit paths still lack an equivalent magnitude
                           guard; not reproduced (TASK-065 §2), tracked as the same follow-up class.
- Build:                   reproducible, clean (MSVC/Ninja release)
- Docs updated:            none required
