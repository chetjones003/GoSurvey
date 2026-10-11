# TASK-269 — Multi-port auto-detection + exact-match priority (issue #486, user follow-up)

## Authority
- User report with 4 screenshots, 2026-09-17: a flange with `gasketFace` (mode → Flange face) and
  `weldNeckFace` (mode → Pipe end) connection points snapped to a pipe end using `gasketFace`'s
  mode instead of `weldNeckFace`'s.
- REQ-345 (spec/requirements.md), amended again.
- Directly follows TASK-268 (connection-mode filtering), which fixed the compatibility CHECK but
  not two other compounding gaps this task closes.

## Problem (two compounding causes)
1. `InsertSourceConnection` (removed this task) always returned `connections.front()` when the
   INSERT dialog named no explicit connector — the FIRST port defined on the block, never
   considering any other port the block might have. TASK-268's filtering ran against only that one
   port.
2. Both `gasketFace` and `weldNeckFace` had their single mode flagged `isDefault` (the screenshots
   show the blue dot filled on both) — the natural authoring state for an only-mode connection
   point. `CadBlockResolveMode`'s `isDefault` fallback means a mode-less-seeming default resolves
   for ANY target, so even with both ports considered, `gasketFace`'s default-flagged mode looked
   "compatible" with a pipe-end target too, tying with `weldNeckFace`'s genuinely correct one.

## Files affected
- `src/commands/CadBlocks.cpp` — `InsertSourceConnection` removed; `SubmitInsertBlockConnectorPick`
  rewritten to build a candidate list of every connection point on the block (or the one explicitly
  chosen), then select in two passes — exact target matches first, default-fallback matches only if
  no exact match exists anywhere on the block.
- `src/util/cadblock.hpp` — new `CadBlockConnectionHasExactMode(conn, target)`.
- `tests/CadBlockImportTests.cpp` — 1 new case reproducing the exact screenshot scenario.
- `spec/requirements.md` — REQ-345 amended.

## Implementation approach
Two-tier priority rather than a single filtered nearest-wins comparison: pass 1 only considers
ports with an EXACT mode for the target under the cursor (ignoring `isDefault` entirely); pass 2 —
only reached if pass 1 found nothing across every candidate port — falls back to the previous
default-inclusive behavior. This makes an explicitly-tagged port always win over a merely-default
one for its own target, however the block's connection points happen to be ordered or flagged.

## Test approach
One `CadBlockImportTests.cpp` case builds the exact reported block shape (`gasketFace` defined
FIRST with a Flange-face mode, `weldNeckFace` defined SECOND with a Pipe-end mode, BOTH modes
flagged `isDefault`) and drives the real `SubmitInsertBlockConnectorPick` path against a
`CadPipeRun`, asserting the placed fitting's `weldNeckFace` world connection lands on the pipe end
— proving neither definition order nor the shared default flag can steal the pick.

## Architectural-boundary check
No new architecture layer — refines the existing issue #496 connection-mode resolution rule
(exact match wins) to also govern candidate PORT selection across a multi-port block, not only
mode selection on an already-chosen port. No ADR needed.

## Verification
- Build: `./dev/build` — clean, no new warnings.
- Tests: `GoSurveySnapTests.exe [issue486][block]` 10/10.
- Full suite: `GoSurveyTests.exe` 1169/1169. `GoSurveySnapTests.exe` 320/320 (up from 319).

## Result
PASS.
