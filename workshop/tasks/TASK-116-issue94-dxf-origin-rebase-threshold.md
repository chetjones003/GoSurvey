# TASK-116 — A DXF round trip does not walk the drawing (#94)

- Type:    bug
- Status:  review — fixed, tested, `dxf-export-stable` re-enabled and green.
- Opened:  2026-08-26
- Owner:   Nathan Johnson

Upstream issue: chetjones003/GoSurvey#94, filed 2026-08-26 by TASK-114 while closing #63.

> **This closes out the `dxf-export-stable` oracle.** It found three defects and needed all three
> fixed before it could come back to the suite: #64 (TASK-083), #63 (TASK-114), #94 (here). It has
> been registered `DISABLED` since 2026-08-18.

## 1. Authority
- Requirements: **REQ-204** (accepted 2026-08-16) — invariant row *"DXF export → import → export is
  stable"*. Same authority TASK-107 and TASK-114 used. **No spec change is needed**, and that
  conclusion is itself a finding — see §8, where the opposite looked true for most of a day.
- Also honoured: REQ-201 (the rebase, where it still fires, still reports itself); REQ-101
  (tolerance — the drift was inside it per cycle, which is exactly why nothing caught it).
- Acceptance: REQ-204's invariant, proven by re-enabling the fixture that tests it rather than by a
  new check written to pass.
- Owning subsystem: `io` (`src/io/DxfIo.cpp`). The threshold and both rebase helpers already exist
  in `CadCoord` and are unchanged.

## 2. Scope
- In scope: the two places DXF import establishes the document origin.
- Out of scope:
  - `.gs` load, which has been gated correctly since before this was found;
  - the entity-coordinate prescan's own `> 10000.0` magnitude test, which is a different and
    narrower question (it picks a *candidate* origin from raw entity text, not a policy) — left
    alone deliberately, and noted in §12;
  - the six-decimal write precision of `$EXTMIN`/`$EXTMAX`, which is not the defect. See §8.
- Smallest change: **two lines**, plus the guard they consult, which already exists.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No** — and this is a case of *removing* an inconsistency rather than adding anything.
          `kLargeCoordinateRebaseThreshold` and `MaybeRebaseLargeCoordinates` already exist and
          already encode this exact policy for `.gs`. The change makes DXF use the policy the
          project already has, instead of a second, unstated one. No new constant, no new function,
          no signature change.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Is this REQ-204's invariant being wrong (as REQ-079's was for #61), or the code being wrong? | 2026-08-26 | **The code.** Decided by measurement, not argument — see §8. `.gs`'s normalization converges; this one ratchets, and REQ-079's own idempotence condition is what says a ratchet is not acceptable normalization. |
| Q2 | Does gating DXF import change behaviour for files that need the rebase? | 2026-08-26 | **No, and it is pinned.** `regression-94a` asserts the rebase still fires and still reports for state-plane coordinates. It passes before *and* after, by design — it is a guard, not a red-before test. |

## 5. Assumptions
```
ASSUMPTION-1: A drawing below the threshold gains nothing from a local-storage rebase.
- Because:       local storage exists to keep float32 mantissa bits away from a large world offset.
                 Below 100000 the world coordinates already fit comfortably, so moving the origin
                 to the extents centre buys no precision and costs one full re-round of every
                 stored float.
- Precedent:     this is not a new judgement — it is the judgement `.gs` load has always made, via
                 the same constant. This task adopts it rather than inventing it.
- Risk if wrong: a mid-magnitude drawing (say 50,000) loses precision it used to get.
- Validate by:   `regression-94a` at 2e6 (rebases, as it must) and `regression-94` at 150 (does
                 not, as it must not). The band between is the same band `.gs` already accepts.
```

## 6. Plan
- Steps:
  - [x] 1. Reproduce over SIX cycles, not one — the single-cycle diff cannot tell a ratchet from a
           one-time normalization, and that distinction decides whether this is code or spec.
  - [x] 2. Gate the header-extents rebase on coordinate magnitude.
  - [x] 3. Replace the unconditional post-parse `RebaseDrawingToLocalOrigin` with
           `MaybeRebaseLargeCoordinates`.
  - [x] 4. Regression for the small-drawing case, proven RED (`regression-94`).
  - [x] 5. Guard for the large-coordinate case (`regression-94a`).
  - [x] 6. Re-enable `dxf-export-stable`; confirm green.
  - [x] 7. Full suite.

## 7. Workflow-specific notes
- Bug: the reproducer existed before the task did — it is the oracle. What it did *not* supply was
  the number of cycles needed to characterise the failure, which is where the real work was.

