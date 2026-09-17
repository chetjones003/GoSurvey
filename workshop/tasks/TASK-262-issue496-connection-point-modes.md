# TASK-262 — Issue #496: smart multi-mode fitting connection points

## Authority
- GitHub issue #496 (follow-on to #486). No formal spec/requirements.md REQ exists for the
  #486/#496 fitting-library line of work; this codebase tracks it by issue number and inline
  design-decision comments, consistent with prior increments (see cadblock.hpp comments citing
  "issue #486 increment A2", "D-2026-09-12 decision 2", etc.).
- Scope decision recorded 2026-09-16 (user chose "Fitting-only classification" over extending the
  general CadSnap::Kind system): connection-point mode matching is a fitting/connector-local
  concept, not part of the general viewport object-snap system.

## Target classification (scope note)
Only two kinds of snap target exist in this codebase today: another block's connection port
(`CadBlockWorldConnection`, via `FindNearestDrawingConnector`), and a bare line/polyline endpoint
(no existing "pipe" entity — pipes are just lines/polylines). Modes therefore classify against:
- `PipeEnd` — nearest line/polyline endpoint (new: `FindNearestPipeEndpoint`)
- `FlangeFace` — nearest block connection port whose owning definition has `partType == Flange`
- `GenericPort` — nearest block connection port on any other fitting/block

## Files affected
- `src/util/cadblock.hpp` — `CadConnectionModeTarget` enum, `CadBlockConnectionMode` struct,
  `CadBlockConnection::modes`, `CadBlockResolveMode`, `CadBlockApplyConnectionModeOffset`,
  `CadBlockWorldConnection::ownerPartType`.
- `src/commands/CadBlocks.hpp` / `.cpp` — `FindNearestPipeEndpoint`; `SubmitInsertBlockConnectorPick`
  now classifies the snapped target and resolves the matching mode; new `BCONNECTMODE` text command
  (add/update/remove/list modes on a connection point), following the existing BCONNECT/BCONNECTEDIT
  command-driven authoring pattern (this codebase has no separate GUI dialog for port authoring —
  BEDIT + text commands *is* the existing "dedicated UI").
- `src/io/GsIo.cpp` — serialize/deserialize `modes` per connection (backward compatible: absent
  `modes` array on read = empty vector = legacy single-mode behavior, unchanged).

## Test approach
Unit tests (Catch2, ASCII names) for: `CadBlockResolveMode` (matching mode / fallback to default /
empty-modes legacy behavior), `FindNearestPipeEndpoint`, mode round-trip through GsIo, and a
BCONNECTMODE command test (add two modes + default, then edit/remove).

## Architectural-boundary check
No new subsystem; extends the existing `cadblock.hpp` data model and its two existing command-driven
entry points (INSERT connector-pick, BEDIT text commands). No change to `CadSnap`.
