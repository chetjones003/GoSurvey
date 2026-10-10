# TASK-218 — REQ-329 increment 1: MOVE / COPY operate in the active UCS and in 3D

- Type:    feature (increment 1 of an accepted cross-cutting requirement)
- Status:  done — PR pending
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #402

## 1. Authority

- REQ-329 (accepted 2026-09-08, D-2026-09-08-b) — increment 1 of 7 (MOVE/COPY).
- Issue #402 draft acceptance criteria 1-2 and the regression guard, for MOVE and COPY only.
- SPEC GAP resolved with the user before any code: one cross-cutting REQ sliced per command;
  ROTATE (increment 2) stays UCS-Z-only, not arbitrary-axis.

## 2. Problem

MOVE and COPY captured their base/second points as plain world X/Y and translated by `(dx, dy)`
with no Z. In an orbited view the cursor delta did not match the on-screen motion, and under a
rotated/tilted UCS a typed `@5,0,0` ran along world X, not UCS X. REQ-322 had already given typed
MOVE a *world* Z; it did not make either command UCS-aware, and the picked path was left flat with
an explicit "stays plan-only for now" note.

## 3. Scope

IN:
- Picked base/second point: resolved at `CadCommitElevation(st)` (the active work plane's elevation,
  or a snapped point's own Z), producing a full `(dx, dy, dz)` world delta. `CadUi` / the headless
  `CLICKUCS` already publish the work-plane hit's Z through `resolvedPointZ`.
- `ResolveTypedModifyPoint` (new static, next to `PeelTypedElevation`): typed MOVE/COPY point →
  storage X/Y + world Z. World-UCS branch is the pre-REQ-329 path byte-for-byte (`ParseStoragePoint`
  + peeled elevation as a world Z). Non-world-UCS branch parses the whole `@dx,dy,dz` / `x,y,z` and
  places it with `ucs::UcsVectorToWorld` / `ucs::UcsToWorld`. Bearing / distance<angle typed forms
  fall back to the flat in-plane parser with no elevation change — a stated increment-1 limitation.
- `HandleModifyText` NeedBase / NeedDestination rewired onto `ResolveTypedModifyPoint`; the
  `consumed` out-param preserves the existing "reported → hold phase" vs "unparsed → fall through"
  split.
- `FinalizeCopyTranslation` gains a `dz` parameter → `DuplicateCadSelectionTranslated(dx,dy,dz)`
  (already 3D from REQ-322 / ARRAY levels).
- `DuplicateSelectedSurveyPointsTranslated` gains a `dz` parameter (`copy.elevation += dz`), fed by
  the new `AppCommandState::pendingCopyDz` through the COPY duplicate-ID modal.

OUT (increments 2-7 of REQ-329, tracked in issue #402):
- ROTATE, SCALE, STRETCH, MIRROR, ALIGN, OFFSET — each its own branch/task/PR.
- A full ROTATE3D about an arbitrary picked axis (separate future issue, per D-2026-09-08-b).
- The transform gizmo ghost / `TransformPreview.cpp` — unchanged this increment (MOVE's translate
  gizmo already goes through `ApplyTranslationToSelection`, which is 3D).
- Bearing/distance typed entry in a 3D delta.

## 4. Files

- `src/commands/CadCommands.hpp`: `pendingCopyDz` field.
- `src/commands/CadCommands.cpp`: `ResolveTypedModifyPoint` (new), `HandleModifyText`
  NeedBase/NeedDestination, `FinalizeCopyTranslation` signature, the MOVE/COPY viewport-pick block
  in `SubmitViewportPickImpl`, the survey-dup modal apply call.
- `src/survey/SurveyPoints.{hpp,cpp}`: `DuplicateSelectedSurveyPointsTranslated` `dz` parameter.
- `tests/headless/transcripts/issue402-move-copy-ucs.txt` (new).

## 5. Regression guard

- `req322-move-3d.txt` passes unchanged (typed MOVE, world Z, solids).
- New transcript's first two scenarios are World-UCS typed + picked MOVE and match the pre-change
  numbers exactly.
- `issue400-array-ucs-plane.txt`, `regression-87-array.txt`, `req154-ucs-plan.txt`,
  `req312-arbitrary-plane-curves.txt` all pass unchanged (shared UCS helpers untouched).

## 6. Tests

`tests/headless/transcripts/issue402-move-copy-ucs.txt`: World-UCS typed regression; World-UCS
picked regression; UCS-Z-90 typed `@5,0,0` (steps along world Y) and absolute `0,0,7`; UCS-X-90
picked MOVE (lifts in world Z) and typed `@0,0,3` (along UCS Z = world -Y); COPY under UCS-X-90
(duplicate carries the 3D delta, normal preserved); rotated-in-plan UCS keeps Z at 0.

Survey-point Z delta (`copy.elevation += dz`) is a one-line mirror of the existing `easting += dx`
and has no headless path (the COPY duplicate-ID modal is not driven by the headless runner);
verified by inspection + a manual GUI COPY under a Front UCS.

## 7. Verification

- `build-project`: PASS (release) — `GoSurvey.exe`, `gosurvey_headless.exe`, both test binaries link.
- `testing`: full `dev/test` — see completion report.
- `architecture-review`: no layer change. New Commands-layer static helper + one `AppCommandState`
  field + a `dz` parameter threaded through an existing Survey-layer function — the same shape as
  REQ-322 and the ARRAY increments.
