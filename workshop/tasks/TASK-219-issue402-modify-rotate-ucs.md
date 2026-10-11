# TASK-219 — REQ-329 increment 2: ROTATE turns about the active UCS Z axis

- Type:    feature (increment 2 of an accepted cross-cutting requirement)
- Status:  done — PR pending
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #402

## 1. Authority

- REQ-329 (accepted 2026-09-08, D-2026-09-08-b) — increment 2 of 7 (ROTATE).
- ROTATE is deliberately UCS-Z-only (REQ-328's `RotateSelectionAboutAxis`); a full arbitrary-axis
  ROTATE3D is a separate future issue.

## 2. Problem

ROTATE always turned the selection about world Z through the picked base point
(`RotateAroundBase` / `ApplyRotationToSelection` / `DuplicateCadSelectionRotated`). Under a tilted
UCS (FRONT/BACK/orbited) that is the wrong axis, and the picked rotation angle — `atan2` of the
click's world-XY delta — collapses toward 0/180 the same way #400 increment 4 found for ARRAY's
fill angle.

## 3. Scope

IN:
- `AppCommandState::rotateBaseZ` (new) — the elevation the rotation axis passes through; set from
  `CadCommitElevation` on both the typed and picked base-point paths.
- `FinishRotateCommand` branches on `CadWorkPlaneIsWorldXy`:
  - world-Z-parallel axis (plan view, or any UCS merely rotated in plan) → unchanged
    `ApplyRotationToSelection` / `DuplicateCadSelectionRotated` (byte-identical);
  - tilted UCS → `RotateSelectionAboutAxis` (copy) or the new `RotateSelectionInPlaceAboutAxis`,
    both about `axisUnit = active UCS Z` through `(bx, by, rotateBaseZ)`.
- `RotateSelectionInPlaceAboutAxis` (new static) — the in-place counterpart of REQ-328's duplicating
  `RotateSelectionAboutAxis`. Line / Circle (+normal) / Arc (+normal, re-anchored) / Polyline /
  FilledRegion rotate in 3D via `ray3d::RotatePointAboutAxis` / `RotateVectorAboutAxis`; Ellipse /
  Annotation / Table / BlockRef / PDF underlay / feature line / survey point are refused by name.
  Drops solids/surfaces first, like `ApplyRotationToSelection`.
- `RotateSelectionAboutAxis` gains a `commandLabel` parameter (default `"ARRAY Polar"`) so ROTATE's
  copy path reports its own name in the excluded-entity message.
- The picked rotation angle (`RP::NeedAngleOrReference` viewport click) is measured in the work
  plane's local X/Y under a tilted UCS (`CadResolvePickOnWorkPlaneAnchored` + `ucs::WorldToPlane`);
  the World-UCS path keeps the exact `-atan2(dx, dy)`.
- `CadResolveArrayPickOnWorkPlane` renamed to `CadResolvePickOnWorkPlaneAnchored` (now shared with
  ROTATE) — pure rename, 6 sites.

OUT (increment-2 boundaries, recorded in REQ-329 and matching `RotateSelectionAboutAxis`'s own
pre-existing limits):
- The two-point *reference* rotation sub-modes still derive their angle from projected world X/Y
  under a tilted UCS.
- A bulge / tilted-arc (REQ-325) polyline segment rotates only its vertices.
- Survey-point rotation about a tilted axis (no 3D survey-rotate path exists yet).
- 3D ROTATE gizmo handle, SCALE/STRETCH/MIRROR/ALIGN/OFFSET — later increments / issues.

## 4. Files

- `src/commands/CadCommands.hpp`: `rotateBaseZ` field.
- `src/commands/CadCommands.cpp`: `RotateSelectionAboutAxis` (`commandLabel` param + neutral message),
  `RotateSelectionInPlaceAboutAxis` (new), `FinishRotateCommand` (axis-aware branch),
  `HandleRotateText` `RP::NeedBase` (capture Z), the ROTATE viewport-pick `NeedBase` / picked-angle
  branches, `CadResolveArrayPickOnWorkPlane` rename, `ResetModifyRotateDraft` (+ the 2nd reset site).
- `tests/headless/transcripts/issue402-rotate-ucs.txt` (new).

## 5. Regression guard

- `req312-tilted-curves-drawn-and-edited.txt` (ROTATE turns a tilted circle's plane — but under the
  World UCS) passes unchanged.
- New transcript's first three scenarios (World UCS typed, World UCS picked, UCS-Z-30 typed+picked)
  match the pre-change world-Z numbers exactly.
- `regression-119-variant-coverage`, `req087-feature-line-modify`, `issue124-blocks`, `mirror-basic`,
  `issue400-array-ucs-plane`, `regression-87-array` all pass unchanged.
- Full headless corpus 129/129; full `dev/test` 1311/1312 (the one failure, `a missing or corrupt
  store loads as an empty list`, is pre-existing on `beta` and unrelated).

## 6. Tests

`tests/headless/transcripts/issue402-rotate-ucs.txt`: World-UCS typed + picked regression; UCS-Z-30
regression; tilted UCS (X 90) typed ROTATE spins a line about the UCS Z axis; a circle's centre and
plane normal both rotate; the picked angle is measured in the work plane; ROTATE copy mode duplicates
the rotated selection; text in the selection is refused by name while the line still rotates.

## 7. Verification

- `build-project`: PASS (release).
- `testing`: PASS — see §5.
- `architecture-review`: no layer change. One new `AppCommandState` field + a Commands-layer static
  mirroring `RotateSelectionAboutAxis`; same shape as REQ-322 / REQ-328 / the ARRAY increments.
