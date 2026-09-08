# TASK-213 — Issue #400 (increment 1 of 3): ARRAY resolves picks through the active UCS plane

- Type:    feature (3D integration of an existing 2D command)
- Status:  done — PR #409
- Opened:  2026-09-07
- Owner:   Workshop
- GitHub:  #400

## 1. Authority

- REQ-305 acceptance 10-11 (added 2026-09-07, D-2026-09-07-b).
- Reused helpers established by #399/TRIM and #395/3D Object Snap:
  `CadActiveWorkPlane` / `CadWorkPlaneAnchoredAt` (`CadCommands.hpp`), `ucs::WorldToPlane` /
  `ucs::PlaneToWorld` (`CadUcs.hpp`/`.cpp`), `ray3d::RayPlaneIntersect`.

## 2. Scope (this increment only)

- Rectangular array: columns run along the active UCS X axis, rows along the active UCS Y axis
  (today: world X/Y).
- Polar array: rotation is about the active UCS Z axis through the picked centre (today: world Z).
  Rotate-items carries orientation through the same UCS-Z rotation.
- Center/anchor points typed or clicked are resolved onto the active UCS plane via the camera ray,
  not the raw XY of the mouse.
- Out of scope (later increments): the "levels" 3D-grid parameter (increment 2); Solid/Surface
  duplication (increment 3 — `DuplicateCadSelectionTranslated`/`Rotated` have no such branch today
  for ANY modify command, confirmed by reading `CadCommands.cpp:8074-8395`).

## 3. Files affected

- `src/commands/CadCommands.cpp`: `ResetArrayDraft` (:10382), `CommitArrayRectangular` (:10407),
  `CommitArrayPolar` (:10431), `HandleArrayText` (:10459), `StartArrayCommand` (:29816), the
  viewport-click array handling around :12240-12270.
- `DuplicateCadSelectionTranslated` (:8074) / `DuplicateCadSelectionRotated` (:8534) take world
  dx/dy or a world base point + world-Z angle; both are reused UNCHANGED by converting a
  UCS-local offset/rotation into the equivalent world dx/dy/base-point/angle before calling them —
  no changes needed inside either duplicator for this increment (they already move entities by a
  world delta, and a UCS-plane rotation still nets out to a world dx/dy per instance once computed
  through the plane).

## 4. Approach

1. Build the active UCS-anchored frame at the array's anchor/center point
   (`CadWorkPlaneAnchoredAt(st, anchorX, anchorY, anchorZ)`).
2. Rectangular: each grid cell's local offset `(c*colSpacing, r*rowSpacing, 0)` is converted to a
   world point via `ucs::PlaneToWorld(frame, localOffset)`, then to a world dx/dy/dz relative to
   the anchor's world position, then passed to (an extended, dz-aware) duplication call.
3. Polar: the centre point picked/typed is resolved onto the UCS plane (not raw XY). Each
   instance's position is computed by rotating the anchor's UCS-local offset from centre by the
   sweep angle IN THE UCS PLANE, then mapping back to world — this replaces the world-Z
   `RotateAroundBase` call with a UCS-plane rotation that reduces to it exactly when the UCS is
   World.
4. Regression guard: when the active UCS is World, `CadWorkPlaneAnchoredAt`'s frame is axis-aligned
   with world XY, so the UCS-plane math reduces to the exact same arithmetic as today — verified by
   a test that asserts bit-identical output under the World UCS.

## 5. Tests

- `CadCommandsTests.cpp` (or wherever ARRAY's existing tests live): rectangular + polar arrays
  under (a) World UCS / plan view (regression — must match pre-change output), (b) a UCS rotated
  90 degrees about Z, (c) a UCS on a tilted plane (orbited-equivalent), (d) polar rotate-items
  on/off under a rotated UCS.

## 6. Verification

- `build-project`, `code-review`, `testing`.
