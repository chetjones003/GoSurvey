# TASK-273 — Pipe runs belong to their own drawing (issue #486, user-reported)

## Authority
- User report, 2026-09-23: "pipe runs leak into other drawings viewports".
- REQ-345 (spec/requirements.md) — `CadPipeRun` is a drawing entity, persisted per drawing in `.gs`.
- Precedent: REQ-341 / D-2026-09-16-b finding 6 (the section clip had the identical defect), and
  REQ-154 (the UCS is per drawing, "state does not leak between viewports").

## Problem
Every per-drawing entity store lives in `DrawingDocument` (CadCommands.hpp) and is copied in and out
by `SaveDocumentToSnapshot` / `RestoreDocumentFromSnapshot` on a tab switch. `cadPipeRuns`,
`cadPipeRunAttrs` and `cadPipingSystems` were never added to any of the three — they arrived after
that pass was written — so a tab switch swapped everything else and left the pipe runs in the live
state. `NewDrawingInTab` and `OpenDrawingInNewTab` (CadUi.cpp) both build an empty document and load
it through the same restore, so File ▸ New and File ▸ Open showed the previous drawing's piping too.

A second instance of the same omission: `ClearCadGeometry` — what a DXF/DWG import calls to replace
a drawing's CAD content — did not clear pipe runs either, so an import into a drawing that already
had runs kept them beside the imported model.

## Files affected
- `src/commands/CadCommands.hpp` — `DrawingDocument` gains the three arrays.
- `src/commands/CadCommands.cpp` — `SaveDocumentToSnapshot` / `RestoreDocumentFromSnapshot` copy
  them; the restore also clears the derived `pipeRunWorldSolids*` and zeroes their signature.
  `ClearCadGeometry` clears the three arrays and the derived ones.
- `tests/CadPipeRunCommandTests.cpp` — 2 new cases.
- `spec/requirements.md` — REQ-345 amended.

## Implementation approach
The smallest correct fix is the one the section clip already got: name the state in the per-document
struct so the existing swap carries it. Nothing new is introduced — no second code path, no new
ownership concept.

The one judgement call is the DERIVED swept solids (`pipeRunWorldSolids`,
`pipeRunWorldSolidAttrs`, `pipeRunWorldSolidOwnerIndex`). They are display data, rebuilt from the
runs by `RebuildPipeRunWorldSolids` behind a content-hash gate, and `blockRefWorldSolids` beside
them is likewise not snapshotted. But the gate compares a hash of the RUNS only, and two drawings
can legitimately hash the same — two empty drawings always do — so leaving the arrays in place on a
restore would let the outgoing drawing's solids be certified as current for the incoming one. The
restore therefore clears them and sets the signature to 0 (never a real FNV signature, and already
the established "force a re-derive" value used by undo's own restore path).

## Test approach
Two Catch2 cases in `CadPipeRunCommandTests` (GoSurveySnapTests — it already links
`gosurvey_domain` for the command-layer functions):
1. Route a run through the real `PIPERUN` command path, put it in a `CadPipingSystem`, refresh the
   derived solids, save to tab 1, restore tab 2: no run, no attrs, no network, no derived solid, and
   still none after a second `RefreshSolidDisplayGeometry` (which is what catches a stale gate
   rather than a stale array). Restore tab 1: everything back, derived solid rebuilt.
2. `ClearCadGeometry` leaves no run and no derived solid.

## Architectural-boundary check
No new architecture. The fix moves existing state into the existing per-document container and uses
the existing derived-array signature convention. No ADR, no new requirement — REQ-345 amended with
the defect and its fix, as its five earlier follow-up paragraphs already are.

## Verification
- Build: `./dev/build` — clean.
- `GoSurveySnapTests.exe "[piperun]"` — 40 cases / 242 assertions, green.
- Full suites: `GoSurveyTests.exe` 1210/1210; `GoSurveySnapTests.exe` 395/395 (up from 393). No
  regressions.

## Result
PASS.
