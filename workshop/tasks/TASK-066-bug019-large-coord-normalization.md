# TASK-066 — Large-coordinate normalization vs REQ-079 byte-identity

- Type:    bug
- Status:  done
- Opened:  2026-08-17
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         none (defect repair, not roadmap-driven)
- Requirements: **REQ-079** (first acceptance condition — and the requirement that turned out to be
  the defect), **REQ-204** (the `gs-roundtrip` oracle), **REQ-101** (precision).
- Constraints:  CLAUDE.md "Additional rules" 1–8; **the spec may not be edited to excuse code** —
  which is exactly the trap this task had to avoid, see §3.
- Acceptance:
  - the cause of the resave difference is confirmed with evidence, not hypothesised;
  - REQ-079 either holds or is amended **by a recorded decision**, never silently;
  - normalization is proven idempotent by a test;
  - the max-Y threshold defect is fixed with its own test;
  - a `--roundtrip` sweep is clean, which is the written precondition for flipping
    `emitRoundTrip` to on.
- Owning subsystem: **spec** (REQ-079), **tests/headless** (the oracle), **Commands**
  (`CadCoordinateFrame`)

## 2. Scope
- In scope: confirming the diagnosis; the max-Y threshold typo; the REQ-079 amendment and its
  recorded decision; the oracle change in both the generator and the transcript; the `emitRoundTrip`
  default and a `--no-roundtrip` flag; regression tests; docs.
- Out of scope: **option (A) — rebasing at entry rather than at load.** It is arguably the cleanest
  design, and it was put to the user as one of three options; the user chose (B). Recorded here so
  the road not taken is legible: (A) would make a stored `.gs` always already optimal, at the cost of
  touching every route large coordinates arrive by and moving the user's frame mid-session.
- Out of scope: option (C), the `originNormalized` format field. Also offered, also declined.
- Smallest change: one character of real logic (`mnY` → `mxY`), plus a spec amendment and the oracle
  that encoded the old rule.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **Yes — and it was escalated rather than decided.** The question "does REQ-079's
          byte-identity condition apply to a load that normalizes storage?" is not the Workshop's to
          settle: it is observable behaviour with format implications. Marked **SPEC GAP**, three
          options were put to the user with a recommendation, and the answer is recorded as decision
          **D-2026-08-17-a** in `spec/project.md`. Only then was anything changed.
    - **This is the case CLAUDE.md's central rule is aimed at**, so it is worth being explicit about
      why amending the spec here is not "editing the spec to excuse code". The evidence came first
      and it showed the *requirement* was wrong: REQ-079 demanded byte-identity, and the local-storage
      precision design makes that impossible for state-plane data. Two independent things stopped
      this from being a rubber stamp — the amendment **adds** a condition (idempotence) rather than
      only removing one, and the decision was the user's, offered against two alternatives that
      would have kept the requirement intact. The spec got stronger and more honest, not more
      permissive.

## 4. Questions  (workflow.md §5)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | The rebase on load changes the origin and every local coordinate, so the first resave can never be byte-identical. Amend REQ-079 and compare the 2nd/3rd saves (B); rebase at entry instead (A); or record "already normalized" in the file (C)? | 2026-08-17 | **(B)** — user decision. Recorded as D-2026-08-17-a in the decision log; REQ-079 Statement + acceptance amended; oracle and default changed. |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: The rebase cannot make precision worse than the file already had.
- What: ShiftAllStorageBy rounds each shifted coordinate back to float, so the round trip does move
  the world position — measured 1874.5 units on one endpoint of #61's reproducer. The claim is that
  this drift is bounded by the float spacing of the REBASED coordinate, which is never coarser than
  the spacing of the value it replaced (the rebase moves coordinates toward zero, and float spacing
  shrinks toward zero).
- Because: it is the load-bearing premise of choosing (B). If the rebase were genuinely lossy,
  amending REQ-079 would be blessing silent data damage, and (A) or (C) would be the only honest
  answers.
