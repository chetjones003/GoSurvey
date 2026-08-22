# TASK-070 — Right-Click Customization dialog + full drawing shortcut menu + object isolation

- Type:    feature
- Status:  done
- Opened:  2026-08-18
- Owner:   Workshop

## 1. Authority
- Goal:         GOAL-01 (a drafting tool a surveyor can actually drive)
- Requirements: REQ-084 (accepted 2026-08-18); touches REQ-054 (selection menu, unchanged),
                REQ-040 (command history reused for Recent Input), REQ-076 (stable entity ids),
                REQ-100 (frame budget), REQ-201 (no silent failures)
- Constraints:  CON-07 (artifacts to the build dir)
- Acceptance:   restated verbatim from REQ-084 "Acceptance" — see spec/requirements.md REQ-084.
- Owning subsystem: UI (dialog + menu + click timing), Commands (isolation state + commands +
  pick gates + ORBIT), Render (draw gates), IO (UserPrefs)

## 2. Scope
- In scope: the Right-Click Customization dialog and its two new preferences; time-sensitive
  right-click classification; the restructured idle shortcut menu incl. Recent Input; object
  isolation (Isolate / Hide / End) with render + pick gates; `Kind::Orbit` so Free Orbit is real.
- Out of scope: **Display Order** (deferred — data-format change, separate REQ); AEC Modify Tools
  (AutoCAD Architecture-only); isolation of survey points and PDF underlays; persisting isolation.
- Smallest change: reuse `cmdEnteredHistory` for Recent Input; reuse the existing
  `CadExtendedGeometryInput` to carry the hidden set (no `RenderScene` signature change); gate
  selection at the two existing pick funnels rather than at every call site.

## 3. Architectural boundary check
- New global state / data-model addition? **Yes** — `hiddenEntityIds` on `AppCommandState`, a new
  `Kind::Orbit`, and a field on `CadExtendedGeometryInput`.
    - [x] Escalated as a SPEC GAP **before** implementation and recorded as **ADR-034** in
          `spec/project.md` (2026-08-18), together with the acceptance of REQ-084.
- No new dependency. No new abstraction (no interface/trait added). No layer inversion: Render
  reads a plain `const std::vector<std::uint64_t>*` handed down from the app, exactly as it already
  reads `drawingLayers`.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Isolate Objects and Display Order have no underlying feature — how far to go? | 2026-08-18 | Build the menu + Isolate now; **defer Display Order** to its own REQ; drop AEC Modify Tools. |
| Q2 | Time-sensitive right-click changes when the menu opens (release/elapse, not press). Confirm, and default? | 2026-08-18 | Confirmed; ships **off by default** so existing profiles are unchanged. |

## 5. Assumptions
```
ASSUMPTION-1: Isolation is session-only and never written to `.gs`.
- Because:       REQ-084 (d) states it, following AutoCAD's OBJECTISOLATIONMODE default of 0.
- Risk if wrong: a user who isolates, saves and reopens expects the isolation back.
- Validate by:   REQ-084 acceptance ("saving with objects isolated and reopening shows every
                 object") — met; if the user later wants it retained that is a new REQ, not a
                 silent change here.

ASSUMPTION-2: Gating `PickClosestCadEntity`'s `consider` funnel and `ComputeSelectionFromRect`'s
              `hits` list covers every way a hidden entity could be selected.
- Because:       both are the single funnel their function has; hover, click-pick, offset pick,
                 trim pick and box-select all route through one of the two.
- Risk if wrong: an invisible entity answers a click somewhere — exactly the failure REQ-084 (d)
                 names as worse than not hiding at all.
- Validate by:   grep for direct entity-array walks that build a `SelectedEntity` outside these
                 two — done; the remaining ones are `PickCadAnnotationAt` and `PickFilledRegionAt`,
                 which are gated individually.
```

