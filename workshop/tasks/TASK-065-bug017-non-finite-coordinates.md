# TASK-065 — Reject non-finite coordinates at the two places that can create them

- Type:    bug
- Status:  done
- Opened:  2026-08-17
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         none (defect repair, not roadmap-driven)
- Requirements:
  - **REQ-204** — its invariant table states "No coordinate is NaN or infinite", so the expected
    behaviour is written down and this is a defect, not a SPEC GAP.
  - **REQ-201** — the refusal must be *reported*. There was no refusal at all, which is the more
    serious half of the violation: the command logged success.
  - **REQ-101** — a value that cannot be represented is a failure, not a tolerance question.
- Constraints:  CLAUDE.md "Additional rules" 1–8; REQ-300 (no new dependency); REQ-301 (no new
  abstraction); **ADR-002** (the Catch2 target links no command TU) — this one shaped the test
  strategy, see §7.
- Acceptance:
  - a circle whose derived radius or centre is non-finite is refused, reported, and not stored;
  - a relative coordinate whose resolved sum is non-finite is refused and reported;
  - `finite-coords` holds across the full fuzz sweep;
  - no legitimate geometry is refused (the guards reject non-finite only, never a magnitude).
- Owning subsystem: **Commands** (`CommitCircle`, `ParseWorldPoint`)

## 2. Scope
- In scope: the two mechanisms **reproduced** during this task.
  1. `CommitCircle` — the derived radius. This is issue #59 as filed.
  2. `ParseWorldPoint`'s relative branch — the resolved sum. Found while investigating #59's
     recommendation to generalize; reproduced independently for **LINE, POLYLINE and RECT**.
- Out of scope, and this is a deliberate limit rather than an oversight: **ARC, ELLIPSE and OFFSET.**
  Issue #59 recommends guarding them on the grounds that they also derive lengths from two user
  points. They were probed the same way and **did not reproduce**:
  | Probe | Result |
  |---|---|
  | `ARC` via three picks at `-1e+38 / 1e+38` magnitudes | arc committed, `finite-coords` **passed** |
  | `ELLIPSE` centre `0,0`, major axis endpoint `3e+38,3e+38`, ratio `0.5` | ellipse committed, **passed** |
  | `OFFSET` of a circle at distance `1e+38` | copy committed, **passed** |
  A 5000-seed sweep with the generator's hostile coordinate ladder also produces no `finite-coords`
  failure at any other site. Guarding them anyway would be a **speculative fix** — the one thing the
  bug-fix workflow forbids outright — so they are recorded as probed-and-not-reproduced. If a future
  seed reaches one, it is a new finding with its own reproducer, which is worth more than a guard
  added today on a guess.
- Also out of scope: `1e+40` typed as an *absolute* coordinate. Probed: already refused and reported
  ("Could not parse input for current CIRCLE step"), because the stream extraction sets failbit on
  overflow. That path was already correct, and it is what told me the relative branch was a **gap in
  an existing rule** rather than a missing new rule.
- Smallest change: two guards, both at the point where a non-finite value is *created*, not at the
  stores that receive it.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **No — proceed.** Two `std::isfinite` guards inside existing functions. No signature
          changed, nothing new owned, no data-format change.
    - Considered and rejected: the `IsStorableCoordinate()` helper issue #59 suggests. It would have
      **one** present-day use once the evidence came in, because the two proven mechanisms need
      different checks in different places — three scalars at a commit site, two at a parse site —
      and the sites that would have made it a shared helper are exactly the ones that did not
      reproduce. Writing it now would be REQ-301's "no abstraction without two or more present-day
      concrete uses" violated in order to look general. Two direct guards are smaller and honest.

## 4. Questions  (workflow.md §5)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Issue #59 offers a choice: "either the centre is rejected as out of range, or the derived radius is rejected as non-finite." Which? | not asked — resolved from the spec | **Reject non-finite.** "Out of range" needs a documented coordinate range, which no requirement defines, so choosing it would have been implementing to an unwritten rule (a SPEC GAP). "Not finite" is already written down, in REQ-204's own invariant table. No guess involved, so no question needed. |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: Refusing a non-finite relative coordinate through ParseWorldPoint's existing
  false return is sufficient for REQ-201, with no new log message.
