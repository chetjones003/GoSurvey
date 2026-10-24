# Qt entity identity sweep — issue #1080

## Bug report and authority

Creating a surface with SURFACECREATE builds its TIN but leaves it invisible until
another undoable command runs. Reproduce with the Ground point group and a newly
created surface; it must display on the next viewport paint without a second command.
Authority: accepted REQ-076 (stable identity assigned at creation), REQ-070
(surface component display), ADR-027, and the existing revision-gated identity sweep.

## Diagnosis

PushUndoSnapshot sweeps before creation. The new surface attributes then retain id
zero. RefreshSurfaceDisplayGeometry skips zero ids, and Qt paintGL omits the sweep
that the former frontend performed each frame. A later undo snapshot assigns the id
and makes the same surface visible (manual reproduction recorded in #1080).

## Plan and Verification review

- Add a Qt regression using the real SURFACECREATE path and a rendered viewport.
  Assert identity, nonempty display batches, stable identity/cache on repeated paints.
- First run it unchanged and record failure; capture the viewport before assertions.
- Call EnsureEntityIds at the beginning of paintGL, before identity consumers.
- Build all Release targets, run the regression and complete CTest suite.
- Capture the affected viewport on the hidden desktop for user visual approval.

Plan verdict: PASS. This reconnects the existing domain API in the Qt adapter; no
new architecture, data format, ownership, dependency, or requirement. Unchanged
frames take EnsureEntityIds' existing constant-time revision check. Regression risk:
identity must remain stable across repeated rendering; cached surfaces must not rebuild.

## Implementation and validation

- `src/qt/EditorViewport.cpp`: invoke the existing revision-gated sweep before
  rendering consumes entity identities. No domain behavior or serialization change.
- `tests/QtEditorTests.cpp`: create a built surface through the actual command,
  paint without another command, check display batches and stable ids/cache across
  three additional paints. Capture before-create and after-create viewport states.
  Also notify geometry change in the isolation fixture after its direct array edit:
  it adds lines after window.show(), which now sweeps the previously empty geometry.
  Without BumpCadGpuCache the fixture incorrectly claims geometry did not change.
- Regression RED: 2 passed, 1 failed; `cadSurfaceAttrs[0].id != 0` failed on the
  unchanged production code (`build/artifacts/qt/issue1080-red.log`).
- Regression GREEN: 3 passed, 0 failed (`issue1080-green.log`).
- Full Release build: PASS, no compiler warnings in `issue1080-build.log`.
  The initial attempt hit a locked application executable; closing this checkout's
  recording instance resolved it. The other worktree's app was left running.
- Architecture/code review: PASS. Existing Qt-to-domain dependency, no new API,
  ownership, persistence, abstraction, or dependency. `git diff --check` passes.
- Dependency audit: not applicable; no manifests/dependencies changed.
- Performance review: unchanged frames return at the existing revision check;
  regression measures zero additional surface regenerations and zero additional
  id allocations across three repeated paints. No timing/speedup claim is made.

- Full CTest run: 190/191 entries passed; the sole failure was the isolation fixture
  above, also reproduced alone (`issue1080-isolation.log`). Both domain shards
  passed (2,556 cases). Build layout, deployment, parity and all headless tests passed.
  Final Release rebuild passes after correcting the fixture; the affected Qt shard
  rerun passes, 1/1 CTest entries (`issue1080-ctest-rerun.log`, 50.33 seconds).
  Testing verdict: PASS across the full run plus affected-shard rerun. No remaining failures.

User approved the before/after viewport captures on 2026-10-24 and authorized
committing, opening the PR, and resuming the live-app promo. No spec changes or new debt.
