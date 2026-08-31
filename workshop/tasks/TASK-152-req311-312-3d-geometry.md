# TASK-152 — 3D geometry: a plane abstraction, and arcs/circles in arbitrary planes

- Type:    feature
- Status:  implement
- Opened:  2026-08-31
- Owner:   nrjohnson2604

## 1. Authority
- Goal:         GOAL-01 (a CAD/survey editor whose geometry is correct)
- Requirements: REQ-311 (accepted), REQ-312 (accepted). Context: REQ-154 (UCS), REQ-057 (Z on every
  entity), REQ-101 (numerical tolerance), REQ-201 (no silent failures), REQ-204 (document
  invariants), REQ-301 (minimal abstraction).
- Constraints:  CON-07 (build reproducibility). The Windows/MSVC/Ninja build is authoritative.
- Acceptance:   restated verbatim from `spec/requirements.md`:
  - REQ-311:
    - World XYZ -> plane 2D -> world XYZ returns the original point, within REQ-101 and in fact
      within 1e-9 at survey magnitude on a tilted plane.
    - The off-plane distance is an explicit output, not a dropped component, positive on the +Z side.
    - Projecting onto the plane leaves zero off-plane distance and removes exactly the normal
      component; a point already on the plane is unchanged.
    - A circle parametrised in the plane has every point at exactly the radius and zero off-plane
      distance, including on a vertical plane; on the world frame it reduces to cos/sin.
    - The UCS uses this type; there is no second plane type and no parallel implementation.
  - REQ-312:
    - A circle created with centre, radius and an arbitrary normal renders in that plane, within
      REQ-101 of the true circle.
    - An arc created on a tilted UCS has endpoints within REQ-101 of the hand-computed positions.
    - Arbitrary-plane arcs and circles survive DXF export -> import (centre, radius, angles, normal)
      within REQ-101, via group 210.
    - Arbitrary-plane arcs and circles survive `.gs` save -> reopen with every stored coordinate
      bit-identical.
    - A drawing of only XY-plane arcs and circles loads and re-saves byte-identically; no normal key
      appears and no existing test changes its expected output.
    - Object snapping (centre, quadrant, endpoint, nearest) resolves on an arbitrary-plane curve
      from an orbited camera, not only in plan view.
    - `docinvariants` checks the normal side-car against the entity count.
- Owning subsystem: Domain (`src/util/ucs.hpp`), then Commands / Render / IO for REQ-312.

## 2. Scope
- In scope: the plane contract on `ucs::Ucs`; a plane normal on arcs and circles, defaulting to
  world +Z; authoring on a tilted UCS; render, hit test and snap through the one parametrisation;
  DXF group 210 both directions; `.gs` persistence; the invariant check.
- Out of scope, each recorded as a scoped decision rather than left silent (issue #145 permits this
  explicitly for the last two):
  - **3D splines.** GoSurvey has no SPLINE command and no spline entity at all — a case-insensitive
    search for "spline" over `src/` returns two hits, both UI strings. REQ-104 already owns SPLINE
    with status `proposed`. There is nothing to extend into 3D; the gap belongs to REQ-104, not here.
  - **Authored meshes.** `CadMesh` is import-only reference geometry by REQ-063's own statement.
    Authoring one needs tessellation and a topology model, which is Phase 3's B-rep kernel (#146).
  - **Ellipses in arbitrary planes.** #120's Phase 2 checklist names Arc3D and Circle3D; the
    ellipse's stored form is a centre plus a major-axis *vector*, a different change from adding a
    normal, and no acceptance condition asks for it.
  - Paper space: arcs and circles there stay flat (ADR-025 (g)), unchanged.
- Smallest change: add the 2D half of the plane contract to the type that is already a plane; add a
  normal per curve as a side-car, defaulting to the value every existing curve already has.

## 3. Architectural boundary check
- Does this need a new abstraction / data-format change?
    - [x] Yes → escalated and recorded BEFORE implementation, as the workflow requires:
      - **D-2026-08-31-e** — `ucs::Ucs` is the plane abstraction; no second `Plane` type.
      - **D-2026-08-31-f** — the normal rides in a side-car array; `.gs` omits it when it is +Z.

      Both are recorded in `spec/project.md` and cited by REQ-311/REQ-312. No further architectural
      decision is made inside the implementation.

