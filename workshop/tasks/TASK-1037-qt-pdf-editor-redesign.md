# TASK-1037 - Redesign the Qt PDF editor and complete zoom/pan navigation

- Type:    feature
- Status:  self-verify
- Opened:  2026-10-22
- Owner:   Claude

## 1. Authority
- Goal:         No numbered GOAL is defined in `spec/project.md`; project direction is Qt parity and usability (REQ-427 family).
- Requirements: REQ-455 (new, accepted by D-2026-10-23-e, directed by the issue), REQ-387 clause 2 (amended), REQ-388, REQ-429, REQ-430, REQ-433 (unchanged semantics)
- Constraints:  REQ-387 clauses 4-5 (off-UI-thread rendering, bounded cache), REQ-201 (honest failure), REQ-300 (no new dependency)
- Acceptance:  REQ-455 acceptance 1-10 (command tabs, continuous layout and page sync, wheel and Ctrl+wheel zoom, middle-button pan with tools active, fit/typed zoom, keyboard pages, tools on later pages, bounded cache, screenshots, old tests still pass)
- Owning subsystem: Qt UI (`src/qt/PdfWindow`, `src/qt/PdfCanvas.hpp`, `src/qt/PdfScaleUi.cpp`); the shared `src/pdf/PdfDocument` is unchanged.

## 2. Scope
- In scope: four command tabs replacing the stacked toolbars; page-by-page and continuous layouts; wheel scroll, Ctrl+wheel pointer-anchored zoom, middle-button pan; fit width / fit page / typed percentage; Page Up/Down/Home/End; page, thumbnail and scale readout following the scroll; tools, highlights and snap working on every visible page; bounded off-thread page cache; spec update.
- Out of scope: persisting the chosen layout, changing annotation/scale/search/compare semantics, the ImGui viewer, `PdfDocument`.
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
- Approach: see section 2. Tabs are one `QToolBar` hosting a `QTabWidget`, so the page area stays the central widget; each tab holds ordinary `QToolBar` rows so action names, shortcuts and styling are unchanged.
- Files: `src/qt/PdfWindow.cpp/.hpp`, `src/qt/PdfCanvas.hpp`, `src/qt/PdfScaleUi.cpp`, `tests/QtEditorTests.cpp`, `spec/requirements.md`, `spec/project.md`.
- Test approach: `issue1037*` Qt tests (tabs, continuous sync, Ctrl+wheel anchor in both layouts, middle pan with tools, fit/typed/keyboard, second-page annotation, cache bound + close-while-rendering) plus the existing `pdf*`/`issue905*`/`issue906*` suites.

## 11. Outcome
- Done: pending verification and user visual approval
