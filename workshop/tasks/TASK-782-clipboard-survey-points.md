# TASK — Carry survey points through Copy/Paste (issue #782)

- Branch: `feat/implement-ticket-782`
- Authority: accepted REQ-383 clauses 1, 2, and 7; accepted D-2026-11-02-a (survey points for all drawing pairs) and D-2026-11-02-b (defined coordinate-system mismatches use Convert / Cancel for all drawing pairs); issue #782.
- Goal: project data remains safe when a paste carries selected survey points into another drawing.
- Constraints: REQ-201 (cancel leaves the drawing unchanged); REQ-038 / ADR-013 (model and paper stores remain distinct); REQ-076 (a pasted copy gets a fresh entity identity, including a fresh point-label link).
- Scope: selected model-space survey points copy with drawing geometry between standalone drawings, within one project, and across projects. Points join the destination point store, including the shared DB for a shared-project drawing. Defined coordinate-system mismatches also use the existing Convert / Cancel flow for every drawing pair; a missing system is not compared. Paper-space paste does not place survey points.
- Files/subsystems: `CadClipboard` and copy/commit flows in `CadCommands`; destination point-conflict handling and paste checks in `ProjectPoints` / `ProjectWarnings` and their existing Qt questions; requirement and decision records; focused domain and Qt tests.
- Implementation approach: capture selected points in the clipboard, include their coordinates in the paste anchor, translate them with the chosen paste offset, and stage the paste until point-number conflicts have an explicit Overwrite/Renumber/Cancel answer. Apply the existing coordinate-system mismatch check before the standalone-pair early return so its Convert / Cancel flow covers all pairs. Reuse existing conversion math, point database synchronization and dialogs; preserve clipboard data on cancellation. Do not introduce a parallel point store or new dependency.
- Test approach: focused tests for point-only and mixed clipboard copy, translated and original paste, standalone and shared-project destinations, number-conflict choices (including cancel), fresh labels, defined-zone mismatch coverage for standalone pairs (including conversion available and missing-zone no-op), and geometry/point transaction behavior; run relevant domain and Qt targets, then the full release build/test workflow.
- Architectural-boundary check: clipboard point storage belongs with the existing in-process `CadClipboard`; coordinate/ID changes belong in survey-point domain helpers; Qt remains responsible only for presenting the existing conflict decision. Verify paper-space storage is untouched.
- Plan verification: matches the user-approved pairing policy and the existing project-owned point database model (ADR-065); applies the REQ-201 cancel invariant and ADR-013 space boundary. No unresolved scope or architecture question identified.

## Progress

- [x] Plan recorded and checked against SPEC/architecture; user clarified that defined coordinate-system mismatches use Convert / Cancel for all drawing pairs.
- [x] Implementation and focused regression coverage; copy/paste and conversion tests cover point-only and mixed selections, conflict choices, shared project storage, and point attributes.
- [x] Release build and full release suite passed (191/191); focused Qt conflict-dialog test passed and saved its screenshot under `build/artifacts/`.
- [x] Coordinate-system pairing policy recorded in SPEC; standalone mismatch is blocked until Convert / Cancel, with defined, undefined and PASTEORIG cases covered.
- [x] Rebuilt release, passed focused REQ-383 tests (239 assertions / 30 cases), focused hidden Qt prompt test, and full release suite (191/191); captured both paste-warning screenshots.
- [x] `git diff --check` passed; final diff manually checked. Awaiting user approval of the updated Qt screenshot visualizer before commit.
