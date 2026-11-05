# Verification: TASK-780 DXF blocks and INSERTs — implementation review

Date: 2026-11-04

## Verdict

**PASS for issue #780's stated acceptance criteria.** Keep issue #780 open until the combined PR merges.

## Requirement and architecture

- The implementation serializes existing `CadBlockDefinition`, model `CadBlockRef`, and per-layout paper `CadBlockRef` stores through the existing DXF writer. No new dependency, data owner, file format, or UI path was introduced.
- Reachable definitions are emitted once. Nested references are emitted only when their definition is reachable and the edge does not form a cycle or exceed the 16-level traversal bound.
- Model and paper INSERTs preserve name, insertion point, scale, Z rotation and entity attributes. Paper INSERTs use the target layout's block record and emit paper-space group 67/410 metadata.
- Invalid/missing top-level references and missing/cyclic/deep nested references are skipped with REQ-201 diagnostics; unsupported block members are counted. Finite checks prevent NaN/Inf values from entering DXF records.
- DXF dynamic `ATTDEF`/`ATTRIB` instance values are not represented by the native DXF codec. They remain an explicit limitation outside this issue's stated acceptance; no values are synthesized.

## Test evidence

- `.\build.bat`: PASS (Release).
- `GoSurveyTests.exe "[issue780]"`: PASS, 39 assertions in 2 cases. Covers model and two-layout paper references, entity layers, transform round-trip, nested reference round-trip, and missing/cyclic/invalid references.
- `GoSurveyTests.exe "[issue765]"`: PASS, 84 assertions in 5 cases.
- AutoCAD 2025 Core Console `AUDIT`: block fixture and no-block baseline both exited 0 and reported `Total errors found 1 fixed 1` for the same `$DIMTFAC` header warning. The block fixture reported no BLOCK_RECORD/LAYOUT association error after the writer fix. Full evidence: `build/artifacts/issue780/audit-summary.txt`.
- `git diff --check`: PASS.

## Documentation and dependencies

- No product-facing UI or workflow changed; no README, privacy, in-app wiki, or public manual update is needed for this issue. Record the full four-source documentation review before opening the combined PR.
- No new dependencies.

## Residual follow-up

If dynamic block attribute values need round-trip support, add explicit DXF `ATTDEF`/`ATTRIB` requirements and tests in a separate increment. Do not treat current omission as supported serialization.
