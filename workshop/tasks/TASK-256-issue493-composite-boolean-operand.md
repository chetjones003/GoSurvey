# TASK-256 — issue #493: composite-operand analytic Booleans (REQ-337)

## Requirement authority

REQ-337 (`spec/requirements.md`), D-2026-09-14-a. Continues REQ-314's Boolean recogniser
architecture (ADR-046) rather than replacing it.

## Why

SUBTRACT refused a stepped/shouldered cylindrical shaft (two coaxial cylinders of different radius,
already unioned into one solid) passing through a flange, with `Problem::BooleanCurvedFace` — even
though a single plain cylinder through the same flange already works (REQ-314 B2a). Every existing
`TryBoolean*` recogniser in `src/util/brep.cpp` pattern-matches a *single* primitive surface pair per
operand; none of them look inside an operand that is itself a union of two recognised pieces.

The user was offered a narrower fix (decompose a coaxial-stepped cutter only) and a full
classification-based general engine (any curved solid vs. any curved solid, no per-configuration
recognisers at all). They chose to record the general engine as the eventual direction (now the
scope-boundary paragraph of REQ-337) while accepting that this task ships only the decomposition
increment — the same delivery-order reasoning ADR-046 used for REQ-314's own B1→B2b-2 slicing.

## Files / subsystems affected

- `src/util/brep.hpp` / `brep.cpp` — a new decomposition helper that, given a `Solid`, tries to
  recognise it as a union of two-or-more coaxial cylinder primitives (same axis, sequential, no
  gap) and returns them as separate `Solid`s in order along the axis. Domain-layer, no GL/ImGui.
- `src/commands/CadCommands.cpp` — `CommitBoolean`'s SUBTRACT path: when a subtrahend solid is
  refused with `Problem::BooleanCurvedFace`, try the new decomposition before falling through to the
  existing single-cylinder `TryGetCylinderInfo` retry (PR #492); if decomposition succeeds, fold the
  pieces through the *existing* per-piece recognisers exactly as a multi-solid subtrahend selection
  already folds today.
- `tests/BrepTests.cpp` — decomposition unit tests (recognise / reject cases).
- `tests/CadBlockImportTests.cpp` or a new `CadCommandsBooleanTests.cpp` — command-level SUBTRACT
  test reproducing the shaft/flange repro from issue #493.

## Existing code to reuse

- `FoldBoolean` (`CadCommands.cpp`) already folds a multi-solid selection left-to-right; the new
  per-operand decomposition reuses the identical fold, just one level deeper (inside one operand).
- `TryGetCylinderInfo` (`CadCommands.cpp`, from PR #492) already extracts axis/radius/length from a
  single bare-cylinder primitive (4 faces / 4 vertices / 6 edges) — the decomposition helper reuses
  this exact recognition per constituent piece, so a coaxial stack is just "N pieces that each pass
  `TryGetCylinderInfo`, sorted by their axis projection, touching end-to-end."
- `brep::SubtractCircleThrough` (PR #492) is the per-piece cut primitive.

## Implementation approach

1. In `brep.cpp`, add `TryDecomposeCoaxialCylinderStack(const Solid&, std::vector<CylinderInfo>*)`
   (domain-layer type, not `CadCommands`' local `TryGetCylinderInfo` — move that logic down into
   `brep.hpp`/`brep.cpp` as a shared `brep::TryGetCylinderPrimitiveInfo`, since both the single-piece
   retry from #492 and this new multi-piece decomposition need it, and duplicating it in
   `CadCommands.cpp` would drift). A solid decomposes when: every face belongs to one of N groups,
   each group is a bare-cylinder topology on its own, all N cylinders share one axis (direction
   parallel within tolerance, axis lines coincident within tolerance), and consecutive cylinders
   (sorted by their axis-projection midpoint) touch with no gap and no overlap beyond their shared
   cap. Returns the pieces in axis order.
2. In `CadCommands.cpp`'s SUBTRACT path (`CommitBoolean`), when `brep::BooleanSubtract(piece, sub, ...)`
   refuses with `BooleanCurvedFace`, try `brep::TryDecomposeCoaxialCylinderStack(sub, &pieces)`
   before the existing single-cylinder retry; on success, fold `SubtractCircleThrough` across every
   piece in axis order (each piece's own centre/normal/radius, auto-depth per REQ-314 B2a), same
   pattern as the existing multi-subtrahend fold.
3. Keep the existing single-cylinder `TryGetCylinderInfo` retry as the N=1 case — either it becomes
   `N==1` inside the same decomposition helper (preferred, less duplication) or stays a fast path;
   decide during implementation based on how much the topology-grouping logic can be shared cleanly.

## Test approach

- `BrepTests.cpp`: a two-step coaxial cylinder union recognised by the decomposition helper with
  pieces returned in correct axis order and correct radii/lengths; a three-step stack; a rejection
  case (two cylinders NOT coaxial — different axis directions); a rejection case (a gap between
  the two cylinders, not touching); confirm a plain single-cylinder solid still resolves (N=1).
- Command-level test: build a flange (box or short wide cylinder) and a two-step coaxial shaft
  through it, SUBTRACT, assert success and a plausible resulting volume (flange volume minus the
  swept-through portion of each step), matching the issue #493 repro shape.
- Full `[brep]` and `[block]`/boolean-tagged regression suites stay green.

## Architectural-boundary check

- Stays inside REQ-314's existing `Problem::` refusal contract — a case this can't decompose still
  refuses by name, document untouched (REQ-201).
- No new `Solid` field, no `.gs` format bump — decomposition operates on an already-built `Solid`'s
  topology at Boolean time, produces ordinary Boolean results indistinguishable from any other.
- Domain/Commands layering preserved: recognition logic lives in `brep.{hpp,cpp}` (pure geometry,
  no `AppCommandState`); only the retry-on-refusal wiring lives in `CadCommands.cpp`.
