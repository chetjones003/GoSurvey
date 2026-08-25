# TASK-111 — Clickable command variants: the mechanism (REQ-119 increment 1)

- Type:    feature
- Status:  self-verify (manual GUI pass outstanding)
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
- Owning subsystem: `UI` (`src/ui/` — the renderer and the parse rule). **As built, `src/commands/`
  was not touched at all** — the plan expected two prompt-string edits there and in
  `CommandInputHint`; neither turned out to be necessary (§8). No command's behaviour changes.

## 2. Scope
- In scope:
  - grouped-form parsing (`[A/B/C]`) and uppercase-run shortcut extraction, as one **pure**
    function;
  - wrap-aware segment layout in the shared renderer;
  - the classic docked panel routed through the shared renderer;
  - deletion of the hand-rolled LINE-only link block (`CadUi.cpp:7404-7429`);
  - fixing the two prompts that currently produce an unsubmittable token — **as built, by the
    parser reading them correctly, with no edit to either prompt string** (§8).
- Out of scope:
  - **the coverage audit** — the ~190 remaining prompt strings stay as they are (REQ-119
    increment 2). This task makes the mechanism correct; it does not spread it;
  - unifying the two prompt vocabularies (DEBT-1 below);
  - any change to what a command *accepts* — every token this task emits must already be
    accepted by the handler today, verified below.
- Smallest change: one pure parsing function + its tests, one wrap-aware renderer, one call-site
  substitution in the dock, one deletion. *(As built it was smaller still — the two prompt-string
  edits proved unnecessary.)*

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
    - the renderer (6876), renamed `LayoutCommandHint` — consume the parsed segments, wrap, and
      **return the height**, so the footer reservation can run the same layout.
    - the LINE-only block (7404-7429) — **delete**; `renderHint` routes through the shared
      renderer instead.
    - ~~`CommandInputHint` (6308) — `[Yes/No]` → `[Y]es/[N]o`.~~ **Not needed** (§8).
    - the footer-height calc — follows the renderer's wrap decision (ASSUMPTION-2), by calling
      the same function with `draw=false`.
  - ~~`src/commands/CadCommands.cpp` — the logged MIRROR prompt.~~ **Not touched** (§8).

- Test approach:
  - **happy path** — `CommandLineTests`: `[A]zimuth, [2P]` → two links, shortcuts `A`/`2P`, the
    surrounding text preserved verbatim; `[DElta/Percent/Total/DYnamic]` → four links with
    shortcuts `DE`/`P`/`T`/`DY`; `[Yes/No]` → two links, `Y`/`N`, with the brackets and `/`
    rendered as plain text. Every case also round-trips the segments back to the original
    string, so parsing can neither lose nor duplicate what the user reads.
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
  - [x] 1. Write `cmdbar::ParsePromptSegments` + its `CommandLineTests` cases; red before green.
  - [x] 2. Re-express the renderer over it (now `LayoutCommandHint`) — the floating bar's
        existing `[A]`/`[2P]` prompt renders identically (`wrapW = 0`, so it cannot reflow).
  - [x] 3. Add wrap-aware placement; reconcile the footer-height calc (ASSUMPTION-2).
  - [x] 4. Route the dock's `renderHint` through the shared renderer; delete the LINE block.
  - [x] ~~5. Correct the MIRROR and LENGTHEN prompts.~~ **Dropped — not needed.** See §8.
  - [x] 6. Add the headless token-validity transcript.
  - [ ] 7. Self-verify (§9) — done except the **manual GUI pass**, which is outstanding (§9).

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

### Implementation — 2026-08-25

