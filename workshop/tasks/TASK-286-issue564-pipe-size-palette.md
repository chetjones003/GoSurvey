# TASK-286 — pipe colour by nominal size

- Type:    feat
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  #564 section 7 (sixth increment; §3 = #566, §1 = #568, §2 = #569, §4 = #570, §6 = #576)

## Requirement authority

REQ-353 (new, D-2026-09-28-i). REQ-345 (pipe runs), REQ-352 /
D-2026-09-28-h (a block's layer-0 content follows its insert — the mechanism that makes a fitting
body show its insert's colour). Issue #564 Q4 / Q5 were answered on the issue.

## Decisions (D-2026-09-28-i)

Asked in plain English, both decided as recommended:

1. 22in has no schedule-40 wall in ASME B36.10M → its row carries STD, 0.375in.
2. A spliced fitting takes its run's **layer** as well as its colour.

Recorded without a question: the issue's "20 sizes" is a miscount of its own lists (21); a split's
far piece keeps the run's attributes.

## Starting state (measured)

| §7 item | Before |
|---|---|
| NPS sizes | 13 — missing 3-1/2, 5, 14, 16, 18, 20, 22, 24 |
| New run colour | `ByLayer` (`MakeNewEntityAttrs`) for every size |
| Auto elbow / tee / PIPEFIT attrs | current layer, `ByLayer` (`NewBlockAttr`) |
| PIPEFIT / PIPESPLIT far piece attrs | fresh `MakeNewEntityAttrs` — an override was lost |
| Refused-size message | a hand-typed list of the 13 sizes |
| Fitting body follows the insert's colour | yes, since D-2026-09-28-h (library parts are layer 0 / ByLayer) |

## Plan / architectural-boundary check

- **Domain** (`cadpiperun.hpp`): `CadPipeNpsEntry` gains `colorHex`; 8 rows added;
  `CadPipeNpsFind`, `CadPipeNominalSizeColor`, `CadPipeKnownNominalSizesText` read the one table.
- **Commands**: `MakeNewPipeRunAttrs` (current layer + size colour) stamps PIPERUN's pieces; one
  `lineAttrs` value is handed to every elbow / tee of the commit. PIPEFIT and PIPESPLIT copy the
  split run's attrs (`DuplicatedEntityAttrs`) to the far piece and to the fitting.
  `CadBlockPlaceInsertNoUndo` takes an optional `lineAttrs` whose layer and colour the insert takes.
- INSERT's connector snap: `FindNearestPipeEndpoint` reports which run's end won;
  `SubmitInsertBlockConnectorPick` records it in `insertBlockSnappedPipeRun`, and
  `CadBlockPlaceInsert` consumes it (reset on start and cancel) — an end flange joins its line.
- No new dependency, no persistence change (a colour is an ordinary entity colour string), no UI
  change.

## Tests

`tests/PipeSizePaletteTests.cpp` (GoSurveySnapTests, `[req353]`):

- the table: 21 rows, the 8 added sizes' OD and wall (22in = 0.375), 21 distinct valid colours,
  ascending sizes, lookups;
- PIPERUN: refusal lists 3.5in…24in; 22in offers 0.375;
- 4in run → `#2D6CDF`, 2in beside it → `#2ECC40`, both on the ASSEMBLED display;
- auto elbow takes the run's colour and layer;
- PIPEFIT valve and reducer take the run's layer and override colour, as does the far piece;
- PIPESPLIT keeps an override on both pieces;
- a flange connector-snapped onto a run's end takes its layer and colour; onto a bare line's end,
  it stays an ordinary insert;
- override wins on the display, and survives save → reload.

`tests/CadPipeRunTests.cpp`: the two "unknown size" examples moved from 5in (now a real size) to 7in.

## Results

- `[req353]`: 8 cases, 212 assertions, all pass. `[piperun]` in GoSurveyTests: 19 cases pass.
- Full suite: 1870/1878 on the first run; the one new failure was the 5in example above (fixed).
  The remaining 7 (issue233, issue402-offset-ucs, regression-58, req068, req087,
  req313-solid-isolines, req313-solid-primitives) fail identically on unmodified `beta`.

## Found in the final review

- The first push coloured only SPLICED fittings. A flange at a run's open end is placed with INSERT's
  connector snap, not PIPEFIT (which refuses one-port parts), so it still took the current layer
  and `ByLayer` — short of §7's "flanges take the run's colour". Fixed in the same PR (see Plan).

## Not in scope / technical debt

- A part connector-snapped onto another FITTING's port (not a run's end) is an ordinary insert.

- The Pipe Fittings palette (REQ-350) is on the unmerged `feat/pipe-fitting-palette` branch, not
  `beta`. Its name-taking splice core must pass the run's attributes to
  `CadBlockPlaceInsertNoUndo` (as `TrySplicePipeFit` now does) when that branch is rebased.
- Runs already in a drawing keep their colour on open; the palette applies at creation.
- The bundled library has no parts for the new sizes; PIPERUN draws smooth bends for them, as it
  does for any size the catalog lacks.
- A user-editable palette (Q5) and the ribbon size dropdown (§8) are later increments.
- The live PIPERUN ghost is not coloured by size (it is a preview, drawn in the preview colour).
