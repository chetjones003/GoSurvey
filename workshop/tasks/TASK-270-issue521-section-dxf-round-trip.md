# TASK-270 — issue #521: a vertical or tilted polyline survives DXF export

- Type:    fix
- Status:  review
- Opened:  2026-09-22
- Owner:   Workshop
- GitHub:  #521

## Requirement authority

REQ-325 (revised this date, under D-2026-09-22-a) and ADR-053 amendment (f). REQ-101 (±0.002 ft),
REQ-201 (no silent wrong answer), REQ-312 (the Arbitrary Axis frame).

## Why

Every non-horizontal polyline was flattened on export: group 38 took the FIRST vertex's Z, the
extrusion stayed (0, 0, 1), and the vertices were the XY projection. A 100 × 50 vertical `SECTION` of
a box came out as a zero-area sliver 100 long at elevation 25. It was recorded as debt in TASK-034,
when nothing made non-level polylines; `SECTION` now makes them routinely, so the debt became data
loss on every section export.

## What changed

- `src/commands/CadCommands.cpp` — `SECTION` writes the cut plane's normal onto each vertex of the
  outlines it creates (REQ-325's per-vertex plane). Without it a sphere's vertical section — a circle
  standing on edge — carried the default +Z and would draw, and export, as if it lay flat.
- `src/io/DxfIo.cpp`, export:
  - `classifyRunPlane` answers flat / planar / not planar for a run. **A run whose vertices are all
    level can still be non-level**: a circle on edge has both vertices at one Z and its arc out of
    the plane, so the stored per-vertex plane is consulted, not just the heights.
  - `emitPolylineRunOcs`: a planar, non-level run is one `LWPOLYLINE` in its own OCS — extrusion =
    the plane normal, group 38 and the vertices in that plane, bulges and closure kept.
  - `emitPolyline3d`: a run that is not planar at all is a 3D `POLYLINE` / `VERTEX` / `SEQEND`, the
    only DXF entity with a Z per vertex.
  - The REQ-325 increment-4 split (flat runs plus one ARC per tilted segment) now runs only when the
    polyline's segments lie in DIFFERENT planes. A single-plane polyline is written whole.
- `src/io/DxfIo.cpp`, import: group 210/220/230 on an `LWPOLYLINE` is read, the vertices are mapped
  back through REQ-312's Arbitrary Axis frame, and the plane is stored per vertex. The normal array
  is kept full-length whenever anything in the drawing carries one (`docinvariants`).

## Tests

- `headless.issue521-section-dxf-round-trip`:
  - the issue's own repro — a vertical section of a box — exported, re-imported, every vertex checked
    in all three axes;
  - a 45° section of the same box, where each vertex's Z equals its own Y, which a flattened export
    cannot reproduce;
  - a sphere's vertical section: one `LWPOLYLINE` carrying group 210 and **no** ARCs, back as one
    closed polyline with its bulges, and still one plane after a second export;
  - a flat polyline round-trips unchanged, which is the case that must not move.
- **Proven to bite:** against the unfixed writer the re-imported vertical outline has every vertex at
  z = 25, and the sphere's section comes back as two ARCs (0 polylines).
- Full suite: 1627/1634; the 7 failures are `beta`'s own.

## Found reviewing this change

- **The per-vertex plane was never persisted.** REQ-325 added the store; no `.gs` key carried it, so a
  section on a vertical or tilted plane came back from its own file flat and exported flat — with the
  DXF side already fixed. `GsIo` now writes `polylineVertsNormal`, additive and guarded exactly as
  `polylineVertsBulge` is, so a drawing with nothing tilted re-saves byte-identically.
- **The 3D `POLYLINE` path had no test.** A genuinely non-planar polyline now round-trips through
  `AcDb3dPolyline` / `VERTEX` / `SEQEND`, asserted vertex by vertex.

## Not in scope

- DWG (`LibreDwgCad`) export has the same shape of ceiling and is untouched here.
- #522 (redundant collinear vertices from a UNION section) is a separate issue.
