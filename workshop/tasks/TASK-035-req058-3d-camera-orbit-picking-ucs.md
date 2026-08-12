# TASK-035 — Orbitable 3D camera, ray picking, and a UCS work plane

- Type:    feature
- Status:  implement (paused — see §10b for the gaps that block completion)
- Opened:  2026-08-11
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-058** (accepted), **REQ-100** (accepted — the 16 ms / 250k-segment budget
  becomes a real gate here for the first time). Enables REQ-059/060/061.
- Architecture: **ADR-025** (c) Camera value type, (d) ray-based input in a pure module,
  (e) UCS on `AppCommandState`.
- Constraints: REQ-101 (±0.01 ft), REQ-200, REQ-201, REQ-301, architecture §11 invariants.
- Acceptance (verbatim from REQ-058):
  - plan view renders pixel-comparable to the pre-change build on a reference drawing;
  - endpoint / midpoint / center / intersection snaps resolve correctly from an orbited camera,
    verified against hand-computed coordinates within REQ-101;
  - LINE, ARC, CIRCLE and TEXT drawn on a non-default UCS land on that plane within REQ-101;
  - the existing test suite stays green;
  - the REQ-100 frame budget is met while orbiting.
- Owning subsystem: Renderer (matrices), util (pure ray math), UI/Commands (input, picking, snap).

## 2. Scope

- **In scope:** `render/Camera` value type; `util/ray3d` pure module; view rotation inserted into
  the renderer MVP; orbit input; the model cursor seam becoming ray×work-plane; ray-based entity
  picking when orbited; a UCS on `AppCommandState`; the REQ-100 bench scene.
- **Out of scope:** the view gizmo (REQ-059/TASK-036), manipulation gizmos (REQ-060/TASK-037),
  per-viewport paper cameras (REQ-061/TASK-038). **Hidden-line removal / shaded visual styles** —
  see the depth decision below; that is a future REQ, not this one. Paper space stays 2D.

## 3. Architectural boundary check (workflow.md §4)

- [x] **No** — proceed. Camera, ray module, and UCS placement were all decided in ADR-025 before
      this task opened. No new abstraction (Camera is a value type with three concrete uses),
      no new dependency, no new global (UCS lives on `AppCommandState`, the settings pattern),
      no new backend. The one judgment call below (depth) is a *visual-style* choice that
      preserves current behaviour, not an architectural change — recorded as ASSUMPTION-1.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Full 3D vs 2.5D? | 2026-08-11 | Full 3D with free orbit. |
| Q2 | Orbit input binding? | — | Shift+middle-drag (AutoCAD 3DORBIT convention); plain middle-drag keeps panning. Reversible, no spec impact. |

## 5. Assumptions

```
ASSUMPTION-1: Depth testing stays DISABLED; orbit shows all edges (no hidden-line removal).
- Because:       ViewportRenderer.cpp:783 explicitly disables GL_DEPTH_TEST today, and this is
                 exactly AutoCAD's "2D Wireframe" visual style, which does not hide back edges
                 even when orbited. Enabling depth would silently change how every existing
                 drawing looks the moment the user tilts the view.
- Risk if wrong: users expect solid-looking occlusion when orbiting.
- Validate by:   show the user an orbited render; if occlusion is wanted it is a visual-style
                 REQ (with a style selector), not a silent default flip. The depth renderbuffer
                 already exists (msDepthRbo_), so enabling it later is a one-line change.
```

```
ASSUMPTION-2: The existing CPU geometry cache survives orbit without re-tessellation.
- Because:       circles/arcs are tessellated as planar polylines in world space; rotating the
                 camera is a matrix op, not a geometry op. Verified by reading the rebuild
                 trigger (ViewportRenderer.cpp:1069-1080): it keys on pan drift, zoom change and
                 cadGpuRevision — none of which an orbit touches.
- Risk if wrong: orbit thrashes the cache and REQ-100 fails.
- Validate by:   the REQ-100 bench asserts the 95th-pct frame during a scripted orbit.
```

