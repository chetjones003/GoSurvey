# Start and drawing tab trapezoids

Authority: user direction 2026-10-30, REQ-055, REQ-308, REQ-436.

The user wants visible trapezoid outlines on Start and every drawing tab, separation from the
strip behind them, and a clear distinction between drawings. The existing native QTabBar is
rectangular; the previous custom implementation was removed and only an unfinished header
remained. Drawing-tab styles also set rectangular fills and native close-button overrides.

Plan: finish the concrete DrawingDocumentTabBar using Qt's layout, scrolling, navigation and
accessible close buttons. Paint each trapezoid and label exactly once, leave visible gaps,
use graphite inactive fills and a lighter active fill plus the existing amber indicator.
Keep ribbon tabs unaffected. Add matching Light theme treatment. Record the user's explicit
shape direction in REQ-436. No data, commands, dependencies or ownership boundaries change.

Plan review: local Qt presentation only, supported by the user's explicit request. Main risks:
duplicate chrome, clipped labels/close controls, overflow and losing the unclosable Start tab.
Capture real widgets in Dark and Light with Start/drawings active, hover, long names and
overflow. Exercise selection, close buttons after removal, keyboard navigation and new drawing.
Build with this worktree's build.bat. Compare paired Civil 3D captures through the required
three-agent panel if the product can be launched; record any limitation honestly.

## Implementation and verification

- `DrawingDocumentTabBar.cpp/.hpp`: one painter for the sloped shapes and labels; Qt retains layout,
  keyboard navigation, scrolling and accessible close buttons. Clicks outside the polygon do not
  select a tab. Close-button callbacks resolve their current index after other tabs are removed.
- `EditorWindow.cpp`, `GoSurveyQt.cmake`: install and build the completed widget; use its 32px height.
- `QtAppearance.cpp`: remove conflicting rectangular fills/native close overrides, provide neutral
  close-button states in both appearances. During screenshot inspection, transparent widget palette
  inheritance selected Dark colors in Light; fixed by using the existing application theme property.
- `spec/requirements.md`, `spec/project.md`: record the user's explicit shape direction under
  REQ-436 and D-2026-10-30-tabs.
- `QtEditorTests.cpp`: `documentTabTrapezoidsNavigationCloseAndCaptures` checks shape-corner misses,
  click/keyboard selection, the unclosable Start tab, current-index close callbacks, Light/Dark fills,
  long names and overflow; captures both themes and meaningful tab states on a hidden desktop.

Release build: PASS (`.\build.bat`). Existing environment messages for vswhere and optional Vulkan
headers remain; no new compiler warnings. Focused Qt run: PASS, 12.481s, 3 QtTest lifecycle/function
entries passed, 0 failed. Build output-layout check: PASS. The full suite was not rerun for this tab
presentation change. Logs: `build/artifacts/document-tabs-tests.log`.

Self-review: code/architecture/dependency checks PASS for this change. It stays inside Qt presentation,
uses Qt parent ownership, adds no dependency or persistence change, and does not change the renderer's
frame path. No performance improvement or benchmark result is claimed. `git diff --check`: PASS.

Captures and independent score sheets: `build/artifacts/Release/document-tabs/`.
Three-reviewer Dark paired comparison: PASS, median scores **1, 2, 1, 2, 2**, no blockers. Civil
reference is the real earlier capture from this task (2026-10-29), not a fresh product run. Physical
window heights and dirty marker differ. Light/overflow/Start-active/hover/focus have no matching
Civil captures, so their paired comparison remains **inconclusive**; only GoSurvey readability was
reviewed for those states. Hover/focus crops alone do not strongly prove the additional feedback.
See `COMPARISON-RECORD.md` in that artifact directory for the scope and limitations.

Status: implemented and visually reviewed; the user approved proceeding to PR on 2026-10-30.
Included in commit 3480e331; see TASK-078-pr-preparation.md for final beta integration and the passing
191-check suite. Broader authorized changes in this worktree are included in the same PR.
