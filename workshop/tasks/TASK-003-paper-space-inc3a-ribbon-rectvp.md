# TASK-003 — Paper Space Inc 3a: Layout ribbon + Rectangular viewport command

- Type:    feature
- Status:  done
- Opened:  2026-06-15
- Owner:   Workshop

## 1. Authority
- Requirements: REQ-032 (contextual Layout ribbon), REQ-033 (rectangular viewport command) — both accepted
- Constraints:  no broken functionality; ADR-006/ADR-008; coding standards; no new dependency
- Acceptance:
  - REQ-032: paper space shows the Layout ribbon with the viewport commands; model space shows the normal ribbon.
  - REQ-033: start the command, click two corners → a viewport of that rectangle; rubber-band preview tracks the cursor; Esc before the second click creates nothing.
- Owning subsystem: UI + Commands (per ADR-008)

## 2. Scope
- In scope: contextual Layout ribbon; `MVIEW`/`RECTVP` command (new Kind) with two-click create + preview; Esc cancel; command-line + ribbon entry; dynamic-input hint.
- Out of scope: polygonal viewport (3d); viewport selection/MOVE/COPY/DELETE (3b); floating model space (3c).

## 3. Architectural boundary check
- [x] No new architectural decision — new command Kind + paper-space input branch are exactly what ADR-008 authorized.

## 4/5. Questions / Assumptions
- Decisions carried from the REQ-032–036 interview (full mspace later, reuse MOVE/COPY/DELETE, incremental).
- ASSUMPTION: a new rectangular viewport defaults to the drawing's plot scale, centered on worldDocumentOrigin (model centroid).

## 6. Plan → done
- PaperRectViewport Kind + draft state (phase, first corner); StartPaperRectViewportCommand; AddViewportRect.
- Registry "mview" (rectviewport/rectvp) + DispatchByPrimary; CancelActiveCommand reset; CommandInputHint.
- Contextual Layout ribbon (model sections gated to model space; Layout section in paper space).
- Paper-space click handler (screen→paper inch) + rubber-band preview in the overlay.

## 8. Implementation log
- 2026-06-15 Kind::PaperRectViewport + draft state; Start/AddViewportRect; registry+dispatch; cancel reset.
- 2026-06-15 CadUi: contextual Layout ribbon; two-click handler + preview; dynamic-input hint.
- 2026-06-15 build clean (after stopping a running instance that locked the exe); tests green (20 cases).

## 9. Self-verification
- [x] build-project        — PASS (clean)
- [x] architecture-review  — PASS (command Kind + paper input branch per ADR-008; no new dependency/global)
- [x] code-review          — PASS (reuses command/ribbon/overlay patterns; readable)
- [x] dependency-audit     — PASS / n-a
- [x] performance-review   — PASS / n-a (preview = one rect)
- [x] testing              — PASS (existing paper-space tests green; command flow verified manually)

## 10. Verification result
- Verdict: PASS — no blocking findings

## 11. Outcome
- Requirements satisfied: REQ-032, REQ-033 (Acceptance met: yes)
- Tests added: none new (UI/command flow; manual) — data-model tests already cover Viewport
- Docs updated: none (commands listed in-app; gs format already documents viewports)
- Done: 2026-06-15
