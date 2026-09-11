# TASK-220 — Ctrl+click gathers solid edges WHILE FILLET is running (REQ-323)

## The report

User, 2026-09-08, with a screenshot showing `FILLET: Select first object or [Radius/Trim] <R=1.000,
Trim> | ESC cancel`: *"can you make it so that while in the fillet command i can use ctrl selection
to select a edge on a 3d object … even when holding onto ctrl it will not select the edge."*

## Root cause

The solid fillet was reachable **only** by selecting edges *first* and then typing the verb. Start
`FILLET` with nothing selected and it opens the 2D command — correct in itself — but from then on
every viewport click routes through `ViewportClickRouteFor` to `RawEntityPick`, which hit-tests
whole 2D entities. The sub-object picker was wired into `ViewportClickRoute::IdleSelection` only, so
with any command running `Ctrl` meant nothing: the user held it, clicked an edge, and nothing
happened.

That ordering — *select, then command* — is backwards from how every other CAD verb is used, and it
is not something a user can be expected to guess.

## The fix

**Ctrl+click during FILLET gathers solid edges**, and a radius then finishes the command.

- `CadUi.cpp`, inside `case ViewportClickRoute::RawEntityPick`: when FILLET is active and `Ctrl` is
  down in model space, the click goes to `SubmitSubObjectPick` instead of the entity pick. The test
  is here rather than in `ViewportClickRouteFor` because that function decides on the **command**
  and this is a decision about the **modifier** — the same split the idle sub-object pick already
  uses, and what keeps the policy switch exhaustive.
- `HandleFilletText`: with an all-edge sub-object selection, a typed number **or** a bare Enter
  rounds those edges. `R` and `T` still do what they always did, so the 2D command is intact
  underneath; the number is checked first because at "select first object" the 2D flow has nothing
  to say about one.
- `SubmitSubObjectPick` re-prompts (`CadFilletReportEdgeSelection`) when FILLET is running: *"N solid
  edge(s) selected. Ctrl+click more, or type a radius <r> and Enter to round them."* Ctrl+clicking a
  face or a vertex says so rather than leaving the prompt looking ready.
- The status line shows the running count.

**The re-prompt lives in `SubmitSubObjectPick`, not at the click site.** First draft called it from
`CadUi.cpp`, which meant the viewport got the message and a transcript did not — and the transcript
case written for this failed on exactly that. Moving it puts every route into the pick on the same
footing, which is the rule REQ-318 / D-2026-09-04-a already set for the pick's own meaning.

## What was built

| file | change |
|---|---|
| `src/ui/CadUi.cpp` | the Ctrl gate inside `RawEntityPick` |
| `src/commands/CadCommands.{hpp,cpp}` | `CadFilletReportEdgeSelection`; the edge branch in `HandleFilletText`; the blank-Enter routing; the status line; the hook in `SubmitSubObjectPick` |
| `tests/headless/transcripts/req323-fillet-solid.txt` | a block for the in-command flow |

## Verified in the running application

Driven through the real GUI, not only headless: `BOX 0,0 20 10 8`, `VS SHADED`, orbit, then `FILLET`
with **nothing selected** (the 2D prompt, exactly the reported state), then Ctrl+click an edge —

> `FILLET: 1 solid edge(s) | Ctrl+click more, radius <2.0000> | ESC cancel`

— then Ctrl+click a second, opposite edge and type `2`:

> `volume now 1565.6637`

which is `1440 + 40π`, the two-opposite-edges closed form. The rounded edges are visible in the
shaded view.

## Test approach

The transcript reaches everything but the literal click routing: `SUBOBJECT` is the headless
equivalent of the Ctrl+click, so what it asserts is the part that can silently break — that a radius
typed *after* the command has started rounds the gathered edges instead of being swallowed by the 2D
flow. It checks the re-prompt appears, that two edges accumulate, and the closed-form result
(`1440 + 40π`, `704 + 44π`, topology 12/18/8).

The click routing itself is GUI-only and was checked by driving the application, as above.

## Verification

- **build-project** — PASS. (Twice interrupted by `LNK1104: cannot open file 'GoSurvey.exe'` — the
  demo instance was still running. Reads like a build error and is not one.)
- **testing** — PASS. `ctest` **1309/1309**; the transcript grew to 125 steps.
- **architecture-review** — PASS. The routing decision stays in `CadUi` where the modifier is known;
  the meaning stays in the command layer. `ViewportClickRouteFor` is untouched, so its exhaustive
  switch and its tests still hold.
- **code-review** — self-run. It found the re-prompt sitting at the click site instead of in the
  shared pick, which the transcript then confirmed by failing.

## Not covered by test, stated plainly

- **The Ctrl+click routing itself.** No headless verb drives a modifier-held viewport click; the
  transcript starts from `SUBOBJECT`. Verified by hand in the application.
- **CHAMFER** has the same shape and the same gap — it is `RawEntityPick` too — but no solid chamfer
  exists yet, so there is nothing to gather edges for.

## Technical debt

- **DEBT-1 — the 2D FILLET's `R` and `T` options are unreachable from the ribbon path once solid
  edges are held**, because a number is interpreted as the radius first. Harmless today (the prompt
  asks for a radius) but worth knowing if the 2D and solid flows are ever merged further.
- **DEBT-2 — still no ribbon route.** The Home ribbon's FILLET button opens the 2D flow; it now
  *works* for solids if the user knows to Ctrl+click, which is a real improvement, but nothing on
  screen says so.