- Because: every caller already reports a false return as a parse failure — verified by probe, where
  an absolute 1e+40 produced "Could not parse input for current CIRCLE step — see hint below." The
  relative case now takes the identical path, so the refusal is reported by construction.
- Risk if wrong: a caller that ignores the return value would refuse silently, violating REQ-201.
- Validate by: regression-59b asserts `EXPECT LOG "Could not parse"` after the refused LINE, so the
  report is checked rather than assumed. VALIDATED 2026-08-17.
```

## 6. Plan  (workflow.md §6 — written before any code)
- Approach: reproduce; probe every sibling the issue names *before* deciding scope; guard where the
  non-finite value is created.
- Steps:
  - [x] 1. Reproduce seed 4737.
  - [x] 2. Probe LINE / ARC / ELLIPSE / OFFSET / POLYLINE / RECT and the absolute-parse path.
  - [x] 3. Write both regression transcripts; prove both red.
  - [x] 4. Guard `CommitCircle`; guard `ParseWorldPoint`.
  - [x] 5. Build, run transcripts, full suite, 5000-seed sweep, round-trip sweep.
  - [x] 6. Update docs and the tracker.

## 7. Workflow-specific notes
- Bug: root cause identified with evidence before any fix; no speculative change.
- **Test strategy is constrained by ADR-002, and this is worth recording.** `ParseWorldPoint` is
  public in `CadCommands.hpp`, pure, and had no tests — an obvious Catch2 candidate. But
  `GoSurveyTests` deliberately links **no command translation unit** ("the test target still links no
  command, UI or GL translation unit, which is the property ADR-002 protects" — `CMakeLists.txt`), and
  `ParseWorldPoint` is defined in `CadCommands.cpp`. Unit-testing it would mean either linking that
  TU into the Catch2 target or moving the function to a pure TU — the first breaks ADR-002, the
  second is a refactor the Workshop may not decide alone. So coverage is via the headless
  transcripts, which link the real `CadCommands.cpp` and exercise the real function. Recorded rather
  than quietly skipped: the *reason* there is no unit test here is architectural, not laziness.

## 8. Implementation log
- 2026-08-17 — **Reproduced.** `fuzz --seed 4737` → `FAIL [invariant|finite-coords]`, minimized
  cleanly to 4 lines (unlike #60, the minimizer worked here, so the transcript is its output verbatim).
- 2026-08-17 — **Mechanism 1 confirmed in the source.** `SubmitViewportPickImpl`'s
  `CirclePhase::WaitRadius` computes `r = sqrt(dx*dx + dy*dy)`; with `dy ≈ 1e38`, `dy*dy` overflows
  and `sqrt(inf) = inf`. `CommitCircle`'s only guard was `r < 1e-5f`, and **`inf < 1e-5f` is false**.
  Worth noting because it is the same trap twice: NaN also fails every comparison, so a NaN radius
  passed that guard too. The new check therefore had to go *before* it, not after.
- 2026-08-17 — **Probed the siblings the issue names, rather than trusting the recommendation.**
  Results in §2. ARC, ELLIPSE and OFFSET all committed geometry at extreme magnitudes and stayed
  finite; only LINE reproduced — and when it did, it was a **different mechanism**.
- 2026-08-17 — **Mechanism 2, found by that probing and not in the issue.** `userLinesFlat[3] = inf`
  came from `@dx,dy` resolution, not from a derived length: `ParseWorldPoint` computes
  `*ox = baseX + dx`, which overflows when both are representable but their sum is not
  (`2e+38 + 2e+38 = 4e+38`, `FLT_MAX ≈ 3.4e+38`). Reproduced for **POLYLINE** and **RECT** as well,
  since all three consume the same parser.
- 2026-08-17 — **The framing that made the fix small.** An absolute coordinate that overflows is
  *already* refused: `1e+40,0` fails stream extraction and the caller reports it. So
  `ParseWorldPoint`'s relative branch was the one path that could produce a non-finite coordinate
  from finite input, and the one path with no check — it did the addition after the validation and
  never re-validated. The guard restores the function's own guarantee, which is why no new log
  message was needed.
- 2026-08-17 — Both guards added; transcripts green; sweeps clean.

## 9. Self-verification  (verification/skills/)
- [x] **build-project** — PASS. Clean configure + build, MSVC 19.5x Release, Ninja. No new warnings;
      the `C4530`/`C4244` warnings present are pre-existing (the `CadCommands.cpp` one is the same
      try/catch site as before, shifted by the added lines).
- [x] **architecture-review** — PASS. Both guards sit in the subsystem that owns the concern; no new
      upward dependency, no new owner, no new global state. Abstraction: **none added, deliberately**
      — see §3 for why the suggested helper was rejected on REQ-301 grounds. No `gl*` outside
      Renderer/Platform. ADR-002 respected rather than worked around (§7).
- [x] **code-review** — PASS. Correctness: the finite check precedes the magnitude check, which is
      load-bearing — placed after, `inf` and `NaN` would still reach the store. Edge cases: NaN as
      well as ±inf; the absolute-parse path verified already-correct rather than assumed; no
      legitimate magnitude is refused (the guards test finiteness, never size). Error paths: both
      report — one with a message, one through the existing parse-failure path that callers already
      log, asserted by `EXPECT LOG`. Simplicity: two guards, no helper, no new file.
- [x] **dependency-audit** — n/a. No dependency added, removed or moved. `<cmath>` already in use.
- [x] **performance-review** — n/a. Two `std::isfinite` calls, one per command commit and one per
      typed relative coordinate. Neither is on a frame path, so no REQ-100 profile is affected.
- [x] **testing** — PASS. Both transcripts proven red before and green after. 401/401 ctest green.
      **5000-seed fuzz sweep: 0 failures** — the same sweep that originally produced #56–#59.
      150-seed `--roundtrip` sweep shows only `expect|SAMEFILE` (#61). No unit test, for the
      architectural reason in §7 — stated, not hidden.

## 10. Verification result
- Submitted: 2026-08-17
- Verdict:   **PASS**
- Findings:  none outstanding. ARC/ELLIPSE/OFFSET deliberately not guarded (§2), with the probe
             results recorded so a future finding can be recognised as new rather than as a
             regression.

## 11. Outcome

```
COMPLETION REPORT — TASK-065 — 2026-08-17
- Requirements satisfied:  REQ-204 (finite-coords holds across a 5000-seed sweep), REQ-201 (both
                           refusals are reported and asserted), REQ-101 (Acceptance met: yes)