## 6. Plan

### The two findings that shape this

**(1) The MVP already works in camera-relative coordinates.** Reading the pipeline:
`proj = Ortho(±halfW, ±halfH)` is centred on the origin and contains **no pan**; committed vertices
are cached *relative to* `cachedViewAnchor`; and `cachedModel = Translate(cachedAnchor − pan)`
brings them to `v_world − pan`. So the shader already receives **offsets from the camera target**
— which is precisely what a view matrix wants. Adding orbit is therefore an *insertion*, not a
rewrite:

```
today:  MVP = Ortho · Translate(cachedAnchor − pan)
after:  MVP = Ortho · R · Translate(cachedAnchor − pan)
```

`R` is the camera rotation about the target. This ordering is FINDING-3 from the REQ-057 review,
and it is load-bearing: the anchor/pan subtraction must happen **in world space, before** the
rotation. Reversed, geometry swims during orbit at large (state-plane) coordinates in a way that
looks like a camera bug but is a cache bug.

**(2) Model-space input has ONE seam.** `CadUi.cpp:8085-8088` computes `rawX`/`rawY` from the
cursor, and every downstream consumer — snap, hover, offset pick, hatch trace, command submission
— reads those two values. Orbit-aware input is therefore one substitution at one place, not a
sweep of the 103 sites that reference the view extents. Those 103 are read-only uses of
`worldLeft/Right/Top/Bottom`, which remain exactly correct in plan view.

### Phases (each independently verifiable)

**Phase A — pure modules, zero behaviour change.**
- `src/render/Camera.hpp/.cpp` — `Camera{eye|target, up, projection, orthoHalfH, fovDeg, near, far}`
  producing view and projection matrices; a `Camera::Plan()` factory whose matrices reproduce the
  current ortho exactly.
- `src/util/ray3d.hpp` — `ScreenToWorldRay`, `RayPlaneIntersect`, `RaySegmentDistance`,
  `RayPointDistance`. Pure, no GL, no ImGui.
- `tests/CameraTests.cpp`, `tests/Ray3dTests.cpp` — including the FINDING-3 composition test at a
  state-plane origin, and a parity test that `Camera::Plan()` equals the existing ortho within 1e-6.

**Phase B — renderer + orbit.**
- Insert `R` into both MVP compositions (the main batch and the cached-VC batch).
- `Camera` replaces `panX/panY/zoom` in `RenderScene` (FINDING-4 — a net parameter *reduction*).
- Shift+middle-drag orbits; plain middle-drag still pans; plan view is the startup default.
- Depth stays off (ASSUMPTION-1).

**Phase C — input + picking.**
- The `rawX/rawY` seam becomes: plan view → existing arithmetic (bit-identical); orbited →
  `RayPlaneIntersect(ScreenToWorldRay(cam, mouse), activeUcs)`.
- `PickClosestCadEntity` gains an orbited path using `RaySegmentDistance`; snapping resolves in 3D.

**Phase D — UCS.**
- `AppCommandState` gains the active work plane (origin + normal, default world XY).
- Draw commands resolve clicks against it (they already consume `rawX/rawY`, so this mostly falls
  out of Phase C).

**Phase E — REQ-100 bench.** A committed 250k-segment scene + scripted orbit; record the
95th-percentile frame.

### Test approach

- Happy path: plan-view parity (matrix + visual); snaps from an orbited camera against
  hand-computed coordinates within REQ-101; draw on a tilted UCS lands on that plane.
- Failure mode: ray parallel to the work plane (no intersection — must not produce a NaN
  coordinate or a silent (0,0) click); zero-length ray; plane behind the camera; degenerate
  camera (eye == target) must not produce a NaN matrix.

### Steps

