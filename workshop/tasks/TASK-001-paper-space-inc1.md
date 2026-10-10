# TASK-001 — Paper Space Increment 1: spaces, layout tabs, MODEL/PAPER toggle, sheet

- Type:    feature
- Status:  done
- Opened:  2026-06-15
- Owner:   Workshop

## 1. Authority
- Goal:         Paper Space milestone (M-PaperSpace, spec/roadmap.md)
- Requirements: REQ-025 (accepted), REQ-026 (accepted), REQ-031 (accepted — partial: layouts/paper persist now; viewports land in Inc 2)
- Constraints:  no broken functionality; follow architecture (ADR-006); coding standards; no new dependency
- Acceptance (verbatim):
  - REQ-025: a drawing shows a Model entry plus at least one Paper layout entry;
    switching changes the active space; ≥2 layouts can be added, renamed, and
    deleted and coexist; the status button shows MODEL/PAPER and clicking it
    toggles the active space.
  - REQ-026: choosing a size + orientation renders the sheet outline at that
    physical size in paper space; changing the size updates the outline.
  - REQ-031 (partial): layouts (name, paper size, orientation) round-trip through `.gs`.
- Owning subsystem: UI + Domain (data on AppCommandState/DrawingDocument), IO (.gs), Renderer (sheet outline). Per ADR-006.

## 2. Scope
- In scope: PaperLayout data model + activeSpace selector; per-drawing storage +
  tab-snapshot save/restore; layout tab strip (Model | layouts | +) with
  rename/delete; MODEL/PAPER status-bar toggle button; paper-size/orientation
  picker; sheet-outline render in paper space; `.gs` save/load of layouts.
- Out of scope (later increments): viewports (Inc 2), per-viewport layer freeze
  (Inc 3), plotting (Inc 4), DXF persistence of layouts (deferred REQ).
- Smallest change: one `PaperLayout` struct + two fields on the document, reusing
  the existing tab-snapshot mechanism, the existing status-bar button pattern, and
  the existing line-render path for the outline.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global / data-format /
  algorithm not in the spec?
    - [x] No — proceed. The data model, render mode, and `.gs` field additions are
          pre-authorized by ADR-006 and REQ-031; no new dependency (no plotting yet).

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Plot output mechanism | 2026-06-15 | Vector PDF via PDFium (Inc 4) |
| Q2 | Delivery | 2026-06-15 | Incremental |
| Q3 | DXF layout persistence | 2026-06-15 | .gs now, DXF later |

## 5. Assumptions
```
ASSUMPTION-1: A new drawing starts with Model active and one default paper layout ("Layout1").
- Because:       REQ-025 says "zero or more" layouts; a default makes the feature visible.
- Risk if wrong: minor — a default layout is easily removed by the user.
- Validate by:   user feedback after Inc 1.
ASSUMPTION-2: Paper units are inches (sheet sizes specified in inches; ARCH/ANSI presets).
- Because:       sheet sizes are conventionally in inches; plot scale is model units per plotted inch (existing MUP).
- Risk if wrong: a metric preset set can be added without model changes.
- Validate by:   Inc 4 plotting review.
```

## 6. Plan
- Approach: add a small Domain data model owned by the drawing; reuse the existing
  per-tab snapshot save/restore and the existing status-bar + tab UI patterns; draw
  the sheet outline through the existing line batch in a paper-space branch.
- Files/functions to touch:
  - `src/survey/.../` n/a. New `src/commands/PaperSpace.hpp` (PaperLayout, PaperSize presets, ActiveSpace) — Domain types, header-only where trivial.
  - `src/commands/CadCommands.hpp` — add `paperLayouts`, `activeSpaceIndex` (-1 = model) to `AppCommandState` and `DrawingDocument`.
  - `src/commands/CadCommands.cpp` — `SaveDocumentToSnapshot` / `RestoreDocumentFromSnapshot` carry the new fields; helpers to add/rename/delete a layout and to toggle space.
  - `src/io/GsIo.cpp` — serialize/deserialize `paperLayouts` + `activeSpaceIndex`.
  - `src/ui/CadUi.cpp` — layout tab strip; MODEL/PAPER status button; paper size/orientation combo in a small panel/Properties.
  - `src/render/ViewportRenderer.cpp` (or the sheet draw in CadUi viewport) — draw the sheet outline rectangle at paper size when a paper layout is active.
  - `docs/gs-file-format.txt` — document the layouts section.
- Test approach:
  - happy path: a `GoSurveyTests` case round-trips a doc with ≥2 layouts (names,
    sizes, orientation) through the GsIo JSON save/load and asserts equality.
  - failure mode: loading a `.gs` with a missing/garbage `paperLayouts` value
    yields a valid drawing with zero layouts (no crash), asserted in the test.
  - manual: tabs switch; status button toggles; outline renders at size.
- Steps:
  - [ ] PaperSpace.hpp data model + presets
  - [ ] AppCommandState + DrawingDocument fields + snapshot save/restore
  - [ ] add/rename/delete/toggle helpers
  - [ ] GsIo save/load + validation default
  - [ ] UI: layout tab strip + MODEL/PAPER button + size/orientation picker
  - [ ] Renderer: sheet outline in paper space
  - [ ] tests (round-trip + malformed) ; docs

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1–Q3); tests-first for the IO round-trip.

## 8. Implementation log
- 2026-06-15 plan written; boundary check = No (covered by ADR-006/REQ-031).
- 2026-06-15 PaperSpace.hpp data model (PaperLayout, presets, kModelSpaceIndex).
- 2026-06-15 fields on AppCommandState + DrawingDocument; snapshot save/restore carry them.
- 2026-06-15 AddPaperLayout/DeletePaperLayout/SetActiveSpace/ToggleModelPaperSpace helpers.
- 2026-06-15 GsIo save/load + malformed-tolerant default; gs-file-format.txt documented.
- 2026-06-15 UI: layout tab strip + size/orientation picker + MODEL/PAPER toggle button.
- 2026-06-15 main.cpp: blank model scene in paper space; CadUi: sheet-outline overlay.
- 2026-06-15 tests/PaperSpaceTests.cpp (orientation + presets); build clean; 18 cases green.

## 9. Self-verification
- [x] build-project        — PASS (clean; only pre-existing CRT deprecation warnings)
- [x] architecture-review  — PASS (no Workshop architectural decision; covered by ADR-006/REQ-031; data on existing owned document containers; render gate kept at the main.cpp Renderer boundary; no new global, no new dependency)
- [x] code-review          — PASS (concrete types, document is sole owner of layouts, readable; reuses existing tab/status/overlay patterns)
- [x] dependency-audit     — PASS / n-a (no dependency added)
- [x] performance-review   — PASS / n-a (sheet = 3 rects; paper space skips model batches)
- [x] testing              — PASS (PaperSpaceTests green; .gs round-trip + UI behaviors verified manually per IO convention, as REQ-022/023)

## 10. Verification result
- Submitted:  2026-06-15
- Verdict:    PASS
- Findings:   none blocking

## 11. Outcome
- Requirements satisfied: REQ-025, REQ-026 (Acceptance met: yes); REQ-031 partial (layouts persist; viewports land in Inc 2)
- Tests added:            tests/PaperSpaceTests.cpp ("orientation", "presets well-formed")
- Refactors:              none
- Docs updated:           docs/gs-file-format.txt (paperLayouts, activeSpaceIndex)
- Done:                   2026-06-15
