# TASK-088 — Surface rollover readout

- Type:    feature
- Status:  implement
- Opened:  2026-08-23
- Owner:   Workshop

## 1. Authority
- Goal:         GOAL-01 (a working surveyor's CAD)
- Requirements: REQ-089 (accepted 2026-08-23, D-2026-08-23-a). Reads REQ-070 (style
                resolution), REQ-074 (interpolated elevation), REQ-084 (d) (isolation),
                REQ-100 (frame budget), REQ-101 (tolerance).
- Constraints:  CON-07 (build reproducibility — no artifacts in the source tree)
- Acceptance:   verbatim from REQ-089:
  - resting the cursor inside a surface for the dwell period shows the readout; moving the
    cursor hides it immediately and re-arms the dwell;
  - the elevation shown equals the planar interpolation at that position within REQ-101 — the
    same condition REQ-074 states, and the same query;
  - a position covered by no triangle — outside the border, in a concave notch, or inside a
    REQ-069 hide-boundary void — shows no readout, and no elevation is extrapolated;
  - a surface whose style name is empty or no longer in the table reads its REQ-070 fallback
    style name, never blank;
  - a surface on an off or frozen layer, or isolated out under REQ-084 (d), produces no readout;
  - two overlapping visible surfaces produce one block each, both named;
  - the per-frame cost of moving the cursor over a surface is unchanged: the covering-surface
    query runs once, when the dwell elapses, and its result is latched — never re-run per frame.
- Owning subsystem: UI (dwell, gating, draw) and Commands (the query). Both already own the
  adjacent work: `CadUi.cpp`'s idle-hover block and `CadCommands.cpp`'s `SurfaceElevationsAt`.

## 2. Scope
- In scope:     the dwell timer, the covering-surface query, the latched payload, and the panel.
- Out of scope: a general per-entity rollover (declined, D-2026-08-23-a (4)); a user-configurable
                dwell (declined, (2)); a ray-cast against the triangulation for orbited views
                (REQ-089's Statement names it as a separate requirement); paper space.
- Smallest change: reuse the existing covering-surface walk, the existing hover-block gating, and
                ImGui's own tooltip window. New code is the dwell rule, the row assembly, and the
                four-row table.

## 3. Architectural boundary check
- [x] **No — proceed.** Each boundary checked individually:
  - new global mutable state (§11.3): **no** — fields on `AppCommandState` beside the existing
    `viewportHoverSurveyPointIndex` / `uiCursorWorldZ`. A file-scope static in `CadUi.cpp` WOULD
    trip this; that is what forced ADR-033 for `g_chrome`, and it is avoided.
  - upward dependency (§11.1): **no** — and it shapes the design. `CadCommands.hpp` may not
    include a UI header, so the dwell state is plain scalars and the pure helper lives in
    `src/util/`, below both layers.
  - index across an object boundary (§11.9): **no** — the latched payload holds formatted
    strings, not a surface index. `cadSurfaces` compacts on erase, so latching an index across
    frames would be a blocking finding.
  - new abstraction (§11.4): **no** — one concrete free function in a pure header, shaped like
    `SurfaceStyles::IntervalsCompatible` beside it.
  - dependency / data format / public API: **none**. Nothing persisted, nothing exported.

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | What counts as hovering the surface — the existing edge-proximity pick, anywhere inside the TIN, or only over drawn components? | 2026-08-23 | **Anywhere inside the TIN.** Which turned out to be the cheapest option, not the most expensive: `TinElevationAt` already locates the containing triangle, so the elevation lookup IS the hit test and no new geometry code is needed. |
| Q2 | Dwell fixed or configurable? | 2026-08-23 | **Fixed 500 ms.** A setting was declined as a data-format change for a value nobody tunes. |
| Q3 | Which rows, and what happens outside the TIN? | 2026-08-23 | **Name / Style / Layer / Elevation**, dash outside. See ASSUMPTION-2 — with Q1's answer the outside case is unreachable through the normal path, so the dash survives only as a defensive fallback. |
| Q4 | Surfaces only, or a general rollover? | 2026-08-23 | **Surfaces only.** |

## 5. Assumptions

ASSUMPTION-1: Under an orbited camera the readout describes the cursor ray's intersection with
              the WORK PLANE, not the triangle under the pixel.
- Because:       `rawX`/`rawY` at the REQ-058 input seam (`CadUi.cpp:8824`) is the work-plane hit,
                 and every other pick in the application consumes it.
- Risk if wrong: under a tilted camera the panel names a position the user is not visually
                 pointing at.
- Validate by:   accepted, not open — SURFELEV reads the same seam, so the two always agree, and
                 REQ-089's Statement records the divergence and names the ray-cast alternative as
                 a separate requirement. Recorded in the decision log as risk (b).

ASSUMPTION-2: The "Elevation —" fallback is unreachable in normal operation.
- Because:       Q1 made containment the trigger, so a readout exists only where an elevation
                 exists. It is kept for the degenerate case `TinElevationAt` guards (a collinear
                 triangle, `area2 == 0`) rather than removed.
- Risk if wrong: none — it is a defensive branch, not a behaviour anyone depends on.
- Validate by:   code review; the branch is one line.

ASSUMPTION-3: The elevation carries no unit suffix.
- Because:       `FormatLinear` emits none and SURFELEV prints none, so a `'` here would be the
                 one readout in the application that disagrees with the others.
- Risk if wrong: cosmetic; a one-line change.
- Validate by:   raised with the user before implementation and accepted.

## 6. Plan
- Approach: advance a dwell timer in the existing idle-hover block, reusing its `blockEntityHover`
  gating verbatim so the readout cannot appear anywhere the hover highlight does not; on the frame
  the dwell elapses, run the covering-surface query ONCE and latch formatted rows; draw with
  `BeginTooltip`, which ImGui themes and clamps to the screen for us — so no new `UiChrome` field
  and therefore no way for a theme to be left with stale colours.
- Files/functions to touch:
  - `src/util/hoverdwell.hpp` (new) — `UpdateHoverDwell`
  - `src/commands/CadCommands.hpp` — `SurfaceHoverRow`; dwell + payload fields; declare
    `BuildSurfaceHoverRows`
  - `src/commands/CadCommands.cpp` — widen the covering-surface walk to yield the index so
    SURFELEV and the readout share ONE walk; add `BuildSurfaceHoverRows`
  - `src/ui/CadUi.cpp` — dwell, sample-once, draw
  - `tests/HoverDwellTests.cpp` (new) + `CMakeLists.txt`
- Test approach:
  - happy path = the dwell fires exactly once after the threshold and does not re-fire while the
    cursor is still;
  - failure modes = sub-threshold jitter does NOT reset the timer; movement past the threshold
    resets and re-arms; a time source that goes backwards does not latch the readout forever.
  - Everything numeric is already pinned and is cited rather than re-tested: containment and
    interpolation by `TinQueryTests.cpp:42,55,67,92,155`, the style fallback by
    `SurfaceStyleTests.cpp:112,123,134,144`.
- Steps:
  - [x] record REQ-089 + decision row
  - [x] pure dwell helper + its tests
  - [x] share the covering-surface walk; add the query
  - [x] state fields
  - [x] UI dwell, sample-once, draw
  - [x] build, run tests
  - [~] run the app — **not completed; see FINDING-1**

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1–Q4 above, all before any code). The dwell helper is written
  test-first; the rest is UI and is verified by running the application, reported as manual
  verification rather than as a green test.

## 8. Implementation log
- 2026-08-23 open → plan → implement. REQ-089 accepted and recorded (D-2026-08-23-a).
- 2026-08-23 `util/hoverdwell.hpp` + `tests/HoverDwellTests.cpp` written first, green (8 cases).
- 2026-08-23 **Boundary found while placing `BuildSurfaceHoverRows`.** The REQ-074 helpers sit inside
  the anonymous namespace that spans `CadCommands.cpp` 3489–8015, so defining a header-declared
  function beside them would have given it internal linkage and failed to link. Placed after the
  namespace closes, beside `CommitClipboardPasteAt`, which is the file's existing precedent for a
  public entry point over a file-local helper — and is the same trap that entry point documents.
- 2026-08-23 **Self-review finding, fixed before submission.** The panel was first drawn with an
  ImGui table inside the tooltip, and `ImGui::BeginTooltip()` was called without checking its return
  — `EndTooltip` must not follow a `false`. Both were replaced: the return is now guarded (the form
  the other tooltips in this file use), and the table gave way to a measured `SameLine` offset, which
  removes an auto-resizing-window sizing rule the four rows never needed. Simpler and with one fewer
  unverified dependency, which mattered given FINDING-1.
- 2026-08-23 FINDING-1 recorded; task submitted with the on-screen check outstanding.

## 9. Self-verification
- [x] build-project       — PASS. Clean build, MSVC via `build.bat` (vcvars). No new warnings.
- [x] architecture-review — PASS. No Workshop architectural decision; §3 boundary table re-checked
      against the finished code. §11.9 holds: the latched payload is text, and the one index that
      exists (`SurfaceCoverage::surfaceIndex`) is consumed inside the call that produced it.
- [x] code-review         — PASS. One finding raised and fixed by self-review (see log above).
      The SURFELEV refactor is behaviour-preserving: same predicate, same order, same pairs — the
      named form is now expressed over the indexed walk rather than duplicating it.
- [x] dependency-audit    — PASS / n-a. No dependency added; one new in-tree pure header.
- [x] performance-review  — PASS by construction rather than by measurement. The per-frame path is
      unchanged: the only per-frame work added is one distance compare in `UpdateHoverDwell`. The
      O(triangles) query runs once per cursor rest, pinned by `HoverDwellTests`
      ("One rest fires exactly once" — 600 frames, 1 query). REQ-100 was NOT re-measured: no
      per-frame cost was added, so the profile has nothing new to measure.
- [x] testing             — PASS. 500 cases / 214,443 assertions green, including 8 new REQ-089
      cases. **Coverage is honest about its limits**: the dwell rule is unit-tested; the readout's
      numbers are covered by the already-pinned calls it reuses (`TinQueryTests`,
      `SurfaceStyleTests`); the gating and the drawing have no automated coverage and cannot, since
      the unit-test target links neither `CadCommands.cpp` nor a window. See FINDING-1.

### FINDING-1 — the on-screen behaviour is NOT visually confirmed (open)
I built a real contoured surface in the running application (`SURFACECREATE Test EG, Existing
Ground` against `samples/surface-demo.gs`) and it rendered correctly, but I could not make the
readout appear, because **I could not make any hover appear**: a ribbon button used as a control did
not highlight under the same synthetic input either. The GLFW backend only feeds ImGui a mouse
position while the window reports `GLFW_FOCUSED`, and neither `SetCursorPos`, injected
`mouse_event(MOUSEEVENTF_ABSOLUTE)`, a title-bar click, nor `AttachThreadInput` + `SetForegroundWindow`
produced a hovered frame from this session. The app itself is fine — it polls with `glfwPollEvents`
and renders continuously, so a resting cursor does keep producing frames and the dwell can fire.

**This is an unverified claim, not a passed test.** What is proven: it compiles, links, runs without
crashing, and the dwell logic is correct in isolation. What is not proven: that the panel appears,
where it appears, and that it reads well. One person with a real mouse settles it in five seconds.

## 10. Verification result
- Submitted:  2026-08-23 — PASS with FINDING-1 open (visual confirmation outstanding).
