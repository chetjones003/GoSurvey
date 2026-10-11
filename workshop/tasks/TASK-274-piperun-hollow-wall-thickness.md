# TASK-274 — Pipe runs are hollow, with a stated wall thickness (issue #486, user-requested)

## Authority
- User request, 2026-09-23: "pipe runs need to be hollow and need a specified wall thickness. this
  should be after the user specifies the pipe size."
- **D-2026-09-23-a** (spec/project.md) — the recorded decision, including the three choices the user
  made. REQ-345 amended (reversing its own "wall thickness does not change the modeled OD"),
  REQ-315 amended (`brep::SweepTube`).

## SPEC GAP raised and resolved before any code
REQ-345 increment B1 stated in as many words that a run is modelled at the nominal OD and that wall
thickness deliberately does **not** change the geometry. The request is the opposite, so this was
put to the user rather than decided here (CLAUDE.md §5). Two questions were asked in plain English
and answered:
1. blank Enter at the wall prompt takes the **schedule-40** wall for the size just chosen;
2. runs in drawings saved before this **become hollow at the standard wall** rather than staying
   rods — the user chose consistency, knowing it changes already-approved drawings on open.

The third choice (a real tube versus two nested solids) was an engineering one and is recorded with
its reasoning in the decision: a nested pair reports a rod's volume and shows nothing at a cut.

## Files affected
- `src/util/brep.hpp` / `src/util/brep.cpp` — **new** `brep::SweepTube`.
- `src/util/cadpiperun.hpp` — schedule-40 column on the NPS table;
  `CadPipeStandardWallThicknessInches`, `CadPipeRunWallThicknessFeet`; the swept solid now builds a
  bore profile and calls `SweepTube`.
- `src/commands/CadEntities.hpp` — `CadPipeRun::wallThicknessIn`.
- `src/commands/CadCommands.hpp` / `.cpp` — `PipeRunPhase::WaitWallThickness`,
  `pipeRunWallThicknessIn`, the prompt text and its input branch, the commit, PIPEFIT/PIPESPLIT/
  PIPEJOIN carrying the wall, PIPEPROP's optional wall argument and its honest refusal, help text.
- `src/viewport/CadRubberPreview.cpp` — the ghost run carries the drafted wall.
- `src/io/GsIo.cpp` — additive `wallThicknessIn` key.
- `src/ui/CadUi.cpp` — `PipeRunWallText`, a Wall row in the hover readout and the Properties table.
- `tests/BrepTests.cpp`, `tests/CadPipeRunTests.cpp`, `tests/CadPipeRunCommandTests.cpp`,
  `tests/GsIoPipeRunTests.cpp`.
- `spec/requirements.md` (REQ-315, REQ-345), `spec/project.md` (decision log).

## Implementation approach
**The kernel gets ONE new operation, not a piping-only geometry path.** `brep::Sweep` takes a single
closed profile, so there was no way to sweep a ring. `SweepTube` runs `Sweep` **twice over the same
path and frames** and merges the two results: the outer sweep's side faces unchanged, the inner
sweep's side faces re-aimed (every loop reversed) and marked `Surface::inward`, and the two planar
caps rebuilt as annuli with the inner rim as a hole loop. The merge is ~90 lines and touches nothing
inside `Sweep`.

Why that shape rather than the alternatives:
- **Not a Boolean.** The kernel's Booleans are an enumerated analytic family; two NURBS-swept bodies
  are not in it, and a bent pipe could never be bored.
- **Not two nested solids.** Volume would be a rod's, a section would show nothing, and the "pipe"
  would be a lie the mass properties agree with.
- **Not a new sweep implementation.** Running the existing one twice means path validity, the
  rotation-minimizing frame, mitre/corner classification, twist and arc-axis clearance are decided
  once, by the code that already owns them — a tube is refused exactly where a rod would be, by the
  same name. This is REQ-301 (minimal abstraction) applied literally.

