# TASK-891 — Complete core Qt geometry properties and context menus

- Type: feature
- Status: complete
- Opened: 2026-10-18
- Owner: Claude

## 1. Authority

- GitHub issue #891 (parent tracker #878). Acceptance: preserve beta 0.7.4 behavior (cancel,
  validation, failure, undo, read-only), a fully usable Qt workflow (a shared route or visible
  ribbon button alone is not parity), an updated parity ledger, focused workflow coverage, and
  2–3 integrated screenshot options for each major affected surface with the user's selection
  applied before the issue is complete.
- Behavior authority is the ImGui `DrawPropertiesPanel` and the `##drawing1_vp_ctx` shortcut menu
  in `CadUi.cpp`, under accepted **REQ-024/REQ-057** (Properties show and edit geometry incl. Z,
  undoable), **REQ-101** (X/Y shown and edited in world coordinates), **REQ-054** (right-click
  selection menu, Select similar), **REQ-084** (Right-click Customization, time-sensitive
  right-click, the shortcut menu, object isolation) and **REQ-201** (no dead controls).

## 2. Scope ("common supported geometry types")

The issue does not list the types. Scope is fixed to what the 0.7.4 Properties panel edits for
core drawing geometry:

- **LINE**, **CIRCLE**, **TEXT / MTEXT**: General (layer, colour, linetype, lineweight,
  transparency) and Geometry sections, editable, one undo step per edit.
- Multi-selection of one type: General editable, Geometry read-only summary (0.7.4 behavior).
- Mixed / other selections: General editable where the type carries attributes, plus the same
  "geometry is read-only here" note. Arc, polyline and ellipse have **no** geometry editor in 0.7.4
  either, so showing none is parity, not a gap.
- Empty selection: the dimmed General / Geometry skeleton.
- Drawing shortcut menu: Command Mode (Enter / Cancel), Default / Edit Mode action menu, REQ-054
  selection items, the Right-click Customization dialog (preferences + time-sensitive right-click),
  and the Selection… and Options… targets.

Out of scope — each has its own tracker issue or is not core geometry: survey-point pick
properties (#902/#904), tables (#898), block references (#895), PDF underlays (#905–#910),
surfaces (#893), pipe runs / solids / section plane / hatch / multileader properties.

## 3. Architectural boundary check

- New abstraction or layer? No new layer. Two small shared pieces in the Commands layer, each with
  two concrete users (ImGui and Qt): the property-edit services (`CadPropertyEdit`) and the
  right-click classifier (`CadClassifyRightClick`). Qt supplies only widgets.
  - [x] No — proceed within existing boundaries.

## 4. Questions

| # | Question | Asked | Answer |
|---|---|---|---|
| Q1 | Which integrated Qt visual direction for the Properties panel? | 2026-10-18 | Option C — navy section banners, X/Y/Z badges. |
| Q2 | Which integrated Qt visual direction for the shortcut menu? | 2026-10-18 | Option C — 0.7.4 order under group captions. |
| Q3 | Which visual direction for the Right-click Customization dialog? | 2026-10-18 | Option B — navy banner with bordered cards. |

## 5. Plan

- [x] `CadPropertyEdit` shared services (general-attribute read, line / circle / text setters, validation, one undo step); `CadApplyTransparencyToSelection`.
- [x] `CadClassifyRightClick` / `CadRightClickIsTimed` shared by ImGui and Qt.
- [x] Qt Properties panel (design C), replacing the stub tree.
- [x] Qt shortcut menu (design C), time-sensitive right-click, Right-click Customization dialog (design B), Selection list, Options... servicing.
- [x] Tests: Catch2 `[issue891]` and Qt workflow tests.
- [x] Ledger updated; screenshots approved by the user.

## 6. Verification

- `.uild.bat imgui` and `.uild.bat qt` — PASS.
- `GoSurveyTestImGui.exe` — all 2476 test cases pass (includes 12 new `[issue891]` cases).
- Qt suite on the hidden desktop — 85 passed, 1 failed: the existing `tabsAppearInitiallyAndTypingRoutesFromEditorChrome` (`tree->hasFocus()`), which also fails on `beta`. 12 new workflow tests pass.
- Screenshots: `build/artifacts/Release/qt-891-final-*.png`; user approved Properties C, menu C, dialog B.
- Architecture: no new layer; Commands-layer services have two users each (ImGui and Qt).

## 7. Completion report

Implemented as planned. Assumption recorded in section 2 (which geometry types count as "core"). Technical debt: the Shift+right-click snap override and annotative-scale rows remain for #903 / #901.
