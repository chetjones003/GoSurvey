# TASK-212 — Issue #372: CENTER osnap and snap marker under an orbited 3D view

- Type:    bug fix
- Status:  done
- Opened:  2026-09-06
- Owner:   Workshop
- GitHub:  #372

## 1. Authority

- Requirements: **REQ-058** (accepted, signed off 2026-08-12) — acceptance conditions:
  > "endpoint / midpoint / **center** / intersection snaps resolve correctly from an orbited
  > camera, verified against hand-computed coordinates within REQ-101"

  > "**snap glyphs face the viewer** … They are UI markers, not geometry."
- REQ-154 — "Object snaps continue to resolve against real WCS geometry and return the snapped
  point's own position."
- Project invariant (`project_3d_preview_commit_point`) — the snap marker and the point a click
  commits must be the same point.

No SPEC GAP: the behaviour is already required by an accepted requirement. This is a defect.

## 2. Problem

In any orbited model view (camera not looking straight down world Z):

1. **CENTER never acquires over a circle.** Plan view offers a circle / ellipse / closed-polyline /
   survey-point CENTRE whenever the cursor is over the *shape* (`CircleCenterPickDistSq` and its
   siblings return 0 for a cursor inside the footprint). The orbited path (`ConsiderSnap`, the
   `acc->ray` branch) re-measured every candidate as the distance from the cursor ray to the
   *exact* snap point and used that as the acceptance test — but the centre point is a whole radius
   from where the cursor sits on the rim, so it is always outside the aperture and CENTRE can never
   be reached.

2. **The snap marker sits off the point.** `BuildSnapOverlayLines` drew the glyph at
   `snap.z + 0.045f`. The snap overlay is drawn with the depth test **off** (`depthForOverlay`), so
   the lift buys nothing — but under an orbited camera a world-Z offset projects to a visible
   on-screen gap between the marker and both the geometry and the point a click commits.

## 3. Fix (smallest correct change)

- `CadSnap::ConsiderSnap` — new `bool heuristicAccept = false`. When a pick ray is present it still
  **ranks** by the true ray distance (issue #103's rule, unchanged). For an ordinary kind the ray
  distance is the acceptance test too (the caller's plan-XY `pickDistSq` is meaningless once the
  view tilts). A `heuristicAccept` kind accepts on the **smaller of** the caller's (plane-recomputed)
  heuristic and the true ray distance — the shape heuristic covers "cursor anywhere over the shape",
  the ray distance keeps the pre-#372 envelope for a shape smaller than the aperture. A phantom
  needs both to be large, so the `min` never revives one.
- `RayXyAtPlaneZ` / `CenterHeuristicPoint` (new file-local helpers) — the CENTRE call sites now
  evaluate their plan-XY heuristic (`CircleCenterPickDistSq` etc.) **at the cursor ray's crossing
  of the shape's own plane** (`z = the shape's elevation`) rather than at `wx,wy`, which is the
  ray's crossing of the *work* plane — a different plane. This is the correct generalisation of the
  plan-view test: every plan heuristic already treats a curve as living in the plane of its own
  elevation, and this feeds it the cursor in that same plane. When a ray is parallel to that plane
  (an edge-on FRONT / LEFT / RIGHT / BACK view of a plan drawing) there is no crossing, so
  `heuristicAccept` is passed `false` and CENTRE falls back to plain ray-distance acceptance — it
  still resolves when the ray points almost exactly at the point, as before #372, just not from
  "anywhere over the shape".
  Applied at: native circle, native ellipse, closed-polyline geometric centre, **survey point**
  (its X marker is a fixed *plotted* size, so at a zoomed-out scale it is many apertures wide — the
  same heuristic, the same breakage), and PDF-underlay circle.
- An earlier draft accepted on "ray passes within `radius + aperture` of the centre *point*". Two
  review rounds rejected it: (a) it still fired a phantom CENTRE when a shallow orbit put the ray
  over a large shape without pointing at it; (b) it needed a per-shape extent. The plane-crossing
  test has neither problem.
- `ViewportRenderer::BuildSnapOverlayLines` — `f.cz = snap.z` (no lift). The snap overlay is drawn
  depth-test-off and after the geometry passes, so draw order already keeps it on top; the lift
  only ever projected to an on-screen gap under orbit. Same reasoning as the hover-circle overlay
  (`ViewportRenderer.cpp` ~L2029), which dropped its own literal Z bias for this reason.

Not changed: a **placed block instance's** circle CENTRE (`CadBlockCollectWorldCenters` /
`Consider` at `CadSnap.cpp` ~L1053) still resolves only when the ray passes near the centre point
itself under orbit — `CadBlockWorldPoint` carries no radius/extent, and adding a
non-uniform-scale-corrected one is a separate change. Pre-existing, not named in #372 (repro is a
native CIRCLE), now called out in a code comment.

## 4. Tests

`tests/CadSnapTests.cpp`, tag `[issue372]` (7 cases):

- orbited circle CENTRE acquired with the cursor over the rim (was: no snap);
- orbited: a genuinely closer line endpoint still out-ranks the CENTRE heuristic (ranking rule
  preserved);
- orbited: **no** phantom CENTRE when a shallow orbit puts the ray over a large circle's footprint
  without pointing at the disc (review regression 1);
- orbited **edge-on**: CENTRE falls back to ray-proximity — resolves on-axis, not from over the rim
  (review regression 2);
- orbited **grazing**: a small (sub-aperture) circle's CENTRE still resolves when the ray points
  nearly at it though its plane-crossing is far off the disc (review regression 3);
- orbited ellipse CENTRE acquired with the cursor over the body;
- orbited survey-point CENTRE acquired from over its X marker at a zoomed-out scale.

The marker-position fix is verified by inspection — `BuildSnapOverlayLines` is file-local and the
change removes a provably-unused constant.

## 5. Verification

- `./dev/build` — clean (release, MSVC).
- `./dev/test` — 1244/1244 pass.
- architecture-review, code-review — see completion notes.

## 6. Acceptance criteria (REQ-058)

- [x] CENTER resolves from an orbited camera — new `[issue372]` tests + existing plan-view CENTER
      tests still green.
- [x] snap glyph is drawn at the snapped point (no view-dependent offset).
- [x] existing test suite stays green.
