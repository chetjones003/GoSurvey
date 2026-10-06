# TASK-285 — DRAPE lays geometry on a surface, baked

- Type:    feat
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  #150 (3D Phase 7), increment 1

## Requirement authority

REQ-074 (surface elevation query, never extrapolated), REQ-101, REQ-201, REQ-076.
**ADR-065** (new, this task) and **D-2026-09-28-l** settle the reference model the whole phase hangs
on, and were recorded before any of this was written.

## Why this, and why first

Phase 7 was taken up to keep clear of #564, which the repository owner is delivering section by
section — §1, §2 and §3 all landed there in one day, and two of us in the same code would duplicate
work. (I had started a branch for §3 before checking, and found it already merged. Checked first
from then on.)

Phase 7's own acceptance names an architectural question and refuses to let Workshop guess it:
does geometry that references a surface **re-evaluate** when that surface changes, or **keep the
shape it was given**? Put to the user in plain English — a photocopy versus a live link — and decided
**baked by default, link on request**.

That decision is what makes `DRAPE` the right first increment: baked needs **no new persisted field
at all**, so the elevations can be proven correct before any link machinery — the risky, new document
content — exists.

## What was found before building (checked, not assumed)

- `TinElevationAtIndexed` plus the per-surface spatial index already solve the query, and the index
  is already cached by stable id + TIN pointer. `DRAPE` reuses that cache rather than rescanning
  every triangle once per vertex.
- **No persisted reference exists anywhere yet.** Every `surfaceId` in the tree is a live-only cache
  (display, query, watershed), explicitly never in `.gs`. So the link is genuinely new document
  content, which is the second reason to defer it.
- The surface caches are keyed by stable id rather than array index *because* `cadSurfaces` compacts
  on erase — the rename-in-flight and erase-then-recreate hazards are written up at
  `CadCommands.hpp:3147`. ADR-065 (c) keys a link the same way rather than re-earning that bug.
- The only `Drape` in the tree was a ribbon button reading "not implemented yet".

## What changed

- `spec/architecture.md` — **ADR-065**: baked by default; link opt-in, keyed by stable entity id,
  visibly marked, and degrading to plain geometry when its surface goes; a vertex off the surface
  refuses its whole entity.
- `spec/project.md` — **D-2026-09-28-l**.
- `src/commands/CadCommands.cpp` — `ExecuteDrapeCommand`, and `DRAPE` in the dispatch beside
  `EXTRACT`, whose surface-naming shape it follows (one surface needs no naming; several are listed
  rather than guessed, because picking silently is how the wrong surface gets used).

Lines, polylines and feature lines drape. Elevations are resolved for a whole entity **before** any
of them is written, so a refusal costs nothing and the undo entry is pushed only when something will
actually move.

## Tests

- `GoSurveySnapTests [drape]` — 6 cases, driven through `ProcessCommandLineSubmit`, so the dispatch
  wiring is under test with the geometry. The ground is a TIN whose elevation is the analytic plane
  `z = 2 + x/10`, so **every draped vertex is checked against hand arithmetic**, not against the code
  that produced it: REQ-101's ±0.002 ft is a real assertion. Covers one-undo-step, the off-surface
  refusal (and that *nothing* moves, not even the covered vertices), a circle refused by name while
  the rest of the selection still drapes, the four "nothing to work with" refusals, and REQ-101 at
  survey magnitude (E 2,196,000).
- `headless.phase7-drape-onto-surface` — the other half: a surface built the way the app builds one,
  from a point group in `samples/surface-demo.dwg`, plus undo/redo, save/reopen and the refusals.
- Full suite: 1819/1826 — `beta`'s 7 headless failures, unchanged.

## Measured rather than guessed

The transcript's coordinates were **confirmed with `SURFELEV` before being used**, not assumed from
the sample drawing's extents: a vertex that happened to fall off the surface would have made `DRAPE`
refuse and the transcript fail for a reason that had nothing to do with the code. The typed-is-world
/ storage-is-local offset (13.607, 0.308) is the same note `req069-surface-definition-commands.txt`
carries.

## Increment 2 - the opt-in link

