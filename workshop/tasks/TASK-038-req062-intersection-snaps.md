# TASK-038 — REQ-062: Intersection and apparent-intersection object snaps

- Type:    feature
- Status:  done
- Opened:  2026-08-12
- Owner:   Workshop

## 1. Authority

- Requirements: **REQ-062** (accepted 2026-08-12 — filed by this task; see §3). Also closes the
  "intersection snaps resolve correctly from an orbited camera" condition of **REQ-058**.
- Constraints: **REQ-101** (±0.01 ft) — the binding one here; REQ-100, architecture §11.
- Owning subsystem: util (pure intersection math), viewport (snap), UI (toggles, menu, glyph), IO
  (persistence).

## 2. Scope

- **In scope:** an Intersection snap (objects that genuinely meet in 3D) and an Apparent
  intersection snap (objects that cross as projected into the current view). Every pair of line
  segments, polyline edges, arcs, circles and ellipses.
- **Out of scope:** *extended* intersection — AutoCAD's mode that finds where two objects **would**
  meet if lengthened. The user asked for the depth case, not the extension case, and it is a
  separate behaviour with its own surprises.

## 3. Spec gap — filed and recorded before any code

Intersection needed no new requirement: REQ-058's acceptance had named it since 2026-08-11 while no
such snap existed, and TASK-036 Q1 resolved that as "build it".

**Apparent intersection was new scope with no requirement behind it.** Per CLAUDE.md the Workshop
does not invent requirements, so before writing code this task filed **REQ-062** in
`spec/requirements.md` and recorded the decision in `spec/project.md`'s decision log, including the
two user rulings that bind the design (§4). Only then was anything built.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | Apparent intersection has two candidate 3D points (one per object). Which is returned? | 2026-08-12 | **The one nearer the camera** — the object the user is visually pointing at. AutoCAD returns "the point on the first object picked"; our snap has no pick order, so this is the closest honest equivalent, and it is stable under orbit. |
| Q2 | How far does entity coverage go, given ellipse×curve is a quartic? | 2026-08-12 | **Everything**, analytic where a closed form exists and numerically refined where it does not. |

## 5. Assumptions

```
ASSUMPTION-1: "Actually intersects" means elevations agree within REQ-101, not exactly.
- Because:       exact float equality is never the right test for coordinates, and REQ-101's
                 ±0.01 ft is the project's own definition of "the same point". Two segments whose
                 elevations differ by 0.001 ft at the crossing are touching for survey purposes.
- Risk if wrong: a hairline miss reads as a hit. That is the correct trade — the alternative is a
                 snap that silently refuses on geometry the user believes is connected.
- Validate by:   asserted in CurveIntersectTests via the kReq101 margin throughout.
```

## 6. Design

### The pure module — `src/util/curveintersect.{hpp,cpp}`

Dependency-free (only `<vector>`, `<cmath>`, `<algorithm>`), so the test target links it directly
like `DwgProbe.cpp` and `CadLinetype.cpp`. That placement is the point: intersection math is wrong
in ways no screenshot reveals, and this is where REQ-101 is actually enforced.

**Why not tessellate.** Chording an arc and running segment×segment over the result is the cheap
implementation and it misses REQ-101 by two orders of magnitude: a 24-chord arc of radius 100
deviates by `r·(1 − cos(π/24)) ≈ 0.86 ft` — **86× the tolerance**. Everything is analytic except
conic×conic with a non-circular member.

**One parametric form for circle, arc and ellipse**: `P(t) = c + u·cos t + v·sin t`, with `u`/`v`
arbitrary 2-D vectors (not required perpendicular or equal in length). This is not tidiness — it is
the only form that makes apparent intersection reuse the same code, for two reasons:

- **It is closed under linear maps.** Projecting a conic into the view basis is just projecting
  `c`, `u`, `v`. An orbited circle genuinely projects to an *ellipse*, so a screen-space path that
  only understood circles would be wrong exactly when it is needed.
- **Projection preserves the parametrization**, so a parameter solved on screen reads straight back
  as a world point through the *unprojected* shape. That is what lets APPINT recover both candidate
  3D points and compare their depth.

Segment×conic is analytic for **every** conic including ellipses, with no special case: substituting
the segment and eliminating its parameter leaves `α·cos t + β·sin t = γ`, which solves in closed
form. Circle×circle uses the radical line. Anything else is bracketed by chord crossings (with an
AABB reject, points precomputed once, no allocation in the loop) and refined by a 2-D Newton step on
`P(u) − Q(v) = 0`; a singular Jacobian means the curves are tangent, and the root is abandoned
rather than nudged somewhere arbitrary.

