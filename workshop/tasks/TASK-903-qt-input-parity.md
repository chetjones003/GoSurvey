# TASK-903 — Qt UCS-aware constraints, shortcuts and transient tools

## Scope and decision

GitHub #903 requests the beta 0.7.4 drafting input workflows in Qt, with cancellation,
validation, failure, undo and read-only behavior preserved. This was a SPEC GAP because
REQ-154 excluded POLAR while the shared ImGui frontend already shipped it. The user resolved
the gap with **Full Qt port** on 2026-10-19; D-2026-10-19-a accepts REQ-428. Scope is existing
behavior only: no new snap-tracking or customizable shortcut features.

## Implementation

- Qt status controls expose ORTHO/F8, POLAR/F10, OSNAP/F3 and 3D OSNAP/F4.
- Cursor prompt fields consume shared `CadDynInput` state for live values, Tab, locks, relative
  coordinates, keyboard submission and cancellation.
- Viewport picks remain routed through shared `SubmitViewportPick`; shared constraints preserve
  UCS geometry and snapping. The viewport renders POLAR/pipe guidance.
- POLAR settings validate and persist through existing preferences. Shift+right-click provides
  a one-shot snap menu with reference-dependent Perpendicular availability.
- F1 opens the bundled manual and selects contextual command help.
- Focused Qt workflow tests cover typed relative LINE input, toggles, Escape, undo, malformed
  input, polar settings persistence/cancel, snap menu availability, and rotated-UCS ORTHO/POLAR.

## Verification

- `build.bat qt` — passed (Release).
- Hidden-desktop focused tests: `issue903DraftingModesAndCursorFields`,
  `issue903PolarSettingsAndOneShotSnap`, `issue903SnapOverrideAppliesToViewportPick`,
  `issue903OrthoAndPolarConstrainInActiveUcs`, and `issue903VisualOptions` — **7 passed, 0 failed**.
- `git diff --check` — passed.
- Known coverage gap: these focused tests do not directly exercise read-only project editing;
  the command layer retains the existing shared read-only rules. One-shot snap is now verified
  by a real midpoint geometry pick with persistent midpoint OSNAP disabled, and the polar dialog
  covers a failed preferences save while retaining the old values. The full
  `ctest --preset ninja-release -R "^qt[.]editor$" --output-on-failure` run timed out after
  280 seconds. Before timeout it reported failures in `customTitleBarAndFloatingCommandLineWork`
  (href hint), `tabsAppearInitiallyAndTypingRoutesFromEditorChrome` (focus), and two viewport
  renderer readiness tests. The timeout prevents claiming a complete full-suite result.

## Visual review

After user feedback that the boxed cursor pane looked wrong, three frameless cursor-adjacent
options were captured. User selected and approved A, which uses a transparent prompt area, compact
dark value fields and subtle lower rules without an enclosing panel. This styling is applied.
Final Qt captures show first-point X/Y entry, subsequent distance/angle entry and active value
editing; full-screen and readable viewport crops are under `build/artifacts/issue903/dynamic-final-*.png`.
User selected A for POLAR settings and temporary snap menu as well: compact native settings and
compact menu rows. Selected-state captures include normal, validation error, save failure, and
snap menu with/without a perpendicular reference under `build/artifacts/issue903/*-final*.png`
and `polar-settings-*.png`. The user then requested the deep navy treatment from `origin/beta`.
After the first revision still read nearly black in the rendered menu, feature fills were brightened
to clearly blue navy. The user approved the revised appearance. Fresh captures are under
`build/artifacts/issue903/blue-navy-v2-*.png`; screenshots and logs remain local build artifacts.