## 4. Questions
| # | Question | Asked (date) | Answer |
|---|----------|--------------|--------|
| Q1 | Should the plane be a new type, or should `ucs::Ucs` become it? | 2026-08-31 | Settled as D-2026-08-31-e: one type. Two types both meaning "plane" can disagree silently, and #145's own acceptance says "the UCS uses that type rather than a parallel implementation". |
| Q2 | Widen the circle stride to carry the normal, or a side-car array? | 2026-08-31 | Settled as D-2026-08-31-f: side-car. The 4-float stride is read at ~300 sites; `userCircleAttrs` already establishes the parallel-array pattern and `docinvariants` already checks that class of desync. |

## 5. Assumptions

ASSUMPTION-1: A flat (+Z normal) arc or circle must round-trip byte-identically, not merely within
tolerance.
- Because:       #145 asks for "a legacy drawing's XY-plane arcs and circles load and re-save
                 unchanged", and "unchanged" is stronger than "within REQ-101".
- Risk if wrong: over-constrains the `.gs` writer for no gain.
- Validate by:   the existing `.gs` corpus re-saving with a zero diff; asserted by test, not by eye.

ASSUMPTION-2: Authoring on the active UCS is the whole of "a circle can be created with an arbitrary
normal" — no new command is needed.
- Because:       this is AutoCAD's behaviour, and the UCS option set (REQ-154) already reaches every
                 plane the acceptance conditions name.
- Risk if wrong: a reviewer expects an explicit normal argument on CIRCLE.
- Validate by:   the headless transcript drives it through UCS + CIRCLE and asserts world vertices.

## 6. Plan
- Approach: two increments, both inside the owning subsystem at each step. First the pure geometry
  contract with no callers, so it is provable in isolation; then the entity change, threading the
  normal through the sites that already maintain a parallel attribute entry.
- Files/functions to touch:
  - `src/util/ucs.hpp` — `Point2D`, `WorldToPlane`, `PlaneToWorld`, `SignedDistanceToPlane`,
    `ProjectOntoPlane`, `PointOnPlaneCircle`.
  - `tests/UcsTests.cpp` — `[req311]` cases.
  - `src/commands/CadEntities.hpp` — normal on `CadArc`.
  - `src/commands/CadCommands.hpp` / `.cpp` — `userCircleNormals` beside `userCircleAttrs`; CIRCLE
    and ARC commit on the active UCS work plane.
  - `src/util/docinvariants.cpp` — side-car count check.
  - `src/render/ViewportRenderer.cpp`, `src/viewport/CadSnap.cpp`,
    `src/viewport/TransformPreview.cpp` — tessellate and hit-test through `PointOnPlaneCircle`.
  - `src/io/DxfIo.cpp` — emit the real 210/220/230; read them on import.
  - `src/io/GsIo.cpp` — persist the normal, omitted when +Z.
- Test approach:
  - happy path = `UcsTests [req311]`; `[req312]` unit cases for the tilted circle/arc geometry; a
    headless transcript driving UCS + CIRCLE/ARC and asserting world vertices with `EXPECT VERTEX`;
    a DXF export->import round trip; a `.gs` save->reopen round trip.
  - failure mode = a degenerate (zero) normal is refused rather than producing a garbage frame
    (REQ-201); a desynchronised side-car fails `docinvariants`; the legacy corpus re-saves with a
    zero diff.
- Steps:
  - [x] 1. Plane contract on `ucs::Ucs` + `[req311]` tests, negative-tested.
  - [x] 2. Normal on `CadArc` and the circle side-car; invariant check; default +Z everywhere.
  - [x] 3. Author on the active UCS work plane (CIRCLE, ARC).
  - [x] 4. Render / hit test / snap through the one parametrisation.
  - [ ] 5. DXF group 210 out and in.
  - [ ] 6. `.gs` persistence, omitted when +Z; legacy byte-identity test.
  - [ ] 7. Headless transcripts; full ctest; completion report.

## 7. Workflow-specific notes
- Feature: pre-flight answered (Q1, Q2 above, both settled as recorded decisions before any code).
  Tests are written alongside each increment, and each new assertion is negative-tested before it is
  trusted — an assertion that cannot fail is worse than none.