### Cost — the only pairwise snap in the system

Every other snap is linear in the drawing; these are O(k²). They stay affordable because **an
intersection lies ON both objects**, so an object can only contribute if it passes within the snap
aperture of the cursor. `GatherNearCursor` applies that as a bounding-sphere test —
`dist(cursor, centre) ≤ tol + radius`, conservative by the triangle inequality, so no true
intersection is culled — measured against the 3-D pick ray when orbited and in plan XY otherwise.
What survives is the handful of objects under the pointer, so `k` is normally 0–5.

The Shift+right-click snap-picker menu is the one place this could still run away: it lists "all in
the model" for every other kind, which for a pairwise snap is unbounded work on a real topo. There
it is deliberately limited to roughly one screen height around the click — recorded in the code as a
trade, not left as an accident.

## 7. Verification

**Tests:** 4173 → **4252 assertions**, 167 → **190 cases**, all green. `tests/CurveIntersectTests.cpp`
(23 cases) works from hand-computed coordinates against a REQ-101 margin, not "close enough":

- seg×seg: crossing, would-only-cross-if-extended, parallel, **collinear overlap reports nothing**
  (a shared interval is not a point), endpoint T-junction;
- seg×circle: the y=60 chord of an r=100 circle at exactly ±80 — the case a tessellated circle fails;
  tangent reported **once**, not as a doubled root; clean miss; segment ending inside;
- arcs: sweep filtering, and a **negative** sweep range-tested correctly;
- ellipses: axis-aligned and rotated, both at exact roots;
- circle×circle: two-point, tangent, separate, nested, and **identical circles report nothing**
  (infinitely many intersections is not a snap point);
- refinement: ellipse 100×50 against circle r=70 — four transversal roots at `±√3200, ±√1700`, each
  verified to lie on both curves. Deliberately *not* the tangent case, which a broken refinement
  could pass by finding nothing;
- projection: plan basis is the identity; elevation reaches the projection only through the basis;
  a circle projects to a conic squashed by exactly `sin(30°)` from a 30°-elevation view.

**In the running app**, scripted, with every other snap type disabled so the glyph on screen can
only be an intersection one — both test lines have their midpoint at the crossing, and leaving
midpoint enabled masked the result on the first attempt:

| Case | Result |
|---|---|
| Two lines crossing at (100,100), both at ELEV 0 | **X glyph** at the crossing — INT fires |
| Same lines, second at ELEV 60, APPINT off | **no glyph** — they cross in plan but not in space |
| Same, APPINT on | **diamond+X** at the plan crossing |
| Same, then orbit until the projections separate | **no glyph** — the snap follows the view |

That is every acceptance condition of REQ-062 except the exactness ones, which the unit tests carry.

## 8. Outcome

- REQ-062 delivered in full.
- **REQ-058 now has one condition left**: the REQ-100 frame budget, still unmeasured with no bench
  scene. Every correctness condition is met — plan-view parity, snaps from an orbited camera,
  intersection snaps, per-entity elevation (TASK-036), screen-facing glyphs (TASK-037).
- Defaults: Intersection **on**, Apparent intersection **off** — as in AutoCAD, since APPINT fires
  on objects that do not touch, which is surprising unless asked for. Both persist in user prefs
  and `.gs`.

---

COMPLETION REPORT — TASK-038 — 2026-08-12
- Requirements satisfied:  REQ-062 (Acceptance met: yes). Closes REQ-058's intersection-snap
                           condition; REQ-058 itself still open on REQ-100 only.
- Summary:                 Intersection and apparent-intersection object snaps, on a new pure
                           `util/curveintersect` module that is analytic wherever a closed form
                           exists and Newton-refined where it does not.
- Tests:                   tests/CurveIntersectTests.cpp, 23 cases against hand-computed
                           coordinates at REQ-101. 4252 assertions / 190 cases, green.
- Verification verdict:    PASS
- Assumptions:             ASSUMPTION-1 documented and covered by test.
- Architectural decisions: none made by Workshop. The one spec change (REQ-062) was filed and
                           recorded in the decision log BEFORE any code, per CLAUDE.md.
- Dependencies:            none added
- Technical debt noted:    none new. The snap-picker menu's neighbourhood limit for these two kinds
                           is a recorded trade, not debt — the alternative is unbounded work.
- Build:                   reproducible, clean on target platform
- Docs updated:            spec/requirements.md (REQ-062 + traceability row), spec/project.md
                           (decision log), this task log.
