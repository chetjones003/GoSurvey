# TASK-024 — ADR-023: WYSIWYG MTEXT editing (in-tree rich text edit widget)

- Type:    feature
- Status:  self-verify (built clean; 87/87 unit tests green; awaiting user manual verification)
- Opened:  2026-07-30
- Owner:   chetjones003

## 1. Authority
- Goal:         MTEXT edits the way AutoCAD's in-place editor does — text wraps at the column width, the
                box grows with it, formatting shows as formatting, and the `[[…]]` wire tags never appear.
- Requirements: REQ-051 (revised 2026-07-30 — the in-place box edits WYSIWYG).
- Architecture: **ADR-023** (accepted 2026-07-30) — the offset-carrying span API + the `ui/RichTextEdit`
                module + the visible-index caret publishing raw byte offsets.
- Constraints:  no new dependency; no change to the rich wire format, `CadAnnotation`, `.gs`, DXF, or the
                PDF plot; every existing "Text Formatting" toolbar control must keep working unchanged;
                single-line TEXT keeps its bare `InputText` box.
- Owning subsystem: UI (`ui/RichTextEdit` new; `ui/MtextRichFormat` public API; `ui/CadUi` call site).

## 2. Scope
- In scope:     span parsing with byte offsets; wrapped layout of visible characters; caret + selection on
                visible indices with raw-offset publication; keyboard editing (typing, Backspace, Delete,
                Enter, arrows, Home/End, Ctrl+arrows, Shift-selection, Ctrl+A); mouse (click, drag-select,
                double-click word); clipboard (Ctrl+C/X/V); in-editor undo/redo (Ctrl+Z/Y); styled drawing
                (bold, italic, underline, caps, per-run font incl. SHX, per-run colour); caret blink;
                the ALL-CAPS typing toggle; vertical scroll when the box hits its height cap.
- Out of scope: the disabled toolbar controls (unchanged follow-ups); drag-to-resize box corner; IME and
                right-to-left text; per-run height; multi-column text.
- Smallest change: one new module + a public span API + swapping the widget at the one call site.

## 3. Architectural boundary check  (workflow.md §4)
- [x] **No new decision needed — ADR-023 already records this one.** The new module, the public-API
      addition, and the caret model are all its content. Nothing here goes beyond it: no dependency, no
      global (editor state lives in `AppCommandState` beside the existing editor fields), no data-format
      change, dependencies flow downward (`RichTextEdit` → `MtextRichFormat`/`FontRegistry`/`ShxDraw`).
      Should implementation reveal a need beyond ADR-023's text, that is a fresh SPEC GAP.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Live with no wrap, hard-wrap on commit, custom editor, or fake the growth? | 2026-07-30 | Build the wrapping editor. |
| Q2 | Wrapped raw text (tags visible) or WYSIWYG (tags hidden, formatting rendered)? | 2026-07-30 | WYSIWYG. |

## 5. Assumptions
```
ASSUMPTION-1: The editor draws text at the MTEXT's own on-screen size, floored at a legible minimum.
- Because:       true WYSIWYG means the editing view matches the committed result, and the box is already
                 positioned and column-sized in screen pixels over the MTEXT.
- Risk if wrong: at a far-out zoom the text is small; the floor keeps it usable but then the editing view
                 no longer matches the drawn size exactly.
- Validate by:   user edits an MTEXT while zoomed out.

ASSUMPTION-2: Typed text should inherit the styling of the character to its LEFT.
- Because:       ADR-023(d); it is what every word processor does, and the alternative (inherit from the
                 right) makes typing at the end of a bold run produce unbolded text.
- Risk if wrong: typing at a run boundary picks the unexpected style.
- Validate by:   type at the end of a bold word — the new characters should be bold.

ASSUMPTION-3: An in-editor undo stack of plain buffer snapshots is sufficient.
- Because:       MTEXT bodies are short (a note, a label); a snapshot per edit-group costs nothing, and
                 the drawing-level undo (PushUndoSnapshot) is a separate concern that only fires on commit.
- Risk if wrong: memory growth on a pathologically long MTEXT.
- Validate by:   cap the stack (e.g. 64 entries) and drop the oldest.
```

## 6. Plan
- **Step 1 — `MtextRichFormat` span API.** Add `MtextRichSpan { size_t rawBegin, rawEnd; bool bold,
  italic, underline, caps; bool hasColor; uint32_t color; std::string font; }` and
  `MtextRichBuildSpans(wire, &out)`. Re-express the existing internal `BuildRuns` on top of it so there is
  one parser. No behavior change — the existing draw/measure paths must render identically.
