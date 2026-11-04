# TASK-grading-toolkit-stage2 — Reuse grading settings and edit elevations in Qt

- Type: feature
- Status: verify (Verification APPROVE; full suite passes, screenshot approval pending)
- Opened: 2026-11-03
- Owner: Codex / Workshop

## 1. Authority

- Goal: no numbered GOAL is defined for this feature in the current project spec. Direct authority is accepted REQ-459 / REQ-088 and D-2026-11-02-a.
- Requirements: REQ-459 (accepted 2026-11-03, D-2026-11-03-a / ADR-076) and REQ-088 (accepted).
- Constraints: REQ-076 stable identity; REQ-101 coordinate/elevation accuracy; REQ-201 visible errors; REQ-300 dependency policy; REQ-301 minimal abstractions; ADR-044 DWG JSON trailer; ADR-075 grading ownership; ADR-076 preset ownership.
- Acceptance (REQ-459):
  > Copy Create explicitly copies an existing grading's inputs to a chosen new baseline, rather than editing the source. Named presets copy their values into each created result; changing a preset never silently updates previous results. Build a Qt elevation table using the existing FLELEV elevation/grade/raise/insert/delete services, preserving one undo transaction per applied edit. Add grading/group properties and visible membership transfers. Acceptance includes independent copied results/presets, accurate displayed elevations/grades, undo, failure preservation and DWG persistence. Linked criteria, locking propagation and automatic regrading are outside this stage.
- Acceptance (REQ-088): a feature-line elevation table shows station, elevation, length ahead, grade back and grade ahead; elevation edits update adjacent grades; grade edits update downstream elevations; raise/lower operations are supported; elevation points can be inserted/deleted independently of PIs. Each edit is one undo step and breakline surfaces rebuild without user action.
- Owning subsystems: Commands/Domain own grading definitions and FLELEV mutation; IO owns the DWG trailer; Qt owns table, palette and property presentation. Typed FLELEV remains authoritative.

## 2. Scope

- In scope: independent Copy Create; drawing-local named presets saved in DWG; Qt FLELEV table; grading/group properties; visible group membership transfer.
- Out of scope: automatic criteria propagation/regrading; linked criteria; locking propagation; optimizer #813; Stage 3 transitions/benches; Stage 4 utilities/quality controls.
- Smallest change: adapt existing grading definitions, FLELEV services, group commands and property/UI services; add only the preset value collection approved by ADR-076.

## 3. Architectural boundary check

- Preset ownership and serialization were approved and recorded by D-2026-11-03-a / ADR-076: value-owned records in active drawing state, per-tab `DrawingDocument`, and `DrawingGeometrySnapshot`; additive optional field in the existing ADR-044 trailer; each preset stores criteria but no baseline/result id; create/update/delete is undoable.
- The Qt table routes edits through existing FLELEV services and shows derived row data. Copy Create and membership transfer route through shared grading/group commands. No new entity kind, global store, dependency, or live graph is introduced.
- [x] No architecture/data-format decision remains open.

## 4. Verification plan

- Architecture: one owner per drawing under the existing active/per-tab/undo snapshot pattern; IO serializer stays in IO; Qt stays an adapter; no upward dependency, global, live graph, or new external dependency.
- Code quality: reject empty/duplicate preset names and nonfinite/out-of-range criteria without mutation; persist a deleted target-surface reference as-is and refuse to apply it without substitution; preserve source result on Copy Create; no stale index references.
- Dependencies: none proposed.
- Performance: table values are derived on demand by the existing row builder; no rendering hot-path change.
- Tests: independent copied result identity/criteria; preset create/update/delete undo and DWG save/reopen; applying and later editing a preset leaves prior results unchanged; tab switching; legacy drawings without presets; malformed preset payload rejected before document replacement; FLELEV table values against `BuildFeatureLineElevTable`; all supported edits route through existing operations and undo once; invalid/cancelled edits preserve geometry and redo history; group properties/transfers reflect stable membership and survive undo/reopen; Qt screenshots for empty, valid, edit, error and membership states.
- Files: `CadEntities.hpp`, `CadCommands.hpp/.cpp`, `GsIo.cpp`, `EditorGradingPalette.cpp`, a Qt FLELEV editor, `EditorProperties.cpp`, command/ribbon routing, CMake source list, command and Qt tests.
- Steps:
- [x] Add preset value state, per-tab/undo snapshots, optional trailer serialization and validation; cover JSON and DWG save/reopen, malformed payload preservation, uniqueness and undo lifecycle.
- [x] Add preset lifecycle and Copy Create to the palette; test independent results, copied values and undo.
- [x] Add the FLELEV table and route every mutation through existing services; focused table/edit/undo test passes and existing REQ-088 transcript covers remaining edits.
- [x] Add grading/group properties and membership transfer view; palette actions use shared group commands, with group membership visible in Properties.
- [x] Rebuild and rerun full Release CTest after review fixes; focused Stage 2 Qt tests pass 10/10 including DWG round-trip, invalid surface-id payload, explicit create-from-preset path, per-tab isolation, and deleted-surface refusal. Full Release CTest passes 194/194 on the current `beta` base after conflict resolution; `build.output-layout` passes.
- [x] Present current screenshots and receive approval before commit (2026-11-03).
- [x] Pre-PR documentation review: `README.md` and `PRIVACY.md` reviewed; no product-facing content there is affected. Updated `resources/wiki/Point-Groups-and-Surfaces.md` and the public GoSurvey GitHub Wiki page `Point-Groups-and-Surfaces` with preset, target, group-boundary, deletion and FLELEVEDIT behavior. The public wiki update was published as `f87eb15`.

## 5. Decisions

- Q1 (2026-11-03): Preset scope? Answer: per drawing, saved in DWG.
- Q2 (2026-11-03): Approve value-owned per-drawing records, additive trailer persistence, undoable preset create/update/delete? Answer: approved; D-2026-11-03-a / ADR-076.

## 6. Verification result

- Verdict: **APPROVE**. Architecture is recorded; ownership, persistence defaults, undo, failure behavior and non-propagation are testable. Dependencies: none. Hot-path impact: none.
- Implementation verification and screenshot approval are complete. Pre-PR documentation review is recorded above.

## 7. PR-agent follow-up (2026-11-03)

- Preserved infill membership and regenerated its closed boundary when a grouped grading result is rebuilt; explicitly adding an infill result as a grading member now snapshots baseline-to-daylight strips instead of reusing the infill ring.
- Elevation edits now report parse/range errors without reusing unrelated command-log entries. The grading palette resolves stored target surfaces by stable ID and clears a missing selection rather than substituting another surface.
- Added regression coverage for infill rebuild/conversion, invalid elevation text and deleted target surfaces.
- Release build passed. All six hidden Qt editor shards passed across the final shard runs. Full CTest has not yet been rerun on this follow-up.
- Refreshed UI screenshots approved by the user on 2026-11-03; full CTest remains pending for this follow-up.
