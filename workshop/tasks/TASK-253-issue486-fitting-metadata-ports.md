# TASK-253 — Fitting metadata + connection port roles/engagement (issue #486, Track A1+A2)

## Requirement authority
- REQ-337 (spec/requirements.md)
- GitHub issue #486, Track A increments A1 and A2 only. Decisions D-2026-09-12 (1)-(4).
- Out of scope: A3 (library export/WBLOCK), A4 (in-editor solid authoring polish), A5 (library
  browser grouping), all of Track B (routing engine), Track C.

## Files/subsystems affected
- `src/util/cadblock.hpp` — new enums (`CadFittingPartType`, `CadPipePressureClass`,
  `CadBlockConnectionRole`) + to/from-string helpers; `CadBlockFittingMeta` struct; new fields on
  `CadBlockDefinition` and `CadBlockConnection`.
- `src/io/GsIo.cpp` — JSON read/write for the new fields, backward-compatible defaults.
- `src/commands/CadBlocks.cpp` / `CadBlocks.hpp` — new `BLOCKFITTING` command; extended
  `BCONNECT` (arg form + pick-a-face form) and `BCONNECTEDIT`.
- `src/commands/CadCommands.hpp` (`AppCommandState`) — pending role/engagement/compat buffers for
  the pick-a-face BCONNECT flow, alongside the existing `bconnectNameBuf`/`bconnectSizeBuf`.
- `tests/CadBlockTests.cpp`, `tests/CadBlockImportTests.cpp` — round-trip + command coverage.

## Implementation approach
1. Add the three enums with explicit `None` members and `ToString`/`FromString` helpers
   (case-insensitive parse, following `CadBlockEqCi`).
2. Add `CadBlockFittingMeta { partType, nominalSize, pressureClass, partNumber }` and a `fitting`
   member on `CadBlockDefinition`.
3. Add `role`, `compatTag`, `engagementLength` members on `CadBlockConnection` (defaults: `None`,
   `""`, `0.f`) so existing `.gs` files and in-memory defaults round-trip unchanged.
4. `GsIo.cpp`: serialize `fitting` as a nested object (only when `partType != None`, mirroring how
   `connections` is only emitted when non-empty); serialize the three new connection fields inline
   with the existing `name`/`nominalSize`/`x`/`y`/`z`/`nx`/`ny`/`nz`, using `.value(...)` defaults
   on read.
5. `CadBlocks.cpp`: add `BLOCKFITTING` command (report/set/clear — see REQ-337 acceptance);
   extend `BCONNECT`'s 8-field literal form and the buffered pick-a-face form with optional
   trailing role/engagement fields; extend `BCONNECTEDIT`'s update and report paths.
6. Reject unrecognized enum tokens with a named-choices message; do not mutate state on a bad
   token.

## Test approach
- Unit tests (Catch2, `CadBlockTests.cpp`): enum string round-trip (valid + invalid tokens);
  `CadBlockFittingMeta` default is "not a fitting"; JSON round-trip via `GsIo` for both new
  connection fields and block fitting metadata, including a synthetic "old" JSON blob missing the
  new fields to prove backward-compatible defaults.
- Headless command tests: `BLOCKFITTING` set/report/clear/reject-bad-token; `BCONNECT` short form
  still works; `BCONNECT` with role+engagement; `BCONNECTEDIT` update + no-arg report shows new
  fields.

## Architectural-boundary check
- Piping topology (Track B) is NOT introduced here — no `CadPipeRun`/`CadPipingSystem`. This task
  only makes block definitions describe themselves as fittings and makes connection ports richer.
- No new UI palette/gizmo (command-line only), consistent with the existing BEDIT toolchain and
  REQ-301 (minimal abstraction — no palette infra until something other than metadata-tagging
  needs it).

## Status
- Planned 2026-09-12.