## 8. Implementation log
- 2026-08-31 Opened. Audited Phase 2's checklist against `upstream/beta` @ `d5c973b`: Point3D,
  Line3D and Polyline3D already done (REQ-057/REQ-085); `Plane`, Arc3D and Circle3D missing; mesh
  partial. Found that DXF export hard-codes `210 0.0 / 220 0.0 / 230 1.0` at all eight emit sites,
  and that import never reads group 210 at all — a tilted ARC or CIRCLE currently imports flat and
  misplaced with no message.
- 2026-08-31 Step 1 done. Plane contract added to `src/util/ucs.hpp`; 7 cases / 90 assertions in
  `UcsTests [req311]` green. Negative-tested by flipping the sign in the parametrisation-agreement
  case: it fails with `10.9172901608 == Approx( -10.9172901608 )`, then restored.

- 2026-08-31 Step 2 done. `CadArc` gains `nx/ny/nz` (default +Z); circles gain a `userCircleNormals`
  side-car in all four stores (`AppCommandState`, `DrawingDocument`, `DrawingGeometrySnapshot`,
  `CadClipboard`) plus `CadBlockContent::circleNormals`, so BLOCK/BEDIT cannot silently flatten a
  tilted circle. Helpers live beside `CadArc` in `CadEntities.hpp`: `PushCircleNormal`,
  `EraseCircleNormal`, `CircleNormalAt`, `CircleIsFlat`, `EnsureCircleNormals`, and `IsFlatNormal`
  — exact comparison, deliberately: a tolerance there would let a normal 1e-9 off +Z re-save as
  flat, which is a silent edit to the user's file. Plus the two transforms `RotateNormalAboutZ` and
  `ReflectNormalAcrossLine`.

  Wired at every site that already maintains `userCircleAttrs`: document↔state, undo snapshot,
  CIRCLE commit, clipboard copy/paste (both spaces), COPY/ROTATE/MIRROR duplication, OFFSET, BREAK
  (the arc inherits the circle plane it was cut out of), ALIGN, OVERKILL, both DELETE paths, BEDIT,
  `EnsureAttrCounts`, `.gs` load, and the DXF/DWG importers. ROTATE, MIRROR and ALIGN **transform**
  the normal rather than copying it — invisible today because every normal is +Z, and a defect the
  moment step 3 lands.

  OVERKILL now treats two circles sharing a centre and radius but lying in different planes as
  distinct objects rather than duplicates — the same reasoning Z already got there.

  ASSUMPTION-3: the reflection helper uses the same `2*phi - theta` rule `ReflectAngleAcrossLine`
  already reflects an arc sweep by, written as its matrix.
  - Because:       a mirrored arc's plane and its swept range must agree about which way the mirror
                   faced; deriving them separately is how they drift apart.
  - Risk if wrong: a mirrored tilted arc renders on the right plane but sweeps the wrong way.
  - Validate by:   step 4's tilted-arc mirror case, which cannot be written until step 3 makes a
                   tilted arc authorable.
  - **INVALIDATED, step 4.** The case was built and the assumption is wrong; the risk above is
                   exactly what happened. `ReflectAngleAcrossLine` reflects the angle in WORLD XY,
                   but a tilted arc's `startRad` is measured in the arc's own frame, which is
                   REBUILT from the normal - so once the normal is reflected the new frame is not
                   the reflection of the old one, and the two rules describe different arcs. See the
                   step 4 log entry for the measured numbers and the replacement rule.

  First run: 5 red — the `docinvariants` fixtures that hand-build a circle. That is the new check
  doing its job (`circle normals: 1 entities but 0 attribute rows`), not a regression; the shared
  `GoodDrawing()` fixture now carries a +Z normal. Three negative tests added for the side-car
  itself: a short stride, a circle with no normal, and a normal outliving its circle.

  Suite: **854/854 ctest green**, including `headless.fuzz-smoke` (REQ-204) and
  `headless.undo-redo-identity`.

  Deferred out of this step, deliberately: block circle tessellation (`cadblock.hpp`,
  `CadBlockFlattenToWorldSegs`) still draws a block circle in the XY plane. It moves to the shared
  parametrisation in step 4 alongside the model renderer, so that decision is made in one place.