- Risk if wrong: the spec amendment would sanction a load that degrades the user's drawing.
- Validate by: the observed drift (1874.5) is well inside half the float spacing at 5e11 (16384), and
  the worked realistic case runs the other way — a 5,000 ft survey at easting 2e6 goes from ~0.25 ft
  quantization to ~0.0002 ft. Stated in REQ-079's Statement so it is a spec claim that can be
  challenged, not a comment. VALIDATED 2026-08-17.
```

## 6. Plan  (workflow.md §6 — written before any code)
- Steps:
  - [x] 1. Reproduce; **diff the two files** rather than reasoning about the load path.
  - [x] 2. Confirm or refute the hypothesis; quantify whether geometry actually moves.
  - [x] 3. Fix the max-Y threshold typo (correct under every option, so safe to do first) + test.
  - [x] 4. Measure the typo fix's effect on the failure rate.
  - [x] 5. **Escalate the SPEC GAP**; do not proceed past here on a guess.
  - [x] 6. Implement the decision: REQ-079, decision log, oracle (generator + transcript), default.
  - [x] 7. Sweep to the written precondition before flipping `emitRoundTrip`.
  - [x] 8. Docs and tracker.

## 7. Workflow-specific notes
- Bug: root cause confirmed with evidence before any change. The one code fix (the typo) was made
  before the escalation *only* because it is correct regardless of how the SPEC GAP resolved — the
  threshold must consider all four extents whenever it runs.

## 8. Implementation log
- 2026-08-17 — **Reproduced**, then diffed `rt-a.gs` against `rt-b.gs`, which is what turned the
  issue's hypothesis into a diagnosis:
  ```
  A: worldDocumentOrigin (0, 0)                      local y = -999999995904
  B: worldDocumentOrigin (106.625, -499999998125.5)  local y = -499999995904
  ```
- 2026-08-17 — **Traced it to the actual caller, and the first guess was wrong.** `GsIo` reads the
  stored origin verbatim (`GsIo.cpp:951`), and the only `ApplyDocumentOriginRebase` /
  `RebaseDrawingToLocalOrigin` callers are in **DxfIo.cpp** — nothing in the `.gs` path. The rebase
  is `MaybeRebaseLargeCoordinates`, called at the *end* of load (`GsIo.cpp:1487`), which fires only
  when the stored origin is exactly `(0,0)` and the magnitude clears the threshold.
- 2026-08-17 — **Checked the issue's optimistic branch instead of accepting it.** #61 says "if the
  hypothesis holds the round trip is geometrically faithful and only the origin/local split differs."
  It is not faithful — reconstructing world coordinates from both files shows one endpoint moving
  **1874.5 units**. That looked damning, and chasing it is what produced the real answer
  (ASSUMPTION-1): the drift is bounded by the float spacing of the rebased coordinate, so it is
  always within the imprecision the file already carried, and for realistic data the rebase is a
  large *improvement*. Neither the issue's optimistic reading nor the alarming raw number is right.
- 2026-08-17 — **Second defect found, in the same function.** The threshold read
  `std::max({fabs(mnX), fabs(mxX), fabs(mnY), fabs(mnY)})` — `mxY` never considered. Proven by probe:
  `LINE (0,0)→(0,1e+12)` was **not** rebased while `LINE (0,0)→(1e+12,0)` was. Filed as BUG-025.
- 2026-08-17 — **Measured the interaction before deciding anything.** Fixing the typo took the
  `gs-roundtrip` failure rate *up*, 48/150 → 53/150 seeds, because more drawings then correctly
  rebase and every rebase broke the old rule. The two defects therefore had to be resolved together.
- 2026-08-17 — **SPEC GAP escalated** with three options and a recommendation. User chose **(B)**.
- 2026-08-17 — Implemented (B). Found and updated **both** places encoding the old rule: the
  generator's emission and `transcripts/gs-roundtrip.txt`. The transcript's own geometry is small, so
  no normalization fires in it — it was still rewritten to the B-vs-C form so the two spellings of
  the round trip state the same rule.
- 2026-08-17 — **Flipped `emitRoundTrip` to on by default, but only after satisfying that option's
  own written precondition**, which was "flip when a `--roundtrip` sweep comes back clean, not when a
  particular issue closes". 1000-seed sweep: **0 failures** (was ~325). Then 2000 seeds with the new
  default: also 0. Added `--no-roundtrip`, and listed both flags in the usage text, which had never
  mentioned `--roundtrip` at all.

## 9. Self-verification  (verification/skills/)
- [x] **build-project** — PASS. Clean configure + build, MSVC 19.5x Release, Ninja. No new warnings.
- [x] **architecture-review** — PASS. The behaviour question was escalated, not decided in the
      Workshop (§3). No new abstraction, dependency, owner or global state; **no data-format change**
      — option (C), the only one that would have added a field, was declined. The code change is one
      identifier inside an existing function.
- [x] **code-review** — PASS. Correctness: `mxY` restores the intent, verified by the asymmetry probe
      rather than by inspection. Edge cases: the oracle's B-vs-C form still catches everything A-vs-B
      caught (a field written but not read, or read but not written) while no longer failing on a
      transformation the format is entitled to make. Error paths: unchanged — the normalization was
      already reported to the user, which is what made carving it out defensible.
- [x] **dependency-audit** — n/a. Nothing added, removed or moved.
- [x] **performance-review** — n/a for the product. The fuzzer now does two extra saves and one extra
      load per seed; `fuzz-smoke` went 0.36 s → 0.45 s, which is the cost of the oracle being on by
      default and is the point.
- [x] **testing** — PASS. `regression-61a` red before / green after; `regression-61` pins idempotence;
      `gs-roundtrip` updated and green. **403/403 ctest green.** 1000-seed `--roundtrip` sweep: 0
      failures. 2000-seed sweep with the new default: 0 failures. `--no-roundtrip` verified to omit
      the round trip.

## 10. Verification result
- Submitted: 2026-08-17
- Verdict:   **PASS**
- Findings:  none outstanding. Options (A) and (C) are recorded as declined alternatives (§2), not as
             debt — (A) remains the cleanest design if the entry-time rebase is ever wanted for its
             own sake.

## 11. Outcome

```
COMPLETION REPORT — TASK-066 — 2026-08-17
- Requirements satisfied:  REQ-079 (amended by D-2026-08-17-a; both conditions now met, including the
                           new idempotence condition), REQ-204 (the gs-roundtrip oracle is correct and
                           now runs by default), REQ-101 (precision claim stated and validated)
