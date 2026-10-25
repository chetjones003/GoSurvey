# TASK-1037 - Redesign the Qt PDF editor and complete zoom/pan navigation

- Type:    feature
- Status:  done (user approved UI 2026-10-25)
- Opened:  2026-10-22
- Owner:   Claude

## 1. Authority
- Goal:         No numbered GOAL is defined in `spec/project.md`; project direction is Qt parity and usability (REQ-427 family).
- Requirements: REQ-455 (new, accepted by D-2026-10-23-e, directed by the issue), REQ-387 clause 2 (amended), REQ-388, REQ-429, REQ-430, REQ-433 (unchanged semantics)
- Constraints:  REQ-387 clauses 4-5 (off-UI-thread rendering, bounded cache), REQ-201 (honest failure), REQ-300 (no new dependency)
- Acceptance:  REQ-455 acceptance 1-15 (command tabs and side panel, continuous layout and page sync, wheel and Ctrl+wheel zoom, middle-button and Browse pan, fit/typed zoom, keyboard pages, tools on later pages, bounded cache, screenshots, old tests still pass, hotkeys, Callout, Revision cloud, box select, compare panel, previews, Accept/Deny all)
- Owning subsystem: Qt UI (`src/qt/PdfWindow`, `src/qt/PdfCanvas.hpp`, `src/qt/PdfScaleUi.cpp`); the shared `src/pdf` layer gains only `PdfDocument::RenderRegion` and the Callout / Revision cloud annotation kinds.

## 2. Scope
- In scope: four command tabs replacing the stacked toolbars; page-by-page and continuous layouts; wheel scroll, Ctrl+wheel pointer-anchored zoom, middle-button pan; fit width / fit page / typed percentage; Page Up/Down/Home/End; page, thumbnail and scale readout following the scroll; tools, highlights and snap working on every visible page; bounded off-thread page cache; spec update.
- Out of scope: persisting the chosen layout, changing the semantics of existing annotation/scale/search/compare behaviour, the ImGui viewer. Added at the user's direction during review (REQ-455 clauses 10-17): hotkeys, Callout, Revision cloud, box select, compare side panel, top-level window.
- Incidental fix (not REQ-455): `EditorViewport::paintGL` runs the pending viewport zoom so ZOOM EXTENTS / ZOOM WINDOW after Accept all take effect; a defect already in `beta`, kept as its own hunk.
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
