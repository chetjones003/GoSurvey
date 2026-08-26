# TASK-119 — ZOOM EXTENTS through an activated viewport frames that viewport (REQ-123, GitHub issue #100)

- Type:    fix
- Status:  done
- Opened:  2026-08-26
- Owner:   Nathan Johnson

## 1. Authority
- Requirements: **REQ-123** — accepted 2026-08-26 by **D-2026-08-26-e**.
- Also honoured: REQ-122 (its `FrameWorldRect` decides the framing; not reimplemented), REQ-120 (the
  gesture, and its floating-model-space claim corrected), REQ-045 (middle-drag pan unchanged),
  REQ-028 / REQ-046 (per-viewport frozen layers), REQ-201 (every refusal states its reason),
  REQ-203 (the driver verbs).
- Acceptance: REQ-123's ten conditions, which restate issue #100's eight.
- Owning subsystem: `Commands` (framing + extents filter); `UI` (raising the gesture in every space).

## 2. Scope
- In scope: the floating-viewport zoom-extents path, the per-viewport extents filter, REQ-120's
  gesture reaching floating model space, and the harness work that makes it testable.
- Out of scope:
  - **which entity kinds a viewport renders.** The viewport renderer draws only lines, polylines,
    circles, arcs and survey points — so a surface or an MTEXT is not visible through a viewport
    today, yet still counts toward the extents. That is a renderer gap, and §7 explains why it is
    deliberately NOT encoded into the extents filter;
  - the wheel and middle-drag routes into a viewport's framing, which already worked and are
    untouched;
  - TASK-113 DEBT-1 in general — narrowed here, not closed (§12).
- Smallest change: one branch, one optional parameter, one hoisted `if`.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public API / data format?
    - [x] **No.** `FrameWorldRectInViewport` is a conversion of `FrameWorldRect`'s output into the
          viewport's units, in the same header, and adds no second framing implementation — which
          issue #88's Architecture section explicitly forbids. The extents filter is an **optional
          parameter defaulting to nullptr**, so every existing caller keeps its exact behaviour and
          no signature changes at any call site. The driver verbs are test-harness code.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Does "content visible through that viewport" mean frozen layers are excluded? | 2026-08-26 | **Yes.** The renderer and the plotter already filter on `IsLayerFrozenInViewport`; the extents must agree with what is drawn, or a viewport zooms out to fit geometry the user deliberately hid. User's decision. |
| Q2 | What does zoom-extents do with the viewport zoom LOCK on? | 2026-08-26 | **Frames the sheet.** Settled from the lock's own stated meaning ("pan/zoom always targets the sheet") rather than asked — zoom-extents is a zoom, and any other answer makes the lock mean two things. Stated in REQ-123. |

## 5. Assumptions
```
ASSUMPTION-1: A viewport's aspect for framing is its paper rectangle, not its pixel rectangle.
- Because:       what the viewport shows is `paperWIn x paperHIn` of sheet at `scaleModelPerPaperIn`
                 model units per inch. Its pixel size is that rectangle times the sheet's own zoom,
                 so the two aspects are identical and the paper one needs no framebuffer.
- Risk if wrong: the framing would depend on the window, which is the defect being fixed.
- Validate by:   measured in the GUI (§13). A viewport measuring 10.77in x 5.03in framed a
                 400 x 100 drawing at 40.4 model units/in on the live status bar; computing it from
                 the paper rectangle alone gives 40.36. The pixel aspect never entered.
```

## 6. Plan
- `src/commands/ZoomFraming.hpp` — `FrameWorldRectInViewport`, calling `FrameWorldRect` and
  restating its answer as `modelCenter` + `scaleModelPerPaperIn`.
- `src/commands/CadCommands.{hpp,cpp}` — optional `const Viewport* vpFilter` on
  `ComputeWorldExtents` / `CollectEntityBoxes` / `ComputeRobustWorldExtents`; the floating branch in
  `ProcessPendingViewportZoom`, above the framebuffer guard; the sheet branch simplified now that
  floating-and-unlocked returns earlier.
- `src/ui/CadUi.cpp` — raise REQ-120's middle double-click outside the `!routeZoomToViewport` block.
- `tests/headless/HeadlessDriver.cpp` — `VIEWPORT`, `VPSELECT`, `CLAYER`, `VPFREEZE`,
  `EXPECT VPFRAME`, and consuming a pending zoom after `CMD`.
