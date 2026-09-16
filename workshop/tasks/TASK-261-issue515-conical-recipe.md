# TASK-261 — issue #515: a cylinder or cone cuts the same way however it was made

- Type:    fix
- Status:  review
- Opened:  2026-09-16
- Owner:   Workshop
- GitHub:  #515

## Requirement authority

REQ-314 (extrude / revolve / slice) and REQ-315 (loft), both amended 2026-09-16 under
D-2026-09-16-c; ADR-046 amendment (o) and the ADR-048 amendment of the same date. REQ-335 (`SECTION`)
is unchanged: it inherits `Slice`'s accepted set, which is what this widens.

## Why

`Slice` cuts a curved solid only through `SliceCurvedPrimitive`, `SliceCylinderOblique`,
`SliceConeOblique` and `SliceConeObliqueOpenNotch`, and each first checks
`solid.recipe.kind == Cylinder / Cone`. `Extrude`, `Revolve` and `Loft` stored no recipe, so a circle
extruded into a cylinder was refused ("flat faces only") by every cut the identical `CYLINDER`
primitive accepts. Found by the section-coverage probe of 2026-09-16 and filed as #515.

## The decision (asked before any code)

The issue offered two routes, and the owner's comment recommended stamping the recipe at
construction. Looking before starting surfaced two things neither had spelled out: the recipe is
**visible** (`SOLIDLIST`, Properties and `.gs` would say `Cylinder` instead of `Solid`, and two
transcripts pin `Solid` deliberately), and a loft's side faces are **NURBS ribbons**, so a `Cone`
recipe on one would describe geometry it does not store. Put to the user with three options — tag
(owner's route), tag with the loft built true, recognise at cut time with no visible change. Chosen:
**tag, with the loft built true** — D-2026-09-16-c.

## Files / subsystems affected

- `src/util/brep.cpp` — three helpers in an anonymous namespace (`ProfileIsFullCircle`,
  `ConicalBetween` + `StampConicalRecipe`, `RevolveProfileIsConical`) and a hook at the end of
  `Extrude`, `Revolve` and `Loft`, each after that operation's own `Validate`.
- `src/util/brep.hpp` — the `Recipe`, `Extrude`, `Revolve` and `Loft` doc comments.
- `tests/BrepTests.cpp`, `tests/BrepJsonTests.cpp` — new `[issue515]` cases; two existing cases that
  used a two-circle loft *to get NURBS faces* now use three circles.
- `tests/headless/transcripts/issue515-conical-recipe.txt` (new); `req314-revolve.txt` and
  `req315-loft.txt` updated from `Solid` to `Cylinder` / `Cone`.
- `spec/` — REQ-314 and REQ-315 revisions, ADR-046 (o), ADR-048 amendment, D-2026-09-16-c.

No Commands, UI, renderer or IO change: every command reaches these kernel functions already, and
`.gs` already persists a recipe (no format bump).

## Implementation approach

- **After, never instead.** Each hook runs after the operation has built and validated its own
  result, so every existing refusal is untouched; the hook can only add a description (extrude,
  revolve) or swap in the equivalent primitive (loft).
- **Exact shapes only.** Extrude: every edge an arc about one centre, same sweep sense, radius
  constant, sweeps totalling a turn. Revolve: a full turn; three or four straight edges; exactly one
  edge on the axis; the edges leaving its ends perpendicular to the axis. Loft: exactly two profiles,
  both full circles, parallel planes, centres on the shared normal. Tolerances are the kernel's own
  (`planeEps` / `axisEps`, 1e-6 of model scale).
- **Orientation the kernel expects.** The larger circle is the base, because `MakeCone` refuses a top
  radius not below the base, and the slice recognisers rebuild pieces with it.
- **The loft is rebuilt, not tagged,** by `MakeCylinder` / `MakeCone` in a frame from the base centre
  along the axis.

## Test approach and results

- `BrepTests [issue515]`, 3 cases / 15 sections: extruded circle up and down (recipe fields, frame
  spans the solid, topology untouched), rectangle extrude keeps none, a non-circle arc loop keeps
  none; revolve rectangle → cylinder, right trapezoid → cone with the WIDER end as base, right
  triangle → apex cone, partial turn → none, stepped shaft → none and still refused by name; loft
  equal circles → analytic cylinder, growing radius → cone, off-axis circles → freeform and none,
  three circles → none, and a tilted frame at E 2,196,000. Every positive case asserts the same
  `Slice` outcome as the primitive at three planes (across the axis, tilted between the caps, along
  the axis — the refusal must match too) and the same `SectionLoop`.
- **Proven to bite:** with `ConicalBetween` forced false, all 3 cases fail (8 assertions).
- `headless.issue515-conical-recipe` (145 steps): through the commands — `CYLINDER` vs `EXTRUDE`
  sections and slices into four equal halves; `REVOLVE` rectangle → `Cylinder`; two-circle `LOFT` →
  `Cone` with the `CONE` primitive's exact volume, area and topology, and sections; both kinds survive
  `.gs`; off-axis circles still refused by name; a rectangle still extrudes to `Solid`.
- Full suite: see the PR. The 7 `beta` failures present before this change are unrelated (listed in
  the PR).

## Architectural-boundary check

Domain layer only (`src/util/brep.*`), no GL / ImGui / document types. The recipe stays description:
`Validate`, `ComputeMassProperties` and `Tessellate` still never read it, which the unchanged
mass-property tests confirm.

## Debt

- **DEBT-1.** Recognition is by construction, not by topology: a Boolean that happens to leave a
  plain cylinder, or a `SWEEP` along a straight path, still carries no recipe and still does not cut.
  The cut-time recogniser the user did not choose would cover those.
- **DEBT-2.** A revolve profile with extra collinear vertices (a rectangle drawn with a midpoint on
  one side) is not recognised; four straight edges are required.

## Final review (after the PR opened)

Re-read the diff as someone else's. Every consumer of `recipe.kind == Cylinder / Cone` was listed:
the four slice recognisers (which rebuild pieces from the recipe — the same-volume assertions cover
them), `PrimitiveKindName`, `.gs`, and `Translate` / `Rotate` / `Scale` (which keep a recipe that
still describes the solid); every modifying operation already drops it. No UI reads it.

One defect found and fixed: `RevolveProfileIsConical` scaled its tolerance by `|t|`, the distance from
the **picked axis point** — so an axis clicked far along the axis loosened the test (1 ft at 1e6).
It now scales by the profile's own extent, as `Revolve`'s `axisEps` does, with a case that picks the
axis point 500,000 ft away. Full suite re-run: 1513/1520, the same 7 `beta` failures.
