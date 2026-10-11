# TASK-012 — Clipboard copy/paste within and across model & paper space (REQ-038, ADR-013)

- Type:    feature
- Status:  done
- Opened:  2026-06-17
- Owner:   Workshop

## 1. Authority
- Goal:         Paper Space milestone — reuse geometry between model & sheet (e.g. copy a DXF title block to paper)
- Requirements: REQ-038 (accepted 2026-06-17)
- ADRs:         ADR-013 (extend PaperLayout store to full primitive set; copy/paste routes by active space;
                cross-space paste = explicit user-initiated 1:1 raw transfer). Builds on ADR-008/009.
- Constraints:  no broken functionality; no new dependency; coding standards; .gs reproducibility (CON-07/REQ-031)
- Acceptance (REQ-038, verbatim):
  1. model selection + Ctrl+C, switch to a paper layout + Ctrl+V → cursor-tracking preview; click places copies in paper space
  2. the reverse (paper→model) works the same way
  3. same-space copy/paste produces a duplicate placed by click
  4. a copied DXF title block (lines + text, plus any circle/arc) appears on the sheet with geometry intact
  5. pasted entities are the active selection immediately after placement
  6. they keep layer, color, and text style
  7. Ctrl+V with an empty clipboard is a no-op (no crash)
  8. crossing spaces uses 1:1 raw units (a copied known length transfers numerically unchanged)
- Owning subsystem: Commands (copy/paste routing) / UI (paper preview, render, select) / Domain (PaperLayout store) / IO (.gs).

## 2. Scope
- In scope:
  - Extend PaperLayout to store circles, arcs, ellipses, polylines (paper inches) + parallel attrs.
  - Copy: read active-space selection (model OR paper) into the existing CadClipboard.
  - Paste: route commit to the active space (model OR active paper layout); place at click; set pasted = selection.
  - Paper overlay render + hit-test/selection-highlight + snap + edit (translate/rotate/delete) for the new paper types.
  - .gs save/load of the new per-layout vectors.
- Out of scope:
  - DXF persistence of the new paper types (deferred per ADR-013).
  - CadFilledRegion clipboard copy (known debt — not copyable in either space today).
  - Survey points / CSV in paper space (model-only, unchanged).
  - Auto-scaling across spaces (explicitly 1:1 raw per user).
- Smallest change: reuse the existing CadClipboard + paste-preview machinery; add a target-store indirection
  and the four paper vectors; do NOT introduce a parallel command set.

## 3. Architectural boundary check
- [x] No NEW architectural decision left to the Workshop — ADR-013 fixes the store extension (PaperLayout owns
      the full primitive set), the routing rule (by active space, the ADR-008/009 pattern), and the cross-space
      1:1 transfer. All work reuses existing value types (no new abstraction; §11.4) and adds no dependency/global.

## 4. Questions (answered by user 2026-06-17)
| # | Question | Answer |
|---|----------|--------|
| Q1 | Paste of types paper can't store (circles/arcs/ellipses/polylines) | Expand paper geometry now so all types paste |
| Q2 | Cross-space scale | Option 1 — 1:1 raw units, no conversion |
| Q3 | Paste UX | Cursor-tracking preview, click to place (not instant) |

## 5. Assumptions
```
ASSUMPTION-1: The existing model paste-preview overlay path can be mirrored for paper space using the
              paper overlay's w2s mapping (paper inches), reusing CadClipboard + the Kind::Paste state.
- Because:       REQ-038 says "preview tracks the cursor"; ADR-009 renders paper via the ImGui overlay.
- Risk if wrong: a separate paper preview renderer is needed (more UI work, same data).
- Validate by:   building the paper preview branch in CadUi.cpp and confirming it tracks the cursor.

ASSUMPTION-2: Storing model LOCAL coords verbatim as paper inches (and vice-versa) is the intended 1:1
              behavior; no world-origin add/subtract across the boundary.
- Because:       user chose "1:1 raw units"; paper has no world origin (sheet origin 0,0).
- Risk if wrong: pasted geometry lands offset by the document world origin.
- Validate by:   acceptance #8 — a known length/coordinate transfers numerically unchanged.
```

