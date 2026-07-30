# TASK-025 — REQ-051: Ctrl+Enter places, MTEXT zoom-scaling fix, and the Options menu

- Type:    feature + fix
- Status:  self-verify (built clean; 98/98 unit tests green; awaiting user manual verification)
- Opened:  2026-07-30
- Owner:   chetjones003

## 1. Authority
- Goal:         finish the AutoCAD-parity MTEXT editor — commit shortcut, correct zoom behaviour, and the
                Options menu from the user's reference screenshot.
- Requirements: REQ-051 (amended 2026-07-30, third review); REQ-050 (the zoom item is a **defect against
                it**: "like any model entity it still scales on screen with zoom").
- Constraints:  no new dependency/abstraction/global; no wire-format or stored-field change; the wire must
                stay well-formed through every new text operation.
- Decision:     recorded 2026-07-30 in `spec/project.md`.
- Owning subsystem: UI (`CadUi` panel + `RichTextEdit`), IO (`UserPrefs`) for the new persisted toggle.

## 2. The three items
1. **Ctrl+Enter places the text.** It previously re-normalised the buffer — redundant, since
   `CommitMtextRichEditor` normalises on the way out. Plain Enter still breaks the line.
2. **MTEXT stopped scaling when zoomed in.** Root cause: `viewportMtextMaxPx` (128) clamped the computed
   font size for *plain* MTEXT, so past that zoom the text stayed 128 px while the geometry kept growing —
   exactly the mismatch in the user's two screenshots. That cap exists for **survey-point labels**, which
   are sized for legibility rather than to scale. Fix: apply it only to survey labels; plain MTEXT (model
   *and* paper) keeps a large rasterisation sanity bound instead.
3. **Options menu** on the panel's chevron, matching the reference screenshot.

## 3. Architectural boundary check
- [x] No — proceed. Same containment as the rest of REQ-051. The new text operations live in a pure
      header (`ui/MtextTextOps.hpp`) with no ImGui and no fonts, so they are unit-tested; nothing new is
      abstracted, no dependency is added, and the only new persisted field follows the existing pattern.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Which Options-menu items must work? | — | Not asked: the split follows the precedent the user already approved twice for the toolbar rows — wire everything the stored text model supports, present the rest disabled with naming tooltips. Flagged in the report so the user can redirect. |

## 5. Assumptions
```
ASSUMPTION-1: "Import Text..." may reuse the existing CSV browse dialog (its filter offers All (*.*)).
- Because:       there is no plain-text browse helper, and adding one to WinFileDialogs for this alone
                 would be more surface than the feature warrants.
- Risk if wrong: the dialog's title/filter says CSV, which reads oddly when importing a .txt.
- Validate by:   user opens Import Text; if the filter annoys, add BrowseOpenFileTxtUtf8 (a 10-line
                 addition mirroring the others).
```

## 6. Implementation
- `CadUi.cpp` — Ctrl+Enter commits and returns; the survey-label-only font cap in both the model and paper
  MTEXT draw paths; the Options popup; the Find and Replace popup; `kMtextSymbolPicks` hoisted to file
  scope so the toolbar's @ dropdown and the menu's Character Set share one list.
- `ui/MtextTextOps.hpp` (new, pure) — `UpperRange`/`LowerRange`, `FlattenToPlain`,
  `RemoveFormattingRange`, `FindReplaceAll`, `AutocorrectCapsLockWord`. Every one rewrites **span text
  only**, never tag bytes.
- `RichTextEdit.cpp` — applies the autocorrect when a word-ending separator is typed.
- `CadCommands.hpp` / `UserPrefs.cpp` — the autocorrect toggle (persisted) and the Find/Replace fields.

### Menu inventory
- **Live**: Import Text…, Find and Replace…, Change Case ▸ (UPPERCASE / lowercase, selection or whole
  MTEXT), All CAPS, Autocorrect cAPS Lock, Character Set ▸, Remove Formatting ▸ (selection / whole),
  Editor Settings ▸ (Show Options Row, Show Ruler), Help.
- **Disabled with a naming tooltip**: Insert Field, Paragraph Alignment, Paragraph…, Bullets and Lists,
  Columns, Combine Paragraphs, Background Mask… — each already a recorded REQ-051 follow-up.

## 7. Self-verification
- [x] build-project        — PASS (clean; app launches)
- [x] architecture-review  — PASS (decision recorded first; UI+IO only; the new ops are a pure header, not
                             an abstraction over anything; no dependency/global/format change)
- [x] code-review          — PASS (ranges clamped; inverted/out-of-bounds ranges refused rather than
                             clamped where clamping would be surprising; find/replace re-parses after each
                             hit rather than trusting stale offsets, and bails on a self-containing
                             replacement; imported and pasted text has `[[` collapsed so a file can never
                             inject a tag)
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a (menu actions are one-shot; `FindReplaceAll` is O(n²) in match count,
                             which is nothing for an MTEXT body and buys correctness)
- [x] testing              — PASS (98/98 green; +11 `MtextTextOpsTests`, including the regression guard
                             that a colour tag's hex digits are not case-changed, that a match never spans
                             a tag boundary, and that a self-containing replacement terminates)

## 8. Verification result
- Verdict: PASS pending user manual verification.

## 9. Outcome
- Requirements satisfied: REQ-051 as amended; REQ-050 defect fixed.
- Tests added:            `tests/MtextTextOpsTests.cpp` (11 cases).
- Docs updated:           `spec/project.md` (decision log). REQ-051's statement still needs the Options
                          menu written into it — **open doc debt**, listed below.
- Technical debt:         REQ-051's statement text not yet updated for the Options menu (the decision log
                          carries it); Import Text reuses the CSV browse dialog (ASSUMPTION-1); the
                          disabled menu items remain follow-ups.
- Done:                   pending user manual verification.

### Manual verification (user)
1. Type in an MTEXT, press **Ctrl+Enter** → the text is placed. Plain Enter still breaks the line.
2. Place an MTEXT, then zoom in and out → the text now grows and shrinks in step with the geometry
   around it, at every zoom level.
3. Click the chevron at the right of row 1 → the Options menu appears.
4. Change Case ▸ UPPERCASE with characters selected → only those change; with nothing selected the whole
   MTEXT changes. Bold/colour formatting survives it.
5. Find and Replace… → replace a word; formatting on the surrounding text is preserved.
6. Autocorrect cAPS Lock on → type "hELLO " → becomes "Hello ".
7. Remove Formatting ▸ From the whole MTEXT → all styling drops, the text stays.
8. Editor Settings ▸ toggles the options row and the ruler.
9. Greyed items do nothing and name themselves on hover.
