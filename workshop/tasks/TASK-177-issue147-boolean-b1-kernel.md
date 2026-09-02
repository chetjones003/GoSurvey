# TASK-177 — Booleans B1 in the B-rep kernel (REQ-314 increment 4, GitHub issue #147)

## Requirement authority

- **REQ-314** — Feature operations on the solid kernel (accepted 2026-09-02, D-2026-09-02-a).
- **ADR-046** — Booleans are analytic B-rep, phased: **B1** covers operand pairs whose every
  intersection curve is already a line or an arc, refusing the rest by name; **B2** adds the general
  analytic intersection-curve type. This is B1's first, tractable slice.
- Builds on REQ-313 / ADR-045, the `WeldPlanarSolid` welder and the classify/clip machinery from
  TASK-176 (slice). REQ-101, REQ-201, REQ-301. GitHub issue #147, Phase 4 of #120.

## Scope of this task — the kernel half, convex planar-faced solids

`brep::BooleanUnion` / `BooleanSubtract` / `BooleanIntersect`, taking two solids and writing a
`std::vector<Solid>` (usually one; a UNION of solids that do not touch is two). No command — that
is a follow-up.

**Convex, planar-faced operands only** — a box, a wedge, a pyramid, a convex extrusion. A curved
face (`BooleanCurvedFace`) or a non-convex operand (`BooleanNonConvex`) is refused: those need B2's
general intersection curve. This first slice deliberately stops short of general planar booleans.

## Implementation approach

Every face of A is clipped by B's face planes into fragments that are each wholly inside or wholly
outside B (for a convex B, its face planes alone decide this); likewise B's faces against A. Then
per operation:

- **INTERSECT** — A-fragments inside B, plus B-fragments inside A.
- **UNION** — A-fragments outside B, plus B-fragments outside A.
- **SUBTRACT (A − B)** — A-fragments outside B, plus B-fragments inside A with their normals (and
  winding) flipped, so they bound the removed volume.

