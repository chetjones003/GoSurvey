# TASK-alignment-layout-editor-issue121 — Dockable alignment layout editor (REQ-486, D-2026-11-04-g)

- Type:    feature (remaining REQ-486 statements)
- Status:  done (in review with PR #1152)
- Opened:  2026-11-04
- Owner:   chetjones003 ("Move on to req 486", 2026-11-04), Claude (implementation)
- GitHub:  #121, PR #1152

## 1. Requirement authority
REQ-486 statements 1–7 and its acceptance criteria; D-2026-11-04-f (radius edit keeps the definition, previous task)
and D-2026-11-04-g (this task). Boundary (`spec/architecture.md`): the solver stays in `src/alignment`, and the Qt table
only submits `ALIGNMENTEDIT` commands. No separate CON applies.

## 2. Gap and evidence (CLAUDE.md §5)
Before this task, the layout table was a modal dialog that let every value be edited. It had no parameter-constraint
column, showed no criteria values, had no fixed-station lock, and its Free-curve refusals did not name the tangents.
REQ-486 asked for a "dockable, non-modal" editor showing "which parameters are editable" and "the value required by
the criteria", but it did not say which columns, what a fixed station means, or how a Free curve answers a length edit.
- Repository: REQ-486.1–.5, acceptance 1 (the issue example CURVELEN 500,132.14 → stations 2+50.00 / 3+82.14 / 4+82.14),
  the existing `ALIGNMENTEDIT`/`ALIGNMENTLOCK` commands and the `[req486]` tests.
- Autodesk help: "Alignment Layout Tools" (Panorama Alignment Entities columns), "About Editing Alignment Constraints"
  ("you can only edit the parameters that affect the specified constraints"; derived values are greyed).
- Civil 3D 2026 (this machine, scratch drawing closed unsaved): the panel road in Panorama → Alignment Entities showed
  Tangency constraint and Parameter constraint columns. For the Free curve only the radius cell was editable, and the
  failing row showed a warning icon with a criteria tooltip (`build/artifacts/panel-486/1-civil3d-*.png`).
- Decision D-2026-11-04-g (evidence-backed, recorded in `spec/project.md` and REQ-486 statement 7): a bottom dock with
  the columns listed below; derived values greyed; editable values follow the definition; a Free curve refuses length,
  Δ and direction edits, naming the curve and both tangents; a fixed station refuses any edit, insert or delete that
  would move it, naming the entity and both stations.

## 3. Implementation
- `alignment::kLockStation` (lock mask 15), stored with the other locks in `GsIo`, set with `ALIGNMENTLOCK … STATION`.
- `Alignment::StationLocksHold` runs after the rebuild in `EditEntity`, the three `Insert*After` calls and
  `DeleteEntity`. It maps entity indices across inserts and deletes.
- `alignment::ParameterEditable` and `ParameterConstraintName` derive editability and the definition label from
  the entity kind, its tangency constraint and `definedByLength`.
- `CriteriaRequirementText` (`AlignmentDesign`) returns R ≥, L ≥ and Ls/A bounds from the check set and design speed.
- `AlignmentLayoutPanel` (`src/qt/EditorAlignmentLayout.*`) is a non-modal panel in the `alignmentLayoutDock` bottom
  dock. It has 17 columns, refills when `cadGpuRevision` or the active drawing changes, greys derived cells, marks
  failing rows (warning icon, bold, red wrapped finding), and submits edits as `ALIGNMENTEDIT`.
  `EditorWindow::ShowAlignmentLayout`/`SyncAlignmentLayout` own the dock.

## 4. Tests
- `tests/AlignmentEditTests.cpp`: AC1 issue example; editability and names per kind/constraint/definition; Free-curve
  refusals naming both tangents and redefinition through the constraint; AC4 Free bearing edit and undo round trip;
  station lock refusals for edit, insert and delete; `ALIGNMENTLOCK STATION` with UNDO/REDO; criteria text.
  `[alignment]` 161 cases / 4039 assertions pass.
- `tests/QtEditorTests.cpp`: `issue121AlignmentLayoutEditorEditsThroughTheSolver` (dock stays open, editability flags,
  radius edit, refused bearing edit, undo refill) and `issue121Phase6DesignScreenshots` (17 columns, criteria, warning
  icon, captures).

## 5. Verification
- Build: `build.bat` Release, no warnings. Full suite: `ctest --preset ninja-release` 194/194 passed
  (`build/artifacts/ctest-486b.log`).
- Architecture: the Qt panel holds no geometry logic. Every edit goes through the command and the solver.
- Visual panel: `build/artifacts/panel-486/` — round 2 **PASS** (all medians ≥ 1, no corroborated blocker);
  `RESULT.md` has the scores.

## 6. Product documentation review (REQ-453)
- `README.md`: reviewed; only a one-line alignment summary, no update needed.
- `PRIVACY.md`: reviewed; no data collection change, no update needed.
- `resources/wiki/`: updated `Alignments.md`, `Commands/editalignment.md`, `Commands/alignmentlock.md`,
  `Command-Reference.md` (STATION lock; escaped `|` in alignment command rows).
- Public wiki: updated `Alignments.md` and `Command-Reference.md` (commit 55f76db; it also repairs rows split on escaped
  pipes). gosurveycad.com: `docs/alignments.html` and the Command Reference Alignments block re-spliced (site commit
  9d49eeb, live and checked; `<name>` placeholders now escaped so they are visible).

## 7. Assumptions and follow-ups
- Opening the table does not select the alignment (Properties shows "No selection", no contextual ribbon tab). Civil 3D
  selects it.
- Lengths and stations show no foot marks; bearings are decimal azimuths; column order differs from Panorama.
- The failing value cell has no warning icon of its own (only the row's # cell does).
