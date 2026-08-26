# TASK-111 — Clickable command variants: the mechanism (REQ-119 increment 1)

- Type:    feature
- Status:  plan
- Opened:  2026-08-25
- Owner:   Nathan Johnson

Upstream issue: chetjones003/GoSurvey#81.

## 1. Authority
- Requirements: **REQ-119** — accepted 2026-08-25 by **D-2026-08-25-m**. This task is
  **increment 1 only** (the mechanism); increment 2 (the coverage audit) is not opened.
- Also honoured: REQ-040 (the floating command bar — its Acceptance (7) already promises the
  `[A]`/`[2P]` hints keep working, so this task must not regress it), REQ-024 (the at-crosshair
  dynamic input shares `CommandInputHint`'s text), REQ-201 (a refusal states its reason).
- Acceptance: REQ-119's **Increment 1** conditions, restated in §6's test map.
- Owning subsystem: `UI` (`src/ui/` — the renderer and `CommandInputHint`). The `*FooterHint`
  prompt strings in `src/commands/` are touched only for the two defective prompts named in
  Acceptance; no command's *behaviour* is touched by this task.

## 2. Scope
- In scope:
  - grouped-form parsing (`[A/B/C]`) and uppercase-run shortcut extraction, as one **pure**
    function;
  - wrap-aware segment layout in the shared renderer;
  - the classic docked panel routed through the shared renderer;
  - deletion of the hand-rolled LINE-only link block (`CadUi.cpp:7404-7429`);
  - correcting the two prompts that currently produce an unsubmittable token.
- Out of scope:
  - **the coverage audit** — the ~190 remaining prompt strings stay as they are (REQ-119
    increment 2). This task makes the mechanism correct; it does not spread it;
  - unifying the two prompt vocabularies (DEBT-1 below);
  - any change to what a command *accepts* — every token this task emits must already be
    accepted by the handler today, verified below.
- Smallest change: one pure parsing function + its tests, one wrap-aware renderer, one call-site
  substitution in the dock, one deletion, two prompt-string edits.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / global state / public API / data
  format / unspecified algorithm?
    - [x] **No** — proceed. The renderer and its `ProcessCommandLineSubmit` click path already
          exist (`CadUi.cpp:6876`) and already cross the UI→Commands boundary in the established
          direction; this task changes *what the parser reads*, not who talks to whom. No new
          type: D-2026-08-25-m explicitly declined the `{display, shortcut, action}` table in
          favour of the existing text convention. The extracted pure function follows the
          `CommandBar.hpp` precedent (ImGui-free helpers unit-tested without a UI harness) — a
          file that already exists for exactly this purpose. No new dependency, no persisted
          state, no data-format change.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Text convention or a declared variant table? | 2026-08-25 | **Convention.** The shortcut is already encoded in the prompts' capitalization and already agrees with the handlers; a table would re-declare it at ~190 sites with no second present-day use (CLAUDE.md rule 2). Cost named in REQ-119. |
| Q2 | Whole audit at once, or mechanism first? | 2026-08-25 | **Mechanism first.** Marking up ~190 strings over a parser that mis-reads the grouped form would spread the defect, not fix it. |
| Q3 | Unify `CommandInputHint` and the `*FooterHint` family? | 2026-08-25 | **No** — mark up both in place, record as debt. Deciding which layer owns prompt text is an ownership change under `spec/architecture.md`, i.e. its own decision. |

## 5. Assumptions

```
ASSUMPTION-1: Every token this task emits is accepted by its handler in that state.
- Because:       the convention implies the shortcut from prompt text rather than declaring it
                 beside the handler, so the two can disagree.
- Risk if wrong: a link that submits a token the command rejects — exactly today's defect.
- Validate by:   read each handler before markup. DONE for all three prompts this task touches:
                 LINE a/2p (existing, working); MIRROR `NeedEraseAnswer` accepts y/yes/n/no/empty
                 (`CadCommands.cpp:16986`); LENGTHEN `TryLengthenModeToggle` accepts
                 de/delta, p/percent, t/total, dy/dynamic (`CadCommands.cpp:10010`).
                 The uppercase-run rule yields exactly Y, N, DE, P, T, DY — all accepted.
```

```
ASSUMPTION-2: No caller depends on the hint strings laying out on a single line.
- Because:       `DrawCommandLinePanel` precomputes `footerH` from `CalcTextSize(s, …, wrapW)`
                 over the same strings the renderer consumes, and the comment at
                 `CadCommands.cpp:22392` warns the text feeds three consumers (footer hint,
                 dynamic-cursor label, height calc) and must stay identical across them.
- Risk if wrong: the bar's reserved height and its rendered content disagree — a clipped footer
                 or a tall empty band, and the REQ-040 note about links being shoved out from
                 under the mouse becomes live again.
- Validate by:   drive the height calc from the SAME wrap decision the renderer makes, rather
                 than from an independent `CalcTextSize`; verify visually at a narrow dock width
                 and a wide one before submitting.
```

## 6. Plan
- Approach: add the parsing rule to `src/ui/CommandBar.hpp` as a pure, ImGui-free function
  (that file exists precisely for testable command-bar logic and is already covered by
  `CommandLineTests`). `RenderClickableCommandHint` becomes a thin ImGui shell over it, gains
  wrap-aware placement, and becomes the single renderer for both surfaces.

- Files/functions to touch:
  - `src/ui/CommandBar.hpp` — **new** `cmdbar::ParsePromptSegments(...)`: prompt text → ordered
    segments, each `{text, isLink, shortcut}`. Pure, no ImGui.
  - `tests/CommandLineTests.cpp` — cases for the new function.
  - `src/ui/CadUi.cpp`
    - `RenderClickableCommandHint` (6876) — consume the parsed segments; wrap between segments
      on `GetContentRegionAvail()`.
    - the LINE-only block (7404-7429) — **delete**; `renderHint` routes through the shared
      renderer instead.
    - `CommandInputHint` (6308) — `[Yes/No]` → `[Y]es/[N]o`.
    - the footer-height calc — must follow the renderer's wrap decision (ASSUMPTION-2).
  - `src/commands/CadCommands.cpp` — the MIRROR prompt logged at 16950 carries the same
    `[Yes/No]` text and is corrected with it, so the log and the prompt cannot disagree.

- Test approach:
  - **happy path** — `CommandLineTests`: `[A]zimuth, [2P]` → two links, shortcuts `A`/`2P`, the
    surrounding text preserved verbatim; `[DElta/Percent/Total/DYnamic]` → four links with
    shortcuts `DE`/`P`/`T`/`DY`; `[Y]es/[N]o` → two links, `Y`/`N`, with `es`/`o` as plain text.
  - **failure mode** — an unclosed `[` is emitted as literal text and produces no link (today's
    parser already does this; the test pins it); an empty group `[]` and a `[/]` produce no link
    and do not lose surrounding characters; a prompt with no brackets round-trips unchanged.
  - **token validity** — `headless.regression-119-variant-token-accepted`: every token the parser
    extracts from a prompt is **accepted by the command showing that prompt**, in that state.
    This is the ASSUMPTION-1 guard and the only thing a headless test here can actually prove.
    *(Renamed from "equivalence" per FINDING-3: a test that submits through
    `ProcessCommandLineSubmit` and compares against typing is comparing a function to itself.
    Click ≡ type is true **by construction** — the click path IS the typed path — so no test
    establishes it, and one claiming to would be worse than none. The real risk the convention
    carries is a prompt naming a token its handler rejects, which is exactly what this pins.)*
    Baseline established during the plan review, to be re-asserted as the test: MIRROR `y` erases
    the source and `n` keeps it (verified by line count); LENGTHEN `de`/`p`/`t`/`dy` each open
    their sub-prompt from `WaitSelectOrMode`.
  - **manual** — links render and hover in BOTH the floating bar and the classic dock; a
    wrapping dock prompt keeps its links on the correct line with no horizontal overflow.

- Steps:
  - [ ] 1. Write `cmdbar::ParsePromptSegments` + its `CommandLineTests` cases; red before green.
  - [ ] 2. Re-express `RenderClickableCommandHint` over it — no behaviour change yet; the
        floating bar's existing `[A]`/`[2P]` prompt must render identically.
  - [ ] 3. Add wrap-aware placement; reconcile the footer-height calc (ASSUMPTION-2).
  - [ ] 4. Route the dock's `renderHint` through the shared renderer; delete the LINE block.
  - [ ] 5. Correct the MIRROR and LENGTHEN prompts (both the hint and the logged copy).
  - [ ] 6. Add the headless equivalence transcript.
  - [ ] 7. Self-verify (§9); manual GUI pass at a narrow and a wide dock width.

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1-Q3, D-2026-08-25-m). **Tests-first** for step 1 — the parsing
  rule is pure and is the part most likely to be got subtly wrong, so it gets its test before its
  implementation. Steps 2-4 are behaviour-preserving for the floating bar by construction, and
  REQ-040's existing Acceptance (7) is the regression gate on that.

