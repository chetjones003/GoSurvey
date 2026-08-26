# TASK-112 — Clickable command variants: the coverage audit (REQ-119 increment 2)

- Type:    feature
- Status:  done (2026-08-25)
- Opened:  2026-08-25
- Owner:   Nathan Johnson

Upstream issue: chetjones003/GoSurvey#81.

## 1. Authority
- Requirements: **REQ-119** — accepted by **D-2026-08-25-m**, amended by **D-2026-08-25-n**.
  This task is **increment 2**, and it depends on increment 1 (TASK-111 / PR #90) for the parser
  and the shared renderer. It adds no mechanism.
- Also honoured: REQ-040 (Acceptance (7) — LINE's links must keep working), REQ-024 (the same
  strings feed the at-cursor label), REQ-304 (its eight new live prompts are the surface this
  marks up), REQ-201 (a refusal states its reason).
- Acceptance: REQ-119's **Increment 2** conditions.
- Owning subsystem: `UI` (`CommandInputHint`) and `Commands` (the `*FooterHint` family). Prompt
  **text** only — no command's behaviour, and no handler, is touched.

## 2. Scope
- In scope: markup of the ~30 prompt strings that name a keyword option, in **both** vocabularies,
  each token verified against its own handler, with headless assertions per token.
- Out of scope:
  - unifying the two vocabularies — DEBT-1 stays open by explicit user decision (Q1);
  - any change to what a command accepts;
  - prompts that name no variant (the large majority — they are point prompts).
- Smallest change: ~30 string edits and their tests. No code path changes.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / global state / public API / data
  format / unspecified algorithm?
    - [x] **No** — proceed. Increment 1 already built and reviewed the mechanism; this is text.
          No file gains a responsibility it did not have: `CommandInputHint` and the
          `*FooterHint` family already own these strings, and they keep owning them (DEBT-1).

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Settle DEBT-1 (prompt-text ownership) before paying its cost ~10 times? | 2026-08-25 | **No — mark up both in place.** Unifying is a genuinely separate architectural change and would hold #81 hostage to it. The duplication cost is bounded: ~10 double edits, once. DEBT-1 stays open. |

## 5. Assumptions

```
ASSUMPTION-1 (inherited from TASK-111, and the whole point of this task):
  Every token marked up is accepted by its handler IN THAT STATE.
- Because:       the convention implies the shortcut from prompt text, so a prompt can name a
                 token its command rejects — and marking up prose is exactly where that happens.
- Risk if wrong: a link that looks actionable and does nothing. Strictly worse than plain text,
                 because plain text does not invite the click.
- Validate by:   every token below was checked against its handler BEFORE markup, statically and
                 then empirically through `gosurvey_headless`. One candidate FAILED and is
                 excluded — see the CIRCLE `D` note in §6.
```

## 6. Plan

### The audit result — what carries a variant, and what each token is

Verified accepted (static read + headless probe):

| Command / state | Prose today | Markup | Token(s) | Handler |
|---|---|---|---|---|
| CIRCLE centre | `Type 3P for three-point circle` | `[3P]` | `3p` | `CadCommands.cpp:4909` |
| ROTATE angle | `R ref \| C copy` | `[R]eference`, `[C]opy` | `r`, `c` | `TryRotateCopyToggle` |
| ROTATE ref p1/p2 | `C toggles copy` | `[C]opy` | `c` | same |
| ROTATE after-ref | `P two pts \| C copy` | `[P]`, `[C]opy` | `p`, `c` | same |
| ROTATE angle pts | `C copy` | `[C]opy` | `c` | same |
| SCALE factor | `R = two-point ref length` | `[R]eference` | `r` | scale text handler |
| TRIM cutting edges | `type L — draw the trim line` | `[L]ine` | `l` | `CadCommands.cpp:21468` |
| TRIM line p1 | `type T — pick cutting edges` | `[T]` | `t` | same family |
| LINE bearing lock | `A clears` | `[A]` | `a` | segment-angle handler |
| POLYLINE first | `CLOSE closes` | `[CLOSE]` | `close` | `CadCommands.cpp:21812` |
| POLYLINE next | `A/2P \| CLOSE / END` | `[A]`, `[2P]`, `[CLOSE]`, `[END]` | `a`,`2p`,`close`,`end` | `14610`, `21812`, `21820` |
| POLYLINE lock | `A clears \| CLOSE / END` | `[A]`, `[CLOSE]`, `[END]` | same | same |
| FEATURELINE first/next | `E = elevation point \| CLOSE/END` | `[E]`, `[CLOSE]`, `[END]` | `e`,`close`,`end` | `CadCommands.cpp:21722` |
| ELEV | `W = world Z 0` | `[W]orld` | `w` | `CadCommands.cpp:21448` |
| DIMLINEAR line pt | `H / V keys` | `[H]`, `[V]` | `h`, `v` | `CadCommands.cpp:22053` |

**Deliberately NOT marked up — and this is the task's most important finding:**

```
CIRCLE radius prompt — "D <value> or D<value> for diameter"
  D is a VALUE PREFIX, not a submittable token. Bare "d" is REJECTED by
  ParseRadiusOrDiameter ("Expected diameter after D (e.g. D 40 or D40)"), because
  ParseOneFloat("") fails on the empty remainder.
  Marking it up would MANUFACTURE a dead link — the exact defect REQ-119 exists to
  remove. The convention cannot express "prefix, needs a value", so D stays prose.
  Pinned by a live rejection assertion in the transcript, so a later well-meaning
  markup pass fails the build instead of shipping the link.
```

The same reasoning retires the other near-misses: `Enter`, `ESC`, `+90/-45` and `X,Y`/`@dx,dy`
are not keyword options at all and are left alone.

- Files to touch:
  - `src/ui/CadUi.cpp` — `CommandInputHint`, 12 strings (6129, 6146, 6148, 6149, 6207, 6235,
    6264, 6287, 6290, 6292, 6295, 6359).
  - `src/commands/CadCommands.cpp` — `*FooterHint` family, 19 strings (22239, 22279-22289,
    22307, 22342, 22344, 22391, 22438, 22469-22470, 22544, 22554-22555, 22607).
  - `tests/headless/transcripts/regression-119-variant-coverage.txt` — **new**.

- Test approach:
  - **happy path** — one headless assertion per marked-up token, submitted in the state whose
    prompt offers it, asserting the state actually moved.
  - **failure mode** — CIRCLE's bare `d` must still be REFUSED, asserted live so the exclusion
    cannot be quietly undone.
  - **regression** — REQ-040 Acceptance (7): LINE's `[A]`/`[2P]` unchanged; the increment-1
    suites stay green.

- Steps:
  - [x] 1. Mark up the 12 `CommandInputHint` strings.
  - [x] 2. Mark up the 19 `*FooterHint` strings.
  - [x] 3. Write the coverage transcript (one assertion per token + the CIRCLE `D` refusal).
  - [x] 4. Build; run both suites.
  - [x] 5. Manual GUI spot-check of two newly-marked commands (ROTATE and POLYLINE).

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1). The audit in §6 IS the tests-first step — every token was
  verified before a single string was edited, which is the discipline ASSUMPTION-1 demands and
  the reason the CIRCLE `D` trap was caught before it shipped rather than after.

