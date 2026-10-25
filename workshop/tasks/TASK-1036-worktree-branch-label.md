# TASK-1036 — Label GoSurvey windows by worktree branch

- Type: feature
- Status: submitted
- Opened: 2026-10-23
- Owner: Codex

## 1. Authority
- Goal: no GOAL identifier is defined in `spec/project.md`; REQ-453 is the direct authority.
- Requirements: REQ-453 (accepted)
- Constraints: none additional; retain the standard per-worktree build output and `GoSurvey.exe` filename.
- Acceptance:
  1. Launching with `run.bat` from a named branch displays that exact branch name in the main application's native window title and custom title area, including after title refreshes triggered by document changes.
  2. Launching without a development branch label keeps the existing `GoSurvey` and document title behavior.
  3. `run.bat` derives the label from the Git branch in its own worktree and passes it only to the launched process; no branch-specific executable name or build-script output path is introduced.
  4. Agent build/launch documentation says to run `build.bat` and `run.bat` from the assigned worktree, preserve that checkout's standard `build/` outputs, and not close an application running from another worktree to unblock a build.
  5. The normal executable filename remains `GoSurvey.exe`; this increment adds neither the standalone manager nor a build/restart watcher.
- Owning subsystem: Application lifecycle / Qt UI / build and launch documentation.

## 2. Scope
- In scope: pass the Git branch from `run.bat` to the application; preserve it in both window title surfaces; state the worktree-local build and launch rule in `CLAUDE.md`.
- Out of scope: executable renaming, build output changes, the standalone manager, embedding, and build/restart watching.
- Smallest change: one launcher environment value, an explicit constructor value on `EditorWindow`, title composition at existing title-update sites, focused Qt tests, and a concise agent rule.

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
  - [x] No — pass a branch string explicitly from application startup into `EditorWindow`; no global state, dependency, or new layer.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Feature scope and deferred manager/watcher | 2026-10-23 | User approved as written. |

## 5. Assumptions
- Detached HEAD has no current branch name and therefore uses the ordinary title, as allowed by REQ-453.
- A direct executable launch without `run.bat` has no development label and keeps the normal title.

## 6. Plan
- Approach: `run.bat` clears any inherited label, reads `git branch --show-current` after changing to its own checkout, and sets a process-local environment variable for the child. `main.cpp` reads it and passes the value to `EditorWindow`. The existing title refresh path composes both the native title and custom title label from the same base title and label.
- Files/functions to touch: `run.bat`; `src/qt/main.cpp`; `src/qt/EditorWindow.hpp`; `src/qt/EditorWindow.cpp`; `tests/QtEditorTests.cpp`; `CLAUDE.md`; REQ-453 traceability and task log.
- Test approach: happy path = named label appears at launch; update path = it remains after drawing/title refresh; empty label = normal title is preserved. Inspect launcher behavior and documentation against two worktree-local output roots. Run applicable Qt tests and build; capture/present affected window screenshots per `CLAUDE.md`.
- Steps:
  - [x] Add focused title and branch-label coverage.
  - [x] Implement branch propagation and title composition; document worktree-local build/launch rules.
  - [x] Build, test, review, and present screenshots for user approval.

## 7. Workflow-specific notes
- Feature: user approved the scope; test-first coverage will be added before implementation.

## 8. Implementation log
- 2026-10-23: Request confirmed; SPEC and Verification completed before implementation.
- 2026-10-23: `run.bat` now passes its checkout branch to startup; both window title surfaces retain it through refresh. Added the worktree-local build/launch rule in `CLAUDE.md`.
- 2026-10-23: `build.bat` passed. All 6 registered `qt.editor.shard-*` tests passed; `build.output-layout` and `git diff --check` passed. The documented exact filter `^qt[.]editor$` matched no test because CTest currently registers six shards; reran with `^qt[.]editor[.]shard` successfully.
- 2026-10-23: Captured labeled and unlabeled title states under `build/artifacts/` and published them for user review. Awaiting visual approval before marking done.
- 2026-10-23: User requested a PR to `beta` after the visual review; proceeding with submission.

## 9. Self-verification
- [x] build-project — PASS (`.\build.bat`)
- [x] architecture-review — PASS (explicit startup value flow; no new layer, global state, dependency, or output-path change)
- [x] code-review — PASS (one branch value retained and used at both title-update points)
- [ ] dependency-audit — n/a unless dependencies change
- [ ] performance-review — n/a (no hot path)
- [x] testing — PASS (6/6 Qt editor shards, output-layout check)

## 10. Verification result
- Submitted: 2026-10-23
- Verdict: PASS — `verification/REQ-453-worktree-branch-label.md`
- Findings: none blocking.

## 11. Outcome
- Requirements satisfied: REQ-453 (implementation and automated acceptance checks met; user screenshot approval pending)
- Tests added: `req453DevelopmentBranchAppearsInBothWindowTitlesAndSurvivesRefresh`; `req453WindowTitleWithoutDevelopmentBranchStaysUnchanged`
- Refactors: none planned
- Docs updated: `CLAUDE.md`, `spec/requirements.md`, `spec/project.md`
- Done: submitted for review
