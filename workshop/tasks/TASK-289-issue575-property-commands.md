# TASK-289 — CHPROP, MATCHPROP, LAYMCUR, a current colour and a ribbon colour dropdown

- Type:    feat
- Status:  review
- Opened:  2026-09-29
- Owner:   Workshop
- GitHub:  #575 (split from #564 §6, D-2026-09-28-g)

## Requirement authority

REQ-356 (new, D-2026-09-29-a). REQ-352 (the shared layer / colour edit and the ribbon Layers combo
rule), REQ-353 (pipe runs keep their size colour), REQ-121 (select objects, Enter to continue),
REQ-201.

## Decisions (D-2026-09-29-a)

1. A current colour for new objects (AutoCAD's CECOLOR), default ByLayer, saved like the current
   layer (user's choice, as recommended).
2. REQ-356 accepted as drafted: CHPROP refuses an unknown layer; MATCHPROP applies per pick;
   linetype / lineweight on the seven Properties-panel types only; transparency out of scope.

## Starting state (measured)

| Item | Before |
|---|---|
| CHPROP / MATCHPROP / LAYMCUR | not commands at all ("Unknown command") |
| Ribbon Match Properties | "not implemented yet" |
| Current colour | none — `MakeNewEntityAttrs` / `NewBlockAttr` hard-code `ByLayer` |
| Properties linetype / lineweight edit | UI-layer loops over 7 types, no undo step |

## Plan / architectural-boundary check

- **Commands** (`CadCommands.{hpp,cpp}`): `Kind::ChProp / MatchProp / LayMCur`, one phase field
  (`propCmdPhase`), `StartChPropCommand` / `StartMatchPropCommand` / `StartLayMCurCommand`,
  `HandlePropCommandTextInput` (Enter + typed), `CadPropCommandPromptText`,
  `CadPropCommandSelectionChanged` (the single-pick steps act as soon as a pick lands — called after
  a fence closes in `SubmitViewportPickImpl` and after an accumulate click in `CadUi`).
  `CadApplyLinetypeToSelection` / `CadApplyLineweightToSelection` (one undo, skipped count) join
  REQ-352's `CadApplyLayerToSelection` / `CadApplyColorToSelection` on the same template.
  `CadSelectionColor` / `CadRibbonPickColor` mirror `CadSelectionLayer` / `CadRibbonPickLayer`.
  `currentColor` stamped by `MakeNewEntityAttrs`, `NewBlockAttr` and the other current-layer
  builders.
- **Domain** (`CadColor.{hpp,cpp}`): `CadColorStorageFromTyped` (ByLayer / ByBlock / 1..255 /
  name / #RRGGBB).
- **IO** (`GsIo.cpp`): `currentColor` beside `currentLayer` in the trailer JSON (additive).
- **Viewport policy**: the three Kinds route to `SelectionAccumulate` in their select steps.
- **UI** (`CadUi.cpp`): ribbon colour combo in the Layers strip; Match Properties button enabled;
  Properties linetype / lineweight edits call the command layer; `SelectColorTarget::RibbonColor`.
- The linetype and lineweight option lists move from `ui/CadUiStyleWidgets.hpp` to the command
  layer (`CadEntities.hpp`) so CHPROP validates against the list the panel shows; the UI names
  alias them.
- No new dependency; no format version bump.

## Tests

`tests/PropertyCommandTests.cpp` (`[req356]`) — one case per acceptance bullet.

## Completion report

- Build: `./dev/build` (Release) clean, no new warnings; `dev/build-devshell.bat` (RelWithDebInfo +
  Developer Shell) clean.
- Tests: `tests/PropertyCommandTests.cpp` 12 cases / 156 assertions green; ctest 1939/1946 — the 7
  failures are the headless transcripts already failing on plain `beta` (issue233,
  issue402-offset-ucs, regression-58, req068, req087, req313-solid-isolines,
  req313-solid-primitives).
- GUI: `GoSurvey.exe --devshell-run req356-ribbon-color` passes — ribbon colour pick with nothing
  selected sets the current colour and a new LINE is red; with the line selected a pick recolours it
  and the current colour stays; the Match Properties button starts MATCHPROP. Screenshots checked:
  the colour combo fits under the layer combo, swatch and label update, the caption switches to
  "Layer of selection".
- Acceptance: every REQ-356 bullet has a test (see PR).
- Found on the way: `beta`'s Developer Shell did not compile — the #578/#579 merge dropped the `};`
  closing the `req354-dyninput-modes` test. Restored.
- Assumptions / debt:
  - The picker's "More colors…" result goes through `CadRibbonPickColor` without a log (the colour
    popup has no log handle); the recolour itself is identical.
  - `currentLayer` is still session-wide rather than per tab (pre-existing); `currentColor` is per
    tab from the start.
  - Undo does not rewind the current colour (it is a setting, not drawing content — as the current
    layer).