- [ ] A1. `util/ray3d.hpp` + `Ray3dTests` (pure, testable first).
- [ ] A2. `render/Camera` + `CameraTests` incl. plan-view parity and the FINDING-3 composition.
- [ ] B1. Insert `R` into both MVP paths; `Camera` into `RenderScene`.
- [ ] B2. Orbit input; plan-view default; verify plan view is unchanged.
- [ ] C1. Cursor seam → ray×plane when orbited.
- [ ] C2. Ray-based picking + 3D snap.
- [ ] D1. UCS on `AppCommandState` + draw routing.
- [ ] E1. Bench scene; record REQ-100 numbers.
- [ ] F. Self-verify (§9), submit.

## 7. Workflow-specific notes

- Feature: pre-flight answered. Tests-first for both pure modules (A1/A2 precede every change
  that depends on them being correct).

## 8. Implementation log

- 2026-08-11 — opened. Authority + Plan complete. Pipeline studied before planning: the MVP is
  already camera-relative, and model input has a single seam — both recorded above because they
  are what make this task a series of insertions rather than a rewrite.
- 2026-08-11 — **Phase A complete.** `util/ray3d.hpp` (16 tests) and `render/Camera.hpp`
  (13 tests) — both pure, both GL-free, both linked into the test target without the UI stack.
  Suite 802 → 917 assertions / 154 cases. Two real bugs caught **by the tests, not by review**:
    * `RaySegmentDistance` had a **negated denominator** in the closest-approach solve. Every pick
      would have clamped to a segment endpoint — "picking works but is inaccurate", the kind of
      defect that survives casual use for months.
    * `ForwardWorld` re-derived the view direction from azimuth/elevation independently of
      `ViewRotation`, and the two **disagreed on the sign of the azimuth term**. `ScreenRay` and
      `WorldToScreen` were therefore inconsistent at any non-zero azimuth: picking would miss what
      was drawn, but only once the user orbited off the meridian. Fixed by deriving forward FROM
      the rotation matrix — one derivation, one convention. Caught by the round-trip test.
- 2026-08-11 — **Phase B (renderer + orbit) largely complete.** `RenderScene` takes a `Camera` in
  place of `panX/panY/zoom` (FINDING-4 — a net parameter reduction), the rotation is inserted as
  `MVP = Proj · R · Translate(anchor − pan)` in **both** MVP paths (FINDING-3 ordering, asserted by
  a test that also proves the reversed order differs), and Shift+middle-drag orbits in model space
  only. Plan view is the default and renders unchanged; app builds, launches and runs.
  `CadViewCamera(st)` derives the camera from the canonical pan/zoom plus two new orientation
  fields, so "the camera" cannot drift from "the view".
- 2026-08-11 — **Scope finding not in the original plan: the ImGui overlay does not inherit the
  MVP.** Model text, MTEXT, dimensions, survey-point labels, grips and snap glyphs are drawn by
  ImGui through an axis-aligned `worldToScreen` lambda built from `worldLeft/Right/Top/Bottom`
  (`CadUi.cpp:9973`, plus ~17 inline uses of the same arithmetic). Orbiting therefore tilts the GL
  linework while every annotation stays in its plan position — orbit is not *correct* without
  fixing this, so it belongs in Phase B rather than being deferred. `Camera::WorldToScreen` added
  (with a test proving it reduces EXACTLY to the legacy mapping in plan view, and a round-trip
  test against `ScreenRay`); converting the model-space overlay call sites to it is the next step.
  Paper-space lambdas (`w2s`, `m2s`) are deliberately left alone — a sheet is 2D (ADR-025 (g)).
- 2026-08-11 — Overlay conversion done: the `worldToScreen` lambda now routes through
  `Camera::WorldToScreen` (one lambda body, 14 call sites), and annotation/survey-label sites pass
  their `insZ` / `elevation` so text rises with the geometry it belongs to.