- **Step 2 — `ui/RichTextEdit.hpp` pure layout core.** Header-only, ImGui-free where possible:
  visible-character enumeration over spans (UTF-8 aware), word-wrap break selection, and the
  visible-index ↔ raw-byte-offset mapping. Unit tested.
- **Step 3 — `ui/RichTextEdit.cpp` the widget.** Layout with real font metrics (TTF via `ImFont`, SHX via
  `Shx::MeasureWidthPx`), draw (selection highlight, styled glyphs, underline, caret), input handling, and
  clipboard/undo. Public entry: `RichTextEdit::Draw(id, &buffer, &caret, &anchor, opts) -> changed`.
- **Step 4 — call site.** `DrawMtextRichEditorOverlay` swaps `InputTextMultiline` for it and keeps
  publishing `mtextRichEditorSelStart/End` as **raw** offsets so B/I/U, font, and colour keep working.
  Box height follows the wrapped line count; the ruler drag still sets the column.
- **Test approach.**
  - happy path — spans carry the right byte ranges for a tagged string; visible↔raw mapping round-trips;
    wrapping breaks at the expected word for a known width; caret motion crosses a wrap boundary.
  - failure mode — malformed/unterminated tags do not desynchronise offsets; an empty buffer yields one
    empty line with the caret at 0; a caret or selection index past the end clamps; a word longer than the
    column breaks rather than looping forever; a zero/negative column width yields no wrap points;
    multi-byte UTF-8 is never split mid-character.
- **Steps:**
  - [ ] 1. Span API + confirm existing rendering unchanged.
  - [ ] 2. Pure layout core + tests green.
  - [ ] 3. Widget: draw, then caret/selection, then editing, then clipboard/undo.
  - [ ] 4. Swap the call site; verify every toolbar control still applies to the selection.
  - [ ] 5. Self-verify; user manual pass.

## 7. Workflow-specific notes
- Feature: Q1/Q2 answered before planning. Tests-first for steps 1–2 (the pure core), which is where the
  subtle failure modes live. Step 3 is drawing/input → manual, per the UI convention.
- **Blast radius**: this replaces a working editor. Single-line TEXT is on a separate path and unaffected;
  the wire format and every persistence path are untouched, so a defect here cannot corrupt saved drawings
  beyond what the user types.

## 8. Implementation log
- 2026-07-30 — opened after ADR-023 was recorded. Authority complete, boundary check clean.
- 2026-07-30 — **step 1**: the parser moved to a new pure header `ui/MtextRichSpans.hpp` (no ImGui, no
  fonts) carrying `MtextRichSpan` with byte offsets. `MtextRichFormat.cpp`'s `BuildRuns` is now a thin
  adapter over it, so there is one parser. Deviation from the plan: the header had to be **standalone**
  rather than an addition to `MtextRichFormat.hpp`, because that header includes `imgui.h` and the test
  target cannot link ImGui — a pure header was the only way to make the parser testable.
- 2026-07-30 — **step 2**: `ui/RichTextLayout.hpp` (pure) + `tests/RichTextLayoutTests.cpp` (13 cases).
  87/87 green. One failure on the way: a test whose *name* contained an em-dash made ctest's name filter
  fail to match it — the assertions were fine; the name is now ASCII.
- 2026-07-30 — **step 3**: `ui/RichTextEdit.{hpp,cpp}` — measure/wrap, mouse (click, drag-select,
  double-click word, shift-click), keys (arrows incl. up/down by x, Home/End, Ctrl+A, Backspace, Delete,
  Enter), clipboard (copy/cut emit **plain** text; paste collapses `[[` so pasted text can never inject a
  tag), a 64-deep in-editor undo/redo, caret blink, and scroll-to-caret when the text outgrows the cap.
- 2026-07-30 — **step 4**: `DrawMtextRichEditorOverlay` swaps the widget in; the old
  `MtextRichEditorInputCallback` is deleted (the widget publishes the offsets and applies ALL-CAPS
  itself). Editor state added to `AppCommandState` as plain members — deliberately **not** a struct from
  `ui/`, which would have made the widget a dependency of the commands layer.
