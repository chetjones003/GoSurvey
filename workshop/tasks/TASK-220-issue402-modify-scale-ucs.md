# TASK-220 — REQ-329 increment 3: SCALE is uniform about the UCS-resolved base

- Type:    feature (increment 3 of an accepted cross-cutting requirement)
- Status:  done — PR pending
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #402

## 1. Authority

- REQ-329 (accepted 2026-09-08, D-2026-09-08-b) — increment 3 of 7 (SCALE).

## 2. Problem

SCALE captured its base point as plain world X/Y and `ApplyScaleToSelection` scaled only X/Y (and
every size) about it — elevations were left untouched (REQ-322's "scales about a flat point" note).
Under a tilted UCS the geometry lives in 3D and a uniform scale must be uniform on every axis.

## 3. Scope

IN:
- `st.modifyBaseZ` captured from `CadCommitElevation` on SCALE's typed (`HandleScaleText`) and
  picked (`SubmitViewportPickImpl`) base-point paths.
- `ScaleSelectionZAboutBase(st, bz, sc)` (new static) — `z' = bz + sc*(z - bz)` on the Z field of
  every selected Line / Circle / Arc / Ellipse / Polyline / FilledRegion / Annotation / Table /
  BlockRef position, plus feature-line vertices and survey-point elevations.
- `FinishScaleCommand` calls it after `ApplyScaleToSelection` **only when `!CadWorkPlaneIsWorldXy`** —
  plan view and any plan-rotated UCS keep the pre-REQ-329 "elevations untouched" behaviour
  byte-for-byte (there the base sits on the world XY plane with the geometry and a Z pass is a no-op
  or an unasked-for change).

OUT (increment-3 boundaries):
- The two-point *reference length* (`SCALE R`) is still measured in projected world X/Y under a
  tilted UCS — the typed scale factor is exact. Matches ROTATE's reference sub-mode limit.
- BlockRef `xf.sz` and PDF-underlay Z are not touched (positions only).
- STRETCH / MIRROR / ALIGN / OFFSET — later increments.

## 4. Files

- `src/commands/CadCommands.cpp`: `ScaleSelectionZAboutBase` (new), `FinishScaleCommand` (tilted Z
  pass), `HandleScaleText` `NeedBase` and the SCALE viewport-pick `NeedBase` (capture Z).
- `tests/headless/transcripts/issue402-scale-ucs.txt` (new).

No new `AppCommandState` field — `modifyBaseZ` already exists (REQ-329 increment 1).

## 5. Regression guard

- `req087-feature-line-modify.txt` (SCALE 2 leaves a feature line's elevation 12.000 unchanged)
  passes unchanged.
- New transcript's World-UCS scenarios (SCALE 2, and SCALE 2 on a line moved up to Z 5) match the
  pre-change numbers; the UCS-Z-30 scenario keeps the elevation untouched.
- `regression-119-variant-coverage`, `issue124-blocks`, `req312-tilted-curves-drawn-and-edited`,
  the ARRAY transcripts, and the increment 1/2 transcripts all pass unchanged.
- Full headless corpus 130/130; `ctest -E headless` 1 failure (`a missing or corrupt store loads as
  an empty list`, pre-existing on `beta`, unrelated).

## 6. Tests

`tests/headless/transcripts/issue402-scale-ucs.txt`: World-UCS SCALE regression; World-UCS SCALE of
an elevated line leaves Z alone; tilted UCS (X 90) — a line along the UCS Y axis scales its
elevation; a circle off the base plane scales its centre elevation and radius (normal unchanged);
SCALE about a non-zero base elevation; UCS-Z-30 regression (elevation untouched).

## 7. Verification

- `build-project`: PASS (release).
- `testing`: PASS — see §5.
- `architecture-review`: no layer change. One new Commands-layer static, same shape as REQ-329
  increments 1-2. No new state.
