# TASK-266 — CadPipeRun "pipe end" connection port (issue #486, user follow-up)

## Authority
- User request, 2026-09-17, on top of TASK-263/264/265 (B1, B2, BEDIT fix + auto-fillet).
- REQ-345 (spec/requirements.md), amended again.

## Problem
`CadConnectionModeTarget::PipeEnd` already existed (issue #496) as a classification a fitting's own
connection port could resolve a `CadBlockConnectionMode` against, but it was only ever assigned for
a bare LINE/polyline endpoint (`FindNearestPipeEndpoint` in CadBlocks.cpp, scanning raw
`st.userLinesFlat`) — its own doc comment explained this as "there is no separate pipe entity in
this codebase." That stopped being true once `CadPipeRun` shipped (B1/B2) but nobody had revisited
it. The user asked for a real pipe run's ends to expose this connection point properly: the centre
of the pipe's end face, with an outward normal.

## Files affected
- `src/util/cadpiperun.hpp` — refactored `CadBuildPipeRunSweptSolid` to split out
  `CadBuildPipeRunSweepPath` (the point/tangent marching, no `Sweep` call), so both the render path
  and the new connection query share one geometry computation, never two independently derived
  answers. New: `CadPipeRunEndPort` struct, `CadPipeRunEndPorts` function.
- `src/commands/CadBlocks.cpp` — `FindNearestPipeEndpoint` extended to also scan `st.cadPipeRuns`
  via `CadPipeRunEndPorts`, feeding both ends into the same nearest-candidate search a bare line's
  endpoints already go through. No changes needed to `SubmitInsertBlockConnectorPick` itself — it
  already treats "found a pipe endpoint" uniformly regardless of source.
- `tests/CadPipeRunTests.cpp` — 3 new cases (straight-run port position/normals, bent-run END port
  reading the post-fillet actual exit tangent, refusal parity with the swept solid).
- `tests/CadBlockImportTests.cpp` — 1 new end-to-end case: `INSERT` with connector-snap reaching a
  `CadPipeRun`'s end through the real `SubmitInsertBlockConnectorPick` path.
- `spec/requirements.md` — REQ-345 amended.

## Implementation approach
Rather than have the connection-port query re-derive the pipe's end position/tangent independently
(risking drift from what `CadBuildPipeRunSweptSolid` actually renders, especially after
auto-fillet's angle-snapping can move an end off its raw clicked vertex), the marching algorithm
was split so both consumers call the same `CadBuildPipeRunSweepPath`. The port's outward normal
sign convention (pointing away from the pipe) matches `FindNearestPipeEndpoint`'s existing
bare-line convention exactly, so a fitting orients identically whether it connects to a plain LINE
end or a real `CadPipeRun` end — no special-casing needed downstream of `FindNearestPipeEndpoint`.

## Test approach
Unit tests on `CadPipeRunEndPorts` directly (position, outward-normal sign and unit length, and
that a bent run's end reads the ACTUAL post-fillet tangent rather than the original clicked
vertex), plus one full end-to-end integration test driving the real `INSERT` connector-snap command
path (`StartInsertBlockCommand` → `SubmitInsertBlockConnectorPick`) against a `CadPipeRun`,
confirming both the log message and the placed fitting's actual world connection point.

## Architectural-boundary check
No new architecture layer — extends the existing #496 connection-mode-target classification to a
data source it was never wired to, using its own established outward-normal convention. No ADR
needed.

## Verification
- Build: `./dev/build` — clean, no new warnings.
- Tests: `GoSurveyTests.exe [port]` 3/3; `GoSurveySnapTests.exe [piperun]` 13/13 (includes the new
  INSERT integration case).
- Full suite: `GoSurveyTests.exe` 1169/1169 (up from 1166). `GoSurveySnapTests.exe` 312/312 (up
  from 311).

## Result
PASS.
