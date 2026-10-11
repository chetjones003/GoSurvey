# TASK-121 — The DXF header's view is derived from the extents as written (#98)

- Type:    bug
- Status:  review — fixed, red-before/green-after, full suite green.
- Opened:  2026-08-26
- Owner:   Nathan Johnson

Upstream issue: chetjones003/GoSurvey#98, filed 2026-08-26 by chetjones003. It is the residual
TASK-116 named as **DEBT-1** while fixing #94, and re-found independently during review of PR #97.

> **This is the second half of the `dxf-export-stable` story.** #94 removed an unbounded ratchet;
> this removes the one-cycle residual it left behind. The oracle now holds on the FIRST cycle for a
> drawing whose coordinates are not exact at six decimals, which is strictly more than REQ-079
> promises for `.gs`.

## 1. Authority
- Requirements: **REQ-204** (accepted 2026-08-16) — invariant row *"DXF export → import → export is
  stable"*. Same authority as TASK-107, TASK-114 and TASK-116.
- **No spec change.** That is the substance of the task, not a footnote: issue #98 explicitly
  offered amending REQ-204 as direction 1, and it was declined. See §4 Q1 and D-2026-08-26-h.
- Also honoured: REQ-101 (the residual was 1e-6, well inside tolerance — which is exactly why no
  geometry test could ever have caught it); REQ-200/CON-07 (the transcript writes only under the
  build tree).
- Owning subsystem: `io` (`src/io/DxfIo.cpp`, export header). Nothing else is touched.

## 2. Scope
- In scope: the derivation of the `$VPORT` `*Active` record's view (groups 12/22/40/41) from the
  drawing extents, in the DXF export path.
