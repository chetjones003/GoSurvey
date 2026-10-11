# TASK-268 — Connection-point-driven candidate filtering (issue #486, user follow-up)

## Authority
- User request, 2026-09-17: "we need to hook up the connection point logic. if i have a connection
  point on a flange set to pipe end then use that point to snap to pipe end, if it is set to flange
  face to snap to. basically we need to detect what we are snapping to and use that blocks
  connection point logic."
- REQ-345 (spec/requirements.md), amended again.
- Builds on the existing issue #496 smart-mode system (`CadBlockConnectionMode`,
  `CadConnectionModeTarget`, `CadBlockResolveMode`) — this is a gap in how that system gets
  invoked, not a new system.

## Problem
`SubmitInsertBlockConnectorPick` already resolved a `CadBlockConnectionMode` from the target kind
(PipeEnd/FlangeFace/GenericPort) it snapped to, but only AFTER choosing which candidate to snap to
— and that choice was distance-only. A connection point configured to mate only with a pipe end
could still snap onto a closer, incompatible flange face (or vice versa), because nothing ever
asked "does this source port's own configured mode(s) actually accept this kind of target."

## Files affected
- `src/util/cadblock.hpp` — new `CadBlockConnectionAcceptsTarget(conn, target)`, beside
  `CadBlockResolveMode` it wraps.
- `src/commands/CadBlocks.cpp` — `SubmitInsertBlockConnectorPick` restructured: `src` (the
  connection point being placed) is resolved first; both the block-port and pipe-end searches are
  filtered through `CadBlockConnectionAcceptsTarget` before the nearer-wins comparison; the
  refusal message distinguishes "nothing nearby at all" from "something nearby, but none of it
  compatible with this port's configured mode(s)."
- `tests/CadBlockImportTests.cpp` — 2 new cases.
- `spec/requirements.md` — REQ-345 amended.

## Implementation approach
`CadBlockConnectionAcceptsTarget` reuses `CadBlockResolveMode` itself as the compatibility test —
a legacy (mode-less) connection point accepts everything, unchanged; a multi-mode point accepts a
target only when an exact-match or `isDefault`-flagged mode exists for it. This is the SAME rule
that already decided which mode applies once a target was chosen; the only change is applying it
BEFORE the target is chosen, so an incompatible candidate is never a candidate at all, however
close.

## Test approach
Two new `CadBlockImportTests.cpp` cases, both driving the real `SubmitInsertBlockConnectorPick`
path: (1) a pipe-end-only-configured port correctly ignores a geometrically CLOSER but
incompatible generic block port, landing on the farther but compatible pipe end instead — the
scenario the user described directly; (2) a flange-face-only-configured port refuses outright
when only an incompatible pipe end is nearby, rather than snapping to it anyway.

## Architectural-boundary check
No new architecture layer — closes a gap in how the existing issue #496 smart-mode system gets
invoked. No ADR needed.

## Verification
- Build: `./dev/build` — clean, no new warnings.
- Tests: `GoSurveySnapTests.exe [issue486][block]` 9/9.
- Full suite: `GoSurveyTests.exe` 1169/1169. `GoSurveySnapTests.exe` 319/319 (up from 317).

## Result
PASS.
