# TASK-219 — a prompted radius for the solid FILLET (REQ-323, bug fix)

## The report

User, 2026-09-08, with a screenshot: *"I am not able to test fillet at the moment due to a problem.
After ctrl selecting 2 lines and then moving to select a radius, hitting the R pulls up a new
command."* The screenshot showed the `RECT` autocomplete open and `R` in the command box.

The command log in that screenshot is the whole diagnosis:

```
Selected edge 5 of solid 1 (1 sub-object…
Selected edge 4 of solid 1 (2 sub-object…
Usage: FILLET <radius> - rounds the sel…
RECT — pick the first corner…
RECT canceled.
```

## Root cause

`CadFilletSolidEdges` was argument-only. Given no radius it logged a usage line and **returned
without entering any command state** — `st.active` stayed `None`. So the command line was idle when
the user's next keystroke arrived, and `R` — the 2D FILLET's Radius option, and the obvious thing to
press — matched the `RECT` command in the autocomplete instead.

The geometry was never involved. This is TASK-217's own DEBT-1 (*"no prompted form"*), which was
recorded as a convenience follow-up and is in fact a defect: **the feature could not be driven by
anyone who did not already know to type the radius on the same line as the verb.** It took under a
minute of real use to hit.

## The fix

A bare `FILLET` with solid edges selected now enters a radius prompt, which is what PRESSPULL grew
for the same reason (issue #396):

```
FILLET - 2 edge(s) selected. Specify fillet radius <0.5000>, Enter to accept, ESC to cancel:
```

- a number applies it; **Enter** accepts the shown default; **ESC** cancels;
- the radius used is remembered as the next default, as the 2D fillet's already is;
- `FILLET 2` on one line still behaves exactly as before.

Both routes go through one new `CadApplyFilletToSelectedEdges`, so the prompted and one-line forms
cannot drift apart about what a fillet does — the same single-implementation rule the gizmo and the
typed MOVE follow.

**The prompt stays up on a bad answer and on a refused one.** A mistyped character or a radius that
does not fit would otherwise end the command and drop the user back at the idle command line — which
is the exact trap being fixed. Nothing is built until the kernel accepts, so the selection survives
both cases and re-typing is all that is needed.

**ESC clears the waiting flag** (in `ResetAllCadDraftTools`, which `CancelActiveCommand` calls).
Without that, cancelling would leave the flag set and the next line typed — whatever it was — would
be read as a radius. That is the same class of bug as the one being fixed, one step further on.

## What was built

| file | change |
|---|---|
| `src/commands/CadCommands.hpp` | `filletSolidAwaitingRadius`; `CadApplyFilletToSelectedEdges` |
| `src/commands/CadCommands.cpp` | the apply split out; the prompt in `CadFilletSolidEdges`; the answer in `HandleFilletText`; blank-Enter routing; the status-line prompt; the ESC reset |
| `tests/headless/transcripts/req323-fillet-solid.txt` | three new blocks |

## Test approach

Three blocks, each pinning one half of the failure:

1. **A bare `FILLET` prompts**, and does **not** consume the selection it is about to round — asking
   for a radius must not eat the edges. `CMD 2` then applies, against the closed forms.
2. **ESC at the prompt leaves the next command alone.** `ESC` then `CIRCLE` must start a circle, not
   be swallowed as a radius. This is the bug-one-step-on described above.
3. **A bad answer and a refused answer both keep the prompt up.** `banana` → *"must be a number"*;
   `8` → *"too large"* plus *"specify a different radius"*; the selection intact and the solid
   untouched through both; then `2` succeeds.

The regression the report is really about — a bare `FILLET` with **nothing** selected still opening
the 2D command — was already asserted in this transcript before the fix and still passes, which is
what says the routing change is confined to the sub-object case.

## Verification

- **build-project** — PASS. (The first attempt failed to link: `GoSurvey.exe` was still running from
  the user's own testing. Worth knowing — the error is `LNK1104: cannot open file 'GoSurvey.exe'`,
  which reads like a build problem and is not one.)
- **testing** — PASS. `ctest` **1309/1309**; the transcript grew from 75 to 112 steps.
- **architecture-review** — PASS. No layer moved; the prompt reuses the existing `Kind::Fillet`
  command state and its ESC plumbing rather than adding a parallel one.
- **code-review** — self-run. It caught the first draft ending the command on a parse failure, which
  reintroduced the reported bug in a smaller form.

## Not covered by test, stated plainly

- **`R` at the new prompt** is not a Radius sub-option — with the edges already chosen there is
  nothing else to ask, so it reports "radius must be a number" and asks again. Deliberate, but it
  means the muscle memory that caused the report gets an error rather than an action.
- **No live radius preview.** PRESSPULL's prompted form drags a ghost; this one does not, and would
  need fillet ghost geometry that does not exist.

## Technical debt

- **DEBT-1 — still no ribbon or viewport route to the solid fillet.** The Home ribbon's FILLET
  button opens the 2D flow, which is correct for a 2D selection and does nothing for solid edges.