`DRAPE <surface>, LINK` stores the surface's **stable entity id** on the entity
(`EntityAttributes::drapedOnSurfaceId`, 0 = not linked), and the geometry re-drapes whenever that
surface is rebuilt. Draping **without** `LINK` clears a link a previous drape left - "bake this
where it is now" is the natural way to ask for that, and a stale link would move the geometry
again at the next rebuild.

- **Persistence is additive and omitted at 0**, so a drawing with no link is byte-identical to one
  written before the field existed (ADR-020 (d)). `.dwg` carries it too: `SaveDrawingDocument`
  appends the same GoSurvey JSON payload, so both formats go through `EntityAttributesToJson`.
- **Re-drape is hooked at all three places a TIN is replaced** - `SURFACEREBUILD` for one surface,
  `SURFACEREBUILD` for all, and the async reap - so a link means the same thing whichever way the
  rebuild was driven. Resolve and apply are split, so the command and the re-drape share one
  definition of "on the ground".
- **An erased surface needs no cleanup pass.** `FindSurfaceIndexById` already answers -1 for an id
  that no longer resolves (and for 0), so the link simply stops resolving and the geometry stays
  exactly where it is (ADR-065 (e)). Nothing is cleared eagerly, which also means undoing the
  erase restores the link for free.
- **A linked entity that leaves the ground is left where it is and reported**, keeping its link, so
  it re-drapes on its own once the surface covers it again.
- A refused drape **pushes no undo step**: elevations are resolved before anything is written, so
  an undo after a drape that moved nothing takes back whatever the user did before it.

Tests: `[drapelink]` adds 6 cases - baked stores nothing / LINK stores the id / re-draping bakes it
again, linked follows a rebuilt surface while baked does not, `SURFACEREBUILD` drives it, an erased
surface resolves to nothing with the geometry untouched, leaving the ground is reported and keeps
the link, and the no-undo-step-on-refusal rule. The transcript proves the link **survives a save
and reopen** by rebuilding after reopening and watching it re-drape - a stored field nobody reads
back would pass a weaker test - and that a **rename** does not break it.

## Increment 3 - the visible mark (ADR-065 (d))

A link that lived only in a file and a rebuild would be a hidden attribute: geometry moves and
nothing ever said it would. Two surfaces, one resolver.

- `DrapedOnSurfaceName` (declared in `CadCommands.hpp`) is the single place a link becomes text a
  person reads, so the panel and the report cannot disagree. It answers empty for all three cases
  that are the same to the user - never linked, baked since, and **linked to a surface that has
  been erased** (ADR-065 (e)): the id stays on the entity but the object is no longer following
  anything and must not claim to be.
- **Properties panel** gains a `Surface` section with `Draped on` and `Follows rebuilds`, shown only
  when something in the selection follows a surface. A mixed selection reads `*varies*`, and
  `Follows rebuilds` says `Some` when only part of the selection is linked.
- **`DRAPELINKS`** lists every object that follows a surface, and which one. This is the
  drawing-wide half: the panel answers "does THIS one move?", `DRAPELINKS` answers "what in here
  moves when I rebuild?" - the question actually asked before editing a surface. It is also the
  only one of the two a headless transcript can assert.

Tests: `[drapemark]` covers the resolver (including a non-drapeable type and an out-of-range index,
which answer "follows nothing" rather than reading off the end of a store), the erased-surface
case, and the three `DRAPELINKS` outcomes. The transcript drives `DRAPELINKS` end to end: nothing
linked, one linked, baking it off the list again, and an object whose surface was deleted.

### Found while doing it, and NOT fixed here

`SURFACEDELETE` leaves a **dangling selection**: `RunSurfaceDelete` -> `EraseSurfaceAtIndex` never
touches `st.selection`, so deleting a surface that is selected leaves a selection entry pointing at
a surface that no longer exists, and the document invariant `selection-in-range` trips on the next
`CHECK ALL`. Pre-existing on `beta` - this work touches neither function - and sidestepped in the
transcript by reopening the drawing (which clears the selection) rather than fixed in an increment
about the drape mark. `ESC` does not help: it cancels the active command, not the selection.

## Not in scope

- Projecting along a direction other than straight down.
- A solid generated from a surface + boundary + depth, and a general `SWEEP` command — later Phase 7
  increments.
- Draping a circle, arc or ellipse: refused by name, since the result would no longer be that shape.
