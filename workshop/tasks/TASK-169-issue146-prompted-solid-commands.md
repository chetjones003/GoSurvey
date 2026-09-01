# TASK-169 — A prompted form for the seven solid primitives (issue #146, REQ-313 amended)

## Requirement authority

- **REQ-313** as amended — the increment-2 acceptance gains the prompted form.
- **D-2026-09-01-d** — the recorded decision, because this AMENDS an accepted requirement's stated
  scope and changes an observable behaviour.
- **REQ-304** (every command Kind carries a live prompt), **REQ-201** (no silent failures),
  **REQ-301** (no unearned abstraction), **REQ-154** (the active UCS).
- GitHub issue #146. Builds on TASK-166 (kernel) and TASK-167 (document integration), both merged.

## The boundary that moved, and why that is not a contradiction

REQ-313 as accepted said, in as many words, that there was "deliberately **no interactive
pick-and-drag placement**" and that a bare `BOX` would print usage "rather than opening a prompt
that never comes". The user asked for the prompt, so the boundary moves.

Worth being precise about what it was protecting, because it was not this. The boundary was about
**rubber-band drag preview** — dragging a cylinder's radius out with the mouse and seeing a 3D ghost
follow — which needs a draft-preview path that does not exist and belongs with #120's Phase 5 direct
modelling. A **prompted command** is not that. It is machinery this project already has seven
examples of, and CIRCLE's own radius step ("click, type a value, or D + diameter") is the same shape
to the letter.

Rubber-band preview, 3D grips and transforming a placed solid are all still out of scope.

## What it does

```
CYLINDER
  CYLINDER — base centre point (click, or type X,Y or X,Y,Z):
100,100
  CYLINDER — R radius, H height. Type a letter + value, a value for the next one, or Enter to create.
R 4
  CYLINDER — radius = 4.
H 25
  CYLINDER — height = 25.
<Enter>
  Cylinder created — volume 1256.6371, surface area 728.8495.
```

Accepted at the dimension prompt: `R 4`; `R` alone (arms it, the next line is the value); a bare `4`
(fills the next unset dimension, in the one-line form's own argument order); and a re-typed letter to
correct a value before Enter. The base point can be **clicked** instead of typed.

Named dimensions per primitive: `L`/`W`/`H` for box and wedge, `S`/`R`/`T`/`H` for pyramid, `R`/`H`
for cylinder, `R`/`T`/`H` for cone, `R` for sphere, `R`/`T` for torus.

**`CYLINDER 100,100 4 25` is unchanged.** It is what REQ-313's "exact dimensions typed at the command
line" acceptance rests on, and every assertion in `req313-solid-primitives.txt` still drives it.

## Design

**One `Kind::Solid` for all seven primitives, not seven Kinds.** They differ only in which named
parameters they carry, and that difference is *data* — `CadSolidParamSpecs` — not control flow. Seven
near-identical state machines is exactly the duplication that lets one of them quietly miss a fix.

**One parameter table, read by both the prompt and the commit.** A prompt that offered a letter the
commit did not know, or a commit that needed a value the prompt never asked for, is the failure that
table exists to make impossible. The table's ORDER is load-bearing twice: it is the order the
one-line form reads its arguments *and* the order a bare typed number fills them, which is what makes
the two forms mean the same thing.

**Both forms reach the same `brep::MakeX` call**, so neither can accept a solid the other would
refuse, and every refusal is the kernel's own.

## Integration points

Each is a place a command can be silently forgotten, and each is guarded by something:

- `ViewportClickRouteFor` — `K::Solid` routes to `SnappedPointPick`. That switch has no `default:`,
  so the new Kind was a compile error until it was routed (the TASK-099 mechanism).
- **`ProcessCommandLineSubmit`'s blank-line block.** Enter is what *creates* the solid, and a blank
  line never reaches the Kind-keyed branch further down — that block consumes it first. Found by the
  flow failing to commit, and the fix is where FEATURELINE's and UCS's own notes say it has to be.
- `CommandInputHint` — REQ-304. The prompt is *computed* rather than a literal because it echoes the
  dimensions already set back to the user.
- `CancelActiveCommand` — Esc names the command and clears the state, so a half-built solid cannot
  leak into the next one.
- `SubmitSolidViewportPick` — a click *after* the base point says "type the dimensions" rather than
  being swallowed, which is how a command comes to look like it has hung.

## Test approach

`headless.req313-solid-prompted` (105 steps). The assertion that matters most is **the two forms
agreeing**: the same cylinder built both ways, asserted to the same volume, area, topology counts and
world bounds. Everything else can be right while those two drift apart.

Also covered: a letter armed on its own; bare numbers filling in order; a dimension corrected before
Enter (20×5×8 = 800, not the 1600 the first width would have given); a **clicked** base point through
`CLICK`, which routes exactly as the application does; Enter with a dimension missing naming it;
a kernel refusal leaving the command open so the value can be retyped; bad input at both prompts; and
Esc leaving nothing behind *and* the next command starting clean.

`ViewportPickPolicyTests` gains `K::Solid` to its pick-driven list — the test that exists so a
command cannot route to `Ignore` and hang.

## Verification

- Build: clean, no new warnings.
- `ctest`: **966/966 green.**
- One pre-existing assertion updated rather than deleted: `req313-solid-primitives.txt` asserted that
  a bare `SPHERE` printed "Usage: SPHERE". That is the behaviour this task deliberately changes, so
  the transcript now asserts the prompt opens, creates nothing, and that Esc leaves the drawing
  exactly as it was.

## Assumptions

- **ASSUMPTION-1 (stated):** the letters chosen (`R`/`H`/`L`/`W`/`T`/`S`) are the ones a user reaches
  for. `R` and `H` were named in the request; the rest follow the same first-letter rule, with `T`
  for "top/tube" the only one that had to be arbitrated (against `R` already taken).

## Technical debt / stated boundaries

- **DEBT-1 — no rubber-band drag preview, no 3D grips, no transforming a placed solid.** #120 Phase
  5. Unchanged by this task, and the reason the original boundary existed.
- **DEBT-2 — the prompt is verified through the command line and `CLICK`, not through the GUI.** The
  at-cursor dynamic text shares one function with the command line (REQ-304's actual requirement), so
  the two cannot disagree; that they *render* is a GUI pass.
- TASK-166's and TASK-167's debt stands unchanged.

## Status

Complete and verified. Goes to review, not done; the issue is not closed here.
