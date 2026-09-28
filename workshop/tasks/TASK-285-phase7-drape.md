# TASK-285 — DRAPE lays geometry on a surface, baked

- Type:    feat
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  #150 (3D Phase 7), increment 1

## Requirement authority

REQ-074 (surface elevation query, never extrapolated), REQ-101, REQ-201, REQ-076.
**ADR-062** (new, this task) and **D-2026-09-28-f** settle the reference model the whole phase hangs
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
  `CadCommands.hpp:3147`. ADR-062 (c) keys a link the same way rather than re-earning that bug.
- The only `Drape` in the tree was a ribbon button reading "not implemented yet".

## What changed

- `spec/architecture.md` — **ADR-062**: baked by default; link opt-in, keyed by stable entity id,
  visibly marked, and degrading to plain geometry when its surface goes; a vertex off the surface
  refuses its whole entity.
- `spec/project.md` — **D-2026-09-28-f**.
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

## Not in scope

- **The opt-in link** (ADR-062 (b)–(e)) — the next increment: a stored surface id, the visible mark,
  re-evaluation on rebuild, and the degrade-to-plain path when the surface is erased.
- Projecting along a direction other than straight down.
- A solid generated from a surface + boundary + depth, and a general `SWEEP` command — later Phase 7
  increments.
- Draping a circle, arc or ellipse: refused by name, since the result would no longer be that shape.
