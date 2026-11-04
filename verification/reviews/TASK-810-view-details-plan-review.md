# Verification review — TASK-810 View Details plan

- Date: 2026-11-04
- Layer: Verification (plan review, before implementation)
- Verdict: **APPROVE**

## Authority and acceptance

Accepted REQ-487 is based on the user's confirmed feature request and decision D-2026-11-04-g. The checks are observable: the Output action presents the active layout's effective stored values; explicit custom and unset cases display correctly; the action is unavailable outside paper space; and inspection leaves the drawing and undo stack unchanged, including for read-only drawings. REQ-445 and REQ-485 govern the meaning of the displayed plot options and style-table selection.

## Architecture

- **Layering:** ribbon action and dialog remain in Qt UI; the UI reads existing `PaperLayout` values and `PageSetupFromLayout`. No lower subsystem depends on the UI.
- **State ownership:** `PaperLayout` remains authoritative for the effective settings. The dialog displays a value snapshot and does not keep a second editable model.
- **API/data/dependencies:** no format, public API, dependency, output backend, persistent field, or new reusable abstraction is required.
- **Scope:** read-only active layout summary only. Do not expose other saved Page Setups or add Apply/Create/Modify behavior.

## Risks and controls

1. **Stale or wrong layout:** resolve the active layout index at action time and refuse invalid/non-paper state; `RefreshRibbon` disables the action in model and floating model space.
2. **Misleading values:** format dimensions from the stored preset/custom dimensions and orientation; display `fitToPaper` versus the stored ratio correctly; show centering and offsets as saved even when centering means offsets are ignored by the plotter; show `plotArea` by its existing enum/meaning.
3. **Confusing current versus named setup:** show the layout's `pageSetupName` when set and clearly identify the layout's own settings when it is empty. Do not let saved setup selection state in the manager change what is displayed.
4. **Mutation:** avoid `EnsureStandardPageSetup`, undo snapshots, and any Page Setup Manager mutation; test a before/after state and undo depth.
5. **Visible UI:** capture the Output tab and dialog in screenshots. Civil 3D is not installed, so any product comparison must be marked inconclusive rather than passed.

## Required tests

- Active paper layout opens the dialog via the Output ribbon action and displays the GoSurvey PDF device plus the correct setup name, paper preset/dimensions, orientation, plot area, fit/ratio, centering, offsets, and CTB/STB value.
- Custom dimensions and a populated CTB/STB path appear accurately; empty setup name and table are explicitly labeled.
- Open/dismiss leaves the `PaperLayout`, overall drawing data, and active document undo depth unchanged.
- Action remains available for a read-only drawing.
- Action is disabled in model and floating model space and cannot open a details dialog there.

## Implementation plan review

The plan reuses the existing layout fields and paper preset catalog, routes the existing ribbon control, and adds a concrete dialog with no editing path. The proposed tests cover the acceptance conditions and edge states without requiring domain or file-format changes. No blocking findings; Workshop implementation is approved.