- Summary:                 CIRCLE committed an infinite radius because the radius is derived from a
                           distance that overflows float while both user points are finite, and the
                           only guard was a magnitude test that inf and NaN both pass. Fixed at the
                           commit site. Probing the siblings the issue named turned up a second,
                           different mechanism — relative-coordinate resolution overflowing in
                           ParseWorldPoint, reproduced for LINE, POLYLINE and RECT — fixed at that
                           parser, where it restores a guarantee the function already made for
                           absolute input.
- Tests:                   headless.regression-59-circle-infinite-radius (the fuzzer's own minimized
                           reproducer, verbatim),
                           headless.regression-59b-relative-coord-overflow (three commands, one per
                           store) — both red before, green after; 401/401 ctest green
- Verification verdict:    PASS  (findings resolved: none outstanding)
- Assumptions:             ASSUMPTION-1 (existing parse-failure report suffices for REQ-201) —
                           VALIDATED by EXPECT LOG
- Architectural decisions: none made by Workshop (escalated: none). The IsStorableCoordinate helper
                           issue #59 suggests was considered and rejected on REQ-301 grounds — see §3
- Dependencies:            none added
- Technical debt noted:    none from this fix. Two limits recorded instead: ARC/ELLIPSE/OFFSET are
                           probed-not-reproduced rather than proven safe (§2), and ParseWorldPoint has
                           no unit test because ADR-002 keeps command TUs out of the Catch2 target
                           (§7). Removal condition for the latter: a pure-TU home for coordinate
                           parsing, which is a refactor decision, not this task's
- Build:                   clean, MSVC Release, reproducible
- Docs updated:            docs/fuzz-harness.md (findings table), TRACKER.md (BUG-017 → FIXED,
                           BUG-024 filed for the relative-coordinate mechanism)
```
