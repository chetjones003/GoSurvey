# TASK-224 — a REFUSED value is not an UNPARSED one (FILLET and CHAMFER)

## Requirement authority

- **REQ-201** — every command reports what it did and why, in its own words.
- **REQ-323** / **REQ-331** — both say refusals are "by name". A refusal followed by a line saying
  the input was not understood is not a refusal by name; it is two contradictory sentences.
- Found by `/code-review high` on TASK-222 (Finding 2), recorded there as **DEBT-5**, and fixed on
  the user's instruction: *"fix the 'could not parse' message too."*

## What the user saw

```
FILLET - 1 edge(s) selected. Specify fillet radius <0.5000>, Enter to accept, ESC to cancel:
8
FILLET - That radius is too large for this edge: it would reach past the far side of an adjacent face.
FILLET - specify a different radius, or ESC to cancel:
Could not parse FILLET input — see command hints.        <-- untrue, and contradicts line 3
```

The input parsed perfectly well. The kernel understood it and declined it. CHAMFER did the same.

## Root cause — one bool meaning two things

`HandleFilletText` / `HandleChamferText` return `bool`, and the caller does exactly one thing with
it:

```cpp
if (HandleFilletText(st, line, log)) return;
ReportUnparsedCommandInput(st, line, "Could not parse FILLET input — see command hints.", log);
```

So the return value's *only* job is to decide whether that trailer is appended — but it was being
written as though it meant "did the command advance". A kernel refusal does not advance the command,
so it returned `false`, and earned a trailer describing a different failure.

**Nothing documented which of the two it meant**, which is how the two readings coexisted.

## The fix

**The return value means "was this input UNDERSTOOD", not "did the command advance"** — now stated
in the header, because the missing contract is the actual defect. Under that reading:

| case | returns | trailer | right? |
|---|---|---|---|
| kernel refused the value | **true** (changed) | none | the refusal already said everything |
| `"banana"` at the radius prompt | false | yes | and it is *useful*: names the running command and how to leave it |
| `"CIRCLE"` typed into a live prompt | false | yes | this is the case `ReportUnparsedCommandInput` exists for |
| `T`/`N`/`D`/`A` mis-answered | false | yes | unchanged |

Four `return false` became `return true` — the two kernel-refusal paths in each handler. **No state
handling changed**: the prompt flag stays set either way, so the prompt stays up and the selection
survives exactly as before.

Only the false half of the trailer was removed. The genuinely useful half — *"FILLET is still
running, so \"circle\" was read as input to it rather than as a command. Press Esc to end FILLET,
then type CIRCLE."* — is asserted still working, in both transcripts.

## `EXPECT NOLOG`, and a correction to my own first version of it

The defect is **a line that should not be there**, and no positive assertion can catch a message
being wrongly present. So the headless driver gains `EXPECT NOLOG "text"`.

**The first version of it was wrong and its own comment said so untruthfully.** It searched the whole
accumulated log, and the comment claimed *"a transcript needing the weaker one should NEW first,
which resets the log"* — asserted without checking. **Nothing resets `run.log`, `NEW` included.** The
assertion failed immediately, on a "Could not parse" line that an *earlier, correct* block had
legitimately provoked.

`NOLOG` is therefore scoped to the **most recent `CMD`** (a new `Run::logMarkBeforeLastCmd`), which
is both the useful claim — "that command did not say this" — and the only one that can work in a
driver where the log never resets. `LOG` keeps its whole-log semantics.

## Verification

- **build-project** — PASS, no warnings.
- **testing** — PASS. `ctest` **1333/1333**, unchanged in count: this adds transcript assertions to
  the two existing transcripts rather than new cases.
- **The new assertion was proven to bite.** One `return true` was reverted to `return false`, the
  project rebuilt, and `headless.req323-fillet-solid` went red on exactly that line —
  *"the last command logged what must not be said: Could not parse"* — before being restored. A
  negative assertion that has never been seen to fail is indistinguishable from one that does
  nothing.
- **architecture-review** — PASS. No new type, no new state beyond one `size_t` on the driver's
  `Run`, no signature change.
- **code-review** — self-run. The risk is suppressing the trailer where it was *earning its keep*.
  Covered by asserting the "still running / Press Esc" path explicitly in both transcripts, in the
  block immediately after.

## Technical debt

- **TASK-222 DEBT-5 is CLOSED** by this task.
**DEBT-1 CLOSED 2026-09-08** by TASK-225, on the user's instruction to go through the other commands. The audit found SCALE (4 paths), ARRAY (7) and MIRROR (1) with the same defect; MOVE/COPY, STRETCH, ROTATE, LENGTHEN, CIRCLE and the inline point parser were clean. The fix is ONE rule at the caller rather than twelve patches, because most of those handlers cover "did not parse" and "parsed and out of range" with a single `return false` that no return value can separate: **the generic fallback is suppressed when the handler already spoke.**

~~- **DEBT-1 — the same `bool` shape exists on other `Handle*Text` handlers**~~ (LENGTHEN, MOVE, SCALE,
  ROTATE, MIRROR, ARRAY, and CIRCLE's step handler all sit behind the same
  `ReportUnparsedCommandInput` pattern). None was inspected here. Whether any of them can also refuse
  a *understood* value is unknown, and this task deliberately did not go looking — but the contract
  is now written down in one place for the next person who does.