## 8. Implementation log
- 2026-08-25 opened; Authority and Plan complete; Status: plan. No code yet.
- 2026-08-25 pre-flight reading recorded in ASSUMPTION-1 — all three handlers were read before
  the markup was designed, so the shortcut rule is derived from what the commands already accept
  rather than imposed on them.
- 2026-08-25 plan review run (§10). Both defects **reproduced in the running program** rather than
  inferred, via `gosurvey_headless run`:
  - `CMD yes/no` at MIRROR's erase prompt → `"MIRROR — answer Yes or No (Enter defaults to No)."`
  - `CMD delta/percent/total/dynamic` at LENGTHEN → `"LENGTHEN — type DE, P, T, or DY …"`
  and the fix direction confirmed the same way: MIRROR `y` erases the source / `n` keeps it
  (asserted by line count, not by log text alone), and LENGTHEN `de`/`p`/`t`/`dy` each open their
  own sub-prompt. One probe failure was **the probe's error, not the product's**: MIRROR with
  erase logs `"MIRROR complete (source erased)."`, not `"MIRROR complete."` — worth knowing before
  step 6 writes the committed transcript. A second probe chained the four LENGTHEN letters in one
  session and failed: mode letters are only accepted at `WaitSelectOrMode` (or at a value prompt
  with an object latched), so each must be exercised from a fresh prompt. Both traps are now
  written into §6's baseline so the committed test does not rediscover them.