- 2026-08-31 Found while wiring ALIGN, **not part of this task**: `CadCommands_Align.cpp`'s
  selective filter tests `sCircles.count(i / 3)` against a stride-4 circle store, so a selective
  ALIGN transforms the wrong circles. Pre-existing and unrelated to REQ-312; left alone to keep this
  PR to one issue. Raised with the user for a call on where it gets filed.

- 2026-08-31 Step 3 done. CIRCLE (centre+radius and 3P) and ARC (3-point) now commit on the active
  work plane. No new command: drawing on a tilted UCS is what produces a tilted curve, which is the
  AutoCAD behaviour and what ASSUMPTION-2 predicted.

  Three things the commands were getting wrong on a tilted plane, none of which a flat drawing can
  show:
  - **The radius was the XY projection of the rim pick**, short by cos(tilt) — and on a vertical
    work plane it collapsed to zero, so the circle was refused outright as "radius too small".
  - **The 3P circumcircle was solved in the XY projection**, so three points on a tilted circle gave
    a centre and radius that were simply a different circle; on a vertical plane they read as
    collinear and ARC refused them.
  - **The centre's elevation came from `CadCommitElevation()` at commit time**, i.e. the Z of the
    LAST pick rather than the pick that placed the centre. New draft fields (`circleCz`, `c3p1z`,
    `c3p2z`, `arcAz`, `arcBz`) keep each pick's own elevation, because on a vertical plane (x, y)
    stops determining Z at all — two picks on a wall differ only in height.

  The tilted path solves in the work plane's own 2D coordinates, **anchored on the first pick** so
  the numbers entering the existing float circumcircle stay small — at state-plane magnitude a float
  has a quarter-foot of resolution, the REQ-101 narrowing hazard arriving by a different door. The
  planar maths itself is untouched: it is right in any plane once the coordinates are measured in
  that plane.

  ARC's `startRad`/`sweepRad` are measured in the arc's OWN frame, `ucs::FromNormal(centre, normal)`,
  not in the UCS frame — the two differ by a rotation about the normal. `CurvePlane()` in
  `CadEntities.hpp` is now the single place that frame is built, so the commit, the renderer, the
  snap, the DXF writer and the tests cannot each pick a different zero direction.

  **Branch guard.** `CadWorkPlaneIsWorldXy()` (true for any UCS that is a translation and/or a
  rotation about Z) keeps the pre-REQ-312 float arithmetic for every flat drawing, bit for bit. That
  is REQ-154's own reasoning for its WCS branch, applied one level out; deliberately not
  `CadUcsIsWorld`, since a UCS squared to a road centreline is still a flat drawing.

  ASSUMPTION-2 (authoring on the active UCS is the whole of "an arbitrary normal") — **validated**
  by `headless.req312-arbitrary-plane-curves`.

  **Test infrastructure this needed.** The headless driver could not express the case at all:
  - `CLICK` / `PICK` hand storage x,y straight through, which carries none of the work-plane
    resolution the GUI does — and on a vertical plane is not even well posed. Added `CLICKUCS <u>
    <v>`: a click stated in the plane the user is drawing on. Under the WCS it is exactly `CLICK`.
  - `EXPECT CIRCLEXYZ <i> <cx> <cy> <cz> <r> <nx> <ny> <nz>` — the centre in world, the radius, and
    the stored plane normal. The normal is the half no count and no log line can see.
  - `EXPECT ARCPOINTS <i> <sx> <sy> <sz> <ex> <ey> <ez>` — where an arc's ends actually are,
    resolved through `CurvePlane` + `CurvePointAt`, so it asserts the same maths the renderer will
    draw with. Endpoints rather than centre/start/sweep because REQ-312's acceptance is written
    about where the ends land, and an angle triple can be plausible while its plane is wrong.

  Both new oracles negative-tested: flipping the vertical circle's normal to +Z gives
  `ny is -1.000000, expected 0.000000`, and moving an arc endpoint 5 ft up gives
  `ez is -0.000001, expected 5.000000`. Restored after.

  Suite: **857/857 ctest green** — 855 plus the new transcript, plus two `UcsTests [req312]` cases
  proving a +Z normal reproduces the world X and Y axes **exactly** (the property the whole flat-case
  guarantee rests on) and that a vertical plane frame behaves.

  Still flat, and moving to step 4 with the renderer: the CIRCLE/ARC **rubber-band preview**
  (`CadRubberPreview.cpp`) and the committed-geometry draw path both still tessellate in XY, so a
  tilted curve is stored correctly and drawn wrongly. That is the next step, not a gap in this one.

