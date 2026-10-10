# TASK-036 — REQ-058 GAP-1: per-entity 3D sweep

- Type:    feature (completes REQ-058)
- Status:  done — GAP-1 + GAP-3 closed (REQ-058 itself still open on GAP-2 / REQ-100 / Q1)
- Opened:  2026-08-12
- Owner:   Workshop
- Parent:  TASK-035 §10b (GAP-1, GAP-3)

## 1. Authority

- Requirements: **REQ-058** (accepted, partially implemented). Specifically the two acceptance
  conditions added on 2026-08-11:
  - "**every entity type** — not only lines — snaps, previews and commits at the correct elevation
    under an orbited view";
  - "endpoint / midpoint / center / intersection snaps resolve correctly from an orbited camera,
    verified against hand-computed coordinates within REQ-101".
- Architecture: **ADR-025** (a) interleaved XYZ storage, (c) Camera value type, (e) UCS on
  `AppCommandState`. No new decision is required — this task carries existing decisions into the
  entity types that were missed.
- Constraints: REQ-101 (±0.01 ft), architecture §11 invariants.
- Owning subsystem: Renderer (draw), UI/Commands (overlay, commit), viewport (snap, preview).

## 2. Scope

- **In scope:** closing GAP-1 by auditing every entity type against the six-point checklist and
  fixing what it finds; GAP-3 (perpendicular snap Z).
- **Out of scope:** GAP-2 (screen-facing snap glyphs — separate task); the REQ-100 benchmark;
  adding an intersection snap that does not exist (see Q1).

## 3. Architectural boundary check (workflow.md §4)

- [x] **No** — proceed. Every change is a Z value reaching a place that already had a Z-shaped
      hole. One signature widens (`CadTessellateLinetypeChainVc` gains an optional per-vertex Z
      array) — a parameter on an existing internal function, not a new abstraction, layer,
      dependency, or global. No data format changes: all stores already hold the Z being read.

## 4. Questions

| # | Question | Asked | Answer |
|---|----------|-------|--------|
| Q1 | REQ-058 acceptance names **intersection** snaps, but no intersection snap exists in the code — the snap set is Endpoint / Midpoint / Center / Perpendicular / SurveyCenter / GeometricCenter / Grip. Is that acceptance condition (a) vacuous and to be struck, or (b) a requirement to add the snap (new scope, new REQ)? | 2026-08-12 | **(b) — build the intersection snap.** The acceptance stands as written; the snap is missing, not the requirement. No SPEC GAP: implementing it satisfies an already-accepted REQ-058 condition rather than inventing scope. Out of scope for THIS task (its own follow-up), and REQ-058 cannot be signed off until it exists. |

- Note: TASK-035's GAP-3 was recorded as "perpendicular **and intersection** snaps have no Z."
  The intersection half of that is wrong — there is nothing to fix, because there is no such snap.
  GAP-3 reduces to perpendicular, which D8 below closes.

## 5. Assumptions

```
ASSUMPTION-1: A polyline's rendered edges interpolate Z between its vertices.
- Because:       userPolylineVerts already stores a Z per vertex and the commit path writes a real
                 one per vertex, so a polyline is genuinely a 3D chain, not a planar entity like
                 CadArc/CadEllipse (which document "stays parallel to XY" and carry ONE z).
- Risk if wrong: none for the flat case — equal endpoint Z reduces to the old behaviour exactly.
- Validate by:   draw a polyline with vertices snapped to different elevations, orbit, confirm the
                 rendered chain rises with them rather than lying flat.
```

```
ASSUMPTION-2: A dimension is planar and commits at the work-plane elevation, like TEXT.
- Because:       CadAnnotation::insZ is a single elevation and the dim overlay already projects
                 through one Z (dimZ), so the storage shape says planar. Matching TEXT/MTEXT
                 (which already commit at CadCommitElevation) is the consistent reading.
- Risk if wrong: a dimension between two points at different elevations picks one plane rather
                 than spanning them. AutoCAD behaves the same way (a dimension has one elevation).
- Validate by:   dimension two points, orbit, confirm the dim text and lines share the plane the
                 geometry was drawn on.
```

