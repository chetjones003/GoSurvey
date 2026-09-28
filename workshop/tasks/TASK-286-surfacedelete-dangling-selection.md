# TASK-286 — deleting a surface no longer leaves the selection pointing at it

- Type:    fix
- Status:  review
- Opened:  2026-09-28
- Owner:   Workshop
- GitHub:  none — found on `beta` while writing the Phase 7 (#150) drape transcript

## Requirement authority

REQ-068 (a surface erase is one undoable step), REQ-076 / architecture §11.9 (the entity arrays
compact on erase, so an index is not a name). No requirement changed: this restores what the
`selection-in-range` document invariant already asserts.

## Why

The Phase 7 drape transcript box-selected the drawing — which picks the **surface** as well — and
then deleted the surface. `CHECK ALL` tripped:

```
selection-in-range: selection[5] type=10 index=0 but only 0 of that type exist
```

`SURFACEDELETE` erased the surface and never touched `st.selection`.

`ESC` is no help: it cancels the active command, not the selection.

## The half that is worse than the dangling entry

A selection entry past the end is at least *loud* — the invariant catches it. The silent case is a
drawing with **two** surfaces where the **first** is deleted: `cadSurfaces` compacts, the entry still
says "surface 1", and that index now names whichever surface moved into the slot. Nothing trips, and
the next command acts on a surface the user never picked. This is exactly the hazard architecture
§11.9 and the surface caches' own comments already record — the caches are keyed by stable id for
this reason; the selection is indexed and had no equivalent maintenance.

## What changed

`EraseSurfaceAtIndex` (`src/commands/CadCommands.cpp`) now, after compacting the two surface arrays:

- drops any selection entry naming the surface that is gone, and
- slides every **later** surface entry down one.

Done there because it is the **one** erase path (REQ-068): `SURFACEDELETE`, the `ERASE` command and
both panel Delete buttons all come through it, and only `ERASE` was clearing up after itself — it
gathers surface indices from the selection and erases highest-first, then clears the selection as a
whole. Fixing the choke point covers the other three callers without a second mechanism.

Nothing else needed maintenance: grepped for stored surface indices and there are none — every other
`surfaceIndex` in the tree is a function parameter, and the display/query/watershed caches are keyed
by stable id, not index.

## Tests

`headless.regression-surfacedelete-selection` pins both halves:

- select everything, delete the surface, `CHECK ALL` — the dangling entry;
- two surfaces both selected, delete the **first**, `CHECK ALL`, then confirm the survivor is still
  the one it always was and is still what a following command acts on — the silent renaming;
- deleting a surface that nothing has selected is unchanged.

Run against the unfixed build first: it fails at the first `CHECK ALL`, with exactly the invariant
message above.

Full suite: 1840/1847 — `beta`'s 7 headless failures, unchanged. One of those seven,
`req068-surface-selection`, is in this same area, so it was run against a fixed **and** an unfixed
build: identical failure both ways (`no log line contains: 1 surface ignored`), unrelated to this.

## Not in scope

- `req068-surface-selection`'s own failure, which is about a missing "1 surface ignored" report.
