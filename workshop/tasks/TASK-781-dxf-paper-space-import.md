# TASK-781 — Import DXF paper-space entities into layouts (issue #781)

- Type: feature / verification completion
- Status: needs review for combined PR
- GitHub issue: #781
- Combined PR: continue on `chore/triage-paper-space-issues`; preserve all other issue work.

## 1. Authority

- Requirement: accepted REQ-113 (DXF import reconstructs paper entities into the matching layout), REQ-201 for named unsupported entities, and REQ-001 for malformed entities.
- Issue acceptance: read LINE, CIRCLE, ARC, ELLIPSE, polyline, TEXT, MTEXT and VIEWPORT into the named layout; round-trip supported geometry to the same layout; report unsupported paper entity types.
- Owner: DXF IO (`DxfIo.cpp`) and its Catch2 coverage.

## 2. Existing implementation and gap

- `ImportDxfPaperSpace` already reads layout records and paper entities from ENTITIES and `*Paper_Space` blocks, then routes supported records into `PaperLayout` stores.
- Existing DXF tests cover export tagging and a broader #768 layout/viewport case, but no focused test exercises the #781 entity set and geometry on multiple layouts.
- #780 adds true block references and definitions. At the time this plan was recorded, its second-layout INSERT round-trip still failed; TASK-780 has since fixed it and the current focused #780 test verifies both paper layouts. Keep block-definition export concerns tracked under #780 while testing #781's listed paper entity set.

## 3. Plan

1. Confirm the accepted REQ-113/REQ-201 behavior and existing parser routes; do not invent layout fallback rules beyond the current `410`/LAYOUT mapping.
2. Add a focused multi-layout DXF export/import regression covering each #781 entity type, with representative geometry and per-layout membership assertions.
3. Exercise a paper-space entity outside the supported set and confirm the importer names its type in the loss log; exercise malformed input only where the parser has a deterministic refusal contract.
4. Build and run focused DXF/paper-space tests; check `git diff --check` and record results and any unresolved limitation.
5. Keep #781 open as needs-review for the combined PR; do not close an unmerged issue.

## 4. Architectural boundary

The importer fills existing `PaperLayout` entity stores. No new data owner, DXF abstraction, dependency, or UI change is needed.

## 5. Risks

- ENTITIES and paper blocks may duplicate records; preserve one copy per source entity.
- DXF layout order may differ from layout names; verify membership by name, not vector order alone.
- Do not turn block-reference loss into a second #781 scope item; that is tracked by #780.

## 6. Progress

- [x] Review issue, REQ-113/REQ-201, current parser, and existing tests.
- [x] Record plan and Verification plan review.
- [x] Add focused two-layout import regression for LINE, CIRCLE, ARC, ELLIPSE, LWPOLYLINE, TEXT, MTEXT and VIEWPORT, plus a named unsupported-type case.
- [x] Aggregate and report unsupported paper entity types by DXF record type.
- [x] Release build and focused Catch2 verification.

## 7. Completion record

### Implementation and verification — 2026-11-04

- `ImportDxfPaperSpace` now aggregates unsupported entity counts by record type and reports each as a paper-space skip; malformed paper ARC/CIRCLE extrusions also receive a specific refusal message.
- Added `DXF import restores paper entity types into the matching layouts (issue #781)` in `tests/LibreDwgCadTests.cpp`. It exports/imports two named layouts with all eight issue-listed entity types and checks representative geometry, text kind/content, viewport state, and layout membership. It injects an unsupported `3DSOLID` record and checks the named skip log.
- `.\build.bat`: PASS (Release build).
- `GoSurveyTests.exe '[issue781]'`: PASS (43 assertions, 1 test case).
- `git diff --check`: PASS.
- No user-visible UI change or issue-specific product-doc change was needed for this IO-only update. The combined PR documentation review is recorded in `TASK-paper-space-issue-batch-pr.md`.
- 2026-11-04 re-review after TASK-780: `GoSurveyTests.exe '[issue781]'` passed again (43 assertions, 1 test case); the stale #780 failure note is resolved. #781 remains ready for the combined PR review.
