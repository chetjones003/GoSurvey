# TASK-780 — Export DXF block definitions and INSERT references (issue #780)

- Type: feature
- Status: ready for review; keep GitHub issue open until the combined PR merges.
- Opened: 2026-11-04
- GitHub issue: #780
- Combined PR: paper-space issue batch; continue on `chore/triage-paper-space-issues` per user instruction.

## 1. Authority

- Requirement: REQ-113 (accepted), with REQ-001 and REQ-201 error/reporting rules.
- Acceptance: issue #780 criteria 1–6 and REQ-113 acceptance 1, 3, 4.
- Owning subsystem: IO (`DxfIo`). Existing block-definition values are owned by `AppCommandState`; model and layout references stay in their current stores.

## 2. Scope

- Write DXF BLOCK definitions and their supported contents for referenced blocks, then write model-space and paper-space INSERT references.
- Preserve each INSERT's block name, insertion point, scale, rotation, and entity properties. Preserve its model/paper owner and layout tag.
- Preserve definitions needed by nested references, with deterministic handling of cycles/invalid definitions and useful REQ-201 logging.
- Import DXF definitions and INSERTs into the existing `CadBlockDefinition`/`CadBlockRef` values so a GoSurvey DXF round-trip preserves references instead of only expanding them into loose geometry. Existing external-DXF flattening behavior must be characterized and any change must remain compatible with visible geometry.
- Out of scope: new block entity types, editing/creation UX, external xref resolution, and changes to DWG or GS formats.

## 3. Architectural boundary check

- A new file format or abstraction is not required. The app already owns block definitions and references; DXF uses standard BLOCK/ENDBLK records and INSERT entities.
- No dependency, layer, public API, or ownership change is planned. If existing block values cannot represent a standard DXF requirement from the issue, stop and record a SPEC GAP rather than expanding the model silently.

## 4. Plan

- Inspect current DXF section assembly, handle allocation, model INSERT writer/importer, block definition representation, paper-layout writer, and existing block/DXF tests.
- Determine a shared handle/count pass that accounts for referenced block definitions, nested definitions, block-entity content, model INSERTs, and each layout's INSERTs before writing HANDSEED.
- Emit BLOCK records in the BLOCKS section using their existing names/base points and supported primitive contents; emit ENDBLK and required owner/record links.
- Emit INSERT entities in model space and each paper layout with existing transforms and common attributes. Ensure paper owner/group 67/group 410 reflect the target layout.
- Extend the native DXF parser only where exported standard BLOCK/INSERT data currently fails to reconstruct. Preserve clear skip logs for unsupported nested content.
- Add tests for model and paper references, two layouts, transforms, attributes, nested definitions, missing/cyclic definitions, and serialize/import equivalence. Add an AutoCAD accoreconsole oracle fixture and AUDIT when available.

## 5. Risks and edge cases

- DXF handle allocation and HANDSEED must include all new entities and object records without collisions.
- Block names may be duplicated with case differences; preserve existing app identity/merge rules and use deterministic DXF lookup.
- Circular nested references must not recurse forever; malformed references must be logged and skipped rather than emitted dangling.
- Attributes that inherit `ByBlock`/`ByLayer` must keep their existing meanings.
- Paper-space INSERTs must remain owned by the correct layout's block record and include group 67=1 and group 410=layout name.

## 6. Verification approach

- Focused Catch2 DXF import/export tests for every issue acceptance criterion.
- Run the hidden oracle fixture writer and AutoCAD 2025 accoreconsole AUDIT; verify no dangling references or repair errors. Where feasible, DXFOUT and inspect/re-import the result.
- Build Release, run focused DXF/block tests, relevant headless transcripts, and `git diff --check`.
- Review architecture, correctness, performance, dependency, and failure-path behavior before marking ready for the combined PR.

## 7. Steps

- [x] Review issue #780, REQ-113, current block model, and DXF read/write paths.
- [x] Write and review the plan before implementation; Verification approved with required edge/reporting coverage.
- [x] Implement block definitions and model/paper INSERT references.
- [x] Add focused tests and AutoCAD oracle evidence.
- [x] Run build, focused verification, and record acceptance results.

## 8. Implementation log