A fragment that lands exactly **on** one of the cutter's planes (a coincident face) is set aside
and resolved against the matching fragment from the other operand: normals agree → one shared face
kept; normals oppose → an internal wall, both cancelled; no partner → an ordinary exterior
fragment, kept by the same inside/outside rule. This is what makes stacked / face-sharing boxes
weld seamlessly (ADR-046's coincident-face hazard).

The kept fragments are welded (`WeldPlanarSolid`) and `Validate`d. Nothing is written unless every
piece validates (REQ-201); `out` is left untouched on failure. An INTERSECT with no shared volume
reports `BooleanEmptyResult`; disjoint operands are detected by a face-normal separating-axis test
(UNION → two solids, SUBTRACT → A unchanged).

New `Problem` values: `BooleanCurvedFace`, `BooleanNonConvex`, `BooleanEmptyResult`,
`BooleanResultInvalid`. `WeldPlanarSolid` gained a `complexReason` parameter so slice and boolean
report their own failure names.

## Test approach (`tests/BrepTests.cpp`, `[brep][req314]`)

- Two overlapping boxes: INTERSECT / UNION / SUBTRACT volumes vs. hand values (`overlap = 360`,
  union `1640`, subtract `640`); the union tessellation winds outward and re-derives its volume.
- SUBTRACT punching a **blind pocket** through one face of a cube — volume `1000 − 24`, Euler
  characteristic still 2 (a pocket, not a tunnel).
- Refusals / reports by name: INTERSECT of disjoint solids (empty), UNION of disjoint (two solids
  back), SUBTRACT of an untouched solid (unchanged), a curved operand.

## Verification

`build-project`, `testing` (full `BrepTests` + wider suite), `code-review`, `architecture-review`
(kernel stays graphics-free and directly unit-tested).

## Status

**Kernel implemented — 2026-09-02 (PR #NNN).** 3 new `[brep][req314]` cases. Full suite green:
**995/995 ctest**. No existing path touched beyond the `WeldPlanarSolid` signature.

### Known limitations carried forward (all named, none silent — REQ-201)

- **Convex operands only** — non-convex refused (`BooleanNonConvex`). General planar booleans (BSP
  or similar) are a later step.
- **Planar faces only** — a plane ⟂ / ∥ to a cylinder axis cuts a circle the kernel *can* hold, but
  that path is not built; every curved operand is refused (`BooleanCurvedFace`). This is B1's next
  slice, alongside curved-face slicing.
- **Coincident faces must match over the same patch** — partial overlap of two coincident faces is
  not resolved; if it makes the weld fail, `BooleanResultInvalid` is returned.
- A tricky corner case can still produce an edge used the wrong number of times → the weld fails →
  the operation is refused with nothing stored (REQ-201 holds; the message is generic).

### Commands — UNION / SUBTRACT / INTERSECT (done, 2026-09-02, PR #NNN)

Selection-driven one-shots, like the EXTRUDE one-liner (no state machine): select **exactly two**
solids, then `UNION` / `SUBTRACT` / `INTERSECT` (aliases `UNI` / `SU` / `IN`). Both operands are
replaced by the result in one undo step; SUBTRACT keeps the first-selected solid and removes the
second. A pair the kernel refuses (curved, non-convex, empty intersect) is reported and the
document is untouched (REQ-201). `CadBooleanSelection` in `CadCommands.cpp`; dispatch + `kRegistry`
beside `SLICE`. Reuses REQ-313's store / `.gs` / render unchanged.

Transcript `tests/headless/transcripts/req314-boolean.txt`: union / subtract / intersect of two
overlapping boxes vs. hand volumes (1640 / 640 / 360), undo restores both operands, >2 selected is
reported, a curved operand is refused, `.gs` round-trip of a subtract result. Full suite
**996/996**.

### Non-convex operands + N-operand commands (done, 2026-09-02, PR #NNN)

The convexity restriction is lifted. Fragment classification switched from a convex half-space test
to a general **even-odd ray cast** against the operand's planar faces (`PointInPlanarSolid`),
robust to the ray grazing an edge by retrying along four incommensurate directions. Face fragments
are still split by *all* of the other operand's face planes — over-splits for a non-convex operand,
but every fragment still ends up wholly inside or outside because the boundary is a subset of those
planes. Disjoint routing switched to an AABB test plus vertex/centroid probes (`SolidsOverlap`).

The `UNION` / `SUBTRACT` / `INTERSECT` commands now take **two or more** selected solids and
left-fold them: UNION keeps disjoint pieces aside and folds later operands against each; a step that
fails is reported with nothing changed.

Tests: `BrepTests` gains a chain — subtract a corner notch (→ non-convex L), then subtract a second
bar from the L, both validated with hand volumes. Transcript adds a three-box UNION folding to one
and a "fewer than two" refusal. Full suite **997/997**.

**Still refused:** curved operands (`BooleanCurvedFace`). `BooleanNonConvex` is now unreachable
(kept in the enum).

### Interactive command flow (done, 2026-09-02, PR #NNN — user feedback)

`SUBTRACT` was a one-shot on a pre-selection ("first − rest"), which is not how AutoCAD works. It
is now a real `Kind::Boolean` state machine: **`SUBTRACT` prompts twice** — "select solids to
subtract FROM", Enter, then "select solids to subtract", Enter. `UNION` / `INTERSECT` prompt once.
A pre-selection answers the first prompt (for `SUBTRACT`, it becomes the "subtract from" set).
Every involved solid is replaced by the result in one undo step.

`CommitBoolean` fold-unions the minuend set, then subtracts each subtrahend solid from every piece.
Wired through the six places (`ViewportPickPolicy` → `SelectionAccumulate` for every phase,
`SubmitViewportPickImpl`, `ProcessCommandLineSubmit` Enter, `CommandInputHint`,
`CancelActiveCommand`, dispatch). `CadBooleanSelection` kept as the pre-selection shortcut.

Transcript rewritten around the two-phase flow: `UNION` from a pre-selection, the full two-prompt
`SUBTRACT`, `SUBTRACT` honouring a pre-selection, `INTERSECT`, a curved refusal, `.gs` round-trip.
Full suite **998/998**.

### Next

Curved operands with line/arc intersections (box ∩ axis-aligned cylinder) — then B2 (the general
analytic intersection curve).
