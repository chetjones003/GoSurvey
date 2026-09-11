# TASK-161 — REQ-312 ALIGN: re-anchor tilted arcs, fix selective circle stride

- Type:    fix
- Status:  in progress
- Opened:  2026-09-01
- Owner:   chetjones003
- Follows: PR #184 (TASK-159). Branch cut from `pr184` head; lands via its own PR to `beta`.

## 1. Authority

- Goal:         GOAL-01 (a CAD/survey editor whose geometry is correct)
- Requirements: REQ-312 (accepted, arcs/circles in arbitrary planes). Context: D-2026-08-31-f (the
  normal rides in a side-car; angles live in the arc's own `ucs::FromNormal` frame), REQ-101
  (tolerance), REQ-204 (document invariants).
- Constraints:  CON-07 (Windows/MSVC/Ninja build authoritative).
- Acceptance:
  - ALIGN / Helmert applied to a drawing containing a tilted arc leaves the arc with the correct
    centre, radius, plane **and** start point — its endpoints and object snaps land on the visible
    curve.
  - A flat arc through ALIGN is bit-identical to its pre-REQ-312 result (`CadReanchorArcStart` is a
    no-op on a flat arc).
  - A selective ALIGN of a chosen subset of circles transforms exactly that subset — no drift for
    the 4th circle onward.
  - 917/917 existing ctests stay green; new cases added below pass.

## 2. Problem

### 2a. Tilted arc not re-anchored (correctness bug)

`src/commands/CadCommands_Align.cpp`, `ApplyHelmertToAllGeometry`, arc loop (~line 145):

```cpp
arc.startRad += rad;
RotateNormalAboutZ(rad, &arc.nx, &arc.ny);
```

`startRad` is measured in the arc's own `ucs::FromNormal(centre, normal)` frame, which itself turns
when the normal turns. Every other committed rotation path in PR #184 (`ApplyRotationToSelection`,
`DuplicateCadSelectionRotated`, `DuplicateCadSelectionReflected`, `TransformPreview`) pairs the
normal rotation with `CadReanchorArcStart` for this reason; the ALIGN path omits it, so `+= rad`
double-counts and the arc is swept from a point rotated a further ~`rad` inside its own plane. This
is the ASSUMPTION-3 defect the rest of the PR fixes.

### 2b. Selective circle guard uses the wrong stride (pre-existing, now load-bearing)

Same function, circle loop (~line 130): `if (selective && !sCircles.count(static_cast<int>(i / 3)))`
inside an `i += 4` loop over `userCirclesCxCyZR` (stride 4: cx, cy, z, r). `sCircles` holds circle
indices, so the guard must be `i / 4` — the REQ-312 normal code four lines below already uses `i / 4`.
As written, a selective ALIGN matches circle 3 against key 4, circle 4 against key 5, and so on.
Pre-existing since `1b62938`; flagged in the PR #184 description. Folded in here because the new
normal-rotation code is gated by the same broken `continue`.

## 3. Files affected

- `src/commands/CadCommands_Align.cpp` — arc loop: add `CadReanchorArcStart`; circle guard:
  `i / 3` -> `i / 4`.
- `tests/headless/transcripts/` + `HeadlessDriver.cpp` — two new transcripts (below).
- `tests/headless/transcripts/req312-dxf-arbitrary-plane-roundtrip.txt` — new Section 4: a tilted
  arc survives three DXF cycles at state-plane magnitude (review finding 3).

## 4. Approach

Arc loop — mirror the ROTATE pattern; `HelmertPt` applies rotation + uniform scale + XY translation
in one step, and both helpers are already in the shared header `src/commands/CadEntities.hpp`:

```cpp
ray3d::Vec3 sp = CurveWorldPointOnArc(arc, static_cast<double>(arc.startRad));
float spx = static_cast<float>(sp.x), spy = static_cast<float>(sp.y);
HelmertPt(a, b, tx, ty, &spx, &spy);
sp.x = spx; sp.y = spy;                 // Helmert is planar; Z unchanged
HelmertPt(a, b, tx, ty, &arc.cx, &arc.cy);
arc.r *= sc;
arc.startRad += rad;
RotateNormalAboutZ(rad, &arc.nx, &arc.ny);
CadReanchorArcStart(&arc, sp);          // no-op on a flat arc
```

Circle guard: `i / 3` -> `i / 4`.

## 5. Test approach

- `headless.req312-align-tilted-arc` — draw a tilted arc, ALIGN, `EXPECT ARCPOINTS` on both
  endpoints against hand-computed world coordinates within REQ-101. Fails against current code.
- `headless.req312-align-selective-circles` — 5 circles, selective ALIGN of circles 3 and 4 only;
  assert circles 0-2 unmoved and 3-4 moved. A 3-circle case passes even against the bug, so >=4 is
  required.
- DXF (finding 3): the new Section 4 confirms a tilted arc's endpoints hold to well within REQ-101
  across three export/import cycles at ~2.15e6 ft. It is NOT yet a byte-level fixed point — the OCS
  group 10/20/30 coordinate the reader re-projects through the normal's frame wobbles its last ULP
  between cycles. Writing those coordinates at `%.17g` (as group 210 already is) narrows but does
  not close it, and changing tilted-DXF byte output is its own reviewable decision. Filed as a
  follow-up issue: "tilted-curve DXF byte-settle at state-plane magnitude (REQ-204)". Not a blocker
  for REQ-312, whose acceptance is geometric.

## 6. Architectural-boundary check

No new abstraction, no new command, no side-car format change. Uses helpers already public in
`CadEntities.hpp`. No spec change — this is REQ-312 as accepted; the PR description's "not in this
PR" note for the selective-circle stride is absorbed rather than deferred.