`Surface::inward` already existed for a Boolean bore's wall, and the tessellator, the normal
evaluator and the volume integrand all honour it for NURBS faces — so the bore's normals, winding
and (critically) its NEGATIVE volume contribution came for free. That is what makes the volume the
difference rather than the sum.

**`wallThicknessIn == 0` means "not stated", never "solid".** One resolver,
`CadPipeRunWallThicknessFeet`, turns a run into the wall it is actually built at, and every consumer
(the solid, the prompt's default, the Properties readout) reads it — so no two of them can disagree
about how thick a pipe is. This is also the whole of the legacy story: an older `.gs` has no key,
loads as 0, and builds at schedule 40, with no migration code and no version bump.

## Test approach
- `BrepTests [tube]` — a straight tube's annular volume in closed form and its tessellated volume,
  two `inward` bands, two two-loop (annular) caps; a bent path's volume against Pappus AND against
  the same sweep's rod scaled by `1 - (ri/ro)²`; three refusals (inner outside outer, mismatched
  profiles, closed path) each storing nothing.
- `CadPipeRunTests [wall]` — the schedule-40 table per size and its refusal of an unlisted one; the
  resolver's own/default precedence; a stated wall changing the built volume and a heavier wall
  being more metal; a wall that leaves no bore refusing the run.
- `CadPipeRunCommandTests [wall]` — the prompt sequence (size → wall → first point), the refusals
  (non-numeric, negative, at half the OD) each holding the prompt, a click not skipping it, the run
  carrying the typed wall, the offered default following the SIZE rather than the last run, and
  PIPEPROP's wall argument plus its wall-specific refusal message.
- `GsIoPipeRunTests [wall]` — the key is written only for a stated wall, and a run without one loads
  as 0.
- Existing volume assertions in `CadPipeRunTests` were rewritten to the hollow expectation (they
  asserted a rod's volume, which is exactly what this changes).

## Architectural-boundary check
The kernel stays pure and knows nothing about pipes: `SweepTube` speaks profiles and paths, like
every other operation there (ADR-048 (a)). `cadpiperun.hpp` remains the one place an NPS label
becomes a physical size — the wall table lives beside the OD table it belongs to. No new layer, no
ADR: this is REQ-315's own operation set gaining one member, recorded as an amendment.

## Assumptions and technical debt
- The schedule-40 walls are ASME B36.10M standard values for the 13 sizes the NPS table carries.
  Only schedule 40 is tabulated; a SCHEDULE-name prompt (SCH 10/80/160) would need a table per
  schedule and is deliberately not guessed here.
- Catalog **fittings** (elbows, tees) are library blocks and are unchanged — they are not swept
  runs. A hollow run therefore meets a solid fitting at a joint; consistent fitting geometry is a
  catalog question, not a sweep one.
- A `.gs` written with a stated wall loads in an older build as the wall-less run it was (the key is
  simply ignored) — additive, per ADR-020 (d).

## Verification
- Build: `./dev/build` — clean, no new warnings.
- `GoSurveyTests.exe` 1216/1216 (up from 1210: 3 `[tube]` + 3 `[wall]`).
- `GoSurveySnapTests.exe` 397/397 (up from 393: 2 `[piperun][wall]`, 1 `[pipeprop][wall]`, 1
  `[gs][wall]`).
- `ctest` — 1767/1774, 7 failures, all **pre-existing on clean `beta`** (headless transcript
  regressions in OFFSET/surface-selection/feature-line/solid-isolines/solid-primitives and one
  command-name prompt, none of them piping). Verified by stashing this work, rebuilding and running
  those seven against untouched `beta`: identical failures. An **eighth** failure present before
  this task — `ctest` could not discover the `PIPERUN END with only a start point` case because its
  name contained an em dash — is fixed here in passing (the name is now ASCII), since the file was
  open and the case was silently not running under `ctest`.

## Result
PASS.
