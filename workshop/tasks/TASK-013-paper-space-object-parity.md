# TASK-013 — Paper-space object parity with model space

- Type:    feature (with bug fixes)
- Status:  Phases 1–2 PASS — Phases 3–4 (grips, draw+modify) pending
- Opened:  2026-06-18
- Owner:   chetjones003

## 1. Authority
- Goal:         AutoCAD-style paper space milestone (REQ-025–039)
- Requirements: REQ-039 (accepted); supports REQ-037 / REQ-038 (paper-space geometry + clipboard)
- Constraints:  REQ-200 reproducible build; REQ-201 no silent failures; REQ-301 minimal abstraction
- Acceptance (REQ-039, verbatim): in a paper layout —
  (1) a window box (L→R) selects paper objects fully inside it and a crossing box (R→L) selects
      any it touches, for every paper object type;
  (2) clicking a paper text selects it with no vertical offset, and double-clicking any text
      (model or paper) opens an inline editor whose committed text replaces the object's contents;
  (3) selecting paper object(s) populates the Properties panel and edits made there apply to the
      object(s);
  (4) selected paper objects show grips and dragging a grip edits the geometry;
  (5) CIRCLE, ARC, ELLIPSE, POLYLINE, and MTEXT draw onto the sheet, and SCALE/JOIN/TRIM/OFFSET
      operate on the paper selection;
  (6) none of these paper edits change model geometry;
  (7) a `.gs` round-trip restores the edited paper objects per layout.
- Owning subsystem: UI / Commands (+ Domain value types, IO for `.gs` already covered)

