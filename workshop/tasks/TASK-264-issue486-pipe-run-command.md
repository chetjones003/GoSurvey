# TASK-264 — Interactive PIPERUN routing command (issue #486 increment B2 / REQ-345)

## Authority
- GitHub issue #486 ("Piping System"), increment B2 of Track B.
- REQ-345 (spec/requirements.md), amended for this increment.
- Builds directly on increment B1 (TASK-263, `CadPipeRun` entity/persistence/render), merged to
  `beta` as PR #527 before this task started.

## Scope
Only B2: the interactive routing command — click-to-add vertices, rubber-band preview solid,
undo, osnap. Explicitly NOT in scope: `CadPipingSystem` container (B3), catalog lookup (B4),
auto-fitting (B5/B6), manual fitting placement (B7), other edit operations (B8), and rewiring the
ribbon's existing "Pipe Network" NYI button (a separate, shared disabled-button helper covering
twelve Civil-3D-style buttons — out of scope for the command layer).

## Files affected
- `src/commands/CadCommands.hpp` — `Kind::PipeRun`, `PipeRunPhase`, draft state fields
  (`pipeRunDraftVerts`/`pipeRunNominalSize`/`pipeRunPressureClassTag`), function declarations.
- `src/commands/CadCommands.cpp` — `StartPipeRunCommand`, `CadPipeRunPromptText`,
  `HandlePipeRunTextInput`, `SubmitPipeRunViewportPick`, `CancelPipeRunCommand`,
  `CommitPipeRunDraft`/`AddPipeRunPoint` (anonymous-namespace helpers) — modeled directly on the
  existing POLYSOLID command (`StartPolysolidCommand` et al.) as the nearest analogue: a
  path-building command with its own `Kind` and its own state machine. Wired into: the ESC-cancel
  switch, the typed-line dispatch in `ProcessCommandLineSubmit`, the viewport-click dispatch, the
  idle-token command dispatcher, and the `kRegistry` help table.
- `src/viewport/CadRubberPreview.cpp` — live preview block, calling the same
  `CadBuildPipeRunSolids` (from B1's `cadpiperun.hpp`) the next click commits.
- `src/viewport/ViewportPickPolicy.hpp` — `ViewportClickRouteFor` case for `Kind::PipeRun`
  (`SnappedPointPick`), required by the exhaustive switch the moment the `Kind` enum grew.
- `src/ui/CadUi.cpp` — status-bar prompt hint.
- `tests/CadPipeRunCommandTests.cpp` — 11 cases, added to the `GoSurveySnapTests` target (needs
  `gosurvey_domain` for the command-layer functions, like `CadBlockImportTests` beside it).
- `spec/requirements.md` — REQ-345 amended with the B2 delivery note.

## Implementation approach
Copied POLYSOLID's proven shape rather than inventing a new one: prompted settings first (nominal
size + optional pressure class, validated with B1's own `CadPipeNominalOdFeet`/
`ParseCadPipePressureClass` — an unresolvable answer refuses and re-prompts, never guessed),
remembered across runs (matching POLYSOLID's width/height/justify), then click-to-add points with
`U`/`END`/blank-Enter/ESC exactly mirroring POLYLINE's and POLYSOLID's own established keyword
vocabulary. The live preview reuses the SAME builder function (`CadBuildPipeRunSolids`) the commit
calls, appending the cursor point to a temporary `CadPipeRun` — POLYSOLID's own "no separately
drawn approximation" argument, applied identically. Osnap needed no new code: `CadSnap::FindBest`
is gated only on "a command is active," not on which one, so adding `Kind::PipeRun` to
`ViewportClickRouteFor` was the only wiring 3D osnap (including existing connection ports and
solid faces) needed.

## Test approach
Direct `AppCommandState` manipulation (Catch2, `tests/CadPipeRunCommandTests.cpp`), the same
pattern `CadBlockImportTests.cpp` already uses for command-layer functions that need the full
`gosurvey_domain` static library: start/prompt state, refusal of an unknown size/class with the
prompt held, a full click-undo-click-END round trip landing in `st.cadPipeRuns`, blank-Enter
finishing the same as END, END refusing with too few points, U's undo-not-past-the-start-point
guard, ESC-equivalent cancel remembering settings, a second run reusing the remembered size, and a
click before the size is set being refused rather than silently accepted. Full suite run after
(`GoSurveyTests.exe`, `GoSurveySnapTests.exe`) with no regressions.

## Architectural-boundary check
No new architecture layer — this is the routing UI over B1's already-accepted entity, following an
existing command's exact shape. No ADR needed.

## Verification
- Build: `./dev/build` — clean, no new warnings (confirmed the `ViewportClickRouteFor` exhaustive
  switch required the new case — caught at compile time as designed, not discovered later).
- Tests: `GoSurveySnapTests.exe [piperun]` — 11/11 passing, 49 assertions.
- Full suite: `GoSurveyTests.exe` — 1160/1160 passing. `GoSurveySnapTests.exe` — 310/310 passing.

## Result
PASS.