## 6. Plan
- Approach: three separable pieces, each inside its owning subsystem.
  1. **Preferences + dialog** (UI/IO): two new fields on `AppCommandState`
     (`rightClickTimeSensitive`, `rightClickLongerClickMs`), persisted by `UserPrefs`; a new
     `DrawRightClickCustomizationDialog` modelled on the existing `DrawUnitsDialog` (snapshot on
     open, Cancel reverts); the User Preferences tab's collapsing header becomes a button.
  2. **Click classification + menu** (UI): the viewport RMB block gains a time-sensitive branch
     that records the press time and decides on release-or-elapse; the popup body is rebuilt with
     the REQ-084 (c) structure.
  3. **Isolation** (Commands/Render): `hiddenEntityIds` + `IsolateSelectedObjects` /
     `HideSelectedObjects` / `EndObjectIsolation`; gates in the renderer's append loops, the
     annotation overlay, and the pick funnels.
- Files/functions to touch:
  - `src/commands/CadCommands.hpp` — new state, `Kind::Orbit`, `CadEntityIdHidden`,
    `CadEntityAttrsForSelected`, isolate/orbit declarations, `CadExtendedGeometryInput::hiddenEntityIds`
  - `src/commands/CadCommands.cpp` — isolate commands, attrs resolver, pick gates, `StartOrbitCommand`,
    `KindName`, Orbit exit in `ProcessCommandLineSubmit`/`CancelActiveCommand`
  - `src/io/UserPrefs.cpp` — persist the two preferences
  - `src/ui/CadUiSettings.cpp` — the dialog + the User Preferences button
  - `src/ui/CadUi.cpp` — time-sensitive classification, the new menu, the annotation draw gate,
    orbit left-drag
  - `src/render/ViewportRenderer.cpp` — draw gates for lines/circles/arcs/ellipses/polylines/
    filled regions/meshes
  - `src/app/main.cpp` — hand the hidden set to the renderer
  - `tests/RightClickTests.cpp` — unit tests for the pure parts
  - `tests/headless/HeadlessDriver.cpp` + a transcript — the isolation gates end-to-end
- Test approach: happy path = isolate hides and unpicks, end-isolation restores, the ms clamp and
  the time-sensitive classifier return the right verdicts. Failure mode = isolate with an empty
  selection reports and changes nothing; a hidden id whose entity was erased does not resurrect a
  different entity that reuses the slot; the classifier with the feature off never returns "menu on
  release".
- Steps:
  - [x] spec: REQ-084 + ADR-034 recorded before any code
  - [x] state + preferences + persistence
  - [x] dialog
  - [x] isolation commands + gates
  - [x] ORBIT command
  - [x] shortcut menu + time-sensitive classification
  - [x] tests
  - [x] build + self-verify

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1/Q2 above). Pure logic is tested first (`RightClickTests`); the
  ImGui surfaces follow the repo's UI-REQ convention of manual verification, as REQ-040 and
  REQ-044 did.

## 8. Implementation log
- 2026-08-18 Spec gap raised and closed: REQ-084 accepted, ADR-034 recorded. Status → implement.
- 2026-08-18 Found while planning: layer **off/frozen** is honoured for meshes and TIN surfaces but
  **not** for lines/arcs/ellipses/polylines/filled regions in the GL renderer — a pre-existing gap
  unrelated to this task. Not fixed here (out of scope, no REQ), recorded below as DEBT-1.
- 2026-08-18 `EntityAttributes`-carried "hidden" bit rejected during design (persisted + clipboard +
  DXF leak); id set chosen — see ADR-034.
- 2026-08-18 **Finding, self-review: `Find…` would have been a dead control.** GoSurvey's
  find/replace (`mtextFindReplaceOpen`) is rendered only inside the MTEXT text-formatting panel and
  searches only the buffer being edited — and the drawing shortcut menu cannot open while that
  editor is up (`blockSnapPickMenu`). The menu item would have set a flag nothing would read.
  Removed, REQ-084 revised to state the omission, and an acceptance condition added that no menu
  entry may be present that cannot act (REQ-201). Recorded as DEBT-3.
