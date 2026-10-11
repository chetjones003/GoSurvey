# TASK-004 — Paper Space Inc 3b: viewport selection + MOVE/COPY/DELETE

- Type:    feature
- Status:  done
- Opened:  2026-06-15
- Owner:   Workshop

## 1. Authority
- Requirements: REQ-035 (accepted) — viewports selectable; MOVE/COPY/DELETE + grips operate on them.
- Constraints:  no broken functionality; ADR-008; coding standards; no new dependency.
- Acceptance: clicking a viewport selects it (window-select selects those inside); MOVE relocates,
  COPY duplicates, DELETE removes; grips move/resize it.
- Owning subsystem: UI + Commands (per ADR-008).

## 2. Scope
- In scope: paper-space selection set; single click + Shift toggle + window-select + hover; grips
  (corner = resize, center = move, click-grab → follow → click-commit); MOVE/COPY two-click on
  selected viewports with ghost preview; DELETE (Del key / command) of selected; Esc cancels.
- Out of scope: floating model space (3c); polygonal viewport (3d); layer freeze (3e).

## 3. Architectural boundary check
- [x] No new architectural decision — paper-space selection set + MOVE/COPY/DELETE branch are exactly
      what ADR-008 authorized. No new dependency/global; state on AppCommandState.

## 6. Plan → done
- selectedViewports vector + IsViewportSelected/SelectViewport/ClearViewportSelection.
- DeleteSelectedViewports / TranslateSelectedViewports / StartPaperMoveCopyViewports.
- StartMove/Copy/DeleteCommand branch to paper variants when in paper space.
- Paper-space input: grip hit-test + body hit-test + window box + live grip-follow; MOVE/COPY phases.
- Render: highlight all selected; corner + center grips on single selection; MOVE/COPY ghost + window box.
- Transient selection cleared on tab switch.

## 8. Implementation log
- 2026-06-15 selection set + helpers; MOVE/COPY/DELETE branch on activeSpace; input + render previews.
- 2026-06-15 build clean (stopped a running instance to relink); tests green (20 cases).

## 9. Self-verification
- [x] build-project        — PASS (clean)
- [x] architecture-review  — PASS (ADR-008; no new dependency/global; reuses command surface)
- [x] code-review          — PASS (hit-test/grip logic localized; readable)
- [x] dependency-audit     — PASS / n-a
- [x] performance-review   — PASS / n-a (hit-tests are O(viewports); preview a few rects)
- [x] testing              — PASS (existing paper tests green; select/grip/move/copy/delete verified manually)

## 10. Verification result
- Verdict: PASS — no blocking findings

## 11. Outcome
- Requirements satisfied: REQ-035 (Acceptance met: yes)
- Tests added: none new (interaction; manual) — data-model tests already cover Viewport
- Docs updated: none (no format change; selection is transient state)
- Done: 2026-06-15