- Out of scope:
  - the six-decimal write precision itself — `std::to_string` stays. Widening it would rewrite every
    coordinate in every exported file and every golden DXF fixture, to fix a defect that does not
    need it;
  - **ARC groups 50/51**, a different residual of the same family found while stress-testing this
    fix. It is pre-existing, unchanged by this work, and filed separately as **#111** — see §12 DEBT-1;
  - DEBT-2 from TASK-116 (the prescan's own `> 10000.0` test), still open and still untouched.
- Smallest change: six assignments and one lambda, ahead of the six lines that already derived the
  view. No new constant, no new function in any header, no signature change.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No.** The lambda is local to the function that already owns these values. The file's
          on-disk shape does not change — the same groups are written, at the same precision, in the
          same order; only the *number* in group 40 changes, and only for drawings where it was
          already unstable. A file exported before this change still imports identically.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Issue #98 offers two directions and says they are "not a decision": amend REQ-204 with a `.gs`-style idempotence carve-out (1), or fix the root cause (2). Which? | not asked — resolved from CLAUDE.md | **(2).** Asking would have been right if this were REQ-079's situation, where the *requirement* was asking the format to promise something the precision design contradicts, and only the user could weigh a weaker spec against a rewritten storage path. It is not. Nothing here is normalizing the user's drawing; the exporter derived a value from more precision than it wrote down, and `* 1.1 + 1.0` amplified the difference past a rounding boundary. Amending REQ-204 for that would be the Workshop editing the Specification to excuse code — the exact move the SPEC GAP rule exists to prevent. Recorded anyway as **D-2026-08-26-h**, because the issue's AC-1 asks for the choice to be visible either way. |
| Q2 | Does fixing this need the write precision widened, as DEBT-1's "remove by" note suggested? | 2026-08-26 | **No** — the note was aiming one step too far. `$EXTMIN`/`$EXTMAX` were already byte-identical across the cycle; only the values *derived* from them were not. Snapping the extents to the precision they are written at, before deriving, is sufficient and touches nothing else. |

## 5. Assumptions
```
ASSUMPTION-1: `std::stod(std::to_string(v))` is the value the file will state.
- Because:       every number in the header goes through `std::to_string`. Round-tripping the double
                 through that same function is the definition of "as written" rather than an
                 approximation of it — which is why it is used in preference to `round(v*1e6)/1e6`,
                 whose product loses precision above ~9e9 and whose tie-breaking is not printf's.
- Risk if wrong: the snap disagrees with the written text and the derivation drifts again.
- Validate by:   `regression-98-dxf-view-precision.txt` — red before, green after, on the exact
                 reproducer from the issue.

ASSUMPTION-2: Extents are finite when the snap runs.
- Because:       #59 (TASK-065) refuses non-finite coordinates at entry, and REQ-204 carries a
                 "no coordinate is NaN or infinite" invariant checked after every step.
- Risk if wrong: `std::stod("nan")` throws and export dies instead of writing a file.
- Handled:       not relied on — the lambda passes a non-finite value through untouched, so the
                 worst case is the pre-existing behaviour rather than an exception escaping the
                 exporter. One `std::isfinite` call; cheaper than reasoning about whether it can
                 happen.
```

## 6. Plan
1. Reproduce the issue's transcript against a clean build; confirm B == C and A != B, and confirm by
   diff that exactly one line differs.
2. Establish *which* value is unstable and why — entity text, then `$EXTMIN`/`$EXTMAX`, then the
   derived view — rather than assuming the issue's framing.
3. Snap the six extent values to the write grid before the view is derived from them.
4. Add the reproducer as a regression transcript asserting the **first** cycle (the issue's AC-3),
   keeping the second-vs-third comparison as the convergence check.
5. Record the direction taken in the decision log (AC-1).
6. Stress the fix past the reproducer, with mixed entity types and untidy coordinates, to find out
   what it does *not* fix.
7. Full ctest suite; confirm `regression-94*` and `dxf-export-stable` still pass (AC-4, AC-5).

## 7. Workflow-specific notes
Step 6 was not optional bonus work. The issue's reproducer is one LINE command, and a fix validated
only against it would be indistinguishable from one that happened to move the rounding boundary. It
is what turned up the ARC residual in §12, which is now filed rather than discovered by someone else
in six months.

## 8. Implementation log
- 2026-08-26 — **Reproduced.** `probe-a` vs `probe-b` differ at byte 4428, both 7154 bytes; `diff`
  shows exactly one differing line, `124.603335` vs `124.603334`. `probe-b` == `probe-c`. Matches
  the issue exactly.
- 2026-08-26 — **Located.** Three measurements, in order:
  1. The LINE's group 10/20/11/21 text is **identical** in both files (`0.000002`, `100.333336`),
     so the entity half of the file is already stable.
  2. `$EXTMIN`/`$EXTMAX` are **identical** in both files (`-6.016665`, `106.350003`).
  3. Only the derived view moved. `vViewH = max(1.0, max(vW, vHspan) * 1.1 + 1.0)`, and
     `(124.603335 - 1) / 1.1 = 112.3666681…` against `112.36666…` — a sub-1e-6 difference in the
     span, amplified by 1.1 into a different sixth decimal.

  The chain: `0.0000015` is written as `0.000002` because six decimals is all `std::to_string`
  gives. Below about 16 the `float` spacing is *finer* than 1e-6, so that text does not identify the
  float it came from, and the re-imported value is genuinely a different float — 2e-6 rather than
  1.5e-6. Above 16 the spacing is coarser than 1e-6 and the text does identify its float, which is
  why every ordinary drawing in the corpus was stable and this one is not.
- 2026-08-26 — **Fixed.** Six extent values snapped through `std::to_string` / `std::stod` before
  `vCx`/`vCy`/`vW`/`vHspan`/`vViewH`/`vAsp` are computed. Z is snapped too, though nothing derives
  from it, so the rule "these variables hold what the file states" holds for all six rather than for
  four of them. The snap is idempotent, so emitting from the snapped values leaves `$EXTMIN` and
  `$EXTMAX` byte-for-byte what they were.
- 2026-08-26 — **Red before, green after.** The regression transcript fails on the unfixed build at
  step 14, byte 4428, 7154 vs 7154 — the same bytes the issue reports — and passes on the fixed one.
  Checked by stashing the source change and rebuilding, not by reasoning about it.
- 2026-08-26 — **Stressed.** A five-entity drawing (2 lines, circle, 3-point arc, 3-vertex polyline)
  with every coordinate deliberately untidy, run to four cycles. Before the fix: three differing
  lines — `$VPORT` group 12 (`50.277779` → `50.277780`) and ARC groups 50/51. After: the group 12
  difference is gone, the ARC one remains. See §12.

## 9. Self-verification
- `build-project`: clean build, MSVC, ninja-release. No new warning in `DxfIo.cpp`.
- `architecture-review`: no boundary crossed — one function in the subsystem that owns the file
  format; nothing added to a header; `io` gains no dependency.
- `code-review`: six assignments and a lambda, placed immediately before their only consumers.
  Reviewed against rule 1 (simple), rule 2 (no abstraction — the lambda does not escape the
  function), rule 4 (the comment states the mechanism and the alternative that was declined, so the
  next reader does not have to re-derive either).
- `dependency-audit`: none added. `<cmath>` and `<string>` were already included.
- `performance-review`: six `to_string`/`stod` pairs per exported file. Not measurable against a
  file write.
- `testing`: below.

## 10. Verification result
- Suite:     `ctest` — **635/635 passed**, 19.51 s. Nothing disabled; the DISABLED list in
             `CMakeLists.txt` is still empty.
- Red/green: `headless.regression-98-dxf-view-precision` fails on the unfixed build and passes on
             the fixed one, at the byte the issue names.
- Guarded:   `headless.regression-94-dxf-origin-no-drift`,
             `headless.regression-94a-dxf-origin-large-coord`, `headless.dxf-export-stable`,
             `headless.regression-63-dxf-arc-ellipse-identity`,
             `headless.regression-64-dxf-polyline-identity` — all pass (AC-4).
- Verdict:   **PASS**.

## 11. Outcome
```
COMPLETION REPORT — TASK-121 — 2026-08-26
- Requirements satisfied:  REQ-204 (invariant "DXF export → import → export is stable" — met on the
                           FIRST cycle, unamended)
- Summary:                 The DXF header's view is now derived from the extents as they are
                           written, not as they were computed, so a `* 1.1 + 1.0` amplification can
                           no longer turn a sub-rounding difference into a different `$VPORT`
                           group 40.
- Tests:                   `headless.regression-98-dxf-view-precision` (happy path: first cycle
                           byte-identical; failure mode: red on the unfixed build at the byte the
                           issue names). Corpus unchanged and green.
- Verification verdict:    PASS (findings resolved: none raised)
- Assumptions:             ASSUMPTION-1 validated by the transcript; ASSUMPTION-2 not relied on —
                           handled instead.
- Architectural decisions: none made by Workshop. One direction-setting decision recorded rather
                           than made silently: D-2026-08-26-h.
- Dependencies:            none added
- Technical debt noted:    DEBT-1 below — filed upstream as #111, not left in a task file
- Build:                   reproducible, clean, MSVC/ninja-release
- Docs updated:            `spec/project.md` (decision log). No requirement text changed.
```

## 12. Technical debt
```
DEBT-1: ARC groups 50/51 still move once on the first cycle.
- What:      a 3-point arc on a drawing with untidy coordinates exports start/end angles
             358.147533 / 180.313487, and the re-export writes 358.147526 / 180.313480. It
             converges on the next cycle, exactly as #98 did.
- Mechanism: an arc angle is stored as `float` RADIANS and written as DEGREES —
             `rad * (180.0 / kPi)` at six decimals, in the `AcDbArc` emit. Import multiplies the
             parsed degrees back by `kDegToRad` and narrows to `float`. Six decimals of a degree is
             ~1.7e-8 radians, finer than `float` spacing at ~6.25 rad, so the narrowing lands on a
             neighbouring float and the next export states a different angle.
- Not this:  #98 is a DERIVED, non-geometric header value. This is STORED geometry, in the entity
             section, and fixing it means changing how an angle is stored or written — a wider
             change than a bug fix for a different defect should carry.
- Pre-existing and unchanged: measured on `beta` with this task's change stashed out. It is not a
             regression introduced here.
- Follow-up: filed upstream as **#111**, with the reproducer, the mechanism and three directions.
             Not left to be rediscovered.

DEBT-2 (inherited, TASK-116): the entity prescan still uses its own `> 10000.0` magnitude test
             rather than kLargeCoordinateRebaseThreshold. Untouched here; still worth folding in
             when someone next has reason to open the prescan.
```
