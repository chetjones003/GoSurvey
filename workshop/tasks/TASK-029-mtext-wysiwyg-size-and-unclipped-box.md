# TASK-029 — MTEXT edits at true rendered size; the box wraps but never hides text

- Type:    bug
- Status:  self-verify (awaiting user's on-screen confirmation)
- Opened:  2026-07-30
- Owner:   chetj

## 1. Authority
- Goal:         editing an MTEXT shows what committing it will produce
- Requirements: REQ-051 (accepted — in-place editing "edits WYSIWYG … text **wraps at the MTEXT's
  column width**"); ADR-023 (WYSIWYG in-place editor); REQ-050 (accepted — MTEXT is sized by the
  viewport scale) for the sizing term the editor must mirror
- Constraints:  none new
- Acceptance (user's statement, 2026-07-30, restated):
  - the size shown while typing is the size the text renders at on commit — no jump on Enter;
  - the bounding box never hides text; text renders regardless of the box's vertical extent;
  - the box's only job is deciding where lines wrap.
- Owning subsystem: UI (MTEXT editor + annotation render).

## 2. Scope
- In scope:  the editor's font-size term; the annotation box's clip and anchor clamps, in both model
             and paper space.
- Out of scope:
  - The paper-space **viewport** clip (`CadUi.cpp:9040`) — a viewport must clip the model content it
    shows (REQ-027). That is not the MTEXT bounding box and is untouched.
  - The editor's own scroll clip: its box already grows to fit content and always fits at least one
    full line, so it hides nothing at the sizes it is asked to draw.
- Smallest change: mirror the render's sizing expression in the editor; clip to the viewport instead
  of to the box; drop the two anchor clamps that shoved overflowing content back inside the box.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / data-format change?
    - [x] No — proceed. Both files already own this behaviour.

## 4. Questions
None — the acceptance was stated directly by the user.

## 5. Assumptions
```
ASSUMPTION-1: in paper space the viewport pans/zooms in paper inches, so the editor's
  worldPerPxY is already inches-per-pixel there and plottedHeightInches / worldPerPxY is the
  on-screen height — matching the paper render's plottedHeightInches * pxPerPaperIn.
- Because:      the two paths express the same scale differently.
- Risk if wrong: paper-space MTEXT edits at the wrong size (model space, the reported case, is unaffected).
- Validate by:  editing a paper-space MTEXT and checking the size does not change on commit.
```

## 6. Plan
- Approach: (a) compute `editFontPx` with the render's own terms; (b) stop clipping to the box.
- Files/functions to touch: `src/ui/CadUi.cpp` — the in-place editor's size block, the model-space
  MTEXT draw in `drawAnnotationVisual`, and the paper-space MTEXT draw.
- Test approach: happy path = type at a zoom where the true size is far outside 10–96 px and confirm
  no jump on commit; failure mode = a box shorter than its text still shows every line.
- Steps:
  - [x] mirror the render's sizing in the editor
  - [x] replace the box clip with a viewport clip, model + paper
  - [x] drop the anchor clamps
  - [x] build + suite

## 7. Workflow-specific notes (Bug)
- Reported: text changes size on commit, and the bounding box hides text.
- Root cause 1 — **two independent clamps.** The committed render clamps the on-screen height to
  `[1, 8192]` px for plain MTEXT (`[viewportMtextMinPx, viewportMtextMaxPx]` for survey labels). The
  editor applied its own unrelated `[10, 96]` px window. Any text whose true size fell outside 10–96
  jumped the instant it was committed: large text was edited shrunk, small text edited enlarged.
  The editor also always used the drawing-wide `modelUnitsPerPlottedInch`, while the render prefers
  the **current viewport's** scale for plain MTEXT (REQ-050) — so editing through a floating viewport
  at a non-drawing scale disagreed as well, independently of the clamp.
- Root cause 2 — **the box clipped.** `PushClipRect(box)` around the MTEXT draw discarded everything
  outside the box, so any line past the box's height simply vanished. Two anchor expressions made it
  worse by clamping content back inside the box before it was clipped
  (`drawY = max(ry0 + 4, ry1 - ph - 4)` and the `drawX` equivalent), so overflowing text was first
  pushed in, then cut off.
- Fix: the box now determines **wrapping only** (`wrapW`, unchanged). Clipping is to the viewport
  rectangle — kept solely so text overhanging its box cannot paint over the surrounding UI — and the
  anchor clamps are gone, so content taller or wider than its box overhangs instead of being hidden.
- Regression test fails-before? Not automated — see §9.

- Root cause 3 — **placement had no size at all** (found on the user's second report, with
  screenshots of the MTEXT command). The sizing block above was guarded by `if (target)`, and while
  *placing* a new MTEXT `target` is null by design — the annotation does not exist yet. So the editor
  fell through to `ImGui::GetFontSize()`, the UI font size, which has nothing to do with the drawing.
  `CommitMtextRichEditor` then stamps `defaultPlottedTextHeightInches`
  (`CadCommands.cpp:6956`; `StampActiveTextStyleOnNewText` copies font/oblique/bold/italic but
  deliberately **not** height), so the text jumped to its real size the instant it was accepted —
  roughly 3× in the reported case. The editor now uses that same default when there is no target, so
  the placement preview and the placed result agree.
- Root cause 4 — **the editor clipped horizontally.** A word too long for the column cannot be
  wrapped, and the committed MTEXT lets it overhang its box. The editor clipped it at the box edge,
  and there is no horizontal scroll to reveal it — so the same text looked truncated while typing and
  complete after commit. The glyph clip is now vertical-only (it is a vertical scroll region); the
  horizontal range is wide and intersected with the current clip, so it still cannot paint outside
  the window.

## 8. Implementation log
- 2026-07-30 traced both defects; mirrored the render's sizing term in the editor (including the
  REQ-050 viewport-scale selection); swapped the box clip for a viewport clip in model and paper
  paths; removed the two clamps. Build + suite green.
- 2026-07-30 user reported the size still jumping, with screenshots — the case was the MTEXT
  *placement* command, which the `if (target)` guard skipped entirely (root cause 3). Fixed by
  sourcing the plotted height from `defaultPlottedTextHeightInches` when there is no target, and
  relaxed the editor's horizontal clip (root cause 4). Build + suite green.

## 9. Self-verification
- [x] build-project        — PASS (release links clean)
- [x] architecture-review  — PASS (no new surface; the viewport clip that must stay was identified
      and left alone)
- [x] code-review          — PASS (the editor now states, in one place, that it mirrors the render)
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a (same work; one clamp changed, one clip rect changed)
- [ ] testing              — PARTIAL and honestly so: `GoSurveyTests` green (611 assertions / 98
      cases) but covers neither path — both need an ImGui context, an `ImFont` and a live viewport
      transform, so they cannot run headlessly in the current test target (same DEBT-1 as TASK-026).
      **Needs an on-screen check** (§10).

## 10. Verification result
- Submitted:  2026-07-30
- Verdict:    pending user confirmation. Worth checking specifically:
  1. **the MTEXT command** (placing new text) — the preview must match the placed result;
  2. re-opening an existing MTEXT to edit, zoomed so the text is much larger than ~96 px;
  3. same zoomed far out, where the true size is under 10 px;
  4. a box deliberately shorter than its content — every line must still draw;
  5. a single word wider than the column — it should overhang, and look the same before and after;
  6. a bottom- or right-anchored MTEXT (attachment 3/6/9) whose text overflows — it should overhang
     the box rather than shift inside it.
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-051 / ADR-023, and REQ-050's sizing now honoured by the editor too
  (Acceptance: pending the checks above)
- Tests added:            none — see §9
- Consequence the user should know about (by design, not a defect):
  true WYSIWYG means that when the drawing is zoomed far out the editor now shows genuinely tiny
  text, because that is what will be committed. The old floor made it legible by lying about the
  size. Zooming in is the remedy; if that proves annoying in practice, a "fit to editor" toggle would
  be a feature request, not a return to the silent mismatch.
- Observation (NOT actioned — out of scope):
  line spacing is `fontPx * 1.22` in the rich-text core (what the editor and normal MTEXT use) but
  `fontPx * 1.4` in `CadUi`'s whole-object SHX branch (model and paper). So an MTEXT whose *base*
  family is an SHX font — DXF-imported text, mainly — still commits at wider line spacing than it
  edits at. It does not affect fonts applied as runs, which is how the samples in TASK-028 were made.
  Unifying means choosing one spacing model and changing the look of already-accepted imported text,
  so it belongs with the TASK-028 §11 observation about retiring that branch.
- Docs updated:           none
- Done:                   pending user confirmation
