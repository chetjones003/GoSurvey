# TASK-913 — Qt traverse import merge and Trimble Access JobXML

- Type: feature
- Status: verify
- Opened: 2026-10-19
- Owner: Workshop
- GitHub: #913, child of #878

## 1. Authority

- Goal: Qt migration to beta 0.7.4 parity.
- Requirement: REQ-431 (accepted by D-2026-10-19-c), with existing traverse/import requirements REQ-015, REQ-016, REQ-201, REQ-403, REQ-417–REQ-423 as behavior authority.
- Constraints: REQ-300 (no new dependency), REQ-301 (no abstraction without two current uses).
- Acceptance: See REQ-431.
- Owning subsystem: Qt Traverse Editor over existing shared `src/traverse/` importers and traverse services.

## 2. Scope

- Complete Qt multi-file raw-data import merging (FBK, STAR*NET DAT, RW5, JobXML) and Trimble Access JobXML workflows, matching beta 0.7.4 behavior. First import into an empty traverse initializes it; subsequent imports append with station ids remapped. Preserve order and per-file reporting.
- Preserve cancel, validation, failure, undo, and read-only semantics wherever applicable.
- Add focused Qt workflow coverage and update `docs/qt-074-parity-ledger.md`.
- Provide 2–3 integrated screenshot options for each major affected surface; apply the selected direction and capture final evidence for user review.
- No importer/parser duplication, ImGui changes, new dependency, or unsupported workflow assumptions.

## 3. Expected files and architectural boundary

- Inspect and likely touch `src/qt/AdjustmentWindow.*`, relevant Qt dialogs/ribbon routes and `tests/QtEditorTests.cpp`.
- Reuse `FbkImport`, `Rw5Import`, `StarNetImport`, `JobXmlImport`, existing traverse merge/adjustment services, and Qt editor patterns.
- Update `spec/requirements.md`, `spec/project.md`, `docs/qt-074-parity-ledger.md`, and this record.
- Plan check: Issue scope aligns with the existing shared importers and traverse requirements. UI behavior stays in Qt; the append/renumber helper now lives in the shared traverse domain and is used by both frontend workflows, with no new dependency or extra abstraction.

## 4. Plan

- [x] Read repository governance, issue #913, related traverse requirements, and neighboring Qt parity tasks.
- [x] Record D-2026-10-19-c and accept REQ-431 based on the user's “Full Qt parity” decision.
- [x] Verify the plan against beta 0.7.4 behavior, existing importers, architecture boundaries, and the Qt implementation; identify the major screenshot surfaces/states.
- [x] Present 2–3 integrated visual options for the Adjustment window's import and merged-traverse review surface; record the user's selection before implementing visible UI changes.
- [x] Implement the complete Qt workflows using the shared traverse merge service and existing importers.
- [x] Add focused Qt coverage for completion and relevant cancel, validation, failure, undo, and read-only paths.
- [x] Update the parity ledger with routes, coverage, and remaining gaps.
- [x] Build both frontends and run the focused Qt and traverse tests; capture final affected surfaces and meaningful states under `build/artifacts/`.
- [x] Obtain user approval of final screenshots before marking the task complete.

## 5. Implementation log

- 2026-10-19 — User decision: “Full Qt parity”; REQ-431 and D-2026-10-19-c recorded.
- 2026-10-19 — Plan verification: `src/ui/CadUi_TraverseEditor.cpp` is the beta behavior source for ordered multi-file import, append/renumber merge, per-file reporting, import markers and imported-file records. Existing importers are shared. `src/qt/AdjustmentWindow.cpp` imported one file by replacing its working traverse and had no multi-file workflow. The merge algorithm is now shared in `src/traverse/TraverseCalc.*`, used by Qt and ImGui; this keeps the domain operation below both UI layers.
- 2026-10-19 — Presented three integrated layout options for the Qt Adjustment window's import and merged-traverse review: A (familiar left Files panel), B (navy title/hierarchy), C (compact batch-first layout). User selected A.
- 2026-10-19 — Added the left Files panel, multi-select raw import, per-file results, JobXML import, and shared station remapping. Added `issue913RawImportsMergeAndReportPerFileFailures` and `issue913JobXmlImportsThroughQtAndCancelKeepsTheSession`. Captures are `qt-913-files-success.png`, `qt-913-files-error.png`, and `qt-913-jobxml-import.png` under `build/artifacts/Release/`.
- 2026-10-19 — Verification: `build.bat` passed; focused Qt tests passed (2 tests, 4 QtTest totals including setup/cleanup); Catch2 `[traverse]` passed (26 cases, 158 assertions). The full Qt CTest `qt.editor` fails before reaching issue 913 in `approvedTopographyStaysInPanelBackgrounds`, crashing in `QWidget::grab` on the hidden desktop; the new focused tests pass through the same runner.
