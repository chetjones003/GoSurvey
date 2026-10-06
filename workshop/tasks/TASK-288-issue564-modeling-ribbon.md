# TASK-288 — a Modeling ribbon tab

- Type:    feat
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  #564 section 8 (last increment; §3 = #566, §1 = #568, §2 = #569, §4 = #570, §6 = #576,
  §7 = #577, §5 = #578)

## Requirement authority

REQ-355 (new, D-2026-09-28-k). REQ-302 / ADR-053 (ribbon + layout engine), REQ-345 (piping),
REQ-353 (21-size NPS table), REQ-060 / D-2026-09-28-a (3DMOVE / 3DROTATE / 3DSCALE), REQ-351.

## Decisions (D-2026-09-28-k)

1. The ribbon's PIPERUN skips BOTH the size and the wall question (user's choice; recommendation
   was to keep the wall question).
2. `pipeRunNominalSize` starts at `4in` (user's choice; recommendation was a placeholder).

Recorded without a question: tab after Survey; a button cancels a running command first; PIPEFIT
gets a part-type menu; dropdown disabled while a run is being drawn.

## Starting state (measured)

| §8 item | Before |
|---|---|
| Ribbon tabs | Home / Insert / Annotate / View / Manage / Output / Survey |
| Listed commands | all 30 exist and are typed-only |
| Icons | the library set already has Box, Wedge, Cone, ... 3D_Move, 3D_Rotate, c3d_pipenet |
| PIPERUN | asks size, then wall; size empty on a new session |
| Typed command while another runs | fed to the running command (idle dispatch needs `active == None`) |

## Plan / architectural-boundary check

- **UI** `src/ui/ModelingRibbon.hpp` (new, plain data): one table of sections → buttons
  (id, label, command text, icon name, tooltip). The tab draws from it; the test reads it.
- **UI** `CadUi.cpp`: `kRibbonTabModeling` tab button; sections built from the table through the
  existing `RibbonSectionSpec` / `drawRibbonSectionSpec` path; click = `CancelActiveCommand` +
  `SubmitRibbonCommand(command)`. Piping section adds a hand-drawn size combo (as the View tab's
  visual-style combo is) bound to `cmd.pipeRunNominalSize`, and a PIPEFIT part-type popup.
- **Commands**: `kRibbonTabModeling = 7`, contextual tabs 8..11, `kRibbonTabCount = 8`;
  `pipeRunNominalSize = "4in"`; `StartPipeRunAtCurrentSize` (ribbon entry: size + standard wall,
  straight to start point; falls back to the typed prompts if the size has no standard wall).
- DevShell ribbon screenshot tour gains the Modeling tab.
- No new dependency, no persistence format change (the tab index is an int slot already clamped).

## Tests

`tests/ModelingRibbonTests.cpp` (`[req355]`):

- the table has the six sections and the listed commands, unique ids;
- every button's command, submitted through `ProcessCommandLineSubmit` on a fresh state, is
  accepted (no "Unknown command");
- a running command is cancelled before a ribbon command starts;
- `StartPipeRunAtCurrentSize` → `WaitFirstPoint`, schedule-40 wall, at the current size (4in
  default, 22in → 0.375); typed PIPERUN still prompts for size;
- dropdown ⇄ command line: a typed size is `pipeRunNominalSize`, which the ribbon start reads.

Existing tests that assumed an empty starting size updated (refusal leaves `4in`).

## Completion report

- Build: `./dev/build` (Release) clean; `dev/build-devshell.bat` (RelWithDebInfo + Developer Shell)
  clean. A true Debug link is impossible in this repo (release-only xerces prebuilt — see
  `dev/build-devshell.bat`); the DevShell tree is the established stand-in. No `RibbonNyiButton`
  is used; every deferred closure captures its block-locals by value.
- Tests: `tests/ModelingRibbonTests.cpp` 7 cases green; ctest 1879/1886 — the 7 failures are the
  headless transcripts already failing on plain `beta` (issue233, issue402-offset-ucs,
  regression-58, req068, req087, req313-solid-isolines, req313-solid-primitives).
- GUI: `GoSurvey.exe --devshell-run req355-modeling-ribbon` passes — tab click, BOX, UNION while BOX
  runs (cancels + starts), size combo → 6in, ribbon PIPERUN → WaitFirstPoint with 0.280in wall.
  Screenshots checked: six sections, three rows, nothing clipped; the size combo is greyed while a
  run is drawn; prompt reads `PIPERUN [6in wall 0.280in] - start point`.
- Acceptance: all REQ-355 criteria met (see PR).
- Assumptions / debt:
  - Ribbon ids drawn through `RibbonLayout::DrawSection` are `##RibbonLayout_<id>`; the old
    `ClickRibbonTool` helper still clicks bare `##RibbonLine` (pre-existing, untouched).
  - Prefs save clamps the Block Editor / Point Cloud contextual tabs to the last permanent tab,
    now Modeling instead of Survey (pre-existing clamp, harmless).
  - Home tab's Array button is still a not-implemented placeholder though ARRAY works (outside §8).