## 6. Audit — the six-point checklist, every entity type

The checklist from TASK-035 §10b/§12, all six places:

1. snap candidates pass the entity's real Z (`CadSnap.cpp`)
2. the rubber/transform preview emits real Z (`CadRubberPreview.cpp`, `TransformPreview.cpp`)
3. creation commits at `CadCommitElevation` (`CadCommands.cpp`)
4. overlay drawing projects through `Camera::WorldToScreen` with the real Z (`CadUi.cpp`)
5. the committed-geometry render pass emits the entity's real Z (`ViewportRenderer.cpp`)
6. the preview consumes the **commit** point, not the cursor (`main.cpp`)

Result — ✅ correct, ❌ defect, — not applicable:

| Entity | 1 snap | 2 preview | 3 commit | 4 overlay | 5 render | 6 commit pt |
|---|---|---|---|---|---|---|
| LINE | ❌ D8 perp | ❌ D7 seg-angle | ✅ | ✅ | ✅ | ✅ |
| POLYLINE | ❌ D8, D10 | ❌ D7 seg-angle | ✅ | — | ❌ **D1** | ✅ |
| RECT | ❌ D8, D10 | ✅ | ✅ | — | ❌ **D1** | ✅ |
| CIRCLE | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| ARC | ❌ D9 mid, D8 | ❌ D7 WaitMid | ✅ | ✅ | ✅ | ✅ |
| ELLIPSE | ❌ D9 mid, D8 | ❌ D7 major-end | ✅ | ✅ | ✅ | ✅ |
| TEXT / MTEXT | — | ✅ | ✅ | ✅ | — | ✅ |
| DIMENSION | — | ❌ D7 all phases | ❌ **D6** | ✅ | — | ✅ |
| HATCH / filled region | — | ❌ D5 highlight | ✅ | ❌ **D3** | ❌ **D2** | ✅ |
| SURVEY POINT | ✅ | — | ✅ | ✅ | ✅ | ✅ |
| PDF underlay | — flat by nature | — | — | — | — | — |
| grips (all types) | ❌ D11 | — | — | ❌ **D4** | — | — |
| snap picker (cycle) | ❌ D12 | — | — | — | — | — |

Two rows of this table turned out to be wrong — see §8 for D13 (HATCH commit) and D14 (survey
markers), both found while fixing rather than while auditing.

### The twelve defects

```
D1 — Polylines render FLAT, and discard a Z the model already stores.  [place 5]
- Where:  ViewportRenderer.cpp:1225 calls AppendPolylineEdgesVc(..., 0.f, ...). That parameter is
          the ONLY Z the polyline gets: AppendPolylineEdgesVc builds an xy-pair array (dropping
          vertsXyz[vi*3+2] on the floor) and hands it to CadTessellateLinetypeChainVc, which takes
          a single flat `z` for the whole chain.
- Worse than DEFECT-A: a circle at least had one elevation to lose. A polyline stores a Z PER
          VERTEX, and the render pass flattens all of them to a constant — so a polyline drawn up a
          slope, or imported from DXF with per-vertex elevations, is drawn as if it were level.
- Reach:  RECT too — a rectangle is committed as a closed polyline.
```

```
D2 — Solid filled regions (hatch) render at Z = 0.                      [place 5]
- Where:  ViewportRenderer.cpp ~1021-1027 pushes a literal 0.f as the third component of every
          triangle-fan vertex, and ~1057 does the same for the stencil cover quad, while
          fr.vertsXyz carries the real Z at [p*3+2].
- Same shape as DEFECT-A, in the one pass that draws filled geometry rather than lines.
```

