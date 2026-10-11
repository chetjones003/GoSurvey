# TASK-170 — A box selection in an orbited view ignored the work plane's elevation

## Requirement authority

- **REQ-058** — the 3D viewport: off plan view, picking and box-selection become a camera
  *projection* rather than a plan XY test. This is a defect in that projection.
- **REQ-154** — the active UCS is the work plane coordinate entry resolves against.
- **REQ-121** — the object-selection step whose fence this is.
- No requirement changes. This is a bug fix, not new behaviour.

## The report

> "when making a ucs on a z axis it seems that the pick box is not on where i am selecting … my
> pick point is not on my mouse"

With a screenshot: an orbited view, a UCS whose Z is off world +Z, the crosshair near the top of the
viewport and the blue selection rectangle a couple of hundred pixels below it. The rectangle's X was
about right and its Y was far out.

## Root cause

`ComputeSelectionFromRect` and the rectangle's own draw code each projected the two drag corners at
**Z = 0**:

```cpp
SP(xa, ya, 0.f, &xa, &ya);   // ComputeSelectionFromRect
SP(xb, yb, 0.f, &xb, &yb);
selCam.WorldToScreen(cmd.selBoxAnchorX, cmd.selBoxAnchorY, 0.0, ...);   // the drawn rect
```

The drag does not happen at Z = 0. It happens on the **work plane**, whose elevation is whatever
`ELEV` or the active UCS put it at — the viewport already computes that elevation per frame
(`uiCursorWorldZ`) precisely because a tilted plane's Z varies across it.

Two reasons it survived this long, and both are the same reason:

- **In plan view, Z does not move a projection at all.** The default view is plan, so the whole
  existing transcript corpus is blind to it.
- **On the world XY plane at elevation zero, Z genuinely is 0.** Every drawing that never touches
  `ELEV` or `UCS` is correct by accident.

The visible consequence is worse than a cosmetic offset, because **lines project at their true Z** —
`ComputeSelectionFromRect`'s own note says "lines are projected endpoint-wise and stay exact". So the
geometry went to the right pixels and the fence went to the wrong ones: the box drew away from the
cursor *and* selected the wrong things, or nothing.

## The fix

Carry each drag corner's elevation and project it there.

- `AppCommandState::selBoxAnchorZ` — the first corner's work-plane Z, set inside
  `BeginSelectionBoxCorner` from the cursor's already-published `uiCursorWorldZ` rather than threaded
  through all five call sites. That is the seam `resolvedPointZ` already uses, and for the same
  reason: a parameter added to five signatures is one that a sixth call site eventually forgets.
- `ComputeSelectionFromRect` takes `za` / `zb` and projects the corners at them.
- The drawn rectangle uses the same two values, so the box still shows exactly what it will select —
  the property that block was written to guarantee and that `Z = 0` was quietly breaking.

`SPBox`'s Z = 0 for an *entity's bounding box* is left alone: that is the pre-existing, documented
conservative approximation for bounds ("the bound of the projection, not the projection of the
bound"), and it is a different question from where the fence is.

## Test approach

`headless.req058-orbited-fence-elevation` — **the first transcript to orbit the view.**

That is the finding behind the finding. Off plan view, picking, snapping and box-selection all leave
the plan XY path and become a camera projection, and until now **no transcript could reach any of
it**, because the only routes to an orbited camera are the ViewCube and a mouse drag. A whole class
of behaviour had no failing test available to it, which is how a Z = 0 projection lived in the
selection path.

New driver verb `VIEWANGLES <azimuth> <elevation>`, on the same footing as `CLAYER` and `LAYERSTATE`:
the product's only route is the GUI, so the driver needs one to state the rule at all.

The transcript covers plan view (the control), a work plane raised by `ELEV` at two different
orbit orientations, and a **tilted** plane from `UCS ZA` — the reported case. Every hit is paired
with a deliberate **miss**, because a stale selection would let a broken fence pass otherwise.

`BOX` in the driver now sets both corners' elevations from the active work plane the way the
viewport's own plan-view branch does. Leaving them at zero would have built the bug into the test.

## Verification

- `ctest`: **967/967 green.**
- **Negative-tested:** restoring `SP(xa, ya, 0.f, …)` turns the orbited case red with
  `SELECTED: expected 1, got 0` — the fence projected onto pixels the line is not at, which is the
  reported symptom stated as an assertion.
- `GoSurvey.exe` itself could not be relinked in this session: the reporter's copy was running and
  holding the file. Every test target built and ran; the application needs one clean build.

## Assumptions

- **ASSUMPTION-1 (validated):** the anchor's elevation is the cursor's published work-plane Z at the
  moment the fence is armed. Validated by the tilted-UCS case, where that Z varies across the plane
  and a constant would not do.

## Technical debt

- **DEBT-1** — the crosshair itself was measured and is *correct*: a probe over five work-plane
  orientations and five cursor positions found the ray → work plane → `WorldToScreen` round trip
  exact to under a millionth of a pixel, including with `uiCursorWorldZ` narrowed to `float`. The
  screenshot's apparent crosshair offset is the *rectangle* being in the wrong place, not the
  crosshair. Recorded because it was the first hypothesis and it is worth knowing it was excluded by
  measurement rather than by argument.
- **DEBT-2** — `SPBox` still projects entity *bounds* at Z = 0. Pre-existing and documented; it makes
  crossing-mode occasionally generous for non-line entities at a large elevation. Its own fix.

## Status

Complete and verified. Goes to review, not done.
