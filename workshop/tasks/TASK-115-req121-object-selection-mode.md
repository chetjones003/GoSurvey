# TASK-115 — Object selection is a visibly distinct mode (REQ-121)

- Type:    feature
- Status:  review — implemented, tested, GUI-verified. REQ-121, the decision entry, the task log,
           the code and the tests ship as ONE PR (workflow change, 2026-08-26).
- Opened:  2026-08-26
- Owner:   Nathan Johnson

Upstream issue: chetjones003/GoSurvey#91.

## 1. Authority
- Requirements: **REQ-121** — accepted 2026-08-26 by **D-2026-08-26-a**, in the same change that
  opens this task. Before that acceptance there was **no** authority for this work: `grep` over
  `spec/requirements.md` found no accepted requirement mentioning a pickbox cursor or OSNAP
  suppression, which under CLAUDE.md step 1 makes it a Specification task, not a Workshop one.
- Also honoured: REQ-304 (the dynamic cursor text and the command line say the same thing —
  rule (3) is REQ-304's rule applied to one more prompt); REQ-305 (accumulate-until-Enter,
  explicitly untouched); REQ-103 step 5 (STRETCH's crossing box stays load-bearing).
- Acceptance: REQ-121's seven conditions, restated there.
- Owning subsystem: `UI` for the cursor and marker suppression, `Commands` for the shared prompt
  string and the selection-step predicate. The raw-vs-snapped pick paths in `Viewport` already
  exist and are mostly correct — see §2.

## 2. Scope

Recon corrected the issue's premise before any of this was scoped, and the correction shrinks the
work: **the hit-test half is already right; the visual half does not exist.**

| | today | REQ-121 |
|---|---|---|
| pick hit-tests against | raw unsnapped cursor, for most steps | raw, for **every** step |
| cursor drawn at | snapped — it jumps | raw — it tracks the mouse |
| snap markers | drawn | not drawn |
| prompt wording | different in every command | one shared string |

- In scope:
  1. **One predicate** — "is an object-selection step active?" — exhaustive over the phases, with
     no `default:`, on `ViewportClickRouteFor`'s precedent. All three rules consult it. This is the
     load-bearing piece: it is what stops a command being half-included a fourth time.
  2. **Marker + cursor-adjustment suppression** while it is true.
  3. **Pickbox cursor** while it is true, reusing the crosshair config's existing
     `pickbox half-size in px` rather than adding a tunable.
  4. **One shared prompt string**, rendered in the command line and the dynamic cursor text.
  5. **The ALIGN gap** — `K::Align`'s `PickSelection` routes to `SelectionAccumulate`
     (`ViewportPickPolicy.hpp:126`) but is absent from `ViewportUseRawWorldForSelectionRectPick`
     (`:9`), so its box corners are snapped while all six siblings are raw. Found while drafting
     REQ-121; fixed here as part of rule (1)'s completeness, not as a separate task.
- Out of scope:
  - which objects each command can select, and the accumulation logic (REQ-305, already done);
  - STRETCH's crossing-box semantics (REQ-103 step 5) — it gets the treatment, its box does not
    change meaning;
  - snapping outside selection steps, which is untouched.

## 3. Architectural boundary check  (fill BEFORE planning)
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [ ] No.
    - [x] **One question to settle before code, and it is small but real.** Rule (1) introduces a
          predicate consulted by both `UI` (cursor, markers) and `Commands` (prompt). Where it
          lives decides whether this is reuse or a new seam:
          - `ViewportPickPolicy.hpp` already holds exactly this kind of pure phase predicate
            (`ViewportUseRawWorldForSelectionRectPick`), is already included by both sides, and is
            already unit-tested by `ViewportPickPolicyTests`. Putting it there is **reuse of an
            established pattern, not a new abstraction** — and it puts the new predicate beside the
            one whose incompleteness caused the ALIGN gap, which is where a reader will look.
          - The alternative — a field on `AppCommandState` — would be new mutable state
            duplicating what the phase enums already say, and could disagree with them.
          Recommendation: the former. Recorded here rather than assumed, because "where does the
          shared predicate live" is the one decision in this task that a reviewer could reasonably
          answer differently.

## 4. Questions  (ask before guessing)
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Exact prompt wording. REQ-121 fixes that there is **one** string; it does not fix what it says. #91 suggests "Select objects, ENTER to continue". | 2026-08-26 | **"Select objects, ENTER to continue"**, #91's own suggestion, as `kSelectObjectsPrompt`. |
| Q4 | **Raised during implementation, not planned for.** Rule (3) as accepted said *every* listed step shows the identical prompt. Applying that literally deletes TRIM's `type L — draw the trim line` and OFFSET's pickable-type list — the only place each is discoverable, and REQ-119 (in flight) exists to make such keywords *more* reachable. Does the shared prompt really replace those? | 2026-08-26 | **No — pure selection steps only.** MOVE/COPY/SCALE/ROTATE/MIRROR/ALIGN/ARRAY/DELETE/JOIN take the shared string; TRIM, OFFSET and STRETCH keep theirs and still get rules (1) and (2). REQ-121 amended to say so, with a new acceptance condition that a prompt which *lost* an option to this requirement is a failure of it. My error in the original draft: I wrote the acceptance without checking what those prompts carried. |
| Q5 | **Also raised during implementation.** #91 lists ZOOM, and I copied that into REQ-121 — but ZOOM WINDOW's box picks a region of the view to fit, not objects. | 2026-08-26 | **Excluded.** "Select objects" would be a prompt that lies and a pickbox would say *click a thing* while the user drags a rectangle. Its corners are already unsnapped, so rule (1) changes nothing there either. REQ-121 amended, and the exclusion is stated rather than silent because chet listed it. |
| Q2 | Does idle selection (no command running) get the pickbox too? REQ-121's first draft called idle an object-selection step, so by the letter yes — but the idle crosshair is what the user sees most of the time, so this is the most visible change in the task and worth confirming rather than inferring. | 2026-08-26 | **No — idle is excluded.** REQ-121 and D-2026-08-26-a were both amended: idle keeps today's crosshair, OSNAP and snap markers. The treatment is a mode signal, and it only signifies against a default; idle is that default. Acceptance now states it positively — a user who never starts a command cannot tell this shipped. |
| Q3 | Do the entity-picking loops of TRIM/EXTEND/FILLET/CHAMFER/BREAK/LENGTHEN get the pickbox as well, or only the `PickSelection` phases? They pick objects, so REQ-121 lists them — but TRIM owns its own pick entry point (`SubmitTrimViewportPick`) and may need its own handling. | 2026-08-26 | **Yes, all of them, and it needed no special handling.** Deriving the predicate from the ROUTE rather than from `cmd.active` answered this for free: those loops are `RawEntityPick`/`TrimPick`, which are object picks by definition. TRIM owning a separate click entry point turned out to be irrelevant — the predicate asks what the PHASE means, not which function consumes the click. |

## 5. Assumptions
```
ASSUMPTION-1: Deriving the predicate from the ROUTE keeps it exhaustive for free.
- Because:       `ViewportClickRouteFor` is already a `default:`-less switch over every command
                 Kind, so a new command cannot exist without being given a route — and its route
                 already answers "objects or a point?".
- Consequence:   a command added later is included or excluded by a decision someone had to make at
                 the point they added it, rather than by remembering this requirement exists. That
                 is REQ-121's own acceptance condition ("a command left out is a build-time or
                 test-time failure") satisfied structurally rather than by vigilance.
- Held?          Yes, with ONE exception found while implementing: ZOOM shares `SelectionBox` with
                 DELETE/JOIN while meaning something different, so the route cannot answer for it.
                 Named at the call site with its reasoning rather than smoothed over (Q5).
- Validate by:   `ViewportPickPolicyTests` asserts both directions — every selection step true, and
                 each exclusion false.
```

## 6. Plan  (write BEFORE any code)
- Steps:
  - [x] 1. Answer Q1-Q3.
  - [x] 2. The predicate, in `ViewportPickPolicy.hpp`, exhaustive with no `default:`; extend
           `ViewportPickPolicyTests` to assert every selection phase answers true. **The ALIGN gap
           is the red-before test here** — it fails on the existing code and passes after.
  - [x] 3. Fix `ViewportUseRawWorldForSelectionRectPick` to include ALIGN (rule 1 completeness).
  - [x] 4. Suppress snap markers and cursor adjustment while the predicate is true.
  - [x] 5. Pickbox cursor while it is true; revert on phase advance and on cancel.
  - [x] 6. One shared prompt string through the command line and the dynamic cursor text.
  - [x] 7. Audit every command in REQ-121's list; confirm none is left behind.
- Test approach: **split, deliberately.** The predicate and the ALIGN fix are pure and go to
  `ViewportPickPolicyTests` — that is where an off-by-one command would hide, and it is fully
  automatable. Everything else is rendered GUI, which this project's anti-requirements exclude from
  automation, so it is a driven manual pass (§13).
  **Planned wrong, corrected during implementation:** this section originally expected a headless
  transcript asserting the prompt via `EXPECT LOG`. That cannot work — REQ-119's own table says
  `log.push_back` is history and never the live prompt, so the log does not contain it. The attempt
  and the two dead ends after it are recorded in §12 DEBT-1 rather than deleted, because both look
  like they ought to work.

## 7. Workflow-specific notes
- Feature: the pre-flight is Q1–Q3 plus §3's placement decision. Do not start at step 2.
- **One PR, complete** — spec, decision entry, task log, code and tests together. This replaces the
  original plan of shipping the spec first on PR #89's precedent (`spec: accept REQ-119 and open
  TASK-111`), which the user rejected on 2026-08-26: a partial or stacked PR makes chet hold context
  across several merges to judge one piece of work, and a spec-only PR asks him to accept a
  requirement before he can see whether it survives implementation. Which is exactly what happened
  here — REQ-121 needed amending **twice** (Q4, Q5) once the code met it.

## 8. Implementation log
- 2026-08-26 Opened. Recon before drafting REQ-121 established that the issue's "several commands
  already do this internally" is half right in a way worth writing down: hit-testing is already
  raw, cursor drawing and markers are not, and the two disagreeing on screen is the actual defect.
  The ALIGN gap was found in the same read.

- 2026-08-26 **Implementing the requirement found two errors IN the requirement**, both mine and both
  in its acceptance rather than its intent. (a) "Every listed command shows the identical prompt"
  would have deleted TRIM's `L` keyword and OFFSET's type list — written without checking what
  those prompts carried, and in direct conflict with REQ-119, which is in flight to make such
  keywords MORE reachable. (b) ZOOM was in the list because #91 listed it, but its box picks a
  region of the view, not objects. Both were put to the user and the spec was amended BEFORE the
  code was written to match it — the alternative was code that satisfied a requirement I already
  knew to be wrong.
- 2026-08-26 **A gap found in passing:** MIRROR and ARRAY had no footer hint at all, so their
  selection step showed a command-line prompt and nothing in the dynamic cursor text. That is
  REQ-304's own rule broken, missed by its audit. Closed by extending `ModifyCommandFooterHint`
  rather than adding two delegates, on REQ-304's own precedent.

## 9. Self-verification
- [x] build-project        — PASS (clean)
- [x] architecture-review  — PASS. No new type, field, signature or dependency. The predicate went
                             to `ViewportPickPolicy.hpp` as §3 recommended, beside the predicate
                             whose incompleteness caused the ALIGN gap. One named exception (ZOOM)
                             rather than splitting the route enum — reasoned at the call site.
- [x] code-review          — PASS
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a (one pure predicate per frame, over an enum)
- [x] testing              — **604/604 ctest green.** Three new `ViewportPickPolicyTests` cases; the
                             ALIGN one is red before the fix. Rules (1)–(3) are GUI-verified (§13)
                             because none is reachable headlessly; see DEBT-1.

## 10. Verification result
- Submitted:  2026-08-26
- Verdict:    PASS
- Findings:   none outstanding. Two scope errors in the requirement were found *by implementing it*
              and fixed in the spec before the code — Q4 (the prompt would have deleted TRIM's and
              OFFSET's keywords) and Q5 (ZOOM is a region pick, not an object pick). Both were mine.

## 11. Outcome
- Requirements satisfied: REQ-121 — all conditions met as amended. Rule (1) OSNAP suppression,
                          rule (2) pickbox, rule (3) shared prompt for the nine pure selection
                          steps, the single exhaustive predicate, idle untouched, REQ-305's
                          accumulation untouched, STRETCH's box semantics untouched.
- Defects fixed:          the **ALIGN** raw-vs-snapped gap (its fence corners were snapped while all
                          six siblings were raw). Also **MIRROR and ARRAY had no footer hint at
                          all** — a REQ-304 gap found here, so their selection step showed a
                          command-line prompt and nothing in the dynamic cursor text.
- Tests added:            `ViewportPickPolicyTests` — ALIGN unsnapped-corners (red before), every
                          object-selection step recognised, and the three exclusions (idle, ZOOM,
                          point phases) asserted rather than left to the absence of a test.
- Refactors:              none
- Docs updated:           `spec/requirements.md` (REQ-121, amended twice during implementation),
                          `spec/project.md` (D-2026-08-26-a)
- Done:                   2026-08-26

## 13. GUI verification — 2026-08-26
Driven against the real window; every result read off a screenshot. All three of REQ-121's rules are
UI-only, so this is not a supplement to the automated tests — for rules (1) and (2) it is the whole
of the evidence.

- **Idle is unchanged (the exclusion, and the control for everything below).** Cursor in an empty
  viewport with no command running: the CAD crosshair, four arms and its aperture-sized centre box,
  exactly as before. A user who never starts a command cannot tell this shipped.
- **Rule (2) — the pickbox.** MOVE started from the ribbon, cursor in the viewport: the arms are
  gone and a small square remains. Escape returns the crosshair immediately, which is the
  "reverts when the command is cancelled" half of the acceptance and the half most likely to be
  forgotten.
- **Rule (1) — OSNAP suppression, tested A/B against a control rather than on its own.** A line was
  drawn, and the SAME endpoint hovered at the SAME pixel in two states:
  - *control*, LINE's point phase: the green endpoint-snap marker draws and the crosshair is pulled
    onto the endpoint — OSNAP visibly working;
  - *test*, MOVE's selection step: no marker, no pull, the pickbox sits where the mouse is.

  Doing it as a pair is the point. "No marker appeared" proves nothing by itself — the marker also
  fails to appear when snapping is broken, when nothing is near, or when the aperture is wrong. The
  control establishes that this drawing, this point and this cursor position DO snap, so the test's
  absence is the suppression and not an accident.
- **Rule (3) — the prompt.** The dynamic cursor text reads
  `Select objects, ENTER to continue | ESC cancel`, the shared constant verbatim.

Two notes from driving it, recorded because they cost time and will again:
- the app is DPI-scaled at 125%, so a synthesized cursor position lands at 1.25x that point in a
  screen capture — every coordinate in the driver script accounts for it;
- the floating command bar starts hidden (REQ-040 allows it) and typed command names go nowhere
  until it is restored with `Ctrl+9`. Commands were started from the ribbon instead.

## 12. Technical debt
```
DEBT-1: None of REQ-121's three rules has automated coverage, and none can have.
- What:      rule (1) is a snap computed in the UI layer from a live cursor; rule (2) is a rendered
             glyph; rule (3)'s two surfaces are `CommandInputHint` (in `CadUi.cpp`, not linked by
             the test target) and the `*FooterHint` chain (dispatched from `CadUi.cpp` too). The
             headless driver has no cursor and no framebuffer, and this project's anti-requirements
             exclude rendered-GUI automation outright.
- Attempted: a transcript asserting the prompt via `EXPECT LOG` — it fails, correctly. REQ-119's own
             table says `log.push_back` is HISTORY and never the live prompt, so the log does not
             contain it. Recorded because it looks like it ought to work.
             Also attempted: unit-testing the `*FooterHint` functions directly. They live in
             `CadCommands.cpp`, which `GoSurveyTests` deliberately does not link (it links only
             small pure TUs). Adding it to fix a prompt test is a large link-line change for a
             narrow gain, and was declined.
- Covered:   the PREDICATE is fully unit-tested, and that is where the real risk lives — a command
             silently omitted from the treatment. The rules themselves are one-line consequences of
             it. So the untested part is thin, and the part that historically breaks is not.
- Remove by: an `EXPECT HINT "<text>"` verb in the headless driver, which needs a Commands-layer
             entry point returning the current prompt — the chain lives in UI today. That is a
             REQ-203 harness extension plus a small hoist, and it would also close TASK-113's DEBT-1
             family of gaps. Worth doing when a third requirement needs it.
- Follow-up: not filed.
```
