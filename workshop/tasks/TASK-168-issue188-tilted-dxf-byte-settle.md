# TASK-168 — Tilted-curve DXF byte-settles at state-plane magnitude

- Type:    fix
- Status:  in progress
- Opened:  2026-09-01
- Owner:   chetjones003
- Follows: GitHub issue #188 (filed by TASK-161 §5 finding 3 / PR #184 review). Branch
  `fix/issue188-tilted-dxf-byte-settle` from `beta`; lands via its own PR to `beta`.

## 1. Authority

- Goal:         GOAL-01 (a CAD/survey editor whose geometry is correct)
- Requirements: REQ-204 (export → import → export is a byte-level fixed point), REQ-312 (arcs and
  circles in arbitrary planes), REQ-101 (tolerance), REQ-200 (deterministic output).
- Constraints:  CON-07 (Windows/MSVC/Ninja build authoritative).
- Decision:     **D-2026-09-01-c** (recorded in `spec/project.md` §9 before implementation). Of the
  three candidate fixes in issue #188, sweep `$EXTMIN/$EXTMAX` for a tilted arc from the
  reader-reconstructed centre (candidate B). Candidate A (`%.17g` for 10/20/30) was implemented and
  measured first — it narrowed the drift but a variable-length `%.17g` string still wobbled by a
  byte on a 1-ULP re-projection change, exactly as the issue predicted. Candidate C perturbs
  geometry on every write.
- Acceptance:
  - A tilted ARC and a tilted CIRCLE at state-plane magnitude (~2.15e6 ft) on a plane tilted off
    every axis reach a byte-level fixed point: from the 2nd exported file on, consecutive
    export → import → export cycles are byte-identical (`EXPECT SAMEFILE`).
  - No emitted coordinate, angle or extrusion normal changes for any curve — the fix only changes
    which centre the header extents are swept from.
  - A flat ARC/CIRCLE/LINE DXF is byte-identical to its pre-change output — regression-94,
    regression-111, dxf-export-stable, regression-63/64 unchanged.
  - Tilted-curve geometry still holds to well within REQ-101 across cycles (unchanged from #184).
  - Full ctest suite stays green.

## 2. Problem

`src/io/DxfIo.cpp`, `WriteDxf`, the tilted-arc branch of the `$EXTMIN/$EXTMAX` sweep (~line 2642).

A tilted curve's centre is written as an OCS group 10/20/30 in the frame group 210 defines, and
`std::to_string` rounds it to six decimals. At state-plane magnitude, projecting that rounded value
back through the frame (which is what a reader does) lands the reconstructed centre ~5e-7 ft off the
in-memory one.

A tilted **circle**'s `$EXTMIN/$EXTMAX` is the conservative `cx ± r` box and — when `cx` is a round
number — `snapToWritePrecision` absorbs that error, so it was already stable (verified: the
section-3 `SAMEFILE` passes against unmodified code).

A tilted **arc**'s extents are *swept* — its swept range is walked point by point in its plane. The
sweep was anchored on the in-memory centre (`CurvePlane(a)`), so the ~5e-7 ft offset moved a swept
extent point across a sixth-decimal boundary. `$EXTMIN/$EXTMAX` then described a drawing the entity
records did not contain; on re-import `RebaseDrawingToLocalOrigin` re-centred against
`ComputeWorldExtents`, the delta was non-zero, `ShiftAllStorageBy` moved every coordinate through
`float`, and the next export differed at `$EXTMIN` Y and at the arc's group-50 angle. The cycle
never settled.

This is the same class of bug already fixed for polyline extents (#64), arc-angle extents (#111)
and ellipse extents (#113): the header must state what a reader computes, or the import rebase
smears.

## 3. Files affected

- `src/io/DxfIo.cpp`
  - new file-scope helper `DxfWorldToOcs` (the inverse of the existing `DxfOcsToWorld`, through the
    same `ucs::FromNormal` frame);
  - tilted-arc branch of the extents sweep: reconstruct the reader's centre (OCS point → six
    decimals → `DxfOcsToWorld`) and build the walk plane from it instead of from `CurvePlane(a)`.
- `tests/headless/transcripts/req312-dxf-arbitrary-plane-roundtrip.txt` — Section 3 gains a
  2nd/3rd-cycle `EXPECT SAMEFILE` for a tilted circle (guards the already-stable case); Section 4's
  note is rewritten and it gains an `EXPECT SAMEFILE` on the tilted arc's 2nd/3rd cycle.
- `spec/project.md` §9 — D-2026-09-01-c.
- `spec/requirements.md` — REQ-312 Revisions line.

## 4. Approach

```cpp
double dcx = static_cast<double>(a.cx);
double dcy = static_cast<double>(a.cy);
if (!arcFlat) {
  const auto snap6 = [](double v) {
    return std::isfinite(v) ? std::stod(std::to_string(v)) : v;
  };
  ray3d::Vec3 ocs{}, wc{};
  if (DxfWorldToOcs(dcx + st.worldDocumentOriginX, dcy + st.worldDocumentOriginY, (double)a.z,
                    a.nx, a.ny, a.nz, &ocs) &&
      DxfOcsToWorld(snap6(ocs.x), snap6(ocs.y), snap6(ocs.z), a.nx, a.ny, a.nz, &wc)) {
    dcx = wc.x - st.worldDocumentOriginX;
    dcy = wc.y - st.worldDocumentOriginY;
  }
}
const ucs::Ucs arcPlane = arcFlat ? ucs::Ucs{}
                                  : CurvePlane(dcx, dcy, (double)a.z, a.nx, a.ny, a.nz);
```

The emit side (`std::to_string` for group 10/20/30) is unchanged: the emitted OCS point is
self-stable across cycles because `world → OCS → world` is exact to ~1e-10, well inside the
six-decimal grid. Only the swept extents needed the reader's centre.

## 5. Test approach

- `req312-dxf-arbitrary-plane-roundtrip` Section 4: tilted arc, three export/import cycles, plus
  `EXPECT SAMEFILE` on cycle b vs c. Fails against unmodified code ($EXTMIN Y and group 50 differ);
  passes with the fix.
- Same transcript Section 3: tilted circle, `EXPORT → IMPORT → EXPORT` twice more, `EXPECT SAMEFILE`
  cycle 2 vs 3 — passes with and without the fix (regression guard for the already-stable case).
- Flat-curve regressions (94, 111, 63, 64, dxf-export-stable) prove no flat output moved.
- Full ctest run.

## 6. Architectural-boundary check

No new abstraction of consequence — one small file-scope helper that is the literal inverse of an
existing one. No new command, no side-car or `.gs` format change, no new dependency. **No DXF byte
output changes** for any entity: the header states what a reader already computes. REQ-200
(determinism) and REQ-204's flat-curve guarantees are unaffected. The decision was still recorded
(D-2026-09-01-c) because issue #188 explicitly asked for one and candidate A had been tried.
