# TASK-064 — Erasing the last polyline must not write an unopenable `.gs`

- Type:    bug
- Status:  done
- Opened:  2026-08-17
- Owner:   Claude Code (AI assistant)

## 1. Authority
- Goal:         none (defect repair, not roadmap-driven)
- Requirements: **REQ-079** — first acceptance condition, "a file at the current version loads with
  no migration and is byte-identical on resave." Here the file does not load at all, which is that
  condition failing in its most severe form. Secondarily **REQ-204**, whose acceptance requires each
  listed invariant to have a fixture that deliberately breaks it and proves the check fires — the
  `polyline-offsets` invariant did not catch this state and so was not the check it appeared to be.
- Constraints:  CLAUDE.md "Additional rules" 1–8. No new dependency, no new abstraction.
- Acceptance:
  - erasing the drawing's only polyline leaves a document that saves and reopens;
  - the resaved file is byte-identical (REQ-079 in full, not merely "openable");
  - the `polyline-offsets` invariant fires on a one-entry table, with a fixture proving it;
  - the invariant stays silent on the legitimate empty table (no false positive);
  - no other polyline behaviour changes.
- Owning subsystem: **Commands** (`ErasePolylineByIndex`), **util** (`docinvariants`)

## 2. Scope
- In scope: the offset-table rebuild in `ErasePolylineByIndex`, the two `BENCH` sites that set the
  same invalid state, the `polyline-offsets` invariant, and a regression transcript.
- Out of scope:
  - **Issue #61** (`expect|SAMEFILE`, large-coordinate resave idempotence). It is the failure that
    seed 28 surfaces *after* this fix and is a separate, undiagnosed bug — see §8.
  - Making the `io` failure signature carry the reader's reason. A real harness weakness this task
    exposed, recorded in `docs/fuzz-harness.md` and worth its own task; fixing it here would mean
    changing the harness in the same change that uses the harness as evidence.
  - Paper-space offsets in the invariant. `paperPolyOffsets` has no invariant coverage at all, which
    is a coverage gap rather than this defect.
- Smallest change: clear a one-entry table instead of leaving the seeded sentinel.

## 3. Architectural boundary check  (workflow.md §4)
- Does this need a NEW abstraction / layer / dependency / ownership change / global state /
  public-API or data-format change / algorithm the spec didn't specify?
    - [x] **No — proceed.** No signature changed, nothing new owned, no data-format change: the
          fix makes the store produce the representation the format **already** specifies, which is
          the opposite of a format change. The invariant addition is a new check inside an existing
          pure function, not a new abstraction.

## 4. Questions  (workflow.md §5)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| (none) | The requirement, the root cause and the fix shape were all unambiguous. The one judgment call — whether to also fix the two BENCH sites — is recorded as ASSUMPTION-1 rather than asked, because leaving them would have made the invariant this task adds knowingly false in-tree. | — | — |

## 5. Assumptions  (workflow.md §8)
```
ASSUMPTION-1: The two BENCH sites that set userPolylineOffsets to {0} are in scope.
- What: StartFrameBudgetBench's mesh and surface profiles both did
  `st.userPolylineOffsets.assign(1, 0)` after emptying the vertex store — the exact invalid state
  this task is fixing. Both are now `.clear()`.
- Because: this task adds an invariant asserting that a one-entry table cannot occur. Leaving two
  in-tree sites that create it would make the new check false by construction — a check that fires
  on the codebase's own behaviour gets waived, and a waived check is not a check. They are also the
  same defect, not a related one.
- Risk if wrong: BENCH's two large profiles would misbehave if any reader of the store could not
  handle an empty table. Retired rather than accepted — see the verification note below.
- Validate by: grepped every read site of userPolylineOffsets (and paperPolyOffsets) for
  `size() - 1`. All 20+ guard the empty case explicitly (`size() > 0 ? size() - 1 : 0`,
  `empty() ? 0 :`, or `std::max(0, (int)size() - 1)`). Empty is the store's normal, well-supported
  state — a new drawing has one — which is exactly why {0} was the anomaly. VALIDATED 2026-08-17.
```

## 6. Plan  (workflow.md §6 — written before any code)
- Approach: reproduce deterministically, confirm the mechanism against the source, then make the
  writer agree with the reader — not the reader with the writer, since the reader is correct.
