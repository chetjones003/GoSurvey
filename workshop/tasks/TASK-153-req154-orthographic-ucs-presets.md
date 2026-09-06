# TASK-153 — Orthographic UCS presets in the View-tab / ViewCube frame selectors

- Type:    feature
- Status:  submitted
- Opened:  2026-09-06
- Owner:   chetjones003

## 1. Authority
- Goal:         GOAL — a familiar CAD drafting surface (3D model space, ADR-025)
- Requirements: REQ-154 (`accepted`; revised 2026-09-06 per D-2026-09-06-a)
- Constraints:  Must not break existing functionality; follow architecture + coding standards;
  no new dependency.
- Acceptance (REQ-154, the clauses this task touches):
  - "The two selectors offer an identical list" of `WCS`, the six orthographic presets, every
    named UCS, and `New UCS`.
  - "A preset is an ordinary UCS change: ORTHO, the grid and `UCSFOLLOW` treat it exactly as any
    other frame." (REQ-154: "ORTHO squares to the UCS axes; the grid is generated in the UCS
    plane" — already verified for arbitrary frames.)
  - "changing the UCS leaves every stored coordinate untouched".
  - "the coordinate readout and `ID` report in the active UCS and name which frame that is" — the
    label names the preset for the five non-World presets.
- Owning subsystem: UI (`src/ui/CadUi.cpp`), with the shared preset table in the pure `util/ucs`
  module (`src/util/ucs.hpp`) — the same placement REQ-154 (a) chose for the frame type.

## 2. Scope
- In scope:
  - `ucs::OrthographicPresets()` — one shared constant table of the six `{name, Ucs}` presets.
  - Both frame-selector menus (View-tab combo ~CadUi.cpp:5107; ViewCube dropdown ~CadUi.cpp:17099)
    gain a separator + the six preset entries between `WCS` and the named-UCS list.
  - `CadUcsFrameLabel` returns the preset name when `activeUcs` matches a non-World preset.
  - Unit tests in `tests/UcsTests.cpp`.
- Out of scope:
  - A `UCS`-command sub-option or command-line keyword for the presets (selector-only, like the
    existing `WCS` entry which also calls `SetActiveUcs` directly).
  - `UCSBASE` (presets relative to a base UCS) — AutoCAD has it; not requested, presets are
    always world-relative here.
  - Any new icon art — the menu entries are text, matching the existing `WCS` / `New UCS` items.
  - Persistence — presets are computed; no `.gs`/DWG change.
- Smallest change: a 6-row constant + a loop in two existing menus + one lookup in one label fn.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global / public-API / data-format / new
  algorithm?
    - [x] No — proceed. The preset table is a constant beside `ucs`'s existing constructors; the
      menu loop mirrors the existing `kPresets` table in the Named Views combo and the existing
      `ucsNamed` loop right next to the insertion point. `SetActiveUcs` is the existing entry
      point already used by the `WCS` menu item. D-2026-09-06-a records the user-facing decision.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | New separate ribbon control, or extend the existing REQ-154 combo (both copies)? | 2026-09-06 | Extend the existing combo, both copies. |
| Q2 | With `UCSFOLLOW=1` a preset also swings the camera — keep or suppress for presets? | 2026-09-06 | Keep existing UCSFOLLOW behaviour; do not special-case presets. |

## 5. Assumptions
```
ASSUMPTION-1: "Top" and "WCS" are the same menu outcome (identical frame); "Top" is offered as a
              familiar label but selecting it sets the WCS and the label then reads "WCS".
- Because:       Top's orthographic frame IS the WCS; FramesMatch / IsWorld cannot tell them apart.
- Risk if wrong: user expects the label to say "Top". Low — AutoCAD shows "Top" only because its
                 Top can differ from World via UCSBASE, which is out of scope here.
- Validate by:   noted in D-2026-09-06-a; user confirmed the extend-the-combo approach.
```