- 2026-08-18 **Finding, self-review: the hidden set was global, not per-drawing.** `hiddenEntityIds`
  first landed as a bare `AppCommandState` field. Entity ids are unique *within* a drawing
  (REQ-076), so isolating in one tab and switching to another would have hidden whatever objects
  happened to carry the same numbers there, and opening a `.gs` into a tab would have hidden
  arbitrary loaded geometry. Fixed by (a) storing it in `DrawingDocument` alongside `selection`, so
  it saves/restores with the tab, and (b) clearing it in `ClearCadGeometry`, which every load runs
  and which is also where the id space restarts. The `.gs` round-trip is now covered by the
  transcript's last section.
- 2026-08-18 **Finding, self-review: Recent Input mutated the vector it was iterating.**
  Re-submitting an entry appends to `cmdEnteredHistory` and, at the 20-entry cap, erases its front —
  so the rest of the frame walked a shifted vector. Restructured to record the choice and run it
  after the loop.
- 2026-08-18 New commands were unreachable from the command line until they were added to
  `kRegistry`; caught by the transcript (`Unknown command. Type HELP.`), not by inspection.
- 2026-08-18 Headless driver extended (test harness only, REQ-203/204's own layer): a `BOX` verb —
  `PICK` alone cannot express a box selection because the FIRST corner is armed by the viewport's
  mouse handler, not by `SubmitViewportPick` — and `EXPECT SELECTED <n>`, the only way to assert
  that something is *not* selectable, which is exactly REQ-084 (d)'s claim.

## 9. Self-verification
- [x] build-project        — PASS (`cmake --build build --clean-first`, exit 0; no new warnings in
      any touched file — the C4530/C4456/C4244 that remain are all pre-existing and in untouched
      lines)
- [x] architecture-review  — PASS (the one architectural addition was escalated first — ADR-034;
      dependencies still flow downward; Render receives a plain const pointer)
- [x] code-review          — PASS
- [x] dependency-audit     — PASS (no dependency added)
- [x] performance-review   — PASS (empty-set early-out: with nothing isolated the added per-entity
      cost is one `empty()` test, so REQ-100's 250k-segment case is untouched)
- [x] testing              — PASS. `tests/RightClickTests.cpp` (11 cases) covers the two pure rules;
      `tests/headless/transcripts/req084-object-isolation.txt` (57 steps) covers isolation
      end-to-end through product code, including the failure modes: isolate with an empty selection,
      ending isolation twice, and reopening a drawing saved while isolated. Full suite:
      **422/422 ctest**, 409 Catch2 cases / 203,811 assertions.

## 10. Verification result
- Submitted: 2026-08-18
- Verdict:   PASS
- Findings:  three found and fixed during self-review (dead `Find…` control; global instead of
  per-drawing hidden set; Recent Input iterating a vector it mutated) — all recorded in §8. None
  outstanding.

## 11. Outcome
- Requirements satisfied: REQ-084 (Acceptance met: yes, except the `Find…` item, which REQ-084 was
  revised to exclude — see DEBT-3)
- Tests added:            `tests/RightClickTests.cpp`,
                          `tests/headless/transcripts/req084-object-isolation.txt`
- Docs updated:           spec/requirements.md (REQ-084), spec/project.md (ADR-034 + acceptance)
- Technical debt noted:
  - **DEBT-1** — layer *off* / *frozen* does not hide linework in the GL renderer (only meshes and
    TIN surfaces honour it). Pre-existing, found while planning this task. Removal condition: a
    requirement for layer visibility in the viewport; follow-up task to be opened against it.
  - **DEBT-2** — Display Order deferred by user decision; needs a persisted per-entity ordering key
    (render + `.gs` + DXF). Removal condition: its own REQ.
  - **DEBT-3** — **Find…** is in the reference screenshot but not in the menu: GoSurvey's
    find/replace searches only the MTEXT buffer being edited (and the shortcut menu cannot open
    while that editor is up), so the item would have been inert (REQ-201). REQ-084 revised
    2026-08-18 to state the omission. Removal condition: a drawing-wide FIND requirement.
- Done: 2026-08-18