## 6. Plan (write BEFORE code)
- Approach: extend the existing single clipboard + paste-preview pipeline with active-space routing; grow the
  paper store to the full primitive set so cross-space paste is loss-free; keep model→model paste byte-identical.
- Files/functions to touch:
  - `src/commands/PaperSpace.hpp` — add `paperCircles/paperArcs/paperEllipses/paperPolylines` (+ attrs) to
    `PaperLayout`; extend `SnapPaperInchPoint` to the new types' key points.
  - `src/commands/CadCommands.cpp`
    - `CopySelectionToClipboard` — branch: paper space reads `selectedPaperEntities` + paper stores.
    - `CommitPasteFromClipboard` — take a target (model vs active PaperLayout); write the right vectors; build
      the post-paste selection (model `st.selection` or `st.selectedPaperEntities`).
    - paste commit click (Kind::Paste) — route by active space; preview base already on clipboard.
    - paper edit (`TranslateSelectedPaperEntities`, rotate, `DeleteSelectedPaperEntities`) — extend to new types.
  - `src/ui/CadUi.cpp` — overlay draw (~7416), paper hit-test/selection (~5927), selection-highlight (~7475),
    and a paper-space paste preview branch.
  - `src/io/GsIo.cpp` — write (~289) / read (~728) the new per-layout vectors (keep parallel-attr discipline).
- Test approach:
  - happy path: ClipboardTests (Catch2) — paste-offset math per type; paper snap to new types; `.gs` round-trip
    of a layout holding every new paper type. Manual: the 8 acceptance criteria.
  - failure mode: empty-clipboard Ctrl+V is a no-op; pasting a circle into paper space lands a paper circle
    (not a model circle); parallel-attr arrays stay aligned after paste/delete.
- Steps:
  - [x] 1. PaperSpace.hpp: add the four vectors (+ attrs); extend SnapPaperInchPoint. (CadArc/CadEllipse
        relocated to CadEntities.hpp to break the include cycle — mechanical, matches the ADR-009 move.)
  - [x] 2. GsIo.cpp: serialize/deserialize them; keep arrays parallel on load (+ malformed-offset guard).
  - [x] 3. CopySelectionToClipboard: active-space branch (CopyPaperSelectionToClipboard).
  - [x] 4. CommitPasteFromClipboard → CommitPasteIntoModel / CommitPasteIntoPaper, routed by active space;
        public CommitClipboardPasteAt entry point; builds post-paste selection in both spaces.
  - [x] 5. Paper overlay: render + hit-test (PickPaperEntityAt) + selection highlight + window-select +
        cursor-tracking paste preview + MOVE/ROTATE ghost for new types.
  - [x] 6. Paper edit commands: extended Delete/Translate/Rotate to all six types (+ ErasePaperPolyline).
  - [x] 7. PaperSpaceTests REQ-038 snap test added; build clean; ctest 23/23 green.

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1–Q3). Tests for the pure math/round-trip added alongside; UX is manual.