- Files to touch:
  - `src/commands/CadCommands.cpp` — `ErasePolylineByIndex`; the two `StartFrameBudgetBench` sites
  - `src/util/docinvariants.cpp` — the reader's rule as an invariant
  - `tests/DocInvariantsTests.cpp` — a broken fixture + a silence fixture
  - `tests/headless/transcripts/regression-60-erase-last-polyline.txt` — new
  - `docs/fuzz-harness.md`, `TRACKER.md` — record the finding and the harness weakness
- Test approach: regression transcript first, proven to fail against the unpatched build; plus two
  Catch2 fixtures for the new invariant (fires on `{0}`, silent on empty).
- Steps:
  - [x] 1. Reproduce seed 28 and confirm the mechanism in the source.
  - [x] 2. Write a deterministic reproducer; prove it fails before the fix.
  - [x] 3. Fix `ErasePolylineByIndex`; fix the two BENCH sites.
  - [x] 4. Extend the `polyline-offsets` invariant; add both fixtures.
  - [x] 5. Build, run the transcript, run the full suite, re-sweep the fuzzer.
  - [x] 6. Update docs and the tracker.

## 7. Workflow-specific notes
- Bug: root cause identified **with evidence before any fix** (§8). No speculative change, no
  widened tolerance, no suppressed symptom — the reader's rule was strengthened, not relaxed.

## 8. Implementation log
- 2026-08-17 — **Reproduced.** `gosurvey_headless fuzz --seed 28 --roundtrip` →
  `FAIL [io|OPEN failed]`.
- 2026-08-17 — **The automatic minimizer degenerated**, exactly as issue #60 predicted: 126 → 2
  lines, `NEW` + `OPEN %OUT%/rt-a.gs`. That reproducer fails because the file is *absent*, not
  because it is rejected — `io|OPEN failed` covers both. So the reproducer was written by hand.