## 6. Plan
- Approach: add the shared constant to `util/ucs.hpp`; consume it in the two menus and the label
  helper in `CadUi.cpp`. No behaviour path other than "feed a known-good frame to `SetActiveUcs`".
- Files/functions to touch:
  - `src/util/ucs.hpp` — `struct OrthoPreset`, `OrthographicPresets()`.
  - `src/ui/CadUi.cpp` — `CadUcsFrameLabel` (preset lookup); the `##RibbonUcsPick` combo; the
    `##ucsdropmenu` popup.
  - `tests/UcsTests.cpp` — new `TEST_CASE`s.
- Test approach:
  - happy path = each preset is right-handed orthonormal; each preset's axis mapping matches the
    D-2026-09-06-a table (assert `xAxis`/`yAxis`/`zAxis` and `WorkPlane` normal); `Top` frame
    `IsWorld`; `FramesMatch` is unique across the six + a couple of rotated frames.
  - failure mode = a rotated-about-Z survey UCS matches **no** preset (guards the label lookup
    from mislabelling a user frame "Front").
- Steps:
  - [x] `ucs::OrthographicPresets()` + tests, run green
  - [x] wire both menus
  - [x] `CadUcsFrameLabel` preset lookup
  - [x] build + full test run
  - [x] self-verify + completion report

## 7. Workflow-specific notes
- Feature: tests-first for the pure table (done before the UI wiring). The UI menus are not
  headless-testable (synthetic hover/click never produces a hovered frame — project memory); the
  behaviour they invoke (`SetActiveUcs` + grid/ORTHO follow) is covered by REQ-154's existing
  `UcsTests` + `req154-ucs-plan` transcript, which this task does not modify.

## 8. Implementation log
- 2026-09-06 open → plan (Authority + Q1/Q2 answered) → implement → build+854/854 → submitted.
- 2026-09-06 PR #370 (feat/view-tab-coordinate-systems → beta). Hands-on testing confirmed the
  feature; surfaced three PRE-EXISTING 3D bugs unrelated to this change, filed as #371 (ORTHO not
  following a rotated UCS — may need a spec decision), #372 (object snaps in orbited views),
  #373 (JOIN drops non-coplanar 3D polyline segments).

## 9. Self-verification
- [x] build-project        — PASS (`build.bat` ninja-release, 253/253, GoSurvey.exe linked)
- [x] architecture-review  — PASS. New code: a 6-row constant in the pure `ucs` module (beside its
      existing constructors) + a loop in two existing menus + one lookup in one label fn. No new
      abstraction/layer/dependency/global/ownership/API/data-format change. D-2026-09-06-a records
      the user-facing decision; REQ-154 revised.
- [x] code-review          — PASS. The menu loop mirrors the adjacent `ucsNamed` loop and the
      Named-Views `kPresets` table; both selectors consume the one shared table so they cannot
      drift (REQ-154). "Top" == WCS is intentional and documented (ASSUMPTION-1).
- [x] dependency-audit     — n-a (no dependency change)
- [x] performance-review   — n-a (a 6-entry loop only while a combo popup is open)
- [x] testing              — PASS (854/854 ctest; 5 new `UcsTests` cases + existing
      `req154-ucs-plan` transcript green)

## 10. Verification result
- Submitted:  2026-09-06
- Verdict:    PASS
- Findings:   none

## 11. Outcome
- Requirements satisfied: REQ-154 (Acceptance met: yes — see §1)
- Tests added:            UcsTests: "The orthographic UCS presets are the six standard views,
                          named in order"; "Every orthographic preset is a right-handed
                          orthonormal frame at the world origin"; "The Top preset is exactly the
                          World Coordinate System"; "Each orthographic preset makes the named
                          world face the XY work plane"; "The orthographic presets are all
                          distinct, and a survey UCS matches none of them"
- Refactors:              none
- Docs updated:           spec/project.md (D-2026-09-06-a), spec/requirements.md (REQ-154 +
                          revision line), workshop/tasks/TASK-153
- Done:                   2026-09-06
