# TASK-005 — Paper Space Inc 3c: floating model space

- Type:    feature
- Status:  done
- Opened:  2026-06-15
- Owner:   Workshop

## 1. Authority
- Requirements: REQ-036 (accepted) — double-click a viewport to edit the model through it; leaving returns to paper.
- Constraints:  no broken functionality; ADR-008; coding standards; no new dependency.
- Acceptance: double-clicking a viewport activates it; drawing/snapping/editing apply to model space (at
  the viewport's scale); leaving returns to paper space with the model unchanged outside the edits.
- Owning subsystem: UI + Commands (per ADR-008).

## 2. Scope
- In scope: double-click enter; reuse the full model edit/snap pipeline for the viewport's view; navigate
  to set the viewport view; Esc / PAPER button / PSPACE to exit (writes view back to the viewport);
  MSPACE/PSPACE commands; floating banner + edge accent.
- Out of scope: in-place editing inside the viewport rect while the sheet stays visible — see debt.
- Smallest change: entering floating space switches activeSpace to model with the viewport's center/scale
  (so all existing model tools work), and exit restores the paper view + writes the framing back.

## 3. Architectural boundary check
- [x] No new architectural decision — floating model space is the ADR-008 mode. Implemented by reusing
      the model pipeline (no new render path); no new dependency/global.

## 5. Assumptions
```
ASSUMPTION-1: Entering "maximizes" the viewport's model view to the window (VPMAX-style MSPACE), rather
  than editing in-place within the rect while the sheet is visible.
- Because:       in-place editing requires routing the model command/snap/render through the viewport
  transform + clip, which depends on the deferred GL per-viewport pass (ADR-006/008 debt).
- Risk if wrong: UX differs from AutoCAD's in-place MSPACE; the edit result is identical (same model).
- Validate by:   user feedback; revisit when the GL viewport pass lands.
```

## 6. Plan → done
- Fields: floatingViewportLayout/Index + saved paper pan/zoom.
- EnterFloatingModelSpace (view = viewport center/scale; activeSpace→model) / ExitFloatingModelSpace
  (write framing back via zoom↔scale through paperHIn; restore paper view).
- Double-click enter in paper input; Esc/PAPER button/PSPACE exit; MSPACE/PSPACE commands.
- Floating banner + viewport-edge accent; reset on tab switch.

## 8. Implementation log
- 2026-06-15 enter/exit + state; double-click enter; Esc + FLOAT button + PSPACE/MSPACE; banner.
- 2026-06-15 build clean (stopped running instance to relink); tests green (20 cases).

## 9. Self-verification
- [x] build-project        — PASS (clean)
- [x] architecture-review  — PASS (ADR-008 mode; reuses model pipeline; no new dependency/global)
- [x] code-review          — PASS (enter/exit localized; mapping documented)
- [x] dependency-audit     — PASS / n-a
- [x] performance-review   — PASS / n-a (reuses model render path)
- [x] testing              — PASS (existing tests green; enter/edit/exit verified manually)

## 10. Verification result
- Verdict: PASS — one tracked tech-debt item (in-place sheet-visible MSPACE via the GL viewport pass).

## 11. Outcome
- Requirements satisfied: REQ-036 (Acceptance met: yes — edit model through the viewport; leave returns to paper)
- Technical debt: in-place MSPACE (sheet visible, editing within the rect) deferred to the GL per-viewport
  transform/clip pass (shared with REQ-034 polygonal + perf); remove when that pass lands.
- Tests added: none new (interaction; manual)
- Docs updated: none
- Done: 2026-06-15
