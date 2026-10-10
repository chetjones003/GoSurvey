# TASK-122 — A DXF file states an arc its own reader can hold (#111)

- Type:    bug
- Status:  review — fixed, red-before/green-after, full suite green.
- Opened:  2026-08-26
- Owner:   Nathan Johnson

Upstream issue: chetjones003/GoSurvey#111, filed 2026-08-26 by TASK-121 while fixing #98.

> **Third and last of the `dxf-export-stable` residuals.** #94 removed an unbounded ratchet, #98
> removed a derived-view residual, and this removes the one in stored geometry. The issue described
> it as a corner case that "converges after one cycle". It is neither: **three arcs in four** were
> unstable on the first cycle, and some never settled at all. See §8.

## 1. Authority
- Requirements: **REQ-204** (accepted 2026-08-16) — invariant row *"DXF export → import → export is
  stable"*. Same authority as TASK-107, TASK-114, TASK-116 and TASK-121.
- **No spec change.** Direction 3 in the issue (amend REQ-204 with a `.gs`-style idempotence
  carve-out) was declined for the second time, and for a stronger reason than in #98 — see §4 Q1 and
  D-2026-08-26-i.
- Also honoured: REQ-101 (the angle moves by at most half a `float` ULP — 1.4e-5 deg, ~2.4e-4 ft of
  arc at a 1000 ft radius); REQ-200/CON-07 (the transcript writes only under the build tree).
- Owning subsystem: `io` (`src/io/DxfIo.cpp`). Nothing else is touched — `CadArc` is unchanged, so
  `.gs` is unchanged and no migration is involved.

## 2. Scope
- In scope: the ARC angles a DXF file states, and the header extents swept from them.
- Out of scope:
  - **`CadEllipse`** — measured, and it is a *different* defect. Its groups 41/42 are literal
    constants (`0.0` and one full turn), so the angles cannot drift at all; what moves is the
    header, because a `ratio` like `0.3333333` does not survive six decimals and the extents are
    swept from the ellipse in memory. Filed separately as **#113**, with the measurement — see §12
    DEBT-1;
  - the HATCH boundary's own arc-edge reader (`edgeKind == "2"`), which has its own copy of the
    degrees→radians normalization with a different threshold (`1e-6` vs `1e-9`). It tessellates to
    segments and never builds a `CadArc`, so it is not on this defect's path. Left alone
    deliberately — see §12 DEBT-2;
  - the six-decimal write precision itself. Widening it would rewrite every exported file to fix a
    defect that does not need it — the same call TASK-121 §2 made;
  - a zero- or near-zero-sweep arc, which DXF cannot express at all. Pre-existing, unchanged, and
    measured — §12 DEBT-3.
- Smallest change: one file-local conversion given a single owner, and three call sites pointed at
  it. No new type in any header, no signature change outside the file, no format change.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No.** `DxfArcAnglesFromDegrees` and `DxfArcToWrite` live in the anonymous namespace of
          the file that already owns both sides of this conversion, and they exist to *remove* a
          duplicate answer rather than to add a layer. Rule 2's two-concrete-uses test is met before
          they are written, not after: the reader and the writer are both already doing this
          conversion today, in two places, with two different results — which is the defect.
    - The on-disk shape does not change: the same groups, at the same precision, in the same order.
      A DXF exported before this change still imports identically.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | The issue offers three directions: write angles at a precision that identifies the `float` (1), store the angles as `double` (2), or accept it and amend REQ-204 (3). Which? | not asked — resolved by measurement, §8 | **None of the three, and the measurement is why.** (1) does not work: widening the text does not help when the value being written is a double sum that no `CadArc` can hold — the reader still narrows it and the next export still disagrees. (2) is a `.gs` format change plus a migration, for a defect that is not in the store. (3) was declined for the same reason as #98, only more strongly — 74% of arcs were affected and some never converged, so there was no stable second cycle to amend the requirement *to*. What the fix does instead: make the file state the arc its own reader will hold. |