- 2026-08-17 — **Two dead ends worth recording, because they are properties of the driver, not of
  this bug.** A transcript cannot select an entity the way a user does:
  - `PICK 50 0` then `CMD DELETE` selected nothing. `SubmitViewportPickImpl` has **no idle
    single-entity-select branch** — click-to-select lives in the UI layer, above the Commands entry
    point the driver drives.
  - `CMD DELETE` then two corner picks also selected nothing. The window-select anchor is set by
    `BeginSelectionBoxCorner`, called from the UI's mouse-down; the driver has no verb for it, so
    `selBoxWaitingSecond` is never true and both picks fall through silently.

  So `DELETE` is not reachable with a selection from a transcript. **`OVERKILL` is** — it needs no
  selection, and it reaches the *same* `ErasePolylineByIndex`. A polyline whose vertices all coincide
  collapses to one distinct vertex and hits OVERKILL's `clean.size() < 2` erase case. That is the
  reproducer: it tests the defect rather than the plumbing. (This gap in headless coverage — no way
  to drive selection — is a real REQ-203 limitation and is not this task's to close.)
- 2026-08-17 — **Root cause confirmed in the source**, not inferred. `ErasePolylineByIndex`
  (`CadCommands.cpp:8354`) seeds `newOff` with 0 and appends one entry per *surviving* polyline; with
  `np == 1` and `pi == 0` the loop body never runs and the table is left `{0}`.
- 2026-08-17 — **Direct evidence.** The saved file the writer reported as successful contains
  `"polylineOffsets":[0]` with `"polylineVerts": []`, and the reader refuses it with
  `.gs: polylineOffsets invalid (expected empty or at least two entries).`
- 2026-08-17 — **The harness gap is confirmed, not assumed.** The reproducer's `CHECK ALL` ran
  *between* the corruption and the save and **passed** on the unpatched build. `{0}` with zero
  vertices starts at 0, never goes backwards, and ends at the vertex count, so all three existing
  checks were satisfied. The reader's rule had to become its own check.
- 2026-08-17 — **In-tree precedent found.** The paper-space sibling `ErasePaperPolyline`
  (`CadCommands.cpp:577`) has always ended with
  `if (L.paperPolyOffsets.size() == 1) L.paperPolyOffsets.clear();`, commented "last polyline removed
  → empty the table entirely". The model store was the outlier, so the fix is the existing
  convention rather than a new one.
- 2026-08-17 — Fixed, invariant extended, fixtures added, docs and tracker updated.
- 2026-08-17 — **Note on what seed 28 does now:** it fails with a *different* signature,
  `expect|SAMEFILE`, minimizing cleanly to 8 lines around a `1e+12` coordinate. That is issue #61
  (BUG-019), which was already open and is not regressed by this change — it was standing behind
  #60 and is now the first thing that seed hits. Recorded rather than glossed, because "the seed
  still fails" would otherwise read as the fix not working.

## 9. Self-verification  (verification/skills/)
- [x] **build-project** — PASS. Clean configure + build, 177/177 targets, MSVC 19.5x Release, Ninja.
      No new warnings; the `C4530` warnings present are pre-existing and in unrelated translation
      units (`TelemetryPing.cpp`, `stlimport.cpp`, `gltfimport.cpp`).
- [x] **architecture-review** — PASS. Layering: the fix is in the subsystem that owns the store
      (Commands) and the check is in the pure `util` module that already owns invariants; no new
      upward dependency. Ownership: nothing new owned. State: no new global mutable state. Abstraction:
      none added — REQ-301 not engaged. Boundaries: no `gl*` outside Renderer/Platform; no
      previously-rejected approach reintroduced.
- [x] **code-review** — PASS. Correctness: REQ-079's first acceptance condition restored and proven
      by `EXPECT SAMEFILE`, not merely by the file opening. Edge cases: the empty table (the
      legitimate state) has its own silence fixture; the early-out guard at the top of
      `ErasePolylineByIndex` already rejects every `pi` when the table has one entry. No error path
      swallowed — a check was *added*. Simplicity: two lines plus a comment explaining why; matches
      the paper-space sibling verbatim rather than inventing a second convention.
- [x] **dependency-audit** — n/a. No dependency added, removed or moved.
- [x] **performance-review** — n/a for the fix. The invariant gains one `size()` comparison inside a
      function that already walks the offset table, and it never links into `GoSurvey.exe`. The two
      BENCH sites are touched but not on a measured path — they run once during setup, before the
      frame loop the REQ-100 figures come from, so no profile needs re-measuring.
- [x] **testing** — PASS. Regression transcript proven red before the fix and green after; both new
      invariant fixtures assert the new rule directly; 399/399 ctest green; 200-seed fuzz sweep clean
      and a 120-seed round-trip sweep free of the `io|OPEN failed` signature entirely.

## 10. Verification result
- Submitted: 2026-08-17
- Verdict:   **PASS**
- Findings:  none outstanding. Two items deliberately left open and recorded rather than fixed here:
  the coarse `io` failure signature (`docs/fuzz-harness.md`) and issue #61.

## 11. Outcome

```
COMPLETION REPORT — TASK-064 — 2026-08-17
- Requirements satisfied:  REQ-079 (Acceptance met: yes — the file both reopens and resaves
                           byte-identically); REQ-204 (the polyline-offsets invariant now has the
                           reader's rule and a fixture that proves it fires)
- Summary:                 Erasing a drawing's only polyline left the CSR offset table as {0}, which
                           the .gs writer serialised and the reader refused — a drawing that saved
                           successfully and could never be reopened. The table is now cleared, and
                           the reader's own rule became an invariant so the corruption is caught
                           where it happens rather than at the load that rejects the file.
- Tests:                   headless.regression-60-erase-last-polyline (new; red before, green after),
                           "polyline-offsets fires on a single-entry table",
                           "polyline-offsets stays silent on an empty table" — 399/399 ctest green
- Verification verdict:    PASS  (findings resolved: none outstanding)
- Assumptions:             ASSUMPTION-1 (BENCH sites in scope) — VALIDATED
- Architectural decisions: none made by Workshop (escalated: none)
- Dependencies:            none added
- Technical debt noted:    the `io` failure signature does not distinguish "file absent" from "file
                           rejected", which is why the minimizer degenerated on this bug. Recorded in
                           docs/fuzz-harness.md. Removal condition: the signature carries the
                           reader's reason. Also: the headless driver cannot drive selection at all
                           (no click-select, no box anchor), so DELETE is untestable from a
                           transcript — a REQ-203 coverage gap, newly documented in §8.
- Build:                   clean, 177/177, MSVC Release, reproducible
- Docs updated:            docs/fuzz-harness.md (findings table + a sixth harness weakness),
                           TRACKER.md (BUG-018 → FIXED)
```