- 2026-11-04: User directed continuation of the paper-space issue batch. #1016 audit/DXF round-trip work is recorded separately. #780 is the next actionable accepted-REQ issue; #1000/#1001 are blocked by #999/#121, and #810 remains needs-triage.
- 2026-11-04: Issue review confirms #780 acceptance covers BLOCK definitions plus INSERT references in model space and each paper layout; no unresolved scope choice. REQ-113 is the authority. Existing writer explicitly logs paper-space references as skipped because no block definitions are written.
- 2026-11-04: Export now writes reachable BLOCK definitions with supported primitive members, followed by model-space, per-layout paper-space and supported nested INSERTs. INSERT names, XYZ, XYZ scale, Z rotation, entity properties, space ownership and layout tags are retained. Native DXF re-import reconstructs definitions and model/paper references.
- 2026-11-04: Nested traversal is cycle-safe and bounded at 16 levels; missing definitions, cyclic/deep references, non-finite transforms and unsupported block members are logged. Invalid top-level model/paper INSERTs are skipped and reported. A cyclic edge is omitted while the reachable non-cyclic definitions remain.
- 2026-11-04: AutoCAD Core Console exposed a paper BLOCK_RECORD/LAYOUT association displaced by the new definition records. The first paper layout link is now emitted with its own record. Focused export/import tests and a two-layout AutoCAD audit confirm that no block or layout audit errors are introduced.
- 2026-11-04: Release build passed. Focused tests passed: `[issue780]` — 39 assertions in 2 cases; `[issue765]` — 84 assertions in 5 cases. AutoCAD 2025 Core Console AUDIT returned exit 0 for the block fixture and the no-block baseline; both report `Total errors found 1 fixed 1` for the same pre-existing `$DIMTFAC` header warning, and neither reports a block/layout error. Raw console outputs are `build/artifacts/issue780/issue780-blocks.accore.out.txt` and `issue780-baseline.accore.out.txt`; the comparison is recorded in `audit-summary.txt`.
- 2026-11-04: No product UI changed. The combined public manual update and pre-PR documentation review are recorded in `TASK-paper-space-issue-batch-pr.md`.
- 2026-11-04: Manual architecture/code/dependency/performance review found no new dependency or ownership boundary change. DXF ATTDEF/ATTRIB instance values are not modeled by the native DXF codec and remain outside issue #780's stated acceptance; they were not synthesized or transformed by guesswork.
- 2026-11-04: Follow-up PR review found that block import decoded SPLINE into scratch state but dropped it without a warning. `CadBlockContent` has no spline store, so import now reports each such member as unsupported; regression coverage injects a valid block SPLINE and checks the warning. The DXF block writer also now skips and counts circles with a non-flat `circleNormals` value instead of flattening them; regression coverage checks both the loss count and absence of a written CIRCLE.
- 2026-11-04: Follow-up review challenged VIEWPORT status bit 2048 for hide-plot. Checked Autodesk Help, but its current help shell did not expose the reference content; the installed AutoCAD Core Console is available but does not expose the flag's semantics through a command query. Independent DXF codec evidence from ezdxf's `viewport.py`/`const.py` names `0x800` `VSF_HIDE_PLOT_MODE`; the existing DXF/DWG mapping is consistent with this, so it is retained. Existing REQ-482 round-trip checks cover the mapping.
- 2026-11-04: PR review identified lost nested INSERT attributes and X/Y rotations. `CadBlockNested` now stores and persists entity attributes; DXF and DWG import/export retain nested-reference attributes. DXF INSERTs now encode the complete orientation as extrusion plus OCS rotation and reconstruct the GoSurvey Euler convention on import. Paper/nested INSERT output removes the generic +Z extrusion before writing its actual normal. Explicit paper/nested true colors use DXF group 420 instead of being reduced to the nearest ACI color. Added DXF round-trip checks for model, paper, and nested 3D rotations, nested layer/linetype/color, and JSON persistence.
- 2026-11-04: Verification after the PR review fix: `build.bat` PASS; `[issue780]` PASS (57 assertions in 2 cases); `[issue765]` PASS (82 assertions in 5 cases); `git diff --check` PASS. A first focused run exposed duplicate generic extrusion output and a mismatch between the block transform Euler order and a general matrix decomposition; both were corrected before the passing run.
- 2026-11-04: Follow-up verification passed: `build.bat`; `[issue780]` (50 assertions in 2 cases); `[issue765]` (82 assertions in 5 cases); unit CTest shard 0 (1379 cases); unit CTest shard 1 (passed, 1379-case shard). `git diff --check` passed. One initial shard-0 attempt exposed a misplaced test assertion in an unrelated test; after correcting the assertion location, the shard passed.

## 9. Completion record

**Ready for review in the combined PR.** Keep #780 open until that PR merges.

- Acceptance: definitions and valid INSERT references are emitted for model space and every paper layout; nested references are emitted where safe; paper references carry their layout owner and tags; GoSurvey DXF round-trip restores supported definitions, spaces, transforms and entity properties; supported references are not logged as skipped.
- Verification: Release build PASS; focused `issue780` and `issue765` tests PASS; AutoCAD 2025 AUDIT shows no new block/layout errors relative to the no-block baseline (both files have the same unrelated `$DIMTFAC` warning, repaired by AUDIT).
- Remaining limitation: GoSurvey's native DXF codec does not represent dynamic ATTDEF/ATTRIB instance values. No values are inferred; broader attribute round-trip is outside issue #780's current acceptance and should be handled as a separate requirement if needed.
- Build output: `build/artifacts/issue780/` (DXF fixtures, Core Console logs/output, and audit summary).