- 2026-08-11 — **DEFECT FOUND FROM TASK-034 STEP 1B — seven aliased stride-3 circle sites.**
  While reading `PickClosestCadEntity` for the ray work I found `const auto& C =
  st.userCirclesCxCyZR;` followed by `C.size() % 3`, `ci += 3`, `C[ci + 2]` and `ci / 3`. **Seven
  such sites existed** (`CadCommands.cpp` 2708, 6316, 6442, 8219, 8624, 8783 + an index divisor at
  2722) — box-select, two extents/bbox walks, TRIM cutting-edge collection, entity picking, and
  hatch-boundary tessellation. All were reading a Z as a radius and stepping through circles at the
  wrong stride: **box-select, picking, snapping and TRIM were all silently wrong for circles.**
  **Why the step-1b audit missed them:** the rename makes every *direct* use a compile error, but a
  local alias (`const auto& C = …`) hides the identifier, so the closing grep — which looked for
  the array name and a stride pattern on the same line — could not see them. This is the honest
  limit of rename-driven refactoring, and it is now recorded as such rather than filed as a
  one-off. Re-audited with an alias-aware AWK sweep over every `auto& C = …CirclesCxCyZR` block;
  clean. `paperCircles` confirmed still stride 3 (11 sites), as ADR-025 (g) requires.
  **These were not caught by tests** because `CadCommands.cpp` is not linkable by the test target
  (it pulls the UI/GL stack) — the same structural gap already recorded against REQ-057's DXF
  round-trip. Splitting that file's pure logic is a SPEC GAP, not a Workshop choice; see §11.
- 2026-08-11 — **Phase C complete.** The input seam now branches: plan view and paper space keep
  the original linear arithmetic **bit-identical**; an orbited model view resolves the cursor as
  ray × work plane. A miss (edge-on UCS) sets `cursorValid = false` and the cursor/snap/hover block
  is skipped — the same state as the cursor being outside the viewport — rather than inventing a
  coordinate (REQ-201).
  `PickClosestCadEntity` gained an optional `const ray3d::Ray*`: when supplied, only the **distance
  metric** changes (via `d2Point` / `d2Segment`), while entity enumeration, strides and indexing
  stay shared between the plan and orbited paths so the two cannot drift apart. Lines, polylines,
  arcs and ellipses measure in true 3D; circles keep the exact analytic distance-to-circumference
  in plan view and sample their circumference when orbited. A null ray reproduces the previous
  behaviour exactly. `AppCommandState` gained the UCS (origin + normal, default world XY) and
  `uiCursorWorldZ`.
