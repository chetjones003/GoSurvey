# TASK-067 — Establish the document origin at entry, not only at load (REQ-101)

- Type:    feature (with a defect at its centre)
- Status:  done
- Opened:  2026-08-17
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         none (raised by the user immediately after TASK-066)
- Requirements: **REQ-101**, which had to be **accepted first** — see §3. Also REQ-201 (refusals
  reported), REQ-079 (normalization must stay idempotent; this must not create a moving origin).
- Constraints:  CLAUDE.md rules 1–8; **CON-03** (no previously-rejected approach without a new
  decision) — directly engaged, since this *is* the option D-2026-08-17-a declined.
- Acceptance:   REQ-101's four conditions as amended (stored within ±0.01 ft at state-plane
  magnitude; one-time establishment; over-large magnitude still refused; regression set at tolerance).
- Owning subsystem: **Commands** (`ParseWorldPointD`, `ProcessCommandLineSubmit`), **util/Commands**
  (`CadCoordinateFrame` bounds)

## 2. Scope
- In scope: the double-precision typed-coordinate parse; entry-time origin establishment with both
  bounds; accepting REQ-101; the recorded decision; regression coverage.
- Out of scope: **picks.** `SubmitViewportPick` takes `float` world coordinates, so a picked point is
  already quantized before it reaches the Commands layer — fixing that means changing the pick
  signature and the view transform that produces it, which is a wider change with a much smaller
  payoff (a pick's precision is bounded by the pixel it came from, not by the storage frame). Typed
  coordinates are where surveyors enter exact values, and they were the measured violation.
- Out of scope: CSV / DXF / glTF import. Those already convert in double and subtract the origin
  (REQ-053's local-storage invariant, and DXF establishes its own origin) — verified by reading, not
  assumed, but not re-tested here.
- Smallest change: one new pure function, one new call site. No signature change and no `const`
  removed from `ParseStoragePoint`'s 29 callers.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **Yes, twice — and both were escalated, not decided here.**
      1. **No accepted requirement existed.** REQ-101 stated the exact tolerance being violated but was
         `Status: proposed`, and CLAUDE.md step 1 requires an `accepted` REQ-NNN before building. So the
         first blocker was not code: the requirement had to be accepted, which is a spec change and the
         user's call. Put to the user with three framings; they chose to accept REQ-101 scoped to
         *stored* coordinates.
      2. **This reverses a recorded decision.** Option (A) was explicitly declined in
         D-2026-08-17-a hours earlier. CON-03 forbids reintroducing a rejected approach without a new
         decision, so this is recorded as **D-2026-08-17-b** with the evidence that changed the answer,
         rather than quietly implemented because the user asked.
    - `ParseWorldPointD` is a new **public API function**, which is why it is worth being explicit that
      it is not a new abstraction in REQ-301's sense: it is the existing function's real
      implementation, and the existing `float` overload now delegates to it, so there is one behaviour
      rather than two. The alternative — a second parallel parse path — is what would have been the
      abstraction problem.

## 4. Questions  (workflow.md §5)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | REQ-101 is `proposed`, so there is no authority to build on. Accept it scoped to stored coordinates, write a new REQ for storage precision, or set a tighter tolerance first? | 2026-08-17 | **Accept REQ-101, scoped to stored coordinates.** Recorded as D-2026-08-17-b. |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: Establishing the origin from the FIRST large coordinate is sufficient; it need not be
  the extents centre.
- Because: precision needs the locals to be SMALL, not centred. A 5,000 ft site whose origin sits at
  one corner still stores locals under 5,000, which float holds to ~1e-4 ft. Re-centring later would
  be actively harmful — each move rounds every stored coordinate through float again, which is the
  compounding drift REQ-079's idempotence condition forbids.
- Risk if wrong: a drawing that grows far from its first point would drift toward coarser locals.
  Bounded: it would take ~1e5 ft of growth to reach even 0.01 ft spacing.
- Validate by: the one-time property is asserted in the regression transcript (a second large
  coordinate must not move the frame). VALIDATED 2026-08-17.

ASSUMPTION-2: 1e9 is a safe ceiling for an establishable origin.
- Because: state plane tops out near 1e7 ft and UTM near 1e7 m, so 1e9 is ~100x beyond any real
  projected system and cannot reject legitimate survey data.
- Risk if wrong: a legitimate CRS beyond 1e9 would silently miss the precision fix (it would still
  load-normalize, so it degrades to the old behaviour rather than breaking).
- Validate by: regression-59/59b pass, and regression-61 (which uses 1e12 and depends on load-time
  normalization) still behaves as it did. VALIDATED 2026-08-17.
```

## 6. Plan  (workflow.md §6 — written before any code)
- Steps:
  - [x] 1. Measure the actual loss at a realistic easting, before assuming there is one.
  - [x] 2. Locate where the narrowing happens (it is the parse, not the commit).
  - [x] 3. **Escalate**: no accepted requirement, and a rejected option being revisited.
  - [x] 4. Add `ParseWorldPointD`; make the `float` overload delegate; reorder `ParseStoragePoint`.
  - [x] 5. Establish the origin once, ahead of dispatch, bounded at both ends.
  - [x] 6. Accept REQ-101; record D-2026-08-17-b; update the traceability matrix.
  - [x] 7. Build, suite, sweeps.

## 7. Workflow-specific notes
- Feature: pre-flight was Q1 plus the CON-03 decision. Neither was skippable — building first and
  writing the requirement afterwards is precisely the drift the three-layer system exists to prevent.

## 8. Implementation log
- 2026-08-17 — **Measured before assuming.** At easting 2e6, typed `2000000.10` stored as
  `2000000.125` (0.025 ft) and `500000.03` as `500000.03125`. REQ-101 allows 0.01 ft, so the easting
  failed by 2.5x — with no save or load involved.
- 2026-08-17 — **First hypothesis about the fix was wrong, and finding that out changed the design.**
  The plan was to call `MaybeRebaseLargeCoordinates` after each commit, i.e. an "entry-time" rebase in
  the loose sense. That would not have worked: `ParseWorldPoint` returns **`float`**, so the value is
  quantized *inside the parse*, and no amount of origin work afterwards recovers it — rebasing only
  rearranges already-quantized numbers. The fix had to move the narrowing, not add a rebase.
- 2026-08-17 — Chose the ordering `parse in double → subtract origin → narrow`, which quantizes at
  local rather than world magnitude. `ParseWorldPointD` is the real implementation; the `float`
  overload delegates and re-checks finiteness at the narrower type (a double that is finite can
  overflow on narrowing, so that guarantee cannot be inherited).
- 2026-08-17 — **Placement chosen to avoid an ownership problem.** The origin cannot be established
  inside `ParseStoragePoint`: it takes `const AppCommandState&`, and 29 call sites would need the
  `const` removed — but more importantly a *parse* function that silently rebases the whole drawing is
  the wrong owner for a document-wide mutation. Established instead ahead of dispatch in
  `ProcessCommandLineSubmit`, the one place every typed coordinate passes through. One call site, no
  signature churn.
- 2026-08-17 — **The tests caught a real error in my own change, which is the part worth recording.**
  `regression-59-circle-infinite-radius` and `-59b` went **red**: establishing a frame around `1e38`
  made those absurd values *representable*, so the non-finite guards added hours earlier stopped
  firing and the circle was created instead of refused. Accommodating garbage is worse than refusing
  it — a typo would silently produce a drawing in a nonsense frame. Added
  `kMaxEstablishableOriginMagnitude` (1e9, ~100x beyond any real projected system). Both tests went
  green again, and they are now the bound's guard.
- 2026-08-17 — Verified: `2000000.10` now stores as origin `2000000` + local `0.10000000149`, a world
  error of **~1.5e-9 ft** against 0.025 ft before. And the **first** resave is byte-identical, because
  the drawing is already normalized when written — the payoff over TASK-066's load-time-only behaviour.

## 9. Self-verification  (verification/skills/)
- [x] **build-project** — PASS. Clean configure + build, MSVC 19.5x Release. No new warnings.
- [x] **architecture-review** — PASS. Both boundary crossings escalated and recorded (§3). No new
      layer, dependency, owner or global state; no data-format change. The new public function is the
      existing one's implementation, not a parallel path (REQ-301 reasoning in §3). No `gl*` outside
      Renderer/Platform. CON-03 satisfied by an explicit new decision.
- [x] **code-review** — PASS. Correctness: the reordering is the fix, and it is measured rather than
      argued. Edge cases: both bounds tested; finiteness re-checked after narrowing; relative `@dx,dy`
      deliberately excluded from establishment (its anchor is already in the current frame, so it
      cannot be the first thing needing a new one). Error paths: refusals still reported (REQ-201),
      asserted by `EXPECT LOG`.
- [x] **dependency-audit** — n/a. Nothing added.
- [x] **performance-review** — n/a. One extra double parse per submitted command line, on a path that
      runs when a human presses Enter. Nothing on a frame path changed.
- [x] **testing** — PASS. `headless.regression-req101-origin-at-entry` covers all four REQ-101
      conditions. **404/404 ctest green.** 1000-seed fuzz sweep (round-trip on by default): 0 failures.
      **Gap stated rather than hidden:** the transcript pins the *mechanism*, not the arithmetic — the
      driver has no verb that reads a stored coordinate back as a number, and ADR-002 keeps command
      TUs out of the Catch2 target, so the ±0.01 ft figure is verified by inspecting the saved `.gs`
      (numbers in §8) rather than asserted by a test. Removal condition: a driver verb that reports a
      stored coordinate, or a pure-TU home for coordinate parsing.

## 10. Verification result
- Submitted: 2026-08-17
- Verdict:   **PASS**
- Findings:  none outstanding. Picks remain quantized at pick time (§2) — a real limit, deliberately
             out of scope, not debt from this change.

## 11. Outcome

```
COMPLETION REPORT — TASK-067 — 2026-08-17
- Requirements satisfied:  REQ-101 (accepted this task; typed-storage conditions met and verified.
                           The reference-dataset condition remains outstanding and is marked so in the
                           traceability matrix — accepting the requirement did not manufacture
                           evidence for the half that has none). REQ-201, REQ-079 idempotence upheld
- Summary:                 A coordinate typed at state-plane magnitude was stored 0.025 ft off — 2.5x
                           REQ-101's tolerance — because the parse narrowed to float BEFORE the
                           document origin was subtracted, quantizing at world magnitude. Typed points
                           are now parsed in double and the origin is established ahead of dispatch,
                           so narrowing happens at local magnitude: the same input now stores within
                           ~1.5e-9 ft, and the first resave is byte-identical
- Tests:                   headless.regression-req101-origin-at-entry (new; four REQ-101 conditions),
                           regression-59/-59b now double as the upper bound's guard — 404/404 green,
                           1000-seed sweep clean
- Verification verdict:    PASS  (findings resolved: the #59 regression my own change introduced)
- Assumptions:             ASSUMPTION-1 (first coordinate, not centre) and ASSUMPTION-2 (1e9 ceiling)
                           — both VALIDATED
- Architectural decisions: TWO, both escalated — accepting REQ-101, and reversing (A)'s rejection
                           under CON-03. Recorded as D-2026-08-17-b
- Dependencies:            none added
- Technical debt noted:    picks are still quantized before reaching the Commands layer (§2);
                           the ±0.01 ft arithmetic is verified by file inspection rather than by an
                           assertion (§9), with a removal condition
- Build:                   clean, MSVC Release, reproducible
- Docs updated:            spec/requirements.md (REQ-101 accepted + traceability),
                           spec/project.md (D-2026-08-17-b), TRACKER.md
```