## 8. Implementation log
- 2026-08-26 **The obvious answer was wrong, and finding that out was most of the task.**
  BUG-019 / issue #61 is this same mechanism for `.gs`, and was resolved as a **SPEC** defect
  (D-2026-08-17-a): the rebase is legitimate *normalization*, bounded and non-lossy, so REQ-079 was
  amended to compare the second and third saves with an idempotence condition, and `gs-roundtrip`
  was rewritten to match. A lines-only probe appeared to confirm the same shape here — `A != B`,
  `B == C`, converged after one cycle.
- 2026-08-26 **It does not converge on the oracle's own geometry.** Six cycles:

  | cycle | `$EXTMIN.y` | `$EXTMAX.y` | view centre Y |
  |---|---|---|---|
  | 1 | -8.500002 | 158.500000 | 74.999999 |
  | 3 | -8.500006 | 158.499996 | 74.999995 |
  | 6 | -8.500012 | 158.499990 | 74.999989 |

  Exactly -2e-6 per cycle, linear, no settling. So the REQ-204 amendment that mirrored #61 would
  **not** have fixed it — the oracle would still fail. REQ-079's own idempotence condition exists to
  catch "geometry drifting further on every open/save cycle", which is precisely this.
- 2026-08-26 **Isolated.** Stable before the fix: lines alone; lines+ARC; lines+POLYLINE;
  lines+CIRCLE. The deciding variable is not entity type but the **magnitude of the resulting
  origin** against float32 spacing — lines+ARC centre it near 25 (spacing 2^-19), the POLYLINE lifts
  maxY to 150 and moves it near 75 (spacing 2^-17), coarse enough that each rebase moves the next
  computed centre again. That is why `regression-94` needs all four entity kinds: drop any one and
  it passes against the unfixed code.
- 2026-08-26 **The root cause is an asymmetry, not an algorithm.** `.gs` load gates normalization on
  `kLargeCoordinateRebaseThreshold`; DXF import did not, and rebased unconditionally. Two loaders,
  one mechanism, one gated and one not.
- 2026-08-26 **One residual, and it is benign.** A drawing with coordinates that are not exact at six
  decimals still differs A-vs-B in exactly one value — VPORT group 40, the saved **view height**, a
  derived view setting rather than geometry — and then converges (`B == C`). That is the `.gs`
  situation from #61: one-time normalization, idempotent after. Left as-is; see §12.

## 9. Self-verification
- [x] build-project        — PASS (clean)
- [x] architecture-review  — PASS (removes an inconsistency; no new constant, function or signature)
- [x] code-review          — PASS (two lines of behaviour change)
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a (strictly less work: a rebase that used to run always now usually
                             does not)
- [x] testing              — **603/603 ctest green with NOTHING disabled**, first time since
                             2026-08-18. `regression-94` proven red-before / green-after.

## 10. Verification result
- Submitted:  2026-08-26
- Verdict:    PASS
- Findings:   none outstanding.

## 11. Outcome
- Requirements satisfied: REQ-204's stability invariant — met, and proven by the fixture written to
                          test it rather than by a new check. REQ-201 — the rebase still reports
                          itself where it still fires (pinned by `regression-94a`).
- Tests added:            `regression-94-dxf-origin-no-drift.txt` (red before),
                          `regression-94a-dxf-origin-large-coord.txt` (guard, green before and
                          after by design).
- Refactors:              none
- Docs updated:           `CMakeLists.txt` — the disabled-oracle list is now **empty**; the note is
                          kept as the record of the convention and of what the oracle found.
- Done:                   2026-08-26

## 12. Technical debt
```
DEBT-1: One derived view value still normalizes once on a drawing with untidy coordinates.
- What:      VPORT group 40 (view height) can differ by 1e-6 between the first and second export of
             a drawing whose extents are not exact at six decimals. It converges immediately
             (B == C) and is a saved view setting, not geometry.
- Why left:  it is exactly the `.gs` situation REQ-079 already accepts as normalization — bounded,
             idempotent, non-lossy. Chasing it to zero means changing how the header is written or
             how the view is derived, which is more risk than a converged 1e-6 in a view setting is
             worth. Named rather than hidden.
- Remove by: writing $EXTMIN/$EXTMAX at a precision the reader can reproduce exactly, if a reason
             ever arises. No user-visible symptom today.
- Follow-up: not filed.

DEBT-2: The entity prescan still uses its own `> 10000.0` magnitude test.
- What:      when the header gives no usable extents, the importer scans entity text for the first
             coordinate over 10000 and rebases to it — a different threshold from
             kLargeCoordinateRebaseThreshold (100000).
- Why left:  it answers a different question (which candidate origin, from raw text, before any
             parsing) and it is not part of #94's failure path. Changing it would be an unrelated
             behaviour change smuggled into a bug fix.
- Remove by: deciding whether the two thresholds should be one constant. Worth doing when someone
             next touches the prescan.
- Follow-up: not filed; noted here so the discrepancy is not rediscovered as a bug.
```
