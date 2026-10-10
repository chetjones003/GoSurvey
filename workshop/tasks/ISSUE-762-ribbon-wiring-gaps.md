# ISSUE-762 — Ribbon wiring gaps (quick wins)

**GitHub:** #762 (audit); this task implements follow-up item 1 (wiring gaps only).

## Authority

- Issue #762 “Wiring gaps” table and “Quick wins” #1
- Existing commands: `ARRAY` (`StartArrayCommand`), `IMPORTPOINTS` (`StartImportPointsCommand`), Properties panel focus (`cmd.pendingPropertiesFocus`)

## Scope

Wire three Home/Insert ribbon controls that already have working commands elsewhere:

| Control | ID | Action |
|---------|-----|--------|
| Array | `##RibbonArray` | `StartArrayCommand` |
| Properties (Palettes) | `##PalProps` | `cmd.pendingPropertiesFocus = true` |
| Points From File | `##RibbonInsPointsFile` | `StartImportPointsCommand` |

## Files

- `src/ui/CadUi.cpp`

## Tests

- `./dev/build`
- `./dev/test` (full suite)

## Verification

- Buttons enabled (not greyed NYI)
- Tooltips match wired behavior (same as Modeling / Survey contextual / other Properties buttons)
- `tests/RibbonWiringGapTests.cpp` — static regression guard (issue #762 follow-up automation)