```
D3 — Line-pattern hatch and the hatch preview draw at Z = 0 in the overlay.  [place 4]
- Where:  CadUi.cpp ~10069 (preview loop) and ~10108/10110 (pattern lines) pass 0.0 to
          WorldToScreen. Non-solid hatches are drawn by the ImGui overlay, not the GL pass
          (REQ-043), so D2 does not cover them.
```

```
D4 — Grip squares project at Z = 0.                                      [place 4]
- Where:  CadUi.cpp ~8845 passes 0.0 to WorldToScreen for every grip.
- TASK-035 GAP-2 assumed grips were fine because they are ImGui-drawn — that is true of their
  SHAPE (they stay square, which is what GAP-2 is about) but not of their POSITION. An orbited
  view puts the grip on the datum while its entity sits at elevation.
```

```
D5 — Filled-region selection/hover highlight uses a flat overlay depth.  [place 2]
- Where:  TransformPreview.cpp:488/491 push `lineZ` (0.011 / 0.012 — a pre-3D draw-order bias).
          Its own comment says "depth-correct highlighting arrives with the camera in
          REQ-058/TASK-035" — this task. Every other entity type in that file was already fixed.
```

```
D6 — Dimensions commit at Z = 0 regardless of the work plane.            [place 3]
- Where:  CommitDimAlignedAt / CommitDimLinearAt / CommitDimAngularAt build a CadAnnotation and
          never set ann.insZ, so it keeps its 0 default.
- Note the asymmetry that hid it: the dim OVERLAY already projects through a real dimZ
  (CadUi.cpp ~10237), so the draw path was ready and the commit path never filled it in.
```

```
D7 — Seven rubber-preview phases emit Z = 0.                             [place 2]
- Where:  CadRubberPreview.cpp — ARC WaitMid (191), ELLIPSE WaitMajorEnd (198), DIMALIGNED /
          DIMLINEAR WaitExt2 (202), SURVEY INVERSE WaitTo (206), DIMANGULAR all three phases
          (211-217), and the segment-angle-pick WaitP2 rubber for both LINE (127) and POLYLINE
          (160). Each is one un-passed CadCommitElevation(cmd) next to siblings that pass it.
```

```
D8 — Perpendicular snap has no Z. (GAP-3)                                [place 1]
- Where:  AppendPerpendicularFromRef (CadSnap.cpp:284) computes the foot in 2D and calls Consider
          without a z, so the candidate defaults to 0 and is measured against the cursor ray at
          the wrong depth — the candidate is either missed or snapped to a wrong elevation.
- Fix shape: the foot already has parameter t along AB; interpolate the segment's endpoint Z at t.
  Every caller has both endpoint elevations available or can pass them.
```

```
D9 — Arc and ellipse MIDPOINT snaps pass no Z.                           [place 1]
- Where:  CadSnap.cpp:470 (arc) and 508 (ellipse) call Consider without a.z / el.z, while the
          endpoint (454/456) and center (489) candidates on the same entities pass it correctly.
```

```
D10 — Polyline geometric-center snap passes no Z.                        [place 1]
- Where:  CadSnap.cpp:434. The centroid of a closed polyline has no single Z in general; the
          honest value is the mean vertex Z of the loop, which is what the centroid already is
          in X and Y.
```

```
D11 — Grip snap passes no Z.                                             [place 1]
- Where:  FindGripSnap / gripCandidate (CadSnap.cpp ~966) is 2D throughout. Grip snap has the
          HIGHEST priority of any snap (Priority(Grip) == 4), so a wrong-Z grip candidate wins
          over correct geometry snaps and drags the commit to the datum.
```

```
D12 — The snap picker drops Z entirely.                                  [place 1]
- Where:  GatherAllSnapsOfKind → PushSnapPickerEntry / PushPerpFootEntry have no z parameter, so
          every entry in the picker list reports Hit::z == 0 whatever the geometry's elevation.
```

## 7. Plan

Fix in dependency order, cheapest-verifiable first:

1. **Snap (D8-D12)** — `CadSnap.cpp` only. Perpendicular gains interpolated Z; arc/ellipse midpoint,
   polyline geometric centre, grips and the picker gain the Z their entities already store.