| Q2 | Does this need `ComputeWorldExtents` in `CadCommands.cpp` to change too, given the note above the arc sweep demands they match exactly? | 2026-08-26 | **No — and the change strengthens that agreement rather than threatening it.** The note requires the DXF header's sweep to match what `ComputeWorldExtents` computes on the document a reader builds. That document holds the as-written angles. Sweeping the header from the same as-written angles is what makes the two agree exactly; sweeping it from the in-memory arc is what made them merely close. |

## 5. Assumptions
```
ASSUMPTION-1: The reader's reconstruction is the definition of "the arc this file states".
- Because:       nothing else can be. A DXF ARC is two angles in degrees at six decimals; whatever a
                 reader builds from those is what the file means. The writer's own intent is not
                 recoverable from the bytes and so cannot be the answer.
- Risk if wrong: the writer pins to something the reader does not reproduce, and the drift stays.
- Validate by:   `regression-111-dxf-arc-angle-roundtrip.txt` (red before, green after) and the
                 2,000,000-arc sweep in §8.

ASSUMPTION-2: Arcs reach DXF export only through `st.userArcs`.
- Because:       `DxfIo.cpp` names no other arc store; paper-space arcs (`PaperLayout::paperArcs`)
                 are not exported to DXF at all today.
- Risk if wrong: a second arc writer keeps the old behaviour and the invariant holds only for model
                 space.
- Validate by:   grep — `paperArcs` does not appear in `src/io/DxfIo.cpp`. Re-check if paper-space
                 export is ever added.
```

## 6. Plan
1. Reproduce the issue's single-arc transcript; confirm which lines move and that `B == C`.
2. Build a standalone probe replicating the writer's and reader's arithmetic exactly, and measure
   how common the defect is and which candidate fix actually removes it — before touching the repo.
3. Give the degrees→`float` radians conversion one owner, and point the reader at it.
4. Have the writer emit the angles that owner produces, so the file states an arc the reader holds.
5. Sweep the header extents from the same as-written arc, so the header and the entity records
   describe one drawing.
6. Add the reproducer as a regression transcript asserting the **first** cycle, widened to cover
   both winding directions and both sides of the sweep-magnitude crossover.
7. Record the direction taken in the decision log (AC-1).
8. Full ctest; confirm the #94, #98 and #63/#64 regressions and `dxf-export-stable` still pass.

## 7. Workflow-specific notes
Step 2 is the reason this task is short. The issue's framing — "converges after one cycle", one
suggested direction being "write more digits" — is wrong on both counts, and a fix written to that
framing would have looked right against the single-arc reproducer while leaving most of the defect
in place. 2,000,000 arcs cost about a minute to measure and settled it before any repo file changed.

## 8. Implementation log
- 2026-08-26 — **Reproduced.** The issue's single arc: `a` vs `b` differ at byte 265, both 6959
  bytes, five lines — groups 50 and 51, `$EXTMIN` x, `$VPORT` 12 and 40. `b == c`.
- 2026-08-26 — **Measured, before writing any fix.** A standalone probe replicating the writer's and
  the reader's arithmetic, over 2,000,000 random arcs in five sweep-magnitude bands and both winding
  directions:

  | writer | not a fixed point on cycle 1 | still moving after 3 cycles |
  |---|---|---|
  | current | **1,479,444** / 2,000,000 | 6,436 |
  | run our angles through the reader once, then emit | 33,632 | 22 |
  | **pin the start, state the end as written-start + sweep** | **0** | **0** |

  Two things fall out of that table. The defect is not a corner case — it is **74% of arcs**, and the
  issue's "converges after one cycle" is not universally true either, so direction 3 had no stable
  second cycle to amend REQ-204 to. And the obvious fix (emit what one round trip would produce) is
  not enough, because the reader's sweep comes from a *subtraction of two independently rounded
  angles* and that subtraction has to be made exact, not merely anticipated.