- Test approach: **transcript for the behaviour, GUI for the gesture and the sheet.**

- Steps:
  - [x] 1. Establish the mechanism by reading: which fields a viewport's view actually is.
  - [x] 2. Framing conversion + the branch.
  - [x] 3. The per-viewport extents filter, every store.
  - [x] 4. Harness verbs, then the transcript; prove it red.
  - [x] 5. GUI pass.

## 7. Workflow-specific notes
- **The interesting decision was what NOT to filter.** Every model store has a parallel attributes
  array with a layer, so filtering by per-viewport freeze is uniform and cheap. The tempting next
  step — also skipping the entity kinds a viewport does not draw — was declined. The viewport
  renderer handles lines, polylines, circles, arcs and survey points and nothing else; a surface or
  a text object simply is not drawn through a viewport today. Encoding that in the extents math
  would make the requirement say "extents means these five kinds", which is a **renderer limitation
  written into the spec** — and it would silently go wrong the moment the renderer is completed.
  A frozen layer is a decision the user made; an unimplemented draw path is not. Only the first is
  a visibility rule. The gap is recorded as an observation, not absorbed.
- **This is the first zoom behaviour a transcript can reach**, and that was not a lucky accident —
  it is a property of the case. Every other zoom is blocked by `fbW <= 0` because its aspect is the
  window's. This one's aspect is paper inches and its output lives on the viewport, so it touches no
  pixel and is handled ahead of that guard. Worth stating because it slightly narrows DEBT-1's claim
  that no zoom behaviour is coverable (§12).
- **REQ-120 claimed something that had never happened.** Its acceptance says floating model space
  frames the model; the gesture that would do it was raised inside a block guarded by
  `!routeZoomToViewport`, which is skipped exactly when a viewport owns pan/zoom. The claim survived
  a GUI pass because that pass checked model and paper space, not the third case. Corrected on the
  requirement itself rather than quietly fixed here.

## 8. Implementation log
- 2026-08-26 Recon settled the mechanism before any edit: `Viewport::modelCenterX/Y` +
  `scaleModelPerPaperIn` is the viewport's whole view, and `CadUi.cpp`'s `routeZoomToViewport` block
  already steers the wheel and middle-drag into those fields. Zoom-extents was simply never joined
  to that route — the mechanism existed and had one caller missing.
- 2026-08-26 The filter needed a rule for a MISSING attributes entry. Both the renderer and the
  plotter read it as the default `EntityAttributes` — layer `"0"` — so this does too; reading it as
  "no layer, never hidden" would make freezing layer `"0"` hide geometry on screen while still
  dragging the extents out to it.
- 2026-08-26 `EXPECT VPFRAME` compares with a relative tolerance rather than asserting the log line.
  Asserting the log would assert a `%.6g` rendering of three floats, which turns a rounding at the
  sixth significant digit into a red test about nothing.

## 9. Self-verification
- [x] build-project        — PASS (clean; /W4 /permissive-, no new warnings)
- [x] architecture-review  — PASS (§3; optional parameter, no call-site changes, no second framing)
- [x] code-review          — PASS (the filter is mechanical and uniform; the branch is one early return)
- [x] dependency-audit     — n-a
- [x] performance-review   — n-a (one string compare per entity per extents sweep, and only when a filter is passed)
- [x] testing              — PASS (§11)

## 10. Verification result
- Submitted:  2026-08-26
- Verdict:    PASS
- Findings:   none outstanding

## 11. Outcome
- Requirements satisfied: REQ-123 (all ten acceptance conditions — met); REQ-122 (its framing reused,
  not duplicated — met); REQ-120 (gesture now reaches floating model space; its incorrect claim
  corrected — met); REQ-045 (middle-drag pan unchanged — met); REQ-028/REQ-046 (per-viewport freeze
  honoured — met)
- Tests added:            `tests/headless/transcripts/req123-viewport-zoom-extents.txt`, 43 steps.
                          **Proven red on `beta`**: `VPFRAME: expected centre 50, 10 scale 13.587;
                          got 0, 0 scale 50` — the viewport untouched at its creation defaults.
                          Suite **632/633**; the single failure is `beta`'s own and unrelated (see
                          §12 DEBT-2).
