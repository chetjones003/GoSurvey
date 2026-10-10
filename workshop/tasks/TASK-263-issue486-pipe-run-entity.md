# TASK-263 — CadPipeRun entity (issue #486 increment B1 / REQ-345)

## Authority
- GitHub issue #486 ("Piping System"), increment B1 of Track B.
- REQ-345 (spec/requirements.md).
- Track A (A1–A5, fitting metadata/connection roles/library export/authoring/browser) already
  fully delivered — PRs #488, #489, #490, #491 — before this task started. Confirmed by `git log`
  search for "486" before planning.

## Scope
Only B1: the `CadPipeRun` entity itself — path storage, .gs persistence, and rendering as a swept
pipe solid. Explicitly NOT in scope: interactive routing command (B2), `CadPipingSystem` container
(B3), catalog lookup (B4), auto-fitting (B5/B6), manual fitting placement (B7), edit ops (B8).

## Files affected
- `src/commands/CadEntities.hpp` — new `CadPipeRun` struct (path + nominalSize + pressureClassTag).
- `src/util/cadpiperun.hpp` — new header: NPS→OD table, label parsing, `CadBuildPipeRunSolids`.
- `src/commands/CadCommands.hpp` — `cadPipeRuns`/`cadPipeRunAttrs` (persisted) +
  `pipeRunWorldSolids`/`pipeRunWorldSolidAttrs`/`pipeRunWorldSolidsSig` (derived, not persisted).
- `src/commands/CadCommands.cpp` — `RebuildPipeRunWorldSolids`, wired into
  `RefreshSolidDisplayGeometry` alongside the existing `RebuildBlockRefWorldSolids`, reusing the
  same tessellation cache and coalesced-batch assembly REQ-313/issue #194 already built.
- `src/io/GsIo.cpp` — additive `pipeRuns`/`pipeRunAttrs` .gs arrays (path + labels only, no
  version bump, ADR-020 (d) precedent).
- `tests/CadPipeRunTests.cpp` — 10 cases (NPS parsing, OD lookup, straight/bent sweeps, degenerate
  segment, invalid-size/too-few-vertices refusal).
- `CMakeLists.txt` — registers the new test file.

## Implementation approach
`CadPipeRun` stores only topology (path + labels) — never the swept solid — matching the issue's
own architectural note ("piping owns topology; blocks own geometry"), extended to pipe segments.
The solid is derived display data, rebuilt only when the path/labels actually change (a
FNV-1a signature gate, the same shape `BlockRefWorldSolidsSig`/`RebuildBlockRefWorldSolids` already
use for block-ref solids), and fed through the existing solid tessellation cache and
issue #194 draw-batch coalescing — no second render path.

NPS→OD conversion is a small closed table of standard sizes (0.5in–12in) rather than an
interpolated or parametric formula: an unlisted size is a SPEC GAP for the future catalog work
(B4), not something to guess.

## Test approach
Header-only unit tests (Catch2, `tests/CadPipeRunTests.cpp`) cover the pure logic without GL:
label parsing edge cases, OD lookup hits/misses, straight and bent run sweeps validated against
`brep::Validate`/`brep::ComputeMassProperties`, and the two "produces nothing" refusal paths
(unresolvable size, <2 vertices). Full suite run after (`GoSurveyTests.exe`, `GoSurveySnapTests.exe`)
with no regressions.

## Architectural-boundary check
No new architecture layer. Follows the `blockRefWorldSolids` derived-array precedent exactly, so
no ADR is needed for this increment — the harder ADR question (linked vs. materialized solid
across an *editable* run) belongs with B2 once there is an editing command to make it for.

## Verification
- Build: `./dev/build` — clean, no new warnings.
- Tests: `GoSurveyTests.exe [issue486]` — 10/10 passing, 44 assertions.
- Full suite: `GoSurveyTests.exe` — 1160/1160 passing. `GoSurveySnapTests.exe` — 299/299 passing.

## Result
PASS.
