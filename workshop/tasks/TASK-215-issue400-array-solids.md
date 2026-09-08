# TASK-215 — Issue #400 (increment 3 of 3): ARRAY duplicates solids (Rectangular only)

- Type:    feature
- Status:  done — PR pending (stacked on #409, #410)
- Opened:  2026-09-07
- Owner:   Workshop
- GitHub:  #400

## 1. Authority

- REQ-305 acceptance 13 (added 2026-09-07, D-2026-09-07-c).
- REQ-322 item 6, amended same decision: Rectangular ARRAY carved out of the "refuses solids" group;
  Polar ARRAY and Surfaces stay excluded.
- Precedent: `TranslateSelectedSolids` (`CadCommands.cpp:9339-9358`, REQ-322) — `brep::Translate`
  returns a new `Solid` by value; wrap in `std::make_shared<const brep::Solid>(...)`.

## 2. Scope

- `DropArrayUnsupportedFromSelection`: stop dropping `Solid` when the array type is (or will be)
  Rectangular. Since the array type isn't chosen until AFTER selection (WaitType comes after
  PickSelection), solids stay in the selection through PickSelection and are dropped instead at the
  point Polar is chosen (mirroring how the Polar-under-tilted-UCS refusal in increment 1 is applied
  at the 'p'/'polar' keystroke, not at selection time).
- `CommitArrayRectangular` (or a helper it calls) duplicates each selected `Solid` at the same
  (dx,dy,dz) as every other entity type, via `brep::Translate` + a fresh `CadSolidPtr` appended to
  `st.cadSolids`.
- `CommitArrayPolar` gains a named refusal/skip + log line for any `Solid` still in the selection
  (reachable only if a later increment changes the Polar-refuses-solids gate; today `Solid` should
  never reach it because the WaitType 'p' handler already dropped it and told the user why).
- Preview (`TransformPreview.cpp`) is NOT extended to draw solid ghosts — matches REQ-305 acceptance
  5's existing scope note that Annotation/FilledRegion are duplicated correctly but not previewed;
  Solid joins that same documented preview gap, not a new one.

## 3. Files

- `src/commands/CadCommands.cpp`: `DropArrayUnsupportedFromSelection` (~:8875), `CommitArrayRectangular`,
  `CommitArrayPolar`, the 'p'/'polar' branch in `HandleArrayText`.

## 4. Tests

- New headless transcript: BOX-select a solid + a line together, Rectangular ARRAY duplicates both
  (solid count check via `EXPECT SOLIDS n` or equivalent); Polar ARRAY on a selection containing a
  solid logs the exclusion by name and arrays only the non-solid entities; undo removes every
  generated solid instance in one step.

## 5. Verification

- `build-project`, `testing`.
