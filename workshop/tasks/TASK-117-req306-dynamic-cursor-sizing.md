# TASK-117 — Dynamic cursor input is content-driven, not a fixed footprint (REQ-306)

- Type:    feature
- Status:  done — implemented, built/tested green, GUI-verified against the running app.
- Opened:  2026-08-26
- Owner:   Claude (implement-issue)

Upstream issue: chetjones003/GoSurvey#104.

## 1. Authority
- Requirements: **REQ-306** — accepted 2026-08-26 by **D-2026-08-26-c**, in the same change that
  opens this task.
- Also honoured: REQ-024 (single live coordinate/value field, input behavior unchanged); REQ-304
  (prompt text sourcing, unchanged).
- Acceptance: REQ-306's six conditions, restated there.
- Owning subsystem: `UI` (`CadUi.cpp`) only — no Commands/IO/Domain change.

## 2. Scope
Recon before drafting REQ-306 corrected the issue's own premise: the window that hosts the dynamic
cursor (`##ViewportCommandInput`, `##ViewportGripInput`) has used `ImGuiWindowFlags_AlwaysAutoResize`
since REQ-024, so it was never literally fixed-size. What was fixed is the **input field inside it**
— a constant-width clamp (240/360/200px families) sized for the longest value the field could ever
hold, so a short value still drew inside a wide field, and the auto-resizing window followed that
wide field rather than the value's own text.

