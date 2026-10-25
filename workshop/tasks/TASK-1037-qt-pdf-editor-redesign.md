# TASK-1037 - Redesign the Qt PDF editor and complete zoom/pan navigation

- Type:    feature
- Status:  implementation complete; awaiting updated visual approval (2026-10-25)
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