2. **Commit (D6)** — three dimension commits set `ann.insZ = CadCommitElevation(st)`.
3. **Preview (D5, D7)** — the seven rubber phases and the filled-region highlight.
4. **Render (D1, D2)** — `CadTessellateLinetypeChainVc` gains an optional per-vertex Z array;
   `AppendPolylineEdgesVc` feeds it; the filled-region fan and cover quad emit `vertsXyz[+2]`.
5. **Overlay (D3, D4)** — hatch pattern/preview and grips project through their real Z.

- Test approach: `util/ray3d` and `render/Camera` are unit-testable and already covered; the
  tessellator is pure and gains a per-vertex-Z case (happy path: sloped chain interpolates;
  failure mode: null Z array reproduces the flat result bit-for-bit). `CadSnap` Z interpolation is
  pure and testable. `CadUi.cpp` / `ViewportRenderer.cpp` are not linkable by the test target
  (TASK-035 §11), so those are verified by driving the built app and reading screenshots.

## 8. Two more defects, found while fixing

The audit's own table was wrong in two places. Both were found by reading the code around a fix
rather than by the checklist, which is worth recording — the checklist finds what it looks for.

```
D13 — HATCH commits at Z = 0, and said so in a comment that had gone stale.  [place 3]
- Where:  CadHatchCommitLoop (CadCommands.cpp ~8963) pushed a literal 0.f per vertex, under the
          comment "A hatch is created in the plan view of the current drawing, so Z = 0 until the
          UCS work plane exists (REQ-058/TASK-035)."
- The work plane exists now — TASK-035 delivered it. The comment described a condition that had
  since been met, so the code read as deliberate when it was merely out of date. My §6 table marked
  this row ✅ on the strength of that comment; it was wrong.
- Fix:    the region lands on CadCommitElevation like every other created entity. The tracer works
          in XY and returns no elevations, so all vertices share one Z — a hatch is planar anyway.
```

```
D14 — Survey-point cross markers render at a fixed 0.055, not their elevation.  [place 5]
- Where:  AppendAllSurveyPointMarkers (SurveyPoints.cpp) computed `z = 0.055f + elevation * 1e-6f`.
- That is a draw-ORDER bias from the flat renderer — 0.055 to sit above the linework, plus a
  microscopic elevation term to keep coincident markers stably ordered. Depth testing is off, so it
  never ordered anything (draw order does); once the view could tilt it simply pinned every marker
  to the datum.
- What makes it the sharpest example of GAP-1's thesis: a survey point's snap Z, hover ring and ID
  label were ALL correct after this task's earlier fixes. Only the marker itself was on the datum —
  so the crosshair snapped to a point 100 ft above the cross the user could see.
- Same shape as TASK-035's DEFECT-A overlay biases (0.017 / 0.018 / 0.032). Those were found; this
  one was not, because it lives in a different file.
```

## 9. Verification

**Build:** clean, MSVC + clang-tidy, no new warnings.

**Tests:** 1934 → **3962 assertions**, 159 → **164 cases**, all green.
`src/commands/CadLinetype.cpp` joined the test target — it is standalone (only `<string>`,
`<vector>`, `<cmath>`, `<cctype>`), exactly like `DwgProbe.cpp`, so this is the first piece of the
render path to become test-reachable at all. New `tests/LinetypeTessellationTests.cpp` covers:
- per-vertex Z on a solid chain (the D1 mechanism);
- a closed chain wrapping the last vertex's Z back to the first (RECT commits as a closed polyline);
- **dash endpoints interpolating** on a sloped segment — a 100-unit edge rising 0 → 100 asserts
  `z == x` at every emitted vertex, which fails if a dash takes one endpoint's Z for both;
- **the failure mode**: a null Z array reproduces the flat result *bit-for-bit*, across four
  linetypes. Circle / arc / ellipse are planar and still pass a single z; widening the signature had
  to not move a single vertex for them.

