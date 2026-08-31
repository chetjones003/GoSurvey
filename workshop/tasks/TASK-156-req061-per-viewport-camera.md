# TASK-156 — Per-viewport camera in paper space (REQ-061)

- Type:    feature
- Status:  done (pending verification sign-off + GUI visual confirmation)
- Opened:  2026-08-31
- Owner:   chetjones003 (with Claude)

## 1. Authority
- Goal:         3D model space (ADR-025) — carry the camera into paper space.
- Requirements: REQ-061 (accepted) — *Per-viewport camera in paper space*.
- Constraints:  CON-07 (build reproducibility / additive `.gs`); REQ-101 (coordinate tolerance);
                REQ-100 (frame budget); ADR-009/013 (paper-space sheet stores stay 2D).
- Acceptance (verbatim from REQ-061):
  - "a layout with two viewports — one plan, one isometric — renders both correctly on screen
    and plots both correctly to PDF;"
  - "a legacy `.gs` loads with every viewport in plan view and renders identically to
    pre-change."
- Owning subsystem: Domain (`Viewport` data), Renderer (projection + draw), IO (`.gs` + plot).
- Origin: GitHub issue #175, split from #155 (decision D-2026-08-28-n deferred REQ-061's
  acceptance out of issue #126 / REQ-154). #155 stays open on the per-viewport *active UCS*.

## 2. Scope
- In scope:
  - `Viewport` carries a camera orientation (azimuth / elevation / roll / projection / fov).
  - One shared projection that reduces to `ModelToPaperIn` bit-for-bit in plan view.
  - On-screen viewport overlay and the PDF plot both project model linework through it.
  - Additive `.gs` persistence; legacy files load all-plan.
  - A standard-views picker (Plan / 4 isometrics / 6 elevations) in the Viewports window.
- Out of scope (documented follow-ups, recorded in the REQ-061 status text):
  - Viewport TEXT / dimension / table glyphs still project their anchor at Z = 0.
  - Interactive draw-inside-a-viewport (floating model space) still assumes a plan camera.
  - Orbit-inside-an-activated-viewport writeback.
  - Per-viewport *active UCS* + UCSFOLLOW — that is the remaining body of issue #155.
- Smallest change: reuse the existing model-viewport `Camera` value type (ADR-025 (c), which
  already names "each paper-space `Viewport` under REQ-061" as one of its three uses); add only
  data to `Viewport` and one projection header; route the two existing render loops through it.

## 3. Architectural boundary check
- New abstraction / layer / dependency / ownership / global state / public-API / data-format /
  new algorithm?
    - [x] No — proceed.
    - `Camera` is a pre-existing value type whose REQ-061 use ADR-025 (c) already sanctioned.
      `render/ViewportProjection.hpp` is a header of two free functions, not an abstraction.
      The `.gs` change is additive under the tolerant-key rule (ADR-030) — no `kGsFormatVersion`
      bump, the ADR-020 (d) / ADR-025 (g) precedent for "older files load all-plan".

## 4. Questions
| # | Question | Asked | Answer |
|---|----------|-------|--------|
| — | none — REQ-061's statement and the existing `Camera` conventions settled the design. | | |

## 5. Assumptions
```
ASSUMPTION-1: Projecting model geometry at its true Z (not Z=0) through a rotated viewport
              camera is the intended behaviour for REQ-061 ("a plan view and an isometric").
- Because:       REQ-061's statement says "render a viewport's model content from that
                 viewport's own camera" without spelling out Z handling.
- Risk if wrong: an isometric viewport of a flat drawing would just be a skewed rectangle.
- Validate by:   the acceptance example ("one plan, one isometric") is only meaningful with Z;
                 ViewportCameraTests pins a Z-sensitive projection.

ASSUMPTION-2: Text/dimension glyph anchors at Z=0 in a rotated viewport is an acceptable
              first increment.
- Because:       REQ-061's acceptance names linework ("plan + isometric render correctly"),
                 not annotation fidelity; the glyph pipeline is 2D/billboarded.
- Risk if wrong: 3D annotations sit at the wrong height in a tilted viewport.
- Validate by:   recorded as a deferred follow-up in the REQ-061 status; revisit if a
                 requirement asks for it.
```

## 6. Plan
- Approach: add camera fields to `Viewport`; a header-only `ModelToPaperInThroughCamera` that
  delegates to `ModelToPaperIn` when `cameraIsPlan()` and otherwise runs the model `Camera`
  (`WorldToScreen`) against the viewport rect's aspect; swap the `m2s` / `m2p` lambdas in the two
  render loops to call it; additive `.gs` read/write; a `View` combo in `DrawViewportsWindow`.
