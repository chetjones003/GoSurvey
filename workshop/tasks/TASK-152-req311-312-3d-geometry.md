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
  - [ ] 2. Normal on `CadArc` and the circle side-car; invariant check; default +Z everywhere.
  - [ ] 3. Author on the active UCS work plane (CIRCLE, ARC).
  - [ ] 4. Render / hit test / snap through the one parametrisation.
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