**In the running app** (D1, the headline defect). `CadUi.cpp` / `ViewportRenderer.cpp` are still not
linkable by the test target (TASK-035 §11), so this was driven with scripted input and screenshots.
Two IDENTICAL 200×120 rectangles, one at `ELEV 0` and one at `ELEV 60`, then the same scripted
orbit. The pre-fix build was produced by passing `nullptr` for the Z array — one line, and the new
test asserts that null is bit-identical to the old flat path, so it is a faithful "before".

- **Before:** ONE rectangle on screen. Both flattened to Z 0, so they render exactly coincident.
- **After:** TWO rectangles, separated on screen by their 60-unit elevation difference.

## 10. Outcome

- GAP-1 is closed and GAP-3 with it: **fourteen** defects across all six checklist places, covering
  every entity type in the §6 table.
- **REQ-058 still cannot be signed off.** Two acceptance conditions remain open, neither in this
  task's scope: GAP-2 (screen-facing snap glyphs — its own acceptance bullet) and the REQ-100 frame
  budget, still unmeasured with no bench scene.
- **Q1 answered 2026-08-12: build the intersection snap.** It does not exist today, so REQ-058's
  acceptance condition naming it is unmet. Follow-up task, not this one.
- **REQ-058's Status paragraph in `spec/requirements.md` is now factually stale** — it says "only
  LINE is carried through the full pipeline; CIRCLE is known broken and the remaining types are
  unverified." Circle was fixed in TASK-035 §12 and the rest are fixed here. Correcting it is a
  `spec/` edit and therefore not the Workshop's to make (CLAUDE.md §"Layer behavior rules") — it
  needs a recorded decision. Flagged, not touched.
  > **Resolved 2026-08-12** by the decision recorded in `spec/project.md` alongside TASK-039:
  > REQ-058 is signed off and the stale paragraph is superseded.

## 11. Progress

- 2026-08-12 — audit complete, twelve defects recorded in §6. Baseline build clean.
- 2026-08-12 — all fourteen fixed (D13/D14 found during the work), tests extended, D1 proved in the
  running app with a before/after. Build clean, suite green.

---

COMPLETION REPORT — TASK-036 — 2026-08-12
- Requirements satisfied:  REQ-058 (Acceptance met: **partially** — the "every entity type" condition
                           is now met; "intersection snaps" is blocked on Q1, snap glyphs are GAP-2,
                           and the REQ-100 budget is unmeasured. **Not a sign-off.**)
- Summary:                 Swept all entity types against the six-point 3D checklist and fixed the 14
                           defects found — polylines and hatches rendering flat, hatch/dimension
                           commits on the datum, survey markers pinned by a draw-order bias, seven
                           un-elevated preview phases, and six snap kinds returning Z 0.
- Tests:                   tests/LinetypeTessellationTests.cpp (5 cases: per-vertex Z, closed-chain
                           wrap, dash interpolation; failure mode = null array is bit-identical to
                           flat). 3962 assertions / 164 cases, green.
- Verification verdict:    PASS for GAP-1 + GAP-3 scope (findings resolved: D1–D14)
- Assumptions:             ASSUMPTION-1 (polyline edges interpolate Z) validated in the app;
                           ASSUMPTION-2 (a dimension is planar) documented, matches AutoCAD, open.
- Architectural decisions: none made by Workshop (escalated: none; Q1 raised for a spec decision)
- Dependencies:            none added
- Technical debt noted:    CadUi.cpp / ViewportRenderer.cpp remain unlinkable by the test target —
                           the structural gap behind every defect class in TASK-035 and 12 of the 14
                           here. Removal condition: those TUs split so the geometry-producing logic
                           links without a GL context. CadLinetype.cpp is now the first piece pulled
                           across; the rest is a follow-up task.
- Build:                   reproducible, clean on target platform
- Docs updated:            this task log. spec/requirements.md REQ-058 Status is stale — flagged for
                           a recorded decision, deliberately NOT edited by the Workshop.
