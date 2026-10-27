# TASK-1079 — Open DWG in its own Qt tab

- Type: bug fix
- Status: implemented; verification recorded
- Opened: 2026-10-26
- GitHub issue: #1079

## 1. Authority

- Requirement: REQ-052 (open a valid DWG and show its geometry) and REQ-055 (File > Open focuses the opened drawing tab).
- Relevant acceptance: a supported DWG opens and is visible in model space; the newly opened drawing is the active tab; a failed open leaves the existing drawing untouched.
- Constraints: architecture layering and the existing per-tab snapshot switching behavior.

## 2. Root cause and evidence

`EditorWindow::OpenDrawing` calls `NewDrawing(uid)`. For a drawing with no project, `NewDrawing` displays the new-drawing chooser and returns without adding a tab when cancelled. `OpenDrawing` then treats whichever tab remained active as its destination, and its failure cleanup unconditionally pops the final tab/document. `CreateDrawingFromTemplate(projectUid)` is the existing direct tab-creation path and snapshots/restores the active document while switching.

## 3. Plan

- In `EditorWindow::OpenDrawing`, create the destination directly with `CreateDrawingFromTemplate(uid)`.
- Retain project discovery/association and the current previous-active-document restoration on failed loads, ensuring cleanup targets only the tab created by this call.
- Add focused Qt coverage for a no-project DWG open and a failed open while another drawing is active.
- Architectural-boundary check: UI-level open flow remains in `EditorWindow`; no new abstraction or dependency.

## 4. Verification plan

- Run the focused Qt editor tests, then `.\build.bat` and the required Qt editor suite as mandated by the repository.
- Self-review against REQ-052/055, architecture, code quality and failure behavior.

## 5. Results

- `OpenDrawing` calls `CreateDrawingFromTemplate(uid)` directly. Project discovery and tab project association remain unchanged; that creation path snapshots the previous active drawing before switching.
- Failure cleanup removes the new trailing tab/document and restores the previous document and active indices. The missing-file regression asserts the existing tab count, active tab, path and line data are preserved.
- Qt coverage: opening `samples/surface-demo.dwg` with no project creates and focuses a new tab, records the imported path, and does not show the template chooser; failed open preserves the prior document.
- Build: `.\build.bat` — PASS (Release).
- Qt tests: all six `qt.editor.shard-*` tests passed across the complete run and focused reruns. Shards 0–3 passed in the initial run; shard 4 passed after its assertion was corrected to use the initial tab count; shard 5 passed with failure-state assertions.
- Architecture / code review: PASS — existing EditorWindow responsibility and snapshot-aware API reused; no new dependencies, abstractions or state.
- `git diff --check` — PASS.
- Beta conflict resolution: merged current `origin/beta` (41f8476c) into the court branch; retained the beta startup regression test alongside both DWG tests.
- Post-merge verification: `build.bat` — PASS (Release); `ctest --preset ninja-release` — PASS (191/191, including all six Qt editor shards and build output layout); `git diff --check` — PASS.
