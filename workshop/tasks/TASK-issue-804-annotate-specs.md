# TASK — Issue #804: Annotate tab dimension, centerline, table, scale and text tooling (specification and dimension audit)

- Type:    docs
- Status:  self-verify (Civil 3D panel not run, see section 8)
- Opened:  2026-10-29
- Owner:   Claude (implement-issue run, at the user's request for option 2 of the #804 options)

## 1. Authority

- Goal: GOAL-NN in `spec/project.md` (drawing and annotation fidelity to AutoCAD behaviour).
- Requirements written by this task (all `proposed` until the user records decisions):
  REQ-458 to REQ-472 and REQ-477 in `spec/requirements.md`.
  Existing accepted requirements these depend on: REQ-111 is listed `accepted` in its header, but its
  revision note says "stays proposed" (inconsistency, flagged); REQ-148, REQ-050, REQ-107, REQ-438, REQ-201.
- Decision record: D-2026-10-29-c in `spec/project.md`, open items 1 to 14.
- Acceptance: this task is a specification task. Its acceptance is that every REQ names its Autodesk source,
  its acceptance criteria, and its open decision. No code change is in this task.

## 2. Scope

- In scope: specify the 14 unbuilt #804 controls; audit DIMLINEAR, DIMALIGNED, DIMANGULAR; record evidence.
- Out of scope: any implementation, including the audit fixes (they wait for REQ-477 and REQ-458 to REQ-460 to be accepted).

## 3. Architectural boundary check

- Specs only. No code moved between layers. New REQs name owner layers per `spec/architecture.md`.

## 4. Audit evidence (dimension implementation)

Build: `.\build.bat` (Release) succeeded in this worktree after clearing ninja's own log cache files
(`.ninja_log`, `.ninja_deps`), which failed to recompact with "Permission denied" on the first three attempts.
Existing tests: `GoSurveyTests.exe "[dim-viewport],[dim],[dimangular]"` — 15 test cases, 70 assertions, all pass.
They assert stroke geometry only. None asserts text rotation, measured value after edits, or arc side.

Findings (see REQ-477 and REQ-458 to REQ-460):

| # | Finding | Evidence | Confirmed by |
|---|---------|----------|--------------|
| A1 | Measurement text rotation is not normalised. Right-to-left DIMLINEAR stores +180°; top-to-bottom vertical stores −90°; DIMALIGNED R→L stores −135°; DIMANGULAR with bisector in lower-left stores −135°. Text is drawn at this rotation. | `build/artifacts/repro/dim_probe.cpp` output; `src/commands/CadDimGeom.hpp`; `src/qt/QtAnnotations.cpp:133-150` (label drawn at `labelRotRad` = `rotationRad`); `src/qt/EditorViewport.cpp:1579` (grip path re-derives the same unnormalised rotation) | compiled probe + code reading |
| A2 | Grip-edit refresh of a measurement rebuilds the text with `DimensionStyles::Default()` and sets only precision, so the active style's unit scale is dropped after a grip move. | `src/commands/CadCommands.cpp` `CadDimRefreshMeasurementText` (around line 6398); commit paths use `st.activeDimensionStyle` | code reading (not yet reproduced in-app) |
| A3 | Angular dimensions only ever measure ≤180°. The arc radius follows the cursor along the bisector, clamped to the sector. Whether reflex angles should exist is a conflicting-sources question. | `CadDimAngularComputeFrame` (`acos`, short sweep); `CadDimAngularPickRadius`; Autodesk DIMANGULAR: "The arc is always less than 180 degrees" (line selection); Bricsys: arc position chooses interior or supplement | code reading + web sources; **not decided** (REQ-458 item 2) |
| A4 | `DimensionStyle.textAlign` is saved and loaded (`src/io/GsIo.cpp:1233, 3037`) but never read when placing text. Placement always lifts the text above the line. The stored default `Center` therefore does not match what is drawn. | grep for `textAlign` finds only the IO lines | grep |
| A5 | Test gap: the dimension tests assert geometry, so A1 to A4 passed CI. | `tests/DimViewportTests.cpp` | test read |

## 5. Oracle status

AutoCAD 2025 (`C:\Program Files\Autodesk\AutoCAD 2025\accoreconsole.exe`) is installed. Attempts to run a
script (`/i` blank DWG plus `/s`, with an AutoLISP helper, with and without CRLF and Windows paths) never
executed a command: the console printed `Command:` and waited until the timeout. The AutoCAD oracle is therefore
**blocked**, and no AutoCAD-measured value is claimed in this task. The reflex-angle and text-orientation questions
stay open decisions for that reason.

## 6. Plan (for the implementation tasks that follow acceptance)

- Task 1 (audit fixes): A1, A2, A5 first, because they are defects against the existing `DIMLINEAR`/`DIMALIGNED`/`DIMANGULAR`
  behaviour and need only REQ-477 and REQ-459 accepted. Add unit tests for rotation normalisation and the
  grip-refresh unit scale. A3 and A4 wait for decisions.
- Task 2+ : one task per command group (Continue/QDIM; Centerlines; Table/Extract/Link; Add Labels; Annotation
  scale; FIND), each opened only after its REQ is accepted.

## 7. Open decisions for the user (D-2026-10-29-c)

1. Angular text orientation: bisector-aligned (normalised) or horizontal.
2. Reflex angles above 180°: adopt the ≤180° rule now or support reflex with an arc-placement rule.
3. Text position default: make `Center` (AutoCAD default look, changes existing drawings) or keep `Above`.
4. Angular DIMCONTINUE: defer or build.
5. QDIM geometry: points and line/polyline vertices only, or also arcs and circles.
6. Named dimension styles: keep one style or add a style table.
7. CENTERMARK: non-associative lines (recommended) or associative marks.
8. CENTERLINE: one-time geometry (recommended) or associative.
9. TABLE styles: one built-in style (recommended) or a style table.
10. Extract Data: fixed column set (recommended) or a column picker; output CSV now, Excel later.
11. Link Data: CSV read-only (recommended) or Excel with write-back.
12. Add Labels: survey points only (recommended) or also surfaces and alignments.
13. Annotation scale model: per-object scale list and DWG mapping (architectural; needs its own decision).
14. FIND wildcards: plain text first (recommended) or full wildcard set now.

## 8. Notes

- REQ-453 is used by two different requirements (pre-existing). Not changed here; flagged for the spec owner.
- REQ-111's header says `accepted` while its revision note says "stays proposed". Not changed here; flagged.
- REQ-107's catalogue row (around the "REQ-107 | Domain/Commands/IO/UI | proposed" line) disagrees with its header
  (`accepted`). Not changed here; flagged.
- Nothing is committed. Working tree changes are `spec/requirements.md`, `spec/project.md`, this file, and
  `build/artifacts/` evidence (not committed; `build/` is generated output).

## 9. Implementation results (REQ-477, REQ-459)

- Accepted 2026-10-29 by user decision: REQ-477 (upright text; angular text horizontal), REQ-458 (angles up to 180 degrees), REQ-459 (value follows the style).
- Code: CadDimReadableTextRotation in src/commands/CadDimGeom.hpp; applied at every dimension rotation site and in CadDimStroke render path; CadDimRefreshMeasurementText now takes the DimensionStyle (all callers updated).
- New tests: 4 cases tagged [issue804] in tests/LibreDwgCadTests.cpp.
- Verification: GoSurveyTests full run passed (2594 test cases, 0 failures). ctest qt.editor shards 0-5 passed, headless issue125 and issue252 dimangular transcripts passed.
- Not done: the Civil 3D paired-screenshot panel required by CLAUDE.md section 3 for visible Qt changes. The result is therefore not a completion-gate pass.

## 10. PR #1141 follow-up — DWG styles and Civil 3D round-trip

- User-approved scope: all six reported Annotate defects, a DIMSTY manager/editor modeled on the
  supplied AutoCAD dialogs, and drawing-owned style/object persistence that can be reopened in
  Civil 3D. The supplied Rapides Substation DWG is the baseline.
- REA inspection of the installed Civil 3D assemblies confirmed the managed `CogoPoint`, `PointGroup`,
  `TinSurface`, and `Alignment` APIs and their style relationships. Autodesk documents
  `StyleBase.ExportTo` as the Civil 3D style transfer API, with conflict choices Ignore, Rename,
  Override, and CancelRemaining.
- Native `WblockCloneObjects` experiments on the supplied drawing exposed a Civil 3D API limitation:
  cloning an AECC surface into another database invalidates the destination point manager metadata;
  a subsequent `CogoPointCollection.SetPointNumber` throws `NullReferenceException`. The importer
  therefore uses Civil 3D APIs to recreate supported COGO points, TIN surfaces, and alignments while
  cloning drawing-owned AutoCAD and Civil 3D style records. This avoids corrupting the point manager.
- Reopened-DWG smoke verification using the supplied 353-point / 1-surface baseline found 353 saved
  COGO points, 1 saved native TIN surface, and the `no points` point style on readback. The rebuilt
  TIN has 406 triangles compared with 398 in the source, so exact triangulation preservation is not
  established; source vertex coordinates are transferred and Civil 3D rebuilds topology.
- Latest Release build and `build.output-layout` passed. `GoSurveyTests.exe "[issue804]"` passed 23
  cases / 128 assertions. The focused Qt issue804 dialog/table-preview test passed earlier in this
  run. A full CTest run previously hit an unrelated 300-second timeout in the existing Qt GUI
  `documentTabTrapezoidsNavigationCloseAndCaptures` case after 21 other cases passed.
- Civil 3D comparison-panel status remains inconclusive: the UI screenshots are captured, but the
  required three-reviewer paired comparison has not been completed. Do not treat this task log as a
  clean full-suite or comparison-panel pass.