- **Step 5 dropped: no prompt string was edited.** The plan assumed the two defective prompts
  had to be rewritten into the inline form (`[Y]es/[N]o`). Implementing the grouped form showed
  that assumption was wrong — the parser reads AutoCAD's existing `[Yes/No]` and
  `[DElta/Percent/Total/DYnamic]` **directly**, so the defect is fixed entirely in the parser
  with **zero prompt churn**. Three consequences, all improvements:
  - `FILLET`/`CHAMFER`'s existing `[Radius/Trim]`, `[Trim/No trim]`, `[Distance/Angle/Trim]`
    become correct for free the moment increment 2 gives them a live prompt entry — no markup
    pass needed for them at all;
  - the click target is the whole option word (`DElta`, not `[DE]`), which is #81's own UX
    requirement — "easy to click without accidentally selecting adjacent command text";
  - nothing that reads these strings elsewhere (the REQ-024 dynamic-cursor label, the logged
    copies at `CadCommands.cpp:9092`/`16950`) changes, so there is no second copy to keep in
    step with a rewritten prompt.

  **Deviation from REQ-119's letter, recorded rather than glossed:** the increment-1 Acceptance
  names `Erase source objects? [Y]es/[N]o <N>:` as the prompt that must render two links. The
  shipped prompt keeps `[Yes/No]` and renders two links from it. The *observable* condition —
  two links, `[Y]` erases the source, `[N]` does not — is met exactly, and is asserted in
  `regression-119-variant-token-accepted`; only the illustrative prompt text differs, and the
  REQ Statement already lists the grouped form as recognized. Flagged for the user in case the
  literal text was wanted.

- **Steps 1-4 as planned.** `cmdbar::ParsePromptSegments`/`VariantShortcut` in `CommandBar.hpp`
  (pure, ImGui-free, beside the existing `HistoryAlpha`/`LogTailStart` helpers);
  `LayoutCommandHint` in `CadUi.cpp` is the single renderer for both surfaces, returning the
  height it occupies so ASSUMPTION-2 is discharged **structurally**: `wrappedBlockH` calls the
  same function with `draw=false`, so the reserved height and the drawn content run one layout
  and cannot disagree about line breaks. The hand-rolled LINE block (25 lines) is deleted; its
  guard condition was redundant — `LineCommandFooterHint` only returns the bracketed string in
  that state, so the string itself is the condition.

- **Wrapping is word-level for plain runs, atomic for links.** REQ-119's Statement says "breaks
  between segments", but that alone cannot satisfy its own Acceptance ("no horizontal overflow")
  when a single plain run is wider than the panel — which TRIM's and POLYLINE's prompts already
  are. Plain text is still emitted **whole whenever it fits**, so today's exact spacing is
  preserved in every existing case; splitting happens only on the wrap path.

- **Rebased onto `beta` after implementation (2026-08-25).** This work was developed against
  `fix/issue84-paper-polyline-routing`, which turned out to be 11 commits behind `beta` — the
  local `upstream` remote-tracking refs were stale, which also produced a false "beta is missing
  PR #85's merge" reading earlier in this task. PR #85 had in fact merged cleanly (`f7dd1f4`).
  Rebasing required renumbering (TASK-110→**111**, `D-…-k`/`-l`→**`-m`/`-n`** — the earlier
  numbers belong to REQ-303/REQ-304 work) and re-resolving the `CadCommands.cpp` citations, which
  shifted by +33/+44 (`CadUi.cpp` did not move at all).

  **The rebase strengthened this increment rather than disturbing it.** REQ-304 moved five
  grouped-variant prompts onto the live clickable path — `FILLET` `[Radius/Trim]` and
  `[Trim/No trim]`, `CHAMFER` `[Distance/Angle/Trim]` (two variants) and `[Trim/No trim]` — every
  one of which the old first-`]` reader turns into a single link submitting a token its own
  handler rejects. `beta` therefore ships **seven** dead links, not two, and all seven are
  repaired here with **no markup change at all** — exactly the "for free" consequence noted above,
  written before REQ-304 was known about. The `[req119]` unit cases already assert FILLET's and
  CHAMFER's extractions, so the coverage predated the rebase that made them live.

- **Red before green.** The `[req119]` cases fail against a first-`]` parser by construction:
  it yields one link `[Yes/No]` → `yes/no`, where the test requires `Yes`→`Y` and `No`→`N`.
  That old behaviour was captured directly from the shipped loop during the plan review, and
  the transcript keeps its rejection as a live assertion so a revert re-fails rather than
  silently shipping a dead link.