- 2026-08-26 — **Two causes, one per line of the fix.**
  1. A DXF arc runs counter-clockwise from group 50 to group 51, so an arc stored with a NEGATIVE
     sweep was written from `startRad + sweepRad` — a **double sum of two floats**, a value no
     `CadArc` can hold. Narrowing it to `float` before emitting makes the file state an angle that
     survives.
  2. The reader derives the sweep as `group 51 - group 50`. Two independently rounded angles put it
     up to 1e-6 deg from the sweep we hold, and above ~8.4 deg of sweep that is **wider than the
     `float` spacing** of the stored radian value, so it lands on a different float. Stating the end
     as the *written* start plus the sweep makes that subtraction return our sweep exactly.
- 2026-08-26 — **The header was carrying it further.** With the angles pinned, the reproducer still
  failed on three lines: `$EXTMIN` x and `$VPORT` 12/40. The extents sweep was sampling the arc in
  MEMORY while the entity record described the arc the reader would rebuild, so the two halves of
  one file described different drawings — the same failure shape TASK-083 fixed for polylines, one
  entity type over. Sweeping from the as-written arc closed it.
- 2026-08-26 — **Red before, green after.** The regression transcript fails on the unfixed build at
  step 29, byte 249, 7652 vs 7652, and passes on the fixed one. Checked by stashing the source
  change and rebuilding, not by reasoning about it.
- 2026-08-26 — **Edge cases**, run against both writers rather than only the new one: full circle
  (+2π and −2π), zero sweep, start exactly 0, start exactly 2π, sweep below the reader's 1e-9 guard,
  sweep 1e-7 deg, negative start with negative sweep, start just under 2π, sweep 359.999999 deg,
  sweep exactly 180 deg, start 359.9999995 deg. All are fixed points under the new writer except the
  three that are the same case — a sweep below the six-decimal degree grid — and those behave
  **identically under the old writer**. Pre-existing, and not fixable in a writer; see §12 DEBT-3.
- 2026-08-26 — **ELLIPSE checked**, because the issue flagged it as unmeasured. Groups 41/42 are
  literal constants, so they cannot drift. An ellipse *does* still move the header, by a different
  mechanism entirely. Filed rather than folded in — §12 DEBT-1.

## 9. Self-verification
- `build-project`: clean build, MSVC, ninja-release. No new warning in `DxfIo.cpp`.
- `architecture-review`: no boundary crossed. The change is confined to `io`; the two helpers are
  file-local; `CadArc`, `.gs` and every other subsystem are untouched.
- `code-review`: the duplication that caused the defect is gone — there is now one definition of
  degrees→`float` radians and three callers of it. Comments state the mechanism and the measurement,
  so the next reader does not re-derive either.
- `dependency-audit`: none added.
- `performance-review`: two `to_string`/`stod` pairs per arc, twice per export (extents sweep and
  emit). Not measurable against a file write.
- `testing`: below.

## 10. Verification result
- Suite:     `ctest` — **638/638 passed**. Nothing disabled; the `DISABLED` list in `CMakeLists.txt`
             is still empty.
- Red/green: `headless.regression-111-dxf-arc-angle-roundtrip` fails on the unfixed build at step
             29, byte 249, and passes on the fixed one.
- Guarded:   `headless.dxf-export-stable`, `headless.regression-94-dxf-origin-no-drift`,
             `headless.regression-94a-dxf-origin-large-coord`,
             `headless.regression-63-dxf-arc-ellipse-identity`,
             `headless.regression-64-dxf-polyline-identity` — all pass.
- Verdict:   **PASS**.

