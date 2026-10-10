# TASK-225 — the generic trailer is suppressed when the handler already spoke (every command)

## Requirement authority

- **REQ-201 — No silent failures.** This is a conformance fix against it, not a new requirement.
  REQ-201 says no failure path is empty; it does not say a path may be reported **twice, in two
  voices, one of them false**. The generic trailer under a specific refusal is the second voice.
- Continues **TASK-224**, which fixed FILLET and CHAMFER. The user's instruction after it:
  *"go through the other commands too."*

## Scope — the audit, and what it found

Eleven call sites of `ReportUnparsedCommandInput`. Every one was read.

| command | verdict |
|---|---|
| MOVE / COPY (`HandleModifyText`) | **clean** — all three `return false` are genuine parse failures |
| STRETCH | **clean** |
| ROTATE | **clean** |
| LENGTHEN | **clean** |
| the inline point parser | **clean** — its own branches log and `return` before reaching the trailer |
| **SCALE** | **4 bad paths** — factor `<= 0`, reference length too small, new length `<= 0`, new length segment too small |
| **ARRAY** | **7 bad paths** — column/row/level counts, all three spacings, polar item count and fill angle |
| **MIRROR** | **1 bad path** — two mirror-line points that parse but coincide |
| **CIRCLE** | inspected; its step handler's `false` paths are parse failures |

A worked example of the shape, from `HandleScaleText`:

```cpp
float sf = 0.f;
if (ParseOneFloat(line, &sf)) {          // it PARSED. The input was understood.
  if (!(sf > 0.f) || !std::isfinite(sf)) {
    log.push_back("SCALE — scale factor must be a positive finite number.");
    return false;                        // ...and then earns "Could not parse SCALE input".
  }
```

## Why this is one rule at the caller, not twelve patches

TASK-224 fixed FILLET and CHAMFER by changing what their return value **means** ("was this input
understood"). That works there because their refusal paths are cleanly separable.

**It does not generalise**, and the audit is what showed why. Most of these handlers look like:

```cpp
if (!ParseOneFloat(line, &v) || !(v >= 1.f) || !std::isfinite(v)) {
  log.push_back("ARRAY Rectangular — number of columns must be a positive whole number.");
  return false;
}
```

One `return false` covers **both** "did not parse" and "parsed and out of range". No return value can
separate them without splitting every guard in two — twelve restructurings, each a chance to change
behaviour by accident, for a message.

What **can** be observed, at the caller and uniformly, is whether the handler already explained
itself. So:

> **The generic fallback is suppressed when the handler logged anything for this input.**

`ReportUnparsedCommandInput` gains a `markBeforeHandler` parameter; each caller passes `log.size()`
captured before invoking its handler.

**The "still running" half is never suppressed.** It answers a different confusion — *"I thought
this command had finished"* — which no handler message addresses, and it is the case the function was
written for. That asymmetry is the whole design.

## What each case now produces

| situation | before | after |
|---|---|---|
| SCALE, factor `-2` | own message **+ "Could not parse SCALE input"** | own message only |
| SCALE, `banana` at the base prompt | "Could not parse SCALE input" | **unchanged** — the handler said nothing, so this is the only message there is |
| SCALE, `CIRCLE` typed at a live prompt | "SCALE is still running… Press Esc" | **unchanged** |
| ARRAY, `0` columns | own message + trailer | own message only |
| MIRROR, two coincident points | own message + trailer | own message only |

## Verification

- **build-project** — PASS, no warnings.
- **testing** — PASS. `ctest` **1334/1334** (+1: `headless.task225-refused-not-unparsed`).
- **The new transcript was proven to bite.** The suppression was disabled (`if (false && …)`), the
  project rebuilt, and the transcript went red at the first `EXPECT NOLOG` before being restored.
- **The two regression risks are asserted, not assumed.** A silent handler must still produce the
  fallback, and a command name must still produce the still-running hint. Both are in the transcript,
  for SCALE and for ARRAY. Suppressing either would be a worse bug than the one being fixed —
  silence is worse than a redundant line.
- **architecture-review** — PASS. One parameter on one existing function; no new type, no new state.
- **code-review** — self-run. The risk is over-suppression, covered above.

## An honest note on the probe that failed first

The first SCALE probe failed and looked like the fix was wrong. It was the probe: `CMD LINE 0,0 100,0`
does not create a line (LINE takes its points as separate submissions), so `BOX` selected nothing and
SCALE never reached its base-point phase. The assertion failing was correct behaviour on an empty
drawing. **Fixed the probe, not the code** — worth recording, because the first instinct on a red
assertion is to distrust the change.

## Relationship to TASK-224

For FILLET and CHAMFER the two mechanisms now overlap: their kernel-refusal paths return `true`
(TASK-224) *and* would be suppressed by this rule anyway. The `return true` is kept, because it
states something this rule does not — that the input was **understood** — and that contract is now
documented on the handler. They cannot disagree in practice: a value the kernel can refuse is always
a number, and a number is never a command name.

## Technical debt

- **DEBT-1 — the suppression is "did the log grow", not "did the handler address THIS input".** A
  handler that logged something incidental while failing to parse would lose its fallback. No such
  handler exists today (checked across all eleven), but nothing enforces it.
- **DEBT-2 — SCALE's and ARRAY's guards still conflate parse failure with out-of-range.** This task
  deliberately did not split them: the message is now correct either way, and splitting twelve guards
  to recover a distinction nothing else consumes is not worth the risk. It is recorded because a
  future change that DOES need the distinction should know it is not there.
