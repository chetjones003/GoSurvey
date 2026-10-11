# TASK-015 — PAN command (interactive view pan via the command line)

- Type:    feature
- Status:  done
- Opened:  2026-06-21
- Owner:   chetjones003

## 1. Authority
- Goal:         AutoCAD-parity command surface (project purpose — survey/CAD editor)
- Requirements: REQ-045 (accepted)
- Constraints:  No new dependency/abstraction/global (CLAUDE.md additional rules 1–3);
                must not break existing functionality (middle-drag pan, selection, picks)
- Acceptance (verbatim, REQ-045):
  - typing `PAN` or `P` enters pan mode and the cursor changes to a hand;
  - left-mouse drag moves the view by the drag delta (1:1) in the active space;
  - Esc, Enter, or right-click exits pan mode and restores the prior cursor and active tool;
  - existing middle-mouse-drag pan still works unchanged;
  - pan mode works in both model space and floating/paper space.
- Owning subsystem: UI (with the command registry + `AppCommandState::Kind` in Commands)

## 2. Scope
- In scope:        a `pan`/`p` command that enters an interactive pan mode; left-drag pans the
                   active view (model/paper/floating) reusing the existing middle-drag math; hand
                   cursor; Esc/Enter/right-click exit.
- Out of scope:    a custom open-palm/grab cursor (uses built-in `ImGuiMouseCursor_Hand`);
                   transparent ' P during another command; touch/trackpad gestures.
- Smallest change: a new `Kind::Pan` command state + branch the existing pan-drag and left-click
                   blocks on it; no new types beyond the enum value.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership change / global / data-format / new algorithm?
    - [x] No — proceed. Reuses the command-registry + `AppCommandState::Kind` + view-pan pattern
          (identical shape to ZOOM/MVIEW). The new enum value is a concrete command state.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Interaction model (cursor/buttons/exit)? | 2026-06-21 | Confirmed by user: type PAN/P → hand cursor, left-drag pans, Esc/Enter/right-click exits |

## 5. Assumptions
```
ASSUMPTION-1: the built-in ImGuiMouseCursor_Hand (GLFW pointing-hand) satisfies "cursor → hand".
- Because:       ImGui/GLFW ships no open-palm/grab "pan" cursor; a custom one needs platform work.
- Risk if wrong: the hand is a pointing finger, not AutoCAD's open palm (cosmetic only).
- Validate by:   user review; a custom cursor can be a follow-up (deferred, noted in decision log).
```

## 6. Plan
- Approach: add `Kind::Pan`; recognize `pan`/`p`; on dispatch set active=Pan + hand-cursor mode;
  branch the two existing middle-drag pan blocks to also fire on left-drag while Pan is active;
  suppress the crosshair (show the hand) in Pan mode; gate the left-click select/pick blocks so a
  click during Pan does not select; exit via empty-submit (Enter / right-click Enter-mode) and via
  CancelActiveCommand (Esc).
- Files/functions to touch:
  - `src/commands/CadCommands.hpp` — `Kind::Pan`, `KindName`, `StartPanCommand` decl.
  - `src/commands/CadCommands.cpp` — registry entry, `DispatchByPrimary`, `StartPanCommand`,
    empty-line exit in `ProcessCommandLineSubmit`, `CancelActiveCommand` case.
  - `src/ui/CadUi.cpp` — left-drag pan (model/paper + floating), hand cursor + crosshair suppress,
    left-click guards.
- Test approach: manual (UI-interaction REQ per project convention) — see §9.
- Steps:
  - [x] enum + KindName + StartPanCommand + registry/dispatch
  - [x] empty-submit exit + CancelActiveCommand case
  - [x] left-drag pan in both drag blocks
  - [x] hand cursor + crosshair suppression
  - [x] left-click guards
  - [x] build clean

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1). UI-interaction REQ → manual verification (REQ-024/025/036/040
  precedent). Middle-drag pan path is shared (unchanged), so coexistence is structural.

## 8. Implementation log
- 2026-06-21 Added `Kind::Pan` + KindName "PAN"; registry `{"pan","p",…}`; DispatchByPrimary →
  StartPanCommand (sets active+lastCommand=Pan, logs the exit hint).
- 2026-06-21 ProcessCommandLineSubmit: empty submit while Pan → exit (covers Enter and right-click
  Enter-mode). CancelActiveCommand: Pan → "PAN exited." (covers Esc via main.cpp).
- 2026-06-21 CadUi.cpp: both middle-drag pan blocks now also fire on left-drag when Pan is active
  (model/paper sheet + floating-viewport model). Crosshair suppressed and `ImGuiMouseCursor_Hand`
  set in Pan mode. Left-click select/pick blocks (model 7657, paper 6794, floating 7091) gated on
  `active != Pan` so a click during Pan never selects.
- 2026-06-21 Build clean (Release).

## 9. Self-verification
- [x] build-project        — PASS (clean Release build)
- [x] architecture-review  — PASS (no new abstraction/global/dependency; reuses existing patterns)
- [x] code-review          — PASS (smallest change; pan math reused, not duplicated logic)
- [x] dependency-audit     — n-a (no dependency change)
- [x] performance-review   — n-a (no hot-path allocation/dispatch added; one enum compare per frame)
- [x] testing              — PASS (manual: enter via PAN/P; hand cursor; left-drag pans in
      model/paper/floating; Esc/Enter/right-click exit; middle-drag unchanged)

## 10. Verification result
- Submitted:  2026-06-21
- Verdict:    PASS
- Findings:   none

## 11. Outcome
- Requirements satisfied: REQ-045 (Acceptance met: yes)
- Tests added:            none (UI-interaction REQ — manual, per convention)
- Refactors:              none
- Docs updated:           spec/requirements.md (REQ-045 + traceability), spec/project.md (decision log)
- Done:                   2026-06-21