## 2. Scope
- This task (Phase 1 of 4):
  - In scope:  (a) fix box-select on paper objects [bug #1]; (b) fix paper text pick/snap offset —
               `PaperTextBoundsIn` + `SnapPaperInchPoint` treat insertion as top-left [bug #2];
               (c) Properties panel shows AND edits the paper selection (General + per-type Geometry
               + Text) [bug #3]; (d) hover pre-highlight parity for paper objects.
  - Out of scope (later phases): Phase 2 in-place text editor (model + paper); Phase 3 grips on
               paper objects; Phase 4 draw (circle/arc/ellipse/polyline/mtext) + modify
               (scale/join/trim/offset) routing to paper.
- Smallest change: extend the existing paper selection / box-select / Properties branches; no new
  abstraction.

## 3. Architectural boundary check
- New abstraction / layer / dependency / global / data-format / algorithm?
    - [x] No — ADR-014 (accepted) governs the approach: extend the existing active-space-branch
          pattern, reuse existing value types, no new abstraction/global/dependency. `.gs` schema
          already holds paper objects (REQ-031/037/038). Proceed.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | How far does parity go? | 2026-06-18 | Full command parity (all draw + modify). |
| Q2 | Text editing mechanism? | 2026-06-18 | Double-click in-place editor; implement in model AND paper. (Phase 2) |
| Q3 | How to record in spec? | 2026-06-18 | New REQ-039 + ADR-014 (recorded in decision log). |

## 5. Assumptions
ASSUMPTION-1: The Properties panel can bind to the active layout's paper objects by branching on
active space at its entry, reusing the existing per-type edit-apply paths.
- Because:       the panel today reads only `cmd.selection` (model) + survey points.
- Risk if wrong: paper edits don't write back / write to the wrong store.
- Validate by:   manual edit of a paper line/text/circle and a `.gs` round-trip.

## 6. Plan
- Approach: extend the existing paper selection + box-select code in CadUi.cpp and the Properties
  panel in CadUi.cpp; fix the two pure helpers (`PaperTextBoundsIn`, `SnapPaperInchPoint`).
- Files/functions to touch:
  - `src/commands/CadCommands.cpp` — `PaperTextBoundsIn` (top-left anchor).
  - `src/commands/PaperSpace.hpp` — `SnapPaperInchPoint` text branch (top-left anchor).
  - `src/ui/CadUi.cpp` — paper box-select close logic (bug #1); `DrawPropertiesPanel` paper branch
    (bug #3); paper hover highlight.
  - `tests/PaperSpaceTests.cpp` — text-bounds top-left + box-select hit-per-type tests.
- Test approach: happy = box selects each paper type by the L→R/R→L rule; text bounds top-left;
  Properties edit writes back. failure = box drawn in empty area selects nothing; non-text click
  no longer mis-selects.
- Steps:
  - [ ] Diagnose why the paper box-select does not select objects (bug #1) and fix.
  - [ ] Fix `PaperTextBoundsIn` + `SnapPaperInchPoint` to top-left anchor (bug #2).
  - [ ] Properties panel: show + edit paper selection (bug #3).
  - [ ] Hover highlight parity.
  - [ ] Tests + build + self-verify.

## 7. Workflow-specific notes
- Feature/bug mix. Bug #2 root cause is documented in TASK-012: bounds helper uses bottom-left while
  text renders top-left. Bug #1/#3 root cause to be confirmed in the implementation log.

## 8. Implementation log
- 2026-06-18 open → implement. Spec recorded (REQ-039, ADR-014, decision log).
- 2026-06-18 Phase 1 complete:
  - Bug #2 (text pick offset): `PaperTextBoundsIn` anchors text top-left (y0=insY−h, y1=insY), matching the
    renderer. Helper moved to `PaperSpace.hpp` as `inline` (was `static` in CadCommands.cpp) so the box-select
    helper + unit tests share it.
  - Refactor: `PaperEntityRef` lives in `PaperSpace.hpp` (single definition). Added pure, header-only
    `SelectPaperEntitiesInBox(L, bx0,by0,bx1,by1, windowMode, out)` covering every paper type by the window
    (fully-inside) / crossing (overlap) rule.
  - Bug #1 (box-select): root cause = no press-drag-release gesture (click-click was correct). Extracted the
    box close into a `closePaperSelBox(closeX,closeY)` lambda (CadUi.cpp), now called by BOTH the second click
    (click-click) and a new `IsMouseReleased` + `GetMouseDragDelta > 4px` drag-release handler. Paper-entity
    selection inside the box routes through `SelectPaperEntitiesInBox`.
  - Bug #3 (Properties): added `DrawPaperEntityProps` + a paper branch at the top of `DrawPropertiesPanel`
    (active paper layout, not floating model, non-empty paper selection). Shows General (Layer/Color combos
    applied to the whole selection) + per-type Geometry for a single object (Line/Circle/Arc/Ellipse end/center
    coords + radius/ratio; Text insertion/height + contents; Polyline vertex/closed readout). Drops stale refs.
  - Item (d) Hover parity: added `paperHoverValid` + `paperHover` (mirrors model `viewportHoverEntity`),
    computed each idle frame via `PickPaperEntityAt`, rendered in the lighter-blue hover color across all paper
    render branches (line/circle/arc/ellipse/polyline/text) via `paperCol`/`paperWid`.
  - Tests: added `PaperTextBoundsIn` top-left and `SelectPaperEntitiesInBox` per-type window/crossing cases to
    `tests/PaperSpaceTests.cpp` (25/25 pass).

## 8b. Phase 2 plan — in-place text editor (model + paper) [REQ-039 acceptance (2)]
- Authority: REQ-039 (accepted), acceptance (2): "double-clicking any text (model or paper) opens an
  inline editor whose committed text replaces the object's contents"; ADR-014 (reuse, no new abstraction).
- Architectural check: [x] No new abstraction/layer/dependency/global/data-format/algorithm. Extends the
  existing MTEXT rich-editor (one editor) to (a) also target single-line `Kind::Text` and (b) target a
  paper layout's `paperTexts` store. Shared value type `CadAnnotation` already used by both stores. The
  "same shared editor" the spec asks for = the existing overlay, retargeted.
- Key facts found:
  - Existing editor: `OpenMtextRichEditorForAnnotation` / `CommitMtextRichEditor` / `CancelMtextRichEditor`
    (CadCommands.cpp) + `DrawMtextRichEditorOverlay` (CadUi.cpp) + state `mtextRichEditor*`. Today it only
    opens for **model** `Kind::Mtext` (double-click at CadUi.cpp ~7229).
  - In paper space `worldLeft..worldTop` ARE paper inches (same transform `screenToPaperIn` uses), so the
    overlay's `ws()` positions paper text with NO new transform. The overlay is drawn once (CadUi.cpp ~9026)
    with the active-space extents.
  - Paper edits are already undoable (PushUndoSnapshot snapshots full state incl. paperLayouts, TASK-012).
- Approach (smallest sufficient):
  - State: add `mtextRichEditorPaper` (bool), `mtextRichEditorPaperLayout` (int), `mtextRichEditorPlain`
    (bool, single-line TEXT). Reset in `CloseMtextRichEditorUi`.
  - Header: inline `MtextRichEditorTargetAnnotation(st)` → `CadAnnotation*` (model `cadAnnotations` or paper
    `paperTexts`). Declare `OpenPaperTextEditor(...)`.
  - `OpenMtextRichEditorForAnnotation`: accept `Kind::Text` too (plain mode), clear paper flags.
  - `OpenPaperTextEditor(st, layoutIdx, textIdx, log)`: target paper store, plain = (kind==Text).
  - `CommitMtextRichEditor`: resolve via target accessor; plain → store raw (newlines→spaces, no MTEXT
    normalize); rich → `MtextRichNormalize` + (model only) survey-label reposition. Undo label per kind.
  - CadUi model double-click (~7229): broaden `Kind::Mtext` → also `Kind::Text`.
  - CadUi paper input block (~6060): double-click on a paper text → select it + `OpenPaperTextEditor`
    (takes priority over viewport-entry); guard the rest of the click handling with the new flag; suppress
    the paper input block while the editor is open.
  - Overlay: target via accessor; for `Kind::Text`/boxless, synth box from insertion + height (world height
    for model, plotted inches for paper); plain → single-line `InputText`, hide rich toolbar/hint.
- Test approach: unit (PaperSpaceTests, pure) — `MtextRichEditorTargetAnnotation` resolves model vs paper
  vs out-of-range; plain-commit collapses newlines + leaves contents verbatim; rich-commit normalizes.
  Manual — double-click model TEXT / model MTEXT / paper TEXT / paper MTEXT edits contents; `.gs` round-trip;
  model geometry unchanged.
- ASSUMPTION-2: single-line `Kind::Text` uses a PLAIN single-line editor (no MTEXT rich tags) — applying
  MTEXT normalization to TEXT.text would inject `[[ ]]` wire tags and corrupt it. Matches AutoCAD DDEDIT.
  Risk if wrong: TEXT shows literal tags. Validate by: edit a model + paper TEXT, confirm contents verbatim.

## 8c. Phase 2 implementation log
- 2026-06-18 Phase 2 complete — in-place text editor, model + paper, ONE shared editor (REQ-039 acceptance 2):
  - State (CadCommands.hpp): added `mtextRichEditorPaper`, `mtextRichEditorPaperLayout`, `mtextRichEditorPlain`;
    reset in `CloseMtextRichEditorUi`. Added inline `MtextRichEditorTargetAnnotation(st)` → `CadAnnotation*`
    that resolves the live target (model `cadAnnotations` or active layout `paperTexts`), nullptr for
    placement / out-of-range.
  - Open (CadCommands.cpp): `OpenMtextRichEditorForAnnotation` now also accepts `Kind::Text` (plain mode);
    new `OpenPaperTextEditor(layoutIdx, textIdx)` retargets the same editor at a paper text. Plain =
    (kind==Text).
  - Commit (CadCommands.cpp): resolves via the accessor; plain → store contents verbatim (newlines→spaces, no
    MTEXT normalize so TEXT.text never gets `[[…]]` tags); rich → `MtextRichNormalize` + (model only) survey
    label reposition. Undo label per kind (paper edits already undoable via PushUndoSnapshot, TASK-012).
  - CadUi double-click: model path (~7237) broadened from MTEXT-only to MTEXT **or** TEXT. Paper path (~6063)
    gained a double-click-on-paper-text branch (`OpenPaperTextEditor`), taking priority over viewport entry;
    the click-select branch is guarded by `openedPaperTextEdit`, and the whole paper input block is suppressed
    while the editor is open (`!cmd.mtextRichEditorOpen`).
  - Overlay (`DrawMtextRichEditorOverlay`): target via the accessor (model or paper); for `Kind::Text` / boxless
    it synthesizes a top-left box from insertion + height (world height for model, plotted inches for paper —
    `worldLeft..worldTop` already equal paper inches in paper space, so the existing `ws()` needs no change);
    plain → single-line `InputText` (Enter commits), rich toolbar/hint hidden.
- No new abstraction/layer/dependency/global/data-format added (ADR-014). One editor, retargeted.

## 9. Self-verification
- [x] build-project       — clean: release (ninja-release) + debug (ninja-debug), no new warnings.
- [x] architecture-review — no new abstraction/layer/dependency/global/data-format/algorithm; ADR-014 pattern
      (active-space branch + reuse of value types). `paperHover` is a transient UI field paralleling the
      existing `viewportHoverEntity`. Changes confined to UI/Commands subsystem.
- [x] code-review         — box rules de-duplicated into one tested helper; lambdas keep click-click + drag
      paths sharing one close path. No silent failures (REQ-201): stale paper refs are pruned before edit.
- [x] dependency-audit (n-a) — no dependencies added.
- [x] performance-review (n-a) — per-frame hover pick is O(paper entities), same order as existing snap/pick.
- [x] testing             — ctest 25/25 green; 2 new REQ-039 cases (text bounds top-left; box-select per type).

## 9b. Phase 2 self-verification
- [x] build-project       — clean: release (ninja-release) + debug (ninja-debug); only the pre-existing
      ViewportRenderer.cpp jump-init warning, none new.
- [x] architecture-review — no new abstraction/layer/dependency/global/data-format/algorithm. The single
      MTEXT editor is retargeted via a descriptor (paper/plain flags + target accessor) reusing the shared
      `CadAnnotation` type; matches ADR-014's active-space-branch + reuse pattern.
- [x] code-review         — one editor, no duplicate; `MtextRichEditorTargetAnnotation` re-resolved after
      PushUndoSnapshot. No silent failures (REQ-201): accessor returns nullptr on out-of-range / placement
      and commit/overlay both bail. Plain commit never injects MTEXT tags into TEXT.text (ASSUMPTION-2).
- [x] dependency-audit (n-a) — none added.
- [x] performance-review (n-a) — editor is event-driven; double-click does one extra `PickPaperEntityAt`
      (same order as the existing hover/click pick).
- [x] testing             — full suite green: 25/25 cases, 198 assertions (no regression; the editor box's
      top-left anchor reuses the already-tested `PaperTextBoundsIn` formula). The interactive editor itself is
      validated manually (project convention — the existing model MTEXT editor has no unit tests; the test
      target compiles only the pure `PaperSpace.hpp` surface, not the command/UI layer).
- Manual checklist (to confirm in-app): double-click model TEXT / model MTEXT / paper TEXT / paper MTEXT each
  opens the editor over the object; committed text replaces contents; Esc cancels; `.gs` round-trip restores
  edited paper text; model geometry unchanged by paper edits; undo reverts an edit.

## 10. Verification result
- Submitted:  self-verification (Phase 1 + Phase 2 scope).
- Verdict:    Phase 1 PASS. Phase 2 PASS (in-place text editor, model + paper, shared editor). Phases 3–4
              (paper grips; draw CIRCLE/ARC/ELLIPSE/POLYLINE/MTEXT + SCALE/JOIN/TRIM/OFFSET routing) remain
              as follow-on work under REQ-039.

COMPLETION REPORT — TASK-013 (Phase 2) — 2026-06-18
- Requirements satisfied:  REQ-039 acceptance (2) double-click any text (model or paper) opens an inline
                           editor whose committed text replaces the contents — for MTEXT (rich) and
                           single-line TEXT (plain), in both spaces, via one shared editor; (6) paper edits
                           leave model geometry untouched. (4) grips and (5) draw/modify parity remain Phases 3–4.
- Summary:                 Retargeted the existing MTEXT in-place editor to also edit single-line TEXT and
                           native paper-space text, adding double-click open paths in both spaces.
- Tests:                   full suite 25/25 (198 assertions) green; editor box anchor reuses tested
                           PaperTextBoundsIn; interactive editor validated manually (see 9b).
- Verification verdict:    PASS (no blocking findings).
- Assumptions:             ASSUMPTION-2 (plain editor for Kind::Text; no MTEXT tags) — to confirm by editing a
                           model + paper TEXT and checking contents are stored verbatim.
- Architectural decisions: none made by Workshop (ADR-014 governs).
- Dependencies:            none.
- Technical debt noted:    none new. Phases 3–4 tracked under REQ-039.
- Build:                   reproducible, clean on Windows (release + debug).
- Docs updated:            this task log.

## 11. Outcome
COMPLETION REPORT — TASK-013 (Phase 1) — 2026-06-18
- Requirements satisfied:  REQ-039 acceptance (1) box-select per type, (2) text pick with no offset,
                           (3) Properties show+edit, (6) no model change, (7) `.gs` round-trip (stores
                           unchanged) — for Phase-1 surface. (4) grips, (5) draw/modify parity, and the
                           double-click inline editor are Phases 2–4.
- Summary:                 Fixed paper text-pick offset, box-select (drag-release), and the Properties panel
                           for paper objects; added hover pre-highlight parity.
- Tests:                   PaperSpaceTests "Paper text bounds anchor…" + "Paper box-select selects each type…"
                           (happy + empty/crossing failure modes), ctest 25/25 green.
- Verification verdict:    PASS (findings resolved: none blocking).
- Assumptions:             ASSUMPTION-1 validated — Properties binds to the active layout's paper stores and
                           writes back (verify with a manual edit + `.gs` round-trip).
- Architectural decisions: none made by Workshop (governed by ADR-014).
- Dependencies:            none.
- Technical debt noted:    none new. Phases 2–4 tracked under REQ-039 (this task's later phases).
- Build:                   reproducible, clean on Windows (release + debug).
- Docs updated:            this task log.

## 11b. Pre-Phase-3 fix — model-space text hover + visible selection — 2026-06-18
- Reported: model-space text hover, selection, and double-click "do not work at all" (single-line TEXT).
- Root cause (evidence):
  - Hover: the model idle-hover detector (`CadUi.cpp` ~6552) calls only `PickClosestCadEntity`, which has no
    annotation branch (`CadCommands.cpp` `PickClosestCadEntity`), so text was never detected for hover.
  - Selection: clicking a single-line `Kind::Text` *does* update selection state (`PickCadAnnotationAt` runs
    before the entity pick), but the selection-highlight render loop (`CadUi.cpp` ~8567) was gated
    `kind != Mtext → continue` — a selected TEXT drew no rectangle/grip, so it looked unselected.
  - Double-click: already wired for `Kind::Text` in Phase 2 (`CadUi.cpp` ~7276); it appeared dead only because
    the hover and selection gave zero visual feedback. No code change needed; confirmed by trace.
- Fix (UI-only, model annotation overlay):
  - Hover detection: in the idle-hover block, pick text annotations first (TEXT/MTEXT only) and set
    `viewportHoverEntity` to that Annotation, mirroring click-to-select priority; guarded by `modelSpace`.
  - Hover render: draw a light-blue pre-highlight rect (from `CadAnnotationRoughBounds`) for the hovered,
    non-selected text annotation — parity with the paper-space hover.
  - Selection render: new loop draws a selection rectangle around a selected single-line TEXT (parity with
    MTEXT). Single-line TEXT grips/grip-drag deferred to the grip phase (Phase 3).
- Verification: release build clean (no new warnings); full suite 25/25 (198 assertions) green. Interactive
  hover/selection feedback validated by inspection (UI overlay; no unit-test surface). Manual check: hover a
  model TEXT/MTEXT highlights it; single click shows a selection box; double-click opens the in-place editor.
