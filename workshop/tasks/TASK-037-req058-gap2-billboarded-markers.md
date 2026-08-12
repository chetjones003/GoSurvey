# TASK-037 — REQ-058 GAP-2: screen-facing snap glyphs and survey markers

- Type:    feature (completes a REQ-058 acceptance condition)
- Status:  done
- Opened:  2026-08-12
- Owner:   Workshop
- Parent:  TASK-035 §10b (GAP-2)

## 1. Authority

- Requirements: **REQ-058** (accepted), the acceptance condition added 2026-08-11:
  > "**snap glyphs face the viewer** at any orientation rather than lying in the work plane, where
  > they foreshorten to an unreadable edge near a horizontal view. They are UI markers, not geometry."
- Architecture: **ADR-025** (c) Camera value type. No new decision — this reads an existing basis.
- Owning subsystem: Renderer (glyph construction), survey (marker construction).

## 2. Scope

- **In scope:** snap glyphs (all six drawn kinds) and survey-point cross markers built in the
  camera's right/up basis instead of world XY.
- **Out of scope:** grips — they are ImGui-drawn as screen-space rectangles, so their SHAPE was
  never affected. Their POSITION was, and that was D4 in TASK-036.

## 3. Architectural boundary check (workshop/workflow.md §4)

- [x] **No** — proceed, with one placement decision worth recording. The renderer already owns a
      `Camera`, so the snap glyph reads `cam` directly. **Survey does not, and must not:** survey is
      a domain module and the camera lives in the renderer, so taking a `Camera` parameter there
      would be an upward dependency and a §11 violation. `AppendAllSurveyPointMarkers` therefore
      takes a plain `MarkerBillboardBasis` (six floats), and `main.cpp` — which already has both —
      converts. No new abstraction: `Camera::RightWorld` / `UpWorld` are two-line accessors that
      read `ViewRotation`, exactly as the existing `ForwardWorld` does.

## 4. Questions

None.

## 5. Assumptions

```
ASSUMPTION-1: The default basis (world +X / +Y) must reproduce plan view exactly.
- Because:       in plan view the camera's right/up ARE world +X/+Y, so every glyph is built from
                 the same offsets it used before. This is what makes the change safe for the
                 "plan view renders pixel-comparable" acceptance condition.
- Risk if wrong: every marker in the app shifts.
- Validate by:   asserted, not assumed — "In plan view the billboard basis is world +X / +Y".
                 It is also the default of MarkerBillboardBasis, so an un-plumbed caller is correct.
```

## 6. What changed

- `Camera::RightWorld()` / `UpWorld()` — rows 0 and 1 of `ViewRotation`. Derived from the matrix,
  never re-derived from the angles: that is the precedent `ForwardWorld` set after the two silently
  disagreed on the sign of the azimuth term and made picking miss what was drawn.
- `SnapGlyphFrame` — a centre plus the two axes, with one `seg(u, v → u, v)` helper. Each glyph is
  now written as plane offsets, so square / triangle / circle / diagonal-cross / cross-in-square all
  billboard from one place instead of each doing its own world-XY arithmetic.
- Circles in glyphs use a new `AppendSnapCircle` in the glyph plane, so a CENTRE snap stays a circle
  instead of projecting to an ellipse. `AppendCircleLineApproxViewRel` was its only caller and is
  deleted — the flat-circle helper had no remaining use.
- `AppendSurveyPointCrossVertices` takes a `MarkerBillboardBasis`, defaulted to world +X/+Y.

## 7. Verification

**Tests:** 3962 → **4173 assertions**, 164 → **167 cases**, green. Three new cases in
`tests/CameraTests.cpp`:
- plan-view parity of the basis (ASSUMPTION-1);
- right / up / forward stay **orthonormal** across 25 azimuth × elevation combinations — a skewed
  or non-unit basis would shear the glyph or change its size as the view turned;
- **the property that actually matters**: stepping from an *elevated* centre along `RightWorld`
  moves purely +x on screen and along `UpWorld` purely −y, by the *same* screen distance. If that
  holds, a square built from those offsets projects to a square at any orientation — which is all
  of GAP-2. Checked across 16 orientations.

**In the running app.** Same scripted drive as TASK-036, orbited to a near-horizontal FRONT/BOTTOM
view (where the work plane is seen nearly edge-on), LINE active, cursor parked on a rectangle
corner so the endpoint glyph shows. Pre-fix build produced by pinning the basis back to world
+X/+Y — one line.

- **Before:** the green endpoint glyph is a flattened horizontal sliver, roughly 4:1 squashed —
  exactly the "unreadable edge" the acceptance condition describes.
- **After:** a clean square.

## 8. Outcome

- **GAP-2 is closed.** With TASK-036 closing GAP-1 and GAP-3, the three gaps TASK-035 left are done.
- **REQ-058 still cannot be signed off.** Two conditions remain:
  - **intersection snaps** — the snap does not exist. Decided 2026-08-12 (TASK-036 Q1): build it.
    That is the next task.
  - **the REQ-100 frame budget** — still unmeasured, no bench scene.

---

COMPLETION REPORT — TASK-037 — 2026-08-12
- Requirements satisfied:  REQ-058 (Acceptance met: the "snap glyphs face the viewer" condition,
                           in full. **Not a sign-off** — intersection snaps and REQ-100 remain.)
- Summary:                 Snap glyphs and survey-point crosses are built in the camera's right/up
                           basis, so they stay readable at any orientation instead of foreshortening
                           into the work plane.
- Tests:                   3 cases in tests/CameraTests.cpp (plan-view parity, orthonormality across
                           25 orientations, screen-axis correspondence across 16). 4173 assertions /
                           167 cases, green.
- Verification verdict:    PASS
- Assumptions:             ASSUMPTION-1 validated by test, not left open.
- Architectural decisions: none made by Workshop. One boundary honoured explicitly: survey takes a
                           plain basis struct rather than a Camera, to keep the domain→renderer
                           dependency from inverting (architecture §11).
- Dependencies:            none added
- Technical debt noted:    none new. AppendCircleLineApproxViewRel deleted as dead.
- Build:                   reproducible, clean on target platform
- Docs updated:            this task log.
