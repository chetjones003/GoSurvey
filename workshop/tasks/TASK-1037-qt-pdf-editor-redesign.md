# TASK-1037 - Redesign the Qt PDF editor and complete zoom/pan navigation

- Type:    feature
- Status:  implementation complete; review fixes applied, awaiting re-review (2026-10-25)
- Opened:  2026-10-22
- Owner:   Claude

## 1. Authority
- Goal:         No numbered GOAL is defined in `spec/project.md`; project direction is Qt parity and usability (REQ-427 family).
- Requirements: REQ-455 (new, accepted by D-2026-10-23-e, directed by the issue), REQ-387 clause 2 (amended), REQ-388, REQ-429, REQ-430, REQ-433 (unchanged semantics)
- Constraints:  REQ-387 clauses 4-5 (off-UI-thread rendering, bounded cache), REQ-201 (honest failure), REQ-300 (no new dependency)
- Acceptance:  REQ-455 acceptance 1-16 (command tabs and side panel, continuous layout and page sync, wheel and Ctrl+wheel zoom, middle-button and Browse pan, fit/typed zoom, keyboard pages, tools on later pages, bounded cache, screenshots, old tests still pass, hotkeys, Callout, Revision cloud, box select, compare panel, previews, Accept/Deny all, top-level window ownership)
- Owning subsystem: Qt UI (`src/qt/PdfWindow`, `src/qt/PdfCanvas.hpp`, `src/qt/PdfScaleUi.cpp`); the shared `src/pdf` layer gains only `PdfDocument::RenderRegion` and the Callout / Revision cloud annotation kinds.

## 2. Scope
- In scope: four command tabs replacing the stacked toolbars; page-by-page and continuous layouts; wheel scroll, Ctrl+wheel pointer-anchored zoom, middle-button pan; fit width / fit page / typed percentage; Page Up/Down/Home/End; page, thumbnail and scale readout following the scroll; tools, highlights and snap working on every visible page; bounded off-thread page cache; spec update.
- Out of scope: persisting the chosen layout, changing the semantics of existing annotation/scale/search/compare behaviour, the ImGui viewer. Added at the user's direction during review (REQ-455 clauses 10-17): hotkeys, Callout, Revision cloud, box select, compare side panel, top-level window.
- Related prior fix: `EditorViewport::paintGL` runs the pending viewport zoom so ZOOM EXTENTS / ZOOM WINDOW after Accept all take effect. It is already present on `beta` via PR #1098 and is not part of this branch's delta.
- Smallest change: the existing canvas stays; "the page" becomes a per-input/per-paint context (`ActivePage`, `PageScope`) so each existing overlay and tool paints and acts on one page's own pixels; one extra worker (`m_StripWork`) fills a bounded image cache for the continuous layout.

## 3. Architectural boundary check
- Does this need a NEW abstraction / layer / dependency / ownership change / global state / public-API or data-format change / algorithm the spec didn't specify?
    - [x] No - proceed.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| 1 | Default layout: page-by-page or continuous? | 2026-10-22 | Not specified by the issue; kept page-by-page (existing behaviour, existing tests) - recorded in D-2026-10-23-e. |

## 5. Assumptions
- 100% zoom means 96 dpi (page width in points x 96/72 pixels); the old zoom number was only a multiple of fit-width.
- The chosen layout is per window and not saved.

