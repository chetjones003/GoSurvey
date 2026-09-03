# TASK-191 — Name the running command when it swallows a command name (GitHub issue #233)

## Requirement authority

- **REQ-201** — no silent failures. The failure path already existed and already logged; this is
  about *what* it said, which is the half REQ-201's "surfaced" is worth nothing without.
- **REQ-304** — every command Kind has a prompt. Same family: the user has to be able to tell what
  the application is waiting for.
- No SPEC change. Nothing here alters what any command does, so there is no decision to record —
  this is a Workshop-level improvement to a message, responding to a filed issue.

## The report

Issue #233. With a point-taking command still active, typing the **name of another command** did
nothing except print a generic parse error:

```
LINE / 0,0 / 10,0 / <Enter> / SPHERE
  → "Could not parse point. Use X,Y or X Y; @dx,dy; A / 2P (two picks); ..."
```

No sphere, no line, and the message named neither `SPHERE` nor `LINE`. Typing it again gave the same
thing. Only Esc got out. From the user's side that reads as "the application stopped responding".

## What is NOT changed, and why that is the point

Two behaviours put the user in this position, and **both are deliberate**:

- `LINE`'s blank Enter ends the chain and **restarts LINE** for the next one — chosen, and recorded
  (D-2026-08-25-j), deliberately unlike POLYLINE's Enter, which exits.
- `CIRCLE` loops back to "specify centre" after making one, so it is still running too.

Neither is touched. Whether a typed verb *should* be able to interrupt a running command is a real
design question — AutoCAD refuses one at a point prompt too — and it needs a decision, not a fix.
**The defect was only ever the wording**, and that is all this changes: the verb is still not
dispatched, and every command still ends exactly where it did.

## What it does

`ReportUnparsedCommandInput(st, line, fallback, log)` replaces the bare `log.push_back(...)` at
eleven failure sites. When the line is a word the dispatcher would accept as a command, it says so:

```
LINE is still running, so "SPHERE" was read as input to it rather than as a command.
Press Esc to end LINE, then type SPHERE.
```

Anything else gets the message it always got.

## The two calls worth reviewing

**One matcher, not two.** `FindRegistryEntry` was factored out of the dispatch loop that *starts* a
command and is now used by both it and the failure path. The whole basis of the new message is that
it agrees with the dispatcher about which words are command names; two copies would be free to drift
apart, and the message would then be confidently wrong.

**The lookup lives in the FAILURE path, and that placement is load-bearing.** Checking the registry
*before* the active command sees the line would be simpler and would break every command whose own
keyword is also a command name — `ARC` and `LINE` inside POLYLINE most obviously. Because the lookup
runs only after the active command has already declined the line, those keywords are consumed long
before they reach it. The transcript asserts that directly rather than leaving it to be rediscovered.

An **alias** is recognised and the message names the **canonical** command: someone who typed `c` is
better served by "then type CIRCLE" than by being shown their own single letter back.

## Test approach

`headless.issue233-command-name-at-point-prompt` (44 steps):

- the case from the issue, asserting both halves of the message and that **nothing was created**
  either way — the refusal is unchanged, only its wording;
- **Esc then actually starting the command**, without which this would be a nicer message on a dead
  end;
- an alias (`C`) naming the canonical `CIRCLE`;
- `CIRCLE`'s loop, which is the second way into the same trap;
- a real typo (`zzzz`) still getting the coordinate help, proving the lookup is exact rather than
  a catch-all;
- **`ARC` inside POLYLINE still drawing an arc segment** — the case that guards the placement above,
  asserted down to the bulge (`tan(22.5°)`), not just to the log line.

## Verification

- `ctest`: **1027/1027 green.**
- **Negative-tested both directions:**
  1. always taking the fallback message → `no log line contains: LINE is still running`;
  2. moving the registry lookup to the top of the text handler, the simplification this is shaped to
     avoid → `no log line contains: POLYLINE — arc mode.` — the ARC case earning its place on the
     first try.

## Technical debt / stated boundaries

- **DEBT-1 — the prompted solid commands report differently.** `Kind::Solid` and `Kind::Polysolid`
  re-print their prompt instead of a parse error, so a command name typed there is still swallowed —
  but the prompt at least names the command, which is the outcome this task is after. Left alone
  rather than given a twelfth call site whose fallback is a prompt.
- **DEBT-2 — a typed verb still cannot interrupt a running command.** Part (1) of issue #233, left
  open there: it changes what Enter and Esc mean across every point-taking command and needs a
  recorded decision first.

## Status

Complete and verified. Goes to review, not done; the issue is not closed here.
