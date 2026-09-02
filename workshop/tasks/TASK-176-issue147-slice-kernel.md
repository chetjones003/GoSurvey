# TASK-176 — Slice in the B-rep kernel (REQ-314 increment 3a, GitHub issue #147)

## Requirement authority

- **REQ-314** — Feature operations on the solid kernel (accepted 2026-09-02, D-2026-09-02-a).
- **ADR-046** — delivery order: extrude → revolve → **slice** → Booleans. Slice is where the
  face-split / classify / stitch machinery the Booleans reuse is built and tested.
- Builds on REQ-313 / ADR-045, REQ-101, REQ-201, REQ-301. GitHub issue #147, Phase 4 of #120.

## Scope of this task — the kernel half, planar-faced solids

Mirrors the extrude / revolve split: `brep::Slice` and its tests only — no command, no viewport,
no `.gs`. The `SLICE` command is increment 3b.

Cut a solid by an **unbounded plane**, keeping the `+normal` side, the `−normal` side, or both.
**Planar-faced solids only** (`SurfaceKind::Plane` faces, `CurveKind::Line` edges) — a box, a
straight extrusion, a revolve of a rectilinear profile. A curved face is refused
(`SliceCurvedFace`): an oblique plane through a cylinder cuts an ellipse the kernel's `{Line, Arc}`
curves cannot hold — that case arrives with the analytic Booleans.

## Implementation approach

`bool Slice(solid, planePoint, planeNormal, SliceKeep{Above|Below|Both}, outAbove, outBelow, ...)`.

1. Classify every vertex by signed distance to the plane (`±eps`).
2. For each face: entirely-above / entirely-below faces pass through unchanged; a straddling face
   is clipped into an above polygon and a below polygon, recording the two boundary crossings.
3. A boundary edge lying **in** the plane (the common box-edge-clipped case) is also recorded as a
   cap edge, deduped since it is shared by two faces.
4. Chain the recorded segments into a single cap loop; more than one loop → `SliceResultComplex`.
5. Add the cap as one plane face (normal `−planeNormal` for the above piece, `+planeNormal` for the
   below piece), wound CCW about that normal.
6. **Weld** each kept side's polygons into a `Solid` — vertices merged by position, every undirected
   edge used by exactly two polygons once in each direction — and `Validate`. Nothing is written
   unless every kept piece validates (REQ-201). Results carry no recipe.

New `Problem` values: `SliceDegeneratePlane`, `SlicePlaneMissesSolid`, `SliceCurvedFace`,
`SliceResultComplex`.

## Test approach (`tests/BrepTests.cpp`, `[brep][req314]`)

- A box sliced by a horizontal plane → two boxes, volumes sum to the original and each equals its
  slab's hand value.
- A box sliced by an **oblique** plane through its centre → two equal wedges; tessellation winds
  outward and re-derives the volume.
- `SliceKeep::Above` writes only the above solid and leaves the other output untouched.
- An **extruded L** sliced → both pieces valid, volume conserved.
- Refusals by name: a plane that misses, a degenerate normal, a curved (cylinder) solid.

## Verification

`build-project`, `testing` (full `BrepTests` + wider suite), `code-review`, `architecture-review`.

## Status

**Kernel implemented — 2026-09-02 (PR #NNN).** 5 new `[brep][req314]` cases. Full suite green:
**991/991 ctest**. No existing path touched.

### Known limitations carried forward (all named, none silent — REQ-201)

- Curved faces refused (`SliceCurvedFace`) — planar solids only in 3a.
- A cut that would split a kept side into **disjoint pieces**, or whose cross-section is more than
  one loop, is refused (`SliceResultComplex`) — that is really a Booleans-B concern.
- A face lying entirely **in** the cutting plane is not specially handled; if it makes the weld
  fail, the generic validity reason is returned rather than a specific one.

### Next: increment 3b

The `SLICE` command (select solid → plane by 3 points / an entity / an axis → keep side), and
curved-face slicing (circle cross-sections) alongside the Booleans B1.