## 8. Implementation log
- 2026-08-25 opened. Audit complete (§6) — ~30 strings carry variants out of ~190 total, so the
  surface is far smaller than REQ-119's original estimate; 15 token groups verified accepted, one
  (CIRCLE `D`) verified REJECTED and excluded.
- 2026-08-25 **31 strings marked up** (12 `CommandInputHint` + 19 `*FooterHint`). All markup is
  the **inline** form (`[R]eference`, `[CLOSE]`), never the grouped form — no bracket in this
  pass contains a `/`, so every link's label is its own bracketed token and the rendering stays
  consistent with LINE's existing `[A]zimuth`.
- 2026-08-25 **GUI spot-check** (step 5), driven against the real window:
  - `POLYLINE next — X,Y / [A] / [AP] / [CLOSE]:` renders three links; clicking `[A]` logged
    *"Bearing ° clockwise from north (decimal/DMS); blank Enter cancels."*
  - `° CW from north / DMS / [R]eference / [C]opy:` renders two links; clicking `[C]` logged
    *"ROTATE — copy mode on (original kept)."* — the same line the transcript asserts for typing
    `c`, so the click path and the typed path agree on a newly-marked command too.
- 2026-08-25 Two transcript expectations were **my** errors, not the product's, and are recorded
  so the next author does not repeat them: closing a polyline logs *"POLYLINE closed."* and
  finishing one open logs *"POLYLINE complete."* — neither says "finished". ROTATE's window
  select is **viewport clicks only**; typed corner coordinates are refused with *"Could not parse
  ROTATE input"*, which is why the GUI check drives it with clicks.

## 9. Self-verification
- [x] build-project        — PASS (clean; no new warnings)
- [x] architecture-review  — PASS (prompt TEXT only; no file gains a responsibility, no invariant touched)
- [x] code-review          — PASS (no code path changed; the risk is entirely "does this token work", which §6 verifies token by token)
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a
- [x] testing              — PASS: 545/545 Catch2, 604/604 ctest (1 known-disabled, #63)

## 10. Verification result
- Submitted:
- Verdict:
- Findings:

## 11. Outcome
- Requirements satisfied: REQ-119 **increment 2** — every variant a live prompt names is now
  clickable, every clickable token is accepted by its handler in that state, no variant lost its
  keyboard path, and no log line is clickable. **With REQ-119 increment 1 (TASK-111), REQ-119 is
  complete and GitHub #81 is satisfied.**
- Tests added:            `headless.regression-119-variant-coverage` (74 steps) — one assertion
                          per marked-up token, plus the CIRCLE `D` refusal as a live guard.
- Docs updated:           none needed.
- Done:                   2026-08-25.

### Coverage statement (REQ-119 increment 2 requires this to be explicit)
Every `AppCommandState::Kind` is either marked up or recorded as carrying no variants. The
**~160 prompt strings not touched** are point/value prompts — "ARC: Start point", "MTEXT: First
corner", "TEXT: Height" and their kin. They name no keyword option, so there is nothing to mark
up and nothing was skipped. `Pan` and `Orbit` remain out of scope by REQ-304's reasoning: they
are continuous drag modes whose feedback is a cursor icon, not a prompt.

## 12. Technical debt
```
DEBT-1 (inherited from TASK-111, still open, and now PAID ONCE): two parallel prompt
vocabularies. This task edited both — ~10 of the ~30 strings are the same command state written
twice, e.g. ROTATE's angle prompt exists as both "ROTATE: ° clockwise / DMS | [R]eference |
[C]opy | ESC" and "° CW from north / DMS / [R]eference / [C]opy:". The user chose this
deliberately (Q1) rather than block #81 behind an ownership decision. The cost is now sunk for
markup; what remains is that every FUTURE prompt edit is still two edits. Removal condition is
unchanged: a SPEC GAP naming one owner for prompt text.
```