- 2026-07-30 — build clean (no new warnings), 87/87 green, app launches.
- 2026-07-30 — **user-reported defect: the whole application became unclickable while the editor was
  open** (no buttons, no dragging, not even the window close box).
  - Root cause: the widget called `ImGui::SetActiveID(id, window)` on open and on click and never
    released it. ImGui's `ItemHoverable` rejects any item when `g.ActiveId` belongs to a different item,
    so holding ActiveID across frames disables hovering — and therefore clicking — application-wide.
    Stock `InputText` avoids this by explicitly releasing ActiveID when a click lands outside it; the new
    widget had no such release.
  - Fix: focus is now the widget's own `mtextEditFocused` flag, which touches none of ImGui's hover
    machinery. ActiveID is borrowed **only for the duration of a drag-select** and cleared on mouse
    release — during a drag, blocking other items is the correct behavior. Key handling additionally
    yields whenever another widget is genuinely active (`activeId == 0 || activeId == id`), so a
    momentary toolbar press returns the keyboard to the editor on release, which is what lets the user
    click B and keep typing.
  - Lesson recorded: never hold ImGui's ActiveID across frames from a custom widget.

## 9. Self-verification
- [x] build-project        — PASS (clean; the only warnings remain the pre-existing `strncpy` ones in
                             `PdfAttachDialog.cpp`. App launches and stays up.)
- [x] architecture-review  — PASS (ADR-023 recorded before code and not exceeded; no new dependency; no
                             new global — editor state sits beside the existing editor fields in
                             `AppCommandState`; no wire-format or stored-field change; layering respected
                             — the pure parser/layout headers depend on nothing, `RichTextEdit` depends
                             downward on `commands`/`FontRegistry`, and the commands layer gained no
                             dependency on `ui/`)
- [x] code-review          — PASS (every index clamped before use; the buffer is re-laid-out after any
                             edit so the draw pass never uses stale cells; selection erase re-derives the
                             insertion offset from the fresh buffer rather than a stale one; paste is
                             sanitised; UTF-8 is never split; undo depth capped at 64)
- [x] dependency-audit     — n-a (no dependency added; rejected vendoring a third-party editor in ADR-023)
- [x] performance-review   — the layout runs per frame **only while an MTEXT is being edited**; span-font
                             resolution is cached per span rather than per character. An MTEXT body is a
                             note or a label, so this is nowhere near a hot path. Re-measure if a
                             pathologically long MTEXT ever stutters.
- [x] testing              — PASS (87/87 ctest green; +13 `RichTextLayoutTests` covering the happy path
                             and the failure modes named in §6: malformed/unterminated tags keep offsets
                             contiguous, multi-byte UTF-8 is never split, an over-long word breaks, a
                             glyph wider than the column does not loop, empty text is one line,
                             out-of-range caret/selection clamp, and an empty buffer indexes nothing).
                             Drawing and input are ImGui → **manual**, per the UI convention.

## 10. Verification result
- Submitted:  2026-07-30
- Verdict:    PASS pending user manual verification.
- Findings:   none blocking. Two plan deviations recorded in §8 (standalone pure header; state as plain
              members), both to preserve testability and layering rather than to cut scope.

## 11. Outcome
- Requirements satisfied: REQ-051 as revised (WYSIWYG in-place editing), under ADR-023.
- Tests added:            `tests/RichTextLayoutTests.cpp` (13 cases).
- Docs updated:           `spec/architecture.md` (ADR-023 + module list), `spec/project.md` (decision log),
                          `spec/requirements.md` (REQ-051 statement + revisions).
- Technical debt:         SHX (`.shx`) families fall back to the UI font **in the editor**, matching what
                          `MtextRichFormat`'s rich draw path already does — neither renders SHX strokes for
                          rich runs. Removal condition: teach both paths `Shx::DrawText` in one change, so
                          the editor and the drawn MTEXT never diverge.
                          No IME or right-to-left support (the stock widget had none either).
- Done:                   pending user manual verification.

### Manual verification (user)
1. Double-click an MTEXT: no `[[b]]` tags anywhere — bold shows as bold, colours as colours.
2. Type past the right edge: the text wraps at the column and the box grows a line. Keep typing → keeps
   growing. Drag the ruler's right marker → the text reflows to the new column.
3. Select a word by double-clicking; drag to select; shift-click to extend; Ctrl+A selects all.
4. With characters selected, click B / the font picker / the colour swatch → applies to exactly those
   characters (this is the ADR-023 raw-offset bridge doing its job).
5. Arrows, Home/End, Backspace, Delete, Enter behave as in any text box; Ctrl+Z/Ctrl+Y undo/redo inside
   the editor; Ctrl+C/X/V copy, cut, paste.
6. Type at the end of a bold word → the new characters are bold (ASSUMPTION-2).
7. OK commits and Esc cancels; reopen the MTEXT → the text and its formatting round-trip.
8. Save/reload `.gs`, export DXF, plot a PDF → all unchanged.