- 2026-08-31 Step 4 done. Everything that turns a stored curve back into points now does it through
  the one parametrisation: the renderer, the rubber-band preview, the transform ghost, the selection
  highlight, block flattening, four object-snap walks, and the two bounds walks. `CadEntities.hpp`
  gained the shared half of that — `CurveSampleAngle`, `SampleCurveWorld`, `AppendCurveWorldSegs`,
  `CurveEndpointsWorld`, `CurveWorldPointOnArc` — beside `CurvePlane` and `CurvePointAt`, which step
  3 had already made the single place a curve's frame is built.

  Every call site keeps its pre-REQ-312 two-dimensional arithmetic behind an `IsFlatNormal` guard.
  Not for correctness — `ucs::FromNormal` reproduces the world X and Y axes exactly for a +Z normal,
  so the two paths agree to the bit — but because this is the per-vertex loop for every curve on
  screen, and a flat drawing should not pay for a frame transform whose answer it already knows.

  **Rendering.** `AppendArcVcDashed` and `AppendCircleVcDashed` build a tilted curve's chain in
  view-relative XY plus a per-vertex Z array, and hand it to the `zPerPt` parameter
  `CadTessellateLinetypeChainVc` already had for sloped polylines — so a tilted curve is dashed and
  linetyped like any other rather than needing a path of its own. Circle normals reach the renderer
  as `CadExtendedGeometryInput::circleNormals`, not as another `RenderScene` parameter: that struct
  already describes itself as "the extra per-entity data the renderer needs", which is what this is.

  **Preview and commit now share their geometry.** The rubber preview had its own circumcircle, its
  own sweep rule and its own tessellation — a parallel implementation of the commit, which is what
  the note at `commitCurX` warns produces a preview of a shape the commit will not make. On a tilted
  plane it would have been wrong in three separate ways at once. So the geometry was split out of
  the three commit functions into `CadSolveCircleFromRimPick`, `CadSolveCircleThreePoints` and
  `CadSolveArcThreePoints` (declared in `CadCommands.hpp`, defined at file scope in
  `CadCommands.cpp` below the anonymous namespace, since the preview calls them from another TU).
  The commits are now solve-plus-commit, the preview is solve-plus-tessellate, and
  `ComputeCircumcircleRubber` / `AppendArcRubberWorld` / `AppendCircleRubberWorld` are gone.

  The arc solver also fills `arc.z` for the flat case, which the commit used to patch in afterwards.
  A solver that returns an incomplete arc is a trap for the preview, which has no second chance to
  remember which field somebody else was going to fill in later.

  **ASSUMPTION-3 is wrong, and step 4's test is what proved it.** Mirroring the wall arc (centre
  (200, 0, 0), radius 10, normal (0, -1, 0), ends at (210, 0, 0) and (190, 0, 0)) across the world
  line y = x should give ends at (0, 190, 0) and (0, 210, 0). Measured, before the fix:
  **(0, 200, -10) and (0, 200, 10)** — the right plane, the right centre, the right radius, and the
  arc turned a quarter turn inside its own plane.

  The cause is the one the assumption did not consider. `ReflectAngleAcrossLine` reflects the angle
  in WORLD XY, and that tracks the arc's frame only while the frame IS world XY. A tilted arc's
  `startRad` lives in `ucs::FromNormal(centre, normal)`, which is REBUILT from the normal — so
  reflecting the normal generally turns the frame by a further rotation about it, and an angle rule
  written for the old frame lands somewhere else in the new one. `a.startRad += rad` under ROTATE
  has the same flaw for the same reason.

  The fix does not transform the angle at all. `CadReanchorArcStart` re-measures `startRad` from
  where a KNOWN point of the arc actually went — the moved start point for a rotation, the moved END
  point for a reflection, since a mirror reverses the traversal and the existing flat rule already
  names that point as the new start. It is a no-op on a flat arc, so every flat drawing keeps the
  old arithmetic exactly. Applied at four sites: MIRROR and ROTATE in `CadCommands.cpp`, and the
  matching two in `TransformPreview.cpp`, whose ghost had drifted the same way.

  **Found while writing that test, and fixed here:** `ApplyRotationToSelection` — ROTATE with copy
  mode off, the IN-PLACE path — carried no normal at all, for arcs or for circles. Step 2 threaded
  the normal through the copying paths and missed this one, and nothing could show it while every
  normal in every store was +Z. A tilted circle rotated in place kept a plane that no longer
  contained it.

  **Scoped out of step 4, each recorded rather than left silent:**
  - **INTERSECTION and APPARENT INTERSECTION on a tilted curve.** `IsectConic` is a planar-XY conic
    by construction, and a tilted circle is not one — it projects to an ellipse, whose intersection
    with another curve is a different problem. Tilted arcs and circles are now EXCLUDED from the
    candidate set rather than flattened into it: flattening offers a point that lies on neither
    curve, which is a wrong answer presented as a right one (REQ-201). True 3D curve-curve
    intersection belongs with the kernel in #146.
  - **TRIM and BREAK against a tilted curve.** `CollectCutSegments` flattens a cutting edge to 2D
    segments. Same reasoning: planar boolean geometry, Phase 3's problem, not this requirement's.
  - **OFFSET of a tilted curve** derives its distance from a projected pick, so the offset comes out
    short by cos(tilt). The normal is already carried (step 2); the distance is not. Left alone.
  - **Box selection's bounding box for a tilted CIRCLE** stays the conservative `cx +/- r` square.
    It is larger than the true footprint, never smaller, so a crossing selection cannot lose one and
    a window selection is merely stricter than it needs to be. Arcs needed the real fix and got it,
    because their XY walk produced bounds that were too SMALL, which loses geometry.

  **REQ-312's acceptance names a snap GoSurvey does not have.** The bullet reads "centre, quadrant,
  endpoint, nearest". `CadSnap::Kind` has Endpoint, Midpoint, Center, Perpendicular, Intersection,
  ApparentIntersection, Grip and Surface — there is no QUADRANT and no NEAREST, and adding them is a
  REQ-062 change, not a REQ-312 one. Every mode that does exist is now plane-aware. Raised with the
  user; the acceptance bullet wants rewording to name the modes the app actually has.

  **Tests.** `tests/headless/transcripts/req312-tilted-curves-drawn-and-edited.txt` (57 steps): a
  tilted arc found by a tight WINDOW selection, the two mirror cases above, and ROTATE carrying a
  circle's plane. `tests/CadSnapTests.cpp` gains four `[CadSnap][req312]` cases driving
  `CadSnap::FindBest` with a pick RAY — the orbited path, where a candidate at the wrong elevation is
  genuinely far away rather than coincidentally on top of the right one.

  All negative-tested, each against the specific line that makes it pass:
  - flat-only `ArcRoughBounds`: the WINDOW select finds nothing (`expected 1, got 0`).
  - the normal not carried through `ApplyRotationToSelection`: `nx is 0.000000, expected -1.000000`.
  - `ArcSnapPoint` forced to its flat branch: 3 of the 4 snap cases fail, and the flat-arc
    regression guard stays green — which is the half of that result worth having.
  - the mirror re-anchor: the pre-fix numbers recorded above ARE its negative test. They were
    measured from a real run, before the fix existed.

  Suite: **862/862 ctest green** (after `cmake -S . -B build` -- the transcript glob has no
  CONFIGURE_DEPENDS, so a new transcript is invisible to ctest until the project is reconfigured).

  Still open for step 5 onward: DXF group 210 (the writer emits a hard-coded +Z at all eight sites
  and the reader ignores it), and `.gs` persistence — a tilted arc's normal is held in memory and
  dropped on save today, which is why the probes behind these tests read a plane through
  `EXPECT ARCPOINTS` rather than out of the file.

## 9. Self-verification
- [ ] build-project
- [ ] architecture-review
- [ ] code-review
- [ ] dependency-audit
- [ ] performance-review
- [ ] testing

## 10. Verification result
- Submitted:  —
- Verdict:    —
- Findings:   —

## 11. Outcome
- Requirements satisfied: —
- Tests added:            `UcsTests [req311]` (7 cases)
- Refactors:              none
- Docs updated:           `spec/requirements.md`, `spec/project.md`
- Done:                   —