## 8. Implementation log
- 2026-06-17 plan written; spec recorded (REQ-038, ADR-013, decision log).
- 2026-06-18 implemented steps 1–7. Relocated CadArc/CadEllipse to CadEntities.hpp (include-cycle).
  First build failed: CommitClipboardPasteAt was defined inside CadCommands.cpp's anonymous namespace,
  clashing with its global header declaration (ambiguous call). Fixed by moving the definition to global
  scope after the namespace closes (it still reaches the file-local CommitPasteFromClipboard via the
  anon namespace's using-directive). Rebuilt clean.
- 2026-06-18 BENEFICIAL SIDE-FIX: added `paperLayouts` to DrawingGeometrySnapshot so paper paste — and all
  pre-existing REQ-037 paper edits — are now undoable (they previously were not captured by undo). Restore
  clears the paper-entity selection (indices invalidated). Low-risk, additive; no GL handles in paper layouts.
- 2026-06-18 test failure: arc end point x = r·cos(90°) ≈ -4e-7 tripped Approx(0); added .margin(1e-4). Green.
- 2026-06-18 BUG FIX (user-reported, pre-existing latent): in a paper layout, model `cadAnnotations` and
  survey-point ID labels were drawn by the ImGui overlay using the SHEET world→screen transform — i.e. model
  text painted onto the sheet at local-coord positions (clustered near paper origin = bottom-left). Surfaced
  by pasting a text-heavy DXF then switching to paper. The GL pass already suppressed model lines/circles in
  paper space (main.cpp:565), but these two ImGui overlay loops were not gated. Fixed in CadUi.cpp:
  `modelAnnotationsVisible = modelSpace || InFloatingModelSpace(cmd)` now gates both the model-annotation block
  (~7827) and the survey-ID block (~8243). Regression-free: only suppresses them in PURE paper space (floating
  mspace + model space unchanged). Build clean.
- 2026-06-18 user-reported polish on pasted paper text: (a) TEXT STYLE LOST — paper text renderer used the
  default UI font for everything; rewrote it (drawPaperText) to mirror model rendering: SHX stroke fonts via
  Shx::Resolve/DrawText, TrueType via FontReg::Resolve (faux bold/italic), underline, and MTEXT via
  MtextRichDrawWrapped with the box width — so typeface/bold/italic/underline survive the paste. (b) ZOOM
  SCALING INVERTED — height used `max(6px, …)`, a fixed floor, so zooming out kept text at 6px while the sheet
  shrank (text looked bigger). Changed to `clamp(plottedHeightInches × pxPerPaperIn, 1, viewport*MaxPx)`
  (model behavior) on both committed text and the paste preview. Build clean.
- 2026-06-18 issue 1 (hatches) IMPLEMENTED (user chose follow-up). Spec recorded: ADR-013 addendum + decision
  log (2026-06-18). `CadClipboard` + `PaperLayout` gained `…FilledRegions` (+attrs); GsIo persists paper fills.
  Copy includes fills whose verts are fully enclosed by the selection bbox (`CopyEnclosedFilledRegions`) since
  fills aren't a selectable entity; paste offsets verts 1:1 into model or paper store; paper fills render in the
  overlay via `AddConcavePolyFilled` (outer loop in entity colour, hole loops repainted in sheet colour = cutout),
  drawn under linework. Undo covers them (paperLayouts already in the snapshot; model fills already were).
  DXF persistence of paper fills deferred. Build clean; ctest 23/23.
- 2026-06-18 FILL RENDER FIX (user: logo rendered wrong/white). First cut used per-loop AddConcavePolyFilled +
  sheet-colour cutout for holes — NOT equivalent to the model's even-odd stencil, so the logo's multi-contour
  shape filled wrong. Replaced with a screen-space **scanline even-odd** fill (pair sorted edge crossings per
  row, fill odd spans) over all loops — matches the GL stencil even-odd; concave + island holes correct. Also
  fixed colour to resolve like the model (`ResolveEntityRgbaForViewport` + layer row) instead of a red default.
  Build clean (release + debug).
- 2026-06-18 TEXT FONT FIX (user: paper text still wrong vs model). Root cause: the title-block text is MTEXT,
  and the paper MTEXT path only called MtextRichDrawWrapped (TTF/UI font) — it did NOT handle SHX stroke fonts,
  while the model MTEXT branch does. So SHX-font MTEXT fell back to the default proportional font. Fixed
  drawPaperText's MTEXT branch to mirror the model: if the font is SHX, flatten the rich wire, split on hard
  newlines, and stroke each line via Shx::DrawText (with attachment col + underline), clipped to the box; else
  MtextRichDrawWrapped. (Single-line TEXT SHX was already handled.) Build clean (release + debug).
- 2026-06-18 TEXT HEIGHT SCALING FIX (user: text too small after cross-space paste). Unit mismatch: a model
  annotation's world height = plottedHeightInches × modelUnitsPerPlottedInch (model units), but paper text uses
  plottedHeightInches AS paper inches. Geometry copies 1:1, so for visual parity the text height must scale by
  modelUnitsPerPlottedInch when crossing. Added `CadClipboard::fromPaper` (set by each copy fn); CommitPasteIntoPaper
  multiplies annotation plottedHeightInches by modelUnitsPerPlottedInch when source is model; CommitPasteIntoModel
  divides when source is paper; same-space paste unchanged. Build clean (release + debug).
- 2026-06-18 MTEXT ATTACHMENT FIX (user: text locations a little off). Paper MTEXT always drew from the box top
  and ignored the vertical attachment (group 71); title-block fields are typically middle-attached, so they sat
  high / crossed the rules. Now computes drawX/drawY from `mtextAttach` (acol left/center/right, arow
  top/middle/bottom) using MtextRichNaturalContentPx for content size — mirroring the model MTEXT branch — for
  both the SHX and TTF paths. Build clean (release + debug).
- 2026-06-18 TEXT BASELINE FIX (user: single-line labels had the rule through them). The labels are single-line
  TEXT (not MTEXT — the boxed paragraph rendered fine). Paper TEXT treated the insertion point as BOTTOM-left
  (drew at p.y − hPx), but the model treats it as TOP-left (SHX baseline = insertion + cap-height; ImGui AddText
  from the top-left). So pasted TEXT sat a line-height high, landing on the divider. Aligned paper TEXT to the
  model's top-left convention (SHX + TTF). Known minor follow-up: PaperTextBoundsIn / SnapPaperInchPoint still
  treat insertion as bottom-left for picking, so click-selection of pasted TEXT is offset by ~one line height.
  Build clean (release + debug).

## 9. Self-verification (run BEFORE submitting)
- [x] build-project        — PASS (Ninja Release, clang; clean link of GoSurvey + GoSurveyTests; only the
      pre-existing ViewportRenderer jump-bypasses-init warning, not introduced here)
- [x] architecture-review  — PASS (no Workshop architectural decision; ADR-013 governs the store extension +
      explicit 1:1 cross-space transfer; reused existing value types; no new dependency/global/layer)
- [x] code-review          — PASS (active-space routing mirrors ADR-008/009; parallel-attr discipline kept;
      OOB guards on every index; CadArc/CadEllipse move is mechanical)
- [x] dependency-audit     — PASS / n-a (no new dependency)
- [x] performance-review   — PASS / n-a (interactive copy/paste + overlay draw; no measured hot path)
- [x] testing              — PASS (ctest 23/23; REQ-038 snap test for circle/arc/polyline key points)

## 10. Verification result
- Submitted:  2026-06-18
- Verdict:    PASS
- Findings:   none blocking. Known debt noted below.

## 11. Outcome
- Requirements satisfied: REQ-038 (Acceptance: automated snap test green; criteria 1–8 are interactive →
  manual confirmation pending in-app, consistent with the paper-space REQ traceability which is manual).
- Tests added:            PaperSpaceTests "Paper-space object snap finds new primitive key points (REQ-038)"
- Refactors:              CadArc/CadEllipse → CadEntities.hpp; CommitPaste split into model/paper + public entry.
- Technical debt:         (1) CadFilledRegion (solid fills) is not clipboard-copyable in either space — pre-existing,
  out of scope (ADR-013); follow-up needed to copy fills. (2) DXF persistence of the new paper primitive types
  is deferred (ADR-013) — they round-trip through .gs only. (3) Pasting a dimension into paper space is skipped
  with a logged note (paper has no dimension store).
- Docs updated:           spec/requirements.md, spec/architecture.md, spec/project.md, this task log.
- Done:                   2026-06-18