## 9. Self-verification
- [x] build-project        — **PASS.** Clean; no new warnings (the C4244s in `CadUi.cpp` are
      pre-existing and in untouched code).
- [x] architecture-review  — **PASS.** No §11 invariant touched. The parse rule sits in `ui/`
      beside the helpers it joins, and the UI→Commands call direction is unchanged (the click
      path already went through `ProcessCommandLineSubmit`). No new type crosses the boundary:
      `PromptSegment` is local to the renderer and never reaches Commands. No global state, no
      dependency, no data-format change. Invariant 4: `ParsePromptSegments` has two present-day
      call sites (the floating bar and the dock) plus the measure path — it is not speculative.
- [x] code-review          — **PASS.** Failure paths are explicit and tested: an unclosed `[`
      and a group with nothing submittable stay literal rather than becoming a link that submits
      an empty token — a dead link that *looks* actionable is worse than plain text. The
      word-split loop cannot spin (`e > i` always holds while `i < size`, checked both ways).
      The submit buffer is `snprintf`-bounded.
- [x] dependency-audit     — n-a (no dependency change; `<string>`/`<vector>`/`<cctype>` added
      to a header that is already C++ standard-library only).
- [x] performance-review   — n-a. One short prompt string parsed per frame while a command is
      active, allocating a handful of small strings. Not a spec-marked hot path (REQ-100 is the
      viewport). If it ever shows, cache per prompt pointer — noted, deliberately not
      pre-optimized (§3.4: no optimization without a profile).
- [x] testing              — **PASS**, with one honest gap named below.
      - `CommandLineTests [req119]` — 4 cases, 53 assertions: the shortcut rule (incl.
        `No trim`→`N`), inline parsing byte-for-byte against LINE's prompt, grouped parsing for
        MIRROR/LENGTHEN/FILLET/CHAMFER, and the malformed cases. Every case also round-trips the
        segments back to the original string, so parsing can neither lose nor duplicate text.
      - `headless.regression-119-variant-token-accepted` — 55 steps: each extracted token is
        accepted by the command showing that prompt, with MIRROR's two answers told apart by
        entity count rather than log text, plus the old parser's token kept as a live rejection.
      - Suites: **545/545** Catch2, **600/600** ctest (1 known-disabled, #63).

- [ ] **manual GUI pass — OUTSTANDING, and it is the real gap.** `LayoutCommandHint` is ImGui
      code and the headless driver never constructs an ImGui context, so **the layout and
      rendering half of this change has no automated coverage** — only the pure rule beneath it
      and the tokens above it do. A smoke launch confirmed the app starts and runs clean for
      12 s, but that proves startup only: the function is not called until a command with a
      bracketed prompt is active, so the smoke test did not execute a single line of it. What
      still needs a human, per REQ-119's own Acceptance:
      1. LINE's `[A]`/`[2P]` render and hover as links in the **floating bar**, and clicking
         each does what typing `a` / `2p` does;
      2. the same in the **classic docked panel** (`cmdLineClassicDock`) — the surface that
         never had links before;
      3. MIRROR's `[Yes/No]` and LENGTHEN's `[DElta/Percent/Total/DYnamic]` render as **two**
         and **four** separate links;
      4. a **narrow** dock width, to exercise the wrap path: links land on the correct line,
         no horizontal overflow, and the footer reserves the right height (the links must not
         shift out from under the cursor).

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
- Requirements satisfied: REQ-119 **increment 1** (Acceptance met: yes for every automatable
  condition; the four manual GUI conditions are outstanding — §9). Increment 2 not started.
- Tests added:            `CommandLineTests [req119]` (4 cases / 53 assertions);
                          `headless.regression-119-variant-token-accepted` (55 steps).
- Refactors:              the hand-rolled LINE link block deleted (25 lines); one renderer now
                          serves both the floating bar and the docked panel.
- Docs updated:           none needed (REQ-119 and this log carry the convention).
- Status:                 **self-verify complete except the manual GUI pass** — not `done`.

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
