# TASK-893 — Port Qt surface and surface-style management dialogs

- Type: feature
- Status: complete
- Opened: 2026-10-18
- Owner: Claude

## 1. Authority

- GitHub issue #893 (parent tracker #878). Acceptance: preserve beta 0.7.4 behavior (cancel, validation,
  failure, undo, read-only), a fully usable Qt workflow (a shared command route or a visible ribbon
  button alone is not parity), an updated parity ledger with focused workflow coverage, and 2–3
  integrated screenshot options for each major affected surface with the user's selection applied
  before the issue is complete.
- Behavior authority is the ImGui `DrawCreateSurfaceWindow` (`CadUi_Toolspace.cpp`),
  `DrawSurfacePropertiesWindow`, `DrawSurfaceManagerWindow` and `DrawSurfaceStyleWindow`, under accepted
  **REQ-068** (TIN surface; delete is one undo step), **REQ-069** (definition, dynamic rebuild, boundaries
  apply in definition order), **REQ-070** (surface styles, interval rule, deleted-style fallback),
  **REQ-072** (analysis bands, slope arrows), **REQ-075** (Surface Manager: state chip, refused rename),
  **REQ-086** (point-file links), **REQ-124 / REQ-136** (empty and volume surfaces), **REQ-361**
  (Object Layers layer for a new surface) and **REQ-201** (no control that cannot act, no silent refusal).

## 2. Scope

The issue says "surface and surface-style management dialogs used by 0.7.4 commands". Four 0.7.4
windows are in scope:

- **Create Surface** (TIN / Grid / Corridor / TIN volume / Grid volume, name, description, style, layer).
- **Surface Properties** (Information, Definition, Analysis, Statistics).
- **Surfaces** manager (definition tree, Add / Remove / Move, point-file link and import, New /
  New volume / New from group, rename, style, status, Rebuild, Delete).
- **Surface Style** editor (Information, Contours, Triangles, Borders, Points, Analysis, Display, Summary).

Entry points: the Survey and Survey-Point ribbons' Create Surface, the contextual Surface tab's Surface
Properties and Add Data > Point Groups, the commands that set the same flags (`SURFSTYLE`, ...), the
Toolspace Surfaces / Surface Styles menus, Surfaces ▸ Edit..., Surface Properties ▸ Edit....

Also in scope because the 0.7.4 Properties panel shows it and #891 deferred it here: the read-only
**Surface** section for a selected TIN.

Out of scope (own issues or not a dialog): Toolspace Prospector definition-folder Add/Refresh menus and
Zoom to / Pan to (the same operations are in the Surfaces manager), Point Group manager (#894), Feature
Line elevation window, Volume dashboard, survey-point properties (#902/#904).

## 3. Architectural boundary check

- New abstraction? One small Commands-layer service file, `commands/CadSurfaceEdit`, holding the edit
  rules the ImGui windows had inline. It has two concrete users (the ImGui windows, which now call it, and
  the Qt dialogs). Qt supplies only widgets. Same shape as #891's `CadPropertyEdit`.
  - [x] No new layer — proceed within existing boundaries.
- `src/qt` gains `EditorSurfaces.cpp`, `EditorSurfaceManager.cpp`, `EditorSurfaceStyles.cpp` and the shared
  widget header `EditorSurfaceUi.hpp`.

## 4. Questions

| # | Question | Asked | Answer |
|---|---|---|---|
| Q1 | Which integrated visual direction for the four surface dialogs? | 2026-10-18 | Option C (accent bar, flat tabs) for all four dialogs, with the accent in GoSurvey blue #438fda, not teal. Approved 2026-10-18. |

Decision taken without asking (recorded, easy to reverse): the Surface Style editor **Cancel restores**
every style and every surface's style name. The 0.7.4 window applied edits live and Cancel only closed the
window, so a user could not back out. The Qt editor keeps the live preview, makes **OK** one undo step for
the whole session and **Cancel** (or closing the window) put everything back. Listed in the ledger as the
single deliberate difference.

## 5. Plan

- [x] `CadSurfaceEdit` services: create (all five kinds), info / rename, style assign, definition add /
      remove / move, point-file link / import, definition rows, analysis mode and bands, style
      rename / add / delete / intervals; ImGui windows call them.
- [x] Qt Create Surface, Surface Properties, Surfaces manager, Surface Style editor.
- [x] Request servicing (flags set by ribbon, commands and Toolspace), ribbon buttons enabled, Toolspace
      Surfaces / Surface Styles right-click menus, Properties-dock Surface section.
- [x] Catch2 `[issue893]` and Qt workflow tests.
- [x] Ledger updated.
- [x] User picked option C in GoSurvey blue; applied; final screenshots approved.

## 6. Differences from 0.7.4 (all deliberate)

- Style editor Cancel restores (above).
- Surfaces manager: reordering a breakline or boundary now takes its undo snapshot **before** the move
  (0.7.4 took it after, so one Undo did nothing visible). One shared function, so ImGui is fixed too.
- Surface Properties ▸ Definition has Move up / Move down. 0.7.4 drew four arrow buttons of which two
  duplicated the other two.
- Disabled placeholders are not drawn (Help, "Create ranges by", "Datum elevation", Preview): REQ-201.
- The Surfaces manager has Add / Remove / Move / Import buttons that follow the tree selection, in addition
  to the right-click menu, so the operations are reachable without a context menu.

## 7. Verification

- `.\build.bat` (both frontends, Release) — PASS.
- `GoSurveyTestImGui.exe` — all test cases pass, including 16 new `[issue893]` cases for `CadSurfaceEdit`; the
  ImGui windows now call the same services and no existing case regressed.
- Qt suite on the hidden desktop — 98 passed, 1 failed. The failure is the existing
  `tabsAppearInitiallyAndTypingRoutesFromEditorChrome` (`tree->hasFocus()`), which also fails on `beta`.
  13 new/extended workflow tests pass (create, grid/volume kinds, request servicing, Properties, Style
  editor live / cancel / OK, Style rename / new / delete, bands and pickers, Surfaces manager, manager
  state notes and file links, ribbon buttons and the Properties-dock section).
- clang-tidy (CI configuration) on the lines this change touches — no findings.
- Screenshots: `build/artifacts/Release/qt-893-final-*.png`; the user approved option C in GoSurvey blue.
- Acceptance: preserve 0.7.4 behavior — met, with the deliberate differences in section 6; usable Qt
  workflow — all four dialogs reachable from ribbon, commands and Toolspace; ledger and focused coverage —
  done; visual direction applied — done.

## 8. Completion report

Implemented as planned. Assumption recorded in section 2 (which surface windows count as "management
dialogs"). Decision taken without asking: the Style editor's Cancel restores (section 4). Technical debt:
Toolspace Prospector definition-folder menus and Zoom to / Pan to are not in the Qt Toolspace (the Surfaces
manager covers the operations).
