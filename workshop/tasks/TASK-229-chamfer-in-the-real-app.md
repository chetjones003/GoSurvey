# TASK-229 — driving the solid CHAMFER through the real GUI, and the bug that found

## Requirement authority

- **REQ-331** (the solid chamfer) and **REQ-318** (sub-object picking) — the behaviour under test.
- **REQ-161 / ADR-040** — the Developer Shell is the Debug-only GUI driver. This is the first test to
  use it on the **viewport**; every existing one drives ribbons and the command line.
- User instruction, 2026-09-08: *"go ahead and do some tests in the app and see if what you added
  implemented well into the app."*

## The bug it found — a real defect in the REQ-331 work

`CadUi.cpp` routes a mid-command `Ctrl`+click to the sub-object pick, and that branch read:

```cpp
if (modelSpace && ImGui::GetIO().KeyCtrl && cmd.active == AppCommandState::Kind::Fillet) {
```

**CHAMFER was never added to it.** TASK-222 added CHAMFER to the *hover* predicate (TASK-221's
DEBT-1) and stopped there. The result in the running app: with CHAMFER active, holding `Ctrl` over a
solid edge **lit the edge up**, and clicking it **did nothing at all**. That is worse than either
half alone — the highlight promises exactly what the click then refuses.

**Why no existing test could see it.** The headless transcripts drive the pick with the `SUBOBJECT`
verb, which calls `SubmitSubObjectPick` directly. That is the pick's *internals*; it never passes
through the routing in `CadUi.cpp`, so `headless.req331-chamfer-solid` passed 197 steps green across
exactly this gap. The unit tests are further away still.

Fixed by adding `Kind::Chamfer` to that condition.

## What the test now covers, that nothing else can

1. **The sub-object pre-highlight during CHAMFER.** TASK-221 recorded this as DEBT-2 ("no headless
   verb drives a modifier-held cursor") and TASK-222 shipped an **argument from similarity** in its
   place, saying so plainly. This is the measurement that argument stood in for:
   `subObjectHoverValid` is true and `subObjectHover.kind` is `Edge`.
2. **That what lights up is what selects** — the clicked edge index equals the hovered one.
3. **The whole command, end to end, in the shipped UI**: a real drawing, a real box, `CHAMFER` from
   the command line, a refusal by name with the solid untouched, then `2` producing volume **1560**
   and area **796 + 40√2** on 7 faces.

## Two things the GUI run measured that the transcripts had no way to

- **`ZOOM EXTENTS` frames the plan footprint, not the orbited view.** Its own log line says
  *"span 20 x 10"*, and it leaves the camera target at `z = 0`. On a box 8 tall at a 20-degree orbit
  the top-back edge projected to `y = -33` — **off the top of the image**, with the box visibly
  clipped. The test frames the view explicitly instead. **Not chased here** — it is a REQ-058 / view
  question, not REQ-331's, and it is recorded as DEBT-1 below rather than fixed in a chamfer PR.
- **The hover pick is rate-gated and only re-runs when the cursor, view or geometry moves**
  (issue #166). Pressing `Ctrl` *after* the cursor arrives can land in a window where the gate has
  already decided "no hover" and has nothing to make it look again — the test was flaky-then-green
  before the ordering was fixed. `Ctrl` now goes down first and the cursor arrives in two steps.

## The one piece of new instrumentation

`DevShell_OnViewportRect` / `DevShell_ViewportRect` (REQ-161, Debug-only, no-op in Release): the
viewport image's screen rectangle, recorded each frame. A test that wants the cursor on a specific
piece of GEOMETRY has to turn a world point into a screen point, and `Camera::WorldToScreen` gives
the offset *inside* the image — only `imgPos` says where that image sits. Without it the GUI driver
can reach ribbons and command lines but never the viewport, which is where every pick, hover and
sub-object selection actually happens.

## Verification

- **build-project** — PASS, Debug and Release, no warnings.
- **testing** — PASS. `ctest` **1334/1334** unchanged, plus `--devshell-run req331-chamfer-viewport`
  **Success** against the live GUI. Screenshot evidence captured: the bevel is visibly on the box and
  the command log reads *"CHAMFER - 1 edge(s) of solid 1 bevelled at ..."* with no "Could not parse"
  line under it (TASK-225 confirmed in the real app).
- **architecture-review** — PASS. The hook is compiled out of Release by the same gate the rest of
  `src/devshell/` is; the `CadUi.cpp` fix is one term in an existing condition.
- **code-review** — self-run. The risk in the fix is letting a Ctrl+click reach the sub-object pick
  during a command that cannot use it; the condition names exactly the two commands that can.

## An honest note on the debugging

The first four runs failed for reasons that were **not** the code under test: the app opens on the
Start tab (no viewport at all), `VIEWANGLES` is a headless-driver verb and not a command, the
screenshot helper that reads the front buffer captured pure black, and the framing above. Each was
found by looking at a screenshot or a printed number rather than by guessing — which is the whole
argument for running the app.

## Technical debt

- **DEBT-1 — `ZOOM EXTENTS` does not account for the orbit or for Z.** Measured above, not fixed:
  wrong REQ, wrong PR. Worth its own bug report.
- **DEBT-2 — the existing devshell tests are stale.** `windows-present` fails on `//Properties`,
  and `command-line-line` on the command bar, both because those panels are not in the current
  default layout. Pre-existing, unrelated to this work, and left alone.
- **DEBT-3 — this test is Debug-only and not in `ctest`.** By design (REQ-161: headless transcripts
  are the CI driver), so the viewport routing it covers has no CI gate. A regression in that one
  `CadUi.cpp` condition would go unnoticed again.

---

## Rebased onto `beta` 2026-09-09, and the renumbering that came with it

This work was written against the pre-merge numbering and had to be re-landed on current `beta`,
where three of its identifiers had been taken by other work in the meantime:

| written as | is actually |
|---|---|
| REQ-329 | **REQ-331** (REQ-329 is now "modify commands in the active UCS", issue #402) |
| TASK-226 | **TASK-229** (TASK-226 is now `issue375-msvc-ehsc`) |
| `req329-chamfer-solid` / `-viewport` | **`req331-...`** |

Everything else of the chamfer work — the kernel, the command, `BlendsFit`, the four new refusals,
`markBeforeHandler`, `EXPECT NOLOG` — was already merged. **This one commit was not**, so `beta`
shipped the pre-highlight without the click: the exact half-and-half state this task exists to fix.

**Re-verified on current `beta`, which now includes ADR-054's float→double storage migration**
(REQ-101 ±0.002 ft, PRs #439/#445/#446/#448) — that migration touches coordinate storage this test
projects through, so the numbers were re-run rather than assumed. Release and Debug both build
clean; `ctest` **1359/1359**; `--devshell-run req331-chamfer-viewport` **Success**.

**The test was proven to catch the defect on this base**, not just on the old one: reverting the
`CadUi.cpp` condition to `Kind::Fillet` alone and rebuilding fails it at
`subObjectSelection.size() [0] == 1`, then passes again when restored.

## A collision worth flagging to the SPEC owner

`beta` currently has **two** TASK-224 files — `TASK-224-issue401-quadrant-snap.md` and
`TASK-224-refused-is-not-unparsed.md` (the second is mine, merged under that number). Not touched
here, because renaming a merged task is the SPEC layer's call, not the Workshop's.