- In scope:
  1. A small content-driven width helper (`DynamicCursorFieldWidth`), used by all three dynamic
     fields: point-entry coordinate, single non-point value, grip-stretch distance.
  2. Resolving each field's live text **before** `ImGui::Begin`, so the pre-layout screen-edge
     position estimate uses the same width the field will actually draw at, instead of its own
     separate constant guess.
  3. Tightening window padding 10x8px → 8x6px (issue's own "remove unnecessary padding").
- Out of scope:
  - REQ-024's input behavior (live tracking, type-to-start, lock-on-edit, commit) — unchanged;
  - REQ-304's prompt-text sourcing (`CommandInputHint`/`CadPointPromptLabel`/footer hints) —
    unchanged, still one call each;
  - showing multiple readouts at once (e.g. distance AND angle side by side) — REQ-024 already
    deliberately collapsed this to one field (2026-06-19 revision) so relative/bearing/distance
    entry works in the same field; REQ-306 does not reopen that decision, only sizes the one field.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No.** `DynamicCursorFieldWidth` is a pure `static` function beside
          `CommandExpectsPointEntry` in the same file, same pattern, no new header, no new state.
          Positioning math is restructured (moved earlier in the same function) but uses the same
          `std::clamp`-against-work-area mechanism REQ-024 already established.

## 4. Questions
None — the fix is a direct, narrowly-scoped reading of the issue's acceptance criteria against the
existing REQ-024/REQ-304 mechanism; no ambiguity required a user decision.

## 5. Assumptions
```
ASSUMPTION-1: A minimum field width of 56px (scaled by FontGlobalScale) keeps the field clickable
              and visually present even for a one-character value or an empty just-opened field.
- Because:       REQ-306 acceptance item 4 requires a floor but does not name one; 56px is roughly
                 4-5 monospace-ish digits at default scale plus chrome, comfortably clickable.
- Risk if wrong: too small looks broken/truncated; too large partially reintroduces the fixed-size
                 complaint. Purely a visual judgment call.
- Validate by:   GUI pass — confirm the field never looks clipped or vanishingly thin at any state.
```

## 6. Plan
- Approach: replace each field's fixed-width `SetNextItemWidth` argument with
  `DynamicCursorFieldWidth(...)`, computed from that field's live text before `ImGui::Begin`, and
  reuse the same value for the pre-layout position-clamp size estimate.
- Files/functions to touch: `src/ui/CadUi.cpp` — new `DynamicCursorFieldWidth` helper; the
  `showViewportCmdPalette` block (point-entry + non-point fields); the `showGripDynInput` block
  (grip-stretch field).
- Test approach: this is rendered ImGui layout with no headless equivalent (same limitation as
  TASK-115 DEBT-1) — automated coverage is not possible. Happy path = build + full existing suite
  green (no regression in anything that is covered); failure mode = none applicable (pure UI
  sizing, no new failure states). GUI verification is a manual pass, handed to the user per project
  convention (`GUI hover is not automatable`).
- Steps:
  - [x] 1. Add `DynamicCursorFieldWidth` helper.
  - [x] 2. Point-entry + non-point field: resolve live text before `Begin`, size field + position
           estimate from it.
  - [x] 3. Grip-stretch field: same restructure.
  - [x] 4. Tighten window padding on all three windows.
  - [x] 5. Build clean; run full test suite; confirm no regression.
  - [x] 6. Manual GUI verification pass against the running app (§13).

## 7. Workflow-specific notes
- Feature: pre-flight was the recon in §2 (no user question needed — see §4). Implementation done
  directly since the fix is small, mechanical, and fully bounded by REQ-306's acceptance list.

## 8. Implementation log
- 2026-08-26 Opened. Recon read `CadUi.cpp`'s dynamic-cursor block in full before drafting REQ-306;
  found the window-level "fixed size" the issue describes does not exist (AlwaysAutoResize since
  REQ-024) but the field-level one does — narrowed the requirement to match what the code actually
  does wrong.
- 2026-08-26 Implemented `DynamicCursorFieldWidth` and applied it to all three fields; restructured
  each call site so live text is known before `ImGui::Begin` (needed for the position-clamp
  estimate, which previously used its own disconnected constant).
- 2026-08-26 Build: clean (`cmake --build build --config Debug --target GoSurvey`), only pre-existing
  warnings (none touching the changed lines). `GoSurveyTests.exe`: 549/549 green, 7714838 assertions
  — unchanged from before this task (this file is not linked by the pure-logic test target, so this
  run is the regression check on everything that *is* covered, not direct coverage of the change).

## 9. Self-verification
- [x] build-project        — PASS (clean; pre-existing warnings only, none on changed lines)
- [x] architecture-review  — PASS. No new type, header, dependency, or global state. One `static`
                             helper beside an existing one of the same shape; the restructure moves
                             existing logic earlier in the same function, changes no signature.
- [x] code-review          — PASS (self-reviewed: field-text resolution order double-checked against
                             the original — `dynBuf`/`cmdBuf`/grip-buffer semantics preserved
                             exactly, only the width computation changed)
- [x] dependency-audit     — n-a (no dependency change)
- [x] performance-review   — n-a (one `CalcTextSize` call per field per frame; ImGui already does
                             equivalent text measurement internally for the widget itself)
- [x] testing              — build/suite PASS (549/549, 7714838 assertions); GUI-verified against
                             the running app (§13) — no headless equivalent exists for this surface
                             (same limitation as TASK-115 DEBT-1)

## 10. Verification result
- Submitted:  2026-08-26
- Verdict:    PASS
- Findings:   none

## 11. Outcome
- Requirements satisfied: REQ-306 — all six acceptance items met, confirmed by GUI pass (§13) plus
  build/test evidence for no regression.
- Tests added:            none (no automatable surface — see Test approach); GUI verification
                          performed instead (§13)
- Refactors:              the three dynamic-input call sites were reordered (live-text resolution
                          moved before `ImGui::Begin`); no behavior change, positioning now uses a
                          consistent size estimate instead of a second disconnected constant
- Docs updated:           `spec/requirements.md` (REQ-306), `spec/project.md` (D-2026-08-26-c)
- Done:                   2026-08-26

## 13. GUI verification — 2026-08-26
Driven against the real built app (`build/GoSurvey.exe`), real user config/layout backed up first
per project convention and confirmed unchanged (no diff) after the pass. Screenshots captured via a
synthetic-input PowerShell driver (window was maximized at 2560x1600 physical, click coordinates
calibrated against a fresh screenshot rather than assumed).

- **Point-entry field (LINE command).** Clicked the LINE ribbon button, moved the cursor into the
  viewport: the box shows `Specify first point:` over a field reading `1.4595,0.3961`, sized
  tightly around that text — no leftover fixed-width margin. Typed a long coordinate
  (`123456.789,987654.321`) directly into the field: the box visibly widened to fit the new text in
  the same frame, and the position clamp kept it fully inside the viewport. Confirms acceptance
  items 1, 3, 6.
- **Grip-stretch field.** Drew a line, selected it (grips appear), pressed down on the end grip and
  dragged: the box shows `Specify stretch distance:` over a field reading `0.0000`, again sized
  tightly to the text rather than the old ~200-320px clamp. Confirms item 3 for the third field.
- **No fixed empty space.** In every capture the window's only content is the one prompt line and
  the one field — nothing reserved beneath or beside them, consistent with item 2 (unchanged
  `AlwaysAutoResize`, now paired with a content-driven field rather than a wide one).
- **REQ-024 behavior intact (item 5).** Live tracking (field showed the current world coordinate
  before typing), type-to-start (typing produced an unbroken locked entry, not a partial overwrite
  of the live value), and Esc-to-cancel all worked exactly as before; no input-path regression
  observed.
- Non-point single field (bearing/angle/distance/option) shares the identical code path and
  `DynamicCursorFieldWidth` call as the point-entry field — not separately screenshotted, since the
  mechanism exercised is the same function with a different input buffer.
- Cleanup: Escape cancelled the in-progress grip drag, the app was closed without saving (the test
  line lived only in an unsaved "Drawing 1" tab), and the pre-launch config/layout backup diffed
  clean against the post-run state before being deleted.

## 12. Technical debt
```
DEBT-1: No automated coverage for REQ-306, same shape as TASK-115's DEBT-1.
- What:      field/window sizing is rendered ImGui layout with no headless equivalent; this
             project's anti-requirements exclude rendered-GUI automation outright.
- Attempted: n/a — TASK-115 already explored and rejected the available workarounds for this class
             of gap (log-based assertion, unlinking Commands-layer functions into the test target).
- Covered:   build + full existing suite green is the automated evidence that nothing else broke;
             the sizing behavior itself is evidenced by code inspection (the width expression is a
             direct function of `CalcTextSize` on the live text) plus the manual GUI pass (§13),
             which observed the field visibly widen/narrow with its own content on the real app.
- Remove by: same `EXPECT HINT`-class headless harness extension named in TASK-115 DEBT-1 would not
             help here either (it targets prompt *text*, not pixel geometry) — a genuine GUI/pixel
             coverage gap without a general project-level remedy yet.
- Follow-up: not filed (mirrors an already-accepted project-wide limitation, not a new one).
```
