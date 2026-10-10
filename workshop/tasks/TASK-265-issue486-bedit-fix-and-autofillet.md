# TASK-265 — BEDIT isolation fix + pipe-run auto-fillet (issue #486, user follow-up)

## Authority
- User-reported bug (with screenshot) and user-specified feature request, 2026-09-17, on top of
  increments B1 (TASK-263, PR #527, merged) and B2 (TASK-264, PR #528, open).
- REQ-345 (spec/requirements.md), amended twice for these two follow-ups.
- Product decision recorded here: fillet angle set is `{90, 60, 45, 30, 22.5, 11.25}` degrees
  (user chose this over a narrower `{90, 45, 22.5}` option when asked). Fillet radius formula
  (1.5x nominal pipe size, long-radius elbow takeoff) was given directly by the user — not a SPEC
  GAP, an established industry formula the user already knew.

## Part 1 — BEDIT isolation bug fix

### Problem
A `CadPipeRun` drawn in the main drawing kept rendering inside BEDIT's own viewport while editing
an unrelated block, because `cadPipeRuns`/`cadPipeRunAttrs` were never added to the block editor's
model-array swap.

### Files affected
- `src/commands/CadCommands.hpp` — `cadPipeRuns`/`cadPipeRunAttrs` added to
  `DrawingGeometrySnapshot`.
- `src/commands/CadCommands.cpp` — added to `CaptureGeometrySnapshot`/`RestoreGeometrySnapshot`;
  `pipeRunWorldSolidsSig` reset to 0 on restore to force a re-derive.
- `src/commands/CadBlocks.cpp` — cleared in `LoadBlockPrimitivesIntoDrawing` alongside
  `cadTables`/`cadSurfaces` ("hide everything that is not the block being edited").
- `tests/CadPipeRunCommandTests.cpp` — new case driving `CadBlocksEnterNamedEditor` and
  `CadRestoreGeometrySnapshot` directly.

## Part 2 — Automatic bend filleting

### Problem / request
The user wants pipe-run bends to auto-fillet to the nearest standard fitting angle (90/60/45/30/
22.5/11.25°), using the long-radius elbow takeoff formula (radius = 1.5x nominal pipe size) they
specified.

### Files affected
- `src/util/cadpiperun.hpp` — rewritten. `CadBuildPipeRunSweptSolid` builds ONE `brep::Sweep`
  solid for the whole run (was: one `brep::MakeCylinder` per straight segment with sharp,
  unfilleted corners). New: `kCadPipeFilletStandardAnglesDeg`, `CadPipeFilletRadiusFeet`,
  `CadPipeSnapFilletAngleRad`, `CadBuildPipeProfile` (round pipe cross-section, modeled on
  `ExtrudeProfileFromSelection`'s own circle branch in CadCommands.cpp). `CadBuildPipeRunSolids`
  keeps its external signature (append-to-vector) for zero call-site changes.
- `tests/CadPipeRunTests.cpp` — rewritten/expanded (7 → 13 cases): existing cases updated for the
  new one-solid-per-run behavior, new cases for the angle-snap function, the radius formula, a real
  90° bend, a sub-threshold kink, leg-length clamping, and a genuinely-too-tight refusal.
- `src/commands/CadCommands.cpp` — `CommitPipeRunDraft`'s log message and stale comment updated
  (a build failure is now a real, reachable outcome — a too-tight corner — not belt-and-braces).

### Implementation approach
Round-pipe sweeps cannot mitre a sharp corner at all (`brep::Sweep`'s mitre path is
polygonal-profile only), so EVERY real bend needs an arc segment — this made "auto-fillet" not
optional once the profile is round, which is exactly what the user asked for anyway. The path is
built by a single forward march from the run's actual start: at each interior vertex, the turn is
measured from the direction the pipe is CURRENTLY travelling (carrying any upstream snapping
drift) toward the ORIGINAL next-vertex direction; the bend plane, arc centre and exit tangent are
derived from that, and the snapped angle is what actually gets swept — not the corner's true
clicked angle. This keeps every joint tangent-continuous (required for `Sweep` to accept it) at the
cost of the built path drifting from the clicked polyline when several close bends compound, stated
explicitly in the function's own doc comment and the REQ entry. The fillet radius is clamped
(not refused) when a leg is short relative to the ideal takeoff; only a corner with essentially no
room left (clamped tangent under 1e-6 ft) refuses the whole run.

### Test approach
`CadPipeRunTests.cpp` (header-only, no GL): angle-snap correctness including a tie, the radius
formula, `brep::Validate`/`brep::ComputeMassProperties` on straight/bent/multi-bend runs, the
collinear-passthrough threshold, the leg-length clamp, and the true-refusal floor. Full suite run
after with no regressions.

## Architectural-boundary check
No new architecture layer. The sweep-with-fillets approach reuses REQ-315/ADR-048's existing
`brep::Sweep` kernel entirely as-is — no kernel change, only a caller building a richer
`SweepPath`. No ADR needed.

## Verification
- Build: `./dev/build` — clean, no new warnings.
- Tests: `GoSurveyTests.exe [piperun]` 13/13; `GoSurveySnapTests.exe [piperun]` 12/12.
- Full suite: `GoSurveyTests.exe` 1166/1166 (up from 1160). `GoSurveySnapTests.exe` 311/311.

## Result
PASS.