- Refactors:              none beyond the optional parameter
- Docs updated:           `spec/requirements.md` (REQ-123; REQ-120's floating-model-space row and a
                          correction note; traceability row), `spec/project.md` (D-2026-08-26-e)
- Done:                   2026-08-26

## 12. Technical debt
```
DEBT-1 (TASK-113) — NARROWED, not closed.
- What:      `ProcessPendingViewportZoom` still early-returns on `fbW <= 0`, so every zoom whose
             camera is the WINDOW's — model ZOOMEXTENTS, ZOOM WINDOW, the sheet branch — remains
             unreachable from a transcript.
- Changed by this task: the floating-viewport case is now covered end to end, because its aspect
             comes from paper inches rather than pixels. DEBT-1's "no zoom behaviour can be
             covered" is no longer literally true, and the reason it was true is now visible: it is
             the framebuffer dependency, not zoom as such.
- Remove by: unchanged — a REQ-203 harness extension giving the driver a synthetic viewport.
- Follow-up: still not filed. The remaining cases are the ones REQ-122 covers by unit-testing the
             framing math directly, so the value of the harness work is lower than it was.
```
```
DEBT-2: the suite is 632/633, and the failure is not this task's.
- What:      `FindBest respects the snap tolerance — nothing outside it is ever returned`
             (`tests/CadSnapTests.cpp`) fails under ctest and passes when run directly.
- Cause:     the EM DASH in the TEST_CASE name. `catch_discover_tests` registers the test by name
             and ctest passes that name back as a filter; the round trip through the console
             codepage mangles the non-ASCII byte, so the filter matches nothing and ctest reports a
             FAILURE rather than a skip. Nothing in the build output hints at it.
- Arrived:   with `425afa7` (REQ-062, issue #103), before this branch was cut. Untouched here.
- Remove by: renaming the test case to pure ASCII. One word; deliberately not done in this PR
             because it belongs to someone else's just-merged work and to a different issue.
```

## 13. GUI verification — 2026-08-26
Driven against the real window; the sheet, a viewport and the live status bar read off screenshots.

**Setup.** Model geometry `0,0–400,0`, `400,0–400,100`, `0,0–0,100` (a U, 400 x 100 — deliberately
not square, so a wrong aspect is visible). ARCH sheet with a title block; one viewport created with
`RECTVP`, roughly 10.77in x 5.03in on the sheet.

- **The framing, measured.** `MSPACE`, then `ZOOMEXTENTS`. The log reads
  `Zoom extents applied in viewport — span 400 x 100`, the U is centred inside the viewport with
  clear margin on both axes, and the status bar's viewport scale reads **`VP 1" = 40.4'`**.
  Computing it from the paper rectangle alone: `needHalfH = max(100/1.84, 400/(2.141·1.84)) = 101.5`,
  `scale = 2·101.5/5.03 = 40.36`. The two agree — the framing is the viewport's rectangle, not the
  window's.
- **Nothing outside the viewport moved.** Viewport border, sheet border and title block are pixel-
  identical to the shot taken before the command. The viewport kept its size and position, which is
  the acceptance condition issue #100 puts first.
- **REQ-120's gesture, in a viewport, for the first time.** A middle double-click inside the
  viewport logs the same `Zoom extents applied in viewport — span 400 x 100` and produces the same
  framing as the typed command. Before this task it did nothing there at all (§7).
- **Middle-drag still pans inside the viewport.** A drag moved the model within the viewport — the
  U shifted and clipped against the viewport border — while the sheet, the border and the scale
  (`40.4'`) were unchanged. Then a middle double-click re-centred it exactly, which is both the
  gesture and the pan verified against each other.
- **Outside a viewport, nothing changed.** `PSPACE` then `ZOOMEXTENTS` logs
  `Returned to paper space.` followed by `Zoom extents applied — span 17 x 11` — the sheet, exactly
  as REQ-120 specifies.
- **Not verified here:** the wheel route into the viewport's framing. `SendInput`'s wheel events do
  not reach the app through this harness (the same limitation showed up in TASK-117's pass), so it
  could not be driven. It is untouched code on a path this task does not modify, and the middle-drag
  half of the same block was exercised.