## 11. Outcome
```
COMPLETION REPORT — TASK-122 — 2026-08-26
- Requirements satisfied:  REQ-204 (invariant "DXF export → import → export is stable" — met on the
                           FIRST cycle for drawings containing arcs, unamended)
- Summary:                 A DXF file now states the arc its own reader will hold, in both its
                           entity records and the header extents swept from them, instead of angles
                           computed at a precision no `CadArc` can carry.
- Tests:                   `headless.regression-111-dxf-arc-angle-roundtrip` (happy path: first
                           cycle byte-identical across four arcs covering both winding directions
                           and both sides of the sweep-magnitude crossover; failure mode: red on the
                           unfixed build). Corpus unchanged and green.
- Verification verdict:    PASS (findings resolved: none raised)
- Assumptions:             ASSUMPTION-1 validated by the transcript and the 2,000,000-arc sweep;
                           ASSUMPTION-2 validated by grep, with a re-check condition recorded.
- Architectural decisions: none made by Workshop. One direction-setting decision recorded rather
                           than made silently: D-2026-08-26-i.
- Dependencies:            none added
- Technical debt noted:    DEBT-1 (filed upstream as #113), DEBT-2, DEBT-3 below
- Build:                   reproducible, clean, MSVC/ninja-release
- Docs updated:            `spec/project.md` (decision log), `TRACKER.md`. No requirement text
                           changed.
```

## 12. Technical debt
```
DEBT-1: An ELLIPSE still moves the header once on the first cycle.
- What:      an ellipse with an untidy ratio exports `$EXTMIN` y `82.535389` and re-exports
             `82.535403`; `$EXTMAX` y and `$VPORT` 40 move with it. `b == c`, so it converges.
- Not the angles: groups 41 and 42 are literal constants — `0.0` and one full turn — because
             `CadEllipse` holds no parameter range. They cannot drift, which answers the question
             #111 left open.
- Mechanism: the ELLIPSE entity record is BYTE-IDENTICAL across the cycle. What changes is the
             stored geometry behind it: a `ratio` of `0.3333333` is written as `0.333333`, and the
             float that comes back is not the float that went out. The extents are swept from the
             ellipse in MEMORY, so the header describes the pre-round-trip ellipse while the entity
             record describes the post-round-trip one — the same header-vs-body shape this task
             fixed for arcs, reached by a different route.
- Why not here: a different entity, a different field, and a different cause. Folding it in would be
             an unrelated behaviour change smuggled into a bug fix (the call TASK-116 made for its
             own DEBT-2).
- Follow-up: filed upstream as **#113**, with the measurement and three directions.

DEBT-2: The HATCH boundary reader keeps its own copy of the arc-angle normalization.
- What:      `edgeKind == "2"` in the hatch-boundary parser repeats the degrees→radians sweep
             normalization with a different threshold (`< 1e-6` where the entity reader uses
             `< 1e-9`).
- Why left:  it tessellates the edge into segments and never builds a `CadArc`, so it is not on this
             defect's path, and the threshold difference may well be deliberate for boundaries.
             Changing it would be an unrelated behaviour change inside a bug fix.
- Remove by: deciding whether a hatch boundary edge should reconstruct exactly like an entity arc.
             Worth doing when someone next opens the hatch reader.

DEBT-3: An arc whose sweep is below the six-decimal degree grid becomes a full circle.
- What:      a sweep under ~1e-6 deg writes group 51 equal to group 50, and the reader restores one
             full turn — because a DXF ARC has no way to state a zero-length arc, and 51 == 50 means
             a full circle by convention.
- Pre-existing and unchanged: measured against both writers; identical behaviour.
- Reachable? Barely. `ComputeArcSweepRad` clamps a created arc's sweep away from zero (`< 1e-12` →
             one full turn) and collinear picks are refused outright, so it needs an arc trimmed or
             broken down to almost nothing.
- Remove by: nothing in the writer can fix it — the format cannot express the entity. It would need
             a decision about what a degenerate arc means on export (drop it? refuse it? write it as
             a tiny LINE?). Named so it is not rediscovered as a bug.
```