- 2026-08-25 Status stays `plan` — cleared to implement, no code written yet.

## 9. Self-verification
- [ ] build-project        —
- [ ] architecture-review  —
- [ ] code-review          —
- [ ] dependency-audit     — n-a (no dependency change)
- [ ] performance-review   — n-a (a per-frame parse of one short prompt string; if it ever shows,
      cache per prompt pointer — noted, deliberately not pre-optimized)
- [ ] testing              —

## 10. Verification result

### Plan review (workflow step 3) — the plan, not the implementation
- Submitted:  2026-08-25
- Verdict:    **SPEC GAP** → resolved by **D-2026-08-25-n**; plan amended, now cleared to implement.
- Gate:       build PASS · 541/541 Catch2 · 595/595 ctest (1 known-disabled, #63)
- Domains:    arch ✓ · quality ✓ · deps ✓ (n-a) · perf ✓ (n-a)
- Findings:
  - **FINDING-1 — advisory, WITHDRAWN.** Verification moved to block the grouped-form parser as
    speculative: one call site in the hint families, and `[DE]lta/[P]ercent/[T]otal/[DY]namic`
    parses correctly under the **existing** loop with no change (proven by simulating it).
    Widening the grep past the hint families collapsed the finding — the grouped form has **six**
    present-day uses (MIRROR, LENGTHEN, FILLET ×2, CHAMFER ×2), so invariant 4 is satisfied and
    step 1's parser work stands. Recorded because stopping at the first grep would have blocked
    this plan on a false finding.
  - **FINDING-2 — blocking, SPEC GAP → resolved, then partly overtaken by REQ-304.** REQ-119
    named two prompt surfaces; a third exists (`log.push_back`), and at the time it was the only
    one FILLET/CHAMFER/ELEV used. D-2026-08-25-n kept the log plain text and put the missing live
    prompts into increment 2. Rebasing this task onto `beta` then found **REQ-304 had already
    written them** — same enum audit, same `Pan`/`Orbit` exclusion, all eight commands given a
    live prompt via `DrawingExtrasFooterHint` — so that half of the amendment is dropped as done
    and increment 2 is a pure markup pass. **No change to this task's scope**: increment 1 was
    never affected. REQ-304 did, however, move five grouped-variant prompts onto the clickable
    path, so `beta` now renders **seven** dead links where it rendered two — increment 1 repairs
    all seven by fixing the reader, with no markup change.
  - **FINDING-3 — advisory, FIXED in §6.** The "equivalence" transcript was circular. Renamed to
    token validity, with what it actually guards stated.
- Verified clean, so the implementation need not re-derive it:
  - no stray `[` anywhere in the hint corpus → routing every dock hint through the shared renderer
    creates **no** accidental links (this was the main risk in step 4);
  - click-during-render is safe: all three log-iteration sites (`6932`, `7058`, `7123`) complete
    **before** the footer hints render at `7404`, so TASK-070's re-entrancy bug does not recur;
  - the uppercase-run rule holds against every present-day grouped string, including its least
    obvious case `No trim` → `N` (`CadCommands.cpp:12469` accepts `n`/`notrim`/`no trim`).
  - both defects reproduced in the running program, not inferred — see §8.

### Implementation review
- Submitted:
- Verdict:
- Findings:

## 11. Outcome
- Requirements satisfied:
- Tests added:
- Refactors:
- Docs updated:
- Done:

## 12. Technical debt

```
DEBT-1: Two parallel prompt vocabularies.
- What:      `CommandInputHint` (src/ui/CadUi.cpp, ~90 strings, UI layer) and the 12
             `*FooterHint` functions (src/commands/CadCommands.cpp, ~96 strings, Commands layer)
             describe the same command states in different words.
- Forced by: collapsing them decides which layer OWNS prompt text — an ownership change under
             spec/architecture.md, so an architectural decision, not a Workshop one. Folding it
             into REQ-119 would roughly double the REQ and grow #81 into a change it never asked
             for. The user chose this explicitly (Q3).
- Cost:      every prompt edit is two edits, and the two can drift silently; REQ-119 increment 2
             pays this cost ~190 times.
- Remove by: a SPEC GAP naming one owner for prompt text, filed BEFORE increment 2 opens —
             increment 2 is the moment the cost is actually incurred, so that is the moment the
             decision is worth making.
- Follow-up: file as its own issue against the spec; referenced from REQ-119 increment 2.
```
