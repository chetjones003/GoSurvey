# TASK-159 — 3D geometry: a plane abstraction, and arcs/circles in arbitrary planes

- Type:    feature
- Status:  submitted (rebased onto beta @ ddc726d; automated verification PASS, 917/917)
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
    - Object snapping resolves on an arbitrary-plane curve from an orbited camera, not only in
      plan view, for every snap mode GoSurvey has: Endpoint, Midpoint, Center, Perpendicular,
      Intersection, Apparent Intersection, Grip and Surface. (Revised 2026-09-01; see the step-4
      log entry and REQ-312's own Revisions line for why the original wording could not be met.)
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
  - [x] 5. DXF group 210 out and in.
  - [x] 6. `.gs` persistence, omitted when +Z; legacy byte-identity test.
  - [x] 7. Headless transcripts; full ctest; completion report.

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

  **Reworded 2026-09-01, at the user's direction.** REQ-312's snap bullet now names the eight
  modes `CadSnap::Kind` has, all of which are plane-aware and covered from an orbited pick ray.
  QUADRANT and NEAREST turned out to belong to no accepted requirement at all — REQ-062 is
  specifically the intersection pair, and REQ-039's "quadrants" are GRIP handles, not a snap —
  so they are a new requirement in the object-snap family rather than a shortfall of this one.
  Recorded in REQ-312's Revisions line so the change is visible, not silent.

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

- 2026-08-31 Step 5 done. DXF group 210 out and in, for ARC and CIRCLE.

  **Group 210 is not an annotation — it changes what groups 10/20/30 MEAN.** With a non-+Z
  extrusion, an entity's own coordinates are OBJECT-coordinate values in the Arbitrary Axis
  Algorithm frame the normal defines, not world ones. So writing the real normal was only half the
  change: the centre has to be written in that frame too, or the file round-trips perfectly through
  GoSurvey while describing a different circle to every other consumer. `ucs::FromNormal` IS that
  algorithm (REQ-311 / D-2026-08-31-e), and it returns the world axes exactly for a +Z normal — so a
  flat curve's OCS point is its world point bit for bit, and the flat path is untouched.

  **The measurement that decided the implementation.** Group 210 is the one value in a DXF whose
  error is ANGULAR rather than positional: the reader rebuilds the entity's whole frame from it, and
  an angular error dTheta moves a point R from the world origin by about R * dTheta. At state-plane
  magnitude R is ~1e6. A standalone probe over 400,000 random normals with centres out to +/-2e6
  (`gosurvey-dxf-roundtrip-family`'s rule — measure, do not reason about the float bands):

  | group 210 written as | worst centre error | worst rim error |
  |---|---|---|
  | six decimals (`std::to_string`, what the writer does to every other number) | **65.39 ft** | 65.40 ft |
  | `%.9g` (round-trips a float) | 0.008997 ft | 0.008996 ft |
  | `%.17g` (round-trips a double) | **0.00000086 ft** | 0.00000086 ft |

  REQ-101 is +/-0.01 ft, so six decimals fails by ~6500x and `%.9g` passes with no margin at all —
  it spends the entire budget. `%.9g` is not enough because the READER parses to double: nine digits
  identify the float but not the double the reader ends up holding, and that residual is an angle
  too. The writer now formats 210/220/230 with `%.17g` and every other number the way it always has.
  Had this gone in as "replace the hard-coded 0.0/0.0/1.0 with `std::to_string(nx)`", it would have
  shipped misplacing tilted curves by tens of feet with a green suite — every test near the origin
  passes, because there the lever arm is short.

  **Export.** `extrusionText` and `ocsPointOf` beside `worldX`/`worldY`. CIRCLE and ARC branch on
  `IsFlatNormal`: flat emits the literal `"0.0"/"0.0"/"1.0"` it always has, tilted emits the real
  normal and an OCS centre. Groups 50/51 need NO adjustment — `DxfArcToWrite` measures them in the
  arc's own frame, and the OCS shares that frame's axes; the two differ only in where the origin
  sits, which an angle about the centre cannot see.

  The header extents sweep walks a tilted arc in its own plane, for the same reason it already takes
  its angles from `DxfArcToWrite` rather than from memory: the box has to be the one a READER
  computes from this file, or the origin the header asks for is not the origin the import settles on
  and the round trip never stabilises (issue #94's failure). Z is accumulated per sample there too —
  a tilted arc spans elevations and its centre's Z is not its extent. That does not disturb the
  agreement the note in that function depends on: `ShiftAllStorageBy` is XY-only.

  **Import.** `DxfOcsToWorld` and `DxfExtrusionIsFlat` beside `DxfArcToWrite`. Both parse branches
  read 210/220/230, defaulting to +Z when absent (which is what every flat DXF omits), resolve the
  OCS point to world, and hand the normal to `appendCircleXF` / `appendArcXF` — which now store it,
  and which walk a tilted curve in its own plane in the non-identity-INSERT branch where the curve
  degrades to segments. A zero-length 210 is a malformed file: the entity is REFUSED and counted,
  and the count is reported once at the end in the shape the other four skip counters already use,
  rather than being quietly taken as flat (REQ-201). That needed a fifth counter pointer on
  `ParseEntityRegion`, threaded through its three call sites.

  This half also closes the live silent-import defect D-2026-08-31-f named: until now the reader
  never looked at group 210 at all, so a tilted ARC or CIRCLE from any other program arrived flat
  and misplaced with no message.

  **Tests.** `tests/headless/transcripts/req312-dxf-arbitrary-plane-roundtrip.txt` (71 steps), three
  sections: a flat drawing's round trip unchanged; a tilted circle and arc surviving with their
  plane; and the same at state-plane magnitude on a deliberately messy plane.

  That third section took two attempts, and the first one is worth recording. A 45-degree plane does
  NOT catch a coarsely-written 210: its normal is (0, -0.7071068, 0.7071068), and rounding both
  components to six decimals scales the vector without TURNING it — `FromNormal` normalises the
  length away and the error cancels. The transcript passed against a writer that was plainly wrong.
  The plane is now stated by `UCS ZAxis` along (1, 2, 3), whose unit normal has three unrelated
  components that six decimals genuinely rotates, with the circle on the UCS origin ~2,150,000 ft
  out so the rotation has a lever arm.

  Negative-tested, both halves, against that file:
  - 210 written at six decimals instead of `%.17g`: `cz is 0.933466, expected 0.000000`.
  - the reader not parsing group 210 (its pre-step-5 state): the OCS point is taken for a world
    point and `cx is 1431083.500000, expected 2000000.000000` — 569,000 ft out.

  **What that transcript cannot catch, stated plainly:** if the writer and the reader both skipped
  the OCS conversion they would agree with each other, every assertion would pass, and the file
  would still describe a different circle to every other DXF consumer. The guard against that is not
  a test here — it is that both sides go through one named helper built on `ucs::FromNormal`, the
  algorithm the DXF specification itself names for group 210. A real cross-consumer check wants a
  fixture written by AutoCAD or LibreDWG; noted, not built.

  Suite: **863/863 ctest green** (after `cmake -S . -B build` for the new transcript).

  Still open: step 6, `.gs` persistence — the normal is held in memory and dropped on save, so a
  tilted curve currently survives a DXF round trip but not a save and reopen.

- 2026-08-31 Step 6 done. `.gs` persistence for a curve's plane.

  The normal is an ADDITIVE key that is OMITTED when it is world +Z (D-2026-08-31-f) — the same
  tolerant-key bargain `circlesZ` and the arc's own `z` already make (ADR-020 (d)), and no
  `kGsFormatVersion` bump. That omission is not a space optimisation: it is the entire mechanism by
  which a legacy drawing re-saves byte for byte. Written unconditionally, every drawing in existence
  would come back from its next save as a different file.

  - `CadArcToJson` / `CadArcFromJson`: `nx`/`ny`/`nz`, absent meaning world +Z. `IsFlatNormal`
    compares exactly, deliberately — a tolerance there would let a normal 1e-9 off +Z save as flat,
    which is a silent edit to the user's file.
  - The document's circle side-car rides in `circlesN`, three floats per circle, beside the existing
    `circles` and `circlesZ`. Omitted when every circle in the drawing is flat.
  - `CadBlockContent::circleNormals` likewise, under `circleNormals`, omitted when every block
    circle is flat. BLOCK and BEDIT both round-trip through there, which is why the field exists on
    the struct at all.

  **A cross-document leak, found while wiring the loader.** `EnsureCircleNormals` only grows or
  truncates, so the side-car was never CLEARED on load — opening a second document over a first kept
  the first document's normals for every index the new one also had. Invisible while every normal in
  every file was +Z, and a real defect the moment one is not. The loader clears first now.

  **Scoped out, recorded rather than left silent: the DWG ENTITY layer does not carry the plane.**
  `LibreDwgCad.cpp` neither writes nor reads an extrusion direction (its own note has said so since
  step 2). Since D-2026-08-29-j made DWG the drawing document, a tilted curve saved as `.dwg`
  round-trips correctly *through GoSurvey* — the `.gs` JSON trailer carries it and wins on reload —
  but AutoCAD opening that same file sees a flat circle, and a tilted circle in a foreign DWG
  imports flat. REQ-312's acceptance names DXF and `.gs`, both of which are now met; the DWG entity
  layer is REQ-175 / ADR-044 territory and wants its own issue.

  **Tests.** `tests/headless/transcripts/req312-gs-plane-persistence.txt` (61 steps): a flat drawing
  saving with no plane key anywhere and re-saving byte-identically, and a MIXED drawing — tilted
  circle, tilted arc, flat circle — surviving save and reopen with a byte-identical re-save. The
  mixture is deliberate: a side-car written only for the tilted entries, or indexed off by one, puts
  the wall's plane on the flat circle.

  This needed one new oracle. A save/reopen/re-save round trip **cannot state acceptance condition
  2**: it compares the file only to itself, so it passes just as happily with a normal written on
  every circle in the file. The property that matters is that the key is not there at all, which is
  a check on CONTENT — `EXPECT FILELACKS <path> "<text>"`, with `EXPECT FILECONTAINS` as its
  counterpart so the absence check cannot pass by naming a key that never exists under any
  circumstances. Both read the `.gs` JSON out of a DWG trailer the same way `SAMEFILE` does.

  Negative-tested, each against the line that makes it pass:
  - the arc normal written unconditionally: `FILELACKS: ...req312-flat-a.gs contains: nx`.
  - `circlesN` not read back: `EXPECT CIRCLEXYZ 0: ny is 0.000000, expected -1.000000`.

  Suite: **864/864 ctest green.**

- 2026-08-31 Step 7 done. The transcripts were written with the steps that needed them, so what this
  step actually contained was the whole-branch sweep, the full suite, and this report.

  **Suite at the time: 864/864 ctest green**, zero build warnings under `/W4 /permissive-` (TASK-150 cleared the
  ~200 that were there; this branch adds none). Reconfigured first -- `file(GLOB)` has no
  `CONFIGURE_DEPENDS`, so the four new transcripts are invisible to ctest until `cmake -S . -B build`
  runs.

  **The sweep found three defects, all in what the branch had already written, none behavioural.**
  Fixed in `76b3f20`:
  - `src/commands/CadCommands.hpp` was **stored** as CRLF from step 3 (`786cd53`) onward, against
    `.gitattributes`' `* text=auto`. Every other file on the branch stores LF, so the one header
    carrying the new API read as 4493 insertions / 4389 deletions against `beta` -- the whole file --
    hiding the 105 lines that actually changed. `git add --renormalize` restores LF storage; the
    working tree keeps CRLF, which is what `core.autocrlf=true` checks out. The branch diff against
    `upstream/beta` drops from **7357/4663 to 2969/275**. Worth stating how it hid: the working copy
    was *correct* the whole time, so every check short of `git show HEAD:<path>` said the file was
    fine.
  - The same write left a bare CR **inside** line 3485, which is why `CadWorkPlaneIsWorldXy`'s
    comment read `deliberately NOT <CR>ef CadUcsIsWorld`. Restored to the contrast the paragraph is
    actually drawing: deliberately NOT `CadUcsIsWorld`.
  - Two dropped apostrophes beside it: `REQ-154s own reasoning`, and `the hit points own Z` in
    `HeadlessDriver.cpp`'s CLICKUCS note. A scan of every added line on the branch for mojibake,
    mid-line CRs and dropped possessives found nothing else.

  **Checked, and correct as it stands:** every one of the 38 mutation sites on `userCirclesCxCyZR`
  maintains the side-car; `ucs::Ucs` is the only plane type in `src/` besides the `ray3d::Plane`
  REQ-311 scopes out loud; the new `TEST_CASE` names are pure ASCII (a non-ASCII one fails under
  ctest while passing when run directly); and every `CMD CIRCLE` in the transcripts is followed by
  `ESC`, which a persistent command needs.

  **Re-derived rather than taken on trust -- the mirror rule.** A reflection is orientation-reversing,
  so in the frame `ucs::FromNormal` rebuilds from the reflected normal the arc's angle runs as
  `phi - t`: the traversal reverses, the new start is the moved END point, and `sweepRad` keeps its
  sign. That is what `CadReanchorArcStart` does at the MIRROR sites, so step 4's replacement for the
  invalidated ASSUMPTION-3 is right for the reason it claims, not only in the one case measured.

  **One more scope note, recorded rather than left silent.** The DXF header extents sweep walks a
  tilted ARC in its own plane (step 5) but leaves a tilted CIRCLE as the conservative `cx +/- r`
  square, contributing no Z at all. That is deliberate and should stay: `ComputeWorldExtents` uses
  the identical square for circles, so writer and reader still agree and issue #94's ratchet
  condition holds; and making circles contribute Z would change `$EXTMIN`/`$EXTMAX` for every
  existing flat drawing, which is exactly the byte-identity REQ-312 promises not to disturb. Arcs
  needed the plane walk because their XY-projected bounds come out too SMALL, which crops geometry;
  the circle square errs the other way, so it is safe where the arc box was not.

  **Carried into the PR as open questions rather than answered here:** REQ-312's snap acceptance
  bullet names QUADRANT and NEAREST, which `CadSnap::Kind` does not have (step 4's log); and the
  pre-existing selective-ALIGN stride bug found while wiring the normals, which stays out of this PR
  under one-issue-one-PR and wants filing on its own.

- 2026-09-01 Rebased onto `upstream/beta` @ `ddc726d`, which had moved **27 commits** since this
  branch was cut. Renumbered TASK-152 -> **TASK-159**, reworded REQ-312's snap acceptance bullet, and
  found one real defect in the process.

  **The identifiers had to be re-checked, and one had gone.** REQ-311, REQ-312, D-2026-08-31-e and
  D-2026-08-31-f were all still free. `TASK-152` was **not** — upstream now has
  `TASK-152-issue117-text-nonfinite-coords.md`. Renumbered to the next free number, 159, by named
  file with an asserted hit count per file rather than a blanket `sed`: both TASK-152 files exist in
  the tree afterwards, and upstream's references to its own are legitimate.

  **Textual conflicts: one.** `spec/project.md`'s decision-log append region, where beta had added
  D-2026-08-31-c (per-viewport UCS) at the same spot. Resolved newest-first: f, e, then c. Ten shared
  code files auto-merged, `src/util/ucs.hpp` and `CadCommands.cpp` among them.

  **The CRLF cost a detour.** Step 3's stored-CRLF header (see the step 7 entry) made every replayed
  commit conflict as ONE hunk covering the whole file. `merge.renormalize=true` did not help under
  `git rebase`. What worked: re-run the three-way merge with all three inputs stripped to LF —
  `git show HEAD:<p>`, `git show REBASE_HEAD^:<p>`, `git show REBASE_HEAD:<p>` through `tr -d '\r'`,
  then `git merge-file`. Every one resolved with **zero** real conflicts, which is the proof that the
  whole-file conflict was line endings and nothing else. A side benefit: the replayed history now
  stores LF from the first commit, so `4a81475` is just the three comment repairs.

  **The defect: a borrowed predicate changed meaning under us.** REQ-312's entire flat-drawing
  guarantee rests on one branch guard, and it was written as

      inline bool CadWorkPlaneIsWorldXy(const AppCommandState& st) { return ucs::PlanViewIsExact(st.activeUcs); }

  At the branch point `PlanViewIsExact` meant "the UCS Z is world +Z" — exactly the property needed.
  Upstream `004635b` (issue #153, exact PLAN of a tilted UCS via a camera roll axis) **redefined it**
  to `IsRightHandedOrthonormal(u)`, which is true for every valid frame. That is a correct change for
  what the predicate is named after — it answers the CAMERA's question, "can PLAN put UCS +Y up the
  screen exactly?", and #153 gave `Camera` the roll axis that made the answer yes. But the name, the
  signature and our call site were all unchanged, so the rebase auto-merged and the build was clean
  and warning-free while **every tilted drawing silently took the FLAT branch**.

  All four REQ-312 transcripts went red and nothing else in 917 tests noticed:
  `ARC - points are collinear, or the work plane is not valid` — the three wall picks (210,0,0),
  (200,0,10), (190,0,0) projected onto one line, which is what a flat solver sees.

  Fixed by making the guard state its own condition inline (`|zAxis.x| <= 1e-6 && |zAxis.y| <= 1e-6
  && zAxis.z > 0`) instead of borrowing a predicate named for another subsystem's concern. It is the
  only caller `PlanViewIsExact` had outside `ucs.hpp`. **The lesson worth carrying:** a clean
  auto-merge says the text did not collide, not that the meaning survived — so after a rebase, diff
  every borrowed helper across the range and re-run the suite, never just the build. Here the
  behavioural transcripts were the only thing between this and a merged PR that drew every tilted
  curve flat.

  **The snap acceptance bullet, reworded (user's decision, 2026-09-01).** It now names the eight
  modes `CadSnap::Kind` has. QUADRANT and NEAREST turned out to belong to no accepted requirement at
  all — REQ-062 is specifically the intersection pair, and REQ-039's "quadrants" are GRIP handles,
  not a snap — so they are a new requirement in the object-snap family, recorded in REQ-312's
  Revisions line rather than dropped.

  **A bonus from the merge, unasked for:** beta's REQ-155 puts a per-viewport frame into
  `AppCommandState::activeUcs` while floating model space is entered, and every REQ-312 authoring
  path reads exactly that. So drawing inside a floating viewport now commits onto that viewport's
  work plane for free. Checked against REQ-155's own rule that persistence must read
  `CadDrawingScopedUcs()` instead: none of this task's readers is a persistence path.

  Suite after the rebase and the guard fix: **917/917 ctest green**, zero warnings.


## 9. Self-verification
- [x] build-project        — PASS. `./dev/build` (MSVC/Ninja release, the authoritative build under
      CON-07). Zero warnings under `/W4 /permissive-`.
- [x] architecture-review  — PASS. Both architectural decisions were recorded BEFORE any code
      (D-2026-08-31-e, D-2026-08-31-f) and no third one was made inside the implementation. There is
      one plane type: a search of `src/` for a second `struct`/`class Plane` returns only
      `ray3d::Plane`, which REQ-311 scopes out loud as the origin+normal ray-casting form. Circle
      normals reach the renderer through `CadExtendedGeometryInput` — the struct that already
      describes itself as the extra per-entity data the renderer needs — rather than as another
      `RenderScene` parameter, so the Render boundary is unchanged.
- [x] code-review          — PASS, with the three defects above found and fixed (`76b3f20`; comments
      and line endings only). The property worth naming: a curve's frame is built in one place
      (`CurvePlane`) and sampled in one place (`CurvePointAt`), so the renderer, the preview, the
      snap, the two extents sweeps and the DXF writer cannot hold different opinions about where a
      tilted curve goes. Step 4 deleted a parallel implementation rather than adding one.
- [x] dependency-audit     — n/a. No dependency added. The only new include is in-tree
      (`util/ucs.hpp` into `commands/CadEntities.hpp`), and it flows downward.
- [x] performance-review   — PASS by construction, not by measurement, and said plainly: every
      per-vertex loop keeps its pre-REQ-312 two-dimensional arithmetic behind an `IsFlatNormal` /
      `CadWorkPlaneIsWorldXy` guard, so a flat drawing executes the same instructions it did before
      plus one well-predicted branch per entity. No benchmark was run, because there is no new work
      on the flat path to measure and the tilted path is new capability with no prior number to beat.
- [x] testing              — PASS. 16 new Catch2 cases and 4 headless transcripts (244 steps), full
      suite 917/917 on the rebased tree (864/864 before it). Every new assertion was negative-tested
      against the specific line that makes it pass, and those failure messages are quoted in the step
      logs above — an assertion that cannot fail is worse than none.
- [ ] MANUAL GUI (not run) — REQ-312's first acceptance bullet is about RENDERING, and the tilted
      draw path (`AppendArcVcDashed` / `AppendCircleVcDashed`'s per-vertex-Z chains) plus the
      rubber-band preview are the one part of this work no transcript reaches: the transcripts
      assert geometry, not pixels. Drawing on a tilted UCS and orbiting is a GUI check. Raised with
      the user rather than assumed either way.

## 10. Verification result
- Submitted:  — (pending the PR against `chetjones003:beta`; the merge is the verification act)
- Verdict:    —
- Findings:   —

## 11. Outcome
- Requirements satisfied: REQ-311 (Acceptance met: yes). REQ-312 (Acceptance met: yes — the snap
                          bullet was reworded 2026-09-01 to name the eight modes `CadSnap::Kind`
                          actually has, every one of them now plane-aware and tested from an
                          orbited pick ray. QUADRANT and NEAREST belong to no accepted
                          requirement and are a new one in the object-snap family.)
- Tests added:            `UcsTests [req311]` (7 cases, 90 assertions) and `[req312]` (2);
                          `CadSnapTests [CadSnap][req312]` (4, driven through `FindBest` with a pick
                          RAY — the orbited path); `DocInvariantsTests [docinvariants][req312]` (3:
                          a short stride, a circle with no normal, a normal outliving its circle);
                          `headless.req312-arbitrary-plane-curves` (55 steps),
                          `headless.req312-tilted-curves-drawn-and-edited` (57),
                          `headless.req312-dxf-arbitrary-plane-roundtrip` (71),
                          `headless.req312-gs-plane-persistence` (61). New driver verbs: `CLICKUCS`,
                          `EXPECT CIRCLEXYZ`, `EXPECT ARCPOINTS`, `EXPECT FILECONTAINS`,
                          `EXPECT FILELACKS`.
- Refactors:              one, and it removed code rather than adding it: the CIRCLE/ARC geometry was
                          lifted out of the three commit functions into `CadSolveCircleFromRimPick`,
                          `CadSolveCircleThreePoints` and `CadSolveArcThreePoints`, deleting the
                          rubber-band preview's parallel circumcircle, sweep rule and tessellation
                          (`ComputeCircumcircleRubber`, `AppendArcRubberWorld`,
                          `AppendCircleRubberWorld`).
- Docs updated:           `spec/requirements.md` (REQ-311, REQ-312, both traceability rows),
                          `spec/project.md` (D-2026-08-31-e, D-2026-08-31-f), this task log.
- Done:                   2026-08-31

## 12. Completion report (CLAUDE.md workflow step 7)

```
COMPLETION REPORT — TASK-159 — 2026-08-31
- Requirements satisfied:  REQ-311 (Acceptance met: yes)
                           REQ-312 (Acceptance met: yes — the snap bullet was reworded 2026-09-01
                             to name the eight modes CadSnap::Kind has, all now plane-aware.
                             QUADRANT and NEAREST belong to no accepted requirement; they are a
                             new one in the object-snap family, recorded in REQ-312's Revisions.)
- Summary:                 ucs::Ucs becomes the project's one plane type (no second Plane), and
                           arcs and circles carry a plane normal defaulting to world +Z — authored
                           on the active UCS, drawn/picked/snapped/bounded through one
                           parametrisation, written and read through DXF group 210, and persisted
                           in .gs with the key omitted when flat so legacy drawings re-save byte
                           for byte.
- Tests:                   16 Catch2 cases (UcsTests [req311] x7 / [req312] x2, CadSnapTests
                           [req312] x4, DocInvariantsTests [req312] x3) + 4 headless transcripts
                           (244 steps). Happy path and failure mode both; every new assertion
                           negative-tested against the line that makes it pass. 917/917 green on the
                           rebased tree (864/864 before the rebase onto beta @ ddc726d).
- Verification verdict:    self-verification PASS (findings resolved: the three defects the branch
                           sweep found — one CRLF-stored header and two mangled comments, 76b3f20).
                           External verdict pending the PR; chet's merge is the verification act.
- Assumptions:             ASSUMPTION-1 validated (byte-identical flat re-save, asserted by
                           transcript). ASSUMPTION-2 validated (authoring on the active UCS is the
                           whole of "an arbitrary normal"). ASSUMPTION-3 INVALIDATED by its own
                           step-4 test and replaced by CadReanchorArcStart — recorded, not quietly
                           dropped.
- Architectural decisions: none made by Workshop. Both were escalated and recorded before any code:
                           D-2026-08-31-e (one plane type) and D-2026-08-31-f (side-car normal,
                           .gs omits it when +Z).
- Dependencies:            none added.
- Technical debt noted:    (1) the tilted DRAW path and rubber-band preview have no automated
                           coverage — pixels, not geometry; removal condition is a GUI pass or a
                           framebuffer-capable harness. (2) INTERSECTION/TRIM/BREAK/OFFSET against
                           a tilted curve are excluded rather than wrong; removal condition is the
                           #146 kernel. (3) the DWG entity layer carries no extrusion direction, so
                           a tilted curve saved as .dwg reads flat in AutoCAD; wants its own issue
                           under REQ-175 / ADR-044. (4) found while wiring ALIGN and deliberately
                           NOT fixed here: CadCommands_Align.cpp tests sCircles.count(i / 3) against
                           a stride-4 store, so a selective ALIGN transforms the wrong circles —
                           pre-existing, unrelated to REQ-312, wants filing on its own.
- Build:                   reproducible; ./dev/build (MSVC/Ninja release, authoritative under
                           CON-07), zero warnings under /W4 /permissive-.
- Docs updated:            spec/requirements.md (REQ-311, REQ-312, both traceability rows),
                           spec/project.md (D-2026-08-31-e, D-2026-08-31-f), this task log.
```