- 2026-08-11 — **Projection sweep finished — ELEVEN overlay sites in total.** Rather than wait for
  the next user report, swept `CadUi.cpp` for every world→screen mapping built from
  `worldLeft/Right/Top/Bottom`. Beyond the seven found reactively (annotations, crosshair,
  click-pick, box-select, highlights, hover, grips) there were four more: **dimension drawing**,
  the **dimension text's on-screen angle**, the **survey-point hover ring**, and the **PDF underlay
  overlay**. The three helpers that took the extents as parameters (`HitTestDimGrip`,
  `HitTestMtextGrip`, `DrawMtextRichEditorOverlay`) now take a `Camera` instead.
  The dim-text angle was the subtle one: it scaled the world rotation by the view extents, which is
  only valid while the two axes scale independently. It now projects a short step ALONG the text
  direction and measures the screen angle — same answer in plan view, correct when orbited.
  Audited to completion: every surviving extent-based projection is on a paper-space branch
  (`!edPaper`, `modelSpace`, the sheet outline, or the crosshair's paper `else`), which is correct
  because a sheet never tilts (ADR-025 (g)).
  **The lesson, stated once:** "route the overlay through the camera" read like one change and was
  eleven, because the same plan-view arithmetic had been copy-pasted into every drawing, hover and
  hit-test site over the life of the file. None of it was reachable by the Camera/ray tests, which
  are pure and were green throughout. A shared projection helper — rather than a local lambda per
  site — is what would have made this one edit; that refactor is worth doing but is a separate,
  spec-visible change.
- 2026-08-11 — **Phase D complete: the ELEV command and work-plane wiring.** REQ-058's third
  acceptance condition ("geometry drawn on a non-default UCS lands on that plane") was previously
  untestable because nothing could move the work plane.
  **Named ELEV, not UCS.** AutoCAD splits the idea: ELEV sets the elevation new geometry is drawn
  at, UCS defines a whole coordinate system. Only the elevation half exists here (the plane stays
  parallel to XY), so it carries the name of the half it actually implements rather than claiming
  to be a full UCS. `UCS` is accepted as an alias so the obvious guess works, and `ELEV W` /
  `UCS W` resets to world. Both the prompt and the inline `ELEV 12.5` form route through one
  `ApplyElevValue`, so neither can accept a value the other rejects (the TRIMSTATE precedent).
  Creation sites now read `CadWorkPlaneElevation(st)` instead of pushing a literal 0: lines,
  polylines (draft and commit), RECT, circles, arcs, ellipses, TEXT and MTEXT. **Model space only**
  — paper TEXT keeps Z = 0, since a sheet is 2D (ADR-025 (g)).
  Reading the elevation from the state at the creation site — rather than threading a Z argument
  through every command signature — is exact while the plane stays parallel to XY, which is all
  ELEV produces. A tilted plane would make Z vary across it and the sites would need the click's
  own intersection Z; that limitation is documented on `CadWorkPlaneElevation`.
  **Two silent-loss gaps found and closed while wiring this**: the camera orientation was saved to
  neither the per-tab `DrawingDocument` nor `.gs`, so switching tabs or reopening a drawing would
  quietly throw the view away. Both now persist (`.gs` keys additive and omitted at their defaults,
  so an un-orbited drawing serializes byte-identically; elevation clamped on load like the zoom).
  Tab restore also clears any in-flight animation rather than resuming another tab's.
  The status bar's UCS field reported the literal word "World"; it now reports the real plane
  ("UCS: Elev 25.0000"), because otherwise geometry lands at an unexpected elevation with nothing
  on screen explaining why (REQ-201).
- 2026-08-11 — **User testing round 1. Five defects reported, all root-caused and fixed.** This is
  the entry that matters most in this log: the pure-module tests were green and the app ran, yet
  orbit was unusable, because **every overlay path that projects world → screen had to be found,
  and I had converted only one of them.**
    1. *"the mouse moves in a circle / wraps to the other side"* — the CROSSHAIR (`CadUi.cpp` ~10930)
       took the snapped cursor's world position and re-projected it with the **plan** mapping. So
       mouse → ray → world was right, then world → screen was wrong, and as the azimuth swept, the
       crosshair traced a circle about the target and flipped past 90°. Now camera-projected.
    2. *"dragging left/right feels backwards"* — the azimuth delta followed the CAMERA; dragging
       should push the MODEL. Negated.
    3. *"hover highlights but clicking selects nothing"* — click selection used `rawPickX/rawPickY`,
       a **second, independent** mouse→world conversion (`CadUi.cpp` ~8542) that I had not
       converted, so hover (fixed) and click (unfixed) disagreed about where the cursor was. Both
       now branch identically, and the click pick receives the same ray the hover used.
    4. *"box select does not work"* — a screen rectangle is not a world-axis-aligned rectangle once
       the camera rotates, so the world-AABB test was wrong by construction.
       `ComputeSelectionFromRect` now takes an optional camera + viewport size and moves the whole
       test to SCREEN space: lines and polylines project per-vertex (exact), other types project
       their bounds (conservative — a grazing box can be included in crossing mode; recorded, not
       hidden). Needed `uiViewportWidthPx/HeightPx` on `AppCommandState` because the command layer
       cannot otherwise know the viewport aspect.
    5. *"changing a line's elevation shows the original AND the changed line"* — the selection
       highlight was built at a fixed overlay depth (`lineZ`) while the geometry sat at its real Z,
       so an orbited view drew the object twice. Highlights (line, arc, ellipse, polyline) now use
       each entity's own Z; `appendCommittedPolylineStrip` lost its flat-Z parameter entirely since
       it only ever draws committed geometry.
  **Lesson recorded:** "convert the projection" was not one change, it was five, spread across
  drawing, hit-testing and selection. The pure Camera/ray tests could not catch any of them —
  they all live in `CadUi.cpp`/`CadCommands.cpp`, which the test target cannot link. That is now
  the third distinct defect class traced to the same structural gap.
- 2026-08-11 — **REQ-059 view gizmo delivered (was TASK-036).** ImOGuizmo vendored to
  `third_party/imoguizmo.hpp` (MIT, 422 lines, unmodified per the user's FINDING-2 ruling), drawn
  top-right of the model viewport. It owns a view MATRIX and mutates it, while the camera here is
  parametrised by azimuth/elevation, so `Camera::SetFromViewRotation` decomposes the returned
  matrix back — **round-trip tested across 36 orientations**, including the pole case where azimuth
  is degenerate and must be left alone rather than snapped (snapping would spin the drawing when
  the top handle is clicked). Z-up comes from `CoordinateSystem::XZY`.

## 9. Self-verification

- [ ] build-project        — PASS
- [ ] architecture-review  — PASS
- [ ] code-review          — PASS
- [ ] dependency-audit     — n-a (no dependency added; ImGuizmo arrives in TASK-037)
- [ ] performance-review   — PASS (REQ-100 numbers recorded)
- [ ] testing              — PASS

## 10. Verification result

- Submitted:  <pending>
- Verdict:    <pending>

## 10b. Known gaps — carried forward (recorded 2026-08-11, end of session)

User-reported after a working session with 3D lines. **LINE is the only entity fully carried
through the 3D pipeline; treat every other entity type as needing the same treatment until proven
otherwise.**

```
GAP-1 — Only LINE is fully 3D end-to-end. CIRCLE is confirmed broken; assume all others are.
- Confirmed:   lines snap in 3D, preview at the right elevation, commit where previewed.
               Circles do NOT — verified by the user.
- Untested:    arc, ellipse, polyline, rect, text/mtext, dimensions, hatch/filled regions,
               survey points. Some had Z threaded through creation and preview in this task, but
               none were exercised in an orbited view, so "wired" is not "working".
- The shape of the fix is now known from LINE, and it is FOUR separate places per entity type —
  which is why this keeps recurring:
    1. snap candidates pass the entity's real Z to Consider/ConsiderSnap (CadSnap.cpp),
    2. the rubber/transform preview emits real Z (CadRubberPreview.cpp, TransformPreview.cpp),
    3. creation commits at CadCommitElevation (CadCommands.cpp),
    4. any overlay drawing projects through Camera::WorldToScreen (CadUi.cpp).
  Missing any ONE of the four produces a different, plausible-looking wrong behaviour, which is
  how each of these shipped: the geometry is right and one stage disagrees.
- Suggested approach: take circle first as the second worked example, then sweep the rest by type
  against the four-point checklist rather than by bug report.
```

```
GAP-2 — Snap glyphs should face the viewer, not lie on the work plane.
- Now:         BuildSnapOverlayLines (ViewportRenderer.cpp ~445) builds each glyph — the endpoint
               square, midpoint triangle, centre circle — as flat geometry in the world XY plane
               at the snap point's elevation. Correct in plan view; under an orbit the glyph
               foreshortens with the plane and goes edge-on and unreadable near a horizontal view.
- Wanted:      the glyph is a UI marker, not geometry. It should be screen-facing (billboarded) so
               it reads the same at any orientation, and stay a constant pixel size as it already
               does via glyphHalfPx.
- Shape of the fix: build the glyph in the CAMERA's right/up basis about the snap point rather
  than in world XY — i.e. offsets `p + right*u + up*v` using rows 0 and 1 of Camera::ViewRotation,
  the same basis Camera::ScreenRay already uses. BuildSnapOverlayLines would need the camera
  passed in; it currently takes only the view anchor.
- Same question applies to the survey-point cross markers and grip squares, which are also drawn
  as flat world geometry. Grips are ImGui-drawn (screen-space already, so fine); the survey cross
  is GL and would foreshorten.
```

```
GAP-3 — Perpendicular and intersection snaps have no Z.
- They construct their candidate points in 2D and pass no elevation, so they default to 0 and are
  measured against the cursor ray at the wrong depth. Endpoint, midpoint, centre, quadrant and
  survey-point snaps are correct.
```

## 11. Outcome

- Status at end of session: **REQ-058 substantially delivered for LINE; not complete.** Camera,
  orbit, ray picking, 3D snapping, the UCS/ELEV command and the ViewCube (REQ-059) all work and are
  in use. What is NOT done: the per-entity sweep (GAP-1), screen-facing snap glyphs (GAP-2),
  perpendicular/intersection snap Z (GAP-3), and the REQ-100 benchmark.
- **REQ-058 cannot be signed off** until GAP-1 is closed: its acceptance says geometry drawn on a
  non-default UCS lands on that plane, and that is only demonstrated for lines.
- **REQ-100 is unmeasured.** The budget is now a real number (16 ms @ 250k segments, 95th pct,
  scripted orbit) but no bench scene exists, so the perf gate is open, not passed.
- Tests: 742 → 1934 assertions, 115 → 159 cases. New pure modules `util/ray3d` and `render/Camera`
  are fully covered; `CadUi.cpp` / `CadCommands.cpp` remain unlinkable by the test target, which is
  the structural gap behind every defect class in this task.

## 12. Follow-up — 2026-08-12: curve elevation is dropped at render, and the view clips at Z 1000

Started from a user report that a 3-point circle vanishes on the third click in TOP view, in an
imported DXF. Three defects, all verified in the running app. DEFECT-C is the reported one; A and B
were found while chasing it and are independent. All three sit in places GAP-1's four-point
checklist does not look: the **render pass** (A, B) and the **preview's input point** (C).

```
DEFECT-A — Circles, arcs and ellipses render at Z = 0, whatever elevation they were committed at.
- Where:  ViewportRenderer.cpp — the committed-geometry pass passed a literal 0.f as the z argument
          to AppendCircleVcDashed / AppendArcVcDashed / AppendEllipseVcDashed, so the entity's own
          z (circlesCxCyZR[ci*4+2], CadArc::z, CadEllipse::z) was read for nothing. Line segments
          were unaffected — they carry z per vertex through CadTessellateLinetypeSegmentVc, which
          is why LINE looked "fully 3D end-to-end" and the curves did not.
- Also:   the hover / selection-highlight / preview circle overlays passed 0.017f / 0.018f / 0.032f.
          Those were depth-ORDER biases from the flat renderer; depth testing is off (draw order
          decides), so once the view could tilt they were simply wrong elevations. Now they use
          [i+2], the elevation the producer already puts in the array.
- Proof:  two identical circles, one at ELEV 0 and one at ELEV 6, then orbit. Before: they stayed
          at the same screen height. After: they separate by the elevation difference.
- Checklist consequence: GAP-1's four places are not four, they are FIVE. Add
    5. the committed-geometry render pass emits the entity's real Z (ViewportRenderer.cpp).
  An entity can pass all four earlier stages — snap, preview, commit, overlay — and still be drawn
  on the wrong plane, which is exactly what circle did.
```

```
DEFECT-B — Anything above Z 1000 is clipped out of the viewport, including in plan view.
- Where:  the ortho near/far was the literal Ortho(..., -1000.f, 1000.f) in ViewportRenderer.cpp,
          with CadViewCamera setting a matching +/-1000 on the camera. Both were carried over from
          the pre-3D pipeline for bit-identical plan-view parity, and were harmless only while
          nothing had a Z.
- Impact: a surveyed site sits a few thousand feet up. Every entity committed at that elevation —
          ELEV set by hand, or a Z inherited from an object snap, or a DXF CIRCLE imported with its
          group-30 elevation — is clipped away and never drawn. In PLAN VIEW, where Z cannot affect
          screen position at all, so nothing on screen explains the disappearance.
- Fix:    the renderer takes near/far from the camera; CadViewCamera uses the Camera default
          +/-100000. Depth testing is off, so range costs nothing but clipping reach.
- Proof:  a LINE and a 3-point CIRCLE drawn at ELEV 1500, in TOP view. Before: neither appears.
          After: both appear, alongside a Z-0 line for reference.
- Parity note: plan view stays pixel-identical for content inside the old range — an orthographic
  z has no effect on x/y, so widening the range only stops discarding geometry.
```

```
DEFECT-C — ROOT CAUSE of the report. The draft preview and the click commit at DIFFERENT points.
- Where:  main.cpp fed AppendCadDraftRubberLines / BuildTransformPreview the CURSOR (curX/curY),
          while SubmitViewportPick commits at commitX/commitY — the snapped point (CadUi ~8617).
          Those are not the same point. On a valid snap the cursor is only EASED toward it by the
          magnet (CadUi ~8359: `*outCursorX = rawX + alpha * dx`, alpha capped at 0.92), so the
          preview always trails the commit.
- Why it usually hides: for endpoint/midpoint snaps the candidate is accepted only within the
          aperture (14 px), so the gap is at most ~1.7 px and reads as a rounding wobble.
- Why it is ruinous here: a CENTRE snap is accepted by hovering the RIM but returns the CENTRE, so
          the committed point is a full radius from the cursor — arbitrarily far. `dist` is then way
          past the magnet's outer radius, alpha collapses to ~0, and the preview sits on the cursor
          while the commit lands on the centre. Same shape for perpendicular/intersection.
- Why CIRCLE 3P and not LINE: a line drawn to the wrong point is a line to the wrong point — off,
          but on screen. A circumcircle is not local in its inputs: displace the third point and the
          centre and radius diverge without bound. The user previews a normal circle, commits three
          different points, and gets a circle whose visible arc is nowhere near the view. Hence the
          exact report — "Circle complete." in the log and nothing on screen.
- Needs geometry to reproduce: a fresh drawing has no snap candidates, which is why every scripted
          repro in an empty drawing (plan TOP, rotated top, tilted, ELEV 0/10/100/1500, typed entry)
          drew correctly. The user's drawing was an imported DXF.
- Fix:    main.cpp computes commitCurX/commitCurY once — the snapped point when
          viewportSnapPickValid, else the cursor — and feeds it to the draft rubber, the transform
          preview and the TRIM cutting-line preview. The crosshair keeps its magnet easing (it is a
          pointer affordance, and the snap glyph already marks the true point); the previews now
          show what will actually be built. OFFSET keeps the raw cursor: its side-of-line test is
          about where the pointer is, not where a point would land.
- Proof:  a circle, then LINE with the cursor parked on its rim. Before: rubber band ends at the
          crosshair, green centre marker 200 px away at the centre where the click commits. After:
          rubber band runs to the centre.
- Rule this establishes: a preview that does not consume the commit point is not a preview. Any new
  draft visual must read the commit point, not the cursor — call it point 6 on GAP-1's checklist.
```

- Tests: 1934 assertions, 159 cases — unchanged and green. None of the three defects is reachable
  from the test target (ViewportRenderer needs a GL context; main.cpp's frame loop is not linkable),
  which is the same structural gap §11 records. All three were found and verified by driving the
  built app with scripted mouse/keyboard input and reading screenshots.
- Still open: GAP-1's remaining entity types, GAP-2, GAP-3, REQ-100. REQ-058 still cannot be signed
  off — but circle, arc and ellipse now satisfy its acceptance at the render stage.
