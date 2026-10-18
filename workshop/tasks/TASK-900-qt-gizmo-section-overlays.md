# TASK-900 — Port the 3D gizmo and section-plane overlays to Qt

- Type: feature
- Status: implement
- Opened: 2026-10-18
- Owner: Workshop
- GitHub: #900, child of #878

## 1. Authority
- Goal: Qt migration to full 0.7.4 parity, authorized by the user in the
  `test/win32UI` migration task (2026-10-16; scope reaffirmed 2026-10-18).
- Requirements: REQ-060, REQ-341, REQ-342, REQ-343, REQ-344; failures and
  refusals follow REQ-201; coordinate tolerance follows REQ-101.
- Constraints: REQ-300 (no new dependency), REQ-301 (no one-use abstraction),
  Qt migration remains additive and must not replace the production executable.
- Acceptance:
  - Qt renders and supports the existing gizmo and section-plane workflows to
    the cited requirements, including handle hover/drag, snapping, cancel,
    refusal, undo, and read-only behavior.
  - Shared command state and renderer behavior remain authoritative; Qt does not
    implement a second transform or section-plane model.
  - Add focused Qt workflow coverage and update `docs/qt-ui-migration.md`'s
    parity ledger.
  - Present 2–3 integrated screenshot options for the affected viewport surface;
    apply the user's selected direction before closing #900.
- Owning subsystem: Qt presentation/input (`src/qt/EditorViewport`) over shared
  Commands, Viewport policy, and Render.

## 2. Scope
- In scope: render the shared gizmo, section clip/plane, grips and previews in
  the Qt viewport; route Qt pointer motion and clicks through the existing
  command and viewport-pick APIs; add focused Qt tests and update the parity
  ledger.
- Out of scope: changing command semantics, geometry, undo, persistence,
  production default, removing ImGui, or adding a dependency.
- Smallest change: populate existing `RenderTuning` and gizmo inputs in the Qt
  render call, then route viewport events to existing shared pick/drag helpers.

## 3. Architectural boundary check
- [x] No new abstraction, layer, dependency, ownership change, global state,
  public API, or data format. The migration architecture is already authorized
  in `workshop/tasks/TASK-win32UI.md`; this task uses its existing shared domain
  and renderer APIs.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| — | Scope of “full Qt parity” | 2026-10-18 | Apply all existing requirements and issue #900 acceptance to this Qt workflow. |

## 5. Assumptions
- None.

## 6. Plan
- Approach: Mirror the existing viewport's overlay-state assembly and pointer
  routing inside `EditorViewport`, reusing shared helpers and renderer paths.
- Files/functions to touch: `src/qt/EditorViewport.cpp/.hpp`,
  `tests/QtEditorTests.cpp`, `docs/qt-ui-migration.md`.
- Test approach: Qt integration tests cover render tuning/state, gizmo and
  section-plane hit routes, drag/cancel and snap-sensitive behavior; run the Qt
  tests plus the existing domain suites.
- Architectural impact: UI/input adapter only; no new dependency or domain API.
- Risks: Qt logical-pixel vs framebuffer-pixel coordinates; section-plane
  drags must use the snapped point in the existing local storage frame; Qt's
  framebuffer/context must remain valid for the current shared renderer.
- Steps:
  - [x] Trace shared render and pointer APIs; confirm the additive Qt branch.
  - [x] Verification approves this plan against REQ-060/341–344 and the Qt
        migration boundary.
  - [x] Wire render state for gizmo, section plane, hover and drag visuals.
  - [x] Route Qt click/move/release and cancel through existing shared interaction APIs.
  - [x] Add focused Qt workflow tests and update the parity ledger.
  - [x] Build and run relevant checks; capture 2–3 integrated viewport options
        for the user's visual direction.

## 7. Workflow-specific notes
- Feature: user supplied title/scope/acceptance in issue #900 and reaffirmed
  full Qt parity. Verification: APPROVE — ownership and behavior are specified
  by existing accepted requirements; plan stays inside the authorized Qt
  frontend and existing shared APIs. No architecture decision is needed.

## 8. Implementation log
- 2026-10-18 — Confirmed Qt branch `origin/test/win32UI` contains the authorized
  migration frontend; current task branch now tracks it. Baseline Qt viewport
  supplies `nullptr` for the gizmo and default-inactive section-plane tuning,
  and pointer events bypass the shared route table.
- 2026-10-18 — Wired the shared renderer overlays and hit/drag/commit/cancel
  routes into Qt. Added framebuffer and interaction coverage; gizmo and section
  plane gesture tests pass with cancellation, commit and undo. Updated the Qt
  parity ledger and captured three viewport screenshot options. The full Qt
  suite still fails in the unrelated editor chrome focus test noted above;
  built the shared test target and confirmed gizmo/section-plane suites pass
  (175 assertions, 16 cases).
- 2026-10-18 — User selected screenshot option A. Applied as the approved Qt
  viewport presentation: emphasize the transform gizmo while an entity is
  selected; section-plane overlays remain active for their workflow.

## 9. Self-verification
- [x] build-project
- [x] architecture-review
- [x] code-review (self-review of changed Qt input/render paths and focused tests)
- [x] dependency-audit: n/a (no manifest change)
- [x] performance-review (no new per-frame allocations beyond the renderer's
      existing vector-based overlay assembly; feature stays on existing path)
- [x] testing (focused issue #900 Qt slots pass; full `qt.editor` suite had an
      unrelated focus-routing failure in `tabsAppearInitiallyAndTypingRoutesFromEditorChrome`)

## 10. Verification result
- Submitted: 2026-10-18
- Verdict: APPROVE (shared command/render APIs remain authoritative; final build and
  focused workflow checks pass)
- Findings: full `qt.editor` suite still contains the editor chrome tree-focus
  failure recorded in §9; the issue #900 focused Qt and shared domain checks pass.

## 11. Outcome
- Requirements satisfied: implementation and focused workflow coverage complete;
  visual direction A selected and applied.
- Tests added: viewport overlay render, gizmo commit/cancel/undo, section-plane
  grip commit/cancel/undo.
- Docs updated: `docs/qt-ui-migration.md` parity ledger.
- Done: implementation complete; GitHub issue state left unchanged.
