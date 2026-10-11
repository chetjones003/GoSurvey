# TASK-267 — Pipe run selection/hover/naming + real osnap fix (issue #486, user follow-up)

## Authority
- User report (2026-09-17, with screenshot): (1) pipe runs not selectable/nameable, want hover info
  matching survey point/surface hover; (2) connection port still not reachable via INSERT's
  connector-snap despite TASK-266.
- REQ-345 (spec/requirements.md), amended again.

## Root cause of "still no connection port to snap to"
TASK-266 gave `CadPipeRun` a correct `CadPipeRunEndPorts` query and wired it into
`FindNearestPipeEndpoint`, but that function only ever runs on a 3D point ALREADY resolved near the
target. The general 3D cursor-point resolution (`CadSnap::FindBest`) had no knowledge of
`CadPipeRun` at all — it offers `Kind::Endpoint` candidates for bare `userLinesFlat` lines but
never for a pipe run's swept solid — so hovering an orbited pipe surface fell back to a
work-plane-projected guess nowhere near the actual pipe. Confirmed by tracing `commitZ`'s source
(`CadUi.cpp` ~13990-14030: ray-vs-work-plane only, overridden by a snap ONLY when one fires) back to
`CadSnap.cpp`'s complete absence of `cadPipeRuns`.

## Files affected
- `src/viewport/CadSnap.cpp` — pipe run ends offered as `Kind::Endpoint` candidates, gated on the
  same `wantEndpoint` toggle a bare line's endpoints already use.
- `src/commands/CadCommands.hpp` — `SelectedEntity::Type::PipeRun = 14`; `pipeRunWorldSolidOwnerIndex`
  parallel array.
- `src/commands/CadCommands.cpp` — `PickClosestPipeRunEntity` (new) + `PickClosestSolidEntity`
  extended to try both `cadSolids` and `pipeRunWorldSolids`, nearer one wins; `ERASE` handling;
  `EnsureAttrCounts`; every `-Wswitch`-flagged exhaustive switch over the type enum given a
  `PipeRun` case (gizmo-anchor bounds, EXPLODE's per-type message, pick-depth sort key).
- `src/commands/CadEntities.hpp` — `CadPipeRun::name` field.
- `src/ui/CadUi.cpp` — `PickSolidUnderCursor`/hover and click-select gates extended to
  `pipeRunWorldSolids`; `ClickToggleSolid` generalized to match on the hit's own type;
  `SelectedEntityAttr`/`FormatPickCandidateTypeLabel` given `PipeRun` cases; new
  `DrawPipeRunRolloverReadout` wired into the survey-point/surface hover precedence chain; new
  Properties panel section (editable Name, read-only Nominal Size/Pressure Class/Length/Vertices).
- `src/viewport/TransformPreview.cpp` — selection highlight for `Type::PipeRun`.
- `src/util/cadpiperun.hpp` — `CadPipeRunLength` (true centreline length from the actual built
  path, not a naive vertex-to-vertex sum).
- `src/io/GsIo.cpp` — `name` persisted (additive).
- `tests/CadSnapTests.cpp`, `tests/CadPipeRunCommandTests.cpp`, `tests/GsIoPipeRunTests.cpp` (new).
- `spec/requirements.md` — REQ-345 amended.

## Implementation approach
Followed `Type::Solid`'s own stated boundary exactly for `Type::PipeRun` (display/select/highlight/
hover/erase, no transform) rather than inventing a new policy — a pipe run's geometry is derived
from its path the same reason a TIN surface's is derived from its definition (ADR-036 (b)), so a
direct drag has nothing to write back to either. Selection deliberately does NOT go through
`PickSubObjectAcrossSolids`/`SelectedSubObject` (the FILLET/CHAMFER/PRESSPULL sub-object system) —
a pipe run's derived solid was never meant to be edge/face-edited, only picked as a whole entity —
so a new, narrower `PickClosestPipeRunEntity` does its own ray test directly against
`pipeRunWorldSolids`, and `PickClosestSolidEntity` picks whichever of a real solid or a pipe run's
solid the ray hits nearer.

## Test approach
`CadSnapTests`: one case confirming a pipe run's end is now reachable as an ordinary `Endpoint`
candidate (the actual root-cause fix). `CadPipeRunCommandTests`: whole-entity pick via
`PickClosestSolidEntity` (with `viewportVisualStyle` set to `Shaded` — faces aren't pickable in the
default `Wireframe2D` style, D-2026-09-16-b) and `ERASE` removing a selected run. `GsIoPipeRunTests`
(new file): a named run's full round trip through `SaveGoSurveyTemplateFile`/
`LoadGoSurveyTemplateFile`, and a pipe-run-free drawing still loading cleanly.

## Architectural-boundary check
No new architecture layer — extends existing, established patterns (`Type::Solid`'s selection
boundary, `CadSnap`'s endpoint-candidate shape, the survey-point/surface hover precedence chain,
`PaperLayout`'s inline-rename precedent) to a data source each was simply never wired to. No ADR
needed.

## Verification
- Build: `./dev/build` — clean, no new warnings (every `-Wswitch` gap the `Type::PipeRun` addition
  opened was closed, confirmed by watching the warning list shrink to zero pipe-run-related entries
  across three separate rebuilds).
- Tests: `GoSurveySnapTests.exe [piperun]` 18/18 (up from 12).
- Full suite: `GoSurveyTests.exe` 1169/1169. `GoSurveySnapTests.exe` 317/317 (up from 312).

## Result
PASS.
