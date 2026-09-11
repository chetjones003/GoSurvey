# TASK-217 — REQ-327 increment 4: smart (drawn-line) TRIM in true 3D

- Type:    feature (increment on an accepted requirement)
- Status:  done — PR pending
- Opened:  2026-09-08
- Owner:   Workshop
- GitHub:  #399

## 1. Authority

- REQ-327 (accepted 2026-09-07; increment 4 added 2026-09-08, D-2026-09-08-a).
- Issue #399 draft acceptance criteria 1-4 — now met for the DEFAULT trim trigger too, not only
  the classic pick.

## 2. Problem

Increments 1-3 made only the classic "pick cutting edges → click the piece to remove" flow
(TRIMSTATE 1, `Try3DLineTrim`) 3D-aware. TRIMSTATE 0 — the default, two clicks draw a line across
the drawing — stayed flat world-XY (`ExecuteDrawnSegmentTrimOnce`). In an orbited view / Front-style
UCS the drawn trim line dropped its Z and committed on the world ground plane, far from the geometry
(user report against issue #399's screenshot: line + circle drawn in a Front UCS, trim line landed
at the world origin).

## 3. Scope

IN:
- `AppCommandState::trimCutInfP1z` / `trimCutInfP2z` (new) — the drawn points carry elevation on the
  pick-ray path.
- `Collect3DTrimCrossings` / `Apply3DTrimCut` (new static helpers) — the increment-1-3 crossing loop
  and apply step, extracted from `Try3DLineTrim` so both TRIM paths share one copy.
- `Solve3DDrawnLineTrim` (new static) — target chosen by true 3D closest approach to the drawn
  segment (`SegSegClosest3D`, same match tolerance `ExecuteDrawnSegmentTrimOnce` uses); crossings via
  `Collect3DTrimCrossings` against the whole drawing; removed side = the one containing the drawn
  line's midpoint. Shared by the commit and the preview.
- `Try3DDrawnLineTrim` (new) — `Solve3DDrawnLineTrim` + `Apply3DTrimCut`.
- `SubmitTrimViewportPick` `CuttingLine_WaitP1` / `WaitP2`: capture Z, route to `Try3DDrawnLineTrim`
  when a valid pick ray is supplied; ORTHO/POLAR on point 2 via `ApplyOrthoConstrainFromAnchor`
  (UCS-aware) on that path. Flat path byte-for-byte unchanged.
- PREVIEW (camera-aware, follow-on commit same day): `main.cpp`'s trim cutting-line block passes both
  rubber-band ends' real Z (`PushRubberSegViewRel` z args) with UCS-aware ORTHO; the dashed removal
  hint routes to `CadTrimAppendCutLineRemovedPreview3D` (new, on `Solve3DDrawnLineTrim`) in an
  orbited model view. Plan view / paper keep `CadTrimAppendCutLineRemovedPreview` (issue #166 perf).

OUT (explicit follow-on, not this task):
- Circle/Arc/Ellipse as a trim TARGET (pre-existing 2D-parity gap).
- True curved (tilted-arc) polyline-segment intersection (REQ-325 territory).
- Skew line/polyline-vs-curve closest-approach solve.

## 4. Files

- `src/commands/CadCommands.hpp`: two float fields; `CadTrimAppendCutLineRemovedPreview3D` decl.
- `src/commands/CadCommands.cpp`: `Collect3DTrimCrossings`, `Apply3DTrimCut` (extracted),
  `Try3DLineTrim` (rewired to the helpers — behaviour identical), `DrawnLineTrimSolution` +
  `Solve3DDrawnLineTrim` + `Try3DDrawnLineTrim` + `CadTrimAppendCutLineRemovedPreview3D` (new),
  `SubmitTrimViewportPick` cutting-line phases.
- `src/app/main.cpp`: trim cutting-line preview block — real-Z rubber band, 3D dashed hint in orbit.
- `tests/Trim3DDrawnLineTests.cpp` (new), `CMakeLists.txt` registration.

## 5. Regression guard

- `Trim3DLineLineTests.cpp` (increments 1-3) must still pass unchanged after the helper extraction.
- `Trim3DDrawnLineTests.cpp` last case: no pick ray → flat `ExecuteDrawnSegmentTrimOnce` path,
  identical result to before.
- `TrimLinePreviewTests.cpp` (2D drawn-cut-line preview) unchanged.

## 6. Tests

`tests/Trim3DDrawnLineTests.cpp`: coplanar X-Z-plane crossing; XY-projection-only crossing refused;
midpoint picks the removed side; coplanar circle cutter; polyline target segment (siblings
untouched); preview emits the removed portion in 3D; preview rides an elevated target's Z;
plan-view regression (no pick ray).

## 7. Verification

- `build-project`: PASS (release).
- `testing`: PASS — `GoSurveySnapTests` 1727 assertions / 170 cases; full `dev/test` ctest 0.
- `architecture-review`: no layer change — Commands-layer helpers + one new `AppCommandState` field,
  same shape as increments 1-3.