## 6. Plan
- Approach: see section 2. The ribbon (`PdfRibbon.hpp`, the main window's ribbon look) sits above the page area, which stays the central widget; a left side panel holds Thumbnails, Tool Chest, Properties and Compare; action names, shortcuts and tooltips are unchanged.
- Files: `src/qt/PdfWindow.cpp/.hpp`, `src/qt/PdfRibbon.hpp`, `src/qt/PdfCanvas.hpp`, `src/qt/PdfScaleUi.cpp`, `src/qt/EditorWindow.cpp/.hpp` (top-level window, coordinate accept), `src/qt/EditorViewport.cpp`, `src/pdf/PdfDocument.*` (`RenderRegion`), `src/pdf/PdfAnnotate.*` (Callout, Revision cloud), `resources/icons/chevron_down.png`, `tests/QtEditorTests.cpp`, `spec/requirements.md`, `spec/project.md`.
- Test approach: `issue1037*` Qt tests (tabs, continuous sync, Ctrl+wheel anchor in both layouts, middle pan with tools, fit/typed/keyboard, second-page annotation, cache bound + close-while-rendering) plus the existing `pdf*`/`issue905*`/`issue906*` suites.

## 11. Outcome
- Done: user-approved after live testing. Beyond the original scope (directed by the user during review): side panel tabs, tool hotkeys, Callout and Revision cloud tools, box select, Compare as a side-panel tab with sharp zoomed overlays (REQ-455 clauses 10-15).
- Known limits: box-selected marks cannot be moved together; Audit scale is still a dialog.

## 12. Review follow-up (PR #1099)
- Authority: REQ-455 clauses 1, 5-7, 9-17 and Acceptance 1-15; REQ-387 clauses 2, 4-5;
  REQ-388, REQ-396, REQ-399 and REQ-201. The existing accepted decision D-2026-10-23-e governs.
- Findings to resolve: rotated-page revision-cloud bounds; preserve the revision detail image if
  detail-mark generation fails; remove remaining numeric tool/mode indexes and unused tool ids;
  consolidate annotation selection reset; make the tooltip match the two-click default cloud mode;
  exercise hotkeys and selection actions, rotated-page clouds, window ownership, page scroll-to-top,
  later-page search/snap, and coordinate-extraction drag isolation; identify whether narrow ribbon
  collapse is truly exercised and keep the status bar usable at narrow widths; name the exact compare and coordinate-review tests in Acceptance;
  record the screenshots already approved; document the existing Zoom Extents correction and the
  early-stop rule for Accept all; state the existing Audit scale dialog limitation; complete the
  cloud save/read-back and drawing-order coverage.
- User decision resolved 2026-10-25: preserve the implementation hotkeys (Callout C, Length D, Angle G,
  Check H; Area A), now stated consistently in REQ-455 clause 10 and D-2026-10-23-e. No decision remains open.
- Plan: fix the demonstrated code defects; add regression coverage for corrected paths and the reported
  acceptance gaps; update REQ-455 acceptance/traceability and this record to name the tested behavior precisely.
- Architectural boundary: existing Qt/PDF and test seams only; no new dependency, layer or data format.
- Verification: Release build passed; all six Qt editor shards passed; full Release CTest passed (191/191);
  `git diff --check` passed. Review screenshots for the cloud button and ribbon overflow are refreshed and
  await user approval before the task/PR can be closed.
- Changes in progress: rotated cloud bounds are rebuilt from transformed corners; failed optional
  compare-detail marks retain the sharp revision bitmap while cancellation discards stale pixels;
  undo/redo/document replacement share `ClearAnnotationSelection`; Compare mode data is enum-backed;
  comparison pick counts and PDF tool ids are named; pixel tolerances now state DIPs and live together;
  the narrow status bar moves First, Last and Fit Page into More, shortens the active-tool label, and
  keeps page and zoom fields separated;
  the default cloud tooltip describes the two-click rectangle; hotkey tests cover Check/Area/Angle;
  additional tests cover rotated cloud round-trip, one-click selection, bulk style/delete, coordinate
  extraction during pan, later-page search/snap, Accept all early stop, top-level window ownership,
  drawing-order callout/cloud round-trips, and failed compare-detail marks.
- Scope correction: the Zoom Extents correction is already present on `beta` via PR #1098 and is not
  part of the current feature-branch delta; it remains documented as prior work rather than as an
  incidental change in PR #1099.
- Review resolution: all listed code and acceptance-evidence findings are addressed. The narrow-ribbon
  test now inspects the overflow menu for Rectangle and Color actions. Build and test verification and
  refreshed screenshots are complete; approval of the revised visual evidence remains before closure.
- Verification complete 2026-10-25: `build.bat`; full Release CTest (191/191); final screenshot shards 3
  and 5 passed after adding the narrow-status, overflow, and cloud captures. The refreshed visual review
  includes the corrected 560 px status bar; `git diff --check` reports no whitespace errors.
- Code-review fixes 2026-10-25: removed the unrelated extra viewport zoom call; compare-detail wait failures
  are logged; revision-cloud corner bounds share one helper; render regions use grouped pixel size and
  rectangle values. The Release build passed after these fixes;
  the 191-test suite above predates this review-fix pass.
- Refactor 2026-10-25: moved the comparison workflow methods and detail-rendering helpers into
  `PdfWindowCompare.cpp`; behavior and ownership stay on `PdfWindow`.
- CI follow-up 2026-10-25: `full / build and test` failed only in Qt shards 4 and 5. The attached
  test logs show the hosted desktop clamps an 860 px window to 749 px; two thumbnail assertions
  assumed four rows were visible instead of checking the viewport. The cache test's `<10` entry
  assertion also exceeded REQ-455 clause 8: the cache is bounded by bytes, and visible pages are
  retained. The CI log shows its per-page off-screen byte-cap assertions passed. The three failed
  tests pass together on this worktree's hidden desktop. Follow-up change is test-only: assert the
  unreadable row is visible, wait for all rows actually visible in the screenshot test, and remove
  the unsupported entry-count assertion. No viewer behavior or SPEC changes.
- CI follow-up verification: `.\build.bat` passed. The three affected Qt tests passed in one hidden-
  desktop run (5 total QtTest cases including setup/cleanup); no production code changed.
- CI follow-up (PR #1101, 2026-10-25): `full / build and test` failed repeatedly on hosted Windows in
  `issue1037ContinuousLayoutAnnotatesTheSecondPage`, at the red-pixel assertion. The standalone test
  and all six Qt shards passed locally. Neither switching from a full-canvas capture to a viewport
  capture nor explicitly repainting and retrying fixed the hosted failure. The CI artifact omits the
  PNG, so its pixels cannot be inspected there. Rendering through `PdfWindow::PaintStrip` into an
  off-screen image reproduced the failure locally and exposed the cause: at 1x, the thin red outline is
  antialiased against white (for example, saved PNG pixels are 255,124,124), outside the test's strict
  ±40 pure-red threshold. This is a test tolerance issue, not a viewer defect.
  - Authority: REQ-455 clause 7 and Acceptance 7 (annotation is drawn on its owning continuous-layout
    page only); REQ-433 high-DPI clause 3.
  - Plan: keep the deterministic `PaintStrip` off-screen render and classify red-dominant pixels with
    enough tolerance for antialiasing against white. Inspect each `PageRect` and assert red appears
    only on page 1. Keep the change test-only; no viewer behavior or SPEC change.
  - Files: `tests/QtEditorTests.cpp` and this task record.
  - Verification: Release build; the focused Qt test; shard 3; all six Qt editor shards in parallel;
    `git diff --check`.
  - Architectural boundary: existing widget/test seams only; no new dependency, production API,
    layer, or data format.
  - First viewport iteration: `build.bat`, the focused test, shard 3, all six Qt shards, and
    `git diff --check` passed locally. This viewport-only implementation was later superseded after
    hosted CI still failed; no production code or SPEC changed.
  - Verification result: `build.bat` passed; the focused test passed ten consecutive 1x runs; all six
    Qt editor shards passed together (6/6); `git diff --check` passed. The antialias-aware predicate
    detects the red stroke and still rejects red pixels on pages 0 and 2. No production code or SPEC changed.
