# TASK-217 — the FILLET command on a solid edge (REQ-323 increment 1, command half)

## Requirement authority

REQ-323, whose Owner-layer line already names this slice: *"Commands (the `FILLET` verb on a
sub-object edge selection, and the undo step)"*. Kernel first (TASK-210, 2026-09-05), command second
— the shape REQ-319 and every Phase 4 slice used.

Closes issue #148's acceptance 6 for the fillet, and **half** of acceptance 5: single edges work,
chains do not, because a chain on any real solid has corners.

## What was built

| file | change |
|---|---|
| `src/commands/CadCommands.{hpp,cpp}` | `CadSubObjectSelectionIsAllEdges`, `CadFilletSolidEdges`, and the dispatch in `ProcessCommandLineSubmit` |
| `tests/headless/transcripts/req323-fillet-solid.txt` | new — 62 steps |
| `spec/requirements.md` | REQ-323 status and traceability row |

## Three choices

1. **The same verb, `FILLET`, rather than a new one.** It is the same idea and every CAD package
   spells it the same way. The two paths cannot be confused because the solid path is taken **only**
   when the sub-object selection holds solid edges and nothing else — a state the 2D flow has never
   been able to reach. With no such selection the line falls through untouched, so every existing
   FILLET habit and transcript behaves exactly as before. The transcript's last block asserts that
   directly rather than leaving it to inspection.

2. **Paired with the sub-object selection, not a selection step of its own.** Ctrl+click names the
   edge, `FILLET <radius>` rounds it — precisely how PRESSPULL is paired with the face pick. No mode
   is entered, for the reason D-2026-09-04-a gave: a persistent mode is one a user can be left in
   without noticing.

3. **The sub-object selection is CLEARED on success, and this is where fillet differs from
   push/pull.** `CadApplyPushPull` re-points its references at the replaced solid because a push
   preserves the topology, so the face index still names the same face. A fillet does not: the
   selected edge is *gone* and every index after it has shifted (the kernel compacts). Keeping the
   reference would leave it naming whatever edge inherited the number — a selection that looks live
   and points at something the user never picked. ADR-049's expiring reference is the precedent.

One solid at a time: edges spanning two solids are refused, because two edits under one undo step
with the second's radius checked against the first's result is a compound edit nobody asked for —
the same reason PRESSPULL refuses more than one target.

## The bug the transcript caught in itself

The first draft had **no `VIEWANGLES`**, so it picked edges in plan view. A ray aimed at a top edge
also passes through the bottom edge directly under it, and which one the pick returns is then a
depth-order detail rather than something the transcript states. The block meant to assert that a
**shared corner is refused** had in fact selected one top edge and one *bottom* edge — which share
no vertex — so it quietly performed two independent fillets and asserted nothing. Found by the
volume coming out at 1574.2478 (a 20-long and a 10-long edge both rounded) instead of 1600.

Recorded because the failure mode is invisible: the transcript passed its own selection counts, and
only the geometry disagreed. Every block now orbits first, with the reason written in the file.

Second, smaller finding, also written into the transcript: **a plain sub-object click accumulates**
— `ToggleSubObjectSelection` only ever *removes* under SHIFT, and a plain click on something new
pushes it. So a block that wants one edge selected has to start from a clean drawing, not from a
plain click.

## Test approach

`req323-fillet-solid.txt`, 62 steps, against the closed forms REQ-323 states — including the AREA
the kernel's unit test had already corrected (`792 + 22*pi`; an end face loses the sliver *between*
the arc and the old square corner, not the quarter-disc inside the arc).

Asserted: the fillet itself and its topology delta; the selection cleared afterwards; one `UNDO`;
four refusals (`r = 8` at the exact limit, `r = 100`, `r = 0`, a non-number) each leaving the solid
byte-identical and the selection intact, because nothing was built rather than something rolled
back; a shared corner refused **by name** (`"share a corner"`, not a generic geometry failure); two
disjoint edges rounded as one operation; a `.gs` round-trip at the new geometry; and a bare `FILLET`
with nothing selected still being the 2D command.

## Verification

- **build-project** — PASS. Release MSVC/Ninja, clean tree.
- **testing** — PASS. `ctest` **1305/1305**.
- **architecture-review** — PASS. The command layer owns the meaning; the kernel owns the geometry
  and every refusal; the UI is untouched (this is a typed command, like PRESSPULL's one-line form).
- **code-review** — self-run. It found the missing `VIEWANGLES` above by asking what the shared-corner
  block actually proved.
- **performance-review** — PASS. One whole-solid copy per filleted edge (ADR-046 (d)).

## Not covered by test, stated plainly

- **No ribbon or viewport entry point.** `FILLET <radius>` is typed. The 2D fillet's ribbon button
  still opens the 2D flow, which is correct — but there is no click-driven way to round a solid edge.
- **Edge CHAINS.** Refused by name; increment 2.

## Technical debt

- **DEBT-1 — no prompted form.** PRESSPULL grew one (`StartPressPullCommand`, issue #396) so a bare
  verb walks the user through target and distance with a live ghost. `FILLET` on a solid takes its
  radius as an argument or nothing happens; a bare `FILLET` with edges selected prints usage. A
  prompted form with a radius preview is the obvious follow-up and would want the ghost geometry
  that does not exist yet.
- **DEBT-2 — inherited from TASK-210:** the concave edge, the oblique end face and the spherical
  corner are each refused by name and each their own increment. Only the corner blocks an issue-#148
  acceptance line.