- Summary:                 #61's hypothesis was correct — MaybeRebaseLargeCoordinates normalizes the
                           document origin on load, so the first resave differs. But it is the
                           local-storage precision design working, not a defect: the rebase's drift is
                           bounded by the float spacing of the rebased coordinate and for real survey
                           data improves precision by ~1000x. REQ-079 was asking the format to promise
                           something that design contradicts, so the requirement was amended by
                           recorded decision — with idempotence added in place of byte-identity, which
                           is the stronger and more testable promise. A separate typo in the same
                           function (mxY never read) was found and fixed.
- Tests:                   headless.regression-61-large-coord-normalization (new),
                           headless.regression-61a-rebase-threshold-max-y (new, red before / green
                           after), headless.gs-roundtrip (updated) — 403/403 ctest green
- Verification verdict:    PASS  (findings resolved: none outstanding)
- Assumptions:             ASSUMPTION-1 (the rebase cannot worsen precision) — VALIDATED, and promoted
                           into REQ-079's Statement so it is challengeable
- Architectural decisions: ONE, and it was escalated, not made here — D-2026-08-17-a, recorded in
                           spec/project.md after the user chose between three options
- Dependencies:            none added
- Technical debt noted:    none. Options (A) and (C) are declined alternatives, recorded in §2
- Build:                   clean, MSVC Release, reproducible
- Docs updated:            spec/requirements.md (REQ-079 Statement + acceptance + revision),
                           spec/project.md (decision log), docs/fuzz-harness.md (oracle table,
                           staging, findings), TRACKER.md (BUG-019 resolved, BUG-025 filed)
- Follow-on unblocked:     round-trip fuzzing is on by default, so the next round-trip defect will be
                           found by the next sweep instead of being buried
```