- Files/functions touched:
  - `src/commands/PaperSpace.hpp` — `Viewport::cam*`, `cameraIsPlan()`, `kViewportStandardViews`.
  - `src/render/ViewportProjection.hpp` (new) — `CameraForViewport`, `ModelToPaperInThroughCamera`.
  - `src/ui/CadUi.cpp` — `DrawDrawingViewport` viewport overlay: `m2sz` lambda + linework Z.
  - `src/io/PdfPlot.cpp` — `PlotLayoutsToPdf` viewport loop: `m2pz` + `emitModelSeg` Z.
  - `src/io/GsIo.cpp` — viewport write/read (additive keys).
  - `src/ui/CadUi_PageSetup.cpp` — standard-view combo.
  - `tests/ViewportCameraTests.cpp`, `tests/GsIoViewportCameraTests.cpp` (new); `CMakeLists.txt`.
- Test approach:
  - happy path: plan-view projection == `ModelToPaperIn` over a grid (bit-exact); a hand-computed
    SW-isometric point lands where the `Camera` math puts it; `.gs` round-trips the camera.
  - failure mode: a `.gs` with the camera keys stripped (a pre-REQ-061 file) loads every viewport
    in plan view; a rotated viewport's projection does not touch a sibling viewport.
- Steps:
  - [x] `Viewport` camera data + `cameraIsPlan()` + standard-views table
  - [x] `ViewportProjection.hpp` with the plan-view delegation
  - [x] wire `CadUi.cpp` overlay (lines/polylines/circles/arcs/ellipses/surfaces/survey crosses)
  - [x] wire `PdfPlot.cpp`
  - [x] additive `.gs` persistence
  - [x] Viewports-window `View` combo
  - [x] tests + CMake wiring
  - [x] spec / `.gs`-format-doc updates

## 7. Workflow-specific notes  (Feature)
- Pre-flight answered: yes (no open questions). Tests-first: the projection parity + hand-computed
  isometric tests were written against the acceptance conditions before the render wiring was
  trusted.
- The load-bearing property (from `Camera.hpp`): "plan view produces an IDENTITY rotation", so
  `WorldToScreen` "in plan view reduces exactly to the pre-3D linear mapping". The parity test
  asserts `ModelToPaperInThroughCamera == ModelToPaperIn` directly rather than assuming it.

## 8. Implementation log
- 2026-08-31 Confirmed #155 was a true SPEC GAP (REQ-061 accepted but "planned"); opened issue
  #175 for the prerequisite at the user's instruction.
- 2026-08-31 Added `Viewport` camera fields; `ViewportProjection.hpp`; routed the on-screen
  overlay and PDF plot through it; additive `.gs`; standard-view combo.
- 2026-08-31 `tests/ViewportCameraTests.cpp` (6 cases, incl. bit-exact grid parity and a
  hand-computed SW-iso point 7.121, 6.225) + `tests/GsIoViewportCameraTests.cpp` (2 cases:
  round-trip, keys-stripped legacy load).
- 2026-08-31 `./dev/build` clean; `./dev/test` → **882/882 green**. `[req061]` cases green.
- 2026-08-31 Spec: REQ-061 marked implemented with its verification-table row; issue-#155
  deferred-item note updated; `docs/gs-file-format.txt` viewport keys documented.
- 2026-08-31 Committed `d8127e8`; PR #176 → `beta`.

## 9. Self-verification
- [x] build-project        — PASS (`./dev/build`, MSVC/Ninja release, clean)
- [x] architecture-review  — PASS (no Workshop architectural decision; `Camera` reuse pre-decided
      by ADR-025 (c); additive `.gs` per ADR-030)
- [x] code-review          — PASS (one shared projection; plan-view path unchanged and asserted;
      linework carries true Z; the 2-arg `m2s` kept for the Z=0 glyph sites)
- [x] dependency-audit     — PASS / n-a (no new dependency)
- [x] performance-review   — PASS / n-a: plan view takes the identical `ModelToPaperIn` path;
      a rotated viewport adds one `Camera::WorldToScreen` per already-projected point and one
      48-gon per circle. No per-frame model-wide scan added; REQ-100 path unchanged in plan view.
- [x] testing              — PASS (8 new assertions-heavy cases; the keys-stripped test fails
      against a build that does not default the camera to plan)

## 10. Verification result
- Submitted:  2026-08-31 (PR #176)
- Verdict:    PASS (self) — awaiting Verification sign-off + a GUI visual check of a real
              two-viewport sheet (plan + isometric) on screen and in a PDF plot, since the
              GUI render path cannot be exercised by a test target.
- Findings:   none open.

## 11. Outcome
- Requirements satisfied: REQ-061 (Acceptance met: yes — plan/iso render + plot via the shared
  camera projection; legacy `.gs` loads all-plan, bit-identical in plan view).
- Tests added: `tests/ViewportCameraTests.cpp` (6), `tests/GsIoViewportCameraTests.cpp` (2).
- Refactors:  none.
- Docs updated: `spec/requirements.md` (REQ-061 status + verification row + issue-#155 note),
  `docs/gs-file-format.txt`, this task log.
- Technical debt noted: viewport annotation glyphs project at Z=0; floating model space assumes
  a plan camera — both recorded in the REQ-061 status with the follow-up being issue #155's
  per-viewport active-UCS work.
- Done: 2026-08-31 (pending the two sign-offs above).
