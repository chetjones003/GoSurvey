# TASK-222 — REQ-329 increment 5: MIRROR's mirror line lies in the UCS plane

- Type:    feature (increment 5 of an accepted cross-cutting requirement)
- Status:  done — PR pending
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #402

## 1. Authority

- REQ-329 (accepted 2026-09-08, D-2026-09-08-b) — increment 5 of 7 (MIRROR).

## 2. Problem

MIRROR captured its two mirror-line points as world XY and reflected geometry across a
world-vertical plane through that line (`DuplicateCadSelectionReflected` → `ReflectPtAcrossLine`,
Z untouched). On a tilted work plane the mirror line the user drew does not lie in a world-vertical
plane, so the reflection is wrong.

## 3. Scope

IN:
- `ray3d::ReflectPointAcrossPlane` / `ReflectVectorAcrossPlane` (new — pure, Householder reflection,
  the point/direction split `RotatePointAboutAxis` uses). `Ray3dTests` `[req329]` (3 cases:
  world-vertical plane vs the 2D reflection, a tilted plane by involution + signed-distance flip +
  a fixed on-plane point, the normal negating and an in-plane direction fixed).
- `AppCommandState::mirrorP1Z` / `mirrorP2Z` — each mirror-line point's elevation, captured from
  `CadCommitElevation` on the typed (`HandleMirrorText`) and picked paths.
- `DuplicateCadSelectionReflectedAcrossPlane(st, planePt, planeUnit, log)` (new) — reflects
  Line / Circle (+ plane normal) / Polyline across the plane; removes Arc / Ellipse / Annotation /
  Table / BlockRef / FeatureLine from the selection and refuses them (plus survey points) by name.
- `FinishMirrorCommand` branches on `CadWorkPlaneIsWorldXy`: flat path unchanged; tilted →
  `planeUnit = normalize((P2 - P1) × UCS-Z)` (refuses a mirror line parallel to UCS Z), then the new
  reflector. Survey-point mirroring stays on the flat path only (its duplicate-ID modal is 2D).

OUT (increment-5 boundaries, recorded in REQ-329 — all inherited REQ-328 item 2):
- Arc reflection across a tilted plane (reflection reverses arc handedness — its own geometry
  problem).
- Ellipse / Annotation / Table / BlockRef / feature line under a tilted mirror plane.
- Survey points under a tilted mirror plane.

## 4. Files

- `src/util/ray3d.hpp`: the two reflection helpers.
- `src/commands/CadCommands.hpp`: `mirrorP1Z` / `mirrorP2Z`.
- `src/commands/CadCommands.cpp`: `DuplicateCadSelectionReflectedAcrossPlane` (new),
  `FinishMirrorCommand` (branch), `HandleMirrorText` + the MIRROR viewport-pick `NeedP1` / `NeedP2`
  (capture Z), the two `ResetModifyRotateDraft` sites (reset the Z fields).
- `tests/Ray3dTests.cpp`: `[req329]` cases.
- `tests/headless/transcripts/issue402-mirror-ucs.txt` (new).

## 5. Regression guard

- `mirror-basic.txt`, `mirror-click-driven.txt` pass unchanged.
- New transcript's World-UCS and plan-rotated-UCS (Z 30) scenarios match the flat reflection
  (elevation preserved).
- `issue400-array-ucs-plane`, `req087-feature-line-modify`, `regression-119-variant-coverage`, and
  the increment 1-4 transcripts pass unchanged.
- Full headless corpus green; full `dev/test` — see completion report.

## 6. Tests

`tests/Ray3dTests.cpp` `[req329]` (3). `tests/headless/transcripts/issue402-mirror-ucs.txt`:
World-UCS regression; tilted UCS (X 90) — the mirror plane contains the picked line (line reflects
across z = 0 and across an offset plane); a circle's centre and plane normal both reflect; text in
the selection refused by name while the line still mirrors; plan-rotated UCS (Z 30) regression
(elevation preserved).

## 7. Verification

- `build-project`: PASS (release).
- `testing`: PASS — see §5.
- `architecture-review`: two pure additions to `ray3d.hpp` (matching the header's contract, as
  REQ-328 did), two `AppCommandState` floats, one new Commands-layer reflector mirroring
  `DuplicateCadSelectionReflected`'s shape. No layer change.
