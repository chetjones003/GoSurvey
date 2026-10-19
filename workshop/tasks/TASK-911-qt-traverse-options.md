# TASK-911 — Qt traverse unit labels, ground-to-grid scale, and curvature/refraction controls

- Type: feature
- Status: implemented; visual direction approved (Option A); PR open
- Opened: 2026-10-19
- Owner: Workshop
- GitHub: #911, child of #878

## 1. Authority

- Goal: Qt migration to beta 0.7.4 parity.
- Requirements: REQ-401 (ground-to-grid scale factor in the Traverse Editor; its `[req401]` acceptance applies to the Qt editor), REQ-201 (visible validation/failure), REQ-015 (the adjustment runs on the scaled distances). Unit labels follow issue #831/#832 (active drawing's linear unit, `CadDrawingInsUnitsAbbrev`); curvature/refraction follows issue #826 (`TraverseData::applyCurvRefCorr` / `refractionCoeff`, domain already implemented). No REQ number covers #826/#831/#832; the 0.7.4 ImGui behavior and the domain tests are the baseline, so no SPEC change is made.
- Issue #911 acceptance: preserve 0.7.4 behavior (cancel, validation, failure, undo, read-only where applicable); the Qt workflow is fully usable (a shared route or button alone is not parity); update the parity ledger and add focused workflow coverage; present 2–3 screenshot options for the affected surface and apply the user's selection before completion.

## 2. Scope

- Qt `AdjustmentWindow` gains an options bar: the active drawing's unit label, a curvature + refraction checkbox with the refraction `k` field and out-of-range warning, and a Scale Factor checkbox with a Combined / Grid only selector and a live `k = …` readout or reason.
- The scale option is the command text's `#SCALE` line; the bar and the text stay in step in both directions. Curvature/refraction is a bar setting that survives re-parsing the text.
- The resolved factor (`ResolveTraverseScaleFactor`) is applied before Run and Save; an unresolvable factor is reported and the traverse computes with factor 1.0 (REQ-401 §5).
- Unit labels replace "drawing units"/bare headings in the Settings dialog and result tables.
- Out of scope: persisting curvature/refraction in `.gstrav` (not persisted in 0.7.4 either), a Table View tab, any domain change.

## 3. Boundary check

- UI only (`src/qt/`). Domain (`TraverseCalc`) and command resolver (`ResolveTraverseScaleFactor`) are reused unchanged. No new dependency, layer, or abstraction.

## 4. Test approach

- Focused QtTest workflows: bar reflects drawing unit; default-off scale leaves results identical; scale on + Combined/Grid scales distances by the resolved k and differ when sea-level is on; no coordinate system reports the reason and computes unscaled; bar ↔ `#SCALE` text sync; curvature toggle/k change alters elevations, survives text edits, invalidates a stale result; k outside −2..+2 warns.

## 5. Verification
- User chose **Option A** (full-width options bar under the toolbar) from three integrated options on 2026-10-19.
- `.\build.bat qt` — PASS.
- Focused hidden-desktop workflows — PASS: the 7 `issue911*` workflow tests (units, default-off scale, Combined/Grid-only with text sync, unresolvable factor, save/load, curvature/refraction, Settings labels) plus the screenshot capture.
- Full Qt suite, run per test because one crashing test aborts `ctest -R qt.editor` at that point: every test other than the crash below ran and all passed except four, none in code this change touches. Crash: `approvedTopographyStaysInPanelBackgrounds` (access violation, crashes alone, constructs `EditorWindow` only). Failures: `customTitleBarAndFloatingCommandLineWork` (hint text), `issue903PolarSettingsAndOneShotSnap` (artifact folder `artifacts/issue903` missing next to the exe), `tabsAppearInitiallyAndTypingRoutesFromEditorChrome` (known focus assertion), `appearanceChangesKeepDrawingAndToolspaceState` (theme colour). Not re-run on an unmodified `beta` build, so "pre-existing" is not proven.
- Screenshots: `build/artifacts/Release/qt-911-*.png` (not committed).
- Technical debt: curvature/refraction is not stored in `.gstrav` (same as 0.7.4).
